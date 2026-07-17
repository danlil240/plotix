#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>

#include "ui/input/input.hpp"

using namespace spectra;

// Helper: create a figure with one axes and known limits/viewport
class InputHandlerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_     = std::make_unique<Figure>(FigureConfig{800, 600});
        auto& ax = fig_->subplot(1, 1, 1);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(0.0f, 10.0f);
        fig_->compute_layout();

        handler_.set_figure(fig_.get());
        handler_.set_active_axes(&ax);
        auto& vp = ax.viewport();
        handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
    }

    Axes& axes() { return *fig_->axes()[0]; }

    std::unique_ptr<Figure> fig_;
    InputHandler            handler_;
};

// ─── screen_to_data ─────────────────────────────────────────────────────────

TEST_F(InputHandlerTest, ScreenToDataCenter)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w / 2.0f;
    float cy = vp.y + vp.h / 2.0f;

    double dx, dy;
    handler_.screen_to_data(cx, cy, dx, dy);

    EXPECT_NEAR(dx, 5.0, 0.1);
    EXPECT_NEAR(dy, 5.0, 0.1);
}

TEST_F(InputHandlerTest, ScreenToDataTopLeft)
{
    auto& vp = axes().viewport();

    double dx, dy;
    handler_.screen_to_data(vp.x, vp.y, dx, dy);

    // Top-left of viewport = data (xmin, ymax) because screen Y is inverted
    EXPECT_NEAR(dx, 0.0, 0.1);
    EXPECT_NEAR(dy, 10.0, 0.1);
}

TEST_F(InputHandlerTest, ScreenToDataBottomRight)
{
    auto& vp = axes().viewport();

    double dx, dy;
    handler_.screen_to_data(vp.x + vp.w, vp.y + vp.h, dx, dy);

    EXPECT_NEAR(dx, 10.0, 0.1);
    EXPECT_NEAR(dy, 0.0, 0.1);
}

// ─── Pan ────────────────────────────────────────────────────────────────────

TEST_F(InputHandlerTest, PanIgnoredOutsideViewport)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;

    handler_.set_tool_mode(ToolMode::Pan);
    handler_.set_active_axes(&axes());

    auto xlim_before = axes().x_limits();

    // Press in the top margin gutter (title area), not inside the data viewport.
    handler_.on_mouse_button(0, 1, 0, cx, vp.y - 10.0f);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);
    EXPECT_EQ(axes().x_limits().min, xlim_before.min);
    EXPECT_EQ(axes().x_limits().max, xlim_before.max);
}

TEST_F(InputHandlerTest, ScrollIgnoredOutsideViewport)
{
    auto& vp          = axes().viewport();
    auto  xlim_before = axes().x_limits();

    handler_.set_active_axes(&axes());
    handler_.on_scroll(0.0, 1.0, vp.x + vp.w * 0.5f, vp.y - 10.0f);

    EXPECT_EQ(axes().x_limits().min, xlim_before.min);
    EXPECT_EQ(axes().x_limits().max, xlim_before.max);
}

TEST_F(InputHandlerTest, PanMovesLimits)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w / 2.0f;
    float cy = vp.y + vp.h / 2.0f;

    // Set pan tool mode
    handler_.set_tool_mode(ToolMode::Pan);

    // Press left button at center
    handler_.on_mouse_button(0, 1, 0, cx, cy);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);

    // Drag right by 10% of viewport width
    float drag_x = cx + vp.w * 0.1f;
    handler_.on_mouse_move(drag_x, cy);

    // Release
    handler_.on_mouse_button(0, 0, 0, drag_x, cy);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    // X limits should have shifted left (dragging right = panning left in data space)
    auto xlim = axes().x_limits();
    EXPECT_LT(xlim.min, 0.0f);
    EXPECT_LT(xlim.max, 10.0f);

    // Y limits should be unchanged
    auto ylim = axes().y_limits();
    EXPECT_NEAR(ylim.min, 0.0f, 0.01f);
    EXPECT_NEAR(ylim.max, 10.0f, 0.01f);
}

// ─── Scroll zoom ────────────────────────────────────────────────────────────

TEST_F(InputHandlerTest, ScrollZoomIn)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w / 2.0f;
    float cy = vp.y + vp.h / 2.0f;

    auto xlim_before = axes().x_limits();
    // auto ylim_before = axes().y_limits();  // Currently unused

    // Scroll up = zoom in
    handler_.on_scroll(0.0, 1.0, cx, cy);

    // Advance smoothing to settle (exponential decay needs several frames)
    for (int i = 0; i < 30; ++i)
        handler_.update(1.0f / 60.0f);

    auto xlim_after = axes().x_limits();
    // auto ylim_after = axes().y_limits();  // Currently unused

    // Range should shrink
    float range_before = xlim_before.max - xlim_before.min;
    float range_after  = xlim_after.max - xlim_after.min;
    EXPECT_LT(range_after, range_before);
}

TEST_F(InputHandlerTest, ScrollZoomOut)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w / 2.0f;
    float cy = vp.y + vp.h / 2.0f;

    auto xlim_before = axes().x_limits();

    // Scroll down = zoom out
    handler_.on_scroll(0.0, -1.0, cx, cy);

    // Advance smoothing to settle (exponential decay needs several frames)
    for (int i = 0; i < 30; ++i)
        handler_.update(1.0f / 60.0f);

    auto xlim_after = axes().x_limits();

    float range_before = xlim_before.max - xlim_before.min;
    float range_after  = xlim_after.max - xlim_after.min;
    EXPECT_GT(range_after, range_before);
}

TEST_F(InputHandlerTest, RightDragExtendsPresentedBuffer)
{
    auto& line = axes().line();
    for (int i = 0; i <= 200; ++i)
    {
        float x = static_cast<float>(i) * 0.1f;
        line.append(x, std::sin(x));
    }

    axes().presented_buffer(5.0f);
    EXPECT_TRUE(axes().has_presented_buffer());
    float before_seconds = axes().presented_buffer_seconds();

    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    // Right-drag left should expand zoom range (increase buffer duration)
    handler_.on_mouse_button(1, 1, 0, cx, cy);
    handler_.on_mouse_move(cx - 40.0, cy);
    handler_.on_mouse_button(1, 0, 0, cx - 40.0, cy);

    EXPECT_TRUE(axes().has_presented_buffer());
    EXPECT_GT(axes().presented_buffer_seconds(), before_seconds);
}

TEST_F(InputHandlerTest, RightDragZoomStableAtEpochScaleLimits)
{
    // Regression: with epoch-scale x limits (~1.78e9), the drag-start anchor
    // must be captured in double precision.  A float anchor quantizes to
    // ~128 s steps, flinging the view on the first zoom step.
    constexpr double epoch = 1783350621.752999;
    axes().xlim(epoch, epoch + 0.04);
    axes().ylim(0.0f, 10.0f);

    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    const double anchor_expected = epoch + 0.02;   // data x at viewport center

    // Right-drag right along X → zoom in around the anchor
    handler_.on_mouse_button(1, 1, 0, cx, cy);
    handler_.on_mouse_move(cx + 40.0, cy);
    handler_.on_mouse_button(1, 0, 0, cx + 40.0, cy);

    auto xlim = axes().x_limits();
    // Range must shrink (zoom in) and stay sub-second
    EXPECT_LT(xlim.max - xlim.min, 0.04);
    EXPECT_GT(xlim.max - xlim.min, 0.0);
    // Anchor must remain inside the new limits (no view fling)
    EXPECT_GT(anchor_expected, xlim.min);
    EXPECT_LT(anchor_expected, xlim.max);
    // Limits stay in the immediate neighborhood of the anchor
    EXPECT_NEAR(xlim.min, anchor_expected, 0.05);
    EXPECT_NEAR(xlim.max, anchor_expected, 0.05);
}

TEST_F(InputHandlerTest, ScrollZoomStableAtEpochScaleLimits)
{
    // Same regression for wheel zoom: cursor anchor in double precision.
    constexpr double epoch = 1783350621.752999;
    axes().xlim(epoch, epoch + 0.04);
    axes().ylim(0.0f, 10.0f);

    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    const double anchor_expected = epoch + 0.02;

    handler_.on_scroll(0.0, 1.0, cx, cy);
    for (int i = 0; i < 30; ++i)
        handler_.update(1.0f / 60.0f);

    auto xlim = axes().x_limits();
    EXPECT_LT(xlim.max - xlim.min, 0.04);
    EXPECT_GT(anchor_expected, xlim.min);
    EXPECT_LT(anchor_expected, xlim.max);
}

TEST_F(InputHandlerTest, ScrollAdjustsPresentedBufferWithoutPausingFollow)
{
    auto& line = axes().line();
    for (int i = 0; i <= 200; ++i)
    {
        float x = static_cast<float>(i) * 0.1f;
        line.append(x, std::sin(x));
    }

    axes().presented_buffer(5.0f);
    float before_seconds = axes().presented_buffer_seconds();

    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    handler_.on_scroll(0.0, 1.0, cx, cy);

    EXPECT_TRUE(axes().is_presented_buffer_following());
    EXPECT_LT(axes().presented_buffer_seconds(), before_seconds);
}

TEST_F(InputHandlerTest, PresentedBufferCanFollowExplicitClock)
{
    axes().presented_buffer(5.0f);
    axes().set_presented_buffer_right_edge(42.0);

    auto xlim = axes().x_limits();
    EXPECT_DOUBLE_EQ(xlim.min, 37.0);
    EXPECT_DOUBLE_EQ(xlim.max, 42.0);
}

TEST_F(InputHandlerTest, RightDragTwentyDegreesIsMostlyX)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    auto  xlim_before   = axes().x_limits();
    auto  ylim_before   = axes().y_limits();
    float xrange_before = xlim_before.max - xlim_before.min;
    float yrange_before = ylim_before.max - ylim_before.min;

    // ~20 degrees from horizontal: both axes zoom, but X should change more than Y.
    handler_.on_mouse_button(1, 1, 0, cx, cy);
    handler_.on_mouse_move(cx - 50.0, cy + 18.0);
    handler_.on_mouse_button(1, 0, 0, cx - 50.0, cy + 18.0);

    auto  xlim_after   = axes().x_limits();
    auto  ylim_after   = axes().y_limits();
    float xrange_after = xlim_after.max - xlim_after.min;
    float yrange_after = ylim_after.max - ylim_after.min;

    float dx_range = xrange_after - xrange_before;
    float dy_range = yrange_after - yrange_before;
    EXPECT_GT(dx_range, 0.0f);
    EXPECT_GT(dy_range, 0.0f);
    EXPECT_GT(dx_range, dy_range);
}

TEST_F(InputHandlerTest, RightDragSeventyDegreesIsMostlyY)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w * 0.5f;
    float cy = vp.y + vp.h * 0.5f;

    auto  xlim_before   = axes().x_limits();
    auto  ylim_before   = axes().y_limits();
    float xrange_before = xlim_before.max - xlim_before.min;
    float yrange_before = ylim_before.max - ylim_before.min;

    // ~70 degrees from horizontal: both axes zoom, but Y should change more than X.
    handler_.on_mouse_button(1, 1, 0, cx, cy);
    handler_.on_mouse_move(cx - 18.0, cy + 50.0);
    handler_.on_mouse_button(1, 0, 0, cx - 18.0, cy + 50.0);

    auto  xlim_after   = axes().x_limits();
    auto  ylim_after   = axes().y_limits();
    float xrange_after = xlim_after.max - xlim_after.min;
    float yrange_after = ylim_after.max - ylim_after.min;

    float dx_range = xrange_after - xrange_before;
    float dy_range = yrange_after - yrange_before;
    EXPECT_GT(dx_range, 0.0f);
    EXPECT_GT(dy_range, 0.0f);
    EXPECT_GT(dy_range, dx_range);
}

TEST_F(InputHandlerTest, RightDragIgnoredFromLeftGutter)
{
    auto& vp      = axes().viewport();
    float press_x = vp.x - 24.0f;
    float press_y = vp.y + vp.h * 0.5f;

    auto ylim_before = axes().y_limits();

    handler_.set_active_axes(&axes());
    handler_.on_mouse_button(1, 1, 0, press_x, press_y);
    handler_.on_mouse_move(press_x, press_y + 50.0f);
    handler_.on_mouse_button(1, 0, 0, press_x, press_y + 50.0f);

    EXPECT_EQ(axes().y_limits().min, ylim_before.min);
    EXPECT_EQ(axes().y_limits().max, ylim_before.max);
}

// ─── Box zoom ───────────────────────────────────────────────────────────────

TEST_F(InputHandlerTest, BoxZoomSetsLimits)
{
    auto& vp = axes().viewport();

    // Set box zoom tool mode
    handler_.set_tool_mode(ToolMode::BoxZoom);

    // Right-click press at 25% from top-left
    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    handler_.on_mouse_button(0, 1, 0, x0, y0);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);

    // Drag to 75% from top-left
    double x1 = vp.x + vp.w * 0.75;
    double y1 = vp.y + vp.h * 0.75;
    handler_.on_mouse_move(x1, y1);

    // Release
    handler_.on_mouse_button(0, 0, 0, x1, y1);
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    // Limits should now be approximately [2.5, 7.5] × [2.5, 7.5]
    auto xlim = axes().x_limits();
    auto ylim = axes().y_limits();
    EXPECT_NEAR(xlim.min, 2.5f, 0.5f);
    EXPECT_NEAR(xlim.max, 7.5f, 0.5f);
    EXPECT_NEAR(ylim.min, 2.5f, 0.5f);
    EXPECT_NEAR(ylim.max, 7.5f, 0.5f);
}

TEST_F(InputHandlerTest, BoxZoomCancelledByEscape)
{
    auto& vp = axes().viewport();

    // Set box zoom tool mode
    handler_.set_tool_mode(ToolMode::BoxZoom);

    double x0 = vp.x + vp.w * 0.25;
    double y0 = vp.y + vp.h * 0.25;
    handler_.on_mouse_button(0, 1, 0, x0, y0);
    EXPECT_EQ(handler_.mode(), InteractionMode::Dragging);

    // Press Escape
    handler_.on_key(256, 1, 0);   // KEY_ESCAPE = 256
    EXPECT_EQ(handler_.mode(), InteractionMode::Idle);

    // Limits should be unchanged
    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 0.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 10.0f, 0.01f);
}

TEST_F(InputHandlerTest, BoxZoomTooSmallIgnored)
{
    auto& vp = axes().viewport();
    handler_.set_tool_mode(ToolMode::BoxZoom);

    // Left-click press in BoxZoom mode
    double x0 = vp.x + vp.w * 0.5;
    double y0 = vp.y + vp.h * 0.5;
    handler_.on_mouse_button(0, 1, 0, x0, y0);

    // Drag only 2 pixels (below MIN_SELECTION_PIXELS threshold)
    handler_.on_mouse_move(x0 + 2.0, y0 + 2.0);
    handler_.on_mouse_button(0, 0, 0, x0 + 2.0, y0 + 2.0);

    // Limits should be unchanged
    auto xlim = axes().x_limits();
    EXPECT_NEAR(xlim.min, 0.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 10.0f, 0.01f);
}

// ─── Keyboard shortcuts ─────────────────────────────────────────────────────

TEST_F(InputHandlerTest, ResetViewAutoFits)
{
    // Zoom in first
    axes().xlim(3.0f, 7.0f);
    axes().ylim(3.0f, 7.0f);

    // Press 'r' (KEY_R = 82)
    handler_.on_key(82, 1, 0);

    // auto_fit() clears manual limits; with no series data, falls back to
    // default extent [0,1] + 5% padding = [-0.05, 1.05]. Verify limits
    // changed from the zoomed [3,7] state.
    auto xlim = axes().x_limits();
    EXPECT_NE(xlim.min, 3.0f);
    EXPECT_NE(xlim.max, 7.0f);
}

TEST_F(InputHandlerTest, ToggleGrid)
{
    EXPECT_TRUE(axes().grid_enabled());

    // Press 'g' (KEY_G = 71)
    handler_.on_key(71, 1, 0);
    EXPECT_FALSE(axes().grid_enabled());

    handler_.on_key(71, 1, 0);
    EXPECT_TRUE(axes().grid_enabled());
}

// ─── Cursor readout ─────────────────────────────────────────────────────────

TEST_F(InputHandlerTest, CursorReadoutUpdatesOnMove)
{
    auto& vp = axes().viewport();
    float cx = vp.x + vp.w / 2.0f;
    float cy = vp.y + vp.h / 2.0f;

    handler_.on_mouse_move(cx, cy);

    auto& readout = handler_.cursor_readout();
    EXPECT_TRUE(readout.valid);
    EXPECT_NEAR(readout.data_x, 5.0f, 0.5f);
    EXPECT_NEAR(readout.data_y, 5.0f, 0.5f);
}

TEST_F(InputHandlerTest, CursorReadoutInvalidOutsideViewport)
{
    // Move cursor way outside the viewport
    handler_.on_mouse_move(-100.0, -100.0);

    auto& readout = handler_.cursor_readout();
    EXPECT_FALSE(readout.valid);
}

// ─── Multi-axes hit-testing ─────────────────────────────────────────────────

class MultiAxesInputTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        fig_      = std::make_unique<Figure>(FigureConfig{800, 600});
        auto& ax1 = fig_->subplot(1, 2, 1);
        auto& ax2 = fig_->subplot(1, 2, 2);
        ax1.xlim(0.0f, 10.0f);
        ax1.ylim(0.0f, 10.0f);
        ax2.xlim(100.0f, 200.0f);
        ax2.ylim(100.0f, 200.0f);
        fig_->compute_layout();

        handler_.set_figure(fig_.get());
    }

    std::unique_ptr<Figure> fig_;
    InputHandler            handler_;
};

TEST_F(MultiAxesInputTest, ClickSelectsCorrectAxes)
{
    auto& ax1 = *fig_->axes()[0];
    auto& ax2 = *fig_->axes()[1];
    auto& vp1 = ax1.viewport();
    auto& vp2 = ax2.viewport();

    // Click in center of first axes
    handler_.on_mouse_button(0, 1, 0, vp1.x + vp1.w / 2.0, vp1.y + vp1.h / 2.0);
    EXPECT_EQ(handler_.active_axes(), &ax1);
    handler_.on_mouse_button(0, 0, 0, vp1.x + vp1.w / 2.0, vp1.y + vp1.h / 2.0);

    // Click in center of second axes
    handler_.on_mouse_button(0, 1, 0, vp2.x + vp2.w / 2.0, vp2.y + vp2.h / 2.0);
    EXPECT_EQ(handler_.active_axes(), &ax2);
    handler_.on_mouse_button(0, 0, 0, vp2.x + vp2.w / 2.0, vp2.y + vp2.h / 2.0);
}

TEST_F(MultiAxesInputTest, ScrollZoomsCorrectAxes)
{
    auto& ax1 = *fig_->axes()[0];
    auto& ax2 = *fig_->axes()[1];
    auto& vp2 = ax2.viewport();

    auto xlim1_before = ax1.x_limits();
    auto xlim2_before = ax2.x_limits();

    // Scroll over second axes
    handler_.on_scroll(0.0, 1.0, vp2.x + vp2.w / 2.0, vp2.y + vp2.h / 2.0);

    // Advance smoothing to settle (exponential decay needs several frames)
    for (int i = 0; i < 30; ++i)
        handler_.update(1.0f / 60.0f);

    auto xlim1_after = ax1.x_limits();
    auto xlim2_after = ax2.x_limits();

    // First axes should be unchanged
    EXPECT_FLOAT_EQ(xlim1_after.min, xlim1_before.min);
    EXPECT_FLOAT_EQ(xlim1_after.max, xlim1_before.max);

    // Second axes should have zoomed
    float range_before = xlim2_before.max - xlim2_before.min;
    float range_after  = xlim2_after.max - xlim2_after.min;
    EXPECT_LT(range_after, range_before);
}

TEST(InputHandler3DTest, CursorReadoutValidOverSubplot3d)
{
    Figure fig(FigureConfig{800, 600});
    auto&  ax = fig.subplot3d(1, 1, 1);
    ax.xlim(-1.0, 1.0);
    ax.ylim(-1.0, 1.0);
    ax.zlim(-1.0, 1.0);
    fig.compute_layout();

    InputHandler handler;
    handler.set_figure(&fig);

    const auto& vp = ax.viewport();
    handler.on_mouse_move(vp.x + vp.w * 0.5, vp.y + vp.h * 0.5);

    const auto& readout = handler.cursor_readout();
    EXPECT_TRUE(readout.valid);
    EXPECT_DOUBLE_EQ(readout.screen_x, vp.x + vp.w * 0.5);
    EXPECT_DOUBLE_EQ(readout.screen_y, vp.y + vp.h * 0.5);
}
