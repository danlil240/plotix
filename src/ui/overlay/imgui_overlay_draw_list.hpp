#pragma once

// ImGuiOverlayDrawList — ImGui ImDrawList implementation of OverlayDrawList.
//
// Maps the framework-neutral OverlayDrawList interface to ImGui's ImDrawList
// so existing overlay code can be gradually refactored to use the abstract
// interface while still rendering through ImGui in the legacy frontend.

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/overlay_draw_list.hpp>

struct ImDrawList;
struct ImFont;

namespace spectra
{

// ImGui font wrapper implementing OverlayFont.
struct ImGuiOverlayFont : OverlayFont
{
    ImFont* font = nullptr;
    explicit ImGuiOverlayFont(ImFont* f = nullptr) : font(f) {}
};

// Maps OverlayDrawList calls to an ImDrawList.
class ImGuiOverlayDrawList final : public OverlayDrawList
{
   public:
    explicit ImGuiOverlayDrawList(ImDrawList* dl) : dl_(dl) {}

    void set_draw_list(ImDrawList* dl) { dl_ = dl; }

    void push_clip_rect(OverlayPoint min, OverlayPoint max) override;
    void pop_clip_rect() override;

    void add_line(OverlayPoint p0, OverlayPoint p1, OverlayColor color, float thickness) override;

    void add_rect_filled(OverlayPoint        p0,
                         OverlayPoint        p1,
                         OverlayColor        color,
                         float               rounding,
                         OverlayRoundCorners corners) override;

    void add_rect(OverlayPoint        p0,
                  OverlayPoint        p1,
                  OverlayColor        color,
                  float               rounding,
                  OverlayRoundCorners corners,
                  float               thickness) override;

    void add_circle_filled(OverlayPoint center,
                           float        radius,
                           OverlayColor color,
                           int          num_segments) override;

    void add_circle(OverlayPoint center,
                    float        radius,
                    OverlayColor color,
                    int          num_segments,
                    float        thickness) override;

    void add_triangle_filled(OverlayPoint p0,
                             OverlayPoint p1,
                             OverlayPoint p2,
                             OverlayColor color) override;

    void add_text(OverlayFont*     font,
                  float            font_size,
                  OverlayPoint     pos,
                  OverlayColor     color,
                  std::string_view text) override;

    OverlayPoint text_size(OverlayFont*     font,
                           float            font_size,
                           std::string_view text) const override;

   private:
    ImDrawList* dl_ = nullptr;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
