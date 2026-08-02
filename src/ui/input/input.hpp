#pragma once

#include <chrono>
#include <functional>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>

namespace spectra
{

class AnimationController;
class AxisLinkManager;
class DataInteraction;
class FigureViewModel;
class GestureRecognizer;
class ShortcutManager;
class TransitionEngine;
class UndoManager;

// Tool mode (selected by toolbar buttons)
enum class ToolMode
{
    Pan,        // Pan tool - left click and drag to pan
    BoxZoom,    // Box zoom tool - right click and drag to zoom
    Select,     // Select tool - left click and drag to region-select data points
    Measure,    // Measure tool - left click and drag to measure distance between two points
    Annotate,   // Annotate tool - place text annotations on the plot
    ROI,        // ROI tool - draw a region of interest rectangle
};

// Interaction state for the input handler (dragging state)
enum class InteractionMode
{
    Idle,
    Dragging,   // Currently dragging (either pan or box zoom)
};

// Cursor readout data (updated every mouse move)
struct CursorReadout
{
    bool valid = false;
    // Double precision: epoch-scale x values (~1.7e9) exceed float's
    // ~7-digit precision and would quantize to ~128 s steps.
    double data_x   = 0.0;
    double data_y   = 0.0;
    double screen_x = 0.0;
    double screen_y = 0.0;
};

// Box zoom selection rectangle (screen coordinates)
struct BoxZoomRect
{
    bool   active = false;
    double x0     = 0.0;
    double y0     = 0.0;
    double x1     = 0.0;
    double y1     = 0.0;
};

// Callback for save-PNG shortcut (Ctrl+S)
using SavePngCallback = std::function<void()>;

// Input handler: maps mouse/keyboard events to axis limit mutations,
// box zoom, keyboard shortcuts, and multi-axes hit-testing.
class InputHandler
{
   public:
    InputHandler();
    ~InputHandler();

    // Set the figure for multi-axes hit-testing
    void set_figure(Figure* fig) { figure_ = fig; }

    // Set the active axes that input events will affect
    void set_active_axes(Axes* axes)
    {
        active_axes_      = axes;
        active_axes_base_ = axes;
    }
    void set_active_axes_base(AxesBase* axes)
    {
        active_axes_base_ = axes;
        active_axes_      = dynamic_cast<Axes*>(axes);
    }
    Axes*     active_axes() const { return active_axes_; }
    AxesBase* active_axes_base() const { return active_axes_base_; }

    // Mouse button event: begin/end drag or box zoom
    void on_mouse_button(int button, int action, int mods, double x, double y);

    // Mouse move event: pan if dragging, update cursor readout
    void on_mouse_move(double x, double y);

    // Clear cursor readout when the pointer leaves the window.
    void clear_cursor_readout();

    // Scroll event: zoom around cursor position (animated)
    void on_scroll(double x_offset, double y_offset, double cursor_x, double cursor_y);

    // Per-frame update: advances animations and inertial pan. dt in seconds.
    void update(float dt);

    // Set the animation controller (owned externally, e.g. by App)
    void                 set_animation_controller(AnimationController* ctrl) { anim_ctrl_ = ctrl; }
    AnimationController* animation_controller() const { return anim_ctrl_; }

    // Set the gesture recognizer (owned externally)
    void               set_gesture_recognizer(GestureRecognizer* gr) { gesture_ = gr; }
    GestureRecognizer* gesture_recognizer() const { return gesture_; }

    // Set the data interaction layer (owned externally)
    void             set_data_interaction(DataInteraction* di) { data_interaction_ = di; }
    DataInteraction* data_interaction() const { return data_interaction_; }

    // Set the transition engine (owned externally by App)
    // When set, preferred over AnimationController for all animations.
    void              set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }
    TransitionEngine* transition_engine() const { return transition_engine_; }

    // Set the shortcut manager (owned externally by App)
    void             set_shortcut_manager(ShortcutManager* sm) { shortcut_mgr_ = sm; }
    ShortcutManager* shortcut_manager() const { return shortcut_mgr_; }

    // Set the undo manager (owned externally by App)
    void         set_undo_manager(UndoManager* um) { undo_mgr_ = um; }
    UndoManager* undo_manager() const { return undo_mgr_; }

    // Set the figure view model (Phase 2 LT-5: ViewModel-based limit mutations)
    void             set_figure_view_model(FigureViewModel* vm) { figure_vm_ = vm; }
    FigureViewModel* figure_view_model() const { return figure_vm_; }

    // Set the axis link manager (owned externally by App)
    void             set_axis_link_manager(AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Key event: keyboard shortcuts
    void on_key(int key, int action, int mods);

    // Set the viewport rect for coordinate mapping (single-axes fallback)
    void set_viewport(float vp_x, float vp_y, float vp_w, float vp_h);

    // Set visible canvas height (for page scroll when subplots overflow)
    void  set_visible_height(float h) { visible_height_ = h; }
    float visible_height() const { return visible_height_; }

    // Screen rect where plot pan/zoom/scroll may start (layout canvas / pane content).
    void set_interaction_rect(const Rect& r);

    // Current interaction state (dragging)
    InteractionMode mode() const { return mode_; }
    void            set_mode(InteractionMode new_mode) { mode_ = new_mode; }

    // Current tool mode (selected by toolbar)
    ToolMode tool_mode() const { return tool_mode_; }
    void     set_tool_mode(ToolMode new_tool);

    // Cursor readout (for overlay rendering)
    const CursorReadout& cursor_readout() const { return cursor_readout_; }

    // Box zoom rectangle (for overlay rendering)
    const BoxZoomRect& box_zoom_rect() const { return box_zoom_; }

    // Measure tool state (for overlay rendering)
    bool  is_measure_dragging() const { return measure_dragging_; }
    bool  has_measure_result() const { return measure_click_state_ >= 1; }
    Axes* measure_axes() const { return measure_axes_; }

    // Select tool rectangle state (for overlay rendering)
    bool               is_select_rect_active() const { return select_rect_active_; }
    const BoxZoomRect& select_rect() const { return select_rect_; }

    // Middle-mouse pan state (for callback passthrough)
    bool   is_middle_pan_dragging() const { return middle_pan_dragging_; }
    double measure_start_data_x() const { return measure_start_data_x_; }
    double measure_start_data_y() const { return measure_start_data_y_; }
    double measure_end_data_x() const { return measure_end_data_x_; }
    double measure_end_data_y() const { return measure_end_data_y_; }
    void   restore_measure_result(Axes*  axes,
                                  double start_x,
                                  double start_y,
                                  double end_x,
                                  double end_y);

    // True if any interaction animation is running (zoom, pan inertia, auto-fit)
    bool has_active_animations() const;

    // Register callback for Ctrl+S save
    void set_save_callback(SavePngCallback cb) { save_callback_ = std::move(cb); }

    // Convert screen coordinates to data coordinates using current active axes.
    // Double precision throughout: axis limits can sit at epoch scale (~1.7e9)
    // where float would quantize to ~128 s steps.
    void screen_to_data(double screen_x, double screen_y, double& data_x, double& data_y) const;

    // Hit-test: find which Axes the cursor is over (public for context menu)
    Axes* hit_test_axes(double screen_x, double screen_y) const;

    // Hit-test all axes (including 3D) — returns AxesBase*
    AxesBase* hit_test_all_axes(double screen_x, double screen_y) const;

    // Invalidate cached figure/axes pointers (call when a figure is destroyed).
    // Also cancels any in-flight AnimationController animations referencing
    // the figure's axes to prevent heap-use-after-free.
    void clear_figure_cache(Figure* fig = nullptr);

   private:
    // Get viewport for a given axes (from figure layout)
    const Rect& viewport_for_axes(const AxesBase* axes) const;

    // Phase 2 (LT-5): Route limit mutations through AxesViewModel when available.
    // Falls back to direct Axes::xlim/ylim when no FigureViewModel is set.
    void set_axes_xlim(Axes* ax, double min, double max);
    void set_axes_ylim(Axes* ax, double min, double max);

    // Apply box zoom: set limits from selection rectangle
    void apply_box_zoom();

    // Cancel box zoom
    void cancel_box_zoom();

    // ─── Pan/Zoom helpers (input_pan_zoom.cpp) ──────────────────────────
    void handle_mouse_button_3d(Axes3D* axes3d, int button, int action, double x, double y);
    void handle_mouse_button_middle_pan(int action, double x, double y);
    void handle_mouse_button_rclick_zoom(int action, double x, double y);
    void handle_mouse_button_box_zoom(int action, double x, double y);
    void handle_mouse_button_pan(int action, int mods, double x, double y);
    void handle_mouse_move_3d_drag(double x, double y);
    void handle_mouse_move_rclick_zoom(double x, double y);
    void handle_mouse_move_pan_drag(double x, double y);
    void handle_scroll_3d(Axes3D* axes3d, double y_offset);
    void handle_scroll_2d(double y_offset, double cursor_x, double cursor_y);
    void snap_axes3d_axis_view(Axes3D* axes3d, Camera::AxisView view);
    void update_axes3d_arrow_hover(AxesBase* hit_base, double x, double y);

    // ─── Select/ROI helpers (input_select.cpp) ──────────────────────────
    void handle_mouse_button_select(int action, double x, double y);
    void handle_mouse_button_roi(int action, double x, double y);
    void handle_mouse_move_select(double x, double y);
    void handle_mouse_move_roi(double x, double y);

    // ─── Measure helpers (input_measure.cpp) ─────────────────────────────
    void handle_mouse_button_measure(int action, double x, double y);
    void handle_mouse_move_measure(double x, double y);

    // ─── Annotate helpers (input_annotate.cpp) ───────────────────────────
    void handle_mouse_button_annotate(int button, int action, double x, double y);
    void handle_mouse_move_annotate(double x, double y);

    Figure*          figure_           = nullptr;
    FigureViewModel* figure_vm_        = nullptr;
    Axes*            active_axes_      = nullptr;
    AxesBase*        active_axes_base_ = nullptr;

    // 3D orbit drag state
    bool                   is_3d_orbit_drag_   = false;
    bool                   is_3d_pan_drag_     = false;
    static constexpr int   MOUSE_BUTTON_MIDDLE = 2;
    static constexpr float ORBIT_SENSITIVITY   = 0.3f;
    static constexpr float ZOOM_3D_FACTOR      = 0.1f;

    InteractionMode mode_      = InteractionMode::Idle;
    ToolMode        tool_mode_ = ToolMode::Pan;   // Default tool mode

    // Pan drag state
    double drag_start_x_        = 0.0;
    double drag_start_y_        = 0.0;
    double drag_start_xlim_min_ = 0.0;
    double drag_start_xlim_max_ = 0.0;
    double drag_start_ylim_min_ = 0.0;
    double drag_start_ylim_max_ = 0.0;

    // Box zoom state
    BoxZoomRect box_zoom_;

    // Cursor readout
    CursorReadout cursor_readout_;

    // Viewport for coordinate mapping (single-axes fallback)
    float vp_x_ = 0.0f;
    float vp_y_ = 0.0f;
    float vp_w_ = 1.0f;
    float vp_h_ = 1.0f;

    // Visible canvas height for page scroll
    float visible_height_ = 0.0f;

    // Plot interaction bounds (set each frame from LayoutManager / dock content_bounds).
    Rect interaction_rect_{};
    bool interaction_rect_set_ = false;

    bool point_in_interaction_rect(double x, double y) const;

    // Callbacks
    SavePngCallback save_callback_;

    // Animation controller (not owned)
    AnimationController* anim_ctrl_ = nullptr;

    // Gesture recognizer (not owned)
    GestureRecognizer* gesture_ = nullptr;

    // Data interaction layer (not owned)
    DataInteraction* data_interaction_ = nullptr;

    // Transition engine (not owned) — preferred over anim_ctrl_ when available
    TransitionEngine* transition_engine_ = nullptr;

    // Shortcut manager (not owned)
    ShortcutManager* shortcut_mgr_ = nullptr;

    // Undo manager (not owned)
    UndoManager* undo_mgr_ = nullptr;

    // Axis link manager (not owned)
    AxisLinkManager* axis_link_mgr_ = nullptr;

    // 3D drag undo snapshots (captured at drag-start)
    AxisLimits drag3d_start_xlim_{};
    AxisLimits drag3d_start_ylim_{};
    AxisLimits drag3d_start_zlim_{};
    Camera     drag3d_start_camera_{};
    Axes3D*    drag3d_axes_ = nullptr;

    // Modifier key tracking (updated from on_key)
    int mods_ = 0;

    // Region selection drag state (ROI mode)
    bool region_dragging_ = false;
    // Select-mode press origin (used to distinguish click-to-select vs drag-select)
    double select_start_x_ = 0.0;
    double select_start_y_ = 0.0;

    // Select mode rectangle drag state (graph multi-select)
    bool        select_rect_active_ = false;
    BoxZoomRect select_rect_;

    // Measure tool state
    bool   measure_dragging_       = false;
    int    measure_click_state_    = 0;   // 0=idle, 1=first point placed, 2=second point placed
    Axes*  measure_axes_           = nullptr;   // axes where the measurement started
    double measure_start_screen_x_ = 0.0;
    double measure_start_screen_y_ = 0.0;
    double measure_start_data_x_   = 0.0;
    double measure_start_data_y_   = 0.0;
    double measure_end_data_x_     = 0.0;
    double measure_end_data_y_     = 0.0;
    bool   crosshair_was_active_   = false;   // to restore crosshair state when leaving Measure

    // Annotate tool state
    bool   annotate_dragging_     = false;
    bool   annotate_press_active_ = false;
    double annotate_start_x_      = 0.0;
    double annotate_start_y_      = 0.0;

    // Middle-mouse pan state (works in all tool modes for 2D axes)
    bool   middle_pan_dragging_ = false;
    double middle_pan_start_x_  = 0.0;
    double middle_pan_start_y_  = 0.0;
    double middle_pan_xlim_min_ = 0.0;
    double middle_pan_xlim_max_ = 0.0;
    double middle_pan_ylim_min_ = 0.0;
    double middle_pan_ylim_max_ = 0.0;

    // Ctrl+drag box zoom state (allows box zoom in Pan mode via modifier)
    bool ctrl_box_zoom_active_ = false;

    // Right-click drag zoom state
    // Drag direction determines axis contribution:
    //   2D: proportional X/Y blend based on drag angle
    //   3D: axis whose screen-projected direction best aligns with drag direction
    enum class ZoomAxis
    {
        None,
        X,
        Y,
        XY,
        Z
    };
    bool     rclick_zoom_dragging_      = false;
    bool     rclick_zoom_3d_            = false;   // true when operating on Axes3D
    ZoomAxis rclick_zoom_axis_          = ZoomAxis::None;
    double   rclick_zoom_start_x_       = 0.0;
    double   rclick_zoom_start_y_       = 0.0;
    double   rclick_zoom_last_x_        = 0.0;
    double   rclick_zoom_last_y_        = 0.0;
    double   rclick_zoom_xlim_min_      = 0.0;
    double   rclick_zoom_xlim_max_      = 0.0;
    double   rclick_zoom_ylim_min_      = 0.0;
    double   rclick_zoom_ylim_max_      = 0.0;
    double   rclick_zoom_zlim_min_      = 0.0;
    double   rclick_zoom_zlim_max_      = 0.0;
    double   rclick_zoom_anchor_data_x_ = 0.0;   // data-space anchor (drag-start point)
    double   rclick_zoom_anchor_data_y_ = 0.0;
    // Screen-space direction of each 3D axis (computed at drag start from camera MVP)
    // Stored as (dx, dy) normalized screen vectors for X, Y, Z axes
    float                   rclick_zoom_axis_sx_[3] = {};   // screen X component for axes [X, Y, Z]
    float                   rclick_zoom_axis_sy_[3] = {};   // screen Y component for axes [X, Y, Z]
    static constexpr float  RCLICK_ZOOM_SENSITIVITY = 0.005f;
    static constexpr double RCLICK_AXIS_LOCK_THRESHOLD = 10.0;   // pixels before axis locks
    static constexpr float  RCLICK_AXIS_1D_THRESHOLD_DEG =
        15.0f;   // 2D cone half-angle for pure X/Y

    // Inertial pan tracking: velocity in screen px/sec at drag release
    double last_move_x_ = 0.0;
    double last_move_y_ = 0.0;
    using Clock         = std::chrono::steady_clock;
    Clock::time_point last_move_time_{};
    Clock::time_point drag_start_time_{};

    // Zoom factor per scroll tick (0.25 = 25% range change per tick)
    static constexpr float ZOOM_FACTOR = 0.25f;

    // Animated zoom duration (seconds)
    static constexpr float ZOOM_ANIM_DURATION = 0.15f;

    // Inertial pan duration (seconds)
    static constexpr float PAN_INERTIA_DURATION = 0.3f;

    // Auto-fit animation duration (seconds)
    static constexpr float AUTOFIT_ANIM_DURATION = 0.25f;

    // Minimum velocity (data-space units/sec) to trigger inertial pan
    static constexpr float MIN_INERTIA_VELOCITY = 0.01f;

    // GLFW key constants (to avoid including GLFW in header)
    static constexpr int KEY_R       = 82;
    static constexpr int KEY_G       = 71;
    static constexpr int KEY_S       = 83;
    static constexpr int KEY_C       = 67;
    static constexpr int KEY_A       = 65;
    static constexpr int KEY_Q       = 81;
    static constexpr int KEY_ESCAPE  = 256;
    static constexpr int MOD_SHIFT   = 0x0001;
    static constexpr int MOD_CONTROL = 0x0002;
};

}   // namespace spectra
