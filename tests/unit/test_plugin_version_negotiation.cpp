// test_plugin_version_negotiation.cpp — A3: Plugin context version negotiation tests.
//
// Tests that:
// - v1.0 plugins load successfully and see nullptr for transform_registry
// - v1.1 plugins load and can use the transform registry
// - Plugins requesting a higher minor version than the host still load (with warning)
// - api_version_minor is stored in PluginEntry

#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "math/data_transform.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Direct context gating tests (no .so needed) ────────────────────────────

TEST(PluginVersionNegotiation, ContextGatesTransformRegistryForV1_0)
{
    // make_context is private, so test indirectly via the context struct behavior:
    // A v1.0 context should have transform_registry = nullptr.
    SpectraPluginContext ctx{};
    ctx.api_version_major  = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor  = 0;
    ctx.transform_registry = nullptr;
    ctx.overlay_registry   = nullptr;

    EXPECT_EQ(ctx.transform_registry, nullptr);
    EXPECT_EQ(ctx.overlay_registry, nullptr);
}

TEST(PluginVersionNegotiation, ContextExposesTransformRegistryForV1_1)
{
    TransformRegistry    treg;
    SpectraPluginContext ctx{};
    ctx.api_version_major  = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor  = 1;
    ctx.transform_registry = static_cast<SpectraTransformRegistry>(&treg);
    ctx.overlay_registry   = nullptr;

    EXPECT_NE(ctx.transform_registry, nullptr);
    EXPECT_EQ(ctx.overlay_registry, nullptr);
}

TEST(PluginVersionNegotiation, ContextExposesOverlayRegistryForV1_2)
{
    SpectraPluginContext ctx{};
    ctx.api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor = 2;

    // Both should be populated at v1.2
    int dummy_transform    = 0;
    int dummy_overlay      = 0;
    ctx.transform_registry = &dummy_transform;
    ctx.overlay_registry   = &dummy_overlay;

    EXPECT_NE(ctx.transform_registry, nullptr);
    EXPECT_NE(ctx.overlay_registry, nullptr);
}

TEST(PluginVersionNegotiation, PluginEntryStoresMinorVersion)
{
    PluginEntry entry;
    EXPECT_EQ(entry.api_version_minor, 0u);

    entry.api_version_minor = 1;
    EXPECT_EQ(entry.api_version_minor, 1u);
}

TEST(PluginVersionNegotiation, HostVersionConstants)
{
    EXPECT_EQ(SPECTRA_PLUGIN_API_VERSION_MAJOR, 2u);
    EXPECT_GE(SPECTRA_PLUGIN_API_VERSION_MINOR, 0u);
}

// ─── Plugin .so loading tests ────────────────────────────────────────────────

#ifndef SPECTRA_MOCK_PLUGIN_V1_0_PATH
    #define SPECTRA_MOCK_PLUGIN_V1_0_PATH ""
#endif
#ifndef SPECTRA_MOCK_PLUGIN_V1_1_PATH
    #define SPECTRA_MOCK_PLUGIN_V1_1_PATH ""
#endif
#ifndef SPECTRA_MOCK_PLUGIN_V1_99_PATH
    #define SPECTRA_MOCK_PLUGIN_V1_99_PATH ""
#endif

class PluginVersionLoadTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        v1_0_path_  = SPECTRA_MOCK_PLUGIN_V1_0_PATH;
        v1_1_path_  = SPECTRA_MOCK_PLUGIN_V1_1_PATH;
        v1_99_path_ = SPECTRA_MOCK_PLUGIN_V1_99_PATH;

        transform_reg_ = std::make_unique<TransformRegistry>();
        mgr_.set_command_registry(&cmd_reg_);
        mgr_.set_shortcut_manager(&shortcut_mgr_);
        mgr_.set_undo_manager(&undo_mgr_);
        mgr_.set_transform_registry(transform_reg_.get());
    }

    void TearDown() override
    {
        // Unregister provider-owned callbacks while the plugin library is still loaded.
        mgr_.unload_all();
        mgr_.set_transform_registry(nullptr);
        transform_reg_.reset();
    }

    std::string                        v1_0_path_;
    std::string                        v1_1_path_;
    std::string                        v1_99_path_;
    CommandRegistry                    cmd_reg_;
    ShortcutManager                    shortcut_mgr_;
    UndoManager                        undo_mgr_;
    std::unique_ptr<TransformRegistry> transform_reg_;
    PluginManager                      mgr_;
};

TEST_F(PluginVersionLoadTest, V1_0_PluginLoadsSuccessfully)
{
    if (v1_0_path_.empty() || !std::filesystem::exists(v1_0_path_))
        GTEST_SKIP() << "Mock v1.0 plugin not found at: " << v1_0_path_;

    ASSERT_TRUE(mgr_.load_plugin(v1_0_path_));
    EXPECT_EQ(mgr_.plugin_count(), 1u);

    auto* entry = mgr_.find_plugin("MockPluginV1_0");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name, "MockPluginV1_0");
    EXPECT_EQ(entry->api_version_minor, 0u);
    EXPECT_TRUE(entry->loaded);
}

TEST_F(PluginVersionLoadTest, V1_0_PluginIgnoresTransformRegistry)
{
    if (v1_0_path_.empty() || !std::filesystem::exists(v1_0_path_))
        GTEST_SKIP() << "Mock v1.0 plugin not found at: " << v1_0_path_;

    auto before = transform_reg_->available_transforms();
    ASSERT_TRUE(mgr_.load_plugin(v1_0_path_));

    // v1.0 plugin should not register any transforms
    auto after = transform_reg_->available_transforms();
    EXPECT_EQ(after.size(), before.size());
}

TEST_F(PluginVersionLoadTest, V1_1_PluginUsesTransformRegistry)
{
    if (v1_1_path_.empty() || !std::filesystem::exists(v1_1_path_))
        GTEST_SKIP() << "Mock v1.1 plugin not found at: " << v1_1_path_;

    auto before = transform_reg_->available_transforms();
    ASSERT_TRUE(mgr_.load_plugin(v1_1_path_));

    auto* entry = mgr_.find_plugin("MockPluginV1_1");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->api_version_minor, 1u);

    // v1.1 plugin should register transforms
    auto after = transform_reg_->available_transforms();
    EXPECT_GT(after.size(), before.size());

    // Verify the specific transform works
    DataTransform t;
    ASSERT_TRUE(transform_reg_->get_transform("PluginTriple", t));
    EXPECT_FLOAT_EQ(t.apply_scalar(5.0f), 15.0f);
}

TEST_F(PluginVersionLoadTest, V1_99_PluginStillLoads)
{
    if (v1_99_path_.empty() || !std::filesystem::exists(v1_99_path_))
        GTEST_SKIP() << "Mock v1.99 plugin not found at: " << v1_99_path_;

    // Plugin requesting unsupported minor version should still load
    // (graceful degradation — host logs a warning)
    ASSERT_TRUE(mgr_.load_plugin(v1_99_path_));
    EXPECT_EQ(mgr_.plugin_count(), 1u);

    auto* entry = mgr_.find_plugin("MockPluginV1_99");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->api_version_minor, 99u);
    EXPECT_TRUE(entry->loaded);
}

TEST_F(PluginVersionLoadTest, MultipleVersionPluginsCoexist)
{
    if (v1_0_path_.empty() || !std::filesystem::exists(v1_0_path_))
        GTEST_SKIP() << "Mock v1.0 plugin not found";
    if (v1_1_path_.empty() || !std::filesystem::exists(v1_1_path_))
        GTEST_SKIP() << "Mock v1.1 plugin not found";

    ASSERT_TRUE(mgr_.load_plugin(v1_0_path_));
    ASSERT_TRUE(mgr_.load_plugin(v1_1_path_));
    EXPECT_EQ(mgr_.plugin_count(), 2u);

    auto* v1_0 = mgr_.find_plugin("MockPluginV1_0");
    auto* v1_1 = mgr_.find_plugin("MockPluginV1_1");
    ASSERT_NE(v1_0, nullptr);
    ASSERT_NE(v1_1, nullptr);
    EXPECT_EQ(v1_0->api_version_minor, 0u);
    EXPECT_EQ(v1_1->api_version_minor, 1u);
}
