#include "session_runtime.hpp"

#include <spectra/animator.hpp>
#include <spectra/axes.hpp>
#include <spectra/event_bus.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "anim/frame_scheduler.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/commands/command_queue.hpp"
#include <spectra/figure_registry.hpp>
#include "ui/input/input.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <cmath>
#endif

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace spectra
{

SessionRuntime::SessionRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry)
    : backend_(backend), renderer_(renderer), registry_(registry),
      win_rt_(backend, renderer, registry)
{
}

SessionRuntime::~SessionRuntime() = default;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
void SessionRuntime::pump_interactive_frame(FrameScheduler& scheduler,
                                            Animator&       animator,
                                            WindowManager*  window_mgr,
                                            FrameState&     frame_state)
{
    if (!window_mgr || interactive_pump_depth_ > 0)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_interactive_pump_ != std::chrono::steady_clock::time_point{}
        && (now - last_interactive_pump_) < std::chrono::milliseconds(8))
    {
        return;
    }
    last_interactive_pump_ = now;

    interactive_pump_depth_++;

    scheduler.begin_frame();
    animator.evaluate(scheduler.elapsed_seconds());
    const float animation_dt = scheduler.dt();
    redraw_tracker_.mark_dirty("interactive");

    auto* vk = static_cast<VulkanBackend*>(&backend_);
    vk->advance_deferred_deletion();

    for (auto* wctx : window_mgr->windows())
    {
        if (!wctx || wctx->should_close || !wctx->ui_ctx || wctx->is_preview
            || wctx->panel_draw_callback)
        {
            continue;
        }

        uint32_t fb_w = 0;
        uint32_t fb_h = 0;
        if (!vk->query_window_framebuffer_size(*wctx, fb_w, fb_h) || fb_w == 0 || fb_h == 0)
            continue;

        if (fb_w != wctx->swapchain.extent.width || fb_h != wctx->swapchain.extent.height)
        {
            wctx->needs_resize                  = true;
            wctx->pending_width                 = fb_w;
            wctx->pending_height                = fb_h;
            wctx->resize_time                   = now;
            wctx->ui_ctx->needs_resize          = true;
            wctx->ui_ctx->new_width             = fb_w;
            wctx->ui_ctx->new_height            = fb_h;
            wctx->ui_ctx->resize_requested_time = now;
        }
        else if (wctx->needs_resize)
        {
            wctx->ui_ctx->needs_resize          = true;
            wctx->ui_ctx->new_width             = wctx->pending_width;
            wctx->ui_ctx->new_height            = wctx->pending_height;
            wctx->ui_ctx->resize_requested_time = wctx->resize_time;
            wctx->needs_resize                  = false;
        }

        vk->set_active_window(wctx);
        if (wctx->imgui_context)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));

        FrameState win_fs;
        win_fs.active_figure_id = wctx->active_figure_id;
        win_fs.active_figure    = registry_.get(win_fs.active_figure_id);
        if (win_fs.active_figure)
            win_fs.has_animation = win_fs.active_figure->has_animation();

        win_rt_.update(*wctx->ui_ctx,
                       win_fs,
                       scheduler,
                       /*allow_animation_tick=*/true,
                       animation_dt,
                       nullptr,
                       window_mgr);

        if (wctx->ui_ctx->fig_mgr)
            wctx->assigned_figures = wctx->ui_ctx->fig_mgr->figure_ids();

        const bool rendered = win_rt_.render(*wctx->ui_ctx, win_fs, nullptr);
        if (rendered && window_mgr->windows().size() > 1)
            vkQueueWaitIdle(vk->graphics_queue());

        wctx->active_figure_id                    = win_fs.active_figure_id;
        wctx->ui_ctx->per_window_active_figure    = win_fs.active_figure;
        wctx->ui_ctx->per_window_active_figure_id = win_fs.active_figure_id;
        wctx->ui_ctx->topics_panel.set_target_figure_id(win_fs.active_figure_id);

        if (!window_mgr->windows().empty() && wctx == window_mgr->windows()[0])
            frame_state = win_fs;
    }

    interactive_pump_depth_--;
}
#endif

void SessionRuntime::queue_detach(PendingDetach pd)
{
    pending_detaches_.push_back(std::move(pd));
}

void SessionRuntime::queue_move(PendingMove pm)
{
    pending_moves_.push_back(pm);
}

FrameState SessionRuntime::tick(FrameScheduler&  scheduler,
                                Animator&        animator,
                                CommandQueue&    cmd_queue,
                                bool             headless,
                                WindowUIContext* headless_ui_ctx,
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                                WindowManager* window_mgr,
#endif
                                FrameState& frame_state)
{
    newly_created_window_ids_.clear();

    const bool vsync_mode              = scheduler.mode() == FrameScheduler::Mode::VSync;
    bool       scheduled_animation     = false;
    bool       animation_due_tick      = false;
    bool       allow_animation_tick    = true;
    bool       should_render_tick      = true;
    bool       has_any_animation       = false;
    bool       has_ui_chrome_animation = false;
    float      max_animation_fps       = 0.0f;
    float      animation_dt            = 0.0f;

    profiler_.begin_frame();

    try
    {
        scheduler.begin_frame();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_CRITICAL("main_loop", "Frame scheduler failed: " + std::string(e.what()));
        running_ = false;
        return frame_state;
    }

    // Drain command queue (apply app-thread mutations)
    SPECTRA_PROFILE_BEGIN(profiler_, "cmd_drain");
    size_t commands_processed = cmd_queue.drain();
    SPECTRA_PROFILE_END(profiler_, "cmd_drain");
    if (commands_processed > 0)
    {
        redraw_tracker_.mark_dirty("commands");
        SPECTRA_LOG_TRACE("main_loop",
                          "Processed " + std::to_string(commands_processed) + " commands");
    }

    // Drain deferred events from background threads.
    if (event_system_)
        event_system_->drain_all_deferred();

    // Commit pending thread-safe series data from background threads.
    SPECTRA_PROFILE_BEGIN(profiler_, "series_commit");
    commit_thread_safe_series();
    SPECTRA_PROFILE_END(profiler_, "series_commit");

    // Evaluate keyframe animations
    SPECTRA_PROFILE_BEGIN(profiler_, "animator");
    animator.evaluate(scheduler.elapsed_seconds());
    SPECTRA_PROFILE_END(profiler_, "animator");

    scheduled_animation = vsync_mode && animation_tick_gate_.active();
    if (scheduled_animation)
        animation_tick_gate_.accumulate_dt(scheduler.dt());

    const auto frame_now = AnimationTickGate::Clock::now();
    animation_due_tick   = scheduled_animation && animation_tick_gate_.should_tick(frame_now);
    allow_animation_tick = (not vsync_mode) || (not scheduled_animation) || animation_due_tick;
    should_render_tick   = headless || (not redraw_tracker_.is_idle()) || animation_due_tick;

    animation_dt = scheduler.dt();
    if (vsync_mode && scheduled_animation)
        animation_dt = animation_due_tick ? animation_tick_gate_.consume_accumulated_dt() : 0.0f;

        // ── Unified window update + render loop ───────────────────────
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (window_mgr)
    {
        auto* vk = static_cast<VulkanBackend*>(&backend_);

        // Advance the deferred-deletion frame counter once per tick
        // (not per window) so buffers survive the correct number of frames.
        vk->advance_deferred_deletion();

        for (auto* wctx : window_mgr->windows())
        {
            if (wctx->should_close)
                continue;

            // Skip windows created this frame
            if (std::find(newly_created_window_ids_.begin(),
                          newly_created_window_ids_.end(),
                          wctx->id)
                != newly_created_window_ids_.end())
                continue;

            // Handle minimized window (0x0 framebuffer): skip until restored
            uint32_t fb_w = 0;
            uint32_t fb_h = 0;
            if (!vk->query_window_framebuffer_size(*wctx, fb_w, fb_h))
                continue;

            if (!wctx->ui_ctx)
            {
                // Legacy window (no ImGui, figure-only) — skip for now,
                // caller handles via render_secondary_window.
                continue;
            }

            // Set active window for Vulkan operations
            vk->set_active_window(wctx);

            // Switch to this window's ImGui context
            if (wctx->imgui_context)
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));

            // Sync WindowContext resize state → UIContext resize fields
            if (wctx->needs_resize)
            {
                redraw_tracker_.mark_dirty("resize");
                wctx->ui_ctx->needs_resize          = true;
                wctx->ui_ctx->new_width             = wctx->pending_width;
                wctx->ui_ctx->new_height            = wctx->pending_height;
                wctx->ui_ctx->resize_requested_time = wctx->resize_time;
                wctx->needs_resize                  = false;
            }

            // Catch resize drift between GLFW callbacks: if the live framebuffer
            // no longer matches the swapchain, queue recreation this frame.
            if (fb_w > 0 && fb_h > 0
                && (fb_w != wctx->swapchain.extent.width || fb_h != wctx->swapchain.extent.height))
            {
                redraw_tracker_.mark_dirty("resize");
                wctx->ui_ctx->needs_resize          = true;
                wctx->ui_ctx->new_width             = fb_w;
                wctx->ui_ctx->new_height            = fb_h;
                wctx->ui_ctx->resize_requested_time = std::chrono::steady_clock::now();
            }

            // Preview windows: render the preview card with actual figure data
            if (wctx->is_preview)
            {
                if (not should_render_tick)
                    continue;

                if (wctx->ui_ctx && wctx->ui_ctx->imgui_ui)
                {
                    // Find the figure being dragged by checking all windows' drag controllers
                    Figure* dragged_fig = nullptr;
                    for (auto* w : window_mgr->windows())
                    {
                        if (w->is_preview || !w->ui_ctx || !w->ui_ctx->imgui_ui)
                            continue;
                        auto* tdc = w->ui_ctx->imgui_ui->tab_drag_controller();
                        if (tdc && tdc->is_active())
                        {
                            dragged_fig = registry_.get(tdc->dragged_figure());
                            break;
                        }
                    }

                    wctx->ui_ctx->imgui_ui->new_frame();
                    wctx->ui_ctx->imgui_ui->build_preview_ui(wctx->title, dragged_fig);

                    bool frame_ok = vk->begin_frame();
                    if (frame_ok)
                    {
                        vk->begin_render_pass(Color{0.0f, 0.0f, 0.0f, 0.0f});
                        wctx->ui_ctx->imgui_ui->render(*vk);
                        vk->end_render_pass();
                        vk->end_frame();

                        // Wait for GPU before the next window overwrites shared buffers.
                        if (window_mgr->windows().size() > 1)
                            vkQueueWaitIdle(vk->graphics_queue());
                    }
                    else
                    {
                        ImGui::EndFrame();
                    }
                }
                continue;
            }

            // ── Panel window: lightweight ImGui frame with panel callback ──
            if (wctx->panel_draw_callback)
            {
                if (not should_render_tick)
                    continue;

                if (wctx->ui_ctx && wctx->ui_ctx->imgui_ui)
                {
                    wctx->ui_ctx->imgui_ui->new_frame();

                    ImGuiViewport* pvp = ImGui::GetMainViewport();

                    // ── Custom title bar ──
                    constexpr float kTitleBarH = 32.0f;
                    constexpr float kBtnSize   = 28.0f;
                    constexpr float kCornerR   = 8.0f;
                    constexpr float kBorderW   = 1.0f;

                    const ImU32 col_title_bg    = IM_COL32(30, 30, 34, 255);      // Dark surface
                    const ImU32 col_title_text  = IM_COL32(210, 210, 215, 255);   // Soft white text
                    const ImU32 col_close_hover = IM_COL32(220, 60, 60, 200);     // Red close hover
                    const ImU32 col_close_icon =
                        IM_COL32(180, 180, 185, 255);                      // Close icon normal
                    const ImU32 col_border  = IM_COL32(55, 55, 62, 255);   // Subtle border
                    const ImU32 col_body_bg = IM_COL32(22, 22, 26, 255);   // Body background

                    ImVec2 wp = pvp->WorkPos;
                    ImVec2 ws = pvp->WorkSize;

                    // Draw full-window background with rounded top corners
                    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
                    bg_dl->AddRectFilled(wp,
                                         ImVec2(wp.x + ws.x, wp.y + ws.y),
                                         col_body_bg,
                                         kCornerR,
                                         ImDrawFlags_RoundCornersTop);

                    // Title bar background
                    ImVec2 tb_min = wp;
                    ImVec2 tb_max = ImVec2(wp.x + ws.x, wp.y + kTitleBarH);
                    bg_dl->AddRectFilled(tb_min,
                                         tb_max,
                                         col_title_bg,
                                         kCornerR,
                                         ImDrawFlags_RoundCornersTop);

                    // Subtle bottom border on title bar
                    bg_dl->AddLine(ImVec2(tb_min.x, tb_max.y),
                                   ImVec2(tb_max.x, tb_max.y),
                                   col_border,
                                   kBorderW);

                    // Title text (centered vertically in title bar)
                    {
                        const char* title_str = wctx->title.c_str();
                        ImVec2      text_size = ImGui::CalcTextSize(title_str);
                        float       text_x    = wp.x + 12.0f;
                        float       text_y    = wp.y + (kTitleBarH - text_size.y) * 0.5f;
                        bg_dl->AddText(ImVec2(text_x, text_y), col_title_text, title_str);
                    }

                    // ── Close button ──
                    float  close_x   = wp.x + ws.x - kBtnSize - 4.0f;
                    float  close_y   = wp.y + (kTitleBarH - kBtnSize) * 0.5f;
                    ImVec2 close_min = ImVec2(close_x, close_y);
                    ImVec2 close_max = ImVec2(close_x + kBtnSize, close_y + kBtnSize);

                    ImVec2 mouse_pos = ImGui::GetMousePos();
                    bool   close_hovered =
                        (mouse_pos.x >= close_min.x && mouse_pos.x <= close_max.x
                         && mouse_pos.y >= close_min.y && mouse_pos.y <= close_max.y);

                    if (close_hovered)
                    {
                        bg_dl->AddRectFilled(close_min, close_max, col_close_hover, 6.0f);
                    }

                    // Draw X icon
                    {
                        float cx    = close_x + kBtnSize * 0.5f;
                        float cy    = close_y + kBtnSize * 0.5f;
                        float half  = 5.0f;
                        ImU32 x_col = close_hovered ? IM_COL32(255, 255, 255, 255) : col_close_icon;
                        bg_dl->AddLine(ImVec2(cx - half, cy - half),
                                       ImVec2(cx + half, cy + half),
                                       x_col,
                                       1.5f);
                        bg_dl->AddLine(ImVec2(cx + half, cy - half),
                                       ImVec2(cx - half, cy + half),
                                       x_col,
                                       1.5f);
                    }

                    // Handle close click
                    if (close_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        wctx->should_close = true;
                    }

                    // ── Title bar drag to move ──
                    bool title_hovered =
                        (mouse_pos.x >= tb_min.x && mouse_pos.x <= tb_max.x
                         && mouse_pos.y >= tb_min.y && mouse_pos.y <= tb_max.y && !close_hovered);

    #ifdef SPECTRA_USE_GLFW
                    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    #endif
                    if (title_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        wctx->titlebar_dragging = true;
    #ifdef SPECTRA_USE_GLFW
                        double cx_d = NAN;
                        double cy_d = NAN;
                        glfwGetCursorPos(glfw_win, &cx_d, &cy_d);
                        wctx->drag_offset_x = cx_d;
                        wctx->drag_offset_y = cy_d;
    #elif defined(SPECTRA_USE_SDL3)
                        float cx_f, cy_f;
                        SDL_GetMouseState(&cx_f, &cy_f);
                        wctx->drag_offset_x = static_cast<double>(cx_f);
                        wctx->drag_offset_y = static_cast<double>(cy_f);
    #endif
                    }
                    if (wctx->titlebar_dragging)
                    {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        {
    #ifdef SPECTRA_USE_GLFW
                            double sx = NAN;
                            double sy = NAN;
                            glfwGetCursorPos(glfw_win, &sx, &sy);
                            int wx = 0;
                            int wy = 0;
                            glfwGetWindowPos(glfw_win, &wx, &wy);
                            int new_x = wx + static_cast<int>(sx - wctx->drag_offset_x);
                            int new_y = wy + static_cast<int>(sy - wctx->drag_offset_y);
                            glfwSetWindowPos(glfw_win, new_x, new_y);
    #elif defined(SPECTRA_USE_SDL3)
                            float sx, sy;
                            SDL_GetMouseState(&sx, &sy);
                            int wx = 0, wy = 0;
                            SDL_GetWindowPosition(static_cast<SDL_Window*>(wctx->glfw_window),
                                                  &wx,
                                                  &wy);
                            int new_x = wx + static_cast<int>(sx - wctx->drag_offset_x);
                            int new_y = wy + static_cast<int>(sy - wctx->drag_offset_y);
                            SDL_SetWindowPosition(static_cast<SDL_Window*>(wctx->glfw_window),
                                                  new_x,
                                                  new_y);
    #endif
                        }
                        else
                        {
                            wctx->titlebar_dragging = false;
                        }
                    }

                    // ── Panel content area (below title bar) ──
                    ImVec2 content_pos  = ImVec2(wp.x, wp.y + kTitleBarH);
                    ImVec2 content_size = ImVec2(ws.x, ws.y - kTitleBarH);
                    ImGui::SetNextWindowPos(content_pos);
                    ImGui::SetNextWindowSize(content_size);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

                    ImGuiWindowFlags panel_flags =
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoDocking;

                    if (ImGui::Begin("##PanelContent", nullptr, panel_flags))
                        wctx->panel_draw_callback();
                    ImGui::End();
                    ImGui::PopStyleVar(3);

                    bool frame_ok = vk->begin_frame();
                    if (frame_ok)
                    {
                        vk->begin_render_pass(Color{0.0f, 0.0f, 0.0f, 1.0f});
                        wctx->ui_ctx->imgui_ui->render(*vk);
                        vk->end_render_pass();
                        vk->end_frame();

                        if (window_mgr->windows().size() > 1)
                            vkQueueWaitIdle(vk->graphics_queue());
                    }
                    else
                    {
                        ImGui::EndFrame();
                    }
                }
                continue;
            }

            // Build per-window FrameState
            FrameState win_fs;
            win_fs.active_figure_id = wctx->active_figure_id;
            win_fs.active_figure    = registry_.get(win_fs.active_figure_id);
            // active_figure may be null (empty-start mode) — still render
            // ImGui chrome (menu bar, empty canvas) so the window isn't black.
            if (win_fs.active_figure)
            {
                win_fs.has_animation = win_fs.active_figure->has_animation();
                if (win_fs.has_animation)
                {
                    has_any_animation = true;
                    max_animation_fps =
                        std::max(max_animation_fps, win_fs.active_figure->anim_.fps);
                    if (not vsync_mode)
                        redraw_tracker_.mark_dirty("animation");
                }
            }

    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            // Mark dirty when the input handler is actively dragging (pan/zoom/box)
            // or when a zoom/inertia animation is still running.
            if (wctx->ui_ctx)
            {
                auto& ih = wctx->ui_ctx->input_handler;
                if (ih.mode() == InteractionMode::Dragging)
                    redraw_tracker_.mark_dirty("drag");
                if (ih.has_active_animations())
                    redraw_tracker_.mark_dirty("input_anim");
            }
    #endif

            // Also check split-view pane figures — animation must continue
            // even when the active tab is not the animated one.
            if (win_fs.active_figure && wctx->ui_ctx->dock_system.is_split())
            {
                for (const auto& pinfo : wctx->ui_ctx->dock_system.get_pane_infos())
                {
                    Figure* pfig = registry_.get(pinfo.figure_index);
                    if (pfig && pfig->has_animation())
                    {
                        win_fs.has_animation = true;
                        has_any_animation    = true;
                        max_animation_fps    = std::max(max_animation_fps, pfig->anim_.fps);
                    }
                }
            }

            if (not should_render_tick)
            {
    #ifdef SPECTRA_USE_IMGUI
                if (wctx->ui_ctx && wctx->ui_ctx->imgui_ui
                    && wctx->ui_ctx->imgui_ui->has_active_chrome_animations())
                {
                    has_ui_chrome_animation = true;
                    redraw_tracker_.mark_dirty("ui_chrome_anim");
                }
                else
    #endif
                {
                    continue;
                }
            }
            else
            {
    #ifdef SPECTRA_USE_IMGUI
                if (wctx->ui_ctx && wctx->ui_ctx->imgui_ui
                    && wctx->ui_ctx->imgui_ui->has_active_chrome_animations())
                {
                    has_ui_chrome_animation = true;
                }
    #endif
            }

            {
                SPECTRA_PROFILE_SCOPE(profiler_, "win_update");
                win_rt_.update(*wctx->ui_ctx,
                               win_fs,
                               scheduler,
                               allow_animation_tick,
                               animation_dt,
                               &profiler_,
                               window_mgr);
            }

            // Sync WindowContext::assigned_figures with FigureManager.
            // process_pending() (called inside update) may create, close, or
            // duplicate figures — keep assigned_figures in lockstep so that
            // detach/move operations can find the figure's owning window.
            if (wctx->ui_ctx->fig_mgr)
            {
                wctx->assigned_figures = wctx->ui_ctx->fig_mgr->figure_ids();
            }

            bool rendered = false;
            {
                SPECTRA_PROFILE_SCOPE(profiler_, "win_render");
                rendered = win_rt_.render(*wctx->ui_ctx, win_fs, &profiler_);
            }
            if (!rendered)
            {
                redraw_tracker_.mark_dirty("swapchain_recovery");
            }

            // Wait for GPU to finish this window's work before rendering the
            // next window.  The Renderer uses shared host-visible buffers
            // (frame UBO, text vertex buffer, overlay buffers) that would be
            // overwritten by the next window while the GPU is still reading
            // them, causing cross-window content blinking / swapping.
            // Skip the wait if no GPU work was submitted (frame skipped due
            // to fence/acquire timeout) — nothing to synchronize.
            if (rendered && window_mgr->windows().size() > 1)
                vkQueueWaitIdle(vk->graphics_queue());

            // Sync active figure back to WindowContext so the next frame
            // reads the correct figure (tab switch via FigureManager updates
            // win_fs.active_figure_id but nothing wrote it back to wctx).
            wctx->active_figure_id = win_fs.active_figure_id;

            // Keep per-window active figure in sync for command lambdas
            // (clipboard, view commands registered via register_standard_commands).
            wctx->ui_ctx->per_window_active_figure    = win_fs.active_figure;
            wctx->ui_ctx->per_window_active_figure_id = win_fs.active_figure_id;

            // Let the Topics panel know which figure the "Plot" button targets.
            wctx->ui_ctx->topics_panel.set_target_figure_id(win_fs.active_figure_id);

            // Sync back to the app-level frame_state for the initial window
            if (wctx == window_mgr->windows()[0])
            {
                frame_state = win_fs;
            }
        }
    }
#endif

    // Headless path (no GLFW, no WindowManager)
    if (headless && headless_ui_ctx && should_render_tick)
    {
        win_rt_.update(*headless_ui_ctx,
                       frame_state,
                       scheduler,
                       allow_animation_tick,
                       animation_dt,
                       &profiler_
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                       ,
                       nullptr
#endif
        );
        win_rt_.render(*headless_ui_ctx, frame_state, &profiler_);
    }

    // Simple render path for non-Vulkan backends (e.g. WebGPU) that don't
    // use WindowManager.  No ImGui, no multi-window — just render the
    // active figure directly.  Always render (no smart redraw tracking
    // without WindowManager to drive the dirty state).
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (!headless && !window_mgr && frame_state.active_figure)
    {
        auto* fig           = frame_state.active_figure;
        fig->config_.width  = backend_.swapchain_width();
        fig->config_.height = backend_.swapchain_height();
        fig->compute_layout();

        if (backend_.begin_frame())
        {
            renderer_.begin_render_pass();
            renderer_.render_figure_content(*fig);
            auto sw = static_cast<float>(backend_.swapchain_width());
            auto sh = static_cast<float>(backend_.swapchain_height());
            renderer_.render_text(sw, sh);
            renderer_.end_render_pass();
            backend_.end_frame();
        }
    }
#endif

    // ── Process deferred preview window create/destroy ─────────
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (window_mgr)
    {
        window_mgr->process_deferred_preview();
    }
#endif

    // ── Process deferred detach requests ─────────────────────────
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (window_mgr && !pending_detaches_.empty())
    {
        for (auto& pd : pending_detaches_)
        {
            // Find the window that actually owns this figure
            WindowContext* src_wctx = nullptr;
            for (auto* w : window_mgr->windows())
            {
                if (!w)
                    continue;
                for (auto fid : w->assigned_figures)
                {
                    if (fid == pd.figure_id)
                    {
                        src_wctx = w;
                        break;
                    }
                }
                if (src_wctx)
                    break;
            }
            if (!src_wctx || !src_wctx->ui_ctx || !src_wctx->ui_ctx->fig_mgr)
                continue;
            auto* src_fm = src_wctx->ui_ctx->fig_mgr;

            FigureState detached_state = src_fm->remove_figure(pd.figure_id);

            // Remove figure from dock system split panes
            auto& src_dock_d = src_wctx->ui_ctx->dock_system;
            if (src_dock_d.is_split())
            {
                auto* pane = src_dock_d.split_view().pane_for_figure(pd.figure_id);
                if (pane)
                {
                    if (pane->figure_count() <= 1)
                        src_dock_d.close_split(pd.figure_id);
                    else
                        pane->remove_figure(pd.figure_id);
                }
            }

            auto& pf = src_wctx->assigned_figures;
            pf.erase(std::remove(pf.begin(), pf.end(), pd.figure_id), pf.end());
            if (src_wctx->active_figure_id == pd.figure_id)
                src_wctx->active_figure_id = pf.empty() ? INVALID_FIGURE_ID : pf.front();

            // Hide (don't destroy) empty windows — full destroy during the
            // main loop can crash when the primary ImGui context is torn down.
            if (pf.empty() && src_wctx->glfw_window)
            {
    #ifdef SPECTRA_USE_GLFW
                glfwHideWindow(static_cast<GLFWwindow*>(src_wctx->glfw_window));
    #elif defined(SPECTRA_USE_SDL3)
                SDL_HideWindow(static_cast<SDL_Window*>(src_wctx->glfw_window));
    #endif
                src_wctx->should_close = true;
            }

            auto* new_wctx = window_mgr->detach_figure(pd.figure_id,
                                                       pd.width,
                                                       pd.height,
                                                       pd.title,
                                                       pd.screen_x,
                                                       pd.screen_y);

            if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
            {
                auto* new_fm                = new_wctx->ui_ctx->fig_mgr;
                new_fm->state(pd.figure_id) = std::move(detached_state);
                std::string correct_title   = new_fm->get_title(pd.figure_id);
                if (new_fm->tab_bar())
                    new_fm->tab_bar()->set_tab_title(0, correct_title);
            }

            frame_state.active_figure_id = src_fm->active_index();
            frame_state.active_figure    = registry_.get(frame_state.active_figure_id);

            if (new_wctx)
                newly_created_window_ids_.push_back(new_wctx->id);
        }
        pending_detaches_.clear();
    }

    // ── Process deferred cross-window moves ──────────────────────
    if (window_mgr && !pending_moves_.empty())
    {
        for (auto& pm : pending_moves_)
        {
            SPECTRA_LOG_DEBUG("window_manager",
                              "Processing cross-window move: fig=" + std::to_string(pm.figure_id)
                                  + " target_wid=" + std::to_string(pm.target_window_id)
                                  + " drop_zone=" + std::to_string(pm.drop_zone));

            // Find source window (the one that has this figure)
            WindowContext* src_wctx = nullptr;
            WindowContext* dst_wctx = nullptr;
            for (auto* w : window_mgr->windows())
            {
                if (!w)
                    continue;
                if (w->id == pm.target_window_id)
                    dst_wctx = w;
                for (auto fid : w->assigned_figures)
                {
                    if (fid == pm.figure_id)
                        src_wctx = w;
                }
            }

            if (!src_wctx || !dst_wctx || src_wctx == dst_wctx)
            {
                SPECTRA_LOG_DEBUG("window_manager",
                                  "Skipping cross-window move: fig=" + std::to_string(pm.figure_id)
                                      + " src_valid=" + std::to_string(src_wctx != nullptr)
                                      + " dst_valid=" + std::to_string(dst_wctx != nullptr)
                                      + " same_window=" + std::to_string(src_wctx == dst_wctx));
                continue;
            }
            if (!src_wctx->ui_ctx || !src_wctx->ui_ctx->fig_mgr)
                continue;
            if (!dst_wctx->ui_ctx || !dst_wctx->ui_ctx->fig_mgr)
                continue;

            auto* src_fm = src_wctx->ui_ctx->fig_mgr;
            auto* dst_fm = dst_wctx->ui_ctx->fig_mgr;

            // Remove from source
            FigureState moved_state = src_fm->remove_figure(pm.figure_id);
            auto&       src_dock    = src_wctx->ui_ctx->dock_system;
            if (src_dock.is_split())
            {
                auto* pane = src_dock.split_view().pane_for_figure(pm.figure_id);
                if (pane)
                {
                    if (pane->figure_count() <= 1)
                        src_dock.close_split(pm.figure_id);
                    else
                        pane->remove_figure(pm.figure_id);
                }
            }

            auto& spf = src_wctx->assigned_figures;
            spf.erase(std::remove(spf.begin(), spf.end(), pm.figure_id), spf.end());
            if (src_wctx->active_figure_id == pm.figure_id)
                src_wctx->active_figure_id = spf.empty() ? INVALID_FIGURE_ID : spf.front();

            // Hide (don't destroy) empty windows — full destroy during the
            // main loop can crash when the primary ImGui context is torn down.
            if (spf.empty() && src_wctx->glfw_window)
            {
    #ifdef SPECTRA_USE_GLFW
                glfwHideWindow(static_cast<GLFWwindow*>(src_wctx->glfw_window));
    #elif defined(SPECTRA_USE_SDL3)
                SDL_HideWindow(static_cast<SDL_Window*>(src_wctx->glfw_window));
    #endif
                src_wctx->should_close = true;
            }

            // Add figure to the dock system so it appears in split views.
            // IMPORTANT: save the dock active figure BEFORE add_figure(),
            // because add_figure → switch_to → tab bar callback will change
            // active_figure_index to the new figure (which isn't in any pane
            // yet), making active_pane() return nullptr.
            auto&    dst_dock         = dst_wctx->ui_ctx->dock_system;
            FigureId prev_dock_active = dst_dock.active_figure_index();

            // Add to destination
            dst_fm->add_figure(pm.figure_id, std::move(moved_state));
            dst_fm->queue_switch(pm.figure_id);
            dst_wctx->assigned_figures.push_back(pm.figure_id);
            dst_wctx->active_figure_id = pm.figure_id;

            // drop_zone: 0=None/Center(add tab), 1=Left, 2=Right, 3=Top, 4=Bottom, 5=Center
            // Use the figure in the pane the cursor was actually over (target_figure_id)
            // rather than the dock's globally active figure (prev_dock_active).
            // Fall back to prev_dock_active if target_figure_id wasn't set.
            FigureId anchor_fig =
                (pm.target_figure_id != INVALID_FIGURE_ID) ? pm.target_figure_id : prev_dock_active;

            bool did_split = false;
            if (pm.drop_zone >= 1 && pm.drop_zone <= 4 && anchor_fig != INVALID_FIGURE_ID)
            {
                // Directional split: split the pane containing anchor_fig
                // and place the NEW figure (pm.figure_id) in the new pane.
                SplitPane* split_result = nullptr;
                switch (pm.drop_zone)
                {
                    case 1:   // Left — split horizontally, new figure goes left
                        split_result = dst_dock.split_figure_right(anchor_fig, pm.figure_id, 0.5f);
                        if (split_result && split_result->parent())
                        {
                            auto* parent = split_result->parent();
                            if (parent->first() && parent->second())
                                parent->first()->swap_contents(*parent->second());
                        }
                        break;
                    case 2:   // Right — split horizontally, new figure goes right
                        split_result = dst_dock.split_figure_right(anchor_fig, pm.figure_id, 0.5f);
                        break;
                    case 3:   // Top — split vertically, new figure goes top
                        split_result = dst_dock.split_figure_down(anchor_fig, pm.figure_id, 0.5f);
                        if (split_result && split_result->parent())
                        {
                            auto* parent = split_result->parent();
                            if (parent->first() && parent->second())
                                parent->first()->swap_contents(*parent->second());
                        }
                        break;
                    case 4:   // Bottom — split vertically, new figure goes bottom
                        split_result = dst_dock.split_figure_down(anchor_fig, pm.figure_id, 0.5f);
                        break;
                }
                if (split_result)
                {
                    did_split = true;
                    dst_dock.set_active_figure_index(pm.figure_id);
                }
            }

            if (!did_split)
            {
                // Center / None: add as a tab in the pane the cursor was over
                if (dst_dock.is_split())
                {
                    auto* target_pane = dst_dock.split_view().pane_for_figure(anchor_fig);
                    if (!target_pane)
                    {
                        auto all = dst_dock.split_view().all_panes();
                        if (!all.empty())
                            target_pane = all.front();
                    }
                    if (target_pane && target_pane->is_leaf())
                        target_pane->add_figure(pm.figure_id);
                    dst_dock.set_active_figure_index(pm.figure_id);
                }
            }

            SPECTRA_LOG_DEBUG("window_manager",
                              "Cross-window move complete: fig=" + std::to_string(pm.figure_id)
                                  + " " + std::to_string(src_wctx->id) + "->"
                                  + std::to_string(dst_wctx->id)
                                  + " src_figs=" + std::to_string(src_wctx->assigned_figures.size())
                                  + " dst_figs=" + std::to_string(dst_wctx->assigned_figures.size())
                                  + " split=" + std::to_string(dst_dock.is_split() ? 1 : 0));
        }
        pending_moves_.clear();
    }
#endif

    // Only run the scheduler's sleep/spin-wait pacing when actively rendering.
    // When idle, glfwWaitEventsTimeout() handles pacing instead — the scheduler's
    // spin-wait would otherwise burn CPU to hit target FPS on frames that don't
    // need rendering (e.g. mouse hover wakeups).
    if (!redraw_tracker_.is_idle())
        scheduler.end_frame();
    profiler_.end_frame();

    if (vsync_mode)
    {
        if (should_render_tick)
        {
            if (not has_any_animation)
            {
                animation_tick_gate_.clear();
            }
            else if (allow_animation_tick)
            {
                float effective_fps =
                    max_animation_fps > 0.0f ? max_animation_fps : scheduler.target_fps();
                animation_tick_gate_.schedule_next(AnimationTickGate::Clock::now(), effective_fps);
            }
        }
    }
    else
    {
        animation_tick_gate_.clear();
    }

    // ── Poll events + check exit ─────────────────────────────────
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (window_mgr)
    {
        // Only check for sustained states that don't generate fresh OS events.
        // Mouse movement / scroll / clicks already wake glfwWaitEventsTimeout()
        // as OS cursor-pos / scroll / button events — checking io.MouseDelta or
        // io.MouseDown here would re-mark dirty every rendered frame and cause
        // a 100% CPU spin loop while the mouse is moving.
        ImGuiIO& io = ImGui::GetIO();

        // Active ImGui widget sustained across frames (slider drag, text input).
        if (ImGui::IsAnyItemActive() || io.WantTextInput)
            redraw_tracker_.mark_dirty("imgui_active");

        // Queued keyboard characters (typing in the command bar, etc.).
        if (io.InputQueueCharacters.Size > 0)
            redraw_tracker_.mark_dirty("keyboard");

        SPECTRA_PROFILE_BEGIN(profiler_, "poll_events");
        const bool keep_animating = has_any_animation || has_ui_chrome_animation;
        if (!keep_animating && redraw_tracker_.is_idle())
        {
            double wait_timeout_s = 0.1;
            if (vsync_mode && scheduled_animation)
            {
                wait_timeout_s =
                    animation_tick_gate_.wait_timeout_seconds(AnimationTickGate::Clock::now(), 0.1);
            }
            window_mgr->wait_events_timeout(wait_timeout_s);
        }
        else if (keep_animating && redraw_tracker_.is_idle())
        {
            // Poll so Win32 WM_ENTERSIZEMOVE keeps delivering refresh events,
            // then sleep until the next animation tick (avoids a busy spin).
            window_mgr->poll_events();
            const double wait_s = vsync_mode ? animation_tick_gate_.wait_timeout_seconds(
                                                   AnimationTickGate::Clock::now(),
                                                   0.1)
                                             : 0.0;
            if (wait_s > 0.0)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(wait_s)));
            }
        }
        else
        {
            window_mgr->poll_events();
        }
        SPECTRA_PROFILE_END(profiler_, "poll_events");
        window_mgr->process_pending_closes();

        if (!window_mgr->any_window_open())
        {
            SPECTRA_LOG_INFO("main_loop", "All windows closed, exiting loop");
            running_ = false;
        }
    }
#endif

    // Headless without animation: single frame
    if (headless && frame_state.active_figure && !frame_state.has_animation)
    {
        SPECTRA_LOG_INFO("main_loop", "Headless single frame mode, exiting loop");
        running_ = false;
    }

    // Refresh active_figure pointer in case it was destroyed during process_pending_closes
    frame_state.active_figure = registry_.get(frame_state.active_figure_id);

    // Advance the redraw tracker — decrement grace counter.
    redraw_tracker_.end_frame();

    // Periodic resource utilization log (CPU%, RAM, frame timing).
    double frame_ms = static_cast<double>(scheduler.dt()) * 1000.0;
    resource_monitor_.tick(frame_ms);

    return frame_state;
}

void SessionRuntime::commit_thread_safe_series()
{
    bool any_committed = false;

    for (auto id : registry_.all_ids())
    {
        Figure* fig = registry_.get(id);
        if (!fig)
            continue;

        fig->for_each_axes(
            [&](AxesBase* ab)
            {
                bool axes_committed = false;
                for (auto& series_ptr : ab->series())
                {
                    if (series_ptr->is_thread_safe() && series_ptr->commit_pending())
                    {
                        any_committed  = true;
                        axes_committed = true;
                    }
                }
                // Drive continuous topic-subscribed auto-zoom for 2D axes.
                // The flag is enabled by the Topics drag/drop subscribe path
                // and self-disables when the user pans or zooms.
                if (axes_committed)
                {
                    if (auto* ax2d = dynamic_cast<Axes*>(ab); ax2d && ax2d->topic_auto_zoom())
                        ax2d->topic_auto_zoom_tick();
                }
            });
    }

    if (any_committed)
        redraw_tracker_.mark_dirty("series_commit");
}

}   // namespace spectra
