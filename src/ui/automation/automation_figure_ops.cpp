#include "automation_figure_ops.hpp"

#include "automation_dispatch.hpp"
#include "automation_json.hpp"
#include "automation_server.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>

#include <cmath>
#include <sstream>
#include <vector>

namespace spectra
{

bool automation_add_series(AutomationRequest& request, FigureRegistry& registry)
{
    if (!json_has_key(request.params_json, "figure_id"))
    {
        request.response_json = json_error(request.id, "Missing parameter: figure_id");
        return false;
    }

    const auto figure_id =
        static_cast<FigureId>(json_get_uint64(request.params_json, "figure_id", 0));
    Figure* figure = registry.get(figure_id);
    if (!figure)
    {
        request.response_json = json_error(request.id, "Figure not found");
        return false;
    }

    std::string type = json_get_string(request.params_json, "type");
    if (type.empty())
        type = "line";
    const int n_points = json_get_int(request.params_json, "n_points", 100);
    if (n_points <= 0 || n_points > 1'000'000)
    {
        request.response_json = json_error(request.id, "n_points must be between 1 and 1000000");
        return false;
    }

    if (figure->axes().empty())
        figure->subplot(1, 1, 1);
    auto& axes = *figure->axes_mut()[0];

    const bool has_x = json_has_key(request.params_json, "x");
    const bool has_y = json_has_key(request.params_json, "y");
    if (has_x != has_y)
    {
        request.response_json = json_error(request.id, "x and y must be provided together");
        return false;
    }

    std::vector<float> x_caller = json_get_float_array(request.params_json, "x");
    std::vector<float> y_caller = json_get_float_array(request.params_json, "y");
    if (has_x && (x_caller.empty() || y_caller.empty() || x_caller.size() != y_caller.size()))
    {
        request.response_json =
            json_error(request.id, "x and y must be non-empty arrays of equal length");
        return false;
    }

    std::vector<float> x_generated;
    std::vector<float> y_generated;
    if (x_caller.empty() || y_caller.empty())
    {
        x_generated.resize(static_cast<size_t>(n_points));
        y_generated.resize(static_cast<size_t>(n_points));
        for (int i = 0; i < n_points; ++i)
        {
            x_generated[i] = static_cast<float>(i);
            y_generated[i] = std::sin(static_cast<float>(i) * 0.1f);
        }
    }

    const std::vector<float>& x = x_caller.empty() ? x_generated : x_caller;
    const std::vector<float>& y = y_caller.empty() ? y_generated : y_caller;

    const std::string label  = json_get_string(request.params_json, "label");
    Series*           series = nullptr;
    if (type == "scatter")
        series = &axes.scatter(x, y);
    else if (type == "bar")
        series = &axes.bar(x, y);
    else if (type == "histogram")
    {
        const int bins = json_get_int(request.params_json, "bins", 30);
        if (bins <= 0)
        {
            request.response_json = json_error(request.id, "bins must be positive");
            return false;
        }
        series = &axes.histogram(y, bins);
    }
    else
        series = &axes.line(x, y);
    if (series && !label.empty())
        series->label(label);

    request.response_json =
        json_ok(request.id, "{\"series_count\":" + std::to_string(axes.series().size()) + "}");
    return true;
}

bool automation_get_figure_info(AutomationRequest& request, FigureRegistry& registry)
{
    if (!json_has_key(request.params_json, "figure_id"))
    {
        request.response_json = json_error(request.id, "Missing parameter: figure_id");
        return false;
    }

    const auto figure_id =
        static_cast<FigureId>(json_get_uint64(request.params_json, "figure_id", 0));
    Figure* figure = registry.get(figure_id);
    if (!figure)
    {
        request.response_json = json_error(request.id, "Figure not found");
        return false;
    }

    std::ostringstream result;
    result << "{\"figure_id\":" << figure_id << ",\"width\":" << figure->width()
           << ",\"height\":" << figure->height() << ",\"axes_count\":" << figure->axes().size()
           << ",\"all_axes_count\":" << figure->all_axes().size();

    result << ",\"axes\":[";
    for (size_t axes_index = 0; axes_index < figure->axes().size(); ++axes_index)
    {
        if (axes_index > 0)
            result << ',';
        auto* axes = figure->axes()[axes_index].get();
        if (!axes)
        {
            result << "null";
            continue;
        }

        const auto x_limits = axes->x_limits();
        const auto y_limits = axes->y_limits();
        result << "{\"index\":" << axes_index << ",\"x_min\":" << x_limits.min
               << ",\"x_max\":" << x_limits.max << ",\"y_min\":" << y_limits.min
               << ",\"y_max\":" << y_limits.max << ",\"series\":[";
        for (size_t series_index = 0; series_index < axes->series().size(); ++series_index)
        {
            if (series_index > 0)
                result << ',';
            auto* series = axes->series()[series_index].get();
            if (!series)
            {
                result << "null";
                continue;
            }

            const char* type        = "unknown";
            size_t      point_count = 0;
            if (auto* line = dynamic_cast<const LineSeries*>(series))
            {
                type        = "line";
                point_count = line->point_count();
            }
            else if (auto* scatter = dynamic_cast<const ScatterSeries*>(series))
            {
                type        = "scatter";
                point_count = scatter->point_count();
            }
            else if (auto* bar = dynamic_cast<const BarSeries*>(series))
            {
                type        = "bar";
                point_count = bar->bar_positions().size();
            }
            else if (auto* histogram = dynamic_cast<const HistogramSeries*>(series))
            {
                type        = "histogram";
                point_count = histogram->bin_counts().size();
            }
            result << R"({"label":")" << json_escape(series->label()) << R"(","type":")" << type
                   << R"(","visible":)" << (series->visible() ? "true" : "false")
                   << ",\"point_count\":" << point_count << '}';
        }
        result << "]}";
    }
    result << ']';

    result << ",\"axes_3d\":[";
    size_t axes_3d_count = 0;
    for (size_t axes_index = 0; axes_index < figure->all_axes().size(); ++axes_index)
    {
        auto* axes = dynamic_cast<Axes3D*>(figure->all_axes()[axes_index].get());
        if (!axes)
            continue;
        if (axes_3d_count > 0)
            result << ',';

        const auto x_limits = axes->x_limits();
        const auto y_limits = axes->y_limits();
        const auto z_limits = axes->z_limits();
        result << "{\"index\":" << axes_index << ",\"x_min\":" << x_limits.min
               << ",\"x_max\":" << x_limits.max << ",\"y_min\":" << y_limits.min
               << ",\"y_max\":" << y_limits.max << ",\"z_min\":" << z_limits.min
               << ",\"z_max\":" << z_limits.max << ",\"series_count\":" << axes->series().size()
               << '}';
        ++axes_3d_count;
    }
    result << "]}";

    request.response_json = json_ok(request.id, result.str());
    return true;
}

}   // namespace spectra
