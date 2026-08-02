#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/axes3d.hpp>

#include "axes3d_pick.hpp"

namespace spectra
{

enum class Axis3DArrowPick
{
    None,
    X,
    Y,
    Z
};

namespace detail
{

inline float dist2_point_to_segment(float px, float py, float ax, float ay, float bx, float by)
{
    const float abx  = bx - ax;
    const float aby  = by - ay;
    const float len2 = abx * abx + aby * aby;
    if (len2 < 1e-8f)
    {
        const float dx = px - ax;
        const float dy = py - ay;
        return dx * dx + dy * dy;
    }
    const float t  = std::clamp(((px - ax) * abx + (py - ay) * aby) / len2, 0.0f, 1.0f);
    const float cx = ax + t * abx;
    const float cy = ay + t * aby;
    const float dx = px - cx;
    const float dy = py - cy;
    return dx * dx + dy * dy;
}

}   // namespace detail

// Hit-test the RGB axis arrows in screen space (matches render_3d arrow geometry).
inline Axis3DArrowPick pick_axes3d_axis_arrow(const Axes3D& axes,
                                              float         cursor_x,
                                              float         cursor_y,
                                              float         hit_radius_px = 28.0f)
{
    const auto& vp = axes.viewport();
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return Axis3DArrowPick::None;

    const auto  xlim        = axes.x_limits();
    const auto  ylim        = axes.y_limits();
    const auto  zlim        = axes.z_limits();
    const float x0          = static_cast<float>(xlim.min);
    const float y0          = static_cast<float>(ylim.min);
    const float z0          = static_cast<float>(zlim.min);
    const float x1          = static_cast<float>(xlim.max);
    const float y1          = static_cast<float>(ylim.max);
    const float z1          = static_cast<float>(zlim.max);
    const float arrow_len_x = static_cast<float>((xlim.max - xlim.min) * 0.18);
    const float arrow_len_y = static_cast<float>((ylim.max - ylim.min) * 0.18);
    const float arrow_len_z = static_cast<float>((zlim.max - zlim.min) * 0.18);

    struct ArrowSeg
    {
        Axis3DArrowPick id;
        vec3            a;
        vec3            b;
    };

    const ArrowSeg segments[] = {
        {Axis3DArrowPick::X, {x1, y0, z0}, {x1 + arrow_len_x, y0, z0}},
        {Axis3DArrowPick::Y, {x0, y1, z0}, {x0, y1 + arrow_len_y, z0}},
        {Axis3DArrowPick::Z, {x0, y0, z1}, {x0, y0, z1 + arrow_len_z}},
    };

    const float     hit_radius2 = hit_radius_px * hit_radius_px;
    Axis3DArrowPick best        = Axis3DArrowPick::None;
    float           best_dist2  = std::numeric_limits<float>::max();

    for (const ArrowSeg& seg : segments)
    {
        const Projected3DPoint pa = project_axes3d_data_point(axes, seg.a);
        const Projected3DPoint pb = project_axes3d_data_point(axes, seg.b);
        if (!pa.visible && !pb.visible)
            continue;

        float ax = pa.screen_x;
        float ay = pa.screen_y;
        float bx = pb.screen_x;
        float by = pb.screen_y;
        if (!pa.visible)
        {
            ax = bx;
            ay = by;
        }
        if (!pb.visible)
        {
            bx = ax;
            by = ay;
        }

        const float dist2 = detail::dist2_point_to_segment(cursor_x, cursor_y, ax, ay, bx, by);
        if (dist2 <= hit_radius2 && dist2 < best_dist2)
        {
            best_dist2 = dist2;
            best       = seg.id;
        }
    }

    return best;
}

inline Axes3D::AxisArrowHover axis_pick_to_hover(Axis3DArrowPick pick)
{
    switch (pick)
    {
        case Axis3DArrowPick::X:
            return Axes3D::AxisArrowHover::X;
        case Axis3DArrowPick::Y:
            return Axes3D::AxisArrowHover::Y;
        case Axis3DArrowPick::Z:
            return Axes3D::AxisArrowHover::Z;
        default:
            return Axes3D::AxisArrowHover::None;
    }
}

inline Camera::AxisView axis_pick_to_camera_view(Axis3DArrowPick pick)
{
    switch (pick)
    {
        case Axis3DArrowPick::X:
            return Camera::AxisView::PositiveX;
        case Axis3DArrowPick::Y:
            return Camera::AxisView::PositiveY;
        case Axis3DArrowPick::Z:
            return Camera::AxisView::PositiveZ;
        default:
            return Camera::AxisView::PositiveX;
    }
}

}   // namespace spectra
