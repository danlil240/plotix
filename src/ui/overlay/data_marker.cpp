#ifdef SPECTRA_USE_IMGUI

    #include "data_marker.hpp"

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes3d.hpp>

    #include "axes3d_pick.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

void DataMarkerManager::add(double        data_x,
                            float         data_y,
                            const Series* series,
                            size_t        index,
                            const Axes*   axes,
                            float         dy_dx,
                            bool          dy_dx_valid)
{
    DataMarker m;
    m.data_x       = data_x;
    m.data_y       = data_y;
    m.series       = series;
    m.point_index  = index;
    m.color        = series ? series->color() : colors::white;
    m.series_label = series ? series->label() : std::string();
    m.axes         = axes;
    m.dy_dx        = dy_dx;
    m.dy_dx_valid  = dy_dx_valid;
    markers_.push_back(m);
}

bool DataMarkerManager::toggle_or_add(double        data_x,
                                      float         data_y,
                                      const Series* series,
                                      size_t        index,
                                      const Axes*   axes,
                                      float         dy_dx,
                                      bool          dy_dx_valid)
{
    int existing = find_duplicate(series, index);
    if (existing >= 0)
    {
        remove(static_cast<size_t>(existing));
        return false;
    }
    add(data_x, data_y, series, index, axes, dy_dx, dy_dx_valid);
    return true;
}

void DataMarkerManager::add_3d(float         data_x,
                               float         data_y,
                               float         data_z,
                               const Series* series,
                               size_t        index,
                               const Axes3D* axes3d)
{
    DataMarker m;
    m.data_x       = data_x;
    m.data_y       = data_y;
    m.data_z       = data_z;
    m.series       = series;
    m.point_index  = index;
    m.color        = series ? series->color() : colors::white;
    m.series_label = series ? series->label() : std::string();
    m.axes3d       = axes3d;
    m.is_3d        = true;
    markers_.push_back(m);
}

bool DataMarkerManager::toggle_or_add_3d(float         data_x,
                                         float         data_y,
                                         float         data_z,
                                         const Series* series,
                                         size_t        index,
                                         const Axes3D* axes3d)
{
    int existing = find_duplicate(series, index);
    if (existing >= 0)
    {
        remove(static_cast<size_t>(existing));
        return false;
    }
    add_3d(data_x, data_y, data_z, series, index, axes3d);
    return true;
}

int DataMarkerManager::find_duplicate(const Series* series, size_t point_index) const
{
    for (size_t i = 0; i < markers_.size(); ++i)
    {
        if (markers_[i].series == series && markers_[i].point_index == point_index)
            return static_cast<int>(i);
    }
    return -1;
}

void DataMarkerManager::remove_for_series(const Series* series)
{
    markers_.erase(std::remove_if(markers_.begin(),
                                  markers_.end(),
                                  [series](const DataMarker& m) { return m.series == series; }),
                   markers_.end());
}

void DataMarkerManager::remove(size_t marker_index)
{
    if (marker_index < markers_.size())
    {
        markers_.erase(markers_.begin() + static_cast<std::ptrdiff_t>(marker_index));
    }
}

void DataMarkerManager::clear()
{
    markers_.clear();
}

void DataMarkerManager::data_to_screen(double      data_x,
                                       float       data_y,
                                       const Rect& viewport,
                                       double      xlim_min,
                                       double      xlim_max,
                                       float       ylim_min,
                                       float       ylim_max,
                                       float&      screen_x,
                                       float&      screen_y)
{
    double x_range = xlim_max - xlim_min;
    float  y_range = ylim_max - ylim_min;
    if (x_range == 0.0)
        x_range = 1.0;
    if (y_range == 0.0f)
        y_range = 1.0f;

    // Subtract in double first: at epoch-scale x the difference is small
    // and converts to float without precision loss.
    auto  norm_x = static_cast<float>((data_x - xlim_min) / x_range);
    float norm_y = (data_y - ylim_min) / y_range;

    screen_x = viewport.x + norm_x * viewport.w;
    // Y is inverted (screen Y goes down, data Y goes up)
    screen_y = viewport.y + (1.0f - norm_y) * viewport.h;
}

void DataMarkerManager::draw(const Rect& viewport,
                             double      xlim_min,
                             double      xlim_max,
                             float       ylim_min,
                             float       ylim_max,
                             float       opacity,
                             const Axes* filter_axes,
                             ImDrawList* dl)
{
    if (markers_.empty())
        return;

    const auto& colors   = theme_mgr_->colors();
    ImDrawList* fg       = dl ? dl : ImGui::GetForegroundDrawList();
    ImFont*     font     = ImGui::GetFont();
    const float fs       = font->LegacySize;
    const float fs_sm    = fs * 0.78f;   // small font for coordinates
    const float pad_x    = 8.0f;         // horizontal padding inside box
    const float pad_y    = 5.0f;         // vertical padding inside box
    const float arrow_h  = 7.0f;         // height of the arrow triangle
    const float arrow_w  = 7.0f;         // half-width of the arrow base
    const float corner_r = 8.0f;         // r8: consistent with tooltips
    const float dot_r    = 4.5f;         // marker dot radius
    const float ring_r   = 6.0f;         // outer ring radius
    const float gap      = 4.0f;         // gap between dot and arrow tip

    for (const auto& m : markers_)
    {
        if (m.is_3d)
            continue;

        // Filter by axes when requested (multi-subplot support)
        if (filter_axes && m.axes != filter_axes)
            continue;

        float sx = NAN;
        float sy = NAN;
        data_to_screen(m.data_x,
                       m.data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);

        // Skip if outside viewport
        if (sx < viewport.x || sx > viewport.x + viewport.w || sy < viewport.y
            || sy > viewport.y + viewport.h)
            continue;

        // ── Marker dot ──────────────────────────────────────────────────
        ImU32 ring_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), ring_r, ring_col);

        ImU32 fill_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(m.color.r, m.color.g, m.color.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), dot_r, fill_col);

        ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_default.r,
                                                                 colors.border_default.g,
                                                                 colors.border_default.b,
                                                                 opacity * 0.4f));
        fg->AddCircle(ImVec2(sx, sy), ring_r, border_col, 0, 1.0f);

        // ── Build label text ────────────────────────────────────────────
        // Large x values (epoch timestamps) use fixed notation to stay precise.
        const std::string coord_buf =
            std::abs(m.data_x) >= 1e6 ? std::format("X: {:.3f}   Y: {:.4g}", m.data_x, m.data_y)
                                      : std::format("X: {:.4g}   Y: {:.4g}", m.data_x, m.data_y);

        std::string deriv_buf;
        bool        has_deriv = m.dy_dx_valid;
        if (has_deriv)
            deriv_buf = std::format("dy/dx: {:.4g}", m.dy_dx);

        bool has_name = !m.series_label.empty();

        // Measure text sizes
        ImVec2 name_sz = has_name ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, m.series_label.c_str())
                                  : ImVec2(0, 0);
        ImVec2 coord_sz = font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, coord_buf.c_str());
        ImVec2 deriv_sz =
            has_deriv ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, deriv_buf.c_str()) : ImVec2(0, 0);

        float text_w = std::max({name_sz.x, coord_sz.x, deriv_sz.x});
        float text_h = coord_sz.y + (has_name ? (name_sz.y + 3.0f) : 0.0f)
                       + (has_deriv ? (deriv_sz.y + 3.0f) : 0.0f);

        float box_w = text_w + pad_x * 2.0f;
        float box_h = text_h + pad_y * 2.0f;

        // ── Position the box above the point (flip below if too close to top)
        bool  flip        = (sy - ring_r - gap - arrow_h - box_h) < viewport.y;
        float box_cx      = sx;   // centered horizontally on point
        float box_top     = NAN;
        float box_bot     = NAN;
        float arrow_tip_y = NAN;

        if (!flip)
        {
            // Box above point
            arrow_tip_y = sy - ring_r - gap;
            box_bot     = arrow_tip_y - arrow_h;
            box_top     = box_bot - box_h;
        }
        else
        {
            // Box below point
            arrow_tip_y = sy + ring_r + gap;
            box_top     = arrow_tip_y + arrow_h;
            box_bot     = box_top + box_h;
        }

        float box_left  = box_cx - box_w * 0.5f;
        float box_right = box_cx + box_w * 0.5f;

        // Clamp horizontally within viewport
        if (box_left < viewport.x + 2.0f)
        {
            float shift = (viewport.x + 2.0f) - box_left;
            box_left += shift;
            box_right += shift;
        }
        if (box_right > viewport.x + viewport.w - 2.0f)
        {
            float shift = box_right - (viewport.x + viewport.w - 2.0f);
            box_left -= shift;
            box_right -= shift;
        }

        // ── Draw shadow ─────────────────────────────────────────────
        ImU32 shadow_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.12f * opacity));
        fg->AddRectFilled(ImVec2(box_left + 1.0f, box_top + 2.0f),
                          ImVec2(box_right + 1.0f, box_bot + 2.0f),
                          shadow_col,
                          corner_r);

        // ── Draw box background — glass-like, matches tooltip_bg ──────────
        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                             colors.tooltip_bg.g,
                                                             colors.tooltip_bg.b,
                                                             colors.tooltip_bg.a * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top), ImVec2(box_right, box_bot), bg_col, corner_r);

        // ── Draw arrow triangle connecting box to point ─────────────────
        float acx = std::clamp(sx, box_left + corner_r, box_right - corner_r);
        if (!flip)
        {
            fg->AddTriangleFilled(ImVec2(acx - arrow_w, box_bot),
                                  ImVec2(acx + arrow_w, box_bot),
                                  ImVec2(acx, arrow_tip_y),
                                  bg_col);
        }
        else
        {
            fg->AddTriangleFilled(ImVec2(acx - arrow_w, box_top),
                                  ImVec2(acx + arrow_w, box_top),
                                  ImVec2(acx, arrow_tip_y),
                                  bg_col);
        }

        // ── Draw box border — hairline, matches tooltip_border ────────────
        ImU32 box_border =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                  colors.tooltip_border.g,
                                                  colors.tooltip_border.b,
                                                  colors.tooltip_border.a * opacity));
        fg->AddRect(ImVec2(box_left, box_top),
                    ImVec2(box_right, box_bot),
                    box_border,
                    corner_r,
                    0,
                    0.5f);

        // ── Color accent bar on left edge ─────────────────────────────────
        ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(m.color.r, m.color.g, m.color.b, 0.85f * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top + corner_r),
                          ImVec2(box_left + 2.5f, box_bot - corner_r),
                          accent_col);

        // ── Draw text ───────────────────────────────────────────────────
        ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity));
        ImU32 text_dim = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                               colors.text_secondary.g,
                                                               colors.text_secondary.b,
                                                               opacity));

        float tx = box_left + pad_x;
        float ty = box_top + pad_y;

        if (has_name)
        {
            fg->AddText(font, fs_sm, ImVec2(tx, ty), text_col, m.series_label.c_str());
            ty += name_sz.y + 3.0f;
        }
        fg->AddText(font, fs_sm, ImVec2(tx, ty), text_dim, coord_buf.c_str());
        if (has_deriv)
        {
            ty += coord_sz.y + 3.0f;
            fg->AddText(font, fs_sm, ImVec2(tx, ty), text_dim, deriv_buf.c_str());
        }
    }
}

int DataMarkerManager::hit_test(float       screen_x,
                                float       screen_y,
                                const Rect& viewport,
                                double      xlim_min,
                                double      xlim_max,
                                float       ylim_min,
                                float       ylim_max,
                                float       radius_px,
                                const Axes* filter_axes) const
{
    // Constants matching draw() for label box geometry
    ImFont*     font    = ImGui::GetFont();
    const float fs      = font->LegacySize;
    const float fs_sm   = fs * 0.78f;
    const float pad_x   = 8.0f;
    const float pad_y   = 5.0f;
    const float arrow_h = 7.0f;
    const float ring_r  = 6.0f;
    const float gap     = 4.0f;

    for (size_t i = 0; i < markers_.size(); ++i)
    {
        const auto& m = markers_[i];

        if (m.is_3d)
            continue;

        // Filter by axes when requested
        if (filter_axes && m.axes != filter_axes)
            continue;

        float sx = NAN;
        float sy = NAN;
        data_to_screen(m.data_x,
                       m.data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);

        // 1) Check the dot itself
        float dx = screen_x - sx;
        float dy = screen_y - sy;
        if (dx * dx + dy * dy <= radius_px * radius_px)
        {
            return static_cast<int>(i);
        }

        // 2) Check the label box (replicate geometry from draw)
        const std::string coord_buf =
            std::abs(m.data_x) >= 1e6 ? std::format("X: {:.3f}   Y: {:.4g}", m.data_x, m.data_y)
                                      : std::format("X: {:.4g}   Y: {:.4g}", m.data_x, m.data_y);

        std::string deriv_buf;
        bool        has_deriv = m.dy_dx_valid;
        if (has_deriv)
            deriv_buf = std::format("dy/dx: {:.4g}", m.dy_dx);

        bool   has_name = !m.series_label.empty();
        ImVec2 name_sz = has_name ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, m.series_label.c_str())
                                  : ImVec2(0, 0);
        ImVec2 coord_sz = font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, coord_buf.c_str());
        ImVec2 deriv_sz =
            has_deriv ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, deriv_buf.c_str()) : ImVec2(0, 0);

        float text_w = std::max({name_sz.x, coord_sz.x, deriv_sz.x});
        float text_h = coord_sz.y + (has_name ? (name_sz.y + 3.0f) : 0.0f)
                       + (has_deriv ? (deriv_sz.y + 3.0f) : 0.0f);
        float box_w = text_w + pad_x * 2.0f;
        float box_h = text_h + pad_y * 2.0f;

        bool  flip    = (sy - ring_r - gap - arrow_h - box_h) < viewport.y;
        float box_top = NAN;
        float box_bot = NAN;
        if (!flip)
        {
            float arrow_tip_y = sy - ring_r - gap;
            box_bot           = arrow_tip_y - arrow_h;
            box_top           = box_bot - box_h;
        }
        else
        {
            float arrow_tip_y = sy + ring_r + gap;
            box_top           = arrow_tip_y + arrow_h;
            box_bot           = box_top + box_h;
        }

        float box_left  = sx - box_w * 0.5f;
        float box_right = sx + box_w * 0.5f;

        // Apply same horizontal clamping as draw()
        if (box_left < viewport.x + 2.0f)
        {
            float shift = (viewport.x + 2.0f) - box_left;
            box_left += shift;
            box_right += shift;
        }
        if (box_right > viewport.x + viewport.w - 2.0f)
        {
            float shift = box_right - (viewport.x + viewport.w - 2.0f);
            box_left -= shift;
            box_right -= shift;
        }

        // Expand hit area slightly for easier clicking
        constexpr float hit_margin = 2.0f;
        if (screen_x >= box_left - hit_margin && screen_x <= box_right + hit_margin
            && screen_y >= std::min(box_top, box_bot) - hit_margin
            && screen_y <= std::max(box_top, box_bot) + hit_margin)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DataMarkerManager::draw_3d(const Axes3D& axes3d, float opacity, ImDrawList* dl)
{
    if (markers_.empty())
        return;

    const auto& colors   = theme_mgr_->colors();
    ImDrawList* fg       = dl ? dl : ImGui::GetForegroundDrawList();
    ImFont*     font     = ImGui::GetFont();
    const float fs_sm    = font->LegacySize * 0.72f;
    const float pad_x    = 6.0f;
    const float pad_y    = 3.0f;
    const float arrow_h  = 5.0f;
    const float arrow_w  = 5.0f;
    const float corner_r = 6.0f;
    const float dot_r    = 3.5f;
    const float ring_r   = 5.0f;
    const float gap      = 3.0f;

    const Rect& viewport = axes3d.viewport();

    for (const auto& m : markers_)
    {
        if (!m.is_3d || m.axes3d != &axes3d)
            continue;

        const Projected3DPoint proj =
            project_axes3d_data_point(axes3d, {m.data_x, m.data_y, m.data_z});
        if (!proj.visible)
            continue;

        const float sx = proj.screen_x;
        const float sy = proj.screen_y;
        if (sx < viewport.x || sx > viewport.x + viewport.w || sy < viewport.y
            || sy > viewport.y + viewport.h)
            continue;

        ImU32 ring_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), ring_r, ring_col);
        ImU32 fill_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(m.color.r, m.color.g, m.color.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), dot_r, fill_col);

        const std::string coord_buf =
            std::format("{:.4g}, {:.4g}, {:.4g}", m.data_x, m.data_y, m.data_z);
        bool has_name = !m.series_label.empty();

        ImVec2 name_sz = has_name ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, m.series_label.c_str())
                                  : ImVec2(0, 0);
        ImVec2 coord_sz = font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, coord_buf.c_str());

        float text_w = std::max(name_sz.x, coord_sz.x);
        float text_h = coord_sz.y + (has_name ? (name_sz.y + 2.0f) : 0.0f);
        float box_w  = text_w + pad_x * 2.0f;
        float box_h  = text_h + pad_y * 2.0f;

        bool  flip        = (sy - ring_r - gap - arrow_h - box_h) < viewport.y;
        float box_top     = NAN;
        float box_bot     = NAN;
        float arrow_tip_y = NAN;
        if (!flip)
        {
            arrow_tip_y = sy - ring_r - gap;
            box_bot     = arrow_tip_y - arrow_h;
            box_top     = box_bot - box_h;
        }
        else
        {
            arrow_tip_y = sy + ring_r + gap;
            box_top     = arrow_tip_y + arrow_h;
            box_bot     = box_top + box_h;
        }

        float box_left  = sx - box_w * 0.5f;
        float box_right = sx + box_w * 0.5f;
        if (box_left < viewport.x + 2.0f)
        {
            float shift = (viewport.x + 2.0f) - box_left;
            box_left += shift;
            box_right += shift;
        }
        if (box_right > viewport.x + viewport.w - 2.0f)
        {
            float shift = box_right - (viewport.x + viewport.w - 2.0f);
            box_left -= shift;
            box_right -= shift;
        }

        ImU32 shadow_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.10f * opacity));
        fg->AddRectFilled(ImVec2(box_left + 1.0f, box_top + 1.0f),
                          ImVec2(box_right + 1.0f, box_bot + 1.0f),
                          shadow_col,
                          corner_r);

        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                             colors.tooltip_bg.g,
                                                             colors.tooltip_bg.b,
                                                             colors.tooltip_bg.a * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top), ImVec2(box_right, box_bot), bg_col, corner_r);

        float acx = std::clamp(sx, box_left + corner_r, box_right - corner_r);
        if (!flip)
        {
            fg->AddTriangleFilled(ImVec2(acx - arrow_w, box_bot),
                                  ImVec2(acx + arrow_w, box_bot),
                                  ImVec2(acx, arrow_tip_y),
                                  bg_col);
        }
        else
        {
            fg->AddTriangleFilled(ImVec2(acx - arrow_w, box_top),
                                  ImVec2(acx + arrow_w, box_top),
                                  ImVec2(acx, arrow_tip_y),
                                  bg_col);
        }

        ImU32 box_border =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                  colors.tooltip_border.g,
                                                  colors.tooltip_border.b,
                                                  colors.tooltip_border.a * opacity));
        fg->AddRect(ImVec2(box_left, box_top),
                    ImVec2(box_right, box_bot),
                    box_border,
                    corner_r,
                    0,
                    0.5f);

        ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(m.color.r, m.color.g, m.color.b, 0.85f * opacity));
        fg->AddRectFilled(ImVec2(box_left, box_top + corner_r),
                          ImVec2(box_left + 2.0f, box_bot - corner_r),
                          accent_col);

        ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity));
        ImU32 text_dim = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                               colors.text_secondary.g,
                                                               colors.text_secondary.b,
                                                               opacity));

        float tx = box_left + pad_x;
        float ty = box_top + pad_y;
        if (has_name)
        {
            fg->AddText(font, fs_sm, ImVec2(tx, ty), text_col, m.series_label.c_str());
            ty += name_sz.y + 2.0f;
        }
        fg->AddText(font, fs_sm, ImVec2(tx, ty), text_dim, coord_buf.c_str());
    }
}

int DataMarkerManager::hit_test_3d(float         screen_x,
                                   float         screen_y,
                                   const Axes3D& axes3d,
                                   float         radius_px) const
{
    ImFont*     font     = ImGui::GetFont();
    const float fs_sm    = font->LegacySize * 0.72f;
    const float pad_x    = 6.0f;
    const float pad_y    = 3.0f;
    const float arrow_h  = 5.0f;
    const float ring_r   = 5.0f;
    const float gap      = 3.0f;
    const Rect& viewport = axes3d.viewport();

    for (size_t i = 0; i < markers_.size(); ++i)
    {
        const auto& m = markers_[i];
        if (!m.is_3d || m.axes3d != &axes3d)
            continue;

        const Projected3DPoint proj =
            project_axes3d_data_point(axes3d, {m.data_x, m.data_y, m.data_z});
        if (!proj.visible)
            continue;

        const float sx = proj.screen_x;
        const float sy = proj.screen_y;

        float dx = screen_x - sx;
        float dy = screen_y - sy;
        if (dx * dx + dy * dy <= radius_px * radius_px)
            return static_cast<int>(i);

        const std::string coord_buf =
            std::format("{:.4g}, {:.4g}, {:.4g}", m.data_x, m.data_y, m.data_z);
        bool   has_name = !m.series_label.empty();
        ImVec2 name_sz = has_name ? font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, m.series_label.c_str())
                                  : ImVec2(0, 0);
        ImVec2 coord_sz = font->CalcTextSizeA(fs_sm, 300.0f, 0.0f, coord_buf.c_str());
        float  text_w   = std::max(name_sz.x, coord_sz.x);
        float  text_h   = coord_sz.y + (has_name ? (name_sz.y + 2.0f) : 0.0f);
        float  box_w    = text_w + pad_x * 2.0f;
        float  box_h    = text_h + pad_y * 2.0f;

        bool  flip    = (sy - ring_r - gap - arrow_h - box_h) < viewport.y;
        float box_top = NAN;
        float box_bot = NAN;
        if (!flip)
        {
            float arrow_tip_y = sy - ring_r - gap;
            box_bot           = arrow_tip_y - arrow_h;
            box_top           = box_bot - box_h;
        }
        else
        {
            float arrow_tip_y = sy + ring_r + gap;
            box_top           = arrow_tip_y + arrow_h;
            box_bot           = box_top + box_h;
        }

        float box_left  = sx - box_w * 0.5f;
        float box_right = sx + box_w * 0.5f;

        constexpr float hit_margin = 2.0f;
        if (screen_x >= box_left - hit_margin && screen_x <= box_right + hit_margin
            && screen_y >= std::min(box_top, box_bot) - hit_margin
            && screen_y <= std::max(box_top, box_bot) + hit_margin)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
