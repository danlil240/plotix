#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstddef>
    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <vector>

struct ImFont;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes;
class Figure;
class TransitionEngine;

// Statistics computed for a selected region of data points.
struct RegionStatistics
{
    size_t point_count = 0;
    float  x_min = 0.0f, x_max = 0.0f;
    float  y_min = 0.0f, y_max = 0.0f;
    float  y_mean = 0.0f;
    float  y_std  = 0.0f;
};

// A point captured inside a region selection.
struct SelectedPoint
{
    const Series* series = nullptr;
    size_t        index  = 0;
    float         data_x = 0.0f;
    float         data_y = 0.0f;
};

// Region selection: shift-drag to select a rectangular region on the plot.
// Shows a floating mini-toolbar with point count and basic statistics.
// The selection rectangle is defined in data coordinates so it survives
// zoom/pan until explicitly dismissed.
class RegionSelect
{
   public:
    RegionSelect() = default;

    // Set fonts for the mini-toolbar rendering
    void set_fonts(ImFont* body, ImFont* heading);

    // Set the transition engine for animated opacity (optional)
    void set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }

    // ─── Selection lifecycle ────────────────────────────────────────────

    // Begin a new selection at the given screen position.
    void begin(double      screen_x,
               double      screen_y,
               const Rect& viewport,
               float       xlim_min,
               float       xlim_max,
               float       ylim_min,
               float       ylim_max);

    // Update the selection end point (while dragging).
    void update_drag(double      screen_x,
                     double      screen_y,
                     const Rect& viewport,
                     float       xlim_min,
                     float       xlim_max,
                     float       ylim_min,
                     float       ylim_max);

    // Finish the selection (mouse release). Computes statistics.
    void finish(const Axes* axes);

    // Restore a completed data-coordinate selection and recompute its point
    // membership/statistics against the recreated axes.
    void restore(float x_min, float x_max, float y_min, float y_max, const Axes* axes);

    // Dismiss / clear the current selection.
    void dismiss();

    // ─── State queries ──────────────────────────────────────────────────

    bool is_dragging() const { return dragging_; }
    bool has_selection() const { return has_selection_; }

    // Data-coordinate bounds of the selection rectangle.
    float data_x_min() const { return data_x0_ < data_x1_ ? data_x0_ : data_x1_; }
    float data_x_max() const { return data_x0_ > data_x1_ ? data_x0_ : data_x1_; }
    float data_y_min() const { return data_y0_ < data_y1_ ? data_y0_ : data_y1_; }
    float data_y_max() const { return data_y0_ > data_y1_ ? data_y0_ : data_y1_; }

    const RegionStatistics&           statistics() const { return stats_; }
    const std::vector<SelectedPoint>& selected_points() const { return selected_points_; }

    // ─── Drawing ────────────────────────────────────────────────────────

    // Draw the selection rectangle and floating mini-toolbar.
    // Call inside ImGui frame, after build_ui.
    void draw(const Rect& viewport,
              float       xlim_min,
              float       xlim_max,
              float       ylim_min,
              float       ylim_max,
              float       window_width,
              float       window_height);

    // ─── Configuration ──────────────────────────────────────────────────

    void set_fill_alpha(float a) { fill_alpha_ = a; }
    void set_border_width(float w) { border_width_ = w; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

   private:
    // Draw the floating mini-toolbar with statistics
    void draw_mini_toolbar(float rx0,
                           float ry0,
                           float rx1,
                           float ry1,
                           float window_width,
                           float window_height);

    // Convert data coordinates to screen coordinates
    static void data_to_screen(float       data_x,
                               float       data_y,
                               const Rect& viewport,
                               float       xlim_min,
                               float       xlim_max,
                               float       ylim_min,
                               float       ylim_max,
                               float&      screen_x,
                               float&      screen_y);

    // Convert screen coordinates to data coordinates
    static void screen_to_data(double      screen_x,
                               double      screen_y,
                               const Rect& viewport,
                               float       xlim_min,
                               float       xlim_max,
                               float       ylim_min,
                               float       ylim_max,
                               float&      data_x,
                               float&      data_y);

    // Collect all data points inside the selection rectangle
    void collect_points(const Axes* axes);

    // Compute statistics from selected_points_
    void compute_statistics();

    bool dragging_      = false;
    bool has_selection_ = false;

    // Selection rectangle in data coordinates
    float data_x0_ = 0.0f, data_y0_ = 0.0f;
    float data_x1_ = 0.0f, data_y1_ = 0.0f;

    // Cached screen coordinates of drag start (for drawing during drag)
    double screen_start_x_ = 0.0;
    double screen_start_y_ = 0.0;
    double screen_end_x_   = 0.0;
    double screen_end_y_   = 0.0;

    RegionStatistics           stats_;
    std::vector<SelectedPoint> selected_points_;

    // Animation
    float             opacity_           = 0.0f;
    TransitionEngine* transition_engine_ = nullptr;

    // Fonts
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Visual config
    float fill_alpha_   = 0.15f;
    float border_width_ = 1.5f;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
