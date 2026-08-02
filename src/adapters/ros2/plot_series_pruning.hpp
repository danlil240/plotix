#pragma once

// Shared plot-series pruning for ROS live plots.
//
// Series X values come from message header.stamp (or wall clock when absent).
// Axes scroll uses wall clock via set_presented_buffer_right_edge().  When
// those time bases diverge, x_limits()-based erase_before() never intersects
// the stored samples and the series grows without bound.
//
// Live-follow mode trims relative to max(newest sample X, scroll right edge).

#include <algorithm>
#include <cmath>

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

namespace spectra::adapters::ros2
{

inline void prune_time_series(Series& series, Axes& axes, double prune_buffer_s, bool enabled)
{
    auto* line = dynamic_cast<LineSeries*>(&series);
    if (!line || !enabled)
        return;

    LineSeries& ls = *line;
    ls.commit_pending();

    const auto xspan = ls.x_data();
    if (xspan.empty())
        return;

    const float  buffer   = static_cast<float>(std::max(0.0, prune_buffer_s));
    const double window_s = axes.has_presented_buffer() && axes.presented_buffer_seconds() > 0.0f
                                ? static_cast<double>(axes.presented_buffer_seconds())
                                : std::max(30.0, prune_buffer_s);

    if (axes.is_presented_buffer_following() && axes.has_presented_buffer()
        && axes.presented_buffer_seconds() > 0.0f)
    {
        // Anchor on the latest sample and the wall-clock scroll edge so stamp/wall
        // skew cannot leave samples outside the retention window.
        const auto  xlim       = axes.x_limits();
        const float anchor_max = std::max(xspan.back(), static_cast<float>(xlim.max));
        const float window     = axes.presented_buffer_seconds();
        const float keep_lo    = anchor_max - window - buffer;
        const float keep_hi    = anchor_max + buffer;
        ls.erase_before(keep_lo);
        ls.erase_after(keep_hi);
    }
    else
    {
        const auto xlim = axes.x_limits();
        ls.erase_before(static_cast<float>(xlim.min - prune_buffer_s));
        ls.erase_after(static_cast<float>(xlim.max + prune_buffer_s));
    }

    // Hard cap: ~150 Hz over (window + 2× buffer), minimum 2k points.
    const size_t point_cap =
        static_cast<size_t>(std::max(2000.0, (window_s + 2.0 * prune_buffer_s) * 150.0));
    ls.trim_to_max_points(point_cap);

    if (ls.is_thread_safe())
        ls.commit_pending();
}

}   // namespace spectra::adapters::ros2
