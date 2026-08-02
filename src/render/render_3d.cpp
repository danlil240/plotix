// render_3d.cpp — 3D series rendering: bounding box, tick marks, axis arrows.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/logger.hpp>

#include "ui/imgui/axes3d_renderer.hpp"
#include "ui/theme/theme.hpp"

namespace spectra
{

// Conditional sRGB-to-linear for themes with linearize_colors enabled.
static bool should_linearize_3d(const ui::ThemeManager& tm)
{
    return tm.current().linearize_colors;
}

static float srgb_chan_to_linear_3d(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static void set_pc_color_3d(float dst[4], const ui::Color& src, const ui::ThemeManager& tm)
{
    if (should_linearize_3d(tm))
    {
        dst[0] = srgb_chan_to_linear_3d(src.r);
        dst[1] = srgb_chan_to_linear_3d(src.g);
        dst[2] = srgb_chan_to_linear_3d(src.b);
    }
    else
    {
        dst[0] = src.r;
        dst[1] = src.g;
        dst[2] = src.b;
    }
    dst[3] = src.a;
}

void Renderer::render_bounding_box(Axes3D& axes, const Rect& /*viewport*/)
{
    if (!axes.show_bounding_box())
        return;

    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& bc             = gpu.bbox_cache;
    bool  limits_changed = !bc.valid || bc.xmin != xlim.min || bc.xmax != xlim.max
                          || bc.ymin != ylim.min || bc.ymax != ylim.max || bc.zmin != zlim.min
                          || bc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {static_cast<float>(xlim.min),
                           static_cast<float>(ylim.min),
                           static_cast<float>(zlim.min)};
        vec3 max_corner = {static_cast<float>(xlim.max),
                           static_cast<float>(ylim.max),
                           static_cast<float>(zlim.max)};

        Axes3DRenderer::BoundingBoxData bbox;
        bbox.generate(min_corner, max_corner);

        if (bbox.edge_vertices.empty())
            return;

        size_t bbox_floats = bbox.edge_vertices.size() * 3;
        if (bbox_scratch_.size() < bbox_floats)
            bbox_scratch_.resize(bbox_floats);
        for (size_t i = 0; i < bbox.edge_vertices.size(); ++i)
        {
            bbox_scratch_[i * 3]     = bbox.edge_vertices[i].x;
            bbox_scratch_[i * 3 + 1] = bbox.edge_vertices[i].y;
            bbox_scratch_[i * 3 + 2] = bbox.edge_vertices[i].z;
        }

        size_t byte_size = bbox_floats * sizeof(float);
        if (!gpu.bbox_buffer || gpu.bbox_capacity < byte_size)
        {
            if (gpu.bbox_buffer)
                backend_.destroy_buffer(gpu.bbox_buffer);
            gpu.bbox_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size);
            gpu.bbox_capacity = byte_size;
        }
        backend_.upload_buffer(gpu.bbox_buffer, bbox_scratch_.data(), byte_size);
        gpu.bbox_vertex_count = static_cast<uint32_t>(bbox.edge_vertices.size());
        bc.xmin               = xlim.min;
        bc.xmax               = xlim.max;
        bc.ymin               = ylim.min;
        bc.ymax               = ylim.max;
        bc.zmin               = zlim.min;
        bc.zmax               = zlim.max;
        bc.valid              = true;
    }

    if (!gpu.bbox_buffer || gpu.bbox_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = theme_mgr_.colors();
    set_pc_color_3d(pc.color, theme_colors.axis_line, theme_mgr_);
    pc.line_width    = 1.5f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.bbox_buffer, 0);
    backend_.draw(gpu.bbox_vertex_count);
}

void Renderer::render_tick_marks(Axes3D& axes, const Rect& /*viewport*/)
{
    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    // Check if limits changed — skip regeneration if cached
    auto& tc             = gpu.tick_cache;
    bool  limits_changed = !tc.valid || tc.xmin != xlim.min || tc.xmax != xlim.max
                          || tc.ymin != ylim.min || tc.ymax != ylim.max || tc.zmin != zlim.min
                          || tc.zmax != zlim.max;

    if (limits_changed)
    {
        vec3 min_corner = {static_cast<float>(xlim.min),
                           static_cast<float>(ylim.min),
                           static_cast<float>(zlim.min)};
        vec3 max_corner = {static_cast<float>(xlim.max),
                           static_cast<float>(ylim.max),
                           static_cast<float>(zlim.max)};

        // Tick length: ~2% of axis range
        auto x_tick_len = static_cast<float>((ylim.max - ylim.min) * 0.02);
        auto y_tick_len = static_cast<float>((xlim.max - xlim.min) * 0.02);
        auto z_tick_len = static_cast<float>((xlim.max - xlim.min) * 0.02);

        Axes3DRenderer::TickMarkData x_data;
        x_data.generate_x_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData y_data;
        y_data.generate_y_ticks(axes, min_corner, max_corner);
        Axes3DRenderer::TickMarkData z_data;
        z_data.generate_z_ticks(axes, min_corner, max_corner);

        size_t total_ticks =
            x_data.positions.size() + y_data.positions.size() + z_data.positions.size();
        if (total_ticks == 0)
            return;

        size_t floats_needed = total_ticks * 6;
        if (tick_scratch_.size() < floats_needed)
            tick_scratch_.resize(floats_needed);
        size_t wi = 0;

        for (const auto& pos : x_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y - x_tick_len;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : y_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - y_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }
        for (const auto& pos : z_data.positions)
        {
            tick_scratch_[wi++] = pos.x;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
            tick_scratch_[wi++] = pos.x - z_tick_len;
            tick_scratch_[wi++] = pos.y;
            tick_scratch_[wi++] = pos.z;
        }

        size_t byte_size = wi * sizeof(float);
        if (!gpu.tick_buffer || gpu.tick_capacity < byte_size)
        {
            if (gpu.tick_buffer)
                backend_.destroy_buffer(gpu.tick_buffer);
            gpu.tick_buffer   = backend_.create_buffer(BufferUsage::Vertex, byte_size * 2);
            gpu.tick_capacity = byte_size * 2;
        }
        backend_.upload_buffer(gpu.tick_buffer, tick_scratch_.data(), byte_size);
        gpu.tick_vertex_count = static_cast<uint32_t>(wi / 3);
        tc.xmin               = xlim.min;
        tc.xmax               = xlim.max;
        tc.ymin               = ylim.min;
        tc.ymax               = ylim.max;
        tc.zmin               = zlim.min;
        tc.zmax               = zlim.max;
        tc.valid              = true;
    }

    if (!gpu.tick_buffer || gpu.tick_vertex_count == 0)
        return;

    backend_.bind_pipeline(grid3d_pipeline_);

    SeriesPushConstants pc{};
    const auto&         theme_colors = theme_mgr_.colors();
    ui::Color           tick_color(theme_colors.grid_major.r * 0.8f,
                         theme_colors.grid_major.g * 0.8f,
                         theme_colors.grid_major.b * 0.8f,
                         theme_colors.grid_major.a);
    set_pc_color_3d(pc.color, tick_color, theme_mgr_);
    pc.line_width    = 1.5f;
    pc.data_offset_x = 0.0f;
    pc.data_offset_y = 0.0f;
    backend_.push_constants(pc);

    backend_.bind_buffer(gpu.tick_buffer, 0);
    backend_.draw(gpu.tick_vertex_count);
}

void Renderer::render_arrows(Axes3D& axes, const Rect& /*viewport*/)
{
    auto  xlim = axes.x_limits();
    auto  ylim = axes.y_limits();
    auto  zlim = axes.z_limits();
    auto& gpu  = axes_gpu_data_[&axes];

    float x0          = xlim.min;
    float y0          = ylim.min;
    float z0          = zlim.min;
    float x1          = xlim.max;
    float y1          = ylim.max;
    float z1          = zlim.max;
    float arrow_len_x = (xlim.max - xlim.min) * 0.18f;
    float arrow_len_y = (ylim.max - ylim.min) * 0.18f;
    float arrow_len_z = (zlim.max - zlim.min) * 0.18f;

    // Transform arrow endpoints from data space to normalized space so that
    // the cylinder/cone geometry is generated in a uniformly-scaled coordinate
    // system. The data_to_normalized_matrix applies non-uniform scale per axis
    // which would distort circular cross-sections into ellipses.
    mat4 model    = axes.data_to_normalized_matrix();
    auto xform_pt = [&](vec3 p) -> vec3
    {
        return {model.m[0] * p.x + model.m[4] * p.y + model.m[8] * p.z + model.m[12],
                model.m[1] * p.x + model.m[5] * p.y + model.m[9] * p.z + model.m[13],
                model.m[2] * p.x + model.m[6] * p.y + model.m[10] * p.z + model.m[14]};
    };

    // Arrow endpoints in normalized space
    vec3 n_x_start = xform_pt({x1, y0, z0});
    vec3 n_x_end   = xform_pt({x1 + arrow_len_x, y0, z0});
    vec3 n_y_start = xform_pt({x0, y1, z0});
    vec3 n_y_end   = xform_pt({x0, y1 + arrow_len_y, z0});
    vec3 n_z_start = xform_pt({x0, y0, z1});
    vec3 n_z_end   = xform_pt({x0, y0, z1 + arrow_len_z});

    // In normalized space, box_half_size is the reference for arrow thickness.
    float hs = spectra::Axes3D::box_half_size();

    // Geometry parameters for solid lit 3D arrows
    constexpr int   SEGMENTS    = 16;
    constexpr float SHAFT_FRAC  = 0.018f;   // shaft radius as fraction of box half-size
    constexpr float CONE_FRAC   = 0.048f;   // cone radius as fraction of box half-size
    constexpr float CONE_LENGTH = 0.25f;    // cone length as fraction of arrow length
    constexpr float PI          = 3.14159265358979f;

    float shaft_r = hs * SHAFT_FRAC;
    float cone_r  = hs * CONE_FRAC;

    // Vertex layout: {px, py, pz, nx, ny, nz} = 6 floats per vertex
    arrow_tri_scratch_.clear();

    // Helper: push one vertex (position + normal) into scratch buffer
    auto push_vert = [&](vec3 pos, vec3 n)
    {
        arrow_tri_scratch_.push_back(pos.x);
        arrow_tri_scratch_.push_back(pos.y);
        arrow_tri_scratch_.push_back(pos.z);
        arrow_tri_scratch_.push_back(n.x);
        arrow_tri_scratch_.push_back(n.y);
        arrow_tri_scratch_.push_back(n.z);
    };

    // Helper: build an orthonormal basis (u, v) perpendicular to direction d
    auto make_basis = [](vec3 d, vec3& u, vec3& v)
    {
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1e-8f)
            return;
        d.x /= len;
        d.y /= len;
        d.z /= len;
        vec3 ref = (std::abs(d.y) < 0.9f) ? vec3{0, 1, 0} : vec3{1, 0, 0};
        u = {d.y * ref.z - d.z * ref.y, d.z * ref.x - d.x * ref.z, d.x * ref.y - d.y * ref.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        if (ul < 1e-8f)
            return;
        u.x /= ul;
        u.y /= ul;
        u.z /= ul;
        v = {d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x};
    };

    // Emit a full lit 3D arrow: cylinder shaft + cone arrowhead with normals.
    auto emit_arrow_3d = [&](vec3 start, vec3 end)
    {
        vec3  dir       = {end.x - start.x, end.y - start.y, end.z - start.z};
        float total_len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (total_len < 1e-6f)
            return;
        vec3 d = {dir.x / total_len, dir.y / total_len, dir.z / total_len};

        vec3 u{};
        vec3 v{};
        make_basis(d, u, v);

        float cone_len  = total_len * CONE_LENGTH;
        float shaft_len = total_len - cone_len;

        vec3 shaft_end = {
            start.x + d.x * shaft_len,
            start.y + d.y * shaft_len,
            start.z + d.z * shaft_len,
        };

        // Precompute circle offsets
        float cos_table[SEGMENTS];
        float sin_table[SEGMENTS];
        for (int i = 0; i < SEGMENTS; ++i)
        {
            float angle  = 2.0f * PI * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            cos_table[i] = std::cos(angle);
            sin_table[i] = std::sin(angle);
        }

        // Point on circle at center c with radius r
        auto circle_pt = [&](vec3 c, float r, int seg) -> vec3
        {
            float cs = cos_table[seg];
            float sn = sin_table[seg];
            return {c.x + (u.x * cs + v.x * sn) * r,
                    c.y + (u.y * cs + v.y * sn) * r,
                    c.z + (u.z * cs + v.z * sn) * r};
        };

        // Outward-pointing radial normal at segment index
        auto radial_normal = [&](int seg) -> vec3
        {
            float cs = cos_table[seg];
            float sn = sin_table[seg];
            return {u.x * cs + v.x * sn, u.y * cs + v.y * sn, u.z * cs + v.z * sn};
        };

        // Negative axis direction (for back-facing caps)
        vec3 neg_d = {-d.x, -d.y, -d.z};

        // ── Cylinder shaft body ──
        // Normals point radially outward from the cylinder axis.
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 b0   = circle_pt(start, shaft_r, i);
            vec3 b1   = circle_pt(start, shaft_r, next);
            vec3 t0   = circle_pt(shaft_end, shaft_r, i);
            vec3 t1   = circle_pt(shaft_end, shaft_r, next);
            vec3 n0   = radial_normal(i);
            vec3 n1   = radial_normal(next);
            // Two triangles per quad
            push_vert(b0, n0);
            push_vert(t0, n0);
            push_vert(b1, n1);
            push_vert(b1, n1);
            push_vert(t0, n0);
            push_vert(t1, n1);
        }

        // ── Shaft start cap (disc) — normal points backward ──
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 p0   = circle_pt(start, shaft_r, i);
            vec3 p1   = circle_pt(start, shaft_r, next);
            push_vert(start, neg_d);
            push_vert(p1, neg_d);
            push_vert(p0, neg_d);
        }

        // ── Cone side ──
        // Cone normal: for a cone with tip at 'end' and base at 'shaft_end',
        // the surface normal tilts outward from the axis. The tilt angle depends
        // on the cone_r / cone_len ratio.
        float cone_slope  = cone_len / std::sqrt(cone_r * cone_r + cone_len * cone_len);
        float cone_radial = cone_r / std::sqrt(cone_r * cone_r + cone_len * cone_len);
        // cone_normal(seg) = radial_normal(seg) * cone_slope + d * cone_radial
        // (tilted outward from axis by the cone half-angle)
        auto cone_normal = [&](int seg) -> vec3
        {
            vec3 rn = radial_normal(seg);
            return {rn.x * cone_slope + d.x * cone_radial,
                    rn.y * cone_slope + d.y * cone_radial,
                    rn.z * cone_slope + d.z * cone_radial};
        };

        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 c0   = circle_pt(shaft_end, cone_r, i);
            vec3 c1   = circle_pt(shaft_end, cone_r, next);
            vec3 cn0  = cone_normal(i);
            vec3 cn1  = cone_normal(next);
            // Average normal at tip for smooth shading
            vec3 cn_avg = {(cn0.x + cn1.x) * 0.5f, (cn0.y + cn1.y) * 0.5f, (cn0.z + cn1.z) * 0.5f};
            push_vert(end, cn_avg);
            push_vert(c0, cn0);
            push_vert(c1, cn1);
        }

        // ── Cone base cap (disc) — normal points backward ──
        for (int i = 0; i < SEGMENTS; ++i)
        {
            int  next = (i + 1) % SEGMENTS;
            vec3 c0   = circle_pt(shaft_end, cone_r, i);
            vec3 c1   = circle_pt(shaft_end, cone_r, next);
            push_vert(shaft_end, neg_d);
            push_vert(c1, neg_d);
            push_vert(c0, neg_d);
        }
    };

    emit_arrow_3d(n_x_start, n_x_end);
    emit_arrow_3d(n_y_start, n_y_end);
    emit_arrow_3d(n_z_start, n_z_end);

    // Triangles per arrow: shaft body (SEGMENTS*2) + shaft cap (SEGMENTS)
    //                     + cone body (SEGMENTS) + cone cap (SEGMENTS)
    //                     = SEGMENTS * 5
    constexpr uint32_t TRIS_PER_ARROW  = SEGMENTS * 5;
    constexpr uint32_t VERTS_PER_ARROW = TRIS_PER_ARROW * 3;
    // 6 floats per vertex (pos + normal)
    constexpr uint32_t FLOATS_PER_ARROW = VERTS_PER_ARROW * 6;

    // Upload and draw all arrow geometry (Arrow3D pipeline — lit, depth tested).
    // Geometry is in normalized space, so we temporarily set the UBO model matrix
    // to identity (the vertex shader must not re-apply the non-uniform data scale).
    auto     total_floats   = static_cast<uint32_t>(arrow_tri_scratch_.size());
    uint32_t tri_vert_count = total_floats / 6;
    if (tri_vert_count > 0)
    {
        size_t tri_bytes = arrow_tri_scratch_.size() * sizeof(float);
        if (!gpu.arrow_tri_buffer || gpu.arrow_tri_capacity < tri_bytes)
        {
            if (gpu.arrow_tri_buffer)
                backend_.destroy_buffer(gpu.arrow_tri_buffer);
            gpu.arrow_tri_buffer   = backend_.create_buffer(BufferUsage::Vertex, tri_bytes * 2);
            gpu.arrow_tri_capacity = tri_bytes * 2;
        }
        backend_.upload_buffer(gpu.arrow_tri_buffer, arrow_tri_scratch_.data(), tri_bytes);
        gpu.arrow_tri_vertex_count = tri_vert_count;

        // Swap model matrix to identity for arrow rendering (geometry already
        // in normalized space). Preserve the rest of the UBO (projection, view,
        // camera_pos, light_dir).
        FrameUBO arrow_ubo{};
        // Read back current UBO contents by rebuilding from axes state
        const auto& cam = axes.camera();
        {
            const auto& vp     = axes.viewport();
            float       aspect = vp.w / std::max(vp.h, 1.0f);
            // Copy projection
            if (cam.projection_mode == Camera::ProjectionMode::Perspective)
            {
                float fov_rad            = cam.fov * PI / 180.0f;
                float f                  = 1.0f / std::tan(fov_rad * 0.5f);
                arrow_ubo.projection[0]  = f / aspect;
                arrow_ubo.projection[5]  = -f;
                arrow_ubo.projection[10] = cam.far_clip / (cam.near_clip - cam.far_clip);
                arrow_ubo.projection[11] = -1.0f;
                arrow_ubo.projection[14] =
                    (cam.far_clip * cam.near_clip) / (cam.near_clip - cam.far_clip);
            }
            else
            {
                float half_w = cam.ortho_size * aspect;
                float half_h = cam.ortho_size;
                build_ortho_projection_3d(-half_w,
                                          half_w,
                                          -half_h,
                                          half_h,
                                          cam.near_clip,
                                          cam.far_clip,
                                          arrow_ubo.projection);
            }
        }
        // View matrix
        const mat4& view_mat = cam.view_matrix();
        std::memcpy(arrow_ubo.view, view_mat.m, 16 * sizeof(float));
        // Identity model matrix (geometry is already in normalized space)
        mat4 identity = mat4_identity();
        std::memcpy(arrow_ubo.model, identity.m, 16 * sizeof(float));
        arrow_ubo.viewport_width  = axes.viewport().w;
        arrow_ubo.viewport_height = axes.viewport().h;
        arrow_ubo.near_plane      = cam.near_clip;
        arrow_ubo.far_plane       = cam.far_clip;
        arrow_ubo.camera_pos[0]   = cam.position.x;
        arrow_ubo.camera_pos[1]   = cam.position.y;
        arrow_ubo.camera_pos[2]   = cam.position.z;
        if (axes.lighting_enabled())
        {
            vec3 ld                = axes.light_dir();
            arrow_ubo.light_dir[0] = ld.x;
            arrow_ubo.light_dir[1] = ld.y;
            arrow_ubo.light_dir[2] = ld.z;
        }

        backend_.upload_buffer(frame_ubo_buffer_, &arrow_ubo, sizeof(FrameUBO));
        backend_.bind_buffer(frame_ubo_buffer_, 0);

        backend_.bind_pipeline(arrow3d_pipeline_);

        float arrow_colors[][4] = {
            {0.902f, 0.275f, 0.275f, 0.88f},   // X: red
            {0.275f, 0.784f, 0.275f, 0.88f},   // Y: green
            {0.314f, 0.510f, 1.000f, 0.88f},   // Z: blue
        };
        constexpr float kHoverBrighten = 1.55f;

        const Axes3D::AxisArrowHover hover = axes.hovered_axis_arrow();

        backend_.bind_buffer(gpu.arrow_tri_buffer, 0);
        uint32_t num_arrows = total_floats / FLOATS_PER_ARROW;
        for (uint32_t i = 0; i < num_arrows && i < 3; ++i)
        {
            const bool is_hovered = (i == 0 && hover == Axes3D::AxisArrowHover::X)
                                    || (i == 1 && hover == Axes3D::AxisArrowHover::Y)
                                    || (i == 2 && hover == Axes3D::AxisArrowHover::Z);
            const float brighten = is_hovered ? kHoverBrighten : 1.0f;

            SeriesPushConstants pc{};
            pc.color[0] = std::min(arrow_colors[i][0] * brighten, 1.0f);
            pc.color[1] = std::min(arrow_colors[i][1] * brighten, 1.0f);
            pc.color[2] = std::min(arrow_colors[i][2] * brighten, 1.0f);
            pc.color[3] = is_hovered ? 1.0f : arrow_colors[i][3];
            pc.opacity  = 1.0f;
            backend_.push_constants(pc);
            backend_.draw(VERTS_PER_ARROW, i * VERTS_PER_ARROW);
        }

        // Restore the original data-space model UBO so subsequent rendering
        // (grid, series) uses the correct non-uniform scale.
        FrameUBO restore_ubo = arrow_ubo;
        std::memcpy(restore_ubo.model, model.m, 16 * sizeof(float));
        backend_.upload_buffer(frame_ubo_buffer_, &restore_ubo, sizeof(FrameUBO));
        backend_.bind_buffer(frame_ubo_buffer_, 0);
    }
}

}   // namespace spectra
