// spectra_vulkan_window.cpp — production QWindow canvas for Vulkan rendering.

#include "spectra_vulkan_window.hpp"

#include "qt_input_router.hpp"
#include "qt_runtime.hpp"

#include "ui/input/input.hpp"

#include <spectra/embed.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <QtGui/QMouseEvent>
#include <QtGui/QPlatformSurfaceEvent>
#include <QTimer>

#include <algorithm>

namespace spectra::adapters::qt
{

SpectraVulkanWindow::SpectraVulkanWindow(QWindow* parent)
    : QWindow(parent)
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
}

void SpectraVulkanWindow::setFigure(Figure* fig)
{
    figure_ = fig;
}

void SpectraVulkanWindow::setInputHandler(InputHandler* ih)
{
    input_ = ih;
    if (runtime_)
    {
        runtime_->set_input_handler(this, input_);
    }
}

void SpectraVulkanWindow::setAnimationTick(AnimationTickCallback cb)
{
    animation_tick_ = std::move(cb);
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

    attached_ = true;
    last_dpr_ = dpr;
    return true;
}

void SpectraVulkanWindow::forceDetach()
{
    if (runtime_ && attached_)
    {
        runtime_->detach_window(this);
    }
    attached_           = false;
    surface_generation_ = 0;  // Invalidate generation on detach
}

void SpectraVulkanWindow::requestFrame()
{
    if (surface_valid())
    {
        requestUpdate();
    }
}

void SpectraVulkanWindow::startAnimationTimer()
{
    if (timer_)
        return;  // Already running

    has_last_frame_time_ = false;
    timer_               = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &SpectraVulkanWindow::renderFrame);
    timer_->start(16);  // ~60 FPS
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
    SPECTRA_LOG_DEBUG("qt_window",
                      "Surface created, generation={}",
                      surface_generation_);
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
    if (event && event->type() == QEvent::PlatformSurface && runtime_)
    {
        auto* platform_event = static_cast<QPlatformSurfaceEvent*>(event);
        if (platform_event->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
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
    if (animation_tick_)
    {
        animation_tick_(dt);
    }

    (void)runtime_->render_window(this, *figure_);
}

void SpectraVulkanWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!input_)
        return;
    auto pos = event->position();
    auto dpr = devicePixelRatio();
    input_->on_mouse_move(pos.x() * dpr, pos.y() * dpr);
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
    input_->on_mouse_button(btn,
                            spectra::embed::ACTION_PRESS,
                            mod,
                            pos.x() * dpr,
                            pos.y() * dpr);
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
    input_->on_mouse_button(btn,
                            spectra::embed::ACTION_RELEASE,
                            mod,
                            pos.x() * dpr,
                            pos.y() * dpr);
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
}

void SpectraVulkanWindow::keyPressEvent(QKeyEvent* event)
{
    if (!input_)
        return;
    int key = QtInputRouter::qtKeyToSpectra(event->key());
    int mod = QtInputRouter::qtModsToSpectra(event->modifiers());
    input_->on_key(key, spectra::embed::ACTION_PRESS, mod);
    requestFrame();
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
