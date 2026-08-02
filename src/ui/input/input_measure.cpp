#include "input.hpp"

#include <cmath>
#include <spectra/logger.hpp>

namespace spectra
{

namespace
{
constexpr int ACTION_PRESS   = 1;
constexpr int ACTION_RELEASE = 0;
}   // anonymous namespace

void InputHandler::restore_measure_result(Axes*  axes,
                                          double start_x,
                                          double start_y,
                                          double end_x,
                                          double end_y)
{
    measure_dragging_     = false;
    measure_click_state_  = axes ? 2 : 0;
    measure_axes_         = axes;
    measure_start_data_x_ = start_x;
    measure_start_data_y_ = start_y;
    measure_end_data_x_   = end_x;
    measure_end_data_y_   = end_y;
}

// ─── Measure tool: drag or two-click distance measurement ───────────────────

void InputHandler::handle_mouse_button_measure(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        if (measure_click_state_ == 1)
        {
            // Second click: finalize measurement at this point
            // Use measure_axes_ viewport for consistent coordinate mapping
            SPECTRA_LOG_DEBUG("input", "Measure: second click placed");
            if (measure_axes_)
            {
                Axes* saved_axes = active_axes_;
                float svx        = vp_x_;
                float svy        = vp_y_;
                float svw        = vp_w_;
                float svh        = vp_h_;
                active_axes_     = measure_axes_;
                const auto& mvp  = measure_axes_->viewport();
                vp_x_            = mvp.x;
                vp_y_            = mvp.y;
                vp_w_            = mvp.w;
                vp_h_            = mvp.h;
                screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
                active_axes_ = saved_axes;
                vp_x_        = svx;
                vp_y_        = svy;
                vp_w_        = svw;
                vp_h_        = svh;
            }
            measure_click_state_ = 2;
            return;
        }

        // First press: start measurement (could be drag or first click)
        SPECTRA_LOG_DEBUG("input", "Starting measure (press)");
        measure_dragging_       = true;
        measure_click_state_    = 0;
        measure_axes_           = active_axes_;
        measure_start_screen_x_ = x;
        measure_start_screen_y_ = y;
        screen_to_data(x, y, measure_start_data_x_, measure_start_data_y_);
        measure_end_data_x_ = measure_start_data_x_;
        measure_end_data_y_ = measure_start_data_y_;
        mode_               = InteractionMode::Dragging;
    }
    else if (action == ACTION_RELEASE && measure_dragging_)
    {
        // Use measure_axes_ viewport for consistent coordinate mapping
        if (measure_axes_)
        {
            Axes* saved_axes = active_axes_;
            float svx        = vp_x_;
            float svy        = vp_y_;
            float svw        = vp_w_;
            float svh        = vp_h_;
            active_axes_     = measure_axes_;
            const auto& mvp  = measure_axes_->viewport();
            vp_x_            = mvp.x;
            vp_y_            = mvp.y;
            vp_w_            = mvp.w;
            vp_h_            = mvp.h;
            screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
            active_axes_ = saved_axes;
            vp_x_        = svx;
            vp_y_        = svy;
            vp_w_        = svw;
            vp_h_        = svh;
        }
        measure_dragging_ = false;
        mode_             = InteractionMode::Idle;

        // Check if the mouse barely moved — treat as a click (first point)
        auto            dx_px              = static_cast<float>(x - measure_start_screen_x_);
        auto            dy_px              = static_cast<float>(y - measure_start_screen_y_);
        float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
        constexpr float CLICK_THRESHOLD_PX = 5.0f;
        if (move_dist < CLICK_THRESHOLD_PX)
        {
            // This was a click, not a drag — enter two-click mode
            SPECTRA_LOG_DEBUG("input", "Measure: first click placed (two-click mode)");
            measure_click_state_ = 1;
        }
        else
        {
            // This was a drag — measurement is complete
            SPECTRA_LOG_DEBUG("input", "Finishing measure drag");
            measure_click_state_ = 2;
        }
    }
}

// ─── Measure drag/tracking on mouse move ────────────────────────────────────

void InputHandler::handle_mouse_move_measure(double x, double y)
{
    if (!measure_axes_)
        return;

    Axes* saved_axes = active_axes_;
    float svx        = vp_x_;
    float svy        = vp_y_;
    float svw        = vp_w_;
    float svh        = vp_h_;
    active_axes_     = measure_axes_;
    const auto& mvp  = measure_axes_->viewport();
    vp_x_            = mvp.x;
    vp_y_            = mvp.y;
    vp_w_            = mvp.w;
    vp_h_            = mvp.h;
    screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
    active_axes_ = saved_axes;
    vp_x_        = svx;
    vp_y_        = svy;
    vp_w_        = svw;
    vp_h_        = svh;
}

}   // namespace spectra
