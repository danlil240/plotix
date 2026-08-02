#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

// ─── BlendMode enum tests ───────────────────────────────────────────────────

TEST(BlendMode, EnumValues)
{
    EXPECT_EQ(static_cast<int>(BlendMode::Alpha), 0);
    EXPECT_EQ(static_cast<int>(BlendMode::Additive), 1);
    EXPECT_EQ(static_cast<int>(BlendMode::Premultiplied), 2);
}

// ─── LineSeries3D transparency ──────────────────────────────────────────────

TEST(LineSeries3DTransparency, DefaultBlendMode)
{
    LineSeries3D s;
    EXPECT_EQ(s.blend_mode(), BlendMode::Alpha);
}

TEST(LineSeries3DTransparency, SetBlendMode)
{
    LineSeries3D s;
    s.blend_mode(BlendMode::Additive);
    EXPECT_EQ(s.blend_mode(), BlendMode::Additive);
}

TEST(LineSeries3DTransparency, IsTransparentOpaque)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST(LineSeries3DTransparency, IsTransparentByColor)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.5f}).opacity(1.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST(LineSeries3DTransparency, IsTransparentByOpacity)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(0.5f);
    EXPECT_TRUE(s.is_transparent());
}

TEST(LineSeries3DTransparency, BlendModeChaining)
{
    LineSeries3D s;
    auto&        ref =
        s.color(Color{1.0f, 0.0f, 0.0f, 0.5f}).opacity(0.7f).blend_mode(BlendMode::Premultiplied);
    EXPECT_EQ(ref.blend_mode(), BlendMode::Premultiplied);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.7f);
    EXPECT_TRUE(ref.is_transparent());
}

// ─── ScatterSeries3D transparency ───────────────────────────────────────────

TEST(ScatterSeries3DTransparency, DefaultBlendMode)
{
    ScatterSeries3D s;
    EXPECT_EQ(s.blend_mode(), BlendMode::Alpha);
}

TEST(ScatterSeries3DTransparency, SetBlendMode)
{
    ScatterSeries3D s;
    s.blend_mode(BlendMode::Additive);
    EXPECT_EQ(s.blend_mode(), BlendMode::Additive);
}

TEST(ScatterSeries3DTransparency, IsTransparentOpaque)
{
    ScatterSeries3D s;
    s.color(Color{0.0f, 1.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST(ScatterSeries3DTransparency, IsTransparentByColor)
{
    ScatterSeries3D s;
    s.color(Color{0.0f, 1.0f, 0.0f, 0.3f});
    EXPECT_TRUE(s.is_transparent());
}

TEST(ScatterSeries3DTransparency, IsTransparentByOpacity)
{
    ScatterSeries3D s;
    s.color(Color{0.0f, 1.0f, 0.0f, 1.0f}).opacity(0.2f);
    EXPECT_TRUE(s.is_transparent());
}

// ─── SurfaceSeries transparency ─────────────────────────────────────────────

TEST(SurfaceSeriesTransparency, DefaultBlendMode)
{
    SurfaceSeries s;
    EXPECT_EQ(s.blend_mode(), BlendMode::Alpha);
}

TEST(SurfaceSeriesTransparency, SetBlendMode)
{
    SurfaceSeries s;
    s.blend_mode(BlendMode::Premultiplied);
    EXPECT_EQ(s.blend_mode(), BlendMode::Premultiplied);
}

TEST(SurfaceSeriesTransparency, DefaultDoubleSided)
{
    SurfaceSeries s;
    EXPECT_TRUE(s.double_sided());
}

TEST(SurfaceSeriesTransparency, SetDoubleSided)
{
    SurfaceSeries s;
    s.double_sided(false);
    EXPECT_FALSE(s.double_sided());
}

TEST(SurfaceSeriesTransparency, DefaultWireframe)
{
    SurfaceSeries s;
    EXPECT_FALSE(s.wireframe());
}

TEST(SurfaceSeriesTransparency, SetWireframe)
{
    SurfaceSeries s;
    s.wireframe(true);
    EXPECT_TRUE(s.wireframe());
}

TEST(SurfaceSeriesTransparency, IsTransparentOpaque)
{
    SurfaceSeries s;
    s.color(Color{0.0f, 0.0f, 1.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST(SurfaceSeriesTransparency, IsTransparentByColor)
{
    SurfaceSeries s;
    s.color(Color{0.0f, 0.0f, 1.0f, 0.6f});
    EXPECT_TRUE(s.is_transparent());
}

TEST(SurfaceSeriesTransparency, IsTransparentByColormapAlpha)
{
    SurfaceSeries s;
    s.color(Color{0.0f, 0.0f, 1.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
    s.colormap_alpha(true);
    EXPECT_TRUE(s.is_transparent());
}

TEST(SurfaceSeriesTransparency, ColormapAlphaDefaults)
{
    SurfaceSeries s;
    EXPECT_FALSE(s.colormap_alpha());
    EXPECT_FLOAT_EQ(s.colormap_alpha_min(), 0.1f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_max(), 1.0f);
}

TEST(SurfaceSeriesTransparency, ColormapAlphaRange)
{
    SurfaceSeries s;
    s.set_colormap_alpha_range(0.2f, 0.8f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_min(), 0.2f);
    EXPECT_FLOAT_EQ(s.colormap_alpha_max(), 0.8f);
}

TEST(SurfaceSeriesTransparency, MaterialChaining)
{
    SurfaceSeries s;
    auto&         ref = s.color(Color{1.0f, 0.0f, 0.0f, 0.5f})
                    .opacity(0.8f)
                    .blend_mode(BlendMode::Alpha)
                    .double_sided(true)
                    .wireframe(false)
                    .ambient(0.2f)
                    .specular(0.5f)
                    .shininess(64.0f);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.8f);
    EXPECT_EQ(ref.blend_mode(), BlendMode::Alpha);
    EXPECT_TRUE(ref.double_sided());
    EXPECT_FALSE(ref.wireframe());
    EXPECT_FLOAT_EQ(ref.ambient(), 0.2f);
    EXPECT_FLOAT_EQ(ref.specular(), 0.5f);
    EXPECT_FLOAT_EQ(ref.shininess(), 64.0f);
}

// ─── MeshSeries transparency ────────────────────────────────────────────────

TEST(MeshSeriesTransparency, DefaultBlendMode)
{
    MeshSeries m;
    EXPECT_EQ(m.blend_mode(), BlendMode::Alpha);
}

TEST(MeshSeriesTransparency, SetBlendMode)
{
    MeshSeries m;
    m.blend_mode(BlendMode::Additive);
    EXPECT_EQ(m.blend_mode(), BlendMode::Additive);
}

TEST(MeshSeriesTransparency, DefaultDoubleSided)
{
    MeshSeries m;
    EXPECT_TRUE(m.double_sided());
}

TEST(MeshSeriesTransparency, SetDoubleSided)
{
    MeshSeries m;
    m.double_sided(false);
    EXPECT_FALSE(m.double_sided());
}

TEST(MeshSeriesTransparency, DefaultWireframe)
{
    MeshSeries m;
    EXPECT_FALSE(m.wireframe());
}

TEST(MeshSeriesTransparency, SetWireframe)
{
    MeshSeries m;
    m.wireframe(true);
    EXPECT_TRUE(m.wireframe());
}

TEST(MeshSeriesTransparency, IsTransparentOpaque)
{
    MeshSeries m;
    m.color(Color{1.0f, 1.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(m.is_transparent());
}

TEST(MeshSeriesTransparency, IsTransparentByColor)
{
    MeshSeries m;
    m.color(Color{1.0f, 1.0f, 0.0f, 0.4f});
    EXPECT_TRUE(m.is_transparent());
}

TEST(MeshSeriesTransparency, IsTransparentByOpacity)
{
    MeshSeries m;
    m.color(Color{1.0f, 1.0f, 0.0f, 1.0f}).opacity(0.3f);
    EXPECT_TRUE(m.is_transparent());
}

TEST(MeshSeriesTransparency, MaterialChaining)
{
    MeshSeries m;
    auto&      ref = m.color(Color{0.5f, 0.5f, 0.5f, 0.7f})
                    .opacity(0.9f)
                    .blend_mode(BlendMode::Premultiplied)
                    .double_sided(false)
                    .wireframe(true)
                    .ambient(0.1f)
                    .specular(0.8f)
                    .shininess(128.0f);
    EXPECT_FLOAT_EQ(ref.opacity(), 0.9f);
    EXPECT_EQ(ref.blend_mode(), BlendMode::Premultiplied);
    EXPECT_FALSE(ref.double_sided());
    EXPECT_TRUE(ref.wireframe());
    EXPECT_FLOAT_EQ(ref.ambient(), 0.1f);
    EXPECT_FLOAT_EQ(ref.specular(), 0.8f);
    EXPECT_FLOAT_EQ(ref.shininess(), 128.0f);
}

// ─── Wireframe mesh generation ──────────────────────────────────────────────

TEST(SurfaceWireframe, GenerateWireframeMesh)
{
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    SurfaceSeries s(x, y, z);
    s.wireframe(true);
    s.generate_wireframe_mesh();

    EXPECT_TRUE(s.is_wireframe_mesh_generated());
    const auto& wm = s.wireframe_mesh();
    EXPECT_EQ(wm.vertex_count, 9u);       // 3x3 grid
    EXPECT_EQ(wm.vertices.size(), 54u);   // 9 vertices * 6 floats each
    // Line indices: 3 rows * 2 horizontal segments + 3 cols * 2 vertical segments = 12 segments * 2
    // indices
    EXPECT_EQ(wm.indices.size(), 24u);
}

TEST(SurfaceWireframe, WireframeMeshEmpty)
{
    SurfaceSeries s;
    s.generate_wireframe_mesh();
    EXPECT_FALSE(s.is_wireframe_mesh_generated());
}

TEST(SurfaceWireframe, WireframeMeshTooSmall)
{
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};

    SurfaceSeries s(x, y, z);
    s.generate_wireframe_mesh();
    EXPECT_FALSE(s.is_wireframe_mesh_generated());
}

TEST(SurfaceWireframe, WireframeMeshResetOnDataChange)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries s(x, y, z);
    s.generate_wireframe_mesh();
    EXPECT_TRUE(s.is_wireframe_mesh_generated());

    // Changing data should reset the wireframe mesh
    std::vector<float> z2 = {4.0f, 5.0f, 6.0f, 7.0f};
    s.set_data(x, y, z2);
    EXPECT_FALSE(s.is_wireframe_mesh_generated());
}

TEST(SurfaceWireframe, WireframeIndexTopology)
{
    // 2x2 grid: should produce 2 horizontal + 2 vertical line segments = 4 segments = 8 indices
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries s(x, y, z);
    s.generate_wireframe_mesh();
    EXPECT_TRUE(s.is_wireframe_mesh_generated());

    const auto& wm = s.wireframe_mesh();
    // 2 rows * 1 horizontal segment + 2 cols * 1 vertical segment = 4 segments
    EXPECT_EQ(wm.indices.size(), 8u);
}

// ─── Transparent pipeline enum tests ────────────────────────────────────────

TEST(TransparentPipeline, EnumTypesExist)
{
    [[maybe_unused]] PipelineType lt  = PipelineType::Line3D_Transparent;
    [[maybe_unused]] PipelineType st  = PipelineType::Scatter3D_Transparent;
    [[maybe_unused]] PipelineType mt  = PipelineType::Mesh3D_Transparent;
    [[maybe_unused]] PipelineType sft = PipelineType::Surface3D_Transparent;
    [[maybe_unused]] PipelineType sw  = PipelineType::SurfaceWireframe3D;
    [[maybe_unused]] PipelineType swt = PipelineType::SurfaceWireframe3D_Transparent;
    SUCCEED();
}

// ─── Transparency threshold edge cases ──────────────────────────────────────

TEST(TransparencyThreshold, ExactlyOpaque)
{
    // color.a * opacity == 1.0 → NOT transparent
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST(TransparencyThreshold, JustBelowOpaque)
{
    // color.a * opacity == 0.98 → transparent
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.98f}).opacity(1.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST(TransparencyThreshold, AtThreshold)
{
    // color.a * opacity == 0.99 → NOT transparent (threshold is < 0.99)
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.99f}).opacity(1.0f);
    EXPECT_FALSE(s.is_transparent());
}

TEST(TransparencyThreshold, FullyTransparent)
{
    LineSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.0f}).opacity(1.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST(TransparencyThreshold, ZeroOpacity)
{
    ScatterSeries3D s;
    s.color(Color{1.0f, 0.0f, 0.0f, 1.0f}).opacity(0.0f);
    EXPECT_TRUE(s.is_transparent());
}

TEST(TransparencyThreshold, CombinedAlphaAndOpacity)
{
    // color.a=0.7 * opacity=0.7 = 0.49 → transparent
    MeshSeries m;
    m.color(Color{1.0f, 0.0f, 0.0f, 0.7f}).opacity(0.7f);
    EXPECT_TRUE(m.is_transparent());
}
