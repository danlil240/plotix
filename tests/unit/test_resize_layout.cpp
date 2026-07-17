#include <gtest/gtest.h>

#include "core/layout.hpp"
#include "ui/app/window_ui_context.hpp"
#include "render/vulkan/window_context.hpp"

using namespace spectra;

// Regression tests for layout correctness under resize-like dimension changes.
// The resize bug root cause was figure dimensions diverging from the actual
// swapchain extent — these tests verify the layout stays valid for any size.

TEST(ResizeLayout, SingleCellShrink)
{
    auto before = compute_subplot_layout(1280.0f, 720.0f, 1, 1);
    auto after  = compute_subplot_layout(800.0f, 600.0f, 1, 1);
    ASSERT_EQ(before.size(), 1u);
    ASSERT_EQ(after.size(), 1u);

    // Viewport must shrink when figure shrinks
    EXPECT_LT(after[0].w, before[0].w);
    EXPECT_LT(after[0].h, before[0].h);
    // Must stay within new figure bounds
    EXPECT_LE(after[0].x + after[0].w, 800.0f);
    EXPECT_LE(after[0].y + after[0].h, 600.0f);
}

TEST(ResizeLayout, MultiSubplotStaysInBoundsAcrossResizes)
{
    // Simulate a resize drag across multiple sizes
    float sizes[][2] = {{1920.0f, 1080.0f},
                        {1600.0f, 900.0f},
                        {1024.0f, 768.0f},
                        {640.0f, 480.0f},
                        {320.0f, 240.0f},
                        {1920.0f, 1080.0f}};

    for (auto& sz : sizes)
    {
        auto rects = compute_subplot_layout(sz[0], sz[1], 2, 2);
        ASSERT_EQ(rects.size(), 4u);

        for (size_t i = 0; i < rects.size(); ++i)
        {
            EXPECT_GE(rects[i].w, 0.0f) << "axes " << i << " at " << sz[0] << "x" << sz[1];
            EXPECT_GE(rects[i].h, 0.0f) << "axes " << i << " at " << sz[0] << "x" << sz[1];
            EXPECT_LE(rects[i].x + rects[i].w, sz[0])
                << "axes " << i << " exceeds width at " << sz[0] << "x" << sz[1];
            EXPECT_LE(rects[i].y + rects[i].h, sz[1])
                << "axes " << i << " exceeds height at " << sz[0] << "x" << sz[1];
        }
    }
}

TEST(ResizeLayout, ZeroDimensionsDoNotCrash)
{
    // Simulates minimized window (0×0)
    auto rects = compute_subplot_layout(0.0f, 0.0f, 1, 1);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GE(rects[0].w, 0.0f);
    EXPECT_GE(rects[0].h, 0.0f);
}

TEST(ResizeLayout, VerySmallDimensionsClampCorrectly)
{
    // Smaller than margins — viewport area should clamp to zero, not go negative
    auto rects = compute_subplot_layout(10.0f, 10.0f, 1, 1);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GE(rects[0].w, 0.0f);
    EXPECT_GE(rects[0].h, 0.0f);
}

TEST(ResizeLayout, ConsecutiveResizesProduceDeterministicLayout)
{
    // Same dimensions must always produce the same layout
    auto a = compute_subplot_layout(1024.0f, 768.0f, 1, 2);
    auto b = compute_subplot_layout(640.0f, 480.0f, 1, 2);
    auto c = compute_subplot_layout(1024.0f, 768.0f, 1, 2);   // back to original

    ASSERT_EQ(a.size(), c.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a[i].x, c[i].x) << "cell " << i;
        EXPECT_FLOAT_EQ(a[i].y, c[i].y) << "cell " << i;
        EXPECT_FLOAT_EQ(a[i].w, c[i].w) << "cell " << i;
        EXPECT_FLOAT_EQ(a[i].h, c[i].h) << "cell " << i;
    }
}

TEST(VulkanSurfaceState, LostSurfaceInvalidatesSwapchainUntilRecovered)
{
    WindowContext context;

    context.mark_surface_lost();
    EXPECT_TRUE(context.surface_lost);
    EXPECT_TRUE(context.swapchain_dirty);
    EXPECT_TRUE(context.swapchain_invalidated);

    context.mark_surface_recovered();
    EXPECT_FALSE(context.surface_lost);
    EXPECT_FALSE(context.swapchain_dirty);
    EXPECT_FALSE(context.swapchain_invalidated);
}
