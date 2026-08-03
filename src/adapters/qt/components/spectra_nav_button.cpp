// spectra_nav_button.cpp — Navigation rail button implementation.

#include "spectra_nav_button.hpp"
#include "spectra_design_tokens.hpp"
#include "ui/layout/layout_manager.hpp"
#include "ui/theme/design_tokens.hpp"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QVariantAnimation>

#include <algorithm>

namespace
{
// Legacy `control_surface_color` / `control_border_color` / `control_text_color`
// from ui/theme/theme.hpp, expressed over the Qt token palette.
QColor lerp(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
}   // namespace

namespace spectra::adapters::qt
{

SpectraNavButton::SpectraNavButton(const QString& icon_codepoint,
                                   const QString& label,
                                   const QString& shortcut_hint,
                                   QWidget*       parent)
    : QPushButton(label, parent), icon_codepoint_(icon_codepoint), label_(label),
      shortcut_hint_(shortcut_hint)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setMinimumHeight(30);
    setMaximumHeight(64);
    setAttribute(Qt::WA_Hover, true);

    // Legacy smooths hover over ~dt*16 and active over ~dt*12 per frame; the
    // equivalent eased durations keep the pill from snapping.
    hover_anim_ = new QVariantAnimation(this);
    hover_anim_->setDuration(120);
    hover_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(hover_anim_,
            &QVariantAnimation::valueChanged,
            this,
            [this](const QVariant& v) { set_hover_progress(v.toReal()); });

    active_anim_ = new QVariantAnimation(this);
    active_anim_->setDuration(160);
    active_anim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(active_anim_,
            &QVariantAnimation::valueChanged,
            this,
            [this](const QVariant& v) { set_active_progress(v.toReal()); });
    setToolTip(label + (shortcut_hint.isEmpty() ? "" : " (" + shortcut_hint + ")"));
    setAccessibleName(label);
    setAccessibleDescription(shortcut_hint.isEmpty() ? QStringLiteral("Navigation control")
                                                     : QString("Shortcut %1").arg(shortcut_hint));

    setStyleSheet(QString("QPushButton {"
                          "  background: transparent;"
                          "  border: none;"
                          "  border-radius: %1px;"
                          "}")
                      .arg(spectra_geometry().radius_md));
}

void SpectraNavButton::set_active(bool active)
{
    if (active_ == active)
        return;
    active_ = active;
    active_anim_->stop();
    active_anim_->setStartValue(active_t_);
    active_anim_->setEndValue(active_ ? 1.0 : 0.0);
    active_anim_->start();
    update();
}

void SpectraNavButton::set_hover_progress(qreal t)
{
    hover_t_ = t;
    update();
}

void SpectraNavButton::set_active_progress(qreal t)
{
    active_t_ = t;
    update();
}

void SpectraNavButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    hover_anim_->stop();
    hover_anim_->setStartValue(hover_t_);
    hover_anim_->setEndValue(1.0);
    hover_anim_->start();
}

void SpectraNavButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    hover_anim_->stop();
    hover_anim_->setStartValue(hover_t_);
    hover_anim_->setEndValue(0.0);
    hover_anim_->start();
}

float SpectraNavButton::cell_scale() const
{
    if (compact_)
        return 1.0f;
    const float floor_scale =
        LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT;
    return std::max(static_cast<float>(height()) / LayoutManager::NAV_RAIL_CELL_HEIGHT,
                    floor_scale);
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
    return QSize(g.nav_rail_width, 56);
}

void SpectraNavButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c  = spectra_colors();
    const auto& g  = spectra_geometry();
    auto&       fm = SpectraFontManager::instance();

    const bool  hovered = underMouse();
    const bool  focused = hasFocus();
    const float scale   = cell_scale();

    // ── Pill geometry (legacy `icon_label_button`) ────────────────────────────
    const qreal  pill_pad = ui::tokens::SPACE_2 * scale;
    const qreal  inset_v  = 3.0 * scale;
    const qreal  lift     = active_t_ * 1.5 + hover_t_ * 0.75;
    const QRectF pill(pill_pad,
                      inset_v - lift,
                      std::max(1.0, width() - pill_pad * 2.0),
                      std::max(1.0, height() - inset_v * 2.0));

    const qreal motion_t = std::max(hover_t_, active_t_);
    if (motion_t > 0.01)
    {
        // Layered outer glow — two rings, stronger on the active tool.
        const qreal glow_a = ui::tokens::NAV_RAIL_GLOW_ALPHA_HOVER * hover_t_
                             + ui::tokens::NAV_RAIL_GLOW_ALPHA_ACTIVE * active_t_;
        p.setBrush(Qt::NoBrush);
        for (int gi = 2; gi >= 1; --gi)
        {
            const qreal e = static_cast<qreal>(gi);
            QColor      glow(c.cyan_glow);
            glow.setAlphaF(std::clamp(glow_a / e, 0.0, 1.0));
            p.setPen(QPen(glow, 1.5));
            p.drawRoundedRect(pill.adjusted(-e, -e, e, e), g.radius_md + e, g.radius_md + e);
        }

        // Pill surface: legacy `control_surface_color` blended toward the accent.
        QColor      fill   = active_ ? lerp(c.bg_tertiary, c.cyan_accent, 0.12)
                                     : (hovered ? lerp(c.bg_tertiary, c.cyan_accent, 0.08)
                                                : lerp(c.panel_surface, c.bg_tertiary, 0.55));
        const qreal fill_a = ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE
                             + active_t_
                                   * (ui::tokens::NAV_RAIL_SURFACE_ALPHA_ACTIVE
                                      - ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE)
                             + hover_t_
                                   * (ui::tokens::NAV_RAIL_SURFACE_ALPHA_HOVER
                                      - ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE);
        fill.setAlphaF(std::clamp(fill_a, 0.0, 1.0));
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(pill, g.radius_md, g.radius_md);

        // Top inner highlight on the pill.
        QColor highlight(c.text_primary);
        highlight.setAlpha(static_cast<int>(24.0 + 18.0 * active_t_));
        p.setPen(QPen(highlight, 1));
        p.drawLine(QPointF(pill.left() + 4.0 * scale, pill.top() + 1.0),
                   QPointF(pill.right() - 4.0 * scale, pill.top() + 1.0));

        // Border: legacy `control_border_color`.
        QColor border =
            active_ ? c.cyan_accent
                    : (hovered ? lerp(c.border_default, c.cyan_accent, 0.35) : c.border_subtle);
        border.setAlphaF(std::clamp(ui::tokens::NAV_RAIL_BORDER_ALPHA_HOVER * hover_t_
                                        + ui::tokens::NAV_RAIL_BORDER_ALPHA_ACTIVE * active_t_,
                                    0.0,
                                    1.0));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(pill, g.radius_md, g.radius_md);
    }

    // Focus ring — Qt-only affordance; legacy relies on ImGui's nav highlight.
    if (focused && !active_)
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.border_strong, 1));
        p.drawRoundedRect(pill.adjusted(-1, -1, 1, 1), g.radius_md + 1, g.radius_md + 1);
    }

    // ── Icon above label, both centered ──────────────────────────────────────
    const qreal icon_size = compact_
                                ? 16.0
                                : std::max(ui::tokens::NAV_RAIL_ICON_SIZE_BASE * scale,
                                           static_cast<float>(ui::tokens::NAV_RAIL_ICON_SIZE_MIN));
    // Legacy grows the glyph 3% when the tool becomes active.
    const int   icon_draw_size = qRound(icon_size * (1.0 + active_t_ * 0.03));
    const qreal label_size =
        compact_ ? 0.0
                 : std::max(ui::tokens::NAV_RAIL_LABEL_SIZE_BASE * scale,
                            static_cast<float>(ui::tokens::NAV_RAIL_LABEL_SIZE_MIN));
    const int icon_gap = compact_ ? 0 : qRound(std::max(ui::tokens::SPACE_1 * 0.75f * scale, 1.0f));

    QFont label_font = fm.font_small();
    if (!compact_)
        label_font.setPixelSize(qRound(label_size));
    const int label_height   = compact_ ? 0 : QFontMetrics(label_font).height();
    const int content_height = icon_draw_size + icon_gap + label_height;
    const int icon_y         = qRound((height() - content_height) / 2.0 - lift * 0.35);

    // Legacy `control_text_color`: accent when active, primary on hover.
    QColor icon_color =
        active_ ? c.cyan_accent.lighter(110) : (hovered ? c.text_primary : c.text_secondary);
    icon_color.setAlphaF(active_ ? ui::tokens::NAV_RAIL_ICON_ALPHA_ACTIVE
                                 : (hovered ? ui::tokens::NAV_RAIL_ICON_ALPHA_HOVER
                                            : ui::tokens::NAV_RAIL_ICON_ALPHA_INACTIVE));

    if (fm.has_icon_font())
    {
        p.setFont(fm.font_icon(icon_draw_size));
        p.setPen(icon_color);
        p.drawText(QRect(0, icon_y, width(), icon_draw_size), Qt::AlignCenter, icon_codepoint_);
    }
    else
    {
        // Fallback: draw a circle
        p.setBrush(icon_color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(width() / 2.0, icon_y + icon_draw_size / 2.0), 8, 8);
    }

    // Label (hidden in compact mode)
    if (!compact_)
    {
        QColor label_color = active_ ? c.cyan_accent.lighter(110) : c.text_secondary;
        label_color.setAlphaF(active_ ? ui::tokens::NAV_RAIL_LABEL_ALPHA_ACTIVE
                                      : (hovered ? ui::tokens::NAV_RAIL_LABEL_ALPHA_HOVER
                                                 : ui::tokens::NAV_RAIL_LABEL_ALPHA_INACTIVE));
        p.setFont(label_font);
        p.setPen(label_color);
        p.drawText(QRect(0, icon_y + icon_draw_size + icon_gap, width(), label_height),
                   Qt::AlignCenter,
                   label_);
    }
}

}   // namespace spectra::adapters::qt
