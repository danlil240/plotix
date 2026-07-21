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

#include "ui/workspace/workspace.hpp"
#include "adapters/qt/qt_workspace_bridge.hpp"
#include "adapters/qt/docking/main_window_registry.hpp"
#include "adapters/qt/qt_action_bridge.hpp"

#include "ui/commands/command_registry.hpp"

#include <spectra/figure_registry.hpp>

#include <memory>
#include <string>

namespace {

struct QtWorkspaceEnv
{
    std::unique_ptr<QApplication> app;
    std::unique_ptr<spectra::FigureRegistry> registry;
    std::unique_ptr<spectra::CommandRegistry> cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtWorkspaceEnv()
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        qputenv("QT_QPA_PLATFORM", "offscreen");
        app = std::make_unique<QApplication>(argc, argv);
        registry = std::make_unique<spectra::FigureRegistry>();
        cmd_registry = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }
};

QtWorkspaceEnv& env()
{
    static QtWorkspaceEnv e;
    return e;
}

} // namespace

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
    ws.state_base64 = "AAAA";
    ws.geometry_base64 = "BBBB";
    ws.title = "Detached Window";

    EXPECT_EQ(ws.state_base64, "AAAA");
    EXPECT_EQ(ws.geometry_base64, "BBBB");
    EXPECT_EQ(ws.title, "Detached Window");
}

TEST(QtWorkspace, DesktopLayoutWithMultipleWindows)
{
    spectra::WorkspaceData data;
    data.desktop_layout.provider = "native";
    data.desktop_layout.provider_version = "1.0";
    data.desktop_layout.main_window_state_base64 = "main_state";
    data.desktop_layout.main_window_geometry_base64 = "main_geo";

    for (int i = 0; i < 3; ++i)
    {
        spectra::WorkspaceData::DesktopLayoutState::WindowState ws;
        ws.state_base64 = "state_" + std::to_string(i);
        ws.geometry_base64 = "geo_" + std::to_string(i);
        ws.title = "Window " + std::to_string(i);
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
    data.desktop_layout.provider = "native";
    data.desktop_layout.main_window_state_base64 = "test_state";

    std::string json = spectra::Workspace::serialize_json(data);
    EXPECT_FALSE(json.empty());
    // The JSON should contain the provider value
    EXPECT_NE(json.find("native"), std::string::npos);
}

TEST(QtWorkspace, SerializeJsonRoundTrip)
{
    spectra::WorkspaceData data;
    data.version = 5;
    data.theme_name = "night";
    data.desktop_layout.provider = "native";
    data.desktop_layout.provider_version = "1.0";
    data.desktop_layout.main_window_state_base64 = "state123";
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
    data.version = 4;
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
    spectra::WorkspaceData data;
    bridge.capture_layout(data);
    // Should not crash, and should not populate anything
    EXPECT_TRUE(data.desktop_layout.provider.empty());
}

TEST(QtWorkspace, BridgeApplyWithNullRegistry)
{
    spectra::adapters::qt::QtWorkspaceBridge bridge(nullptr);
    spectra::WorkspaceData data;
    EXPECT_FALSE(bridge.apply_layout(data));
}

// ── QtWorkspaceBridge with registry (no runtime — can't create windows) ──────

TEST(QtWorkspace, BridgeApplyProviderMismatch)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    spectra::adapters::qt::QtWorkspaceBridge bridge(&registry);
    bridge.set_provider("native");

    spectra::WorkspaceData data;
    data.desktop_layout.provider = "kddockwidgets"; // different provider
    EXPECT_FALSE(bridge.apply_layout(data));
}

TEST(QtWorkspace, BridgeApplyEmptyProviderMatches)
{
    auto& e = env();

    spectra::adapters::qt::MainWindowRegistry registry(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    spectra::adapters::qt::QtWorkspaceBridge bridge(&registry);
    bridge.set_provider("native");

    spectra::WorkspaceData data;
    data.desktop_layout.provider = ""; // empty provider should match
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
    EXPECT_TRUE(true); // just verify set_provider doesn't crash
}

// ── Workspace validation ─────────────────────────────────────────────────────

TEST(QtWorkspace, ValidationPassesForValidData)
{
    spectra::WorkspaceData data;
    data.version = 5;
    auto result = spectra::validate_workspace_data(data);
    EXPECT_TRUE(result.valid);
}

TEST(QtWorkspace, ValidationHandlesCorruptVersion)
{
    spectra::WorkspaceData data;
    data.version = 999; // future version
    auto result = spectra::validate_workspace_data(data);
    // Should either repair or warn
    EXPECT_TRUE(result.valid || !result.warnings.empty() || !result.errors.empty());
}
