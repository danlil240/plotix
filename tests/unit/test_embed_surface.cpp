#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/embed.hpp>
#include <spectra/spectra_embed_c.h>

#include <spectra/figure_registry.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

using namespace spectra;

namespace
{
struct ScreenPoint
{
    float x;
    float y;
};

ScreenPoint data_to_screen(const Axes& ax, double data_x, double data_y)
{
    const auto vp   = ax.viewport();
    const auto xlim = ax.x_limits();
    const auto ylim = ax.y_limits();
    const auto xr   = (xlim.max - xlim.min) != 0.0 ? (xlim.max - xlim.min) : 1.0;
    const auto yr   = (ylim.max - ylim.min) != 0.0 ? (ylim.max - ylim.min) : 1.0;
    const auto nx   = (data_x - xlim.min) / xr;
    const auto ny   = (data_y - ylim.min) / yr;
    return {static_cast<float>(vp.x + nx * vp.w), static_cast<float>(vp.y + (1.0 - ny) * vp.h)};
}
}   // namespace

// ─── Construction ───────────────────────────────────────────────────────────

TEST(EmbedSurface, DefaultConstruction)
{
    EmbedSurface surface;
    EXPECT_TRUE(surface.is_valid());
    EXPECT_EQ(surface.width(), 800u);
    EXPECT_EQ(surface.height(), 600u);
}

TEST(EmbedSurface, CustomDimensions)
{
    EmbedConfig cfg;
    cfg.width  = 1920;
    cfg.height = 1080;
    EmbedSurface surface(cfg);
    EXPECT_TRUE(surface.is_valid());
    EXPECT_EQ(surface.width(), 1920u);
    EXPECT_EQ(surface.height(), 1080u);
}

TEST(EmbedSurface, MoveConstruction)
{
    EmbedSurface a({.width = 400, .height = 300});
    ASSERT_TRUE(a.is_valid());
    EmbedSurface b(std::move(a));
    EXPECT_TRUE(b.is_valid());
    EXPECT_EQ(b.width(), 400u);
    EXPECT_EQ(b.height(), 300u);
}

// ─── Figure Management ──────────────────────────────────────────────────────

TEST(EmbedSurface, CreateFigure)
{
    EmbedSurface surface;
    ASSERT_TRUE(surface.is_valid());

    auto& fig = surface.figure();
    EXPECT_NE(surface.active_figure(), nullptr);
    EXPECT_EQ(surface.active_figure(), &fig);
}

TEST(EmbedSurface, MultipleFigures)
{
    EmbedSurface surface;
    auto&        fig1 = surface.figure();
    auto&        fig2 = surface.figure();

    // First figure auto-activated
    EXPECT_EQ(surface.active_figure(), &fig1);

    // Switch to second
    surface.set_active_figure(&fig2);
    EXPECT_EQ(surface.active_figure(), &fig2);
}

TEST(EmbedSurface, FigureWithSubplot)
{
    EmbedSurface surface;
    auto&        fig = surface.figure();
    auto&        ax  = fig.subplot(1, 1, 1);

    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};
    ax.line(x, y);

    EXPECT_EQ(fig.axes().size(), 1u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(EmbedSurface, FigureRegistry)
{
    EmbedSurface surface;
    EXPECT_EQ(surface.figure_registry().count(), 0u);

    surface.figure();
    EXPECT_EQ(surface.figure_registry().count(), 1u);

    surface.figure();
    EXPECT_EQ(surface.figure_registry().count(), 2u);
}

// ─── Rendering ──────────────────────────────────────────────────────────────

TEST(EmbedSurface, RenderToBufferEmpty)
{
    EmbedConfig cfg;
    cfg.width  = 64;
    cfg.height = 64;
    EmbedSurface surface(cfg);
    auto&        fig = surface.figure();
    fig.subplot(1, 1, 1);

    std::vector<uint8_t> pixels(64 * 64 * 4, 0);
    EXPECT_TRUE(surface.render_to_buffer(pixels.data()));

    // Buffer should have been written to (not all zeros)
    bool any_nonzero = false;
    for (auto p : pixels)
    {
        if (p != 0)
        {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero);
}

TEST(EmbedSurface, RenderToBufferWithData)
{
    EmbedConfig cfg;
    cfg.width  = 128;
    cfg.height = 128;
    EmbedSurface       surface(cfg);
    auto&              fig = surface.figure();
    auto&              ax  = fig.subplot(1, 1, 1);
    std::vector<float> x   = {0, 1, 2, 3, 4, 5};
    std::vector<float> y   = {0, 1, 4, 9, 16, 25};
    ax.line(x, y);

    std::vector<uint8_t> pixels(128 * 128 * 4, 0);
    EXPECT_TRUE(surface.render_to_buffer(pixels.data()));

    // Should have non-trivial content
    size_t nonzero = 0;
    for (auto p : pixels)
    {
        if (p != 0)
            nonzero++;
    }
    EXPECT_GT(nonzero, 100u);
}

TEST(EmbedSurface, RenderToBufferNullptr)
{
    EmbedSurface surface;
    surface.figure().subplot(1, 1, 1);
    EXPECT_FALSE(surface.render_to_buffer(nullptr));
}

TEST(EmbedSurface, RenderToBufferNoFigure)
{
    EmbedSurface         surface;
    std::vector<uint8_t> pixels(800 * 600 * 4, 0);
    // No figure created — should fail gracefully
    EXPECT_FALSE(surface.render_to_buffer(pixels.data()));
}

TEST(EmbedSurface, MultipleRenders)
{
    EmbedConfig cfg;
    cfg.width  = 64;
    cfg.height = 64;
    EmbedSurface surface(cfg);
    auto&        fig = surface.figure();
    fig.subplot(1, 1, 1);

    std::vector<uint8_t> pixels(64 * 64 * 4, 0);

    // Render multiple times without issues
    for (int i = 0; i < 5; i++)
    {
        EXPECT_TRUE(surface.render_to_buffer(pixels.data()));
    }
}

// ─── Resize ─────────────────────────────────────────────────────────────────

TEST(EmbedSurface, Resize)
{
    EmbedConfig cfg;
    cfg.width  = 100;
    cfg.height = 100;
    EmbedSurface surface(cfg);
    auto&        fig = surface.figure();
    fig.subplot(1, 1, 1);

    EXPECT_TRUE(surface.resize(200, 150));
    EXPECT_EQ(surface.width(), 200u);
    EXPECT_EQ(surface.height(), 150u);

    std::vector<uint8_t> pixels(200 * 150 * 4, 0);
    EXPECT_TRUE(surface.render_to_buffer(pixels.data()));
}

TEST(EmbedSurface, ResizeToZero)
{
    EmbedSurface surface;
    EXPECT_FALSE(surface.resize(0, 0));
    EXPECT_FALSE(surface.resize(100, 0));
    EXPECT_FALSE(surface.resize(0, 100));
    // Original dimensions preserved
    EXPECT_EQ(surface.width(), 800u);
    EXPECT_EQ(surface.height(), 600u);
}

TEST(EmbedSurface, ResizeSameDimensions)
{
    EmbedConfig cfg;
    cfg.width  = 400;
    cfg.height = 300;
    EmbedSurface surface(cfg);
    // Same size = no-op, returns true
    EXPECT_TRUE(surface.resize(400, 300));
}

TEST(EmbedSurface, ResizeThenRender)
{
    EmbedConfig cfg;
    cfg.width  = 64;
    cfg.height = 64;
    EmbedSurface       surface(cfg);
    auto&              fig = surface.figure();
    auto&              ax  = fig.subplot(1, 1, 1);
    std::vector<float> x   = {0, 1, 2, 3};
    std::vector<float> y   = {0, 1, 4, 9};
    ax.line(x, y);

    // Render at original size
    std::vector<uint8_t> pixels1(64 * 64 * 4);
    EXPECT_TRUE(surface.render_to_buffer(pixels1.data()));

    // Resize up
    EXPECT_TRUE(surface.resize(128, 96));

    // Render at new size
    std::vector<uint8_t> pixels2(128 * 96 * 4);
    EXPECT_TRUE(surface.render_to_buffer(pixels2.data()));
}

// ─── Input Forwarding ───────────────────────────────────────────────────────

TEST(EmbedSurface, InjectMouseMove)
{
    EmbedConfig cfg;
    cfg.width  = 200;
    cfg.height = 200;
    EmbedSurface surface(cfg);
    auto&        fig = surface.figure();
    fig.subplot(1, 1, 1);

    // Should not crash
    surface.inject_mouse_move(100.0f, 100.0f);
    surface.inject_mouse_move(150.0f, 50.0f);
}

TEST(EmbedSurface, InjectMouseButton)
{
    EmbedConfig cfg;
    cfg.width  = 200;
    cfg.height = 200;
    EmbedSurface surface(cfg);
    auto&        fig = surface.figure();
    fig.subplot(1, 1, 1);

    // Simulate left press + release
    surface.inject_mouse_button(embed::MOUSE_BUTTON_LEFT, embed::ACTION_PRESS, 0, 100.0f, 100.0f);
    surface.inject_mouse_move(120.0f, 110.0f);
    surface.inject_mouse_button(embed::MOUSE_BUTTON_LEFT, embed::ACTION_RELEASE, 0, 120.0f, 110.0f);
}

TEST(EmbedSurface, InjectScroll)
{
    EmbedConfig cfg;
    cfg.width  = 200;
    cfg.height = 200;
    EmbedSurface       surface(cfg);
    auto&              fig = surface.figure();
    auto&              ax  = fig.subplot(1, 1, 1);
    std::vector<float> x   = {0, 1, 2, 3, 4};
    std::vector<float> y   = {0, 1, 4, 9, 16};
    ax.line(x, y);

    // Scroll to zoom — should not crash
    surface.inject_scroll(0.0f, 1.0f, 100.0f, 100.0f);
    surface.inject_scroll(0.0f, -1.0f, 100.0f, 100.0f);
}

TEST(EmbedSurface, InjectKey)
{
    EmbedSurface surface;
    surface.figure().subplot(1, 1, 1);

    // Key press/release — should not crash
    surface.inject_key(embed::KEY_R, embed::ACTION_PRESS, 0);
    surface.inject_key(embed::KEY_R, embed::ACTION_RELEASE, 0);
}

TEST(EmbedSurface, InjectChar)
{
    EmbedSurface surface;
    surface.figure().subplot(1, 1, 1);
    // Should not crash (currently a no-op)
    surface.inject_char('a');
    surface.inject_char(0x00E9);   // é
}

TEST(EmbedSurface, Update)
{
    EmbedSurface surface;
    surface.figure().subplot(1, 1, 1);
    // Advance animations — should not crash
    surface.update(0.016f);
    surface.update(0.016f);
    surface.update(0.016f);
}

// ─── Properties ─────────────────────────────────────────────────────────────

TEST(EmbedSurface, DpiScale)
{
    EmbedConfig cfg;
    cfg.dpi_scale = 2.0f;
    EmbedSurface surface(cfg);
    EXPECT_FLOAT_EQ(surface.dpi_scale(), 2.0f);

    surface.set_dpi_scale(1.5f);
    EXPECT_FLOAT_EQ(surface.dpi_scale(), 1.5f);
}

TEST(EmbedSurface, BackgroundAlpha)
{
    EmbedConfig cfg;
    cfg.background_alpha = 0.0f;
    EmbedSurface surface(cfg);
    EXPECT_FLOAT_EQ(surface.background_alpha(), 0.0f);

    surface.set_background_alpha(0.5f);
    EXPECT_FLOAT_EQ(surface.background_alpha(), 0.5f);
}

// ─── Vulkan Interop ─────────────────────────────────────────────────────────

TEST(EmbedSurface, RenderToImageNotEnabled)
{
    EmbedSurface surface;
    surface.figure().subplot(1, 1, 1);

    VulkanInteropInfo interop;
    // Should fail — interop not enabled
    EXPECT_FALSE(surface.render_to_image(interop));
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

TEST(EmbedSurface, RedrawCallback)
{
    EmbedSurface surface;
    bool         called = false;
    surface.set_redraw_callback([&]() { called = true; });
    // Callback is stored but not triggered in this test (it's host-driven)
    EXPECT_FALSE(called);
}

TEST(EmbedSurface, CursorChangeCallback)
{
    EmbedSurface surface;
    CursorShape  last_shape = CursorShape::Arrow;
    surface.set_cursor_change_callback([&](CursorShape s) { last_shape = s; });
    // Callback stored — cursor changes happen during input handling
    EXPECT_EQ(last_shape, CursorShape::Arrow);
}

TEST(EmbedSurface, InteractiveCallbacksViaDeterministicInputSimulation)
{
    int point_calls  = 0;
    int series_calls = 0;
    int hover_calls  = 0;
    int view_calls   = 0;

    bool                       point_hit  = false;
    bool                       series_hit = false;
    bool                       hover_hit  = false;
    std::optional<size_t>      hover_pi;
    std::optional<double>      hover_x;
    std::optional<double>      hover_y;
    std::optional<ScreenPoint> hovered_screen;

    EmbedConfig cfg;
    cfg.width             = 400;
    cfg.height            = 300;
    cfg.show_imgui_chrome = true;

    auto* surface = new EmbedSurface(cfg);
    ASSERT_TRUE(surface->is_valid());
    auto&              fig = surface->figure();
    auto&              ax  = fig.subplot(1, 1, 1);
    std::vector<float> x   = {1.0f, 2.0f, 3.0f};
    std::vector<float> y   = {2.0f, 3.0f, 6.0f};
    ax.line(x, y);
    ax.xlim(1.0f, 3.0f);
    ax.ylim(2.0f, 6.0f);

    surface->set_on_point_selected(
        [&](int ai, int si, std::size_t pi, double, double)
        {
            (void)ai;
            (void)si;
            ++point_calls;
            point_hit = hover_pi.has_value() && pi == *hover_pi;
        });
    surface->set_on_series_selected(
        [&](int ai, int si)
        {
            (void)ai;
            (void)si;
            ++series_calls;
            series_hit = true;
        });
    surface->set_on_hover(
        [&](int /* ai */, int /* si */, std::size_t pi, double hx, double hy)
        {
            ++hover_calls;
            if (pi != 0u || hx != 0.0 || hy != 0.0)
            {
                hover_hit = true;
                hover_pi  = pi;
                hover_x   = hx;
                hover_y   = hy;
            }
        });
    surface->set_on_view_changed([&](double, double, double, double) { ++view_calls; });

    std::vector<uint8_t> pixels(cfg.width * cfg.height * 4, 0);
    ASSERT_TRUE(surface->render_to_buffer(pixels.data()));
    EXPECT_GE(view_calls, 1);

    const auto        vp = ax.viewport();
    const ScreenPoint probe{
        static_cast<float>(vp.x + vp.w * 0.5f),
        static_cast<float>(vp.y + vp.h * 0.5f),
    };
    surface->inject_mouse_move(probe.x, probe.y);
    ASSERT_TRUE(surface->render_to_buffer(pixels.data()));
    EXPECT_GE(hover_calls, 1);
    EXPECT_TRUE(hover_hit);
    ASSERT_TRUE(hover_x.has_value());
    ASSERT_TRUE(hover_y.has_value());
    hovered_screen = data_to_screen(ax, *hover_x, *hover_y);
    surface->inject_mouse_button(embed::MOUSE_BUTTON_LEFT,
                                 embed::ACTION_PRESS,
                                 0,
                                 hovered_screen->x,
                                 hovered_screen->y);
    surface->inject_mouse_button(embed::MOUSE_BUTTON_LEFT,
                                 embed::ACTION_RELEASE,
                                 0,
                                 hovered_screen->x,
                                 hovered_screen->y);
    EXPECT_GE(point_calls, 1);
    EXPECT_GE(series_calls, 1);
    EXPECT_TRUE(point_hit);
    EXPECT_TRUE(series_hit);

    const int view_calls_before_zoom = view_calls;
    surface->inject_scroll(0.0f, 1.0f, probe.x, probe.y);
    ASSERT_TRUE(surface->render_to_buffer(pixels.data()));
    EXPECT_GT(view_calls, view_calls_before_zoom);
}

// ─── Advanced ───────────────────────────────────────────────────────────────

TEST(EmbedSurface, BackendAccess)
{
    EmbedSurface surface;
    EXPECT_NE(surface.backend(), nullptr);
    EXPECT_NE(surface.renderer(), nullptr);
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST(EmbedSurface, InputOnUninitializedSurface)
{
    EmbedSurface surface;
    // No figure, no axes — input calls should not crash
    surface.inject_mouse_move(50.0f, 50.0f);
    surface.inject_mouse_button(0, 1, 0, 50.0f, 50.0f);
    surface.inject_scroll(0, 1, 50, 50);
    surface.inject_key(embed::KEY_A, embed::ACTION_PRESS, 0);
    surface.inject_char('x');
    surface.update(0.016f);
}

TEST(EmbedSurface, RenderAfterSetActiveNull)
{
    EmbedConfig cfg;
    cfg.width  = 64;
    cfg.height = 64;
    EmbedSurface surface(cfg);
    surface.figure().subplot(1, 1, 1);

    surface.set_active_figure(nullptr);
    std::vector<uint8_t> pixels(64 * 64 * 4);
    // No active figure — render should fail gracefully
    EXPECT_FALSE(surface.render_to_buffer(pixels.data()));
}

TEST(EmbedSurface, RenderWith3DSubplot)
{
    EmbedConfig cfg;
    cfg.width  = 64;
    cfg.height = 64;
    EmbedSurface surface(cfg);
    auto&        fig  = surface.figure();
    auto&        ax3d = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 2};
    std::vector<float> z = {0, 1, 4};
    ax3d.scatter3d(x, y, z);

    std::vector<uint8_t> pixels(64 * 64 * 4, 0);
    EXPECT_TRUE(surface.render_to_buffer(pixels.data()));
}

// ─── C API ──────────────────────────────────────────────────────────────────

TEST(EmbedCApi, CreateDestroy)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(spectra_embed_is_valid(s), 1);
    spectra_embed_destroy(s);
}

TEST(EmbedCApi, HandlesRemainStableWhenPoolsGrow)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);

    SpectraFigure* first_figure = spectra_embed_figure(s);
    ASSERT_NE(first_figure, nullptr);
    for (int i = 0; i < 1024; ++i)
        ASSERT_NE(spectra_embed_figure(s), nullptr);

    SpectraAxes* first_axes = spectra_figure_subplot(first_figure, 1, 1, 1);
    ASSERT_NE(first_axes, nullptr);
    for (int i = 0; i < 1024; ++i)
        ASSERT_NE(spectra_figure_subplot(first_figure, 1, 1, 1), nullptr);

    const std::vector<float> x  = {0.0f, 1.0f};
    const std::vector<float> y  = {0.0f, 1.0f};
    SpectraSeries* first_series = spectra_axes_line(first_axes, x.data(), y.data(), 2, nullptr);
    ASSERT_NE(first_series, nullptr);
    for (int i = 0; i < 1024; ++i)
        ASSERT_NE(spectra_axes_line(first_axes, x.data(), y.data(), 2, nullptr), nullptr);

    spectra_series_set_opacity(first_series, 0.5f);
    spectra_embed_destroy(s);
}

TEST(EmbedCApi, CreateExWithTheme)
{
    SpectraEmbed* s = spectra_embed_create_ex(64, 64, "dark", 1.0f, 1, 1.0f);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(spectra_embed_is_valid(s), 1);
    spectra_embed_destroy(s);
}

TEST(EmbedCApi, CreateExNullTheme)
{
    SpectraEmbed* s = spectra_embed_create_ex(64, 64, nullptr, 1.0f, 1, 1.0f);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(spectra_embed_is_valid(s), 1);
    spectra_embed_destroy(s);
}

TEST(EmbedCApi, BackgroundAlpha)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);
    spectra_embed_set_background_alpha(s, 0.5f);
    EXPECT_FLOAT_EQ(spectra_embed_get_background_alpha(s), 0.5f);
    spectra_embed_destroy(s);
}

TEST(EmbedCApi, HistogramSeries)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);

    SpectraFigure* fig = spectra_embed_figure(s);
    ASSERT_NE(fig, nullptr);
    SpectraAxes* ax = spectra_figure_subplot(fig, 1, 1, 1);
    ASSERT_NE(ax, nullptr);

    std::vector<float> values = {1, 2, 2, 3, 3, 3, 4, 4, 5};
    SpectraSeries*     series =
        spectra_axes_histogram(ax, values.data(), static_cast<uint32_t>(values.size()), 5, "hist");
    EXPECT_NE(series, nullptr);

    spectra_embed_destroy(s);
}

TEST(EmbedCApi, BarSeries)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);

    SpectraFigure* fig = spectra_embed_figure(s);
    ASSERT_NE(fig, nullptr);
    SpectraAxes* ax = spectra_figure_subplot(fig, 1, 1, 1);
    ASSERT_NE(ax, nullptr);

    std::vector<float> pos     = {1, 2, 3, 4};
    std::vector<float> heights = {10, 20, 15, 25};
    SpectraSeries*     series =
        spectra_axes_bar(ax, pos.data(), heights.data(), static_cast<uint32_t>(pos.size()), "bar");
    EXPECT_NE(series, nullptr);

    spectra_embed_destroy(s);
}

TEST(EmbedCApi, AutoFit)
{
    SpectraEmbed* s = spectra_embed_create(64, 64);
    ASSERT_NE(s, nullptr);

    SpectraFigure* fig = spectra_embed_figure(s);
    ASSERT_NE(fig, nullptr);
    SpectraAxes* ax = spectra_figure_subplot(fig, 1, 1, 1);
    ASSERT_NE(ax, nullptr);

    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    spectra_axes_line(ax, x.data(), y.data(), static_cast<uint32_t>(x.size()), nullptr);
    spectra_axes_auto_fit(ax);   // Must not crash

    spectra_embed_destroy(s);
}
