#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <thread>
#include <vector>

#include "ui/commands/series_clipboard.hpp"
#include "ui/input/selection_context.hpp"

using namespace spectra;

// Helper: create a LineSeries with data using set_x/set_y
static LineSeries make_line(std::initializer_list<float> x, std::initializer_list<float> y)
{
    std::vector<float> xv(x);
    std::vector<float> yv(y);
    LineSeries         ls;
    ls.set_x(xv);
    ls.set_y(yv);
    return ls;
}

// Helper: create a ScatterSeries with data using set_x/set_y
static ScatterSeries make_scatter(std::initializer_list<float> x, std::initializer_list<float> y)
{
    std::vector<float> xv(x);
    std::vector<float> yv(y);
    ScatterSeries      ss;
    ss.set_x(xv);
    ss.set_y(yv);
    return ss;
}

// ─── Snapshot Tests ──────────────────────────────────────────────────────────

TEST(SeriesClipboard, SnapshotLineSeries)
{
    auto ls = make_line({1, 2, 3, 4, 5}, {10, 20, 30, 40, 50});
    ls.label("test_line");
    ls.color(Color{1.0f, 0.0f, 0.0f});
    ls.width(3.0f);
    ls.visible(false);
    ls.x_offset(1783350621.752999);

    auto snap = SeriesClipboard::snapshot(ls);

    EXPECT_EQ(snap.type, SeriesSnapshot::Type::Line);
    EXPECT_EQ(snap.label, "test_line");
    EXPECT_FLOAT_EQ(snap.color.r, 1.0f);
    EXPECT_FLOAT_EQ(snap.color.g, 0.0f);
    EXPECT_FLOAT_EQ(snap.line_width, 3.0f);
    EXPECT_FALSE(snap.visible);
    EXPECT_NEAR(snap.x_offset, 1783350621.752999, 1e-6);
    ASSERT_EQ(snap.x_data.size(), 5u);
    ASSERT_EQ(snap.y_data.size(), 5u);
    EXPECT_FLOAT_EQ(snap.x_data[0], 1.0f);
    EXPECT_FLOAT_EQ(snap.y_data[4], 50.0f);
}

TEST(SeriesClipboard, SnapshotScatterSeries)
{
    auto ss = make_scatter({0, 1, 2}, {5, 6, 7});
    ss.label("test_scatter");
    ss.color(Color{0.0f, 1.0f, 0.0f});
    ss.size(8.0f);

    auto snap = SeriesClipboard::snapshot(ss);

    EXPECT_EQ(snap.type, SeriesSnapshot::Type::Scatter);
    EXPECT_EQ(snap.label, "test_scatter");
    EXPECT_FLOAT_EQ(snap.color.g, 1.0f);
    EXPECT_FLOAT_EQ(snap.point_size, 8.0f);
    EXPECT_TRUE(snap.visible);
    ASSERT_EQ(snap.x_data.size(), 3u);
}

TEST(SeriesClipboard, SnapshotDeepCopy)
{
    auto ls = make_line({1, 2, 3}, {4, 5, 6});
    ls.label("orig");

    auto snap = SeriesClipboard::snapshot(ls);

    // Modify original — snapshot must be independent
    ls.label("modified");

    EXPECT_EQ(snap.label, "orig");
    EXPECT_FLOAT_EQ(snap.x_data[0], 1.0f);
}

// ─── Paste Tests ─────────────────────────────────────────────────────────────

TEST(SeriesClipboard, PasteLineSeriesToAxes)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    SeriesSnapshot snap;
    snap.type       = SeriesSnapshot::Type::Line;
    snap.label      = "pasted_line";
    snap.color      = Color{0.5f, 0.5f, 0.5f};
    snap.line_width = 4.0f;
    snap.visible    = true;
    snap.x_offset   = 1783350621.752999;
    snap.x_data     = {1, 2, 3};
    snap.y_data     = {10, 20, 30};

    Series* result = SeriesClipboard::paste_to(ax, snap);
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->label(), "pasted_line");
    EXPECT_FLOAT_EQ(result->color().r, 0.5f);
    EXPECT_TRUE(result->visible());
    EXPECT_NEAR(result->x_offset(), 1783350621.752999, 1e-6);
    EXPECT_EQ(ax.series().size(), 1u);

    auto* line = dynamic_cast<LineSeries*>(result);
    ASSERT_NE(line, nullptr);
    EXPECT_FLOAT_EQ(line->width(), 4.0f);
    EXPECT_EQ(line->point_count(), 3u);
}

TEST(SeriesClipboard, PasteScatterSeriesToAxes)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    SeriesSnapshot snap;
    snap.type       = SeriesSnapshot::Type::Scatter;
    snap.label      = "pasted_scatter";
    snap.color      = Color{0.0f, 0.0f, 1.0f};
    snap.point_size = 12.0f;
    snap.x_data     = {0, 1};
    snap.y_data     = {5, 6};

    Series* result = SeriesClipboard::paste_to(ax, snap);
    ASSERT_NE(result, nullptr);

    auto* scatter = dynamic_cast<ScatterSeries*>(result);
    ASSERT_NE(scatter, nullptr);
    EXPECT_FLOAT_EQ(scatter->size(), 12.0f);
    EXPECT_EQ(scatter->point_count(), 2u);
}

// ─── Clipboard Operations ────────────────────────────────────────────────────

TEST(SeriesClipboard, CopyStoresData)
{
    SeriesClipboard clipboard;
    EXPECT_FALSE(clipboard.has_data());

    auto ls = make_line({1, 2}, {3, 4});
    ls.label("my_series");

    clipboard.copy(ls);
    EXPECT_TRUE(clipboard.has_data());
    EXPECT_FALSE(clipboard.is_cut());

    const auto* snap = clipboard.peek();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->label, "my_series");
    EXPECT_EQ(snap->x_data.size(), 2u);
}

TEST(SeriesClipboard, CutSetsFlag)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1}, {2});

    clipboard.cut(ls);
    EXPECT_TRUE(clipboard.has_data());
    EXPECT_TRUE(clipboard.is_cut());
}

TEST(SeriesClipboard, PasteCreatesSeriesOnAxes)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1, 2, 3}, {10, 20, 30});
    ls.label("source");
    ls.color(Color{1, 0, 0});

    clipboard.copy(ls);

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    Series* pasted = clipboard.paste(ax);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->label(), "source");
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(SeriesClipboard, PasteEmptyClipboardReturnsNull)
{
    SeriesClipboard clipboard;

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    Series* pasted = clipboard.paste(ax);
    EXPECT_EQ(pasted, nullptr);
    EXPECT_EQ(ax.series().size(), 0u);
}

TEST(SeriesClipboard, CutThenPasteClearsFlag)
{
    SeriesClipboard clipboard;

    auto ss = make_scatter({1, 2}, {3, 4});
    ss.label("cut_scatter");

    clipboard.cut(ss);
    EXPECT_TRUE(clipboard.is_cut());

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    Series* pasted = clipboard.paste(ax);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->label(), "cut_scatter");

    // After paste of a cut, is_cut flag should be cleared
    EXPECT_FALSE(clipboard.is_cut());
    // But data stays for potential re-paste
    EXPECT_TRUE(clipboard.has_data());
}

TEST(SeriesClipboard, ClearResetsEverything)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1}, {2});
    clipboard.copy(ls);

    EXPECT_TRUE(clipboard.has_data());
    clipboard.clear();
    EXPECT_FALSE(clipboard.has_data());
    EXPECT_EQ(clipboard.peek(), nullptr);
}

TEST(SeriesClipboard, MultipleCopiesOverwrite)
{
    SeriesClipboard clipboard;

    auto ls1 = make_line({1, 2}, {3, 4});
    ls1.label("first");
    clipboard.copy(ls1);

    auto ls2 = make_line({5, 6, 7}, {8, 9, 10});
    ls2.label("second");
    clipboard.copy(ls2);

    const auto* snap = clipboard.peek();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->label, "second");
    EXPECT_EQ(snap->x_data.size(), 3u);
}

TEST(SeriesClipboard, PasteMultipleTimes)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1, 2}, {3, 4});
    ls.label("reuse");
    clipboard.copy(ls);

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    clipboard.paste(ax);
    clipboard.paste(ax);
    clipboard.paste(ax);

    EXPECT_EQ(ax.series().size(), 3u);
}

TEST(SeriesClipboard, CopyPreservesStyle)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1, 2}, {3, 4});
    ls.label("styled");
    ls.color(Color{0.1f, 0.2f, 0.3f, 0.4f});
    ls.line_style(LineStyle::Dashed);
    ls.marker_style(MarkerStyle::Circle);
    ls.marker_size(7.5f);
    ls.opacity(0.8f);
    ls.width(5.0f);

    clipboard.copy(ls);

    Figure fig;
    auto&  ax     = fig.subplot(1, 1, 1);
    auto*  pasted = clipboard.paste(ax);
    ASSERT_NE(pasted, nullptr);

    EXPECT_FLOAT_EQ(pasted->color().r, 0.1f);
    EXPECT_FLOAT_EQ(pasted->color().g, 0.2f);
    EXPECT_FLOAT_EQ(pasted->color().b, 0.3f);
    EXPECT_EQ(pasted->line_style(), LineStyle::Dashed);
    EXPECT_EQ(pasted->marker_style(), MarkerStyle::Circle);
    EXPECT_FLOAT_EQ(pasted->marker_size(), 7.5f);
    EXPECT_FLOAT_EQ(pasted->opacity(), 0.8f);

    auto* line = dynamic_cast<LineSeries*>(pasted);
    ASSERT_NE(line, nullptr);
    EXPECT_FLOAT_EQ(line->width(), 5.0f);
}

// ─── Thread Safety (basic smoke test) ────────────────────────────────────────

TEST(SeriesClipboard, ConcurrentCopyAndPeek)
{
    SeriesClipboard clipboard;

    auto ls = make_line({1, 2, 3}, {4, 5, 6});
    ls.label("concurrent");

    // Run copy and peek from multiple threads — should not crash
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i)
    {
        threads.emplace_back(
            [&clipboard, &ls]()
            {
                clipboard.copy(ls);
                clipboard.has_data();
                clipboard.peek();
            });
    }
    for (auto& t : threads)
        t.join();

    EXPECT_TRUE(clipboard.has_data());
}

// ─── 3D Series Tests ─────────────────────────────────────────────────────────

TEST(SeriesClipboard, SnapshotLineSeries3D)
{
    std::vector<float> x = {1, 2, 3};
    std::vector<float> y = {4, 5, 6};
    std::vector<float> z = {7, 8, 9};
    LineSeries3D       ls(x, y, z);
    ls.label("line3d");
    ls.color(Color{0.5f, 0.5f, 0.5f});
    ls.width(4.0f);

    auto snap = SeriesClipboard::snapshot(ls);

    EXPECT_EQ(snap.type, SeriesSnapshot::Type::Line3D);
    EXPECT_TRUE(snap.is_3d());
    EXPECT_FALSE(snap.is_2d());
    EXPECT_EQ(snap.label, "line3d");
    EXPECT_FLOAT_EQ(snap.line_width, 4.0f);
    ASSERT_EQ(snap.x_data.size(), 3u);
    ASSERT_EQ(snap.z_data.size(), 3u);
    EXPECT_FLOAT_EQ(snap.z_data[2], 9.0f);
}

TEST(SeriesClipboard, SnapshotScatterSeries3D)
{
    std::vector<float> x = {0, 1};
    std::vector<float> y = {2, 3};
    std::vector<float> z = {4, 5};
    ScatterSeries3D    ss(x, y, z);
    ss.label("scatter3d");
    ss.size(10.0f);

    auto snap = SeriesClipboard::snapshot(ss);

    EXPECT_EQ(snap.type, SeriesSnapshot::Type::Scatter3D);
    EXPECT_TRUE(snap.is_3d());
    EXPECT_FLOAT_EQ(snap.point_size, 10.0f);
    ASSERT_EQ(snap.z_data.size(), 2u);
}

TEST(SeriesClipboard, Paste3DInto2DDropsZ)
{
    // Copy a 3D line, paste into 2D axes → should become a 2D LineSeries
    SeriesClipboard clipboard;

    std::vector<float> x = {1, 2, 3};
    std::vector<float> y = {4, 5, 6};
    std::vector<float> z = {7, 8, 9};
    LineSeries3D       ls3(x, y, z);
    ls3.label("from3d");

    clipboard.copy(ls3);

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    Series* pasted = clipboard.paste(ax);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->label(), "from3d");

    auto* line2d = dynamic_cast<LineSeries*>(pasted);
    ASSERT_NE(line2d, nullptr);
    EXPECT_EQ(line2d->point_count(), 3u);
    // x/y data preserved, z dropped
    EXPECT_FLOAT_EQ(line2d->x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(line2d->y_data()[2], 6.0f);
}

TEST(SeriesClipboard, Paste2DInto3DAddsZero)
{
    // Copy a 2D line, paste into 3D axes → should become LineSeries3D with z=0
    SeriesClipboard clipboard;

    auto ls = make_line({10, 20}, {30, 40});
    ls.label("from2d");
    clipboard.copy(ls);

    Figure fig;
    auto&  ax3d = fig.subplot3d(1, 1, 1);

    Series* pasted = clipboard.paste(ax3d);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->label(), "from2d");

    auto* line3d = dynamic_cast<LineSeries3D*>(pasted);
    ASSERT_NE(line3d, nullptr);
    EXPECT_EQ(line3d->point_count(), 2u);
    EXPECT_FLOAT_EQ(line3d->x_data()[0], 10.0f);
    EXPECT_FLOAT_EQ(line3d->z_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(line3d->z_data()[1], 0.0f);
}

TEST(SeriesClipboard, Paste3DScatterInto2D)
{
    SeriesClipboard clipboard;

    std::vector<float> x = {1, 2};
    std::vector<float> y = {3, 4};
    std::vector<float> z = {5, 6};
    ScatterSeries3D    ss3(x, y, z);
    ss3.label("scat3d");
    ss3.size(12.0f);

    clipboard.copy(ss3);

    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    Series* pasted = clipboard.paste(ax);
    ASSERT_NE(pasted, nullptr);

    auto* scat2d = dynamic_cast<ScatterSeries*>(pasted);
    ASSERT_NE(scat2d, nullptr);
    EXPECT_FLOAT_EQ(scat2d->size(), 12.0f);
    EXPECT_EQ(scat2d->point_count(), 2u);
}

TEST(SeriesClipboard, Paste2DScatterInto3D)
{
    SeriesClipboard clipboard;

    auto ss = make_scatter({5, 6, 7}, {8, 9, 10});
    ss.label("scat2d");
    ss.size(8.0f);
    clipboard.copy(ss);

    Figure fig;
    auto&  ax3d = fig.subplot3d(1, 1, 1);

    Series* pasted = clipboard.paste(ax3d);
    ASSERT_NE(pasted, nullptr);

    auto* scat3d = dynamic_cast<ScatterSeries3D*>(pasted);
    ASSERT_NE(scat3d, nullptr);
    EXPECT_FLOAT_EQ(scat3d->size(), 8.0f);
    EXPECT_EQ(scat3d->point_count(), 3u);
    EXPECT_FLOAT_EQ(scat3d->z_data()[0], 0.0f);
}

// ─── Regression: copy+paste+delete must not leave dangling selection ─────────

TEST(SeriesClipboard, CopyPasteDeleteClearsSelection)
{
    // Reproduces the crash scenario: copy series, paste it, then delete the
    // original while the selection still points to it.  The on_series_removed
    // callback must clear the selection to prevent a dangling dereference.
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    // Create original series
    std::vector<float> x1{1, 2, 3}, y1{4, 5, 6};
    auto&              s1 = ax.line(x1, y1);
    s1.label("original");

    // Set up selection pointing to s1
    ui::SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s1, 0);
    ASSERT_EQ(ctx.type, ui::SelectionType::Series);
    ASSERT_EQ(ctx.series, &s1);

    // Copy
    SeriesClipboard clipboard;
    clipboard.copy(s1);
    EXPECT_TRUE(clipboard.has_data());

    // Paste — creates s2 in same axes, selection still points to s1
    Series* s2 = clipboard.paste(ax);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(ax.series().size(), 2u);
    EXPECT_EQ(ctx.series, &s1);   // selection unchanged by paste

    // Simulate on_series_removed callback clearing selection (the fix)
    // Wire a callback that clears selection when s1 is removed
    ax.set_series_removed_callback(
        [&ctx](const Series* s)
        {
            if (ctx.series == s)
                ctx.clear();
            else
            {
                auto& sv = ctx.selected_series;
                for (auto it = sv.begin(); it != sv.end(); ++it)
                {
                    if (it->series == s)
                    {
                        sv.erase(it);
                        if (sv.empty())
                            ctx.clear();
                        break;
                    }
                }
            }
        });

    // Delete s1 (the originally selected series)
    ax.remove_series(0);

    // Selection must be cleared — no dangling pointer
    EXPECT_EQ(ctx.type, ui::SelectionType::None);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ax.series().size(), 1u);   // only s2 remains
}

TEST(SeriesClipboard, DeleteClearsMultiSelection)
{
    // Verify that removing a series from a multi-selection properly
    // cleans up the selected_series vector.
    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> xa{1, 2}, ya{3, 4};
    std::vector<float> xb{5, 6}, yb{7, 8};
    std::vector<float> xc{9, 10}, yc{11, 12};
    auto&              s1 = ax.line(xa, ya);
    auto&              s2 = ax.line(xb, yb);
    auto&              s3 = ax.line(xc, yc);

    ui::SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s1, 0);
    ctx.add_series(&fig, &ax, nullptr, 0, &s2, 1);
    ctx.add_series(&fig, &ax, nullptr, 0, &s3, 2);
    ASSERT_EQ(ctx.selected_series.size(), 3u);

    // Wire callback
    ax.set_series_removed_callback(
        [&ctx](const Series* s)
        {
            if (ctx.series == s)
                ctx.clear();
            else
            {
                auto& sv = ctx.selected_series;
                for (auto it = sv.begin(); it != sv.end(); ++it)
                {
                    if (it->series == s)
                    {
                        sv.erase(it);
                        if (sv.empty())
                            ctx.clear();
                        break;
                    }
                }
            }
        });

    // After add_series, ctx.series points to s3 (last added)
    EXPECT_EQ(ctx.series, &s3);

    // Remove s2 (middle of selection, index 1)
    ax.remove_series(1);
    EXPECT_EQ(ctx.selected_series.size(), 2u);
    EXPECT_EQ(ctx.type, ui::SelectionType::Series);   // still has selection

    // Remove s1 (index 0, in vector but not ctx.series) — shrinks vector
    ax.remove_series(0);
    EXPECT_EQ(ctx.selected_series.size(), 1u);
    EXPECT_EQ(ctx.type, ui::SelectionType::Series);   // s3 still selected

    // Remove s3 (now index 0, IS ctx.series) — should clear entirely
    ax.remove_series(0);
    EXPECT_EQ(ctx.type, ui::SelectionType::None);
    EXPECT_EQ(ctx.series, nullptr);
}
