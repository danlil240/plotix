// qt_overlay_draw_list.cpp — QPainter implementation of OverlayDrawList.

#include "qt_overlay_draw_list.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QString>

namespace spectra::adapters::qt
{

QColor QtOverlayDrawList::to_qcolor(OverlayColor c)
{
    // OverlayColor is 0xAABBGGRR (matching ImU32)
    uint8_t r = static_cast<uint8_t>(c & 0xFF);
    uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>((c >> 16) & 0xFF);
    uint8_t a = static_cast<uint8_t>((c >> 24) & 0xFF);
    return QColor(r, g, b, a);
}

QRectF QtOverlayDrawList::to_qrectf(OverlayPoint p0, OverlayPoint p1)
{
    return QRectF(QPointF(p0.x, p0.y), QPointF(p1.x, p1.y));
}

void QtOverlayDrawList::push_clip_rect(OverlayPoint min, OverlayPoint max)
{
    if (painter_)
    {
        painter_->save();
        painter_->setClipRect(QRectF(QPointF(min.x, min.y), QPointF(max.x, max.y)));
    }
}

void QtOverlayDrawList::pop_clip_rect()
{
    if (painter_)
        painter_->restore();
}

void QtOverlayDrawList::add_line(OverlayPoint p0, OverlayPoint p1,
                                 OverlayColor color, float thickness)
{
    if (!painter_)
        return;
    QPen pen(to_qcolor(color));
    pen.setWidthF(thickness);
    painter_->setPen(pen);
    painter_->setBrush(Qt::NoBrush);
    painter_->drawLine(QPointF(p0.x, p0.y), QPointF(p1.x, p1.y));
}

void QtOverlayDrawList::add_rect_filled(OverlayPoint p0, OverlayPoint p1,
                                        OverlayColor color, float rounding,
                                        OverlayRoundCorners /*corners*/)
{
    if (!painter_)
        return;
    painter_->setPen(Qt::NoPen);
    painter_->setBrush(to_qcolor(color));
    if (rounding > 0.0f)
    {
        QRectF rect = to_qrectf(p0, p1);
        // Qt doesn't support per-corner rounding easily; use uniform for now
        painter_->drawRoundedRect(rect, rounding, rounding);
    }
    else
    {
        painter_->drawRect(to_qrectf(p0, p1));
    }
}

void QtOverlayDrawList::add_rect(OverlayPoint p0, OverlayPoint p1,
                                 OverlayColor color, float rounding,
                                 OverlayRoundCorners /*corners*/, float thickness)
{
    if (!painter_)
        return;
    QPen pen(to_qcolor(color));
    pen.setWidthF(thickness);
    painter_->setPen(pen);
    painter_->setBrush(Qt::NoBrush);
    if (rounding > 0.0f)
    {
        QRectF rect = to_qrectf(p0, p1);
        painter_->drawRoundedRect(rect, rounding, rounding);
    }
    else
    {
        painter_->drawRect(to_qrectf(p0, p1));
    }
}

void QtOverlayDrawList::add_circle_filled(OverlayPoint center, float radius,
                                          OverlayColor color, int)
{
    if (!painter_)
        return;
    painter_->setPen(Qt::NoPen);
    painter_->setBrush(to_qcolor(color));
    painter_->drawEllipse(QPointF(center.x, center.y), radius, radius);
}

void QtOverlayDrawList::add_circle(OverlayPoint center, float radius,
                                   OverlayColor color, int, float thickness)
{
    if (!painter_)
        return;
    QPen pen(to_qcolor(color));
    pen.setWidthF(thickness);
    painter_->setPen(pen);
    painter_->setBrush(Qt::NoBrush);
    painter_->drawEllipse(QPointF(center.x, center.y), radius, radius);
}

void QtOverlayDrawList::add_triangle_filled(OverlayPoint p0, OverlayPoint p1,
                                            OverlayPoint p2, OverlayColor color)
{
    if (!painter_)
        return;
    QPolygonF triangle;
    triangle << QPointF(p0.x, p0.y) << QPointF(p1.x, p1.y) << QPointF(p2.x, p2.y);
    painter_->setPen(Qt::NoPen);
    painter_->setBrush(to_qcolor(color));
    painter_->drawPolygon(triangle);
}

void QtOverlayDrawList::add_text(OverlayFont* font, float font_size,
                                 OverlayPoint pos, OverlayColor color,
                                 std::string_view text)
{
    if (!painter_)
        return;

    QFont qfont;
    if (font)
    {
        auto* qt_font = static_cast<QtOverlayFont*>(font);
        qfont = qt_font->font;
    }
    qfont.setPointSizeF(font_size * 0.75f);   // approximate px→pt
    painter_->setFont(qfont);
    painter_->setPen(to_qcolor(color));

    QString str = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    painter_->drawText(QPointF(pos.x, pos.y + font_size), str);
}

OverlayPoint QtOverlayDrawList::text_size(OverlayFont* font, float font_size,
                                          std::string_view text) const
{
    QFont qfont;
    if (font)
    {
        auto* qt_font = static_cast<QtOverlayFont*>(font);
        qfont = qt_font->font;
    }
    qfont.setPointSizeF(font_size * 0.75f);
    QFontMetrics fm(qfont);

    QString str = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    QRectF br = fm.boundingRect(str);
    return {static_cast<float>(br.width()), static_cast<float>(br.height())};
}

}   // namespace spectra::adapters::qt
