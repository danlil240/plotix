// handlers_fuzz.cpp — Weighted random fuzz actions for MCP-driven QA agents.
// Mirrors the fuzz action table in tests/qa/qa_agent.cpp.

#include "../automation_dispatch.hpp"
#include "../automation_handler.hpp"
#include "../automation_imgui_input.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/input/input.hpp"
#include "ui/native_dialog_policy.hpp"
#include "ui/ui_interaction_log.hpp"
#include "ui/window/window_manager.hpp"

#include "render/vulkan/window_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #include <GLFW/glfw3.h>
#endif

#include <cmath>
#include <cstdlib>
#include <format>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace spectra
{
namespace
{

std::mt19937  g_fuzz_rng{std::random_device{}()};
uint64_t      g_fuzz_step_index = 0;
uint64_t      g_fuzz_base_seed  = 0;
bool          g_fuzz_seed_set   = false;

enum class FuzzAction
{
    ExecuteCommand,
    MouseClick,
    MouseDrag,
    MouseScroll,
    KeyPress,
    CreateFigure,
    CloseFigure,
    SwitchTab,
    AddSeries,
    UpdateData,
    LargeDataset,
    SplitDock,
    WaitFrames,
    WindowResize,
    WindowDrag,
    TabDetach,
};

struct ActionWeight
{
    FuzzAction action;
    int        weight;
};

constexpr ActionWeight kFuzzWeights[] = {
    {FuzzAction::ExecuteCommand, 15},
    {FuzzAction::MouseClick, 15},
    {FuzzAction::MouseDrag, 10},
    {FuzzAction::MouseScroll, 10},
    {FuzzAction::KeyPress, 10},
    {FuzzAction::CreateFigure, 5},
    {FuzzAction::CloseFigure, 3},
    {FuzzAction::SwitchTab, 8},
    {FuzzAction::AddSeries, 8},
    {FuzzAction::UpdateData, 5},
    {FuzzAction::LargeDataset, 1},
    {FuzzAction::SplitDock, 3},
    {FuzzAction::WaitFrames, 7},
    {FuzzAction::WindowResize, 3},
    {FuzzAction::WindowDrag, 3},
    {FuzzAction::TabDetach, 2},
};

const char* fuzz_action_name(FuzzAction action)
{
    switch (action)
    {
        case FuzzAction::ExecuteCommand:
            return "ExecuteCommand";
        case FuzzAction::MouseClick:
            return "MouseClick";
        case FuzzAction::MouseDrag:
            return "MouseDrag";
        case FuzzAction::MouseScroll:
            return "MouseScroll";
        case FuzzAction::KeyPress:
            return "KeyPress";
        case FuzzAction::CreateFigure:
            return "CreateFigure";
        case FuzzAction::CloseFigure:
            return "CloseFigure";
        case FuzzAction::SwitchTab:
            return "SwitchTab";
        case FuzzAction::AddSeries:
            return "AddSeries";
        case FuzzAction::UpdateData:
            return "UpdateData";
        case FuzzAction::LargeDataset:
            return "LargeDataset";
        case FuzzAction::SplitDock:
            return "SplitDock";
        case FuzzAction::WaitFrames:
            return "WaitFrames";
        case FuzzAction::WindowResize:
            return "WindowResize";
        case FuzzAction::WindowDrag:
            return "WindowDrag";
        case FuzzAction::TabDetach:
            return "TabDetach";
    }
    return "Unknown";
}

bool parse_fuzz_action(const std::string& name, FuzzAction& out)
{
    for (const auto& entry : kFuzzWeights)
    {
        if (name == fuzz_action_name(entry.action))
        {
            out = entry.action;
            return true;
        }
    }
    return false;
}

bool is_fuzz_denied_command(const std::string& id)
{
    static constexpr const char* kDenied[] = {
        "figure.close",
        "app.quit",
        "file.save_figure",
        "file.load_figure",
        "file.export_png",
        "file.export_svg",
        "file.copy_to_clipboard",
        "help.show",
        "accessibility.sonify_series",
        "data.export_html_table",
    };
    for (const char* denied : kDenied)
    {
        if (id == denied)
            return true;
    }
    if (native_dialogs_enabled()
        && (id == "file.save_workspace" || id == "file.load_workspace"))
        return true;
    return false;
}

FuzzAction pick_weighted_action(std::mt19937& rng)
{
    const bool skip_drag = std::getenv("SPECTRA_FUZZ_SKIP_DRAG") != nullptr;

    int total = 0;
    for (const auto& w : kFuzzWeights)
    {
        if (skip_drag
            && (w.action == FuzzAction::TabDetach || w.action == FuzzAction::WindowDrag))
            continue;
        total += w.weight;
    }
    if (total <= 0)
        return FuzzAction::WaitFrames;

    std::uniform_int_distribution<int> dist(0, total - 1);
    const int                          roll = dist(rng);
    int                                cumulative = 0;
    for (const auto& w : kFuzzWeights)
    {
        if (skip_drag
            && (w.action == FuzzAction::TabDetach || w.action == FuzzAction::WindowDrag))
            continue;
        cumulative += w.weight;
        if (roll < cumulative)
            return w.action;
    }
    return FuzzAction::WaitFrames;
}

void mark_dirty(App* app, const char* reason)
{
    if (auto* sess = app->session())
        sess->redraw_tracker().mark_dirty(reason);
}

void get_window_dims(App* app, double& width, double& height)
{
    width  = 1280.0;
    height = 720.0;
    if (app && app->backend())
    {
        width  = static_cast<double>(app->backend()->swapchain_width());
        height = static_cast<double>(app->backend()->swapchain_height());
    }
    if (width < 1.0)
        width = 1280.0;
    if (height < 1.0)
        height = 720.0;
}

Figure& create_random_figure(App* app, std::mt19937& rng)
{
    std::uniform_int_distribution<int> size_dist(640, 1600);
    Figure&                            fig =
        app->figure({static_cast<uint32_t>(size_dist(rng)), static_cast<uint32_t>(size_dist(rng))});
    auto& ax = fig.subplot(1, 1, 1);

    std::uniform_int_distribution<int>    n_dist(20, 200);
    const int                             n = n_dist(rng);
    std::vector<float>                    x(static_cast<size_t>(n));
    std::vector<float>                    y(static_cast<size_t>(n));
    std::uniform_real_distribution<float> val(-10.0f, 10.0f);
    for (int i = 0; i < n; ++i)
    {
        x[static_cast<size_t>(i)] = static_cast<float>(i);
        y[static_cast<size_t>(i)] = val(rng);
    }
    ax.line(x, y);
    return fig;
}

std::string run_fuzz_action(FuzzAction action,
                            App*        app,
                            WindowUIContext* ui_ctx,
                            std::mt19937&    rng,
                            int&             pump_frames)
{
    pump_frames = 1;
    std::ostringstream details;
    details << "{";

    switch (action)
    {
        case FuzzAction::ExecuteCommand:
        {
#ifdef SPECTRA_USE_IMGUI
            if (!ui_ctx)
                break;
            auto                     cmd_ptrs = ui_ctx->cmd_registry.all_commands();
            std::vector<std::string> cmds;
            for (auto* c : cmd_ptrs)
                if (c)
                    cmds.push_back(c->id);
            if (cmds.empty())
                break;
            std::uniform_int_distribution<size_t> dist(0, cmds.size() - 1);
            const std::string& id = cmds[dist(rng)];
            if (!is_fuzz_denied_command(id))
            {
                ui_ctx->cmd_registry.execute(id);
                mark_dirty(app, "fuzz_execute_command");
                details << R"("command_id":")" << json_escape(id) << '"';
            }
            else
            {
                ui::log_ui_action("command", id, "skipped", "fuzz_denied");
                details << R"("skipped_command":")" << json_escape(id) << '"';
            }
#else
            (void)ui_ctx;
#endif
            break;
        }

        case FuzzAction::MouseClick:
        {
            if (!ui_ctx)
                break;
            double w = 1280.0;
            double h = 720.0;
            get_window_dims(app, w, h);
            std::uniform_real_distribution<double> px(0, w);
            std::uniform_real_distribution<double> py(0, h);
            std::uniform_int_distribution<int>       btn(0, 1);
            const double                             mx = px(rng);
            const double                             my = py(rng);
            const int                                b  = btn(rng);
#ifdef SPECTRA_USE_IMGUI
            automation::inject_mouse_click(ui_ctx, mx, my, b);
#endif
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            ui_ctx->input_handler.on_mouse_button(b, 1, 0, mx, my);
            ui_ctx->input_handler.on_mouse_button(b, 0, 0, mx, my);
#endif
#ifdef SPECTRA_USE_IMGUI
            ui::log_ui_action("fuzz_click",
                              std::to_string(b),
                              "ok",
                              std::format("x={:.0f} y={:.0f}", mx, my));
#endif
            mark_dirty(app, "fuzz_mouse_click");
            details << "\"x\":" << mx << ",\"y\":" << my << ",\"button\":" << b;
            break;
        }

        case FuzzAction::MouseDrag:
        {
            if (!ui_ctx)
                break;
            double w = 1280.0;
            double h = 720.0;
            get_window_dims(app, w, h);
            std::uniform_real_distribution<double> px(0, w);
            std::uniform_real_distribution<double> py(0, h);
            const double                             x1 = px(rng);
            const double                             y1 = py(rng);
            const double                             x2 = px(rng);
            const double                             y2 = py(rng);
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            ui_ctx->input_handler.on_mouse_button(0, 1, 0, x1, y1);
            for (int s = 1; s <= 5; ++s)
            {
                const double t  = static_cast<double>(s) / 5.0;
                const double cx = x1 + (x2 - x1) * t;
                const double cy = y1 + (y2 - y1) * t;
                ui_ctx->input_handler.on_mouse_move(cx, cy);
            }
            ui_ctx->input_handler.on_mouse_button(0, 0, 0, x2, y2);
#endif
            mark_dirty(app, "fuzz_mouse_drag");
            details << "\"x1\":" << x1 << ",\"y1\":" << y1 << ",\"x2\":" << x2 << ",\"y2\":" << y2;
            break;
        }

        case FuzzAction::MouseScroll:
        {
            if (!ui_ctx)
                break;
            double w = 1280.0;
            double h = 720.0;
            get_window_dims(app, w, h);
            std::uniform_real_distribution<double> px(0, w);
            std::uniform_real_distribution<double> py(0, h);
            std::uniform_real_distribution<double> scroll(-3.0, 3.0);
            const double                             x  = px(rng);
            const double                             y  = py(rng);
            const double                             dy = scroll(rng);
#ifdef SPECTRA_USE_IMGUI
            automation::inject_scroll(ui_ctx, x, y, 0.0, dy);
#endif
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            ui_ctx->input_handler.on_scroll(x, y, 0.0, dy);
#endif
#ifdef SPECTRA_USE_IMGUI
            ui::log_ui_action("fuzz_scroll",
                              std::to_string(static_cast<int>(dy)),
                              "ok",
                              std::format("x={:.0f} y={:.0f}", x, y));
#endif
            mark_dirty(app, "fuzz_mouse_scroll");
            details << "\"x\":" << x << ",\"y\":" << y << ",\"dy\":" << dy;
            break;
        }

        case FuzzAction::KeyPress:
        {
            if (!ui_ctx)
                break;
            std::uniform_int_distribution<int> key(32, 126);
            const int                          k = key(rng);
#ifdef SPECTRA_USE_IMGUI
            automation::inject_key(ui_ctx, k, 0);
#endif
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            ui_ctx->input_handler.on_key(k, 1, 0);
            ui_ctx->input_handler.on_key(k, 0, 0);
#endif
#ifdef SPECTRA_USE_IMGUI
            ui::log_ui_action("fuzz_key", std::to_string(k), "ok");
#endif
            mark_dirty(app, "fuzz_key_press");
            details << "\"key\":" << k;
            break;
        }

        case FuzzAction::CreateFigure:
        {
            const auto ids = app->figure_registry().all_ids();
            if (ids.size() < 20)
            {
                create_random_figure(app, rng);
                mark_dirty(app, "fuzz_create_figure");
                details << "\"created\":true";
            }
            else
            {
                details << "\"skipped\":\"max_figures\"";
            }
            break;
        }

        case FuzzAction::CloseFigure:
        {
#ifdef SPECTRA_USE_IMGUI
            if (!ui_ctx || !ui_ctx->fig_mgr || ui_ctx->fig_mgr->count() <= 1)
                break;
            const auto ids = app->figure_registry().all_ids();
            if (ids.size() > 1)
            {
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                const FigureId                        fid = ids[dist(rng)];
                ui_ctx->fig_mgr->queue_close(fid);
                mark_dirty(app, "fuzz_close_figure");
                details << "\"figure_id\":" << fid;
            }
#else
            (void)ui_ctx;
#endif
            break;
        }

        case FuzzAction::SwitchTab:
        {
#ifdef SPECTRA_USE_IMGUI
            if (!ui_ctx || !ui_ctx->fig_mgr)
                break;
            const auto ids = app->figure_registry().all_ids();
            if (!ids.empty())
            {
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                const FigureId                        fid = ids[dist(rng)];
                ui_ctx->fig_mgr->queue_switch(fid);
                mark_dirty(app, "fuzz_switch_tab");
                details << "\"figure_id\":" << fid;
            }
#else
            (void)ui_ctx;
#endif
            break;
        }

        case FuzzAction::AddSeries:
        {
            const auto ids = app->figure_registry().all_ids();
            if (ids.empty())
                break;
            std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
            Figure*                               fig = app->figure_registry().get(ids[fig_dist(rng)]);
            if (!fig || fig->axes().empty())
                break;

            std::uniform_int_distribution<int>    n_dist(10, 200);
            const int                             n = n_dist(rng);
            std::vector<float>                    x(static_cast<size_t>(n));
            std::vector<float>                    y(static_cast<size_t>(n));
            std::uniform_real_distribution<float> val(-50.0f, 50.0f);
            for (int i = 0; i < n; ++i)
            {
                x[static_cast<size_t>(i)] = static_cast<float>(i);
                y[static_cast<size_t>(i)] = val(rng);
            }

            auto&                              ax = fig->subplot(1, 1, 1);
            std::uniform_int_distribution<int> type_dist(0, 1);
            if (type_dist(rng) == 0)
                ax.line(x, y);
            else
                ax.scatter(x, y);
            mark_dirty(app, "fuzz_add_series");
            details << "\"points\":" << n;
            break;
        }

        case FuzzAction::UpdateData:
        {
            const auto ids = app->figure_registry().all_ids();
            if (ids.empty())
                break;
            std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
            Figure* fig = app->figure_registry().get(ids[fig_dist(rng)]);
            if (!fig || fig->axes().empty())
                break;
            auto& ax = *fig->axes()[0];
            if (ax.series().empty())
                break;
            auto* series = ax.series()[0].get();
            auto* line   = dynamic_cast<LineSeries*>(series);
            if (line)
            {
                const auto                            xd = line->x_data();
                std::vector<float>                    new_y(xd.size());
                std::uniform_real_distribution<float> val(-50.0f, 50.0f);
                for (size_t i = 0; i < new_y.size(); ++i)
                    new_y[i] = val(rng);
                line->set_y(new_y);
                mark_dirty(app, "fuzz_update_data");
                details << "\"points\":" << new_y.size();
            }
            break;
        }

        case FuzzAction::LargeDataset:
        {
            const auto ids = app->figure_registry().all_ids();
            if (ids.empty())
                break;
            std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
            Figure* fig = app->figure_registry().get(ids[fig_dist(rng)]);
            if (!fig)
                break;

            std::uniform_int_distribution<int> n_dist(100000, 500000);
            const int                            n = n_dist(rng);
            std::vector<float>                   x(static_cast<size_t>(n));
            std::vector<float>                   y(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                x[static_cast<size_t>(i)] = static_cast<float>(i);
                y[static_cast<size_t>(i)] = std::sin(static_cast<float>(i) * 0.001f);
            }
            fig->subplot(1, 1, 1).line(x, y);
            mark_dirty(app, "fuzz_large_dataset");
            details << "\"points\":" << n;
            break;
        }

        case FuzzAction::SplitDock:
        {
#ifdef SPECTRA_USE_IMGUI
            if (!ui_ctx)
                break;
            std::uniform_int_distribution<int> dir(0, 1);
            const char*                        cmd = dir(rng) == 0 ? "view.split_right" : "view.split_down";
            ui_ctx->cmd_registry.execute(cmd);
            mark_dirty(app, "fuzz_split_dock");
            details << R"("command_id":")" << cmd << '"';
#else
            (void)ui_ctx;
#endif
            break;
        }

        case FuzzAction::WaitFrames:
        {
            std::uniform_int_distribution<int> wait(1, 10);
            pump_frames = wait(rng);
            details << "\"requested_frames\":" << pump_frames;
            break;
        }

        case FuzzAction::WindowResize:
        {
#ifdef SPECTRA_USE_GLFW
            auto* wm = app->window_manager();
            if (!wm || wm->windows().empty())
                break;
            auto* glfw_win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
            if (!glfw_win || glfwWindowShouldClose(glfw_win))
                break;
            std::uniform_int_distribution<int> dim(200, 1920);
            const int                          nw = dim(rng);
            const int                          nh = dim(rng);
            glfwSetWindowSize(glfw_win, nw, nh);
            mark_dirty(app, "fuzz_window_resize");
            details << "\"width\":" << nw << ",\"height\":" << nh;
#else
            (void)app;
#endif
            break;
        }

        case FuzzAction::WindowDrag:
        {
#ifdef SPECTRA_USE_GLFW
            auto* wm = app->window_manager();
            if (!wm || wm->windows().empty())
                break;
            auto* glfw_win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
            if (!glfw_win || glfwWindowShouldClose(glfw_win))
                break;
            std::uniform_int_distribution<int> pos_x(0, 1600);
            std::uniform_int_distribution<int> pos_y(0, 900);
            const int                          px = pos_x(rng);
            const int                          py = pos_y(rng);
            glfwSetWindowPos(glfw_win, px, py);
            mark_dirty(app, "fuzz_window_drag");
            details << "\"x\":" << px << ",\"y\":" << py;
#else
            (void)app;
#endif
            break;
        }

        case FuzzAction::TabDetach:
        {
#ifdef SPECTRA_USE_GLFW
            auto* wm = app->window_manager();
            if (!wm)
                break;
            const auto ids = app->figure_registry().all_ids();
            if (ids.size() < 2)
            {
                details << R"("skipped":"insufficient_figures","figure_count":)" << ids.size();
                break;
            }
            std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
            const FigureId                        fid = ids[fig_dist(rng)];
            if (wm->window_count() < 5)
            {
                std::uniform_int_distribution<int> pos(50, 800);
                const int                          px = pos(rng);
                const int                          py = pos(rng);
                auto* w = wm->detach_figure(fid, 640, 480, "Fuzz Detach", px, py);
                mark_dirty(app, "fuzz_tab_detach");
                details << "\"figure_id\":" << fid << ",\"detached\":" << (w ? "true" : "false");
            }
            else
            {
                const auto wins = wm->windows();
                if (wins.size() > 1)
                {
                    std::uniform_int_distribution<size_t> win_dist(1, wins.size() - 1);
                    wm->request_close(wins[win_dist(rng)]->id);
                    wm->process_pending_closes();
                    mark_dirty(app, "fuzz_close_extra_window");
                    details << "\"closed_window\":true";
                }
            }
#else
            (void)app;
#endif
            break;
        }
    }

    details << "}";
#ifdef SPECTRA_USE_IMGUI
    ui::log_ui_action("fuzz", fuzz_action_name(action), "ok");
    automation::dismiss_ui_capture(ui_ctx);
#endif
    return details.str();
}

}   // namespace

std::vector<AutomationHandlerEntry> make_fuzz_handlers()
{
    using Ctx = AutomationContextFlag;
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "fuzz_step",
        "Execute one weighted-random QA fuzz action (same distribution as spectra_qa_agent). "
        "Optional seed resets RNG; optional action forces a specific action name.",
        Ctx::UiContext,
        {{.name = "seed", .kind = ParamKind::Int, .required = false},
         {.name = "action", .kind = ParamKind::String, .required = false}},
        [](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
        {
            if (json_has_key(req.params_json, "seed"))
            {
                g_fuzz_base_seed  = static_cast<uint64_t>(json_get_int(req.params_json, "seed", 0));
                g_fuzz_rng        = std::mt19937(static_cast<uint32_t>(g_fuzz_base_seed));
                g_fuzz_seed_set   = true;
                g_fuzz_step_index = 0;
            }

            FuzzAction action = pick_weighted_action(g_fuzz_rng);
            const std::string forced = json_get_string(req.params_json, "action");
            if (!forced.empty())
            {
                FuzzAction parsed{};
                if (!parse_fuzz_action(forced, parsed))
                {
                    req.response_json = json_error(req.id, "Unknown fuzz action: " + forced);
                    return;
                }
                action = parsed;
            }

            int               pump_frames = 1;
            const std::string details =
                run_fuzz_action(action, app, ui_ctx, g_fuzz_rng, pump_frames);
            ++g_fuzz_step_index;

            std::ostringstream oss;
            oss << "{\"action\":\"" << fuzz_action_name(action) << "\",\"step\":" << g_fuzz_step_index
                << ",\"seed\":" << (g_fuzz_seed_set ? g_fuzz_base_seed : 0) << ",\"details\":"
                << details << ",\"pump_frames\":" << pump_frames << "}";
            req.response_json = json_ok(req.id, oss.str());
        }));

    entries.push_back(automation_handler(
        "fuzz_reset",
        "Reset fuzz RNG state and step counter. Optional seed sets a deterministic starting point.",
        Ctx::None,
        {{.name = "seed", .kind = ParamKind::Int, .required = false}},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        {
            g_fuzz_step_index = 0;
            if (json_has_key(req.params_json, "seed"))
            {
                g_fuzz_base_seed = static_cast<uint64_t>(json_get_int(req.params_json, "seed", 0));
                g_fuzz_rng       = std::mt19937(static_cast<uint32_t>(g_fuzz_base_seed));
                g_fuzz_seed_set  = true;
            }
            else
            {
                g_fuzz_rng      = std::mt19937(std::random_device{}());
                g_fuzz_seed_set = false;
            }
            req.response_json = json_ok(req.id,
                                        "{\"reset\":true,\"seed\":" + std::to_string(g_fuzz_base_seed)
                                            + ",\"seed_explicit\":"
                                            + (g_fuzz_seed_set ? "true" : "false") + "}");
        }));

    entries.push_back(automation_handler(
        "list_fuzz_actions",
        "List available fuzz action names and their weights.",
        Ctx::None,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < sizeof(kFuzzWeights) / sizeof(kFuzzWeights[0]); ++i)
            {
                if (i > 0)
                    oss << ",";
                oss << R"({"action":")" << fuzz_action_name(kFuzzWeights[i].action)
                    << R"(","weight":)" << kFuzzWeights[i].weight << "}";
            }
            oss << "]";
            req.response_json = json_ok(req.id, "{\"actions\":" + oss.str() + "}");
        }));

    return entries;
}

}   // namespace spectra
