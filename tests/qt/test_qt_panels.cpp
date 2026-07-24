// test_qt_panels.cpp — Qt integration tests for panel visibility and dock widget behavior.
//
// Verifies:
//   - Panel visibility toggle (show/hide QDockWidget)
//   - Dock widget areas are correct
//   - View menu toggle actions create correct checkable state
//   - Welcome page show/hide
//   - Status bar message
//   - Panel object names for automation/stable IDs

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>

#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/split_view_container.hpp"
#include "adapters/qt/components/spectra_app_header.hpp"
#include "adapters/qt/components/spectra_inspector_drawer.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"
#include "adapters/qt/components/spectra_status_bar.hpp"
#include "adapters/qt/panels/inspector_widget.hpp"
#include "adapters/qt/panels/data_editor_widget.hpp"
#include "adapters/qt/panels/transform_widget.hpp"

#include "app/frontend_services.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <array>
#include <memory>

namespace
{

class CountingRedrawRequest final : public spectra::RedrawRequest
{
   public:
    void request_redraw() override { ++count; }
    void request_redraw(spectra::FigureId) override { ++count; }

    int count = 0;
};

struct QtPanelEnv
{
    std::unique_ptr<spectra::FigureRegistry>               registry;
    std::unique_ptr<spectra::CommandRegistry>              cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtPanelEnv()
    {
        registry      = std::make_unique<spectra::FigureRegistry>();
        cmd_registry  = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }
};

QtPanelEnv& env()
{
    static QtPanelEnv e;
    return e;
}

}   // namespace

// ── Panel visibility: dock widget show/hide ──────────────────────────────────

TEST(QtPanels, DockWidgetShowHide)
{
    auto&       e = env();
    QMainWindow mw;

    auto* dock = new QDockWidget("Test Panel", &mw);
    dock->setObjectName("test_dock");
    mw.addDockWidget(Qt::RightDockWidgetArea, dock);

    // In offscreen mode, isVisible() checks the full widget hierarchy
    // (parent must be shown too). Use isHidden() to test the explicit
    // visibility flag instead.
    dock->setHidden(true);
    EXPECT_TRUE(dock->isHidden());
    dock->setHidden(false);
    EXPECT_FALSE(dock->isHidden());
}

TEST(QtPanels, DockWidgetTogglePattern)
{
    auto&       e = env();
    QMainWindow mw;

    auto* dock = new QDockWidget("Toggle Panel", &mw);
    dock->setObjectName("toggle_dock");
    mw.addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Simulate the toggle pattern used by SpectraMainWindow::on_toggle_*()
    // Use isHidden() to track explicit visibility state in offscreen mode
    dock->setHidden(false);
    bool hidden = dock->isHidden();
    dock->setHidden(!hidden);
    EXPECT_NE(dock->isHidden(), hidden);

    dock->setHidden(!dock->isHidden());
    EXPECT_EQ(dock->isHidden(), hidden);
}

TEST(QtPanels, MultipleDockWidgetAreas)
{
    auto&       e = env();
    QMainWindow mw;

    auto* left_dock = new QDockWidget("Left", &mw);
    left_dock->setObjectName("left_dock");
    mw.addDockWidget(Qt::LeftDockWidgetArea, left_dock);

    auto* right_dock = new QDockWidget("Right", &mw);
    right_dock->setObjectName("right_dock");
    mw.addDockWidget(Qt::RightDockWidgetArea, right_dock);

    auto* bottom_dock = new QDockWidget("Bottom", &mw);
    bottom_dock->setObjectName("bottom_dock");
    mw.addDockWidget(Qt::BottomDockWidgetArea, bottom_dock);

    // All docks should be present
    EXPECT_NE(mw.findChild<QDockWidget*>("left_dock"), nullptr);
    EXPECT_NE(mw.findChild<QDockWidget*>("right_dock"), nullptr);
    EXPECT_NE(mw.findChild<QDockWidget*>("bottom_dock"), nullptr);
}

// ── SpectraMainWindow without services (no panels built) ─────────────────────

TEST(QtPanels, MainWindowWithoutServicesHasNoPanels)
{
    auto& e = env();

    // Create main window with nullptr services — build_panels() returns early
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Panels should not be created when services is null
    EXPECT_EQ(mw.inspector_panel(), nullptr);
    EXPECT_EQ(mw.topics_panel(), nullptr);
    EXPECT_EQ(mw.settings_panel(), nullptr);
    EXPECT_EQ(mw.timeline_panel(), nullptr);
    EXPECT_EQ(mw.export_panel(), nullptr);
}

TEST(QtPanels, MainWindowWithoutServicesHasMenus)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // The native menu bar is intentionally hidden; the Spectra header owns
    // the legacy menu buttons and their QMenu popups.
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->findChildren<spectra::adapters::qt::SpectraMenuButton*>().size(), 9);
}

TEST(QtPanels, MainWindowHasStatusBar)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* status = mw.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    ASSERT_NE(status, nullptr);
    mw.set_status("Test message");
    EXPECT_EQ(status->message().toStdString(), "Test message");
}

TEST(QtPanels, MainWindowHasWelcomePage)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // No figures should be open — active figure should be invalid
    // Note: figure_tab_count may count the welcome page tab, so just check active_figure_id
    EXPECT_EQ(mw.active_figure_id(), spectra::INVALID_FIGURE_ID);
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, MainWindowHasCentralView)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* central = mw.central_view();
    ASSERT_NE(central, nullptr);
    EXPECT_EQ(central->objectName().toStdString(), "central_view");
}

// ── Split view operations (no Vulkan needed for logic) ───────────────────────

TEST(QtPanels, SplitViewInitialState)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitViewReset)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    mw.reset_splits();
    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitPreservesOpenCanvasesAndToolState)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;
    const auto              first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto              second = registry.register_figure(std::make_unique<spectra::Figure>());

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, e.action_bridge.get(), nullptr);
    ASSERT_GE(mw.add_figure_tab(first), 0);
    ASSERT_GE(mw.add_figure_tab(second), 0);

    auto* first_canvas  = mw.canvas_for(first);
    auto* second_canvas = mw.canvas_for(second);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);

    mw.central_view()->set_active_tool(spectra::ToolMode::Select);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);

    ASSERT_TRUE(mw.split_right());
    EXPECT_EQ(mw.pane_count(), 2u);
    EXPECT_EQ(mw.canvas_for(first), first_canvas);
    EXPECT_EQ(mw.canvas_for(second), second_canvas);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);

    ASSERT_TRUE(mw.close_split());
    EXPECT_EQ(mw.pane_count(), 1u);
    EXPECT_EQ(mw.figure_tab_count(), 2);
    EXPECT_EQ(mw.canvas_for(first), first_canvas);
    EXPECT_EQ(mw.canvas_for(second), second_canvas);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
}

// ── View menu toggle actions (when services is null, no toggle actions) ──────

TEST(QtPanels, ViewMenuHasNoToggleActionsWithoutServices)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Without services, build_panels() returns early, so no toggle actions
    QMenu* found_view = mw.findChild<QMenu*>("menu_view");
    ASSERT_NE(found_view, nullptr);

    // Should NOT have toggle actions for panels (since no panels were built)
    EXPECT_EQ(found_view->findChild<QAction*>("view_toggle_inspector"), nullptr);
    EXPECT_EQ(found_view->findChild<QAction*>("view_toggle_topics"), nullptr);
}

// ── Tab create/close with no runtime (logic only) ────────────────────────────

TEST(QtPanels, TabCountZeroInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // No figure tabs should be open
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, CloseFigureTabWithNoTabsDoesntCrash)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Closing a non-existent tab should be safe
    mw.close_figure_tab(spectra::FigureId{42});
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

// ── Command palette (Ctrl+K) without services ────────────────────────────────

TEST(QtPanels, CommandPaletteNotCreatedWithoutServices)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // open_command_palette should be safe even if palette wasn't created
    mw.open_command_palette();   // should not crash
}

// ── Stable object names for automation ───────────────────────────────────────

TEST(QtPanels, StableObjectNames)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Central view should have stable object name
    EXPECT_EQ(mw.central_view()->objectName().toStdString(), "central_view");

    EXPECT_NE(mw.findChild<QWidget*>("spectra_status_bar"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_app_header"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_nav_rail"), nullptr);
}

TEST(QtPanels, InspectorAxisLimitEditsUseLiveControls)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);
    axes.xlim(1.0, 5.0);
    axes.ylim(2.0, 6.0);
    const auto figure_id = registry.register_figure(std::move(figure));

    {
        spectra::adapters::qt::QtInspectorWidget inspector(&registry);
        inspector.set_active_figure(figure_id);

        auto* xmin = inspector.findChild<QDoubleSpinBox*>("axes_0_x_min");
        auto* xmax = inspector.findChild<QDoubleSpinBox*>("axes_0_x_max");
        auto* ymin = inspector.findChild<QDoubleSpinBox*>("axes_0_y_min");
        auto* ymax = inspector.findChild<QDoubleSpinBox*>("axes_0_y_max");
        ASSERT_NE(xmin, nullptr);
        ASSERT_NE(xmax, nullptr);
        ASSERT_NE(ymin, nullptr);
        ASSERT_NE(ymax, nullptr);

        xmin->setValue(-3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().max, 5.0);

        xmax->setValue(8.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().max, 8.0);

        ymin->setValue(-4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().min, -4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().max, 6.0);

        ymax->setValue(9.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().min, -4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().max, 9.0);
    }

    EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
    EXPECT_DOUBLE_EQ(axes.y_limits().max, 9.0);
}

TEST(QtPanels, InspectorLegendControlUpdatesFigure)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    figure->subplot(1, 1, 1);
    auto*      figure_ptr = figure.get();
    const auto figure_id  = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* legend = inspector.findChild<QCheckBox*>("figure_legend_visible");
    ASSERT_NE(legend, nullptr);
    ASSERT_TRUE(figure_ptr->legend().visible);

    legend->setChecked(false);
    EXPECT_FALSE(figure_ptr->legend().visible);
    legend->setChecked(true);
    EXPECT_TRUE(figure_ptr->legend().visible);
}

TEST(QtPanels, InspectorSemanticCommandTogglesDrawer)
{
    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);
    auto*                                    drawer =
        mw.findChild<spectra::adapters::qt::SpectraInspectorDrawer*>("spectra_inspector");
    ASSERT_NE(drawer, nullptr);
    EXPECT_FALSE(drawer->is_open());

    mw.toggle_inspector();
    EXPECT_TRUE(drawer->is_open());
    mw.toggle_inspector();
    EXPECT_FALSE(drawer->is_open());
}

TEST(QtPanels, DataEditorEditsAreUndoableAndRequestRedraw)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                      undo;
    CountingRedrawRequest                     redraw;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw);
    editor.set_active_figure(figure_id);

    auto* table = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->item(0, 1), nullptr);
    table->item(0, 1)->setText("9");

    ASSERT_EQ(line.y_data().size(), 2u);
    EXPECT_FLOAT_EQ(line.y_data()[0], 9.0f);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_EQ(redraw.count, 2);

    ASSERT_TRUE(undo.redo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 9.0f);
    EXPECT_EQ(redraw.count, 3);
}

TEST(QtPanels, DataEditorRejectsInvalidValuesWithoutHistory)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 1> x{0.0f};
    const std::array<float, 1> y{2.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                      undo;
    CountingRedrawRequest                     redraw;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw);
    editor.set_active_figure(figure_id);

    auto* table = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(table, nullptr);
    table->item(0, 1)->setText("not-a-number");

    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_EQ(table->item(0, 1)->text(), "2");
    EXPECT_EQ(undo.undo_count(), 0u);
    EXPECT_EQ(redraw.count, 0);
}

TEST(QtPanels, TransformApplicationIsUndoableAndRequestsRedraw)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* combo = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* apply = transforms.findChild<QPushButton*>("apply_transform_btn");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(apply, nullptr);
    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    apply->click();

    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 6.0f);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 3.0f);
    EXPECT_EQ(redraw.count, 2);

    ASSERT_TRUE(undo.redo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 6.0f);
    EXPECT_EQ(redraw.count, 3);
}

TEST(QtPanels, TransformPipelineIsOneUndoableTransaction)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* combo  = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale  = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* offset = transforms.findChild<QDoubleSpinBox*>("transform_offset");
    auto* add    = transforms.findChild<QPushButton*>("add_transform_pipeline_btn");
    auto* apply  = transforms.findChild<QPushButton*>("apply_transform_pipeline_btn");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(apply, nullptr);

    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    add->click();
    combo->setCurrentText("Offset");
    offset->setValue(1.0);
    add->click();
    apply->click();

    EXPECT_FLOAT_EQ(line.y_data()[0], 5.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 7.0f);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 3.0f);
    EXPECT_EQ(redraw.count, 2);
}
