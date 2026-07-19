#pragma once

#include <algorithm>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/math3d.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

// ColormapType is defined in color.hpp — included transitively.

enum class BlendMode
{
    Alpha = 0,       // Standard alpha blending (src_alpha, 1-src_alpha)
    Additive,        // Additive blending (src_alpha, one)
    Premultiplied,   // Premultiplied alpha (one, 1-src_alpha)
};

class LineSeries3D : public Series
{
   public:
    LineSeries3D() = default;
    LineSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    LineSeries3D& set_x(std::span<const float> x);
    LineSeries3D& set_y(std::span<const float> y);
    LineSeries3D& set_z(std::span<const float> z);
    using Series::append;   // Bring base 2-arg append into scope (avoid -Woverloaded-virtual)
    void append(float x, float y, float z);

    LineSeries3D& width(float w)
    {
        line_width_ = w;
        dirty_      = true;
        return *this;
    }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return std::min({x_.size(), y_.size(), z_.size()}); }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    LineSeries3D& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    LineSeries3D& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    LineSeries3D& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    LineSeries3D& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    LineSeries3D& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    LineSeries3D& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    LineSeries3D& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float              line_width_ = 2.0f;
    BlendMode          blend_mode_ = BlendMode::Alpha;
};

class ScatterSeries3D : public Series
{
   public:
    ScatterSeries3D() = default;
    ScatterSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    ScatterSeries3D& set_x(std::span<const float> x);
    ScatterSeries3D& set_y(std::span<const float> y);
    ScatterSeries3D& set_z(std::span<const float> z);
    using Series::append;   // Bring base 2-arg append into scope (avoid -Woverloaded-virtual)
    void append(float x, float y, float z);

    ScatterSeries3D& size(float s)
    {
        point_size_ = s;
        dirty_      = true;
        return *this;
    }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return std::min({x_.size(), y_.size(), z_.size()}); }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    ScatterSeries3D& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ScatterSeries3D& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ScatterSeries3D& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    ScatterSeries3D& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    ScatterSeries3D& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    ScatterSeries3D& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    ScatterSeries3D& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float              point_size_ = 4.0f;
    BlendMode          blend_mode_ = BlendMode::Alpha;
};

struct SurfaceMesh
{
    std::vector<float>    vertices;   // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices;    // Triangle indices
    size_t                vertex_count   = 0;
    size_t                triangle_count = 0;
};

class SurfaceSeries : public Series
{
   public:
    SurfaceSeries() = default;
    SurfaceSeries(std::span<const float> x_grid,
                  std::span<const float> y_grid,
                  std::span<const float> z_values);

    void set_data(std::span<const float> x_grid,
                  std::span<const float> y_grid,
                  std::span<const float> z_values);

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    std::span<const float> x_grid() const { return x_grid_; }
    std::span<const float> y_grid() const { return y_grid_; }
    std::span<const float> z_values() const { return z_values_; }

    const SurfaceMesh& mesh() const { return mesh_; }
    const SurfaceMesh& wireframe_mesh() const { return wireframe_mesh_; }
    bool               is_mesh_generated() const { return mesh_generated_; }
    bool               is_wireframe_mesh_generated() const { return wireframe_mesh_generated_; }

    void generate_mesh();
    void generate_wireframe_mesh();

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    SurfaceSeries& colormap(ColormapType cm)
    {
        colormap_ = cm;
        dirty_    = true;
        return *this;
    }
    SurfaceSeries& colormap(const std::string& name);
    ColormapType   colormap_type() const { return colormap_; }

    SurfaceSeries& colormap_range(float min_val, float max_val)
    {
        cmap_min_ = min_val;
        cmap_max_ = max_val;
        return *this;
    }
    float colormap_min() const { return cmap_min_; }
    float colormap_max() const { return cmap_max_; }

    // Deprecated alias
    void set_colormap_range(float min_val, float max_val)
    {
        cmap_min_ = min_val;
        cmap_max_ = max_val;
    }

    static Color sample_colormap(ColormapType cm, float t);

    using Series::color;
    using Series::label;
    using Series::opacity;

    SurfaceSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    SurfaceSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    SurfaceSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Material properties for Phong lighting
    SurfaceSeries& ambient(float a)
    {
        ambient_ = a;
        return *this;
    }
    SurfaceSeries& specular(float s)
    {
        specular_ = s;
        return *this;
    }
    SurfaceSeries& shininess(float s)
    {
        shininess_ = s;
        return *this;
    }
    float ambient() const { return ambient_; }
    float specular() const { return specular_; }
    float shininess() const { return shininess_; }

    SurfaceSeries& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    SurfaceSeries& double_sided(bool d)
    {
        double_sided_ = d;
        return *this;
    }
    bool double_sided() const { return double_sided_; }

    SurfaceSeries& wireframe(bool w)
    {
        wireframe_ = w;
        dirty_     = true;
        return *this;
    }
    bool wireframe() const { return wireframe_; }

    // Per-vertex alpha from colormap: when enabled, the colormap also drives
    // the alpha channel based on the Z value (low Z = transparent, high Z = opaque).
    SurfaceSeries& colormap_alpha(bool enabled)
    {
        colormap_alpha_ = enabled;
        dirty_          = true;
        return *this;
    }
    bool colormap_alpha() const { return colormap_alpha_; }

    // Custom alpha range for colormap alpha mapping
    SurfaceSeries& colormap_alpha_range(float min_alpha, float max_alpha)
    {
        cmap_alpha_min_ = min_alpha;
        cmap_alpha_max_ = max_alpha;
        return *this;
    }
    float colormap_alpha_min() const { return cmap_alpha_min_; }
    float colormap_alpha_max() const { return cmap_alpha_max_; }

    // Deprecated alias
    void set_colormap_alpha_range(float min_alpha, float max_alpha)
    {
        cmap_alpha_min_ = min_alpha;
        cmap_alpha_max_ = max_alpha;
    }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f || colormap_alpha_; }

   private:
    std::vector<float> x_grid_;
    std::vector<float> y_grid_;
    std::vector<float> z_values_;
    int                rows_ = 0;
    int                cols_ = 0;

    SurfaceMesh mesh_;
    SurfaceMesh wireframe_mesh_;
    bool        mesh_generated_           = false;
    bool        wireframe_mesh_generated_ = false;

    ColormapType colormap_ = ColormapType::None;
    float        cmap_min_ = 0.0f;
    float        cmap_max_ = 1.0f;

    float ambient_   = 0.0f;   // 0 = shader default (0.15)
    float specular_  = 0.0f;   // 0 = shader default (0.3)
    float shininess_ = 0.0f;   // 0 = shader default (32)

    BlendMode blend_mode_     = BlendMode::Alpha;
    bool      double_sided_   = true;   // Default: render both sides (shader flips normal)
    bool      wireframe_      = false;
    bool      colormap_alpha_ = false;
    float     cmap_alpha_min_ = 0.1f;
    float     cmap_alpha_max_ = 1.0f;
};

class MeshSeries : public Series
{
   public:
    MeshSeries() = default;
    MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices);

    void set_vertices(std::span<const float> vertices);
    void set_indices(std::span<const uint32_t> indices);

    std::span<const float>    vertices() const { return vertices_; }
    std::span<const uint32_t> indices() const { return indices_; }

    size_t vertex_count() const { return vertices_.size() / 6; }
    size_t triangle_count() const { return indices_.size() / 3; }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::opacity;

    MeshSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    MeshSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    MeshSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Material properties for Phong lighting
    MeshSeries& ambient(float a)
    {
        ambient_ = a;
        return *this;
    }
    MeshSeries& specular(float s)
    {
        specular_ = s;
        return *this;
    }
    MeshSeries& shininess(float s)
    {
        shininess_ = s;
        return *this;
    }
    float ambient() const { return ambient_; }
    float specular() const { return specular_; }
    float shininess() const { return shininess_; }

    MeshSeries& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    MeshSeries& double_sided(bool d)
    {
        double_sided_ = d;
        return *this;
    }
    bool double_sided() const { return double_sided_; }

    MeshSeries& wireframe(bool w)
    {
        wireframe_ = w;
        dirty_     = true;
        return *this;
    }
    bool wireframe() const { return wireframe_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

   private:
    std::vector<float>    vertices_;   // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices_;    // Triangle indices

    float ambient_   = 0.0f;   // 0 = shader default (0.15)
    float specular_  = 0.0f;   // 0 = shader default (0.3)
    float shininess_ = 0.0f;   // 0 = shader default (32)

    BlendMode blend_mode_   = BlendMode::Alpha;
    bool      double_sided_ = true;
    bool      wireframe_    = false;
};

}   // namespace spectra
