// Figure operations: detach, move, preview windows, cross-window drop zones.
// Split from window_manager.cpp — see QW-5 in ARCHITECTURE_REVIEW_V2.md.

#include "window_manager.hpp"

#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>

#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/figures/figure_manager.hpp"
    #include <spectra/figure_registry.hpp>
    #include "ui/imgui/imgui_integration.hpp"
#endif

#include <algorithm>
#include <chrono>

namespace spectra
{

// ── Figure detach ───────────────────────────────────────────────────────────

WindowContext* WindowManager::detach_figure(FigureId           figure_id,
                                            uint32_t           width,
                                            uint32_t           height,
                                            const std::string& title,
                                            int                screen_x,
                                            int                screen_y)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "detach_figure: not initialized");
        return nullptr;
    }

    if (figure_id == INVALID_FIGURE_ID)
    {
        SPECTRA_LOG_ERROR("window_manager", "detach_figure: invalid figure id");
        return nullptr;
    }

    // Clamp dimensions to reasonable minimums
    uint32_t w = width > 0 ? width : 800;
    uint32_t h = height > 0 ? height : 600;

    // If we have a registry, create a window with full UI; otherwise bare window
    WindowContext* wctx = nullptr;
    if (registry_)
    {
        wctx = create_window_with_ui(w, h, title, figure_id, screen_x, screen_y);
    }
    else
    {
        wctx = create_window(w, h, title);
        if (wctx)
        {
            wctx->assigned_figure_index = figure_id;
            wctx->assigned_figures      = {figure_id};
            wctx->active_figure_id      = figure_id;
            set_window_position(*wctx, screen_x, screen_y);
        }
    }

    if (!wctx)
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "detach_figure: failed to create window for figure " + std::to_string(figure_id));
        return nullptr;
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Detached figure " + std::to_string(figure_id) + " to window "
                         + std::to_string(wctx->id) + " at (" + std::to_string(screen_x) + ", "
                         + std::to_string(screen_y) + ")");
    return wctx;
}

// ── Figure move ─────────────────────────────────────────────────────────────

bool WindowManager::move_figure(FigureId figure_id, uint32_t from_window_id, uint32_t to_window_id)
{
    auto* from_wctx = find_window(from_window_id);
    auto* to_wctx   = find_window(to_window_id);
    if (!from_wctx || !to_wctx)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "move_figure: invalid window id (from=" + std::to_string(from_window_id)
                              + " to=" + std::to_string(to_window_id) + ")");
        return false;
    }
    if (from_wctx == to_wctx)
    {
        return false;   // No-op: same window
    }

    // Verify the source window has this figure in its assigned_figures list
    auto& src_figs = from_wctx->assigned_figures;
    auto  src_it   = std::find(src_figs.begin(), src_figs.end(), figure_id);
    if (src_it == src_figs.end())
    {
        // Fallback: check legacy single-figure field
        if (from_wctx->assigned_figure_index != figure_id)
        {
            SPECTRA_LOG_DEBUG("window_manager",
                              "move_figure: source window " + std::to_string(from_window_id)
                                  + " does not have figure " + std::to_string(figure_id));
            return false;
        }
    }

    // Remove from source window's assigned_figures
    if (src_it != src_figs.end())
    {
        src_figs.erase(src_it);
    }

    // Update source's active figure if we just removed the active one
    if (from_wctx->active_figure_id == figure_id)
    {
        from_wctx->active_figure_id = src_figs.empty() ? INVALID_FIGURE_ID : src_figs.front();
    }

    // Update legacy single-figure field
    from_wctx->assigned_figure_index =
        src_figs.empty() ? INVALID_FIGURE_ID : from_wctx->active_figure_id;

    // Add to target window's assigned_figures (avoid duplicates)
    auto& dst_figs = to_wctx->assigned_figures;
    if (std::find(dst_figs.begin(), dst_figs.end(), figure_id) == dst_figs.end())
    {
        dst_figs.push_back(figure_id);
    }
    to_wctx->active_figure_id      = figure_id;
    to_wctx->assigned_figure_index = figure_id;

    // Sync per-window FigureManagers if they exist
#ifdef SPECTRA_USE_IMGUI
    if (from_wctx->ui_ctx && from_wctx->ui_ctx->fig_mgr)
    {
        // Remove from source FigureManager (preserves FigureState, does NOT unregister)
        FigureState transferred_state = from_wctx->ui_ctx->fig_mgr->remove_figure(figure_id);

        // Remove from source DockSystem split panes if active
        auto& src_dock = from_wctx->ui_ctx->dock_system;
        if (src_dock.is_split())
        {
            src_dock.split_view().close_pane(figure_id);
        }

        // Add to target FigureManager with the transferred state.
        // Save dock active figure BEFORE add_figure(), because
        // add_figure → switch_to → tab bar callback will change
        // active_figure_index to the new figure (not yet in any pane).
        if (to_wctx->ui_ctx && to_wctx->ui_ctx->fig_mgr)
        {
            auto&    dst_dock         = to_wctx->ui_ctx->dock_system;
            FigureId prev_dock_active = dst_dock.active_figure_index();

            to_wctx->ui_ctx->fig_mgr->add_figure(figure_id, std::move(transferred_state));

            // Place the figure in a split pane so it's visible
            if (dst_dock.is_split())
            {
                auto* target_pane = dst_dock.split_view().pane_for_figure(prev_dock_active);
                if (!target_pane)
                {
                    auto all = dst_dock.split_view().all_panes();
                    if (!all.empty())
                        target_pane = all.front();
                }
                if (target_pane && target_pane->is_leaf())
                    target_pane->add_figure(figure_id);
                dst_dock.set_active_figure_index(figure_id);
            }
        }

        // Update InputHandler in target window
        if (to_wctx->ui_ctx)
        {
            Figure* fig = registry_ ? registry_->get(figure_id) : nullptr;
            if (fig)
            {
                to_wctx->ui_ctx->input_handler.set_figure(fig);
                if (!fig->axes().empty() && fig->axes()[0])
                {
                    to_wctx->ui_ctx->input_handler.set_active_axes(fig->axes()[0].get());
                    auto& vp = fig->axes()[0]->viewport();
                    to_wctx->ui_ctx->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
            }
        }

        // Reset source window's InputHandler so it no longer references the
        // moved figure's axes (which would cause cross-window interaction).
        if (from_wctx->ui_ctx && from_wctx->ui_ctx->fig_mgr)
        {
            FigureId remaining_id = from_wctx->ui_ctx->fig_mgr->active_index();
            Figure*  remaining    = (remaining_id != INVALID_FIGURE_ID && registry_)
                                        ? registry_->get(remaining_id)
                                        : nullptr;
            if (remaining)
            {
                from_wctx->ui_ctx->input_handler.set_figure(remaining);
                if (!remaining->axes().empty() && remaining->axes()[0])
                {
                    from_wctx->ui_ctx->input_handler.set_active_axes(remaining->axes()[0].get());
                    auto& vp = remaining->axes()[0]->viewport();
                    from_wctx->ui_ctx->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
                from_wctx->ui_ctx->input_handler.set_active_axes_base(nullptr);
            }
            else
            {
                from_wctx->ui_ctx->input_handler.set_figure(nullptr);
                from_wctx->ui_ctx->input_handler.set_active_axes(nullptr);
                from_wctx->ui_ctx->input_handler.set_active_axes_base(nullptr);
            }
        }
    }
#endif

    SPECTRA_LOG_INFO("window_manager",
                     "Moved figure " + std::to_string(figure_id) + " from window "
                         + std::to_string(from_window_id) + " to window "
                         + std::to_string(to_window_id));
    return true;
}

// ── Cross-window drop zone ──────────────────────────────────────────────────

int WindowManager::compute_cross_window_drop_zone(uint32_t target_wid, float local_x, float local_y)
{
    cross_drop_info_ = CrossWindowDropInfo{};

#ifdef SPECTRA_USE_IMGUI
    auto* wctx = find_window(target_wid);
    if (!wctx || !wctx->ui_ctx)
        return 0;

    auto& dock = wctx->ui_ctx->dock_system;

    // Use the DockSystem's compute_drop_target logic by temporarily
    // beginning a drag, computing the target, then cancelling.
    // But we can't call begin_drag (it sets dragging state).
    // Instead, replicate the edge-zone math from DockSystem::compute_drop_target.

    // Get the root pane bounds (canvas area)
    auto panes = dock.split_view().all_panes();
    if (panes.empty())
        return 0;

    // Find the pane under the cursor
    SplitPane* target_pane = nullptr;
    for (auto* p : panes)
    {
        if (!p->is_leaf())
            continue;
        Rect b = p->bounds();
        if (local_x >= b.x && local_x < b.x + b.w && local_y >= b.y && local_y < b.y + b.h)
        {
            target_pane = const_cast<SplitPane*>(p);
            break;
        }
    }

    if (!target_pane)
    {
        // Cursor not over any pane — try using the full window area
        // (common for non-split windows where pane bounds may not cover the tab bar area)
        if (panes.size() == 1)
            target_pane = const_cast<SplitPane*>(panes[0]);
        else
            return 0;
    }

    Rect b = target_pane->bounds();
    if (b.w < 1.0f || b.h < 1.0f)
        return 0;

    constexpr float DROP_ZONE_FRACTION = 0.25f;
    constexpr float DROP_ZONE_MIN_SIZE = 40.0f;

    float edge_w = std::max(b.w * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);
    float edge_h = std::max(b.h * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);
    edge_w       = std::min(edge_w, b.w * 0.4f);
    edge_h       = std::min(edge_h, b.h * 0.4f);

    float rel_x = local_x - b.x;
    float rel_y = local_y - b.y;

    // DropZone: 0=None, 1=Left, 2=Right, 3=Top, 4=Bottom, 5=Center
    int zone = 5;   // Default: Center

    if (rel_x < edge_w)
        zone = 1;   // Left
    else if (rel_x > b.w - edge_w)
        zone = 2;   // Right
    else if (rel_y < edge_h)
        zone = 3;   // Top
    else if (rel_y > b.h - edge_h)
        zone = 4;   // Bottom

    // Compute highlight rect
    float hx = b.x;
    float hy = b.y;
    float hw = b.w;
    float hh = b.h;
    switch (zone)
    {
        case 1:   // Left
            hw = b.w * 0.5f;
            break;
        case 2:   // Right
            hx = b.x + b.w * 0.5f;
            hw = b.w * 0.5f;
            break;
        case 3:   // Top
            hh = b.h * 0.5f;
            break;
        case 4:   // Bottom
            hy = b.y + b.h * 0.5f;
            hh = b.h * 0.5f;
            break;
        case 5:   // Center
        default:
            break;
    }

    cross_drop_info_.zone             = zone;
    cross_drop_info_.hx               = hx;
    cross_drop_info_.hy               = hy;
    cross_drop_info_.hw               = hw;
    cross_drop_info_.hh               = hh;
    cross_drop_info_.target_figure_id = target_pane->figure_index();
    return zone;
#else
    (void)target_wid;
    (void)local_x;
    (void)local_y;
    return 0;
#endif
}

// ── Tearoff preview window ──────────────────────────────────────────────────

void WindowManager::request_preview_window(uint32_t           width,
                                           uint32_t           height,
                                           int                screen_x,
                                           int                screen_y,
                                           const std::string& figure_title)
{
    pending_preview_create_ = PendingPreviewCreate{width, height, screen_x, screen_y, figure_title};
}

void WindowManager::request_destroy_preview()
{
    pending_preview_destroy_ = true;
    pending_preview_create_.reset();   // Cancel any pending create
}

void WindowManager::warmup_preview_window(uint32_t width, uint32_t height)
{
    if (pooled_preview_ || !backend_ || backend_->is_headless())
        return;

#ifdef SPECTRA_USE_GLFW
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // Create hidden

    GLFWwindow* glfw_win = glfwCreateWindow(static_cast<int>(width),
                                            static_cast<int>(height),
                                            "Preview",
                                            nullptr,
                                            nullptr);

    // Reset hints to defaults for future windows
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    if (!glfw_win)
    {
        SPECTRA_LOG_WARN("window_manager", "warmup_preview_window: glfwCreateWindow failed");
        return;
    }

    auto wctx           = std::make_unique<WindowContext>();
    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_win;
    wctx->glfw_window   = glfw_win;
    wctx->is_preview    = true;

    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_WARN("window_manager", "warmup_preview_window: Vulkan init failed");
        glfwDestroyWindow(glfw_win);
        return;
    }

    glfwSetWindowUserPointer(glfw_win, this);
    install_glfw_lifecycle_callbacks(glfw_win);
    // On X11 the implicit pointer grab may transfer from the source window to
    // this preview when it is mapped. Keep cursor events flowing so the main
    // loop continues updating the source tab's drag controller.
    glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
    glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);

    if (!init_minimal_window_imgui(*wctx))
    {
        SPECTRA_LOG_WARN("window_manager", "warmup_preview_window: ImGui init failed");
        backend_->destroy_window_context(*wctx);
        glfwDestroyWindow(glfw_win);
        return;
    }

    pooled_preview_ = std::move(wctx);

    SPECTRA_LOG_INFO("window_manager",
                     "Warmed up preview window (id=" + std::to_string(pooled_preview_->id) + ")");
#endif
}

void WindowManager::process_deferred_preview()
{
    if (pending_preview_destroy_)
    {
        pending_preview_destroy_ = false;
        // Also cancel any pending create — the drag ended before the
        // preview could appear, so don't flash it briefly.
        pending_preview_create_.reset();
        destroy_preview_window_impl();
        return;
    }
    if (pending_preview_create_)
    {
        auto req = std::move(*pending_preview_create_);
        pending_preview_create_.reset();
        create_preview_window_impl(req.width, req.height, req.screen_x, req.screen_y, req.title);
    }
}

bool WindowManager::has_preview_window() const
{
    return preview_window_id_ != 0 || pending_preview_create_.has_value();
}

WindowContext* WindowManager::create_preview_window_impl(uint32_t           width,
                                                         uint32_t           height,
                                                         int                screen_x,
                                                         int                screen_y,
                                                         const std::string& figure_title)
{
    // Destroy any existing visible preview window first
    destroy_preview_window_impl();

    if (!backend_ || backend_->is_headless())
        return nullptr;

#ifdef SPECTRA_USE_GLFW
    // ── Fast path: reuse the pre-created hidden preview window ──────
    if (pooled_preview_)
    {
        auto* glfw_win = static_cast<GLFWwindow*>(pooled_preview_->glfw_window);

        // Update title, resize if needed, and position before showing
        glfwSetWindowTitle(glfw_win, figure_title.c_str());

        int cur_w = 0;
        int cur_h = 0;
        glfwGetWindowSize(glfw_win, &cur_w, &cur_h);
        if (cur_w != static_cast<int>(width) || cur_h != static_cast<int>(height))
        {
            glfwSetWindowSize(glfw_win, static_cast<int>(width), static_cast<int>(height));
            pooled_preview_->swapchain_dirty = true;
        }

        glfwSetWindowPos(glfw_win,
                         screen_x - static_cast<int>(width) / 2,
                         screen_y - static_cast<int>(height) / 3);

        // Suppress spurious mouse release from glfwShowWindow on X11
        if (mouse_release_tracking_)
        {
            suppress_release_until_ =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        }

        glfwShowWindow(glfw_win);

        pooled_preview_->title        = figure_title;
        pooled_preview_->should_close = false;
        preview_window_id_            = pooled_preview_->id;

        WindowContext* ptr = pooled_preview_.get();
        windows_.push_back(std::move(pooled_preview_));
        rebuild_active_list();

        SPECTRA_LOG_DEBUG("window_manager",
                          "Activated pooled preview window " + std::to_string(ptr->id));
        return ptr;
    }

    // ── Slow path: create from scratch (fallback) ──────────────────
    // Create hidden, position, then show — same pattern as warmup to avoid
    // a visible position jump on X11.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* glfw_win = glfwCreateWindow(static_cast<int>(width),
                                            static_cast<int>(height),
                                            figure_title.c_str(),
                                            nullptr,
                                            nullptr);

    // Reset hints to defaults for future windows
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    if (!glfw_win)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: glfwCreateWindow failed");
        return nullptr;
    }

    // Position the window at the cursor before showing
    glfwSetWindowPos(glfw_win,
                     screen_x - static_cast<int>(width) / 2,
                     screen_y - static_cast<int>(height) / 3);

    auto wctx           = std::make_unique<WindowContext>();
    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_win;
    wctx->glfw_window   = glfw_win;
    wctx->is_preview    = true;
    wctx->title         = figure_title;

    // Initialize Vulkan resources
    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: Vulkan init failed");
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    // Set GLFW callbacks (minimal — framebuffer resize, close, and mouse button).
    // Mouse button callback is needed so we catch ButtonRelease events if the
    // X11 implicit pointer grab transfers to this window during a tab drag.
    glfwSetWindowUserPointer(glfw_win, this);
    install_glfw_lifecycle_callbacks(glfw_win);
    glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
    glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);

    if (!init_minimal_window_imgui(*wctx))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_preview_window: ImGui init failed");
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    // Suppress spurious mouse release from glfwShowWindow on X11.
    // The WM grabs the pointer when the window is mapped, sending a real
    // ButtonRelease to the source window.  Suppress for 200ms after show.
    if (mouse_release_tracking_)
    {
        suppress_release_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    }

    glfwShowWindow(glfw_win);

    preview_window_id_ = wctx->id;

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Created preview window " + std::to_string(ptr->id) + ": "
                          + std::to_string(width) + "x" + std::to_string(height));
    return ptr;
#else
    (void)width;
    (void)height;
    (void)screen_x;
    (void)screen_y;
    (void)figure_title;
    return nullptr;
#endif
}

void WindowManager::move_preview_window(int screen_x, int screen_y) const
{
#ifdef SPECTRA_USE_GLFW
    auto* wctx = preview_window();
    if (!wctx || !wctx->glfw_window)
        return;

    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    int   w        = 0;
    int   h        = 0;
    glfwGetWindowSize(glfw_win, &w, &h);
    glfwSetWindowPos(glfw_win, screen_x - w / 2, screen_y - h / 3);
#else
    (void)screen_x;
    (void)screen_y;
#endif
}

void WindowManager::destroy_preview_window_impl()
{
    if (preview_window_id_ == 0)
        return;

    uint32_t id        = preview_window_id_;
    preview_window_id_ = 0;

#ifdef SPECTRA_USE_GLFW
    // Recycle the preview window back into the pool instead of destroying it.
    // Find it in windows_ and move it to pooled_preview_ after hiding.
    auto it =
        std::find_if(windows_.begin(), windows_.end(), [id](const auto& w) { return w->id == id; });
    if (it != windows_.end() && (*it)->is_preview && !pooled_preview_)
    {
        auto* glfw_win = static_cast<GLFWwindow*>((*it)->glfw_window);
        if (glfw_win)
            glfwHideWindow(glfw_win);

        pooled_preview_ = std::move(*it);
        windows_.erase(it);
        rebuild_active_list();

        SPECTRA_LOG_DEBUG("window_manager",
                          "Recycled preview window " + std::to_string(id) + " to pool");
        return;
    }
#endif

    // Fallback: destroy if we couldn't recycle
    destroy_window(id);
}

WindowContext* WindowManager::preview_window() const
{
    if (preview_window_id_ == 0)
        return nullptr;
    return find_window(preview_window_id_);
}

}   // namespace spectra
