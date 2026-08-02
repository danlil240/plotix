#include "input.hpp"

#include <cmath>
#include <spectra/logger.hpp>

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

// ─── Annotate tool: place, edit, and drag annotations ───────────────────────

void InputHandler::handle_mouse_button_annotate(int button, int action, double x, double y)
{
#ifdef SPECTRA_USE_IMGUI
    if (!data_interaction_)
        return;

    // Right-click removes annotation
    if (button == MOUSE_BUTTON_RIGHT)
    {
        if (action == ACTION_PRESS)
        {
            data_interaction_->on_mouse_click_annotate(1, x, y);
        }
        return;
    }

    if (button != MOUSE_BUTTON_LEFT)
        return;

    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        // Check if we're clicking on an existing annotation to drag it
        annotate_start_x_  = x;
        annotate_start_y_  = y;
        annotate_dragging_ = false;
        data_interaction_->begin_annotation_drag(x, y);
        if (data_interaction_->is_annotation_dragging())
        {
            annotate_dragging_ = true;
            mode_              = InteractionMode::Dragging;
            return;
        }
        // Not on an existing annotation — will place on release (click)
        annotate_press_active_ = true;
    }
    else if (action == ACTION_RELEASE)
    {
        if (annotate_dragging_)
        {
            data_interaction_->end_annotation_drag();
            annotate_dragging_ = false;
            mode_              = InteractionMode::Idle;
            return;
        }
        if (annotate_press_active_)
        {
            annotate_press_active_ = false;
            // Only place if mouse didn't move much (click, not drag)
            auto            dx_px              = static_cast<float>(x - annotate_start_x_);
            auto            dy_px              = static_cast<float>(y - annotate_start_y_);
            float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
            constexpr float CLICK_THRESHOLD_PX = 5.0f;
            if (move_dist < CLICK_THRESHOLD_PX)
            {
                data_interaction_->on_mouse_click_annotate(0, x, y);
            }
        }
    }
#else
    (void)button;
    (void)action;
    (void)x;
    (void)y;
#endif
}

// ─── Annotate drag on mouse move ────────────────────────────────────────────

void InputHandler::handle_mouse_move_annotate(double x, double y)
{
#ifdef SPECTRA_USE_IMGUI
    if (data_interaction_)
    {
        data_interaction_->update_annotation_drag(x, y);
    }
#else
    (void)x;
    (void)y;
#endif
}

}   // namespace spectra
