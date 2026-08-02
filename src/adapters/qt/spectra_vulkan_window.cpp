// spectra_vulkan_window.cpp — production QWindow canvas for Vulkan rendering.

#include "spectra_vulkan_window.hpp"

#include "qt_input_router.hpp"
#include "qt_runtime.hpp"
#include "render/vulkan/vk_backend.hpp"

#include "ui/app/perf_metrics.hpp"
#include "ui/input/input.hpp"

#include <spectra/embed.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/export.hpp>

#include <QtGui/QMouseEvent>
#include <QtGui/QPlatformSurfaceEvent>
#include <QImage>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace spectra::adapters::qt
{

SpectraVulkanWindow::SpectraVulkanWindow(QWindow* parent) : QWindow(parent)
{
    setSurfaceType(QSurface::VulkanSurface);
    setMinimumSize(QSize(400, 300));
    resize(800, 600);
}

void SpectraVulkanWindow::setRuntime(QtRuntime* rt)
{
    runtime_ = rt;
    if (runtime_ && input_)
    {
        runtime_->set_input_handler(this, input_);
    }
    if (runtime_ && (inspector_toggle_ || inspector_is_open_))
    {
        runtime_->set_inspector_toggle_callbacks(this, inspector_toggle_, inspector_is_open_);
    }
    syncSeriesSelection();
}

void SpectraVulkanWindow::setFigure(Figure* fig)
{
    figure_ = fig;
    if (attached_ && runtime_ && figure_ && pending_overlay_snapshot_
        && runtime_->restore_overlay_snapshot(this, *figure_, *pending_overlay_snapshot_))
    {
        pending_overlay_snapshot_.reset();
    }
}

void SpectraVulkanWindow::setInputHandler(InputHandler* ih)
{
    input_ = ih;
    if (runtime_)
    {
        runtime_->set_input_handler(this, input_);
    }
}

void SpectraVulkanWindow::setInspectorToggleCallbacks(std::function<void()> toggle,
                                                      std::function<bool()> is_open)
{
    inspector_toggle_  = std::move(toggle);
    inspector_is_open_ = std::move(is_open);
    if (runtime_)
    {
        runtime_->set_inspector_toggle_callbacks(this, inspector_toggle_, inspector_is_open_);
    }
}

void SpectraVulkanWindow::toggleInspector()
{
    if (inspector_toggle_)
        inspector_toggle_();
}

bool SpectraVulkanWindow::isInspectorOpen() const
{
    return inspector_is_open_ ? inspector_is_open_() : false;
}

void SpectraVulkanWindow::selectSeries(Figure* figure,
                                       Axes*   axes,
                                       int     axes_index,
                                       Series* series,
                                       int     series_index)
{
    if (!figure || !axes || !series)
        return;
    if (selected_series_.size() == 1 && selected_series_[0].series == series)
    {
        deselectSeries();
        return;
    }
    selectSeriesNoToggle(figure, axes, axes_index, series, series_index);
}

void SpectraVulkanWindow::selectSeriesNoToggle(Figure* figure,
                                               Axes*   axes,
                                               int     axes_index,
                                               Series* series,
                                               int     series_index)
{
    if (!figure || figure != figure_ || !axes || !series)
        return;
    selected_series_ = {{static_cast<AxesBase*>(axes), axes, series, axes_index, series_index}};
    syncSeriesSelection();
}

void SpectraVulkanWindow::setSeriesSelection(std::vector<SeriesSelection> selection)
{
    selected_series_.clear();
    for (const auto& entry : selection)
    {
        if (!entry.owner || !entry.series)
            continue;
        const auto duplicate = std::find_if(selected_series_.begin(),
                                            selected_series_.end(),
                                            [&entry](const SeriesSelection& current)
                                            { return current.series == entry.series; });
        if (duplicate == selected_series_.end())
            selected_series_.push_back(entry);
    }
    syncSeriesSelection();
}

bool SpectraVulkanWindow::cycleSeriesSelection()
{
    if (!figure_)
        return false;

    for (size_t axes_index = 0; axes_index < figure_->axes().size(); ++axes_index)
    {
        Axes* axes = figure_->axes_mut()[axes_index].get();
        if (!axes || axes->series().empty())
            continue;

        int next = 0;
        if (selected_series_.size() == 1 && selected_series_[0].owner == axes
            && selected_series_[0].series_index >= 0)
        {
            next = (selected_series_[0].series_index + 1) % static_cast<int>(axes->series().size());
        }
        Series* series = axes->series_mut()[static_cast<size_t>(next)].get();
        if (!series)
            return false;
        selectSeries(figure_, axes, static_cast<int>(axes_index), series, next);
        return true;
    }
    return false;
}

void SpectraVulkanWindow::deselectSeries()
{
    if (selected_series_.empty())
        return;
    selected_series_.clear();
    syncSeriesSelection();
}

void SpectraVulkanWindow::notifySeriesRemoved(const Series* series)
{
    if (!series)
        return;
    std::erase_if(selected_series_,
                  [series](const SeriesSelection& entry) { return entry.series == series; });
    if (runtime_)
        runtime_->notify_series_removed(this, series);
    syncSeriesSelection();
}

AxesBase* SpectraVulkanWindow::selectedSeriesAxes() const
{
    return selected_series_.empty() ? nullptr : selected_series_.back().owner;
}

void SpectraVulkanWindow::syncSeriesSelection()
{
    if (!runtime_)
        return;
    std::vector<QtSeriesSelectionEntry> selected;
    selected.reserve(selected_series_.size());
    for (const auto& entry : selected_series_)
    {
        if (entry.series)
        {
            selected.push_back({figure_,
                                entry.owner,
                                entry.axes,
                                entry.series,
                                entry.axes_index,
                                entry.series_index});
        }
    }
    runtime_->set_series_selection(this, selected);
    requestFrame();
}

void SpectraVulkanWindow::toggleCrosshair()
{
    crosshair_enabled_ = !crosshair_enabled_;
    if (pending_overlay_snapshot_)
        pending_overlay_snapshot_->crosshair_enabled = crosshair_enabled_;
    if (runtime_)
        runtime_->set_crosshair(this, crosshair_enabled_);
    requestFrame();
    emit persistentStateChanged();
}

bool SpectraVulkanWindow::captureOverlaySnapshot(OverlaySnapshot& snapshot)
{
    if (runtime_ && figure_ && runtime_->capture_overlay_snapshot(this, *figure_, snapshot))
        return true;
    if (pending_overlay_snapshot_)
    {
        snapshot = *pending_overlay_snapshot_;
        return true;
    }
    snapshot.crosshair_enabled = crosshair_enabled_;
    return figure_ != nullptr;
}

void SpectraVulkanWindow::restoreOverlaySnapshot(const OverlaySnapshot& snapshot)
{
    crosshair_enabled_        = snapshot.crosshair_enabled;
    pending_overlay_snapshot_ = snapshot;
    if (runtime_ && figure_
        && runtime_->restore_overlay_snapshot(this, *figure_, *pending_overlay_snapshot_))
    {
        pending_overlay_snapshot_.reset();
    }
    requestFrame();
}

size_t SpectraVulkanWindow::markerCount()
{
    if (pending_overlay_snapshot_)
        return pending_overlay_snapshot_->markers.size();
    return runtime_ ? runtime_->marker_count(this) : 0;
}

bool SpectraVulkanWindow::clearMarkers()
{
    bool changed = false;
    if (pending_overlay_snapshot_ && !pending_overlay_snapshot_->markers.empty())
    {
        pending_overlay_snapshot_->markers.clear();
        changed = true;
    }
    else if (runtime_)
    {
        changed = runtime_->clear_markers(this);
    }
    if (!changed)
        return false;
    requestFrame();
    emit persistentStateChanged();
    return true;
}

void SpectraVulkanWindow::setAnimationTick(AnimationTickCallback cb)
{
    animation_tick_ = std::move(cb);
}

void SpectraVulkanWindow::tickAnimation(float dt)
{
    if (animation_tick_)
        animation_tick_(dt);
}

bool SpectraVulkanWindow::ensureAttached()
{
    if (attached_)
        return true;
    if (!runtime_ || !isExposed())
        return false;

    auto dpr = devicePixelRatio();
    auto w   = static_cast<uint32_t>(width() * dpr);
    auto h   = static_cast<uint32_t>(height() * dpr);
    if (w == 0 || h == 0)
        return false;

    if (!runtime_->attach_window(this, w, h))
        return false;

    runtime_->set_input_handler(this, input_);
    if (pending_overlay_snapshot_ && figure_
        && runtime_->restore_overlay_snapshot(this, *figure_, *pending_overlay_snapshot_))
    {
        pending_overlay_snapshot_.reset();
    }
    else
    {
        runtime_->set_crosshair(this, crosshair_enabled_);
    }
    syncSeriesSelection();
    runtime_->set_inspector_toggle_callbacks(this, inspector_toggle_, inspector_is_open_);
    attached_ = true;
    last_dpr_ = dpr;
    return true;
}

void SpectraVulkanWindow::forceDetach()
{
    if (runtime_ && attached_)
    {
        OverlaySnapshot snapshot;
        if (captureOverlaySnapshot(snapshot))
            pending_overlay_snapshot_ = std::move(snapshot);
        runtime_->detach_window(this);
    }
    attached_           = false;
    surface_generation_ = 0;   // Invalidate generation on detach
}

void SpectraVulkanWindow::requestFrame()
{
    if (surface_valid())
    {
        requestUpdate();
    }
}

bool SpectraVulkanWindow::captureRgba(std::vector<uint8_t>& pixels,
                                      uint32_t&             width_out,
                                      uint32_t&             height_out,
                                      uint32_t              target_width,
                                      uint32_t              target_height)
{
    if (!runtime_ || !runtime_->backend() || !figure_ || !isExposed() || !ensureAttached())
        return false;

    renderFrame();
    const qreal dpr           = devicePixelRatio();
    const auto  source_width  = static_cast<uint32_t>(width() * dpr);
    const auto  source_height = static_cast<uint32_t>(height() * dpr);
    if (source_width == 0 || source_height == 0)
        return false;

    std::vector<uint8_t> source(static_cast<size_t>(source_width) * source_height * 4);
    if (!runtime_->backend()->readback_framebuffer(source.data(), source_width, source_height))
        return false;

    width_out  = target_width > 0 ? target_width : source_width;
    height_out = target_height > 0 ? target_height : source_height;
    if (width_out == source_width && height_out == source_height)
    {
        pixels = std::move(source);
        return true;
    }

    const QImage source_image(source.data(),
                              static_cast<int>(source_width),
                              static_cast<int>(source_height),
                              static_cast<int>(source_width * 4),
                              QImage::Format_RGBA8888);
    const QImage scaled = source_image.scaled(static_cast<int>(width_out),
                                              static_cast<int>(height_out),
                                              Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);
    if (scaled.isNull())
        return false;
    pixels.resize(static_cast<size_t>(width_out) * height_out * 4);
    for (uint32_t row = 0; row < height_out; ++row)
    {
        std::memcpy(pixels.data() + static_cast<size_t>(row) * width_out * 4,
                    scaled.constScanLine(static_cast<int>(row)),
                    static_cast<size_t>(width_out) * 4);
    }
    return true;
}

bool SpectraVulkanWindow::savePng(const std::string& path,
                                  uint32_t           target_width,
                                  uint32_t           target_height)
{
    std::vector<uint8_t> pixels;
    uint32_t             captured_width  = 0;
    uint32_t             captured_height = 0;
    return captureRgba(pixels, captured_width, captured_height, target_width, target_height)
           && ImageExporter::write_png(path, pixels.data(), captured_width, captured_height);
}

void SpectraVulkanWindow::startAnimationTimer()
{
    if (timer_)
        return;   // Already running

    has_last_frame_time_ = false;
    timer_               = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &SpectraVulkanWindow::renderFrame);
    timer_->start(16);   // ~60 FPS
}

void SpectraVulkanWindow::stopAnimationTimer()
{
    if (timer_)
    {
        timer_->stop();
        delete timer_;
        timer_ = nullptr;
    }
}

void SpectraVulkanWindow::handleSurfaceCreated()
{
    ++surface_generation_;
    SPECTRA_LOG_DEBUG("qt_window", "Surface created, generation={}", surface_generation_);
}

void SpectraVulkanWindow::handleSurfaceAboutToBeDestroyed()
{
    if (surface_generation_ == 0)
        return;

    SPECTRA_LOG_DEBUG("qt_window",
                      "Surface about to be destroyed, generation={} invalidated",
                      surface_generation_);

    // Stop new frames immediately
    surface_generation_ = 0;

    // Detach from runtime — this waits on fences and destroys swapchain resources
    forceDetach();
}

bool SpectraVulkanWindow::event(QEvent* event)
{
    if (event && event->type() == QEvent::Leave)
    {
        if (input_)
            input_->clear_cursor_readout();
        emit cursorMoved(0.0, 0.0, false);
    }
    if (event && event->type() == QEvent::PlatformSurface && runtime_)
    {
        auto* platform_event = static_cast<QPlatformSurfaceEvent*>(event);
        if (platform_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            handleSurfaceAboutToBeDestroyed();
        }
        else if (platform_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
        {
            handleSurfaceCreated();
            (void)ensureAttached();
        }
    }
    return QWindow::event(event);
}

void SpectraVulkanWindow::exposeEvent(QExposeEvent* /*event*/)
{
    if (!isExposed())
        return;

    if (!attached_ && runtime_)
    {
        if (ensureAttached())
        {
            renderFrame();
        }
        return;
    }

    if (attached_ && runtime_)
    {
        // Check for DPR change (moved to a different-scale monitor)
        auto dpr = devicePixelRatio();
        if (dpr != last_dpr_)
        {
            last_dpr_ = dpr;
            runtime_->mark_swapchain_dirty(this);
        }

        // Ensure a frame is rendered promptly after becoming visible again
        renderFrame();
    }
}

void SpectraVulkanWindow::resizeEvent(QResizeEvent* /*event*/)
{
    if (!attached_ || !runtime_)
        return;

    // Set dirty flag — actual swapchain recreation is deferred to
    // begin_frame() at the next frame boundary.
    runtime_->mark_swapchain_dirty(this);
}

void SpectraVulkanWindow::renderFrame()
{
    if (!attached_ || !runtime_ || !figure_)
        return;

    // Surface generation guard: skip rendering when surface is invalid
    if (!surface_valid())
        return;

    // Visibility guard: skip rendering when window is not exposed
    if (!isExposed())
        return;

    auto dpr = devicePixelRatio();
    auto w   = static_cast<uint32_t>(width() * dpr);
    auto h   = static_cast<uint32_t>(height() * dpr);
    if (w == 0 || h == 0)
        return;

    float      dt  = 1.0f / 60.0f;
    const auto now = Clock::now();
    if (has_last_frame_time_)
    {
        dt = std::chrono::duration<float>(now - last_frame_time_).count();
        dt = std::clamp(dt, 1.0f / 240.0f, 0.1f);
    }
    last_frame_time_     = now;
    has_last_frame_time_ = true;

    if (input_)
    {
        input_->update(dt);
    }
    tickAnimation(dt);

    if (!runtime_->render_window(this, *figure_))
        return;

    PerfMetrics::instance().increment_frame_count();

    // Emit frame stats for status bar updates
    const double gpu_ms = dt * 1000.0;
    const int    fps    = dt > 0.0 ? static_cast<int>(1.0 / dt) : 0;
    emit         frameStats(fps, gpu_ms);
}

void SpectraVulkanWindow::mouseMoveEvent(QMouseEvent* event)
{
    auto pos = event->position();
    if (!input_)
    {
        emit cursorMoved(0.0, 0.0, false);
        return;
    }
    auto dpr = devicePixelRatio();
    input_->on_mouse_move(pos.x() * dpr, pos.y() * dpr);
    const CursorReadout& readout = input_->cursor_readout();
    emit                 cursorMoved(readout.data_x, readout.data_y, readout.valid);
    requestFrame();
}

void SpectraVulkanWindow::mousePressEvent(QMouseEvent* event)
{
    if (!input_)
        return;
    auto pos = event->position();
    auto dpr = devicePixelRatio();
    int  btn = QtInputRouter::qtButtonToSpectra(event->button());
    int  mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_mouse_button(btn, spectra::embed::ACTION_PRESS, mod, pos.x() * dpr, pos.y() * dpr);
    requestFrame();
}

void SpectraVulkanWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (!input_)
        return;
    auto pos = event->position();
    auto dpr = devicePixelRatio();
    int  btn = QtInputRouter::qtButtonToSpectra(event->button());
    int  mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_mouse_button(btn, spectra::embed::ACTION_RELEASE, mod, pos.x() * dpr, pos.y() * dpr);
    requestFrame();
    emit persistentStateChanged();
}

void SpectraVulkanWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!input_)
        return;
    auto pos = event->position();
    auto dpr = devicePixelRatio();
    int  btn = QtInputRouter::qtButtonToSpectra(event->button());
    int  mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_mouse_button(btn, spectra::embed::ACTION_PRESS, mod, pos.x() * dpr, pos.y() * dpr);
    requestFrame();
}

void SpectraVulkanWindow::wheelEvent(QWheelEvent* event)
{
    if (!input_)
        return;
    auto  pos = event->position();
    auto  dpr = devicePixelRatio();
    float dy  = static_cast<float>(event->angleDelta().y()) / 120.0f;
    float dx  = static_cast<float>(event->angleDelta().x()) / 120.0f;
    input_->on_scroll(dx, dy, pos.x() * dpr, pos.y() * dpr);
    requestFrame();
    emit persistentStateChanged();
}

void SpectraVulkanWindow::keyPressEvent(QKeyEvent* event)
{
    if (!input_)
        return;
    int key = QtInputRouter::qtKeyToSpectra(event->key());
    int mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_key(key, spectra::embed::ACTION_PRESS, mod);
    requestFrame();
    emit persistentStateChanged();
}

void SpectraVulkanWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (!input_)
        return;
    int key = QtInputRouter::qtKeyToSpectra(event->key());
    int mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_key(key, spectra::embed::ACTION_RELEASE, mod);
    requestFrame();
}

}   // namespace spectra::adapters::qt
