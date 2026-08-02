#include "window_runtime.hpp"

#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include "anim/frame_scheduler.hpp"
#include "core/layout.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include <spectra/figure_registry.hpp>
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "ui/layout/layout_manager.hpp"
#endif

#if defined(SPECTRA_USE_SDL3)
    #include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <span>
#include <string>

namespace spectra
{

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
namespace
{

void apply_window_size_limits(WindowUIContext& ui_ctx, ImGuiIntegration& imgui_ui)
{
    if (!ui_ctx.glfw_window)
        return;

    const float min_w = LayoutManager::min_window_width(imgui_ui.is_nav_rail_visible());
    const float min_h = LayoutManager::min_window_height(imgui_ui.is_nav_rail_visible(),
                                                         imgui_ui.is_command_bar_visible(),
                                                         imgui_ui.is_status_bar_visible());

    if (min_w == ui_ctx.applied_min_window_w_ && min_h == ui_ctx.applied_min_window_h_)
        return;

    ui_ctx.applied_min_window_w_ = min_w;
    ui_ctx.applied_min_window_h_ = min_h;

    const int min_wi = static_cast<int>(std::ceil(min_w));
    const int min_hi = static_cast<int>(std::ceil(min_h));

    #ifdef SPECTRA_USE_GLFW
    auto* win = static_cast<GLFWwindow*>(ui_ctx.glfw_window);
    glfwSetWindowSizeLimits(win, min_wi, min_hi, GLFW_DONT_CARE, GLFW_DONT_CARE);

    int ww = 0;
    int wh = 0;
    glfwGetWindowSize(win, &ww, &wh);
    if (ww < min_wi || wh < min_hi)
        glfwSetWindowSize(win, std::max(ww, min_wi), std::max(wh, min_hi));
    #elif defined(SPECTRA_USE_SDL3)
    auto* win = static_cast<SDL_Window*>(ui_ctx.glfw_window);
    SDL_SetWindowMinimumSize(win, min_wi, min_hi);

    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww < min_wi || wh < min_hi)
        SDL_SetWindowSize(win, std::max(ww, min_wi), std::max(wh, min_hi));
    #endif
}

}   // namespace
#endif

WindowRuntime::WindowRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry)
    : backend_(backend), renderer_(renderer), registry_(registry)
{
}

// ─── update ───────────────────────────────────────────────────────────────────
// Per-window update: advance animations, build ImGui UI, compute layout.
void WindowRuntime::update(WindowUIContext& ui_ctx,
                           FrameState&      fs,
                           FrameScheduler&  scheduler,
                           bool             allow_animation_tick,
                           float            animation_dt,
                           FrameProfiler*   profiler
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                           ,
                           WindowManager* /*window_mgr*/
#endif
)
{
    auto* active_figure    = fs.active_figure;
    auto& active_figure_id = fs.active_figure_id;
    auto& has_animation    = fs.has_animation;

#ifdef SPECTRA_USE_IMGUI
    auto& imgui_ui         = ui_ctx.imgui_ui;
    auto& data_interaction = ui_ctx.data_interaction;
    auto& dock_system      = ui_ctx.dock_system;
    auto& timeline_editor  = ui_ctx.timeline_editor;
    auto& mode_transition  = ui_ctx.mode_transition;
    auto& fig_mgr          = *ui_ctx.fig_mgr;
    auto& anim_controller  = ui_ctx.anim_controller;

    // Advance timeline editor (drives interpolator evaluation)
    // When Playing, we control the playhead ourselves to avoid double-speed
    if (timeline_editor.playback_state() != PlaybackState::Playing)
    {
        timeline_editor.advance(scheduler.dt());
    }

    // Update mode transition animation — only animate camera, never axis limits
    if (active_figure && mode_transition.is_active())
    {
        mode_transition.update(scheduler.dt());

        Axes3D* ax3d = nullptr;
        for (auto& ax_base : active_figure->all_axes())
        {
            if (ax_base)
            {
                ax3d = dynamic_cast<Axes3D*>(ax_base.get());
                if (ax3d)
                    break;
            }
        }
        if (ax3d)
        {
            Camera interp_cam = mode_transition.interpolated_camera();
            // Set position directly (not via orbit) because the
            // top-down camera is on the Z axis, not an orbit position.
            ax3d->camera().position        = interp_cam.position;
            ax3d->camera().target          = interp_cam.target;
            ax3d->camera().up              = interp_cam.up;
            ax3d->camera().fov             = interp_cam.fov;
            ax3d->camera().ortho_size      = interp_cam.ortho_size;
            ax3d->camera().projection_mode = interp_cam.projection_mode;
            ax3d->camera().near_clip       = interp_cam.near_clip;
            ax3d->camera().far_clip        = interp_cam.far_clip;
            ax3d->camera().distance        = interp_cam.distance;
        }
    }
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Update interaction animations (animated zoom, inertial pan, auto-fit)
    auto& input_handler = ui_ctx.input_handler;
    input_handler.update(scheduler.dt());
#endif

#ifdef SPECTRA_USE_IMGUI
    auto sync_active_figure_from_manager = [&]()
    {
        FigureId mgr_active = fig_mgr.active_index();
        if (mgr_active != active_figure_id || active_figure != registry_.get(active_figure_id))
        {
            active_figure_id = mgr_active;
            Figure* fig      = registry_.get(active_figure_id);
            fs.active_figure = fig;
            active_figure    = fig;
            if (active_figure)
            {
                scheduler.set_target_fps(active_figure->anim_.fps);
                has_animation = active_figure->has_animation();
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                input_handler.set_figure(active_figure);
                // Phase 2 (LT-5): wire FigureViewModel for ViewModel-based limit mutations
                if (ui_ctx.fig_mgr)
                    input_handler.set_figure_view_model(&fig_mgr.active_state());
                else
                    input_handler.set_figure_view_model(nullptr);
                if (!active_figure->axes().empty() && active_figure->axes()[0])
                {
                    input_handler.set_active_axes(active_figure->axes()[0].get());
                    const auto& vp = active_figure->axes()[0]->viewport();
                    input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
    #endif
            }
        }
    };

    // Sync before UI work so build_ui never sees a stale figure pointer.
    sync_active_figure_from_manager();
#endif

    // Helper: wire deferred-deletion callbacks on a figure's axes
    // BEFORE the user's on_frame callback can call clear_series().
    // Only set on axes that don't already have it (avoids per-frame
    // std::function allocation for the common case of unchanged axes).
    auto wire_series_callbacks = [this, &ui_ctx](Figure* fig)
    {
        for (auto& axes_ptr : fig->axes())
        {
            if (axes_ptr && !axes_ptr->has_series_removed_callback())
                axes_ptr->set_series_removed_callback(
                    [this, &ui_ctx](const Series* s)
                    {
                        renderer_.notify_series_removed(s);
#ifdef SPECTRA_USE_IMGUI
                        if (ui_ctx.data_interaction)
                            ui_ctx.data_interaction->notify_series_removed(s);
                        if (ui_ctx.imgui_ui)
                            ui_ctx.imgui_ui->notify_series_removed(s);
#endif
                    });
        }
        for (auto& axes_ptr : fig->all_axes())
        {
            if (axes_ptr && !axes_ptr->has_series_removed_callback())
                axes_ptr->set_series_removed_callback(
                    [this, &ui_ctx](const Series* s)
                    {
                        renderer_.notify_series_removed(s);
#ifdef SPECTRA_USE_IMGUI
                        if (ui_ctx.data_interaction)
                            ui_ctx.data_interaction->notify_series_removed(s);
                        if (ui_ctx.imgui_ui)
                            ui_ctx.imgui_ui->notify_series_removed(s);
#endif
                    });
        }
    };

    // Wire callbacks for ALL figures in the window (not just the active one).
    // The active figure's on_frame callback may mutate axes on other figures
    // (e.g. a shared update_all that calls clear_series() on a non-active tab's
    // axes).  Without wiring, those axes' clear_series() won't notify
    // DataInteraction/ImGuiIntegration, leaving stale Series* pointers.
#ifdef SPECTRA_USE_IMGUI
    for (auto fid : fig_mgr.figure_ids())
    {
        Figure* fig = registry_.get(fid);
        if (fig)
            wire_series_callbacks(fig);
    }
#else
    if (active_figure)
        wire_series_callbacks(active_figure);
#endif

    // Helper: drive animation for a single figure using its own anim_time_.
    // is_active controls whether this figure syncs with the timeline editor.
    auto drive_figure_anim = [&](Figure* fig, bool is_active)
    {
        if (!fig->anim_.on_frame)
            return;

        Frame frame = scheduler.current_frame();
        frame.dt    = animation_dt;

#ifdef SPECTRA_USE_IMGUI
        if (is_active)
        {
            auto tl_state = timeline_editor.playback_state();
            if (tl_state == PlaybackState::Playing)
            {
                float tl_playhead = timeline_editor.playhead();
                float diff        = tl_playhead - fig->anim_.time;
                // If the figure's anim_time_ has advanced past the timeline
                // playhead (e.g. it was running as non-active while a different
                // tab was selected), sync the playhead forward to the figure
                // instead of resetting the figure backward.
                if (diff < -0.001f)
                {
                    // Figure is ahead of playhead — sync playhead to figure
                    timeline_editor.set_playhead(fig->anim_.time);
                }
                else if (diff > 0.001f)
                {
                    // User scrubbed the playhead forward — sync figure to playhead
                    fig->anim_.time = tl_playhead;
                }
                fig->anim_.time += frame.dt;
                frame.elapsed_sec = fig->anim_.time;
                fig->anim_.on_frame(frame);
                if (fig->anim_.time > timeline_editor.duration())
                {
                    timeline_editor.set_duration(fig->anim_.time + 30.0f);
                }
                timeline_editor.set_playhead(fig->anim_.time);
            }
            else if (tl_state == PlaybackState::Paused)
            {
                fig->anim_.time   = timeline_editor.playhead();
                frame.elapsed_sec = fig->anim_.time;
                frame.dt          = 0.0f;
                fig->anim_.on_frame(frame);
            }
            else
            {
                fig->anim_.time   = 0.0f;
                frame.elapsed_sec = 0.0f;
                frame.dt          = 0.0f;
                fig->anim_.on_frame(frame);
            }
        }
        else
        {
            // Non-active animated figure: advance its own time independently
            fig->anim_.time += frame.dt;
            frame.elapsed_sec = fig->anim_.time;
            fig->anim_.on_frame(frame);
        }
#else
        fig->anim_.time += frame.dt;
        frame.elapsed_sec = fig->anim_.time;
        fig->anim_.on_frame(frame);
#endif

        // Post-callback guard: if on_frame left all axes empty, the callback
        // likely holds dangling Series& references (e.g. knob_demo captures
        // `line` by ref then externally clears).  Kill it to prevent
        // use-after-free on the NEXT frame.  This runs AFTER the callback so
        // that clear_series() + re-add within the same on_frame works fine.
        {
            bool has_any_series = false;
            for (auto& ax : fig->axes())
                if (ax && !ax->series().empty())
                {
                    has_any_series = true;
                    break;
                }
            if (!has_any_series)
            {
                for (auto& ax : fig->all_axes())
                    if (ax && !ax->series().empty())
                    {
                        has_any_series = true;
                        break;
                    }
            }
            if (!has_any_series && !fig->anim_.loop)
            {
                fig->anim_.on_frame = nullptr;
            }
        }
    };

    // Drive animation for the active figure
    if (allow_animation_tick && active_figure && has_animation)
    {
        drive_figure_anim(active_figure, /*is_active=*/true);
    }

#ifdef SPECTRA_USE_IMGUI
    // Drive animation for non-active figures visible in split view panes.
    if (allow_animation_tick && dock_system.is_split())
    {
        auto pane_infos = dock_system.get_pane_infos();
        for (const auto& pinfo : pane_infos)
        {
            if (pinfo.figure_index == fs.active_figure_id)
                continue;   // already driven above
            Figure* pfig = registry_.get(pinfo.figure_index);
            if (!pfig || !pfig->anim_.on_frame)
                continue;
            wire_series_callbacks(pfig);
            drive_figure_anim(pfig, /*is_active=*/false);
        }
    }

#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Keep swapchain extent matched to the live framebuffer BEFORE ImGui
    // new_frame so layout zones and GPU rendering share the same dimensions.
    bool     resize_active       = false;
    bool     swapchain_recreated = false;
    uint32_t live_fb_w           = 0;
    uint32_t live_fb_h           = 0;
    {
        auto& needs_resize = ui_ctx.needs_resize;
        // auto& new_width    = ui_ctx.new_width;
        // auto& new_height   = ui_ctx.new_height;
        auto* vk = static_cast<VulkanBackend*>(&backend_);
        auto* aw = vk->active_window();

        if (aw)
        {
            vk->query_window_framebuffer_size(*aw, live_fb_w, live_fb_h);
            resize_active =
                live_fb_w > 0 && live_fb_h > 0
                && (live_fb_w != vk->swapchain_width() || live_fb_h != vk->swapchain_height());

            const bool want_recreate =
                resize_active || needs_resize || aw->swapchain_dirty || aw->swapchain_invalidated;
            if (want_recreate && live_fb_w > 0 && live_fb_h > 0)
            {
                const uint32_t target_w = live_fb_w;
                const uint32_t target_h = live_fb_h;
                SPECTRA_LOG_DEBUG("resize",
                                  "Recreating swapchain: " + std::to_string(target_w) + "x"
                                      + std::to_string(target_h));
                needs_resize              = false;
                aw->swapchain_invalidated = false;
                vk->clear_swapchain_dirty();
                backend_.recreate_swapchain(target_w, target_h);
                swapchain_recreated = true;
                resize_active       = false;

                if (active_figure)
                {
                    active_figure->config_.width  = backend_.swapchain_width();
                    active_figure->config_.height = backend_.swapchain_height();
                }
    #ifdef SPECTRA_USE_IMGUI
                if (imgui_ui)
                {
                    imgui_ui->on_swapchain_recreated(*vk);
                }
    #endif
            }
            else if (needs_resize)
            {
                needs_resize = false;
            }
        }
    }
#endif

    // Start ImGui frame (updates layout manager with current window size).
    fs.imgui_frame_started = false;
#ifdef SPECTRA_USE_IMGUI
    if (imgui_ui)
    {
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        imgui_ui->get_layout_manager().set_resize_active(resize_active || swapchain_recreated);
    #endif
        imgui_ui->new_frame();
        fs.imgui_frame_started = true;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        // GLFW can report a stale DisplaySize for a frame during live resize.
        // Re-read window dimensions and snap layout so chrome zones stay aligned.
        if ((resize_active || swapchain_recreated) && ui_ctx.glfw_window)
        {
            auto* win = static_cast<GLFWwindow*>(ui_ctx.glfw_window);
            int   ww  = 0;
            int   wh  = 0;
            int   fbw = 0;
            int   fbh = 0;
            glfwGetWindowSize(win, &ww, &wh);
            glfwGetFramebufferSize(win, &fbw, &fbh);
            if (ww > 0 && wh > 0)
            {
                ImGuiIO& io    = ImGui::GetIO();
                io.DisplaySize = ImVec2(static_cast<float>(ww), static_cast<float>(wh));
                io.DisplayFramebufferScale =
                    ImVec2(static_cast<float>(fbw) / static_cast<float>(ww),
                           static_cast<float>(fbh) / static_cast<float>(wh));
                imgui_ui->update_layout(io.DisplaySize.x, io.DisplaySize.y, 0.0f);
            }
        }
    #endif
    }
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Update input handler with current active axes viewport
    if (active_figure && !active_figure->axes().empty() && active_figure->axes()[0])
    {
        auto& vp = active_figure->axes()[0]->viewport();
        input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }
#endif

#ifdef SPECTRA_USE_IMGUI
    // Build ImGui UI (new_frame was already called above before layout computation)
    if (imgui_ui && fs.imgui_frame_started)
    {
        if (profiler)
            profiler->begin_stage("imgui_build");
        if (active_figure)
            imgui_ui->build_ui(*active_figure, &fig_mgr.active_state());
        else
            imgui_ui->build_empty_ui();

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        apply_window_size_limits(ui_ctx, *imgui_ui);
    #endif

        // Old TabBar is replaced by unified pane tab headers
        // (drawn by draw_pane_tab_headers in ImGuiIntegration)
        // Always hide the layout manager's tab bar zone so canvas
        // extends into that space — pane headers draw in the canvas area.
        imgui_ui->get_layout_manager().set_tab_bar_visible(false);

        // Handle interaction state from UI — Home restores original view
        if (active_figure && imgui_ui->should_reset_view())
        {
            auto& active_vm = fig_mgr.active_state();
            for (auto& ax : active_figure->axes_mut())
            {
                if (ax)
                {
                    auto it = active_vm.home_limits().find(ax.get());
                    if (it != active_vm.home_limits().end())
                    {
                        // Animate back to the user's original limits
                        anim_controller.animate_axis_limits(*ax,
                                                            it->second.x,
                                                            it->second.y,
                                                            0.25f,
                                                            ease::ease_out);
                    }
                    else
                    {
                        // Fallback: auto-fit if we don't have saved limits
                        auto old_xlim = ax->x_limits();
                        auto old_ylim = ax->y_limits();
                        ax->auto_fit();
                        AxisLimits target_x = ax->x_limits();
                        AxisLimits target_y = ax->y_limits();
                        ax->xlim(old_xlim.min, old_xlim.max);
                        ax->ylim(old_ylim.min, old_ylim.max);
                        anim_controller.animate_axis_limits(*ax,
                                                            target_x,
                                                            target_y,
                                                            0.25f,
                                                            ease::ease_out);
                    }
                }
            }
            // 3D axes (subplot3d populates all_axes only)
            for (auto& ax_base : active_figure->all_axes_mut())
            {
                if (ax_base)
                {
                    if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
                        ax3d->auto_fit();
                }
            }
            imgui_ui->clear_reset_view();
        }

        // Live reset chip: restore presented-buffer-driven tracking view
        if (active_figure && imgui_ui->should_reset_live_view())
        {
            for (auto& ax : active_figure->axes_mut())
            {
                if (ax && ax->has_presented_buffer())
                {
                    ax->presented_buffer(ax->presented_buffer_seconds());
                }
            }
            imgui_ui->clear_reset_live_view();
        }

        // Update input handler tool mode
        input_handler.set_tool_mode(imgui_ui->get_interaction_mode());

        // Feed cursor data to status bar
        auto readout = input_handler.cursor_readout();
        imgui_ui->set_cursor_data(readout.data_x, readout.data_y, readout.valid);

        // Update data interaction layer (nearest-point query, tooltip state)
        if (active_figure && data_interaction)
        {
            data_interaction->update(readout, *active_figure);
        }

        // Feed zoom level (approximate: based on data bounds vs view)
        // Cache data_range to avoid O(n) minmax_element scan every frame.
        // Zoom cache is per-figure (stored in ViewModel) so switching tabs
        // doesn't share stale cache between different figures.
        if (active_figure && !active_figure->axes().empty() && active_figure->axes()[0])
        {
            auto& ax         = active_figure->axes()[0];
            auto  xlim       = ax->x_limits();
            float view_range = xlim.max - xlim.min;

            auto& zoom_vm = fig_mgr.active_state();

            // Invalidate cache when series count changes or any series is dirty
            size_t series_count = ax->series().size();
            bool   needs_recompute =
                !zoom_vm.zoom_cache_valid() || series_count != zoom_vm.cached_zoom_series_count();
            if (!needs_recompute)
            {
                for (auto& s : ax->series())
                {
                    if (s && s->is_dirty())
                    {
                        needs_recompute = true;
                        break;
                    }
                }
            }

            if (needs_recompute)
            {
                float data_min = xlim.max;
                float data_max = xlim.min;
                for (auto& s : ax->series())
                {
                    if (!s)
                        continue;
                    std::span<const float> xd;
                    if (auto* ls = dynamic_cast<LineSeries*>(s.get()))
                        xd = ls->x_data();
                    else if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
                        xd = sc->x_data();
                    if (!xd.empty())
                    {
                        auto [it_min, it_max] = std::minmax_element(xd.begin(), xd.end());
                        data_min              = std::min(data_min, *it_min);
                        data_max              = std::max(data_max, *it_max);
                    }
                }
                zoom_vm.set_zoom_cache(data_min, data_max, series_count);
            }

            float data_range = zoom_vm.cached_data_max() - zoom_vm.cached_data_min();
            if (view_range > 0.0f && data_range > 0.0f)
            {
                imgui_ui->set_zoom_level(data_range / view_range);
            }
        }

        // Always hide old tab bar — unified pane tab headers handle all tabs
        imgui_ui->get_layout_manager().set_tab_bar_visible(false);

        if (profiler)
            profiler->end_stage("imgui_build");
    }
#endif

#ifdef SPECTRA_USE_IMGUI
    // Flush deferred series removals AFTER build_ui finishes.
    // Commands like series.delete/cut (fired during glfwPollEvents) defer
    // the actual remove_series call so:
    //   1. User on_frame callbacks (raw Series& refs) run safely
    //   2. build_ui / tooltips can still read series data
    // Now that all reads are done, perform the actual destruction.
    if (imgui_ui)
        imgui_ui->flush_deferred_series_removals();

    FigureId prev_dock_active = dock_system.active_figure_index();

    // Process queued figure operations (create, close, switch)
    fig_mgr.process_pending();

    // build_ui() can switch figures directly (e.g. duplicate), so sync again
    // after processing queued operations.
    sync_active_figure_from_manager();

    // Sync root pane's figure_indices_ with actual figures when not split.
    // The unified pane tab headers always read from the root pane.
    if (!dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root && root->is_leaf())
        {
            // Ensure root has exactly the right figures
            const auto& current         = root->figure_indices();
            const auto& mgr_ids         = fig_mgr.figure_ids();
            bool        needs_sync_dock = (current.size() != mgr_ids.size());
            if (!needs_sync_dock)
            {
                for (auto id : mgr_ids)
                {
                    if (!root->has_figure(id))
                    {
                        needs_sync_dock = true;
                        break;
                    }
                }
            }
            if (needs_sync_dock)
            {
                // Rebuild figure_indices_ to match actual figures
                while (root->figure_count() > 0)
                {
                    root->remove_figure(root->figure_indices().back());
                }
                for (auto id : mgr_ids)
                {
                    root->add_figure(id);
                }
            }
            // Sync active tab
            size_t active = fig_mgr.active_index();

            // If the active figure is being torn off, switch to the next
            // available figure so the source window shows different content.
            if (imgui_ui)
            {
                FigureId tearoff = imgui_ui->tearoff_figure();
                if (tearoff != INVALID_FIGURE_ID && active == tearoff)
                {
                    for (auto id : mgr_ids)
                    {
                        if (id != tearoff)
                        {
                            active = id;
                            break;
                        }
                    }
                }
            }

            dock_system.set_active_figure_index(active);
            for (size_t li = 0; li < root->figure_indices().size(); ++li)
            {
                if (root->figure_indices()[li] == active)
                {
                    root->set_active_local_index(li);
                    break;
                }
            }
        }
    }
    else
    {
        FigureId active = fig_mgr.active_index();
        if (active != INVALID_FIGURE_ID && !dock_system.split_view().is_figure_visible(active))
        {
            SplitPane* target_pane = dock_system.split_view().pane_for_figure(prev_dock_active);
            if (!target_pane)
            {
                auto panes = dock_system.split_view().all_panes();
                if (!panes.empty())
                    target_pane = panes.front();
            }

            if (target_pane)
            {
                target_pane->add_figure(active);
                for (size_t li = 0; li < target_pane->figure_indices().size(); ++li)
                {
                    if (target_pane->figure_indices()[li] == active)
                    {
                        target_pane->set_active_local_index(li);
                        break;
                    }
                }
                dock_system.set_active_figure_index(active);
            }
        }
    }
#endif

    // Compute subplot layout AFTER build_ui() so that nav rail / inspector
    // toggles from the current frame are immediately reflected.
    {
        if (profiler)
            profiler->begin_stage("scene_update");
#ifdef SPECTRA_USE_IMGUI
        if (imgui_ui)
        {
            ImGuiIO& io = ImGui::GetIO();
            imgui_ui->get_layout_manager().set_nav_rail_visible(imgui_ui->is_nav_rail_visible());
            imgui_ui->update_layout(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);

            const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();

            // Update dock system layout with current canvas bounds
            dock_system.update_layout(canvas);

            if (dock_system.is_split())
            {
                // Per-pane layout: each pane renders its own figure
                auto pane_infos = dock_system.get_pane_infos();
                for (const auto& pinfo : pane_infos)
                {
                    {
                        auto* fig = registry_.get(pinfo.figure_index);
                        if (!fig)
                            continue;
                        // Use figure's style margins, clamped to fit pane bounds
                        const auto& fs_style = fig->style();
                        Margins     pane_margins;
                        pane_margins.left  = std::min(fs_style.margin_left, pinfo.bounds.w * 0.3f);
                        pane_margins.right = std::min(fs_style.margin_right, pinfo.bounds.w * 0.2f);
                        pane_margins.bottom =
                            std::min(fs_style.margin_bottom, pinfo.bounds.h * 0.3f);
                        pane_margins.top = std::min(fs_style.margin_top, pinfo.bounds.h * 0.2f);

                        float content_h = 0.0f;
                        float min_sub_h = fs_style.min_subplot_height;
                        float origin_y  = pinfo.bounds.y - fig->scroll_offset_y();

                        const auto rects = compute_subplot_layout(pinfo.bounds.w,
                                                                  pinfo.bounds.h,
                                                                  fig->grid_rows_,
                                                                  fig->grid_cols_,
                                                                  pane_margins,
                                                                  pinfo.bounds.x,
                                                                  origin_y,
                                                                  min_sub_h,
                                                                  &content_h);
                        fig->set_content_height(content_h);

                        // Clamp scroll offset to valid range
                        float max_scroll = std::max(0.0f, content_h - pinfo.bounds.h);
                        if (fig->scroll_offset_y() > max_scroll)
                            fig->set_scroll_offset_y(max_scroll);
                        if (fig->scroll_offset_y() < 0.0f)
                            fig->set_scroll_offset_y(0.0f);

                        for (size_t i = 0; i < fig->axes_mut().size() && i < rects.size(); ++i)
                        {
                            if (fig->axes_mut()[i])
                            {
                                fig->axes_mut()[i]->set_viewport(rects[i]);
                            }
                        }
                        for (size_t i = 0; i < fig->all_axes_mut().size() && i < rects.size(); ++i)
                        {
                            if (fig->all_axes_mut()[i])
                            {
                                fig->all_axes_mut()[i]->set_viewport(rects[i]);
                            }
                        }
                    }
                }
            }
            else if (active_figure)
            {
                SplitPane* root = dock_system.split_view().root();
                Rect       cb   = (root && root->is_leaf()) ? root->content_bounds() : canvas;
                // Use figure's style margins for layout
                const auto& af_style = active_figure->style();
                Margins     fig_margins;
                fig_margins.left   = af_style.margin_left;
                fig_margins.right  = af_style.margin_right;
                fig_margins.top    = af_style.margin_top;
                fig_margins.bottom = af_style.margin_bottom;

                float content_h = 0.0f;
                float min_sub_h = af_style.min_subplot_height;
                float origin_y  = cb.y - active_figure->scroll_offset_y();

                const auto rects = compute_subplot_layout(cb.w,
                                                          cb.h,
                                                          active_figure->grid_rows_,
                                                          active_figure->grid_cols_,
                                                          fig_margins,
                                                          cb.x,
                                                          origin_y,
                                                          min_sub_h,
                                                          &content_h);
                active_figure->set_content_height(content_h);

                // Clamp scroll offset to valid range
                float max_scroll = std::max(0.0f, content_h - cb.h);
                if (active_figure->scroll_offset_y() > max_scroll)
                    active_figure->set_scroll_offset_y(max_scroll);
                if (active_figure->scroll_offset_y() < 0.0f)
                    active_figure->set_scroll_offset_y(0.0f);

                for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i)
                {
                    if (active_figure->axes_mut()[i])
                    {
                        active_figure->axes_mut()[i]->set_viewport(rects[i]);
                    }
                }
                for (size_t i = 0; i < active_figure->all_axes_mut().size() && i < rects.size();
                     ++i)
                {
                    if (active_figure->all_axes_mut()[i])
                    {
                        active_figure->all_axes_mut()[i]->set_viewport(rects[i]);
                    }
                }
            }
        }
        else if (active_figure)
        {
            active_figure->compute_layout();
        }
#else
        if (active_figure)
            active_figure->compute_layout();
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        // Update input handler with visible canvas height for page scroll
        if (active_figure)
        {
    #ifdef SPECTRA_USE_IMGUI
            if (imgui_ui)
            {
                const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
                ui_ctx.input_handler.set_visible_height(canvas.h);

                SplitPane* root = dock_system.split_view().root();
                const Rect interaction =
                    (root && root->is_leaf()) ? root->content_bounds() : canvas;
                ui_ctx.input_handler.set_interaction_rect(interaction);
            }
    #endif
        }
#endif
        if (profiler)
            profiler->end_stage("scene_update");
    }
}

// ─── render ───────────────────────────────────────────────────────────────────
// Render one window: begin_frame, render pass, figure content, ImGui, end_frame.
// Returns true if the frame was successfully presented.
bool WindowRuntime::render(WindowUIContext& ui_ctx, FrameState& fs, FrameProfiler* profiler)
{
    auto* active_figure       = fs.active_figure;
    auto& imgui_frame_started = fs.imgui_frame_started;

    // Render frame. If begin_frame fails (OUT_OF_DATE), recreate and
    // retry once so we present content immediately (no black-flash gap).
    if (profiler)
        profiler->begin_stage("begin_frame");
    bool frame_ok = backend_.begin_frame(profiler);
    if (profiler)
        profiler->end_stage("begin_frame");

    if (!frame_ok)
    {
#ifdef SPECTRA_USE_IMGUI
        if (imgui_frame_started)
        {
            ImGui::EndFrame();
            imgui_frame_started = false;
        }
#endif
        // Only attempt swapchain recreation if it was actually invalidated
        // (OUT_OF_DATE).  Fence/acquire timeouts just mean this window is
        // temporarily busy — skip it without recreation.
        auto* vk = static_cast<VulkanBackend*>(&backend_);
        auto* aw = vk->active_window();
        if (aw && !aw->swapchain_invalidated)
            return false;   // Timeout — just skip this window

        // Swapchain truly unusable — recreate and retry
        uint32_t target_w = 0;
        uint32_t target_h = 0;
        if (aw)
        {
            vk->query_window_framebuffer_size(*aw, target_w, target_h);
        }
        if (target_w == 0 || target_h == 0)
        {
            if (aw)
            {
                target_w = aw->pending_width;
                target_h = aw->pending_height;
            }
        }
        if (aw && target_w > 0 && target_h > 0)
        {
            SPECTRA_LOG_DEBUG("resize",
                              "OUT_OF_DATE, recreating: " + std::to_string(target_w) + "x"
                                  + std::to_string(target_h));
            if (profiler)
                profiler->increment_counter("swapchain_recreate");
            aw->swapchain_invalidated = false;
            backend_.recreate_swapchain(target_w, target_h);
            vk->clear_swapchain_dirty();
            if (active_figure)
            {
                active_figure->config_.width  = backend_.swapchain_width();
                active_figure->config_.height = backend_.swapchain_height();
            }
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            ui_ctx.needs_resize = false;
#endif
#ifdef SPECTRA_USE_IMGUI
            if (ui_ctx.imgui_ui)
            {
                ui_ctx.imgui_ui->on_swapchain_recreated(*vk);
            }
#endif
            // UI was built for the pre-recreate layout — skip this frame so the
            // next update tick rebuilds chrome at the correct size.
            return false;
        }
    }

    if (frame_ok)
    {
        // begin_frame() just waited on the in-flight fence, so all GPU
        // work from DELETION_RING_SIZE frames ago is guaranteed complete.
        // Safe to free those deferred resources now.
        renderer_.flush_pending_deletions();

        renderer_.begin_render_pass();

#ifdef SPECTRA_USE_IMGUI
        // Pass selected series to the renderer for GPU-rendered highlight
        if (ui_ctx.imgui_ui)
        {
            const auto& sel = ui_ctx.imgui_ui->selection_context();
            if (sel.type == ui::SelectionType::Series && !sel.selected_series.empty())
            {
                std::vector<const Series*> sel_ptrs;
                sel_ptrs.reserve(sel.selected_series.size());
                for (const auto& e : sel.selected_series)
                {
                    if (e.series)
                        sel_ptrs.push_back(e.series);
                }
                renderer_.set_selected_series(sel_ptrs);
            }
            else
            {
                renderer_.clear_selected_series();
            }
        }
#endif

        if (profiler)
            profiler->begin_stage("cmd_record");
#ifdef SPECTRA_USE_IMGUI
        // Decide whether Vulkan figure content (axes, gridlines, series) should
        // be rendered.  Adapter shells may suppress the ImGui canvas overlay
        // (set_canvas_visible(false)) while still enabling Vulkan rendering
        // via set_render_figure_enabled(true).
        const bool render_canvas = !ui_ctx.imgui_ui || ui_ctx.imgui_ui->is_canvas_visible()
                                   || ui_ctx.imgui_ui->is_render_figure_enabled();
        auto& dock_system = ui_ctx.dock_system;
        if (render_canvas && active_figure && dock_system.is_split())
        {
            uint32_t sw_u       = backend_.swapchain_width();
            uint32_t sh_u       = backend_.swapchain_height();
            auto     sw_f       = static_cast<float>(sw_u);
            auto     sh_f       = static_cast<float>(sh_u);
            auto     pane_infos = dock_system.get_pane_infos();
            for (const auto& pinfo : pane_infos)
            {
                Figure* pfig = registry_.get(pinfo.figure_index);
                if (pfig)
                {
                    // All split-pane figures share the same swapchain and must
                    // use its dimensions for viewport/scissor clipping.  Only
                    // the active figure is updated elsewhere; non-active figures
                    // would retain stale dimensions, causing the rightmost pane
                    // to be clipped (title flicker during zoom).
                    pfig->config_.width  = sw_u;
                    pfig->config_.height = sh_u;
                    // Phase 2 (LT-5): pass FigureViewModel for ViewModel-based rendering
                    FigureViewModel* pane_vm = nullptr;
                    if (ui_ctx.fig_mgr)
                        pane_vm = &ui_ctx.fig_mgr->state(pinfo.figure_index);
                    renderer_.render_figure_content(*pfig, pane_vm);
                    // Flush text per-figure so that each pane's title, tick
                    // labels, and axis labels are drawn immediately after its
                    // geometry.  Batching all panes' text into a single deferred
                    // flush caused the rightmost/bottom pane's title to flicker
                    // during zoom — the later pane's geometry pipeline state
                    // (UBO ring offset, viewport) could interfere with the
                    // combined text draw.
                    renderer_.render_text(sw_f, sh_f);
                }
            }
        }
        else if (render_canvas && active_figure)
        {
            active_figure->config_.width  = backend_.swapchain_width();
            active_figure->config_.height = backend_.swapchain_height();
            // Phase 2 (LT-5): pass FigureViewModel for ViewModel-based rendering
            FigureViewModel* active_vm = nullptr;
            if (ui_ctx.fig_mgr)
                active_vm = &ui_ctx.fig_mgr->active_state();
            renderer_.render_figure_content(*active_figure, active_vm);
        }
#else
        if (active_figure)
        {
            // Phase 2 (LT-5): pass FigureViewModel when available
            FigureViewModel* active_vm = nullptr;
            if (ui_ctx.fig_mgr)
                active_vm = &ui_ctx.fig_mgr->active_state();
            renderer_.render_figure_content(*active_figure, active_vm);
        }
#endif
        if (profiler)
            profiler->end_stage("cmd_record");

            // Invoke scene render callback — allows adapter shells (spectra-ros)
            // to issue GPU draw calls for 3D scene content during the active
            // Vulkan render pass, before ImGui overlays.
#ifdef SPECTRA_USE_IMGUI
        if (ui_ctx.imgui_ui)
        {
            const auto& scene_cb = ui_ctx.imgui_ui->scene_render_callback();
            if (scene_cb)
                scene_cb(renderer_);
        }
#endif

        // Flush Vulkan plot text BEFORE ImGui so that UI overlays (command
        // palette, inspector, menus) render on top of plot labels.
        // The ImGui canvas ##window uses NoBackground so it won't overwrite text.
        // Split view already flushes text per pane above; a second flush is redundant.
#ifdef SPECTRA_USE_IMGUI
        if (render_canvas && !(dock_system.is_split() && active_figure))
#endif
        {
            auto sw = static_cast<float>(backend_.swapchain_width());
            auto sh = static_cast<float>(backend_.swapchain_height());
            renderer_.render_text(sw, sh);
        }

#ifdef SPECTRA_USE_IMGUI
        // Only render ImGui if we have a valid frame (not a retry frame
        // where we already ended the ImGui frame)
        if (ui_ctx.imgui_ui && imgui_frame_started)
        {
            if (profiler)
                profiler->begin_stage("imgui_render");
            ui_ctx.imgui_ui->render(*static_cast<VulkanBackend*>(&backend_));
            if (profiler)
                profiler->end_stage("imgui_render");
        }
#endif

        renderer_.end_render_pass();
        if (profiler)
            profiler->begin_stage("end_frame");
        backend_.end_frame(profiler);
        if (profiler)
            profiler->end_stage("end_frame");

        // Post-present recovery: if vkQueuePresentKHR returned OUT_OF_DATE,
        // the swapchain is permanently invalidated (Vulkan spec). Recreate
        // now so the next frame's begin_frame() starts with a valid swapchain
        // instead of entering the recovery path every frame (infinite loop).
        {
            auto* vk_post = static_cast<VulkanBackend*>(&backend_);
            auto* aw_post = vk_post->active_window();
            if (aw_post && aw_post->swapchain_invalidated)
            {
                aw_post->swapchain_invalidated = false;
                uint32_t rw                    = aw_post->swapchain.extent.width;
                uint32_t rh                    = aw_post->swapchain.extent.height;
                vk_post->query_window_framebuffer_size(*aw_post, rw, rh);
                SPECTRA_LOG_DEBUG("resize",
                                  "Post-present OUT_OF_DATE, recreating: " + std::to_string(rw)
                                      + "x" + std::to_string(rh));
                backend_.recreate_swapchain(rw, rh);
                vk_post->clear_swapchain_dirty();
                if (active_figure)
                {
                    active_figure->config_.width  = backend_.swapchain_width();
                    active_figure->config_.height = backend_.swapchain_height();
                }
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                ui_ctx.needs_resize = false;
#endif
#ifdef SPECTRA_USE_IMGUI
                if (ui_ctx.imgui_ui)
                {
                    ui_ctx.imgui_ui->on_swapchain_recreated(*vk_post);
                }
#endif
            }
        }
    }

    return frame_ok;
}

}   // namespace spectra
