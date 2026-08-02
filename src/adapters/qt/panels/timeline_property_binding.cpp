#include "timeline_property_binding.hpp"

#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/animation/timeline_editor.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <string_view>

namespace spectra::adapters::qt
{
namespace
{

struct ParsedPath
{
    size_t      axes_index   = 0;
    size_t      series_index = 0;
    bool        has_series   = false;
    std::string property;
};

std::vector<AxesBase*> figure_axes(Figure& figure)
{
    std::vector<AxesBase*> result;
    figure.for_each_axes([&result](AxesBase* axes) { result.push_back(axes); });
    return result;
}

bool parse_index(std::string_view text, size_t& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool parse_path(std::string_view path, ParsedPath& parsed)
{
    constexpr std::string_view prefix = "axes/";
    if (!path.starts_with(prefix))
        return false;
    path.remove_prefix(prefix.size());
    const size_t slash = path.find('/');
    if (slash == std::string_view::npos || !parse_index(path.substr(0, slash), parsed.axes_index))
        return false;
    path.remove_prefix(slash + 1);
    if (path.starts_with("series/"))
    {
        path.remove_prefix(7);
        const size_t series_slash = path.find('/');
        if (series_slash == std::string_view::npos
            || !parse_index(path.substr(0, series_slash), parsed.series_index))
            return false;
        parsed.has_series = true;
        path.remove_prefix(series_slash + 1);
    }
    parsed.property.assign(path);
    return !parsed.property.empty();
}

std::string axes_label(const AxesBase& axes, size_t index)
{
    std::string label = "Axes " + std::to_string(index + 1);
    if (!axes.title().empty())
        label += " (" + axes.title() + ")";
    return label;
}

std::string series_label(const Series& series, size_t index)
{
    return series.label().empty() ? "Series " + std::to_string(index + 1) : series.label();
}

void add_limit_targets(std::vector<TimelinePropertyTarget>& targets,
                       const std::string&                   prefix,
                       const std::string&                   label,
                       std::string_view                     axis_key,
                       AxisLimits                           limits)
{
    std::string axis_label(axis_key);
    if (!axis_label.empty())
        axis_label.front() = static_cast<char>(std::toupper(axis_label.front()));
    targets.push_back({prefix + "/" + std::string(axis_key) + "_min",
                       label + " / " + axis_label + " Min",
                       static_cast<float>(limits.min)});
    targets.push_back({prefix + "/" + std::string(axis_key) + "_max",
                       label + " / " + axis_label + " Max",
                       static_cast<float>(limits.max)});
}

bool apply_property(Figure& figure, const ParsedPath& path, float value)
{
    auto axes = figure_axes(figure);
    if (path.axes_index >= axes.size() || !axes[path.axes_index] || !std::isfinite(value))
        return false;
    AxesBase* base = axes[path.axes_index];

    if (path.has_series)
    {
        auto& series = base->series_mut();
        if (path.series_index >= series.size() || !series[path.series_index])
            return false;
        Series* target = series[path.series_index].get();
        if (path.property == "opacity")
        {
            target->opacity(std::clamp(value, 0.0f, 1.0f));
            return true;
        }
        if (path.property == "line_width")
        {
            if (auto* line = dynamic_cast<LineSeries*>(target))
                line->width(std::max(value, 0.0f));
            else if (auto* line3d = dynamic_cast<LineSeries3D*>(target))
                line3d->width(std::max(value, 0.0f));
            else
                return false;
            return true;
        }
        if (path.property == "point_size")
        {
            if (auto* scatter = dynamic_cast<ScatterSeries*>(target))
                scatter->size(std::max(value, 0.0f));
            else if (auto* scatter3d = dynamic_cast<ScatterSeries3D*>(target))
                scatter3d->size(std::max(value, 0.0f));
            else
                return false;
            return true;
        }
        return false;
    }

    auto set_limits = [value](AxisLimits current, bool minimum, const auto& setter)
    {
        constexpr double epsilon = 1.0e-9;
        if (minimum)
            setter(static_cast<double>(value),
                   std::max(current.max, static_cast<double>(value) + epsilon));
        else
            setter(std::min(current.min, static_cast<double>(value) - epsilon),
                   static_cast<double>(value));
    };

    if (auto* axes2d = dynamic_cast<Axes*>(base))
    {
        if (path.property == "x_min" || path.property == "x_max")
        {
            set_limits(axes2d->x_limits(),
                       path.property == "x_min",
                       [axes2d](double min, double max) { axes2d->xlim(min, max); });
            return true;
        }
        if (path.property == "y_min" || path.property == "y_max")
        {
            set_limits(axes2d->y_limits(),
                       path.property == "y_min",
                       [axes2d](double min, double max) { axes2d->ylim(min, max); });
            return true;
        }
        return false;
    }

    auto* axes3d = dynamic_cast<Axes3D*>(base);
    if (!axes3d)
        return false;
    if (path.property == "x_min" || path.property == "x_max")
        set_limits(axes3d->x_limits(),
                   path.property == "x_min",
                   [axes3d](double min, double max) { axes3d->xlim(min, max); });
    else if (path.property == "y_min" || path.property == "y_max")
        set_limits(axes3d->y_limits(),
                   path.property == "y_min",
                   [axes3d](double min, double max) { axes3d->ylim(min, max); });
    else if (path.property == "z_min" || path.property == "z_max")
        set_limits(axes3d->z_limits(),
                   path.property == "z_min",
                   [axes3d](double min, double max) { axes3d->zlim(min, max); });
    else
        return false;
    return true;
}

}   // namespace

std::vector<TimelinePropertyTarget> timeline_property_targets(Figure& figure)
{
    std::vector<TimelinePropertyTarget> targets;
    const auto                          axes = figure_axes(figure);
    for (size_t axes_index = 0; axes_index < axes.size(); ++axes_index)
    {
        AxesBase* base = axes[axes_index];
        if (!base)
            continue;
        const std::string prefix = "axes/" + std::to_string(axes_index);
        const std::string label  = axes_label(*base, axes_index);
        if (auto* axes2d = dynamic_cast<Axes*>(base))
        {
            add_limit_targets(targets, prefix, label, "x", axes2d->x_limits());
            add_limit_targets(targets, prefix, label, "y", axes2d->y_limits());
        }
        else if (auto* axes3d = dynamic_cast<Axes3D*>(base))
        {
            add_limit_targets(targets, prefix, label, "x", axes3d->x_limits());
            add_limit_targets(targets, prefix, label, "y", axes3d->y_limits());
            add_limit_targets(targets, prefix, label, "z", axes3d->z_limits());
        }

        for (size_t series_index = 0; series_index < base->series().size(); ++series_index)
        {
            const auto& owned = base->series()[series_index];
            if (!owned)
                continue;
            const std::string series_prefix = prefix + "/series/" + std::to_string(series_index);
            const std::string series_name   = label + " / " + series_label(*owned, series_index);
            targets.push_back({series_prefix + "/opacity",
                               series_name + " / Opacity",
                               owned->opacity(),
                               0.0f,
                               1.0f});
            if (const auto* line = dynamic_cast<const LineSeries*>(owned.get()))
                targets.push_back({series_prefix + "/line_width",
                                   series_name + " / Line Width",
                                   line->width(),
                                   0.0f,
                                   1.0e6f});
            else if (const auto* line3d = dynamic_cast<const LineSeries3D*>(owned.get()))
                targets.push_back({series_prefix + "/line_width",
                                   series_name + " / Line Width",
                                   line3d->width(),
                                   0.0f,
                                   1.0e6f});
            if (const auto* scatter = dynamic_cast<const ScatterSeries*>(owned.get()))
                targets.push_back({series_prefix + "/point_size",
                                   series_name + " / Point Size",
                                   scatter->size(),
                                   0.0f,
                                   1.0e6f});
            else if (const auto* scatter3d = dynamic_cast<const ScatterSeries3D*>(owned.get()))
                targets.push_back({series_prefix + "/point_size",
                                   series_name + " / Point Size",
                                   scatter3d->size(),
                                   0.0f,
                                   1.0e6f});
        }
    }
    return targets;
}

bool bind_timeline_property(TimelineEditor& timeline, Figure& figure, uint32_t track_id)
{
    KeyframeInterpolator* interpolator = timeline.interpolator();
    const TimelineTrack*  track        = timeline.get_track(track_id);
    if (!interpolator || !track)
        return false;
    const std::string path = track->property_path;
    interpolator->unbind(track_id);
    if (path.empty())
        return true;
    ParsedPath parsed;
    if (!parse_path(path, parsed))
        return false;
    const auto targets = timeline_property_targets(figure);
    const auto target  = std::find_if(targets.begin(),
                                     targets.end(),
                                     [&path](const auto& item) { return item.path == path; });
    if (target == targets.end())
        return false;
    interpolator->bind_callback(track_id,
                                target->label,
                                [&figure, parsed](float value)
                                { apply_property(figure, parsed, value); });
    return true;
}

void bind_timeline_properties(TimelineEditor& timeline, Figure& figure)
{
    for (const auto& track : timeline.tracks())
        bind_timeline_property(timeline, figure, track.id);
}

}   // namespace spectra::adapters::qt
