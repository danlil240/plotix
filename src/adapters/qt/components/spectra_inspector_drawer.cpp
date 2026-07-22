// spectra_inspector_drawer.cpp — Custom inspector drawer implementation.

#include "spectra_inspector_drawer.hpp"
#include "spectra_design_tokens.hpp"
#include "spectra_dock_header.hpp"
#include "spectra_panel.hpp"
#include "spectra_controls.hpp"

#include <QHBoxLayout>
#include <QLabel>
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

    // Content panel
    content_ = new SpectraPanel(this);
    auto* content_layout = content_->content_layout();

    // Figure title
    auto* title_label = new QLabel("Title");
    title_label->setStyleSheet("color: #8A909C; font-size: 11px; font-family: 'Inter';");
    content_layout->addWidget(title_label);

    auto* title_edit = new SpectraLineEdit(content_);
    title_edit->setText("Figure 1");
    content_layout->addWidget(title_edit);

    content_layout->addSpacing(12);

    // Axes section
    auto* axes_label = new QLabel("Axes");
    axes_label->setStyleSheet("color: #8A909C; font-size: 11px; font-family: 'Inter';");
    content_layout->addWidget(axes_label);

    auto* x_label_check = new SpectraCheckBox("X Label Visible", content_);
    x_label_check->setChecked(true);
    content_layout->addWidget(x_label_check);

    auto* y_label_check = new SpectraCheckBox("Y Label Visible", content_);
    y_label_check->setChecked(true);
    content_layout->addWidget(y_label_check);

    auto* grid_check = new SpectraCheckBox("Show Grid", content_);
    grid_check->setChecked(true);
    content_layout->addWidget(grid_check);

    content_layout->addSpacing(12);

    // Legend
    auto* legend_label = new QLabel("Legend");
    legend_label->setStyleSheet("color: #8A909C; font-size: 11px; font-family: 'Inter';");
    content_layout->addWidget(legend_label);

    auto* legend_check = new SpectraCheckBox("Show Legend", content_);
    legend_check->setChecked(false);
    content_layout->addWidget(legend_check);

    content_layout->addStretch();

    layout_->addWidget(content_, 1);
}

void SpectraInspectorDrawer::open()
{
    open_ = true;
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
