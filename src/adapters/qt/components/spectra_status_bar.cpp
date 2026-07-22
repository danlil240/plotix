// spectra_status_bar.cpp — Status bar implementation.

#include "spectra_status_bar.hpp"
#include "spectra_status_chip.hpp"
#include "spectra_design_tokens.hpp"

#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>

namespace spectra::adapters::qt
{

SpectraStatusBar::~SpectraStatusBar() = default;

SpectraStatusBar::SpectraStatusBar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(spectra_geometry().status_bar_height);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(16, 0, 16, 0);
    layout_->setSpacing(12);

    // Left: cursor coordinates
    coord_chip_ = new SpectraStatusChip("x: —  y: —",
                                        SpectraStatusChip::Type::Normal, this);
    coord_chip_->setObjectName("spectra_status_message");
    layout_->addWidget(coord_chip_);

    // Active tool
    tool_chip_ = new SpectraStatusChip("Pan",
                                       SpectraStatusChip::Type::Accent, this);
    layout_->addWidget(tool_chip_);

    // Zoom
    zoom_chip_ = new SpectraStatusChip("100%",
                                       SpectraStatusChip::Type::Normal, this);
    layout_->addWidget(zoom_chip_);

    layout_->addStretch();

    // Right: FPS (green)
    fps_chip_ = new SpectraStatusChip("0 fps",
                                      SpectraStatusChip::Type::Success, this);
    layout_->addWidget(fps_chip_);

    // GPU frame time
    gpu_chip_ = new SpectraStatusChip("GPU: 0.0ms",
                                      SpectraStatusChip::Type::Normal, this);
    layout_->addWidget(gpu_chip_);
}

void SpectraStatusBar::set_cursor_coords(double x, double y)
{
    coord_chip_->set_text(QString("x: %1  y: %2")
                              .arg(x, 0, 'f', 2)
                              .arg(y, 0, 'f', 2));
}

void SpectraStatusBar::set_message(const QString& message)
{
    coord_chip_->set_text(message);
}

QString SpectraStatusBar::message() const
{
    return coord_chip_->text();
}

void SpectraStatusBar::set_active_tool(const QString& tool_name)
{
    tool_chip_->set_text(tool_name);
}

void SpectraStatusBar::set_zoom(double zoom)
{
    zoom_chip_->set_text(QString("%1%").arg(static_cast<int>(zoom * 100)));
}

void SpectraStatusBar::set_fps(int fps)
{
    fps_chip_->set_text(QString("%1 FPS").arg(fps));
}

void SpectraStatusBar::set_gpu_frame_time(double ms)
{
    gpu_chip_->set_text(QString("GPU: %1ms").arg(ms, 0, 'f', 1));
}

int SpectraStatusBar::height_hint() const
{
    return spectra_geometry().status_bar_height;
}

void SpectraStatusBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Background
    p.fillRect(rect(), c.workspace_surface);

    // Top hairline
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, 0, width(), 0);
}

}   // namespace spectra::adapters::qt
