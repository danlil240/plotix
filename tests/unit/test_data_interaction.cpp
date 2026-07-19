#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "ui/overlay/axes3d_pick.hpp"
#include "data/sorted_x_query.hpp"

// DataInteraction and its components are ImGui-guarded.
// These tests exercise the pure-logic parts (nearest-point, markers)
// without requiring a running ImGui context, by testing the underlying
// data structures directly.

using namespace spectra;

TEST(SortedXQuery, NarrowsMillionPointInteractionToVisibleCandidates)
{
    std::vector<float> x(1'000'000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(i);

    auto range = sorted_x_query_range(x, 500'000.0, 12.0);
    EXPECT_LE(range.end - range.begin, 28u);
    EXPECT_LE(range.begin, 500'000u);
    EXPECT_GT(range.end, 500'000u);
}

TEST(SortedXQuery, KeepsSmallSeriesOnLinearPath)
{
    std::vector<float> x     = {0.0f, 1.0f, 2.0f};
    auto               range = sorted_x_query_range(x, 1.0, 0.1);
    EXPECT_EQ(range.begin, 0u);
    EXPECT_EQ(range.end, x.size());
}

TEST(SortedXQuery, PreservesDoublePrecisionAroundLargeOffsets)
{
    std::vector<float> x(1'000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 1.0e9f + static_cast<float>(i * 128);

    const double center = static_cast<double>(x[500]) + 40.0;
    auto         range  = sorted_x_query_range(x, center, 10.0);
    EXPECT_EQ(range.begin, 500u);
    EXPECT_EQ(range.end, 502u);
}

// ─── Nearest-point logic (standalone, mirrors DataInteraction::find_nearest) ──

namespace
{

struct NearestPointResult
{
    bool          found       = false;
    const Series* series      = nullptr;
    size_t        point_index = 0;
    float         data_x = 0.0f, data_y = 0.0f;
    float         screen_x = 0.0f, screen_y = 0.0f;
    float         distance_px = 0.0f;
};

// Standalone nearest-point query for testing without ImGui
NearestPointResult find_nearest_standalone(float       cursor_screen_x,
                                           float       cursor_screen_y,
                                           const Axes& axes,
                                           const Rect& viewport)
{
    NearestPointResult best;
    best.found       = false;
    best.distance_px = 1e30f;

    auto  xlim    = axes.x_limits();
    auto  ylim    = axes.y_limits();
    float x_range = xlim.max - xlim.min;
    float y_range = ylim.max - ylim.min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    for (auto& series_ptr : axes.series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;

        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t       count  = 0;

        if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
        {
            x_data = ls->x_data().data();
            y_data = ls->y_data().data();
            count  = ls->point_count();
        }
        else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
        {
            x_data = sc->x_data().data();
            y_data = sc->y_data().data();
            count  = sc->point_count();
        }

        if (!x_data || !y_data || count == 0)
            continue;

        for (size_t i = 0; i < count; ++i)
        {
            float norm_x = (x_data[i] - xlim.min) / x_range;
            float norm_y = (y_data[i] - ylim.min) / y_range;
            float sx     = viewport.x + norm_x * viewport.w;
            float sy     = viewport.y + (1.0f - norm_y) * viewport.h;

            float dx   = cursor_screen_x - sx;
            float dy   = cursor_screen_y - sy;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < best.distance_px)
            {
                best.found       = true;
                best.series      = series_ptr.get();
                best.point_index = i;
                best.data_x      = x_data[i];
                best.data_y      = y_data[i];
                best.screen_x    = sx;
                best.screen_y    = sy;
                best.distance_px = dist;
            }
        }
    }

    return best;
}

}   // anonymous namespace

// ─── Tests ──────────────────────────────────────────────────────────────────

class DataInteractionTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Create axes with known viewport and limits
        axes_ = std::make_unique<Axes>();
        axes_->xlim(0.0f, 10.0f);
        axes_->ylim(0.0f, 10.0f);
        axes_->set_viewport(Rect{100.0f, 100.0f, 800.0f, 600.0f});

        // Add a line series with known points
        std::vector<float> x    = {0.0f, 2.5f, 5.0f, 7.5f, 10.0f};
        std::vector<float> y    = {0.0f, 5.0f, 10.0f, 5.0f, 0.0f};
        auto&              line = axes_->line(x, y);
        line.label("test_series");
    }

    std::unique_ptr<Axes> axes_;
};

TEST_F(DataInteractionTest, NearestPointFindsExactMatch)
{
    // Point (5.0, 10.0) maps to screen (500, 100) in our viewport
    // viewport: x=100, y=100, w=800, h=600
    // norm_x = 0.5 -> screen_x = 100 + 0.5*800 = 500
    // norm_y = 1.0 -> screen_y = 100 + (1-1)*600 = 100
    auto result = find_nearest_standalone(500.0f, 100.0f, *axes_, axes_->viewport());
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.point_index, 2u);
    EXPECT_FLOAT_EQ(result.data_x, 5.0f);
    EXPECT_FLOAT_EQ(result.data_y, 10.0f);
    EXPECT_NEAR(result.distance_px, 0.0f, 0.5f);
}

TEST_F(DataInteractionTest, NearestPointFindsClosest)
{
    // Cursor near point (2.5, 5.0) -> screen (300, 400)
    // norm_x = 0.25 -> 100 + 0.25*800 = 300
    // norm_y = 0.5  -> 100 + 0.5*600 = 400
    auto result = find_nearest_standalone(305.0f, 405.0f, *axes_, axes_->viewport());
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.point_index, 1u);
    EXPECT_FLOAT_EQ(result.data_x, 2.5f);
    EXPECT_FLOAT_EQ(result.data_y, 5.0f);
}

TEST_F(DataInteractionTest, NearestPointSnapsToActualData)
{
    // Cursor between two points — should snap to nearest, not interpolate
    // Midpoint between (2.5,5) and (5,10) in screen space
    // (2.5,5) -> screen (300, 400)
    // (5,10)  -> screen (500, 100)
    // Midpoint: (400, 250)
    auto result = find_nearest_standalone(400.0f, 250.0f, *axes_, axes_->viewport());
    ASSERT_TRUE(result.found);
    // Should be one of the actual data points, not an interpolated value
    bool is_actual_point = (result.point_index == 1 || result.point_index == 2);
    EXPECT_TRUE(is_actual_point);
}

TEST_F(DataInteractionTest, NearestPointEmptySeriesReturnsNotFound)
{
    Axes empty_axes;
    empty_axes.xlim(0.0f, 10.0f);
    empty_axes.ylim(0.0f, 10.0f);
    empty_axes.set_viewport(Rect{0.0f, 0.0f, 800.0f, 600.0f});

    auto result = find_nearest_standalone(400.0f, 300.0f, empty_axes, empty_axes.viewport());
    EXPECT_FALSE(result.found);
}

TEST_F(DataInteractionTest, NearestPointHiddenSeriesSkipped)
{
    // Hide the series
    for (auto& s : axes_->series_mut())
    {
        if (s)
            s->visible(false);
    }

    auto result = find_nearest_standalone(500.0f, 100.0f, *axes_, axes_->viewport());
    EXPECT_FALSE(result.found);
}

TEST_F(DataInteractionTest, NearestPointScatterSeries)
{
    Axes scatter_axes;
    scatter_axes.xlim(0.0f, 100.0f);
    scatter_axes.ylim(0.0f, 100.0f);
    scatter_axes.set_viewport(Rect{0.0f, 0.0f, 1000.0f, 1000.0f});

    std::vector<float> x = {10.0f, 50.0f, 90.0f};
    std::vector<float> y = {10.0f, 50.0f, 90.0f};
    scatter_axes.scatter(x, y).label("scatter_test");

    // Cursor near (50, 50) -> screen (500, 500)
    auto result = find_nearest_standalone(500.0f, 500.0f, scatter_axes, scatter_axes.viewport());
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.point_index, 1u);
    EXPECT_FLOAT_EQ(result.data_x, 50.0f);
    EXPECT_FLOAT_EQ(result.data_y, 50.0f);
}

TEST_F(DataInteractionTest, NearestPointFirstAndLastPoints)
{
    // Test snapping to first point (0, 0) -> screen (100, 700)
    auto result = find_nearest_standalone(100.0f, 700.0f, *axes_, axes_->viewport());
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.point_index, 0u);
    EXPECT_FLOAT_EQ(result.data_x, 0.0f);
    EXPECT_FLOAT_EQ(result.data_y, 0.0f);

    // Test snapping to last point (10, 0) -> screen (900, 700)
    result = find_nearest_standalone(900.0f, 700.0f, *axes_, axes_->viewport());
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.point_index, 4u);
    EXPECT_FLOAT_EQ(result.data_x, 10.0f);
    EXPECT_FLOAT_EQ(result.data_y, 0.0f);
}

// ─── DataMarker logic tests (standalone, no ImGui) ──────────────────────────

namespace
{

struct TestMarker
{
    float         data_x, data_y;
    const Series* series;
    size_t        point_index;
};

// Standalone data_to_screen for testing
void data_to_screen(float       data_x,
                    float       data_y,
                    const Rect& viewport,
                    float       xlim_min,
                    float       xlim_max,
                    float       ylim_min,
                    float       ylim_max,
                    float&      screen_x,
                    float&      screen_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;
    float norm_x = (data_x - xlim_min) / x_range;
    float norm_y = (data_y - ylim_min) / y_range;
    screen_x     = viewport.x + norm_x * viewport.w;
    screen_y     = viewport.y + (1.0f - norm_y) * viewport.h;
}

int marker_hit_test(const std::vector<TestMarker>& markers,
                    float                          screen_x,
                    float                          screen_y,
                    const Rect&                    viewport,
                    float                          xlim_min,
                    float                          xlim_max,
                    float                          ylim_min,
                    float                          ylim_max,
                    float                          radius_px = 10.0f)
{
    for (size_t i = 0; i < markers.size(); ++i)
    {
        float sx, sy;
        data_to_screen(markers[i].data_x,
                       markers[i].data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);
        float dx = screen_x - sx;
        float dy = screen_y - sy;
        if (dx * dx + dy * dy <= radius_px * radius_px)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}   // anonymous namespace

TEST(DataMarkerTest, HitTestFindsMarker)
{
    Rect                    vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    std::vector<TestMarker> markers;
    markers.push_back({50.0f, 50.0f, nullptr, 0});

    // Marker at (50,50) -> screen (500, 500)
    int idx = marker_hit_test(markers, 502.0f, 498.0f, vp, 0.0f, 100.0f, 0.0f, 100.0f);
    EXPECT_EQ(idx, 0);
}

TEST(DataMarkerTest, HitTestMissesDistantClick)
{
    Rect                    vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    std::vector<TestMarker> markers;
    markers.push_back({50.0f, 50.0f, nullptr, 0});

    // Click far from marker
    int idx = marker_hit_test(markers, 100.0f, 100.0f, vp, 0.0f, 100.0f, 0.0f, 100.0f);
    EXPECT_EQ(idx, -1);
}

TEST(DataMarkerTest, HitTestMultipleMarkers)
{
    Rect                    vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    std::vector<TestMarker> markers;
    markers.push_back({10.0f, 10.0f, nullptr, 0});
    markers.push_back({90.0f, 90.0f, nullptr, 1});

    // Click near second marker (90,90) -> screen (900, 100)
    int idx = marker_hit_test(markers, 901.0f, 101.0f, vp, 0.0f, 100.0f, 0.0f, 100.0f);
    EXPECT_EQ(idx, 1);
}

TEST(DataMarkerTest, MarkerPersistsThroughZoom)
{
    // Verify that data_to_screen correctly maps after limit changes
    Rect  vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    float sx1, sy1, sx2, sy2;

    // Before zoom: limits [0, 100]
    data_to_screen(50.0f, 50.0f, vp, 0.0f, 100.0f, 0.0f, 100.0f, sx1, sy1);
    EXPECT_FLOAT_EQ(sx1, 500.0f);
    EXPECT_FLOAT_EQ(sy1, 500.0f);

    // After zoom: limits [25, 75] — marker should move to edges
    data_to_screen(50.0f, 50.0f, vp, 25.0f, 75.0f, 25.0f, 75.0f, sx2, sy2);
    EXPECT_FLOAT_EQ(sx2, 500.0f);   // Still centered
    EXPECT_FLOAT_EQ(sy2, 500.0f);

    // Point at (25, 25) should now be at bottom-left
    data_to_screen(25.0f, 25.0f, vp, 25.0f, 75.0f, 25.0f, 75.0f, sx2, sy2);
    EXPECT_FLOAT_EQ(sx2, 0.0f);
    EXPECT_FLOAT_EQ(sy2, 1000.0f);
}

// ─── 3D nearest-point pick ───────────────────────────────────────────────────

TEST(Axes3DPickTest, FindsNearestScatterPoint)
{
    Axes3D axes;
    axes.xlim(-2.0, 2.0);
    axes.ylim(-2.0, 2.0);
    axes.zlim(-2.0, 2.0);
    axes.set_viewport(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    axes.camera().set_azimuth(45.0f).set_elevation(30.0f).set_distance(8.0f);

    std::vector<float> xs = {0.0f, 1.5f};
    std::vector<float> ys = {0.0f, 0.0f};
    std::vector<float> zs = {0.0f, 0.0f};
    axes.scatter3d(xs, ys, zs);

    const Projected3DPoint origin = project_axes3d_data_point(axes, {0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(origin.visible);

    auto pick = find_nearest_3d_in_axes(axes, origin.screen_x + 2.0f, origin.screen_y + 2.0f);
    ASSERT_TRUE(pick.found);
    EXPECT_EQ(pick.point_index, 0u);
    EXPECT_FLOAT_EQ(pick.data_x, 0.0f);
    EXPECT_FLOAT_EQ(pick.data_y, 0.0f);
    EXPECT_FLOAT_EQ(pick.data_z, 0.0f);
    EXPECT_LT(pick.distance_px, 8.0f);
}

TEST(Axes3DPickTest, PrefersFrontmostWhenOverlapping)
{
    Axes3D axes;
    axes.xlim(-2.0, 2.0);
    axes.ylim(-2.0, 2.0);
    axes.zlim(-2.0, 2.0);
    axes.set_viewport(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    axes.camera().set_azimuth(0.0f).set_elevation(0.0f).set_distance(8.0f);

    std::vector<float> xs2 = {0.0f, 0.0f};
    std::vector<float> ys2 = {0.0f, 0.0f};
    std::vector<float> zs2 = {-1.0f, 1.0f};
    axes.scatter3d(xs2, ys2, zs2);

    const Projected3DPoint proj = project_axes3d_data_point(axes, {0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(proj.visible);

    auto pick = find_nearest_3d_in_axes(axes, proj.screen_x, proj.screen_y);
    ASSERT_TRUE(pick.found);
    EXPECT_FLOAT_EQ(pick.data_z, 1.0f);
}
