#include <gtest/gtest.h>
#include <spectra/app.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series3d.hpp>

#include "render/backend.hpp"

using namespace spectra;

TEST(Pipeline3D, Line3DCreation)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Line3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, Scatter3DCreation)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, Grid3DCreation)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Grid3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, DepthTestingEnabled)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend = app.backend();

    auto line_pipeline    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter_pipeline = backend->create_pipeline(PipelineType::Scatter3D);
    auto grid_pipeline    = backend->create_pipeline(PipelineType::Grid3D);

    EXPECT_TRUE(line_pipeline);
    EXPECT_TRUE(scatter_pipeline);
    EXPECT_TRUE(grid_pipeline);
}

TEST(Pipeline3D, EnumTypesExist)
{
    // Verify the enum values exist (compile-time check)
    [[maybe_unused]] PipelineType line3d    = PipelineType::Line3D;
    [[maybe_unused]] PipelineType scatter3d = PipelineType::Scatter3D;
    [[maybe_unused]] PipelineType grid3d    = PipelineType::Grid3D;
    [[maybe_unused]] PipelineType mesh3d    = PipelineType::Mesh3D;
    [[maybe_unused]] PipelineType surface3d = PipelineType::Surface3D;

    SUCCEED();
}

TEST(DepthBuffer, CreatedWithSwapchain)
{
    AppConfig config;
    config.headless = true;
    App app(config);

    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Scatter3D);

    EXPECT_TRUE(pipeline);
}

TEST(Pipeline2D, UnaffectedBy3D)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend = app.backend();

    auto line_2d    = backend->create_pipeline(PipelineType::Line);
    auto scatter_2d = backend->create_pipeline(PipelineType::Scatter);
    auto grid_2d    = backend->create_pipeline(PipelineType::Grid);

    EXPECT_TRUE(line_2d);
    EXPECT_TRUE(scatter_2d);
    EXPECT_TRUE(grid_2d);
}

TEST(Pipeline2D3D, CanCoexist)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend = app.backend();

    auto line_2d    = backend->create_pipeline(PipelineType::Line);
    auto line_3d    = backend->create_pipeline(PipelineType::Line3D);
    auto scatter_2d = backend->create_pipeline(PipelineType::Scatter);
    auto scatter_3d = backend->create_pipeline(PipelineType::Scatter3D);

    EXPECT_TRUE(line_2d);
    EXPECT_TRUE(line_3d);
    EXPECT_TRUE(scatter_2d);
    EXPECT_TRUE(scatter_3d);
}

// --- Surface3D and Mesh3D pipeline creation ---

TEST(Pipeline3D, Surface3DCreation)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Surface3D);
    EXPECT_TRUE(pipeline);
}

TEST(Pipeline3D, Mesh3DCreation)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend  = app.backend();
    auto  pipeline = backend->create_pipeline(PipelineType::Mesh3D);
    EXPECT_TRUE(pipeline);
}

// --- Lighting API tests ---

TEST(Lighting, Axes3DDefaultLightDir)
{
    Axes3D axes;
    vec3   ld = axes.light_dir();
    EXPECT_FLOAT_EQ(ld.x, 1.0f);
    EXPECT_FLOAT_EQ(ld.y, 1.0f);
    EXPECT_FLOAT_EQ(ld.z, 1.0f);
}

TEST(Lighting, Axes3DSetLightDir)
{
    Axes3D axes;
    axes.set_light_dir(0.5f, 0.7f, 1.0f);
    vec3 ld = axes.light_dir();
    EXPECT_FLOAT_EQ(ld.x, 0.5f);
    EXPECT_FLOAT_EQ(ld.y, 0.7f);
    EXPECT_FLOAT_EQ(ld.z, 1.0f);
}

TEST(Lighting, Axes3DSetLightDirVec3)
{
    Axes3D axes;
    axes.set_light_dir(vec3{-1.0f, 0.0f, 0.5f});
    vec3 ld = axes.light_dir();
    EXPECT_FLOAT_EQ(ld.x, -1.0f);
    EXPECT_FLOAT_EQ(ld.y, 0.0f);
    EXPECT_FLOAT_EQ(ld.z, 0.5f);
}

TEST(Lighting, Axes3DLightingEnabledDefault)
{
    Axes3D axes;
    EXPECT_TRUE(axes.lighting_enabled());
}

TEST(Lighting, Axes3DLightingDisable)
{
    Axes3D axes;
    axes.set_lighting_enabled(false);
    EXPECT_FALSE(axes.lighting_enabled());
    axes.set_lighting_enabled(true);
    EXPECT_TRUE(axes.lighting_enabled());
}

// --- Material properties tests ---

TEST(Material, SurfaceSeriesDefaults)
{
    SurfaceSeries s;
    EXPECT_FLOAT_EQ(s.ambient(), 0.0f);
    EXPECT_FLOAT_EQ(s.specular(), 0.0f);
    EXPECT_FLOAT_EQ(s.shininess(), 0.0f);
}

TEST(Material, SurfaceSeriesSetProperties)
{
    SurfaceSeries s;
    s.ambient(0.2f).specular(0.5f).shininess(64.0f);
    EXPECT_FLOAT_EQ(s.ambient(), 0.2f);
    EXPECT_FLOAT_EQ(s.specular(), 0.5f);
    EXPECT_FLOAT_EQ(s.shininess(), 64.0f);
}

TEST(Material, MeshSeriesDefaults)
{
    MeshSeries m;
    EXPECT_FLOAT_EQ(m.ambient(), 0.0f);
    EXPECT_FLOAT_EQ(m.specular(), 0.0f);
    EXPECT_FLOAT_EQ(m.shininess(), 0.0f);
}

TEST(Material, MeshSeriesSetProperties)
{
    MeshSeries m;
    m.ambient(0.1f).specular(0.8f).shininess(128.0f);
    EXPECT_FLOAT_EQ(m.ambient(), 0.1f);
    EXPECT_FLOAT_EQ(m.specular(), 0.8f);
    EXPECT_FLOAT_EQ(m.shininess(), 128.0f);
}

TEST(Material, SurfaceSeriesChaining)
{
    SurfaceSeries s;
    auto&         ref = s.ambient(0.3f)
                    .specular(0.4f)
                    .shininess(32.0f)
                    .color(Color{1.0f, 0.0f, 0.0f, 1.0f})
                    .opacity(0.8f);
    EXPECT_FLOAT_EQ(ref.ambient(), 0.3f);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.8f);
}

// --- MSAA configuration tests ---

TEST(MSAA, DefaultSampleCount)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend = app.backend();
    EXPECT_EQ(backend->msaa_samples(), 1u);
}

TEST(MSAA, SetSampleCount4x)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto* backend = app.backend();
    backend->set_msaa_samples(4);
    EXPECT_EQ(backend->msaa_samples(), 4u);
}

// --- Transparency detection tests ---

TEST(Transparency, OpaqueSeriesDetection)
{
    SurfaceSeries s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(1.0f);
    float effective_alpha = s.color().a * s.opacity();
    EXPECT_GE(effective_alpha, 0.99f);
}

TEST(Transparency, TransparentSeriesDetection)
{
    SurfaceSeries s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.5f}).opacity(1.0f);
    float effective_alpha = s.color().a * s.opacity();
    EXPECT_LT(effective_alpha, 0.99f);
}

TEST(Transparency, OpacityMakesTransparent)
{
    SurfaceSeries s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(0.5f);
    float effective_alpha = s.color().a * s.opacity();
    EXPECT_LT(effective_alpha, 0.99f);
}

// --- Centroid computation for painter's sort ---

TEST(PainterSort, LineSeries3DCentroid)
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

TEST(PainterSort, ScatterSeries3DCentroid)
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
