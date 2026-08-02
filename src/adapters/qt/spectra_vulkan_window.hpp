#pragma once

// SpectraVulkanWindow — production QWindow canvas for Vulkan rendering.
//
// Promoted from qt_embed_demo.cpp into a reusable production component.
// Key improvements over the demo version:
//   - Surface generations: monotonically increasing token per canvas,
//     invalidated on SurfaceAboutToBeDestroyed, validated on surface creation.
//     Every async frame request checks the generation before rendering.
//   - Event-driven scheduling via QWindow::requestUpdate() instead of a
//     permanent QTimer.  A timer is used only while continuous animation
//     is active (streaming, animation playback).
//   - No ImGui dependency — pure canvas rendering.

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QtGui/QWindow>

#include "ui/workspace/overlay_snapshot.hpp"

class QPlatformSurfaceEvent;

namespace spectra
{
class Figure;
class InputHandler;
class Axes;
class AxesBase;
class Series;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtRuntime;

class SpectraVulkanWindow : public QWindow
{
    Q_OBJECT
   public:
    using AnimationTickCallback = std::function<void(float)>;

    struct SeriesSelection
    {
        AxesBase* owner        = nullptr;
        Axes*     axes         = nullptr;
        Series*   series       = nullptr;
        int       axes_index   = -1;
        int       series_index = -1;
    };

    explicit SpectraVulkanWindow(QWindow* parent = nullptr);

    // ── Configuration ──────────────────────────────────────────────────────

    void setRuntime(QtRuntime* rt);
    void setFigure(Figure* fig);
    void setInputHandler(InputHandler* ih);
    void setInspectorToggleCallbacks(std::function<void()> toggle, std::function<bool()> is_open);
    void setAnimationTick(AnimationTickCallback cb);
    // Advance the canvas-owned animation model once. Exposed separately from
    // rendering so deterministic hosts/tests can drive the same callback.
    void tickAnimation(float dt);

    // Canvas-scoped inspector actions. These are also used by the retained
    // overlay chevron through QtRuntime.
    void toggleInspector();
    bool isInspectorOpen() const;

    void toggleCrosshair();
    bool crosshairEnabled() const { return crosshair_enabled_; }

    // Preserve markers, annotations, tooltip, and crosshair state across
    // workspace reloads and native surface destruction/recreation.
    bool   captureOverlaySnapshot(OverlaySnapshot& snapshot);
    void   restoreOverlaySnapshot(const OverlaySnapshot& snapshot);
    size_t markerCount();
    bool   clearMarkers();

    // Canvas-owned selection shared by pointer interaction, commands, and
    // renderer highlighting.
    void selectSeries(Figure* figure, Axes* axes, int axes_index, Series* series, int series_index);
    void selectSeriesNoToggle(Figure* figure,
                              Axes*   axes,
                              int     axes_index,
                              Series* series,
                              int     series_index);
    void setSeriesSelection(std::vector<SeriesSelection> selection);
    bool cycleSeriesSelection();
    void deselectSeries();
    void notifySeriesRemoved(const Series* series);
    const std::vector<SeriesSelection>& selectedSeries() const { return selected_series_; }
    AxesBase*                           selectedSeriesAxes() const;

    // ── Surface lifecycle ──────────────────────────────────────────────────

    // Returns true if the window has a valid attached Vulkan surface.
    bool isAttached() const { return attached_; }

    // Attempt to attach (create surface + swapchain) if exposed and not yet attached.
    // Returns true if attachment succeeded (or was already attached).
    bool ensureAttached();

    // Detach from the runtime (destroy swapchain + surface resources).
    void forceDetach();

    // ── Surface generation ─────────────────────────────────────────────────
    // Monotonically increasing token.  Invalidated (set to 0) when the
    // platform surface is about to be destroyed.  Incremented on creation.
    // Every asynchronous frame request must validate the generation before
    // touching Vulkan resources.
    uint32_t surface_generation() const { return surface_generation_; }
    bool     surface_valid() const { return surface_generation_ != 0; }

    // ── Rendering ──────────────────────────────────────────────────────────

    // Render a single frame.  Called from the update event or timer.
    void renderFrame();

    // Request an update via QWindow::requestUpdate().
    // Coalesces duplicate requests — safe to call from any thread via signal.
    void requestFrame();

    // Capture the most recently rendered native canvas through Vulkan
    // readback. Optional target dimensions are applied deterministically.
    bool captureRgba(std::vector<uint8_t>& pixels,
                     uint32_t&             width,
                     uint32_t&             height,
                     uint32_t              target_width  = 0,
                     uint32_t              target_height = 0);
    bool savePng(const std::string& path, uint32_t target_width = 0, uint32_t target_height = 0);

    // Start/stop a continuous animation timer (~60 FPS).
    // Use only while animation or streaming requires continuous frames.
    void startAnimationTimer();
    void stopAnimationTimer();
    bool hasAnimationTimer() const { return timer_ != nullptr; }

    // ── DPR tracking ───────────────────────────────────────────────────────

    qreal lastDpr() const { return last_dpr_; }

   signals:
    // Emitted after input hit-testing with authoritative data coordinates.
    // valid=false clears the status readout when the pointer is outside axes.
    void cursorMoved(double x, double y, bool valid);
    // Emitted after each rendered frame with FPS and GPU frame time (ms).
    void frameStats(int fps, double gpu_ms);
    // Emitted only for user input that can change serialized figure, view, or
    // overlay state. Hover-only mouse moves and render frames do not emit it.
    void persistentStateChanged();

   protected:
    // QWindow overrides
    bool event(QEvent* event) override;
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

   private:
    void handleSurfaceCreated();
    void handleSurfaceAboutToBeDestroyed();
    void syncSeriesSelection();

    QtRuntime*                     runtime_ = nullptr;
    Figure*                        figure_  = nullptr;
    InputHandler*                  input_   = nullptr;
    std::function<void()>          inspector_toggle_;
    std::function<bool()>          inspector_is_open_;
    bool                           crosshair_enabled_ = false;
    std::optional<OverlaySnapshot> pending_overlay_snapshot_;
    std::vector<SeriesSelection>   selected_series_;
    AnimationTickCallback          animation_tick_;
    QTimer*                        timer_    = nullptr;
    bool                           attached_ = false;
    qreal                          last_dpr_ = 1.0;

    // Surface generation: 0 = invalid, >0 = valid surface.
    // Incremented each time a new platform surface is created.
    uint32_t surface_generation_ = 0;

    // Frame timing
    using Clock = std::chrono::steady_clock;
    Clock::time_point last_frame_time_;
    bool              has_last_frame_time_ = false;
};

}   // namespace spectra::adapters::qt
