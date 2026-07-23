// spectra_inspector_drawer.cpp — Custom inspector drawer implementation.

#include "spectra_inspector_drawer.hpp"
#include "spectra_design_tokens.hpp"
#include "spectra_dock_header.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

SpectraInspectorDrawer::~SpectraInspectorDrawer() = default;

SpectraInspectorDrawer::SpectraInspectorDrawer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(spectra_geometry().inspector_width);
    setVisible(false);  // Hidden by default

    build_ui();
}

void SpectraInspectorDrawer::build_ui()
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // Custom dock header
    header_ = new SpectraDockHeader("Inspector", this);
    header_->set_closable(true);
    header_->set_collapsible(false);
    connect(header_, &SpectraDockHeader::close_requested,
            this, &SpectraInspectorDrawer::close);
    layout_->addWidget(header_);

}

void SpectraInspectorDrawer::set_content_widget(QWidget* widget)
{
    if (!widget || widget == content_)
        return;

    if (content_)
    {
        layout_->removeWidget(content_);
        content_->hide();
        content_->setParent(nullptr);
    }

    content_ = widget;
    content_->setParent(this);
    layout_->addWidget(content_, 1);
    content_->show();
}

void SpectraInspectorDrawer::open()
{
    open_ = true;
    if (content_)
        content_->show();
    show();
    emit opened();
}

void SpectraInspectorDrawer::close()
{
    open_ = false;
    hide();
    emit closed();
}

void SpectraInspectorDrawer::toggle()
{
    if (open_)
        close();
    else
        open();
}

int SpectraInspectorDrawer::width_hint() const
{
    return spectra_geometry().inspector_width;
}

void SpectraInspectorDrawer::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Background
    p.fillRect(rect(), c.panel_surface);

    // Left border
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, 0, 0, height());
}

}   // namespace spectra::adapters::qt
