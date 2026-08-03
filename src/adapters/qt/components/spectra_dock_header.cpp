// spectra_dock_header.cpp — Custom dock header implementation.

#include "spectra_dock_header.hpp"
#include "spectra_design_tokens.hpp"
#include "spectra_inspector_widgets.hpp"
#include "ui/theme/design_tokens.hpp"

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
    // Legacy draws the panel eyebrow inside the panel's own padding: the caption
    // baseline sits PANEL_PADDING below the panel top and the segmented control
    // begins 58px down. See imgui_panels.cpp ("INSPECTOR" eyebrow).
    setFixedHeight(kEyebrowHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(kTextX, kTextY, static_cast<int>(ui::tokens::SPACE_2), 0);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    // Chevron (for collapsible sections)
    btn_chevron_ = new QPushButton(this);
    btn_chevron_->setFixedSize(16, 16);
    btn_chevron_->setFlat(true);
    btn_chevron_->setCursor(Qt::PointingHandCursor);
    btn_chevron_->setFocusPolicy(Qt::StrongFocus);
    btn_chevron_->setAccessibleName(QString("Collapse or expand %1").arg(title_));
    btn_chevron_->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 0; }");
    btn_chevron_->setText(QStringLiteral("\u25B8"));
    connect(btn_chevron_, &QPushButton::clicked, this, &SpectraDockHeader::toggle_collapsed);
    layout->addWidget(btn_chevron_);

    // Close button. Legacy toggles the panel from the nav rail and has no close
    // affordance here, so this stays a muted glyph that does not compete with
    // the caption.
    btn_close_ = new QPushButton(this);
    btn_close_->setFixedSize(16, 16);
    btn_close_->setFlat(true);
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setFocusPolicy(Qt::StrongFocus);
    btn_close_->setAccessibleName(QString("Close %1").arg(title_));
    btn_close_->setStyleSheet(
        QString("QPushButton { background: transparent; border: none; padding: 0; color: %1; }")
            .arg(spectra_colors().text_muted.name(QColor::HexArgb)));
    btn_close_->setText(QStringLiteral("\u2715"));
    connect(btn_close_, &QPushButton::clicked, this, &SpectraDockHeader::close_requested);

    layout->addStretch();
    layout->addWidget(btn_close_);
}

void SpectraDockHeader::set_title(const QString& title)
{
    title_ = title;
    btn_chevron_->setAccessibleName(QString("Collapse or expand %1").arg(title_));
    btn_close_->setAccessibleName(QString("Close %1").arg(title_));
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
    btn_chevron_->setText(collapsed ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"));
}

void SpectraDockHeader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // The legacy eyebrow has no band and no divider: it sits directly on the
    // panel surface, so the drawer's own background shows through.
    QFont f = SpectraFontManager::instance().font_medium();
    f.setPixelSize(imgui_font_px(kImGuiHeadingSize));
    p.setFont(f);

    QColor caption = c.text_muted;
    caption.setAlphaF(0.75f);
    p.setPen(caption);
    p.drawText(QRect(kTextX, kTextY, width() - kTextX - 32, kCaptionH),
               Qt::AlignLeft | Qt::AlignVCenter,
               title_.toUpper());
}

}   // namespace spectra::adapters::qt
