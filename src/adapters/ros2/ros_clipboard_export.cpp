#include "ros_clipboard_export.hpp"
#include "ros_plot_manager.hpp"

#include <spectra/series.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RosClipboardExport::RosClipboardExport(RosPlotManager& mgr) : mgr_(mgr) {}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string RosClipboardExport::format_value(double v, int precision)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

std::string RosClipboardExport::format_int64(int64_t v)
{
    return std::to_string(v);
}

std::string RosClipboardExport::make_column_name(const std::string& topic,
                                                 const std::string& field_path)
{
    if (field_path.empty())
        return topic;
    if (!topic.empty() && topic.back() == '/')
        return topic + field_path;
    return topic + "/" + field_path;
}

void RosClipboardExport::split_timestamp(double   timestamp_s,
                                         int64_t  timestamp_ns,
                                         int64_t& out_sec,
                                         int64_t& out_nsec)
{
    if (timestamp_ns != 0)
    {
        out_sec  = timestamp_ns / static_cast<int64_t>(1'000'000'000LL);
        out_nsec = timestamp_ns % static_cast<int64_t>(1'000'000'000LL);
        if (out_nsec < 0)
        {
            out_sec -= 1;
            out_nsec += 1'000'000'000LL;
        }
    }
    else
    {
        out_sec  = static_cast<int64_t>(std::floor(timestamp_s));
        out_nsec = static_cast<int64_t>(std::round((timestamp_s - std::floor(timestamp_s)) * 1e9));
        if (out_nsec >= 1'000'000'000LL)
        {
            out_sec += 1;
            out_nsec -= 1'000'000'000LL;
        }
    }
}

bool RosClipboardExport::is_copy_shortcut(int key, bool ctrl_held)
{
    // Key code 'C' — matches both ASCII 'C' (67) and 'c' (99).
    // Callers may pass either; we normalise to uppercase.
    if (!ctrl_held)
        return false;
    return (key == 'C' || key == 'c' || key == 67 || key == 99);
}

// ---------------------------------------------------------------------------
// TSV builder
// ---------------------------------------------------------------------------

std::string RosClipboardExport::build_tsv(const std::vector<SeriesData>& series,
                                          SelectionRange                 mode,
                                          double                         x_min,
                                          double                         x_max) const
{
    if (series.empty())
        return {};

    // Collect union of all X values as double; optionally clamp to [x_min, x_max].
    std::vector<double> all_x;
    for (const auto& sd : series)
    {
        for (float xf : sd.x)
        {
            auto xd = static_cast<double>(xf);
            if (mode == SelectionRange::Range)
            {
                if (xd < x_min || xd > x_max)
                    continue;
            }
            all_x.push_back(xd);
        }
    }

    // Sort and deduplicate with epsilon tolerance (1e-12) for float round-trips.
    std::sort(all_x.begin(), all_x.end());
    {
        constexpr double EPS = 1e-12;
        auto             it  = std::unique(all_x.begin(),
                              all_x.end(),
                              [](double a, double b) { return (b - a) < EPS; });
        all_x.erase(it, all_x.end());
    }

    if (all_x.empty())
        return {};

    std::ostringstream out;

    // Header row.
    if (config_.write_header)
    {
        out << "timestamp_sec\ttimestamp_nsec\twall_clock";
        for (const auto& sd : series)
            out << "\t" << sd.column_name;
        out << "\n";
    }

    // Per-series cursor for O(n) scan.
    std::vector<size_t> cursors(series.size(), 0u);

    for (double row_x : all_x)
    {
        // Timestamp columns: use ns from the first series that has a matching sample.
        int64_t ts_ns = 0;
        for (size_t si = 0; si < series.size(); ++si)
        {
            const auto& sd = series[si];
            size_t      c  = cursors[si];
            if (c < sd.x.size())
            {
                auto             xd  = static_cast<double>(sd.x[c]);
                constexpr double EPS = 1e-12;
                if (std::abs(xd - row_x) < EPS && !sd.ns.empty())
                {
                    ts_ns = sd.ns[c];
                    break;
                }
            }
        }

        int64_t sec  = 0;
        int64_t nsec = 0;
        split_timestamp(row_x, ts_ns, sec, nsec);

        out << format_int64(sec) << "\t" << format_int64(nsec) << "\t"
            << format_value(row_x, config_.wall_clock_precision);

        // Value columns.
        for (size_t si = 0; si < series.size(); ++si)
        {
            const auto& sd = series[si];
            size_t&     c  = cursors[si];

            out << "\t";

            if (c < sd.x.size())
            {
                auto             xd  = static_cast<double>(sd.x[c]);
                constexpr double EPS = 1e-12;
                if (std::abs(xd - row_x) < EPS)
                {
                    out << format_value(static_cast<double>(sd.y[c]), config_.precision);
                    ++c;
                    continue;
                }
            }
            out << config_.missing_value;
        }

        out << "\n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
// Clipboard write
// ---------------------------------------------------------------------------

void RosClipboardExport::set_clipboard(const std::string& text)
{
    last_text_ = text;
#ifdef SPECTRA_USE_IMGUI
    ImGui::SetClipboardText(text.c_str());
#endif
}

// ---------------------------------------------------------------------------
// Internal build + copy
// ---------------------------------------------------------------------------

ClipboardCopyResult RosClipboardExport::build_and_copy(const std::vector<int>& ids,
                                                       SelectionRange          mode,
                                                       double                  x_min,
                                                       double                  x_max)
{
    ClipboardCopyResult result;

    if (ids.empty())
    {
        result.error = "No plot ids provided";
        return result;
    }

    // Collect SeriesData for each requested id.
    std::vector<SeriesData> series_data;
    series_data.reserve(ids.size());

    for (int id : ids)
    {
        PlotHandle h = mgr_.handle(id);
        if (!h.valid())
        {
            result.error = "Plot id " + std::to_string(id) + " not found";
            return result;
        }

        const spectra::LineSeries* ls = h.series;
        if (!ls)
        {
            result.error = "Null series for plot id " + std::to_string(id);
            return result;
        }

        SeriesData sd;
        sd.column_name = make_column_name(h.topic, h.field_path);

        // Copy x/y data from LineSeries spans (render-thread safe when poll is idle).
        auto xspan = ls->x_data();
        auto yspan = ls->y_data();
        sd.x.assign(xspan.begin(), xspan.end());
        sd.y.assign(yspan.begin(), yspan.end());
        // timestamp_ns per-sample not stored in LineSeries (float X is wall time);
        // split_timestamp falls back to float decomposition when ns vec is empty.

        series_data.push_back(std::move(sd));
    }

    // Build TSV.
    std::string tsv = build_tsv(series_data, mode, x_min, x_max);

    if (tsv.empty())
    {
        result.error =
            (mode == SelectionRange::Range) ? "No data in the selected range" : "No data available";
        return result;
    }

    // Count rows (lines minus optional header).
    size_t newlines = 0;
    for (char ch : tsv)
        if (ch == '\n')
            ++newlines;

    result.row_count    = config_.write_header ? newlines - 1 : newlines;
    result.column_count = 3 + series_data.size();   // 3 timestamp + N value cols
    result.tsv_text     = tsv;
    result.ok           = true;

    set_clipboard(tsv);
    return result;
}

// ---------------------------------------------------------------------------
// Public copy API
// ---------------------------------------------------------------------------

ClipboardCopyResult RosClipboardExport::copy_plot(int plot_id)
{
    return build_and_copy({plot_id}, SelectionRange::Full, 0.0, 0.0);
}

ClipboardCopyResult RosClipboardExport::copy_plot(int plot_id, double x_min, double x_max)
{
    return build_and_copy({plot_id}, SelectionRange::Range, x_min, x_max);
}

ClipboardCopyResult RosClipboardExport::copy_plots(const std::vector<int>& plot_ids)
{
    return build_and_copy(plot_ids, SelectionRange::Full, 0.0, 0.0);
}

ClipboardCopyResult RosClipboardExport::copy_plots(const std::vector<int>& plot_ids,
                                                   SelectionRange          mode,
                                                   double                  x_min,
                                                   double                  x_max)
{
    return build_and_copy(plot_ids, mode, x_min, x_max);
}

}   // namespace spectra::adapters::ros2
