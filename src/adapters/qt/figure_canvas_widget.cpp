// figure_canvas_widget.cpp — QWidget wrapper around SpectraVulkanWindow.

#include "figure_canvas_widget.hpp"

#include "qt_runtime.hpp"

#include <spectra/figure.hpp>
#include "ui/input/input.hpp"

#include <QVBoxLayout>
#include <QEvent>

namespace spectra::adapters::qt
{

FigureCanvasWidget::FigureCanvasWidget(QtRuntime*    runtime,
                                       Figure*       figure,
                                       InputHandler* input,
                                       QWidget*      parent)
    : QWidget(parent)
{
    window_ = new SpectraVulkanWindow;
    window_->setRuntime(runtime);
    if (figure)
        window_->setFigure(figure);
    if (input)
        window_->setInputHandler(input);

    // Set the Vulkan instance BEFORE createWindowContainer — Qt may create
    // the platform surface during container embedding, and a VulkanSurface
    // without a QVulkanInstance causes a segfault.
    if (runtime && runtime->vulkan_instance())
        window_->setVulkanInstance(runtime->vulkan_instance());

    // Embed the QWindow into a QWidget container
    auto* container = QWidget::createWindowContainer(window_, this);
    container->setMinimumSize(400, 300);
    container->setFocusPolicy(Qt::StrongFocus);
    container->installEventFilter(this);
    window_->installEventFilter(this);

    // Layout: container fills the widget
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(container);
}

bool FigureCanvasWidget::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == window_ || watched->parent() == this)
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress))
        emit activated();
    return QWidget::eventFilter(watched, event);
}

void FigureCanvasWidget::setFigure(Figure* fig)
{
    if (window_)
        window_->setFigure(fig);
}

void FigureCanvasWidget::setInputHandler(InputHandler* ih)
{
    if (window_)
        window_->setInputHandler(ih);
}

void FigureCanvasWidget::setAnimationTick(SpectraVulkanWindow::AnimationTickCallback cb)
{
    if (window_)
        window_->setAnimationTick(std::move(cb));
}

void FigureCanvasWidget::startAnimationTimer()
{
    if (window_)
        window_->startAnimationTimer();
}

void FigureCanvasWidget::stopAnimationTimer()
{
    if (window_)
        window_->stopAnimationTimer();
}

}   // namespace spectra::adapters::qt
