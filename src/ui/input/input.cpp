#include "input.hpp"

#include <cmath>

#include <algorithm>
#include <format>
#include <limits>
#include <spectra/logger.hpp>

#include "gesture_recognizer.hpp"
#include "ui/ui_interaction_log.hpp"
#include "ui/overlay/axes3d_axis_pick.hpp"
#include "ui/animation/animation_controller.hpp"
#include "ui/animation/transition_engine.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/data/axis_link.hpp"
#ifdef SPECTRA_USE_IMGUI
    #include "ui/overlay/data_interaction.hpp"
#endif
#include "ui/viewmodel/axes_view_model.hpp"
#include "ui/viewmodel/figure_view_model.hpp"

namespace spectra
{

// Mouse button constants (matching GLFW)
namespace
{
constexpr int MOUSE_BUTTON_LEFT  = 0;
constexpr int MOUSE_BUTTON_RIGHT = 1;
constexpr int ACTION_PRESS       = 1;
constexpr int ACTION_RELEASE     = 0;
}   // anonymous namespace

// ─── Tool mode ──────────────────────────────────────────────────────────────

void InputHandler::set_tool_mode(ToolMode new_tool)
{
    if (new_tool == tool_mode_)
        return;

    // Leaving Select mode: cancel any select rectangle
    if (tool_mode_ == ToolMode::Select)
    {
        select_rect_active_ = false;
    }

    // Leaving ROI mode: dismiss region selection
    if (tool_mode_ == ToolMode::ROI)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
            data_interaction_->dismiss_region_select();
#endif
        region_dragging_ = false;
    }

    // Leaving Measure mode: restore previous crosshair state, reset measure
    if (tool_mode_ == ToolMode::Measure)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
            data_interaction_->set_crosshair(crosshair_was_active_);
#endif
        measure_dragging_    = false;
        measure_click_state_ = 0;
        measure_axes_        = nullptr;
    }

    // Leaving Annotate mode: cancel any active editing, reset state
    if (tool_mode_ == ToolMode::Annotate)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
            data_interaction_->annotations().cancel_editing();
#endif
        annotate_dragging_     = false;
        annotate_press_active_ = false;
    }

    // Entering Measure mode: auto-enable crosshair
    if (new_tool == ToolMode::Measure)
    {
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
        {
            crosshair_was_active_ = data_interaction_->crosshair_active();
            data_interaction_->set_crosshair(true);
        }
#endif
    }

    tool_mode_ = new_tool;
}

// ─── Interaction bounds ─────────────────────────────────────────────────────

void InputHandler::set_interaction_rect(const Rect& r)
{
    interaction_rect_     = r;
    interaction_rect_set_ = r.w > 0.0f && r.h > 0.0f;
}

bool InputHandler::point_in_interaction_rect(double x, double y) const
{
    if (!interaction_rect_set_)
        return true;
    const Rect& r = interaction_rect_;
    return static_cast<float>(x) >= r.x && static_cast<float>(x) < r.x + r.w
           && static_cast<float>(y) >= r.y && static_cast<float>(y) < r.y + r.h;
}

// ─── Hit-testing ────────────────────────────────────────────────────────────

Axes* InputHandler::hit_test_axes(double screen_x, double screen_y) const
{
    if (!figure_)
        return nullptr;

    for (auto& axes_ptr : figure_->axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x && static_cast<float>(screen_x) <= vp.x + vp.w
            && static_cast<float>(screen_y) >= vp.y && static_cast<float>(screen_y) <= vp.y + vp.h)
        {
            return axes_ptr.get();
        }
    }
    return nullptr;
}

const Rect& InputHandler::viewport_for_axes(const AxesBase* axes) const
{
    if (axes)
    {
        return axes->viewport();
    }
    // Fallback: return a static rect built from stored viewport values
    static Rect fallback;
    fallback = {vp_x_, vp_y_, vp_w_, vp_h_};
    return fallback;
}

AxesBase* InputHandler::hit_test_all_axes(double screen_x, double screen_y) const
{
    if (!figure_)
        return nullptr;

    // Search 3D axes first (all_axes_ holds only 3D)
    for (auto& axes_ptr : figure_->all_axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x && static_cast<float>(screen_x) <= vp.x + vp.w
            && static_cast<float>(screen_y) >= vp.y && static_cast<float>(screen_y) <= vp.y + vp.h)
        {
            return axes_ptr.get();
        }
    }
    // Search 2D axes (axes_ holds only 2D)
    for (auto& axes_ptr : figure_->axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x && static_cast<float>(screen_x) <= vp.x + vp.w
            && static_cast<float>(screen_y) >= vp.y && static_cast<float>(screen_y) <= vp.y + vp.h)
        {
            return axes_ptr.get();
        }
    }
    return nullptr;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

InputHandler::InputHandler()  = default;
InputHandler::~InputHandler() = default;

// Phase 2 (LT-5): Route limit mutations through AxesViewModel when available.
void InputHandler::set_axes_xlim(Axes* ax, double min, double max)
{
    if (!ax)
        return;
    if (figure_vm_)
    {
        auto& avm = figure_vm_->get_or_create_axes_vm(ax);
        avm.set_visual_xlim(min, max);
    }
    else
    {
        ax->xlim(min, max);
    }
}

void InputHandler::set_axes_ylim(Axes* ax, double min, double max)
{
    if (!ax)
        return;
    if (figure_vm_)
    {
        auto& avm = figure_vm_->get_or_create_axes_vm(ax);
        avm.set_visual_ylim(min, max);
    }
    else
    {
        ax->ylim(min, max);
    }
}

void InputHandler::clear_figure_cache(Figure* fig)
{
    if (!fig || figure_ == fig)
    {
        // Cancel any in-flight animations that hold raw Axes* pointers
        // to axes owned by the figure being destroyed.  Without this,
        // AnimationController::update() would dereference freed memory.
        if (anim_ctrl_)
        {
            if (fig)
            {
                for (const auto& ax : fig->axes())
                {
                    if (ax)
                        anim_ctrl_->cancel_for_axes(ax.get());
                }
            }
            else
            {
                anim_ctrl_->cancel_all();
            }
        }

        figure_           = nullptr;
        active_axes_      = nullptr;
        active_axes_base_ = nullptr;
        drag3d_axes_      = nullptr;
        measure_axes_     = nullptr;
        mode_             = InteractionMode::Idle;
        is_3d_orbit_drag_ = false;
        is_3d_pan_drag_   = false;
    }
}

// ─── Mouse button ───────────────────────────────────────────────────────────

void InputHandler::on_mouse_button(int button, int action, int mods, double x, double y)
{
    // Update modifier state from the authoritative GLFW mods bitmask
    mods_ = mods;

    if (action == ACTION_PRESS)
    {
        ui::log_ui_action("input",
                          std::to_string(button),
                          "ok",
                          std::format("x={:.0f} y={:.0f}", x, y));
    }

    // Validate cached axes pointers still belong to the current figure.
    // They can become dangling when figures are closed or moved between windows.
    if (active_axes_base_ && figure_)
    {
        bool found = false;
        for (const auto& ax : figure_->axes())
        {
            if (ax.get() == active_axes_base_)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (const auto& ax : figure_->all_axes())
            {
                if (ax.get() == active_axes_base_)
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            active_axes_base_ = nullptr;
            active_axes_      = nullptr;
            drag3d_axes_      = nullptr;
        }
    }
    else if (active_axes_base_ && !figure_)
    {
        active_axes_base_ = nullptr;
        active_axes_      = nullptr;
        drag3d_axes_      = nullptr;
    }

    // Hit-test all axes (including 3D) — but only on PRESS when no drag is
    // active.  During an active drag the axes must stay locked to the one that
    // was clicked, otherwise moving the cursor over a different subplot would
    // swap the target axes mid-drag (causing jumps in 2D and broken release
    // handling in 3D).
    bool in_active_drag = is_3d_orbit_drag_ || is_3d_pan_drag_ || rclick_zoom_dragging_
                          || mode_ == InteractionMode::Dragging || middle_pan_dragging_;

    // Plot interactions may only start inside the layout canvas (not chrome).
    if (!in_active_drag && !point_in_interaction_rect(x, y))
    {
        if (action == ACTION_PRESS)
        {
            active_axes_      = nullptr;
            active_axes_base_ = nullptr;
        }
        return;
    }

    if (!in_active_drag)
    {
        AxesBase* hit_base = hit_test_all_axes(x, y);
        active_axes_base_  = hit_base;
        active_axes_       = dynamic_cast<Axes*>(hit_base);
        if (hit_base)
        {
            const auto& vp = hit_base->viewport();
            vp_x_          = vp.x;
            vp_y_          = vp.y;
            vp_w_          = vp.w;
            vp_h_          = vp.h;
        }

        // Plot pan/zoom/tools only start from a press inside the axes viewport
        // (the inner data area — not title/label gutters or other chrome).
        if (action == ACTION_PRESS && !hit_base)
            return;
    }

    // Handle 3D axes camera interaction
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        handle_mouse_button_3d(axes3d, button, action, x, y);
        return;   // 3D axes don't support other interactions
    }

    if (!active_axes_)
        return;

    // Middle-mouse pan: works in ALL tool modes for 2D axes
    if (button == MOUSE_BUTTON_MIDDLE)
    {
        handle_mouse_button_middle_pan(action, x, y);
        return;
    }

    // Right-click drag zoom:
    // - near horizontal/vertical drags: 1D axis zoom (X or Y)
    // - diagonal drags: 2D slant zoom (X and Y together)
    if (button == MOUSE_BUTTON_RIGHT)
    {
        if (action == ACTION_PRESS && !rclick_zoom_dragging_ && active_axes_)
        {
            // Annotate mode: right-click removes annotation
#ifdef SPECTRA_USE_IMGUI
            if (tool_mode_ == ToolMode::Annotate && data_interaction_
                && data_interaction_->on_mouse_click_annotate(1, x, y))
            {
                return;
            }

            // Let DataInteraction handle right-click first (marker remove).
            // If a marker was removed, don't start zoom drag.
            if (data_interaction_ && data_interaction_->on_mouse_click_datatip_only(1, x, y))
            {
                // Marker was removed — don't start zoom drag
                return;
            }
#endif

            handle_mouse_button_rclick_zoom(action, x, y);
            return;
        }
        if (action == ACTION_RELEASE && rclick_zoom_dragging_ && !rclick_zoom_3d_)
        {
            handle_mouse_button_rclick_zoom(action, x, y);
            return;
        }
    }

    // Measure mode: left-click/drag to measure distance between two data points
    // Supports both drag (press-move-release) and two-click (click, move, click)
    if (button == MOUSE_BUTTON_LEFT && tool_mode_ == ToolMode::Measure)
    {
        handle_mouse_button_measure(action, x, y);
        return;
    }

    if (button == MOUSE_BUTTON_LEFT)
    {
        // Select mode: left-drag for rectangle multi-select of graphs
        if (tool_mode_ == ToolMode::Select)
        {
            handle_mouse_button_select(action, x, y);
            return;
        }

        // ROI mode: left-drag for region analysis
        if (tool_mode_ == ToolMode::ROI)
        {
            handle_mouse_button_roi(action, x, y);
            return;
        }

        // Annotate mode: left-click to place/edit annotations, drag to reposition
        if (tool_mode_ == ToolMode::Annotate)
        {
            handle_mouse_button_annotate(button, action, x, y);
            return;
        }

        // BoxZoom tool mode: left-click to draw box zoom rectangle
        if (tool_mode_ == ToolMode::BoxZoom)
        {
            handle_mouse_button_box_zoom(action, x, y);
            return;
        }

        // Pan tool mode: left-click to pan, double-click to auto-fit, Ctrl+click for box zoom
        if (tool_mode_ == ToolMode::Pan)
        {
            handle_mouse_button_pan(action, mods, x, y);
            return;
        }
    }
}

// ─── Mouse move ─────────────────────────────────────────────────────────────

void InputHandler::clear_cursor_readout()
{
    cursor_readout_.valid = false;
}

void InputHandler::on_mouse_move(double x, double y)
{
    SPECTRA_LOG_TRACE(
        "input",
        "Mouse move event - pos: (" + std::to_string(x) + ", " + std::to_string(y) + ")");

    // Handle 3D camera drag (orbit or pan)
    if (is_3d_orbit_drag_ || is_3d_pan_drag_)
    {
        handle_mouse_move_3d_drag(x, y);
        return;
    }

    // Handle right-click drag zoom (both 2D and 3D)
    if (rclick_zoom_dragging_)
    {
        handle_mouse_move_rclick_zoom(x, y);
        return;
    }

    // Annotate mode: update drag position
    if (annotate_dragging_)
    {
        handle_mouse_move_annotate(x, y);
        return;
    }

    // Update cursor readout regardless of mode (2D and 3D axes)
    AxesBase* hit_base = hit_test_all_axes(x, y);
    if (hit_base)
    {
        SPECTRA_LOG_TRACE("input", "Mouse move hit axes");
        Axes*       hit2d      = dynamic_cast<Axes*>(hit_base);
        Axes*       prev       = active_axes_;
        AxesBase*   prev_base  = active_axes_base_;
        const auto& vp         = viewport_for_axes(hit_base);
        float       saved_vp_x = vp_x_;
        float       saved_vp_y = vp_y_;
        float       saved_vp_w = vp_w_;
        float       saved_vp_h = vp_h_;
        vp_x_                  = vp.x;
        vp_y_                  = vp.y;
        vp_w_                  = vp.w;
        vp_h_                  = vp.h;
        active_axes_base_      = hit_base;
        active_axes_           = hit2d;

        cursor_readout_.valid    = true;
        cursor_readout_.screen_x = x;
        cursor_readout_.screen_y = y;
        if (hit2d)
            screen_to_data(x, y, cursor_readout_.data_x, cursor_readout_.data_y);

        // Restore if we were in a drag with a different axes
        // (includes middle-mouse pan, measure drag, and measure two-click mode)
        if (mode_ == InteractionMode::Dragging || middle_pan_dragging_ || measure_dragging_
            || (measure_click_state_ == 1 && tool_mode_ == ToolMode::Measure))
        {
            active_axes_      = prev;
            active_axes_base_ = prev_base;
            vp_x_             = saved_vp_x;
            vp_y_             = saved_vp_y;
            vp_w_             = saved_vp_w;
            vp_h_             = saved_vp_h;
        }
        else
        {
            // In idle mode, update active axes to hovered one
            active_axes_base_ = hit_base;
            active_axes_      = hit2d;
            vp_x_             = vp.x;
            vp_y_             = vp.y;
            vp_w_             = vp.w;
            vp_h_             = vp.h;
        }
    }
    else
    {
        cursor_readout_.valid    = false;
        cursor_readout_.screen_x = x;
        cursor_readout_.screen_y = y;

        const bool in_active_drag = is_3d_orbit_drag_ || is_3d_pan_drag_ || rclick_zoom_dragging_
                                    || mode_ == InteractionMode::Dragging || middle_pan_dragging_;
        const bool keep_axes_for_measure =
            tool_mode_ == ToolMode::Measure && measure_click_state_ == 1;
        if (!in_active_drag && !keep_axes_for_measure)
        {
            active_axes_      = nullptr;
            active_axes_base_ = nullptr;
        }
    }

    update_axes3d_arrow_hover(hit_base, x, y);

    if (!active_axes_)
        return;

    // Middle-mouse pan (works in all tool modes)
    if (middle_pan_dragging_ && active_axes_)
    {
        const auto& vp        = viewport_for_axes(active_axes_);
        double      dx_screen = x - middle_pan_start_x_;
        double      dy_screen = y - middle_pan_start_y_;
        double      x_range   = middle_pan_xlim_max_ - middle_pan_xlim_min_;
        double      y_range   = middle_pan_ylim_max_ - middle_pan_ylim_min_;
        double      dx_data   = -dx_screen * x_range / vp.w;
        double      dy_data   = dy_screen * y_range / vp.h;
        set_axes_xlim(active_axes_, middle_pan_xlim_min_ + dx_data, middle_pan_xlim_max_ + dx_data);
        set_axes_ylim(active_axes_, middle_pan_ylim_min_ + dy_data, middle_pan_ylim_max_ + dy_data);
        if (axis_link_mgr_)
            axis_link_mgr_->propagate_limits(active_axes_,
                                             active_axes_->x_limits(),
                                             active_axes_->y_limits());
        // Don't return — allow cursor readout and other overlays to update too
    }

    // Update measure drag (Measure mode)
    if (measure_dragging_ && tool_mode_ == ToolMode::Measure)
    {
        handle_mouse_move_measure(x, y);
        return;
    }

    // Update measure endpoint in two-click mode (first point placed, tracking cursor)
    if (measure_click_state_ == 1 && tool_mode_ == ToolMode::Measure)
    {
        handle_mouse_move_measure(x, y);
        // Don't return — allow cursor readout to update
    }

    // Update select rectangle drag (Select mode)
    if (select_rect_active_ && tool_mode_ == ToolMode::Select)
    {
        handle_mouse_move_select(x, y);
        return;
    }

    // Update region selection drag (ROI mode)
    if (region_dragging_ && tool_mode_ == ToolMode::ROI)
    {
        handle_mouse_move_roi(x, y);
        return;
    }

    if (mode_ == InteractionMode::Dragging)
    {
        handle_mouse_move_pan_drag(x, y);
    }
}

// ─── Scroll ─────────────────────────────────────────────────────────────────

void InputHandler::on_scroll(double /*x_offset*/, double y_offset, double cursor_x, double cursor_y)
{
    if (!point_in_interaction_rect(cursor_x, cursor_y))
        return;

    // Validate cached axes pointers still belong to the current figure.
    // They can become dangling when figures are closed or moved between windows.
    if (active_axes_base_ && figure_)
    {
        bool found = false;
        for (const auto& ax : figure_->axes())
        {
            if (ax.get() == active_axes_base_)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (const auto& ax : figure_->all_axes())
            {
                if (ax.get() == active_axes_base_)
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            active_axes_base_ = nullptr;
            active_axes_      = nullptr;
            drag3d_axes_      = nullptr;
        }
    }
    else if (active_axes_base_ && !figure_)
    {
        active_axes_base_ = nullptr;
        active_axes_      = nullptr;
        drag3d_axes_      = nullptr;
    }

    // Hit-test all axes (including 3D) for scroll zoom — cursor must be over the
    // plot data viewport, not a stale axes from a prior hover.
    AxesBase* hit_base = hit_test_all_axes(cursor_x, cursor_y);
    if (hit_base)
    {
        active_axes_base_ = hit_base;
        active_axes_      = nullptr;
        if (figure_)
        {
            for (const auto& ax : figure_->axes())
            {
                if (ax.get() == hit_base)
                {
                    active_axes_ = ax.get();
                    break;
                }
            }
        }
    }
    else if (figure_ && figure_->needs_scroll(visible_height_) && visible_height_ > 0.0f)
    {
        // Cursor is not over any axes and the figure has scrollable overflow.
        // Scroll the page instead of zooming.
        constexpr float SCROLL_SPEED = 40.0f;
        float new_offset = figure_->scroll_offset_y() - static_cast<float>(y_offset) * SCROLL_SPEED;
        float max_scroll = std::max(0.0f, figure_->content_height() - visible_height_);
        new_offset       = std::clamp(new_offset, 0.0f, max_scroll);
        figure_->set_scroll_offset_y(new_offset);
        return;
    }
    else
    {
        return;
    }

    // Handle 3D zoom by scaling axis limits (box stays fixed visual size)
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        handle_scroll_3d(axes3d, y_offset);
        return;
    }

    handle_scroll_2d(y_offset, cursor_x, cursor_y);
}

// ─── Keyboard ───────────────────────────────────────────────────────────────

void InputHandler::on_key(int key, int action, int mods)
{
    // Track modifier state for use in mouse callbacks
    mods_ = mods;

    // Delegate to ShortcutManager first — if it handles the key, we're done
    if (shortcut_mgr_ && shortcut_mgr_->on_key(key, action, mods))
    {
        return;
    }

    if (action != ACTION_PRESS)
        return;

    if (key == KEY_ESCAPE)
    {
        // Cancel box zoom if active
        cancel_box_zoom();
        // Dismiss region selection if active
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_ && data_interaction_->has_region_selection())
        {
            data_interaction_->dismiss_region_select();
        }
#endif
        return;
    }

    if (key == KEY_R && !(mods & MOD_CONTROL))
    {
        // Reset view: animated auto-fit all axes in the figure (2D and 3D)
        // Note: subplot() populates axes() (2D only); subplot3d() populates
        // all_axes() (3D only). Both must be iterated.
        if (figure_)
        {
            for (auto& axes_ptr : figure_->axes())
            {
                if (!axes_ptr)
                    continue;
                if (transition_engine_ || anim_ctrl_)
                {
                    auto old_xlim = axes_ptr->x_limits();
                    auto old_ylim = axes_ptr->y_limits();
                    axes_ptr->auto_fit();
                    AxisLimits target_x = axes_ptr->x_limits();
                    AxisLimits target_y = axes_ptr->y_limits();
                    set_axes_xlim(axes_ptr.get(), old_xlim.min, old_xlim.max);
                    set_axes_ylim(axes_ptr.get(), old_ylim.min, old_ylim.max);
                    if (transition_engine_)
                    {
                        transition_engine_->animate_limits(*axes_ptr,
                                                           target_x,
                                                           target_y,
                                                           AUTOFIT_ANIM_DURATION,
                                                           ease::ease_out);
                    }
                    else
                    {
                        anim_ctrl_->animate_axis_limits(*axes_ptr,
                                                        target_x,
                                                        target_y,
                                                        AUTOFIT_ANIM_DURATION,
                                                        ease::ease_out);
                    }
                }
                else
                {
                    axes_ptr->auto_fit();
                }
            }
            for (auto& axes_base_ptr : figure_->all_axes())
            {
                if (!axes_base_ptr)
                    continue;
                if (auto* axes3d = dynamic_cast<Axes3D*>(axes_base_ptr.get()))
                    axes3d->auto_fit();
            }
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
        {
            axes3d->auto_fit();
        }
        else if (active_axes_)
        {
            if (transition_engine_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                set_axes_xlim(active_axes_, old_xlim.min, old_xlim.max);
                set_axes_ylim(active_axes_, old_ylim.min, old_ylim.max);
                transition_engine_->animate_limits(*active_axes_,
                                                   target_x,
                                                   target_y,
                                                   AUTOFIT_ANIM_DURATION,
                                                   ease::ease_out);
            }
            else if (anim_ctrl_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                set_axes_xlim(active_axes_, old_xlim.min, old_xlim.max);
                set_axes_ylim(active_axes_, old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(*active_axes_,
                                                target_x,
                                                target_y,
                                                AUTOFIT_ANIM_DURATION,
                                                ease::ease_out);
            }
            else
            {
                active_axes_->auto_fit();
            }
        }
        return;
    }

    if (key == KEY_G && !(mods & MOD_CONTROL))
    {
        // Toggle grid on active axes
        if (active_axes_)
        {
            active_axes_->grid(!active_axes_->grid_enabled());
        }
        return;
    }

    if (key == KEY_S && (mods & MOD_CONTROL))
    {
        // Ctrl+S: save PNG
        if (save_callback_)
        {
            save_callback_();
        }
        return;
    }

    if (key == KEY_C && !(mods & MOD_CONTROL))
    {
        // Toggle crosshair overlay
#ifdef SPECTRA_USE_IMGUI
        if (data_interaction_)
        {
            data_interaction_->toggle_crosshair();
            SPECTRA_LOG_DEBUG(
                "input",
                "Crosshair toggled: "
                    + std::string(data_interaction_->crosshair_active() ? "ON" : "OFF"));
        }
#endif
        return;
    }

    if (key == KEY_A && !(mods & MOD_CONTROL))
    {
        // Animated auto-fit active axes only
        if (active_axes_)
        {
            if (transition_engine_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                set_axes_xlim(active_axes_, old_xlim.min, old_xlim.max);
                set_axes_ylim(active_axes_, old_ylim.min, old_ylim.max);
                transition_engine_->animate_limits(*active_axes_,
                                                   target_x,
                                                   target_y,
                                                   AUTOFIT_ANIM_DURATION,
                                                   ease::ease_out);
            }
            else if (anim_ctrl_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                set_axes_xlim(active_axes_, old_xlim.min, old_xlim.max);
                set_axes_ylim(active_axes_, old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(*active_axes_,
                                                target_x,
                                                target_y,
                                                AUTOFIT_ANIM_DURATION,
                                                ease::ease_out);
            }
            else
            {
                active_axes_->auto_fit();
            }
        }
        return;
    }
}

// ─── Box zoom ───────────────────────────────────────────────────────────────

void InputHandler::apply_box_zoom()
{
    if (!active_axes_ || !box_zoom_.active)
    {
        cancel_box_zoom();
        return;
    }

    const auto& vp = viewport_for_axes(active_axes_);

    // Convert box corners from screen to data space
    float saved_vp_x = vp_x_;
    float saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_;
    float saved_vp_h = vp_h_;
    vp_x_            = vp.x;
    vp_y_            = vp.y;
    vp_w_            = vp.w;
    vp_h_            = vp.h;

    double d_x0 = NAN;
    double d_y0 = NAN;
    double d_x1 = NAN;
    double d_y1 = NAN;
    screen_to_data(box_zoom_.x0, box_zoom_.y0, d_x0, d_y0);
    screen_to_data(box_zoom_.x1, box_zoom_.y1, d_x1, d_y1);

    vp_x_ = saved_vp_x;
    vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w;
    vp_h_ = saved_vp_h;

    // Ensure min < max
    double xmin = std::min(d_x0, d_x1);
    double xmax = std::max(d_x0, d_x1);
    double ymin = std::min(d_y0, d_y1);
    double ymax = std::max(d_y0, d_y1);

    // Only apply if the selection is large enough (avoid accidental clicks)
    constexpr float MIN_SELECTION_PIXELS = 5.0f;
    auto            dx_screen = static_cast<float>(std::abs(box_zoom_.x1 - box_zoom_.x0));
    auto            dy_screen = static_cast<float>(std::abs(box_zoom_.y1 - box_zoom_.y0));

    if (dx_screen > MIN_SELECTION_PIXELS && dy_screen > MIN_SELECTION_PIXELS)
    {
        // Animated box zoom transition
        AxisLimits target_x{xmin, xmax};
        AxisLimits target_y{ymin, ymax};
        if (transition_engine_)
        {
            transition_engine_->animate_limits(*active_axes_,
                                               target_x,
                                               target_y,
                                               ZOOM_ANIM_DURATION,
                                               ease::ease_out);
        }
        else if (anim_ctrl_)
        {
            anim_ctrl_->animate_axis_limits(*active_axes_,
                                            target_x,
                                            target_y,
                                            ZOOM_ANIM_DURATION,
                                            ease::ease_out);
        }
        else
        {
            set_axes_xlim(active_axes_, xmin, xmax);
            set_axes_ylim(active_axes_, ymin, ymax);
        }

        // Propagate box zoom to linked axes
        if (axis_link_mgr_)
        {
            axis_link_mgr_->propagate_limits(active_axes_, target_x, target_y);
        }
    }

    box_zoom_.active = false;
}

void InputHandler::cancel_box_zoom()
{
    if (mode_ == InteractionMode::Dragging
        && (tool_mode_ == ToolMode::BoxZoom || ctrl_box_zoom_active_))
    {
        mode_ = InteractionMode::Idle;
    }
    box_zoom_.active      = false;
    ctrl_box_zoom_active_ = false;
}

// ─── Viewport ───────────────────────────────────────────────────────────────

void InputHandler::set_viewport(float vp_x, float vp_y, float vp_w, float vp_h)
{
    vp_x_ = vp_x;
    vp_y_ = vp_y;
    vp_w_ = vp_w;
    vp_h_ = vp_h;
}

void InputHandler::screen_to_data(double  screen_x,
                                  double  screen_y,
                                  double& data_x,
                                  double& data_y) const
{
    if (!active_axes_)
    {
        data_x = 0.0;
        data_y = 0.0;
        return;
    }

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Normalize screen position within viewport [0, 1] (double precision:
    // limits can sit at epoch scale where float quantizes to ~128 s)
    double norm_x = (screen_x - static_cast<double>(vp_x_)) / static_cast<double>(vp_w_);
    double norm_y = (screen_y - static_cast<double>(vp_y_)) / static_cast<double>(vp_h_);

    // Invert Y (screen Y goes down, data Y goes up)
    norm_y = 1.0 - norm_y;

    // Map to data space
    data_x = xlim.min + norm_x * (xlim.max - xlim.min);
    data_y = ylim.min + norm_y * (ylim.max - ylim.min);
}

// ─── Per-frame update ───────────────────────────────────────────────────────

void InputHandler::update(float dt)
{
    if (transition_engine_)
    {
        transition_engine_->update(dt);
    }
    if (anim_ctrl_)
    {
        anim_ctrl_->update(dt);
    }
}

// ─── Animation query ────────────────────────────────────────────────────────

bool InputHandler::has_active_animations() const
{
    if (transition_engine_ && transition_engine_->has_active_animations())
        return true;
    return (anim_ctrl_ != nullptr) && anim_ctrl_->has_active_animations();
}

}   // namespace spectra
