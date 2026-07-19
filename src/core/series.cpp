#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/event_bus.hpp>
#include <spectra/series.hpp>

#include "pending_series_data.hpp"

namespace spectra
{

// --- Series (base) ---

void PendingSeriesDataDeleter::operator()(PendingSeriesData* ptr) const
{
    delete ptr;
}

Series::~Series() = default;

Series::Series(const Series& other)
    : label_(other.label_), color_(other.color_), style_(other.style_), visible_(other.visible_),
      x_offset_(other.x_offset_), excluded_from_autoscale_(other.excluded_from_autoscale_),
      show_in_legend_(other.show_in_legend_), is_reference_line_(other.is_reference_line_),
      dirty_(other.dirty_.load(std::memory_order_relaxed)), event_system_(other.event_system_),
      owning_axes_(other.owning_axes_), pending_(nullptr)
{
}

Series& Series::operator=(const Series& other)
{
    if (this != &other)
    {
        label_                   = other.label_;
        color_                   = other.color_;
        style_                   = other.style_;
        visible_                 = other.visible_;
        x_offset_                = other.x_offset_;
        excluded_from_autoscale_ = other.excluded_from_autoscale_;
        show_in_legend_          = other.show_in_legend_;
        is_reference_line_       = other.is_reference_line_;
        dirty_.store(other.dirty_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        event_system_ = other.event_system_;
        owning_axes_  = other.owning_axes_;
        // Thread-safe state is NOT copied — must be re-enabled explicitly.
    }
    return *this;
}

void Series::mark_dirty()
{
    dirty_ = true;
    if (event_system_)
        event_system_->series_data_changed().emit({owning_axes_, this});
}

void Series::set_thread_safe(bool enabled)
{
    thread_safe_ = enabled;
    if (enabled && !pending_)
        pending_.reset(new PendingSeriesData());
}

bool Series::commit_pending()
{
    return false;
}

void Series::set_wake_fn(std::function<void()> fn)
{
    if (pending_)
        pending_->set_wake_fn(std::move(fn));
}

Series& Series::plot_style(const PlotStyle& ps)
{
    style_ = ps;
    if (ps.color.has_value())
    {
        color_ = *ps.color;
    }
    dirty_ = true;
    return *this;
}

Series& Series::apply_format_string(std::string_view fmt)
{
    PlotStyle ps        = parse_format_string(fmt);
    style_.line_style   = ps.line_style;
    style_.marker_style = ps.marker_style;
    if (ps.color.has_value())
    {
        color_ = *ps.color;
    }
    dirty_ = true;
    return *this;
}

// --- LineSeries ---

LineSeries::LineSeries(std::span<const float> x, std::span<const float> y)
    : x_(x.begin(), x.end()), y_(y.begin(), y.end())
{
    x_sorted_ = std::is_sorted(x_.begin(), x_.end());
    dirty_    = true;
}

LineSeries& LineSeries::set_x(std::span<const float> x)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_x(x);
        return *this;
    }
    x_.assign(x.begin(), x.end());
    x_sorted_ = std::is_sorted(x_.begin(), x_.end());
    dirty_    = true;
    return *this;
}

LineSeries& LineSeries::set_y(std::span<const float> y)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_y(y);
        return *this;
    }
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void LineSeries::append(float x, float y)
{
    if (thread_safe_ && pending_)
    {
        pending_->append(x, y);
        return;
    }
    if (!x_.empty() && x < x_.back())
        x_sorted_ = false;
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

size_t LineSeries::erase_before(float x_threshold)
{
    if (thread_safe_ && pending_)
    {
        pending_->erase_before(x_threshold);
        return 0;   // Actual count available after commit.
    }
    const size_t count = point_count();
    if (count == 0)
        return 0;

    // Binary search for the first element >= x_threshold (x_ is sorted ascending).
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (x_[mid] < x_threshold)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return 0;

    x_.erase(x_.begin(), x_.begin() + static_cast<ptrdiff_t>(lo));
    y_.erase(y_.begin(), y_.begin() + static_cast<ptrdiff_t>(lo));
    dirty_ = true;
    return lo;
}

size_t LineSeries::erase_after(float x_threshold)
{
    if (thread_safe_ && pending_)
    {
        pending_->erase_after(x_threshold);
        return 0;
    }
    const size_t count = point_count();
    if (count == 0)
        return 0;

    auto         it     = std::upper_bound(x_.begin(), x_.begin() + count, x_threshold);
    const size_t keep   = static_cast<size_t>(it - x_.begin());
    const size_t remove = count - keep;
    if (remove == 0)
        return 0;

    x_.erase(it, it + static_cast<ptrdiff_t>(remove));
    y_.erase(y_.begin() + static_cast<ptrdiff_t>(keep),
             y_.begin() + static_cast<ptrdiff_t>(keep + remove));
    dirty_ = true;
    return remove;
}

size_t LineSeries::trim_to_max_points(size_t max_points)
{
    if (thread_safe_ && pending_)
    {
        pending_->trim_to_max_points(max_points);
        return 0;
    }
    const size_t count = point_count();
    if (count <= max_points)
        return 0;

    const size_t remove = count - max_points;
    x_.erase(x_.begin(), x_.begin() + static_cast<ptrdiff_t>(remove));
    y_.erase(y_.begin(), y_.begin() + static_cast<ptrdiff_t>(remove));
    dirty_ = true;
    return remove;
}

LineSeries& LineSeries::format(std::string_view fmt)
{
    Series::apply_format_string(fmt);
    return *this;
}

bool LineSeries::commit_pending()
{
    if (!pending_ || !pending_->has_pending())
        return false;
    if (pending_->commit(x_, y_, &x_sorted_))
    {
        mark_dirty();
        return true;
    }
    return false;
}

// --- ScatterSeries ---

ScatterSeries::ScatterSeries(std::span<const float> x, std::span<const float> y)
    : x_(x.begin(), x.end()), y_(y.begin(), y.end())
{
    x_sorted_ = std::is_sorted(x_.begin(), x_.end());
    dirty_    = true;
}

ScatterSeries& ScatterSeries::set_x(std::span<const float> x)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_x(x);
        return *this;
    }
    x_.assign(x.begin(), x.end());
    x_sorted_ = std::is_sorted(x_.begin(), x_.end());
    dirty_    = true;
    return *this;
}

ScatterSeries& ScatterSeries::set_y(std::span<const float> y)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_y(y);
        return *this;
    }
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void ScatterSeries::append(float x, float y)
{
    if (thread_safe_ && pending_)
    {
        pending_->append(x, y);
        return;
    }
    if (!x_.empty() && x < x_.back())
        x_sorted_ = false;
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

ScatterSeries& ScatterSeries::format(std::string_view fmt)
{
    Series::apply_format_string(fmt);
    return *this;
}

bool ScatterSeries::commit_pending()
{
    if (!pending_ || !pending_->has_pending())
        return false;
    if (pending_->commit(x_, y_, &x_sorted_))
    {
        mark_dirty();
        return true;
    }
    return false;
}

ScatterSeries& ScatterSeries::color_values(std::span<const float> values)
{
    color_values_.assign(values.begin(), values.end());
    if (!colormap_set_ && !color_values_.empty())
    {
        // Ignore missing samples when deriving the scalar range. NaN values
        // can still be uploaded, but they must not poison every point's map.
        float min_value = std::numeric_limits<float>::infinity();
        float max_value = -std::numeric_limits<float>::infinity();
        for (float value : color_values_)
        {
            if (!std::isfinite(value))
                continue;
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
        if (min_value <= max_value)
        {
            colormap_min_ = min_value;
            colormap_max_ = max_value;
        }
        else
        {
            colormap_min_ = 0.0f;
            colormap_max_ = 1.0f;
        }
    }
    dirty_ = true;
    return *this;
}

}   // namespace spectra
