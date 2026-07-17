#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <random>

namespace spectra
{
namespace
{

// Helper: create a simple figure with one line series
Figure make_line_figure()
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
    ax.line(x, y).label("test-line").color(rgb(1.0f, 0.0f, 0.0f));
    ax.title("Test Title");
    ax.xlabel("X Label");
    ax.ylabel("Y Label");

    fig.compute_layout();
    return fig;
}

// Helper: create a figure with scatter series
Figure make_scatter_figure()
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 4.0f, 2.0f};
    ax.scatter(x, y).label("points").color(rgb(0.0f, 0.0f, 1.0f));

    fig.compute_layout();
    return fig;
}

TEST(SvgExport, ToStringProducesValidSvg)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Must start with XML declaration and SVG root
    EXPECT_NE(svg.find("<?xml"), std::string::npos);
    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
}

TEST(SvgExport, ContainsViewBoxDimensions)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("width=\"800\""), std::string::npos);
    EXPECT_NE(svg.find("height=\"600\""), std::string::npos);
    EXPECT_NE(svg.find("viewBox=\"0 0 800 600\""), std::string::npos);
}

TEST(SvgExport, ContainsPolylineForLineSeries)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<polyline"), std::string::npos);
    // Red color: rgb(255,0,0)
    EXPECT_NE(svg.find("rgb(255,0,0)"), std::string::npos);
}

TEST(SvgExport, ContainsCirclesForScatterSeries)
{
    auto        fig = make_scatter_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Should have 3 circle elements
    size_t pos          = 0;
    int    circle_count = 0;
    while ((pos = svg.find("<circle", pos)) != std::string::npos)
    {
        ++circle_count;
        ++pos;
    }
    // At least 3 data circles (legend may add one more)
    EXPECT_GE(circle_count, 3);
}

TEST(SvgExport, ContainsTitleText)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("Test Title"), std::string::npos);
}

TEST(SvgExport, ContainsAxisLabels)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("X Label"), std::string::npos);
    EXPECT_NE(svg.find("Y Label"), std::string::npos);
}

TEST(SvgExport, ContainsLegendEntry)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("test-line"), std::string::npos);
}

TEST(SvgExport, ContainsGridLines)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Grid group should exist
    EXPECT_NE(svg.find("class=\"grid\""), std::string::npos);
}

TEST(SvgExport, ContainsBorderRect)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Should have a rect with stroke for the border
    EXPECT_NE(svg.find("fill=\"none\" stroke=\"#000\""), std::string::npos);
}

TEST(SvgExport, ContainsTickLabels)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("class=\"tick-labels\""), std::string::npos);
}

TEST(SvgExport, ContainsClipPath)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<clipPath"), std::string::npos);
    EXPECT_NE(svg.find("clip-path="), std::string::npos);
}

TEST(SvgExport, WriteToFile)
{
    auto fig = make_line_figure();

    // Use cross-platform temporary file
    std::filesystem::path temp_dir    = std::filesystem::temp_directory_path();
    std::string           random_name = "plot_" + std::to_string(std::random_device{}());
    std::string           path        = random_name + ".svg";

    bool ok = SvgExporter::write_svg(path, fig);
    EXPECT_TRUE(ok);

    // Verify file exists and has content
    std::ifstream file(path);
    EXPECT_TRUE(file.good());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_GT(content.size(), 100u);
    EXPECT_NE(content.find("<svg"), std::string::npos);

    // Clean up
    std::remove(path.c_str());
}

TEST(SvgExport, MultiSubplotProducesMultipleAxesGroups)
{
    Figure fig({.width = 1200, .height = 600});
    auto&  ax1 = fig.subplot(1, 2, 1);
    auto&  ax2 = fig.subplot(1, 2, 2);

    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y1 = {0.0f, 1.0f, 0.5f};
    std::vector<float> y2 = {1.0f, 0.5f, 1.5f};

    ax1.line(x, y1).label("series1");
    ax2.line(x, y2).label("series2");
    ax1.title("Plot 1");
    ax2.title("Plot 2");

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Should have two axes groups
    size_t pos        = 0;
    int    axes_count = 0;
    while ((pos = svg.find("class=\"axes\"", pos)) != std::string::npos)
    {
        ++axes_count;
        ++pos;
    }
    EXPECT_EQ(axes_count, 2);

    // Both titles present
    EXPECT_NE(svg.find("Plot 1"), std::string::npos);
    EXPECT_NE(svg.find("Plot 2"), std::string::npos);
}

TEST(SvgExport, EmptyFigureProducesMinimalSvg)
{
    Figure fig({.width = 400, .height = 300});
    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
    // No axes groups since no subplots created
    EXPECT_EQ(svg.find("class=\"axes\""), std::string::npos);
}

TEST(SvgExport, XmlEscapesSpecialCharacters)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.title("A < B & C > D");

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    ax.line(x, y);

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Special chars should be escaped
    EXPECT_NE(svg.find("A &lt; B &amp; C &gt; D"), std::string::npos);
    // Raw special chars should NOT appear in text content
    // (they do appear in SVG markup itself, so just check the title is escaped)
}

TEST(SvgExport, GridDisabledOmitsGridGroup)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.grid(false);

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    ax.line(x, y);

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Grid group should NOT exist
    EXPECT_EQ(svg.find("class=\"grid\""), std::string::npos);
}

TEST(SvgExport, MapsEpochOffsetSeriesIntoViewport)
{
    Figure                   fig({.width = 800, .height = 600});
    auto&                    ax = fig.subplot(1, 1, 1);
    const std::vector<float> x  = {0.0f, 0.02f, 0.04f};
    const std::vector<float> y  = {1.0f, 2.0f, 3.0f};
    ax.line(x, y).x_offset(1783350621.752999);
    ax.auto_fit();
    fig.compute_layout();

    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_EQ(svg.find("<polyline fill=\"none\"") == std::string::npos, false);
    EXPECT_EQ(svg.find("points=\"-"), std::string::npos);
}

}   // namespace
}   // namespace spectra
