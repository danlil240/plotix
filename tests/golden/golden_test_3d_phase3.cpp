#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"

namespace fs = std::filesystem;

// Single shared App — one VkInstance for the entire test binary.  See
// golden_test_3d.cpp for the rationale.
static spectra::App* g_app = nullptr;

namespace spectra::test
{

static fs::path baseline_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_BASELINE_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "baseline";
}

static fs::path output_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_OUTPUT_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "output";
}

static bool update_baselines()
{
    const char* env = std::getenv("SPECTRA_UPDATE_BASELINES");
    return (env != nullptr) && std::string(env) == "1";
}

static bool render_headless(Figure& fig, App& app, std::vector<uint8_t>& pixels)
{
    uint32_t w = fig.width();
    uint32_t h = fig.height();

    app.run();

    pixels.resize(static_cast<size_t>(w) * h * 4);
    Backend* backend = app.backend();
    if (!backend)
        return false;

    return backend->readback_framebuffer(pixels.data(), w, h);
}

static void run_golden_test_3d_p3(const std::string&                 scene_name,
                                  std::function<void(App&, Figure&)> setup_scene,
                                  uint32_t                           width             = 640,
                                  uint32_t                           height            = 480,
                                  double                             tolerance_percent = 2.0,
                                  double                             max_mae           = 3.0)
{
    fs::path baseline_path = baseline_dir() / (scene_name + ".raw");
    fs::path actual_path   = output_dir() / (scene_name + "_actual.raw");
    fs::path diff_path     = output_dir() / (scene_name + "_diff.raw");

    fs::create_directories(output_dir());

    ASSERT_NE(g_app, nullptr) << "Shared App not initialized";
    App&     app    = *g_app;
    auto&    fig    = app.figure({.width = width, .height = height});
    FigureId fig_id = app.figure_registry().find_id(&fig);

    setup_scene(app, fig);

    std::vector<uint8_t> actual_pixels;
    bool                 render_ok = render_headless(fig, app, actual_pixels);
    if (fig_id != INVALID_FIGURE_ID)
    {
        if (auto* r = app.renderer())
        {
            fig.for_each_axes(
                [&](AxesBase* axes)
                {
                    for (auto& ser_ptr : axes->series())
                        r->notify_series_removed(ser_ptr.get());
                    r->notify_axes_removed(axes);
                });
            r->notify_figure_removed(&fig);
        }
        app.figure_registry().unregister_figure(fig_id);
    }

    ASSERT_TRUE(render_ok) << "Failed to render scene: " << scene_name;

    ASSERT_TRUE(save_raw_rgba(actual_path.string(), actual_pixels.data(), width, height))
        << "Failed to save actual render for: " << scene_name;

    if (update_baselines())
    {
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), actual_pixels.data(), width, height))
            << "Failed to save baseline for: " << scene_name;
        std::cout << "[GOLDEN 3D P3] Updated baseline: " << baseline_path << "\n";
        return;
    }

    if (!fs::exists(baseline_path))
    {
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with SPECTRA_UPDATE_BASELINES=1 to generate)";
        return;
    }

    std::vector<uint8_t> baseline_pixels;
    uint32_t             baseline_w = 0;
    uint32_t             baseline_h = 0;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline_pixels, baseline_w, baseline_h))
        << "Failed to load baseline: " << baseline_path;

    ASSERT_EQ(baseline_w, width) << "Baseline width mismatch for: " << scene_name;
    ASSERT_EQ(baseline_h, height) << "Baseline height mismatch for: " << scene_name;

    DiffResult diff = compare_images(actual_pixels.data(), baseline_pixels.data(), width, height);

    std::vector<uint8_t> diff_pixels =
        generate_diff_image(actual_pixels.data(), baseline_pixels.data(), width, height);
    save_raw_rgba(diff_path.string(), diff_pixels.data(), width, height);

    EXPECT_LE(diff.percent_different, tolerance_percent)
        << "Scene: " << scene_name << "\n  MAE: " << diff.mean_absolute_error
        << "\n  Max error: " << diff.max_absolute_error
        << "\n  Different pixels: " << diff.percent_different << "%"
        << "\n  Diff image: " << diff_path;

    EXPECT_LE(diff.mean_absolute_error, max_mae)
        << "Scene: " << scene_name << " has high mean absolute error";
}

// ═══════════════════════════════════════════════════════════════════════════════
// 1. Lit Surface — Phong shading with configurable material
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, LitSurface_SinCos)
{
    run_golden_test_3d_p3("3d_p3_lit_surface_sincos",
                          [](App& /*app*/, Figure& fig)
                          {
                              auto& ax = fig.subplot3d(1, 1, 1);

                              const int          nx = 30;
                              const int          ny = 30;
                              std::vector<float> x_grid;
                              std::vector<float> y_grid;
                              std::vector<float> z_values;
                              x_grid.reserve(nx);
                              for (int i = 0; i < nx; ++i)
                                  x_grid.push_back(static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f);
                              y_grid.reserve(ny);
                              for (int j = 0; j < ny; ++j)
                                  y_grid.push_back(static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f);
                              for (int j = 0; j < ny; ++j)
                                  for (int i = 0; i < nx; ++i)
                                      z_values.push_back(std::sin(x_grid[i]) * std::cos(y_grid[j]));

                              ax.surface(x_grid, y_grid, z_values)
                                  .color(Color{0.8f, 0.4f, 0.1f, 1.0f})
                                  .ambient(0.2f)
                                  .specular(0.6f)
                                  .shininess(64.0f);

                              ax.set_light_dir(1.0f, 1.0f, 1.0f);
                              ax.set_lighting_enabled(true);
                              ax.title("Lit Surface: sin(x)*cos(y)");
                          });
}

TEST(Golden3DPhase3, LitSurface_HighSpecular)
{
    run_golden_test_3d_p3(
        "3d_p3_lit_surface_specular",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            const int          nx = 25;
            const int          ny = 25;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
            y_grid.reserve(ny);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
            z_values.reserve(nx * ny);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]) + 0.001f;
                    z_values.push_back(std::sin(r * 2.0f) / r);
                }

            ax.surface(x_grid, y_grid, z_values)
                .color(Color{0.2f, 0.6f, 0.9f, 1.0f})
                .ambient(0.1f)
                .specular(0.9f)
                .shininess(256.0f);

            ax.set_light_dir(0.5f, 0.8f, 1.0f);
            ax.title("High Specular Surface");
        });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Lit Mesh — Phong shading on custom geometry
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, LitMesh_Quad)
{
    run_golden_test_3d_p3("3d_p3_lit_mesh_quad",
                          [](App& /*app*/, Figure& fig)
                          {
                              auto& ax = fig.subplot3d(1, 1, 1);

                              std::vector<float> vertices = {
                                  -1.5f, -1.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.5f, -1.5f,
                                  0.0f,  0.0f,  0.0f,  1.0f, 1.5f, 1.5f, 0.0f, 0.0f,
                                  0.0f,  1.0f,  -1.5f, 1.5f, 0.0f, 0.0f, 0.0f, 1.0f,
                              };
                              std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

                              ax.mesh(vertices, indices)
                                  .color(Color{0.3f, 0.7f, 0.3f, 1.0f})
                                  .ambient(0.2f)
                                  .specular(0.5f)
                                  .shininess(64.0f);

                              ax.set_light_dir(1.0f, 1.0f, 1.0f);
                              ax.title("Lit Mesh: Quad");
                          });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Transparent Surface
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, TransparentSurface)
{
    run_golden_test_3d_p3("3d_p3_transparent_surface",
                          [](App& /*app*/, Figure& fig)
                          {
                              auto& ax = fig.subplot3d(1, 1, 1);

                              const int          nx = 25;
                              const int          ny = 25;
                              std::vector<float> x_grid;
                              std::vector<float> y_grid;
                              std::vector<float> z_values;
                              x_grid.reserve(nx);
                              y_grid.reserve(ny);
                              z_values.reserve(nx * ny);
                              for (int i = 0; i < nx; ++i)
                                  x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
                              for (int j = 0; j < ny; ++j)
                                  y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
                              for (int j = 0; j < ny; ++j)
                                  for (int i = 0; i < nx; ++i)
                                      z_values.push_back(std::sin(x_grid[i]) * std::cos(y_grid[j]));

                              ax.surface(x_grid, y_grid, z_values)
                                  .color(Color{1.0f, 0.5f, 0.0f, 0.5f})
                                  .ambient(0.2f)
                                  .specular(0.4f)
                                  .shininess(32.0f);

                              ax.title("Transparent Surface (alpha=0.5)");
                          });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Transparent Scatter Overlay on Opaque Surface
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, TransparentScatterOnSurface)
{
    run_golden_test_3d_p3(
        "3d_p3_transparent_scatter_on_surface",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            // Opaque surface
            const int          nx = 20;
            const int          ny = 20;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            y_grid.reserve(ny);
            z_values.reserve(nx * ny);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    z_values.push_back(std::sin(x_grid[i]) * std::cos(y_grid[j]));

            ax.surface(x_grid, y_grid, z_values)
                .color(colors::blue)
                .ambient(0.2f)
                .specular(0.5f)
                .shininess(64.0f);

            // Transparent scatter overlay
            std::vector<float> sx;
            std::vector<float> sy;
            std::vector<float> sz;
            sx.reserve(200);
            sy.reserve(200);
            sz.reserve(200);
            for (int i = 0; i < 200; ++i)
            {
                float t = static_cast<float>(i) * 0.05f;
                sx.push_back(std::cos(t) * 1.5f);
                sy.push_back(std::sin(t) * 1.5f);
                sz.push_back(std::sin(t * 0.5f) + 0.5f);
            }
            ax.scatter3d(sx, sy, sz).color(Color{1.0f, 0.0f, 0.0f, 0.4f}).size(6.0f);

            ax.title("Transparent Scatter on Lit Surface");
        });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. Wireframe Surface
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, WireframeSurface)
{
    run_golden_test_3d_p3(
        "3d_p3_wireframe_surface",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            const int          nx = 20;
            const int          ny = 20;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            y_grid.reserve(ny);
            z_values.reserve(nx * ny);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]) + 0.001f;
                    z_values.push_back(std::sin(r) / r);
                }

            ax.surface(x_grid, y_grid, z_values).color(colors::green).wireframe(true);

            ax.title("Wireframe Surface: sinc(r)");
        });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. Surface with Colormap + Alpha
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, SurfaceColormapAlpha)
{
    run_golden_test_3d_p3(
        "3d_p3_surface_colormap_alpha",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            const int          nx = 30;
            const int          ny = 30;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            y_grid.reserve(ny);
            z_values.reserve(nx * ny);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]) + 0.001f;
                    z_values.push_back(std::sin(r) / r);
                }

            ax.surface(x_grid, y_grid, z_values)
                .colormap(ColormapType::Viridis)
                .colormap_alpha(true)
                .set_colormap_alpha_range(0.2f, 1.0f);

            ax.title("Viridis Colormap + Alpha");
        });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7. Lighting Disabled (flat shading)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, LightingDisabled)
{
    run_golden_test_3d_p3("3d_p3_lighting_disabled",
                          [](App& /*app*/, Figure& fig)
                          {
                              auto& ax = fig.subplot3d(1, 1, 1);

                              const int          nx = 20;
                              const int          ny = 20;
                              std::vector<float> x_grid;
                              std::vector<float> y_grid;
                              std::vector<float> z_values;
                              x_grid.reserve(nx);
                              y_grid.reserve(ny);
                              z_values.reserve(nx * ny);
                              for (int i = 0; i < nx; ++i)
                                  x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
                              for (int j = 0; j < ny; ++j)
                                  y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
                              for (int j = 0; j < ny; ++j)
                                  for (int i = 0; i < nx; ++i)
                                      z_values.push_back(std::sin(x_grid[i]) * std::cos(y_grid[j]));

                              ax.surface(x_grid, y_grid, z_values).color(colors::orange);

                              ax.set_lighting_enabled(false);
                              ax.title("Lighting Disabled (flat)");
                          });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 8. Multiple Transparent Surfaces (Painter's Sort)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, MultipleTransparentSurfaces)
{
    run_golden_test_3d_p3("3d_p3_multi_transparent_surfaces",
                          [](App& /*app*/, Figure& fig)
                          {
                              auto& ax = fig.subplot3d(1, 1, 1);

                              const int          nx = 20;
                              const int          ny = 20;
                              std::vector<float> x_grid;
                              std::vector<float> y_grid;
                              x_grid.reserve(nx);
                              y_grid.reserve(ny);
                              for (int i = 0; i < nx; ++i)
                                  x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
                              for (int j = 0; j < ny; ++j)
                                  y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);

                              // Surface 1: sin(x)*cos(y)
                              std::vector<float> z1(nx * ny);
                              for (int j = 0; j < ny; ++j)
                                  for (int i = 0; i < nx; ++i)
                                      z1[j * nx + i] = std::sin(x_grid[i]) * std::cos(y_grid[j]);

                              ax.surface(x_grid, y_grid, z1)
                                  .color(Color{1.0f, 0.2f, 0.2f, 0.5f})
                                  .ambient(0.2f)
                                  .specular(0.4f)
                                  .shininess(32.0f);

                              // Surface 2: cos(x)*sin(y) + offset
                              std::vector<float> z2(nx * ny);
                              for (int j = 0; j < ny; ++j)
                                  for (int i = 0; i < nx; ++i)
                                      z2[j * nx + i] =
                                          std::cos(x_grid[i]) * std::sin(y_grid[j]) + 0.5f;

                              ax.surface(x_grid, y_grid, z2)
                                  .color(Color{0.2f, 0.2f, 1.0f, 0.5f})
                                  .ambient(0.2f)
                                  .specular(0.4f)
                                  .shininess(32.0f);

                              ax.title("Two Transparent Surfaces");
                          });
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9. Mixed 2D + Lit 3D (Phase 3 acceptance scenario)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, Mixed2DAndLit3D)
{
    run_golden_test_3d_p3(
        "3d_p3_mixed_2d_lit3d",
        [](App& /*app*/, Figure& fig)
        {
            // 2D line plot
            auto&              ax2d = fig.subplot(2, 1, 1);
            std::vector<float> x2d;
            std::vector<float> y2d;
            x2d.reserve(200);
            y2d.reserve(200);
            for (int i = 0; i < 200; ++i)
            {
                float t = static_cast<float>(i) * 0.05f;
                x2d.push_back(t);
                y2d.push_back(std::sin(t) * std::exp(-t * 0.1f));
            }
            ax2d.line(x2d, y2d).color(colors::blue);
            ax2d.title("2D: Damped Sine");

            // 3D lit surface
            auto&              ax3d = fig.subplot3d(2, 1, 2);
            const int          nx   = 25;
            const int          ny   = 25;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            y_grid.reserve(ny);
            z_values.reserve(nx * ny);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    z_values.push_back(std::sin(x_grid[i]) * std::cos(y_grid[j]));

            ax3d.surface(x_grid, y_grid, z_values)
                .color(Color{0.9f, 0.4f, 0.1f, 1.0f})
                .ambient(0.2f)
                .specular(0.5f)
                .shininess(64.0f);
            ax3d.set_light_dir(1.0f, 1.0f, 1.0f);
            ax3d.title("3D: Lit Surface");
        },
        640,
        960);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10. Lit Surface with Colormap (Viridis + Phong)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Golden3DPhase3, LitSurfaceColormap)
{
    run_golden_test_3d_p3(
        "3d_p3_lit_surface_colormap",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            const int          nx = 30;
            const int          ny = 30;
            std::vector<float> x_grid;
            std::vector<float> y_grid;
            std::vector<float> z_values;
            x_grid.reserve(nx);
            y_grid.reserve(ny);
            z_values.reserve(nx * ny);
            for (int i = 0; i < nx; ++i)
                x_grid.push_back(static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                y_grid.push_back(static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]) + 0.001f;
                    z_values.push_back(std::sin(r) / r);
                }

            ax.surface(x_grid, y_grid, z_values)
                .colormap(ColormapType::Plasma)
                .ambient(0.15f)
                .specular(0.5f)
                .shininess(64.0f);

            ax.set_light_dir(0.7f, 0.7f, 1.0f);
            ax.title("Lit Surface + Plasma Colormap");
        });
}

}   // namespace spectra::test

// Custom main creates a single shared App (one VkInstance) and calls ::_exit()
// after all tests complete, bypassing C++ destructors.  See golden_test_3d.cpp.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    g_app      = new spectra::App({.headless = true, .socket_path = ""});
    int result = RUN_ALL_TESTS();
    ::_exit(result);
}
