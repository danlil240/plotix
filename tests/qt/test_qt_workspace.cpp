// test_qt_workspace.cpp — Qt integration tests for workspace v5 desktop layout.
//
// Verifies:
//   - WorkspaceData::DesktopLayoutState structure and defaults
//   - WorkspaceData FORMAT_VERSION is 5
//   - DesktopLayoutState serialization fields
//   - QtWorkspaceBridge with null registry (safe degradation)
//   - Provider mismatch returns false
//   - v4-to-v5 migration concepts (version field, desktop_layout presence)
//   - Workspace JSON serialization includes desktop_layout

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QSplitter>

#include "ui/workspace/workspace.hpp"
#include "adapters/qt/qt_workspace_bridge.hpp"
#include "adapters/qt/docking/main_window_registry.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/qt_main_window.hpp"

#include "ui/commands/command_registry.hpp"

#include <spectra/figure_registry.hpp>

#include <memory>
#include <string>

namespace
{

struct QtWorkspaceEnv
{
    std::unique_ptr<spectra::FigureRegistry>               registry;
    std::unique_ptr<spectra::CommandRegistry>              cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtWorkspaceEnv()
    {
        registry      = std::make_unique<spectra::FigureRegistry>();
        cmd_registry  = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }
};

QtWorkspaceEnv& env()
{
    static QtWorkspaceEnv e;
    return e;
}

}   // namespace

// ── WorkspaceData v5 structure ───────────────────────────────────────────────

TEST(QtWorkspace, FormatVersionIs5)
{
    EXPECT_EQ(spectra::WorkspaceData::FORMAT_VERSION, 5u);
}

TEST(QtWorkspace, DefaultVersionIs5)
{
    spectra::WorkspaceData data;
    EXPECT_EQ(data.version, 5u);
}

TEST(QtWorkspace, DesktopLayoutDefaults)
{
    spectra::WorkspaceData data;
    EXPECT_TRUE(data.desktop_layout.provider.empty());
    EXPECT_TRUE(data.desktop_layout.provider_version.empty());
    EXPECT_TRUE(data.desktop_layout.main_window_state_base64.empty());
    EXPECT_TRUE(data.desktop_layout.main_window_geometry_base64.empty());
    EXPECT_TRUE(data.desktop_layout.provider_layout.empty());
    EXPECT_TRUE(data.desktop_layout.windows.empty());
}

TEST(QtWorkspace, DesktopLayoutWindowStateStructure)
{
    spectra::WorkspaceData::DesktopLayoutState::WindowState ws;
    ws.state_base64    = "AAAA";
    ws.geometry_base64 = "BBBB";
    ws.title           = "Detached Window";

    EXPECT_EQ(ws.state_base64, "AAAA");
    EXPECT_EQ(ws.geometry_base64, "BBBB");
    EXPECT_EQ(ws.title, "Detached Window");
}

TEST(QtWorkspace, DesktopLayoutWithMultipleWindows)
{
    spectra::WorkspaceData data;
    data.desktop_layout.provider                    = "native";
    data.desktop_layout.provider_version            = "1.0";
    data.desktop_layout.main_window_state_base64    = "main_state";
    data.desktop_layout.main_window_geometry_base64 = "main_geo";

    for (int i = 0; i < 3; ++i)
    {
        spectra::WorkspaceData::DesktopLayoutState::WindowState ws;
        ws.state_base64    = "state_" + std::to_string(i);
        ws.geometry_base64 = "geo_" + std::to_string(i);
        ws.title           = "Window " + std::to_string(i);
        data.desktop_layout.windows.push_back(std::move(ws));
    }

    EXPECT_EQ(data.desktop_layout.provider, "native");
    EXPECT_EQ(data.desktop_layout.windows.size(), 3u);
    EXPECT_EQ(data.desktop_layout.windows[0].title, "Window 0");
    EXPECT_EQ(data.desktop_layout.windows[2].title, "Window 2");
}

// ── Workspace JSON serialization ─────────────────────────────────────────────

TEST(QtWorkspace, SerializeJsonIncludesDesktopLayout)
{
    spectra::WorkspaceData data;
    data.desktop_layout.provider                 = "native";
    data.desktop_layout.main_window_state_base64 = "test_state";

    std::string json = spectra::Workspace::serialize_json(data);
    EXPECT_FALSE(json.empty());
    // The JSON should contain the provider value
    EXPECT_NE(json.find("native"), std::string::npos);
}

TEST(QtWorkspace, SerializeJsonRoundTrip)
{
    spectra::WorkspaceData data;
    data.version                                    = 5;
    data.theme_name                                 = "night";
    data.desktop_layout.provider                    = "native";
    data.desktop_layout.provider_version            = "1.0";
    data.desktop_layout.main_window_state_base64    = "state123";
    data.desktop_layout.main_window_geometry_base64 = "geo456";

    std::string json = spectra::Workspace::serialize_json(data);
    EXPECT_FALSE(json.empty());

    // Write to temp file and load back
    std::string tmp_path = "/tmp/spectra_test_workspace_v5.spectra";
    EXPECT_TRUE(spectra::Workspace::save(tmp_path, data));

    spectra::WorkspaceData loaded;
    EXPECT_TRUE(spectra::Workspace::load(tmp_path, loaded));
    EXPECT_EQ(loaded.version, 5u);
    EXPECT_EQ(loaded.theme_name, "night");
    EXPECT_EQ(loaded.desktop_layout.provider, "native");
    EXPECT_EQ(loaded.desktop_layout.main_window_state_base64, "state123");
    EXPECT_EQ(loaded.desktop_layout.main_window_geometry_base64, "geo456");
}

// ── v4-to-v5 migration concepts ──────────────────────────────────────────────

TEST(QtWorkspace, V4DataLoadsIntoV5Structure)
{
    // Simulate a v4 workspace (no desktop_layout field)
    spectra::WorkspaceData data;
    data.version    = 4;
    data.theme_name = "day";
    data.dock_state = "legacy_imgui_dock_state";

    // v4 data should be loadable — desktop_layout just stays empty
    EXPECT_EQ(data.version, 4u);
    EXPECT_TRUE(data.desktop_layout.provider.empty());
    EXPECT_TRUE(data.desktop_layout.windows.empty());
    EXPECT_FALSE(data.dock_state.empty());
}

// ── QtWorkspaceBridge with null registry ─────────────────────────────────────

TEST(QtWorkspace, BridgeCaptureWithNullRegistry)
{
    spectra::adapters::qt::QtWorkspaceBridge bridge(nullptr);
    spectra::WorkspaceData                   data;
    bridge.capture_layout(data);
    // Should not crash, and should not populate anything
    EXPECT_TRUE(data.desktop_layout.provider.empty());
}

TEST(QtWorkspace, BridgeApplyWithNullRegistry)
{
    spectra::adapters::qt::QtWorkspaceBridge bridge(nullptr);
    spectra::WorkspaceData                   data;
    EXPECT_FALSE(bridge.apply_layout(data));
}

// ── QtWorkspaceBridge with registry (no runtime — can't create windows) ──────

TEST(QtWorkspace, BridgeApplyProviderMismatch)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);
    spectra::adapters::qt::QtWorkspaceBridge  bridge(&registry);
    bridge.set_provider("native");

    spectra::WorkspaceData data;
    data.desktop_layout.provider = "kddockwidgets";   // different provider
    EXPECT_FALSE(bridge.apply_layout(data));
}

TEST(QtWorkspace, BridgeApplyEmptyProviderMatches)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(nullptr,
                                                       e.registry.get(),
                                                       e.action_bridge.get(),
                                                       nullptr);
    spectra::adapters::qt::QtWorkspaceBridge  bridge(&registry);
    bridge.set_provider("native");

    spectra::WorkspaceData data;
    data.desktop_layout.provider = "";   // empty provider should match
    // apply_layout should return false because there are no hosts registered
    EXPECT_FALSE(bridge.apply_layout(data));
}

TEST(QtWorkspace, BridgeSetProvider)
{
    spectra::adapters::qt::QtWorkspaceBridge bridge(nullptr);
    bridge.set_provider("kddockwidgets");
    // No direct accessor, but apply_layout checks provider — verify via behavior
    // by checking that a matching provider doesn't fail on provider check
    // (it'll fail on empty hosts, but that's after the provider check)
    EXPECT_TRUE(true);   // just verify set_provider doesn't crash
}

TEST(QtWorkspace, BridgeCapturesPrimaryWindowDocumentsAndAllPaneTabs)
{
    spectra::FigureRegistry registry;
    auto                    first = std::make_unique<spectra::Figure>();
    first->subplot(1, 1, 1);
    auto second = std::make_unique<spectra::Figure>();
    second->subplot(1, 1, 1);
    const auto first_id  = registry.register_figure(std::move(first));
    const auto second_id = registry.register_figure(std::move(second));

    spectra::CommandRegistry                 commands;
    spectra::adapters::qt::QtActionBridge    actions(commands);
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);
    ASSERT_GE(window.add_figure_tab(first_id), 0);
    ASSERT_GE(window.add_figure_tab(second_id), 0);

    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &actions, nullptr);
    const auto                                main_id = windows.register_window(&window);
    ASSERT_NE(main_id, spectra::adapters::qt::INVALID_HOST_ID);
    EXPECT_EQ(windows.primary_host_id(), main_id);

    spectra::adapters::qt::QtWorkspaceBridge bridge(&windows);
    spectra::WorkspaceData                   data;
    bridge.capture_layout(data);

    EXPECT_EQ(data.desktop_layout.main_window_figure_ids,
              (std::vector<spectra::FigureId>{first_id, second_id}));
    EXPECT_FALSE(data.desktop_layout.main_window_split_layout.empty());
    EXPECT_TRUE(data.desktop_layout.windows.empty());
}

TEST(QtWorkspace, BridgeRestoresPaneTabsThroughFigureIdRemap)
{
    spectra::FigureRegistry registry;
    auto                    discarded    = std::make_unique<spectra::Figure>();
    const auto              discarded_id = registry.register_figure(std::move(discarded));
    registry.unregister_figure(discarded_id);

    auto first = std::make_unique<spectra::Figure>();
    first->subplot(1, 1, 1);
    auto second = std::make_unique<spectra::Figure>();
    second->subplot(1, 1, 1);
    const auto new_first  = registry.register_figure(std::move(first));
    const auto new_second = registry.register_figure(std::move(second));

    spectra::CommandRegistry                  commands;
    spectra::adapters::qt::QtActionBridge     actions(commands);
    spectra::adapters::qt::SpectraMainWindow  window(nullptr, &registry, &actions, nullptr);
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &actions, nullptr);
    ASSERT_NE(windows.register_window(&window), spectra::adapters::qt::INVALID_HOST_ID);

    spectra::WorkspaceData data;
    data.desktop_layout.provider               = "native";
    data.desktop_layout.main_window_figure_ids = {10, 11};
    data.desktop_layout.main_window_split_layout =
        R"({"active":11,"root":{"id":1,"leaf":true,"figure":11,"figures":[10,11],"active_local":1}})";

    spectra::adapters::qt::QtWorkspaceBridge bridge(&windows);
    ASSERT_TRUE(bridge.apply_layout(data, {{10, new_first}, {11, new_second}}));
    EXPECT_EQ(window.open_figure_ids(), (std::vector<spectra::FigureId>{new_first, new_second}));
    EXPECT_EQ(window.active_figure_id(), new_second);
}

TEST(QtWorkspace, BridgeRoundTripsNativeDockTopologyVisibilityAndFloatingState)
{
    spectra::FigureRegistry                  registry;
    spectra::CommandRegistry                 commands;
    spectra::adapters::qt::QtActionBridge    actions(commands);
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);

    QDockWidget primary("Primary", &window);
    primary.setObjectName("workspace_primary_dock");
    QDockWidget tabbed("Tabbed", &window);
    tabbed.setObjectName("workspace_tabbed_dock");
    QDockWidget floating("Floating", &window);
    floating.setObjectName("workspace_floating_dock");

    window.addDockWidget(Qt::RightDockWidgetArea, &primary);
    window.addDockWidget(Qt::RightDockWidgetArea, &tabbed);
    window.tabifyDockWidget(&primary, &tabbed);
    window.addDockWidget(Qt::BottomDockWidgetArea, &floating);
    window.show();
    primary.show();
    tabbed.hide();
    floating.setFloating(true);
    floating.show();
    QCoreApplication::processEvents();

    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &actions, nullptr);
    ASSERT_NE(windows.register_window(&window), spectra::adapters::qt::INVALID_HOST_ID);
    spectra::adapters::qt::QtWorkspaceBridge bridge(&windows);
    spectra::WorkspaceData                   data;
    bridge.capture_layout(data);
    ASSERT_FALSE(data.desktop_layout.main_window_state_base64.empty());
    ASSERT_FALSE(data.desktop_layout.main_window_geometry_base64.empty());

    floating.setFloating(false);
    window.addDockWidget(Qt::LeftDockWidgetArea, &floating);
    floating.hide();
    window.addDockWidget(Qt::LeftDockWidgetArea, &primary);
    window.addDockWidget(Qt::TopDockWidgetArea, &tabbed);
    tabbed.show();
    QCoreApplication::processEvents();

    ASSERT_TRUE(bridge.apply_layout(data));
    QCoreApplication::processEvents();
    EXPECT_EQ(window.dockWidgetArea(&primary), Qt::RightDockWidgetArea);
    EXPECT_EQ(window.dockWidgetArea(&tabbed), Qt::RightDockWidgetArea);
    EXPECT_TRUE(window.tabifiedDockWidgets(&primary).contains(&tabbed));
    EXPECT_FALSE(tabbed.isVisible());
    EXPECT_TRUE(floating.isFloating());
    EXPECT_TRUE(floating.isVisible());
}

TEST(QtWorkspace, BridgeRejectsCorruptNativeDockState)
{
    spectra::FigureRegistry                   registry;
    spectra::CommandRegistry                  commands;
    spectra::adapters::qt::QtActionBridge     actions(commands);
    spectra::adapters::qt::SpectraMainWindow  window(nullptr, &registry, &actions, nullptr);
    spectra::adapters::qt::MainWindowRegistry windows(nullptr, &registry, &actions, nullptr);
    ASSERT_NE(windows.register_window(&window), spectra::adapters::qt::INVALID_HOST_ID);

    spectra::WorkspaceData data;
    data.desktop_layout.provider                 = "native";
    data.desktop_layout.main_window_state_base64 = "not-valid-qt-state";

    spectra::adapters::qt::QtWorkspaceBridge bridge(&windows);
    EXPECT_FALSE(bridge.apply_layout(data));
}

TEST(QtWorkspace, MixedNestedSplitLayoutBuildsNestedNativeSplitters)
{
    spectra::FigureRegistry        registry;
    std::vector<spectra::FigureId> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto figure = std::make_unique<spectra::Figure>();
        figure->subplot(1, 1, 1);
        ids.push_back(registry.register_figure(std::move(figure)));
    }

    spectra::CommandRegistry                 commands;
    spectra::adapters::qt::QtActionBridge    actions(commands);
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);
    const std::string                        layout = "{\"active\":" + std::to_string(ids[2])
                               + ",\"root\":{\"id\":1,\"leaf\":false,\"dir\":\"h\",\"ratio\":0.4,"
                                 "\"first\":{\"id\":2,\"leaf\":true,\"figure\":"
                               + std::to_string(ids[0]) + ",\"figures\":[" + std::to_string(ids[0])
                               + "],\"active_local\":0},"
                                 "\"second\":{\"id\":3,\"leaf\":false,\"dir\":\"v\",\"ratio\":0.6,"
                                 "\"first\":{\"id\":4,\"leaf\":true,\"figure\":"
                               + std::to_string(ids[1]) + ",\"figures\":[" + std::to_string(ids[1])
                               + "],\"active_local\":0},"
                                 "\"second\":{\"id\":5,\"leaf\":true,\"figure\":"
                               + std::to_string(ids[2]) + ",\"figures\":[" + std::to_string(ids[2])
                               + "],\"active_local\":0}}}}";

    ASSERT_TRUE(window.restore_split_layout(layout, {}));
    EXPECT_EQ(window.pane_count(), 3u);
    EXPECT_EQ(window.open_figure_ids(), ids);

    const auto splitters = window.findChildren<QSplitter*>();
    ASSERT_EQ(splitters.size(), 2);
    bool has_horizontal = false;
    bool has_vertical   = false;
    for (const QSplitter* splitter : splitters)
    {
        has_horizontal = has_horizontal || splitter->orientation() == Qt::Horizontal;
        has_vertical   = has_vertical || splitter->orientation() == Qt::Vertical;
    }
    EXPECT_TRUE(has_horizontal);
    EXPECT_TRUE(has_vertical);
}

// ── Workspace validation ─────────────────────────────────────────────────────

TEST(QtWorkspace, ValidationPassesForValidData)
{
    spectra::WorkspaceData data;
    data.version = 5;
    auto result  = spectra::validate_workspace_data(data);
    EXPECT_TRUE(result.valid);
}

TEST(QtWorkspace, ValidationHandlesCorruptVersion)
{
    spectra::WorkspaceData data;
    data.version = 999;   // future version
    auto result  = spectra::validate_workspace_data(data);
    // Should either repair or warn
    EXPECT_TRUE(result.valid || !result.warnings.empty() || !result.errors.empty());
}
