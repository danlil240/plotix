#pragma once

// FigureCanvasWidget — QWidget wrapper around SpectraVulkanWindow.
//
// Uses QWidget::createWindowContainer() to embed a Vulkan QWindow inside
// a QWidget-based layout.  This is the standard Qt pattern for embedding
// native render surfaces in widget hierarchies.
//
// Ownership: FigureCanvasWidget owns the SpectraVulkanWindow via
// createWindowContainer (Qt parent-child).

#include <QWidget>

#include "spectra_vulkan_window.hpp"

namespace spectra
{
class Figure;
class InputHandler;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtRuntime;

class FigureCanvasWidget : public QWidget
{
    Q_OBJECT

   public:
    explicit FigureCanvasWidget(QtRuntime*   runtime,
                                Figure*      figure   = nullptr,
                                InputHandler* input   = nullptr,
                                QWidget*     parent   = nullptr);

    // Access the underlying Vulkan window
    SpectraVulkanWindow* vulkanWindow() const { return window_; }

    // Forward configuration to the Vulkan window
    void setFigure(Figure* fig);
    void setInputHandler(InputHandler* ih);
    void setAnimationTick(SpectraVulkanWindow::AnimationTickCallback cb);

    // Animation timer control
    void startAnimationTimer();
    void stopAnimationTimer();

   signals:
    void activated();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    SpectraVulkanWindow* window_ = nullptr;
};

}   // namespace spectra::adapters::qt
