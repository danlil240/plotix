#ifdef SPECTRA_USE_IMGUI

    #include "region_select.hpp"

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/series.hpp>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

void RegionSelect::set_fonts(ImFont* body, ImFont* heading)
{
    font_body_    = body;
    font_heading_ = heading;
}

// ─── Coordinate conversion ──────────────────────────────────────────────────

void RegionSelect::data_to_screen(float       data_x,
                                  float       data_y,
                                  const Rect& viewport,
                                  float       xlim_min,
                                  float       xlim_max,
                                  float       ylim_min,
                                  float       ylim_max,
                                  float&      screen_x,
                                  float&      screen_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    float norm_x = (data_x - xlim_min) / x_range;
    float norm_y = (data_y - ylim_min) / y_range;

    screen_x = viewport.x + norm_x * viewport.w;
    screen_y = viewport.y + (1.0f - norm_y) * viewport.h;
}

void RegionSelect::screen_to_data(double      screen_x,
                                  double      screen_y,
                                  const Rect& viewport,
                                  float       xlim_min,
                                  float       xlim_max,
                                  float       ylim_min,
                                  float       ylim_max,
                                  float&      data_x,
                                  float&      data_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;

    float norm_x = (static_cast<float>(screen_x) - viewport.x) / viewport.w;
    float norm_y = 1.0f - (static_cast<float>(screen_y) - viewport.y) / viewport.h;

    data_x = xlim_min + norm_x * x_range;
    data_y = ylim_min + norm_y * y_range;
}

// ─── Selection lifecycle ────────────────────────────────────────────────────

void RegionSelect::begin(double      screen_x,
                         double      screen_y,
                         const Rect& viewport,
                         float       xlim_min,
                         float       xlim_max,
                         float       ylim_min,
                         float       ylim_max)
{
    dragging_      = true;
    has_selection_ = false;
    selected_points_.clear();
    stats_ = {};

    screen_start_x_ = screen_x;
    screen_start_y_ = screen_y;
    screen_end_x_   = screen_x;
    screen_end_y_   = screen_y;

    screen_to_data(screen_x,
                   screen_y,
                   viewport,
                   xlim_min,
                   xlim_max,
                   ylim_min,
                   ylim_max,
                   data_x0_,
                   data_y0_);
    data_x1_ = data_x0_;
    data_y1_ = data_y0_;

    opacity_ = 0.0f;
}

void RegionSelect::update_drag(double      screen_x,
                               double      screen_y,
                               const Rect& viewport,
                               float       xlim_min,
                               float       xlim_max,
                               float       ylim_min,
                               float       ylim_max)
{
    if (!dragging_)
        return;

    screen_end_x_ = screen_x;
    screen_end_y_ = screen_y;

    screen_to_data(screen_x,
                   screen_y,
                   viewport,
                   xlim_min,
                   xlim_max,
                   ylim_min,
                   ylim_max,
                   data_x1_,
                   data_y1_);
}

void RegionSelect::finish(const Axes* axes)
{
    if (!dragging_)
        return;
    dragging_ = false;

    // Require minimum selection size (in data coords)
    float dx = std::abs(data_x1_ - data_x0_);
    float dy = std::abs(data_y1_ - data_y0_);
    if (dx < 1e-10f && dy < 1e-10f)
    {
        dismiss();
        return;
    }

    has_selection_ = true;

    if (axes)
    {
        collect_points(axes);
        compute_statistics();
    }
}

void RegionSelect::restore(float x_min, float x_max, float y_min, float y_max, const Axes* axes)
{
    dragging_      = false;
    has_selection_ = x_min != x_max || y_min != y_max;
    data_x0_       = x_min;
    data_x1_       = x_max;
    data_y0_       = y_min;
    data_y1_       = y_max;
    selected_points_.clear();
    stats_   = {};
    opacity_ = has_selection_ ? 1.0f : 0.0f;
    if (has_selection_ && axes)
    {
        collect_points(axes);
        compute_statistics();
    }
}

void RegionSelect::dismiss()
{
    dragging_      = false;
    has_selection_ = false;
    selected_points_.clear();
    stats_   = {};
    opacity_ = 0.0f;
}

// ─── Point collection ───────────────────────────────────────────────────────

void RegionSelect::collect_points(const Axes* axes)
{
    selected_points_.clear();
    if (!axes)
        return;

    float xmin = std::min(data_x0_, data_x1_);
    float xmax = std::max(data_x0_, data_x1_);
    float ymin = std::min(data_y0_, data_y1_);
    float ymax = std::max(data_y0_, data_y1_);

    for (auto& series_ptr : axes->series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;

        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t       count  = 0;

        if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
        {
            x_data = ls->x_data().data();
            y_data = ls->y_data().data();
            count  = ls->point_count();
        }
        else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
        {
            x_data = sc->x_data().data();
            y_data = sc->y_data().data();
            count  = sc->point_count();
        }

        if (!x_data || !y_data || count == 0)
            continue;

        for (size_t i = 0; i < count; ++i)
        {
            if (x_data[i] >= xmin && x_data[i] <= xmax && y_data[i] >= ymin && y_data[i] <= ymax)
            {
                selected_points_.push_back({series_ptr.get(), i, x_data[i], y_data[i]});
            }
        }
    }
}

void RegionSelect::compute_statistics()
{
    stats_ = {};
    if (selected_points_.empty())
        return;

    stats_.point_count = selected_points_.size();
    stats_.x_min       = selected_points_[0].data_x;
    stats_.x_max       = selected_points_[0].data_x;
    stats_.y_min       = selected_points_[0].data_y;
    stats_.y_max       = selected_points_[0].data_y;

    double sum_y = 0.0;
    for (const auto& pt : selected_points_)
    {
        stats_.x_min = std::min(stats_.x_min, pt.data_x);
        stats_.x_max = std::max(stats_.x_max, pt.data_x);
        stats_.y_min = std::min(stats_.y_min, pt.data_y);
        stats_.y_max = std::max(stats_.y_max, pt.data_y);
        sum_y += static_cast<double>(pt.data_y);
    }

    stats_.y_mean = static_cast<float>(sum_y / static_cast<double>(stats_.point_count));

    // Standard deviation
    if (stats_.point_count > 1)
    {
        double sum_sq = 0.0;
        for (const auto& pt : selected_points_)
        {
            double diff = static_cast<double>(pt.data_y) - static_cast<double>(stats_.y_mean);
            sum_sq += diff * diff;
        }
        stats_.y_std =
            static_cast<float>(std::sqrt(sum_sq / static_cast<double>(stats_.point_count - 1)));
    }
}

// ─── Drawing ────────────────────────────────────────────────────────────────

void RegionSelect::draw(const Rect& viewport,
                        float       xlim_min,
                        float       xlim_max,
                        float       ylim_min,
                        float       ylim_max,
                        float       window_width,
                        float       window_height)
{
    bool visible = dragging_ || has_selection_;

    // Animate opacity
    float target = visible ? 1.0f : 0.0f;
    float dt     = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 12.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors = theme_mgr_->colors();
    ImDrawList* fg     = ImGui::GetForegroundDrawList();

    // Convert data rect to screen rect
    float sx0 = NAN;
    float sy0 = NAN;
    float sx1 = NAN;
    float sy1 = NAN;
    data_to_screen(data_x0_, data_y0_, viewport, xlim_min, xlim_max, ylim_min, ylim_max, sx0, sy0);
    data_to_screen(data_x1_, data_y1_, viewport, xlim_min, xlim_max, ylim_min, ylim_max, sx1, sy1);

    // Normalize to min/max
    float rx0 = std::min(sx0, sx1);
    float ry0 = std::min(sy0, sy1);
    float rx1 = std::max(sx0, sx1);
    float ry1 = std::max(sy0, sy1);

    // Clamp to viewport
    rx0 = std::max(rx0, viewport.x);
    ry0 = std::max(ry0, viewport.y);
    rx1 = std::min(rx1, viewport.x + viewport.w);
    ry1 = std::min(ry1, viewport.y + viewport.h);

    // ROI fill using roi_fill theme token
    ImU32 fill_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.roi_fill.r, colors.roi_fill.g, colors.roi_fill.b, fill_alpha_ * opacity_));
    fg->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1), fill_col);

    // ROI border — dashed using roi_border theme token
    ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.roi_border.r,
                                                             colors.roi_border.g,
                                                             colors.roi_border.b,
                                                             colors.roi_border.a * opacity_));
    {
        constexpr float DASH        = 6.0f;
        constexpr float GAP         = 4.0f;
        auto            draw_dashed = [&](float x0, float y0, float x1, float y1)
        {
            float dx  = x1 - x0;
            float dy  = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1.0f)
                return;
            float nx  = dx / len;
            float ny  = dy / len;
            float pos = 0.0f;
            while (pos < len)
            {
                float end = std::min(pos + DASH, len);
                fg->AddLine(ImVec2(x0 + nx * pos, y0 + ny * pos),
                            ImVec2(x0 + nx * end, y0 + ny * end),
                            border_col,
                            1.5f);
                pos += DASH + GAP;
            }
        };
        draw_dashed(rx0, ry0, rx1, ry0);   // top
        draw_dashed(rx1, ry0, rx1, ry1);   // right
        draw_dashed(rx1, ry1, rx0, ry1);   // bottom
        draw_dashed(rx0, ry1, rx0, ry0);   // left
    }

    // Night theme glow around ROI border
    if (colors.glow_intensity > 0.01f)
    {
        ImU32 glow_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_glow.r,
                                                  colors.accent_glow.g,
                                                  colors.accent_glow.b,
                                                  colors.accent_glow.a * opacity_ * 0.25f));
        fg->AddRect(ImVec2(rx0 - 1, ry0 - 1), ImVec2(rx1 + 1, ry1 + 1), glow_col, 0.0f, 0, 2.0f);
    }

    // Corner handles (small squares)
    float handle_size = 4.0f;
    ImU32 handle_col  = border_col;
    auto  draw_handle = [&](float hx, float hy)
    {
        fg->AddRectFilled(ImVec2(hx - handle_size, hy - handle_size),
                          ImVec2(hx + handle_size, hy + handle_size),
                          handle_col,
                          1.0f);
    };
    draw_handle(rx0, ry0);
    draw_handle(rx1, ry0);
    draw_handle(rx0, ry1);
    draw_handle(rx1, ry1);

    // ─── Floating mini-toolbar (only when selection is finalized) ────────
    if (has_selection_ && !dragging_ && stats_.point_count > 0)
    {
        draw_mini_toolbar(rx0, ry0, rx1, ry1, window_width, window_height);
    }
}

void RegionSelect::draw_mini_toolbar(float rx0,
                                     float ry0,
                                     float rx1,
                                     float ry1,
                                     float window_width,
                                     float window_height)
{
    const auto& colors = theme_mgr_->colors();

    // Format statistics
    const std::string count_buf = std::format("{} points", stats_.point_count);
    const std::string mean_buf  = std::format("Mean: {:.4g}", stats_.y_mean);
    const std::string std_buf   = std::format("Std: {:.4g}", stats_.y_std);
    const std::string range_buf = std::format("X: [{:.4g}, {:.4g}]  Y: [{:.4g}, {:.4g}]",
                                              stats_.x_min,
                                              stats_.x_max,
                                              stats_.y_min,
                                              stats_.y_max);

    ImFont* font      = font_body_ ? font_body_ : ImGui::GetFont();
    float   font_size = font->LegacySize * 0.85f;

    ImVec2 count_sz = font->CalcTextSizeA(font_size, 400.0f, 0.0f, count_buf.c_str());
    ImVec2 mean_sz  = font->CalcTextSizeA(font_size, 400.0f, 0.0f, mean_buf.c_str());
    ImVec2 std_sz   = font->CalcTextSizeA(font_size, 400.0f, 0.0f, std_buf.c_str());
    ImVec2 range_sz = font->CalcTextSizeA(font_size, 400.0f, 0.0f, range_buf.c_str());

    constexpr float pad           = 10.0f;
    constexpr float row_h         = 16.0f;
    constexpr float dismiss_row_h = 20.0f;

    float content_w = std::max({count_sz.x, mean_sz.x, std_sz.x, range_sz.x, 120.0f});
    float toolbar_w = content_w + pad * 2.0f;
    float toolbar_h = pad * 2.0f + row_h * 4.0f + dismiss_row_h;

    // Position: below the selection rectangle, centered
    float cx = (rx0 + rx1) * 0.5f;
    float tx = cx - toolbar_w * 0.5f;
    float ty = ry1 + 8.0f;

    // Clamp to window
    if (tx < 4.0f)
        tx = 4.0f;
    if (tx + toolbar_w > window_width - 4.0f)
        tx = window_width - toolbar_w - 4.0f;
    if (ty + toolbar_h > window_height - 4.0f)
        ty = ry0 - toolbar_h - 8.0f;
    if (ty < 4.0f)
        ty = 4.0f;

    // Draw toolbar window
    ImGui::SetNextWindowPos(ImVec2(tx, ty));
    ImGui::SetNextWindowSize(ImVec2(toolbar_w, toolbar_h));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.border_default.r,
                                 colors.border_default.g,
                                 colors.border_default.b,
                                 colors.border_default.a));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##region_stats", nullptr, flags))
    {
        ImU32 text_primary   = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                                   colors.text_primary.g,
                                                                   colors.text_primary.b,
                                                                   colors.text_primary.a));
        ImU32 text_secondary = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                                     colors.text_secondary.g,
                                                                     colors.text_secondary.b,
                                                                     colors.text_secondary.a));
        ImU32 accent_col     = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, colors.accent.a));

        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      cursor = ImGui::GetCursorScreenPos();

        // Row 1: Point count (accent color, bold)
        dl->AddText(font, font_size, cursor, accent_col, count_buf.c_str());
        cursor.y += row_h;

        // Row 2: Mean
        dl->AddText(font, font_size, cursor, text_primary, mean_buf.c_str());
        cursor.y += row_h;

        // Row 3: Std
        dl->AddText(font, font_size, cursor, text_primary, std_buf.c_str());
        cursor.y += row_h;

        // Row 4: Range
        dl->AddText(font, font_size, cursor, text_secondary, range_buf.c_str());
        cursor.y += row_h + 4.0f;

        // Dismiss button
        ImGui::SetCursorScreenPos(cursor);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(colors.accent_subtle.r,
                                     colors.accent_subtle.g,
                                     colors.accent_subtle.b,
                                     colors.accent_subtle.a));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));

        if (ImGui::Button("Dismiss"))
        {
            dismiss();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
