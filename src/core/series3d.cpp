#include <algorithm>
#include <atomic>
#include <cmath>
#include <spectra/series3d.hpp>

namespace spectra
{

namespace
{

void assign_coord(std::vector<float>& out, std::span<const float> in, std::atomic<bool>& dirty)
{
    out.assign(in.begin(), in.end());
    dirty.store(true, std::memory_order_release);
}

vec3 compute_centroid_xyz(const std::vector<float>& x,
                          const std::vector<float>& y,
                          const std::vector<float>& z)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
        return {0.0f, 0.0f, 0.0f};

    vec3 sum{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < n; ++i)
    {
        sum.x += x[i];
        sum.y += y[i];
        sum.z += z[i];
    }
    return sum / static_cast<float>(n);
}

void get_bounds_xyz(const std::vector<float>& x,
                    const std::vector<float>& y,
                    const std::vector<float>& z,
                    vec3&                     min_out,
                    vec3&                     max_out)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {x[0], y[0], z[0]};
    max_out = min_out;

    for (size_t i = 1; i < n; ++i)
    {
        min_out.x = std::fmin(min_out.x, x[i]);
        min_out.y = std::fmin(min_out.y, y[i]);
        min_out.z = std::fmin(min_out.z, z[i]);
        max_out.x = std::fmax(max_out.x, x[i]);
        max_out.y = std::fmax(max_out.y, y[i]);
        max_out.z = std::fmax(max_out.z, z[i]);
    }
}

}   // anonymous namespace

// ─── LineSeries3D ────────────────────────────────────────────────────────────

LineSeries3D::LineSeries3D(std::span<const float> x,
                           std::span<const float> y,
                           std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

LineSeries3D& LineSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void LineSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

vec3 LineSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void LineSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── ScatterSeries3D ─────────────────────────────────────────────────────────

ScatterSeries3D::ScatterSeries3D(std::span<const float> x,
                                 std::span<const float> y,
                                 std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

ScatterSeries3D& ScatterSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void ScatterSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

vec3 ScatterSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void ScatterSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── SurfaceSeries ───────────────────────────────────────────────────────────

SurfaceSeries::SurfaceSeries(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    set_data(x_grid, y_grid, z_values);
}

void SurfaceSeries::set_data(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    x_grid_.assign(x_grid.begin(), x_grid.end());
    y_grid_.assign(y_grid.begin(), y_grid.end());
    z_values_.assign(z_values.begin(), z_values.end());

    cols_ = static_cast<int>(x_grid_.size());
    rows_ = static_cast<int>(y_grid_.size());

    mesh_generated_           = false;
    wireframe_mesh_generated_ = false;
    dirty_                    = true;
}

void SurfaceSeries::generate_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertices.clear();
    mesh_.indices.clear();

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertex_count = rows_ * cols_;
    mesh_.vertices.reserve(mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            vec3 normal{0.0f, 0.0f, 1.0f};

            if (i > 0 && i < rows_ - 1 && j > 0 && j < cols_ - 1)
            {
                float z_left  = z_values_[i * cols_ + (j - 1)];
                float z_right = z_values_[i * cols_ + (j + 1)];
                float z_down  = z_values_[(i - 1) * cols_ + j];
                float z_up    = z_values_[(i + 1) * cols_ + j];

                float dx_left  = x_grid_[j] - x_grid_[j - 1];
                float dx_right = x_grid_[j + 1] - x_grid_[j];
                float dy_down  = y_grid_[i] - y_grid_[i - 1];
                float dy_up    = y_grid_[i + 1] - y_grid_[i];

                float dz_dx = (z_right - z_left) / (dx_left + dx_right);
                float dz_dy = (z_up - z_down) / (dy_down + dy_up);

                vec3 tangent_x{1.0f, 0.0f, dz_dx};
                vec3 tangent_y{0.0f, 1.0f, dz_dy};
                normal = vec3_normalize(vec3_cross(tangent_x, tangent_y));
            }

            mesh_.vertices.push_back(x);
            mesh_.vertices.push_back(y);
            mesh_.vertices.push_back(z);
            mesh_.vertices.push_back(normal.x);
            mesh_.vertices.push_back(normal.y);
            mesh_.vertices.push_back(normal.z);
        }
    }

    mesh_.triangle_count = (rows_ - 1) * (cols_ - 1) * 2;
    mesh_.indices.reserve(mesh_.triangle_count * 3);

    for (int i = 0; i < rows_ - 1; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            uint32_t idx0 = i * cols_ + j;
            uint32_t idx1 = i * cols_ + (j + 1);
            uint32_t idx2 = (i + 1) * cols_ + j;
            uint32_t idx3 = (i + 1) * cols_ + (j + 1);

            mesh_.indices.push_back(idx0);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx1);

            mesh_.indices.push_back(idx1);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx3);
        }
    }

    mesh_generated_ = true;
}

void SurfaceSeries::generate_wireframe_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    wireframe_mesh_.vertices.clear();
    wireframe_mesh_.indices.clear();

    // Reuse the same vertex layout as the solid mesh (position + normal)
    wireframe_mesh_.vertex_count = rows_ * cols_;
    wireframe_mesh_.vertices.reserve(wireframe_mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            // Normal pointing up for wireframe (not used for lighting but needed for vertex layout)
            wireframe_mesh_.vertices.push_back(x);
            wireframe_mesh_.vertices.push_back(y);
            wireframe_mesh_.vertices.push_back(z);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(1.0f);
        }
    }

    // Generate line indices: horizontal lines + vertical lines
    // Each line segment = 2 indices
    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j + 1));
        }
    }
    for (int j = 0; j < cols_; ++j)
    {
        for (int i = 0; i < rows_ - 1; ++i)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>((i + 1) * cols_ + j));
        }
    }

    wireframe_mesh_.triangle_count = 0;   // Not triangles — line segments
    wireframe_mesh_generated_      = true;
}

vec3 SurfaceSeries::compute_centroid() const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        return {0.0f, 0.0f, 0.0f};
    }

    float x_sum = 0.0f;
    float y_sum = 0.0f;
    float z_sum = 0.0f;
    for (float x : x_grid_)
        x_sum += x;
    for (float y : y_grid_)
        y_sum += y;
    for (float z : z_values_)
        z_sum += z;

    return {x_sum / static_cast<float>(x_grid_.size()),
            y_sum / static_cast<float>(y_grid_.size()),
            z_sum / static_cast<float>(z_values_.size())};
}

void SurfaceSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    auto [x_min, x_max] = std::minmax_element(x_grid_.begin(), x_grid_.end());
    auto [y_min, y_max] = std::minmax_element(y_grid_.begin(), y_grid_.end());
    auto [z_min, z_max] = std::minmax_element(z_values_.begin(), z_values_.end());

    min_out = {*x_min, *y_min, *z_min};
    max_out = {*x_max, *y_max, *z_max};
}

// ─── MeshSeries ──────────────────────────────────────────────────────────────

MeshSeries::MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices)
{
    set_vertices(vertices);
    set_indices(indices);
}

void MeshSeries::set_vertices(std::span<const float> vertices)
{
    vertices_.assign(vertices.begin(), vertices.end());
    dirty_ = true;
}

void MeshSeries::set_indices(std::span<const uint32_t> indices)
{
    indices_.assign(indices.begin(), indices.end());
    dirty_ = true;
}

vec3 MeshSeries::compute_centroid() const
{
    if (vertices_.empty())
        return {0.0f, 0.0f, 0.0f};

    vec3   sum{0.0f, 0.0f, 0.0f};
    size_t vertex_count = vertices_.size() / 6;

    for (size_t i = 0; i < vertex_count; ++i)
    {
        sum.x += vertices_[i * 6 + 0];
        sum.y += vertices_[i * 6 + 1];
        sum.z += vertices_[i * 6 + 2];
    }

    return sum / static_cast<float>(vertex_count);
}

void MeshSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (vertices_.empty())
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {vertices_[0], vertices_[1], vertices_[2]};
    max_out = min_out;

    size_t vertex_count = vertices_.size() / 6;
    for (size_t i = 1; i < vertex_count; ++i)
    {
        float x = vertices_[i * 6 + 0];
        float y = vertices_[i * 6 + 1];
        float z = vertices_[i * 6 + 2];

        min_out.x = std::fmin(min_out.x, x);
        min_out.y = std::fmin(min_out.y, y);
        min_out.z = std::fmin(min_out.z, z);
        max_out.x = std::fmax(max_out.x, x);
        max_out.y = std::fmax(max_out.y, y);
        max_out.z = std::fmax(max_out.z, z);
    }
}

// ─── Colormap Support ─────────────────────────────────────────────────────────

SurfaceSeries& SurfaceSeries::colormap(const std::string& name)
{
    if (name == "viridis")
        colormap_ = ColormapType::Viridis;
    else if (name == "plasma")
        colormap_ = ColormapType::Plasma;
    else if (name == "inferno")
        colormap_ = ColormapType::Inferno;
    else if (name == "magma")
        colormap_ = ColormapType::Magma;
    else if (name == "jet")
        colormap_ = ColormapType::Jet;
    else if (name == "coolwarm")
        colormap_ = ColormapType::Coolwarm;
    else if (name == "grayscale")
        colormap_ = ColormapType::Grayscale;
    else
        colormap_ = ColormapType::None;
    dirty_ = true;
    return *this;
}

Color SurfaceSeries::sample_colormap(ColormapType cm, float t)
{
    t = std::fmax(0.0f, std::fmin(1.0f, t));

    switch (cm)
    {
        case ColormapType::Viridis:
        {
            // Simplified viridis: dark purple → teal → yellow
            float r =
                std::fmax(0.0f,
                          std::fmin(1.0f, -0.35f + 1.7f * t - 0.9f * t * t + 0.55f * t * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, -0.05f + 0.7f * t + 0.3f * t * t));
            float b =
                std::fmax(0.0f,
                          std::fmin(1.0f, 0.33f + 0.7f * t - 1.6f * t * t + 0.6f * t * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Plasma:
        {
            // Simplified plasma: dark blue → magenta → yellow
            float r = std::fmax(0.0f, std::fmin(1.0f, 0.05f + 2.2f * t - 1.3f * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, -0.2f + 1.2f * t));
            float b =
                std::fmax(0.0f,
                          std::fmin(1.0f, 0.53f + 0.5f * t - 2.0f * t * t + 1.0f * t * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Inferno:
        {
            // Simplified inferno: black → red → yellow
            float r = std::fmax(0.0f, std::fmin(1.0f, -0.1f + 2.5f * t - 1.5f * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, -0.3f + 1.5f * t));
            float b =
                std::fmax(0.0f, std::fmin(1.0f, 0.1f + 2.0f * t - 3.5f * t * t + 1.5f * t * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Magma:
        {
            // Simplified magma: black → purple → orange → white
            float r = std::fmax(0.0f, std::fmin(1.0f, -0.05f + 2.0f * t - 0.8f * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, -0.3f + 1.3f * t + 0.1f * t * t));
            float b =
                std::fmax(0.0f,
                          std::fmin(1.0f, 0.15f + 1.5f * t - 2.5f * t * t + 1.5f * t * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Jet:
        {
            // Classic jet: blue → cyan → green → yellow → red
            float r = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.75f) * 4.0f));
            float g = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.5f) * 4.0f));
            float b = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.25f) * 4.0f));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Coolwarm:
        {
            // Cool (blue) to warm (red) diverging
            float r = std::fmax(0.0f, std::fmin(1.0f, 0.23f + 1.5f * t - 0.7f * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, 0.3f + 1.2f * t - 1.5f * t * t));
            float b = std::fmax(0.0f, std::fmin(1.0f, 0.75f - 0.5f * t - 0.2f * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Grayscale:
            return {t, t, t, 1.0f};
        case ColormapType::None:
        default:
            return {0.5f, 0.5f, 0.5f, 1.0f};
    }
}

}   // namespace spectra
