// spectra_nav_button.cpp — Navigation rail button implementation.

#include "spectra_nav_button.hpp"
#include "spectra_design_tokens.hpp"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

namespace spectra::adapters::qt
{

SpectraNavButton::SpectraNavButton(const QString& icon_codepoint,
                                   const QString& label,
                                   const QString& shortcut_hint,
                                   QWidget*       parent)
    : QPushButton(parent),
      icon_codepoint_(icon_codepoint),
      label_(label),
      shortcut_hint_(shortcut_hint)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMinimumHeight(32);
    setMaximumHeight(64);
    setToolTip(label + (shortcut_hint.isEmpty() ? "" : " (" + shortcut_hint + ")"));

    setStyleSheet(QString(
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: %1px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(31, 34, 41, 180);"
        "}"
        "QPushButton:focus {"
        "  border: 1px solid #2A2D36;"
        "}"
    ).arg(spectra_geometry().radius_md));
}

void SpectraNavButton::set_active(bool active)
{
    active_ = active;
    update();
}

void SpectraNavButton::set_compact_mode(bool compact)
{
    compact_ = compact;
    update();
}

QSize SpectraNavButton::sizeHint() const
{
    const auto& g = spectra_geometry();
    if (compact_)
        return QSize(g.nav_rail_width_compact, 56);
    return QSize(g.nav_rail_width, 64);
}

void SpectraNavButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();
    auto& fm = SpectraFontManager::instance();

    bool hovered = underMouse();
    bool focused = hasFocus();

    // Active glow background
    if (active_)
    {
        QRectF glow_rect = rect().adjusted(4, 4, -4, -4);
        p.setBrush(QColor(c.cyan_accent.red(), c.cyan_accent.green(),
                          c.cyan_accent.blue(), 24));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(glow_rect, g.radius_md, g.radius_md);

        // Cyan border
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.cyan_accent, 1.5));
        p.drawRoundedRect(glow_rect, g.radius_md, g.radius_md);
    }
    else if (hovered)
    {
        QRectF hover_rect = rect().adjusted(4, 4, -4, -4);
        p.setBrush(QColor(c.elevated_surface.red(), c.elevated_surface.green(),
                          c.elevated_surface.blue(), 180));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(hover_rect, g.radius_md, g.radius_md);
    }

    // Focus ring
    if (focused && !active_)
    {
        QRectF focus_rect = rect().adjusted(3, 3, -3, -3);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.border_strong, 1));
        p.drawRoundedRect(focus_rect, g.radius_md, g.radius_md);
    }

    // Icon
    QColor icon_color = active_ ? c.cyan_accent :
                        hovered ? c.text_primary : c.text_secondary;

    const bool condensed = !compact_ && height() < 52;
    int icon_size = condensed ? 14 : 20;
    int icon_y = compact_ ? (height() - icon_size) / 2 : (condensed ? 3 : 14);

    // Draw icon using icon font
    if (fm.has_icon_font())
    {
        QFont icon_font = fm.font_icon(icon_size);
        p.setFont(icon_font);
        p.setPen(icon_color);
        QRect icon_rect(0, icon_y, width(), icon_size);
        p.drawText(icon_rect, Qt::AlignCenter, icon_codepoint_);
    }
    else
    {
        // Fallback: draw a circle
        p.setBrush(icon_color);
        p.setPen(Qt::NoPen);
        int cx = width() / 2;
        p.drawEllipse(QPointF(cx, icon_y + icon_size / 2.0), 8, 8);
    }

    // Label (hidden in compact mode)
    if (!compact_)
    {
        p.setFont(condensed ? fm.font_status() : fm.font_small());
        p.setPen(active_ ? c.text_primary :
                 hovered ? c.text_secondary : c.text_muted);
        QRect label_rect(0,
                         icon_y + icon_size + (condensed ? 1 : 4),
                         width(),
                         condensed ? 14 : 16);
        p.drawText(label_rect, Qt::AlignCenter, label_);
    }
}

}   // namespace spectra::adapters::qt
