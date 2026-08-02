#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"
#include "ui/animation/camera_animator.hpp"
#include "ui/overlay/axes3d_axis_pick.hpp"
#include "ui/overlay/axes3d_pick.hpp"

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// Fixture — headless App for all regression tests
// ═══════════════════════════════════════════════════════════════════════════════

class Regression3DTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
    }
    void TearDown() override { app_.reset(); }

    std::unique_ptr<App> app_;

    // Helper: generate a surface grid
    struct SurfaceData
    {
        std::vector<float> x, y, z;
        int                nx, ny;
    };

    SurfaceData make_surface(int nx, int ny, float x0, float x1, float y0, float y1)
    {
        SurfaceData d;
        d.nx = nx;
        d.ny = ny;
        d.x.resize(nx);
        d.y.resize(ny);
        d.z.resize(nx * ny);
        for (int i = 0; i < nx; ++i)
            d.x[i] = x0 + (x1 - x0) * static_cast<float>(i) / (nx - 1);
        for (int j = 0; j < ny; ++j)
            d.y[j] = y0 + (y1 - y0) * static_cast<float>(j) / (ny - 1);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                d.z[j * nx + i] = std::sin(d.x[i]) * std::cos(d.y[j]);
        return d;
    }

    // Helper: generate a mesh grid (vertices + indices)
    struct MeshData
    {
        std::vector<float>    vertices;
        std::vector<uint32_t> indices;
    };

    MeshData make_mesh_grid(int nx, int ny)
    {
        MeshData m;
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                float x = static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f;
                float y = static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f;
                float z = std::sin(x) * std::cos(y);
                m.vertices.push_back(x);
                m.vertices.push_back(y);
                m.vertices.push_back(z);
                m.vertices.push_back(0.0f);
                m.vertices.push_back(0.0f);
                m.vertices.push_back(1.0f);
            }
        }
        for (int j = 0; j < ny - 1; ++j)
        {
            for (int i = 0; i < nx - 1; ++i)
            {
                uint32_t tl = static_cast<uint32_t>(j * nx + i);
                uint32_t tr = tl + 1;
                uint32_t bl = tl + static_cast<uint32_t>(nx);
                uint32_t br = bl + 1;
                m.indices.push_back(tl);
                m.indices.push_back(bl);
                m.indices.push_back(tr);
                m.indices.push_back(tr);
                m.indices.push_back(bl);
                m.indices.push_back(br);
            }
        }
        return m;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// 1. Lighting API Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, LightingDefaultEnabled)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    EXPECT_TRUE(ax.lighting_enabled());
}

TEST_F(Regression3DTest, LightingDefaultDirection)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    vec3  ld = ax.light_dir();
    EXPECT_FLOAT_EQ(ld.x, 1.0f);
    EXPECT_FLOAT_EQ(ld.y, 1.0f);
    EXPECT_FLOAT_EQ(ld.z, 1.0f);
}

TEST_F(Regression3DTest, LightingToggle)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.set_lighting_enabled(false);
    EXPECT_FALSE(ax.lighting_enabled());
    ax.set_lighting_enabled(true);
    EXPECT_TRUE(ax.lighting_enabled());
}

TEST_F(Regression3DTest, LightingDirectionSetFloat)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.set_light_dir(0.3f, 0.6f, 0.9f);
    vec3 ld = ax.light_dir();
    EXPECT_FLOAT_EQ(ld.x, 0.3f);
    EXPECT_FLOAT_EQ(ld.y, 0.6f);
    EXPECT_FLOAT_EQ(ld.z, 0.9f);
}

TEST_F(Regression3DTest, LightingDirectionSetVec3)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.set_light_dir(vec3{-1.0f, 0.5f, 0.0f});
    vec3 ld = ax.light_dir();
    EXPECT_FLOAT_EQ(ld.x, -1.0f);
    EXPECT_FLOAT_EQ(ld.y, 0.5f);
    EXPECT_FLOAT_EQ(ld.z, 0.0f);
}

TEST_F(Regression3DTest, LightingPerAxesIndependence)
{
    auto& fig = app_->figure();
    auto& ax1 = fig.subplot3d(1, 2, 1);
    auto& ax2 = fig.subplot3d(1, 2, 2);

    ax1.set_light_dir(1.0f, 0.0f, 0.0f);
    ax2.set_light_dir(0.0f, 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(ax1.light_dir().x, 1.0f);
    EXPECT_FLOAT_EQ(ax2.light_dir().y, 1.0f);
    EXPECT_FLOAT_EQ(ax1.light_dir().y, 0.0f);
    EXPECT_FLOAT_EQ(ax2.light_dir().x, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Material Properties Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, SurfaceMaterialDefaults)
{
    SurfaceSeries s;
    EXPECT_FLOAT_EQ(s.ambient(), 0.0f);
    EXPECT_FLOAT_EQ(s.specular(), 0.0f);
    EXPECT_FLOAT_EQ(s.shininess(), 0.0f);
}

TEST_F(Regression3DTest, SurfaceMaterialSetAndGet)
{
    SurfaceSeries s;
    s.ambient(0.2f).specular(0.6f).shininess(64.0f);
    EXPECT_FLOAT_EQ(s.ambient(), 0.2f);
    EXPECT_FLOAT_EQ(s.specular(), 0.6f);
    EXPECT_FLOAT_EQ(s.shininess(), 64.0f);
}

TEST_F(Regression3DTest, MeshMaterialDefaults)
{
    MeshSeries m;
    EXPECT_FLOAT_EQ(m.ambient(), 0.0f);
    EXPECT_FLOAT_EQ(m.specular(), 0.0f);
    EXPECT_FLOAT_EQ(m.shininess(), 0.0f);
}

TEST_F(Regression3DTest, MeshMaterialSetAndGet)
{
    MeshSeries m;
    m.ambient(0.15f).specular(0.9f).shininess(128.0f);
    EXPECT_FLOAT_EQ(m.ambient(), 0.15f);
    EXPECT_FLOAT_EQ(m.specular(), 0.9f);
    EXPECT_FLOAT_EQ(m.shininess(), 128.0f);
}

TEST_F(Regression3DTest, SurfaceMaterialChainingWithColor)
{
    SurfaceSeries s;
    auto&         ref = s.color(Color{1.0f, 0.0f, 0.0f, 1.0f})
                    .ambient(0.3f)
                    .specular(0.5f)
                    .shininess(32.0f)
                    .opacity(0.9f);
    EXPECT_FLOAT_EQ(ref.ambient(), 0.3f);
    EXPECT_FLOAT_EQ(ref.specular(), 0.5f);
    EXPECT_FLOAT_EQ(ref.shininess(), 32.0f);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.9f);
}

TEST_F(Regression3DTest, MeshMaterialChainingWithColor)
{
    MeshSeries m;
    auto&      ref = m.color(Color{0.0f, 0.0f, 1.0f, 1.0f})
                    .ambient(0.1f)
                    .specular(0.8f)
                    .shininess(256.0f)
                    .opacity(0.5f);
    EXPECT_FLOAT_EQ(ref.ambient(), 0.1f);
    EXPECT_FLOAT_EQ(ref.specular(), 0.8f);
    EXPECT_FLOAT_EQ(ref.shininess(), 256.0f);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Transparency Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, LineSeries3DOpaqueByDefault)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST_F(Regression3DTest, LineSeries3DTransparentByAlpha)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.5f}).opacity(1.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST_F(Regression3DTest, LineSeries3DTransparentByOpacity)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(0.5f);
    EXPECT_TRUE(s.is_transparent());
}

TEST_F(Regression3DTest, ScatterSeries3DTransparencyThreshold)
{
    ScatterSeries3D s;
    // 0.99 * 1.0 = 0.99 → NOT transparent (threshold is < 0.99)
    s.color(Color{1.0f, 0.0f, 0.0f, 0.99f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());

    // 0.98 * 1.0 = 0.98 → transparent
    s.color(Color{1.0f, 0.0f, 0.0f, 0.98f}).opacity(1.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST_F(Regression3DTest, SurfaceTransparentByColormapAlpha)
{
    SurfaceSeries s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
    s.colormap_alpha(true);
    EXPECT_TRUE(s.is_transparent());
}

TEST_F(Regression3DTest, MeshTransparentCombinedAlphaOpacity)
{
    MeshSeries m;
    // 0.7 * 0.7 = 0.49 → transparent
    m.color(Color{1.0f, 0.0f, 0.0f, 0.7f}).opacity(0.7f);
    EXPECT_TRUE(m.is_transparent());
}

TEST_F(Regression3DTest, BlendModeDefaults)
{
    LineSeries3D    line;
    ScatterSeries3D scatter;
    SurfaceSeries   surface;
    MeshSeries      mesh;

    EXPECT_EQ(line.blend_mode(), BlendMode::Alpha);
    EXPECT_EQ(scatter.blend_mode(), BlendMode::Alpha);
    EXPECT_EQ(surface.blend_mode(), BlendMode::Alpha);
    EXPECT_EQ(mesh.blend_mode(), BlendMode::Alpha);
}

TEST_F(Regression3DTest, BlendModeSetAndGet)
{
    LineSeries3D line;
    line.blend_mode(BlendMode::Additive);
    EXPECT_EQ(line.blend_mode(), BlendMode::Additive);

    SurfaceSeries surface;
    surface.blend_mode(BlendMode::Premultiplied);
    EXPECT_EQ(surface.blend_mode(), BlendMode::Premultiplied);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Wireframe Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, SurfaceWireframeDefault)
{
    SurfaceSeries s;
    EXPECT_FALSE(s.wireframe());
}

TEST_F(Regression3DTest, SurfaceWireframeToggle)
{
    SurfaceSeries s;
    s.wireframe(true);
    EXPECT_TRUE(s.wireframe());
    s.wireframe(false);
    EXPECT_FALSE(s.wireframe());
}

TEST_F(Regression3DTest, SurfaceWireframeMeshGeneration)
{
    auto          sd = make_surface(5, 5, -2.0f, 2.0f, -2.0f, 2.0f);
    SurfaceSeries s(sd.x, sd.y, sd.z);
    s.wireframe(true);
    s.generate_wireframe_mesh();
    EXPECT_TRUE(s.is_wireframe_mesh_generated());
    EXPECT_GT(s.wireframe_mesh().vertices.size(), 0u);
    EXPECT_GT(s.wireframe_mesh().indices.size(), 0u);
}

TEST_F(Regression3DTest, SurfaceWireframeMeshResetOnDataChange)
{
    auto          sd = make_surface(4, 4, -1.0f, 1.0f, -1.0f, 1.0f);
    SurfaceSeries s(sd.x, sd.y, sd.z);
    s.generate_wireframe_mesh();
    EXPECT_TRUE(s.is_wireframe_mesh_generated());

    auto sd2 = make_surface(4, 4, -2.0f, 2.0f, -2.0f, 2.0f);
    s.set_data(sd2.x, sd2.y, sd2.z);
    EXPECT_FALSE(s.is_wireframe_mesh_generated());
}

TEST_F(Regression3DTest, MeshWireframeDefault)
{
    MeshSeries m;
    EXPECT_FALSE(m.wireframe());
}

TEST_F(Regression3DTest, MeshWireframeToggle)
{
    MeshSeries m;
    m.wireframe(true);
    EXPECT_TRUE(m.wireframe());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. Double-Sided Rendering Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, SurfaceDoubleSidedDefault)
{
    SurfaceSeries s;
    EXPECT_TRUE(s.double_sided());
}

TEST_F(Regression3DTest, SurfaceDoubleSidedToggle)
{
    SurfaceSeries s;
    s.double_sided(false);
    EXPECT_FALSE(s.double_sided());
    s.double_sided(true);
    EXPECT_TRUE(s.double_sided());
}

TEST_F(Regression3DTest, MeshDoubleSidedDefault)
{
    MeshSeries m;
    EXPECT_TRUE(m.double_sided());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. Colormap Alpha Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, ColormapAlphaDefault)
{
    SurfaceSeries s;
    EXPECT_FALSE(s.colormap_alpha());
    EXPECT_FLOAT_EQ(s.colormap_alpha_min(), 0.1f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_max(), 1.0f);
}

TEST_F(Regression3DTest, ColormapAlphaRangeSet)
{
    SurfaceSeries s;
    s.set_colormap_alpha_range(0.2f, 0.8f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_min(), 0.2f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_max(), 0.8f);
}

TEST_F(Regression3DTest, ColormapAlphaEnableDisable)
{
    SurfaceSeries s;
    s.colormap_alpha(true);
    EXPECT_TRUE(s.colormap_alpha());
    s.colormap_alpha(false);
    EXPECT_FALSE(s.colormap_alpha());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7. MSAA Configuration Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, MSAADefault1x)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->msaa_samples(), 1u);
}

TEST_F(Regression3DTest, MSAASet4x)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    backend->set_msaa_samples(4);
    EXPECT_EQ(backend->msaa_samples(), 4u);
}

TEST_F(Regression3DTest, MSAASetBack1x)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);
    backend->set_msaa_samples(4);
    EXPECT_EQ(backend->msaa_samples(), 4u);
    backend->set_msaa_samples(1);
    EXPECT_EQ(backend->msaa_samples(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 8. Pipeline Types Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, TransparentPipelineEnumsExist)
{
    [[maybe_unused]] PipelineType lt  = PipelineType::Line3D_Transparent;
    [[maybe_unused]] PipelineType st  = PipelineType::Scatter3D_Transparent;
    [[maybe_unused]] PipelineType mt  = PipelineType::Mesh3D_Transparent;
    [[maybe_unused]] PipelineType sft = PipelineType::Surface3D_Transparent;
    [[maybe_unused]] PipelineType sw  = PipelineType::SurfaceWireframe3D;
    [[maybe_unused]] PipelineType swt = PipelineType::SurfaceWireframe3D_Transparent;
    SUCCEED();
}

TEST_F(Regression3DTest, OpaquePipelineCreation)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    EXPECT_TRUE(backend->create_pipeline(PipelineType::Line3D));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Scatter3D));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Mesh3D));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Surface3D));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Grid3D));
}

TEST_F(Regression3DTest, TwoDPipelinesUnaffected)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    EXPECT_TRUE(backend->create_pipeline(PipelineType::Line));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Scatter));
    EXPECT_TRUE(backend->create_pipeline(PipelineType::Grid));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9. Painter's Sort — Centroid Computation
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, LineSeries3DCentroid)
{
    std::vector<float> x = {0.0f, 2.0f, 4.0f};
    std::vector<float> y = {0.0f, 2.0f, 4.0f};
    std::vector<float> z = {0.0f, 2.0f, 4.0f};
    LineSeries3D       line;
    line.set_x(x).set_y(y).set_z(z);
    vec3 c = line.compute_centroid();
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, 2.0f);
}

TEST_F(Regression3DTest, ScatterSeries3DCentroid)
{
    std::vector<float> x = {1.0f, 3.0f};
    std::vector<float> y = {2.0f, 4.0f};
    std::vector<float> z = {5.0f, 7.0f};
    ScatterSeries3D    scatter;
    scatter.set_x(x).set_y(y).set_z(z);
    vec3 c = scatter.compute_centroid();
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, 6.0f);
}

TEST_F(Regression3DTest, SurfaceCentroid)
{
    auto          sd = make_surface(5, 5, -2.0f, 2.0f, -2.0f, 2.0f);
    SurfaceSeries s(sd.x, sd.y, sd.z);
    vec3          c = s.compute_centroid();
    // Symmetric grid centered at origin — centroid should be near 0
    EXPECT_NEAR(c.x, 0.0f, 0.5f);
    EXPECT_NEAR(c.y, 0.0f, 0.5f);
}

TEST_F(Regression3DTest, MeshCentroid)
{
    std::vector<float> vertices = {
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        3.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        3.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    MeshSeries            m(vertices, indices);
    vec3                  c = m.compute_centroid();
    EXPECT_FLOAT_EQ(c.x, 1.0f);
    EXPECT_FLOAT_EQ(c.y, 1.0f);
    EXPECT_FLOAT_EQ(c.z, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10. Data-to-Normalized Matrix Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, DataToNormalizedMatrixNonIdentity)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(-5.0f, 5.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.zlim(-5.0f, 5.0f);

    mat4 model       = ax.data_to_normalized_matrix();
    mat4 identity    = mat4_identity();
    bool is_identity = true;
    for (int i = 0; i < 16; ++i)
    {
        if (std::abs(model.m[i] - identity.m[i]) > 1e-6f)
        {
            is_identity = false;
            break;
        }
    }
    EXPECT_FALSE(is_identity);
}

TEST_F(Regression3DTest, BoxHalfSizeIs3)
{
    EXPECT_FLOAT_EQ(Axes3D::box_half_size(), 3.0f);
}

TEST_F(Regression3DTest, ZoomLimitsShrinks)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-1.0f, 1.0f);
    ax.zlim(-1.0f, 1.0f);

    ax.zoom_limits(0.5f);

    auto xlim = ax.x_limits();
    EXPECT_GT(xlim.min, -1.0f);
    EXPECT_LT(xlim.max, 1.0f);
}

TEST_F(Regression3DTest, ZoomLimitsExpands)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-1.0f, 1.0f);
    ax.zlim(-1.0f, 1.0f);

    ax.zoom_limits(2.0f);

    auto xlim = ax.x_limits();
    EXPECT_LT(xlim.min, -1.0f);
    EXPECT_GT(xlim.max, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 11. Camera Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, CameraDefaultProjection)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    EXPECT_EQ(ax.camera().projection_mode, Camera::ProjectionMode::Perspective);
}

TEST_F(Regression3DTest, CameraOrthographicSwitch)
{
    auto& ax                    = app_->figure().subplot3d(1, 1, 1);
    ax.camera().projection_mode = Camera::ProjectionMode::Orthographic;
    EXPECT_EQ(ax.camera().projection_mode, Camera::ProjectionMode::Orthographic);
}

TEST_F(Regression3DTest, CameraOrbitChangesPosition)
{
    auto& ax              = app_->figure().subplot3d(1, 1, 1);
    ax.camera().azimuth   = 0.0f;
    ax.camera().elevation = 0.0f;
    ax.camera().update_position_from_orbit();
    vec3 before = ax.camera().position;

    ax.camera().orbit(90.0f, 0.0f);
    vec3 after = ax.camera().position;

    EXPECT_NE(before.x, after.x);
}

TEST_F(Regression3DTest, AxisArrowPickAndAlignView)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);
    ax.zlim(0.0, 1.0);
    ax.set_viewport(Rect{0.0f, 0.0f, 800.0f, 600.0f});
    ax.camera().set_azimuth(35.0f).set_elevation(25.0f).set_distance(6.0f);

    const float            arrow_len_x = 0.18f;
    const Projected3DPoint px0         = project_axes3d_data_point(ax, {1.0f, 0.0f, 0.0f});
    const Projected3DPoint px1 = project_axes3d_data_point(ax, {1.0f + arrow_len_x, 0.0f, 0.0f});
    ASSERT_TRUE(px0.visible && px1.visible);

    const float mid_x = 0.5f * (px0.screen_x + px1.screen_x);
    const float mid_y = 0.5f * (px0.screen_y + px1.screen_y);
    EXPECT_EQ(pick_axes3d_axis_arrow(ax, mid_x, mid_y, 32.0f), Axis3DArrowPick::X);
    EXPECT_EQ(pick_axes3d_axis_arrow(ax, mid_x + 200.0f, mid_y, 32.0f), Axis3DArrowPick::None);

    ax.camera().align_view_to_axis(Camera::AxisView::PositiveY);
    EXPECT_NEAR(ax.camera().elevation, 75.0f, 0.1f);
    ax.camera().align_view_to_axis(Camera::AxisView::PositiveX);
    EXPECT_NEAR(ax.camera().azimuth, 0.0f, 0.1f);
    EXPECT_NEAR(ax.camera().elevation, 0.0f, 0.1f);
    EXPECT_NEAR(ax.camera().position.x, 6.0f, 0.1f);
}

TEST_F(Regression3DTest, CameraSerializationRoundTrip)
{
    auto& ax              = app_->figure().subplot3d(1, 1, 1);
    ax.camera().azimuth   = 123.0f;
    ax.camera().elevation = 45.0f;
    ax.camera().distance  = 7.5f;
    ax.camera().fov       = 60.0f;

    std::string json = ax.camera().serialize();
    EXPECT_FALSE(json.empty());

    Camera restored;
    restored.deserialize(json);

    EXPECT_NEAR(restored.azimuth, 123.0f, 0.1f);
    EXPECT_NEAR(restored.elevation, 45.0f, 0.1f);
    EXPECT_NEAR(restored.distance, 7.5f, 0.1f);
    EXPECT_NEAR(restored.fov, 60.0f, 0.1f);
}

TEST_F(Regression3DTest, CameraReset)
{
    auto& ax              = app_->figure().subplot3d(1, 1, 1);
    ax.camera().azimuth   = 200.0f;
    ax.camera().elevation = 80.0f;
    ax.camera().distance  = 50.0f;
    ax.camera().reset();

    EXPECT_FLOAT_EQ(ax.camera().azimuth, 45.0f);
    EXPECT_FLOAT_EQ(ax.camera().elevation, 30.0f);
    EXPECT_FLOAT_EQ(ax.camera().distance, 5.0f);
}

TEST_F(Regression3DTest, CameraIndependenceAcrossSubplots)
{
    auto& fig = app_->figure();
    auto& ax1 = fig.subplot3d(1, 2, 1);
    auto& ax2 = fig.subplot3d(1, 2, 2);

    ax1.camera().azimuth = 45.0f;
    ax2.camera().azimuth = 135.0f;

    EXPECT_FLOAT_EQ(ax1.camera().azimuth, 45.0f);
    EXPECT_FLOAT_EQ(ax2.camera().azimuth, 135.0f);
}

TEST_F(Regression3DTest, CameraViewMatrixNonZero)
{
    auto& ax              = app_->figure().subplot3d(1, 1, 1);
    ax.camera().azimuth   = 45.0f;
    ax.camera().elevation = 30.0f;
    ax.camera().distance  = 5.0f;
    ax.camera().update_position_from_orbit();

    mat4 view        = ax.camera().view_matrix();
    bool has_nonzero = false;
    for (int i = 0; i < 16; ++i)
    {
        if (view.m[i] != 0.0f)
        {
            has_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(has_nonzero);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 12. Camera Animator Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, CameraAnimatorOrbitInterpolation)
{
    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam1;
    cam1.azimuth   = 0.0f;
    cam1.elevation = 30.0f;
    cam1.distance  = 5.0f;

    Camera cam2;
    cam2.azimuth   = 180.0f;
    cam2.elevation = 30.0f;
    cam2.distance  = 5.0f;

    animator.add_keyframe(0.0f, cam1);
    animator.add_keyframe(2.0f, cam2);

    Camera mid = animator.evaluate(1.0f);
    EXPECT_NEAR(mid.azimuth, 90.0f, 1.0f);
    EXPECT_NEAR(mid.elevation, 30.0f, 1.0f);
}

TEST_F(Regression3DTest, CameraAnimatorTargetBinding)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);

    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam1;
    cam1.azimuth   = 0.0f;
    cam1.elevation = 30.0f;
    cam1.distance  = 5.0f;

    Camera cam2;
    cam2.azimuth   = 360.0f;
    cam2.elevation = 30.0f;
    cam2.distance  = 5.0f;

    animator.add_keyframe(0.0f, cam1);
    animator.add_keyframe(4.0f, cam2);

    animator.set_target_camera(&ax.camera());
    EXPECT_EQ(animator.target_camera(), &ax.camera());

    animator.evaluate_at(2.0f);
    EXPECT_NEAR(ax.camera().azimuth, 180.0f, 1.0f);
}

TEST_F(Regression3DTest, CameraAnimatorTurntable)
{
    CameraAnimator animator;

    Camera base;
    base.azimuth   = 0.0f;
    base.elevation = 30.0f;
    base.distance  = 5.0f;

    animator.create_turntable(base, 4.0f);
    EXPECT_EQ(animator.keyframe_count(), 2u);

    Camera mid = animator.evaluate(2.0f);
    EXPECT_NEAR(mid.azimuth, 180.0f, 1.0f);
}

TEST_F(Regression3DTest, CameraAnimatorSerializationRoundTrip)
{
    CameraAnimator animator;
    animator.set_path_mode(CameraPathMode::Orbit);

    Camera cam;
    cam.azimuth   = 45.0f;
    cam.elevation = 30.0f;
    cam.distance  = 7.0f;
    animator.add_keyframe(0.0f, cam);

    cam.azimuth = 135.0f;
    animator.add_keyframe(1.0f, cam);

    std::string json = animator.serialize();
    EXPECT_FALSE(json.empty());

    CameraAnimator restored;
    EXPECT_TRUE(restored.deserialize(json));
    EXPECT_EQ(restored.keyframe_count(), 2u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 13. Grid Planes & Bounding Box Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, GridPlaneDefaultAll)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::All);
}

TEST_F(Regression3DTest, GridPlaneSetAndGet)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.grid_planes(Axes3D::GridPlane::XY);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::XY);

    ax.grid_planes(Axes3D::GridPlane::None);
    EXPECT_EQ(ax.grid_planes(), Axes3D::GridPlane::None);
}

TEST_F(Regression3DTest, GridPlaneBitwiseCombination)
{
    auto combined = Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ;
    EXPECT_NE(static_cast<int>(combined), 0);
    EXPECT_NE(combined, Axes3D::GridPlane::All);
}

TEST_F(Regression3DTest, BoundingBoxDefaultEnabled)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    EXPECT_TRUE(ax.show_bounding_box());
}

TEST_F(Regression3DTest, BoundingBoxToggle)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.show_bounding_box(false);
    EXPECT_FALSE(ax.show_bounding_box());
    ax.show_bounding_box(true);
    EXPECT_TRUE(ax.show_bounding_box());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 14. Colormap Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, ColormapSetAndGet)
{
    auto  sd   = make_surface(5, 5, -2.0f, 2.0f, -2.0f, 2.0f);
    auto& ax   = app_->figure().subplot3d(1, 1, 1);
    auto& surf = ax.surface(sd.x, sd.y, sd.z);

    surf.colormap(ColormapType::Viridis);
    EXPECT_EQ(surf.colormap_type(), ColormapType::Viridis);

    surf.colormap(ColormapType::Jet);
    EXPECT_EQ(surf.colormap_type(), ColormapType::Jet);
}

TEST_F(Regression3DTest, ColormapSamplingAllTypes)
{
    for (int cm = 0; cm <= static_cast<int>(ColormapType::Grayscale); ++cm)
    {
        auto  type = static_cast<ColormapType>(cm);
        Color c0   = SurfaceSeries::sample_colormap(type, 0.0f);
        Color c1   = SurfaceSeries::sample_colormap(type, 1.0f);

        EXPECT_GE(c0.r, 0.0f);
        EXPECT_LE(c0.r, 1.0f);
        EXPECT_GE(c0.g, 0.0f);
        EXPECT_LE(c0.g, 1.0f);
        EXPECT_GE(c0.b, 0.0f);
        EXPECT_LE(c0.b, 1.0f);
        EXPECT_GE(c1.r, 0.0f);
        EXPECT_LE(c1.r, 1.0f);
    }
}

TEST_F(Regression3DTest, ColormapRangeSetAndGet)
{
    SurfaceSeries s;
    s.set_colormap_range(-10.0f, 10.0f);
    EXPECT_FLOAT_EQ(s.colormap_min(), -10.0f);
    EXPECT_FLOAT_EQ(s.colormap_max(), 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 15. Auto-Fit Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, AutoFitEncompassesData)
{
    auto&              ax = app_->figure().subplot3d(1, 1, 1);
    std::vector<float> x  = {-5.0f, 5.0f};
    std::vector<float> y  = {-10.0f, 10.0f};
    std::vector<float> z  = {-2.0f, 2.0f};
    ax.scatter3d(x, y, z);
    ax.auto_fit();

    EXPECT_LE(ax.x_limits().min, -5.0f);
    EXPECT_GE(ax.x_limits().max, 5.0f);
    EXPECT_LE(ax.y_limits().min, -10.0f);
    EXPECT_GE(ax.y_limits().max, 10.0f);
    EXPECT_LE(ax.z_limits().min, -2.0f);
    EXPECT_GE(ax.z_limits().max, 2.0f);
}

TEST_F(Regression3DTest, AutoFitMultipleSeries)
{
    auto&              ax = app_->figure().subplot3d(1, 1, 1);
    std::vector<float> x1 = {0.0f, 1.0f};
    std::vector<float> y1 = {0.0f, 1.0f};
    std::vector<float> z1 = {0.0f, 1.0f};
    ax.scatter3d(x1, y1, z1);

    std::vector<float> x2 = {-10.0f, 10.0f};
    std::vector<float> y2 = {-10.0f, 10.0f};
    std::vector<float> z2 = {-10.0f, 10.0f};
    ax.line3d(x2, y2, z2);

    ax.auto_fit();
    EXPECT_LE(ax.x_limits().min, -10.0f);
    EXPECT_GE(ax.x_limits().max, 10.0f);
}

TEST_F(Regression3DTest, AutoFitEmptyAxesNoCrash)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    EXPECT_NO_FATAL_FAILURE(ax.auto_fit());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 16. Series Lifecycle Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, ClearSeries3D)
{
    auto&              ax = app_->figure().subplot3d(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f};
    std::vector<float> y  = {0.0f, 1.0f};
    std::vector<float> z  = {0.0f, 1.0f};
    ax.scatter3d(x, y, z);
    ax.line3d(x, y, z);
    EXPECT_EQ(ax.series().size(), 2u);

    ax.clear_series();
    EXPECT_EQ(ax.series().size(), 0u);
}

TEST_F(Regression3DTest, RemoveSingleSeries3D)
{
    auto&              ax = app_->figure().subplot3d(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f};
    std::vector<float> y  = {0.0f, 1.0f};
    std::vector<float> z  = {0.0f, 1.0f};
    ax.scatter3d(x, y, z).label("first");
    ax.line3d(x, y, z).label("second");
    EXPECT_EQ(ax.series().size(), 2u);

    bool removed = ax.remove_series(0);
    EXPECT_TRUE(removed);
    EXPECT_EQ(ax.series().size(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 17. Mixed 2D + 3D Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, Mixed2DAnd3DFigure)
{
    auto& fig = app_->figure();

    auto&              ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d  = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y2d  = {0.0f, 1.0f, 0.5f, 1.5f};
    ax2d.line(x2d, y2d).color(colors::blue);

    auto&              ax3d = fig.subplot3d(2, 1, 2);
    std::vector<float> x3d  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y3d  = {0.0f, 1.0f, 0.5f};
    std::vector<float> z3d  = {0.0f, 0.5f, 1.0f};
    ax3d.scatter3d(x3d, y3d, z3d).color(colors::red);

    EXPECT_EQ(ax2d.series().size(), 1u);
    EXPECT_EQ(ax3d.series().size(), 1u);
}

TEST_F(Regression3DTest, No2DRegressions)
{
    auto&              ax = app_->figure().subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y  = {0.0f, 1.0f, 4.0f, 9.0f};

    auto& line    = ax.line(x, y).color(colors::blue).width(2.0f);
    auto& scatter = ax.scatter(x, y).color(colors::red).size(5.0f);

    EXPECT_EQ(line.point_count(), 4u);
    EXPECT_EQ(scatter.point_count(), 4u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 18. FrameUBO Layout Regression
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, FrameUBOSize)
{
    EXPECT_EQ(sizeof(FrameUBO), 240u);
}

TEST_F(Regression3DTest, SeriesPushConstantsSize)
{
    EXPECT_EQ(sizeof(SeriesPushConstants), 96u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 19. Render Smoke Tests (headless)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, RenderLitSurfaceSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sd = make_surface(10, 10, -2.0f, 2.0f, -2.0f, 2.0f);
    ax.surface(sd.x, sd.y, sd.z)
        .color(colors::orange)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);

    ax.set_light_dir(1.0f, 1.0f, 1.0f);
    ax.set_lighting_enabled(true);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderTransparentScatterSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    std::vector<float> x, y, z;
    for (int i = 0; i < 100; ++i)
    {
        float t = static_cast<float>(i) * 0.1f;
        x.push_back(std::cos(t));
        y.push_back(std::sin(t));
        z.push_back(t * 0.1f);
    }
    ax.scatter3d(x, y, z)
        .color(Color{0.0f, 0.5f, 1.0f, 0.5f})
        .size(6.0f)
        .blend_mode(BlendMode::Alpha);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderWireframeSurfaceSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sd = make_surface(10, 10, -2.0f, 2.0f, -2.0f, 2.0f);
    ax.surface(sd.x, sd.y, sd.z).color(colors::green).wireframe(true);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderLitMeshSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto md = make_mesh_grid(10, 10);
    ax.mesh(md.vertices, md.indices)
        .color(colors::cyan)
        .ambient(0.15f)
        .specular(0.6f)
        .shininess(32.0f);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderTransparentSurfaceSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sd = make_surface(10, 10, -2.0f, 2.0f, -2.0f, 2.0f);
    ax.surface(sd.x, sd.y, sd.z)
        .color(Color{1.0f, 0.5f, 0.0f, 0.6f})
        .ambient(0.2f)
        .specular(0.4f)
        .shininess(32.0f);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderMixedOpaqueAndTransparentSmoke)
{
    auto& fig = app_->figure({.width = 128, .height = 128});
    auto& ax  = fig.subplot3d(1, 1, 1);

    // Opaque scatter
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f};
    std::vector<float> z = {0.0f, 0.5f, 1.0f};
    ax.scatter3d(x, y, z).color(colors::red).size(8.0f);

    // Transparent line
    ax.line3d(x, y, z).color(Color{0.0f, 0.0f, 1.0f, 0.4f}).width(3.0f);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

TEST_F(Regression3DTest, RenderMultiSubplot3DSmoke)
{
    auto& fig = app_->figure({.width = 256, .height = 256});

    auto&              ax1 = fig.subplot3d(2, 2, 1);
    std::vector<float> x   = {0.0f, 1.0f, 2.0f};
    std::vector<float> y   = {0.0f, 1.0f, 0.5f};
    std::vector<float> z   = {0.0f, 0.5f, 1.0f};
    ax1.scatter3d(x, y, z).color(colors::red);

    auto& ax2 = fig.subplot3d(2, 2, 2);
    ax2.line3d(x, y, z).color(colors::green);

    auto& ax3 = fig.subplot3d(2, 2, 3);
    auto  sd  = make_surface(8, 8, -2.0f, 2.0f, -2.0f, 2.0f);
    ax3.surface(sd.x, sd.y, sd.z).color(colors::orange);

    auto& ax4 = fig.subplot3d(2, 2, 4);
    auto  md  = make_mesh_grid(8, 8);
    ax4.mesh(md.vertices, md.indices).color(colors::cyan);

    EXPECT_NO_FATAL_FAILURE(app_->run());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 20. Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(Regression3DTest, SinglePoint3D)
{
    auto&              ax      = app_->figure().subplot3d(1, 1, 1);
    std::vector<float> x       = {1.0f};
    std::vector<float> y       = {2.0f};
    std::vector<float> z       = {3.0f};
    auto&              scatter = ax.scatter3d(x, y, z);
    EXPECT_EQ(scatter.point_count(), 1u);
    vec3 c = scatter.compute_centroid();
    EXPECT_FLOAT_EQ(c.x, 1.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, 3.0f);
}

TEST_F(Regression3DTest, LargeDataset10K)
{
    auto&              ax = app_->figure().subplot3d(1, 1, 1);
    const size_t       N  = 10000;
    std::vector<float> x(N), y(N), z(N);
    for (size_t i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) * 0.001f;
        x[i]    = std::cos(t) * t;
        y[i]    = std::sin(t) * t;
        z[i]    = t;
    }
    auto& scatter = ax.scatter3d(x, y, z);
    EXPECT_EQ(scatter.point_count(), N);
}

TEST_F(Regression3DTest, NegativeAxisLimits)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(-100.0f, -50.0f);
    ax.ylim(-200.0f, -100.0f);
    ax.zlim(-300.0f, -200.0f);

    EXPECT_FLOAT_EQ(ax.x_limits().min, -100.0f);
    EXPECT_FLOAT_EQ(ax.x_limits().max, -50.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().min, -200.0f);
    EXPECT_FLOAT_EQ(ax.y_limits().max, -100.0f);
    EXPECT_FLOAT_EQ(ax.z_limits().min, -300.0f);
    EXPECT_FLOAT_EQ(ax.z_limits().max, -200.0f);
}

TEST_F(Regression3DTest, AxisLabels3D)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");
    EXPECT_EQ(ax.get_xlabel(), "X");
    EXPECT_EQ(ax.get_ylabel(), "Y");
    EXPECT_EQ(ax.get_zlabel(), "Z");
}

TEST_F(Regression3DTest, TickComputation3D)
{
    auto& ax = app_->figure().subplot3d(1, 1, 1);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);
    ax.zlim(0.0f, 10.0f);

    auto x_ticks = ax.compute_x_ticks();
    auto y_ticks = ax.compute_y_ticks();
    auto z_ticks = ax.compute_z_ticks();

    EXPECT_GT(x_ticks.positions.size(), 0u);
    EXPECT_GT(y_ticks.positions.size(), 0u);
    EXPECT_GT(z_ticks.positions.size(), 0u);
    EXPECT_EQ(x_ticks.positions.size(), x_ticks.labels.size());
}

TEST_F(Regression3DTest, SurfaceMeshTopologyCorrect)
{
    auto&     ax = app_->figure().subplot3d(1, 1, 1);
    const int nx = 6, ny = 4;
    auto      sd   = make_surface(nx, ny, -1.0f, 1.0f, -1.0f, 1.0f);
    auto&     surf = ax.surface(sd.x, sd.y, sd.z);
    surf.generate_mesh();
    EXPECT_EQ(surf.mesh().triangle_count, static_cast<size_t>((nx - 1) * (ny - 1) * 2));
}

TEST_F(Regression3DTest, MeshCustomGeometryTriangleCount)
{
    std::vector<float> vertices = {
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    MeshSeries            m(vertices, indices);
    EXPECT_EQ(m.vertex_count(), 3u);
    EXPECT_EQ(m.triangle_count(), 1u);
}
