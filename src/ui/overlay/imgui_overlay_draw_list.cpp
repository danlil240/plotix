// imgui_overlay_draw_list.cpp — ImGui ImDrawList implementation of OverlayDrawList.

#ifdef SPECTRA_USE_IMGUI

    #include "imgui_overlay_draw_list.hpp"

    #include <imgui.h>
    #include <string>

namespace spectra
{

static inline ImVec2 to_imvec2(OverlayPoint p)
{
    return ImVec2(p.x, p.y);
}

static inline ImU32 to_imu32(OverlayColor c)
{
    return static_cast<ImU32>(c);
}

static inline ImDrawFlags to_imdraw_flags(OverlayRoundCorners corners)
{
    return static_cast<ImDrawFlags>(static_cast<uint32_t>(corners));
}

void ImGuiOverlayDrawList::push_clip_rect(OverlayPoint min, OverlayPoint max)
{
    if (dl_)
        dl_->PushClipRect(to_imvec2(min), to_imvec2(max), true);
}

void ImGuiOverlayDrawList::pop_clip_rect()
{
    if (dl_)
        dl_->PopClipRect();
}

void ImGuiOverlayDrawList::add_line(OverlayPoint p0,
                                    OverlayPoint p1,
                                    OverlayColor color,
                                    float        thickness)
{
    if (dl_)
        dl_->AddLine(to_imvec2(p0), to_imvec2(p1), to_imu32(color), thickness);
}

void ImGuiOverlayDrawList::add_rect_filled(OverlayPoint        p0,
                                           OverlayPoint        p1,
                                           OverlayColor        color,
                                           float               rounding,
                                           OverlayRoundCorners corners)
{
    if (dl_)
        dl_->AddRectFilled(to_imvec2(p0),
                           to_imvec2(p1),
                           to_imu32(color),
                           rounding,
                           to_imdraw_flags(corners));
}

void ImGuiOverlayDrawList::add_rect(OverlayPoint        p0,
                                    OverlayPoint        p1,
                                    OverlayColor        color,
                                    float               rounding,
                                    OverlayRoundCorners corners,
                                    float               thickness)
{
    if (dl_)
        dl_->AddRect(to_imvec2(p0),
                     to_imvec2(p1),
                     to_imu32(color),
                     rounding,
                     to_imdraw_flags(corners),
                     thickness);
}

void ImGuiOverlayDrawList::add_circle_filled(OverlayPoint center,
                                             float        radius,
                                             OverlayColor color,
                                             int          num_segments)
{
    if (dl_)
        dl_->AddCircleFilled(to_imvec2(center), radius, to_imu32(color), num_segments);
}

void ImGuiOverlayDrawList::add_circle(OverlayPoint center,
                                      float        radius,
                                      OverlayColor color,
                                      int          num_segments,
                                      float        thickness)
{
    if (dl_)
        dl_->AddCircle(to_imvec2(center), radius, to_imu32(color), num_segments, thickness);
}

void ImGuiOverlayDrawList::add_triangle_filled(OverlayPoint p0,
                                               OverlayPoint p1,
                                               OverlayPoint p2,
                                               OverlayColor color)
{
    if (dl_)
        dl_->AddTriangleFilled(to_imvec2(p0), to_imvec2(p1), to_imvec2(p2), to_imu32(color));
}

void ImGuiOverlayDrawList::add_text(OverlayFont*     font,
                                    float            font_size,
                                    OverlayPoint     pos,
                                    OverlayColor     color,
                                    std::string_view text)
{
    if (!dl_)
        return;

    ImFont* im_font = nullptr;
    if (font)
    {
        auto* ig_font = static_cast<ImGuiOverlayFont*>(font);
        im_font       = ig_font->font;
    }

    // ImGui requires null-terminated strings; create a temporary.
    std::string tmp(text);
    dl_->AddText(im_font, font_size, to_imvec2(pos), to_imu32(color), tmp.c_str());
}

OverlayPoint ImGuiOverlayDrawList::text_size(OverlayFont*     font,
                                             float            font_size,
                                             std::string_view text) const
{
    ImFont* im_font = nullptr;
    if (font)
    {
        auto* ig_font = static_cast<ImGuiOverlayFont*>(font);
        im_font       = ig_font->font;
    }
    if (!im_font)
        im_font = ImGui::GetFont();

    std::string tmp(text);
    ImVec2      sz = im_font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, tmp.c_str());
    return {sz.x, sz.y};
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
