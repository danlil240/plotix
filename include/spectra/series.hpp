#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/plot_style.hpp>
#include <string>
#include <vector>

namespace spectra
{

class PendingSeriesData;

// Custom deleter for PendingSeriesData (allows incomplete type in unique_ptr).
struct PendingSeriesDataDeleter
{
    void operator()(PendingSeriesData* ptr) const;
};

struct SeriesStyle
{
    Color color      = colors::blue;
    float line_width = 2.0f;
    float point_size = 4.0f;
    float opacity    = 1.0f;
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class Series
{
   public:
    virtual ~Series();

    Series() = default;

    // Move operations — deleted because std::atomic<bool> is not movable.
    // Series instances are managed by pointer/reference; moves are not needed.
    Series(Series&&)            = delete;
    Series& operator=(Series&&) = delete;

    // Copy operations — pending buffer is NOT copied (thread-safe state
    // is per-instance and must be re-enabled on copies).
    Series(const Series& other);
    Series& operator=(const Series& other);

    Series& label(const std::string& lbl)
    {
        label_ = lbl;
        return *this;
    }
    Series& color(const Color& c)
    {
        color_ = c;
        dirty_ = true;
        return *this;
    }
    Series& visible(bool v)
    {
        visible_ = v;
        return *this;
    }

    const std::string& label() const { return label_; }
    const Color&       color() const { return color_; }
    bool               visible() const { return visible_; }

    // Reference lines (hline/vline) are excluded from autoscale calculations
    // so their large span does not pollute axis limits.
    bool excluded_from_autoscale() const { return excluded_from_autoscale_; }
    void set_excluded_from_autoscale(bool v) { excluded_from_autoscale_ = v; }

    // ── Double-precision x-axis offset ──
    // Logical x value = x_offset() + x_data()[i].  Allows plotting data with
    // large absolute x values (e.g. epoch timestamps ~1.7e9) that exceed
    // float's ~7-digit precision: the stored floats stay small and precise
    // while axis limits, ticks, and cursor readouts show absolute values.
    Series& x_offset(double off)
    {
        x_offset_ = off;
        dirty_    = true;
        return *this;
    }
    double x_offset() const { return x_offset_; }

    // Control whether this series appears in the legend.
    bool show_in_legend() const { return show_in_legend_; }
    void set_show_in_legend(bool v) { show_in_legend_ = v; }

    // True for Axes::hline / Axes::vline reference lines (not general legend hiding).
    bool is_reference_line() const { return is_reference_line_; }
    void set_reference_line(bool v) { is_reference_line_ = v; }

    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }
    void mark_dirty();

    // ── Thread-safe data access (opt-in) ──
    // When enabled, set_x/set_y/append/erase_before route through a pending
    // buffer that is committed atomically at frame boundary.  Default off
    // (zero overhead for single-threaded callers).
    void set_thread_safe(bool enabled);
    bool is_thread_safe() const { return thread_safe_; }

    // Apply pending data from background threads.  Called by SessionRuntime
    // at frame boundary.  Returns true if data was committed.
    virtual bool commit_pending();

    // Set a callback invoked when background data arrives (for waking idle loops).
    void set_wake_fn(std::function<void()> fn);
    void set_color(const Color& c)
    {
        color_ = c;
        dirty_ = true;
    }

    // ── Event system integration ──
    // Called by AxesBase when the series is added to axes with an event system.
    void set_event_context(EventSystem* es, AxesBase* axes)
    {
        event_system_ = es;
        owning_axes_  = axes;
    }

    // ── Plot style (line style, marker style, etc.) ──
    Series& line_style(LineStyle s)
    {
        style_.line_style = s;
        dirty_            = true;
        return *this;
    }
    Series& marker_style(MarkerStyle s)
    {
        style_.marker_style = s;
        dirty_              = true;
        return *this;
    }
    Series& marker_size(float s)
    {
        style_.marker_size = s;
        dirty_             = true;
        return *this;
    }
    Series& opacity(float o)
    {
        style_.opacity = o;
        dirty_         = true;
        return *this;
    }
    Series& plot_style(const PlotStyle& ps);

    LineStyle        line_style() const { return style_.line_style; }
    MarkerStyle      marker_style() const { return style_.marker_style; }
    float            marker_size() const { return style_.marker_size; }
    float            opacity() const { return style_.opacity; }
    const PlotStyle& plot_style() const { return style_; }
    PlotStyle&       plot_style_mut() { return style_; }

    // ── Streaming data interface (optional; implemented by LineSeries / ChunkedLineSeries) ──
    virtual void   append(float /*x*/, float /*y*/) {}
    virtual size_t erase_before(float /*x_threshold*/) { return 0; }
    virtual size_t erase_after(float /*x_threshold*/) { return 0; }
    virtual size_t trim_to_max_points(size_t /*max_points*/) { return 0; }
    virtual size_t memory_bytes() const { return 0; }

   protected:
    Series& apply_format_string(std::string_view fmt);

    std::string       label_;
    Color             color_ = colors::blue;
    PlotStyle         style_;   // line/marker style, sizes, opacity
    bool              visible_                 = true;
    double            x_offset_                = 0.0;
    bool              excluded_from_autoscale_ = false;
    bool              show_in_legend_          = true;
    bool              is_reference_line_       = false;
    std::atomic<bool> dirty_{true};
    EventSystem*      event_system_ = nullptr;
    AxesBase*         owning_axes_  = nullptr;

    // Thread-safe data buffering (opt-in).
    bool                                                         thread_safe_ = false;
    std::unique_ptr<PendingSeriesData, PendingSeriesDataDeleter> pending_;
};

class LineSeries : public Series
{
   public:
    LineSeries() = default;
    LineSeries(std::span<const float> x, std::span<const float> y);

    LineSeries& set_x(std::span<const float> x);
    LineSeries& set_y(std::span<const float> y);
    void        append(float x, float y) override;

    LineSeries& width(float w)
    {
        line_width_ = w;
        dirty_      = true;
        return *this;
    }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t                 point_count() const { return std::min(x_.size(), y_.size()); }
    bool                   x_is_sorted() const { return x_sorted_; }

    // Remove all points with x < x_threshold.  Assumes x is sorted ascending.
    // Returns the number of points removed.
    size_t erase_before(float x_threshold) override;

    // Remove all points with x > x_threshold.  Assumes x is sorted ascending.
    size_t erase_after(float x_threshold) override;

    // Drop oldest points until at most max_points remain (FIFO).
    size_t trim_to_max_points(size_t max_points) override;

    // Estimated memory consumption in bytes (x + y vectors).
    size_t memory_bytes() const override { return x_.size() * 2 * sizeof(float); }

    // Bring base-class getters into scope (setters below would otherwise hide them)
    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    // Re-declare fluent setters with correct return type
    LineSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    LineSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    LineSeries& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    LineSeries& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    LineSeries& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    LineSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Apply a MATLAB-style format string (e.g. "r--o")
    LineSeries& format(std::string_view fmt);

    // Thread-safe commit override.
    bool commit_pending() override;

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    float              line_width_ = 2.0f;
    bool               x_sorted_   = true;
};

class ScatterSeries : public Series
{
   public:
    ScatterSeries() = default;
    ScatterSeries(std::span<const float> x, std::span<const float> y);

    ScatterSeries& set_x(std::span<const float> x);
    ScatterSeries& set_y(std::span<const float> y);
    void           append(float x, float y) override;

    ScatterSeries& size(float s)
    {
        point_size_ = s;
        dirty_      = true;
        return *this;
    }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t                 point_count() const { return std::min(x_.size(), y_.size()); }
    bool                   x_is_sorted() const { return x_sorted_; }

    // Bring base-class getters into scope (setters below would otherwise hide them)
    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    // Re-declare fluent setters with correct return type
    ScatterSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ScatterSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ScatterSeries& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    ScatterSeries& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    ScatterSeries& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    ScatterSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Apply a MATLAB-style format string (e.g. "ro")
    ScatterSeries& format(std::string_view fmt);

    // ── Colormap support (Phase 7C) ─────────────────────────────────────
    // Set per-point color values (one float per point).  Values are mapped
    // through the active colormap to produce per-point colors.
    ScatterSeries& color_values(std::span<const float> values);

    // Set the colormap type used to map color_values to RGBA.
    ScatterSeries& colormap(ColormapType cm)
    {
        colormap_type_ = cm;
        dirty_         = true;
        return *this;
    }
    ColormapType colormap() const { return colormap_type_; }

    // Set the colormap value range [min, max]. Values outside this range
    // are clamped. When not set, uses the data range of color_values_. */
    ScatterSeries& colormap_range(float min_val, float max_val)
    {
        colormap_min_ = min_val;
        colormap_max_ = max_val;
        colormap_set_ = true;
        dirty_        = true;
        return *this;
    }
    void clear_colormap_range()
    {
        colormap_set_ = false;
        dirty_        = true;
    }

    // Access per-point color values (one float per point).
    std::span<const float> color_values_data() const { return color_values_; }
    bool                   has_colormap() const
    {
        return colormap_type_ != ColormapType::None && color_values_.size() == x_.size()
               && x_.size() == y_.size() && !x_.empty();
    }
    float colormap_min() const { return colormap_min_; }
    float colormap_max() const { return colormap_max_; }
    bool  colormap_range_set() const { return colormap_set_; }

    // Thread-safe commit override.
    bool commit_pending() override;

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    float              point_size_ = 4.0f;
    bool               x_sorted_   = true;
    // Colormap support (Phase 7C)
    std::vector<float> color_values_;
    ColormapType       colormap_type_ = ColormapType::None;
    float              colormap_min_  = 0.0f;
    float              colormap_max_  = 1.0f;
    bool               colormap_set_  = false;
};

}   // namespace spectra
