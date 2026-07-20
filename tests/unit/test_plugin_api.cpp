#include <filesystem>
#include <gtest/gtest.h>

#include "ui/commands/command_registry.hpp"
#include "ui/workspace/plugin_api.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"

using namespace spectra;

// ─── C ABI Functions ─────────────────────────────────────────────────────────

TEST(PluginCAPI, RegisterCommand)
{
    CommandRegistry registry;

    bool               called = false;
    SpectraCommandDesc desc{};
    desc.id            = "plugin.test";
    desc.label         = "Test Command";
    desc.category      = "Plugin";
    desc.shortcut_hint = "Ctrl+T";
    desc.callback      = [](void* ud) { *static_cast<bool*>(ud) = true; };
    desc.user_data     = &called;

    int result = spectra_register_command(&registry, &desc);
    EXPECT_EQ(result, 0);

    auto* cmd = registry.find("plugin.test");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->label, "Test Command");
    EXPECT_EQ(cmd->category, "Plugin");

    registry.execute("plugin.test");
    EXPECT_TRUE(called);
}

TEST(PluginCAPI, RegisterCommandNullRegistry)
{
    SpectraCommandDesc desc{};
    desc.id    = "test";
    desc.label = "Test";
    EXPECT_EQ(spectra_register_command(nullptr, &desc), -1);
}

TEST(PluginCAPI, RegisterCommandNullDesc)
{
    CommandRegistry registry;
    EXPECT_EQ(spectra_register_command(&registry, nullptr), -1);
}

TEST(PluginCAPI, RegisterCommandNullId)
{
    CommandRegistry    registry;
    SpectraCommandDesc desc{};
    desc.id    = nullptr;
    desc.label = "Test";
    EXPECT_EQ(spectra_register_command(&registry, &desc), -1);
}

TEST(PluginCAPI, UnregisterCommand)
{
    CommandRegistry registry;
    registry.register_command("plugin.test", "Test", []() {});
    EXPECT_NE(registry.find("plugin.test"), nullptr);

    int result = spectra_unregister_command(&registry, "plugin.test");
    EXPECT_EQ(result, 0);
    EXPECT_EQ(registry.find("plugin.test"), nullptr);
}

TEST(PluginCAPI, UnregisterCommandNull)
{
    EXPECT_EQ(spectra_unregister_command(nullptr, "test"), -1);
    CommandRegistry registry;
    EXPECT_EQ(spectra_unregister_command(&registry, nullptr), -1);
}

TEST(PluginCAPI, ExecuteCommand)
{
    CommandRegistry registry;
    bool            called = false;
    registry.register_command("plugin.test", "Test", [&]() { called = true; });

    int result = spectra_execute_command(&registry, "plugin.test");
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(called);
}

TEST(PluginCAPI, ExecuteCommandNotFound)
{
    CommandRegistry registry;
    EXPECT_EQ(spectra_execute_command(&registry, "nonexistent"), -1);
}

TEST(PluginCAPI, ExecuteCommandNull)
{
    EXPECT_EQ(spectra_execute_command(nullptr, "test"), -1);
    CommandRegistry registry;
    EXPECT_EQ(spectra_execute_command(&registry, nullptr), -1);
}

TEST(PluginCAPI, BindShortcut)
{
    ShortcutManager mgr;
    int             result = spectra_bind_shortcut(&mgr, "Ctrl+T", "test.cmd");
    EXPECT_EQ(result, 0);
    EXPECT_EQ(mgr.command_for_shortcut(Shortcut::from_string("Ctrl+T")), "test.cmd");
}

TEST(PluginCAPI, BindShortcutInvalid)
{
    ShortcutManager mgr;
    EXPECT_EQ(spectra_bind_shortcut(&mgr, "", "test.cmd"), -1);
}

TEST(PluginCAPI, BindShortcutNull)
{
    EXPECT_EQ(spectra_bind_shortcut(nullptr, "Ctrl+T", "test"), -1);
    ShortcutManager mgr;
    EXPECT_EQ(spectra_bind_shortcut(&mgr, nullptr, "test"), -1);
    EXPECT_EQ(spectra_bind_shortcut(&mgr, "Ctrl+T", nullptr), -1);
}

TEST(PluginCAPI, PushUndo)
{
    UndoManager undo;
    int         value = 0;

    int result = spectra_push_undo(
        &undo,
        "Set value",
        [](void* ud) { *static_cast<int*>(ud) = 0; },
        &value,
        [](void* ud) { *static_cast<int*>(ud) = 42; },
        &value);
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(undo.can_undo());
    EXPECT_EQ(undo.undo_description(), "Set value");

    undo.undo();
    EXPECT_EQ(value, 0);

    undo.redo();
    EXPECT_EQ(value, 42);
}

TEST(PluginCAPI, PushUndoNull)
{
    EXPECT_EQ(spectra_push_undo(nullptr, "test", nullptr, nullptr, nullptr, nullptr), -1);
    UndoManager undo;
    EXPECT_EQ(spectra_push_undo(&undo, nullptr, nullptr, nullptr, nullptr, nullptr), -1);
}

// ─── PluginManager ───────────────────────────────────────────────────────────

TEST(PluginManagerTest, Construction)
{
    PluginManager mgr;
    EXPECT_EQ(mgr.plugin_count(), 0u);
    EXPECT_TRUE(mgr.plugins().empty());
}

TEST(PluginManagerTest, LoadNonexistent)
{
    PluginManager mgr;
    EXPECT_FALSE(mgr.load_plugin("/nonexistent/plugin.so"));
}

TEST(PluginManagerTest, UnloadNonexistent)
{
    PluginManager mgr;
    EXPECT_FALSE(mgr.unload_plugin("nonexistent"));
}

TEST(PluginManagerTest, FindPluginEmpty)
{
    PluginManager mgr;
    EXPECT_EQ(mgr.find_plugin("test"), nullptr);
}

TEST(PluginManagerTest, UnloadAll)
{
    PluginManager mgr;
    // Should not crash when empty
    mgr.unload_all();
    EXPECT_EQ(mgr.plugin_count(), 0u);
}

TEST(PluginManagerTest, DiscoverNonexistentDir)
{
    PluginManager mgr;
    auto          paths = mgr.discover("/nonexistent/plugin/dir");
    EXPECT_TRUE(paths.empty());
}

TEST(PluginManagerTest, DiscoverEmptyDir)
{
    auto tmp = std::filesystem::temp_directory_path() / "spectra_test_plugins_empty";
    std::filesystem::create_directories(tmp);

    PluginManager mgr;
    auto          paths = mgr.discover(tmp.string());
    EXPECT_TRUE(paths.empty());

    std::filesystem::remove(tmp);
}

TEST(PluginManagerTest, DefaultPluginDir)
{
    std::string dir = PluginManager::default_plugin_dir();
    EXPECT_FALSE(dir.empty());
    EXPECT_NE(dir.find("plugins"), std::string::npos);
}

// ─── PluginManager Serialization ─────────────────────────────────────────────

TEST(PluginManagerSerialize, EmptyState)
{
    PluginManager mgr;
    std::string   json = mgr.serialize_state();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"plugins\""), std::string::npos);
}

TEST(PluginManagerSerialize, DeserializeEmpty)
{
    PluginManager mgr;
    EXPECT_TRUE(mgr.deserialize_state("{\"plugins\": []}"));
}

// ─── PluginEntry Struct ──────────────────────────────────────────────────────

TEST(PluginEntryTest, DefaultValues)
{
    PluginEntry entry;
    EXPECT_TRUE(entry.name.empty());
    EXPECT_TRUE(entry.version.empty());
    EXPECT_FALSE(entry.loaded);
    EXPECT_TRUE(entry.enabled);
    EXPECT_EQ(entry.api_version_minor, 0u);
    EXPECT_EQ(entry.handle, nullptr);
    EXPECT_EQ(entry.shutdown_fn, nullptr);
    EXPECT_TRUE(entry.registered_commands.empty());
}

// ─── Plugin Context ──────────────────────────────────────────────────────────

TEST(PluginContextTest, VersionConstants)
{
    EXPECT_EQ(SPECTRA_PLUGIN_API_VERSION_MAJOR, 2u);
    EXPECT_EQ(SPECTRA_PLUGIN_API_VERSION_MINOR, 1u);
}

TEST(PluginContextTest, ContextStruct)
{
    SpectraPluginContext ctx{};
    ctx.api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;
    EXPECT_EQ(ctx.api_version_major, 2u);
    EXPECT_EQ(ctx.api_version_minor, 1u);
    EXPECT_EQ(ctx.command_registry, nullptr);
    EXPECT_EQ(ctx.shortcut_manager, nullptr);
    EXPECT_EQ(ctx.undo_manager, nullptr);
    EXPECT_EQ(ctx.transform_registry, nullptr);
    EXPECT_EQ(ctx.overlay_registry, nullptr);
    EXPECT_EQ(ctx.data_source_registry, nullptr);
    EXPECT_EQ(ctx.series_type_registry, nullptr);
    EXPECT_EQ(ctx.backend_handle, nullptr);
    EXPECT_EQ(ctx.plugin_ui_registry, nullptr);
}

TEST(PluginContextTest, InfoStruct)
{
    SpectraPluginInfo info{};
    info.name              = "TestPlugin";
    info.version           = "1.0.0";
    info.author            = "Test Author";
    info.description       = "A test plugin";
    info.api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    info.api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

    EXPECT_STREQ(info.name, "TestPlugin");
    EXPECT_STREQ(info.version, "1.0.0");
    EXPECT_STREQ(info.author, "Test Author");
    EXPECT_STREQ(info.description, "A test plugin");
}

// ─── Plugin Enable/Disable ───────────────────────────────────────────────────

TEST(PluginManagerEnable, EnableDisableNoPlugins)
{
    PluginManager mgr;
    // Should not crash
    mgr.set_plugin_enabled("nonexistent", false);
    mgr.set_plugin_enabled("nonexistent", true);
}

// ─── C ABI Command with Default Category ─────────────────────────────────────

TEST(PluginCAPI, RegisterCommandDefaultCategory)
{
    CommandRegistry registry;

    SpectraCommandDesc desc{};
    desc.id        = "plugin.nocategory";
    desc.label     = "No Category";
    desc.category  = nullptr;   // Should default to "Plugin"
    desc.callback  = nullptr;
    desc.user_data = nullptr;

    int result = spectra_register_command(&registry, &desc);
    EXPECT_EQ(result, 0);

    auto* cmd = registry.find("plugin.nocategory");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->category, "Plugin");
}

TEST(PluginCAPI, RegisterCommandNoCallback)
{
    CommandRegistry registry;

    SpectraCommandDesc desc{};
    desc.id       = "plugin.nocb";
    desc.label    = "No Callback";
    desc.callback = nullptr;

    int result = spectra_register_command(&registry, &desc);
    EXPECT_EQ(result, 0);

    // Execute should fail (no callback)
    EXPECT_FALSE(registry.execute("plugin.nocb"));
}
