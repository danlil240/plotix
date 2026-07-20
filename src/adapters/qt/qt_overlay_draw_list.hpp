#pragma once

// QtOverlayDrawList — QPainter implementation of OverlayDrawList.
//
// Enables overlay rendering (tooltip, crosshair, annotations, markers, etc.)
// through QPainter on a QWidget or QImage, used by the Qt frontend for
// canvas overlays that are not yet ported to renderer-level Vulkan overlays.
//
// The QPainter must be set before each frame of overlay drawing via set_painter().

#include <spectra/overlay_draw_list.hpp>

#include <QPainter>
#include <QFont>
#include <QString>

namespace spectra::adapters::qt
{

// Qt font wrapper implementing OverlayFont.
struct QtOverlayFont : OverlayFont
{
    QFont font;
    explicit QtOverlayFont(const QFont& f = QFont()) : font(f) {}
};

class QtOverlayDrawList final : public OverlayDrawList
{
public:
    QtOverlayDrawList() = default;

    void set_painter(QPainter* p) { painter_ = p; }

    void push_clip_rect(OverlayPoint min, OverlayPoint max) override;
    void pop_clip_rect() override;

    void add_line(OverlayPoint p0, OverlayPoint p1, OverlayColor color, float thickness) override;

    void add_rect_filled(OverlayPoint p0, OverlayPoint p1, OverlayColor color,
                         float rounding, OverlayRoundCorners corners) override;

    void add_rect(OverlayPoint p0, OverlayPoint p1, OverlayColor color,
                  float rounding, OverlayRoundCorners corners, float thickness) override;

    void add_circle_filled(OverlayPoint center, float radius, OverlayColor color,
                           int num_segments) override;

    void add_circle(OverlayPoint center, float radius, OverlayColor color,
                    int num_segments, float thickness) override;

    void add_triangle_filled(OverlayPoint p0, OverlayPoint p1, OverlayPoint p2,
                             OverlayColor color) override;

    void add_text(OverlayFont* font, float font_size, OverlayPoint pos,
                  OverlayColor color, std::string_view text) override;

    OverlayPoint text_size(OverlayFont* font, float font_size,
                           std::string_view text) const override;

private:
    QPainter* painter_ = nullptr;

    static QColor to_qcolor(OverlayColor c);
    static QRectF to_qrectf(OverlayPoint p0, OverlayPoint p1);
};

}   // namespace spectra::adapters::qt
