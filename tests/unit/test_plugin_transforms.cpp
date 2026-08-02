#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "math/data_transform.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Direct C ABI Transform Registration Tests ──────────────────────────────

TEST(PluginTransformCAPI, RegisterScalarTransform)
{
    TransformRegistry registry;

    auto before = registry.available_transforms();

    int result = spectra_register_transform(
        &registry,
        "TestDouble",
        [](float v, void*) -> float { return v * 2.0f; },
        nullptr,
        "Doubles each value");
    EXPECT_EQ(result, 0);

    auto after = registry.available_transforms();
    EXPECT_EQ(after.size(), before.size() + 1);

    // Verify it's in the list
    bool found = false;
    for (const auto& name : after)
    {
        if (name == "TestDouble")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    // Verify apply_y produces correct output
    DataTransform t;
    ASSERT_TRUE(registry.get_transform("TestDouble", t));

    std::vector<float> x_in = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y_in = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);

    ASSERT_EQ(y_out.size(), 4u);
    EXPECT_FLOAT_EQ(y_out[0], 2.0f);
    EXPECT_FLOAT_EQ(y_out[1], 4.0f);
    EXPECT_FLOAT_EQ(y_out[2], 6.0f);
    EXPECT_FLOAT_EQ(y_out[3], 8.0f);
}

TEST(PluginTransformCAPI, RegisterXYTransform)
{
    TransformRegistry registry;

    int result = spectra_register_xy_transform(
        &registry,
        "TestReverse",
        [](const float* x_in,
           const float* y_in,
           size_t       count,
           float*       x_out,
           float*       y_out,
           size_t*      out_count,
           void*)
        {
            *out_count = count;
            for (size_t i = 0; i < count; ++i)
            {
                x_out[i] = x_in[count - 1 - i];
                y_out[i] = y_in[count - 1 - i];
            }
        },
        nullptr,
        "Reverses data order");
    EXPECT_EQ(result, 0);

    DataTransform t;
    ASSERT_TRUE(registry.get_transform("TestReverse", t));

    std::vector<float> x_in = {1.0f, 2.0f, 3.0f};
    std::vector<float> y_in = {10.0f, 20.0f, 30.0f};
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);

    ASSERT_EQ(x_out.size(), 3u);
    ASSERT_EQ(y_out.size(), 3u);
    EXPECT_FLOAT_EQ(x_out[0], 3.0f);
    EXPECT_FLOAT_EQ(x_out[1], 2.0f);
    EXPECT_FLOAT_EQ(x_out[2], 1.0f);
    EXPECT_FLOAT_EQ(y_out[0], 30.0f);
    EXPECT_FLOAT_EQ(y_out[1], 20.0f);
    EXPECT_FLOAT_EQ(y_out[2], 10.0f);
}

TEST(PluginTransformCAPI, RegisterTransformNullRegistry)
{
    EXPECT_EQ(spectra_register_transform(
                  nullptr,
                  "x",
                  [](float v, void*) { return v; },
                  nullptr,
                  ""),
              -1);
}

TEST(PluginTransformCAPI, RegisterTransformNullName)
{
    TransformRegistry registry;
    EXPECT_EQ(spectra_register_transform(
                  &registry,
                  nullptr,
                  [](float v, void*) { return v; },
                  nullptr,
                  ""),
              -1);
}

TEST(PluginTransformCAPI, RegisterTransformNullCallback)
{
    TransformRegistry registry;
    EXPECT_EQ(spectra_register_transform(&registry, "x", nullptr, nullptr, ""), -1);
}

TEST(PluginTransformCAPI, RegisterXYTransformNullRegistry)
{
    EXPECT_EQ(spectra_register_xy_transform(
                  nullptr,
                  "x",
                  [](const float*, const float*, size_t, float*, float*, size_t*, void*) {},
                  nullptr,
                  ""),
              -1);
}

TEST(PluginTransformCAPI, RegisterXYTransformNullName)
{
    TransformRegistry registry;
    EXPECT_EQ(spectra_register_xy_transform(
                  &registry,
                  nullptr,
                  [](const float*, const float*, size_t, float*, float*, size_t*, void*) {},
                  nullptr,
                  ""),
              -1);
}

TEST(PluginTransformCAPI, RegisterXYTransformNullCallback)
{
    TransformRegistry registry;
    EXPECT_EQ(spectra_register_xy_transform(&registry, "x", nullptr, nullptr, ""), -1);
}

TEST(PluginTransformCAPI, ScalarTransformWithUserData)
{
    TransformRegistry registry;
    float             multiplier = 3.0f;

    spectra_register_transform(
        &registry,
        "ScaleByUserData",
        [](float v, void* ud) -> float { return v * *static_cast<float*>(ud); },
        &multiplier,
        "Scale by user-provided multiplier");

    DataTransform t;
    ASSERT_TRUE(registry.get_transform("ScaleByUserData", t));
    EXPECT_FLOAT_EQ(t.apply_scalar(5.0f), 15.0f);
}

TEST(PluginTransformCAPI, XYTransformChangesLength)
{
    TransformRegistry registry;

    // Register transform that filters out negative Y values
    spectra_register_xy_transform(
        &registry,
        "FilterPositive",
        [](const float* x_in,
           const float* y_in,
           size_t       count,
           float*       x_out,
           float*       y_out,
           size_t*      out_count,
           void*)
        {
            size_t j = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (y_in[i] >= 0.0f)
                {
                    x_out[j] = x_in[i];
                    y_out[j] = y_in[i];
                    ++j;
                }
            }
            *out_count = j;
        },
        nullptr,
        "Keeps only non-negative Y values");

    DataTransform t;
    ASSERT_TRUE(registry.get_transform("FilterPositive", t));

    std::vector<float> x_in = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y_in = {1.0f, -2.0f, 3.0f, -4.0f, 5.0f};
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);

    ASSERT_EQ(x_out.size(), 3u);
    ASSERT_EQ(y_out.size(), 3u);
    EXPECT_FLOAT_EQ(x_out[0], 0.0f);
    EXPECT_FLOAT_EQ(x_out[1], 2.0f);
    EXPECT_FLOAT_EQ(x_out[2], 4.0f);
    EXPECT_FLOAT_EQ(y_out[0], 1.0f);
    EXPECT_FLOAT_EQ(y_out[1], 3.0f);
    EXPECT_FLOAT_EQ(y_out[2], 5.0f);
}

TEST(PluginTransformCAPI, UnregisterTransformRemovesCustomTransform)
{
    TransformRegistry registry;

    ASSERT_EQ(spectra_register_transform(
                  &registry,
                  "TempTransform",
                  [](float v, void*) -> float { return v; },
                  nullptr,
                  "temporary"),
              0);

    DataTransform t;
    ASSERT_TRUE(registry.get_transform("TempTransform", t));

    EXPECT_EQ(spectra_unregister_transform(&registry, "TempTransform"), 0);
    EXPECT_FALSE(registry.get_transform("TempTransform", t));
}

TEST(PluginTransformCAPI, UnregisterTransformNullInputs)
{
    TransformRegistry registry;
    EXPECT_EQ(spectra_unregister_transform(nullptr, "X"), -1);
    EXPECT_EQ(spectra_unregister_transform(&registry, nullptr), -1);
}

// ─── Plugin .so Loading Tests ────────────────────────────────────────────────

#ifndef SPECTRA_MOCK_TRANSFORM_PLUGIN_PATH
    #define SPECTRA_MOCK_TRANSFORM_PLUGIN_PATH ""
#endif

class PluginTransformLoadTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        plugin_path_ = SPECTRA_MOCK_TRANSFORM_PLUGIN_PATH;
        if (plugin_path_.empty() || !std::filesystem::exists(plugin_path_))
        {
            GTEST_SKIP() << "Mock transform plugin .so not found at: " << plugin_path_;
        }

        mgr_.set_command_registry(&cmd_reg_);
        mgr_.set_shortcut_manager(&shortcut_mgr_);
        mgr_.set_undo_manager(&undo_mgr_);
        mgr_.set_transform_registry(&transform_reg_);
    }

    void TearDown() override { mgr_.unload_all(); }

    std::string     plugin_path_;
    CommandRegistry cmd_reg_;
    ShortcutManager shortcut_mgr_;
    UndoManager     undo_mgr_;
    TransformRegistry transform_reg_;
    PluginManager     mgr_;
};

TEST_F(PluginTransformLoadTest, LoadPluginRegistersTransforms)
{
    auto before = transform_reg_.available_transforms();

    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    EXPECT_EQ(mgr_.plugin_count(), 1u);

    auto* entry = mgr_.find_plugin("MockTransformPlugin");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name, "MockTransformPlugin");
    EXPECT_TRUE(entry->loaded);

    // Verify transforms were registered
    auto after = transform_reg_.available_transforms();
    EXPECT_GT(after.size(), before.size());

    bool found_double  = false;
    bool found_reverse = false;
    for (const auto& name : after)
    {
        if (name == "PluginDouble")
            found_double = true;
        if (name == "PluginReverse")
            found_reverse = true;
    }
    EXPECT_TRUE(found_double) << "PluginDouble transform not found in registry";
    EXPECT_TRUE(found_reverse) << "PluginReverse transform not found in registry";
    EXPECT_EQ(transform_reg_.transform_source("PluginDouble"), "MockTransformPlugin");
    EXPECT_EQ(transform_reg_.transform_source("PluginReverse"), "MockTransformPlugin");
}

TEST_F(PluginTransformLoadTest, UnloadRemovesOwnedTransformsBeforeLibraryClose)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    ASSERT_TRUE(mgr_.unload_plugin("MockTransformPlugin"));
    DataTransform transform;
    EXPECT_FALSE(transform_reg_.get_transform("PluginDouble", transform));
    EXPECT_FALSE(transform_reg_.get_transform("PluginReverse", transform));
}

TEST_F(PluginTransformLoadTest, ScalarTransformProducesCorrectOutput)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));

    DataTransform t;
    ASSERT_TRUE(transform_reg_.get_transform("PluginDouble", t));

    // Test scalar
    EXPECT_FLOAT_EQ(t.apply_scalar(5.0f), 10.0f);
    EXPECT_FLOAT_EQ(t.apply_scalar(-3.0f), -6.0f);
    EXPECT_FLOAT_EQ(t.apply_scalar(0.0f), 0.0f);

    // Test via apply_y
    std::vector<float> x_in = {0.0f, 1.0f, 2.0f};
    std::vector<float> y_in = {10.0f, 20.0f, 30.0f};
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);

    ASSERT_EQ(y_out.size(), 3u);
    EXPECT_FLOAT_EQ(y_out[0], 20.0f);
    EXPECT_FLOAT_EQ(y_out[1], 40.0f);
    EXPECT_FLOAT_EQ(y_out[2], 60.0f);
}

TEST_F(PluginTransformLoadTest, XYTransformProducesCorrectOutput)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));

    DataTransform t;
    ASSERT_TRUE(transform_reg_.get_transform("PluginReverse", t));

    std::vector<float> x_in = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y_in = {10.0f, 20.0f, 30.0f, 40.0f};
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);

    ASSERT_EQ(x_out.size(), 4u);
    ASSERT_EQ(y_out.size(), 4u);
    EXPECT_FLOAT_EQ(x_out[0], 4.0f);
    EXPECT_FLOAT_EQ(x_out[1], 3.0f);
    EXPECT_FLOAT_EQ(x_out[2], 2.0f);
    EXPECT_FLOAT_EQ(x_out[3], 1.0f);
    EXPECT_FLOAT_EQ(y_out[0], 40.0f);
    EXPECT_FLOAT_EQ(y_out[1], 30.0f);
    EXPECT_FLOAT_EQ(y_out[2], 20.0f);
    EXPECT_FLOAT_EQ(y_out[3], 10.0f);
}

TEST_F(PluginTransformLoadTest, LoadPluginTwiceFails)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    EXPECT_FALSE(mgr_.load_plugin(plugin_path_));
    EXPECT_EQ(mgr_.plugin_count(), 1u);
}

// NOTE: Explicit unload + transform cleanup is tested in E3 (integration tests)
// once PluginManager tracks and removes transforms registered by each plugin.
