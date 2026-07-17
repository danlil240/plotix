#include <format>
#include <fstream>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <sstream>
#include <vector>

namespace spectra
{

// ─── SVG Helpers ────────────────────────────────────────────────────────────

namespace
{

// Convert a Color to an SVG rgb() string
std::string svg_color(const Color& c)
{
    return std::format("rgb({},{},{})",
                       static_cast<int>(c.r * 255.0f),
                       static_cast<int>(c.g * 255.0f),
                       static_cast<int>(c.b * 255.0f));
}

// Convert a float to a compact string (no trailing zeros)
std::string fmt(float v)
{
    return std::format("{:.4g}", static_cast<double>(v));
}

// XML-escape a string for safe embedding in SVG attributes/text content
std::string xml_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

// Map data coordinates to SVG pixel coordinates within a viewport.
// SVG has Y-down, data has Y-up, so we flip Y.
struct DataToSvg
{
    float  vp_x, vp_y, vp_w, vp_h;
    double x_min, x_max, y_min, y_max;

    [[nodiscard]] float map_x(double data_x) const
    {
        double range = x_max - x_min;
        if (range == 0.0)
            range = 1.0;
        return static_cast<float>(vp_x + (data_x - x_min) / range * vp_w);
    }

    [[nodiscard]] float map_y(double data_y) const
    {
        double range = y_max - y_min;
        if (range == 0.0)
            range = 1.0;
        // Flip Y: data y_max maps to vp_y (top), y_min maps to vp_y + vp_h (bottom)
        return static_cast<float>(vp_y + (1.0 - (data_y - y_min) / range) * vp_h);
    }
};

// Emit grid lines for one Axes
void emit_grid(std::ostringstream& svg, const Axes& axes, const DataToSvg& m)
{
    if (!axes.grid_enabled())
        return;

    auto x_ticks = axes.compute_x_ticks();
    auto y_ticks = axes.compute_y_ticks();

    svg << "    <g class=\"grid\" stroke=\"#d9d9d9\" stroke-width=\"1\" "
           "stroke-dasharray=\"4,2\">\n";

    // Vertical grid lines
    for (double tx : x_ticks.positions)
    {
        float sx = m.map_x(tx);
        svg << "      <line x1=\"" << fmt(sx) << "\" y1=\"" << fmt(m.vp_y) << "\" x2=\"" << fmt(sx)
            << "\" y2=\"" << fmt(m.vp_y + m.vp_h) << "\"/>\n";
    }

    // Horizontal grid lines
    for (double ty : y_ticks.positions)
    {
        float sy = m.map_y(ty);
        svg << "      <line x1=\"" << fmt(m.vp_x) << "\" y1=\"" << fmt(sy) << "\" x2=\""
            << fmt(m.vp_x + m.vp_w) << "\" y2=\"" << fmt(sy) << "\"/>\n";
    }

    svg << "    </g>\n";
}

// Emit axis border (box around plot area)
void emit_border(std::ostringstream& svg, const DataToSvg& m)
{
    svg << "    <rect x=\"" << fmt(m.vp_x) << "\" y=\"" << fmt(m.vp_y) << "\" width=\""
        << fmt(m.vp_w) << "\" height=\"" << fmt(m.vp_h)
        << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"1\"/>\n";
}

// Emit tick labels
void emit_tick_labels(std::ostringstream& svg, const Axes& axes, const DataToSvg& m)
{
    auto x_ticks = axes.compute_x_ticks();
    auto y_ticks = axes.compute_y_ticks();

    constexpr float tick_len     = 5.0f;
    constexpr float label_offset = 14.0f;
    constexpr float font_size    = 10.0f;

    svg << "    <g class=\"tick-labels\" font-family=\"sans-serif\" "
           "font-size=\""
        << fmt(font_size) << "\" fill=\"#333\">\n";

    // X-axis tick marks and labels (bottom)
    for (size_t i = 0; i < x_ticks.positions.size(); ++i)
    {
        float sx     = m.map_x(x_ticks.positions[i]);
        float bottom = m.vp_y + m.vp_h;
        // Tick mark
        svg << "      <line x1=\"" << fmt(sx) << "\" y1=\"" << fmt(bottom) << "\" x2=\"" << fmt(sx)
            << "\" y2=\"" << fmt(bottom + tick_len) << "\" stroke=\"#000\" stroke-width=\"1\"/>\n";
        // Label
        svg << "      <text x=\"" << fmt(sx) << "\" y=\"" << fmt(bottom + label_offset)
            << R"(" text-anchor="middle">)" << xml_escape(x_ticks.labels[i]) << "</text>\n";
    }

    // Y-axis tick marks and labels (left)
    for (size_t i = 0; i < y_ticks.positions.size(); ++i)
    {
        float sy = m.map_y(y_ticks.positions[i]);
        // Tick mark
        svg << "      <line x1=\"" << fmt(m.vp_x - tick_len) << "\" y1=\"" << fmt(sy) << "\" x2=\""
            << fmt(m.vp_x) << "\" y2=\"" << fmt(sy) << "\" stroke=\"#000\" stroke-width=\"1\"/>\n";
        // Label
        svg << "      <text x=\"" << fmt(m.vp_x - tick_len - 3.0f) << "\" y=\"" << fmt(sy + 3.5f)
            << R"(" text-anchor="end">)" << xml_escape(y_ticks.labels[i]) << "</text>\n";
    }

    svg << "    </g>\n";
}

// Emit axis labels and title
void emit_labels(std::ostringstream& svg, const Axes& axes, const DataToSvg& m)
{
    constexpr float title_font = 14.0f;
    constexpr float label_font = 12.0f;

    // Title (centered above plot area)
    if (!axes.get_title().empty())
    {
        float cx = m.vp_x + m.vp_w * 0.5f;
        float ty = m.vp_y - 10.0f;
        svg << "    <text x=\"" << fmt(cx) << "\" y=\"" << fmt(ty)
            << "\" text-anchor=\"middle\" font-family=\"sans-serif\" "
               "font-size=\""
            << fmt(title_font) << R"(" font-weight="bold" fill="#000">)"
            << xml_escape(axes.get_title()) << "</text>\n";
    }

    // X-axis label (centered below tick labels)
    if (!axes.get_xlabel().empty())
    {
        float cx = m.vp_x + m.vp_w * 0.5f;
        float ly = m.vp_y + m.vp_h + 35.0f;
        svg << "    <text x=\"" << fmt(cx) << "\" y=\"" << fmt(ly)
            << "\" text-anchor=\"middle\" font-family=\"sans-serif\" "
               "font-size=\""
            << fmt(label_font) << R"(" fill="#333">)" << xml_escape(axes.get_xlabel())
            << "</text>\n";
    }

    // Y-axis label (rotated, centered left of tick labels)
    if (!axes.get_ylabel().empty())
    {
        float cy = m.vp_y + m.vp_h * 0.5f;
        float lx = m.vp_x - 45.0f;
        svg << "    <text x=\"" << fmt(lx) << "\" y=\"" << fmt(cy)
            << "\" text-anchor=\"middle\" font-family=\"sans-serif\" "
               "font-size=\""
            << fmt(label_font)
            << "\" fill=\"#333\" "
               "transform=\"rotate(-90,"
            << fmt(lx) << "," << fmt(cy) << ")\">" << xml_escape(axes.get_ylabel()) << "</text>\n";
    }
}

// Emit a LineSeries as an SVG <polyline>
void emit_line_series(std::ostringstream& svg, const LineSeries& series, const DataToSvg& m)
{
    if (series.point_count() < 2)
        return;

    auto        x        = series.x_data();
    auto        y        = series.y_data();
    const auto  x_offset = series.x_offset();
    const auto& c        = static_cast<const Series&>(series).color();

    svg << R"(    <polyline fill="none" stroke=")" << svg_color(c) << "\" stroke-width=\""
        << fmt(series.width()) << "\" stroke-opacity=\"" << fmt(c.a)
        << R"(" stroke-linejoin="round" stroke-linecap="round" points=")";

    for (size_t i = 0; i < series.point_count(); ++i)
    {
        if (i > 0)
            svg << " ";
        svg << fmt(m.map_x(static_cast<double>(x[i]) + x_offset)) << "," << fmt(m.map_y(y[i]));
    }

    svg << "\"/>\n";
}

// Emit a ScatterSeries as SVG <circle> elements
void emit_scatter_series(std::ostringstream& svg, const ScatterSeries& series, const DataToSvg& m)
{
    if (series.point_count() == 0)
        return;

    auto        x        = series.x_data();
    auto        y        = series.y_data();
    float       r        = series.size();
    const auto  x_offset = series.x_offset();
    const auto& c        = static_cast<const Series&>(series).color();

    svg << "    <g fill=\"" << svg_color(c) << "\" fill-opacity=\"" << fmt(c.a) << "\">\n";

    for (size_t i = 0; i < series.point_count(); ++i)
    {
        svg << "      <circle cx=\"" << fmt(m.map_x(static_cast<double>(x[i]) + x_offset))
            << "\" cy=\"" << fmt(m.map_y(y[i])) << "\" r=\"" << fmt(r) << "\"/>\n";
    }

    svg << "    </g>\n";
}

// Emit legend
void emit_legend(std::ostringstream& svg, const Axes& axes, const DataToSvg& m)
{
    // Collect series with labels
    struct LegendEntry
    {
        std::string label;
        Color       color;
        bool        is_line;
    };
    std::vector<LegendEntry> entries;

    for (const auto& s : axes.series())
    {
        if (!s || s->label().empty() || !s->show_in_legend())
            continue;
        bool is_line = (dynamic_cast<const LineSeries*>(s.get()) != nullptr);
        entries.push_back({s->label(), s->color(), is_line});
    }

    if (entries.empty())
        return;

    constexpr float entry_h   = 18.0f;
    constexpr float padding   = 8.0f;
    constexpr float swatch_w  = 20.0f;
    constexpr float gap       = 6.0f;
    constexpr float font_size = 10.0f;

    float legend_h = padding * 2.0f + static_cast<float>(entries.size()) * entry_h;
    float legend_w = 120.0f;   // approximate; could measure text
    float lx       = m.vp_x + m.vp_w - legend_w - 10.0f;
    float ly       = m.vp_y + 10.0f;

    // Background box
    svg << "    <rect x=\"" << fmt(lx) << "\" y=\"" << fmt(ly) << "\" width=\"" << fmt(legend_w)
        << "\" height=\"" << fmt(legend_h)
        << "\" fill=\"white\" fill-opacity=\"0.9\" stroke=\"#ccc\" stroke-width=\"1\" rx=\"3\"/>\n";

    svg << R"(    <g font-family="sans-serif" font-size=")" << fmt(font_size)
        << "\" fill=\"#333\">\n";

    for (size_t i = 0; i < entries.size(); ++i)
    {
        float ey = ly + padding + static_cast<float>(i) * entry_h + entry_h * 0.5f;
        float ex = lx + padding;

        if (entries[i].is_line)
        {
            // Line swatch
            svg << "      <line x1=\"" << fmt(ex) << "\" y1=\"" << fmt(ey) << "\" x2=\""
                << fmt(ex + swatch_w) << "\" y2=\"" << fmt(ey) << "\" stroke=\""
                << svg_color(entries[i].color) << "\" stroke-width=\"2\"/>\n";
        }
        else
        {
            // Circle swatch
            svg << "      <circle cx=\"" << fmt(ex + swatch_w * 0.5f) << "\" cy=\"" << fmt(ey)
                << R"(" r="4" fill=")" << svg_color(entries[i].color) << "\"/>\n";
        }

        // Label text
        svg << "      <text x=\"" << fmt(ex + swatch_w + gap) << "\" y=\"" << fmt(ey + 3.5f)
            << "\">" << xml_escape(entries[i].label) << "</text>\n";
    }

    svg << "    </g>\n";
}

// Emit a single Axes (subplot)
void emit_axes(std::ostringstream& svg, const Axes& axes, const Rect& viewport)
{
    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();

    DataToSvg m{};
    m.vp_x  = viewport.x;
    m.vp_y  = viewport.y;
    m.vp_w  = viewport.w;
    m.vp_h  = viewport.h;
    m.x_min = xlim.min;
    m.x_max = xlim.max;
    m.y_min = ylim.min;
    m.y_max = ylim.max;

    svg << "  <g class=\"axes\">\n";

    // Clip series data to the plot area
    svg << "    <defs>\n";
    svg << "      <clipPath id=\"clip-" << fmt(viewport.x) << "-" << fmt(viewport.y) << "\">\n";
    svg << "        <rect x=\"" << fmt(m.vp_x) << "\" y=\"" << fmt(m.vp_y) << "\" width=\""
        << fmt(m.vp_w) << "\" height=\"" << fmt(m.vp_h) << "\"/>\n";
    svg << "      </clipPath>\n";
    svg << "    </defs>\n";

    // Grid (behind data)
    emit_grid(svg, axes, m);

    // Axis border
    emit_border(svg, m);

    // Series data (clipped to plot area)
    svg << "    <g clip-path=\"url(#clip-" << fmt(viewport.x) << "-" << fmt(viewport.y) << ")\">\n";

    for (const auto& series_ptr : axes.series())
    {
        if (!series_ptr)
            continue;

        if (auto* ls = dynamic_cast<const LineSeries*>(series_ptr.get()))
        {
            emit_line_series(svg, *ls, m);
        }
        else if (auto* ss = dynamic_cast<const ScatterSeries*>(series_ptr.get()))
        {
            emit_scatter_series(svg, *ss, m);
        }
    }

    svg << "    </g>\n";

    // Tick labels
    emit_tick_labels(svg, axes, m);

    // Axis labels and title
    emit_labels(svg, axes, m);

    // Legend
    emit_legend(svg, axes, m);

    svg << "  </g>\n";
}

}   // anonymous namespace

// ─── SvgExporter ────────────────────────────────────────────────────────────

std::string SvgExporter::to_string(const Figure& figure)
{
    uint32_t w = figure.width();
    uint32_t h = figure.height();

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
           "width=\""
        << w << "\" height=\"" << h
        << "\" "
           "viewBox=\"0 0 "
        << w << " " << h << "\">\n";

    // White background
    svg << "  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";

    // We need layout to be computed. Since Figure::compute_layout() is non-const,
    // we work with the viewports that are already set on each Axes.
    // The caller should ensure compute_layout() was called before export.
    for (const auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        emit_axes(svg, *axes_ptr, axes_ptr->viewport());
    }

    svg << "</svg>\n";
    return svg.str();
}

bool SvgExporter::write_svg(const std::string& path, const Figure& figure)
{
    std::string content = to_string(figure);

    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    file << content;
    return file.good();
}

}   // namespace spectra
