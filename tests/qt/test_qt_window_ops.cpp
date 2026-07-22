// test_qt_window_ops.cpp — Qt integration tests for window operations.
//
// Verifies:
//   - MainWindowRegistry tracking with no runtime
//   - Close order safety (close_detached_window on invalid ID)
//   - Focus switching concepts (find_host_for_figure)
//   - Shortcut scope (QShortcut on QMainWindow)
//   - Tab create/close/move logic
//   - Detach/attach safety with no runtime
//   - Platform-surface destroy/recreate concepts (SpectraVulkanWindow lifecycle)

#include <gtest/gtest.h>

#include <QApplication>
#include <QMainWindow>
#include <QShortcut>
#include <QKeySequence>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QLabel>

#include "adapters/qt/docking/docking_host.hpp"
#include "adapters/qt/docking/main_window_registry.hpp"
#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/spectra_vulkan_window.hpp"
#include "adapters/qt/components/spectra_app_header.hpp"
#include "adapters/qt/components/spectra_document_tab_bar.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"
#include "adapters/qt/components/spectra_status_bar.hpp"

#include "ui/commands/command_registry.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <memory>
#include <atomic>

namespace {

struct QtWindowOpsEnv
{
    std::unique_ptr<spectra::FigureRegistry> registry;
    std::unique_ptr<spectra::CommandRegistry> cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtWindowOpsEnv()
    {
        registry = std::make_unique<spectra::FigureRegistry>();
        cmd_registry = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }

};

QtWindowOpsEnv& env()
{
    static QtWindowOpsEnv e;
    return e;
}

} // namespace

// ── MainWindowRegistry with no runtime ───────────────────────────────────────

TEST(QtWindowOps, RegistryCreateDetachedWithoutRuntime)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto id = registry.create_detached_window();
    EXPECT_EQ(id, spectra::adapters::qt::INVALID_HOST_ID);
    EXPECT_EQ(registry.window_count(), 0u);
}

TEST(QtWindowOps, RegistryCloseInvalidHost)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Closing a non-existent host should return false, not crash
    EXPECT_FALSE(registry.close_detached_window(spectra::adapters::qt::HostId{999}));
}

TEST(QtWindowOps, RegistryHostLookupInvalid)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_EQ(registry.host(spectra::adapters::qt::HostId{42}), nullptr);
    EXPECT_EQ(registry.native_host(spectra::adapters::qt::HostId{42}), nullptr);
}

TEST(QtWindowOps, RegistryFindHostForFigure)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // No hosts registered, so any figure lookup should return invalid
    EXPECT_EQ(registry.find_host_for_figure(spectra::FigureId{1}),
              spectra::adapters::qt::INVALID_HOST_ID);
}

TEST(QtWindowOps, RegistryAllHostsEmpty)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_TRUE(registry.all_hosts().empty());
}

TEST(QtWindowOps, RegistryCloseAllDetached)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Should be safe with no windows
    registry.close_all_detached();
    EXPECT_EQ(registry.window_count(), 0u);
}

// ── Shortcut scopes ──────────────────────────────────────────────────────────

TEST(QtWindowOps, ShortcutScopeOnMainWindow)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // The command palette shortcut (Ctrl+K) is created in build_command_palette()
    // which returns early when services_ is null. Verify no Ctrl+K shortcut exists.
    auto shortcuts = mw.findChildren<QShortcut*>();
    bool has_ctrl_k = false;
    for (auto* s : shortcuts)
    {
        if (s->key() == QKeySequence("Ctrl+K"))
            has_ctrl_k = true;
    }
    EXPECT_FALSE(has_ctrl_k); // No shortcut without services
}

TEST(QtWindowOps, ShortcutScopeChildWindow)
{
    auto& e = env();

    // Shortcuts on a QMainWindow should be scoped to that window
    QMainWindow mw1;
    QMainWindow mw2;

    auto* s1 = new QShortcut(QKeySequence("Ctrl+1"), &mw1);
    s1->setObjectName("shortcut1");
    auto* s2 = new QShortcut(QKeySequence("Ctrl+2"), &mw2);
    s2->setObjectName("shortcut2");

    // Each window should find its own shortcut
    EXPECT_NE(mw1.findChild<QShortcut*>("shortcut1"), nullptr);
    EXPECT_EQ(mw1.findChild<QShortcut*>("shortcut2"), nullptr);
    EXPECT_NE(mw2.findChild<QShortcut*>("shortcut2"), nullptr);
    EXPECT_EQ(mw2.findChild<QShortcut*>("shortcut1"), nullptr);
}

// ── Tab create/close/move (no runtime — logic only) ──────────────────────────

TEST(QtWindowOps, AddFigureTabNoRuntimeSkipped)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Adding a figure tab without runtime requires Vulkan surface creation
    // which is not available in offscreen mode. Skip this test.
    GTEST_SKIP() << "add_figure_tab requires Vulkan runtime; tested via integration";
}

TEST(QtWindowOps, CloseFigureTabSafe)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Closing a non-existent tab should be safe
    mw.close_figure_tab(spectra::FigureId{99});
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtWindowOps, CanvasForNonExistentFigure)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_EQ(mw.canvas_for(spectra::FigureId{42}), nullptr);
}

// ── Detach/attach safety ─────────────────────────────────────────────────────

TEST(QtWindowOps, DetachWithoutRuntime)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Detaching without runtime should return invalid host ID
    // This tests the safety of the detach path when no runtime is available
    EXPECT_EQ(registry.create_detached_window(),
              spectra::adapters::qt::INVALID_HOST_ID);
}

// ── Close order: closing main window shouldn't crash registry ────────────────

TEST(QtWindowOps, CloseOrderSafety)
{
    auto& e = env();

    // Create a registry, then destroy it — should not crash
    {
        spectra::adapters::qt::MainWindowRegistry registry(
            nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
        // Registry goes out of scope here
    }
    // If we reach here, the destructor was safe
    SUCCEED();
}

// ── Focus switching: active_figure_id ────────────────────────────────────────

TEST(QtWindowOps, ActiveFigureIdNoneInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_EQ(mw.active_figure_id(), spectra::INVALID_FIGURE_ID);
}

TEST(QtWindowOps, OpenFigureIdsEmptyInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtWindowOps, DocumentAddControlExecutesNewFigureCommand)
{
    spectra::CommandRegistry commands;
    bool                     executed = false;
    commands.register_command("figure.new", "New Figure",
                              [&executed]() { executed = true; },
                              "Ctrl+T", "Figure");

    spectra::adapters::qt::QtActionBridge bridge(commands);
    bridge.rebuild();
    spectra::FigureRegistry registry;
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, &registry, &bridge, nullptr);

    auto* tab_bar = mw.findChild<spectra::adapters::qt::SpectraDocumentTabBar*>(
        "spectra_doc_tab_bar");
    ASSERT_NE(tab_bar, nullptr);

    ASSERT_TRUE(QMetaObject::invokeMethod(tab_bar, "tab_add_requested",
                                          Qt::DirectConnection));
    EXPECT_TRUE(executed);
}

// ── SpectraVulkanWindow lifecycle (platform-surface destroy/recreate) ────────

TEST(QtWindowOps, VulkanWindowCreation)
{
    auto& e = env();

    // Create a SpectraVulkanWindow — should not crash even without Vulkan
    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);
    window.setObjectName("test_vulkan_window");

    // No runtime set — should not be attached
    EXPECT_FALSE(window.isAttached());
}

TEST(QtWindowOps, VulkanWindowSurfaceGeneration)
{
    auto& e = env();

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);

    // Initial surface generation should be 0 (invalid)
    EXPECT_EQ(window.surface_generation(), 0u);
    EXPECT_FALSE(window.surface_valid());
}

TEST(QtWindowOps, VulkanWindowRequestFrame)
{
    auto& e = env();

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);

    // Requesting a frame without a runtime should be safe
    window.requestFrame();
}

TEST(QtWindowOps, VulkanWindowForceDetach)
{
    auto& e = env();

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);

    // Force detach without being attached should be safe
    window.forceDetach();
    EXPECT_FALSE(window.isAttached());
}

TEST(QtWindowOps, VulkanWindowAnimationTimer)
{
    auto& e = env();

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);

    // Animation timer should not be running initially
    EXPECT_FALSE(window.hasAnimationTimer());

    // Start/stop should be safe without a runtime
    window.startAnimationTimer();
    EXPECT_TRUE(window.hasAnimationTimer());
    window.stopAnimationTimer();
    EXPECT_FALSE(window.hasAnimationTimer());
}

// ── Menu structure verification ──────────────────────────────────────────────

TEST(QtWindowOps, MenuBarHasStandardMenus)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>(
        "spectra_app_header");
    ASSERT_NE(header, nullptr);

    QStringList expected_menus = {
        "File", "Edit", "View", "Tools", "Plot", "Data", "Axes", "Transforms"};
    QStringList actual_menus;
    for (auto* button : header->findChildren<spectra::adapters::qt::SpectraMenuButton*>())
        actual_menus << button->text();

    for (const auto& expected : expected_menus)
        EXPECT_TRUE(actual_menus.contains(expected))
            << "Missing menu: " << expected.toStdString();
}

TEST(QtWindowOps, ViewMenuHasSplitActions)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    QMenu* view_menu = mw.findChild<QMenu*>("menu_view");
    ASSERT_NE(view_menu, nullptr);

    // Split actions are only created when services_ is not null (inside build_panels)
    // Without services, these actions won't exist — verify the menu exists but
    // split actions are absent.
    EXPECT_EQ(view_menu->findChild<QAction*>("view_split_right"), nullptr);
    EXPECT_EQ(view_menu->findChild<QAction*>("view_split_down"), nullptr);
}

// ── Toolbar structure ────────────────────────────────────────────────────────

TEST(QtWindowOps, CustomHeaderReplacesToolbar)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_EQ(mw.findChild<QToolBar*>("main_toolbar"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_app_header"), nullptr);
}

// ── Multiple SpectraMainWindow instances ─────────────────────────────────────

TEST(QtWindowOps, MultipleMainWindowInstances)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw1(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    spectra::adapters::qt::SpectraMainWindow mw2(nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    // Both should have independent central views
    EXPECT_NE(mw1.central_view(), mw2.central_view());

    // Both should have independent custom status bars
    mw1.set_status("Window 1");
    mw2.set_status("Window 2");
    auto* status1 = mw1.findChild<spectra::adapters::qt::SpectraStatusBar*>(
        "spectra_status_bar");
    auto* status2 = mw2.findChild<spectra::adapters::qt::SpectraStatusBar*>(
        "spectra_status_bar");
    ASSERT_NE(status1, nullptr);
    ASSERT_NE(status2, nullptr);
    EXPECT_EQ(status1->message().toStdString(), "Window 1");
    EXPECT_EQ(status2->message().toStdString(), "Window 2");
}

// ── Command registry integration with actions ────────────────────────────────

TEST(QtWindowOps, CommandExecutionViaAction)
{
    spectra::CommandRegistry reg;
    std::atomic<bool> executed{false};
    reg.register_command("test.window_cmd", "Test Cmd", [&] { executed = true; }, "Ctrl+W", "Window");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.window_cmd");
    ASSERT_NE(action, nullptr);
    action->trigger();
    EXPECT_TRUE(executed);
}
