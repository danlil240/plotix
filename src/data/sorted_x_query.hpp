#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

namespace spectra
{

struct SortedXQueryRange
{
    size_t begin = 0;
    size_t end   = 0;
};

// Return the contiguous candidate range around center for monotonic X data.
// One neighbor is retained on each side so callers can preserve nearest-point
// behavior at a snap-window boundary. Small inputs stay linear to avoid binary
// search overhead.
inline SortedXQueryRange sorted_x_query_range(std::span<const float> x,
                                              double                 center,
                                              double                 radius,
                                              size_t                 linear_threshold = 64)
{
    if (x.size() <= linear_threshold)
        return {0, x.size()};

    const double range_min = center - radius;
    const double range_max = center + radius;
    size_t       begin     = static_cast<size_t>(std::lower_bound(x.begin(),
                                                        x.end(),
                                                        range_min,
                                                        [](float lhs, double rhs)
                                                        { return static_cast<double>(lhs) < rhs; })
                                       - x.begin());
    size_t       end       = static_cast<size_t>(std::upper_bound(x.begin(),
                                                      x.end(),
                                                      range_max,
                                                      [](double lhs, float rhs)
                                                      { return lhs < static_cast<double>(rhs); })
                                     - x.begin());
    if (begin > 0)
        --begin;
    if (end < x.size())
        ++end;
    return {begin, end};
}

}   // namespace spectra
