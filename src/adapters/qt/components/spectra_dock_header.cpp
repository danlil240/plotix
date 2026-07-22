// spectra_dock_header.cpp — Custom dock header implementation.

#include "spectra_dock_header.hpp"
#include "spectra_design_tokens.hpp"

#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>

namespace spectra::adapters::qt
{

SpectraDockHeader::SpectraDockHeader(const QString& title, QWidget* parent)
    : QWidget(parent), title_(title)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(4);

    // Chevron (for collapsible sections)
    btn_chevron_ = new QPushButton(this);
    btn_chevron_->setFixedSize(16, 16);
    btn_chevron_->setFlat(true);
    btn_chevron_->setCursor(Qt::PointingHandCursor);
    btn_chevron_->setFocusPolicy(Qt::NoFocus);
    btn_chevron_->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #8A909C; }"
        "QPushButton:hover { color: #E8ECF1; }");
    btn_chevron_->setText(QStringLiteral("\u25B8"));
    connect(btn_chevron_, &QPushButton::clicked,
            this, &SpectraDockHeader::toggle_collapsed);
    layout->addWidget(btn_chevron_);

    // Title
    layout->addSpacing(4);
    auto& fm = SpectraFontManager::instance();

    // Close button
    btn_close_ = new QPushButton(this);
    btn_close_->setFixedSize(16, 16);
    btn_close_->setFlat(true);
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setFocusPolicy(Qt::NoFocus);
    btn_close_->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #8A909C; }"
        "QPushButton:hover { color: #E8ECF1; }");
    btn_close_->setText(QStringLiteral("\u2715"));
    connect(btn_close_, &QPushButton::clicked,
            this, &SpectraDockHeader::close_requested);

    layout->addStretch();
    layout->addWidget(btn_close_);
}

void SpectraDockHeader::set_title(const QString& title)
{
    title_ = title;
    update();
}

void SpectraDockHeader::set_closable(bool closable)
{
    closable_ = closable;
    btn_close_->setVisible(closable);
}

void SpectraDockHeader::set_collapsible(bool collapsible)
{
    collapsible_ = collapsible;
    btn_chevron_->setVisible(collapsible);
}

void SpectraDockHeader::set_collapsed(bool collapsed)
{
    collapsed_ = collapsed;
    btn_chevron_->setText(collapsed ? QStringLiteral("\u25B8") :
                           QStringLiteral("\u25BE"));
}

void SpectraDockHeader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    auto& fm = SpectraFontManager::instance();

    // Background
    p.fillRect(rect(), c.panel_surface);

    // Bottom hairline
    p.setPen(QPen(c.border_subtle, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    // Title text
    p.setFont(fm.font_medium());
    p.setPen(c.text_primary);
    QRect text_rect(32, 0, width() - 60, height());
    p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, title_);
}

}   // namespace spectra::adapters::qt
