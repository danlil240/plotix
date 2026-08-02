#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <spectra/fwd.hpp>
#include "ui/app/window_ui_context_builder.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include <string>
#include <vector>

// Forward declarations
struct GLFWwindow;
struct SDL_Window;

namespace spectra
{

struct WindowContext;
class VulkanBackend;
class Renderer;
class ExportFormatRegistry;
class PluginManager;
class SessionRuntime;

namespace ui::settings
{
class SettingsStore;
}

// Manages multiple OS windows, each with its own GLFW window, Vulkan surface,
// swapchain, command buffers, and sync objects.  Shared Vulkan resources
// (VkInstance, VkDevice, pipelines, descriptor pool, series GPU buffers)
// remain in VulkanBackend.
//
// Usage:
//   WindowManager wm;
//   wm.init(backend);
//   auto* initial = wm.create_initial_window(glfw_window);
//   auto* secondary = wm.create_window(800, 600, "Window 2");
//   ...
//   // In render loop:
//   wm.poll_events();
//   wm.process_pending_closes();
//   for (auto* wctx : wm.windows()) { ... }
//   ...
//   wm.shutdown();
class WindowManager
{
   public:
    WindowManager() = default;
    ~WindowManager();

    WindowManager(const WindowManager&)            = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Initialize the window manager with a reference to the Vulkan backend
    // and figure registry.  Must be called before any other method.
    void init(VulkanBackend*    backend,
              FigureRegistry*   registry  = nullptr,
              Renderer*         renderer  = nullptr,
              ui::ThemeManager* theme_mgr = nullptr);

    // Create the initial (first) window uniformly — same ownership as
    // secondary windows.  Takes ownership of the backend's initial
    // WindowContext (which already has surface + swapchain initialized)
    // and installs WindowManager GLFW callbacks on it.
    // Returns the context pointer, or nullptr on failure.
    WindowContext* create_initial_window(void* glfw_window);

    // Create a new OS window with its own swapchain and Vulkan resources.
    // Returns nullptr on failure.
    WindowContext* create_window(uint32_t width, uint32_t height, const std::string& title);

    // Mark a window for destruction.  Actual cleanup happens in
    // process_pending_closes() to avoid mutating the window list during
    // iteration.
    void request_close(uint32_t window_id);

    // Destroy a window immediately.  Waits for GPU idle on that window's
    // resources before cleanup.  Do NOT call while iterating windows().
    void destroy_window(uint32_t window_id);

    // Process any windows that have been marked for close (either via
    // request_close or GLFW's should_close flag).
    void process_pending_closes();

    // Single glfwPollEvents() call for all windows.
    void poll_events();

    // SDL3 event pump — drains SDL_PollEvent and routes to input handlers.
    void process_sdl3_events();

    // Block until an event arrives or timeout (seconds) elapses.
    // Use when the application is idle (nothing to render) to save CPU/GPU.
    void wait_events_timeout(double timeout_seconds);

    // Returns all currently active (non-closed) window contexts.
    const std::vector<WindowContext*>& windows() const { return active_ptrs_; }

    // Returns the window that currently has OS focus, or nullptr.
    WindowContext* focused_window() const;

    // Returns true if at least one window is still open.
    bool any_window_open() const;

    // Returns the window context for a given window ID, or nullptr.
    WindowContext* find_window(uint32_t window_id) const;

    // Invalidate cached figure/axes pointers across all window UI contexts.
    void clear_figure_caches(Figure* fig);

    // Set the screen position of a window (top-left corner).
    void set_window_position(WindowContext& wctx, int x, int y);

    // Move a figure from one window to another.
    // Reassigns the figure's FigureId to the target window.
    // Returns true on success, false if source or target window not found.
    bool move_figure(FigureId figure_id, uint32_t from_window_id, uint32_t to_window_id);

    // Detach a figure into a new OS window at the given screen position.
    // Creates a new window sized to the figure's dimensions and assigns the
    // figure to it.  Returns the new WindowContext, or nullptr on failure.
    // Will refuse to detach if it's the last figure (caller must check).
    WindowContext* detach_figure(FigureId           figure_id,
                                 uint32_t           width,
                                 uint32_t           height,
                                 const std::string& title,
                                 int                screen_x,
                                 int                screen_y);

    // Create the very first window with full UI stack.  Unlike create_window_with_ui(),
    // this method also handles the backend's initial WindowContext handoff (the backend
    // creates surface + swapchain during init, and this method takes ownership).
    // After this call, the first window is identical to any secondary window.
    // The glfw_window handle must already exist (created by GlfwAdapter or caller).
    // figure_ids are ALL figures to assign to this window (the first becomes active).
    // Returns the new WindowContext, or nullptr on failure.
    WindowContext* create_first_window_with_ui(void*                        glfw_window,
                                               const std::vector<FigureId>& figure_ids);

    // Create a new OS window with full UI stack (ImGui, FigureManager, DockSystem,
    // InputHandler, etc.).  The window gets its own ImGui context and can render
    // independently.  figure_id is the initial figure to assign to the window.
    // Returns the new WindowContext, or nullptr on failure.
    WindowContext* create_window_with_ui(uint32_t           width,
                                         uint32_t           height,
                                         const std::string& title,
                                         FigureId           initial_figure_id,
                                         int                screen_x = 0,
                                         int                screen_y = 0);

    // Install full input GLFW callbacks (cursor, mouse, scroll, key, char,
    // cursor_enter) on a WindowContext that already has ui_ctx set.
    // Used after moving a UI context into a window that was created with
    // only basic callbacks (framebuffer size, close, focus).
    void install_input_callbacks(WindowContext& wctx);

    // Returns the number of open windows.
    size_t window_count() const { return active_ptrs_.size(); }

    // ── Detached-panel window ─────────────────────────────────────────

    // Create a lightweight OS window that renders only panel content via
    // the supplied draw callback.  Session runtime detects the callback
    // and runs a minimal ImGui frame (no FigureManager / DockSystem).
    // Returns the new WindowContext, or nullptr on failure.
    WindowContext* create_panel_window(uint32_t              width,
                                       uint32_t              height,
                                       const std::string&    title,
                                       std::function<void()> draw_callback,
                                       int                   screen_x = 0,
                                       int                   screen_y = 0);

    // Destroy a panel window by its ID (immediate, GPU-wait + cleanup).
    void destroy_panel_window(uint32_t window_id);

    // ── Tearoff preview window ───────────────────────────────────────

    // Pre-create a hidden preview window so tearoff drags are instant.
    // Call once after the first window is fully initialized.
    // Safe to call multiple times (no-op if already warmed up).
    void warmup_preview_window(uint32_t width = 280, uint32_t height = 200);

    // Request creation of a preview window (deferred to avoid mutating
    // windows_ while iterating).  Safe to call from TabDragController.
    void request_preview_window(uint32_t           width,
                                uint32_t           height,
                                int                screen_x,
                                int                screen_y,
                                const std::string& figure_title);

    // Request destruction of the preview window (deferred).
    void request_destroy_preview();

    // Move the preview window to follow the mouse cursor (screen coords).
    // This is safe mid-iteration (only calls glfwSetWindowPos).
    void move_preview_window(int screen_x, int screen_y) const;

    // Process deferred preview create/destroy requests.
    // Call AFTER the render loop iteration completes.
    void process_deferred_preview();

    // Returns true if a preview window exists (or creation is pending).
    bool has_preview_window() const;

    // Get the current preview window (nullptr if none).
    WindowContext* preview_window() const;

    // Check if a mouse button is pressed on ANY GLFW window.
    // On X11, creating a new window during an active drag can transfer
    // the implicit pointer grab, so we check all windows.
    bool is_mouse_button_held(int glfw_button) const;

    // Callback-based mouse release tracking for tab drag.
    // When active, is_mouse_button_held uses the callback-tracked state
    // instead of polling (which can give false RELEASE after creating
    // a new GLFW window on X11).
    void begin_mouse_release_tracking();
    void end_mouse_release_tracking();

    // Get the global screen-space cursor position by querying GLFW. Pass a
    // window id to keep the reference frame stable while another window (such
    // as a tearoff preview) is being shown/moved. Returns true on success.
    bool get_global_cursor_pos(double&  screen_x,
                               double&  screen_y,
                               uint32_t reference_window_id = 0) const;

    // Cross-window drag target — set each frame by the active
    // TabDragController so target windows can draw dock highlights.
    void     set_drag_target_window(uint32_t wid) { drag_target_window_id_ = wid; }
    uint32_t drag_target_window() const { return drag_target_window_id_; }

    // Compute the drop zone on a target window's DockSystem given cursor
    // position in that window's local coordinates.  Returns the DropZone
    // enum cast to int (0=None,1=Left,2=Right,3=Top,4=Bottom,5=Center).
    // Also stores the highlight rect for overlay rendering.
    int compute_cross_window_drop_zone(uint32_t target_wid, float local_x, float local_y);

    // Query the last computed cross-window drop highlight rect (in target window local coords).
    // Returns {0,0,0,0} if no valid zone.
    struct CrossWindowDropInfo
    {
        int      zone = 0;                               // DropZone cast to int
        float    hx = 0, hy = 0, hw = 0, hh = 0;         // highlight rect
        FigureId target_figure_id = INVALID_FIGURE_ID;   // figure in the pane under cursor
    };
    CrossWindowDropInfo cross_window_drop_info() const { return cross_drop_info_; }

    // Tab drag handlers — stored and applied to every new window's
    // TabDragController in init_window_ui().  Set these before creating
    // windows so all windows get the same drag behavior.
    using TabDetachHandler = std::function<
        void(FigureId fid, uint32_t w, uint32_t h, const std::string& title, int sx, int sy)>;
    using TabMoveHandler       = std::function<void(FigureId fid,
                                              uint32_t target_window_id,
                                              int      drop_zone,
                                              float    local_x,
                                              float    local_y,
                                              FigureId target_figure_id)>;
    using RedrawRequestHandler = std::function<void(const char* reason)>;
    using FileDropHandler      = std::function<void(uint32_t window_id, const std::string& path)>;
    using FigureUnregisteringHandler = std::function<void(FigureId, Figure*)>;
    using InteractiveFrameHandler    = std::function<void()>;

    void set_tab_detach_handler(TabDetachHandler cb) { tab_detach_handler_ = std::move(cb); }
    void set_tab_move_handler(TabMoveHandler cb) { tab_move_handler_ = std::move(cb); }
    void set_redraw_request_handler(RedrawRequestHandler cb)
    {
        redraw_request_handler_ = std::move(cb);
    }
    void set_file_drop_handler(FileDropHandler cb) { file_drop_handler_ = std::move(cb); }
    void set_figure_unregistering_handler(FigureUnregisteringHandler cb)
    {
        figure_unregistering_handler_ = std::move(cb);
    }
    void set_interactive_frame_handler(InteractiveFrameHandler cb)
    {
        interactive_frame_handler_ = std::move(cb);
    }

    // Render one frame synchronously (Win32 size/move modal loop).
    void request_interactive_frame();

    // Set the event system for cross-subsystem notifications.
    void set_event_system(EventSystem* es) { event_system_ = es; }

    // Shared plugin services wired into each window UI context.
    void set_plugin_manager(PluginManager* mgr) { plugin_manager_ = mgr; }
    void set_export_format_registry(ExportFormatRegistry* reg) { export_format_registry_ = reg; }
    void set_session_runtime(SessionRuntime* session) { session_ = session; }

    // Process-scoped settings store passed through to each window UI context.
    void set_settings_store(ui::settings::SettingsStore* s) { settings_store_ = s; }

    // Shared overlay registry (owned here, shared with all windows and PluginManager).
    OverlayRegistry& overlay_registry() { return shared_overlay_registry_; }

    // Shutdown: destroy all windows and release resources.
    // After this, the WindowManager is in an uninitialized state.
    void shutdown();

   private:
    // Create Vulkan resources (surface, swapchain, cmd buffers, sync) for a
    // WindowContext that already has a valid glfw_window pointer.
    bool init_vulkan_resources(WindowContext& wctx, uint32_t width, uint32_t height);

    // Destroy Vulkan resources for a window context (surface, swapchain, etc).
    void destroy_vulkan_resources(WindowContext& wctx);

    // Rebuild the active_ptrs_ cache from windows_.
    void rebuild_active_list();

    // GLFW callback trampolines — route events to the correct WindowContext.
    static void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void glfw_window_close_callback(GLFWwindow* window);
    static void glfw_window_focus_callback(GLFWwindow* window, int focused);
    static void glfw_window_refresh_callback(GLFWwindow* window);

    // Framebuffer/close/focus/refresh — shared by all GLFW windows.
    void install_glfw_lifecycle_callbacks(GLFWwindow* glfw_win);

    // Full input callbacks for windows with UI (mouse, key, scroll, char, cursor enter)
    static void glfw_cursor_pos_callback(GLFWwindow* window, double x, double y);
    static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void glfw_scroll_callback(GLFWwindow* window, double x_offset, double y_offset);
    static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint);
    static void glfw_cursor_enter_callback(GLFWwindow* window, int entered);
    static void glfw_drop_callback(GLFWwindow* window, int count, const char** paths);

    // Helper: find the WindowContext for a GLFW window handle
    WindowContext* find_by_glfw_window(GLFWwindow* window) const;

    // Helper: find the WindowContext for an SDL3 window handle
    WindowContext* find_by_sdl3_window(SDL_Window* window) const;

    void request_redraw(const char* reason);

    // Initialize the full UI subsystem bundle (ImGui, FigureManager, etc.)
    bool init_window_ui(WindowContext& wctx, FigureId initial_figure_id);

    WindowUIContextBuildOptions make_ui_build_options(WindowContext& wctx,
                                                      FigureId       initial_figure_id);

    bool init_minimal_window_imgui(WindowContext& wctx);

    void wire_tab_drag_handlers(WindowUIContext& ui);

    VulkanBackend*                              backend_   = nullptr;
    FigureRegistry*                             registry_  = nullptr;
    Renderer*                                   renderer_  = nullptr;
    ui::ThemeManager*                           theme_mgr_ = nullptr;
    std::vector<std::unique_ptr<WindowContext>> windows_;
    std::vector<WindowContext*> active_ptrs_;   // cache of raw pointers for fast iteration
    uint32_t                    next_window_id_ = 1;

    // IDs of windows pending destruction (deferred to avoid mutation during iteration)
    std::vector<uint32_t> pending_close_ids_;

    // Tearoff preview window (0 = none active)
    uint32_t preview_window_id_ = 0;
    bool     preview_rendered_  = false;   // True after preview has been rendered at least once

    // Pre-created hidden preview window for instant tearoff.
    // Lives outside windows_ when pooled; moved in on show, back on hide.
    std::unique_ptr<WindowContext> pooled_preview_;

    // Tab drag handlers (applied to every new window's TabDragController)
    TabDetachHandler        tab_detach_handler_;
    TabMoveHandler          tab_move_handler_;
    RedrawRequestHandler    redraw_request_handler_;
    InteractiveFrameHandler interactive_frame_handler_;
    bool                    interactive_frame_in_flight_ = false;

    // OS file drop handler
    FileDropHandler            file_drop_handler_;
    FigureUnregisteringHandler figure_unregistering_handler_;

    // Cross-window drag target tracking
    uint32_t            drag_target_window_id_ = 0;
    CrossWindowDropInfo cross_drop_info_;

    // Shared clipboard across all windows (owned here, passed by pointer to each window's UI)
    SeriesClipboard shared_clipboard_;

    // Shared overlay registry across all windows (owned here, wired to PluginManager)
    OverlayRegistry shared_overlay_registry_;

    // Shared plugin services (owned externally by AppRuntime)
    PluginManager*        plugin_manager_         = nullptr;
    ExportFormatRegistry* export_format_registry_ = nullptr;
    SessionRuntime*       session_                = nullptr;

    // Process-scoped settings store (owned externally by App/agent).
    ui::settings::SettingsStore* settings_store_ = nullptr;

    // Monotonic z-order counter — incremented each time any window gains focus.
    // Each window's z_order field stores the value at its last focus event.
    uint64_t next_z_order_ = 1;

    // Callback-based mouse release tracking (tab drag)
    bool                                  mouse_release_tracking_ = false;
    bool                                  mouse_release_seen_     = false;
    std::chrono::steady_clock::time_point suppress_release_until_{};

    EventSystem* event_system_ = nullptr;

    // Deferred preview window requests
    struct PendingPreviewCreate
    {
        uint32_t    width    = 0;
        uint32_t    height   = 0;
        int         screen_x = 0;
        int         screen_y = 0;
        std::string title;
    };
    std::optional<PendingPreviewCreate> pending_preview_create_;
    bool                                pending_preview_destroy_ = false;

    // Internal: actually create/destroy the preview window.
    WindowContext* create_preview_window_impl(uint32_t           width,
                                              uint32_t           height,
                                              int                screen_x,
                                              int                screen_y,
                                              const std::string& figure_title);
    void           destroy_preview_window_impl();
};

}   // namespace spectra
