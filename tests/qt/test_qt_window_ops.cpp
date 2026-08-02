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
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMainWindow>
#include <QShortcut>
#include <QKeySequence>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>
#include <QTabBar>
#include <QTabWidget>

#include "adapters/qt/docking/docking_host.hpp"
#include "adapters/qt/docking/main_window_registry.hpp"
#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/figure_canvas_widget.hpp"
#include "adapters/qt/split_view_container.hpp"
#include "adapters/qt/spectra_vulkan_window.hpp"
#include "adapters/qt/components/spectra_app_header.hpp"
#include "adapters/qt/components/spectra_document_tab_bar.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"
#include "adapters/qt/components/spectra_status_bar.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <atomic>
#include <array>
#include <memory>

namespace
{

struct QtWindowOpsEnv
{
    std::unique_ptr<spectra::FigureRegistry>               registry;
    std::unique_ptr<spectra::CommandRegistry>              cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtWindowOpsEnv()
    {
        registry      = std::make_unique<spectra::FigureRegistry>();
        cmd_registry  = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }
};

QtWindowOpsEnv& env()
{
    static QtWindowOpsEnv e;
    return e;
}

}   // namespace

// ── MainWindowRegistry with no runtime ───────────────────────────────────────

TEST(QtWindowOps, RegistryCreateDetachedWithoutRuntime)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    auto id = registry.create_detached_window();
    EXPECT_EQ(id, spectra::adapters::qt::INVALID_HOST_ID);
    EXPECT_EQ(registry.window_count(), 0u);
}

TEST(QtWindowOps, RegistryCloseInvalidHost)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    // Closing a non-existent host should return false, not crash
    EXPECT_FALSE(registry.close_detached_window(spectra::adapters::qt::HostId{999}));
}

TEST(QtWindowOps, RegistryHostLookupInvalid)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    EXPECT_EQ(registry.host(spectra::adapters::qt::HostId{42}), nullptr);
    EXPECT_EQ(registry.native_host(spectra::adapters::qt::HostId{42}), nullptr);
}

TEST(QtWindowOps, RegistryFindHostForFigure)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    // No hosts registered, so any figure lookup should return invalid
    EXPECT_EQ(registry.find_host_for_figure(spectra::FigureId{1}),
              spectra::adapters::qt::INVALID_HOST_ID);
}

TEST(QtWindowOps, RegistryAllHostsEmpty)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    EXPECT_TRUE(registry.all_hosts().empty());
}

TEST(QtWindowOps, RegistryCloseAllDetached)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    // Should be safe with no windows
    registry.close_all_detached();
    EXPECT_EQ(registry.window_count(), 0u);
}

TEST(QtWindowOps, RegistryReportsCanvasAndToolPersistenceMutations)
{
    spectra::FigureRegistry models;
    auto                    figure = std::make_unique<spectra::Figure>();
    figure->subplot(1, 1, 1);
    const auto                                figure_id = models.register_figure(std::move(figure));
    spectra::CommandRegistry                  commands;
    spectra::adapters::qt::QtActionBridge     actions(commands);
    spectra::adapters::qt::SpectraMainWindow  window(nullptr, &models, &actions, nullptr);
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &models, &actions, nullptr);
    ASSERT_NE(windows.register_window(&window), spectra::adapters::qt::INVALID_HOST_ID);
    ASSERT_GE(window.add_figure_tab(figure_id), 0);

    int dirty_count = 0;
    windows.set_document_changed_callback([&dirty_count]() { ++dirty_count; });
    window.set_active_tool(spectra::ToolMode::Measure);
    EXPECT_EQ(dirty_count, 1);
    ASSERT_TRUE(window.toggle_active_crosshair());
    EXPECT_EQ(dirty_count, 2);
    window.set_nav_rail_visible(false);
    EXPECT_EQ(dirty_count, 3);
}

TEST(QtWindowOps, RedockMovesDocumentBackToPrimaryHost)
{
    spectra::FigureRegistry models;
    auto                    figure = std::make_unique<spectra::Figure>();
    figure->subplot(1, 1, 1);
    const auto                                figure_id = models.register_figure(std::move(figure));
    spectra::CommandRegistry                  commands;
    spectra::adapters::qt::QtActionBridge     actions(commands);
    spectra::adapters::qt::SpectraMainWindow  primary(nullptr, &models, &actions, nullptr);
    spectra::adapters::qt::SpectraMainWindow  detached(nullptr, &models, &actions, nullptr);
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &models, &actions, nullptr);
    const auto                                primary_id  = windows.register_window(&primary);
    const auto                                detached_id = windows.register_window(&detached);
    ASSERT_NE(primary_id, spectra::adapters::qt::INVALID_HOST_ID);
    ASSERT_NE(detached_id, spectra::adapters::qt::INVALID_HOST_ID);
    ASSERT_GE(detached.add_figure_tab(figure_id), 0);

    ASSERT_TRUE(windows.redock_document(figure_id, detached_id));
    EXPECT_NE(primary.canvas_for(figure_id), nullptr);
    EXPECT_EQ(detached.canvas_for(figure_id), nullptr);
    EXPECT_EQ(windows.find_host_for_figure(figure_id), primary_id);
}

// ── Shortcut scopes ──────────────────────────────────────────────────────────

TEST(QtWindowOps, ShortcutScopeOnMainWindow)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // The command palette shortcut (Ctrl+K) is created in build_command_palette()
    // which returns early when services_ is null. Verify no Ctrl+K shortcut exists.
    auto shortcuts  = mw.findChildren<QShortcut*>();
    bool has_ctrl_k = false;
    for (auto* s : shortcuts)
    {
        if (s->key() == QKeySequence("Ctrl+K"))
            has_ctrl_k = true;
    }
    EXPECT_FALSE(has_ctrl_k);   // No shortcut without services
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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Adding a figure tab without runtime requires Vulkan surface creation
    // which is not available in offscreen mode. Skip this test.
    GTEST_SKIP() << "add_figure_tab requires Vulkan runtime; tested via integration";
}

TEST(QtWindowOps, CloseFigureTabSafe)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Closing a non-existent tab should be safe
    mw.close_figure_tab(spectra::FigureId{99});
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtWindowOps, CanvasForNonExistentFigure)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_EQ(mw.canvas_for(spectra::FigureId{42}), nullptr);
}

// ── Detach/attach safety ─────────────────────────────────────────────────────

TEST(QtWindowOps, DetachWithoutRuntime)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);

    // Detaching without runtime should return invalid host ID
    // This tests the safety of the detach path when no runtime is available
    EXPECT_EQ(registry.create_detached_window(), spectra::adapters::qt::INVALID_HOST_ID);
}

// ── Close order: closing main window shouldn't crash registry ────────────────

TEST(QtWindowOps, CloseOrderSafety)
{
    auto& e = env();

    // Create a registry, then destroy it — should not crash
    {
        spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                           e.registry.get(),
                                                           e.action_bridge.get(),
                                                           nullptr);
        // Registry goes out of scope here
    }
    // If we reach here, the destructor was safe
    SUCCEED();
}

// ── Focus switching: active_figure_id ────────────────────────────────────────

TEST(QtWindowOps, ActiveFigureIdNoneInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_EQ(mw.active_figure_id(), spectra::INVALID_FIGURE_ID);
}

TEST(QtWindowOps, OpenFigureIdsEmptyInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtWindowOps, ClosingTabAlsoClosesRegisteredFigure)
{
    spectra::FigureRegistry               registry;
    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &action_bridge, nullptr);
    spectra::adapters::qt::SpectraMainWindow  window(nullptr, &registry, &action_bridge, nullptr);
    windows.register_window(&window);
    int dirty_notifications = 0;
    windows.set_document_changed_callback([&dirty_notifications]() { ++dirty_notifications; });

    const auto fid = registry.register_figure(std::make_unique<spectra::Figure>());
    ASSERT_GE(window.add_figure_tab(fid), 0);
    ASSERT_NE(registry.get(fid), nullptr);
    int close_notifications = 0;
    QObject::connect(&window,
                     &spectra::adapters::qt::SpectraMainWindow::figure_closed,
                     &window,
                     [&close_notifications](spectra::FigureId) { ++close_notifications; });

    EXPECT_TRUE(window.close_figure_tab(fid));
    EXPECT_EQ(close_notifications, 1);
    EXPECT_EQ(dirty_notifications, 1);
    EXPECT_EQ(registry.get(fid), nullptr);
    EXPECT_EQ(window.canvas_for(fid), nullptr);
}

TEST(QtWindowOps, InvalidMovePreservesSourceDocument)
{
    spectra::FigureRegistry               registry;
    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &action_bridge, nullptr);
    spectra::adapters::qt::SpectraMainWindow  source(nullptr, &registry, &action_bridge, nullptr);
    const auto                                source_host = windows.register_window(&source);
    int                                       dirty_notifications = 0;
    windows.set_document_changed_callback([&dirty_notifications]() { ++dirty_notifications; });

    const auto fid = registry.register_figure(std::make_unique<spectra::Figure>());
    ASSERT_GE(source.add_figure_tab(fid), 0);

    EXPECT_FALSE(windows.move_document(fid, source_host, spectra::adapters::qt::HostId{999}));
    EXPECT_NE(source.canvas_for(fid), nullptr);
    EXPECT_NE(registry.get(fid), nullptr);
    EXPECT_EQ(windows.find_host_for_figure(fid), source_host);
    EXPECT_EQ(dirty_notifications, 0);
}

TEST(QtWindowOps, FailedDetachPreservesSourceDocument)
{
    spectra::FigureRegistry               registry;
    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &action_bridge, nullptr);
    spectra::adapters::qt::SpectraMainWindow  source(nullptr, &registry, &action_bridge, nullptr);
    const auto                                source_host = windows.register_window(&source);
    int                                       dirty_notifications = 0;
    windows.set_document_changed_callback([&dirty_notifications]() { ++dirty_notifications; });

    const auto fid = registry.register_figure(std::make_unique<spectra::Figure>());
    ASSERT_GE(source.add_figure_tab(fid), 0);

    EXPECT_EQ(windows.detach_document(fid, source_host), spectra::adapters::qt::INVALID_HOST_ID);
    EXPECT_NE(source.canvas_for(fid), nullptr);
    EXPECT_NE(registry.get(fid), nullptr);
    EXPECT_EQ(windows.find_host_for_figure(fid), source_host);
    EXPECT_EQ(dirty_notifications, 0);
}

TEST(QtWindowOps, MoveTransfersDocumentWithoutClosingModel)
{
    spectra::FigureRegistry               registry;
    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &action_bridge, nullptr);
    spectra::adapters::qt::SpectraMainWindow  source(nullptr, &registry, &action_bridge, nullptr);
    spectra::adapters::qt::SpectraMainWindow  target(nullptr, &registry, &action_bridge, nullptr);
    const auto                                source_host = windows.register_window(&source);
    const auto                                target_host = windows.register_window(&target);
    int                                       dirty_notifications = 0;
    windows.set_document_changed_callback([&dirty_notifications]() { ++dirty_notifications; });

    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line = axes.plot(x, y).label("moved");
    const auto                 fid  = registry.register_figure(std::move(figure));
    ASSERT_GE(source.add_figure_tab(fid), 0);

    auto*                    source_canvas = source.canvas_for(fid)->vulkanWindow();
    spectra::OverlaySnapshot overlay;
    overlay.markers.push_back({1.0f, 3.0f, "moved", 1, 0});
    source_canvas->restoreOverlaySnapshot(overlay);
    source.central_view()->set_active_tool(spectra::ToolMode::ROI);
    source_canvas->selectSeries(registry.get(fid), &axes, 0, &line, 0);

    ASSERT_TRUE(windows.move_document(fid, source_host, target_host));
    EXPECT_EQ(source.canvas_for(fid), nullptr);
    EXPECT_NE(target.canvas_for(fid), nullptr);
    EXPECT_NE(registry.get(fid), nullptr);
    EXPECT_EQ(windows.find_host_for_figure(fid), target_host);
    EXPECT_EQ(dirty_notifications, 1);

    auto*                    target_canvas = target.canvas_for(fid)->vulkanWindow();
    spectra::OverlaySnapshot restored;
    ASSERT_TRUE(target_canvas->captureOverlaySnapshot(restored));
    ASSERT_EQ(restored.markers.size(), 1u);
    EXPECT_EQ(restored.markers[0].series_label, "moved");
    EXPECT_EQ(target.central_view()->input_handler_for(fid)->tool_mode(), spectra::ToolMode::ROI);
    ASSERT_EQ(target_canvas->selectedSeries().size(), 1u);
    EXPECT_EQ(target_canvas->selectedSeries()[0].series, &line);
}

TEST(QtWindowOps, CrossWindowTabDropUsesTransactionalRegistryMove)
{
    spectra::FigureRegistry               registry;
    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge actions(commands);
    actions.rebuild();
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &actions, nullptr);
    spectra::adapters::qt::SpectraMainWindow  source(nullptr, &registry, &actions, nullptr);
    spectra::adapters::qt::SpectraMainWindow  target(nullptr, &registry, &actions, nullptr);
    const auto                                source_host = windows.register_window(&source);
    const auto                                target_host = windows.register_window(&target);
    ASSERT_NE(source_host, target_host);

    const auto moving   = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto resident = registry.register_figure(std::make_unique<spectra::Figure>());
    ASSERT_GE(source.add_figure_tab(moving), 0);
    ASSERT_GE(target.add_figure_tab(resident), 0);

    auto panes = target.central_view()->findChildren<QTabWidget*>();
    ASSERT_EQ(panes.size(), 1);
    QTabBar* target_bar = panes.front()->tabBar();
    ASSERT_NE(target_bar, nullptr);
    ASSERT_TRUE(target_bar->acceptDrops());

    int dirty_notifications = 0;
    windows.set_document_changed_callback([&dirty_notifications]() { ++dirty_notifications; });
    QMimeData mime;
    mime.setData("application/x-spectra-figure-id", QByteArray::number(moving));
    QDragEnterEvent enter(QPoint(5, 5), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target_bar, &enter);
    ASSERT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(5, 5), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target_bar, &drop);
    ASSERT_TRUE(drop.isAccepted());
    QCoreApplication::processEvents();

    EXPECT_EQ(source.canvas_for(moving), nullptr);
    EXPECT_NE(target.canvas_for(moving), nullptr);
    EXPECT_NE(target.canvas_for(resident), nullptr);
    EXPECT_EQ(windows.find_host_for_figure(moving), target_host);
    EXPECT_NE(registry.get(moving), nullptr);
    EXPECT_EQ(dirty_notifications, 1);
}

TEST(QtWindowOps, DocumentAddControlExecutesNewFigureCommand)
{
    spectra::CommandRegistry commands;
    bool                     executed = false;
    commands.register_command(
        "figure.new",
        "New Figure",
        [&executed]() { executed = true; },
        "Ctrl+T",
        "Figure");

    spectra::adapters::qt::QtActionBridge bridge(commands);
    bridge.rebuild();
    spectra::FigureRegistry                  registry;
    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, &bridge, nullptr);

    auto* tab_bar =
        mw.findChild<spectra::adapters::qt::SpectraDocumentTabBar*>("spectra_doc_tab_bar");
    ASSERT_NE(tab_bar, nullptr);

    ASSERT_TRUE(QMetaObject::invokeMethod(tab_bar, "tab_add_requested", Qt::DirectConnection));
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

TEST(QtWindowOps, VulkanWindowCursorSignalUsesDataCoordinatesAndValidity)
{
    spectra::Figure figure;
    auto&           axes = figure.subplot(1, 1, 1);
    axes.xlim(0.0, 10.0);
    axes.ylim(0.0, 20.0);
    axes.set_viewport({0.0f, 0.0f, 100.0f, 100.0f});

    spectra::InputHandler input;
    input.set_figure(&figure);
    input.set_active_axes(&axes);

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);
    window.setFigure(&figure);
    window.setInputHandler(&input);

    double data_x = 0.0;
    double data_y = 0.0;
    bool   valid  = false;
    QObject::connect(&window,
                     &spectra::adapters::qt::SpectraVulkanWindow::cursorMoved,
                     [&](double x, double y, bool is_valid)
                     {
                         data_x = x;
                         data_y = y;
                         valid  = is_valid;
                     });

    const qreal dpr = window.devicePixelRatio();
    QMouseEvent inside(QEvent::MouseMove,
                       QPointF(25.0 / dpr, 75.0 / dpr),
                       QPointF(25.0 / dpr, 75.0 / dpr),
                       QPointF(25.0 / dpr, 75.0 / dpr),
                       Qt::NoButton,
                       Qt::NoButton,
                       Qt::NoModifier);
    QApplication::sendEvent(&window, &inside);
    EXPECT_TRUE(valid);
    EXPECT_DOUBLE_EQ(data_x, 2.5);
    EXPECT_DOUBLE_EQ(data_y, 5.0);

    QMouseEvent outside(QEvent::MouseMove,
                        QPointF(150.0 / dpr, 150.0 / dpr),
                        QPointF(150.0 / dpr, 150.0 / dpr),
                        QPointF(150.0 / dpr, 150.0 / dpr),
                        Qt::NoButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&window, &outside);
    EXPECT_FALSE(valid);
}

TEST(QtWindowOps, VulkanWindowForceDetach)
{
    auto& e = env();

    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);

    // Force detach without being attached should be safe
    window.forceDetach();
    EXPECT_FALSE(window.isAttached());
}

TEST(QtWindowOps, VulkanWindowQueuesOverlayStateUntilNativeSurfaceAttaches)
{
    spectra::Figure figure;
    figure.subplot(1, 1, 1);
    spectra::adapters::qt::SpectraVulkanWindow window(nullptr);
    window.setFigure(&figure);

    spectra::OverlaySnapshot expected;
    expected.crosshair_enabled = true;
    expected.tooltip_enabled   = false;
    expected.annotations.push_back(
        {1.0f, 2.0f, "queued", {1.0f, 0.0f, 0.0f, 1.0f}, 3.0f, -9.0f, 0});
    expected.markers.push_back({3.0f, 4.0f, "queued marker", 2, 0});
    window.restoreOverlaySnapshot(expected);

    spectra::OverlaySnapshot actual;
    ASSERT_TRUE(window.captureOverlaySnapshot(actual));
    EXPECT_TRUE(window.crosshairEnabled());
    EXPECT_TRUE(actual.crosshair_enabled);
    EXPECT_FALSE(actual.tooltip_enabled);
    ASSERT_EQ(actual.annotations.size(), 1u);
    EXPECT_EQ(actual.annotations[0].text, "queued");
    EXPECT_EQ(window.markerCount(), 1u);

    int persistent_changes = 0;
    QObject::connect(&window,
                     &spectra::adapters::qt::SpectraVulkanWindow::persistentStateChanged,
                     [&persistent_changes]() { ++persistent_changes; });
    EXPECT_TRUE(window.clearMarkers());
    EXPECT_EQ(window.markerCount(), 0u);
    EXPECT_EQ(persistent_changes, 1);
    ASSERT_TRUE(window.captureOverlaySnapshot(actual));
    EXPECT_TRUE(actual.markers.empty());
    EXPECT_EQ(actual.annotations.size(), 1u);
    EXPECT_FALSE(window.clearMarkers());
    EXPECT_EQ(persistent_changes, 1);

    window.toggleCrosshair();
    ASSERT_TRUE(window.captureOverlaySnapshot(actual));
    EXPECT_FALSE(actual.crosshair_enabled);
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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    ASSERT_NE(header, nullptr);

    QStringList expected_menus =
        {"File", "Edit", "View", "Tools", "Plot", "Data", "Axes", "Transforms", "Help"};
    QStringList actual_menus;
    for (auto* button : header->findChildren<spectra::adapters::qt::SpectraMenuButton*>())
        actual_menus << button->text();

    for (const auto& expected : expected_menus)
        EXPECT_TRUE(actual_menus.contains(expected)) << "Missing menu: " << expected.toStdString();
    EXPECT_NE(mw.findChild<QAction*>("axes_link_x"), nullptr);
    EXPECT_NE(mw.findChild<QAction*>("axes_unlink_all"), nullptr);
    EXPECT_NE(mw.findChild<QAction*>("transform_square"), nullptr);
    EXPECT_NE(mw.findChild<QAction*>("transform_custom_formula"), nullptr);
}

TEST(QtWindowOps, RegisteredPanelAndSplitActionsAppearExactlyOnce)
{
    spectra::CommandRegistry commands;
    int                      panel_calls = 0;
    int                      split_calls = 0;
    commands.register_command(
        "panel.toggle_inspector",
        "Toggle Inspector",
        [&panel_calls]() { ++panel_calls; },
        "I",
        "Panel");
    commands.register_command(
        "view.split_right",
        "Split Right",
        [&split_calls]() { ++split_calls; },
        "Ctrl+\\",
        "View");
    spectra::adapters::qt::QtActionBridge bridge(commands);
    bridge.rebuild();
    spectra::FigureRegistry                  registry;
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &bridge, nullptr);

    auto* panels = window.findChild<QMenu*>("menu_view_panels");
    auto* splits = window.findChild<QMenu*>("menu_view_splits");
    ASSERT_NE(panels, nullptr);
    ASSERT_NE(splits, nullptr);
    QAction* panel_action = bridge.action_for("panel.toggle_inspector");
    QAction* split_action = bridge.action_for("view.split_right");
    ASSERT_NE(panel_action, nullptr);
    ASSERT_NE(split_action, nullptr);
    EXPECT_EQ(panels->actions().count(panel_action), 1);
    EXPECT_EQ(splits->actions().count(split_action), 1);
    EXPECT_EQ(panels->actions().size(), 1);
    EXPECT_EQ(splits->actions().size(), 1);

    panel_action->trigger();
    split_action->trigger();
    EXPECT_EQ(panel_calls, 1);
    EXPECT_EQ(split_calls, 1);
}

TEST(QtWindowOps, ViewMenuHasSplitActions)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

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

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_EQ(mw.findChild<QToolBar*>("main_toolbar"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_app_header"), nullptr);
}

// ── Multiple SpectraMainWindow instances ─────────────────────────────────────

TEST(QtWindowOps, MultipleMainWindowInstances)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw1(nullptr,
                                                 e.registry.get(),
                                                 e.action_bridge.get(),
                                                 nullptr);
    spectra::adapters::qt::SpectraMainWindow mw2(nullptr,
                                                 e.registry.get(),
                                                 e.action_bridge.get(),
                                                 nullptr);

    // Both should have independent central views
    EXPECT_NE(mw1.central_view(), mw2.central_view());

    // Both should have independent custom status bars
    mw1.set_status("Window 1");
    mw2.set_status("Window 2");
    auto* status1 = mw1.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    auto* status2 = mw2.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    ASSERT_NE(status1, nullptr);
    ASSERT_NE(status2, nullptr);
    EXPECT_EQ(status1->message().toStdString(), "Window 1");
    EXPECT_EQ(status2->message().toStdString(), "Window 2");
}

// ── Command registry integration with actions ────────────────────────────────

TEST(QtWindowOps, CommandExecutionViaAction)
{
    spectra::CommandRegistry reg;
    std::atomic<bool>        executed{false};
    reg.register_command(
        "test.window_cmd",
        "Test Cmd",
        [&] { executed = true; },
        "Ctrl+W",
        "Window");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.window_cmd");
    ASSERT_NE(action, nullptr);
    action->trigger();
    EXPECT_TRUE(executed);
}
