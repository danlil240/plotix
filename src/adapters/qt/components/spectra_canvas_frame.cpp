// spectra_canvas_frame.cpp — Canvas frame implementation.

#include "spectra_canvas_frame.hpp"
#include "spectra_design_tokens.hpp"

#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

SpectraCanvasFrame::~SpectraCanvasFrame() = default;

SpectraCanvasFrame::SpectraCanvasFrame(QWidget* central_widget, QWidget* parent)
    : QWidget(parent), central_(central_widget)
{
    setAttribute(Qt::WA_StyledBackground, true);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    if (central_)
        layout_->addWidget(central_);
}

void SpectraCanvasFrame::set_central_widget(QWidget* widget)
{
    if (central_)
        delete central_;

    central_ = widget;
    if (central_)
        layout_->addWidget(central_);
}

void SpectraCanvasFrame::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    p.fillRect(rect(), c.workspace_surface);
}

}   // namespace spectra::adapters::qt
