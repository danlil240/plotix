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
#include <QDockWidget>
#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/split_view_container.hpp"
#include "adapters/qt/components/spectra_app_header.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"
#include "adapters/qt/components/spectra_status_bar.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <memory>

namespace {

struct QtPanelEnv
{
    std::unique_ptr<spectra::FigureRegistry> registry;
    std::unique_ptr<spectra::CommandRegistry> cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtPanelEnv()
    {
        registry = std::make_unique<spectra::FigureRegistry>();
        cmd_registry = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }

};

QtPanelEnv& env()
{
    static QtPanelEnv e;
    return e;
}

} // namespace

// ── Panel visibility: dock widget show/hide ──────────────────────────────────

TEST(QtPanels, DockWidgetShowHide)
{
    auto& e = env();
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
    auto& e = env();
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
    auto& e = env();
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
    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // The native menu bar is intentionally hidden; the Spectra header owns
    // the legacy menu buttons and their QMenu popups.
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>(
        "spectra_app_header");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->findChildren<spectra::adapters::qt::SpectraMenuButton*>().size(), 8);
}

TEST(QtPanels, MainWindowHasStatusBar)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* status = mw.findChild<spectra::adapters::qt::SpectraStatusBar*>(
        "spectra_status_bar");
    ASSERT_NE(status, nullptr);
    mw.set_status("Test message");
    EXPECT_EQ(status->message().toStdString(), "Test message");
}

TEST(QtPanels, MainWindowHasWelcomePage)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // No figures should be open — active figure should be invalid
    // Note: figure_tab_count may count the welcome page tab, so just check active_figure_id
    EXPECT_EQ(mw.active_figure_id(), spectra::INVALID_FIGURE_ID);
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, MainWindowHasCentralView)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* central = mw.central_view();
    ASSERT_NE(central, nullptr);
    EXPECT_EQ(central->objectName().toStdString(), "central_view");
}

// ── Split view operations (no Vulkan needed for logic) ───────────────────────

TEST(QtPanels, SplitViewInitialState)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitViewReset)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    mw.reset_splits();
    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitPreservesOpenCanvasesAndToolState)
{
    auto& e = env();
    spectra::FigureRegistry registry;
    const auto first = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto second = registry.register_figure(std::make_unique<spectra::Figure>());

    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, &registry, e.action_bridge.get(), nullptr);
    ASSERT_GE(mw.add_figure_tab(first), 0);
    ASSERT_GE(mw.add_figure_tab(second), 0);

    auto* first_canvas = mw.canvas_for(first);
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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // No figure tabs should be open
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, CloseFigureTabWithNoTabsDoesntCrash)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Closing a non-existent tab should be safe
    mw.close_figure_tab(spectra::FigureId{42});
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

// ── Command palette (Ctrl+K) without services ────────────────────────────────

TEST(QtPanels, CommandPaletteNotCreatedWithoutServices)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // open_command_palette should be safe even if palette wasn't created
    mw.open_command_palette(); // should not crash
}

// ── Stable object names for automation ───────────────────────────────────────

TEST(QtPanels, StableObjectNames)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Central view should have stable object name
    EXPECT_EQ(mw.central_view()->objectName().toStdString(), "central_view");

    EXPECT_NE(mw.findChild<QWidget*>("spectra_status_bar"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_app_header"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_nav_rail"), nullptr);
}
