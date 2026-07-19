#include "box_zoom_overlay.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

// ─── Update ─────────────────────────────────────────────────────────────────

void BoxZoomOverlay::update(float dt)
{
    if (!input_handler_)
        return;

    const auto& bz = input_handler_->box_zoom_rect();
    // bool was_active = active_;  // Currently unused
    active_ = bz.active;

    if (active_)
    {
        // Cache screen-space rect
        rect_x0_ = static_cast<float>(bz.x0);
        rect_y0_ = static_cast<float>(bz.y0);
        rect_x1_ = static_cast<float>(bz.x1);
        rect_y1_ = static_cast<float>(bz.y1);

        // Fade in
        float target = 1.0f;
        opacity_ += (target - opacity_) * std::min(1.0f, FADE_IN_SPEED * dt);
        if (opacity_ > 0.99f)
            opacity_ = 1.0f;
    }
    else
    {
        // Fade out
        opacity_ += (0.0f - opacity_) * std::min(1.0f, FADE_OUT_SPEED * dt);
        if (opacity_ < 0.01f)
            opacity_ = 0.0f;
    }
}

// ─── Draw ───────────────────────────────────────────────────────────────────

void BoxZoomOverlay::draw(float /*window_width*/, float /*window_height*/)
{
    if (opacity_ < 0.01f)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl)
        return;

    const auto& colors = ui::theme();
    float       alpha  = opacity_;

    // Normalize rect corners
    float x0 = std::min(rect_x0_, rect_x1_);
    float y0 = std::min(rect_y0_, rect_y1_);
    float x1 = std::max(rect_x0_, rect_x1_);
    float y1 = std::max(rect_y0_, rect_y1_);

    {
        const auto& fill     = colors.selection_fill;
        ImU32       fill_col = IM_COL32(static_cast<uint8_t>(fill.r * 255),
                                  static_cast<uint8_t>(fill.g * 255),
                                  static_cast<uint8_t>(fill.b * 255),
                                  static_cast<uint8_t>(fill_opacity_ * alpha * 255));
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill_col, ui::tokens::RADIUS_SM);
        dl->AddRectFilledMultiColor(ImVec2(x0, y0),
                                    ImVec2(x1, y0 + (y1 - y0) * 0.45f),
                                    IM_COL32(255, 255, 255, static_cast<int>(alpha * 12.0f)),
                                    IM_COL32(255, 255, 255, static_cast<int>(alpha * 12.0f)),
                                    IM_COL32(255, 255, 255, 0),
                                    IM_COL32(255, 255, 255, 0));
    }

    const auto& border     = colors.selection_border;
    ImU32       border_col = IM_COL32(static_cast<uint8_t>(border.r * 255),
                                static_cast<uint8_t>(border.g * 255),
                                static_cast<uint8_t>(border.b * 255),
                                static_cast<uint8_t>(alpha * 255));
    ImU32       glow_col   = IM_COL32(static_cast<uint8_t>(colors.accent_glow.r * 255),
                              static_cast<uint8_t>(colors.accent_glow.g * 255),
                              static_cast<uint8_t>(colors.accent_glow.b * 255),
                              static_cast<uint8_t>(alpha * 46));

    dl->AddRect(ImVec2(x0 - 1.0f, y0 - 1.0f),
                ImVec2(x1 + 1.0f, y1 + 1.0f),
                glow_col,
                ui::tokens::RADIUS_SM,
                0,
                3.0f);

    draw_dashed_line_impl(x0, y0, x1, y0, border_col, border_width_);   // top
    draw_dashed_line_impl(x1, y0, x1, y1, border_col, border_width_);   // right
    draw_dashed_line_impl(x1, y1, x0, y1, border_col, border_width_);   // bottom
    draw_dashed_line_impl(x0, y1, x0, y0, border_col, border_width_);   // left

    draw_corner_handles_impl(x0, y0, x1, y1, border_col);

    if (show_crosshair_ && input_handler_ && input_handler_->active_axes())
    {
        const auto& vp        = input_handler_->active_axes()->viewport();
        ImU32       cross_col = IM_COL32(static_cast<uint8_t>(border.r * 255),
                                   static_cast<uint8_t>(border.g * 255),
                                   static_cast<uint8_t>(border.b * 255),
                                   static_cast<uint8_t>(alpha * 0.24f * 255));
        draw_zoom_crosshair_impl(x0, y0, x1, y1, vp.x, vp.y, vp.w, vp.h, cross_col);
    }

    if (show_dimensions_)
    {
        draw_dimension_label_impl(x0, y0, x1, y1, border_col);
    }
}

// ─── Dashed line ────────────────────────────────────────────────────────────

void BoxZoomOverlay::draw_dashed_line_impl(float        x0,
                                           float        y0,
                                           float        x1,
                                           float        y1,
                                           unsigned int col,
                                           float        thickness) const
{
    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    float       dx  = x1 - x0;
    float       dy  = y1 - y0;
    float       len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return;

    float nx      = dx / len;
    float ny      = dy / len;
    float segment = dash_length_ + dash_gap_;
    float pos     = 0.0f;

    while (pos < len)
    {
        float dash_end = std::min(pos + dash_length_, len);
        dl->AddLine(ImVec2(x0 + nx * pos, y0 + ny * pos),
                    ImVec2(x0 + nx * dash_end, y0 + ny * dash_end),
                    col,
                    thickness);
        pos += segment;
    }
}

// ─── Corner handles ─────────────────────────────────────────────────────────

void BoxZoomOverlay::draw_corner_handles_impl(float        x0,
                                              float        y0,
                                              float        x1,
                                              float        y1,
                                              unsigned int col) const
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float       s  = CORNER_HANDLE_SIZE;
    // Four corners: filled squares
    dl->AddRectFilled(ImVec2(x0 - s, y0 - s), ImVec2(x0 + s, y0 + s), col);
    dl->AddRectFilled(ImVec2(x1 - s, y0 - s), ImVec2(x1 + s, y0 + s), col);
    dl->AddRectFilled(ImVec2(x0 - s, y1 - s), ImVec2(x0 + s, y1 + s), col);
    dl->AddRectFilled(ImVec2(x1 - s, y1 - s), ImVec2(x1 + s, y1 + s), col);
}

// ─── Dimension label ────────────────────────────────────────────────────────

void BoxZoomOverlay::draw_dimension_label_impl(float        x0,
                                               float        y0,
                                               float        x1,
                                               float        y1,
                                               unsigned int col) const
{
    ImDrawList* dl   = ImGui::GetForegroundDrawList();
    float       w_px = std::abs(x1 - x0);
    float       h_px = std::abs(y1 - y0);

    // Only show label if selection is large enough
    if (w_px < 30.0f || h_px < 20.0f)
        return;

    // If we have an input handler with active axes, show data-space dimensions
    std::string buf;
    if (input_handler_ && input_handler_->active_axes())
    {
        // Convert corners to data space for the label
        double d_x0 = NAN;
        double d_y0 = NAN;
        double d_x1 = NAN;
        double d_y1 = NAN;
        // Use the public screen_to_data (requires const_cast since it reads active_axes_)
        auto* ih = const_cast<InputHandler*>(input_handler_);
        ih->screen_to_data(x0, y0, d_x0, d_y0);
        ih->screen_to_data(x1, y1, d_x1, d_y1);
        double dw = std::abs(d_x1 - d_x0);
        double dh = std::abs(d_y1 - d_y0);
        buf       = std::format("{:.3g} \xc3\x97 {:.3g}", dw, dh);
    }
    else
    {
        buf = std::format("{:.0f} \xc3\x97 {:.0f} px", w_px, h_px);
    }

    // Position label below the bottom edge, centered
    ImVec2 text_size = ImGui::CalcTextSize(buf.c_str());
    float  label_x   = (x0 + x1) * 0.5f - text_size.x * 0.5f;
    float  label_y   = std::max(y0, y1) + 6.0f;

    float       pad_x  = 6.0f;
    float       pad_y  = 2.0f;
    const auto& colors = ui::theme();
    ImU32       bg_col = IM_COL32(static_cast<uint8_t>(colors.bg_elevated.r * 255),
                            static_cast<uint8_t>(colors.bg_elevated.g * 255),
                            static_cast<uint8_t>(colors.bg_elevated.b * 255),
                            static_cast<uint8_t>(0.88f * 255));
    dl->AddRectFilled(ImVec2(label_x - pad_x, label_y - pad_y),
                      ImVec2(label_x + text_size.x + pad_x, label_y + text_size.y + pad_y),
                      bg_col,
                      ui::tokens::RADIUS_SM);
    dl->AddRect(ImVec2(label_x - pad_x, label_y - pad_y),
                ImVec2(label_x + text_size.x + pad_x, label_y + text_size.y + pad_y),
                IM_COL32(static_cast<uint8_t>(colors.border_default.r * 255),
                         static_cast<uint8_t>(colors.border_default.g * 255),
                         static_cast<uint8_t>(colors.border_default.b * 255),
                         96),
                ui::tokens::RADIUS_SM);

    dl->AddText(ImVec2(label_x, label_y), col, buf.c_str());
}

// ─── Zoom crosshair ─────────────────────────────────────────────────────────

void BoxZoomOverlay::draw_zoom_crosshair_impl(float        x0,
                                              float        y0,
                                              float        x1,
                                              float        y1,
                                              float        vp_x,
                                              float        vp_y,
                                              float        vp_w,
                                              float        vp_h,
                                              unsigned int col) const
{
    ImDrawList* dl        = ImGui::GetForegroundDrawList();
    float       vp_right  = vp_x + vp_w;
    float       vp_bottom = vp_y + vp_h;

    // Horizontal lines from selection edges to viewport edges
    // Top edge
    dl->AddLine(ImVec2(vp_x, y0), ImVec2(x0, y0), col, 0.5f);
    dl->AddLine(ImVec2(x1, y0), ImVec2(vp_right, y0), col, 0.5f);
    // Bottom edge
    dl->AddLine(ImVec2(vp_x, y1), ImVec2(x0, y1), col, 0.5f);
    dl->AddLine(ImVec2(x1, y1), ImVec2(vp_right, y1), col, 0.5f);

    // Vertical lines from selection edges to viewport edges
    // Left edge
    dl->AddLine(ImVec2(x0, vp_y), ImVec2(x0, y0), col, 0.5f);
    dl->AddLine(ImVec2(x0, y1), ImVec2(x0, vp_bottom), col, 0.5f);
    // Right edge
    dl->AddLine(ImVec2(x1, vp_y), ImVec2(x1, y0), col, 0.5f);
    dl->AddLine(ImVec2(x1, y1), ImVec2(x1, vp_bottom), col, 0.5f);
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
