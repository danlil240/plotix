#pragma once

// ─── Spectra ↔ Eigen Direct Integration ────────────────────────────────────
//
// Include this header to pass Eigen vectors and matrices directly to any
// Spectra function that accepts std::span<const float>.  Zero-copy: the
// Eigen storage is read in-place via .data() / .size().
//
// Requirements:
//   - Eigen 3.x  (header-only)
//   - Build with -DSPECTRA_USE_EIGEN=ON
//
// Usage:
//
//   #include <spectra/eigen.hpp>          // pulls in series + axes + Eigen
//
//   Eigen::VectorXf x = Eigen::VectorXf::LinSpaced(100, 0, 2 * M_PI);
//   Eigen::VectorXf y = x.array().sin();
//
//   spectra::App app;
//   auto& fig = app.figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//
//   // Direct — no .data()/.size(), no std::vector copy
//   ax.line(x, y);
//   ax.scatter(x, y);
//   ax.plot(x, y, "r--o");
//
// 3D:
//   Eigen::VectorXf z = (x.array().square() + y.array().square()).sqrt();
//   auto& ax3 = fig.subplot3d(1, 1, 1);
//   ax3.line3d(x, y, z);
//   ax3.scatter3d(x, y, z);
//   ax3.surface(xg, yg, zv);
//
// ─────────────────────────────────────────────────────────────────────────────

#include <Eigen/Core>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <type_traits>

namespace spectra
{

// ─── Type Traits ─────────────────────────────────────────────────────────────

namespace eigen_detail
{

// Detect any Eigen dense expression with float scalar and compile-time or
// dynamic column count == 1 (i.e. a column vector or segment/head/tail).
template <typename T, typename = void>
struct is_eigen_float_vector : std::false_type
{
};

template <typename T>
struct is_eigen_float_vector<
    T,
    std::enable_if_t<std::is_base_of_v<Eigen::DenseBase<std::decay_t<T>>, std::decay_t<T>>
                     && std::is_same_v<typename std::decay_t<T>::Scalar, float>
                     && (std::decay_t<T>::ColsAtCompileTime == 1
                         || std::decay_t<T>::ColsAtCompileTime == Eigen::Dynamic)>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_eigen_float_vector_v = is_eigen_float_vector<T>::value;

// Own an evaluated Eigen expression while exposing a span-compatible view.
// This keeps lazy-expression storage alive through calls that immediately copy
// the data into a series.
template <typename Derived>
class EvaluatedSpan
{
   public:
    explicit EvaluatedSpan(const Eigen::DenseBase<Derived>& value) : value_(value.derived()) {}

    const float* data() const { return value_.data(); }
    size_t       size() const { return static_cast<size_t>(value_.size()); }
    float        operator[](size_t index) const { return data()[index]; }

    operator std::span<const float>() const { return {data(), size()}; }

   private:
    typename Derived::PlainObject value_;
};

// Convert any Eigen float vector expression to an owning, span-compatible view.
template <typename Derived>
EvaluatedSpan<Derived> to_span(const Eigen::DenseBase<Derived>& v)
{
    static_assert(std::is_same_v<typename Derived::Scalar, float>,
                  "Spectra Eigen adapter requires float scalar type. "
                  "Use .cast<float>() to convert.");
    return EvaluatedSpan<Derived>(v);
}

// Overload for Eigen::VectorXf and fixed-size vectors (already evaluated).
inline std::span<const float> to_span(const Eigen::VectorXf& v)
{
    return {v.data(), static_cast<size_t>(v.size())};
}

// Convert Eigen::VectorXi to span<const uint32_t> for index buffers.
// Only valid when sizeof(int) == sizeof(uint32_t) and the vector is contiguous.
inline std::span<const uint32_t> to_index_span(const Eigen::VectorXi& v)
{
    static_assert(sizeof(int) == sizeof(uint32_t),
                  "Eigen::VectorXi to uint32_t span requires 32-bit int");
    return {reinterpret_cast<const uint32_t*>(v.data()), static_cast<size_t>(v.size())};
}

}   // namespace eigen_detail

// ─── LineSeries Eigen Overloads ──────────────────────────────────────────────

// Construction
template <typename XDerived, typename YDerived>
auto make_line_series(const Eigen::DenseBase<XDerived>& x, const Eigen::DenseBase<YDerived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries>
{
    return LineSeries(eigen_detail::to_span(x), eigen_detail::to_span(y));
}

// ─── Axes Eigen Overloads ────────────────────────────────────────────────────

// line(EigenVec, EigenVec)
template <typename XDerived, typename YDerived>
auto line(Axes& ax, const Eigen::DenseBase<XDerived>& x, const Eigen::DenseBase<YDerived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries&>
{
    return ax.line(eigen_detail::to_span(x), eigen_detail::to_span(y));
}

// scatter(EigenVec, EigenVec)
template <typename XDerived, typename YDerived>
auto scatter(Axes& ax, const Eigen::DenseBase<XDerived>& x, const Eigen::DenseBase<YDerived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        ScatterSeries&>
{
    return ax.scatter(eigen_detail::to_span(x), eigen_detail::to_span(y));
}

// plot(EigenVec, EigenVec, fmt)
template <typename XDerived, typename YDerived>
auto plot(Axes&                             ax,
          const Eigen::DenseBase<XDerived>& x,
          const Eigen::DenseBase<YDerived>& y,
          std::string_view                  fmt = "-")
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries&>
{
    return ax.plot(eigen_detail::to_span(x), eigen_detail::to_span(y), fmt);
}

// plot(EigenVec, EigenVec, PlotStyle)
template <typename XDerived, typename YDerived>
auto plot(Axes&                             ax,
          const Eigen::DenseBase<XDerived>& x,
          const Eigen::DenseBase<YDerived>& y,
          const PlotStyle&                  style)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries&>
{
    return ax.plot(eigen_detail::to_span(x), eigen_detail::to_span(y), style);
}

// ─── LineSeries set_x / set_y ────────────────────────────────────────────────

template <typename Derived>
auto set_x(LineSeries& s, const Eigen::DenseBase<Derived>& x)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, LineSeries&>
{
    return s.set_x(eigen_detail::to_span(x));
}

template <typename Derived>
auto set_y(LineSeries& s, const Eigen::DenseBase<Derived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, LineSeries&>
{
    return s.set_y(eigen_detail::to_span(y));
}

// ─── ScatterSeries set_x / set_y ────────────────────────────────────────────

template <typename Derived>
auto set_x(ScatterSeries& s, const Eigen::DenseBase<Derived>& x)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, ScatterSeries&>
{
    return s.set_x(eigen_detail::to_span(x));
}

template <typename Derived>
auto set_y(ScatterSeries& s, const Eigen::DenseBase<Derived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, ScatterSeries&>
{
    return s.set_y(eigen_detail::to_span(y));
}

// ─── Axes3D Eigen Overloads ──────────────────────────────────────────────────

// line3d(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto line3d(Axes3D&                           ax,
            const Eigen::DenseBase<XDerived>& x,
            const Eigen::DenseBase<YDerived>& y,
            const Eigen::DenseBase<ZDerived>& z)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>
                            && eigen_detail::is_eigen_float_vector_v<ZDerived>,
                        LineSeries3D&>
{
    return ax.line3d(eigen_detail::to_span(x), eigen_detail::to_span(y), eigen_detail::to_span(z));
}

// scatter3d(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto scatter3d(Axes3D&                           ax,
               const Eigen::DenseBase<XDerived>& x,
               const Eigen::DenseBase<YDerived>& y,
               const Eigen::DenseBase<ZDerived>& z)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>
                            && eigen_detail::is_eigen_float_vector_v<ZDerived>,
                        ScatterSeries3D&>
{
    return ax.scatter3d(eigen_detail::to_span(x),
                        eigen_detail::to_span(y),
                        eigen_detail::to_span(z));
}

// surface(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto surface(Axes3D&                           ax,
             const Eigen::DenseBase<XDerived>& x_grid,
             const Eigen::DenseBase<YDerived>& y_grid,
             const Eigen::DenseBase<ZDerived>& z_values)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<XDerived>
                            && eigen_detail::is_eigen_float_vector_v<YDerived>
                            && eigen_detail::is_eigen_float_vector_v<ZDerived>,
                        SurfaceSeries&>
{
    return ax.surface(eigen_detail::to_span(x_grid),
                      eigen_detail::to_span(y_grid),
                      eigen_detail::to_span(z_values));
}

// mesh(EigenVec vertices, EigenVectorXi indices)
template <typename VDerived>
auto mesh(Axes3D& ax, const Eigen::DenseBase<VDerived>& vertices, const Eigen::VectorXi& indices)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<VDerived>, MeshSeries&>
{
    return ax.mesh(eigen_detail::to_span(vertices), eigen_detail::to_index_span(indices));
}

// ─── 3D Series set_x / set_y / set_z ────────────────────────────────────────

template <typename Derived>
auto set_x(LineSeries3D& s, const Eigen::DenseBase<Derived>& x)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, LineSeries3D&>
{
    return s.set_x(eigen_detail::to_span(x));
}

template <typename Derived>
auto set_y(LineSeries3D& s, const Eigen::DenseBase<Derived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, LineSeries3D&>
{
    return s.set_y(eigen_detail::to_span(y));
}

template <typename Derived>
auto set_z(LineSeries3D& s, const Eigen::DenseBase<Derived>& z)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, LineSeries3D&>
{
    return s.set_z(eigen_detail::to_span(z));
}

template <typename Derived>
auto set_x(ScatterSeries3D& s, const Eigen::DenseBase<Derived>& x)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, ScatterSeries3D&>
{
    return s.set_x(eigen_detail::to_span(x));
}

template <typename Derived>
auto set_y(ScatterSeries3D& s, const Eigen::DenseBase<Derived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, ScatterSeries3D&>
{
    return s.set_y(eigen_detail::to_span(y));
}

template <typename Derived>
auto set_z(ScatterSeries3D& s, const Eigen::DenseBase<Derived>& z)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<Derived>, ScatterSeries3D&>
{
    return s.set_z(eigen_detail::to_span(z));
}

// ─── Eigen ↔ spectra::vec3 / mat4 Conversion ────────────────────────────────

// Convert Eigen::Vector3f to spectra::vec3
inline vec3 to_vec3(const Eigen::Vector3f& v)
{
    return {v.x(), v.y(), v.z()};
}

// Convert spectra::vec3 to Eigen::Vector3f
inline Eigen::Vector3f to_eigen(const vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// Convert Eigen::Matrix4f (column-major) to spectra::mat4
inline mat4 to_mat4(const Eigen::Matrix4f& m)
{
    mat4 result;
    // Eigen is column-major by default, same as spectra::mat4
    for (int i = 0; i < 16; ++i)
        result.m[i] = m.data()[i];
    return result;
}

// Convert spectra::mat4 to Eigen::Matrix4f
inline Eigen::Matrix4f to_eigen(const mat4& m)
{
    return Eigen::Map<const Eigen::Matrix4f>(m.m);
}

}   // namespace spectra
