#include <gtest/gtest.h>

#include "ui/layout/layout_manager.hpp"

using namespace spectra;

// ─── Basic Zone Computation ─────────────────────────────────────────────────

TEST(LayoutManager, DefaultZonesAt1280x720)
{
    LayoutManager lm;
    lm.update(1280.0f, 720.0f);

    // Command bar spans full width at top
    auto cb = lm.command_bar_rect();
    EXPECT_FLOAT_EQ(cb.x, 0.0f);
    EXPECT_FLOAT_EQ(cb.y, 0.0f);
    EXPECT_FLOAT_EQ(cb.w, 1280.0f);
    EXPECT_FLOAT_EQ(cb.h, LayoutManager::COMMAND_BAR_HEIGHT);

    // Status bar spans full width at bottom
    auto sb = lm.status_bar_rect();
    EXPECT_FLOAT_EQ(sb.x, 0.0f);
    EXPECT_FLOAT_EQ(sb.y, 720.0f - LayoutManager::STATUS_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(sb.w, 1280.0f);
    EXPECT_FLOAT_EQ(sb.h, LayoutManager::STATUS_BAR_HEIGHT);

    // Nav rail: collapsed by default (48px)
    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.x, 0.0f);
    EXPECT_FLOAT_EQ(nr.y, LayoutManager::COMMAND_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(nr.w, LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);
    float content_h = 720.0f - LayoutManager::COMMAND_BAR_HEIGHT - LayoutManager::STATUS_BAR_HEIGHT;
    EXPECT_FLOAT_EQ(nr.h, content_h);

    // Inspector: hidden by default (width 0)
    EXPECT_FALSE(lm.is_inspector_visible());
    auto insp = lm.inspector_rect();
    EXPECT_FLOAT_EQ(insp.w, 0.0f);

    // Canvas starts after nav toolbar inset
    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, LayoutManager::NAV_TOOLBAR_INSET);
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f - LayoutManager::NAV_TOOLBAR_INSET);
    EXPECT_FLOAT_EQ(cv.h, content_h);
}

// ─── Inspector Visibility ───────────────────────────────────────────────────

TEST(LayoutManager, InspectorOpenReducesCanvas)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // dt=0 snaps instantly

    auto insp = lm.inspector_rect();
    EXPECT_GT(insp.w, 0.0f);
    EXPECT_FLOAT_EQ(insp.w, LayoutManager::INSPECTOR_DEFAULT_WIDTH);

    auto  cv = lm.canvas_rect();
    float expected_canvas_w =
        1280.0f - LayoutManager::NAV_TOOLBAR_INSET - LayoutManager::INSPECTOR_DEFAULT_WIDTH;
    EXPECT_FLOAT_EQ(cv.w, expected_canvas_w);

    // Inspector is on the right edge
    EXPECT_FLOAT_EQ(insp.x + insp.w, 1280.0f);
}

TEST(LayoutManager, InspectorCloseExpandsCanvas)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // snap open

    lm.set_inspector_visible(false);
    lm.update(1280.0f, 720.0f);   // snap closed (dt=0)

    auto  cv         = lm.canvas_rect();
    float expected_w = 1280.0f - LayoutManager::NAV_TOOLBAR_INSET;
    EXPECT_FLOAT_EQ(cv.w, expected_w);
}

// ─── Inspector Resize ───────────────────────────────────────────────────────

TEST(LayoutManager, InspectorWidthClamped)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);

    // Below minimum
    lm.set_inspector_width(100.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_MIN_WIDTH);

    // Above maximum
    lm.set_inspector_width(1000.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_MAX_WIDTH);

    // Within range
    lm.set_inspector_width(350.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), 350.0f);
}

TEST(LayoutManager, InspectorResizeActiveSnaps)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // snap open

    // Simulate active drag: animated width should snap immediately
    lm.set_inspector_resize_active(true);
    lm.set_inspector_width(400.0f);

    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), 400.0f);
}

TEST(LayoutManager, ResetInspectorWidth)
{
    LayoutManager lm;
    lm.set_inspector_width(400.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), 400.0f);

    lm.reset_inspector_width();
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_DEFAULT_WIDTH);
}

// ─── Nav Rail Expand/Collapse ───────────────────────────────────────────────

TEST(LayoutManager, NavRailExpandedWidth)
{
    LayoutManager lm;
    EXPECT_FALSE(lm.is_nav_rail_expanded());
    EXPECT_FLOAT_EQ(lm.nav_rail_width(), LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);

    lm.set_nav_rail_expanded(true);
    lm.update(1280.0f, 720.0f);   // snap (dt=0)

    EXPECT_TRUE(lm.is_nav_rail_expanded());
    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, LayoutManager::NAV_RAIL_EXPANDED_WIDTH);

    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, LayoutManager::NAV_RAIL_EXPANDED_WIDTH);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f - LayoutManager::NAV_RAIL_EXPANDED_WIDTH);
}

TEST(LayoutManager, NavRailHiddenExpandsCanvas)
{
    LayoutManager lm;
    lm.set_nav_rail_visible(false);
    lm.update(1280.0f, 720.0f);

    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, 0.0f);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f);

    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, 0.0f);
}

TEST(LayoutManager, NavRailCustomWidth)
{
    LayoutManager lm;
    lm.set_nav_rail_width(180.0f);
    lm.set_nav_rail_expanded(true);
    lm.update(1280.0f, 720.0f);

    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, 180.0f);
}

// ─── Tab Bar ────────────────────────────────────────────────────────────────

TEST(LayoutManager, TabBarHiddenByDefault)
{
    LayoutManager lm;
    lm.update(1280.0f, 720.0f);

    EXPECT_FALSE(lm.is_tab_bar_visible());
    auto tb = lm.tab_bar_rect();
    EXPECT_FLOAT_EQ(tb.w, 0.0f);
    EXPECT_FLOAT_EQ(tb.h, 0.0f);
}

TEST(LayoutManager, TabBarVisibleOffsetsCanvas)
{
    LayoutManager lm;
    lm.set_tab_bar_visible(true);
    lm.update(1280.0f, 720.0f);

    auto tb = lm.tab_bar_rect();
    EXPECT_GT(tb.w, 0.0f);
    EXPECT_FLOAT_EQ(tb.h, LayoutManager::TAB_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(tb.y, LayoutManager::COMMAND_BAR_HEIGHT);

    // Canvas should be pushed down by tab bar height
    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::TAB_BAR_HEIGHT);

    float content_h = 720.0f - LayoutManager::COMMAND_BAR_HEIGHT - LayoutManager::STATUS_BAR_HEIGHT
                      - LayoutManager::TAB_BAR_HEIGHT;
    EXPECT_FLOAT_EQ(cv.h, content_h);
}

// ─── Animation ──────────────────────────────────────────────────────────────

TEST(LayoutManager, AnimationProgressesWithDt)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);

    // First update with dt=0 snaps
    lm.update(1280.0f, 720.0f, 0.0f);
    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), LayoutManager::INSPECTOR_DEFAULT_WIDTH);

    // Now close and animate with small dt steps
    lm.set_inspector_visible(false);
    lm.update(1280.0f, 720.0f, 0.016f);   // ~1 frame

    // Should be animating (not yet at target)
    float anim_w = lm.inspector_animated_width();
    EXPECT_GT(anim_w, 0.0f);
    EXPECT_LT(anim_w, LayoutManager::INSPECTOR_DEFAULT_WIDTH);
    EXPECT_TRUE(lm.is_animating());
}

TEST(LayoutManager, AnimationConverges)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f, 0.0f);   // snap open

    lm.set_inspector_visible(false);

    // Run many frames — should converge to 0
    for (int i = 0; i < 200; ++i)
    {
        lm.update(1280.0f, 720.0f, 0.016f);
    }

    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), 0.0f);
    EXPECT_FALSE(lm.is_animating());
}

// ─── Window Resize ──────────────────────────────────────────────────────────

TEST(LayoutManager, ZonesAdaptToWindowResize)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1920.0f, 1080.0f);

    auto cb = lm.command_bar_rect();
    EXPECT_FLOAT_EQ(cb.w, 1920.0f);

    auto sb = lm.status_bar_rect();
    EXPECT_FLOAT_EQ(sb.y, 1080.0f - LayoutManager::STATUS_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(sb.w, 1920.0f);

    auto insp = lm.inspector_rect();
    EXPECT_FLOAT_EQ(insp.x + insp.w, 1920.0f);
}

TEST(LayoutManager, SmallWindowClampsToZero)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(100.0f, 100.0f);

    auto cv = lm.canvas_rect();
    EXPECT_GE(cv.w, 0.0f);
    EXPECT_GE(cv.h, 0.0f);
}

// ─── Combined State ─────────────────────────────────────────────────────────

TEST(LayoutManager, AllZonesOpenSimultaneously)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.set_nav_rail_expanded(true);
    lm.set_tab_bar_visible(true);
    lm.update(1920.0f, 1080.0f);

    auto cv   = lm.canvas_rect();
    auto insp = lm.inspector_rect();
    auto tb   = lm.tab_bar_rect();

    const float nav_w = LayoutManager::NAV_RAIL_EXPANDED_WIDTH;
    EXPECT_NEAR(nav_w + cv.w + insp.w, 1920.0f, 1.0f);

    // Tab bar starts at plot grid (canvas + left margin) and is narrower
    EXPECT_FLOAT_EQ(tb.x, nav_w + LayoutManager::PLOT_LEFT_MARGIN);
    EXPECT_FLOAT_EQ(tb.w, cv.w - LayoutManager::PLOT_LEFT_MARGIN);

    EXPECT_FLOAT_EQ(cv.x, nav_w);

    // Canvas starts below tab bar
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::TAB_BAR_HEIGHT);
}

TEST(LayoutManager, NavRailScaleFitsContent)
{
    const float nominal = LayoutManager::nav_rail_nominal_content_height();
    const float min_h   = LayoutManager::nav_rail_min_content_height();

    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(nominal), 1.0f);
    EXPECT_NEAR(LayoutManager::nav_rail_scale_for_height(nominal * 0.75f), 0.75f, 0.01f);
    const float min_scale =
        LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT;
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(min_h), min_scale);
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(min_h - 1.0f), min_scale);
}

TEST(LayoutManager, NavRailScaleUsesItemCounts)
{
    const int buttons = LayoutManager::NAV_RAIL_BUTTON_COUNT;
    const int seps    = LayoutManager::NAV_RAIL_SEPARATOR_COUNT;
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_content_height(buttons, seps, 1.0f),
                    LayoutManager::nav_rail_nominal_content_height());

    const float h = 640.0f;
    EXPECT_LT(
        LayoutManager::nav_rail_scale_for_height(h, buttons, seps),
        LayoutManager::nav_rail_scale_for_height(h,
                                                 LayoutManager::NAV_RAIL_FALLBACK_BUTTON_COUNT,
                                                 seps));
}

TEST(LayoutManager, MinWindowSizeAccountsForNavRail)
{
    const float with_nav    = LayoutManager::min_window_height(true, true, true);
    const float without_nav = LayoutManager::min_window_height(false, true, true);

    EXPECT_GT(with_nav, without_nav);
    EXPECT_GE(with_nav,
              LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::STATUS_BAR_HEIGHT
                  + LayoutManager::nav_rail_min_content_height());

    EXPECT_FLOAT_EQ(
        LayoutManager::min_window_width(true),
        LayoutManager::NAV_RAIL_COLLAPSED_WIDTH + LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
}
