#include "layout_manager.hpp"

#include <algorithm>
#include <cmath>

#include <spectra/animator.hpp>

#include "ui/theme/design_tokens.hpp"

namespace spectra
{

LayoutManager::LayoutManager()
{
    compute_zones();
}

float LayoutManager::smooth_toward(float current, float target, float speed, float dt)
{
    if (dt <= 0.0f)
        return target;   // No dt → snap instantly
    float diff = target - current;
    if (std::abs(diff) < 0.5f)
        return target;   // Close enough → snap
    return current + diff * std::min(1.0f, speed * dt);
}

void LayoutManager::update_inspector_animation(float dt)
{
    const float target_t = inspector_visible_ ? 1.0f : 0.0f;

    if (resize_active_ || dt <= 0.0f)
    {
        inspector_anim_t_     = target_t;
        inspector_anim_width_ = inspector_visible_ ? inspector_width_ : 0.0f;
        return;
    }

    if (std::abs(inspector_anim_t_ - target_t) > 0.001f)
    {
        const bool  opening = target_t > inspector_anim_t_;
        const float duration =
            opening ? ui::tokens::DURATION_INSPECTOR_OPEN : ui::tokens::DURATION_INSPECTOR_CLOSE;
        // Cap per-frame progress so a large DeltaTime spike cannot snap the panel.
        constexpr float kMaxProgressStep = 0.25f;
        const float     step             = std::min(dt / duration, kMaxProgressStep);
        if (opening)
            inspector_anim_t_ = std::min(target_t, inspector_anim_t_ + step);
        else
            inspector_anim_t_ = std::max(target_t, inspector_anim_t_ - step);
    }
    else
    {
        inspector_anim_t_ = target_t;
    }

    // ease-out when opening (fast start), ease-in feel when closing (slow start).
    const float eased_t   = ease::ease_out(inspector_anim_t_);
    inspector_anim_width_ = inspector_width_ * eased_t;
}

void LayoutManager::update(float window_width, float window_height, float dt)
{
    window_width_  = window_width;
    window_height_ = window_height;

    // Compute animation targets
    float nav_rail_target = 0.0f;
    if (nav_rail_visible_)
    {
        nav_rail_target = nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
    }

    // Animate toward targets (snap instantly during live resize so chrome zones
    // don't lag behind the swapchain and expose the plot background underneath).
    if (resize_active_)
    {
        nav_rail_anim_width_ = nav_rail_target;
    }
    else
    {
        nav_rail_anim_width_ = smooth_toward(nav_rail_anim_width_, nav_rail_target, ANIM_SPEED, dt);
    }
    update_inspector_animation(dt);

    compute_zones();
}

void LayoutManager::compute_zones()
{
    command_bar_rect_ = compute_command_bar();
    nav_rail_rect_    = compute_nav_rail();
    inspector_rect_   = compute_inspector();
    status_bar_rect_  = compute_status_bar();
    tab_bar_rect_     = compute_tab_bar();
    workspace_rect_   = compute_workspace();
    canvas_rect_      = canvas_override_set_ ? canvas_override_ : workspace_rect_;
}

Rect LayoutManager::compute_command_bar() const
{
    return Rect{0.0f, 0.0f, window_width_, COMMAND_BAR_HEIGHT};
}

Rect LayoutManager::compute_nav_rail() const
{
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{0.0f, COMMAND_BAR_HEIGHT, nav_rail_anim_width_, std::max(0.0f, content_h)};
}

Rect LayoutManager::compute_inspector() const
{
    if (inspector_anim_width_ < 1.0f)
    {
        return Rect{window_width_, COMMAND_BAR_HEIGHT, 0.0f, 0.0f};
    }
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{window_width_ - inspector_anim_width_,
                COMMAND_BAR_HEIGHT,
                inspector_anim_width_,
                std::max(0.0f, content_h)};
}

Rect LayoutManager::compute_status_bar() const
{
    return Rect{0.0f, window_height_ - STATUS_BAR_HEIGHT, window_width_, STATUS_BAR_HEIGHT};
}

Rect LayoutManager::compute_tab_bar() const
{
    if (!tab_bar_visible_)
    {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    float x = nav_rail_anim_width_ + PLOT_LEFT_MARGIN;
    float w = window_width_ - x - inspector_anim_width_;
    return Rect{x, COMMAND_BAR_HEIGHT, std::max(0.0f, w), TAB_BAR_HEIGHT};
}

Rect LayoutManager::workspace_rect() const
{
    return workspace_rect_;
}

Rect LayoutManager::compute_workspace() const
{
    float x = nav_rail_anim_width_;
    float w = window_width_ - nav_rail_anim_width_ - inspector_anim_width_;
    float y = COMMAND_BAR_HEIGHT;

    float h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT - bottom_panel_height_;

    // Offset canvas below tab bar when visible
    if (tab_bar_visible_)
    {
        y += TAB_BAR_HEIGHT;
        h -= TAB_BAR_HEIGHT;
    }

    return Rect{x, y, std::max(0.0f, w), std::max(0.0f, h)};
}

Rect LayoutManager::compute_canvas() const
{
    if (canvas_override_set_)
        return canvas_override_;
    return compute_workspace();
}

// Zone rectangle getters
Rect LayoutManager::command_bar_rect() const
{
    return command_bar_rect_;
}
Rect LayoutManager::nav_rail_rect() const
{
    return nav_rail_rect_;
}
Rect LayoutManager::canvas_rect() const
{
    return canvas_rect_;
}
Rect LayoutManager::inspector_rect() const
{
    return inspector_rect_;
}
Rect LayoutManager::status_bar_rect() const
{
    return status_bar_rect_;
}

Rect LayoutManager::tab_bar_rect() const
{
    return tab_bar_rect_;
}

float LayoutManager::nav_rail_width() const
{
    if (!nav_rail_visible_)
        return 0.0f;
    return nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
}

bool LayoutManager::is_animating() const
{
    const float inspector_target_t = inspector_visible_ ? 1.0f : 0.0f;
    float       nav_target = nav_rail_visible_ ? (nav_rail_expanded_ ? nav_rail_expanded_width_
                                                                     : nav_rail_collapsed_width_)
                                               : 0.0f;
    return std::abs(inspector_anim_t_ - inspector_target_t) > 0.001f
           || std::abs(nav_rail_anim_width_ - nav_target) > 0.5f;
}

float LayoutManager::inspector_open_amount() const
{
    if (inspector_width_ <= 0.0f)
        return 0.0f;
    return std::clamp(inspector_anim_width_ / inspector_width_, 0.0f, 1.0f);
}

// Configuration methods
void LayoutManager::set_inspector_visible(bool visible)
{
    inspector_visible_ = visible;
}

void LayoutManager::set_inspector_width(float width)
{
    inspector_width_ = std::clamp(width, INSPECTOR_MIN_WIDTH, INSPECTOR_MAX_WIDTH);
    // During active drag, snap the animated width to avoid lag
    if (inspector_resize_active_ && inspector_visible_)
    {
        inspector_anim_t_     = 1.0f;
        inspector_anim_width_ = inspector_width_;
        compute_zones();
    }
}

void LayoutManager::reset_inspector_width()
{
    inspector_width_ = INSPECTOR_DEFAULT_WIDTH;
}

void LayoutManager::set_nav_rail_width(float width)
{
    nav_rail_expanded_width_ = std::max(width, NAV_RAIL_COLLAPSED_WIDTH);
}

void LayoutManager::set_nav_rail_visible(bool visible)
{
    nav_rail_visible_ = visible;
}

void LayoutManager::set_nav_rail_expanded(bool expanded)
{
    nav_rail_expanded_ = expanded;
}

void LayoutManager::set_tab_bar_visible(bool visible)
{
    tab_bar_visible_ = visible;
}

float LayoutManager::nav_rail_content_height(int   button_count,
                                             int   separator_count,
                                             float cell_scale)
{
    return NAV_RAIL_VERTICAL_PADDING
           + static_cast<float>(button_count) * NAV_RAIL_CELL_HEIGHT * cell_scale
           + static_cast<float>(separator_count) * NAV_RAIL_SEPARATOR_HEIGHT * cell_scale;
}

float LayoutManager::nav_rail_nominal_content_height()
{
    return nav_rail_content_height(NAV_RAIL_BUTTON_COUNT, NAV_RAIL_SEPARATOR_COUNT, 1.0f);
}

float LayoutManager::nav_rail_min_content_height()
{
    const float scale = NAV_RAIL_CELL_HEIGHT_MIN / NAV_RAIL_CELL_HEIGHT;
    return nav_rail_content_height(NAV_RAIL_BUTTON_COUNT, NAV_RAIL_SEPARATOR_COUNT, scale);
}

float LayoutManager::nav_rail_scale_for_height(float available_height,
                                               int   button_count,
                                               int   separator_count)
{
    const float nominal   = nav_rail_content_height(button_count, separator_count, 1.0f);
    const float min_scale = NAV_RAIL_CELL_HEIGHT_MIN / NAV_RAIL_CELL_HEIGHT;
    if (available_height >= nominal)
        return 1.0f;
    const float min_h = nav_rail_content_height(button_count, separator_count, min_scale);
    if (available_height <= min_h)
        return min_scale;
    return std::max(min_scale, available_height / nominal);
}

float LayoutManager::nav_rail_scale_for_height(float available_height)
{
    return nav_rail_scale_for_height(available_height,
                                     NAV_RAIL_BUTTON_COUNT,
                                     NAV_RAIL_SEPARATOR_COUNT);
}

float LayoutManager::min_window_height(bool nav_rail_visible,
                                       bool command_bar_visible,
                                       bool status_bar_visible)
{
    float h = 0.0f;
    if (command_bar_visible)
        h += COMMAND_BAR_HEIGHT;
    if (status_bar_visible)
        h += STATUS_BAR_HEIGHT;
    if (nav_rail_visible)
        h += nav_rail_min_content_height();
    else
        h = std::max(h, WINDOW_MIN_HEIGHT_NO_NAV);
    return h;
}

float LayoutManager::min_window_width(bool nav_rail_visible)
{
    float w = WINDOW_MIN_CANVAS_WIDTH;
    if (nav_rail_visible)
        w += NAV_RAIL_COLLAPSED_WIDTH;
    return w;
}

}   // namespace spectra
