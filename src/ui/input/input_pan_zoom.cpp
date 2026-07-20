#include "input.hpp"

#include <cmath>

#include <algorithm>
#include <limits>
#include <spectra/logger.hpp>

#include "gesture_recognizer.hpp"
#include "ui/animation/animation_controller.hpp"
#include "ui/animation/transition_engine.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/overlay/axes3d_axis_pick.hpp"
#ifdef SPECTRA_USE_IMGUI
    #include "ui/overlay/data_interaction.hpp"
#endif

namespace spectra
{

namespace
{
constexpr int MOUSE_BUTTON_LEFT  = 0;
constexpr int MOUSE_BUTTON_RIGHT = 1;
constexpr int ACTION_PRESS       = 1;
constexpr int ACTION_RELEASE     = 0;

}   // anonymous namespace

void InputHandler::update_axes3d_arrow_hover(AxesBase* hit_base, double x, double y)
{
    if (is_3d_orbit_drag_ || is_3d_pan_drag_ || rclick_zoom_dragging_)
        return;

    if (figure_)
    {
        for (auto& axes_ptr : figure_->all_axes_mut())
        {
            auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
            if (axes3d)
                axes3d->set_hovered_axis_arrow(Axes3D::AxisArrowHover::None);
        }
    }

    auto* axes3d = dynamic_cast<Axes3D*>(hit_base);
    if (!axes3d)
        return;

    const Axis3DArrowPick pick =
        pick_axes3d_axis_arrow(*axes3d, static_cast<float>(x), static_cast<float>(y));
    axes3d->set_hovered_axis_arrow(axis_pick_to_hover(pick));
}

void InputHandler::snap_axes3d_axis_view(Axes3D* axes3d, Camera::AxisView axis_view)
{
    Camera&      cam    = axes3d->camera();
    const Camera before = cam;

    Camera target = cam;
    target.align_view_to_axis(axis_view);

    if (transition_engine_)
    {
        transition_engine_->cancel_for_camera(&cam);
        transition_engine_->animate_camera(cam, target, AUTOFIT_ANIM_DURATION, ease::ease_out);
    }
    else if (anim_ctrl_)
    {
        anim_ctrl_->cancel_all();
        anim_ctrl_->animate_camera(cam, target, AUTOFIT_ANIM_DURATION, ease::ease_out);
    }
    else
    {
        cam = target;
        cam.update_position_from_orbit();
    }

    if (undo_mgr_)
    {
        Axes3D* ax = axes3d;
        undo_mgr_->push(UndoAction{"Align 3D axis view",
                                   [ax, before]()
                                   {
                                       ax->camera() = before;
                                       ax->camera().update_position_from_orbit();
                                   },
                                   [ax, target]()
                                   {
                                       ax->camera() = target;
                                       ax->camera().update_position_from_orbit();
                                   }});
    }
}

// ─── 3D camera interaction (orbit, right-click zoom, middle-pan) ────────────

void InputHandler::handle_mouse_button_3d(Axes3D* axes3d,
                                          int     button,
                                          int     action,
                                          double  x,
                                          double  y)
{
    if (button == MOUSE_BUTTON_LEFT && action == ACTION_PRESS)
    {
        if (gesture_ && gesture_->on_click(x, y))
        {
            is_3d_orbit_drag_ = false;
            mode_             = InteractionMode::Idle;
            drag3d_axes_      = nullptr;

            const Axis3DArrowPick axis_pick =
                pick_axes3d_axis_arrow(*axes3d, static_cast<float>(x), static_cast<float>(y));
            if (axis_pick != Axis3DArrowPick::None)
            {
                SPECTRA_LOG_DEBUG("input", "Double-click on 3D axis arrow — align view");
                snap_axes3d_axis_view(axes3d, axis_pick_to_camera_view(axis_pick));
                return;
            }

            SPECTRA_LOG_DEBUG("input", "Double-click on 3D axes — auto-fit");
            axes3d->auto_fit();
            return;
        }

        if (transition_engine_)
            transition_engine_->cancel_for_camera(&axes3d->camera());
        else if (anim_ctrl_)
            anim_ctrl_->cancel_all();

        is_3d_orbit_drag_ = true;
        drag_start_x_     = x;
        drag_start_y_     = y;
        mode_             = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        return;
    }
    if (button == MOUSE_BUTTON_LEFT && action == ACTION_RELEASE && is_3d_orbit_drag_)
    {
        const double dx_click = x - drag_start_x_;
        const double dy_click = y - drag_start_y_;
        const float  move_dist =
            static_cast<float>(std::sqrt(dx_click * dx_click + dy_click * dy_click));
        constexpr float CLICK_THRESHOLD_PX = 5.0f;
        const bool      was_click          = move_dist < CLICK_THRESHOLD_PX;

        is_3d_orbit_drag_ = false;
        mode_             = InteractionMode::Idle;

        if (was_click
#ifdef SPECTRA_USE_IMGUI
            && data_interaction_
#endif
        )
        {
            // Short click: undo only micro-orbit jitter, not an intentional nudge away
            // from an axis-aligned snap (drag3d_start may be the snapped pose).
            auto&       cam         = axes3d->camera();
            const float angle_delta = std::abs(cam.azimuth - drag3d_start_camera_.azimuth)
                                      + std::abs(cam.elevation - drag3d_start_camera_.elevation);
            const vec3  pos_delta  = cam.position - drag3d_start_camera_.position;
            const float pos_delta2 = vec3_length_sq(pos_delta);
            if (angle_delta < 0.5f && pos_delta2 < 1e-4f)
            {
                cam = drag3d_start_camera_;
                cam.update_position_from_orbit();
            }
#ifdef SPECTRA_USE_IMGUI
            data_interaction_->on_mouse_click_datatip_only(0, x, y);
#endif
            drag3d_axes_ = nullptr;
            return;
        }

        // Push undo for orbit drag
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim   = drag3d_start_xlim_;
            auto    before_ylim   = drag3d_start_ylim_;
            auto    before_zlim   = drag3d_start_zlim_;
            auto    before_camera = drag3d_start_camera_;
            auto    after_camera  = axes3d->camera();
            Axes3D* ax            = axes3d;
            undo_mgr_->push(UndoAction{"Orbit 3D",
                                       [ax, before_xlim, before_ylim, before_zlim, before_camera]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                           ax->camera() = before_camera;
                                           ax->camera().update_position_from_orbit();
                                       },
                                       [ax, after_camera]()
                                       {
                                           ax->camera() = after_camera;
                                           ax->camera().update_position_from_orbit();
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
    if (button == MOUSE_BUTTON_RIGHT && action == ACTION_PRESS)
    {
        rclick_zoom_dragging_ = true;
        rclick_zoom_3d_       = true;
        rclick_zoom_axis_     = ZoomAxis::None;
        rclick_zoom_start_x_  = x;
        rclick_zoom_start_y_  = y;
        rclick_zoom_last_x_   = x;
        rclick_zoom_last_y_   = y;
        rclick_zoom_xlim_min_ = axes3d->x_limits().min;
        rclick_zoom_xlim_max_ = axes3d->x_limits().max;
        rclick_zoom_ylim_min_ = axes3d->y_limits().min;
        rclick_zoom_ylim_max_ = axes3d->y_limits().max;
        rclick_zoom_zlim_min_ = axes3d->z_limits().min;
        rclick_zoom_zlim_max_ = axes3d->z_limits().max;
        mode_                 = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        // Project each data axis direction to screen space so we can later
        // pick the axis most aligned with the drag direction.
        {
            const auto& vp     = axes3d->viewport();
            const auto& cam    = axes3d->camera();
            float       aspect = vp.w / std::max(vp.h, 1.0f);
            mat4        proj   = cam.projection_matrix(aspect);
            mat4        view   = cam.view_matrix();
            mat4        model  = axes3d->data_to_normalized_matrix();
            mat4        mvp    = mat4_mul(proj, mat4_mul(view, model));

            // Project a normalized-cube point to viewport screen coords.
            auto project = [&](vec3 p, float& sx, float& sy) -> bool
            {
                float cx = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
                float cy = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
                float cw = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
                if (cw <= 0.001f)
                    return false;
                sx = vp.x + (cx / cw + 1.0f) * 0.5f * vp.w;
                sy = vp.y + (cy / cw + 1.0f) * 0.5f * vp.h;
                return true;
            };

            // Origin of the normalized cube
            vec3  origin{0.0f, 0.0f, 0.0f};
            float ox = NAN;
            float oy = NAN;
            if (project(origin, ox, oy))
            {
                // Unit steps along each normalized-cube axis
                const float step         = 1.0f;
                vec3        axis_tips[3] = {{step, 0, 0}, {0, step, 0}, {0, 0, step}};
                for (int i = 0; i < 3; ++i)
                {
                    float tx = NAN;
                    float ty = NAN;
                    if (project(axis_tips[i], tx, ty))
                    {
                        float ddx = tx - ox;
                        float ddy = ty - oy;
                        float len = std::sqrt(ddx * ddx + ddy * ddy);
                        if (len > 1e-4f)
                        {
                            ddx /= len;
                            ddy /= len;
                        }
                        rclick_zoom_axis_sx_[i] = ddx;
                        rclick_zoom_axis_sy_[i] = ddy;
                    }
                    else
                    {
                        rclick_zoom_axis_sx_[i] = (i == 0) ? 1.0f : 0.0f;
                        rclick_zoom_axis_sy_[i] = (i == 1) ? 1.0f : 0.0f;
                    }
                }
            }
            else
            {
                rclick_zoom_axis_sx_[0] = 1.0f;
                rclick_zoom_axis_sy_[0] = 0.0f;
                rclick_zoom_axis_sx_[1] = 0.0f;
                rclick_zoom_axis_sy_[1] = 1.0f;
                rclick_zoom_axis_sx_[2] = 0.0f;
                rclick_zoom_axis_sy_[2] = 0.0f;
            }
        }
        return;
    }
    if (button == MOUSE_BUTTON_RIGHT && action == ACTION_RELEASE && rclick_zoom_dragging_
        && rclick_zoom_3d_)
    {
        rclick_zoom_dragging_ = false;
        rclick_zoom_3d_       = false;
        rclick_zoom_axis_     = ZoomAxis::None;
        mode_                 = InteractionMode::Idle;
        // Push undo for 1D zoom
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim = drag3d_start_xlim_;
            auto    before_ylim = drag3d_start_ylim_;
            auto    before_zlim = drag3d_start_zlim_;
            auto    after_xlim  = axes3d->x_limits();
            auto    after_ylim  = axes3d->y_limits();
            auto    after_zlim  = axes3d->z_limits();
            Axes3D* ax          = axes3d;
            undo_mgr_->push(UndoAction{"Zoom 1D 3D",
                                       [ax, before_xlim, before_ylim, before_zlim]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                       },
                                       [ax, after_xlim, after_ylim, after_zlim]()
                                       {
                                           ax->xlim(after_xlim.min, after_xlim.max);
                                           ax->ylim(after_ylim.min, after_ylim.max);
                                           ax->zlim(after_zlim.min, after_zlim.max);
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
    if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_PRESS)
    {
        is_3d_pan_drag_ = true;
        drag_start_x_   = x;
        drag_start_y_   = y;
        mode_           = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        return;
    }
    if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_RELEASE && is_3d_pan_drag_)
    {
        is_3d_pan_drag_ = false;
        mode_           = InteractionMode::Idle;
        // Push undo for middle-mouse pan
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim = drag3d_start_xlim_;
            auto    before_ylim = drag3d_start_ylim_;
            auto    before_zlim = drag3d_start_zlim_;
            auto    after_xlim  = axes3d->x_limits();
            auto    after_ylim  = axes3d->y_limits();
            auto    after_zlim  = axes3d->z_limits();
            Axes3D* ax          = axes3d;
            undo_mgr_->push(UndoAction{"Pan 3D",
                                       [ax, before_xlim, before_ylim, before_zlim]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                       },
                                       [ax, after_xlim, after_ylim, after_zlim]()
                                       {
                                           ax->xlim(after_xlim.min, after_xlim.max);
                                           ax->ylim(after_ylim.min, after_ylim.max);
                                           ax->zlim(after_zlim.min, after_zlim.max);
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
}

// ─── Middle-mouse pan (2D) ──────────────────────────────────────────────────

void InputHandler::handle_mouse_button_middle_pan(int action, double x, double y)
{
    if (action == ACTION_PRESS && !middle_pan_dragging_ && active_axes_)
    {
        // Cancel any running animations
        if (transition_engine_)
            transition_engine_->cancel_for_axes(active_axes_);
        else if (anim_ctrl_)
            anim_ctrl_->cancel_for_axes(active_axes_);

        middle_pan_dragging_ = true;
        middle_pan_start_x_  = x;
        middle_pan_start_y_  = y;
        auto xlim            = active_axes_->x_limits();
        auto ylim            = active_axes_->y_limits();
        middle_pan_xlim_min_ = xlim.min;
        middle_pan_xlim_max_ = xlim.max;
        middle_pan_ylim_min_ = ylim.min;
        middle_pan_ylim_max_ = ylim.max;
    }
    else if (action == ACTION_RELEASE && middle_pan_dragging_)
    {
        middle_pan_dragging_ = false;
    }
}

// ─── Right-click drag zoom (2D) ────────────────────────────────────────────

void InputHandler::handle_mouse_button_rclick_zoom(int action, double x, double y)
{
    if (action == ACTION_PRESS && !rclick_zoom_dragging_ && active_axes_)
    {
        // Cancel any running animations
        if (transition_engine_)
            transition_engine_->cancel_for_axes(active_axes_);
        else if (anim_ctrl_)
            anim_ctrl_->cancel_for_axes(active_axes_);

        rclick_zoom_dragging_ = true;
        rclick_zoom_3d_       = false;
        rclick_zoom_axis_     = ZoomAxis::None;
        rclick_zoom_start_x_  = x;
        rclick_zoom_start_y_  = y;
        rclick_zoom_last_x_   = x;
        rclick_zoom_last_y_   = y;
        auto xlim             = active_axes_->x_limits();
        auto ylim             = active_axes_->y_limits();
        rclick_zoom_xlim_min_ = xlim.min;
        rclick_zoom_xlim_max_ = xlim.max;
        rclick_zoom_ylim_min_ = ylim.min;
        rclick_zoom_ylim_max_ = ylim.max;
        // Capture drag-start position in data space as zoom anchor
        screen_to_data(x, y, rclick_zoom_anchor_data_x_, rclick_zoom_anchor_data_y_);
    }
    else if (action == ACTION_RELEASE && rclick_zoom_dragging_ && !rclick_zoom_3d_)
    {
        rclick_zoom_dragging_ = false;
        rclick_zoom_axis_     = ZoomAxis::None;
    }
}

// ─── Box zoom tool (left-click) ─────────────────────────────────────────────

void InputHandler::handle_mouse_button_box_zoom(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle && active_axes_)
    {
        // Cancel any running animations on this axes
        if (transition_engine_)
        {
            transition_engine_->cancel_for_axes(active_axes_);
        }
        else if (anim_ctrl_)
        {
            anim_ctrl_->cancel_for_axes(active_axes_);
        }
        SPECTRA_LOG_DEBUG("input", "Starting box zoom (BoxZoom tool)");
        mode_            = InteractionMode::Dragging;
        box_zoom_.active = true;
        box_zoom_.x0     = x;
        box_zoom_.y0     = y;
        box_zoom_.x1     = x;
        box_zoom_.y1     = y;
    }
    else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging)
    {
        SPECTRA_LOG_DEBUG("input", "Ending box zoom (BoxZoom tool)");
        apply_box_zoom();
        mode_ = InteractionMode::Idle;
    }
}

// ─── Pan tool (left-click: drag, double-click, ctrl+box-zoom, inertia) ──────

void InputHandler::handle_mouse_button_pan(int action, int mods, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        // Cancel any running animations on this axes (new input overrides)
        if (transition_engine_)
        {
            transition_engine_->cancel_for_axes(active_axes_);
        }
        else if (anim_ctrl_)
        {
            anim_ctrl_->cancel_for_axes(active_axes_);
        }

        // Ctrl+left-click in Pan mode → begin box zoom
        if (mods & MOD_CONTROL)
        {
            SPECTRA_LOG_DEBUG("input", "Ctrl+left-click — starting box zoom in Pan mode");
            mode_                 = InteractionMode::Dragging;
            ctrl_box_zoom_active_ = true;
            box_zoom_.active      = true;
            box_zoom_.x0          = x;
            box_zoom_.y0          = y;
            box_zoom_.x1          = x;
            box_zoom_.y1          = y;
            return;
        }

        // Double-click detection: auto-fit with animated transition
        if (gesture_)
        {
            bool is_double = gesture_->on_click(x, y);
            if (is_double)
            {
                SPECTRA_LOG_DEBUG("input", "Double-click detected — animated auto-fit");
                if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
                {
                    // 3D: auto_fit resets limits + camera in one call
                    axes3d->auto_fit();
                    return;   // Don't start a pan drag on double-click
                }
                if (active_axes_ && (transition_engine_ || anim_ctrl_))
                {
                    // Compute auto-fit target limits
                    auto old_xlim = active_axes_->x_limits();
                    auto old_ylim = active_axes_->y_limits();
                    active_axes_->auto_fit();
                    AxisLimits target_x = active_axes_->x_limits();
                    AxisLimits target_y = active_axes_->y_limits();
                    // Restore current limits so animation can interpolate
                    active_axes_->xlim(old_xlim.min, old_xlim.max);
                    active_axes_->ylim(old_ylim.min, old_ylim.max);
                    if (transition_engine_)
                    {
                        transition_engine_->animate_limits(*active_axes_,
                                                           target_x,
                                                           target_y,
                                                           AUTOFIT_ANIM_DURATION,
                                                           ease::ease_out);
                    }
                    else
                    {
                        anim_ctrl_->animate_axis_limits(*active_axes_,
                                                        target_x,
                                                        target_y,
                                                        AUTOFIT_ANIM_DURATION,
                                                        ease::ease_out);
                    }
                    // Propagate auto-fit to linked axes
                    if (axis_link_mgr_)
                    {
                        axis_link_mgr_->propagate_limits(active_axes_, target_x, target_y);
                    }
                    return;   // Don't start a pan drag on double-click
                }
            }
        }

        // Begin pan drag
        mode_            = InteractionMode::Dragging;
        drag_start_x_    = x;
        drag_start_y_    = y;
        last_move_x_     = x;
        last_move_y_     = y;
        last_move_time_  = Clock::now();
        drag_start_time_ = last_move_time_;

        auto xlim            = active_axes_->x_limits();
        auto ylim            = active_axes_->y_limits();
        drag_start_xlim_min_ = xlim.min;
        drag_start_xlim_max_ = xlim.max;
        drag_start_ylim_min_ = ylim.min;
        drag_start_ylim_max_ = ylim.max;
    }
    else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging)
    {
        // Check if this was a Ctrl+drag box zoom
        if (ctrl_box_zoom_active_)
        {
            SPECTRA_LOG_DEBUG("input", "Ending Ctrl+drag box zoom");
            apply_box_zoom();
            mode_                 = InteractionMode::Idle;
            ctrl_box_zoom_active_ = false;
            return;
        }

        mode_ = InteractionMode::Idle;

        // Detect click-without-drag: if the mouse barely moved, revert tiny
        // pan jitter and keep Pan mode non-selecting.
        {
            auto            dx_px              = static_cast<float>(x - drag_start_x_);
            auto            dy_px              = static_cast<float>(y - drag_start_y_);
            float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
            constexpr float CLICK_THRESHOLD_PX = 5.0f;
            if (move_dist < CLICK_THRESHOLD_PX)
            {
                // Undo the tiny pan that occurred during the drag
                if (active_axes_)
                {
                    active_axes_->xlim(drag_start_xlim_min_, drag_start_xlim_max_);
                    active_axes_->ylim(drag_start_ylim_min_, drag_start_ylim_max_);
                }

                // Pan-mode click behavior: select/highlight nearest point (data tip).
#ifdef SPECTRA_USE_IMGUI
                if (data_interaction_)
                {
                    data_interaction_->on_mouse_click_datatip_only(0, x, y);
                }
#endif
                return;
            }
        }

        // Compute release velocity for inertial pan
        if ((transition_engine_ || anim_ctrl_) && active_axes_)
        {
            auto  now        = Clock::now();
            float dt_sec     = std::chrono::duration<float>(now - last_move_time_).count();
            float drag_total = std::chrono::duration<float>(now - drag_start_time_).count();

            // Only apply inertia if the drag was short and recent movement exists
            if (dt_sec < 0.1f && dt_sec > 0.0f && drag_total > 0.05f)
            {
                // Skip inertia if mouse barely moved — prevents spurious
                // acceleration from sub-pixel or 1-2 px jitter on release
                auto            dx_px               = static_cast<float>(x - last_move_x_);
                auto            dy_px               = static_cast<float>(y - last_move_y_);
                float           dist_px             = std::sqrt(dx_px * dx_px + dy_px * dy_px);
                constexpr float MIN_RELEASE_DIST_PX = 2.0f;

                if (dist_px >= MIN_RELEASE_DIST_PX)
                {
                    const auto& vp   = viewport_for_axes(active_axes_);
                    auto        xlim = active_axes_->x_limits();
                    auto        ylim = active_axes_->y_limits();

                    double x_range = xlim.max - xlim.min;
                    double y_range = ylim.max - ylim.min;

                    // Use a minimum dt floor to prevent velocity blow-up from
                    // sub-millisecond intervals between last move and release
                    constexpr float MIN_DT_SEC   = 0.008f;   // 8ms floor
                    float           effective_dt = std::max(dt_sec, MIN_DT_SEC);

                    // Screen velocity → data velocity
                    double vx_screen = dx_px / effective_dt;
                    double vy_screen = dy_px / effective_dt;

                    // Clamp screen velocity as a safety net
                    constexpr double MAX_SCREEN_VEL = 3000.0;   // px/sec
                    vx_screen = std::clamp(vx_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);
                    vy_screen = std::clamp(vy_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);

                    auto vx_data = static_cast<float>(-vx_screen * x_range / vp.w);
                    auto vy_data = static_cast<float>(vy_screen * y_range / vp.h);

                    float speed = std::sqrt(vx_data * vx_data + vy_data * vy_data);
                    if (speed > MIN_INERTIA_VELOCITY)
                    {
                        SPECTRA_LOG_DEBUG("input",
                                          "Inertial pan: v=(" + std::to_string(vx_data) + ", "
                                              + std::to_string(vy_data) + ")");
                        if (transition_engine_)
                        {
                            transition_engine_->animate_inertial_pan(*active_axes_,
                                                                     vx_data,
                                                                     vy_data,
                                                                     PAN_INERTIA_DURATION);
                        }
                        else if (anim_ctrl_)
                        {
                            anim_ctrl_->animate_inertial_pan(*active_axes_,
                                                             vx_data,
                                                             vy_data,
                                                             PAN_INERTIA_DURATION);
                        }
                    }
                }
            }
        }
    }
}

// ─── 3D camera drag (orbit/pan) on mouse move ──────────────────────────────

void InputHandler::handle_mouse_move_3d_drag(double x, double y)
{
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        auto& cam = axes3d->camera();
        auto  dx  = static_cast<float>(x - drag_start_x_);
        auto  dy  = static_cast<float>(y - drag_start_y_);

        if (is_3d_orbit_drag_)
        {
            if (transition_engine_)
                transition_engine_->cancel_for_camera(&cam);
            else if (anim_ctrl_)
                anim_ctrl_->cancel_all();
            cam.orbit(dx * ORBIT_SENSITIVITY, -dy * ORBIT_SENSITIVITY);
        }
        else if (is_3d_pan_drag_)
        {
            const auto& vp = viewport_for_axes(axes3d);
            axes3d->pan_limits(dx, dy, vp.w, vp.h);
            if (axis_link_mgr_)
                axis_link_mgr_->propagate_from_3d(axes3d);
        }

        drag_start_x_ = x;
        drag_start_y_ = y;
    }
}

// ─── Right-click drag zoom on mouse move ────────────────────────────────────

void InputHandler::handle_mouse_move_rclick_zoom(double x, double y)
{
    double dx_total     = x - rclick_zoom_start_x_;
    double dy_total     = y - rclick_zoom_start_y_;
    double dx_delta     = x - rclick_zoom_last_x_;
    double dy_delta     = y - rclick_zoom_last_y_;
    rclick_zoom_last_x_ = x;
    rclick_zoom_last_y_ = y;

    // Lock axis direction once drag exceeds threshold
    if (rclick_zoom_axis_ == ZoomAxis::None)
    {
        double abs_dx = std::abs(dx_total);
        double abs_dy = std::abs(dy_total);
        if (abs_dx >= RCLICK_AXIS_LOCK_THRESHOLD || abs_dy >= RCLICK_AXIS_LOCK_THRESHOLD)
        {
            if (rclick_zoom_3d_)
            {
                // 3D: pick the axis whose screen-projected direction best aligns
                // with the drag direction (camera-aware, works at any view angle).
                auto drag_len = static_cast<float>(std::sqrt(abs_dx * abs_dx + abs_dy * abs_dy));
                if (drag_len > 1e-4f)
                {
                    float ndx      = static_cast<float>(dx_total) / drag_len;
                    float ndy      = static_cast<float>(dy_total) / drag_len;
                    float best_dot = -1.0f;
                    int   best_i   = 0;
                    for (int i = 0; i < 3; ++i)
                    {
                        // Use absolute dot product — axis direction or its opposite both zoom
                        float d =
                            std::abs(ndx * rclick_zoom_axis_sx_[i] + ndy * rclick_zoom_axis_sy_[i]);
                        if (d > best_dot)
                        {
                            best_dot = d;
                            best_i   = i;
                        }
                    }
                    static constexpr ZoomAxis kMap[3] = {ZoomAxis::X, ZoomAxis::Y, ZoomAxis::Z};
                    rclick_zoom_axis_                 = kMap[best_i];
                }
            }
            else
            {
                // 2D: 15° cones near horizontal/vertical are pure 1D.
                // Middle angles use proportional XY blending.
                constexpr double kRadToDeg = 57.29577951308232;
                auto angle_deg = static_cast<float>(std::atan2(abs_dy, abs_dx) * kRadToDeg);
                if (angle_deg <= RCLICK_AXIS_1D_THRESHOLD_DEG)
                {
                    rclick_zoom_axis_ = ZoomAxis::X;
                }
                else if (angle_deg >= (90.0f - RCLICK_AXIS_1D_THRESHOLD_DEG))
                {
                    rclick_zoom_axis_ = ZoomAxis::Y;
                }
                else
                {
                    rclick_zoom_axis_ = ZoomAxis::XY;
                }
            }
        }
    }

    // Apply zoom once axis is locked
    if (rclick_zoom_axis_ != ZoomAxis::None)
    {
        // Use the dominant delta component for zoom magnitude
        // Drag right/up = zoom in (shrink range), drag left/down = zoom out (expand range)
        float pixel_delta = 0.0f;
        if (rclick_zoom_axis_ == ZoomAxis::X)
            pixel_delta = static_cast<float>(dx_delta);
        else if (rclick_zoom_axis_ == ZoomAxis::Y)
            pixel_delta = static_cast<float>(-dy_delta);   // screen Y inverted
        else if (rclick_zoom_axis_ == ZoomAxis::XY || rclick_zoom_axis_ == ZoomAxis::Z)
            pixel_delta = static_cast<float>(dx_delta - dy_delta) * 0.5f;

        float factor = 1.0f - pixel_delta * RCLICK_ZOOM_SENSITIVITY;
        factor       = std::clamp(factor, 0.9f, 1.1f);   // limit per-frame zoom step

        float factor_x = factor;
        float factor_y = factor;
        if (!rclick_zoom_3d_ && rclick_zoom_axis_ == ZoomAxis::XY)
        {
            // Blend each axis toward identity based on drag direction:
            // near-horizontal => mostly X, near-vertical => mostly Y.
            double abs_dx_total = std::abs(dx_total);
            double abs_dy_total = std::abs(dy_total);
            double sum          = abs_dx_total + abs_dy_total;
            float  x_weight     = 0.5f;
            float  y_weight     = 0.5f;
            if (sum > 1e-6)
            {
                x_weight = static_cast<float>(abs_dx_total / sum);
                y_weight = static_cast<float>(abs_dy_total / sum);
            }

            factor_x = 1.0f + (factor - 1.0f) * x_weight;
            factor_y = 1.0f + (factor - 1.0f) * y_weight;
        }

        if (rclick_zoom_3d_)
        {
            if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
            {
                if (rclick_zoom_axis_ == ZoomAxis::X)
                    axes3d->zoom_limits_x(factor);
                else if (rclick_zoom_axis_ == ZoomAxis::Y)
                    axes3d->zoom_limits_y(factor);
                else if (rclick_zoom_axis_ == ZoomAxis::Z)
                    axes3d->zoom_limits_z(factor);
                if (axis_link_mgr_)
                    axis_link_mgr_->propagate_from_3d(axes3d);
            }
        }
        else if (active_axes_)
        {
            const double anchor_x = rclick_zoom_anchor_data_x_;
            const double anchor_y = rclick_zoom_anchor_data_y_;

            if (active_axes_->is_presented_buffer_following()
                && (rclick_zoom_axis_ == ZoomAxis::X || rclick_zoom_axis_ == ZoomAxis::XY))
            {
                float seconds = active_axes_->presented_buffer_seconds();
                seconds       = std::clamp(seconds * factor_x, 0.1f, 86400.0f);
                active_axes_->presented_buffer(seconds);
            }
            else if (rclick_zoom_axis_ == ZoomAxis::X || rclick_zoom_axis_ == ZoomAxis::XY)
            {
                // Zoom anchored at drag-start X position:
                auto   xlim     = active_axes_->x_limits();
                double new_xmin = anchor_x - (anchor_x - xlim.min) * factor_x;
                double new_xmax = anchor_x + (xlim.max - anchor_x) * factor_x;
                {
                    double abs_max   = std::max(std::abs(new_xmin), std::abs(new_xmax));
                    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
                    if (min_range < 1e-300)
                        min_range = 1e-300;
                    if (new_xmax - new_xmin < min_range)
                    {
                        double mid = (new_xmin + new_xmax) * 0.5;
                        new_xmin   = mid - min_range * 0.5;
                        new_xmax   = mid + min_range * 0.5;
                    }
                }
                active_axes_->xlim(new_xmin, new_xmax);
            }

            if (rclick_zoom_axis_ == ZoomAxis::Y || rclick_zoom_axis_ == ZoomAxis::XY)
            {
                // Zoom anchored at drag-start Y position
                auto   ylim     = active_axes_->y_limits();
                double new_ymin = anchor_y - (anchor_y - ylim.min) * factor_y;
                double new_ymax = anchor_y + (ylim.max - anchor_y) * factor_y;
                {
                    double abs_max   = std::max(std::abs(new_ymin), std::abs(new_ymax));
                    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
                    if (min_range < 1e-300)
                        min_range = 1e-300;
                    if (new_ymax - new_ymin < min_range)
                    {
                        double mid = (new_ymin + new_ymax) * 0.5;
                        new_ymin   = mid - min_range * 0.5;
                        new_ymax   = mid + min_range * 0.5;
                    }
                }
                active_axes_->ylim(new_ymin, new_ymax);
            }
            if (axis_link_mgr_)
                axis_link_mgr_->propagate_limits(active_axes_,
                                                 active_axes_->x_limits(),
                                                 active_axes_->y_limits());
        }
    }
}

// ─── Pan/BoxZoom drag on mouse move ─────────────────────────────────────────

void InputHandler::handle_mouse_move_pan_drag(double x, double y)
{
    // Ctrl+drag box zoom in Pan mode: update box rect
    if (ctrl_box_zoom_active_)
    {
        box_zoom_.x1 = x;
        box_zoom_.y1 = y;
        return;
    }

    if (tool_mode_ == ToolMode::Pan)
    {
        // Track velocity for inertial pan
        last_move_x_    = x;
        last_move_y_    = y;
        last_move_time_ = Clock::now();

        // Pan logic
        const auto& vp = viewport_for_axes(active_axes_);

        // Compute drag delta in screen pixels
        double dx_screen = x - drag_start_x_;
        double dy_screen = y - drag_start_y_;

        // Convert pixel delta to data-space delta (double precision
        // to avoid catastrophic cancellation at deep zoom)
        double x_range = drag_start_xlim_max_ - drag_start_xlim_min_;
        double y_range = drag_start_ylim_max_ - drag_start_ylim_min_;

        double dx_data = -dx_screen * x_range / vp.w;
        double dy_data = dy_screen * y_range / vp.h;

        active_axes_->xlim(drag_start_xlim_min_ + dx_data, drag_start_xlim_max_ + dx_data);
        active_axes_->ylim(drag_start_ylim_min_ + dy_data, drag_start_ylim_max_ + dy_data);

        // Propagate pan to linked axes
        if (axis_link_mgr_)
        {
            axis_link_mgr_->propagate_limits(active_axes_,
                                             active_axes_->x_limits(),
                                             active_axes_->y_limits());
        }
    }
    else if (tool_mode_ == ToolMode::BoxZoom)
    {
        // Box zoom logic
        box_zoom_.x1 = x;
        box_zoom_.y1 = y;
    }
}

// ─── Scroll: 3D zoom ───────────────────────────────────────────────────────

void InputHandler::handle_scroll_3d(Axes3D* axes3d, double y_offset)
{
    auto  before_xlim = axes3d->x_limits();
    auto  before_ylim = axes3d->y_limits();
    auto  before_zlim = axes3d->z_limits();
    float factor      = (y_offset > 0) ? (1.0f - ZOOM_3D_FACTOR) : (1.0f + ZOOM_3D_FACTOR);
    axes3d->zoom_limits(factor);
    if (axis_link_mgr_)
        axis_link_mgr_->propagate_from_3d(axes3d);
    if (undo_mgr_)
    {
        auto    after_xlim = axes3d->x_limits();
        auto    after_ylim = axes3d->y_limits();
        auto    after_zlim = axes3d->z_limits();
        Axes3D* ax         = axes3d;
        undo_mgr_->push(UndoAction{"Zoom 3D",
                                   [ax, before_xlim, before_ylim, before_zlim]()
                                   {
                                       ax->xlim(before_xlim.min, before_xlim.max);
                                       ax->ylim(before_ylim.min, before_ylim.max);
                                       ax->zlim(before_zlim.min, before_zlim.max);
                                   },
                                   [ax, after_xlim, after_ylim, after_zlim]()
                                   {
                                       ax->xlim(after_xlim.min, after_xlim.max);
                                       ax->ylim(after_ylim.min, after_ylim.max);
                                       ax->zlim(after_zlim.min, after_zlim.max);
                                   }});
    }
}

// ─── Scroll: 2D zoom ───────────────────────────────────────────────────────

void InputHandler::handle_scroll_2d(double y_offset, double cursor_x, double cursor_y)
{
    Axes* hit = hit_test_axes(cursor_x, cursor_y);
    if (!hit)
        return;

    active_axes_      = hit;
    active_axes_base_ = hit;

    const auto& vp = viewport_for_axes(active_axes_);

    // Cancel any running animations — new scroll input takes priority
    if (transition_engine_)
    {
        transition_engine_->cancel_for_axes(active_axes_);
    }
    else if (anim_ctrl_)
    {
        anim_ctrl_->cancel_for_axes(active_axes_);
    }

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Compute cursor position in data space
    float saved_vp_x = vp_x_;
    float saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_;
    float saved_vp_h = vp_h_;
    vp_x_            = vp.x;
    vp_y_            = vp.y;
    vp_w_            = vp.w;
    vp_h_            = vp.h;

    double data_x = NAN;
    double data_y = NAN;
    screen_to_data(cursor_x, cursor_y, data_x, data_y);

    vp_x_ = saved_vp_x;
    vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w;
    vp_h_ = saved_vp_h;

    // Exponential zoom: symmetric in both directions.
    // scroll up (y_offset>0) → factor<1 (zoom in), scroll down → factor>1 (zoom out)
    float factor = std::pow(1.0f / (1.0f + ZOOM_FACTOR), static_cast<float>(y_offset));
    factor       = std::clamp(factor, 0.1f, 10.0f);

    // Live views keep following while the user changes the visible history
    // span with the wheel. Manual Y overrides are handled separately.
    if (active_axes_->is_presented_buffer_following() && active_axes_->has_presented_buffer())
    {
        float seconds = active_axes_->presented_buffer_seconds();
        seconds       = std::clamp(seconds * factor, 0.1f, 86400.0f);
        active_axes_->presented_buffer(seconds);
        return;
    }

    // Apply zoom instantly — scroll zoom must be immediate and responsive.
    // (Animations are used for auto-fit, box zoom, and inertial pan instead.)
    // Use double arithmetic to preserve precision at deep zoom levels.
    const double dx       = data_x;
    const double dy       = data_y;
    double       new_xmin = dx + (xlim.min - dx) * factor;
    double       new_xmax = dx + (xlim.max - dx) * factor;
    double       new_ymin = dy + (ylim.min - dy) * factor;
    double       new_ymax = dy + (ylim.max - dy) * factor;

    // Clamp to double precision limits — prevent zooming past what double can represent
    {
        double abs_max   = std::max(std::abs(new_xmin), std::abs(new_xmax));
        double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
        if (min_range < 1e-300)
            min_range = 1e-300;
        if (new_xmax - new_xmin < min_range)
        {
            double mid = (new_xmin + new_xmax) * 0.5;
            new_xmin   = mid - min_range * 0.5;
            new_xmax   = mid + min_range * 0.5;
        }
    }
    {
        double abs_max   = std::max(std::abs(new_ymin), std::abs(new_ymax));
        double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
        if (min_range < 1e-300)
            min_range = 1e-300;
        if (new_ymax - new_ymin < min_range)
        {
            double mid = (new_ymin + new_ymax) * 0.5;
            new_ymin   = mid - min_range * 0.5;
            new_ymax   = mid + min_range * 0.5;
        }
    }

    active_axes_->xlim(new_xmin, new_xmax);
    active_axes_->ylim(new_ymin, new_ymax);

    // Propagate zoom to linked axes
    if (axis_link_mgr_)
    {
        axis_link_mgr_->propagate_zoom(active_axes_, data_x, data_y, factor);
    }
}

}   // namespace spectra
