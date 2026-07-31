// handlers_figure.cpp — create_figure, add_series, switch_figure,
//                       get_figure_info, get_state handlers.

#include "../automation_handler.hpp"
#include "../automation_figure_ops.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/native_dialog_policy.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/theme.hpp"
    #include "ui/input/input.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
#endif

#include <sstream>

namespace spectra
{

#ifdef SPECTRA_USE_IMGUI
namespace
{
const char* tool_mode_name(ToolMode mode)
{
    switch (mode)
    {
        case ToolMode::Pan:
            return "Pan";
        case ToolMode::BoxZoom:
            return "Zoom";
        case ToolMode::Select:
            return "Select";
        case ToolMode::Measure:
            return "Measure";
        case ToolMode::Annotate:
            return "Annotate";
        case ToolMode::ROI:
            return "ROI";
    }
    return "Unknown";
}
}   // namespace
#endif

std::vector<AutomationHandlerEntry> make_figure_handlers()
{
    using Ctx = AutomationContextFlag;
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "get_state",
        "Get the current state of the Spectra application.",
        Ctx::None,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
        {
            std::ostringstream oss;
            oss << "{";
            auto ids = app->figure_registry().all_ids();
            oss << "\"figure_count\":" << ids.size();

            FigureId active_id = INVALID_FIGURE_ID;
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx && ui_ctx->fig_mgr)
                active_id = ui_ctx->fig_mgr->active_index();
#endif
            if (active_id == INVALID_FIGURE_ID)
                oss << ",\"active_figure_id\":null";
            else
                oss << ",\"active_figure_id\":" << active_id;

            oss << ",\"figures\":[";
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (i > 0)
                    oss << ",";
                Figure* fig = app->figure_registry().get(ids[i]);
                oss << "{\"id\":" << ids[i];
                if (fig)
                {
                    oss << ",\"width\":" << fig->width() << ",\"height\":" << fig->height()
                        << ",\"axes_count\":" << fig->axes().size();
                    size_t ts = 0;
                    for (const auto& ax : fig->axes())
                    {
                        if (ax)
                            ts += ax->series().size();
                    }
                    oss << ",\"total_series\":" << ts;
                }
                oss << "}";
            }
            oss << "]";

#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx)
            {
                bool has_3d_axes = false;
                if (ui_ctx->fig_mgr)
                {
                    if (Figure* fig = ui_ctx->fig_mgr->active_figure())
                    {
                        for (const auto& ax_base : fig->all_axes())
                        {
                            if (ax_base && dynamic_cast<Axes3D*>(ax_base.get()))
                            {
                                has_3d_axes = true;
                                break;
                            }
                        }
                    }
                }
                oss << ",\"undo_count\":" << ui_ctx->undo_mgr.undo_count()
                    << ",\"redo_count\":" << ui_ctx->undo_mgr.redo_count()
                    << ",\"is_3d_mode\":" << (has_3d_axes ? "true" : "false") << R"(,"theme":")"
                    << json_escape(ui_ctx->theme_mgr ? ui_ctx->theme_mgr->current_theme_name() : "")
                    << '"';
                if (ui_ctx->imgui_ui)
                {
                    auto& imgui = *ui_ctx->imgui_ui;
                    auto& lm    = imgui.get_layout_manager();
                    oss << R"(,"ui":{)" << R"("interaction_mode":")"
                        << tool_mode_name(imgui.get_interaction_mode())
                        << R"(","timeline_visible":)"
                        << (imgui.is_timeline_visible() ? "true" : "false")
                        << R"(,"curve_editor_visible":)"
                        << (imgui.is_curve_editor_visible() ? "true" : "false")
                        << R"(,"plugins_panel_visible":)"
                        << (imgui.is_plugins_panel_visible() ? "true" : "false")
                        << R"(,"inspector_visible":)"
                        << (lm.is_inspector_visible() ? "true" : "false")
                        << R"(,"nav_rail_visible":)"
                        << (lm.is_nav_rail_visible() ? "true" : "false")
                        << R"(,"transform_dialog_open":)"
                        << (imgui.is_transform_dialog_open() ? "true" : "false")
                        << R"(,"theme_settings_visible":)"
                        << (imgui.is_theme_settings_visible() ? "true" : "false")
                        << R"(,"tab_drag_active":)"
                        << ((ui_ctx->tab_drag_controller.is_active() || imgui.is_tab_interacting())
                                ? "true"
                                : "false")
                        << "}";
                    if (auto* shell = imgui.app_shell())
                    {
                        oss << R"(,"panels":{)";
                        bool first = true;
                        for (const auto& [panel_id, visible] : shell->capture_panel_visibility())
                        {
                            if (!first)
                                oss << ",";
                            first = false;
                            oss << "\"" << json_escape(panel_id)
                                << "\":" << (visible ? "true" : "false");
                        }
                        oss << "}";
                    }
                }
            }
#else
            (void)ui_ctx;
#endif
            oss << ",\"native_dialogs_enabled\":" << (native_dialogs_enabled() ? "true" : "false");
            oss << "}";
            req.response_json = json_ok(req.id, oss.str());
        }));

    entries.push_back(automation_handler(
        "create_figure",
        "Create a new figure in Spectra.",
        Ctx::None,
        {},
        [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
        {
            uint32_t w        = static_cast<uint32_t>(json_get_int(req.params_json, "width", 1280));
            uint32_t h        = static_cast<uint32_t>(json_get_int(req.params_json, "height", 720));
            Figure&  new_fig  = app->figure({w, h});
            FigureId new_id   = app->figure_registry().find_id(&new_fig);
            req.response_json = json_ok(req.id, "{\"figure_id\":" + std::to_string(new_id) + "}");
        }));

    entries.push_back(
        automation_handler("add_series",
                           "Add a data series to a figure.",
                           Ctx::None,
                           {{.name = "figure_id", .kind = ParamKind::Int, .required = true}},
                           [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
                           { automation_add_series(req, app->figure_registry()); }));

    entries.push_back(
        automation_handler("switch_figure",
                           "Switch to a specific figure by its ID.",
                           Ctx::ImGui | Ctx::FigureMgr,
                           {{.name = "figure_id", .kind = ParamKind::Int, .required = true}},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
                           {
#ifdef SPECTRA_USE_IMGUI
                               uint64_t fig_id = json_get_uint64(req.params_json, "figure_id", 0);
                               ui_ctx->fig_mgr->queue_switch(static_cast<FigureId>(fig_id));
                               req.response_json = json_ok(req.id);
#else
            (void)ui_ctx;
            req.response_json = json_error(req.id, "ImGui not available");
#endif
                           }));

    entries.push_back(
        automation_handler("get_figure_info",
                           "Get detailed information about a figure.",
                           Ctx::None,
                           {{.name = "figure_id", .kind = ParamKind::Int, .required = true}},
                           [](AutomationRequest& req, App* app, WindowUIContext* /*ui_ctx*/)
                           { automation_get_figure_info(req, app->figure_registry()); }));

    return entries;
}

}   // namespace spectra
