#include "series_clipboard.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

namespace spectra
{

SeriesSnapshot SeriesClipboard::snapshot(const Series& series)
{
    SeriesSnapshot snap;
    snap.label    = series.label();
    snap.color    = series.color();
    snap.style    = series.plot_style();
    snap.visible  = series.visible();
    snap.x_offset = series.x_offset();

    // 2D types
    if (const auto* line = dynamic_cast<const LineSeries*>(&series))
    {
        snap.type       = SeriesSnapshot::Type::Line;
        snap.line_width = line->width();
        auto xd         = line->x_data();
        auto yd         = line->y_data();
        snap.x_data.assign(xd.begin(), xd.end());
        snap.y_data.assign(yd.begin(), yd.end());
    }
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&series))
    {
        snap.type       = SeriesSnapshot::Type::Scatter;
        snap.point_size = scatter->size();
        auto xd         = scatter->x_data();
        auto yd         = scatter->y_data();
        snap.x_data.assign(xd.begin(), xd.end());
        snap.y_data.assign(yd.begin(), yd.end());
    }
    // 3D types
    else if (const auto* line3d = dynamic_cast<const LineSeries3D*>(&series))
    {
        snap.type       = SeriesSnapshot::Type::Line3D;
        snap.line_width = line3d->width();
        auto xd         = line3d->x_data();
        auto yd         = line3d->y_data();
        auto zd         = line3d->z_data();
        snap.x_data.assign(xd.begin(), xd.end());
        snap.y_data.assign(yd.begin(), yd.end());
        snap.z_data.assign(zd.begin(), zd.end());
    }
    else if (const auto* scatter3d = dynamic_cast<const ScatterSeries3D*>(&series))
    {
        snap.type       = SeriesSnapshot::Type::Scatter3D;
        snap.point_size = scatter3d->size();
        auto xd         = scatter3d->x_data();
        auto yd         = scatter3d->y_data();
        auto zd         = scatter3d->z_data();
        snap.x_data.assign(xd.begin(), xd.end());
        snap.y_data.assign(yd.begin(), yd.end());
        snap.z_data.assign(zd.begin(), zd.end());
    }

    return snap;
}

// Apply common style properties to a newly created series
static void apply_style(Series& s, const SeriesSnapshot& snap)
{
    s.label(snap.label.empty() ? "Pasted" : snap.label);
    s.color(snap.color);
    s.line_style(snap.style.line_style);
    s.marker_style(snap.style.marker_style);
    s.marker_size(snap.style.marker_size);
    s.opacity(snap.style.opacity);
    s.visible(snap.visible);
    s.x_offset(snap.x_offset);
}

Series* SeriesClipboard::paste_to(AxesBase& axes_base, const SeriesSnapshot& snap)
{
    auto* axes_2d = dynamic_cast<Axes*>(&axes_base);
    auto* axes_3d = dynamic_cast<Axes3D*>(&axes_base);

    // ── Paste into 2D axes ──────────────────────────────────────────────
    if (axes_2d)
    {
        if (snap.type == SeriesSnapshot::Type::Line)
        {
            auto& s = axes_2d->line(snap.x_data, snap.y_data);
            apply_style(s, snap);
            s.width(snap.line_width);
            return &s;
        }
        if (snap.type == SeriesSnapshot::Type::Scatter)
        {
            auto& s = axes_2d->scatter(snap.x_data, snap.y_data);
            apply_style(s, snap);
            s.size(snap.point_size);
            return &s;
        }
        // 3D → 2D: project by dropping z
        if (snap.type == SeriesSnapshot::Type::Line3D)
        {
            auto& s = axes_2d->line(snap.x_data, snap.y_data);
            apply_style(s, snap);
            s.width(snap.line_width);
            return &s;
        }
        if (snap.type == SeriesSnapshot::Type::Scatter3D)
        {
            auto& s = axes_2d->scatter(snap.x_data, snap.y_data);
            apply_style(s, snap);
            s.size(snap.point_size);
            return &s;
        }
    }

    // ── Paste into 3D axes ──────────────────────────────────────────────
    if (axes_3d)
    {
        if (snap.type == SeriesSnapshot::Type::Line3D)
        {
            auto& s = axes_3d->line3d(snap.x_data, snap.y_data, snap.z_data);
            apply_style(s, snap);
            s.width(snap.line_width);
            return &s;
        }
        if (snap.type == SeriesSnapshot::Type::Scatter3D)
        {
            auto& s = axes_3d->scatter3d(snap.x_data, snap.y_data, snap.z_data);
            apply_style(s, snap);
            s.size(snap.point_size);
            return &s;
        }
        // 2D → 3D: add z=0
        if (snap.type == SeriesSnapshot::Type::Line)
        {
            std::vector<float> z(snap.x_data.size(), 0.0f);
            auto&              s = axes_3d->line3d(snap.x_data, snap.y_data, z);
            apply_style(s, snap);
            s.width(snap.line_width);
            return &s;
        }
        if (snap.type == SeriesSnapshot::Type::Scatter)
        {
            std::vector<float> z(snap.x_data.size(), 0.0f);
            auto&              s = axes_3d->scatter3d(snap.x_data, snap.y_data, z);
            apply_style(s, snap);
            s.size(snap.point_size);
            return &s;
        }
    }

    return nullptr;
}

void SeriesClipboard::copy(const Series& series)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
    buffers_.push_back(snapshot(series));
    has_data_ = true;
    is_cut_   = false;
}

void SeriesClipboard::cut(const Series& series)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
    buffers_.push_back(snapshot(series));
    has_data_ = true;
    is_cut_   = true;
}

void SeriesClipboard::copy_multi(const std::vector<const Series*>& series_list)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
    for (const auto* s : series_list)
    {
        if (s)
            buffers_.push_back(snapshot(*s));
    }
    has_data_ = !buffers_.empty();
    is_cut_   = false;
}

void SeriesClipboard::cut_multi(const std::vector<const Series*>& series_list)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
    for (const auto* s : series_list)
    {
        if (s)
            buffers_.push_back(snapshot(*s));
    }
    has_data_ = !buffers_.empty();
    is_cut_   = true;
}

Series* SeriesClipboard::paste(AxesBase& axes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_data_ || buffers_.empty())
        return nullptr;

    Series* result = paste_to(axes, buffers_[0]);

    if (is_cut_)
    {
        is_cut_ = false;
    }

    return result;
}

std::vector<Series*> SeriesClipboard::paste_all(AxesBase& axes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Series*>        results;
    if (!has_data_)
        return results;

    for (const auto& snap : buffers_)
    {
        Series* s = paste_to(axes, snap);
        if (s)
            results.push_back(s);
    }

    if (is_cut_)
    {
        is_cut_ = false;
    }

    return results;
}

bool SeriesClipboard::has_data() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return has_data_;
}

bool SeriesClipboard::is_cut() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return is_cut_;
}

size_t SeriesClipboard::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.size();
}

void SeriesClipboard::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
    has_data_ = false;
    is_cut_   = false;
}

const SeriesSnapshot* SeriesClipboard::peek() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return (has_data_ && !buffers_.empty()) ? buffers_.data() : nullptr;
}

const std::vector<SeriesSnapshot>& SeriesClipboard::peek_all() const
{
    // Note: caller must hold awareness that this is not atomic across calls.
    // For display purposes only.
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_;
}

}   // namespace spectra
