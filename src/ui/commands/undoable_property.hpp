#pragma once

#include <functional>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "undo_manager.hpp"

namespace spectra
{

// ─── Convenience helpers for undoable property mutations ─────────────────────
// These capture before/after state and push to UndoManager in a single call.
// All helpers are safe to call with a null UndoManager pointer (no-op).

// Generic: push a value change with a setter lambda
template <typename T>
void undoable_set(UndoManager*                  mgr,
                  const std::string&            description,
                  T                             before,
                  T                             after,
                  std::function<void(const T&)> setter)
{
    if (setter)
        setter(after);
    if (mgr)
        mgr->push_value<T>(description, before, after, std::move(setter));
}

// ─── Axis limits ─────────────────────────────────────────────────────────────

inline void undoable_xlim(UndoManager* mgr, Axes& ax, float new_min, float new_max)
{
    auto old = ax.x_limits();
    ax.xlim(new_min, new_max);
    if (mgr)
    {
        Axes*      ptr    = &ax;
        AxisLimits before = old;
        AxisLimits after{new_min, new_max};
        mgr->push(UndoAction{"Change X limits",
                             [ptr, before]() { ptr->xlim(before.min, before.max); },
                             [ptr, after]() { ptr->xlim(after.min, after.max); }});
    }
}

inline void undoable_ylim(UndoManager* mgr, Axes& ax, float new_min, float new_max)
{
    auto old = ax.y_limits();
    ax.ylim(new_min, new_max);
    if (mgr)
    {
        Axes*      ptr    = &ax;
        AxisLimits before = old;
        AxisLimits after{new_min, new_max};
        mgr->push(UndoAction{"Change Y limits",
                             [ptr, before]() { ptr->ylim(before.min, before.max); },
                             [ptr, after]() { ptr->ylim(after.min, after.max); }});
    }
}

inline void undoable_set_limits(UndoManager* mgr, Axes& ax, AxisLimits new_x, AxisLimits new_y)
{
    auto old_x = ax.x_limits();
    auto old_y = ax.y_limits();
    ax.xlim(new_x.min, new_x.max);
    ax.ylim(new_y.min, new_y.max);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{"Change axis limits",
                             [ptr, old_x, old_y]()
                             {
                                 ptr->xlim(old_x.min, old_x.max);
                                 ptr->ylim(old_y.min, old_y.max);
                             },
                             [ptr, new_x, new_y]()
                             {
                                 ptr->xlim(new_x.min, new_x.max);
                                 ptr->ylim(new_y.min, new_y.max);
                             }});
    }
}

// ─── Grid toggle ─────────────────────────────────────────────────────────────

inline void undoable_toggle_grid(UndoManager* mgr, Axes& ax)
{
    bool old_val = ax.grid_enabled();
    bool new_val = !old_val;
    ax.set_grid_enabled(new_val);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{new_val ? "Show grid" : "Hide grid",
                             [ptr, old_val]() { ptr->set_grid_enabled(old_val); },
                             [ptr, new_val]() { ptr->set_grid_enabled(new_val); }});
    }
}

// ─── Border toggle ──────────────────────────────────────────────────────────

inline void undoable_toggle_border(UndoManager* mgr, Axes& ax)
{
    bool old_val = ax.border_enabled();
    bool new_val = !old_val;
    ax.set_border_enabled(new_val);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{new_val ? "Show border" : "Hide border",
                             [ptr, old_val]() { ptr->set_border_enabled(old_val); },
                             [ptr, new_val]() { ptr->set_border_enabled(new_val); }});
    }
}

// ─── Series visibility ──────────────────────────────────────────────────────

inline void undoable_toggle_series_visibility(UndoManager* mgr, Series& s)
{
    bool old_val = s.visible();
    bool new_val = !old_val;
    s.visible(new_val);
    if (mgr)
    {
        Series*     ptr  = &s;
        std::string name = s.label().empty() ? "series" : s.label();
        mgr->push(UndoAction{new_val ? "Show " + name : "Hide " + name,
                             [ptr, old_val]() { ptr->visible(old_val); },
                             [ptr, new_val]() { ptr->visible(new_val); }});
    }
}

// ─── Series color ────────────────────────────────────────────────────────────

inline void undoable_set_series_color(UndoManager* mgr, Series& s, const Color& new_color)
{
    Color old_color = s.color();
    s.set_color(new_color);
    if (mgr)
    {
        Series* ptr = &s;
        mgr->push(UndoAction{"Change color of " + (s.label().empty() ? "series" : s.label()),
                             [ptr, old_color]() { ptr->set_color(old_color); },
                             [ptr, new_color]() { ptr->set_color(new_color); }});
    }
}

// ─── Line width ──────────────────────────────────────────────────────────────

inline void undoable_set_line_width(UndoManager* mgr, LineSeries& ls, float new_width)
{
    float old_width = ls.width();
    ls.width(new_width);
    if (mgr)
    {
        LineSeries* ptr = &ls;
        mgr->push(UndoAction{"Change line width",
                             [ptr, old_width]() { ptr->width(old_width); },
                             [ptr, new_width]() { ptr->width(new_width); }});
    }
}

// ─── Marker size ─────────────────────────────────────────────────────────────

inline void undoable_set_marker_size(UndoManager* mgr, ScatterSeries& sc, float new_size)
{
    float old_size = sc.size();
    sc.size(new_size);
    if (mgr)
    {
        ScatterSeries* ptr = &sc;
        mgr->push(UndoAction{"Change marker size",
                             [ptr, old_size]() { ptr->size(old_size); },
                             [ptr, new_size]() { ptr->size(new_size); }});
    }
}

// ─── Line style ──────────────────────────────────────────────────────────────

inline void undoable_set_line_style(UndoManager* mgr, Series& s, LineStyle new_style)
{
    LineStyle old_style = s.line_style();
    s.line_style(new_style);
    if (mgr)
    {
        Series* ptr = &s;
        mgr->push(UndoAction{"Change line style",
                             [ptr, old_style]() { ptr->line_style(old_style); },
                             [ptr, new_style]() { ptr->line_style(new_style); }});
    }
}

// ─── Marker style ────────────────────────────────────────────────────────────

inline void undoable_set_marker_style(UndoManager* mgr, Series& s, MarkerStyle new_style)
{
    MarkerStyle old_style = s.marker_style();
    s.marker_style(new_style);
    if (mgr)
    {
        Series* ptr = &s;
        mgr->push(UndoAction{"Change marker style",
                             [ptr, old_style]() { ptr->marker_style(old_style); },
                             [ptr, new_style]() { ptr->marker_style(new_style); }});
    }
}

// ─── Series marker size (on base Series) ─────────────────────────────────────

inline void undoable_set_series_marker_size(UndoManager* mgr, Series& s, float new_size)
{
    float old_size = s.marker_size();
    s.marker_size(new_size);
    if (mgr)
    {
        Series* ptr = &s;
        mgr->push(UndoAction{"Change marker size",
                             [ptr, old_size]() { ptr->marker_size(old_size); },
                             [ptr, new_size]() { ptr->marker_size(new_size); }});
    }
}

// ─── Series opacity ──────────────────────────────────────────────────────────

inline void undoable_set_opacity(UndoManager* mgr, Series& s, float new_opacity)
{
    float old_opacity = s.opacity();
    s.opacity(new_opacity);
    if (mgr)
    {
        Series* ptr = &s;
        mgr->push(UndoAction{"Change opacity",
                             [ptr, old_opacity]() { ptr->opacity(old_opacity); },
                             [ptr, new_opacity]() { ptr->opacity(new_opacity); }});
    }
}

// ─── Editable series data ───────────────────────────────────────────────────

struct EditableSeriesData
{
    std::vector<float> x;
    std::vector<float> y;
};

inline bool capture_editable_series_data(const Series& series, EditableSeriesData& data)
{
    if (const auto* line = dynamic_cast<const LineSeries*>(&series))
    {
        data.x.assign(line->x_data().begin(), line->x_data().end());
        data.y.assign(line->y_data().begin(), line->y_data().end());
        return true;
    }
    if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&series))
    {
        data.x.assign(scatter->x_data().begin(), scatter->x_data().end());
        data.y.assign(scatter->y_data().begin(), scatter->y_data().end());
        return true;
    }
    return false;
}

inline bool restore_editable_series_data(Series& series, const EditableSeriesData& data)
{
    if (auto* line = dynamic_cast<LineSeries*>(&series))
    {
        line->set_x(data.x).set_y(data.y);
        return true;
    }
    if (auto* scatter = dynamic_cast<ScatterSeries*>(&series))
    {
        scatter->set_x(data.x).set_y(data.y);
        return true;
    }
    return false;
}

inline AxesBase* find_figure_axes(Figure& figure, size_t axes_index)
{
    size_t    current = 0;
    AxesBase* result  = nullptr;
    figure.for_each_axes(
        [&](AxesBase* axes)
        {
            if (current == axes_index)
                result = axes;
            ++current;
        });
    return result;
}

inline Series* find_figure_series(FigureRegistry& registry,
                                  FigureId        figure_id,
                                  size_t          axes_index,
                                  size_t          series_index)
{
    Figure* figure = registry.get(figure_id);
    if (!figure)
        return nullptr;
    AxesBase* axes = find_figure_axes(*figure, axes_index);
    if (!axes || series_index >= axes->series().size())
        return nullptr;
    return axes->series()[series_index].get();
}

inline bool undoable_set_figure_title(UndoManager*       mgr,
                                      FigureRegistry&    registry,
                                      FigureId           figure_id,
                                      const std::string& new_title)
{
    Figure* figure = registry.get(figure_id);
    if (!figure || figure->tab_title() == new_title)
        return false;

    const std::string old_title = figure->tab_title();
    figure->set_tab_title(new_title);
    if (mgr)
    {
        auto* registry_ptr = &registry;
        mgr->push(UndoAction{"Change figure title",
                             [registry_ptr, figure_id, old_title]()
                             {
                                 if (Figure* target = registry_ptr->get(figure_id))
                                     target->set_tab_title(old_title);
                             },
                             [registry_ptr, figure_id, new_title]()
                             {
                                 if (Figure* target = registry_ptr->get(figure_id))
                                     target->set_tab_title(new_title);
                             }});
    }
    return true;
}

inline bool undoable_set_series_label(UndoManager*       mgr,
                                      FigureRegistry&    registry,
                                      FigureId           figure_id,
                                      size_t             axes_index,
                                      size_t             series_index,
                                      const std::string& new_label)
{
    Series* series = find_figure_series(registry, figure_id, axes_index, series_index);
    if (!series || series->label() == new_label)
        return false;

    const std::string old_label = series->label();
    series->label(new_label);
    if (mgr)
    {
        auto* registry_ptr = &registry;
        mgr->push(UndoAction{
            "Change series label",
            [registry_ptr, figure_id, axes_index, series_index, old_label]()
            {
                if (Series* target =
                        find_figure_series(*registry_ptr, figure_id, axes_index, series_index))
                    target->label(old_label);
            },
            [registry_ptr, figure_id, axes_index, series_index, new_label]()
            {
                if (Series* target =
                        find_figure_series(*registry_ptr, figure_id, axes_index, series_index))
                    target->label(new_label);
            }});
    }
    return true;
}

inline bool undoable_set_series_line_width(UndoManager*    mgr,
                                           FigureRegistry& registry,
                                           FigureId        figure_id,
                                           size_t          axes_index,
                                           size_t          series_index,
                                           float           new_width)
{
    Series* series = find_figure_series(registry, figure_id, axes_index, series_index);
    if (!series || series->plot_style().line_width == new_width)
        return false;

    const float old_width = series->plot_style().line_width;
    auto        style     = series->plot_style();
    style.line_width      = new_width;
    series->plot_style(style);

    if (mgr)
    {
        auto* registry_ptr = &registry;
        auto  set_width    = [registry_ptr, figure_id, axes_index, series_index](float width)
        {
            Series* target = find_figure_series(*registry_ptr, figure_id, axes_index, series_index);
            if (!target)
                return;
            auto target_style       = target->plot_style();
            target_style.line_width = width;
            target->plot_style(target_style);
        };
        mgr->push(UndoAction{"Change series line width",
                             [set_width, old_width]() { set_width(old_width); },
                             [set_width, new_width]() { set_width(new_width); }});
    }
    return true;
}

inline bool undoable_set_series_data(UndoManager*          mgr,
                                     FigureRegistry&       registry,
                                     FigureId              figure_id,
                                     size_t                axes_index,
                                     size_t                series_index,
                                     EditableSeriesData    after,
                                     const std::string&    description,
                                     std::function<void()> notify = {})
{
    Series* series = find_figure_series(registry, figure_id, axes_index, series_index);
    if (!series)
        return false;

    EditableSeriesData before;
    if (!capture_editable_series_data(*series, before)
        || (before.x == after.x && before.y == after.y))
        return false;

    if (!restore_editable_series_data(*series, after))
        return false;
    if (notify)
        notify();

    if (mgr)
    {
        auto* registry_ptr = &registry;
        mgr->push(UndoAction{
            description,
            [registry_ptr, figure_id, axes_index, series_index, before, notify]()
            {
                Series* target =
                    find_figure_series(*registry_ptr, figure_id, axes_index, series_index);
                if (target && restore_editable_series_data(*target, before) && notify)
                    notify();
            },
            [registry_ptr, figure_id, axes_index, series_index, after, notify]()
            {
                Series* target =
                    find_figure_series(*registry_ptr, figure_id, axes_index, series_index);
                if (target && restore_editable_series_data(*target, after) && notify)
                    notify();
            }});
    }
    return true;
}

struct FigureEditableDataSnapshot
{
    struct SeriesEntry
    {
        size_t             axes_index   = 0;
        size_t             series_index = 0;
        EditableSeriesData data;
    };

    struct AxesEntry
    {
        size_t     axes_index = 0;
        AxisLimits x_limits;
        AxisLimits y_limits;
    };

    std::vector<SeriesEntry> series;
    std::vector<AxesEntry>   axes;
};

inline FigureEditableDataSnapshot capture_figure_editable_data(Figure& figure)
{
    FigureEditableDataSnapshot snapshot;
    size_t                     axes_index = 0;
    figure.for_each_axes(
        [&](AxesBase* axes)
        {
            if (auto* axes_2d = dynamic_cast<Axes*>(axes))
            {
                snapshot.axes.push_back({axes_index, axes_2d->x_limits(), axes_2d->y_limits()});
            }

            const auto& series = axes->series();
            for (size_t series_index = 0; series_index < series.size(); ++series_index)
            {
                if (!series[series_index])
                    continue;
                EditableSeriesData data;
                if (capture_editable_series_data(*series[series_index], data))
                {
                    snapshot.series.push_back({axes_index, series_index, std::move(data)});
                }
            }
            ++axes_index;
        });
    return snapshot;
}

inline bool restore_figure_editable_data(FigureRegistry&                   registry,
                                         FigureId                          figure_id,
                                         const FigureEditableDataSnapshot& snapshot)
{
    Figure* figure = registry.get(figure_id);
    if (!figure)
        return false;

    bool restored = false;
    for (const auto& entry : snapshot.series)
    {
        Series* series =
            find_figure_series(registry, figure_id, entry.axes_index, entry.series_index);
        restored = (series && restore_editable_series_data(*series, entry.data)) || restored;
    }
    for (const auto& entry : snapshot.axes)
    {
        if (auto* axes = dynamic_cast<Axes*>(find_figure_axes(*figure, entry.axes_index)))
        {
            axes->xlim(entry.x_limits.min, entry.x_limits.max);
            axes->ylim(entry.y_limits.min, entry.y_limits.max);
            restored = true;
        }
    }
    return restored;
}

inline bool undoable_edit_figure_data(UndoManager*                 mgr,
                                      FigureRegistry&              registry,
                                      FigureId                     figure_id,
                                      const std::string&           description,
                                      std::function<bool(Figure&)> mutation,
                                      std::function<void()>        notify = {})
{
    Figure* figure = registry.get(figure_id);
    if (!figure || !mutation)
        return false;

    const auto before = capture_figure_editable_data(*figure);
    if (!mutation(*figure))
        return false;
    const auto after = capture_figure_editable_data(*figure);
    if (notify)
        notify();

    if (mgr)
    {
        auto* registry_ptr = &registry;
        mgr->push(UndoAction{
            description,
            [registry_ptr, figure_id, before, notify]()
            {
                if (restore_figure_editable_data(*registry_ptr, figure_id, before) && notify)
                    notify();
            },
            [registry_ptr, figure_id, after, notify]()
            {
                if (restore_figure_editable_data(*registry_ptr, figure_id, after) && notify)
                    notify();
            }});
    }
    return true;
}

// ─── Legend visibility ───────────────────────────────────────────────────────

inline void undoable_toggle_legend(UndoManager* mgr, Figure& fig)
{
    bool old_val         = fig.legend().visible;
    bool new_val         = !old_val;
    fig.legend().visible = new_val;
    if (mgr)
    {
        Figure* ptr = &fig;
        mgr->push(UndoAction{new_val ? "Show legend" : "Hide legend",
                             [ptr, old_val]() { ptr->legend().visible = old_val; },
                             [ptr, new_val]() { ptr->legend().visible = new_val; }});
    }
}

// ─── Axis title / labels ────────────────────────────────────────────────────

inline void undoable_set_title(UndoManager* mgr, Axes& ax, const std::string& new_title)
{
    std::string old_title = ax.get_title();
    ax.title(new_title);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{"Change title",
                             [ptr, old_title]() { ptr->title(old_title); },
                             [ptr, new_title]() { ptr->title(new_title); }});
    }
}

inline void undoable_set_xlabel(UndoManager* mgr, Axes& ax, const std::string& new_label)
{
    std::string old_label = ax.get_xlabel();
    ax.xlabel(new_label);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{"Change X label",
                             [ptr, old_label]() { ptr->xlabel(old_label); },
                             [ptr, new_label]() { ptr->xlabel(new_label); }});
    }
}

inline void undoable_set_ylabel(UndoManager* mgr, Axes& ax, const std::string& new_label)
{
    std::string old_label = ax.get_ylabel();
    ax.ylabel(new_label);
    if (mgr)
    {
        Axes* ptr = &ax;
        mgr->push(UndoAction{"Change Y label",
                             [ptr, old_label]() { ptr->ylabel(old_label); },
                             [ptr, new_label]() { ptr->ylabel(new_label); }});
    }
}

// ─── Grouped multi-axes operations ──────────────────────────────────────────

// Toggle grid on all 2D axes in a figure as a single undo action
// (Axes3D grid is always rendered and not togglable via this function)
inline void undoable_toggle_grid_all(UndoManager* mgr, Figure& fig)
{
    if (mgr)
        mgr->begin_group("Toggle grid");
    for (auto& ax : fig.axes_mut())
    {
        if (ax)
            undoable_toggle_grid(mgr, *ax);
    }
    if (mgr)
        mgr->end_group();
}

// Toggle border on all 2D axes in a figure as a single undo action
// (Axes3D border is always rendered and not togglable via this function)
inline void undoable_toggle_border_all(UndoManager* mgr, Figure& fig)
{
    if (mgr)
        mgr->begin_group("Toggle border");
    for (auto& ax : fig.axes_mut())
    {
        if (ax)
            undoable_toggle_border(mgr, *ax);
    }
    if (mgr)
        mgr->end_group();
}

// Capture full figure axis state for undo (e.g., before auto-fit / reset view)
// Captures both 2D axes (xlim/ylim) and 3D axes (xlim/ylim/zlim + camera).
struct FigureAxisSnapshot
{
    struct Entry2D
    {
        Axes*      axes;
        AxisLimits x_limits;
        AxisLimits y_limits;
    };
    struct Entry3D
    {
        Axes3D*    axes;
        AxisLimits x_limits;
        AxisLimits y_limits;
        AxisLimits z_limits;
        Camera     camera;
    };
    std::vector<Entry2D> entries;
    std::vector<Entry3D> entries3d;
};

inline FigureAxisSnapshot capture_figure_axes(Figure& fig)
{
    FigureAxisSnapshot snap;
    for (auto& ax : fig.axes_mut())
    {
        if (ax)
        {
            snap.entries.push_back({ax.get(), ax->x_limits(), ax->y_limits()});
        }
    }
    for (auto& ax_base : fig.all_axes_mut())
    {
        if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
        {
            snap.entries3d.push_back(
                {ax3d, ax3d->x_limits(), ax3d->y_limits(), ax3d->z_limits(), ax3d->camera()});
        }
    }
    return snap;
}

inline void restore_figure_axes(const FigureAxisSnapshot& snap)
{
    for (auto& e : snap.entries)
    {
        if (e.axes)
        {
            e.axes->xlim(e.x_limits.min, e.x_limits.max);
            e.axes->ylim(e.y_limits.min, e.y_limits.max);
        }
    }
    for (auto& e : snap.entries3d)
    {
        if (e.axes)
        {
            e.axes->xlim(e.x_limits.min, e.x_limits.max);
            e.axes->ylim(e.y_limits.min, e.y_limits.max);
            e.axes->zlim(e.z_limits.min, e.z_limits.max);
            e.axes->camera() = e.camera;
            e.axes->camera().update_position_from_orbit();
        }
    }
}

inline void undoable_reset_view(UndoManager* mgr, Figure& fig)
{
    auto before = capture_figure_axes(fig);
    for (auto& ax : fig.axes_mut())
    {
        if (ax)
            ax->auto_fit();
    }
    for (auto& ax_base : fig.all_axes_mut())
    {
        if (auto* ax3d = dynamic_cast<Axes3D*>(ax_base.get()))
            ax3d->auto_fit();
    }
    auto after = capture_figure_axes(fig);
    if (mgr)
    {
        mgr->push(UndoAction{"Reset view",
                             [before]() { restore_figure_axes(before); },
                             [after]() { restore_figure_axes(after); }});
    }
}

}   // namespace spectra
