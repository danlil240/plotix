#include <gtest/gtest.h>
#include <stdexcept>
#include <spectra/axes.hpp>
#include <spectra/series_stats.hpp>

#include <cmath>
#include <vector>

using namespace spectra;

TEST(BandSeries, BuildsTwoTrianglesPerFiniteInterval)
{
    std::vector<float> x     = {0.0f, 1.0f, 2.0f};
    std::vector<float> lower = {-1.0f, 0.0f, 1.0f};
    std::vector<float> upper = {1.0f, 2.0f, 3.0f};
    BandSeries         band(x, lower, upper);

    EXPECT_EQ(band.sample_count(), 3u);
    EXPECT_EQ(band.fill_vertex_count(), 12u);
    EXPECT_EQ(band.point_count(), 7u);
    EXPECT_TRUE(std::isnan(band.x_data()[3]));
}

TEST(BandSeries, RejectsMisalignedInputs)
{
    std::vector<float> x     = {0.0f, 1.0f};
    std::vector<float> lower = {0.0f};
    std::vector<float> upper = {1.0f, 2.0f};
    EXPECT_THROW(BandSeries(x, lower, upper), std::invalid_argument);
}

TEST(BandSeries, ParticipatesInAutoscale)
{
    Axes               axes;
    std::vector<float> x     = {10.0f, 20.0f};
    std::vector<float> lower = {-4.0f, -3.0f};
    std::vector<float> upper = {7.0f, 9.0f};
    axes.band(x, lower, upper);
    axes.auto_fit();

    EXPECT_LE(axes.x_limits().min, 10.0);
    EXPECT_GE(axes.x_limits().max, 20.0);
    EXPECT_LE(axes.y_limits().min, -4.0);
    EXPECT_GE(axes.y_limits().max, 9.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// BoxPlotSeries
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// Series x_offset — double-precision absolute x (epoch timestamps)
// ═══════════════════════════════════════════════════════════════════════════

TEST(SeriesXOffset, AxisLimitsIncludeOffset)
{
    // Epoch-scale timestamps stored as small relative floats + double offset.
    // Axis limits must reflect the absolute values at double precision.
    constexpr double   epoch = 1783350621.752999;
    std::vector<float> x     = {0.0f, 0.02f, 0.04f};
    std::vector<float> y     = {1.0f, 2.0f, 3.0f};

    Axes  ax;
    auto& line = ax.line(x, y);
    line.x_offset(epoch);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    // Limits bracket the absolute range [epoch, epoch + 0.04] with padding.
    EXPECT_LT(xlim.min, epoch);
    EXPECT_GT(xlim.max, epoch + 0.04);
    // Sub-second resolution preserved: the whole span is well under a second.
    EXPECT_LT(xlim.max - xlim.min, 1.0);
    EXPECT_NEAR(xlim.min, epoch, 0.1);
}

TEST(SeriesXOffset, DefaultOffsetIsZero)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 2.0f, 3.0f};

    Axes  ax;
    auto& line = ax.line(x, y);
    EXPECT_EQ(line.x_offset(), 0.0);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    EXPECT_LE(xlim.min, 1.0);
    EXPECT_GE(xlim.max, 3.0);
}

TEST(BoxPlotStats, ComputeFromData)
{
    std::vector<float> data  = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto               stats = BoxPlotSeries::compute_stats(data);

    EXPECT_FLOAT_EQ(stats.median, 5.5f);
    EXPECT_FLOAT_EQ(stats.q1, 3.25f);
    EXPECT_FLOAT_EQ(stats.q3, 7.75f);
    EXPECT_GE(stats.whisker_low, 1.0f);
    EXPECT_LE(stats.whisker_high, 10.0f);
    EXPECT_TRUE(stats.outliers.empty());
}

TEST(BoxPlotStats, OutlierDetection)
{
    std::vector<float> data  = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};
    auto               stats = BoxPlotSeries::compute_stats(data);

    EXPECT_FALSE(stats.outliers.empty());
    // 100 should be an outlier
    bool found_100 = false;
    for (float o : stats.outliers)
    {
        if (o == 100.0f)
            found_100 = true;
    }
    EXPECT_TRUE(found_100);
}

TEST(BoxPlotStats, EmptyData)
{
    std::vector<float> data;
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 0.0f);
}

TEST(BoxPlotStats, SingleValue)
{
    std::vector<float> data  = {42.0f};
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 42.0f);
    EXPECT_FLOAT_EQ(stats.q1, 42.0f);
    EXPECT_FLOAT_EQ(stats.q3, 42.0f);
}

TEST(BoxPlotStats, NaNFiltering)
{
    float              nan   = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> data  = {1, nan, 3, nan, 5};
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 3.0f);
}

TEST(BoxPlotSeries, AddBoxFromData)
{
    BoxPlotSeries      bp;
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bp.add_box(1.0f, data);

    EXPECT_EQ(bp.positions().size(), 1u);
    EXPECT_EQ(bp.stats().size(), 1u);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, AddBoxFromStats)
{
    BoxPlotSeries bp;
    bp.add_box(1.0f, 5.0f, 3.0f, 7.0f, 1.0f, 9.0f);

    EXPECT_EQ(bp.positions().size(), 1u);
    EXPECT_FLOAT_EQ(bp.stats()[0].median, 5.0f);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, MultipleBoxes)
{
    BoxPlotSeries      bp;
    std::vector<float> d1 = {1, 2, 3, 4, 5};
    std::vector<float> d2 = {10, 20, 30, 40, 50};
    bp.add_box(1.0f, d1);
    bp.add_box(2.0f, d2);

    EXPECT_EQ(bp.positions().size(), 2u);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, GeometryContainsNaNBreaks)
{
    BoxPlotSeries      bp;
    std::vector<float> data = {1, 2, 3, 4, 5};
    bp.add_box(1.0f, data);

    bool has_nan = false;
    for (size_t i = 0; i < bp.point_count(); ++i)
    {
        if (std::isnan(bp.x_data()[i]))
        {
            has_nan = true;
            break;
        }
    }
    EXPECT_TRUE(has_nan);
}

TEST(BoxPlotSeries, FluentAPI)
{
    BoxPlotSeries bp;
    bp.label("test").color(colors::red).box_width(0.4f).show_outliers(false);

    EXPECT_EQ(bp.label(), "test");
    EXPECT_FLOAT_EQ(bp.box_width(), 0.4f);
    EXPECT_FALSE(bp.show_outliers());
}

// ═══════════════════════════════════════════════════════════════════════════
// ViolinSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(ViolinSeries, AddViolin)
{
    ViolinSeries       vn;
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    vn.add_violin(1.0f, data);

    EXPECT_GT(vn.point_count(), 0u);
}

TEST(ViolinSeries, MultipleViolins)
{
    ViolinSeries       vn;
    std::vector<float> d1 = {1, 2, 3, 4, 5};
    std::vector<float> d2 = {10, 20, 30, 40, 50};
    vn.add_violin(1.0f, d1);
    vn.add_violin(2.0f, d2);

    EXPECT_GT(vn.point_count(), 0u);
}

TEST(ViolinSeries, EmptyData)
{
    ViolinSeries       vn;
    std::vector<float> data;
    vn.add_violin(1.0f, data);

    EXPECT_EQ(vn.point_count(), 0u);
}

TEST(ViolinSeries, Resolution)
{
    ViolinSeries vn;
    vn.resolution(20);
    EXPECT_EQ(vn.resolution(), 20);

    std::vector<float> data = {1, 2, 3, 4, 5};
    vn.add_violin(1.0f, data);

    // With resolution=20, each violin has 20 right + 20 left + 1 close + 1 NaN = 42 points
    // Plus inner box if show_box is true
    EXPECT_GT(vn.point_count(), 40u);
}

TEST(ViolinSeries, FluentAPI)
{
    ViolinSeries vn;
    vn.label("violin").color(colors::green).violin_width(0.5f).show_box(false);

    EXPECT_EQ(vn.label(), "violin");
    EXPECT_FLOAT_EQ(vn.violin_width(), 0.5f);
    EXPECT_FALSE(vn.show_box());
}

// ═══════════════════════════════════════════════════════════════════════════
// HistogramSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(HistogramSeries, BasicConstruction)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist(data, 5);

    EXPECT_GT(hist.point_count(), 0u);
    EXPECT_EQ(hist.bins(), 5);
    EXPECT_EQ(hist.bin_edges().size(), 6u);   // bins + 1
    EXPECT_EQ(hist.bin_counts().size(), 5u);
}

TEST(HistogramSeries, BinCountsSum)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist(data, 5);

    float total = 0.0f;
    for (float c : hist.bin_counts())
        total += c;
    EXPECT_FLOAT_EQ(total, 10.0f);
}

TEST(HistogramSeries, EmptyData)
{
    std::vector<float> data;
    HistogramSeries    hist(data, 10);

    EXPECT_EQ(hist.point_count(), 0u);
    EXPECT_TRUE(hist.bin_edges().empty());
}

TEST(HistogramSeries, SingleValue)
{
    std::vector<float> data = {5.0f, 5.0f, 5.0f};
    HistogramSeries    hist(data, 10);

    EXPECT_GT(hist.point_count(), 0u);
}

TEST(HistogramSeries, Cumulative)
{
    std::vector<float> data = {1, 2, 3, 4, 5};
    HistogramSeries    hist;
    hist.cumulative(true);
    hist.set_data(data, 5);

    // Last bin count should equal total
    EXPECT_FLOAT_EQ(hist.bin_counts().back(), 5.0f);
}

TEST(HistogramSeries, Density)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist;
    hist.density(true);
    hist.set_data(data, 5);

    // Area under density histogram should be approximately 1
    float area      = 0.0f;
    float bin_width = (hist.bin_edges().back() - hist.bin_edges().front()) / 5.0f;
    for (float c : hist.bin_counts())
        area += c * bin_width;
    EXPECT_NEAR(area, 1.0f, 0.01f);
}

TEST(HistogramSeries, FluentAPI)
{
    HistogramSeries hist;
    hist.label("hist").color(colors::orange).bins(20);

    EXPECT_EQ(hist.label(), "hist");
    EXPECT_EQ(hist.bins(), 20);
}

TEST(HistogramSeries, GeometryIsStepFunction)
{
    std::vector<float> data = {1, 2, 3, 4, 5};
    HistogramSeries    hist(data, 3);

    // Step function starts and ends at y=0
    EXPECT_FLOAT_EQ(hist.y_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(hist.y_data()[hist.point_count() - 1], 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BarSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(BarSeries, BasicConstruction)
{
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    BarSeries          bars(pos, heights);

    EXPECT_GT(bars.point_count(), 0u);
    EXPECT_EQ(bars.bar_positions().size(), 3u);
    EXPECT_EQ(bars.bar_heights().size(), 3u);
}

TEST(BarSeries, EmptyData)
{
    std::vector<float> pos;
    std::vector<float> heights;
    BarSeries          bars(pos, heights);

    EXPECT_EQ(bars.point_count(), 0u);
}

TEST(BarSeries, BarWidth)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars(pos, heights);
    bars.bar_width(0.5f);

    EXPECT_FLOAT_EQ(bars.bar_width(), 0.5f);
}

TEST(BarSeries, Baseline)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars;
    bars.baseline(5.0f);
    bars.set_data(pos, heights);

    // Check that geometry includes baseline value
    bool has_baseline = false;
    for (size_t i = 0; i < bars.point_count(); ++i)
    {
        if (!std::isnan(bars.y_data()[i]) && bars.y_data()[i] == 5.0f)
        {
            has_baseline = true;
            break;
        }
    }
    EXPECT_TRUE(has_baseline);
}

TEST(BarSeries, HorizontalOrientation)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars;
    bars.orientation(BarOrientation::Horizontal);
    bars.set_data(pos, heights);

    EXPECT_GT(bars.point_count(), 0u);
}

TEST(BarSeries, GeometryContainsNaNBreaks)
{
    std::vector<float> pos     = {1, 2};
    std::vector<float> heights = {10, 20};
    BarSeries          bars(pos, heights);

    bool has_nan = false;
    for (size_t i = 0; i < bars.point_count(); ++i)
    {
        if (std::isnan(bars.x_data()[i]))
        {
            has_nan = true;
            break;
        }
    }
    EXPECT_TRUE(has_nan);
}

TEST(BarSeries, FluentAPI)
{
    BarSeries bars;
    bars.label("bars").color(colors::blue).bar_width(0.4f).baseline(1.0f);

    EXPECT_EQ(bars.label(), "bars");
    EXPECT_FLOAT_EQ(bars.bar_width(), 0.4f);
    EXPECT_FLOAT_EQ(bars.baseline(), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Axes integration
// ═══════════════════════════════════════════════════════════════════════════

TEST(AxesStats, CreateBoxPlot)
{
    Axes               ax;
    auto&              bp   = ax.box_plot();
    std::vector<float> data = {1, 2, 3, 4, 5};
    bp.add_box(1.0f, data);

    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(AxesStats, CreateViolin)
{
    Axes               ax;
    auto&              vn   = ax.violin();
    std::vector<float> data = {1, 2, 3, 4, 5};
    vn.add_violin(1.0f, data);

    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(AxesStats, CreateHistogram)
{
    Axes               ax;
    std::vector<float> data = {1, 2, 3, 4, 5};
    auto&              hist = ax.histogram(data, 5);

    EXPECT_EQ(ax.series().size(), 1u);
    EXPECT_GT(hist.point_count(), 0u);
}

TEST(AxesStats, CreateBar)
{
    Axes               ax;
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    auto&              bars    = ax.bar(pos, heights);

    EXPECT_EQ(ax.series().size(), 1u);
    EXPECT_GT(bars.point_count(), 0u);
}

TEST(AxesStats, AutoFitWithStats)
{
    Axes               ax;
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    ax.bar(pos, heights);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    // Should encompass the data
    EXPECT_LE(xlim.min, 1.0f);
    EXPECT_GE(xlim.max, 3.0f);
    EXPECT_LE(ylim.min, 0.0f);
    EXPECT_GE(ylim.max, 15.0f);
}

TEST(AxesStats, MixedSeriesTypes)
{
    Axes               ax;
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    ax.line(x, y);

    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    ax.bar(pos, heights);

    EXPECT_EQ(ax.series().size(), 2u);
    ax.auto_fit();
}
