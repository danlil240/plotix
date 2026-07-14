#pragma once

#include <cstdint>
#include <functional>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

#include "ui/animation/keyframe_interpolator.hpp"

namespace spectra
{

// Hit-test result for curve editor interaction.
enum class CurveHitType : uint8_t
{
    None,
    Keyframe,
    InTangent,
    OutTangent,
    Curve,
    Background,
};

struct CurveHitResult
{
    CurveHitType type           = CurveHitType::None;
    uint32_t     channel_id     = 0;
    size_t       keyframe_index = 0;
    float        time           = 0.0f;
    float        value          = 0.0f;
};

// Drag state for interactive editing.
struct CurveDragState
{
    bool         active         = false;
    CurveHitType dragging       = CurveHitType::None;
    uint32_t     channel_id     = 0;
    size_t       keyframe_index = 0;
    float        start_time     = 0.0f;
    float        start_value    = 0.0f;
    float        start_mouse_x  = 0.0f;
    float        start_mouse_y  = 0.0f;
};

// View transform for the curve editor coordinate space.
struct CurveViewTransform
{
    float time_min  = 0.0f;
    float time_max  = 10.0f;
    float value_min = -0.1f;
    float value_max = 1.1f;

    // Viewport pixel dimensions
    float width    = 400.0f;
    float height   = 200.0f;
    float origin_x = 0.0f;   // Screen-space origin
    float origin_y = 0.0f;

    // Convert time/value to screen coordinates
    float time_to_x(float t) const;
    float value_to_y(float v) const;

    // Convert screen coordinates to time/value
    float x_to_time(float x) const;
    float y_to_value(float y) const;

    // Zoom around a center point
    void zoom_time(float factor, float center_time);
    void zoom_value(float factor, float center_value);
    void zoom(float factor, float center_x, float center_y);

    // Pan by pixel delta
    void pan(float dx, float dy);

    // Fit view to show all keyframes with padding
    void fit_to_channel(const AnimationChannel& channel, float padding = 0.1f);
};

// Callback types for curve editor events.
using CurveEditCallback = std::function<void(uint32_t channel_id, size_t keyframe_index)>;
using CurveValueChangeCallback =
    std::function<void(uint32_t channel_id, float time, float old_value, float new_value)>;

// AnimationCurveEditor — visual curve editor for keyframe animation channels.
//
// Provides:
// - Curve visualization with configurable resolution
// - Keyframe diamond markers with selection
// - Tangent handle visualization and dragging
// - Zoom/pan navigation
// - Grid with adaptive tick spacing
// - Multi-channel overlay with per-channel colors
// - Hit-testing for interactive editing
//
// The ImGui drawing code is behind SPECTRA_USE_IMGUI guards.
// Pure logic (hit-testing, view transforms, curve sampling) is always available.
class AnimationCurveEditor
{
   public:
    AnimationCurveEditor();
    ~AnimationCurveEditor() = default;

    AnimationCurveEditor(const AnimationCurveEditor&)            = delete;
    AnimationCurveEditor& operator=(const AnimationCurveEditor&) = delete;

    // ─── Interpolator binding ────────────────────────────────────────

    // Set the KeyframeInterpolator to visualize/edit.
    void                  set_interpolator(KeyframeInterpolator* interp);
    KeyframeInterpolator* interpolator() const { return interpolator_; }

    // ─── Channel visibility ──────────────────────────────────────────

    // Show/hide a specific channel in the editor.
    void set_channel_visible(uint32_t channel_id, bool visible);
    bool is_channel_visible(uint32_t channel_id) const;

    // Set the color for a channel's curve.
    void  set_channel_color(uint32_t channel_id, Color color);
    Color channel_color(uint32_t channel_id) const;

    // Solo a channel (hide all others).
    void solo_channel(uint32_t channel_id);

    // Show all channels.
    void show_all_channels();

    // ─── View ────────────────────────────────────────────────────────

    CurveViewTransform&       view() { return view_; }
    const CurveViewTransform& view() const { return view_; }

    // Fit view to show all visible channels.
    void fit_view();

    // Reset view to default.
    void reset_view();

    // ─── Selection ───────────────────────────────────────────────────

    // Select/deselect keyframes.
    void select_keyframe(uint32_t channel_id, size_t index);
    void deselect_all();
    void select_keyframes_in_rect(float t_min, float t_max, float v_min, float v_max);

    size_t selected_count() const;

    // Delete all selected keyframes.
    void delete_selected();

    // Set interpolation mode for all selected keyframes.
    void set_selected_interp(InterpMode mode);

    // Set tangent mode for all selected keyframes.
    void set_selected_tangent_mode(TangentMode mode);

    // ─── Hit testing ─────────────────────────────────────────────────

    // Hit-test at screen coordinates. Returns the closest element.
    CurveHitResult hit_test(float screen_x, float screen_y, float tolerance = 8.0f) const;

    // ─── Drag interaction ────────────────────────────────────────────

    void begin_drag(float screen_x, float screen_y);
    void update_drag(float screen_x, float screen_y);
    void end_drag();
    void cancel_drag();

    bool is_dragging() const { return drag_.active; }

    // ─── Display options ─────────────────────────────────────────────

    // Curve sampling resolution (points per visible time unit).
    uint32_t curve_resolution() const { return curve_resolution_; }
    void     set_curve_resolution(uint32_t res) { curve_resolution_ = res; }

    // Show/hide grid.
    bool show_grid() const { return show_grid_; }
    void set_show_grid(bool show) { show_grid_ = show; }

    // Show/hide tangent handles.
    bool show_tangents() const { return show_tangents_; }
    void set_show_tangents(bool show) { show_tangents_ = show; }

    // Show/hide value labels on keyframes.
    bool show_value_labels() const { return show_value_labels_; }
    void set_show_value_labels(bool show) { show_value_labels_ = show; }

    // Playhead time (drawn as vertical line).
    float playhead_time() const { return playhead_time_; }
    void  set_playhead_time(float t) { playhead_time_ = t; }

    // ─── Callbacks ───────────────────────────────────────────────────

    void set_on_keyframe_moved(CurveEditCallback cb);
    void set_on_value_changed(CurveValueChangeCallback cb);
    void set_on_tangent_changed(CurveEditCallback cb);

    // ─── ImGui Drawing ───────────────────────────────────────────────
#ifdef SPECTRA_USE_IMGUI
    void draw(float width, float height);
#endif

   private:
    KeyframeInterpolator* interpolator_ = nullptr;

    CurveViewTransform view_;
    CurveDragState     drag_;

    // Per-channel display state
    struct ChannelDisplay
    {
        uint32_t channel_id = 0;
        Color    color      = colors::cyan;
        bool     visible    = true;
    };
    std::vector<ChannelDisplay> channel_displays_;

    // Display options
    uint32_t curve_resolution_  = 200;
    bool     show_grid_         = true;
    bool     show_tangents_     = true;
    bool     show_value_labels_ = false;
    float    playhead_time_     = 0.0f;

    // Callbacks
    CurveEditCallback        on_keyframe_moved_;
    CurveValueChangeCallback on_value_changed_;
    CurveEditCallback        on_tangent_changed_;

    // Default channel colors (cycle through these)
    static constexpr Color kChannelColors[] = {
        {0.40f, 0.76f, 1.00f, 1.0f},   // Light blue
        {1.00f, 0.60f, 0.30f, 1.0f},   // Orange
        {0.50f, 0.90f, 0.50f, 1.0f},   // Green
        {1.00f, 0.40f, 0.40f, 1.0f},   // Red
        {0.80f, 0.60f, 1.00f, 1.0f},   // Purple
        {1.00f, 0.85f, 0.30f, 1.0f},   // Yellow
        {0.40f, 1.00f, 0.85f, 1.0f},   // Teal
        {1.00f, 0.50f, 0.75f, 1.0f},   // Pink
    };
    static constexpr size_t kChannelColorCount = sizeof(kChannelColors) / sizeof(kChannelColors[0]);

    ChannelDisplay*       find_display(uint32_t channel_id);
    const ChannelDisplay* find_display(uint32_t channel_id) const;
    ChannelDisplay&       ensure_display(uint32_t channel_id);

    // Box-select state (used by draw() input handling).
    bool  box_select_active_  = false;
    float box_select_start_x_ = 0.0f;
    float box_select_start_y_ = 0.0f;
};

}   // namespace spectra
