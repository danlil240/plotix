#include <cmath>

#include <algorithm>
#include <format>
#include <limits>
#include <vector>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/event_bus.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>

namespace spectra
{

// --- Safe series removal ---

void AxesBase::clear_series()
{
    for (auto& s : series_)
    {
        if (on_series_removed_)
            on_series_removed_(s.get());
        if (event_system_)
            event_system_->series_removed().emit({this, s.get()});
    }
    series_.clear();
}

bool AxesBase::remove_series(size_t index)
{
    if (index >= series_.size())
        return false;
    auto* ptr = series_[index].get();
    if (on_series_removed_)
        on_series_removed_(ptr);
    if (event_system_)
        event_system_->series_removed().emit({this, ptr});
    series_.erase(series_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

bool AxesBase::move_series(size_t from, size_t to)
{
    if (from >= series_.size() || to >= series_.size() || from == to)
        return false;
    auto tmp = std::move(series_[from]);
    series_.erase(series_.begin() + static_cast<ptrdiff_t>(from));
    series_.insert(series_.begin() + static_cast<ptrdiff_t>(to), std::move(tmp));
    return true;
}

// --- Internal series factory ---

template <typename T, typename... Args>
T& Axes::add_series(Args&&... args)
{
    auto  s   = std::make_unique<T>(std::forward<Args>(args)...);
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    ref.set_event_context(event_system_, this);
    series_.push_back(std::move(s));
    if (event_system_)
        event_system_->series_added().emit({this, &ref});
    return ref;
}

// --- Series creation ---

LineSeries& Axes::line(std::span<const float> x, std::span<const float> y)
{
    return add_series<LineSeries>(x, y);
}

LineSeries& Axes::line()
{
    return add_series<LineSeries>();
}

ScatterSeries& Axes::scatter(std::span<const float> x, std::span<const float> y)
{
    return add_series<ScatterSeries>(x, y);
}

ScatterSeries& Axes::scatter()
{
    return add_series<ScatterSeries>();
}

ChunkedLineSeries& Axes::chunked_line()
{
    return add_series<ChunkedLineSeries>();
}

// --- MATLAB-style plot ---

LineSeries& Axes::plot(std::span<const float> x, std::span<const float> y, std::string_view fmt)
{
    auto& ref = line(x, y);
    ref.format(fmt);
    return ref;
}

LineSeries& Axes::plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style)
{
    auto& ref = line(x, y);
    ref.plot_style(style);
    return ref;
}

namespace
{
constexpr float k_ref_line_span = 1e12f;
}   // namespace

LineSeries& Axes::hline(double y, std::string_view fmt)
{
    const float xs[] = {-k_ref_line_span, k_ref_line_span};
    const float ys[] = {static_cast<float>(y), static_cast<float>(y)};
    auto&       line = plot(xs, ys, fmt);
    line.set_excluded_from_autoscale(true);
    line.set_show_in_legend(false);
    line.set_reference_line(true);
    return line;
}

LineSeries& Axes::vline(double x, std::string_view fmt)
{
    const float xs[] = {static_cast<float>(x), static_cast<float>(x)};
    const float ys[] = {-k_ref_line_span, k_ref_line_span};
    auto&       line = plot(xs, ys, fmt);
    line.set_excluded_from_autoscale(true);
    line.set_show_in_legend(false);
    line.set_reference_line(true);
    return line;
}

LineSeries& Axes::fplot(std::function<double(double)> func,
                        double                        xmin,
                        double                        xmax,
                        int                           n,
                        std::string_view              fmt)
{
    n = std::max(n, 2);
    std::vector<float> xs(static_cast<size_t>(n));
    std::vector<float> ys(static_cast<size_t>(n));
    const double       dx = (xmax - xmin) / static_cast<double>(n - 1);
    for (int i = 0; i < n; ++i)
    {
        const double x             = xmin + dx * static_cast<double>(i);
        xs[static_cast<size_t>(i)] = static_cast<float>(x);
        ys[static_cast<size_t>(i)] = static_cast<float>(func(x));
    }
    return plot(xs, ys, fmt);
}

// --- Statistical series creation ---

BoxPlotSeries& Axes::box_plot()
{
    return add_series<BoxPlotSeries>();
}

ViolinSeries& Axes::violin()
{
    return add_series<ViolinSeries>();
}

HistogramSeries& Axes::histogram(std::span<const float> values, int bins)
{
    return add_series<HistogramSeries>(values, bins);
}

BarSeries& Axes::bar(std::span<const float> positions, std::span<const float> heights)
{
    return add_series<BarSeries>(positions, heights);
}

BandSeries& Axes::band(std::span<const float> x,
                       std::span<const float> lower,
                       std::span<const float> upper)
{
    return add_series<BandSeries>(x, lower, upper);
}

StemSeries& Axes::stem(std::span<const float> x, std::span<const float> y)
{
    auto& ref = add_series<StemSeries>();
    ref.set_data(x, y);
    return ref;
}

ShapeSeries& Axes::shapes()
{
    return add_series<ShapeSeries>();
}

// --- Axis configuration ---

void Axes::xlim(double min, double max)
{
    // Explicit X limits freeze live follow, but keep the configured buffer so
    // users can resume via the Live button.
    presented_buffer_following_ = false;
    xlim_                       = AxisLimits{min, max};
    if (event_system_)
    {
        auto xl = x_limits();
        auto yl = y_limits();
        event_system_->axes_limits_changed().emit({this, xl.min, xl.max, yl.min, yl.max});
    }
}

void Axes::ylim(double min, double max)
{
    // Y overrides are independent from live X-follow: callers can keep a
    // streaming time window while locking or zooming the Y axis manually.
    ylim_ = AxisLimits{min, max};
    if (event_system_)
    {
        auto xl = x_limits();
        auto yl = y_limits();
        event_system_->axes_limits_changed().emit({this, xl.min, xl.max, yl.min, yl.max});
    }
}

void Axes::clear_ylim()
{
    ylim_.reset();
}

void Axes::title(const std::string& t)
{
    title_ = t;
}

void Axes::xlabel(const std::string& lbl)
{
    xlabel_ = lbl;
}

void Axes::ylabel(const std::string& lbl)
{
    ylabel_ = lbl;
}

void Axes::grid(bool enabled)
{
    grid_enabled_ = enabled;
}

void Axes::show_border(bool enabled)
{
    border_enabled_ = enabled;
}

void Axes::autoscale_mode(AutoscaleMode mode)
{
    if (mode == AutoscaleMode::Manual && autoscale_mode_ != AutoscaleMode::Manual)
    {
        // Switching TO Manual: freeze current computed limits so pan/zoom works
        if (!xlim_.has_value())
        {
            auto lim = x_limits();
            xlim_    = lim;
        }
        if (!ylim_.has_value())
        {
            auto lim = y_limits();
            ylim_    = lim;
        }
        // Manual mode should not keep a moving streaming window.
        presented_buffer_following_ = false;
    }
    else if (mode != AutoscaleMode::Manual)
    {
        // Switching to an auto mode: clear explicit limits so the
        // auto-computed limits take effect immediately.
        xlim_.reset();
        ylim_.reset();
    }
    autoscale_mode_ = mode;
}

void Axes::presented_buffer(float seconds)
{
    if (seconds > 0.0f)
    {
        presented_buffer_seconds_   = seconds;
        presented_buffer_following_ = true;
        // Presented buffer drives the X window from data, so clear any manual
        // X override but preserve manual Y if the user explicitly set one.
        xlim_.reset();
        if (autoscale_mode_ == AutoscaleMode::Manual)
            autoscale_mode_ = AutoscaleMode::Padded;
    }
    else
    {
        presented_buffer_seconds_.reset();
        presented_buffer_following_ = false;
        presented_buffer_right_edge_.reset();
    }
}

void Axes::resume_follow()
{
    if (presented_buffer_seconds_.has_value())
    {
        presented_buffer_following_ = true;
        xlim_.reset();
    }
}

void Axes::set_presented_buffer_right_edge(double x)
{
    if (std::isfinite(x))
        presented_buffer_right_edge_ = x;
}

// --- Limits ---

static void consume_finite_extent(std::span<const float> values,
                                  double&                min_value,
                                  double&                max_value,
                                  double                 offset = 0.0)
{
    for (float value : values)
    {
        if (!std::isfinite(value))
            continue;
        const double adjusted = static_cast<double>(value) + offset;
        min_value             = std::min(min_value, adjusted);
        max_value             = std::max(max_value, adjusted);
    }
}

// Compute data extent across all series
static void data_extent(const std::vector<std::unique_ptr<Series>>& series,
                        double&                                     x_min,
                        double&                                     x_max,
                        double&                                     y_min,
                        double&                                     y_max)
{
    x_min = std::numeric_limits<double>::max();
    x_max = -std::numeric_limits<double>::max();
    y_min = std::numeric_limits<double>::max();
    y_max = -std::numeric_limits<double>::max();

    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;

        // Per-series double x-offset: logical x = x_offset + stored float.
        // Accumulating in double preserves precision for epoch-scale offsets.
        const double xoff = s->x_offset();

        // Try LineSeries
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
        {
            consume_finite_extent(ls->x_data(), x_min, x_max, xoff);
            consume_finite_extent(ls->y_data(), y_min, y_max);
        }
        // Try ScatterSeries
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
        {
            consume_finite_extent(ss->x_data(), x_min, x_max, xoff);
            consume_finite_extent(ss->y_data(), y_min, y_max);
        }
        // Try statistical series (all expose x_data/y_data with NaN breaks)
        auto try_stat = [&](std::span<const float> xd, std::span<const float> yd)
        {
            consume_finite_extent(xd, x_min, x_max, xoff);
            consume_finite_extent(yd, y_min, y_max);
        };
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
        {
            try_stat(bp->x_data(), bp->y_data());
            // Include outlier extents
            consume_finite_extent(bp->outlier_y(), y_min, y_max);
        }
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            try_stat(vn->x_data(), vn->y_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            try_stat(hs->x_data(), hs->y_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            try_stat(bs->x_data(), bs->y_data());
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
            try_stat(band->x_data(), band->y_data());
        if (auto* sh = dynamic_cast<const ShapeSeries*>(s.get()))
            try_stat(sh->x_data(), sh->y_data());
    }

    // Fallback if no data
    if (x_min > x_max)
    {
        x_min = 0.0;
        x_max = 1.0;
    }
    if (y_min > y_max)
    {
        y_min = 0.0;
        y_max = 1.0;
    }

    // Add 5% padding
    double x_pad = (x_max - x_min) * 0.05;
    double y_pad = (y_max - y_min) * 0.05;
    if (x_pad == 0.0)
        x_pad = 0.5;
    if (y_pad == 0.0)
        y_pad = 0.5;
    x_min -= x_pad;
    x_max += x_pad;
    y_min -= y_pad;
    y_max += y_pad;
}

static void data_extent_with_mode(const std::vector<std::unique_ptr<Series>>& series,
                                  AutoscaleMode                               mode,
                                  double&                                     x_min,
                                  double&                                     x_max,
                                  double&                                     y_min,
                                  double&                                     y_max)
{
    // Compute raw extent
    data_extent(series, x_min, x_max, y_min, y_max);

    if (mode == AutoscaleMode::Tight)
    {
        // Re-compute without padding (data_extent adds 5% padding)
        x_min = std::numeric_limits<double>::max();
        x_max = -std::numeric_limits<double>::max();
        y_min = std::numeric_limits<double>::max();
        y_max = -std::numeric_limits<double>::max();
        for (const auto& s : series)
        {
            if (s->excluded_from_autoscale())
                continue;
            const double xoff = s->x_offset();
            if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            {
                consume_finite_extent(ls->x_data(), x_min, x_max, xoff);
                consume_finite_extent(ls->y_data(), y_min, y_max);
            }
            if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            {
                consume_finite_extent(ss->x_data(), x_min, x_max, xoff);
                consume_finite_extent(ss->y_data(), y_min, y_max);
            }
            if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
            {
                consume_finite_extent(band->x_values(), x_min, x_max, xoff);
                consume_finite_extent(band->lower_values(), y_min, y_max);
                consume_finite_extent(band->upper_values(), y_min, y_max);
            }
        }
        if (x_min > x_max)
        {
            x_min = 0.0;
            x_max = 1.0;
        }
        if (y_min > y_max)
        {
            y_min = 0.0;
            y_max = 1.0;
        }
        // Handle zero range
        if (x_max == x_min)
        {
            x_min -= 0.5;
            x_max += 0.5;
        }
        if (y_max == y_min)
        {
            y_min -= 0.5;
            y_max += 0.5;
        }
    }
    // Fit and Padded use the default data_extent behavior (with padding)
}

static bool latest_x_value(const std::vector<std::unique_ptr<Series>>& series, double& latest_x)
{
    bool has_value = false;

    double current_xoff = 0.0;

    auto update_latest = [&](float x)
    {
        if (!std::isfinite(x))
            return;
        const double abs_x = static_cast<double>(x) + current_xoff;
        if (!has_value || abs_x > latest_x)
        {
            latest_x  = abs_x;
            has_value = true;
        }
    };

    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;
        current_xoff = s->x_offset();
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
        {
            for (float x : ls->x_data())
                update_latest(x);
        }
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
        {
            for (float x : ss->x_data())
                update_latest(x);
        }

        auto update_from_span = [&](std::span<const float> xd)
        {
            for (float x : xd)
                update_latest(x);
        };
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
            update_from_span(bp->x_data());
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            update_from_span(vn->x_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            update_from_span(hs->x_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            update_from_span(bs->x_data());
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
            update_from_span(band->x_values());
    }

    return has_value;
}

static bool windowed_y_extent(const std::vector<std::unique_ptr<Series>>& series,
                              double                                      window_min,
                              double                                      window_max,
                              float&                                      y_min,
                              float&                                      y_max)
{
    y_min      = std::numeric_limits<float>::max();
    y_max      = -std::numeric_limits<float>::max();
    bool has_y = false;

    double current_xoff = 0.0;

    auto consume_xy = [&](std::span<const float> xd, std::span<const float> yd)
    {
        size_t n = std::min(xd.size(), yd.size());
        for (size_t i = 0; i < n; ++i)
        {
            float x = xd[i];
            float y = yd[i];
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            const double abs_x = static_cast<double>(x) + current_xoff;
            if (abs_x < window_min || abs_x > window_max)
                continue;
            y_min = std::min(y_min, y);
            y_max = std::max(y_max, y);
            has_y = true;
        }
    };

    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;
        current_xoff = s->x_offset();
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            consume_xy(ls->x_data(), ls->y_data());
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            consume_xy(ss->x_data(), ss->y_data());
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
            consume_xy(bp->x_data(), bp->y_data());
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            consume_xy(vn->x_data(), vn->y_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            consume_xy(hs->x_data(), hs->y_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            consume_xy(bs->x_data(), bs->y_data());
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
        {
            consume_xy(band->x_values(), band->lower_values());
            consume_xy(band->x_values(), band->upper_values());
        }
        if (auto* sh = dynamic_cast<const ShapeSeries*>(s.get()))
            consume_xy(sh->x_data(), sh->y_data());
    }

    return has_y;
}

AxisLimits Axes::x_limits() const
{
    if (presented_buffer_following_ && presented_buffer_seconds_.has_value()
        && presented_buffer_seconds_.value() > 0.0f)
    {
        if (presented_buffer_right_edge_.has_value())
        {
            const double right = presented_buffer_right_edge_.value();
            const double left  = right - static_cast<double>(presented_buffer_seconds_.value());
            return {left, right};
        }

        double latest_x = 0.0;
        if (latest_x_value(series_, latest_x))
        {
            return {latest_x - static_cast<double>(presented_buffer_seconds_.value()), latest_x};
        }
    }

    if (xlim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return xlim_.value_or(AxisLimits{0.0, 1.0});
    double xmin = NAN;
    double xmax = NAN;
    double ymin = NAN;
    double ymax = NAN;
    data_extent_with_mode(series_, autoscale_mode_, xmin, xmax, ymin, ymax);
    return {xmin, xmax};
}

AxisLimits Axes::y_limits() const
{
    if (ylim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return ylim_.value_or(AxisLimits{0.0, 1.0});

    if (presented_buffer_seconds_.has_value() && presented_buffer_seconds_.value() > 0.0f)
    {
        double window_min  = 0.0;
        double window_max  = 0.0;
        bool   have_window = false;

        if (presented_buffer_following_)
        {
            if (presented_buffer_right_edge_.has_value())
            {
                window_max  = presented_buffer_right_edge_.value();
                window_min  = window_max - static_cast<double>(presented_buffer_seconds_.value());
                have_window = true;
            }
            else
            {
                double latest_x = 0.0;
                if (latest_x_value(series_, latest_x))
                {
                    window_min  = latest_x - static_cast<double>(presented_buffer_seconds_.value());
                    window_max  = latest_x;
                    have_window = true;
                }
            }
        }
        else if (xlim_.has_value())
        {
            window_min  = xlim_->min;
            window_max  = xlim_->max;
            have_window = true;
        }

        if (have_window)
        {
            float y_min = 0.0f;
            float y_max = 0.0f;
            if (windowed_y_extent(series_, window_min, window_max, y_min, y_max))
            {
                if (autoscale_mode_ == AutoscaleMode::Tight)
                    return {y_min, y_max};

                float y_pad = (y_max - y_min) * 0.05f;
                if (y_pad == 0.0f)
                    y_pad = 0.5f;
                return {y_min - y_pad, y_max + y_pad};
            }
        }
    }

    double xmin = NAN;
    double xmax = NAN;
    double ymin = NAN;
    double ymax = NAN;
    data_extent_with_mode(series_, autoscale_mode_, xmin, xmax, ymin, ymax);
    return {ymin, ymax};
}

void Axes::auto_fit()
{
    xlim_.reset();
    ylim_.reset();
}

void Axes::set_topic_auto_zoom(bool enabled)
{
    topic_auto_zoom_ = enabled;
    if (!enabled)
    {
        last_auto_xlim_.reset();
        last_auto_ylim_.reset();
    }
}

void Axes::topic_auto_zoom_tick()
{
    if (!topic_auto_zoom_)
        return;

    // ── Step 1: detect user override ────────────────────────────────────────
    //
    // If the limits we set on the previous tick no longer match the current
    // ones (i.e. user panned/zoomed/typed an explicit range), disable
    // auto-zoom so the user's view is preserved.
    const AxisLimits cur_x = x_limits();
    const AxisLimits cur_y = y_limits();

    if (last_auto_xlim_.has_value() && last_auto_ylim_.has_value())
    {
        const AxisLimits& lx    = *last_auto_xlim_;
        const AxisLimits& ly    = *last_auto_ylim_;
        const double      eps_x = std::max(std::abs(lx.max - lx.min), 1.0) * 1e-9;
        const double      eps_y = std::max(std::abs(ly.max - ly.min), 1.0) * 1e-9;
        if (std::abs(cur_x.min - lx.min) > eps_x || std::abs(cur_x.max - lx.max) > eps_x
            || std::abs(cur_y.min - ly.min) > eps_y || std::abs(cur_y.max - ly.max) > eps_y)
        {
            topic_auto_zoom_ = false;
            last_auto_xlim_.reset();
            last_auto_ylim_.reset();
            return;
        }
    }

    // ── Step 2: recompute auto-fit limits from current data ────────────────
    //
    // Temporarily clear any previously-installed explicit limits so x_limits/
    // y_limits return the live data extent.
    const auto saved_x = xlim_;
    const auto saved_y = ylim_;
    xlim_.reset();
    ylim_.reset();

    const AxisLimits new_x = x_limits();
    const AxisLimits new_y = y_limits();

    if (!std::isfinite(new_x.min) || !std::isfinite(new_x.max) || !std::isfinite(new_y.min)
        || !std::isfinite(new_y.max))
    {
        // Degenerate; restore previous explicit limits and try again next frame.
        xlim_ = saved_x;
        ylim_ = saved_y;
        return;
    }

    xlim_           = new_x;
    ylim_           = new_y;
    last_auto_xlim_ = new_x;
    last_auto_ylim_ = new_y;

    if (event_system_)
        event_system_->axes_limits_changed().emit(
            {this, new_x.min, new_x.max, new_y.min, new_y.max});
}

// --- Tick generation ---
// Simple "nice numbers" algorithm: pick tick spacing as 1, 2, or 5 × 10^n
// to produce roughly 5–10 ticks in the given range.

static double nice_ceil_d(double x, bool round_flag)
{
    double exp_v = std::floor(std::log10(x));
    double frac  = x / std::pow(10.0, exp_v);
    double nice  = NAN;
    if (round_flag)
    {
        if (frac < 1.5)
            nice = 1.0;
        else if (frac < 3.0)
            nice = 2.0;
        else if (frac < 7.0)
            nice = 5.0;
        else
            nice = 10.0;
    }
    else
    {
        if (frac <= 1.0)
            nice = 1.0;
        else if (frac <= 2.0)
            nice = 2.0;
        else if (frac <= 5.0)
            nice = 5.0;
        else
            nice = 10.0;
    }
    return nice * std::pow(10.0, exp_v);
}

// Format a tick value smartly: use enough decimal digits so that ticks at
// the given spacing are distinguishable.  Falls back to scientific notation
// when the offset is large relative to the spacing (deep-zoom regime).
static std::string format_tick_value(double value, double spacing)
{
    // Snap near-zero to exactly zero
    if (std::abs(value) < spacing * 1e-6)
    {
        return "0";
    }

    double abs_val     = std::abs(value);
    double abs_spacing = std::abs(spacing);

    // How many significant digits do we need?
    // We need enough digits to distinguish value from value±spacing.
    // digits_needed = ceil(-log10(spacing)) + 1, but at least 1.
    int digits_after_decimal = 0;
    if (abs_spacing > 0 && std::isfinite(abs_spacing))
    {
        digits_after_decimal = static_cast<int>(std::ceil(-std::log10(abs_spacing))) + 1;
        if (digits_after_decimal < 0)
            digits_after_decimal = 0;
    }

    // If the absolute value is much larger than the spacing, we need
    // scientific/engineering notation to show the difference.
    // e.g. value=7.9000012, spacing=1e-6 → need "7.9000012" not "7.9"
    int total_sig_digits = 0;
    if (abs_val > 0 && abs_spacing > 0)
    {
        // Total significant digits = log10(abs_val / abs_spacing) + 1
        total_sig_digits = static_cast<int>(std::ceil(std::log10(abs_val / abs_spacing))) + 2;
        if (total_sig_digits < 4)
            total_sig_digits = 4;
        if (total_sig_digits > 15)
            total_sig_digits = 15;
    }
    else
    {
        total_sig_digits = 6;
    }

    // Use fixed notation if it results in a reasonable string,
    // otherwise switch to scientific notation.  The 1e12 bound keeps
    // epoch-scale timestamps (~1.7e9) in plain fixed notation like
    // PlotJuggler/rqt instead of scientific notation.
    if (digits_after_decimal <= 9 && abs_val < 1e12 && abs_val >= 0.001)
    {
        // Fixed notation with enough decimals
        std::string str = std::format("{:.{}f}", value, digits_after_decimal);
        // Trim trailing zeros after decimal point, but keep at least
        // min_digits digits so all ticks at this spacing have consistent
        // digit counts (e.g. "6.0819710" stays, not "6.081971").
        int min_digits = 0;
        if (abs_spacing > 0 && std::isfinite(abs_spacing))
        {
            min_digits = static_cast<int>(std::ceil(-std::log10(abs_spacing)));
            if (min_digits < 0)
                min_digits = 0;
        }
        auto dot_pos = str.find('.');
        if (dot_pos != std::string::npos)
        {
            size_t current_decimals = str.size() - dot_pos - 1;
            while (current_decimals > static_cast<size_t>(min_digits) && str.back() == '0')
            {
                str.pop_back();
                current_decimals--;
            }
            if (str.back() == '.')
                str.pop_back();
        }
        return str;
    }

    // Scientific notation with enough significant digits
    return std::format("{:.{}e}", value, total_sig_digits - 1);
}

static TickResult generate_ticks(double dmin, double dmax, int target_ticks = 7)
{
    TickResult result;

    double range = dmax - dmin;

    // Edge case: zero or negative range
    if (range <= 0.0)
    {
        if (range == 0.0 && dmin != 0.0)
        {
            double half = std::abs(dmin) * 0.1;
            if (half == 0.0)
                half = 0.5;
            return generate_ticks(dmin - half, dmin + half, target_ticks);
        }
        result.positions.push_back(dmin);
        result.labels.push_back(format_tick_value(dmin, 1.0));
        return result;
    }

    // Minimum range: limited by double precision of the values themselves.
    // For value V stored as double, the smallest distinguishable step is
    // ~|V| * DBL_EPSILON.  Below that, ticks would be identical.
    double abs_max   = std::max(std::abs(dmin), std::abs(dmax));
    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
    if (min_range < 1e-300)
        min_range = 1e-300;   // absolute floor for values near zero

    if (range < min_range)
    {
        // Range is at double precision limit — show a single centered tick
        double mid = (dmin + dmax) * 0.5;
        result.positions.push_back(mid);
        result.labels.push_back(format_tick_value(mid, range));
        return result;
    }

    double nice_range = nice_ceil_d(range, false);
    double spacing    = nice_ceil_d(nice_range / static_cast<double>(target_ticks - 1), true);

    // Guard against degenerate spacing
    if (spacing <= 0.0 || !std::isfinite(spacing))
    {
        result.positions.push_back(dmin);
        result.labels.push_back(format_tick_value(dmin, range));
        return result;
    }

    double nice_min = std::floor(dmin / spacing) * spacing;
    double nice_max = std::ceil(dmax / spacing) * spacing;

    // Safety: cap iterations to avoid infinite loops
    int max_iters = target_ticks * 3;
    int iters     = 0;
    for (double v = nice_min; v <= nice_max + spacing * 0.5 && iters < max_iters;
         v += spacing, ++iters)
    {
        if (v >= dmin - spacing * 0.01 && v <= dmax + spacing * 0.01)
        {
            // Snap near-zero values to exactly zero to avoid "-0" labels
            if (std::abs(v) < spacing * 1e-6)
                v = 0.0;
            result.positions.push_back(v);
            result.labels.push_back(format_tick_value(v, spacing));
        }
    }

    return result;
}

TickResult Axes::compute_x_ticks() const
{
    auto lim = x_limits();
    if (xscale_ == ScaleType::Log10 || xscale_ == ScaleType::Log2)
    {
        double base = (xscale_ == ScaleType::Log2) ? 2.0 : 10.0;
        // Generate nice logarithmic ticks
        TickResult result;
        double     log_min = std::log(lim.min) / std::log(base);
        double     log_max = std::log(lim.max) / std::log(base);
        // Clamp to safe range if limits include non-positive values
        if (!std::isfinite(log_min) || !std::isfinite(log_max))
        {
            result.positions.push_back(lim.min);
            result.labels.push_back(format_tick_value(lim.min, lim.max - lim.min));
            return result;
        }
        int lo_exp = static_cast<int>(std::floor(log_min));
        int hi_exp = static_cast<int>(std::ceil(log_max));
        for (int e = lo_exp; e <= hi_exp; ++e)
        {
            double v = std::pow(base, static_cast<double>(e));
            if (v >= lim.min * 0.99 && v <= lim.max * 1.01)
            {
                result.positions.push_back(v);
                result.labels.push_back(format_tick_value(v, v * 0.1));
            }
        }
        if (result.positions.empty())
        {
            result.positions.push_back(lim.min);
            result.labels.push_back(format_tick_value(lim.min, lim.max - lim.min));
        }
        return result;
    }
    if (xscale_ == ScaleType::Sqrt)
    {
        // Map to squared space, generate linear ticks, map back
        double     smin  = lim.min * lim.min;
        double     smax  = lim.max * lim.max;
        auto       inner = generate_ticks(smin, smax);
        TickResult result;
        for (auto& p : inner.positions)
            result.positions.push_back(std::sqrt(std::max(0.0, p)));
        result.labels = std::move(inner.labels);
        return result;
    }
    return generate_ticks(lim.min, lim.max);
}

TickResult Axes::compute_y_ticks() const
{
    auto lim = y_limits();
    if (yscale_ == ScaleType::Log10 || yscale_ == ScaleType::Log2)
    {
        double     base = (yscale_ == ScaleType::Log2) ? 2.0 : 10.0;
        TickResult result;
        double     log_min = std::log(lim.min) / std::log(base);
        double     log_max = std::log(lim.max) / std::log(base);
        if (!std::isfinite(log_min) || !std::isfinite(log_max))
        {
            result.positions.push_back(lim.min);
            result.labels.push_back(format_tick_value(lim.min, lim.max - lim.min));
            return result;
        }
        int lo_exp = static_cast<int>(std::floor(log_min));
        int hi_exp = static_cast<int>(std::ceil(log_max));
        for (int e = lo_exp; e <= hi_exp; ++e)
        {
            double v = std::pow(base, static_cast<double>(e));
            if (v >= lim.min * 0.99 && v <= lim.max * 1.01)
            {
                result.positions.push_back(v);
                result.labels.push_back(format_tick_value(v, v * 0.1));
            }
        }
        if (result.positions.empty())
        {
            result.positions.push_back(lim.min);
            result.labels.push_back(format_tick_value(lim.min, lim.max - lim.min));
        }
        return result;
    }
    if (yscale_ == ScaleType::Sqrt)
    {
        double     smin  = lim.min * lim.min;
        double     smax  = lim.max * lim.max;
        auto       inner = generate_ticks(smin, smax);
        TickResult result;
        for (auto& p : inner.positions)
            result.positions.push_back(std::sqrt(std::max(0.0, p)));
        result.labels = std::move(inner.labels);
        return result;
    }
    return generate_ticks(lim.min, lim.max);
}

}   // namespace spectra
