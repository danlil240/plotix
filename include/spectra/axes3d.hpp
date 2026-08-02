#pragma once

#include <memory>
#include <spectra/axes.hpp>
#include <spectra/camera.hpp>
#include <spectra/fwd.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes3d.hpp>
#include <string>
#include <vector>

namespace spectra
{

class Axes3D : public AxesBase
{
   public:
    Axes3D();
    ~Axes3D();

    void xlim(double min, double max);
    void ylim(double min, double max);
    void zlim(double min, double max);

    void xlabel(const std::string& lbl);
    void ylabel(const std::string& lbl);
    void zlabel(const std::string& lbl);

    AxisLimits x_limits() const;
    AxisLimits y_limits() const;
    AxisLimits z_limits() const;

    const std::string& xlabel() const { return xlabel_; }
    const std::string& ylabel() const { return ylabel_; }
    const std::string& zlabel() const { return zlabel_; }

    // Deprecated aliases
    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    const std::string& get_zlabel() const { return zlabel_; }

    TickResult compute_x_ticks() const;
    TickResult compute_y_ticks() const;
    TickResult compute_z_ticks() const;

    void auto_fit() override;

    Camera&       camera() { return *camera_; }
    const Camera& camera() const { return *camera_; }

    enum class GridPlane
    {
        None = 0,
        XY   = 1 << 0,
        XZ   = 1 << 1,
        YZ   = 1 << 2,
        All  = XY | XZ | YZ
    };

    // UI highlight for axis-arrow hover (drives brighter arrow rendering).
    enum class AxisArrowHover
    {
        None,
        X,
        Y,
        Z
    };

    void           set_hovered_axis_arrow(AxisArrowHover hover) { hovered_axis_arrow_ = hover; }
    AxisArrowHover hovered_axis_arrow() const { return hovered_axis_arrow_; }

    void      grid_planes(GridPlane planes) { grid_planes_ = static_cast<int>(planes); }
    GridPlane grid_planes() const { return static_cast<GridPlane>(grid_planes_); }

    // Deprecated: use grid_planes(GridPlane) instead
    void set_grid_planes(int planes) { grid_planes_ = planes; }

    bool show_bounding_box() const { return show_bounding_box_; }
    void show_bounding_box(bool enabled) { show_bounding_box_ = enabled; }

    // Directional light configuration
    void light_dir(float x, float y, float z) { light_dir_ = {x, y, z}; }
    void light_dir(vec3 dir) { light_dir_ = dir; }
    vec3 light_dir() const { return light_dir_; }

    void lighting_enabled(bool enabled) { lighting_enabled_ = enabled; }
    bool lighting_enabled() const { return lighting_enabled_; }

    // Deprecated aliases
    void set_light_dir(float x, float y, float z) { light_dir_ = {x, y, z}; }
    void set_light_dir(vec3 dir) { light_dir_ = dir; }
    void set_lighting_enabled(bool enabled) { lighting_enabled_ = enabled; }

    // Returns a model matrix that maps data coordinates [xlim, ylim, zlim]
    // into a fixed-size normalized cube [-box_half_size, +box_half_size]³.
    // This keeps the bounding box a constant visual size regardless of zoom.
    mat4 data_to_normalized_matrix() const;

    // The half-size of the fixed normalized bounding box in world units.
    static constexpr float box_half_size() { return 3.0f; }

    // Zoom by scaling axis limits (bounding box stays fixed, data range changes)
    void zoom_limits(float factor);

    // Zoom a single axis only (for right-click 1D zoom)
    void zoom_limits_x(float factor);
    void zoom_limits_y(float factor);
    void zoom_limits_z(float factor);

    // Pan by shifting axis limits (bounding box stays fixed, data range shifts)
    // dx_screen/dy_screen are pixel deltas, vp_w/vp_h are viewport dimensions.
    void pan_limits(float dx_screen, float dy_screen, float vp_w, float vp_h);

    LineSeries3D&    line3d(std::span<const float> x,
                            std::span<const float> y,
                            std::span<const float> z);
    ScatterSeries3D& scatter3d(std::span<const float> x,
                               std::span<const float> y,
                               std::span<const float> z);
    SurfaceSeries&   surface(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values);
    MeshSeries&      mesh(std::span<const float> vertices, std::span<const uint32_t> indices);
    ShapeSeries3D&   shapes3d();

   private:
    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;
    std::optional<AxisLimits> zlim_;

    std::string xlabel_;
    std::string ylabel_;
    std::string zlabel_;

    std::unique_ptr<Camera> camera_;
    int                     grid_planes_        = static_cast<int>(GridPlane::All);
    bool                    show_bounding_box_  = true;
    vec3                    light_dir_          = {1.0f, 1.0f, 1.0f};   // Default: top-right-front
    bool                    lighting_enabled_   = true;
    AxisArrowHover          hovered_axis_arrow_ = AxisArrowHover::None;
};

inline Axes3D::GridPlane operator|(Axes3D::GridPlane a, Axes3D::GridPlane b)
{
    return static_cast<Axes3D::GridPlane>(static_cast<int>(a) | static_cast<int>(b));
}

inline Axes3D::GridPlane operator&(Axes3D::GridPlane a, Axes3D::GridPlane b)
{
    return static_cast<Axes3D::GridPlane>(static_cast<int>(a) & static_cast<int>(b));
}

inline Axes3D::GridPlane operator~(Axes3D::GridPlane a)
{
    return static_cast<Axes3D::GridPlane>(~static_cast<int>(a));
}

}   // namespace spectra
