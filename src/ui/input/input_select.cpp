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
constexpr int ACTION_PRESS   = 1;
constexpr int ACTION_RELEASE = 0;
}   // anonymous namespace

// ─── Select tool: rectangle multi-select of graphs ──────────────────────────

void InputHandler::handle_mouse_button_select(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        select_start_x_     = x;
        select_start_y_     = y;
        select_rect_active_ = true;
        select_rect_.x0     = x;
        select_rect_.y0     = y;
        select_rect_.x1     = x;
        select_rect_.y1     = y;
        SPECTRA_LOG_DEBUG("input", "Starting select rectangle");
    }
    else if (action == ACTION_RELEASE && select_rect_active_)
    {
        auto            dx_px              = static_cast<float>(x - select_start_x_);
        auto            dy_px              = static_cast<float>(y - select_start_y_);
        float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
        constexpr float CLICK_THRESHOLD_PX = 5.0f;

        if (move_dist < CLICK_THRESHOLD_PX)
        {
            // Click (not drag): single series select/deselect
#ifdef SPECTRA_USE_IMGUI
            if (data_interaction_)
                data_interaction_->on_mouse_click_series_only(x, y);
#endif
        }
        else
        {
            // Drag finished: select all series intersecting the rectangle
            SPECTRA_LOG_DEBUG("input", "Finishing select rectangle");
#ifdef SPECTRA_USE_IMGUI
            if (data_interaction_ && figure_)
                data_interaction_->select_series_in_rect(select_rect_, *figure_);
#endif
        }
        select_rect_active_ = false;
    }
}

// ─── ROI tool: region-of-interest selection ─────────────────────────────────

void InputHandler::handle_mouse_button_roi(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
        {
            select_start_x_ = x;
            select_start_y_ = y;
            SPECTRA_LOG_DEBUG("input", "Starting ROI region selection");
            data_interaction_->begin_region_select(x, y);
            region_dragging_ = true;
        }
#endif
    }
    else if (action == ACTION_RELEASE && region_dragging_)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
        {
            auto            dx_px              = static_cast<float>(x - select_start_x_);
            auto            dy_px              = static_cast<float>(y - select_start_y_);
            float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
            constexpr float CLICK_THRESHOLD_PX = 5.0f;

            if (move_dist < CLICK_THRESHOLD_PX)
            {
                // ROI click: dismiss
                data_interaction_->dismiss_region_select();
            }
            else
            {
                SPECTRA_LOG_DEBUG("input", "Finishing ROI region selection");
                data_interaction_->finish_region_select();
            }
        }
#endif
        region_dragging_ = false;
    }
}

// ─── Select rect drag on mouse move ─────────────────────────────────────────

void InputHandler::handle_mouse_move_select(double x, double y)
{
    select_rect_.x1 = x;
    select_rect_.y1 = y;
}

// ─── ROI region drag on mouse move ──────────────────────────────────────────

void InputHandler::handle_mouse_move_roi(double x, double y)
{
#ifdef SPECTRA_USE_IMGUI
    if (data_interaction_)
    {
        data_interaction_->update_region_drag(x, y);
    }
#endif
}

}   // namespace spectra
