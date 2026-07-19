#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>
#include <vector>

using namespace spectra;

// ─── LineSeries3D Tests ──────────────────────────────────────────────────────

TEST(LineSeries3D, DefaultConstruction)
{
    LineSeries3D series;
    EXPECT_EQ(series.point_count(), 0);
    EXPECT_TRUE(series.x_data().empty());
    EXPECT_TRUE(series.y_data().empty());
    EXPECT_TRUE(series.z_data().empty());
}

TEST(LineSeries3D, ConstructionWithData)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {4.0f, 5.0f, 6.0f};
    std::vector<float> z = {7.0f, 8.0f, 9.0f};

    LineSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 3);
    EXPECT_EQ(series.x_data().size(), 3);
    EXPECT_EQ(series.y_data().size(), 3);
    EXPECT_EQ(series.z_data().size(), 3);
}

TEST(LineSeries3D, SetData)
{
    LineSeries3D       series;
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};

    series.set_x(x).set_y(y).set_z(z);
    EXPECT_EQ(series.point_count(), 2);
    EXPECT_FLOAT_EQ(series.x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(series.y_data()[1], 4.0f);
    EXPECT_FLOAT_EQ(series.z_data()[0], 5.0f);
}

TEST(LineSeries3D, AppendPoint)
{
    LineSeries3D series;
    series.append(1.0f, 2.0f, 3.0f);
    series.append(4.0f, 5.0f, 6.0f);

    EXPECT_EQ(series.point_count(), 2);
    EXPECT_FLOAT_EQ(series.x_data()[1], 4.0f);
    EXPECT_FLOAT_EQ(series.y_data()[1], 5.0f);
    EXPECT_FLOAT_EQ(series.z_data()[1], 6.0f);
}

TEST(LineSeries3D, ComputeCentroid)
{
    std::vector<float> x = {0.0f, 2.0f, 4.0f};
    std::vector<float> y = {0.0f, 3.0f, 6.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f};

    LineSeries3D series(x, y, z);
    vec3         centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 2.0f);
    EXPECT_FLOAT_EQ(centroid.y, 3.0f);
    EXPECT_FLOAT_EQ(centroid.z, 1.0f);
}

TEST(LineSeries3D, ComputeCentroidEmpty)
{
    LineSeries3D series;
    vec3         centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 0.0f);
    EXPECT_FLOAT_EQ(centroid.y, 0.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);
}

TEST(LineSeries3D, GetBounds)
{
    std::vector<float> x = {-1.0f, 2.0f, 5.0f};
    std::vector<float> y = {-3.0f, 0.0f, 4.0f};
    std::vector<float> z = {-2.0f, 1.0f, 3.0f};

    LineSeries3D series(x, y, z);
    vec3         min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -3.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 5.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 4.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(LineSeries3D, GetBoundsEmpty)
{
    LineSeries3D series;
    vec3         min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 0.0f);
}

TEST(LineSeries3D, WidthProperty)
{
    LineSeries3D series;
    series.width(3.5f);
    EXPECT_FLOAT_EQ(series.width(), 3.5f);
}

TEST(LineSeries3D, FluentInterface)
{
    LineSeries3D series;
    auto&        result = series.width(2.0f).color(colors::red).opacity(0.8f);
    EXPECT_EQ(&result, &series);
}

TEST(LineSeries3D, DirtyFlagOnConstruction)
{
    std::vector<float> x = {1.0f}, y = {2.0f}, z = {3.0f};
    LineSeries3D       series(x, y, z);
    EXPECT_TRUE(series.is_dirty());
}

TEST(LineSeries3D, DirtyFlagOnSetData)
{
    LineSeries3D series;
    series.clear_dirty();
    EXPECT_FALSE(series.is_dirty());

    std::vector<float> x = {1.0f};
    series.set_x(x);
    EXPECT_TRUE(series.is_dirty());
}

TEST(LineSeries3D, DirtyFlagOnAppend)
{
    LineSeries3D series;
    series.clear_dirty();
    series.append(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(series.is_dirty());
}

TEST(LineSeries3D, DirtyFlagOnWidthChange)
{
    LineSeries3D series;
    series.clear_dirty();
    series.width(5.0f);
    EXPECT_TRUE(series.is_dirty());
}

TEST(LineSeries3D, VisibilityDefault)
{
    LineSeries3D series;
    EXPECT_TRUE(series.visible());
}

TEST(LineSeries3D, VisibilityToggle)
{
    LineSeries3D series;
    series.visible(false);
    EXPECT_FALSE(series.visible());
    series.visible(true);
    EXPECT_TRUE(series.visible());
}

TEST(LineSeries3D, LabelProperty)
{
    LineSeries3D series;
    series.label("3D trajectory");
    EXPECT_EQ(series.label(), "3D trajectory");
}

// ─── ScatterSeries3D Tests ───────────────────────────────────────────────────

TEST(ScatterSeries3D, DefaultConstruction)
{
    ScatterSeries3D series;
    EXPECT_EQ(series.point_count(), 0);
    EXPECT_FLOAT_EQ(series.size(), 4.0f);
}

TEST(ScatterSeries3D, ConstructionWithData)
{
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};

    ScatterSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 2);
}

TEST(ScatterSeries3D, SetData)
{
    ScatterSeries3D    series;
    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};
    std::vector<float> z = {3.0f};

    series.set_x(x).set_y(y).set_z(z);
    EXPECT_EQ(series.point_count(), 1);
    EXPECT_FLOAT_EQ(series.x_data()[0], 1.0f);
}

TEST(ScatterSeries3D, AppendPoint)
{
    ScatterSeries3D series;
    series.append(1.0f, 2.0f, 3.0f);

    EXPECT_EQ(series.point_count(), 1);
    EXPECT_FLOAT_EQ(series.z_data()[0], 3.0f);
}

TEST(ScatterSeries3D, ComputeCentroid)
{
    std::vector<float> x = {1.0f, 3.0f, 5.0f};
    std::vector<float> y = {2.0f, 4.0f, 6.0f};
    std::vector<float> z = {0.0f, 2.0f, 4.0f};

    ScatterSeries3D series(x, y, z);
    vec3            centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 3.0f);
    EXPECT_FLOAT_EQ(centroid.y, 4.0f);
    EXPECT_FLOAT_EQ(centroid.z, 2.0f);
}

TEST(ScatterSeries3D, GetBounds)
{
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {-5.0f, 5.0f};
    std::vector<float> z = {-1.0f, 1.0f};

    ScatterSeries3D series(x, y, z);
    vec3            min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 10.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -5.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 5.0f);
}

TEST(ScatterSeries3D, SizeProperty)
{
    ScatterSeries3D series;
    series.size(8.0f);
    EXPECT_FLOAT_EQ(series.size(), 8.0f);
}

TEST(ScatterSeries3D, LargeDataset)
{
    std::vector<float> x(10000), y(10000), z(10000);
    for (int i = 0; i < 10000; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i * 2);
        z[i] = static_cast<float>(i * 3);
    }

    ScatterSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), 10000);
}

TEST(ScatterSeries3D, FluentChaining)
{
    ScatterSeries3D series;
    auto&           result = series.size(10.0f).color(colors::green).opacity(0.5f).label("scatter");
    EXPECT_EQ(&result, &series);
    EXPECT_FLOAT_EQ(series.size(), 10.0f);
    EXPECT_EQ(series.label(), "scatter");
}

TEST(ScatterSeries3D, DirtyFlagOnSizeChange)
{
    ScatterSeries3D series;
    series.clear_dirty();
    series.size(12.0f);
    EXPECT_TRUE(series.is_dirty());
}

// ─── SurfaceSeries Tests ─────────────────────────────────────────────────────

TEST(SurfaceSeries, DefaultConstruction)
{
    SurfaceSeries series;
    EXPECT_EQ(series.rows(), 0);
    EXPECT_EQ(series.cols(), 0);
    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, ConstructionWithData)
{
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};   // 2 rows × 3 cols

    SurfaceSeries series(x, y, z);
    EXPECT_EQ(series.cols(), 3);
    EXPECT_EQ(series.rows(), 2);
}

TEST(SurfaceSeries, SetData)
{
    SurfaceSeries      series;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {1.0f, 2.0f, 3.0f, 4.0f};

    series.set_data(x, y, z);
    EXPECT_EQ(series.cols(), 2);
    EXPECT_EQ(series.rows(), 2);
}

TEST(SurfaceSeries, GenerateMeshSimple)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_TRUE(series.is_mesh_generated());
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertex_count, 4);     // 2×2 grid
    EXPECT_EQ(mesh.triangle_count, 2);   // 1 quad = 2 triangles
}

TEST(SurfaceSeries, GenerateMeshLarger)
{
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);   // 3×3 grid

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_TRUE(series.is_mesh_generated());
    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertex_count, 9);
    EXPECT_EQ(mesh.triangle_count, 8);   // 4 quads = 8 triangles
}

TEST(SurfaceSeries, MeshVertexFormat)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.vertices.size(), 24);   // 4 vertices × 6 floats (x,y,z,nx,ny,nz)
}

TEST(SurfaceSeries, MeshIndicesFormat)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    EXPECT_EQ(mesh.indices.size(), 6);   // 2 triangles × 3 indices
}

TEST(SurfaceSeries, GenerateMeshInvalidSize)
{
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, GenerateMeshMismatchedSize)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f};   // Wrong size: should be 4

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(SurfaceSeries, ComputeCentroid)
{
    std::vector<float> x = {0.0f, 2.0f};
    std::vector<float> y = {0.0f, 4.0f};
    std::vector<float> z = {0.0f, 2.0f, 4.0f, 6.0f};

    SurfaceSeries series(x, y, z);
    vec3          centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(centroid.z, 3.0f);
}

TEST(SurfaceSeries, GetBounds)
{
    std::vector<float> x = {-1.0f, 1.0f};
    std::vector<float> y = {-2.0f, 2.0f};
    std::vector<float> z = {-3.0f, 0.0f, 0.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    vec3          min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 2.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -3.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(SurfaceSeries, NormalComputation)
{
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);   // Flat surface

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    // Check that normals exist (every 6th float starting from index 3)
    for (size_t i = 0; i < mesh.vertex_count; ++i)
    {
        float nx  = mesh.vertices[i * 6 + 3];
        float ny  = mesh.vertices[i * 6 + 4];
        float nz  = mesh.vertices[i * 6 + 5];
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        EXPECT_NEAR(len, 1.0f, 1e-5f);   // Normals should be normalized
    }
}

TEST(SurfaceSeries, NormalPointsUpForFlatSurface)
{
    // For a flat z=0 surface, interior normals should point in +z or -z
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    // Center vertex (index 4) should have a well-defined normal
    float nz = mesh.vertices[4 * 6 + 5];
    EXPECT_NEAR(std::fabs(nz), 1.0f, 1e-5f);
}

TEST(SurfaceSeries, MeshVertexPositions)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {10.0f, 20.0f, 30.0f, 40.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    // First vertex: (x=0, y=0, z=10)
    EXPECT_FLOAT_EQ(mesh.vertices[0], 0.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[1], 0.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[2], 10.0f);
    // Second vertex: (x=1, y=0, z=20)
    EXPECT_FLOAT_EQ(mesh.vertices[6], 1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[7], 0.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[8], 20.0f);
}

TEST(SurfaceSeries, MeshIndexTopology)
{
    // 2×2 grid should produce 2 triangles sharing the diagonal
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    const auto& mesh = series.mesh();
    // All indices should be in range [0, vertex_count)
    for (auto idx : mesh.indices)
    {
        EXPECT_LT(idx, mesh.vertex_count);
    }
    // Should have exactly 6 indices (2 triangles)
    EXPECT_EQ(mesh.indices.size(), 6u);
}

TEST(SurfaceSeries, RegenerateMeshAfterSetData)
{
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();
    EXPECT_TRUE(series.is_mesh_generated());

    // Update data — mesh should be invalidated
    std::vector<float> z2 = {10.0f, 20.0f, 30.0f, 40.0f};
    series.set_data(x, y, z2);
    // After set_data, mesh_generated_ should be reset
    // generate_mesh again
    series.generate_mesh();
    EXPECT_TRUE(series.is_mesh_generated());
    EXPECT_FLOAT_EQ(series.mesh().vertices[2], 10.0f);   // z of first vertex
}

// ─── MeshSeries Tests ────────────────────────────────────────────────────────

TEST(MeshSeries, DefaultConstruction)
{
    MeshSeries series;
    EXPECT_EQ(series.vertex_count(), 0);
    EXPECT_EQ(series.triangle_count(), 0);
}

TEST(MeshSeries, ConstructionWithData)
{
    std::vector<float> verts = {
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,   // v0: pos + normal
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,   // v1
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f   // v2
    };
    std::vector<uint32_t> indices = {0, 1, 2};

    MeshSeries series(verts, indices);
    EXPECT_EQ(series.vertex_count(), 3);
    EXPECT_EQ(series.triangle_count(), 1);
}

TEST(MeshSeries, SetVertices)
{
    MeshSeries         series;
    std::vector<float> verts = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

    series.set_vertices(verts);
    EXPECT_EQ(series.vertex_count(), 1);
}

TEST(MeshSeries, SetIndices)
{
    MeshSeries            series;
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};

    series.set_indices(indices);
    EXPECT_EQ(series.triangle_count(), 2);
}

TEST(MeshSeries, ComputeCentroid)
{
    std::vector<float>    verts   = {0.0f,
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
                                     6.0f,
                                     0.0f,
                                     0.0f,
                                     0.0f,
                                     1.0f};
    std::vector<uint32_t> indices = {0, 1, 2};

    MeshSeries series(verts, indices);
    vec3       centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 1.0f);
    EXPECT_FLOAT_EQ(centroid.y, 2.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);
}

TEST(MeshSeries, GetBounds)
{
    std::vector<float> verts =
        {-1.0f, -2.0f, -3.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 1.0f};
    std::vector<uint32_t> indices = {0, 1, 0};

    MeshSeries series(verts, indices);
    vec3       min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, -1.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 1.0f);
    EXPECT_FLOAT_EQ(min_bound.y, -2.0f);
    EXPECT_FLOAT_EQ(max_bound.y, 2.0f);
    EXPECT_FLOAT_EQ(min_bound.z, -3.0f);
    EXPECT_FLOAT_EQ(max_bound.z, 3.0f);
}

TEST(MeshSeries, EmptyMesh)
{
    MeshSeries series;
    vec3       centroid = series.compute_centroid();
    EXPECT_FLOAT_EQ(centroid.x, 0.0f);

    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
}

TEST(MeshSeries, ComplexMesh)
{
    // Cube vertices (8 vertices × 6 floats)
    std::vector<float> verts;
    for (int i = 0; i < 8; ++i)
    {
        verts.push_back((i & 1) ? 1.0f : 0.0f);   // x
        verts.push_back((i & 2) ? 1.0f : 0.0f);   // y
        verts.push_back((i & 4) ? 1.0f : 0.0f);   // z
        verts.push_back(0.0f);                    // nx
        verts.push_back(0.0f);                    // ny
        verts.push_back(1.0f);                    // nz
    }

    std::vector<uint32_t> indices = {
        0,
        1,
        2,
        1,
        3,
        2,   // 2 triangles
        4,
        5,
        6,
        5,
        7,
        6   // 2 more triangles
    };

    MeshSeries series(verts, indices);
    EXPECT_EQ(series.vertex_count(), 8);
    EXPECT_EQ(series.triangle_count(), 4);
}

TEST(MeshSeries, DirtyFlagOnSetVertices)
{
    MeshSeries series;
    series.clear_dirty();
    std::vector<float> verts = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    series.set_vertices(verts);
    EXPECT_TRUE(series.is_dirty());
}

TEST(MeshSeries, DirtyFlagOnSetIndices)
{
    MeshSeries series;
    series.clear_dirty();
    std::vector<uint32_t> indices = {0, 1, 2};
    series.set_indices(indices);
    EXPECT_TRUE(series.is_dirty());
}

TEST(MeshSeries, FluentChaining)
{
    MeshSeries series;
    auto&      result = series.label("mesh").color(colors::red).opacity(0.7f);
    EXPECT_EQ(&result, &series);
    EXPECT_EQ(series.label(), "mesh");
}

// ─── Colormap Tests ──────────────────────────────────────────────────────────

TEST(Colormap, DefaultIsNone)
{
    SurfaceSeries series;
    EXPECT_EQ(series.colormap_type(), ColormapType::None);
}

TEST(Colormap, SetByEnum)
{
    SurfaceSeries series;
    series.colormap(ColormapType::Viridis);
    EXPECT_EQ(series.colormap_type(), ColormapType::Viridis);
}

TEST(Colormap, SetByString)
{
    SurfaceSeries series;
    series.colormap("jet");
    EXPECT_EQ(series.colormap_type(), ColormapType::Jet);
}

TEST(Colormap, SetByStringAllTypes)
{
    SurfaceSeries series;

    series.colormap("viridis");
    EXPECT_EQ(series.colormap_type(), ColormapType::Viridis);
    series.colormap("plasma");
    EXPECT_EQ(series.colormap_type(), ColormapType::Plasma);
    series.colormap("inferno");
    EXPECT_EQ(series.colormap_type(), ColormapType::Inferno);
    series.colormap("magma");
    EXPECT_EQ(series.colormap_type(), ColormapType::Magma);
    series.colormap("coolwarm");
    EXPECT_EQ(series.colormap_type(), ColormapType::Coolwarm);
    series.colormap("grayscale");
    EXPECT_EQ(series.colormap_type(), ColormapType::Grayscale);
}

TEST(Colormap, UnknownStringDefaultsToNone)
{
    SurfaceSeries series;
    series.colormap("nonexistent");
    EXPECT_EQ(series.colormap_type(), ColormapType::None);
}

TEST(Colormap, ColormapRange)
{
    SurfaceSeries series;
    series.set_colormap_range(-5.0f, 5.0f);
    EXPECT_FLOAT_EQ(series.colormap_min(), -5.0f);
    EXPECT_FLOAT_EQ(series.colormap_max(), 5.0f);
}

TEST(Colormap, SampleGrayscale)
{
    Color c0 = SurfaceSeries::sample_colormap(ColormapType::Grayscale, 0.0f);
    EXPECT_FLOAT_EQ(c0.r, 0.0f);
    EXPECT_FLOAT_EQ(c0.g, 0.0f);
    EXPECT_FLOAT_EQ(c0.b, 0.0f);

    Color c1 = SurfaceSeries::sample_colormap(ColormapType::Grayscale, 1.0f);
    EXPECT_FLOAT_EQ(c1.r, 1.0f);
    EXPECT_FLOAT_EQ(c1.g, 1.0f);
    EXPECT_FLOAT_EQ(c1.b, 1.0f);

    Color c5 = SurfaceSeries::sample_colormap(ColormapType::Grayscale, 0.5f);
    EXPECT_FLOAT_EQ(c5.r, 0.5f);
}

TEST(Colormap, SampleClampsInput)
{
    Color c_neg = SurfaceSeries::sample_colormap(ColormapType::Grayscale, -1.0f);
    EXPECT_FLOAT_EQ(c_neg.r, 0.0f);

    Color c_over = SurfaceSeries::sample_colormap(ColormapType::Grayscale, 2.0f);
    EXPECT_FLOAT_EQ(c_over.r, 1.0f);
}

TEST(Colormap, SampleViridisEndpoints)
{
    Color c0 = SurfaceSeries::sample_colormap(ColormapType::Viridis, 0.0f);
    Color c1 = SurfaceSeries::sample_colormap(ColormapType::Viridis, 1.0f);
    // Viridis goes from dark to bright — c1 should be brighter
    float lum0 = c0.r * 0.299f + c0.g * 0.587f + c0.b * 0.114f;
    float lum1 = c1.r * 0.299f + c1.g * 0.587f + c1.b * 0.114f;
    EXPECT_GT(lum1, lum0);
}

TEST(Colormap, SampleJetEndpoints)
{
    Color c0 = SurfaceSeries::sample_colormap(ColormapType::Jet, 0.0f);
    Color c1 = SurfaceSeries::sample_colormap(ColormapType::Jet, 1.0f);
    // Jet: t=0 should be blue-ish, t=1 should be red-ish
    EXPECT_GT(c0.b, c0.r);
    EXPECT_GT(c1.r, c1.b);
}

TEST(Colormap, SampleNoneReturnsGray)
{
    Color c = SurfaceSeries::sample_colormap(ColormapType::None, 0.5f);
    EXPECT_FLOAT_EQ(c.r, 0.5f);
    EXPECT_FLOAT_EQ(c.g, 0.5f);
    EXPECT_FLOAT_EQ(c.b, 0.5f);
}

TEST(Colormap, AllColormapsReturnValidColors)
{
    ColormapType types[] = {ColormapType::Viridis,
                            ColormapType::Plasma,
                            ColormapType::Inferno,
                            ColormapType::Magma,
                            ColormapType::Jet,
                            ColormapType::Coolwarm,
                            ColormapType::Grayscale};
    for (auto cm : types)
    {
        for (float t = 0.0f; t <= 1.0f; t += 0.1f)
        {
            Color c = SurfaceSeries::sample_colormap(cm, t);
            EXPECT_GE(c.r, 0.0f);
            EXPECT_LE(c.r, 1.0f);
            EXPECT_GE(c.g, 0.0f);
            EXPECT_LE(c.g, 1.0f);
            EXPECT_GE(c.b, 0.0f);
            EXPECT_LE(c.b, 1.0f);
            EXPECT_FLOAT_EQ(c.a, 1.0f);
        }
    }
}

TEST(Colormap, ColormapMarksDirty)
{
    SurfaceSeries series;
    series.clear_dirty();
    series.colormap(ColormapType::Jet);
    EXPECT_TRUE(series.is_dirty());
}

// ─── Axes3D Integration Tests ────────────────────────────────────────────────

TEST(Axes3DIntegration, Line3dFactory)
{
    Axes3D             axes;
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f};

    auto& series = axes.line3d(x, y, z);
    EXPECT_EQ(series.point_count(), 3);
    EXPECT_EQ(axes.series().size(), 1);
}

TEST(Axes3DIntegration, Scatter3dFactory)
{
    Axes3D             axes;
    std::vector<float> x = {1.0f, 2.0f};
    std::vector<float> y = {3.0f, 4.0f};
    std::vector<float> z = {5.0f, 6.0f};

    auto& series = axes.scatter3d(x, y, z);
    EXPECT_EQ(series.point_count(), 2);
    EXPECT_EQ(axes.series().size(), 1);
}

TEST(Axes3DIntegration, SurfaceFactory)
{
    Axes3D             axes;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f, 2.0f, 3.0f};

    auto& series = axes.surface(x, y, z);
    EXPECT_EQ(series.rows(), 2);
    EXPECT_EQ(series.cols(), 2);
    EXPECT_EQ(axes.series().size(), 1);
}

TEST(Axes3DIntegration, MeshFactory)
{
    Axes3D                axes;
    std::vector<float>    verts   = {0.0f,
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
                                     0.0f,
                                     1.0f,
                                     0.0f,
                                     0.0f,
                                     0.0f,
                                     1.0f};
    std::vector<uint32_t> indices = {0, 1, 2};

    auto& series = axes.mesh(verts, indices);
    EXPECT_EQ(series.vertex_count(), 3);
    EXPECT_EQ(series.triangle_count(), 1);
    EXPECT_EQ(axes.series().size(), 1);
}

TEST(Axes3DIntegration, MultipleSeries)
{
    Axes3D             axes;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    axes.line3d(x, y, z);
    axes.scatter3d(x, y, z);

    EXPECT_EQ(axes.series().size(), 2);
}

TEST(Axes3DIntegration, AutoFitWithLine3D)
{
    Axes3D             axes;
    std::vector<float> x = {-5.0f, 5.0f};
    std::vector<float> y = {-10.0f, 10.0f};
    std::vector<float> z = {-1.0f, 1.0f};

    axes.line3d(x, y, z);
    axes.auto_fit();

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    // After auto_fit with 5% padding, limits should encompass the data
    EXPECT_LE(xlim.min, -5.0f);
    EXPECT_GE(xlim.max, 5.0f);
    EXPECT_LE(ylim.min, -10.0f);
    EXPECT_GE(ylim.max, 10.0f);
    EXPECT_LE(zlim.min, -1.0f);
    EXPECT_GE(zlim.max, 1.0f);
}

TEST(Axes3DIntegration, AutoFitWithSurface)
{
    Axes3D             axes;
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {0.0f, 20.0f};
    std::vector<float> z = {-5.0f, 5.0f, -5.0f, 5.0f};

    axes.surface(x, y, z);
    axes.auto_fit();

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    EXPECT_LE(xlim.min, 0.0f);
    EXPECT_GE(xlim.max, 10.0f);
    EXPECT_LE(ylim.min, 0.0f);
    EXPECT_GE(ylim.max, 20.0f);
    EXPECT_LE(zlim.min, -5.0f);
    EXPECT_GE(zlim.max, 5.0f);
}

TEST(Axes3DIntegration, AutoFitEmpty)
{
    Axes3D axes;
    axes.auto_fit();

    auto xlim = axes.x_limits();
    // auto ylim = axes.y_limits();  // Currently unused
    // auto zlim = axes.z_limits();  // Currently unused

    EXPECT_FLOAT_EQ(xlim.min, -1.0f);
    EXPECT_FLOAT_EQ(xlim.max, 1.0f);
}

TEST(Axes3DIntegration, FactoryReturnsFluent)
{
    Axes3D             axes;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    auto& line = axes.line3d(x, y, z).width(3.0f).color(colors::red).label("line");
    EXPECT_FLOAT_EQ(line.width(), 3.0f);
    EXPECT_EQ(line.label(), "line");

    auto& scatter = axes.scatter3d(x, y, z).size(10.0f).color(colors::blue);
    EXPECT_FLOAT_EQ(scatter.size(), 10.0f);
}

// ─── Figure + Axes3D Integration ─────────────────────────────────────────────

TEST(FigureIntegration, Subplot3dCreation)
{
    Figure fig;
    auto&  ax = fig.subplot3d(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    std::vector<float> z = {0.0f, 1.0f};

    ax.scatter3d(x, y, z);
    EXPECT_EQ(ax.series().size(), 1);
    EXPECT_EQ(fig.all_axes().size(), 1);
}

TEST(FigureIntegration, Subplot3dOutOfRange)
{
    Figure fig;
    EXPECT_THROW(fig.subplot3d(1, 1, 0), std::out_of_range);
    EXPECT_THROW(fig.subplot3d(1, 1, 2), std::out_of_range);
    EXPECT_THROW(fig.subplot3d(0, 1, 1), std::out_of_range);
}

// ─── Edge Cases and Stress Tests ─────────────────────────────────────────────

TEST(Series3D, MismatchedArraySizes)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 2.0f};
    std::vector<float> z = {1.0f};

    LineSeries3D series(x, y, z);
    // Should handle gracefully - point_count is min of all sizes
    EXPECT_EQ(series.point_count(), 1);

    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    // Should only process the minimum count
    EXPECT_FLOAT_EQ(min_bound.x, 1.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 1.0f);

    ScatterSeries3D scatter(x, y, z);
    EXPECT_EQ(scatter.point_count(), 1);
}

TEST(Series3D, MissingCoordinateHasNoCompletePoints)
{
    std::vector<float> x = {1.0f};
    std::vector<float> y = {2.0f};

    LineSeries3D series;
    series.set_x(x).set_y(y);
    EXPECT_EQ(series.point_count(), 0);

    const vec3 centroid = series.compute_centroid();
    EXPECT_FLOAT_EQ(centroid.x, 0.0f);
    EXPECT_FLOAT_EQ(centroid.y, 0.0f);
    EXPECT_FLOAT_EQ(centroid.z, 0.0f);

    vec3 min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);
    EXPECT_FLOAT_EQ(min_bound.x, 0.0f);
    EXPECT_FLOAT_EQ(max_bound.x, 0.0f);
}

TEST(Series3D, SinglePoint)
{
    std::vector<float> x = {5.0f};
    std::vector<float> y = {10.0f};
    std::vector<float> z = {15.0f};

    ScatterSeries3D series(x, y, z);
    vec3            centroid = series.compute_centroid();

    EXPECT_FLOAT_EQ(centroid.x, 5.0f);
    EXPECT_FLOAT_EQ(centroid.y, 10.0f);
    EXPECT_FLOAT_EQ(centroid.z, 15.0f);
}

TEST(Series3D, NegativeCoordinates)
{
    std::vector<float> x = {-10.0f, -5.0f, -1.0f};
    std::vector<float> y = {-20.0f, -10.0f, -5.0f};
    std::vector<float> z = {-30.0f, -15.0f, -7.5f};

    LineSeries3D series(x, y, z);
    vec3         min_bound, max_bound;
    series.get_bounds(min_bound, max_bound);

    EXPECT_FLOAT_EQ(min_bound.x, -10.0f);
    EXPECT_FLOAT_EQ(max_bound.x, -1.0f);
}

TEST(Series3D, ZeroSizedSurface)
{
    std::vector<float> x = {0.0f};
    std::vector<float> y = {0.0f};
    std::vector<float> z = {0.0f};

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_FALSE(series.is_mesh_generated());
}

TEST(Series3D, VeryLargeSurface)
{
    int                size = 100;
    std::vector<float> x(size), y(size), z(size * size);

    for (int i = 0; i < size; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i);
    }
    for (int i = 0; i < size * size; ++i)
    {
        z[i] = std::sin(static_cast<float>(i) * 0.1f);
    }

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_TRUE(series.is_mesh_generated());
    EXPECT_EQ(series.mesh().vertex_count, size * size);
    EXPECT_EQ(series.mesh().triangle_count, (size - 1) * (size - 1) * 2);
}

TEST(Series3D, LargeScatter3DPerformance)
{
    // 100k points should be handled without issues
    const size_t       N = 100000;
    std::vector<float> x(N), y(N), z(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.01f;
        y[i] = std::sin(x[i]);
        z[i] = std::cos(x[i]);
    }

    ScatterSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), N);

    vec3 centroid = series.compute_centroid();
    EXPECT_NE(centroid.x, 0.0f);   // Non-trivial centroid
}

TEST(Series3D, LargeLine3DPerformance)
{
    const size_t       N = 50000;
    std::vector<float> x(N), y(N), z(N);
    for (size_t i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(N);
        x[i]    = std::cos(t * 20.0f);
        y[i]    = std::sin(t * 20.0f);
        z[i]    = t * 10.0f;
    }

    LineSeries3D series(x, y, z);
    EXPECT_EQ(series.point_count(), N);

    vec3 min_b, max_b;
    series.get_bounds(min_b, max_b);
    EXPECT_LE(min_b.x, -0.9f);
    EXPECT_GE(max_b.x, 0.9f);
}

TEST(Series3D, SurfaceNonUniformGrid)
{
    // Non-uniform spacing in x and y
    std::vector<float> x = {0.0f, 0.1f, 1.0f};
    std::vector<float> y = {0.0f, 0.5f, 10.0f};
    std::vector<float> z(9, 0.0f);

    SurfaceSeries series(x, y, z);
    series.generate_mesh();

    EXPECT_TRUE(series.is_mesh_generated());
    EXPECT_EQ(series.mesh().vertex_count, 9);
}
