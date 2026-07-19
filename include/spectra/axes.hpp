#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <spectra/series_shapes.hpp>
#include <string>
#include <vector>

namespace spectra
{

enum class AutoscaleMode
{
    Fit,      // Fit to data range exactly
    Tight,    // Fit with no padding
    Padded,   // Fit with small padding (default)
    Manual,   // User-specified limits only
};

enum class ScaleType
{
    Linear,   // Standard linear scale (default)
    Log10,    // Base-10 logarithmic scale
    Log2,     // Base-2 logarithmic scale
    Sqrt,     // Square-root scale
};

struct AxisStyle
{
    Color tick_color  = colors::black;
    Color label_color = colors::black;
    Color grid_color  = {0.0f, 0.0f, 0.0f, 0.0f};   // alpha=0 → use theme grid_line color
    float tick_length = 5.0f;
    float label_size  = 14.0f;
    float title_size  = 16.0f;
    float grid_width  = 1.0f;
};

struct AxisLimits
{
    double min = 0.0;
    double max = 1.0;
};

struct TickResult
{
    std::vector<double>      positions;
    std::vector<std::string> labels;
};

class AxesBase
{
   public:
    virtual ~AxesBase() = default;

    virtual void auto_fit() = 0;

    const std::vector<std::unique_ptr<Series>>& series() const { return series_; }
    std::vector<std::unique_ptr<Series>>&       series_mut() { return series_; }

    // Safely remove all series, notifying the renderer to defer GPU cleanup.
    // Always prefer this over series_mut().clear().
    void clear_series();

    // Remove a single series by index (0-based).  Returns false if out of range.
    bool remove_series(size_t index);

    // Move a series from one index to another (reorder).  Returns false if out of range.
    bool move_series(size_t from, size_t to);

    // Called by the framework to wire up deferred GPU cleanup.
    using SeriesRemovedCallback = std::function<void(const Series*)>;
    void set_series_removed_callback(SeriesRemovedCallback cb)
    {
        on_series_removed_ = std::move(cb);
    }
    bool has_series_removed_callback() const { return static_cast<bool>(on_series_removed_); }

    // ── Event system integration ──
    void         set_event_system(EventSystem* es) { event_system_ = es; }
    EventSystem* event_system() const { return event_system_; }

    void        set_viewport(const Rect& r) { viewport_ = r; }
    const Rect& viewport() const { return viewport_; }

    const std::string& title() const { return title_; }
    void               title(const std::string& t) { title_ = t; }

    bool grid_enabled() const { return grid_enabled_; }
    void grid(bool enabled) { grid_enabled_ = enabled; }

    bool border_enabled() const { return border_enabled_; }
    void show_border(bool enabled) { border_enabled_ = enabled; }

    // Colormap colorbar visibility (Phase 7C)
    bool colorbar_visible() const { return colorbar_visible_; }
    void show_colorbar(bool visible) { colorbar_visible_ = visible; }

    AxisStyle&       axis_style() { return axis_style_; }
    const AxisStyle& axis_style() const { return axis_style_; }

    // Deprecated aliases — prefer grid(bool) and show_border(bool)
    void               set_grid_enabled(bool e) { grid_enabled_ = e; }
    void               set_border_enabled(bool e) { border_enabled_ = e; }
    const std::string& get_title() const { return title_; }

   protected:
    std::vector<std::unique_ptr<Series>> series_;
    std::string                          title_;
    bool                                 grid_enabled_     = true;
    bool                                 border_enabled_   = true;
    bool                                 colorbar_visible_ = false;
    AxisStyle                            axis_style_;
    Rect                                 viewport_;
    SeriesRemovedCallback                on_series_removed_;
    EventSystem*                         event_system_ = nullptr;
};

class Axes : public AxesBase
{
   public:
    Axes() = default;

    // Series creation — returns reference for fluent API
    LineSeries& line(std::span<const float> x, std::span<const float> y);
    LineSeries& line();

    ScatterSeries& scatter(std::span<const float> x, std::span<const float> y);
    ScatterSeries& scatter();

    // MATLAB-style plot: plot(x, y, "r--o") creates a line series with the
    // given format string applied. See parse_format_string() in plot_style.hpp.
    LineSeries& plot(std::span<const float> x,
                     std::span<const float> y,
                     std::string_view       fmt = "-");
    LineSeries& plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style);

    // Horizontal / vertical reference lines (e.g. y=0, x=0). Span uses sentinel
    // coordinates so auto_fit ignores them.
    LineSeries& hline(double y, std::string_view fmt = "-");
    LineSeries& vline(double x, std::string_view fmt = "-");

    // Sample y = f(x) over [xmin, xmax] with n evenly spaced points.
    LineSeries& fplot(std::function<double(double)> func,
                      double                        xmin,
                      double                        xmax,
                      int                           n   = 200,
                      std::string_view              fmt = "-");

    // Chunked line series — for large datasets and streaming (ROS2, PX4, etc.)
    ChunkedLineSeries& chunked_line();

    // Statistical plot series creation
    BoxPlotSeries&   box_plot();
    ViolinSeries&    violin();
    HistogramSeries& histogram(std::span<const float> values, int bins = 30);
    BarSeries&       bar(std::span<const float> positions, std::span<const float> heights);
    BandSeries&      band(std::span<const float> x,
                          std::span<const float> lower,
                          std::span<const float> upper);
    StemSeries&      stem(std::span<const float> x, std::span<const float> y);

    // Shape annotation series (rectangles, circles, arrows, polygons, etc.)
    ShapeSeries& shapes();

    // Axis configuration
    void xlim(double min, double max);
    void ylim(double min, double max);
    void clear_ylim();
    void title(const std::string& t);
    void xlabel(const std::string& lbl);
    void ylabel(const std::string& lbl);
    void grid(bool enabled);
    void show_border(bool enabled);
    void autoscale_mode(AutoscaleMode mode);
    void presented_buffer(float seconds);
    void set_presented_buffer_right_edge(double x);

    // Axis scale type (linear, log10, log2, sqrt)
    void      xscale(ScaleType s) { xscale_ = s; }
    ScaleType xscale() const { return xscale_; }
    void      yscale(ScaleType s) { yscale_ = s; }
    ScaleType yscale() const { return yscale_; }

    // Accessors
    AxisLimits         x_limits() const;
    AxisLimits         y_limits() const;
    const std::string& title() const { return title_; }
    const std::string& xlabel() const { return xlabel_; }
    const std::string& ylabel() const { return ylabel_; }
    bool               grid_enabled() const { return grid_enabled_; }
    bool               border_enabled() const { return border_enabled_; }
    AutoscaleMode      autoscale_mode() const { return autoscale_mode_; }
    bool has_presented_buffer() const { return presented_buffer_seconds_.has_value(); }
    bool is_presented_buffer_following() const
    {
        return presented_buffer_following_ && presented_buffer_seconds_.has_value();
    }
    float presented_buffer_seconds() const { return presented_buffer_seconds_.value_or(0.0f); }

    // Re-enable live X-follow without clearing any manual Y override.
    // No-op if no presented_buffer has been configured.
    void resume_follow();

    // Deprecated aliases
    const std::string& get_title() const { return title_; }
    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    AutoscaleMode      get_autoscale_mode() const { return autoscale_mode_; }

    // Tick computation
    TickResult compute_x_ticks() const;
    TickResult compute_y_ticks() const;

    // Auto-fit limits to data
    void auto_fit() override;

    // ── Continuous topic-driven auto-zoom ──
    //
    // When enabled (typically by the Topics drag-drop / subscribe path), the
    // session runtime calls topic_auto_zoom_tick() each frame after thread-safe
    // series have been committed.  The tick re-runs auto_fit() so the view
    // keeps following the live data extent.  If the user pans or zooms (which
    // mutates xlim_/ylim_ to values that differ from the last auto-fit result),
    // auto-zoom is automatically disabled so the user's view is preserved.
    void set_topic_auto_zoom(bool enabled);
    bool topic_auto_zoom() const { return topic_auto_zoom_; }
    void topic_auto_zoom_tick();

   private:
    // Internal helper: construct T(args...), assign default cycle color, push to series_
    template <typename T, typename... Args>
    T& add_series(Args&&... args);

    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;

    std::string           xlabel_;
    std::string           ylabel_;
    ScaleType             xscale_         = ScaleType::Linear;
    ScaleType             yscale_         = ScaleType::Linear;
    AutoscaleMode         autoscale_mode_ = AutoscaleMode::Padded;
    std::optional<float>  presented_buffer_seconds_;
    bool                  presented_buffer_following_ = false;
    std::optional<double> presented_buffer_right_edge_;

    // Topic-driven continuous auto-zoom state (see set_topic_auto_zoom).
    bool                      topic_auto_zoom_ = false;
    std::optional<AxisLimits> last_auto_xlim_;
    std::optional<AxisLimits> last_auto_ylim_;
};

}   // namespace spectra
