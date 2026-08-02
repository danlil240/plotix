// spectra_panel.cpp — Custom panel implementation.

#include "spectra_panel.hpp"
#include "spectra_design_tokens.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

SpectraPanel::SpectraPanel(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(12, 12, 12, 12);
    layout_->setSpacing(8);
}

void SpectraPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Background
    p.fillRect(rect(), c.panel_surface);

    // Left border accent
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, 0, 0, height());
}

}   // namespace spectra::adapters::qt
