// spectra_status_chip.cpp — Status bar chip implementation.

#include "spectra_status_chip.hpp"
#include "spectra_design_tokens.hpp"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

namespace spectra::adapters::qt
{

SpectraStatusChip::SpectraStatusChip(const QString& text, Type type, QWidget* parent)
    : QWidget(parent), text_(text), type_(type)
{
    setAttribute(Qt::WA_StyledBackground, true);
}

void SpectraStatusChip::set_text(const QString& text)
{
    text_ = text;
    update();
    updateGeometry();
}

void SpectraStatusChip::set_type(Type type)
{
    type_ = type;
    update();
}

QSize SpectraStatusChip::sizeHint() const
{
    auto&        fm = SpectraFontManager::instance();
    QFont        f  = fm.font_status();
    QFontMetrics metrics(f);
    int          text_w = metrics.horizontalAdvance(text_);
    int          h      = 22;
    int          w      = text_w + 12;
    return QSize(w, h);
}

void SpectraStatusChip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c  = spectra_colors();
    auto&       fm = SpectraFontManager::instance();

    // Determine colors based on type
    QColor bg_color, text_color, border_color;
    switch (type_)
    {
        case Type::Accent:
            bg_color = QColor(c.cyan_accent.red(), c.cyan_accent.green(), c.cyan_accent.blue(), 30);
            text_color = c.cyan_accent;
            border_color =
                QColor(c.cyan_accent.red(), c.cyan_accent.green(), c.cyan_accent.blue(), 48);
            break;
        case Type::Success:
            bg_color =
                QColor(c.success_green.red(), c.success_green.green(), c.success_green.blue(), 30);
            text_color = c.success_green;
            border_color =
                QColor(c.success_green.red(), c.success_green.green(), c.success_green.blue(), 42);
            break;
        case Type::Warning:
            bg_color =
                QColor(c.warning_amber.red(), c.warning_amber.green(), c.warning_amber.blue(), 30);
            text_color = c.warning_amber;
            border_color =
                QColor(c.warning_amber.red(), c.warning_amber.green(), c.warning_amber.blue(), 42);
            break;
        default:
            bg_color   = QColor(c.elevated_surface.red(),
                              c.elevated_surface.green(),
                              c.elevated_surface.blue(),
                              82);
            text_color = c.text_secondary;
            border_color =
                QColor(c.border_subtle.red(), c.border_subtle.green(), c.border_subtle.blue(), 40);
            break;
    }

    // Draw pill
    QRectF r(0, (height() - 22) / 2.0, width(), 22);
    p.setBrush(bg_color);
    p.setPen(QPen(border_color, 1));
    p.drawRoundedRect(r, 11, 11);

    // Draw text
    p.setFont(fm.font_status());
    p.setPen(text_color);
    p.drawText(r, Qt::AlignCenter, text_);
}

}   // namespace spectra::adapters::qt
