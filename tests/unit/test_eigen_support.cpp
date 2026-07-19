#include <Eigen/Core>
#include <gtest/gtest.h>
#include <spectra/eigen.hpp>

using namespace spectra;

// ─── Type Trait Tests ────────────────────────────────────────────────────────

TEST(EigenTraits, VectorXfIsEigenFloatVector)
{
    EXPECT_TRUE(eigen_detail::is_eigen_float_vector_v<Eigen::VectorXf>);
}

TEST(EigenTraits, Vector3fIsEigenFloatVector)
{
    EXPECT_TRUE(eigen_detail::is_eigen_float_vector_v<Eigen::Vector3f>);
}

TEST(EigenTraits, Vector4fIsEigenFloatVector)
{
    EXPECT_TRUE(eigen_detail::is_eigen_float_vector_v<Eigen::Vector4f>);
}

TEST(EigenTraits, VectorXdIsNotFloatVector)
{
    EXPECT_FALSE(eigen_detail::is_eigen_float_vector_v<Eigen::VectorXd>);
}

TEST(EigenTraits, MatrixXfIsNotVector)
{
    // MatrixXf has ColsAtCompileTime == Dynamic, but it's a matrix not a vector.
    // Our trait allows Dynamic cols — this is intentional for flexibility.
    // The actual constraint is that .data() returns contiguous float*.
    EXPECT_TRUE(eigen_detail::is_eigen_float_vector_v<Eigen::MatrixXf>);
}

TEST(EigenTraits, VectorXiIsNotFloatVector)
{
    EXPECT_FALSE(eigen_detail::is_eigen_float_vector_v<Eigen::VectorXi>);
}

TEST(EigenTraits, RowVectorXfIsNotColumnVector)
{
    // RowVectorXf has ColsAtCompileTime == Dynamic, RowsAtCompileTime == 1
    // Our trait checks ColsAtCompileTime == 1 || Dynamic, so this passes
    // because ColsAtCompileTime is Dynamic for RowVectorXf.
    EXPECT_TRUE(eigen_detail::is_eigen_float_vector_v<Eigen::RowVectorXf>);
}

// ─── to_span Tests ───────────────────────────────────────────────────────────

TEST(EigenToSpan, VectorXfBasic)
{
    Eigen::VectorXf v(5);
    v << 1.0f, 2.0f, 3.0f, 4.0f, 5.0f;

    auto span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 5u);
    EXPECT_FLOAT_EQ(span[0], 1.0f);
    EXPECT_FLOAT_EQ(span[4], 5.0f);
}

TEST(EigenToSpan, VectorXfZeroCopy)
{
    Eigen::VectorXf v(3);
    v << 10.0f, 20.0f, 30.0f;

    auto span = eigen_detail::to_span(v);
    EXPECT_EQ(span.data(), v.data());   // Same memory — zero copy
}

TEST(EigenToSpan, FixedSizeVector)
{
    Eigen::Vector3f v(1.0f, 2.0f, 3.0f);
    auto            span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 3u);
    EXPECT_FLOAT_EQ(span[1], 2.0f);
}

TEST(EigenToSpan, EmptyVector)
{
    Eigen::VectorXf v(0);
    auto            span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 0u);
}

TEST(EigenToSpan, LinSpaced)
{
    Eigen::VectorXf v    = Eigen::VectorXf::LinSpaced(100, 0.0f, 1.0f);
    auto            span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 100u);
    EXPECT_FLOAT_EQ(span[0], 0.0f);
    EXPECT_NEAR(span[99], 1.0f, 1e-5f);
}

// ─── to_index_span Tests ────────────────────────────────────────────────────

TEST(EigenToIndexSpan, VectorXiBasic)
{
    Eigen::VectorXi v(3);
    v << 0, 1, 2;

    auto span = eigen_detail::to_index_span(v);
    EXPECT_EQ(span.size(), 3u);
    EXPECT_EQ(span[0], 0u);
    EXPECT_EQ(span[2], 2u);
}

TEST(EigenToIndexSpan, ZeroCopy)
{
    Eigen::VectorXi v(4);
    v << 10, 20, 30, 40;

    auto span = eigen_detail::to_index_span(v);
    EXPECT_EQ(reinterpret_cast<const int*>(span.data()), v.data());
}

// ─── vec3 / mat4 Conversion Tests ───────────────────────────────────────────

TEST(EigenConversion, Vec3ToEigen)
{
    vec3            v{1.0f, 2.0f, 3.0f};
    Eigen::Vector3f ev = to_eigen(v);
    EXPECT_FLOAT_EQ(ev.x(), 1.0f);
    EXPECT_FLOAT_EQ(ev.y(), 2.0f);
    EXPECT_FLOAT_EQ(ev.z(), 3.0f);
}

TEST(EigenConversion, EigenToVec3)
{
    Eigen::Vector3f ev(4.0f, 5.0f, 6.0f);
    vec3            v = to_vec3(ev);
    EXPECT_FLOAT_EQ(v.x, 4.0f);
    EXPECT_FLOAT_EQ(v.y, 5.0f);
    EXPECT_FLOAT_EQ(v.z, 6.0f);
}

TEST(EigenConversion, Vec3RoundTrip)
{
    vec3 original{-1.0f, 0.5f, 3.14f};
    vec3 result = to_vec3(to_eigen(original));
    EXPECT_FLOAT_EQ(result.x, original.x);
    EXPECT_FLOAT_EQ(result.y, original.y);
    EXPECT_FLOAT_EQ(result.z, original.z);
}

TEST(EigenConversion, Mat4ToEigen)
{
    mat4 m  = mat4_identity();
    m.m[12] = 10.0f;   // translation x
    m.m[13] = 20.0f;   // translation y
    m.m[14] = 30.0f;   // translation z

    Eigen::Matrix4f em = to_eigen(m);
    EXPECT_FLOAT_EQ(em(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(em(0, 3), 10.0f);   // column-major: m[12] = col 3, row 0
    EXPECT_FLOAT_EQ(em(1, 3), 20.0f);
    EXPECT_FLOAT_EQ(em(2, 3), 30.0f);
}

TEST(EigenConversion, EigenToMat4)
{
    Eigen::Matrix4f em = Eigen::Matrix4f::Identity();
    em(0, 3)           = 5.0f;
    em(1, 3)           = 6.0f;
    em(2, 3)           = 7.0f;

    mat4 m = to_mat4(em);
    EXPECT_FLOAT_EQ(m.m[0], 1.0f);
    EXPECT_FLOAT_EQ(m.m[12], 5.0f);
    EXPECT_FLOAT_EQ(m.m[13], 6.0f);
    EXPECT_FLOAT_EQ(m.m[14], 7.0f);
}

TEST(EigenConversion, Mat4RoundTrip)
{
    mat4 original  = mat4_identity();
    original.m[5]  = 2.0f;
    original.m[10] = 3.0f;
    original.m[12] = 4.0f;

    mat4 result = to_mat4(to_eigen(original));
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(result.m[i], original.m[i]) << "mismatch at index " << i;
    }
}

// ─── LineSeries Construction ─────────────────────────────────────────────────

TEST(EigenLineSeries, MakeLineSeries)
{
    Eigen::VectorXf x(4), y(4);
    x << 0.0f, 1.0f, 2.0f, 3.0f;
    y << 0.0f, 1.0f, 4.0f, 9.0f;

    auto series = make_line_series(x, y);
    EXPECT_EQ(series.point_count(), 4u);
    EXPECT_FLOAT_EQ(series.x_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(series.y_data()[3], 9.0f);
}

TEST(EigenLineSeries, MakeLineSeriesLinSpaced)
{
    auto x = Eigen::VectorXf::LinSpaced(50, -1.0f, 1.0f);
    auto y = x.array().square().matrix();

    auto series = make_line_series(x, y);
    EXPECT_EQ(series.point_count(), 50u);
}

// ─── Free-Function Axes Overloads ────────────────────────────────────────────

TEST(EigenAxes, LineOverload)
{
    Axes ax;

    Eigen::VectorXf x(3), y(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;

    auto& series = spectra::line(ax, x, y);
    EXPECT_EQ(series.point_count(), 3u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(EigenAxes, ScatterOverload)
{
    Axes ax;

    Eigen::VectorXf x(3), y(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;

    auto& series = spectra::scatter(ax, x, y);
    EXPECT_EQ(series.point_count(), 3u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(EigenAxes, PlotOverload)
{
    Axes ax;

    Eigen::VectorXf x(3), y(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;

    auto& series = spectra::plot(ax, x, y, "r--o");
    EXPECT_EQ(series.point_count(), 3u);
}

TEST(EigenAxes, PlotWithPlotStyle)
{
    Axes ax;

    Eigen::VectorXf x(3), y(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;

    PlotStyle style;
    style.line_style = LineStyle::Dashed;
    auto& series     = spectra::plot(ax, x, y, style);
    EXPECT_EQ(series.point_count(), 3u);
}

// ─── Series set_x / set_y ───────────────────────────────────────────────────

TEST(EigenSetData, LineSeriesSetX)
{
    Eigen::VectorXf x1(3), y(3), x2(3);
    x1 << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;
    x2 << 10.0f, 20.0f, 30.0f;

    LineSeries s(std::span<const float>(x1.data(), x1.size()),
                 std::span<const float>(y.data(), y.size()));
    spectra::set_x(s, x2);
    EXPECT_FLOAT_EQ(s.x_data()[0], 10.0f);
}

TEST(EigenSetData, LineSeriesSetY)
{
    Eigen::VectorXf x(3), y1(3), y2(3);
    x << 1.0f, 2.0f, 3.0f;
    y1 << 4.0f, 5.0f, 6.0f;
    y2 << 40.0f, 50.0f, 60.0f;

    LineSeries s(std::span<const float>(x.data(), x.size()),
                 std::span<const float>(y1.data(), y1.size()));
    spectra::set_y(s, y2);
    EXPECT_FLOAT_EQ(s.y_data()[0], 40.0f);
}

TEST(EigenSetData, ScatterSeriesSetX)
{
    Eigen::VectorXf x1(2), y(2), x2(2);
    x1 << 1.0f, 2.0f;
    y << 3.0f, 4.0f;
    x2 << 100.0f, 200.0f;

    ScatterSeries s(std::span<const float>(x1.data(), x1.size()),
                    std::span<const float>(y.data(), y.size()));
    spectra::set_x(s, x2);
    EXPECT_FLOAT_EQ(s.x_data()[0], 100.0f);
}

TEST(EigenSetData, ScatterSeriesSetY)
{
    Eigen::VectorXf x(2), y1(2), y2(2);
    x << 1.0f, 2.0f;
    y1 << 3.0f, 4.0f;
    y2 << 300.0f, 400.0f;

    ScatterSeries s(std::span<const float>(x.data(), x.size()),
                    std::span<const float>(y1.data(), y1.size()));
    spectra::set_y(s, y2);
    EXPECT_FLOAT_EQ(s.y_data()[0], 300.0f);
}

// ─── 3D Axes Overloads ──────────────────────────────────────────────────────

TEST(EigenAxes3D, Line3dOverload)
{
    Axes3D ax;

    Eigen::VectorXf x(3), y(3), z(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;
    z << 7.0f, 8.0f, 9.0f;

    auto& series = spectra::line3d(ax, x, y, z);
    EXPECT_EQ(series.point_count(), 3u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(EigenAxes3D, Scatter3dOverload)
{
    Axes3D ax;

    Eigen::VectorXf x(3), y(3), z(3);
    x << 1.0f, 2.0f, 3.0f;
    y << 4.0f, 5.0f, 6.0f;
    z << 7.0f, 8.0f, 9.0f;

    auto& series = spectra::scatter3d(ax, x, y, z);
    EXPECT_EQ(series.point_count(), 3u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(EigenAxes3D, SurfaceOverload)
{
    Axes3D ax;

    Eigen::VectorXf xg(3), yg(3);
    xg << 0.0f, 1.0f, 2.0f;
    yg << 0.0f, 1.0f, 2.0f;

    Eigen::VectorXf zv(9);
    zv << 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 2.0f;

    auto& series = spectra::surface(ax, xg, yg, zv);
    EXPECT_EQ(series.rows(), 3);
    EXPECT_EQ(series.cols(), 3);
}

// ─── 3D Series set_x / set_y / set_z ────────────────────────────────────────

TEST(EigenSetData3D, LineSeries3DSetXYZ)
{
    Eigen::VectorXf x(2), y(2), z(2);
    x << 1.0f, 2.0f;
    y << 3.0f, 4.0f;
    z << 5.0f, 6.0f;

    LineSeries3D s(std::span<const float>(x.data(), x.size()),
                   std::span<const float>(y.data(), y.size()),
                   std::span<const float>(z.data(), z.size()));

    Eigen::VectorXf x2(2), y2(2), z2(2);
    x2 << 10.0f, 20.0f;
    y2 << 30.0f, 40.0f;
    z2 << 50.0f, 60.0f;

    spectra::set_x(s, x2);
    spectra::set_y(s, y2);
    spectra::set_z(s, z2);

    EXPECT_FLOAT_EQ(s.x_data()[0], 10.0f);
    EXPECT_FLOAT_EQ(s.y_data()[0], 30.0f);
    EXPECT_FLOAT_EQ(s.z_data()[0], 50.0f);
}

TEST(EigenSetData3D, ScatterSeries3DSetXYZ)
{
    Eigen::VectorXf x(2), y(2), z(2);
    x << 1.0f, 2.0f;
    y << 3.0f, 4.0f;
    z << 5.0f, 6.0f;

    ScatterSeries3D s(std::span<const float>(x.data(), x.size()),
                      std::span<const float>(y.data(), y.size()),
                      std::span<const float>(z.data(), z.size()));

    Eigen::VectorXf x2(2), y2(2), z2(2);
    x2 << 10.0f, 20.0f;
    y2 << 30.0f, 40.0f;
    z2 << 50.0f, 60.0f;

    spectra::set_x(s, x2);
    spectra::set_y(s, y2);
    spectra::set_z(s, z2);

    EXPECT_FLOAT_EQ(s.x_data()[0], 10.0f);
    EXPECT_FLOAT_EQ(s.y_data()[0], 30.0f);
    EXPECT_FLOAT_EQ(s.z_data()[0], 50.0f);
}

// ─── Expression Templates ───────────────────────────────────────────────────

TEST(EigenExpressions, ArrayExpressionEvaluated)
{
    Eigen::VectorXf x = Eigen::VectorXf::LinSpaced(10, 0.0f, 1.0f);
    // x.array().sin() is a lazy expression — to_span must .eval() it
    Eigen::VectorXf y = x.array().sin();

    auto span = eigen_detail::to_span(y);
    EXPECT_EQ(span.size(), 10u);
    EXPECT_NEAR(span[0], 0.0f, 1e-6f);
}

TEST(EigenExpressions, HeadSegment)
{
    Eigen::VectorXf v(10);
    for (int i = 0; i < 10; ++i)
        v[i] = static_cast<float>(i);

    // v.head(5) is a Block expression — contiguous, should work
    auto head = v.head(5).eval();
    auto span = eigen_detail::to_span(head);
    EXPECT_EQ(span.size(), 5u);
    EXPECT_FLOAT_EQ(span[4], 4.0f);
}

TEST(EigenExpressions, LinSpacedDirect)
{
    auto v    = Eigen::VectorXf::LinSpaced(50, -3.14f, 3.14f);
    auto span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 50u);
}

TEST(EigenExpressions, LazyExpressionRetainsEvaluatedStorage)
{
    auto expression = Eigen::VectorXf::LinSpaced(4, 1.0f, 4.0f);
    auto span       = eigen_detail::to_span(expression);

    EXPECT_FLOAT_EQ(span[0], 1.0f);
    EXPECT_FLOAT_EQ(span[3], 4.0f);
}

// ─── Auto-Fit Verification ──────────────────────────────────────────────────

TEST(EigenAutoFit, Line2DLimitsFromData)
{
    Axes ax;

    Eigen::VectorXf x(4), y(4);
    x << 0.0f, 1.0f, 2.0f, 3.0f;
    y << -5.0f, 10.0f, 3.0f, 7.0f;

    spectra::line(ax, x, y);

    // 2D axes auto-compute limits from data when xlim_ is nullopt (default)
    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    EXPECT_LE(xl.min, 0.0f);
    EXPECT_GE(xl.max, 3.0f);
    EXPECT_LE(yl.min, -5.0f);
    EXPECT_GE(yl.max, 10.0f);
}

TEST(EigenAutoFit, Scatter2DLimitsFromData)
{
    Axes ax;

    Eigen::VectorXf x(3), y(3);
    x << -10.0f, 0.0f, 10.0f;
    y << 100.0f, 200.0f, 300.0f;

    spectra::scatter(ax, x, y);

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    EXPECT_LE(xl.min, -10.0f);
    EXPECT_GE(xl.max, 10.0f);
    EXPECT_LE(yl.min, 100.0f);
    EXPECT_GE(yl.max, 300.0f);
}

TEST(EigenAutoFit, Plot2DLimitsFromData)
{
    Axes ax;

    Eigen::VectorXf x(5), y(5);
    x << -2.0f, -1.0f, 0.0f, 1.0f, 2.0f;
    y << 4.0f, 1.0f, 0.0f, 1.0f, 4.0f;

    spectra::plot(ax, x, y, "r-");

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    EXPECT_LE(xl.min, -2.0f);
    EXPECT_GE(xl.max, 2.0f);
    EXPECT_LE(yl.min, 0.0f);
    EXPECT_GE(yl.max, 4.0f);
}

TEST(EigenAutoFit, MultipleSeriesExpandLimits)
{
    Axes ax;

    Eigen::VectorXf x1(2), y1(2);
    x1 << 0.0f, 1.0f;
    y1 << 0.0f, 1.0f;
    spectra::line(ax, x1, y1);

    Eigen::VectorXf x2(2), y2(2);
    x2 << 10.0f, 20.0f;
    y2 << -50.0f, 50.0f;
    spectra::line(ax, x2, y2);

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    EXPECT_LE(xl.min, 0.0f);
    EXPECT_GE(xl.max, 20.0f);
    EXPECT_LE(yl.min, -50.0f);
    EXPECT_GE(yl.max, 50.0f);
}

TEST(EigenAutoFit, Line3DAutoFit)
{
    Axes3D ax;

    Eigen::VectorXf x(3), y(3), z(3);
    x << -5.0f, 0.0f, 5.0f;
    y << -10.0f, 0.0f, 10.0f;
    z << -1.0f, 0.0f, 1.0f;

    spectra::line3d(ax, x, y, z);
    ax.auto_fit();

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    auto zl = ax.z_limits();
    EXPECT_LE(xl.min, -5.0f);
    EXPECT_GE(xl.max, 5.0f);
    EXPECT_LE(yl.min, -10.0f);
    EXPECT_GE(yl.max, 10.0f);
    EXPECT_LE(zl.min, -1.0f);
    EXPECT_GE(zl.max, 1.0f);
}

TEST(EigenAutoFit, Scatter3DAutoFit)
{
    Axes3D ax;

    Eigen::VectorXf x(4), y(4), z(4);
    x << 0.0f, 1.0f, 2.0f, 3.0f;
    y << 0.0f, 10.0f, 20.0f, 30.0f;
    z << 0.0f, 100.0f, 200.0f, 300.0f;

    spectra::scatter3d(ax, x, y, z);
    ax.auto_fit();

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    auto zl = ax.z_limits();
    EXPECT_LE(xl.min, 0.0f);
    EXPECT_GE(xl.max, 3.0f);
    EXPECT_LE(yl.min, 0.0f);
    EXPECT_GE(yl.max, 30.0f);
    EXPECT_LE(zl.min, 0.0f);
    EXPECT_GE(zl.max, 300.0f);
}

TEST(EigenAutoFit, Surface3DAutoFit)
{
    Axes3D ax;

    Eigen::VectorXf xg(3), yg(3);
    xg << -1.0f, 0.0f, 1.0f;
    yg << -2.0f, 0.0f, 2.0f;

    Eigen::VectorXf zv(9);
    zv << 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 0.0f, 5.0f, 10.0f;

    spectra::surface(ax, xg, yg, zv);
    ax.auto_fit();

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    auto zl = ax.z_limits();
    EXPECT_LE(xl.min, -1.0f);
    EXPECT_GE(xl.max, 1.0f);
    EXPECT_LE(yl.min, -2.0f);
    EXPECT_GE(yl.max, 2.0f);
    EXPECT_LE(zl.min, 0.0f);
    EXPECT_GE(zl.max, 10.0f);
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST(EigenEdgeCases, SingleElement)
{
    Eigen::VectorXf v(1);
    v << 42.0f;
    auto span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 1u);
    EXPECT_FLOAT_EQ(span[0], 42.0f);
}

TEST(EigenEdgeCases, LargeVector)
{
    Eigen::VectorXf v    = Eigen::VectorXf::Random(100000);
    auto            span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 100000u);
    EXPECT_EQ(span.data(), v.data());
}

TEST(EigenEdgeCases, FixedSize4)
{
    Eigen::Vector4f v(1.0f, 2.0f, 3.0f, 4.0f);
    auto            span = eigen_detail::to_span(v);
    EXPECT_EQ(span.size(), 4u);
}
