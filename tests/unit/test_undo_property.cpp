#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "ui/commands/undoable_property.hpp"

using namespace spectra;

// ─── Helper: create a figure with one axes and one line series ───────────────

static Figure make_test_figure()
{
    Figure       fig;
    auto&        ax  = fig.subplot(1, 1, 1);
    static float x[] = {0.0f, 1.0f, 2.0f};
    static float y[] = {0.0f, 1.0f, 0.0f};
    ax.line(x, y).label("test_line").color(colors::blue);
    return fig;
}

// ─── Axis limits ─────────────────────────────────────────────────────────────

TEST(UndoProperty, UndoXlim)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.xlim(0.0f, 10.0f);
    undoable_xlim(&mgr, ax, 2.0f, 8.0f);

    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 8.0f);

    mgr.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 10.0f);

    mgr.redo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 8.0f);
}

TEST(UndoProperty, UndoYlim)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.ylim(-1.0f, 1.0f);
    undoable_ylim(&mgr, ax, -5.0f, 5.0f);

    EXPECT_FLOAT_EQ(ax.y_limits().min, -5.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().max, 5.0f);

    mgr.undo();
    EXPECT_FLOAT_EQ(ax.y_limits().min, -1.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().max, 1.0f);
}

TEST(UndoProperty, UndoSetLimits)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    AxisLimits new_x{1.0f, 9.0f};
    AxisLimits new_y{2.0f, 8.0f};
    undoable_set_limits(&mgr, ax, new_x, new_y);

    EXPECT_FLOAT_EQ(ax.x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, 2.0f);

    mgr.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, 0.0f);

    mgr.redo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, 2.0f);
}

// ─── Grid toggle ─────────────────────────────────────────────────────────────

TEST(UndoProperty, UndoToggleGrid)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    EXPECT_TRUE(ax.grid_enabled());

    undoable_toggle_grid(&mgr, ax);
    EXPECT_FALSE(ax.grid_enabled());

    mgr.undo();
    EXPECT_TRUE(ax.grid_enabled());

    mgr.redo();
    EXPECT_FALSE(ax.grid_enabled());
}

TEST(UndoProperty, UndoToggleGridAll)
{
    UndoManager mgr;
    Figure      fig;
    fig.subplot(1, 2, 1);
    fig.subplot(1, 2, 2);

    EXPECT_TRUE(fig.axes()[0]->grid_enabled());
    EXPECT_TRUE(fig.axes()[1]->grid_enabled());

    undoable_toggle_grid_all(&mgr, fig);
    EXPECT_FALSE(fig.axes()[0]->grid_enabled());
    EXPECT_FALSE(fig.axes()[1]->grid_enabled());

    // Single undo should revert both axes (grouped)
    EXPECT_EQ(mgr.undo_count(), 1u);
    mgr.undo();
    EXPECT_TRUE(fig.axes()[0]->grid_enabled());
    EXPECT_TRUE(fig.axes()[1]->grid_enabled());
}

// ─── Border toggle ──────────────────────────────────────────────────────────

TEST(UndoProperty, UndoToggleBorder)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    EXPECT_TRUE(ax.border_enabled());

    undoable_toggle_border(&mgr, ax);
    EXPECT_FALSE(ax.border_enabled());

    mgr.undo();
    EXPECT_TRUE(ax.border_enabled());
}

TEST(UndoProperty, UndoToggleBorderAll)
{
    UndoManager mgr;
    Figure      fig;
    fig.subplot(1, 2, 1);
    fig.subplot(1, 2, 2);

    undoable_toggle_border_all(&mgr, fig);
    EXPECT_FALSE(fig.axes()[0]->border_enabled());
    EXPECT_FALSE(fig.axes()[1]->border_enabled());

    EXPECT_EQ(mgr.undo_count(), 1u);
    mgr.undo();
    EXPECT_TRUE(fig.axes()[0]->border_enabled());
    EXPECT_TRUE(fig.axes()[1]->border_enabled());
}

// ─── Series visibility ──────────────────────────────────────────────────────

TEST(UndoProperty, UndoToggleSeriesVisibility)
{
    UndoManager mgr;
    Figure      fig    = make_test_figure();
    auto&       series = *fig.axes()[0]->series()[0];

    EXPECT_TRUE(series.visible());

    undoable_toggle_series_visibility(&mgr, series);
    EXPECT_FALSE(series.visible());

    mgr.undo();
    EXPECT_TRUE(series.visible());

    mgr.redo();
    EXPECT_FALSE(series.visible());
}

TEST(UndoProperty, UndoToggleSeriesVisibilityDescription)
{
    UndoManager mgr;
    Figure      fig    = make_test_figure();
    auto&       series = *fig.axes()[0]->series()[0];

    undoable_toggle_series_visibility(&mgr, series);
    EXPECT_EQ(mgr.undo_description(), "Hide test_line");

    mgr.undo();
    undoable_toggle_series_visibility(&mgr, series);
    // Now it was visible, toggling hides it
    EXPECT_EQ(mgr.undo_description(), "Hide test_line");
}

// ─── Series color ────────────────────────────────────────────────────────────

TEST(UndoProperty, UndoSetSeriesColor)
{
    UndoManager mgr;
    Figure      fig    = make_test_figure();
    auto&       series = *fig.axes()[0]->series()[0];

    Color old_color = series.color();
    Color new_color = colors::red;

    undoable_set_series_color(&mgr, series, new_color);
    EXPECT_FLOAT_EQ(series.color().r, 1.0f);
    EXPECT_FLOAT_EQ(series.color().g, 0.0f);

    mgr.undo();
    EXPECT_FLOAT_EQ(series.color().r, old_color.r);
    EXPECT_FLOAT_EQ(series.color().g, old_color.g);
    EXPECT_FLOAT_EQ(series.color().b, old_color.b);

    mgr.redo();
    EXPECT_FLOAT_EQ(series.color().r, 1.0f);
}

// ─── Line width ──────────────────────────────────────────────────────────────

TEST(UndoProperty, UndoSetLineWidth)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto*       ls  = dynamic_cast<LineSeries*>(fig.axes()[0]->series()[0].get());
    ASSERT_NE(ls, nullptr);

    float old_width = ls->width();
    undoable_set_line_width(&mgr, *ls, 5.0f);
    EXPECT_FLOAT_EQ(ls->width(), 5.0f);

    mgr.undo();
    EXPECT_FLOAT_EQ(ls->width(), old_width);

    mgr.redo();
    EXPECT_FLOAT_EQ(ls->width(), 5.0f);
}

TEST(UndoProperty, RegistryBackedInspectorPropertiesAreUndoable)
{
    FigureRegistry registry;
    auto           figure    = std::make_unique<Figure>(make_test_figure());
    const FigureId figure_id = registry.register_figure(std::move(figure));
    Figure*        stored    = registry.get(figure_id);
    ASSERT_NE(stored, nullptr);
    Series* series = stored->axes()[0]->series()[0].get();
    ASSERT_NE(series, nullptr);
    const float old_width = series->plot_style().line_width;

    UndoManager mgr;
    ASSERT_TRUE(undoable_set_figure_title(&mgr, registry, figure_id, "Renamed"));
    ASSERT_TRUE(undoable_set_series_label(&mgr, registry, figure_id, 0, 0, "renamed_series"));
    ASSERT_TRUE(undoable_set_series_line_width(&mgr, registry, figure_id, 0, 0, 7.0f));
    EXPECT_EQ(stored->tab_title(), "Renamed");
    EXPECT_EQ(series->label(), "renamed_series");
    EXPECT_FLOAT_EQ(series->plot_style().line_width, 7.0f);

    ASSERT_TRUE(mgr.undo());
    EXPECT_FLOAT_EQ(series->plot_style().line_width, old_width);
    ASSERT_TRUE(mgr.undo());
    EXPECT_EQ(series->label(), "test_line");
    ASSERT_TRUE(mgr.undo());
    EXPECT_TRUE(stored->tab_title().empty());

    ASSERT_TRUE(mgr.redo());
    ASSERT_TRUE(mgr.redo());
    ASSERT_TRUE(mgr.redo());
    EXPECT_EQ(stored->tab_title(), "Renamed");
    EXPECT_EQ(series->label(), "renamed_series");
    EXPECT_FLOAT_EQ(series->plot_style().line_width, 7.0f);
}

// ─── Legend visibility ───────────────────────────────────────────────────────

TEST(UndoProperty, UndoToggleLegend)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();

    EXPECT_TRUE(fig.legend().visible);

    undoable_toggle_legend(&mgr, fig);
    EXPECT_FALSE(fig.legend().visible);

    mgr.undo();
    EXPECT_TRUE(fig.legend().visible);

    mgr.redo();
    EXPECT_FALSE(fig.legend().visible);
}

// ─── Axis title / labels ────────────────────────────────────────────────────

TEST(UndoProperty, UndoSetTitle)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.title("Original");
    undoable_set_title(&mgr, ax, "New Title");
    EXPECT_EQ(ax.get_title(), "New Title");

    mgr.undo();
    EXPECT_EQ(ax.get_title(), "Original");

    mgr.redo();
    EXPECT_EQ(ax.get_title(), "New Title");
}

TEST(UndoProperty, UndoSetXLabel)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.xlabel("Time");
    undoable_set_xlabel(&mgr, ax, "Frequency");
    EXPECT_EQ(ax.get_xlabel(), "Frequency");

    mgr.undo();
    EXPECT_EQ(ax.get_xlabel(), "Time");
}

TEST(UndoProperty, UndoSetYLabel)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.ylabel("Amplitude");
    undoable_set_ylabel(&mgr, ax, "Power");
    EXPECT_EQ(ax.get_ylabel(), "Power");

    mgr.undo();
    EXPECT_EQ(ax.get_ylabel(), "Amplitude");
}

// ─── Reset view (full figure snapshot) ──────────────────────────────────────

TEST(UndoProperty, UndoResetView)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.xlim(5.0f, 15.0f);
    ax.ylim(5.0f, 15.0f);

    undoable_reset_view(&mgr, fig);

    // After auto_fit, limits should change
    auto xl = ax.x_limits();
    // auto yl = ax.y_limits();  // Currently unused

    mgr.undo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 5.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 15.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, 5.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().max, 15.0f);

    mgr.redo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, xl.min);
    EXPECT_FLOAT_EQ(ax.x_limits().max, xl.max);
}

TEST(UndoProperty, CaptureRestoreFigureAxes)
{
    Figure fig;
    fig.subplot(1, 2, 1);
    fig.subplot(1, 2, 2);
    fig.axes()[0]->xlim(1.0f, 2.0f);
    fig.axes()[1]->xlim(3.0f, 4.0f);

    auto snap = capture_figure_axes(fig);
    ASSERT_EQ(snap.entries.size(), 2u);
    EXPECT_FLOAT_EQ(snap.entries[0].x_limits.min, 1.0f);
    EXPECT_FLOAT_EQ(snap.entries[1].x_limits.min, 3.0f);

    fig.axes()[0]->xlim(10.0f, 20.0f);
    fig.axes()[1]->xlim(30.0f, 40.0f);

    restore_figure_axes(snap);
    EXPECT_FLOAT_EQ(fig.axes()[0]->x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(fig.axes()[1]->x_limits().min, 3.0f);
}

// ─── Null UndoManager safety ────────────────────────────────────────────────

TEST(UndoProperty, NullManagerXlim)
{
    Figure fig = make_test_figure();
    auto&  ax  = *fig.axes()[0];
    ax.xlim(0.0f, 10.0f);

    // Should not crash with null manager
    undoable_xlim(nullptr, ax, 2.0f, 8.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, 8.0f);
}

TEST(UndoProperty, NullManagerToggleGrid)
{
    Figure fig = make_test_figure();
    auto&  ax  = *fig.axes()[0];
    EXPECT_TRUE(ax.grid_enabled());

    undoable_toggle_grid(nullptr, ax);
    EXPECT_FALSE(ax.grid_enabled());
}

TEST(UndoProperty, NullManagerToggleLegend)
{
    Figure fig = make_test_figure();
    EXPECT_TRUE(fig.legend().visible);

    undoable_toggle_legend(nullptr, fig);
    EXPECT_FALSE(fig.legend().visible);
}

TEST(UndoProperty, NullManagerResetView)
{
    Figure fig = make_test_figure();
    auto&  ax  = *fig.axes()[0];
    ax.xlim(5.0f, 15.0f);

    undoable_reset_view(nullptr, fig);
    // Should auto-fit without crashing
    EXPECT_NE(ax.x_limits().min, 5.0f);
}

// ─── Generic undoable_set ───────────────────────────────────────────────────

TEST(UndoProperty, GenericUndoableSet)
{
    UndoManager mgr;
    float       value = 3.14f;

    undoable_set<float>(&mgr,
                        "Change value",
                        3.14f,
                        6.28f,
                        [&value](const float& v) { value = v; });
    EXPECT_FLOAT_EQ(value, 6.28f);

    mgr.undo();
    EXPECT_FLOAT_EQ(value, 3.14f);

    mgr.redo();
    EXPECT_FLOAT_EQ(value, 6.28f);
}

TEST(UndoProperty, GenericUndoableSetString)
{
    UndoManager mgr;
    std::string text = "hello";

    undoable_set<std::string>(&mgr,
                              "Change text",
                              "hello",
                              "world",
                              [&text](const std::string& v) { text = v; });
    EXPECT_EQ(text, "world");

    mgr.undo();
    EXPECT_EQ(text, "hello");
}

// ─── Multiple undo/redo chain ───────────────────────────────────────────────

TEST(UndoProperty, MultipleUndoRedoChain)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    ax.xlim(0.0f, 10.0f);

    // Action 1: change xlim
    undoable_xlim(&mgr, ax, 1.0f, 9.0f);
    // Action 2: toggle grid
    undoable_toggle_grid(&mgr, ax);
    // Action 3: change xlim again
    undoable_xlim(&mgr, ax, 2.0f, 8.0f);

    EXPECT_EQ(mgr.undo_count(), 3u);
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
    EXPECT_FALSE(ax.grid_enabled());

    mgr.undo();   // Undo action 3
    EXPECT_FLOAT_EQ(ax.x_limits().min, 1.0f);
    EXPECT_FALSE(ax.grid_enabled());

    mgr.undo();   // Undo action 2
    EXPECT_TRUE(ax.grid_enabled());

    mgr.undo();   // Undo action 1
    EXPECT_FLOAT_EQ(ax.x_limits().min, 0.0f);

    // Redo all
    mgr.redo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 1.0f);
    mgr.redo();
    EXPECT_FALSE(ax.grid_enabled());
    mgr.redo();
    EXPECT_FLOAT_EQ(ax.x_limits().min, 2.0f);
}

// ─── Undo descriptions ─────────────────────────────────────────────────────

TEST(UndoProperty, DescriptionGrid)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    undoable_toggle_grid(&mgr, ax);
    EXPECT_EQ(mgr.undo_description(), "Hide grid");

    mgr.undo();
    undoable_toggle_grid(&mgr, ax);
    EXPECT_EQ(mgr.undo_description(), "Hide grid");
}

TEST(UndoProperty, DescriptionLegend)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();

    undoable_toggle_legend(&mgr, fig);
    EXPECT_EQ(mgr.undo_description(), "Hide legend");
}

TEST(UndoProperty, DescriptionBorder)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();
    auto&       ax  = *fig.axes()[0];

    undoable_toggle_border(&mgr, ax);
    EXPECT_EQ(mgr.undo_description(), "Hide border");
}

TEST(UndoProperty, DescriptionColor)
{
    UndoManager mgr;
    Figure      fig    = make_test_figure();
    auto&       series = *fig.axes()[0]->series()[0];

    undoable_set_series_color(&mgr, series, colors::red);
    EXPECT_EQ(mgr.undo_description(), "Change color of test_line");
}

TEST(UndoProperty, DescriptionResetView)
{
    UndoManager mgr;
    Figure      fig = make_test_figure();

    undoable_reset_view(&mgr, fig);
    EXPECT_EQ(mgr.undo_description(), "Reset view");
}
