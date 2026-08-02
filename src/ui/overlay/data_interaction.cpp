#ifdef SPECTRA_USE_IMGUI

    #include "data_interaction.hpp"

    #include <algorithm>
    #include <cmath>
    #include <imgui.h>
    #include <limits>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <spectra/series3d.hpp>

    #include "data/sorted_x_query.hpp"
    #include "axes3d_pick.hpp"
    #include "ui/data/axis_link.hpp"

namespace spectra
{

void DataInteraction::set_fonts(ImFont* body, ImFont* heading, ImFont* icon)
{
    tooltip_.set_fonts(body, heading);
    region_.set_fonts(body, heading);
    legend_.set_fonts(body, icon);
    annotations_.set_fonts(body, heading);
}

void DataInteraction::set_theme_manager(ui::ThemeManager* tm)
{
    theme_mgr_ = tm;
    crosshair_.set_theme_manager(tm);
    tooltip_.set_theme_manager(tm);
    markers_.set_theme_manager(tm);
    annotations_.set_theme_manager(tm);
    region_.set_theme_manager(tm);
    legend_.set_theme_manager(tm);
}

bool DataInteraction::select_point(const Series* series, size_t point_index)
{
    if (!series)
        return false;

    const float* x_data = nullptr;
    const float* y_data = nullptr;
    size_t       count  = 0;

    if (auto* ls = dynamic_cast<const LineSeries*>(series))
    {
        x_data = ls->x_data().data();
        y_data = ls->y_data().data();
        count  = ls->point_count();
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(series))
    {
        x_data = sc->x_data().data();
        y_data = sc->y_data().data();
        count  = sc->point_count();
    }
    else
    {
        // Point highlighting currently supports 2D series only.
        return false;
    }

    if (!x_data || !y_data || point_index >= count)
        return false;

    // Find the axes that owns this series
    const Axes* target_axes = nullptr;
    if (last_figure_)
    {
        for (auto& axes_ptr : last_figure_->axes())
        {
            if (!axes_ptr)
                continue;
            for (auto& s : axes_ptr->series())
            {
                if (s.get() == series)
                {
                    target_axes = axes_ptr.get();
                    break;
                }
            }
            if (target_axes)
                break;
        }
    }

    // Compute dy/dx at the selected point
    float dy_dx       = 0.0f;
    bool  dy_dx_valid = false;
    if (count >= 2)
    {
        size_t i = point_index;
        if (i > 0 && i + 1 < count)
        {
            float dx = x_data[i + 1] - x_data[i - 1];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[i + 1] - y_data[i - 1]) / dx;
                dy_dx_valid = true;
            }
        }
        else if (i == 0)
        {
            float dx = x_data[1] - x_data[0];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[1] - y_data[0]) / dx;
                dy_dx_valid = true;
            }
        }
        else if (i == count - 1)
        {
            float dx = x_data[count - 1] - x_data[count - 2];
            if (std::abs(dx) > 1e-30f)
            {
                dy_dx       = (y_data[count - 1] - y_data[count - 2]) / dx;
                dy_dx_valid = true;
            }
        }
    }

    // Absolute x = series x_offset + stored relative float.
    const double abs_x = static_cast<double>(x_data[point_index]) + series->x_offset();

    markers_.clear();
    markers_.add(abs_x, y_data[point_index], series, point_index, target_axes, dy_dx, dy_dx_valid);

    // Keep nearest cache coherent so tooltip/cursor feedback remains aligned.
    nearest_.found       = true;
    nearest_.series      = series;
    nearest_.point_index = point_index;
    nearest_.data_x      = abs_x;
    nearest_.data_y      = y_data[point_index];

    return true;
}

void DataInteraction::set_transition_engine(TransitionEngine* te)
{
    region_.set_transition_engine(te);
    legend_.set_transition_engine(te);
}

void DataInteraction::update(const CursorReadout& cursor, Figure& figure)
{
    last_cursor_ = cursor;
    last_figure_ = &figure;

    // Update legend animation state
    float dt = 0.016f;   // fallback
    #ifdef SPECTRA_USE_IMGUI
    dt = ImGui::GetIO().DeltaTime;
    #endif
    legend_.update(dt, figure);

    // Determine which axes the cursor is over by hit-testing viewports (2D then 3D)
    active_axes_      = nullptr;
    active_axes3d_    = nullptr;
    auto sx           = static_cast<float>(cursor.screen_x);
    auto sy           = static_cast<float>(cursor.screen_y);
    auto try_axes_hit = [&](AxesBase* axes_base) -> bool
    {
        if (!axes_base || !cursor.valid)
            return false;
        const auto& vp = axes_base->viewport();
        if (sx < vp.x || sx > vp.x + vp.w || sy < vp.y || sy > vp.y + vp.h)
            return false;
        active_viewport_ = vp;
        if (auto* axes2d = dynamic_cast<Axes*>(axes_base))
        {
            active_axes_ = axes2d;
            auto xl      = axes2d->x_limits();
            auto yl      = axes2d->y_limits();
            xlim_min_    = xl.min;
            xlim_max_    = xl.max;
            ylim_min_    = yl.min;
            ylim_max_    = yl.max;
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(axes_base))
        {
            active_axes3d_ = axes3d;
        }
        return true;
    };

    for (auto& axes_ptr : figure.axes())
    {
        if (try_axes_hit(axes_ptr.get()))
            break;
    }
    if (!active_axes_ && !active_axes3d_)
    {
        for (auto& axes_ptr : figure.all_axes())
        {
            if (try_axes_hit(axes_ptr.get()))
                break;
        }
    }

    // Broadcast shared cursor to linked axes
    if (axis_link_mgr_ && active_axes_ && cursor.valid)
    {
        SharedCursor sc;
        sc.valid  = true;
        sc.data_x = xlim_min_
                    + (static_cast<float>(cursor.screen_x) - active_viewport_.x)
                          / active_viewport_.w * (xlim_max_ - xlim_min_);
        sc.data_y = ylim_max_
                    - (static_cast<float>(cursor.screen_y) - active_viewport_.y)
                          / active_viewport_.h * (ylim_max_ - ylim_min_);
        sc.screen_x    = cursor.screen_x;
        sc.screen_y    = cursor.screen_y;
        sc.source_axes = active_axes_;
        axis_link_mgr_->update_shared_cursor(sc);
    }
    else if (axis_link_mgr_)
    {
        axis_link_mgr_->clear_shared_cursor();
    }

    // Run nearest-point query (skip when pointer is outside the plot/window)
    if (!cursor.valid)
        nearest_ = {};
    else
        nearest_ = find_nearest(cursor, figure);
}

bool DataInteraction::refresh_pointer_state(double screen_x, double screen_y)
{
    if (!last_figure_)
        return false;

    CursorReadout cursor;
    cursor.screen_x = screen_x;
    cursor.screen_y = screen_y;

    active_axes_   = nullptr;
    active_axes3d_ = nullptr;
    const auto sx  = static_cast<float>(screen_x);
    const auto sy  = static_cast<float>(screen_y);

    auto try_refresh_hit = [&](AxesBase* axes_base) -> bool
    {
        if (!axes_base)
            return false;
        const auto& vp = axes_base->viewport();
        if (vp.w <= 0.0f || vp.h <= 0.0f)
            return false;
        if (sx < vp.x || sx > vp.x + vp.w || sy < vp.y || sy > vp.y + vp.h)
            return false;

        active_viewport_ = vp;
        cursor.valid     = true;
        cursor.screen_x  = screen_x;
        cursor.screen_y  = screen_y;

        if (auto* axes2d = dynamic_cast<Axes*>(axes_base))
        {
            active_axes_ = axes2d;
            auto xl      = axes2d->x_limits();
            auto yl      = axes2d->y_limits();
            xlim_min_    = xl.min;
            xlim_max_    = xl.max;
            ylim_min_    = yl.min;
            ylim_max_    = yl.max;

            float x_range = xlim_max_ - xlim_min_;
            float y_range = ylim_max_ - ylim_min_;
            if (x_range == 0.0f)
                x_range = 1.0f;
            if (y_range == 0.0f)
                y_range = 1.0f;
            cursor.data_x = xlim_min_ + (sx - vp.x) / vp.w * x_range;
            cursor.data_y = ylim_max_ - (sy - vp.y) / vp.h * y_range;
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(axes_base))
        {
            active_axes3d_ = axes3d;
        }
        return true;
    };

    for (auto& axes_ptr : last_figure_->axes())
    {
        if (try_refresh_hit(axes_ptr.get()))
            break;
    }
    if (!active_axes_ && !active_axes3d_)
    {
        for (auto& axes_ptr : last_figure_->all_axes())
        {
            if (try_refresh_hit(axes_ptr.get()))
                break;
        }
    }

    last_cursor_ = cursor;
    nearest_     = find_nearest(cursor, *last_figure_);
    return cursor.valid;
}

void DataInteraction::draw_legend_for_figure(Figure& figure)
{
    const auto& config = figure.legend();
    if (!config.visible)
        return;
    if (config.position == LegendPosition::None)
        return;

    auto   fig_id = reinterpret_cast<uintptr_t>(&figure);
    size_t idx    = 0;
    for (auto& axes_ptr : figure.axes_mut())
    {
        if (axes_ptr)
        {
            legend_.draw(*axes_ptr, axes_ptr->viewport(), idx, config, fig_id);
        }
        ++idx;
    }
}

void DataInteraction::draw_overlays(float       window_width,
                                    float       window_height,
                                    Figure*     current_figure,
                                    ImDrawList* dl)
{
    Figure* overlay_figure = current_figure ? current_figure : last_figure_;
    if (current_figure)
        last_figure_ = current_figure;

    // Draw legend interaction for each axes (gated on figure legend visibility)
    if (overlay_figure)
    {
        draw_legend_for_figure(*overlay_figure);
    }

    // Draw markers (data tips) — always visible, even when cursor is outside the figure.
    // Each marker is drawn using its owning axes' viewport and limits so that
    // markers stay in their correct subplot regardless of cursor position.
    if (overlay_figure && !markers_.markers().empty())
    {
        for (auto& axes_ptr : overlay_figure->axes())
        {
            if (!axes_ptr)
                continue;
            const auto& vp = axes_ptr->viewport();
            auto        xl = axes_ptr->x_limits();
            auto        yl = axes_ptr->y_limits();
            markers_.draw(vp,
                          xl.min,
                          xl.max,
                          static_cast<float>(yl.min),
                          static_cast<float>(yl.max),
                          1.0f,
                          axes_ptr.get(),
                          dl);
        }
        for (auto& axes_ptr : overlay_figure->all_axes())
        {
            if (!axes_ptr)
                continue;
            if (auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get()))
                markers_.draw_3d(*axes3d, 1.0f, dl);
        }
    }

    // Draw region selection overlay — use the axes where the ROI was started
    // so it stays in the correct subplot when the cursor moves.
    Axes* roi_axes = region_axes_ ? region_axes_ : active_axes_;
    if (roi_axes && (region_.is_dragging() || region_.has_selection()))
    {
        const auto& vp = roi_axes->viewport();
        auto        xl = roi_axes->x_limits();
        auto        yl = roi_axes->y_limits();
        region_.draw(vp,
                     static_cast<float>(xl.min),
                     static_cast<float>(xl.max),
                     static_cast<float>(yl.min),
                     static_cast<float>(yl.max),
                     window_width,
                     window_height);
    }

    // Draw crosshair: use multi-axes mode if figure has multiple axes
    if (overlay_figure && overlay_figure->axes().size() > 1)
    {
        crosshair_.draw_all_axes(last_cursor_, *overlay_figure, axis_link_mgr_, dl);
    }
    else if (active_axes_)
    {
        crosshair_
            .draw(last_cursor_, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_, dl);
    }

    // Draw annotations
    if (overlay_figure)
    {
        for (auto& axes_ptr : overlay_figure->axes())
        {
            if (!axes_ptr)
                continue;
            const auto& vp = axes_ptr->viewport();
            auto        xl = axes_ptr->x_limits();
            auto        yl = axes_ptr->y_limits();
            annotations_.draw(vp,
                              static_cast<float>(xl.min),
                              static_cast<float>(xl.max),
                              static_cast<float>(yl.min),
                              static_cast<float>(yl.max),
                              1.0f,
                              axes_ptr.get(),
                              dl);
        }
    }

    // Draw tooltip last (on top)
    tooltip_.draw(nearest_, window_width, window_height, dl);
}

bool DataInteraction::dispatch_series_selection_from_nearest()
{
    if (!nearest_.found || !nearest_.series || !last_figure_)
        return false;

    auto dispatch_for_axes = [&](AxesBase* axes_base, int ax_idx) -> bool
    {
        if (!axes_base)
            return false;
        int s_idx = 0;
        for (auto& series_ptr : axes_base->series())
        {
            if (series_ptr.get() == nearest_.series)
            {
                Axes* axes2d = dynamic_cast<Axes*>(axes_base);
                if (on_series_selected_)
                {
                    on_series_selected_(last_figure_, axes2d, ax_idx, series_ptr.get(), s_idx);
                }
                if (on_point_selected_)
                {
                    on_point_selected_(last_figure_,
                                       axes2d,
                                       ax_idx,
                                       series_ptr.get(),
                                       s_idx,
                                       nearest_.point_index);
                }
                return true;
            }
            s_idx++;
        }
        return false;
    };

    int ax_idx = 0;
    for (auto& axes_ptr : last_figure_->axes())
    {
        if (dispatch_for_axes(axes_ptr.get(), ax_idx))
            return true;
        ax_idx++;
    }

    ax_idx = 0;
    for (auto& axes_ptr : last_figure_->all_axes())
    {
        if (dispatch_for_axes(axes_ptr.get(), ax_idx))
            return true;
        ax_idx++;
    }

    // Fallback: nearest point selected, but series lookup path above didn't match.
    if (on_point_selected_ && active_axes_)
    {
        on_point_selected_(last_figure_,
                           active_axes_,
                           0,
                           const_cast<Series*>(nearest_.series),
                           0,
                           nearest_.point_index);
        return true;
    }

    return false;
}

bool DataInteraction::try_toggle_datatip_at_nearest()
{
    constexpr float SELECT_SNAP_PX = 30.0f;
    if (!nearest_.found || nearest_.distance_px > SELECT_SNAP_PX)
        return false;

    if (nearest_.is_3d && nearest_.axes3d)
    {
        markers_.toggle_or_add_3d(nearest_.data_x,
                                  nearest_.data_y,
                                  nearest_.data_z,
                                  nearest_.series,
                                  nearest_.point_index,
                                  nearest_.axes3d);
        return true;
    }

    if (active_axes_)
    {
        markers_.toggle_or_add(nearest_.data_x,
                               nearest_.data_y,
                               nearest_.series,
                               nearest_.point_index,
                               active_axes_,
                               nearest_.dy_dx,
                               nearest_.dy_dx_valid);
        return true;
    }

    return false;
}

bool DataInteraction::on_mouse_click_datatip_only(int button, double screen_x, double screen_y)
{
    refresh_pointer_state(screen_x, screen_y);

    if ((!active_axes_ && !active_axes3d_) || !last_figure_)
        return false;

    if (button == 0)
    {
        if (active_axes3d_)
        {
            int marker_hit = markers_.hit_test_3d(static_cast<float>(screen_x),
                                                  static_cast<float>(screen_y),
                                                  *active_axes3d_,
                                                  10.0f);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }
        else if (active_axes_)
        {
            int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                               static_cast<float>(screen_y),
                                               active_viewport_,
                                               xlim_min_,
                                               xlim_max_,
                                               ylim_min_,
                                               ylim_max_,
                                               10.0f,
                                               active_axes_);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }

        if (try_toggle_datatip_at_nearest())
            return true;
    }

    // Right click: remove marker
    if (button == 1)
    {
        if (active_axes3d_)
        {
            int idx = markers_.hit_test_3d(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           *active_axes3d_,
                                           10.0f);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
        else if (active_axes_)
        {
            int idx = markers_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_viewport_,
                                        xlim_min_,
                                        xlim_max_,
                                        ylim_min_,
                                        ylim_max_,
                                        10.0f,
                                        active_axes_);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
    }

    return false;
}

bool DataInteraction::on_mouse_click_series_only(double screen_x, double screen_y)
{
    refresh_pointer_state(screen_x, screen_y);

    if (!active_axes_ || !last_figure_)
        return false;

    constexpr float SELECT_SNAP_PX = 30.0f;
    if (nearest_.found && nearest_.distance_px <= SELECT_SNAP_PX)
    {
        return dispatch_series_selection_from_nearest();
    }

    if (on_series_deselected_)
    {
        on_series_deselected_();
        return true;
    }

    return false;
}

bool DataInteraction::on_mouse_click(int button, double screen_x, double screen_y)
{
    refresh_pointer_state(screen_x, screen_y);

    if ((!active_axes_ && !active_axes3d_) || !last_figure_)
        return false;

    // Left click: remove an existing data tip if clicked on it, otherwise pin a new one
    if (button == 0)
    {
        if (active_axes3d_)
        {
            int marker_hit = markers_.hit_test_3d(static_cast<float>(screen_x),
                                                  static_cast<float>(screen_y),
                                                  *active_axes3d_,
                                                  10.0f);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }
        else if (active_axes_)
        {
            int marker_hit = markers_.hit_test(static_cast<float>(screen_x),
                                               static_cast<float>(screen_y),
                                               active_viewport_,
                                               xlim_min_,
                                               xlim_max_,
                                               ylim_min_,
                                               ylim_max_,
                                               10.0f,
                                               active_axes_);
            if (marker_hit >= 0)
            {
                markers_.remove(static_cast<size_t>(marker_hit));
                return true;
            }
        }

        if (try_toggle_datatip_at_nearest())
        {
            dispatch_series_selection_from_nearest();
            return true;
        }
        if (on_series_deselected_)
        {
            // Clicked on canvas but not near any series — deselect
            on_series_deselected_();
            return true;
        }
    }

    // Right click: remove marker
    if (button == 1)
    {
        if (active_axes3d_)
        {
            int idx = markers_.hit_test_3d(static_cast<float>(screen_x),
                                           static_cast<float>(screen_y),
                                           *active_axes3d_,
                                           10.0f);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
        else if (active_axes_)
        {
            int idx = markers_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_viewport_,
                                        xlim_min_,
                                        xlim_max_,
                                        ylim_min_,
                                        ylim_max_,
                                        10.0f,
                                        active_axes_);
            if (idx >= 0)
            {
                markers_.remove(static_cast<size_t>(idx));
                return true;
            }
        }
    }

    return false;
}

void DataInteraction::add_marker(float data_x, float data_y, const Series* series, size_t index)
{
    markers_.add(data_x, data_y, series, index);
}

void DataInteraction::remove_marker(size_t idx)
{
    markers_.remove(idx);
}

void DataInteraction::clear_markers()
{
    markers_.clear();
}

void DataInteraction::set_snap_radius(float px)
{
    tooltip_.set_snap_radius(px);
}

// ─── Region selection ───────────────────────────────────────────────────────

void DataInteraction::begin_region_select(double screen_x, double screen_y)
{
    if (!active_axes_)
        return;
    region_axes_ = active_axes_;
    region_.begin(screen_x, screen_y, active_viewport_, xlim_min_, xlim_max_, ylim_min_, ylim_max_);
}

void DataInteraction::update_region_drag(double screen_x, double screen_y)
{
    // Use the axes where the ROI was started, not whatever the cursor is over now
    if (!region_axes_)
        return;
    const auto& vp = region_axes_->viewport();
    auto        xl = region_axes_->x_limits();
    auto        yl = region_axes_->y_limits();
    region_.update_drag(screen_x,
                        screen_y,
                        vp,
                        static_cast<float>(xl.min),
                        static_cast<float>(xl.max),
                        static_cast<float>(yl.min),
                        static_cast<float>(yl.max));
}

void DataInteraction::finish_region_select()
{
    region_.finish(region_axes_ ? region_axes_ : active_axes_);
}

void DataInteraction::dismiss_region_select()
{
    region_.dismiss();
    region_axes_ = nullptr;
}

void DataInteraction::select_series_in_rect(const BoxZoomRect& rect, Figure& figure)
{
    std::vector<RectSelectedEntry> hits;

    // Normalize the screen-space rectangle
    double rx0 = std::min(rect.x0, rect.x1);
    double ry0 = std::min(rect.y0, rect.y1);
    double rx1 = std::max(rect.x0, rect.x1);
    double ry1 = std::max(rect.y0, rect.y1);

    int ax_idx = 0;
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
        {
            ax_idx++;
            continue;
        }
        const auto& vp   = axes_ptr->viewport();
        auto        xlim = axes_ptr->x_limits();
        auto        ylim = axes_ptr->y_limits();
        double      xr   = xlim.max - xlim.min;
        double      yr   = ylim.max - ylim.min;
        if (xr == 0.0)
            xr = 1.0;
        if (yr == 0.0)
            yr = 1.0;

        int s_idx = 0;
        for (auto& series_ptr : axes_ptr->series())
        {
            if (!series_ptr || !series_ptr->visible())
            {
                s_idx++;
                continue;
            }

            const float* x_data = nullptr;
            const float* y_data = nullptr;
            size_t       count  = 0;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                x_data = ls->x_data().data();
                y_data = ls->y_data().data();
                count  = ls->point_count();
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                x_data = sc->x_data().data();
                y_data = sc->y_data().data();
                count  = sc->point_count();
            }

            if (!x_data || !y_data || count == 0)
            {
                s_idx++;
                continue;
            }

            // Check if any data point maps to a screen position inside the rectangle
            bool hit = false;
            for (size_t i = 0; i < count && !hit; ++i)
            {
                double norm_x = (static_cast<double>(x_data[i]) - xlim.min) / xr;
                double norm_y = (static_cast<double>(y_data[i]) - ylim.min) / yr;
                double scr_x  = vp.x + norm_x * vp.w;
                double scr_y  = vp.y + (1.0 - norm_y) * vp.h;
                if (scr_x >= rx0 && scr_x <= rx1 && scr_y >= ry0 && scr_y <= ry1)
                    hit = true;
            }

            // Also check line segment intersections with the rectangle edges
            if (!hit && count >= 2)
            {
                for (size_t i = 0; i + 1 < count && !hit; ++i)
                {
                    double nx0 = (static_cast<double>(x_data[i]) - xlim.min) / xr;
                    double ny0 = (static_cast<double>(y_data[i]) - ylim.min) / yr;
                    double nx1 = (static_cast<double>(x_data[i + 1]) - xlim.min) / xr;
                    double ny1 = (static_cast<double>(y_data[i + 1]) - ylim.min) / yr;

                    double sx0 = vp.x + nx0 * vp.w;
                    double sy0 = vp.y + (1.0 - ny0) * vp.h;
                    double sx1 = vp.x + nx1 * vp.w;
                    double sy1 = vp.y + (1.0 - ny1) * vp.h;

                    // Cohen-Sutherland: if the segment can be clipped to the rectangle, it
                    // intersects
                    double cx0     = sx0;
                    double cy0     = sy0;
                    double cx1     = sx1;
                    double cy1     = sy1;
                    auto   outcode = [&](double px, double py) -> int
                    {
                        int code = 0;
                        if (px < rx0)
                            code |= 1;
                        else if (px > rx1)
                            code |= 2;
                        if (py < ry0)
                            code |= 4;
                        else if (py > ry1)
                            code |= 8;
                        return code;
                    };
                    int oc0 = outcode(cx0, cy0);
                    int oc1 = outcode(cx1, cy1);
                    for (int iter = 0; iter < 20; ++iter)
                    {
                        if ((oc0 | oc1) == 0)
                        {
                            hit = true;
                            break;
                        }
                        if ((oc0 & oc1) != 0)
                            break;
                        int    oc_out = oc0 ? oc0 : oc1;
                        double px     = 0;
                        double py     = 0;
                        if (oc_out & 8)
                        {
                            px = cx0 + (cx1 - cx0) * (ry1 - cy0) / (cy1 - cy0);
                            py = ry1;
                        }
                        else if (oc_out & 4)
                        {
                            px = cx0 + (cx1 - cx0) * (ry0 - cy0) / (cy1 - cy0);
                            py = ry0;
                        }
                        else if (oc_out & 2)
                        {
                            py = cy0 + (cy1 - cy0) * (rx1 - cx0) / (cx1 - cx0);
                            px = rx1;
                        }
                        else if (oc_out & 1)
                        {
                            py = cy0 + (cy1 - cy0) * (rx0 - cx0) / (cx1 - cx0);
                            px = rx0;
                        }
                        if (oc_out == oc0)
                        {
                            cx0 = px;
                            cy0 = py;
                            oc0 = outcode(cx0, cy0);
                        }
                        else
                        {
                            cx1 = px;
                            cy1 = py;
                            oc1 = outcode(cx1, cy1);
                        }
                    }
                }
            }

            if (hit)
            {
                hits.push_back({&figure, axes_ptr.get(), ax_idx, series_ptr.get(), s_idx});
            }
            s_idx++;
        }
        ax_idx++;
    }

    if (on_rect_series_selected_)
        on_rect_series_selected_(hits);
}

// ─── Annotation tool ────────────────────────────────────────────────────────

bool DataInteraction::on_mouse_click_annotate(int button, double screen_x, double screen_y)
{
    if (!active_axes_ || !last_figure_)
        return false;

    // Right click: remove annotation
    if (button == 1)
    {
        int idx = annotations_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_viewport_,
                                        xlim_min_,
                                        xlim_max_,
                                        ylim_min_,
                                        ylim_max_,
                                        10.0f,
                                        active_axes_);
        if (idx >= 0)
        {
            annotations_.remove(static_cast<size_t>(idx));
            return true;
        }
        return false;
    }

    // Left click
    if (button == 0)
    {
        // If currently editing an annotation, commit it first
        annotations_.cancel_editing();

        // Hit-test existing annotations — clicking on one starts editing
        int hit = annotations_.hit_test(static_cast<float>(screen_x),
                                        static_cast<float>(screen_y),
                                        active_viewport_,
                                        xlim_min_,
                                        xlim_max_,
                                        ylim_min_,
                                        ylim_max_,
                                        10.0f,
                                        active_axes_);
        if (hit >= 0)
        {
            // Re-enter editing mode on the clicked annotation
            auto& anns                             = annotations_.annotations_mut();
            anns[static_cast<size_t>(hit)].editing = true;
            return true;
        }

        // Place a new annotation at the click position (data coordinates)
        float x_range = xlim_max_ - xlim_min_;
        float y_range = ylim_max_ - ylim_min_;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

        float norm_x = (static_cast<float>(screen_x) - active_viewport_.x) / active_viewport_.w;
        float norm_y =
            1.0f - (static_cast<float>(screen_y) - active_viewport_.y) / active_viewport_.h;
        float data_x = xlim_min_ + norm_x * x_range;
        float data_y = ylim_min_ + norm_y * y_range;

        annotations_.add(data_x, data_y, active_axes_);
        return true;
    }

    return false;
}

void DataInteraction::begin_annotation_drag(double screen_x, double screen_y)
{
    if (!active_axes_)
        return;

    int hit = annotations_.hit_test(static_cast<float>(screen_x),
                                    static_cast<float>(screen_y),
                                    active_viewport_,
                                    xlim_min_,
                                    xlim_max_,
                                    ylim_min_,
                                    ylim_max_,
                                    10.0f,
                                    active_axes_);
    if (hit >= 0)
    {
        annotations_.begin_drag(static_cast<size_t>(hit),
                                static_cast<float>(screen_x),
                                static_cast<float>(screen_y));
    }
}

void DataInteraction::update_annotation_drag(double screen_x, double screen_y)
{
    annotations_.update_drag(static_cast<float>(screen_x), static_cast<float>(screen_y));
}

void DataInteraction::end_annotation_drag()
{
    annotations_.end_drag();
}

NearestPointResult DataInteraction::find_nearest(const CursorReadout& cursor, Figure& figure) const
{
    last_query_points_examined_ = 0;
    NearestPointResult best;
    best.found       = false;
    best.distance_px = std::numeric_limits<float>::max();
    float best_dist2 = std::numeric_limits<float>::max();

    auto cx = static_cast<float>(cursor.screen_x);
    auto cy = static_cast<float>(cursor.screen_y);

    // Pure-3D figures never set cursor.valid via 2D hit-test; still allow 3D pick
    // when the pointer is inside a 3D subplot viewport.
    bool over_plot = cursor.valid;
    if (!over_plot)
    {
        for (auto& axes_ptr : figure.all_axes())
        {
            if (!axes_ptr)
                continue;
            const auto& vp = axes_ptr->viewport();
            if (cx >= vp.x && cx <= vp.x + vp.w && cy >= vp.y && cy <= vp.y + vp.h)
            {
                over_plot = true;
                break;
            }
        }
    }
    if (!over_plot)
        return best;

    // Search all axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();

        // Only search axes the cursor is inside
        if (cx < vp.x || cx > vp.x + vp.w || cy < vp.y || cy > vp.y + vp.h)
            continue;

        auto   xlim    = axes_ptr->x_limits();
        auto   ylim    = axes_ptr->y_limits();
        double x_range = xlim.max - xlim.min;
        double y_range = ylim.max - ylim.min;
        if (x_range == 0.0)
            x_range = 1.0;
        if (y_range == 0.0)
            y_range = 1.0;

        for (auto& series_ptr : axes_ptr->series())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            const float* x_data   = nullptr;
            const float* y_data   = nullptr;
            size_t       count    = 0;
            bool         x_sorted = false;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                x_data   = ls->x_data().data();
                y_data   = ls->y_data().data();
                count    = ls->point_count();
                x_sorted = ls->x_is_sorted();
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                x_data   = sc->x_data().data();
                y_data   = sc->y_data().data();
                count    = sc->point_count();
                x_sorted = sc->x_is_sorted();
            }

            if (!x_data || !y_data || count == 0)
                continue;

            // x_data is relative to the series x_offset; limits are absolute.
            const double xoff = series_ptr->x_offset();

            size_t query_begin = 0;
            size_t query_end   = count;

            // For monotonic X data, only points within the tooltip's horizontal
            // snap radius can be interactable. Binary search that exact range;
            // keep one neighbor on either side so nearest-point reporting stays
            // stable just outside the snap window. Unsorted data retains the
            // full scan and therefore identical behavior.
            if (x_sorted && count > 64 && vp.w > 0.0f)
            {
                const double cursor_abs_x =
                    xlim.min + (static_cast<double>(cx - vp.x) / vp.w) * x_range;
                const double local_cursor_x = cursor_abs_x - xoff;
                const double local_radius =
                    std::abs(x_range) * static_cast<double>(tooltip_.snap_radius()) / vp.w;
                const auto query = sorted_x_query_range(std::span<const float>(x_data, count),
                                                        local_cursor_x,
                                                        local_radius);
                query_begin      = query.begin;
                query_end        = query.end;
            }

            last_query_points_examined_ += query_end - query_begin;
            for (size_t i = query_begin; i < query_end; ++i)
            {
                // Convert data point to screen coordinates.  Compute the
                // x normalization in double: (x + xoff - xlim.min) stays
                // small and precise even for epoch-scale offsets.
                const double abs_x  = static_cast<double>(x_data[i]) + xoff;
                auto         norm_x = static_cast<float>((abs_x - xlim.min) / x_range);
                float        norm_y = (y_data[i] - ylim.min) / y_range;
                float        sx     = vp.x + norm_x * vp.w;
                float        sy     = vp.y + (1.0f - norm_y) * vp.h;

                float dx    = cx - sx;
                float dy    = cy - sy;
                float dist2 = dx * dx + dy * dy;

                if (dist2 < best_dist2)
                {
                    best.found       = true;
                    best.series      = series_ptr.get();
                    best.point_index = i;
                    best.data_x      = abs_x;
                    best.data_y      = y_data[i];
                    best.screen_x    = sx;
                    best.screen_y    = sy;
                    best_dist2       = dist2;
                }
            }
        }
    }

    if (best.found)
        best.distance_px = std::sqrt(best_dist2);

    // Search 3D axes (stored in all_axes_, not axes_)
    for (auto& axes_ptr : figure.all_axes())
    {
        if (!axes_ptr)
            continue;
        auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
        if (!axes3d)
            continue;

        const Nearest3DPickCandidate pick = find_nearest_3d_in_axes(*axes3d, cx, cy);
        if (!pick.found)
            continue;

        if (!best.found || pick.distance_px < best.distance_px)
        {
            best.found       = true;
            best.series      = pick.series;
            best.point_index = pick.point_index;
            best.data_x      = pick.data_x;
            best.data_y      = pick.data_y;
            best.data_z      = pick.data_z;
            best.screen_x    = pick.screen_x;
            best.screen_y    = pick.screen_y;
            best.distance_px = pick.distance_px;
            best.ndc_depth   = pick.ndc_depth;
            best.is_3d       = true;
            best.axes3d      = axes3d;
            best.dy_dx_valid = false;
            best.dy_dx       = 0.0f;
        }
    }

    // Compute dy/dx at the nearest point via finite difference (2D only)
    if (best.found && !best.is_3d && best.series)
    {
        const float* xd    = nullptr;
        const float* yd    = nullptr;
        size_t       count = 0;
        if (auto* ls = dynamic_cast<const LineSeries*>(best.series))
        {
            xd    = ls->x_data().data();
            yd    = ls->y_data().data();
            count = ls->point_count();
        }
        else if (auto* sc = dynamic_cast<const ScatterSeries*>(best.series))
        {
            xd    = sc->x_data().data();
            yd    = sc->y_data().data();
            count = sc->point_count();
        }

        if (xd && yd && count >= 2)
        {
            size_t i = best.point_index;
            if (i > 0 && i + 1 < count)
            {
                // Central difference
                float dx = xd[i + 1] - xd[i - 1];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[i + 1] - yd[i - 1]) / dx;
                    best.dy_dx_valid = true;
                }
            }
            else if (i == 0 && count >= 2)
            {
                // Forward difference
                float dx = xd[1] - xd[0];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[1] - yd[0]) / dx;
                    best.dy_dx_valid = true;
                }
            }
            else if (i == count - 1 && count >= 2)
            {
                // Backward difference
                float dx = xd[count - 1] - xd[count - 2];
                if (std::abs(dx) > 1e-30f)
                {
                    best.dy_dx       = (yd[count - 1] - yd[count - 2]) / dx;
                    best.dy_dx_valid = true;
                }
            }
        }
    }

    return best;
}

// ─── Overlay snapshot ────────────────────────────────────────────────────────

OverlaySnapshot DataInteraction::capture_overlay_snapshot(const Figure& figure) const
{
    OverlaySnapshot snap;
    snap.crosshair_enabled = crosshair_.enabled();
    snap.tooltip_enabled   = tooltip_.enabled();

    // Helper: resolve an Axes pointer to its index in the figure
    auto resolve_axes_index = [&](const Axes* axes) -> size_t
    {
        if (!axes)
            return 0;
        size_t idx = 0;
        for (const auto& ax : figure.axes())
        {
            if (ax.get() == axes)
                return idx;
            ++idx;
        }
        return 0;
    };

    // Markers
    for (const auto& m : markers_.markers())
    {
        OverlaySnapshot::MarkerEntry me;
        me.data_x       = m.data_x;
        me.data_y       = m.data_y;
        me.series_label = m.series_label;
        me.point_index  = m.point_index;
        me.axes_index   = resolve_axes_index(m.axes);
        snap.markers.push_back(std::move(me));
    }

    // Annotations
    for (const auto& ann : annotations_.annotations())
    {
        if (ann.editing || ann.text.empty())
            continue;
        OverlaySnapshot::AnnotationEntry ae;
        ae.data_x     = ann.data_x;
        ae.data_y     = ann.data_y;
        ae.text       = ann.text;
        ae.color      = ann.color;
        ae.offset_x   = ann.offset_x;
        ae.offset_y   = ann.offset_y;
        ae.axes_index = resolve_axes_index(ann.axes);
        snap.annotations.push_back(std::move(ae));
    }

    if (region_.has_selection() && region_axes_)
    {
        snap.region.valid      = true;
        snap.region.x_min      = region_.data_x_min();
        snap.region.x_max      = region_.data_x_max();
        snap.region.y_min      = region_.data_y_min();
        snap.region.y_max      = region_.data_y_max();
        snap.region.axes_index = resolve_axes_index(region_axes_);
    }

    return snap;
}

void DataInteraction::restore_overlay_snapshot(const OverlaySnapshot& snapshot, Figure& figure)
{
    set_crosshair(snapshot.crosshair_enabled);
    set_tooltip(snapshot.tooltip_enabled);

    // Helper: resolve an axes index to a pointer
    auto resolve_axes_ptr = [&](size_t idx) -> const Axes*
    {
        if (idx < figure.axes().size() && figure.axes()[idx])
            return figure.axes()[idx].get();
        return nullptr;
    };

    // Restore markers
    clear_markers();
    for (const auto& me : snapshot.markers)
    {
        // Find the series by label to get a valid pointer
        const Series* series_ptr = nullptr;
        const Axes*   axes_ptr   = resolve_axes_ptr(me.axes_index);
        if (axes_ptr)
        {
            for (const auto& sp : axes_ptr->series())
            {
                if (sp->label() == me.series_label)
                {
                    series_ptr = sp.get();
                    break;
                }
            }
        }
        else
        {
            // Search all axes for the series
            for (const auto& ax : figure.axes())
            {
                if (!ax)
                    continue;
                for (const auto& sp : ax->series())
                {
                    if (sp->label() == me.series_label)
                    {
                        series_ptr = sp.get();
                        axes_ptr   = ax.get();
                        break;
                    }
                }
                if (series_ptr)
                    break;
            }
        }
        markers_.add(me.data_x, me.data_y, series_ptr, me.point_index, axes_ptr);
    }

    // Restore annotations
    annotations_.clear();
    for (const auto& ae : snapshot.annotations)
    {
        if (ae.text.empty())
            continue;
        const Axes* axes_ptr = resolve_axes_ptr(ae.axes_index);
        size_t      idx      = annotations_.add(ae.data_x, ae.data_y, axes_ptr);
        auto&       ann      = annotations_.annotations_mut()[idx];
        ann.text             = ae.text;
        ann.color            = ae.color;
        ann.offset_x         = ae.offset_x;
        ann.offset_y         = ae.offset_y;
        ann.editing          = false;
    }

    region_.dismiss();
    region_axes_ = nullptr;
    if (snapshot.region.valid)
    {
        region_axes_ = const_cast<Axes*>(resolve_axes_ptr(snapshot.region.axes_index));
        if (region_axes_)
        {
            region_.restore(snapshot.region.x_min,
                            snapshot.region.x_max,
                            snapshot.region.y_min,
                            snapshot.region.y_max,
                            region_axes_);
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
