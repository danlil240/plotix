#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QtGui/QVulkanInstance>

#include "ui/theme/theme.hpp"
#include "ui/workspace/overlay_snapshot.hpp"

class QWindow;

namespace spectra
{
class VulkanBackend;
class Renderer;
class Figure;
class InputHandler;
class Axes;
class AxesBase;
class Series;
struct WindowContext;
#ifdef SPECTRA_USE_IMGUI
class ImGuiIntegration;
class DataInteraction;
#endif
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtSurfaceHost;

struct QtSeriesSelectionEntry
{
    Figure*   figure       = nullptr;
    AxesBase* owner        = nullptr;
    Axes*     axes         = nullptr;
    Series*   series       = nullptr;
    int       axes_index   = -1;
    int       series_index = -1;
};

// Bootstrap runtime that owns Spectra's Vulkan backend + renderer and
// drives a frame loop for one or more QWindow canvases.
//
// Typical usage:
//   QtRuntime rt;
//   rt.init();                     // creates VkInstance, VkDevice, Renderer
//   rt.attach_window(qwindow, w, h);  // creates surface + swapchain
//   // in requestUpdate / timer:
//   rt.begin_frame();
//   rt.render_figure(fig);
//   rt.end_frame();
class QtRuntime
{
   public:
    QtRuntime();
    ~QtRuntime();

    // Non-copyable
    QtRuntime(const QtRuntime&)            = delete;
    QtRuntime& operator=(const QtRuntime&) = delete;

    // Initialize Vulkan instance, device, and renderer.
    // Must be called before attach_window().
    bool init();

    // Shut down all Vulkan resources.  Safe to call multiple times.
    void shutdown();

    bool is_initialized() const { return initialized_; }

    // Adopt an existing VkInstance (e.g. from host app).
    // Must be called BEFORE init() if the host already created a VkInstance.
    // If not called, init() creates its own instance.
    bool adopt_vulkan_instance(VkInstance instance);

    // Bind a QWindow as a Vulkan canvas.
    // Creates VkSurfaceKHR + swapchain + command buffers + sync objects.
    // The QWindow must have surfaceType() == QSurface::VulkanSurface.
    // Returns true on success.
    bool attach_window(QWindow* window, uint32_t width, uint32_t height);
    void detach_window(QWindow* window);
    bool has_window(QWindow* window) const;

    // Recreate swapchain after resize.
    bool resize(QWindow* window, uint32_t width, uint32_t height);
    bool resize(uint32_t width, uint32_t height);

    // Mark swapchain as needing recreation (deferred to next begin_frame).
    // Use this from resize/DPR-change events instead of calling resize() directly.
    void mark_swapchain_dirty(QWindow* window);
    void mark_swapchain_dirty();

    // Frame lifecycle — call from Qt's update/timer callback.
    bool begin_frame(QWindow* window);
    bool begin_frame();
    void render_figure(QWindow* window, Figure& figure);
    void render_figure(Figure& figure);
    void end_frame(QWindow* window);
    void end_frame();

    // Convenience: full frame lifecycle in one call.
    bool render_window(QWindow* window, Figure& figure);
    bool render_window(Figure& figure);

    // Wire per-window input handler so modular UI overlays (legend/tooltips)
    // receive the same interaction state as the canvas.
    void set_input_handler(QWindow* window, InputHandler* input);
    void set_input_handler(InputHandler* input);

    // Apply the per-canvas crosshair state to the existing data-interaction
    // overlay. Returns false until that canvas is attached and initialized.
    bool set_crosshair(QWindow* window, bool enabled);

    // Losslessly transfer the retained overlay state for a native canvas.
    // These return false when the canvas has not attached its interaction layer.
    bool capture_overlay_snapshot(QWindow*         window,
                                  const Figure&    figure,
                                  OverlaySnapshot& snapshot) const;
    bool restore_overlay_snapshot(QWindow* window, Figure& figure, const OverlaySnapshot& snapshot);
    size_t marker_count(QWindow* window) const;
    bool   clear_markers(QWindow* window);

    // Canvas selection is the authoritative per-window state. Mirror it into
    // the retained interaction/inspector context and renderer highlight.
    void set_series_selection(QWindow* window, const std::vector<QtSeriesSelectionEntry>& selected);
    void notify_series_removed(QWindow* window, const Series* series);

    // Keep the exact legacy-rendered inspector chevron while delegating its
    // state and action to the Qt-owned inspector drawer for this canvas.
    void set_inspector_toggle_callbacks(QWindow*              window,
                                        std::function<void()> toggle,
                                        std::function<bool()> is_open);

    // ── Surface generation API ─────────────────────────────────────────────
    // Returns the current surface generation for a window, or 0 if invalid.
    uint32_t surface_generation(QWindow* window) const;
    bool     surface_valid(QWindow* window) const;

    // ── Explicit render-target API ─────────────────────────────────────────
    // Begin a frame targeting a specific WindowContext (not global active window).
    // This is the first step toward phasing out global mutable active-window state.
    bool begin_frame(WindowContext* wctx);
    void render_figure(WindowContext* wctx, Figure& figure);
    void end_frame(WindowContext* wctx);

    // Accessors
    QVulkanInstance*  vulkan_instance() const;
    VulkanBackend*    backend() const { return backend_.get(); }
    Renderer*         renderer() const { return renderer_.get(); }
    ui::ThemeManager* theme_manager() { return &theme_mgr_; }
    WindowContext*    window_context(QWindow* window) const;
    WindowContext*    window_context() const;

   private:
    struct InspectorCallbacks
    {
        std::function<void()> toggle;
        std::function<bool()> is_open;
    };

    struct WindowState
    {
        std::unique_ptr<WindowContext>        window_ctx;
        InputHandler*                         input_handler = nullptr;
        InspectorCallbacks                    inspector_callbacks;
        std::chrono::steady_clock::time_point last_resize_request;
        bool                                  resize_pending           = false;
        uint32_t                              swapchain_recreate_count = 0;
        uint32_t                              frame_skip_count         = 0;
        std::vector<const Series*>            selected_series;

        // Surface generation: 0 = invalid, >0 = valid surface.
        // Incremented on attach, invalidated on detach.
        uint32_t surface_generation = 0;

#ifdef SPECTRA_USE_IMGUI
        std::unique_ptr<ImGuiIntegration>     imgui_ui;
        std::unique_ptr<DataInteraction>      data_interaction;
        std::chrono::steady_clock::time_point ui_last_frame_time{};
        bool                                  ui_has_last_frame_time = false;
#endif
    };

    WindowState*       find_window_state(QWindow* window);
    const WindowState* find_window_state(QWindow* window) const;

    bool initialized_ = false;

    ui::ThemeManager                                           theme_mgr_;
    std::unique_ptr<QtSurfaceHost>                             surface_host_;
    std::unique_ptr<QVulkanInstance>                           vulkan_instance_;
    std::unique_ptr<VulkanBackend>                             backend_;
    std::unique_ptr<Renderer>                                  renderer_;
    std::unordered_map<QWindow*, std::unique_ptr<WindowState>> window_states_;
    std::unordered_map<QWindow*, InputHandler*>                pending_input_handlers_;
    std::unordered_map<QWindow*, InspectorCallbacks>           pending_inspector_callbacks_;
    QWindow*                                                   primary_window_       = nullptr;
    QWindow*                                                   current_frame_window_ = nullptr;
    uint32_t                                                   next_window_id_       = 1;

    // Debounce: track last resize request time to coalesce rapid resizes.
    static constexpr std::chrono::milliseconds k_resize_debounce{50};
};

}   // namespace spectra::adapters::qt
