#include "html_table_export.hpp"

#include <format>
#include <fstream>
#include <sstream>

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>

namespace spectra
{

namespace
{

// Escape text for safe HTML embedding.
static std::string html_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s)
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
                out += "&#39;";
                break;
            default:
                out += static_cast<char>(c);
                break;
        }
    }
    return out;
}

static std::string fmt_float(float v)
{
    return std::format("{:.6g}", v);
}

// Render a single 2D series as an HTML <table>.
static std::string series_2d_table(const Series& s, int series_idx)
{
    const auto* line    = dynamic_cast<const LineSeries*>(&s);
    const auto* scatter = dynamic_cast<const ScatterSeries*>(&s);

    if (!line && !scatter)
        return {};

    const std::string lbl =
        s.label().empty() ? ("Series " + std::to_string(series_idx + 1)) : s.label();

    std::string xname = "X";
    std::string yname = "Y";

    std::ostringstream o;
    o << "<table>\n"
      << "  <caption>" << html_escape(lbl) << "</caption>\n"
      << "  <thead><tr><th scope=\"col\">Index</th>" << "<th scope=\"col\">" << html_escape(xname)
      << "</th>" << "<th scope=\"col\">" << html_escape(yname) << "</th></tr></thead>\n"
      << "  <tbody>\n";

    size_t n = line ? line->point_count() : (scatter ? scatter->point_count() : 0);
    for (size_t i = 0; i < n; ++i)
    {
        float x = 0.0f;
        float y = 0.0f;
        if (line)
        {
            x = line->x_data()[i];
            y = line->y_data()[i];
        }
        else if (scatter)
        {
            x = scatter->x_data()[i];
            y = scatter->y_data()[i];
        }
        o << "    <tr><td>" << i << "</td><td>" << fmt_float(x) << "</td><td>" << fmt_float(y)
          << "</td></tr>\n";
    }

    o << "  </tbody>\n</table>\n";
    return o.str();
}

// Render a single 3D series as an HTML <table>.
static std::string series_3d_table(const Series& s, int series_idx)
{
    const auto* line3 = dynamic_cast<const LineSeries3D*>(&s);
    const auto* scat3 = dynamic_cast<const ScatterSeries3D*>(&s);
    if (!line3 && !scat3)
        return {};

    const std::string lbl =
        s.label().empty() ? ("Series " + std::to_string(series_idx + 1)) : s.label();

    std::ostringstream o;
    o << "<table>\n"
      << "  <caption>" << html_escape(lbl) << "</caption>\n"
      << "  <thead><tr><th scope=\"col\">Index</th>" << "<th scope=\"col\">X</th>"
      << "<th scope=\"col\">Y</th>" << "<th scope=\"col\">Z</th></tr></thead>\n"
      << "  <tbody>\n";

    size_t n = line3 ? line3->point_count() : (scat3 ? scat3->point_count() : 0);
    for (size_t i = 0; i < n; ++i)
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (line3)
        {
            x = line3->x_data()[i];
            y = line3->y_data()[i];
            z = line3->z_data()[i];
        }
        else if (scat3)
        {
            x = scat3->x_data()[i];
            y = scat3->y_data()[i];
            z = scat3->z_data()[i];
        }
        o << "    <tr><td>" << i << "</td><td>" << fmt_float(x) << "</td><td>" << fmt_float(y)
          << "</td><td>" << fmt_float(z) << "</td></tr>\n";
    }

    o << "  </tbody>\n</table>\n";
    return o.str();
}

}   // anonymous namespace

std::string figure_to_html_table(const Figure& fig)
{
    std::ostringstream o;

    o << "<!DOCTYPE html>\n"
      << "<html lang=\"en\">\n"
      << "<head><meta charset=\"UTF-8\">" << "<title>Spectra Figure Data</title></head>\n"
      << "<body>\n"
      << "<h1>Figure Data</h1>\n";

    // 2D axes
    int ax_idx = 0;
    for (const auto& ax_ptr : fig.axes())
    {
        if (!ax_ptr)
            continue;
        ++ax_idx;

        const std::string ax_title = ax_ptr->title();
        const std::string xlabel   = ax_ptr->xlabel();
        const std::string ylabel   = ax_ptr->ylabel();

        o << "<section aria-label=\"Axes " << ax_idx << "\">\n";
        if (!ax_title.empty())
            o << "  <h2>" << html_escape(ax_title) << "</h2>\n";
        if (!xlabel.empty() || !ylabel.empty())
        {
            o << "  <p>";
            if (!xlabel.empty())
                o << "X: " << html_escape(xlabel);
            if (!xlabel.empty() && !ylabel.empty())
                o << " — ";
            if (!ylabel.empty())
                o << "Y: " << html_escape(ylabel);
            o << "</p>\n";
        }

        int s_idx = 0;
        for (const auto& sp : ax_ptr->series())
        {
            if (!sp)
                continue;
            o << series_2d_table(*sp, s_idx);
            ++s_idx;
        }
        o << "</section>\n";
    }

    // 3D axes
    for (const auto& ax_base_ptr : fig.all_axes())
    {
        if (!ax_base_ptr)
            continue;
        const auto* ax3d = dynamic_cast<const Axes3D*>(ax_base_ptr.get());
        if (!ax3d)
            continue;
        ++ax_idx;

        const std::string ax_title = ax3d->title();
        o << "<section aria-label=\"3D Axes " << ax_idx << "\">\n";
        if (!ax_title.empty())
            o << "  <h2>" << html_escape(ax_title) << "</h2>\n";

        int s_idx = 0;
        for (const auto& sp : ax3d->series())
        {
            if (!sp)
                continue;
            o << series_3d_table(*sp, s_idx);
            ++s_idx;
        }
        o << "</section>\n";
    }

    o << "</body>\n</html>\n";
    return o.str();
}

bool figure_to_html_table_file(const Figure& fig, const std::string& path)
{
    std::ofstream f(path);
    if (!f.is_open())
        return false;
    f << figure_to_html_table(fig);
    return f.good();
}

}   // namespace spectra
