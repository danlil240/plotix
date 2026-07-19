#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <functional>
    #include <spectra/figure.hpp>
    #include <spectra/fwd.hpp>

    #include "annotation.hpp"
    #include "crosshair.hpp"
    #include "data_marker.hpp"
    #include "ui/input/input.hpp"
    #include "legend_interaction.hpp"
    #include "ui/input/region_select.hpp"
    #include "tooltip.hpp"
    #include "ui/workspace/overlay_snapshot.hpp"

struct ImFont;

namespace spectra
{

// Orchestrates all data-interaction features:
//   - Nearest-point spatial query
//   - Rich hover tooltip
//   - Crosshair overlay
//   - Persistent data markers (click to pin, right-click to remove)
class DataInteraction
{
   public:
    DataInteraction() = default;

    // Set fonts for tooltip/legend/region rendering
    void set_fonts(ImFont* body, ImFont* heading, ImFont* icon = nullptr);

    // Inject ThemeManager into all overlay sub-objects.
    void set_theme_manager(ui::ThemeManager* tm);

    // Main update: run nearest-point query and update internal state.
    // Call once per frame after input handling.
    void update(const CursorReadout& cursor, Figure& figure);

    // Draw all overlays (tooltip, crosshair, markers).
    // Pass current_figure when available so overlay rendering does not depend
    // on stale cached figure pointers.
    // When dl is non-null, all overlay primitives are drawn into that draw list
    // instead of GetForegroundDrawList(), enabling proper z-ordering behind menus.
    void draw_overlays(float       window_width,
                       float       window_height,
                       Figure*     current_figure = nullptr,
                       ImDrawList* dl             = nullptr);

    // Draw legend overlay for a specific figure (for split-mode panes).
    // Respects figure.legend().visible.
    void draw_legend_for_figure(Figure& figure);

    // Nearest-point result from the last update
    const NearestPointResult& nearest_point() const { return nearest_; }
    size_t last_query_points_examined() const { return last_query_points_examined_; }

    // Crosshair control
    bool crosshair_active() const { return crosshair_.enabled(); }
    void toggle_crosshair() { crosshair_.toggle(); }
    void set_crosshair(bool e) { crosshair_.set_enabled(e); }

    // Tooltip control
    bool tooltip_active() const { return tooltip_.enabled(); }
    void set_tooltip(bool e) { tooltip_.set_enabled(e); }

    // Marker / data label control
    void add_marker(float data_x, float data_y, const Series* series, size_t index);
    void remove_marker(size_t idx);
    void clear_markers();
    const std::vector<DataMarker>& markers() const { return markers_.markers(); }

    // Toggle a data label (datatip) on a point: adds if absent, removes if present.
    // Returns true if a label was added.
    bool toggle_data_label(float         data_x,
                           float         data_y,
                           const Series* series,
                           size_t        index,
                           const Axes*   axes        = nullptr,
                           float         dy_dx       = 0.0f,
                           bool          dy_dx_valid = false)
    {
        return markers_.toggle_or_add(data_x, data_y, series, index, axes, dy_dx, dy_dx_valid);
    }

    // Handle mouse click for marker placement/removal.
    // Returns true if the click was consumed by the data interaction layer.
    bool on_mouse_click(int button, double screen_x, double screen_y);

    // Pan-mode click behavior: datatip marker operations only (no series selection callbacks).
    bool on_mouse_click_datatip_only(int button, double screen_x, double screen_y);

    // Select-mode click behavior: series selection callbacks only (no datatip marker mutations).
    bool on_mouse_click_series_only(double screen_x, double screen_y);

    // Region selection (ROI tool — analysis rectangle)
    void                    begin_region_select(double screen_x, double screen_y);
    void                    update_region_drag(double screen_x, double screen_y);
    void                    finish_region_select();
    void                    dismiss_region_select();
    bool                    is_region_dragging() const { return region_.is_dragging(); }
    bool                    has_region_selection() const { return region_.has_selection(); }
    const RegionStatistics& region_statistics() const { return region_.statistics(); }

    // Rectangle multi-select: select all series whose data intersects a screen-space rectangle.
    // Fires the on_rect_series_selected_ callback with all matching series.
    void select_series_in_rect(const BoxZoomRect& rect, Figure& figure);

    // Legend interaction
    LegendInteraction&       legend() { return legend_; }
    const LegendInteraction& legend() const { return legend_; }

    // Annotation management (Annotate tool)
    AnnotationManager&       annotations() { return annotations_; }
    const AnnotationManager& annotations() const { return annotations_; }

    // Handle mouse click in Annotate mode.
    // Returns true if the click was consumed (annotation placed/removed).
    bool on_mouse_click_annotate(int button, double screen_x, double screen_y);

    // Handle mouse drag for annotation repositioning.
    void begin_annotation_drag(double screen_x, double screen_y);
    void update_annotation_drag(double screen_x, double screen_y);
    void end_annotation_drag();
    bool is_annotation_dragging() const { return annotations_.is_dragging(); }

    // Set the transition engine for animated markers/regions
    void set_transition_engine(class TransitionEngine* te);

    // Set the axis link manager for shared cursor across subplots
    void             set_axis_link_manager(class AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Set snap radius for nearest-point detection (in pixels)
    void  set_snap_radius(float px);
    float snap_radius() const { return tooltip_.snap_radius(); }

    // Clean up all references to a series that is about to be destroyed.
    // Call this BEFORE the series is freed.
    void notify_series_removed(const Series* s)
    {
        markers_.remove_for_series(s);
        if (nearest_.series == s)
            nearest_ = {};
    }

    // Invalidate cached figure pointer (call when a figure is destroyed)
    void clear_figure_cache(Figure* fig = nullptr)
    {
        if (!fig || last_figure_ == fig)
        {
            last_figure_   = nullptr;
            active_axes_   = nullptr;
            active_axes3d_ = nullptr;
            region_axes_   = nullptr;
            // Reset nearest-point result — it holds a raw Series* pointer
            // that becomes dangling when the figure's series are destroyed.
            nearest_ = {};
            // Clear markers — they hold raw Series* pointers that become
            // dangling when the figure (and its series) are destroyed.
            markers_.clear();
        }
    }

    // Series selection callback: fired when user left-clicks near a series (toggles selection).
    // Args: (Figure*, Axes*, int axes_index, Series*, int series_index)
    using SeriesSelectedCallback = std::function<void(Figure*, Axes*, int, Series*, int)>;
    void set_on_series_selected(SeriesSelectedCallback cb) { on_series_selected_ = std::move(cb); }

    // Right-click series selection: fired when user right-clicks near a series (no toggle, always
    // selects).
    void set_on_series_right_click_selected(SeriesSelectedCallback cb)
    {
        on_series_rc_selected_ = std::move(cb);
    }

    // Series deselection callback: fired when user left-clicks on canvas but NOT near any series.
    using SeriesDeselectedCallback = std::function<void()>;
    void set_on_series_deselected(SeriesDeselectedCallback cb)
    {
        on_series_deselected_ = std::move(cb);
    }

    // Point selection callback: fired when a concrete point is selected.
    // Args: (Figure*, Axes*, int axes_index, Series*, int series_index, size_t point_index)
    using PointSelectedCallback = std::function<void(Figure*, Axes*, int, Series*, int, size_t)>;
    void set_on_point_selected(PointSelectedCallback cb) { on_point_selected_ = std::move(cb); }

    // Rectangle multi-select callback: fired when a rectangle drag selects multiple series.
    // Args: vector of (Figure*, Axes*, axes_index, Series*, series_index) tuples
    struct RectSelectedEntry
    {
        Figure* figure       = nullptr;
        Axes*   axes         = nullptr;
        int     axes_index   = -1;
        Series* series       = nullptr;
        int     series_index = -1;
    };
    using RectSeriesSelectedCallback = std::function<void(const std::vector<RectSelectedEntry>&)>;
    void set_on_rect_series_selected(RectSeriesSelectedCallback cb)
    {
        on_rect_series_selected_ = std::move(cb);
    }

    // Programmatically select/highlight a point (used by Data Editor row selection).
    // Returns true when the point was valid for the provided series and a marker was placed.
    bool select_point(const Series* series, size_t point_index);

    // Capture all overlay state into a plain-data snapshot for serialization.
    // Resolves raw Axes* pointers to index within the given figure.
    OverlaySnapshot capture_overlay_snapshot(const Figure& figure) const;

    // Restore overlay state from a snapshot. Resolves axes indices back to
    // pointers using the given figure. Clears existing markers/annotations first.
    void restore_overlay_snapshot(const OverlaySnapshot& snapshot, Figure& figure);

   private:
    // Dispatch series/point callbacks from current nearest_ selection.
    // Returns true when dispatch succeeded.
    bool dispatch_series_selection_from_nearest();

    // Refresh cached cursor/axes/nearest-point state for an explicit screen position.
    bool refresh_pointer_state(double screen_x, double screen_y);

    // Perform nearest-point spatial query across all visible series in the active axes.
    NearestPointResult find_nearest(const CursorReadout& cursor, Figure& figure) const;

    // Pin / unpin a datatip at the current nearest point (2D or 3D). Returns true if handled.
    bool try_toggle_datatip_at_nearest();

    NearestPointResult nearest_;
    Tooltip            tooltip_;
    Crosshair          crosshair_;
    DataMarkerManager  markers_;
    AnnotationManager  annotations_;
    RegionSelect       region_;
    LegendInteraction  legend_;

    // Axis link manager for shared cursor
    AxisLinkManager* axis_link_mgr_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Series selection / deselection callbacks
    SeriesSelectedCallback     on_series_selected_;
    SeriesSelectedCallback     on_series_rc_selected_;
    SeriesDeselectedCallback   on_series_deselected_;
    PointSelectedCallback      on_point_selected_;
    RectSeriesSelectedCallback on_rect_series_selected_;

    // Cached state for drawing
    CursorReadout last_cursor_;
    Figure*       last_figure_   = nullptr;
    Axes*         active_axes_   = nullptr;
    const Axes3D* active_axes3d_ = nullptr;
    Rect          active_viewport_;
    // Double precision for x: limits can sit at epoch scale (~1.7e9).
    double xlim_min_ = 0.0, xlim_max_ = 1.0;
    float  ylim_min_ = 0.0f, ylim_max_ = 1.0f;

    // Region selection: remember which axes the ROI was started in
    // so it stays in the correct subplot when the cursor moves.
    Axes*          region_axes_                = nullptr;
    mutable size_t last_query_points_examined_ = 0;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
