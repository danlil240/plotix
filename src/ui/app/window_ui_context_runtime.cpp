#include "window_ui_context_runtime.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>

    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <spectra/series.hpp>

    #include "session_runtime.hpp"
    #include "window_ui_context.hpp"

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        #include "ui/window/window_manager.hpp"
    #endif

namespace spectra
{

namespace
{

void queue_figure_detach(SessionRuntime& session,
                         FigureRegistry& registry,
                         FigureManager&  fig_mgr,
                         FigureId        figure_id,
                         float           screen_x,
                         float           screen_y)
{
    Figure* fig = registry.get(figure_id);
    if (!fig)
        return;

    const uint32_t    win_w = fig->width() > 0 ? fig->width() : 800;
    const uint32_t    win_h = fig->height() > 0 ? fig->height() : 600;
    const std::string title = fig_mgr.get_title(figure_id);
    session.queue_detach(
        {figure_id, win_w, win_h, title, static_cast<int>(screen_x), static_cast<int>(screen_y)});
}

void wire_timeline_from_figure(WindowUIContext& ui_ctx, Figure* figure, bool has_animation)
{
    ui_ctx.timeline_editor.set_interpolator(&ui_ctx.keyframe_interpolator);
    ui_ctx.curve_editor.set_interpolator(&ui_ctx.keyframe_interpolator);

    if (!figure)
        return;

    if (figure->anim_duration() > 0.0f)
        ui_ctx.timeline_editor.set_duration(figure->anim_duration());
    else if (has_animation)
        ui_ctx.timeline_editor.set_duration(60.0f);

    if (figure->anim_loop())
        ui_ctx.timeline_editor.set_loop_mode(LoopMode::Loop);

    if (figure->anim_fps() > 0.0f)
        ui_ctx.timeline_editor.set_fps(figure->anim_fps());

    if (has_animation)
        ui_ctx.timeline_editor.play();
}

void wire_demo_animation_channels(WindowUIContext& ui_ctx, Figure& figure)
{
    auto& timeline_editor       = ui_ctx.timeline_editor;
    auto& keyframe_interpolator = ui_ctx.keyframe_interpolator;

    const float anim_dur = timeline_editor.duration();
    int         s_idx    = 0;

    for (auto& ax : figure.axes())
    {
        if (!ax)
            continue;

        for (auto& s : ax->series_mut())
        {
            if (!s)
                continue;

            const std::string prefix =
                s->label().empty() ? "Series " + std::to_string(s_idx) : s->label();

            {
                const uint32_t ch_id =
                    timeline_editor.add_animated_track(prefix + " Opacity", 1.0f);
                timeline_editor.add_animated_keyframe(ch_id, 0.0f, 1.0f, 1);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.4f, 0.3f, 6);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.7f, 0.8f, 4);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur, 1.0f, 5);

                Series* raw = s.get();
                keyframe_interpolator.bind_callback(ch_id,
                                                    prefix + " Opacity",
                                                    [raw](float v)
                                                    { raw->opacity(std::clamp(v, 0.0f, 1.0f)); });
            }

            if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
            {
                const float    base  = sc->size();
                const uint32_t ch_id = timeline_editor.add_animated_track(prefix + " Size", base);
                timeline_editor.add_animated_keyframe(ch_id, 0.0f, base, 1);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.3f, base * 2.5f, 3);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.6f, base * 0.5f, 6);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur, base, 5);

                keyframe_interpolator.bind_callback(ch_id,
                                                    prefix + " Size",
                                                    [sc](float v) { sc->size(std::max(v, 1.0f)); });
            }
            else if (auto* ln = dynamic_cast<LineSeries*>(s.get()))
            {
                const float    base  = ln->width();
                const uint32_t ch_id = timeline_editor.add_animated_track(prefix + " Width", base);
                timeline_editor.add_animated_keyframe(ch_id, 0.0f, base, 1);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.3f, base * 3.0f, 3);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur * 0.6f, base * 0.5f, 6);
                timeline_editor.add_animated_keyframe(ch_id, anim_dur, base, 5);

                keyframe_interpolator.bind_callback(ch_id,
                                                    prefix + " Width",
                                                    [ln](float v)
                                                    { ln->width(std::max(v, 0.5f)); });
            }

            ++s_idx;
        }
    }

    keyframe_interpolator.compute_all_auto_tangents();
}

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
void wire_input_handler_from_figure(WindowUIContext& ui_ctx, Figure* figure)
{
    if (!figure)
        return;

    ui_ctx.input_handler.set_figure(figure);
    if (figure->axes().empty() || !figure->axes()[0])
        return;

    ui_ctx.input_handler.set_active_axes(figure->axes()[0].get());
    const auto& vp = figure->axes()[0]->viewport();
    ui_ctx.input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
}
    #endif

void wire_tab_split_callbacks(TabBar&        tabs,
                              DockSystem&    dock_system,
                              FigureManager& fig_mgr,
                              TabSplitMode   mode)
{
    if (mode == TabSplitMode::DuplicateThenSplit)
    {
        tabs.set_tab_split_right_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                const FigureId id      = fig_mgr.figure_ids()[pos];
                const FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_right(id, new_fig);
                dock_system.set_active_figure_index(id);
            });

        tabs.set_tab_split_down_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                const FigureId id      = fig_mgr.figure_ids()[pos];
                const FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_down(id, new_fig);
                dock_system.set_active_figure_index(id);
            });
        return;
    }

    tabs.set_tab_split_right_callback(
        [&dock_system, &fig_mgr](size_t pos)
        {
            if (pos >= fig_mgr.figure_ids().size())
                return;
            const FigureId id   = fig_mgr.figure_ids()[pos];
            auto*          pane = dock_system.split_view().root()
                                      ? dock_system.split_view().root()->find_by_figure(id)
                                      : nullptr;
            if (!pane || pane->figure_count() < 2)
                return;
            auto* new_pane = dock_system.split_figure_right(id, id);
            if (!new_pane)
                return;
            auto* parent = new_pane->parent();
            if (parent && parent->first())
                parent->first()->remove_figure(id);
            dock_system.set_active_figure_index(id);
        });

    tabs.set_tab_split_down_callback(
        [&dock_system, &fig_mgr](size_t pos)
        {
            if (pos >= fig_mgr.figure_ids().size())
                return;
            const FigureId id   = fig_mgr.figure_ids()[pos];
            auto*          pane = dock_system.split_view().root()
                                      ? dock_system.split_view().root()->find_by_figure(id)
                                      : nullptr;
            if (!pane || pane->figure_count() < 2)
                return;
            auto* new_pane = dock_system.split_figure_down(id, id);
            if (!new_pane)
                return;
            auto* parent = new_pane->parent();
            if (parent && parent->first())
                parent->first()->remove_figure(id);
            dock_system.set_active_figure_index(id);
        });
}

}   // namespace

void wire_window_ui_runtime(const WindowUIContextRuntimeWireOptions& options)
{
    if (!options.ui_ctx || !options.registry || !options.session || !options.ui_ctx->fig_mgr)
        return;

    auto& ui_ctx   = *options.ui_ctx;
    auto& fig_mgr  = *ui_ctx.fig_mgr;
    auto& session  = *options.session;
    auto& registry = *options.registry;

    wire_timeline_from_figure(ui_ctx, options.active_figure, options.has_animation);

    if (options.wire_demo_animation_channels && options.active_figure)
        wire_demo_animation_channels(ui_ctx, *options.active_figure);

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (options.window_manager)
    {
        // Always assign WindowManager — tab drag needs it for preview windows and
        // cross-window hit testing even when drop callbacks were wired by the builder.
        ui_ctx.tab_drag_controller.set_window_manager(options.window_manager);

        wire_input_handler_from_figure(ui_ctx, options.active_figure);
    }
    #endif

    if (options.enable_window_tab_callbacks && ui_ctx.figure_tabs)
    {
        wire_tab_split_callbacks(*ui_ctx.figure_tabs,
                                 ui_ctx.dock_system,
                                 fig_mgr,
                                 options.tab_split_mode);

        ui_ctx.figure_tabs->set_tab_detach_callback(
            [&fig_mgr, &session, &registry](size_t pos, float screen_x, float screen_y)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                const FigureId id = fig_mgr.figure_ids()[pos];
                if (fig_mgr.count() <= 1)
                    return;
                queue_figure_detach(session, registry, fig_mgr, id, screen_x, screen_y);
            });
    }

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (!options.tab_drag_already_wired && options.window_manager)
    {
        auto* window_mgr = options.window_manager;
        ui_ctx.tab_drag_controller.set_on_drop_outside(
            [&fig_mgr, &session, &registry](FigureId index, float screen_x, float screen_y)
            { queue_figure_detach(session, registry, fig_mgr, index, screen_x, screen_y); });

        ui_ctx.tab_drag_controller.set_on_drop_on_window(
            [&session, window_mgr](FigureId index,
                                   uint32_t target_window_id,
                                   float /*screen_x*/,
                                   float /*screen_y*/)
            {
                int   zone = 0;
                float lx   = 0.0f;
                float ly   = 0.0f;
                if (window_mgr)
                {
                    const auto info = window_mgr->cross_window_drop_info();
                    zone            = info.zone;
                    lx              = info.hx;
                    ly              = info.hy;
                }
                session.queue_move({index, target_window_id, zone, lx, ly});
            });
    }
    #endif

    if (ui_ctx.imgui_ui)
    {
        ui_ctx.imgui_ui->set_pane_tab_detach_cb(
            [&fig_mgr, &session, &registry](FigureId index, float screen_x, float screen_y)
            { queue_figure_detach(session, registry, fig_mgr, index, screen_x, screen_y); });
    }

    ui_ctx.cmd_palette.set_body_font(nullptr);
    ui_ctx.cmd_palette.set_heading_font(nullptr);

    capture_figure_home_limits(registry, fig_mgr);
}

void capture_figure_home_limits(FigureRegistry& registry, FigureManager& fig_mgr)
{
    for (const auto id : registry.all_ids())
    {
        Figure* fig_ptr = registry.get(id);
        if (!fig_ptr)
            continue;

        auto& vm = fig_mgr.state(id);
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                vm.set_home_limit(ax.get(), {ax->x_limits(), ax->y_limits()});
        }
    }
}

}   // namespace spectra

#endif
