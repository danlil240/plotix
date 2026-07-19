#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/figure.hpp>
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
