#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <string>
    #include <vector>

struct ImDrawList;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes;
class Axes3D;

// A persistent data marker pinned to a specific data point.
struct DataMarker
{
    // Absolute data x (includes series x_offset); double so epoch-scale
    // timestamps keep sub-second precision.
    double        data_x      = 0.0;
    float         data_y      = 0.0f;
    float         data_z      = 0.0f;
    const Series* series      = nullptr;
    size_t        point_index = 0;
    Color         color       = colors::white;
    std::string   series_label;            // series name shown in datatip
    const Axes*   axes        = nullptr;   // owning 2D axes (for multi-subplot)
    const Axes3D* axes3d      = nullptr;   // owning 3D axes
    float         dy_dx       = 0.0f;      // finite-difference derivative
    bool          dy_dx_valid = false;     // true when derivative was computed
    bool          is_3d       = false;
};

// Manages a collection of persistent data markers.
// Markers survive zoom/pan and are drawn as pinned indicators on the canvas.
class DataMarkerManager
{
   public:
    DataMarkerManager() = default;

    void add(double        data_x,
             float         data_y,
             const Series* series,
             size_t        index,
             const Axes*   axes        = nullptr,
             float         dy_dx       = 0.0f,
             bool          dy_dx_valid = false);
    void remove(size_t marker_index);
    void clear();

    // Remove all markers that reference the given series (call before series is destroyed)
    void remove_for_series(const Series* series);

    // Toggle a data label: if a marker already exists at this (series, index),
    // remove it; otherwise add a new one. Returns true if a label was added.
    bool toggle_or_add(double        data_x,
                       float         data_y,
                       const Series* series,
                       size_t        index,
                       const Axes*   axes        = nullptr,
                       float         dy_dx       = 0.0f,
                       bool          dy_dx_valid = false);

    void add_3d(float         data_x,
                float         data_y,
                float         data_z,
                const Series* series,
                size_t        index,
                const Axes3D* axes3d);
    bool toggle_or_add_3d(float         data_x,
                          float         data_y,
                          float         data_z,
                          const Series* series,
                          size_t        index,
                          const Axes3D* axes3d);

    const std::vector<DataMarker>& markers() const { return markers_; }
    size_t                         count() const { return markers_.size(); }

    // Draw all markers. Converts data coords to screen coords using the viewport and limits.
    // When filter_axes is non-null, only markers belonging to that axes are drawn.
    // When dl is non-null, draws into the given draw list instead of GetForegroundDrawList().
    void draw(const Rect& viewport,
              double      xlim_min,
              double      xlim_max,
              float       ylim_min,
              float       ylim_max,
              float       opacity     = 1.0f,
              const Axes* filter_axes = nullptr,
              ImDrawList* dl          = nullptr);

    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

    // Hit-test: returns index of marker near screen position, or -1.
    // When filter_axes is non-null, only markers belonging to that axes are tested.
    int hit_test(float       screen_x,
                 float       screen_y,
                 const Rect& viewport,
                 double      xlim_min,
                 double      xlim_max,
                 float       ylim_min,
                 float       ylim_max,
                 float       radius_px   = 10.0f,
                 const Axes* filter_axes = nullptr) const;

    // Draw / hit-test compact pinned labels on 3D axes (camera-projected).
    void draw_3d(const Axes3D& axes3d, float opacity = 1.0f, ImDrawList* dl = nullptr);
    int  hit_test_3d(float         screen_x,
                     float         screen_y,
                     const Axes3D& axes3d,
                     float         radius_px = 10.0f) const;

   private:
    std::vector<DataMarker> markers_;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Find existing marker for the same series + point_index, or -1
    int find_duplicate(const Series* series, size_t point_index) const;

    // Convert data coordinates to screen coordinates
    static void data_to_screen(double      data_x,
                               float       data_y,
                               const Rect& viewport,
                               double      xlim_min,
                               double      xlim_max,
                               float       ylim_min,
                               float       ylim_max,
                               float&      screen_x,
                               float&      screen_y);
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
