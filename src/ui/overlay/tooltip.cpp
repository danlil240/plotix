#ifdef SPECTRA_USE_IMGUI

    #include "tooltip.hpp"

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes3d.hpp>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

void Tooltip::set_fonts(ImFont* body, ImFont* heading)
{
    font_body_    = body;
    font_heading_ = heading;
}

void Tooltip::fade_out_if_no_snap(const NearestPointResult& nearest, float dt)
{
    if (nearest.found)
        return;

    // No snap target — hide immediately. Do not keep full opacity during the
    // snap-radius hysteresis window; that drew a stale box at screen (0, 0).
    hysteresis_     = 0.0f;
    target_opacity_ = 0.0f;
    opacity_ += (target_opacity_ - opacity_) * std::min(1.0f, 10.0f * dt);
    if (std::abs(opacity_ - target_opacity_) < 0.01f)
        opacity_ = target_opacity_;
}

void Tooltip::draw(const NearestPointResult& nearest,
                   float                     window_width,
                   float                     window_height,
                   ImDrawList*               dl)
{
    if (!enabled_)
        return;

    float dt = ImGui::GetIO().DeltaTime;
    fade_out_if_no_snap(nearest, dt);
    if (!nearest.found)
        return;

    if (nearest.is_3d)
    {
        draw_3d(nearest, window_width, window_height, dl);
        return;
    }

    // Hysteresis: keep tooltip visible for 100ms after cursor leaves snap radius
    bool in_range = nearest.found && nearest.distance_px <= snap_radius_px_;

    if (in_range)
    {
        hysteresis_     = 0.0f;
        target_opacity_ = 1.0f;
    }
    else
    {
        hysteresis_ += dt;
        if (hysteresis_ > 0.1f)   // 100ms hysteresis delay
            target_opacity_ = 0.0f;
    }

    // Asymmetric fade: 50ms in (speed=20), 100ms out (speed=10)
    float speed = (target_opacity_ > opacity_) ? 20.0f : 10.0f;
    opacity_ += (target_opacity_ - opacity_) * std::min(1.0f, speed * dt);
    if (std::abs(opacity_ - target_opacity_) < 0.01f)
        opacity_ = target_opacity_;

    if (opacity_ < 0.01f)
        return;

    const auto& colors = theme_mgr_->colors();

    // Format coordinate strings.  Large x values (epoch timestamps) use
    // fixed notation with full precision instead of lossy scientific form.
    const std::string x_buf = std::abs(nearest.data_x) >= 1e6
                                  ? std::format("{:.3f}", nearest.data_x)
                                  : std::format("{:.6g}", nearest.data_x);
    const std::string y_buf = std::format("{:.6g}", nearest.data_y);

    // Format dy/dx value (label rendered separately)
    std::string dydx_value;
    bool        show_dydx = nearest.dy_dx_valid;
    if (show_dydx)
        dydx_value = std::format("{:.6g}", nearest.dy_dx);

    const char* series_name  = "Unknown";
    Color       series_color = colors::gray;
    if (nearest.series)
    {
        if (!nearest.series->label().empty())
            series_name = nearest.series->label().c_str();
        series_color = nearest.series->color();
    }

    // Tooltip layout constants
    constexpr float padding   = 10.0f;
    constexpr float swatch_r  = 5.0f;    // circle radius for color dot
    constexpr float col_gap   = 10.0f;   // gap between label and value columns
    constexpr float min_width = 140.0f;

    ImFont* body_font = font_body_ ? font_body_ : ImGui::GetFont();
    float   font_sz   = body_font->LegacySize;
    float   row_h     = font_sz + 4.0f;

    // Format label/value pairs
    const char* lbl_x    = "X";
    const char* lbl_y    = "Y";
    const char* lbl_dydx = "dy/dx";

    // Measure column widths
    float lbl_w =
        std::max({body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_x).x,
                  body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_y).x,
                  show_dydx ? body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_dydx).x : 0.0f});
    float val_w = std::max(
        {body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, x_buf.c_str()).x,
         body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, y_buf.c_str()).x,
         show_dydx ? body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, dydx_value.c_str()).x : 0.0f});
    float name_w =
        body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, series_name).x + swatch_r * 2.0f + 6.0f;
    float data_row_w = lbl_w + col_gap + val_w;
    float content_w  = std::max({name_w, data_row_w, min_width});
    int   data_rows  = 2 + (show_dydx ? 1 : 0);
    float divider_h  = 6.0f;
    float tooltip_w  = content_w + padding * 2.0f;
    float tooltip_h  = padding * 2.0f + row_h   // name row
                      + divider_h               // separator gap
                      + row_h * static_cast<float>(data_rows);

    // Position: above-right of the snap point, clamped to window
    float offset_x = 14.0f;
    float offset_y = -tooltip_h - 8.0f;
    float tx       = nearest.screen_x + offset_x;
    float ty       = nearest.screen_y + offset_y;

    if (tx + tooltip_w > window_width - 4.0f)
        tx = nearest.screen_x - tooltip_w - offset_x;
    if (ty < 4.0f)
        ty = nearest.screen_y + 16.0f;
    if (tx < 4.0f)
        tx = 4.0f;
    if (ty + tooltip_h > window_height - 4.0f)
        ty = window_height - tooltip_h - 4.0f;

    // Draw entirely on the foreground draw list — this renders above all ImGui windows,
    // including popups and open menus, since the foreground draw list is composited last.
    {
        ImDrawList* fg  = dl ? dl : ImGui::GetForegroundDrawList();
        float       rnd = ui::tokens::RADIUS_MD;

        // Shadow
        ImU32 sh_col = IM_COL32(0, 0, 0, static_cast<int>(40.0f * opacity_));
        fg->AddRectFilled(ImVec2(tx + 3.0f, ty + 3.0f),
                          ImVec2(tx + tooltip_w + 3.0f, ty + tooltip_h + 3.0f),
                          sh_col,
                          rnd + 2.0f);

        // Night-theme accent glow
        if (colors.glow_intensity > 0.01f)
        {
            ImU32 glow_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.10f * opacity_ * colors.glow_intensity));
            fg->AddRect(ImVec2(tx - 1.0f, ty - 1.0f),
                        ImVec2(tx + tooltip_w + 1.0f, ty + tooltip_h + 1.0f),
                        glow_col,
                        rnd + 2.0f,
                        0,
                        2.0f);
        }

        // Background
        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                             colors.tooltip_bg.g,
                                                             colors.tooltip_bg.b,
                                                             colors.tooltip_bg.a * opacity_));
        fg->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tooltip_w, ty + tooltip_h), bg_col, rnd);

        // Border
        ImU32 border_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                  colors.tooltip_border.g,
                                                  colors.tooltip_border.b,
                                                  colors.tooltip_border.a * opacity_));
        fg->AddRect(ImVec2(tx, ty),
                    ImVec2(tx + tooltip_w, ty + tooltip_h),
                    border_col,
                    rnd,
                    0,
                    0.75f);

        float ox = tx + padding;
        float oy = ty + padding;

        // ── Row 1: circle swatch + series name ──
        float cy_name = oy + (row_h - swatch_r * 2.0f) * 0.5f;
        fg->AddCircleFilled(ImVec2(ox + swatch_r, cy_name + swatch_r),
                            swatch_r,
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(series_color.r, series_color.g, series_color.b, opacity_)));
        fg->AddCircle(ImVec2(ox + swatch_r, cy_name + swatch_r),
                      swatch_r,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.18f * opacity_)));

        fg->AddText(body_font,
                    font_sz,
                    ImVec2(ox + swatch_r * 2.0f + 6.0f, oy + (row_h - font_sz) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                          colors.text_primary.g,
                                                          colors.text_primary.b,
                                                          colors.text_primary.a * opacity_)),
                    series_name);

        // ── Divider ──
        {
            float div_y   = oy + row_h + divider_h * 0.5f - 0.5f;
            ImU32 div_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                                  colors.border_subtle.g,
                                                                  colors.border_subtle.b,
                                                                  0.35f * opacity_));
            fg->AddLine(ImVec2(ox, div_y), ImVec2(ox + content_w, div_y), div_col, 0.75f);
        }

        // ── Data rows: label (dim) + value (bright) ──
        auto draw_data_row = [&](int row_idx, const char* label, const char* value)
        {
            float ry = oy + row_h + divider_h + row_h * static_cast<float>(row_idx);
            fg->AddText(body_font,
                        font_sz,
                        ImVec2(ox, ry + (row_h - font_sz) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                              colors.text_secondary.g,
                                                              colors.text_secondary.b,
                                                              0.70f * opacity_)),
                        label);
            fg->AddText(body_font,
                        font_sz,
                        ImVec2(ox + lbl_w + col_gap, ry + (row_h - font_sz) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                              colors.text_primary.g,
                                                              colors.text_primary.b,
                                                              colors.text_primary.a * opacity_)),
                        value);
        };

        draw_data_row(0, lbl_x, x_buf.c_str());
        draw_data_row(1, lbl_y, y_buf.c_str());
        if (show_dydx)
            draw_data_row(2, lbl_dydx, dydx_value.c_str());
    }

    // Draw triangular arrow pointer toward data point
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList*     fg         = dl ? dl : ImGui::GetForegroundDrawList();
        ImU32           arrow_col  = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                                colors.tooltip_bg.g,
                                                                colors.tooltip_bg.b,
                                                                colors.tooltip_bg.a * opacity_));
        constexpr float arrow_size = 6.0f;

        // Determine which edge the arrow should appear on
        bool tooltip_above = (ty + tooltip_h < nearest.screen_y);
        bool tooltip_below = (ty > nearest.screen_y);

        float arrow_x = std::clamp(nearest.screen_x,
                                   tx + arrow_size + 4.0f,
                                   tx + tooltip_w - arrow_size - 4.0f);

        if (tooltip_above)
        {
            // Arrow points down from tooltip bottom
            fg->AddTriangleFilled(ImVec2(arrow_x - arrow_size, ty + tooltip_h),
                                  ImVec2(arrow_x + arrow_size, ty + tooltip_h),
                                  ImVec2(arrow_x, ty + tooltip_h + arrow_size),
                                  arrow_col);
        }
        else if (tooltip_below)
        {
            // Arrow points up from tooltip top
            fg->AddTriangleFilled(ImVec2(arrow_x - arrow_size, ty),
                                  ImVec2(arrow_x + arrow_size, ty),
                                  ImVec2(arrow_x, ty - arrow_size),
                                  arrow_col);
        }
    }

    // Draw snap indicator dot at the data point and connection line
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList* fg        = dl ? dl : ImGui::GetForegroundDrawList();
        ImU32       dot_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(series_color.r, series_color.g, series_color.b, opacity_));
        ImU32 ring_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity_));

        // Connection line from tooltip to data point (dashed, subtle)
        {
            ImVec2 tooltip_center(tx + tooltip_w * 0.5f, ty + tooltip_h);
            ImVec2 data_point(nearest.screen_x, nearest.screen_y);

            // Only draw if there's meaningful distance
            float dx   = data_point.x - tooltip_center.x;
            float dy   = data_point.y - tooltip_center.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 20.0f)
            {
                ImU32 line_color =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                          colors.crosshair.g,
                                                          colors.crosshair.b,
                                                          colors.crosshair.a * 0.4f * opacity_));
                // Draw dashed connection line
                constexpr float dash_len = 4.0f;
                constexpr float gap_len  = 3.0f;
                float           nx       = dx / dist;
                float           ny       = dy / dist;
                float           pos      = 0.0f;
                while (pos < dist)
                {
                    float seg_start = pos;
                    float seg_end   = std::min(pos + dash_len, dist);
                    fg->AddLine(
                        ImVec2(tooltip_center.x + nx * seg_start,
                               tooltip_center.y + ny * seg_start),
                        ImVec2(tooltip_center.x + nx * seg_end, tooltip_center.y + ny * seg_end),
                        line_color,
                        1.0f);
                    pos = seg_end + gap_len;
                }
            }
        }

        // Night theme: series-colored glow halo around snap dot (Vision.png style)
        if (colors.glow_intensity > 0.01f)
        {
            // Outer soft bloom — large, very faint
            ImU32 glow_outer =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.12f * opacity_ * colors.glow_intensity));
            fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 12.0f, glow_outer, 24);
            // Inner bright bloom
            ImU32 glow_inner =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.30f * opacity_ * colors.glow_intensity));
            fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 7.0f, glow_inner, 16);
        }

        fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, dot_color);
        fg->AddCircle(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, ring_color, 0, 1.0f);
    }
}

void Tooltip::draw_3d(const NearestPointResult& nearest,
                      float                     window_width,
                      float                     window_height,
                      ImDrawList*               dl)
{
    const float snap_radius = snap_radius_3d_px_;

    float dt = ImGui::GetIO().DeltaTime;
    fade_out_if_no_snap(nearest, dt);
    if (!nearest.found)
        return;

    bool in_range = nearest.found && nearest.distance_px <= snap_radius;

    if (in_range)
    {
        hysteresis_     = 0.0f;
        target_opacity_ = 1.0f;
    }
    else
    {
        hysteresis_ += dt;
        if (hysteresis_ > 0.1f)
            target_opacity_ = 0.0f;
    }

    float speed = (target_opacity_ > opacity_) ? 20.0f : 10.0f;
    opacity_ += (target_opacity_ - opacity_) * std::min(1.0f, speed * dt);
    if (std::abs(opacity_ - target_opacity_) < 0.01f)
        opacity_ = target_opacity_;

    if (opacity_ < 0.01f)
        return;

    const auto& colors = theme_mgr_->colors();

    const std::string x_buf = std::format("{:.6g}", nearest.data_x);
    const std::string y_buf = std::format("{:.6g}", nearest.data_y);
    const std::string z_buf = std::format("{:.6g}", nearest.data_z);

    const char* series_name  = "Unknown";
    Color       series_color = colors::gray;
    if (nearest.series)
    {
        if (!nearest.series->label().empty())
            series_name = nearest.series->label().c_str();
        series_color = nearest.series->color();
    }

    const char* lbl_x = "X";
    const char* lbl_y = "Y";
    const char* lbl_z = "Z";
    if (nearest.axes3d)
    {
        if (!nearest.axes3d->xlabel().empty())
            lbl_x = nearest.axes3d->xlabel().c_str();
        if (!nearest.axes3d->ylabel().empty())
            lbl_y = nearest.axes3d->ylabel().c_str();
        if (!nearest.axes3d->zlabel().empty())
            lbl_z = nearest.axes3d->zlabel().c_str();
    }

    constexpr float padding     = 12.0f;
    constexpr float accent_w    = 4.0f;
    constexpr float swatch_r    = 5.0f;
    constexpr float col_gap     = 12.0f;
    constexpr float min_width   = 168.0f;
    constexpr float depth_bar_h = 3.0f;
    constexpr float depth_pad   = 6.0f;

    ImFont* body_font = font_body_ ? font_body_ : ImGui::GetFont();
    float   font_sz   = ui::tokens::FONT_MONO;
    float   row_h     = font_sz + 6.0f;

    float lbl_w = std::max({body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_x).x,
                            body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_y).x,
                            body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_z).x});
    float val_w = std::max({body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, x_buf.c_str()).x,
                            body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, y_buf.c_str()).x,
                            body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, z_buf.c_str()).x});
    float name_w =
        body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, series_name).x + swatch_r * 2.0f + 8.0f;
    float content_w = std::max({name_w, lbl_w + col_gap + val_w, min_width});
    float divider_h = 8.0f;
    float tooltip_w = content_w + padding * 2.0f + accent_w;
    float tooltip_h = padding * 2.0f + row_h + divider_h + row_h * 3.0f + depth_pad + depth_bar_h;

    float offset_x = 18.0f;
    float offset_y = -tooltip_h - 12.0f;
    float tx       = nearest.screen_x + offset_x;
    float ty       = nearest.screen_y + offset_y;

    if (tx + tooltip_w > window_width - 4.0f)
        tx = nearest.screen_x - tooltip_w - offset_x;
    if (ty < 4.0f)
        ty = nearest.screen_y + 20.0f;
    if (tx < 4.0f)
        tx = 4.0f;
    if (ty + tooltip_h > window_height - 4.0f)
        ty = window_height - tooltip_h - 4.0f;

    ImDrawList* fg  = dl ? dl : ImGui::GetForegroundDrawList();
    float       rnd = ui::tokens::RADIUS_MD;

    ImU32 sh_col = IM_COL32(0, 0, 0, static_cast<int>(48.0f * opacity_));
    fg->AddRectFilled(ImVec2(tx + 4.0f, ty + 4.0f),
                      ImVec2(tx + tooltip_w + 4.0f, ty + tooltip_h + 4.0f),
                      sh_col,
                      rnd + 2.0f);

    if (colors.glow_intensity > 0.01f)
    {
        ImU32 glow_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                  series_color.g,
                                                  series_color.b,
                                                  0.14f * opacity_ * colors.glow_intensity));
        fg->AddRect(ImVec2(tx - 1.0f, ty - 1.0f),
                    ImVec2(tx + tooltip_w + 1.0f, ty + tooltip_h + 1.0f),
                    glow_col,
                    rnd + 2.0f,
                    0,
                    2.0f);
    }

    ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                         colors.tooltip_bg.g,
                                                         colors.tooltip_bg.b,
                                                         colors.tooltip_bg.a * opacity_));
    fg->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tooltip_w, ty + tooltip_h), bg_col, rnd);

    fg->AddRectFilled(ImVec2(tx, ty),
                      ImVec2(tx + accent_w, ty + tooltip_h),
                      ImGui::ColorConvertFloat4ToU32(
                          ImVec4(series_color.r, series_color.g, series_color.b, opacity_)),
                      rnd,
                      ImDrawFlags_RoundCornersLeft);

    ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                             colors.tooltip_border.g,
                                                             colors.tooltip_border.b,
                                                             colors.tooltip_border.a * opacity_));
    fg->AddRect(ImVec2(tx, ty), ImVec2(tx + tooltip_w, ty + tooltip_h), border_col, rnd, 0, 0.75f);

    float ox = tx + accent_w + padding;
    float oy = ty + padding;

    float cy_name = oy + (row_h - swatch_r * 2.0f) * 0.5f;
    fg->AddCircleFilled(ImVec2(ox + swatch_r, cy_name + swatch_r),
                        swatch_r,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(series_color.r, series_color.g, series_color.b, opacity_)));
    fg->AddText(body_font,
                font_sz + 1.0f,
                ImVec2(ox + swatch_r * 2.0f + 8.0f, oy + (row_h - font_sz - 1.0f) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                      colors.text_primary.g,
                                                      colors.text_primary.b,
                                                      colors.text_primary.a * opacity_)),
                series_name);

    {
        float div_y   = oy + row_h + divider_h * 0.5f - 0.5f;
        ImU32 div_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                              colors.border_subtle.g,
                                                              colors.border_subtle.b,
                                                              0.40f * opacity_));
        fg->AddLine(ImVec2(ox, div_y), ImVec2(ox + content_w, div_y), div_col, 0.75f);
    }

    auto draw_coord_row = [&](int row_idx, const char* label, const char* value, bool highlight)
    {
        float ry = oy + row_h + divider_h + row_h * static_cast<float>(row_idx);
        if (highlight)
        {
            ImU32 chip_bg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(series_color.r, series_color.g, series_color.b, 0.10f * opacity_));
            fg->AddRectFilled(ImVec2(ox - 4.0f, ry - 1.0f),
                              ImVec2(ox + content_w + 4.0f, ry + row_h + 1.0f),
                              chip_bg,
                              ui::tokens::RADIUS_SM);
        }
        fg->AddText(body_font,
                    font_sz,
                    ImVec2(ox, ry + (row_h - font_sz) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                          colors.text_secondary.g,
                                                          colors.text_secondary.b,
                                                          0.75f * opacity_)),
                    label);
        fg->AddText(body_font,
                    font_sz,
                    ImVec2(ox + lbl_w + col_gap, ry + (row_h - font_sz) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                          colors.text_primary.g,
                                                          colors.text_primary.b,
                                                          colors.text_primary.a * opacity_)),
                    value);
    };

    draw_coord_row(0, lbl_x, x_buf.c_str(), false);
    draw_coord_row(1, lbl_y, y_buf.c_str(), false);
    draw_coord_row(2, lbl_z, z_buf.c_str(), true);

    if (nearest.axes3d)
    {
        auto  zlim   = nearest.axes3d->z_limits();
        float z_span = static_cast<float>(zlim.max - zlim.min);
        float z_norm = 0.5f;
        if (z_span > 1e-12f)
            z_norm =
                std::clamp((nearest.data_z - static_cast<float>(zlim.min)) / z_span, 0.0f, 1.0f);

        float bar_y  = oy + row_h + divider_h + row_h * 3.0f + depth_pad;
        float bar_x0 = ox;
        float bar_x1 = ox + content_w;

        ImU32 track_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                                colors.border_subtle.g,
                                                                colors.border_subtle.b,
                                                                0.25f * opacity_));
        fg->AddRectFilled(ImVec2(bar_x0, bar_y),
                          ImVec2(bar_x1, bar_y + depth_bar_h),
                          track_col,
                          depth_bar_h * 0.5f);

        float fill_x   = bar_x0 + z_norm * (bar_x1 - bar_x0);
        ImU32 fill_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(series_color.r, series_color.g, series_color.b, 0.85f * opacity_));
        fg->AddRectFilled(ImVec2(bar_x0, bar_y),
                          ImVec2(fill_x, bar_y + depth_bar_h),
                          fill_col,
                          depth_bar_h * 0.5f);
    }

    if (nearest.found && in_range)
    {
        float pulse = 0.5f + 0.5f * std::sin(ImGui::GetTime() * 6.0f);
        ImU32 pulse_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                  series_color.g,
                                                  series_color.b,
                                                  (0.15f + 0.20f * pulse) * opacity_));
        fg->AddCircle(ImVec2(nearest.screen_x, nearest.screen_y),
                      10.0f + pulse * 4.0f,
                      pulse_col,
                      32,
                      2.0f);

        ImU32 dot_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(series_color.r, series_color.g, series_color.b, opacity_));
        ImU32 ring_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity_));

        if (colors.glow_intensity > 0.01f)
        {
            ImU32 glow_outer =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.14f * opacity_ * colors.glow_intensity));
            fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 14.0f, glow_outer, 24);
        }

        fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 5.0f, dot_color);
        fg->AddCircle(ImVec2(nearest.screen_x, nearest.screen_y), 5.0f, ring_color, 0, 1.25f);

        ImVec2 tooltip_anchor(tx + tooltip_w * 0.5f, ty + tooltip_h);
        ImVec2 data_point(nearest.screen_x, nearest.screen_y);
        float  dx   = data_point.x - tooltip_anchor.x;
        float  dy   = data_point.y - tooltip_anchor.y;
        float  dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 24.0f)
        {
            ImU32 line_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(series_color.r, series_color.g, series_color.b, 0.45f * opacity_));
            constexpr float dash_len = 5.0f;
            constexpr float gap_len  = 4.0f;
            float           nx       = dx / dist;
            float           ny       = dy / dist;
            float           pos      = 0.0f;
            while (pos < dist)
            {
                float seg_start = pos;
                float seg_end   = std::min(pos + dash_len, dist);
                fg->AddLine(
                    ImVec2(tooltip_anchor.x + nx * seg_start, tooltip_anchor.y + ny * seg_start),
                    ImVec2(tooltip_anchor.x + nx * seg_end, tooltip_anchor.y + ny * seg_end),
                    line_color,
                    1.25f);
                pos = seg_end + gap_len;
            }
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
