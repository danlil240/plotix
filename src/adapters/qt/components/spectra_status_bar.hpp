#pragma once

// SpectraStatusBar — bottom status bar.
//
// Shows: cursor coordinates, active tool chip, zoom level, FPS, GPU frame time.
// No debug text.

#include <QWidget>
#include <QHBoxLayout>

namespace spectra::adapters::qt
{

class SpectraStatusChip;

class SpectraStatusBar : public QWidget
{
    Q_OBJECT
   public:
    explicit SpectraStatusBar(QWidget* parent = nullptr);
    ~SpectraStatusBar() override;
    SpectraStatusBar(const SpectraStatusBar&)            = delete;
    SpectraStatusBar& operator=(const SpectraStatusBar&) = delete;

    void    set_cursor_coords(double x, double y);
    void    clear_cursor_coords();
    void    set_message(const QString& message);
    QString message() const;
    void    set_active_tool(const QString& tool_name);
    QString active_tool() const;
    void    set_zoom(double zoom);
    double  zoom() const { return zoom_; }
    void    set_fps(int fps);
    void    set_gpu_frame_time(double ms);

    int height_hint() const;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QHBoxLayout* layout_ = nullptr;

    SpectraStatusChip* coord_chip_ = nullptr;
    SpectraStatusChip* tool_chip_  = nullptr;
    SpectraStatusChip* zoom_chip_  = nullptr;
    SpectraStatusChip* fps_chip_   = nullptr;
    SpectraStatusChip* gpu_chip_   = nullptr;
    double             zoom_       = 1.0;
};

}   // namespace spectra::adapters::qt
