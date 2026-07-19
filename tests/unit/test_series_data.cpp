#include <gtest/gtest.h>
#include <limits>
#include <spectra/color.hpp>
#include <spectra/series.hpp>
#include <vector>

using namespace spectra;

// ─── LineSeries ─────────────────────────────────────────────────────────────

TEST(LineSeries, DefaultConstruction)
{
    LineSeries s;
    EXPECT_EQ(s.point_count(), 0u);
    EXPECT_TRUE(s.is_dirty());
    EXPECT_TRUE(s.visible());
}

TEST(LineSeries, ConstructWithData)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f, 6.0f};

    LineSeries s(x, y);
    EXPECT_EQ(s.point_count(), 3u);
    EXPECT_TRUE(s.is_dirty());

    auto xd = s.x_data();
    auto yd = s.y_data();
    EXPECT_FLOAT_EQ(xd[0], 1.0f);
    EXPECT_FLOAT_EQ(xd[2], 3.0f);
    EXPECT_FLOAT_EQ(yd[1], 5.0f);
}

TEST(LineSeries, TracksMonotonicXForIndexedInteraction)
{
    std::vector<float> sorted_x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y        = {0.0f, 0.0f, 0.0f};
    LineSeries         series(sorted_x, y);
    EXPECT_TRUE(series.x_is_sorted());

    series.append(4.0f, 0.0f);
    EXPECT_TRUE(series.x_is_sorted());
    series.append(2.5f, 0.0f);
    EXPECT_FALSE(series.x_is_sorted());

    std::vector<float> replacement = {-2.0f, -1.0f, 0.0f};
    series.set_x(replacement);
    EXPECT_TRUE(series.x_is_sorted());
}

TEST(LineSeries, TracksMonotonicXAcrossThreadSafeCommits)
{
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {0.0f, 0.0f};
    LineSeries         series(x, y);
    series.set_thread_safe(true);

    series.append(3.0f, 0.0f);
    series.append(4.0f, 0.0f);
    ASSERT_TRUE(series.commit_pending());
    EXPECT_TRUE(series.x_is_sorted());

    series.append(3.5f, 0.0f);
    ASSERT_TRUE(series.commit_pending());
    EXPECT_FALSE(series.x_is_sorted());
}

TEST(LineSeries, SetXY)
{
    LineSeries         s;
    std::vector<float> x = {10.0f, 20.0f};
    std::vector<float> y = {30.0f, 40.0f};

    s.set_x(x);
    s.set_y(y);
    EXPECT_EQ(s.point_count(), 2u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 10.0f);
    EXPECT_FLOAT_EQ(s.y_data()[1], 40.0f);
    EXPECT_TRUE(s.is_dirty());
}

TEST(LineSeries, MismatchedCoordinatesCountCompletePairs)
{
    LineSeries         s;
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f};

    s.set_x(x);
    s.set_y(y);

    EXPECT_EQ(s.point_count(), 1u);
}

TEST(LineSeries, Append)
{
    LineSeries s;
    s.append(1.0f, 2.0f);
    s.append(3.0f, 4.0f);

    EXPECT_EQ(s.point_count(), 2u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(s.y_data()[1], 4.0f);
}

TEST(LineSeries, FluentAPI)
{
    LineSeries s;
    auto&      ref = s.label("test").color(colors::red).width(3.0f);

    EXPECT_EQ(&ref, &s);
    // Access through base class reference to use the no-arg accessors
    const Series& base = s;
    EXPECT_EQ(base.label(), "test");
    EXPECT_FLOAT_EQ(base.color().r, 1.0f);
    EXPECT_FLOAT_EQ(s.width(), 3.0f);
}

TEST(LineSeries, ClearDirty)
{
    LineSeries s;
    EXPECT_TRUE(s.is_dirty());
    s.clear_dirty();
    EXPECT_FALSE(s.is_dirty());

    // Modifying data should re-set dirty
    std::vector<float> x = {1.0f};
    s.set_x(x);
    EXPECT_TRUE(s.is_dirty());
}

TEST(LineSeries, Visibility)
{
    LineSeries s;
    EXPECT_TRUE(s.visible());
    s.visible(false);
    EXPECT_FALSE(s.visible());
    s.visible(true);
    EXPECT_TRUE(s.visible());
}

// ─── ScatterSeries ──────────────────────────────────────────────────────────

TEST(ScatterSeries, DefaultConstruction)
{
    ScatterSeries s;
    EXPECT_EQ(s.point_count(), 0u);
    EXPECT_TRUE(s.is_dirty());
}

TEST(ScatterSeries, ConstructWithData)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {2.0f, 3.0f};

    ScatterSeries s(x, y);
    EXPECT_EQ(s.point_count(), 2u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(s.y_data()[1], 3.0f);
}

TEST(ScatterSeries, SetXY)
{
    ScatterSeries      s;
    std::vector<float> x = {5.0f};
    std::vector<float> y = {6.0f};

    s.set_x(x);
    s.set_y(y);
    EXPECT_EQ(s.point_count(), 1u);
}

TEST(ScatterSeries, MismatchedCoordinatesCountCompletePairs)
{
    ScatterSeries      s;
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f};

    s.set_x(x);
    s.set_y(y);

    EXPECT_EQ(s.point_count(), 2u);
}

TEST(ScatterSeries, Append)
{
    ScatterSeries s;
    s.append(10.0f, 20.0f);
    EXPECT_EQ(s.point_count(), 1u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 10.0f);
    EXPECT_FLOAT_EQ(s.y_data()[0], 20.0f);
}

TEST(ScatterSeries, FluentAPI)
{
    ScatterSeries s;
    auto&         ref = s.label("scatter").color(colors::green).size(8.0f);

    EXPECT_EQ(&ref, &s);
    const Series& base = s;
    EXPECT_EQ(base.label(), "scatter");
    EXPECT_FLOAT_EQ(base.color().g, 1.0f);
    EXPECT_FLOAT_EQ(s.size(), 8.0f);
}

TEST(ScatterSeries, DefaultSize)
{
    ScatterSeries s;
    EXPECT_FLOAT_EQ(s.size(), 4.0f);
}

TEST(ScatterSeries, ColormapRequiresOneScalarPerPoint)
{
    std::vector<float> x      = {0.0f, 1.0f, 2.0f};
    std::vector<float> y      = {3.0f, 4.0f, 5.0f};
    std::vector<float> values = {10.0f, 20.0f, 30.0f};
    ScatterSeries      series(x, y);

    series.color_values(values).colormap(ColormapType::Viridis);
    EXPECT_TRUE(series.has_colormap());
    EXPECT_FLOAT_EQ(series.colormap_min(), 10.0f);
    EXPECT_FLOAT_EQ(series.colormap_max(), 30.0f);

    values.pop_back();
    series.color_values(values);
    EXPECT_FALSE(series.has_colormap());
}

TEST(ScatterSeries, TracksUnsortedXForSafeFallback)
{
    std::vector<float> x = {0.0f, 2.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    ScatterSeries      series(x, y);
    EXPECT_FALSE(series.x_is_sorted());
}

TEST(ScatterSeries, ColormapRangeIgnoresMissingScalarValues)
{
    std::vector<float> x      = {0.0f, 1.0f, 2.0f};
    std::vector<float> y      = {3.0f, 4.0f, 5.0f};
    std::vector<float> values = {std::numeric_limits<float>::quiet_NaN(), -4.0f, 8.0f};
    ScatterSeries      series(x, y);

    series.color_values(values).colormap(ColormapType::Viridis);
    EXPECT_FLOAT_EQ(series.colormap_min(), -4.0f);
    EXPECT_FLOAT_EQ(series.colormap_max(), 8.0f);
}

TEST(LineSeries, EraseAfter)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    LineSeries         s(x, y);

    const size_t removed = s.erase_after(3.0f);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(s.point_count(), 3u);
    EXPECT_FLOAT_EQ(s.x_data().back(), 3.0f);
}

TEST(LineSeries, TrimToMaxPoints)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    LineSeries         s(x, y);

    const size_t removed = s.trim_to_max_points(3);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(s.point_count(), 3u);
    EXPECT_FLOAT_EQ(s.x_data()[0], 3.0f);
    EXPECT_FLOAT_EQ(s.y_data()[0], 30.0f);
}
