#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include "math/expression_eval.hpp"
#include "ui/plot/plot_annotations.hpp"

TEST(PlotAnnotations, HorizontalReferenceLine)
{
    spectra::Axes ax;
    auto&         line = spectra::ui::add_horizontal_reference_line(ax, 0.0f, "y = 0");
    ASSERT_EQ(line.point_count(), 2u);
    EXPECT_FLOAT_EQ(line.y_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 0.0f);
    EXPECT_FALSE(line.show_in_legend());
    EXPECT_TRUE(line.is_reference_line());
}

TEST(PlotAnnotations, VerticalReferenceLine)
{
    spectra::Axes ax;
    auto&         line = spectra::ui::add_vertical_reference_line(ax, 1.5f);
    ASSERT_EQ(line.point_count(), 2u);
    EXPECT_FLOAT_EQ(line.x_data()[0], 1.5f);
    EXPECT_FLOAT_EQ(line.x_data()[1], 1.5f);
    EXPECT_TRUE(line.is_reference_line());
}

TEST(PlotAnnotations, CopyPreservesLegendAndReferenceFlags)
{
    spectra::Axes ax;
    auto&         src = spectra::ui::add_horizontal_reference_line(ax, 1.0f, "ref");
    src.set_show_in_legend(false);
    src.set_reference_line(true);

    spectra::LineSeries copy = src;
    EXPECT_FALSE(copy.show_in_legend());
    EXPECT_TRUE(copy.is_reference_line());
}

TEST(PlotAnnotations, LegendHiddenDataSeriesIsNotReferenceLine)
{
    float         x[] = {0.0f, 1.0f};
    float         y[] = {0.0f, 1.0f};
    spectra::Axes ax;
    auto&         line = ax.plot(std::span<const float>(x, 2), std::span<const float>(y, 2), "r-");
    line.set_show_in_legend(false);
    EXPECT_FALSE(line.is_reference_line());
}

TEST(PlotAnnotations, FunctionPlotParabola)
{
    spectra::Axes ax;
    auto          info = spectra::parse_expression("x^2");
    ASSERT_TRUE(info.ast != nullptr);
    auto& line = spectra::ui::add_function_plot(ax, *info.ast, -2.0f, 2.0f, 5, "x^2");
    ASSERT_EQ(line.point_count(), 5u);
    EXPECT_FLOAT_EQ(line.x_data()[0], -2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[2], 0.0f);
}
