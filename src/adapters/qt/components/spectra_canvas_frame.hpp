#pragma once

// SpectraCanvasFrame — wrapper around the figure canvas with custom frame.
//
// Provides a rounded, bordered container for the Vulkan render surface.

#include <QWidget>

class QVBoxLayout;

namespace spectra::adapters::qt
{

class SpectraCanvasFrame : public QWidget
{
    Q_OBJECT
   public:
    explicit SpectraCanvasFrame(QWidget* central_widget, QWidget* parent = nullptr);
    ~SpectraCanvasFrame() override;
    SpectraCanvasFrame(const SpectraCanvasFrame&)            = delete;
    SpectraCanvasFrame& operator=(const SpectraCanvasFrame&) = delete;

    void set_central_widget(QWidget* widget);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QWidget*     central_ = nullptr;
    QVBoxLayout* layout_  = nullptr;
};

}   // namespace spectra::adapters::qt
