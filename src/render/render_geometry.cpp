// render_geometry.cpp — Grid, border, tick marks, screen-space overlay, plot text.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series_shapes.hpp>

#include "ui/imgui/axes3d_renderer.hpp"
#include "ui/theme/theme.hpp"
#include "ui/viewmodel/axes_view_model.hpp"
#include "ui/viewmodel/figure_view_model.hpp"

namespace spectra
{

// Conditional sRGB-to-linear for themes with linearize_colors enabled.
static bool should_linearize_geom(const ui::ThemeManager& tm)
{
    return tm.current().linearize_colors;
}

static float srgb_chan_to_linear_geom(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static void set_pc_color_geom(float dst[4], const ui::Color& src, const ui::ThemeManager& tm)
{
    if (should_linearize_geom(tm))
    {
        dst[0] = srgb_chan_to_linear_geom(src.r);
        dst[1] = srgb_chan_to_linear_geom(src.g);
        dst[2] = srgb_chan_to_linear_geom(src.b);
    }
    else
    {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
    }
    dst[3] = src.a;
}

static void set_pc_color_geom(float                   dst[4],
                              const ui::Color&        src,
                              float                   alpha_override,
                              const ui::ThemeManager& tm)
{
    if (should_linearize_geom(tm))
    {
        dst[0] = srgb_chan_to_linear_geom(src.r);
        dst[1] = srgb_chan_to_linear_geom(src.g);
        dst[2] = srgb_chan_to_linear_geom(src.b);
    }
    else
    {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
    }
    dst[3] = alpha_override;
}

void Renderer::render_plot_text(Figure& figure)
{
    if (!text_renderer_.is_initialized())
        return;

    const auto& colors = theme_mgr_.colors();

    auto color_to_rgba = [this](const ui::Color& c) -> uint32_t
    {
        float cr = c.r;
        float cg = c.g;
        float cb = c.b;
        if (should_linearize_geom(theme_mgr_))
        {
            cr = srgb_chan_to_linear_geom(cr);
            cg = srgb_chan_to_linear_geom(cg);
            cb = srgb_chan_to_linear_geom(cb);
        }
        auto r = static_cast<uint8_t>(cr * 255);
        auto g = static_cast<uint8_t>(cg * 255);
        auto b = static_cast<uint8_t>(cb * 255);
        auto a = static_cast<uint8_t>(c.a * 255);
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
               | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    };

    uint32_t tick_col  = color_to_rgba(colors.tick_label);
    uint32_t label_col = color_to_rgba(colors.text_secondary);
    uint32_t title_col = color_to_rgba(colors.text_primary);

    constexpr float tick_padding = 5.0f;

    // uint32_t fig_w = figure.width();
    uint32_t fig_h = figure.height();

    // ── 2D Axes: tick labels, axis labels, title ──
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto&       axes = *axes_ptr;
        const auto& vp   = axes.viewport();

        // Skip axes entirely outside the framebuffer (scrolled off-screen)
        if (vp.y + vp.h < 0.0f || vp.y > static_cast<float>(fig_h))
            continue;

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        {
            auto* axes2d_ptr = dynamic_cast<Axes*>(&axes);
            if (figure_vm_ && axes2d_ptr)
            {
                auto& avm = figure_vm_->get_or_create_axes_vm(axes2d_ptr);
                xlim      = avm.visual_xlim();
                ylim      = avm.visual_ylim();
            }
            else
            {
                xlim = axes.x_limits();
                ylim = axes.y_limits();
            }
        }

        double x_range = xlim.max - xlim.min;
        double y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float
        { return static_cast<float>(vp.x + (dx - xlim.min) / x_range * vp.w); };
        auto data_to_px_y = [&](double dy) -> float
        { return static_cast<float>(vp.y + (1.0 - (dy - ylim.min) / y_range) * vp.h); };

        const auto& as = axes.axis_style();
        float       tl = as.tick_length;

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();

        // X tick labels — skip labels that would overlap
        {
            float           last_x_right = -1e30f;
            constexpr float label_gap    = 6.0f;
            for (size_t i = 0; i < x_ticks.positions.size(); ++i)
            {
                float px     = data_to_px_x(x_ticks.positions[i]);
                auto  ext    = text_renderer_.measure_text(x_ticks.labels[i], FontSize::Tick);
                float x_left = px - ext.width * 0.5f;
                if (x_left < last_x_right + label_gap && i > 0)
                    continue;
                text_renderer_.draw_text(x_ticks.labels[i],
                                         px,
                                         vp.y + vp.h + tl + tick_padding,
                                         FontSize::Tick,
                                         tick_col,
                                         TextAlign::Center,
                                         TextVAlign::Top);
                last_x_right = px + ext.width * 0.5f;
            }
        }

        // Y tick labels — skip labels that would overlap.
        // Ticks are in ascending data order = ascending screen-Y bottom-to-top.
        // Iterate in reverse (high data → low data = top-to-bottom on screen).
        // Track last_y_bottom: skip next label if its top is within gap of last bottom.
        float max_y_tick_width = 0.0f;
        {
            float           last_y_bottom = -1e30f;
            constexpr float label_gap     = 4.0f;
            for (int i = static_cast<int>(y_ticks.positions.size()) - 1; i >= 0; --i)
            {
                float py       = data_to_px_y(y_ticks.positions[i]);
                auto  ext      = text_renderer_.measure_text(y_ticks.labels[i], FontSize::Tick);
                float y_top    = py - ext.height * 0.5f;
                float y_bottom = py + ext.height * 0.5f;
                // Skip if this label's top is above the bottom of the last drawn label
                // (i.e. would overlap going downward).
                // Always draw the topmost tick (i == size-1, first in this loop).
                if (y_top < last_y_bottom + label_gap
                    && i < static_cast<int>(y_ticks.positions.size()) - 1)
                    continue;
                text_renderer_.draw_text(y_ticks.labels[i],
                                         vp.x - tl - tick_padding,
                                         py,
                                         FontSize::Tick,
                                         tick_col,
                                         TextAlign::Right,
                                         TextVAlign::Middle);
                last_y_bottom    = y_bottom;
                max_y_tick_width = std::max(max_y_tick_width, ext.width);
            }
        }

        // X axis label
        if (!axes.get_xlabel().empty())
        {
            float cx = vp.x + vp.w * 0.5f;
            float py = vp.y + vp.h + tick_padding + 16.0f + tick_padding;
            text_renderer_.draw_text(axes.get_xlabel(),
                                     cx,
                                     py,
                                     FontSize::Label,
                                     label_col,
                                     TextAlign::Center);
        }

        // Y axis label (rotated -90°)
        if (!axes.get_ylabel().empty())
        {
            const auto label_ext = text_renderer_.measure_text(axes.get_ylabel(), FontSize::Label);
            constexpr float label_gap = 6.0f;   // gap between widest tick label and axis label
            float           center_x =
                vp.x - tl - tick_padding - max_y_tick_width - label_gap - label_ext.height * 0.5f;
            float           center_y   = vp.y + vp.h * 0.5f;
            constexpr float neg_90_deg = -1.5707963f;   // -π/2
            text_renderer_.draw_text_rotated(axes.get_ylabel(),
                                             center_x,
                                             center_y,
                                             neg_90_deg,
                                             FontSize::Label,
                                             label_col);
        }

        // Title
        if (!axes.get_title().empty())
        {
            auto  ext = text_renderer_.measure_text(axes.get_title(), FontSize::Title);
            float cx  = vp.x + vp.w * 0.5f;
            float py  = vp.y - ext.height - tick_padding;
            // Only clamp if the title would go off the top of the figure.
            // The previous clamp (py < vp.y + 2) always fired and placed the
            // title inside the axes viewport where it competed with grid/series
            // rendering, contributing to flicker in split-view panes.
            if (py < 2.0f)
                py = 2.0f;
            text_renderer_
                .draw_text(axes.get_title(), cx, py, FontSize::Title, title_col, TextAlign::Center);
        }

        // ── ShapeSeries text annotations (data-space text labels) ──
        for (const auto& sp : axes.series())
        {
            auto* shape = dynamic_cast<const ShapeSeries*>(sp.get());
            if (!shape || shape->text_annotations().empty())
                continue;

            const auto& sc = shape->color();
            for (const auto& ann : shape->text_annotations())
            {
                float px = data_to_px_x(ann.x);
                float py = data_to_px_y(ann.y);

                // Determine text color: use annotation color if set, else series color
                Color    tc        = (ann.text_color.a > 0.01f) ? ann.text_color : sc;
                auto     tr        = static_cast<uint8_t>(tc.r * 255);
                auto     tg        = static_cast<uint8_t>(tc.g * 255);
                auto     tb        = static_cast<uint8_t>(tc.b * 255);
                auto     ta        = static_cast<uint8_t>(tc.a * shape->opacity() * 255);
                uint32_t text_rgba = static_cast<uint32_t>(tr) | (static_cast<uint32_t>(tg) << 8)
                                     | (static_cast<uint32_t>(tb) << 16)
                                     | (static_cast<uint32_t>(ta) << 24);

                auto fs        = FontSize::Label;   // default
                auto ta_align  = static_cast<TextAlign>(std::clamp(ann.align, 0, 2));
                auto ta_valign = static_cast<TextVAlign>(std::clamp(ann.valign, 0, 2));

                text_renderer_.draw_text(ann.content, px, py, fs, text_rgba, ta_align, ta_valign);
            }
        }
    }

    // ── 3D Axes: billboarded tick labels, axis labels, title ──
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const auto& vp  = axes3d->viewport();
        const auto& cam = axes3d->camera();

        // Build MVP matrix: projection * view * model
        float aspect = vp.w / std::max(vp.h, 1.0f);
        mat4  proj   = cam.projection_matrix(aspect);
        mat4  view   = cam.view_matrix();
        mat4  model  = axes3d->data_to_normalized_matrix();
        mat4  mvp    = mat4_mul(proj, mat4_mul(view, model));

        // Helper: project a 3D world point to screen coords within the viewport
        // Also outputs NDC depth in [0,1] for depth-tested text rendering.
        auto world_to_screen = [&](vec3 world_pos, float& sx, float& sy, float& ndc_depth) -> bool
        {
            float clip_x = mvp.m[0] * world_pos.x + mvp.m[4] * world_pos.y + mvp.m[8] * world_pos.z
                           + mvp.m[12];
            float clip_y = mvp.m[1] * world_pos.x + mvp.m[5] * world_pos.y + mvp.m[9] * world_pos.z
                           + mvp.m[13];
            float clip_z = mvp.m[2] * world_pos.x + mvp.m[6] * world_pos.y + mvp.m[10] * world_pos.z
                           + mvp.m[14];
            float clip_w = mvp.m[3] * world_pos.x + mvp.m[7] * world_pos.y + mvp.m[11] * world_pos.z
                           + mvp.m[15];

            if (clip_w <= 0.001f)
                return false;

            float ndc_x = clip_x / clip_w;
            float ndc_y = clip_y / clip_w;
            ndc_depth   = clip_z / clip_w;   // Vulkan depth range [0,1]

            sx = vp.x + (ndc_x + 1.0f) * 0.5f * vp.w;
            sy = vp.y + (ndc_y + 1.0f) * 0.5f * vp.h;

            float margin = 200.0f;
            return sx >= vp.x - margin && sx <= vp.x + vp.w + margin && sy >= vp.y - margin
                   && sy <= vp.y + vp.h + margin;
        };

        auto xlim = axes3d->x_limits();
        auto ylim = axes3d->y_limits();
        auto zlim = axes3d->z_limits();

        auto x0 = static_cast<float>(xlim.min);
        auto y0 = static_cast<float>(ylim.min);
        auto z0 = static_cast<float>(zlim.min);

        vec3 view_dir       = vec3_normalize(cam.target - cam.position);
        bool looking_down_y = std::abs(view_dir.y) > 0.98f;
        bool looking_down_z = std::abs(view_dir.z) > 0.98f;

        auto            x_tick_offset = static_cast<float>((xlim.max - xlim.min) * 0.04);
        auto            y_tick_offset = static_cast<float>((xlim.max - xlim.min) * 0.04);
        auto            z_tick_offset = static_cast<float>((xlim.max - xlim.min) * 0.04);
        constexpr float tick_label_min_spacing_px = 18.0f;
        auto            should_skip_overlapping_tick =
            [&](float sx, float sy, float& last_sx, float& last_sy, bool& has_last) -> bool
        {
            if (!has_last)
            {
                last_sx  = sx;
                last_sy  = sy;
                has_last = true;
                return false;
            }
            float dx = sx - last_sx;
            float dy = sy - last_sy;
            if ((dx * dx + dy * dy) < (tick_label_min_spacing_px * tick_label_min_spacing_px))
                return true;
            last_sx = sx;
            last_sy = sy;
            return false;
        };

        // --- X-axis tick labels ---
        {
            auto  x_ticks  = axes3d->compute_x_ticks();
            float last_sx  = 0.0f;
            float last_sy  = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < x_ticks.positions.size(); ++i)
            {
                float sx    = 0.0f;
                float sy    = 0.0f;
                float depth = 0.0f;
                vec3  pos   = {static_cast<float>(x_ticks.positions[i]), y0 - x_tick_offset, z0};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(x_ticks.labels[i],
                                               sx,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Center,
                                               TextVAlign::Top);
            }
        }

        // --- Y-axis tick labels ---
        if (!looking_down_y)
        {
            auto  y_ticks  = axes3d->compute_y_ticks();
            float last_sx  = 0.0f;
            float last_sy  = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < y_ticks.positions.size(); ++i)
            {
                float sx    = 0.0f;
                float sy    = 0.0f;
                float depth = 0.0f;
                vec3  pos   = {x0 - y_tick_offset, static_cast<float>(y_ticks.positions[i]), z0};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(y_ticks.labels[i],
                                               sx,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Right,
                                               TextVAlign::Middle);
            }
        }

        // --- Z-axis tick labels ---
        if (!looking_down_z)
        {
            auto  z_ticks  = axes3d->compute_z_ticks();
            float last_sx  = 0.0f;
            float last_sy  = 0.0f;
            bool  has_last = false;
            for (size_t i = 0; i < z_ticks.positions.size(); ++i)
            {
                float sx    = 0.0f;
                float sy    = 0.0f;
                float depth = 0.0f;
                vec3  pos   = {x0 - z_tick_offset, y0, static_cast<float>(z_ticks.positions[i])};
                if (!world_to_screen(pos, sx, sy, depth))
                    continue;
                if (should_skip_overlapping_tick(sx, sy, last_sx, last_sy, has_last))
                    continue;
                text_renderer_.draw_text_depth(z_ticks.labels[i],
                                               sx - tick_padding,
                                               sy,
                                               depth,
                                               FontSize::Tick,
                                               tick_col,
                                               TextAlign::Right,
                                               TextVAlign::Middle);
            }
        }

        // --- 3D axis arrow labels ---
        {
            auto x1          = static_cast<float>(xlim.max);
            auto y1          = static_cast<float>(ylim.max);
            auto z1          = static_cast<float>(zlim.max);
            auto arrow_len_x = static_cast<float>((xlim.max - xlim.min) * 0.18);
            auto arrow_len_y = static_cast<float>((ylim.max - ylim.min) * 0.18);
            auto arrow_len_z = static_cast<float>((zlim.max - zlim.min) * 0.18);

            auto pack_rgba = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t
            {
                return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
                       | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
            };
            uint32_t vk_x_arrow_col = pack_rgba(230, 70, 70, 220);
            uint32_t vk_y_arrow_col = pack_rgba(70, 200, 70, 220);
            uint32_t vk_z_arrow_col = pack_rgba(80, 130, 255, 220);

            // Helper: draw arrow label text at the tip of an axis arrow
            auto draw_arrow_label = [&](vec3               start,
                                        vec3               end,
                                        uint32_t           vk_col,
                                        const char*        default_lbl,
                                        const std::string& user_lbl)
            {
                float sx0 = 0.0f;
                float sy0 = 0.0f;
                float d0  = 0.0f;
                float sx1 = 0.0f;
                float sy1 = 0.0f;
                float d1  = 0.0f;
                if (!world_to_screen(start, sx0, sy0, d0) || !world_to_screen(end, sx1, sy1, d1))
                    return;
                const char* lbl          = user_lbl.empty() ? default_lbl : user_lbl.c_str();
                float       label_offset = 8.0f;
                float       dir_x        = sx1 - sx0;
                float       dir_y        = sy1 - sy0;
                float       dir_len      = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                float lx = sx1 + (dir_len > 1.0f ? dir_x / dir_len * label_offset : label_offset);
                float ly_center = sy1 + (dir_len > 1.0f ? dir_y / dir_len * label_offset : 0.0f);
                text_renderer_.draw_text_depth(lbl,
                                               lx,
                                               ly_center,
                                               d1,
                                               FontSize::Label,
                                               vk_col,
                                               TextAlign::Left,
                                               TextVAlign::Middle);
            };

            draw_arrow_label({x1, y0, z0},
                             {x1 + arrow_len_x, y0, z0},
                             vk_x_arrow_col,
                             "X",
                             axes3d->get_xlabel());
            draw_arrow_label({x0, y1, z0},
                             {x0, y1 + arrow_len_y, z0},
                             vk_y_arrow_col,
                             "Y",
                             axes3d->get_ylabel());
            draw_arrow_label({x0, y0, z1},
                             {x0, y0, z1 + arrow_len_z},
                             vk_z_arrow_col,
                             "Z",
                             axes3d->get_zlabel());
        }

        // --- 3D Title ---
        if (!axes3d->get_title().empty())
        {
            float cx  = vp.x + vp.w * 0.5f;
            auto  ext = text_renderer_.measure_text(axes3d->get_title(), FontSize::Title);
            float py  = vp.y - ext.height - tick_padding;
            if (py < vp.y + 2.0f)
                py = vp.y + 2.0f;
            text_renderer_.draw_text(axes3d->get_title(),
                                     cx,
                                     py,
                                     FontSize::Title,
                                     title_col,
                                     TextAlign::Center);
        }
    }
}

void Renderer::render_plot_geometry(Figure& figure)
{
    uint32_t fig_w = figure.width();
    uint32_t fig_h = figure.height();
    auto     fw    = static_cast<float>(fig_w);
    auto     fh    = static_cast<float>(fig_h);

    // Select the buffer slot for this frame so that the previous in-flight
    // frame's GPU commands still read from the other slot while we write this one.
    const uint32_t slot = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;

    const auto& colors = theme_mgr_.colors();

    overlay_line_scratch_.clear();

    // ── 2D Axes: border and tick mark lines ──
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto&       axes = *axes_ptr;
        const auto& vp   = axes.viewport();

        // Skip axes entirely outside the framebuffer (scrolled off-screen)
        if (vp.y + vp.h < 0.0f || vp.y > fh)
            continue;

        // ── Border: 4 lines at viewport edges (screen-space) ──
        // Rendered in screen space to avoid a GPU buffer race condition:
        // the previous data-space border vertex buffer was shared across
        // in-flight frames, causing misalignment during fast zoom.
        if (axes.border_enabled())
        {
            float bx0 = vp.x;
            float by0 = vp.y;
            float bx1 = vp.x + vp.w;
            float by1 = vp.y + vp.h;
            // Bottom edge
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by1);
            overlay_line_scratch_.push_back(bx1);
            overlay_line_scratch_.push_back(by1);
            // Top edge
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by0);
            overlay_line_scratch_.push_back(bx1);
            overlay_line_scratch_.push_back(by0);
            // Left edge
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by0);
            overlay_line_scratch_.push_back(bx0);
            overlay_line_scratch_.push_back(by1);
            // Right edge
            overlay_line_scratch_.push_back(bx1);
            overlay_line_scratch_.push_back(by0);
            overlay_line_scratch_.push_back(bx1);
            overlay_line_scratch_.push_back(by1);
        }

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        {
            auto* axes2d_ptr = dynamic_cast<Axes*>(&axes);
            if (figure_vm_ && axes2d_ptr)
            {
                auto& avm = figure_vm_->get_or_create_axes_vm(axes2d_ptr);
                xlim      = avm.visual_xlim();
                ylim      = avm.visual_ylim();
            }
            else
            {
                xlim = axes.x_limits();
                ylim = axes.y_limits();
            }
        }

        double x_range = xlim.max - xlim.min;
        double y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float
        { return static_cast<float>(vp.x + (dx - xlim.min) / x_range * vp.w); };
        auto data_to_px_y = [&](double dy) -> float
        { return static_cast<float>(vp.y + (1.0 - (dy - ylim.min) / y_range) * vp.h); };

        const auto& as = axes.axis_style();
        float       tl = as.tick_length;
        if (tl <= 0.0f)
            continue;

        auto x_ticks = axes.compute_x_ticks();
        auto y_ticks = axes.compute_y_ticks();

        // X tick marks (at bottom edge of viewport)
        for (double position : x_ticks.positions)
        {
            float px = data_to_px_x(position);
            overlay_line_scratch_.push_back(px);
            overlay_line_scratch_.push_back(vp.y + vp.h);
            overlay_line_scratch_.push_back(px);
            overlay_line_scratch_.push_back(vp.y + vp.h + tl);
        }

        // Y tick marks (at left edge of viewport)
        for (double position : y_ticks.positions)
        {
            float py = data_to_px_y(position);
            overlay_line_scratch_.push_back(vp.x);
            overlay_line_scratch_.push_back(py);
            overlay_line_scratch_.push_back(vp.x - tl);
            overlay_line_scratch_.push_back(py);
        }
    }

    // NOTE: 3D axis arrows are now rendered by render_arrows() inside render_axes()
    // with depth testing, so they are properly occluded by 3D geometry.

    // ── Set up screen-space ortho projection in UBO ──
    // Screen coordinates are Y-down (0=top, h=bottom).
    // Vulkan clip space has Y-down (positive m[5]), WebGPU has Y-up (negative m[5]).
    FrameUBO ubo{};   // Value-initializes all members to zero
    float    y_sign    = backend_.clip_y_down() ? 1.0f : -1.0f;
    ubo.projection[0]  = 2.0f / fw;            // X: [0, fw] → [-1, +1]
    ubo.projection[5]  = y_sign * 2.0f / fh;   // Y: flip for WebGPU
    ubo.projection[10] = -1.0f;
    ubo.projection[12] = -1.0f;
    ubo.projection[13] = y_sign * -1.0f;
    ubo.projection[15] = 1.0f;
    // Identity view + model
    ubo.view[0]         = 1.0f;
    ubo.view[5]         = 1.0f;
    ubo.view[10]        = 1.0f;
    ubo.view[15]        = 1.0f;
    ubo.model[0]        = 1.0f;
    ubo.model[5]        = 1.0f;
    ubo.model[10]       = 1.0f;
    ubo.model[15]       = 1.0f;
    ubo.viewport_width  = fw;
    ubo.viewport_height = fh;

    backend_.set_viewport(0, 0, fw, fh);
    backend_.set_scissor(0, 0, fig_w, fig_h);
    backend_.upload_buffer(frame_ubo_buffer_, &ubo, sizeof(FrameUBO));
    backend_.bind_buffer(frame_ubo_buffer_, 0);

    // ── Draw 2D grid lines per-axes (screen-space, scissored to viewport) ──
    // Rendered in screen-space to avoid the same GPU buffer race that
    // affected the border (shared host-visible buffers overwritten while
    // previous flight frames still reference them during fast zoom).
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes2d = dynamic_cast<Axes*>(axes_ptr.get());
        if (!axes2d || !axes2d->grid_enabled())
            continue;

        const auto& vp = axes2d->viewport();

        // Phase 2 (LT-5): read limits via AxesViewModel when available
        AxisLimits xlim;
        AxisLimits ylim;
        if (figure_vm_)
        {
            auto& avm = figure_vm_->get_or_create_axes_vm(axes2d);
            xlim      = avm.visual_xlim();
            ylim      = avm.visual_ylim();
        }
        else
        {
            xlim = axes2d->x_limits();
            ylim = axes2d->y_limits();
        }

        double x_range = xlim.max - xlim.min;
        double y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        auto data_to_px_x = [&](double dx) -> float
        { return static_cast<float>(vp.x + (dx - xlim.min) / x_range * vp.w); };
        auto data_to_px_y = [&](double dy) -> float
        { return static_cast<float>(vp.y + (1.0 - (dy - ylim.min) / y_range) * vp.h); };

        auto x_ticks = axes2d->compute_x_ticks();
        auto y_ticks = axes2d->compute_y_ticks();

        size_t num_x = x_ticks.positions.size();
        size_t num_y = y_ticks.positions.size();
        if (num_x == 0 && num_y == 0)
            continue;

        grid_scratch_.clear();
        float fy_top    = vp.y;
        float fy_bottom = vp.y + vp.h;
        float fx_left   = vp.x;
        float fx_right  = vp.x + vp.w;
        for (size_t i = 0; i < num_x; ++i)
        {
            float px = data_to_px_x(x_ticks.positions[i]);
            grid_scratch_.push_back(px);
            grid_scratch_.push_back(fy_top);
            grid_scratch_.push_back(px);
            grid_scratch_.push_back(fy_bottom);
        }
        for (size_t i = 0; i < num_y; ++i)
        {
            float py = data_to_px_y(y_ticks.positions[i]);
            grid_scratch_.push_back(fx_left);
            grid_scratch_.push_back(py);
            grid_scratch_.push_back(fx_right);
            grid_scratch_.push_back(py);
        }

        auto grid_vert_count = static_cast<uint32_t>(grid_scratch_.size() / 2);
        if (grid_vert_count == 0)
            continue;

        size_t grid_bytes = grid_scratch_.size() * sizeof(float);
        auto&  gpu        = axes_gpu_data_[axes2d];
        if (!gpu.grid_buffer[slot] || gpu.grid_capacity[slot] < grid_bytes)
        {
            if (gpu.grid_buffer[slot])
                backend_.destroy_buffer(gpu.grid_buffer[slot]);
            gpu.grid_buffer[slot]   = backend_.create_buffer(BufferUsage::Vertex, grid_bytes * 2);
            gpu.grid_capacity[slot] = grid_bytes * 2;
        }
        backend_.upload_buffer(gpu.grid_buffer[slot], grid_scratch_.data(), grid_bytes);

        // Scissor to axes viewport so grid lines don't bleed outside
        backend_.set_scissor(static_cast<int32_t>(vp.x),
                             static_cast<int32_t>(vp.y),
                             static_cast<uint32_t>(vp.w),
                             static_cast<uint32_t>(vp.h));

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants grid_pc{};
        const auto&         as = axes2d->axis_style();
        if (as.grid_color.a > 0.0f)
        {
            grid_pc.color[0] = as.grid_color.r;
            grid_pc.color[1] = as.grid_color.g;
            grid_pc.color[2] = as.grid_color.b;
            grid_pc.color[3] = as.grid_color.a;
        }
        else
        {
            grid_pc.color[0] = colors.grid_major.r;
            grid_pc.color[1] = colors.grid_major.g;
            grid_pc.color[2] = colors.grid_major.b;
            grid_pc.color[3] = colors.grid_major.a;
        }
        grid_pc.line_width = as.grid_width;
        backend_.push_constants(grid_pc);

        backend_.set_line_width(std::max(1.0f, as.grid_width));
        backend_.bind_buffer(gpu.grid_buffer[slot], 0);
        backend_.draw(grid_vert_count);
        backend_.set_line_width(1.0f);

        // ── Minor grid lines (subdivisions between major ticks) ──
        // Generate 4 subdivision lines between each pair of adjacent major ticks,
        // and also extend into the margin regions before the first tick and after
        // the last tick.  Without edge extension, auto-fitted axes (which add 5%
        // padding beyond the data range) show minor grid only inside the rectangle
        // bounded by the outermost ticks, creating a visible "closed square" artifact.
        constexpr int MINOR_SUBDIVISIONS = 5;   // 5 intervals → 4 interior lines
        minor_grid_scratch_.clear();
        // X-axis minor grid (between adjacent tick pairs)
        for (size_t i = 0; i + 1 < num_x; ++i)
        {
            double step = (x_ticks.positions[i + 1] - x_ticks.positions[i]) / MINOR_SUBDIVISIONS;
            for (int s = 1; s < MINOR_SUBDIVISIONS; ++s)
            {
                float px = data_to_px_x(x_ticks.positions[i] + step * s);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_top);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_bottom);
            }
        }
        // X-axis minor grid — extend into left margin (before first tick)
        if (num_x >= 2)
        {
            double step = (x_ticks.positions[1] - x_ticks.positions[0]) / MINOR_SUBDIVISIONS;
            for (int k = 1;; ++k)
            {
                double x = x_ticks.positions[0] - step * k;
                if (x < xlim.min)
                    break;
                float px = data_to_px_x(x);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_top);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_bottom);
            }
        }
        // X-axis minor grid — extend into right margin (after last tick)
        if (num_x >= 2)
        {
            double step =
                (x_ticks.positions[num_x - 1] - x_ticks.positions[num_x - 2]) / MINOR_SUBDIVISIONS;
            for (int k = 1;; ++k)
            {
                double x = x_ticks.positions[num_x - 1] + step * k;
                if (x > xlim.max)
                    break;
                float px = data_to_px_x(x);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_top);
                minor_grid_scratch_.push_back(px);
                minor_grid_scratch_.push_back(fy_bottom);
            }
        }
        // Y-axis minor grid (between adjacent tick pairs)
        for (size_t i = 0; i + 1 < num_y; ++i)
        {
            double step = (y_ticks.positions[i + 1] - y_ticks.positions[i]) / MINOR_SUBDIVISIONS;
            for (int s = 1; s < MINOR_SUBDIVISIONS; ++s)
            {
                float py = data_to_px_y(y_ticks.positions[i] + step * s);
                minor_grid_scratch_.push_back(fx_left);
                minor_grid_scratch_.push_back(py);
                minor_grid_scratch_.push_back(fx_right);
                minor_grid_scratch_.push_back(py);
            }
        }
        // Y-axis minor grid — extend into bottom margin (below first tick in data space,
        // which is the top tick position in screen space since Y is flipped)
        if (num_y >= 2)
        {
            double step = (y_ticks.positions[1] - y_ticks.positions[0]) / MINOR_SUBDIVISIONS;
            for (int k = 1;; ++k)
            {
                double y = y_ticks.positions[0] - step * k;
                if (y < ylim.min)
                    break;
                float py = data_to_px_y(y);
                minor_grid_scratch_.push_back(fx_left);
                minor_grid_scratch_.push_back(py);
                minor_grid_scratch_.push_back(fx_right);
                minor_grid_scratch_.push_back(py);
            }
        }
        // Y-axis minor grid — extend into top margin (above last tick in data space)
        if (num_y >= 2)
        {
            double step =
                (y_ticks.positions[num_y - 1] - y_ticks.positions[num_y - 2]) / MINOR_SUBDIVISIONS;
            for (int k = 1;; ++k)
            {
                double y = y_ticks.positions[num_y - 1] + step * k;
                if (y > ylim.max)
                    break;
                float py = data_to_px_y(y);
                minor_grid_scratch_.push_back(fx_left);
                minor_grid_scratch_.push_back(py);
                minor_grid_scratch_.push_back(fx_right);
                minor_grid_scratch_.push_back(py);
            }
        }

        auto minor_vert_count = static_cast<uint32_t>(minor_grid_scratch_.size() / 2);
        if (minor_vert_count > 0)
        {
            size_t minor_bytes = minor_grid_scratch_.size() * sizeof(float);
            if (!gpu.minor_grid_buffer[slot] || gpu.minor_grid_capacity[slot] < minor_bytes)
            {
                if (gpu.minor_grid_buffer[slot])
                    backend_.destroy_buffer(gpu.minor_grid_buffer[slot]);
                gpu.minor_grid_buffer[slot] =
                    backend_.create_buffer(BufferUsage::Vertex, minor_bytes * 2);
                gpu.minor_grid_capacity[slot] = minor_bytes * 2;
            }
            backend_.upload_buffer(gpu.minor_grid_buffer[slot],
                                   minor_grid_scratch_.data(),
                                   minor_bytes);

            SeriesPushConstants minor_pc{};
            minor_pc.color[0]   = colors.grid_minor.r;
            minor_pc.color[1]   = colors.grid_minor.g;
            minor_pc.color[2]   = colors.grid_minor.b;
            minor_pc.color[3]   = colors.grid_minor.a;
            minor_pc.line_width = std::max(1.0f, as.grid_width * 0.5f);
            backend_.push_constants(minor_pc);

            backend_.set_line_width(minor_pc.line_width);
            backend_.bind_buffer(gpu.minor_grid_buffer[slot], 0);
            backend_.draw(minor_vert_count);
            backend_.set_line_width(1.0f);
        }
    }

    // Restore full-figure scissor for border + tick overlay
    backend_.set_scissor(0, 0, fig_w, fig_h);

    // ── Draw 2D border + tick mark lines ──
    // Use a per-figure buffer so that split-view figures each have independent
    // GPU storage.  A single shared buffer is unsafe: all figures render inside
    // ONE render pass / command buffer, so figure N's upload overwrites the
    // data that figure N-1's draw command already recorded a reference to.
    auto line_vert_count = static_cast<uint32_t>(overlay_line_scratch_.size() / 2);
    if (line_vert_count > 0)
    {
        size_t         line_bytes = overlay_line_scratch_.size() * sizeof(float);
        FigureGpuData& fig_gpu    = figure_gpu_data_[&figure];
        if (!fig_gpu.overlay_line_buffer[slot] || fig_gpu.overlay_line_capacity[slot] < line_bytes)
        {
            if (fig_gpu.overlay_line_buffer[slot])
                backend_.destroy_buffer(fig_gpu.overlay_line_buffer[slot]);
            fig_gpu.overlay_line_buffer[slot] =
                backend_.create_buffer(BufferUsage::Vertex, line_bytes * 2);
            fig_gpu.overlay_line_capacity[slot] = line_bytes * 2;
        }
        backend_.upload_buffer(fig_gpu.overlay_line_buffer[slot],
                               overlay_line_scratch_.data(),
                               line_bytes);

        backend_.bind_pipeline(grid_pipeline_);

        SeriesPushConstants pc{};
        set_pc_color_geom(pc.color, colors.axis_line, theme_mgr_);
        pc.line_width = 1.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(fig_gpu.overlay_line_buffer[slot], 0);
        backend_.draw(line_vert_count);
    }
}

void Renderer::render_grid(AxesBase& axes, const Rect& /*viewport*/)
{
    // Check if this is a 3D axes
    if (auto* axes3d = dynamic_cast<Axes3D*>(&axes))
    {
        if (!axes3d->grid_enabled())
            return;

        const uint32_t slot = backend_.current_flight_frame() % FRAME_BUFFER_SLOTS;

        auto  xlim = axes3d->x_limits();
        auto  ylim = axes3d->y_limits();
        auto  zlim = axes3d->z_limits();
        auto  gp   = axes3d->grid_planes();
        auto& gpu  = axes_gpu_data_[&axes];

        // Check if limits/planes changed — skip regeneration if cached
        auto& gc             = gpu.grid_cache[slot];
        bool  limits_changed = !gc.valid || gc.xmin != xlim.min || gc.xmax != xlim.max
                              || gc.ymin != ylim.min || gc.ymax != ylim.max || gc.zmin != zlim.min
                              || gc.zmax != zlim.max
                              || gpu.cached_grid_planes[slot] != static_cast<int>(gp);

        if (limits_changed)
        {
            // Generate 3D grid vertices at tick positions (matches tick labels)
            Axes3DRenderer::GridPlaneData grid_gen;
            vec3                          min_corner = {static_cast<float>(xlim.min),
                                                        static_cast<float>(ylim.min),
                                                        static_cast<float>(zlim.min)};
            vec3                          max_corner = {static_cast<float>(xlim.max),
                                                        static_cast<float>(ylim.max),
                                                        static_cast<float>(zlim.max)};

            auto x_ticks_d = axes3d->compute_x_ticks().positions;
            auto y_ticks_d = axes3d->compute_y_ticks().positions;
            auto z_ticks_d = axes3d->compute_z_ticks().positions;
            // 3D grid generators expect float vectors — convert from double
            auto to_float_vec = [](const std::vector<double>& src)
            {
                std::vector<float> dst(src.size());
                for (size_t i = 0; i < src.size(); ++i)
                    dst[i] = static_cast<float>(src[i]);
                return dst;
            };
            std::vector<float> x_ticks = to_float_vec(x_ticks_d);
            std::vector<float> y_ticks = to_float_vec(y_ticks_d);
            std::vector<float> z_ticks = to_float_vec(z_ticks_d);

            if (static_cast<int>(gp & Axes3D::GridPlane::XY))
                grid_gen.generate_xy_plane(min_corner,
                                           max_corner,
                                           static_cast<float>(zlim.min),
                                           x_ticks,
                                           y_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::XZ))
                grid_gen.generate_xz_plane(min_corner,
                                           max_corner,
                                           static_cast<float>(ylim.min),
                                           x_ticks,
                                           z_ticks);
            if (static_cast<int>(gp & Axes3D::GridPlane::YZ))
                grid_gen.generate_yz_plane(min_corner,
                                           max_corner,
                                           static_cast<float>(xlim.min),
                                           y_ticks,
                                           z_ticks);

            if (grid_gen.vertices.empty())
                return;

            size_t float_count = grid_gen.vertices.size() * 3;
            if (grid_scratch_.size() < float_count)
                grid_scratch_.resize(float_count);
            for (size_t i = 0; i < grid_gen.vertices.size(); ++i)
            {
                grid_scratch_[i * 3]     = grid_gen.vertices[i].x;
                grid_scratch_[i * 3 + 1] = grid_gen.vertices[i].y;
                grid_scratch_[i * 3 + 2] = grid_gen.vertices[i].z;
            }

            size_t byte_size = float_count * sizeof(float);
            if (!gpu.grid_buffer[slot] || gpu.grid_capacity[slot] < byte_size)
            {
                if (gpu.grid_buffer[slot])
                    backend_.destroy_buffer(gpu.grid_buffer[slot]);
                gpu.grid_buffer[slot] = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
                gpu.grid_capacity[slot] = byte_size * 2;
            }
            backend_.upload_buffer(gpu.grid_buffer[slot], grid_scratch_.data(), byte_size);
            gpu.grid_vertex_count[slot]  = static_cast<uint32_t>(float_count / 3);
            gc.xmin                      = xlim.min;
            gc.xmax                      = xlim.max;
            gc.ymin                      = ylim.min;
            gc.ymax                      = ylim.max;
            gc.zmin                      = zlim.min;
            gc.zmax                      = zlim.max;
            gpu.cached_grid_planes[slot] = static_cast<int>(gp);
            gc.valid                     = true;
        }

        if (!gpu.grid_buffer[slot] || gpu.grid_vertex_count[slot] == 0)
            return;

        // Draw 3D grid as overlay (no depth test so it's always visible)
        backend_.bind_pipeline(grid_overlay3d_pipeline_);

        SeriesPushConstants pc{};
        const auto&         theme_colors = theme_mgr_.colors();
        float               blend        = 0.3f;
        ui::Color           blended(theme_colors.grid_major.r * (1.0f - blend) + blend,
                          theme_colors.grid_major.g * (1.0f - blend) + blend,
                          theme_colors.grid_major.b * (1.0f - blend) + blend);
        set_pc_color_geom(pc.color, blended, 0.35f, theme_mgr_);
        pc.line_width    = 1.0f;
        pc.data_offset_x = 0.0f;
        pc.data_offset_y = 0.0f;
        backend_.push_constants(pc);

        backend_.bind_buffer(gpu.grid_buffer[slot], 0);
        backend_.draw(gpu.grid_vertex_count[slot]);
    }
    // 2D grid is now rendered in screen-space by render_plot_geometry()
    // to avoid the same GPU buffer race that affected the border.
}

void Renderer::render_axis_border(AxesBase& axes,
                                  const Rect& /*viewport*/,
                                  uint32_t /*fig_width*/,
                                  uint32_t /*fig_height*/)
{
    // Draw border in data space using the already-bound data-space UBO.
    // Inset vertices by a tiny fraction of the axis range so they don't
    // land exactly on the NDC ±1.0 clip boundary (which causes clipping
    // of the top/right edges on some GPUs).
    // Vertices are generated relative to the view center for camera-relative
    // rendering (matching the centered projection).
    auto* axes2d = dynamic_cast<Axes*>(&axes);
    if (!axes2d)
        return;   // Border only for 2D axes

    // Phase 2 (LT-5): read limits via AxesViewModel when available
    AxisLimits xlim;
    AxisLimits ylim;
    if (figure_vm_)
    {
        auto& avm = figure_vm_->get_or_create_axes_vm(axes2d);
        xlim      = avm.visual_xlim();
        ylim      = avm.visual_ylim();
    }
    else
    {
        xlim = axes2d->x_limits();
        ylim = axes2d->y_limits();
    }

    // Retrieve view center for camera-relative coordinates
    auto&  agpu = axes_gpu_data_[&axes];
    double vcx  = agpu.view_center_x;
    double vcy  = agpu.view_center_y;

    double x_range_d = xlim.max - xlim.min;
    double y_range_d = ylim.max - ylim.min;
    if (x_range_d == 0.0)
        x_range_d = 1.0;
    if (y_range_d == 0.0)
        y_range_d = 1.0;

    // Use epsilon to prevent NDC boundary clipping
    double       eps_x   = 0.002 * x_range_d;
    double       eps_y   = 0.002 * y_range_d;
    const double MIN_EPS = 1e-15;
    if (eps_x < MIN_EPS)
        eps_x = MIN_EPS;
    if (eps_y < MIN_EPS)
        eps_y = MIN_EPS;

    // View-relative border coordinates
    auto x0 = static_cast<float>((xlim.min + eps_x) - vcx);
    auto y0 = static_cast<float>((ylim.min + eps_y) - vcy);
    auto x1 = static_cast<float>((xlim.max - eps_x) - vcx);
    auto y1 = static_cast<float>((ylim.max - eps_y) - vcy);

    float border_verts[] = {
        // Bottom edge
        x0,
        y0,
        x1,
        y0,
        // Top edge
        x0,
        y1,
        x1,
        y1,
        // Left edge
        x0,
        y0,
        x0,
        y1,
        // Right edge
        x1,
        y0,
        x1,
        y1,
    };

    size_t byte_size = sizeof(border_verts);

    // Use per-axes border buffer so multi-subplot draws don't overwrite
    // each other within the same command buffer.
    auto& gpu = axes_gpu_data_[&axes];
    if (!gpu.border_buffer || gpu.border_capacity < byte_size)
    {
        if (gpu.border_buffer)
        {
            backend_.destroy_buffer(gpu.border_buffer);
        }
        gpu.border_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size);
        gpu.border_capacity = byte_size;
    }
    backend_.upload_buffer(gpu.border_buffer, border_verts, byte_size);

    backend_.bind_pipeline(grid_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = theme_mgr_.colors();
    set_pc_color_geom(pc.color, theme_colors.axis_line, theme_mgr_);
    pc.line_width    = 1.0f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.border_buffer, 0);
    backend_.draw(8);   // 4 lines × 2 vertices
}

}   // namespace spectra
