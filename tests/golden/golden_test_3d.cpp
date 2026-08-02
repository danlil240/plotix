#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"

namespace fs = std::filesystem;

// Single shared App — one VkInstance for the entire test binary.  This avoids
// the non-deterministic SIGSEGV in the NVIDIA Vulkan driver that occurs when
// multiple VkInstances are created and destroyed sequentially in the same
// process.  The App is heap-allocated (never deleted) so that its VkInstance
// destructor is never invoked; ::_exit() in main() terminates the process
// without running any C++ destructors.
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

static void run_golden_test_3d(const std::string&                 scene_name,
                               std::function<void(App&, Figure&)> setup_scene,
                               uint32_t                           width             = 640,
                               uint32_t                           height            = 480,
                               double                             tolerance_percent = 2.0,
                               double                             max_mae           = 3.0,
                               size_t max_differing_pixels = std::numeric_limits<size_t>::max())
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
    // Clean up figure from registry so subsequent tests start with a fresh state.
    // Notify the renderer to purge cached GPU data for all axes of this figure
    // BEFORE unregistering — once the figure is destroyed the axes pointers are
    // dangling, and the allocator may recycle their addresses for the next test.
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
        std::cout << "[GOLDEN 3D] Updated baseline: " << baseline_path << "\n";
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

    if (max_differing_pixels != std::numeric_limits<size_t>::max())
    {
        EXPECT_LE(diff.differing_pixels, max_differing_pixels)
            << "Scene: " << scene_name << " exceeded differing-pixel budget"
            << "\n  Differing pixels: " << diff.differing_pixels
            << "\n  Allowed: " << max_differing_pixels << "\n  Diff image: " << diff_path;
    }
}

TEST(Golden3D, Scatter3D_Basic)
{
    run_golden_test_3d("3d_scatter_basic",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
                           std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
                           std::vector<float> z = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f};

                           ax.scatter3d(x, y, z).color(colors::blue).size(8.0f);
                           ax.title("3D Scatter Plot");
                           ax.xlabel("X Axis");
                           ax.ylabel("Y Axis");
                           ax.zlabel("Z Axis");
                       });
}

TEST(Golden3D, Scatter3D_LargeDataset)
{
    run_golden_test_3d("3d_scatter_large",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x;
                           std::vector<float> y;
                           std::vector<float> z;
                           for (int i = 0; i < 1000; ++i)
                           {
                               float t = static_cast<float>(i) * 0.01f;
                               x.push_back(std::cos(t) * t);
                               y.push_back(std::sin(t) * t);
                               z.push_back(t);
                           }

                           ax.scatter3d(x, y, z).color(colors::red).size(3.0f);
                           ax.title("Spiral Scatter");
                       });
}

TEST(Golden3D, Line3D_Basic)
{
    run_golden_test_3d("3d_line_basic",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
                           std::vector<float> y = {0.0f, 1.0f, 0.0f, 1.0f};
                           std::vector<float> z = {0.0f, 0.0f, 1.0f, 1.0f};

                           ax.line3d(x, y, z).color(colors::green).width(3.0f);
                           ax.title("3D Line Plot");
                       });
}

TEST(Golden3D, Line3D_Helix)
{
    run_golden_test_3d("3d_line_helix",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x;
                           std::vector<float> y;
                           std::vector<float> z;
                           for (int i = 0; i < 200; ++i)
                           {
                               float t = static_cast<float>(i) * 0.1f;
                               x.push_back(std::cos(t));
                               y.push_back(std::sin(t));
                               z.push_back(t * 0.1f);
                           }

                           ax.line3d(x, y, z).color(colors::cyan).width(2.5f);
                           ax.title("Helix");
                       });
}

TEST(Golden3D, Surface_Basic)
{
    run_golden_test_3d("3d_surface_basic",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x_grid;
                           std::vector<float> y_grid;
                           std::vector<float> z_values;
                           const int          nx = 20;
                           const int          ny = 20;
                           x_grid.reserve(nx);
                           y_grid.reserve(ny);
                           z_values.reserve(nx * ny);
                           for (int i = 0; i < nx; ++i)
                               x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
                           for (int j = 0; j < ny; ++j)
                               y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);

                           for (int j = 0; j < ny; ++j)
                           {
                               for (int i = 0; i < nx; ++i)
                               {
                                   float x = x_grid[i];
                                   float y = y_grid[j];
                                   float z = std::sin(x) * std::cos(y);
                                   z_values.push_back(z);
                               }
                           }

                           ax.surface(x_grid, y_grid, z_values).color(colors::orange);
                           ax.title("Surface: sin(x)*cos(y)");
                       });
}

TEST(Golden3D, BoundingBox)
{
    run_golden_test_3d("3d_bounding_box",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           ax.xlim(-1.0f, 1.0f);
                           ax.ylim(-1.0f, 1.0f);
                           ax.zlim(-1.0f, 1.0f);
                           ax.show_bounding_box(true);
                           ax.title("Bounding Box Only");
                       });
}

TEST(Golden3D, GridPlanes_XY)
{
    run_golden_test_3d("3d_grid_xy",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           ax.grid_planes(Axes3D::GridPlane::XY);

                           std::vector<float> x = {0.0f, 1.0f};
                           std::vector<float> y = {0.0f, 1.0f};
                           std::vector<float> z = {0.0f, 1.0f};
                           ax.scatter3d(x, y, z).color(colors::blue);

                           ax.title("XY Grid Plane");
                       });
}

TEST(Golden3D, GridPlanes_All)
{
    run_golden_test_3d("3d_grid_all",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           ax.grid_planes(Axes3D::GridPlane::All);

                           std::vector<float> x = {0.5f};
                           std::vector<float> y = {0.5f};
                           std::vector<float> z = {0.5f};
                           ax.scatter3d(x, y, z).color(colors::red).size(10.0f);

                           ax.title("All Grid Planes");
                       });
}

TEST(Golden3D, CameraAngle_Front)
{
    run_golden_test_3d("3d_camera_front",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x = {0.0f, 1.0f, 0.5f};
                           std::vector<float> y = {0.0f, 0.0f, 1.0f};
                           std::vector<float> z = {0.0f, 1.0f, 0.5f};
                           ax.scatter3d(x, y, z).color(colors::magenta).size(8.0f);

                           ax.camera().azimuth   = 0.0f;
                           ax.camera().elevation = 0.0f;
                           ax.camera().distance  = 5.0f;

                           ax.title("Front View");
                       });
}

TEST(Golden3D, CameraAngle_Top)
{
    run_golden_test_3d("3d_camera_top",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x = {0.0f, 1.0f, 0.5f};
                           std::vector<float> y = {0.0f, 0.0f, 1.0f};
                           std::vector<float> z = {0.0f, 1.0f, 0.5f};
                           ax.scatter3d(x, y, z).color(colors::yellow).size(8.0f);

                           ax.camera().azimuth   = 0.0f;
                           ax.camera().elevation = 90.0f;
                           ax.camera().distance  = 5.0f;

                           ax.title("Top View");
                       });
}

TEST(Golden3D, DepthOcclusion)
{
    run_golden_test_3d("3d_depth_occlusion",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x_front = {0.0f};
                           std::vector<float> y_front = {0.0f};
                           std::vector<float> z_front = {1.0f};

                           std::vector<float> x_back = {0.0f};
                           std::vector<float> y_back = {0.0f};
                           std::vector<float> z_back = {-1.0f};

                           ax.scatter3d(x_back, y_back, z_back).color(colors::blue).size(20.0f);
                           ax.scatter3d(x_front, y_front, z_front).color(colors::red).size(15.0f);

                           ax.title("Depth Test: Red in Front");
                       });
}

TEST(Golden3D, Mixed2DAnd3D)
{
    run_golden_test_3d(
        "3d_mixed_2d_3d",
        [](App& /*app*/, Figure& fig)
        {
            auto&              ax2d = fig.subplot(2, 1, 1);
            std::vector<float> x2d  = {0.0f, 1.0f, 2.0f, 3.0f};
            std::vector<float> y2d  = {0.0f, 1.0f, 0.5f, 1.5f};
            ax2d.line(x2d, y2d).color(colors::green);
            ax2d.title("2D Line");

            auto&              ax3d = fig.subplot3d(2, 1, 2);
            std::vector<float> x3d  = {0.0f, 1.0f, 2.0f};
            std::vector<float> y3d  = {0.0f, 1.0f, 0.5f};
            std::vector<float> z3d  = {0.0f, 0.5f, 1.0f};
            ax3d.scatter3d(x3d, y3d, z3d).color(colors::blue);
            ax3d.title("3D Scatter");
        },
        640,
        960);
}

TEST(Golden3D, Mesh3D_Triangle)
{
    run_golden_test_3d(
        "3d_mesh_triangle",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            // A simple colored triangle with normals
            std::vector<float> vertices = {
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                2.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                1.0f,
                2.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            std::vector<uint32_t> indices = {0, 1, 2};

            ax.mesh(vertices, indices).color(colors::cyan);
            ax.title("Mesh: Single Triangle");
        },
        640,
        480,
        4.0,
        4.0);
}

TEST(Golden3D, Mesh3D_Quad)
{
    run_golden_test_3d(
        "3d_mesh_quad",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            // A quad made of 2 triangles
            std::vector<float> vertices = {
                -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                1.0f,  1.0f,  0.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f,
            };
            std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

            ax.mesh(vertices, indices).color(colors::green);
            ax.title("Mesh: Quad");
        },
        640,
        480,
        6.0,
        4.0);
}

TEST(Golden3D, Surface_Colormap)
{
    run_golden_test_3d("3d_surface_colormap",
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
                           {
                               for (int i = 0; i < nx; ++i)
                               {
                                   float x = x_grid[i];
                                   float y = y_grid[j];
                                   float r = std::sqrt(x * x + y * y) + 0.001f;
                                   z_values.push_back(std::sin(r) / r);
                               }
                           }

                           ax.surface(x_grid, y_grid, z_values).colormap(ColormapType::Viridis);
                           ax.title("Surface: sinc(r) + Viridis");
                       });
}

TEST(Golden3D, CameraAngle_Orthographic)
{
    run_golden_test_3d(
        "3d_camera_ortho",
        [](App& /*app*/, Figure& fig)
        {
            auto& ax = fig.subplot3d(1, 1, 1);

            std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
            std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f};
            std::vector<float> z = {0.0f, 0.5f, 1.0f, 0.5f};

            ax.scatter3d(x, y, z).color(colors::red).size(8.0f);

            // Offset line slightly above scatter points so they don't fight for
            // the same depth — avoids non-deterministic GPU z-fighting at equal depth.
            std::vector<float> z_line = {0.02f, 0.52f, 1.02f, 0.52f};
            ax.line3d(x, y, z_line).color(colors::blue).width(2.0f);

            ax.camera().projection_mode = Camera::ProjectionMode::Orthographic;
            ax.camera().ortho_size      = 5.0f;
            ax.camera().azimuth         = 45.0f;
            ax.camera().elevation       = 30.0f;

            ax.title("Orthographic Projection");
        },
        640,
        480,
        2.0,
        3.0,
        // Orthographic edge rasterization differs slightly across Mesa/LLVM
        // versions on CI (single-digit/low-teens pixels).
        16);
}

TEST(Golden3D, MultiSubplot3D)
{
    run_golden_test_3d(
        "3d_multi_subplot",
        [](App& /*app*/, Figure& fig)
        {
            // 2x2 grid of 3D subplots
            auto&              ax1 = fig.subplot3d(2, 2, 1);
            std::vector<float> x1  = {0.0f, 1.0f, 2.0f};
            std::vector<float> y1  = {0.0f, 1.0f, 0.5f};
            std::vector<float> z1  = {0.0f, 0.5f, 1.0f};
            ax1.scatter3d(x1, y1, z1).color(colors::red).size(6.0f);
            ax1.title("Scatter");

            auto&              ax2 = fig.subplot3d(2, 2, 2);
            std::vector<float> x2;
            std::vector<float> y2;
            std::vector<float> z2;
            for (int i = 0; i < 100; ++i)
            {
                float t = static_cast<float>(i) * 0.1f;
                x2.push_back(std::cos(t));
                y2.push_back(std::sin(t));
                z2.push_back(t * 0.1f);
            }
            ax2.line3d(x2, y2, z2).color(colors::green).width(2.0f);
            ax2.title("Helix");

            auto&              ax3 = fig.subplot3d(2, 2, 3);
            const int          nx  = 15;
            const int          ny  = 15;
            std::vector<float> xg;
            std::vector<float> yg;
            std::vector<float> zv;
            xg.reserve(nx);
            yg.reserve(ny);
            zv.reserve(nx * ny);

            for (int i = 0; i < nx; ++i)
                xg.push_back(static_cast<float>(i) - 7.0f);
            for (int j = 0; j < ny; ++j)
                yg.push_back(static_cast<float>(j) - 7.0f);
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    zv.push_back(std::sin(xg[i] * 0.5f) * std::cos(yg[j] * 0.5f));
            ax3.surface(xg, yg, zv).color(colors::orange);
            ax3.title("Surface");

            auto& ax4 = fig.subplot3d(2, 2, 4);
            ax4.xlim(-1.0f, 1.0f);
            ax4.ylim(-1.0f, 1.0f);
            ax4.zlim(-1.0f, 1.0f);
            ax4.grid_planes(Axes3D::GridPlane::All);
            ax4.title("Empty + Grids");
        },
        800,
        600);
}

TEST(Golden3D, CombinedLineAndScatter3D)
{
    run_golden_test_3d("3d_combined_line_scatter",
                       [](App& /*app*/, Figure& fig)
                       {
                           auto& ax = fig.subplot3d(1, 1, 1);

                           std::vector<float> x;
                           std::vector<float> y;
                           std::vector<float> z;
                           x.reserve(50);
                           y.reserve(50);
                           z.reserve(50);
                           for (int i = 0; i < 50; ++i)
                           {
                               float t = static_cast<float>(i) * 0.2f;
                               x.push_back(std::cos(t) * 2.0f);
                               y.push_back(std::sin(t) * 2.0f);
                               z.push_back(t * 0.2f);
                           }

                           ax.line3d(x, y, z).color(colors::blue).width(2.0f);
                           ax.scatter3d(x, y, z).color(colors::red).size(4.0f);
                           ax.title("Line + Scatter Combined");
                       });
}

}   // namespace spectra::test

// Custom main creates a single shared App (one VkInstance) and calls ::_exit()
// after all tests complete, bypassing C++ destructors.  This avoids the
// non-deterministic SIGSEGV in the NVIDIA Vulkan driver during VkInstance
// teardown that occurs when multiple instances are destroyed in the same process.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    g_app      = new spectra::App({.headless = true, .socket_path = ""});
    int result = RUN_ALL_TESTS();
    ::_exit(result);
}
