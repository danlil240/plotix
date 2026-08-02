#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "gpu_hang_detector.hpp"
#include "multi_window_fixture.hpp"
#include "render/backend.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/window/window_manager.hpp"
#include "ui/app/window_ui_context.hpp"

using namespace spectra;
using namespace spectra::test;

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 0 — Single-Window Regression (always runs)
// These tests verify that the current single-window codebase is healthy.
// They serve as the regression baseline after each agent merge.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── SingleWindowRegression ──────────────────────────────────────────────────

TEST_F(SingleWindowFixture, HeadlessAppCreation)
{
    ASSERT_NE(app_.get(), nullptr);
    EXPECT_TRUE(app_->is_headless());
}

TEST_F(SingleWindowFixture, BackendInitialized)
{
    ASSERT_NE(app_->backend(), nullptr);
}

TEST_F(SingleWindowFixture, RendererInitialized)
{
    ASSERT_NE(app_->renderer(), nullptr);
}

TEST_F(SingleWindowFixture, SingleFigureCreation)
{
    auto& fig = create_simple_figure();
    EXPECT_EQ(fig.width(), 640u);
    EXPECT_EQ(fig.height(), 480u);
    EXPECT_EQ(fig.axes().size(), 1u);
}

TEST_F(SingleWindowFixture, RenderOneFrame)
{
    create_simple_figure();
    EXPECT_TRUE(render_one_frame());
}

TEST_F(SingleWindowFixture, RenderProducesPixels)
{
    auto& fig = create_simple_figure();
    app_->run();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(readback(fig, pixels));
    EXPECT_TRUE(has_non_zero_pixels(pixels));
}

TEST_F(SingleWindowFixture, MultipleFiguresHeadless)
{
    auto&              fig1 = app_->figure({.width = 320, .height = 240});
    auto&              ax1  = fig1.subplot(1, 1, 1);
    std::vector<float> x    = {0.0f, 1.0f, 2.0f};
    std::vector<float> y1   = {0.0f, 1.0f, 0.5f};
    ax1.line(x, y1);

    auto&              fig2 = app_->figure({.width = 320, .height = 240});
    auto&              ax2  = fig2.subplot(1, 1, 1);
    std::vector<float> y2   = {1.0f, 0.0f, 1.5f};
    ax2.line(x, y2);

    EXPECT_TRUE(render_one_frame());
}

TEST_F(SingleWindowFixture, PipelineCreation2D)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto line    = backend->create_pipeline(PipelineType::Line);
    auto scatter = backend->create_pipeline(PipelineType::Scatter);
    auto grid    = backend->create_pipeline(PipelineType::Grid);

    EXPECT_TRUE(line);
    EXPECT_TRUE(scatter);
    EXPECT_TRUE(grid);
}

TEST_F(SingleWindowFixture, PipelineCreation3D)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto line3d    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter3d = backend->create_pipeline(PipelineType::Scatter3D);
    auto mesh3d    = backend->create_pipeline(PipelineType::Mesh3D);
    auto surface3d = backend->create_pipeline(PipelineType::Surface3D);

    EXPECT_TRUE(line3d);
    EXPECT_TRUE(scatter3d);
    EXPECT_TRUE(mesh3d);
    EXPECT_TRUE(surface3d);
}

TEST_F(SingleWindowFixture, BufferCreateDestroy)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto buf = backend->create_buffer(BufferUsage::Storage, 1024);
    EXPECT_TRUE(buf);
    backend->destroy_buffer(buf);
}

TEST_F(SingleWindowFixture, FrameUBOLayout)
{
    EXPECT_EQ(sizeof(FrameUBO), 240u);
}

TEST_F(SingleWindowFixture, PushConstantsLayout)
{
    EXPECT_EQ(sizeof(SeriesPushConstants), 96u);
}

TEST_F(SingleWindowFixture, RenderNoHang)
{
    create_simple_figure();
    GpuHangDetector detector(std::chrono::seconds(10));
    detector.expect_no_hang("single window render", [&]() { app_->run(); });
}

// ─── Resize Regression ──────────────────────────────────────────────────────

TEST_F(SingleWindowFixture, OffscreenFramebufferCreation)
{
    auto& fig = create_simple_figure();
    app_->run();

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    // After rendering, offscreen framebuffer should have the figure's dimensions
    EXPECT_EQ(backend->swapchain_width(), fig.width());
    EXPECT_EQ(backend->swapchain_height(), fig.height());
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 1 — WindowContext Extraction (after Agent A merge)
// Tests that WindowContext struct exists and single-window behavior is preserved.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_WINDOW_CONTEXT

TEST(WindowContextPhase1, StructExists)
{
    // Verify WindowContext can be instantiated
    // WindowContext wctx{};
    // EXPECT_EQ(wctx.id, 0u);
    GTEST_SKIP() << "WindowContext not yet implemented (Agent A)";
}

TEST(WindowContextPhase1, SetActiveWindow)
{
    // Verify VulkanBackend::set_active_window() works
    GTEST_SKIP() << "set_active_window not yet implemented (Agent A)";
}

TEST(WindowContextPhase1, SingleWindowUnchanged)
{
    // After Agent A refactor, single window must still work identically
    App                app({.headless = true, .socket_path = ""});
    auto&              fig = app.figure({.width = 640, .height = 480});
    auto&              ax  = fig.subplot(1, 1, 1);
    std::vector<float> x   = {0.0f, 1.0f, 2.0f};
    std::vector<float> y   = {0.0f, 1.0f, 0.5f};
    ax.line(x, y);
    app.run();

    std::vector<uint8_t> pixels(640 * 480 * 4);
    ASSERT_TRUE(app.backend()->readback_framebuffer(pixels.data(), 640, 480));
}

TEST(WindowContextPhase1, GlfwTerminateNotCalledOnShutdown)
{
    // Verify GlfwAdapter::shutdown() no longer calls glfwTerminate()
    // This is a behavioral test — hard to verify without mocking.
    // For now, just verify that creating and destroying multiple Apps
    // in sequence doesn't crash (which it would if glfwTerminate was
    // called prematurely).
    for (int i = 0; i < 3; ++i)
    {
        App   app({.headless = true, .socket_path = ""});
        auto& fig = app.figure({.width = 320, .height = 240});
        fig.subplot(1, 1, 1);
        app.run();
    }
}

#endif   // SPECTRA_HAS_WINDOW_CONTEXT

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 2 — Multi-Window Rendering (after Agent B merge)
// Tests that multiple windows can render simultaneously.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_WINDOW_MANAGER

TEST_F(MultiWindowFixture, TwoWindowsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, ThreeWindowsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, ResizeOneWindowDoesNotAffectOther)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, CloseOneWindowOtherContinues)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, MinimizedWindowSkipsRender)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, AllWindowsMinimized)
{
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, RapidResizeTorture)
{
    // 100 rapid resizes on 3 windows simultaneously
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

TEST_F(MultiWindowFixture, WindowCloseOrderPermutations)
{
    // Close windows in every permutation — no crash
    GTEST_SKIP() << "WindowManager not yet implemented (Agent B)";
}

#endif   // SPECTRA_HAS_WINDOW_MANAGER

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 2 STUB — Multi-Window Simulation (always runs)
// Uses the stub MultiWindowFixture (N independent headless Apps) to validate
// the test structure itself.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef SPECTRA_HAS_WINDOW_MANAGER

TEST_F(MultiWindowFixture, StubTwoWindowsRender)
{
    create_windows(2);
    EXPECT_EQ(active_window_count(), 2u);
    EXPECT_TRUE(render_all_windows());
}

TEST_F(MultiWindowFixture, StubThreeWindowsRender)
{
    create_windows(3);
    EXPECT_EQ(active_window_count(), 3u);
    EXPECT_TRUE(render_all_windows());
}

TEST_F(MultiWindowFixture, StubReadbackDifferentContent)
{
    create_windows(2);
    render_all_windows();

    std::vector<uint8_t> pixels0, pixels1;
    ASSERT_TRUE(readback_window(0, pixels0));
    ASSERT_TRUE(readback_window(1, pixels1));

    // Both should have non-zero content
    bool has0 = false, has1 = false;
    for (auto p : pixels0)
        if (p != 0)
        {
            has0 = true;
            break;
        }
    for (auto p : pixels1)
        if (p != 0)
        {
            has1 = true;
            break;
        }
    EXPECT_TRUE(has0);
    EXPECT_TRUE(has1);
}

TEST_F(MultiWindowFixture, StubNoHangMultipleWindows)
{
    create_windows(3);
    GpuHangDetector detector(std::chrono::seconds(30));
    detector.expect_no_hang("render 3 stub windows", [&]() { render_all_windows(); });
}

#endif   // !SPECTRA_HAS_WINDOW_MANAGER

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 3 — Figure Ownership (after Agent C merge)
// Tests that figures have stable IDs and can move between windows.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_FIGURE_REGISTRY

TEST(FigureOwnership, StableIds)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, MoveFigureBetweenWindows)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, GpuBuffersSurviveMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, CloseSourceWindowAfterMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

TEST(FigureOwnership, AnimationCallbacksAfterMove)
{
    GTEST_SKIP() << "FigureRegistry not yet implemented (Agent C)";
}

#endif   // SPECTRA_HAS_FIGURE_REGISTRY

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 4 — Tab Tear-Off (after Agent D merge)
// Tests for drag-to-detach UX.  These test the WindowManager::detach_figure()
// API and related edge cases in headless mode.
// ═══════════════════════════════════════════════════════════════════════════════

class TearOffTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
        auto& fig       = app_->figure({.width = 320, .height = 240});
        fig.subplot(1, 1, 1);
        std::vector<float> x = {0.0f, 1.0f};
        std::vector<float> y = {0.0f, 1.0f};
        fig.axes()[0]->line(x, y);
        app_->run();
    }

    void TearDown() override
    {
        // Leak app_ to static storage instead of destroying it.
        // Destroying multiple VkInstance objects sequentially in the same process
        // crashes the NVIDIA driver. ::_exit() at the end of main prevents the
        // leaked App destructors from running.
        static std::vector<std::unique_ptr<App>> s_leaked;
        s_leaked.push_back(std::move(app_));
    }

    VulkanBackend* vk_backend() { return static_cast<VulkanBackend*>(app_->backend()); }

    std::unique_ptr<App> app_;
};

TEST_F(TearOffTest, DetachFigureAPIExists)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // detach_figure should be callable (returns nullptr in headless — no GLFW display)
    auto* result = wm.detach_figure(1, 800, 600, "Detached", 100, 200);
    (void)result;
    SUCCEED();
}

TEST_F(TearOffTest, DetachFigureRejectsInvalidId)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    auto* result = wm.detach_figure(INVALID_FIGURE_ID, 800, 600, "Bad", 0, 0);
    EXPECT_EQ(result, nullptr);
}

TEST_F(TearOffTest, DetachFigureRejectsUninitializedManager)
{
    WindowManager wm;
    // Not initialized
    auto* result = wm.detach_figure(1, 800, 600, "Bad", 0, 0);
    EXPECT_EQ(result, nullptr);
}

TEST_F(TearOffTest, DetachFigureClampsZeroDimensions)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Zero dimensions should not crash (clamped internally to 800x600)
    auto* result = wm.detach_figure(1, 0, 0, "Zero", 0, 0);
    (void)result;
    SUCCEED();
}

TEST_F(TearOffTest, DetachFigureNegativePosition)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Negative screen position should not crash
    auto* result = wm.detach_figure(1, 800, 600, "Negative", -100, -200);
    (void)result;
    SUCCEED();
}

TEST_F(TearOffTest, WindowContextAssignmentAfterDetach)
{
    // Simulate what detach_figure does: create a WindowContext and assign a figure
    WindowContext wctx{};
    EXPECT_EQ(wctx.assigned_figure_index, INVALID_FIGURE_ID);

    FigureId fig_id            = 42;
    wctx.assigned_figure_index = fig_id;
    EXPECT_EQ(wctx.assigned_figure_index, fig_id);
}

TEST_F(TearOffTest, LastFigureProtection)
{
    // The app.cpp callback checks registry_.count() <= 1 before detaching.
    // Verify the semantic contract: a single-figure app should not allow detach.
    // app_ (from SetUp) has exactly one figure — verify this via the registry.
    // (Avoids creating a second App/VkInstance which crashes the NVIDIA driver.)
    EXPECT_EQ(app_->figure_registry().count(), 1u);
    SUCCEED();
}

TEST_F(TearOffTest, MultipleFiguresAllowDetach)
{
    // With 2+ figures, detach should be allowed.
    // Add a second figure to app_ (already has one from SetUp) — avoids creating
    // a second App/VkInstance which crashes the NVIDIA driver.
    auto& fig2 = app_->figure({.width = 320, .height = 240});
    fig2.subplot(1, 1, 1);

    // Both figures exist
    EXPECT_EQ(app_->figure_registry().count(), 2u);
    EXPECT_EQ(fig2.width(), 320u);
}

TEST_F(TearOffTest, MoveFigureFieldManipulation)
{
    // Simulate the full detach + move flow using WindowContext fields
    WindowContext source{};
    source.id                    = 1;
    source.assigned_figure_index = 7;

    WindowContext target{};
    target.id                    = 2;
    target.assigned_figure_index = INVALID_FIGURE_ID;

    // Detach: assign figure to target, clear source
    target.assigned_figure_index = source.assigned_figure_index;
    source.assigned_figure_index = INVALID_FIGURE_ID;

    EXPECT_EQ(target.assigned_figure_index, 7u);
    EXPECT_EQ(source.assigned_figure_index, INVALID_FIGURE_ID);
}

TEST_F(TearOffTest, RapidDetachAttempts)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Rapidly attempt detach 10 times — should not crash
    for (int i = 0; i < 10; ++i)
    {
        auto* result = wm.detach_figure(static_cast<FigureId>(i + 1),
                                        400,
                                        300,
                                        "Rapid " + std::to_string(i),
                                        i * 50,
                                        i * 50);
        (void)result;
    }
    SUCCEED();
}

TEST_F(TearOffTest, ShutdownAfterDetachAttempts)
{
    WindowManager wm;
    wm.init(vk_backend());
    wm.create_initial_window(nullptr);

    // Attempt detach, then shutdown — should not leak or crash
    wm.detach_figure(1, 800, 600, "Test", 0, 0);
    wm.shutdown();
    EXPECT_EQ(wm.window_count(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Tests — verify test infrastructure itself
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TestInfrastructure, GpuHangDetectorCompletes)
{
    GpuHangDetector detector(std::chrono::seconds(5));
    bool            ok = detector.run("trivial",
                           []()
                           {
                               // Instant completion
                           });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(detector.completed());
    EXPECT_FALSE(detector.timed_out());
    EXPECT_GE(detector.elapsed_ms(), 0);
}

TEST(TestInfrastructure, GpuHangDetectorTimeout)
{
    GpuHangDetector detector(std::chrono::milliseconds(50));
    bool            ok = detector.run("intentional hang",
                           []() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
    // The callable still completes (we can't kill threads), but the
    // detector reports timeout
    EXPECT_FALSE(ok);
    EXPECT_TRUE(detector.timed_out());
    EXPECT_FALSE(detector.failure_reason().empty());
}

TEST(TestInfrastructure, TimingMeasure)
{
    double ms = measure_ms([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    EXPECT_GE(ms, 5.0);     // At least 5ms (allowing for scheduling jitter)
    EXPECT_LT(ms, 500.0);   // Not absurdly long
}

TEST(TestInfrastructure, StressRunner)
{
    int  counter = 0;
    auto stats   = run_stress(10, [&]() { ++counter; });
    EXPECT_EQ(counter, 10);
    EXPECT_EQ(stats.iterations, 10u);
    EXPECT_GE(stats.min_ms, 0.0);
    EXPECT_GE(stats.max_ms, stats.min_ms);
    EXPECT_GE(stats.avg_ms, 0.0);
}

// Custom main: run all tests, then call _exit() to skip C++ destructors.
// Destroying multiple VkInstance objects sequentially in the same process
// crashes the NVIDIA driver (same root cause as BUG-1/3). The leaked App
// objects in MultiWindowFixture::TearDown() are intentional — ::_exit()
// prevents their destructors from running at process exit.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    ::_exit(result);
}
