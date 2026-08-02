#include <cmath>
#include <gtest/gtest.h>
#include <spectra/easy.hpp>
#include <vector>

// ─── Helper: reset easy state between tests ─────────────────────────────────
// The easy API uses global state, so we need to reset it between tests.
// We do this by directly accessing the detail::easy_state() singleton.

class EasyAPITest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Reset easy state so each test starts fresh
        auto& s = spectra::detail::easy_state();
        s.reset();
        s.app = nullptr;   // Force re-creation
    }
};

// ─── Basic State Management ─────────────────────────────────────────────────

TEST_F(EasyAPITest, InitialStateIsEmpty)
{
    EXPECT_EQ(spectra::gcf(), nullptr);
    EXPECT_EQ(spectra::gca(), nullptr);
    EXPECT_EQ(spectra::gca3d(), nullptr);
}

TEST_F(EasyAPITest, PlotAutoCreatesFigureAndAxes)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    spectra::plot(x, y);

    EXPECT_NE(spectra::gcf(), nullptr);
    EXPECT_NE(spectra::gca(), nullptr);
}

TEST_F(EasyAPITest, ScatterAutoCreatesFigureAndAxes)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    spectra::scatter(x, y);

    EXPECT_NE(spectra::gcf(), nullptr);
    EXPECT_NE(spectra::gca(), nullptr);
}

TEST_F(EasyAPITest, EmptyPlotCreatesEmptySeries)
{
    auto& line = spectra::plot();
    EXPECT_EQ(line.point_count(), 0u);
    EXPECT_NE(spectra::gca(), nullptr);
}

TEST_F(EasyAPITest, EmptyScatterCreatesEmptySeries)
{
    auto& sc = spectra::scatter();
    EXPECT_EQ(sc.point_count(), 0u);
    EXPECT_NE(spectra::gca(), nullptr);
}

// ─── Figure Management ──────────────────────────────────────────────────────

TEST_F(EasyAPITest, FigureCreatesFigure)
{
    auto& fig = spectra::figure();
    EXPECT_EQ(spectra::gcf(), &fig);
    EXPECT_EQ(spectra::gca(), nullptr);   // No axes yet
}

TEST_F(EasyAPITest, FigureWithDimensions)
{
    auto& fig = spectra::figure(800, 600);
    EXPECT_EQ(fig.width(), 800u);
    EXPECT_EQ(fig.height(), 600u);
}

TEST_F(EasyAPITest, NewFigureResetsCurrent)
{
    std::vector<float> x = {0, 1};
    std::vector<float> y = {0, 1};
    spectra::plot(x, y);
    auto* ax1 = spectra::gca();

    spectra::figure();
    EXPECT_EQ(spectra::gca(), nullptr);   // Axes reset on new figure

    spectra::plot(x, y);
    auto* ax2 = spectra::gca();
    EXPECT_NE(ax1, ax2);   // Different axes on different figures
}

// ─── Subplot ────────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, SubplotSelectsAxes)
{
    spectra::subplot(2, 1, 1);
    auto* ax1 = spectra::gca();
    EXPECT_NE(ax1, nullptr);

    spectra::subplot(2, 1, 2);
    auto* ax2 = spectra::gca();
    EXPECT_NE(ax2, nullptr);
    EXPECT_NE(ax1, ax2);
}

TEST_F(EasyAPITest, SubplotCreatesFigureImplicitly)
{
    EXPECT_EQ(spectra::gcf(), nullptr);
    spectra::subplot(1, 2, 1);
    EXPECT_NE(spectra::gcf(), nullptr);
}

// ─── 2D Plotting ────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, PlotWithFormatString)
{
    std::vector<float> x    = {0, 1, 2, 3};
    std::vector<float> y    = {0, 1, 4, 9};
    auto&              line = spectra::plot(x, y, "r--o");

    EXPECT_EQ(line.line_style(), spectra::LineStyle::Dashed);
    EXPECT_EQ(line.marker_style(), spectra::MarkerStyle::Circle);
    EXPECT_EQ(line.point_count(), 4u);
}

TEST_F(EasyAPITest, PlotWithPlotStyle)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    spectra::PlotStyle ps;
    ps.line_style = spectra::LineStyle::Dotted;
    ps.color      = spectra::colors::red;
    auto& line    = spectra::plot(x, y, ps);

    EXPECT_EQ(line.line_style(), spectra::LineStyle::Dotted);
}

TEST_F(EasyAPITest, MultiplePlotsOnSameAxes)
{
    std::vector<float> x  = {0, 1, 2};
    std::vector<float> y1 = {0, 1, 4};
    std::vector<float> y2 = {0, 2, 8};
    spectra::plot(x, y1);
    spectra::plot(x, y2);

    EXPECT_EQ(spectra::gca()->series().size(), 2u);
}

TEST_F(EasyAPITest, FluentChaining)
{
    std::vector<float> x    = {0, 1, 2};
    std::vector<float> y    = {0, 1, 4};
    auto&              line = spectra::plot(x, y, "b-").label("data").color(spectra::colors::green);

    EXPECT_EQ(line.label(), "data");
}

// ─── Axes Configuration ─────────────────────────────────────────────────────

TEST_F(EasyAPITest, TitleSetsAxesTitle)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::title("My Plot");
    EXPECT_EQ(spectra::gca()->title(), "My Plot");
}

TEST_F(EasyAPITest, AxisLabels)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::xlabel("X Axis");
    spectra::ylabel("Y Axis");
    EXPECT_EQ(spectra::gca()->xlabel(), "X Axis");
    EXPECT_EQ(spectra::gca()->ylabel(), "Y Axis");
}

TEST_F(EasyAPITest, GridToggle)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::grid(false);
    EXPECT_FALSE(spectra::gca()->grid_enabled());
    spectra::grid(true);
    EXPECT_TRUE(spectra::gca()->grid_enabled());
}

TEST_F(EasyAPITest, AxisLimits)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::xlim(-5.0f, 5.0f);
    spectra::ylim(-10.0f, 10.0f);
    EXPECT_FLOAT_EQ(spectra::gca()->x_limits().min, -5.0f);
    EXPECT_FLOAT_EQ(spectra::gca()->x_limits().max, 5.0f);
    EXPECT_FLOAT_EQ(spectra::gca()->y_limits().min, -10.0f);
    EXPECT_FLOAT_EQ(spectra::gca()->y_limits().max, 10.0f);
}

TEST_F(EasyAPITest, PresentedBufferTracksLatestWindow)
{
    auto& line = spectra::plot();
    spectra::presented_buffer(5.0f);

    for (int i = 0; i <= 20; ++i)
        line.append(static_cast<float>(i), static_cast<float>(i));

    auto xl = spectra::gca()->x_limits();
    auto yl = spectra::gca()->y_limits();

    EXPECT_FLOAT_EQ(xl.min, 15.0f);
    EXPECT_FLOAT_EQ(xl.max, 20.0f);
    EXPECT_FLOAT_EQ(yl.min, 14.75f);
    EXPECT_FLOAT_EQ(yl.max, 20.25f);
}

TEST_F(EasyAPITest, ManualLimitsPausePresentedBufferFollow)
{
    auto& line = spectra::plot();
    spectra::presented_buffer(5.0f);
    EXPECT_TRUE(spectra::gca()->has_presented_buffer());
    EXPECT_TRUE(spectra::gca()->is_presented_buffer_following());

    for (int i = 0; i <= 10; ++i)
        line.append(static_cast<float>(i), std::sin(static_cast<float>(i)));

    spectra::xlim(-2.0f, 2.0f);
    spectra::ylim(-3.0f, 3.0f);

    EXPECT_TRUE(spectra::gca()->has_presented_buffer());
    EXPECT_FALSE(spectra::gca()->is_presented_buffer_following());
    auto xl = spectra::gca()->x_limits();
    auto yl = spectra::gca()->y_limits();
    EXPECT_FLOAT_EQ(xl.min, -2.0f);
    EXPECT_FLOAT_EQ(xl.max, 2.0f);
    EXPECT_FLOAT_EQ(yl.min, -3.0f);
    EXPECT_FLOAT_EQ(yl.max, 3.0f);
}

TEST_F(EasyAPITest, ManualYLimitKeepsPresentedBufferFollow)
{
    auto& line = spectra::plot();
    spectra::presented_buffer(5.0f);

    for (int i = 0; i <= 10; ++i)
        line.append(static_cast<float>(i), static_cast<float>(i));

    spectra::ylim(-3.0f, 3.0f);
    EXPECT_TRUE(spectra::gca()->is_presented_buffer_following());

    auto xl_before = spectra::gca()->x_limits();

    for (int i = 11; i <= 20; ++i)
        line.append(static_cast<float>(i), static_cast<float>(i));

    auto xl_after = spectra::gca()->x_limits();
    auto yl_after = spectra::gca()->y_limits();

    EXPECT_GT(xl_after.max, xl_before.max);
    EXPECT_FLOAT_EQ(yl_after.min, -3.0f);
    EXPECT_FLOAT_EQ(yl_after.max, 3.0f);
}

// ─── Legend ──────────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, LegendEnables)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::legend();
    EXPECT_TRUE(spectra::gcf()->legend().visible);
    EXPECT_EQ(spectra::gcf()->legend().position, spectra::LegendPosition::TopRight);
}

TEST_F(EasyAPITest, LegendPosition)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::legend(spectra::LegendPosition::BottomLeft);
    EXPECT_EQ(spectra::gcf()->legend().position, spectra::LegendPosition::BottomLeft);
}

// ─── Clear Axes ─────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, ClaRemovesSeries)
{
    std::vector<float> x = {0, 1}, y1 = {0, 1}, y2 = {1, 0};
    spectra::plot(x, y1);
    spectra::plot(x, y2);
    EXPECT_EQ(spectra::gca()->series().size(), 2u);

    spectra::cla();
    EXPECT_EQ(spectra::gca()->series().size(), 0u);
}

// ─── 3D Plotting ────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, Plot3AutoCreates3DAxes)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 2};
    std::vector<float> z = {0, 1, 4};
    spectra::plot3(x, y, z);

    EXPECT_NE(spectra::gca3d(), nullptr);
    EXPECT_EQ(spectra::gca(), nullptr);   // 2D axes should be null
}

TEST_F(EasyAPITest, Scatter3AutoCreates3DAxes)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 2};
    std::vector<float> z = {0, 1, 4};
    spectra::scatter3(x, y, z);

    EXPECT_NE(spectra::gca3d(), nullptr);
}

TEST_F(EasyAPITest, SurfAutoCreates3DAxes)
{
    std::vector<float> xg = {0, 1};
    std::vector<float> yg = {0, 1};
    std::vector<float> zv = {0, 1, 2, 3};
    spectra::surf(xg, yg, zv);

    EXPECT_NE(spectra::gca3d(), nullptr);
}

TEST_F(EasyAPITest, Subplot3dCreates3DAxes)
{
    spectra::subplot3d(1, 1, 1);
    EXPECT_NE(spectra::gca3d(), nullptr);
    EXPECT_EQ(spectra::gca(), nullptr);
}

// ─── 3D Axes Configuration ──────────────────────────────────────────────────

TEST_F(EasyAPITest, ZlimWorks)
{
    spectra::subplot3d(1, 1, 1);
    spectra::zlim(-1.0f, 1.0f);
    EXPECT_FLOAT_EQ(spectra::gca3d()->z_limits().min, -1.0f);
    EXPECT_FLOAT_EQ(spectra::gca3d()->z_limits().max, 1.0f);
}

TEST_F(EasyAPITest, ZlabelWorks)
{
    spectra::subplot3d(1, 1, 1);
    spectra::zlabel("Z Axis");
    EXPECT_EQ(spectra::gca3d()->zlabel(), "Z Axis");
}

TEST_F(EasyAPITest, TitleWorksOn3D)
{
    spectra::subplot3d(1, 1, 1);
    spectra::title("3D Plot");
    EXPECT_EQ(spectra::gca3d()->title(), "3D Plot");
}

// ─── Export (does not actually write, just sets path) ───────────────────────

TEST_F(EasyAPITest, SavePngSetsPath)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    // Just verify it doesn't crash — actual write happens during run()
    EXPECT_NO_THROW(spectra::save_png("/tmp/test_easy_api.png"));
}

TEST_F(EasyAPITest, SaveSvgSetsPath)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    EXPECT_NO_THROW(spectra::save_svg("/tmp/test_easy_api.svg"));
}

// ─── On Update ──────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, OnUpdateRegistersCallback)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    bool called = false;
    spectra::on_update([&](float, float) { called = true; });
    // Callback won't actually be called until show() runs the event loop,
    // but we verify the figure has an animation registered
    EXPECT_TRUE(spectra::gcf()->has_animation());
}

TEST_F(EasyAPITest, OnUpdateWithFps)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::plot(x, y);
    spectra::on_update(30.0f, [](float, float) {});
    EXPECT_TRUE(spectra::gcf()->has_animation());
    EXPECT_FLOAT_EQ(spectra::gcf()->anim_fps(), 30.0f);
}

// ─── Mixed 2D/3D Workflow ───────────────────────────────────────────────────

TEST_F(EasyAPITest, SwitchBetween2DAnd3D)
{
    std::vector<float> x = {0, 1}, y = {0, 1}, z = {0, 1};
    spectra::subplot(1, 2, 1);
    spectra::plot(x, y);
    EXPECT_NE(spectra::gca(), nullptr);
    EXPECT_EQ(spectra::gca3d(), nullptr);

    spectra::subplot3d(1, 2, 2);
    spectra::plot3(x, y, z);
    EXPECT_EQ(spectra::gca(), nullptr);   // Switched away from 2D
    EXPECT_NE(spectra::gca3d(), nullptr);
}

// ─── Multi-Figure Workflow ──────────────────────────────────────────────────

TEST_F(EasyAPITest, MultipleFigures)
{
    std::vector<float> x = {0, 1}, y1 = {0, 1}, y2 = {1, 0};
    auto&              fig1 = spectra::figure();
    spectra::plot(x, y1);

    auto& fig2 = spectra::figure();
    spectra::plot(x, y2);

    EXPECT_NE(&fig1, &fig2);
    EXPECT_EQ(spectra::gcf(), &fig2);   // Current figure is the last one created
}

// ─── Append for real-time ───────────────────────────────────────────────────

TEST_F(EasyAPITest, AppendToEmptyLine)
{
    auto& line = spectra::plot();
    line.append(0.0f, 1.0f);
    line.append(1.0f, 2.0f);
    line.append(2.0f, 3.0f);
    EXPECT_EQ(line.point_count(), 3u);
}

TEST_F(EasyAPITest, AppendToEmptyScatter)
{
    auto& sc = spectra::scatter();
    sc.append(0.0f, 1.0f);
    sc.append(1.0f, 2.0f);
    EXPECT_EQ(sc.point_count(), 2u);
}

// ─── Tab Control ────────────────────────────────────────────────────────────

TEST_F(EasyAPITest, TabCreatesNewFigure)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::figure();
    auto* fig1 = spectra::gcf();
    spectra::plot(x, y);

    spectra::tab();
    auto* fig2 = spectra::gcf();
    spectra::plot(x, y);

    EXPECT_NE(fig1, fig2);   // Different figure objects
}

TEST_F(EasyAPITest, TabResetsAxes)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::figure();
    spectra::plot(x, y);
    EXPECT_NE(spectra::gca(), nullptr);

    spectra::tab();
    EXPECT_EQ(spectra::gca(), nullptr);   // Axes reset — new tab has no axes yet
}

TEST_F(EasyAPITest, TabWithNoFigureCreatesFigure)
{
    // tab() with no current figure should act like figure()
    spectra::tab();
    EXPECT_NE(spectra::gcf(), nullptr);
}

TEST_F(EasyAPITest, MultipleTabsInSameWindow)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::figure();
    spectra::plot(x, y);

    spectra::tab();
    spectra::plot(x, y);

    spectra::tab();
    spectra::plot(x, y);

    // Three figures total (1 figure + 2 tabs)
    // All should be different figure objects
    EXPECT_NE(spectra::gcf(), nullptr);
}

TEST_F(EasyAPITest, TabThenFigureCreatesNewWindow)
{
    std::vector<float> x = {0, 1}, y = {0, 1};
    spectra::figure();
    spectra::plot(x, y);
    auto* fig_w1 = spectra::gcf();

    spectra::tab();
    spectra::plot(x, y);

    spectra::figure();   // New OS window
    spectra::plot(x, y);
    auto* fig_w2 = spectra::gcf();

    EXPECT_NE(fig_w1, fig_w2);
}

TEST_F(EasyAPITest, GcaReturnsCorrectAxesAfterTab)
{
    std::vector<float> x = {0, 1}, y1 = {0, 1}, y2 = {1, 0};

    spectra::figure();
    spectra::plot(x, y1);
    auto* ax1 = spectra::gca();

    spectra::tab();
    spectra::plot(x, y2);
    auto* ax2 = spectra::gca();

    EXPECT_NE(ax1, ax2);   // Different axes on different tabs
}

// ─── Reference Lines & Function Plots ───────────────────────────────────────

TEST_F(EasyAPITest, HlineCreatesHorizontalReferenceLine)
{
    auto& line = spectra::hline(0.0);
    ASSERT_EQ(line.point_count(), 2u);
    auto y = line.y_data();
    EXPECT_FLOAT_EQ(y[0], 0.0f);
    EXPECT_FLOAT_EQ(y[1], 0.0f);
}

TEST_F(EasyAPITest, VlineCreatesVerticalReferenceLine)
{
    auto& line = spectra::vline(1.5);
    ASSERT_EQ(line.point_count(), 2u);
    auto x = line.x_data();
    EXPECT_FLOAT_EQ(x[0], 1.5f);
    EXPECT_FLOAT_EQ(x[1], 1.5f);
}

TEST_F(EasyAPITest, FplotSamplesFunction)
{
    auto& line = spectra::fplot([](double x) { return x * x; }, -2.0, 2.0, 5);
    ASSERT_EQ(line.point_count(), 5u);
    auto x = line.x_data();
    auto y = line.y_data();
    EXPECT_FLOAT_EQ(x[0], -2.0f);
    EXPECT_FLOAT_EQ(x[4], 2.0f);
    EXPECT_FLOAT_EQ(y[0], 4.0f);
    EXPECT_FLOAT_EQ(y[2], 0.0f);
    EXPECT_FLOAT_EQ(y[4], 4.0f);
}

TEST_F(EasyAPITest, IhlineCreatesKnobAndBinding)
{
    auto& line = spectra::ihline(1.0, -5.0, 5.0);
    ASSERT_EQ(line.point_count(), 2u);
    auto& s = spectra::detail::easy_state();
    EXPECT_EQ(s.interactive_hlines_.size(), 1u);
    EXPECT_FALSE(s.knob_mgr.empty());
}

TEST_F(EasyAPITest, IfplotCreatesParamKnobs)
{
    auto& line = spectra::ifplot([](double x, std::span<const double> p) { return p[0] * x; },
                                 -1.0,
                                 1.0,
                                 {{"scale", 1.0, 0.0, 2.0}});
    EXPECT_GT(line.point_count(), 2u);
    EXPECT_EQ(spectra::detail::easy_state().interactive_fplots_.size(), 1u);
}
