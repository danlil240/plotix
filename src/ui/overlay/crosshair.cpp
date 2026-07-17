#ifdef SPECTRA_USE_IMGUI

    #include "crosshair.hpp"

    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>

    #include "ui/input/input.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

// Helper: draw a dashed line on an ImDrawList
static void draw_dashed_line(ImDrawList* dl,
                             ImVec2      p0,
                             ImVec2      p1,
                             ImU32       color,
                             float       dash,
                             float       gap,
                             float       thickness)
{
    float dx     = p1.x - p0.x;
    float dy     = p1.y - p0.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    float nx      = dx / length;
    float ny      = dy / length;
    float drawn   = 0.0f;
    bool  drawing = true;

    while (drawn < length)
    {
        float seg = drawing ? dash : gap;
        float end = std::min(drawn + seg, length);
        if (drawing)
        {
            dl->AddLine(ImVec2(p0.x + nx * drawn, p0.y + ny * drawn),
                        ImVec2(p0.x + nx * end, p0.y + ny * end),
                        color,
                        thickness);
        }
        drawn   = end;
        drawing = !drawing;
    }
}

void Crosshair::draw(const CursorReadout& cursor,
                     const Rect&          viewport,
                     float /*xlim_min*/,
                     float /*xlim_max*/,
                     float /*ylim_min*/,
                     float /*ylim_max*/,
                     ImDrawList* dl)
{
    // Animate opacity
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt     = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors     = theme_mgr_->colors();
    ImU32       line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                             colors.crosshair.g,
                                                             colors.crosshair.b,
                                                             colors.crosshair.a * opacity_));

    auto sx = static_cast<float>(cursor.screen_x);
    auto sy = static_cast<float>(cursor.screen_y);

    // Clamp to viewport
    float vx0 = viewport.x;
    float vy0 = viewport.y;
    float vx1 = viewport.x + viewport.w;
    float vy1 = viewport.y + viewport.h;

    if (sx < vx0 || sx > vx1 || sy < vy0 || sy > vy1)
        return;

    ImDrawList* fg = dl ? dl : ImGui::GetForegroundDrawList();

    if (colors.glow_intensity > 0.01f)
    {
        ImU32 glow_color =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_glow.r,
                                                  colors.accent_glow.g,
                                                  colors.accent_glow.b,
                                                  colors.accent_glow.a * opacity_ * 0.26f));
        fg->AddLine(ImVec2(sx, vy0), ImVec2(sx, vy1), glow_color, 4.0f);
        fg->AddLine(ImVec2(vx0, sy), ImVec2(vx1, sy), glow_color, 4.0f);
    }

    // Vertical line (full height of viewport)
    draw_dashed_line(fg,
                     ImVec2(sx, vy0),
                     ImVec2(sx, vy1),
                     line_color,
                     dash_length_,
                     gap_length_,
                     1.0f);

    // Horizontal line (full width of viewport)
    draw_dashed_line(fg,
                     ImVec2(vx0, sy),
                     ImVec2(vx1, sy),
                     line_color,
                     dash_length_,
                     gap_length_,
                     1.0f);

    {
        constexpr float DOT_RADIUS = 4.0f;
        ImU32           dot_fill   = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, opacity_));
        fg->AddCircleFilled(ImVec2(sx, sy), DOT_RADIUS, dot_fill, 12);
        ImU32 dot_stroke = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_canvas.r, colors.bg_canvas.g, colors.bg_canvas.b, opacity_));
        fg->AddCircle(ImVec2(sx, sy), DOT_RADIUS, dot_stroke, 12, 1.0f);
        fg->AddCircle(
            ImVec2(sx, sy),
            DOT_RADIUS + 3.0f,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, opacity_ * 0.28f)),
            18,
            1.0f);

        if (colors.glow_intensity > 0.01f)
        {
            constexpr float GLOW_RADIUS = 10.0f;
            ImU32           glow_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_glow.r,
                                                      colors.accent_glow.g,
                                                      colors.accent_glow.b,
                                                      colors.accent_glow.a * opacity_ * 0.56f));
            fg->AddCircleFilled(ImVec2(sx, sy), GLOW_RADIUS, glow_col, 16);
        }
    }

    // Axis-intersection labels
    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.84f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    // Large x values (epoch timestamps) use fixed notation to stay precise.
    const std::string x_label = std::abs(cursor.data_x) >= 1e6
                                    ? std::format("{:.3f}", cursor.data_x)
                                    : std::format("{:.4g}", cursor.data_x);
    const std::string y_label = std::format("{:.4g}", cursor.data_y);

    ImFont*         font      = ImGui::GetFont();
    constexpr float label_pad = 3.0f;

    // X label at bottom of viewport
    {
        ImVec2 sz = font->CalcTextSizeA(font->LegacySize * 0.85f, 200.0f, 0.0f, x_label.c_str());
        float  lx = sx - sz.x * 0.5f;
        float  ly = vy1 + 2.0f;
        // Clamp horizontally
        if (lx < vx0)
            lx = vx0;
        if (lx + sz.x + label_pad * 2.0f > vx1)
            lx = vx1 - sz.x - label_pad * 2.0f;

        fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                          ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                          label_bg,
                          3.0f);
        fg->AddText(font,
                    font->LegacySize * 0.85f,
                    ImVec2(lx, ly + label_pad),
                    label_text,
                    x_label.c_str());
    }

    // Y label inside left edge of viewport
    {
        ImVec2 sz = font->CalcTextSizeA(font->LegacySize * 0.85f, 200.0f, 0.0f, y_label.c_str());
        float  lx = vx0 + 4.0f;
        float  ly = sy - sz.y * 0.5f;
        // Clamp vertically
        if (ly < vy0)
            ly = vy0;
        if (ly + sz.y + label_pad * 2.0f > vy1)
            ly = vy1 - sz.y - label_pad * 2.0f;

        fg->AddRectFilled(ImVec2(lx - label_pad, ly - label_pad),
                          ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad),
                          label_bg,
                          3.0f);
        fg->AddText(font, font->LegacySize * 0.85f, ImVec2(lx, ly), label_text, y_label.c_str());
    }
}

void Crosshair::draw_all_axes(const CursorReadout& cursor,
                              Figure&              figure,
                              AxisLinkManager* /*link_mgr*/,
                              ImDrawList* dl)
{
    // Animate opacity (shared across all axes)
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt     = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors     = theme_mgr_->colors();
    ImU32       line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                             colors.crosshair.g,
                                                             colors.crosshair.b,
                                                             colors.crosshair.a * opacity_));

    auto cx = static_cast<float>(cursor.screen_x);
    auto cy = static_cast<float>(cursor.screen_y);

    ImDrawList* fg = dl ? dl : ImGui::GetForegroundDrawList();

    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.9f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    ImFont*         font      = ImGui::GetFont();
    constexpr float label_pad = 3.0f;

    // Find which axes the cursor is over
    Axes* hovered_axes = nullptr;
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (cx >= vp.x && cx <= vp.x + vp.w && cy >= vp.y && cy <= vp.y + vp.h)
        {
            hovered_axes = axes_ptr.get();
            break;
        }
    }

    if (!hovered_axes)
        return;

    // Get the data-X coordinate from the hovered axes
    auto        xlim_h   = hovered_axes->x_limits();
    auto        ylim_h   = hovered_axes->y_limits();
    const auto& vp_h     = hovered_axes->viewport();
    float       norm_x_h = (cx - vp_h.x) / vp_h.w;
    float       data_x   = xlim_h.min + norm_x_h * (xlim_h.max - xlim_h.min);

    // Get the data-Y coordinate from the hovered axes
    double y_range_h = ylim_h.max - ylim_h.min;
    if (y_range_h == 0.0)
        y_range_h = 1.0;
    float norm_y_h = 1.0f - (cy - vp_h.y) / vp_h.h;
    float data_y   = ylim_h.min + norm_y_h * y_range_h;

    // Draw on ALL axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp      = axes_ptr->viewport();
        auto        xlim    = axes_ptr->x_limits();
        auto        ylim    = axes_ptr->y_limits();
        double      x_range = xlim.max - xlim.min;
        double      y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        float vx0 = vp.x;
        float vy0 = vp.y;
        float vx1 = vp.x + vp.w;
        float vy1 = vp.y + vp.h;

        // Vertical line at the same data-X on every axes
        auto  norm_x = static_cast<float>((data_x - xlim.min) / x_range);
        float sx     = vp.x + norm_x * vp.w;

        if (sx >= vx0 && sx <= vx1)
        {
            draw_dashed_line(fg,
                             ImVec2(sx, vy0),
                             ImVec2(sx, vy1),
                             line_color,
                             dash_length_,
                             gap_length_,
                             1.0f);

            // X label at bottom
            const std::string x_label = std::format("{:.4g}", data_x);
            ImVec2            sz =
                font->CalcTextSizeA(font->LegacySize * 0.85f, 200.0f, 0.0f, x_label.c_str());
            float lx = sx - sz.x * 0.5f;
            float ly = vy1 + 2.0f;
            if (lx < vx0)
                lx = vx0;
            if (lx + sz.x + label_pad * 2.0f > vx1)
                lx = vx1 - sz.x - label_pad * 2.0f;

            fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                              ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                              label_bg,
                              3.0f);
            fg->AddText(font,
                        font->LegacySize * 0.85f,
                        ImVec2(lx, ly + label_pad),
                        label_text,
                        x_label.c_str());
        }

        // Horizontal line on the hovered axes (at cursor Y)
        if (axes_ptr.get() == hovered_axes)
        {
            if (cy >= vy0 && cy <= vy1)
            {
                draw_dashed_line(fg,
                                 ImVec2(vx0, cy),
                                 ImVec2(vx1, cy),
                                 line_color,
                                 dash_length_,
                                 gap_length_,
                                 1.0f);

                // Y label inside left edge of viewport
                const std::string y_label = std::format("{:.4g}", data_y);
                ImVec2            sz =
                    font->CalcTextSizeA(font->LegacySize * 0.85f, 200.0f, 0.0f, y_label.c_str());
                float lx2 = vx0 + 4.0f;
                float ly2 = cy - sz.y * 0.5f;
                if (ly2 < vy0)
                    ly2 = vy0;
                if (ly2 + sz.y + label_pad * 2.0f > vy1)
                    ly2 = vy1 - sz.y - label_pad * 2.0f;

                fg->AddRectFilled(ImVec2(lx2 - label_pad, ly2 - label_pad),
                                  ImVec2(lx2 + sz.x + label_pad, ly2 + sz.y + label_pad),
                                  label_bg,
                                  3.0f);
                fg->AddText(font,
                            font->LegacySize * 0.85f,
                            ImVec2(lx2, ly2),
                            label_text,
                            y_label.c_str());
            }
        }
        // Horizontal line on non-hovered axes at the same data-Y
        else
        {
            // Map the hovered axes' data-Y into this subplot's screen coordinates
            auto  norm_iy = static_cast<float>((data_y - ylim.min) / y_range);
            float sy      = vy0 + (1.0f - norm_iy) * vp.h;

            if (sy >= vy0 && sy <= vy1)
            {
                // Dimmer line for non-hovered axes
                ImU32 dim_color =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                          colors.crosshair.g,
                                                          colors.crosshair.b,
                                                          colors.crosshair.a * opacity_ * 0.6f));

                draw_dashed_line(fg,
                                 ImVec2(vx0, sy),
                                 ImVec2(vx1, sy),
                                 dim_color,
                                 dash_length_,
                                 gap_length_,
                                 1.0f);

                // Y label showing the same data-Y value
                const std::string y_label = std::format("{:.4g}", data_y);
                ImVec2            sz =
                    font->CalcTextSizeA(font->LegacySize * 0.85f, 200.0f, 0.0f, y_label.c_str());
                float lx2 = vx0 + 4.0f;
                float ly2 = sy - sz.y * 0.5f;
                if (ly2 < vy0)
                    ly2 = vy0;
                if (ly2 + sz.y + label_pad * 2.0f > vy1)
                    ly2 = vy1 - sz.y - label_pad * 2.0f;

                ImU32 dim_label_text = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                                             colors.text_primary.g,
                                                                             colors.text_primary.b,
                                                                             opacity_ * 0.6f));

                fg->AddRectFilled(ImVec2(lx2 - label_pad, ly2 - label_pad),
                                  ImVec2(lx2 + sz.x + label_pad, ly2 + sz.y + label_pad),
                                  label_bg,
                                  3.0f);
                fg->AddText(font,
                            font->LegacySize * 0.85f,
                            ImVec2(lx2, ly2),
                            dim_label_text,
                            y_label.c_str());
            }
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
