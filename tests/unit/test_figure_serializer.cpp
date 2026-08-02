#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "ui/workspace/figure_serializer.hpp"

using namespace spectra;

class FigureSerializerTest : public ::testing::Test
{
   protected:
    std::string tmp_path_;

    void SetUp() override
    {
        tmp_path_ =
            (std::filesystem::temp_directory_path() / "spectra_test_figure_serializer.spectra")
                .string();
    }

    void TearDown() override { std::remove(tmp_path_.c_str()); }
};

TEST_F(FigureSerializerTest, SaveLoadRestores2DAxesAndSeries)
{
    Figure src({800, 600});
    auto&  ax = src.subplot(1, 1, 1);

    std::vector<float> x(80), y1(80), y2(80);
    for (int i = 0; i < 80; ++i)
    {
        x[i]  = static_cast<float>(i) * 0.1f;
        y1[i] = std::sin(x[i]);
        y2[i] = std::cos(x[i]);
    }

    ax.line(x, y1).label("sin");
    ax.scatter(x, y2).label("cos");
    ax.title("Serialization Test");
    ax.xlabel("X");
    ax.ylabel("Y");

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));

    Figure dst({640, 480});
    dst.subplot(1, 1, 1);   // Ensure load clears existing content before restoring.

    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_FALSE(dst.axes().empty());
    ASSERT_NE(dst.axes()[0], nullptr);
    EXPECT_EQ(dst.axes().size(), 1u);

    const auto& loaded_ax = *dst.axes()[0];
    EXPECT_EQ(loaded_ax.title(), "Serialization Test");
    EXPECT_EQ(loaded_ax.xlabel(), "X");
    EXPECT_EQ(loaded_ax.ylabel(), "Y");
    EXPECT_EQ(loaded_ax.series().size(), 2u);
    EXPECT_EQ(loaded_ax.series()[0]->label(), "sin");
    EXPECT_EQ(loaded_ax.series()[1]->label(), "cos");
}

TEST_F(FigureSerializerTest, RoundTripsScatterColormapAndUncertaintyBand)
{
    Figure             src({800, 600});
    auto&              axes   = src.subplot(1, 1, 1);
    std::vector<float> x      = {0.0f, 1.0f, 2.0f};
    std::vector<float> y      = {2.0f, 3.0f, 4.0f};
    std::vector<float> values = {10.0f, 20.0f, 30.0f};
    axes.scatter(x, y)
        .label("temperature")
        .color_values(values)
        .colormap(ColormapType::Plasma)
        .colormap_range(0.0f, 40.0f);

    std::vector<float> lower = {1.0f, 1.5f, 2.0f};
    std::vector<float> upper = {3.0f, 4.5f, 6.0f};
    axes.band(x, lower, upper)
        .label("95% CI")
        .fill_opacity(0.35f)
        .edge_width(2.0f)
        .show_edges(false);

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));
    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    ASSERT_EQ(dst.axes()[0]->series().size(), 2u);

    auto* scatter = dynamic_cast<ScatterSeries*>(dst.axes()[0]->series()[0].get());
    ASSERT_NE(scatter, nullptr);
    EXPECT_TRUE(scatter->has_colormap());
    EXPECT_EQ(scatter->colormap(), ColormapType::Plasma);
    EXPECT_FLOAT_EQ(scatter->colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(scatter->colormap_max(), 40.0f);
    EXPECT_EQ(scatter->color_values_data().size(), 3u);

    auto* band = dynamic_cast<BandSeries*>(dst.axes()[0]->series()[1].get());
    ASSERT_NE(band, nullptr);
    EXPECT_EQ(band->sample_count(), 3u);
    EXPECT_FLOAT_EQ(band->fill_opacity(), 0.35f);
    EXPECT_FLOAT_EQ(band->edge_width(), 2.0f);
    EXPECT_FALSE(band->show_edges());
}

TEST_F(FigureSerializerTest, InMemoryRoundTripPreservesPopulatedFigure)
{
    Figure src({913, 517});
    src.set_tab_title("Restart-safe figure");
    auto&                    axes = src.subplot(1, 1, 1);
    const std::vector<float> x    = {0.0f, 1.0f, 2.0f};
    const std::vector<float> y    = {4.0f, 5.0f, 8.0f};
    axes.line(x, y).label("measurements").width(3.5f).x_offset(1783350621.752999);

    std::string payload;
    ASSERT_TRUE(FigureSerializer::serialize(src, payload));
    ASSERT_FALSE(payload.empty());

    Figure dst;
    ASSERT_TRUE(FigureSerializer::deserialize(payload, dst));
    EXPECT_EQ(dst.width(), 913u);
    EXPECT_EQ(dst.height(), 517u);
    ASSERT_EQ(dst.axes().size(), 1u);
    ASSERT_EQ(dst.axes()[0]->series().size(), 1u);
    auto* line = dynamic_cast<LineSeries*>(dst.axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->label(), "measurements");
    EXPECT_EQ(std::vector<float>(line->x_data().begin(), line->x_data().end()), x);
    EXPECT_EQ(std::vector<float>(line->y_data().begin(), line->y_data().end()), y);
    EXPECT_FLOAT_EQ(line->width(), 3.5f);
    EXPECT_NEAR(line->x_offset(), 1783350621.752999, 1e-6);
}

TEST_F(FigureSerializerTest, InMemoryRoundTripPreservesThreeDimensionalXOffsets)
{
    Figure                   src;
    auto&                    axes = src.subplot3d(1, 1, 1);
    const std::vector<float> x{0.0f, 0.25f};
    const std::vector<float> y{1.0f, 2.0f};
    const std::vector<float> z{3.0f, 4.0f};
    axes.line3d(x, y, z).x_offset(1783350621.752999);
    axes.scatter3d(x, y, z).x_offset(1783350622.0);

    std::string payload;
    ASSERT_TRUE(FigureSerializer::serialize(src, payload));
    Figure dst;
    ASSERT_TRUE(FigureSerializer::deserialize(payload, dst));
    Axes3D* loaded_axes = nullptr;
    dst.for_each_axes(
        [&loaded_axes](AxesBase* candidate)
        {
            if (!loaded_axes)
                loaded_axes = dynamic_cast<Axes3D*>(candidate);
        });
    ASSERT_NE(loaded_axes, nullptr);
    ASSERT_EQ(loaded_axes->series().size(), 2u);
    EXPECT_NEAR(loaded_axes->series()[0]->x_offset(), 1783350621.752999, 1e-6);
    EXPECT_DOUBLE_EQ(loaded_axes->series()[1]->x_offset(), 1783350622.0);
}
