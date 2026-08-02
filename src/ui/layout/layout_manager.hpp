#pragma once

#include <spectra/series.hpp>   // For Rect definition

namespace spectra
{

/**
 * LayoutManager - Zone-based layout engine for Spectra UI
 *
 * Replaces hardcoded pixel positions with a responsive zone system.
 * All UI components query their layout rectangles from this manager.
 * Supports smooth animated transitions for panel open/close/resize.
 */
class LayoutManager
{
   public:
    LayoutManager();
    ~LayoutManager() = default;

    // Disable copying
    LayoutManager(const LayoutManager&)            = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    /**
     * Update all zone rectangles based on current window size and delta time.
     * Call this once per frame. Drives animated transitions.
     */
    void update(float window_width, float window_height, float dt = 0.0f);

    // Zone rectangle queries
    Rect command_bar_rect() const;
    Rect nav_rail_rect() const;
    Rect canvas_rect() const;
    // Chrome-based workspace between toolbars; ignores plot-area canvas override.
    Rect workspace_rect() const;
    Rect inspector_rect() const;
    Rect status_bar_rect() const;
    Rect tab_bar_rect() const;

    // Configuration
    void set_inspector_visible(bool visible);
    void set_inspector_width(float width);
    void set_nav_rail_width(float width);
    void set_nav_rail_visible(bool visible);
    void set_nav_rail_expanded(bool expanded);
    void set_tab_bar_visible(bool visible);
    void reset_inspector_width();

    // Canvas rect override — allows adapter shells (e.g. spectra-ros) to
    // specify the exact canvas region instead of computing it from chrome zones.
    void set_canvas_override(Rect r)
    {
        canvas_override_     = r;
        canvas_override_set_ = true;
    }
    void clear_canvas_override() { canvas_override_set_ = false; }
    bool has_canvas_override() const { return canvas_override_set_; }

    // Bottom panel (timeline)
    void  set_bottom_panel_height(float h) { bottom_panel_height_ = h; }
    float bottom_panel_height() const { return bottom_panel_height_; }

    // State queries
    bool  is_inspector_visible() const { return inspector_visible_; }
    float inspector_width() const { return inspector_width_; }
    float inspector_animated_width() const { return inspector_anim_width_; }
    // Normalized open amount [0, 1] for content fade during slide animation.
    float inspector_open_amount() const;
    bool  is_nav_rail_visible() const { return nav_rail_visible_; }
    bool  is_nav_rail_expanded() const { return nav_rail_expanded_; }
    float nav_rail_width() const;
    float nav_rail_animated_width() const { return nav_rail_anim_width_; }
    bool  is_tab_bar_visible() const { return tab_bar_visible_; }
    bool  is_animating() const;

    // When true, panel width animations snap instantly (live window resize).
    void set_resize_active(bool active) { resize_active_ = active; }
    bool is_resize_active() const { return resize_active_; }

    // Inspector resize interaction helpers
    bool is_inspector_resize_hovered() const { return inspector_resize_hovered_; }
    void set_inspector_resize_hovered(bool hovered) { inspector_resize_hovered_ = hovered; }
    bool is_inspector_resize_active() const { return inspector_resize_active_; }
    void set_inspector_resize_active(bool active) { inspector_resize_active_ = active; }

    // Layout constants (matching the design spec)
    static constexpr float COMMAND_BAR_HEIGHT       = 48.0f;
    static constexpr float STATUS_BAR_HEIGHT        = 34.0f;
    static constexpr float NAV_RAIL_COLLAPSED_WIDTH = 72.0f;
    static constexpr float NAV_RAIL_EXPANDED_WIDTH  = 200.0f;
    static constexpr float NAV_TOOLBAR_INSET =
        72.0f;   // Nav rail width (full-height docked rail, no floating offset)
    static constexpr float PLOT_LEFT_MARGIN =
        100.0f;   // Default plot left margin (matches Margins::left) for tab alignment
    static constexpr float INSPECTOR_DEFAULT_WIDTH = 320.0f;
    static constexpr float INSPECTOR_MIN_WIDTH     = 240.0f;
    static constexpr float INSPECTOR_MAX_WIDTH     = 480.0f;
    static constexpr float TAB_BAR_HEIGHT          = 40.0f;
    static constexpr float RESIZE_HANDLE_WIDTH     = 6.0f;
    static constexpr float ANIM_SPEED              = 12.0f;   // Nav rail exponential smoothing

    // Nav rail vertical content (icon+label buttons stacked in draw_nav_rail).
    // SpectraNavRail: 8 tools + 6 panels + Help = 15 buttons, 4 section separators.
    static constexpr int NAV_RAIL_BUTTON_COUNT    = 15;
    static constexpr int NAV_RAIL_SEPARATOR_COUNT = 4;
    // Legacy ImGuiIntegration::draw_nav_rail fallback (no AppShell).
    static constexpr int   NAV_RAIL_FALLBACK_BUTTON_COUNT = 12;
    static constexpr float NAV_RAIL_CELL_HEIGHT           = 56.0f;
    static constexpr float NAV_RAIL_CELL_HEIGHT_MIN       = 36.0f;
    static constexpr float NAV_RAIL_SEPARATOR_HEIGHT      = 9.0f;
    static constexpr float NAV_RAIL_VERTICAL_PADDING      = 24.0f;   // top+bottom window padding
    static constexpr float WINDOW_MIN_HEIGHT_NO_NAV       = 240.0f;
    static constexpr float WINDOW_MIN_CANVAS_WIDTH        = 160.0f;

    static float nav_rail_content_height(int button_count, int separator_count, float cell_scale);
    static float nav_rail_nominal_content_height();
    static float nav_rail_min_content_height();
    static float nav_rail_scale_for_height(float available_height);
    static float nav_rail_scale_for_height(float available_height,
                                           int   button_count,
                                           int   separator_count);
    static float min_window_height(bool nav_rail_visible,
                                   bool command_bar_visible,
                                   bool status_bar_visible);
    static float min_window_width(bool nav_rail_visible);

   private:
    // Window dimensions
    float window_width_  = 1280.0f;
    float window_height_ = 720.0f;

    // Zone rectangles (computed in update())
    Rect command_bar_rect_;
    Rect nav_rail_rect_;
    Rect canvas_rect_;
    Rect workspace_rect_;
    Rect inspector_rect_;
    Rect status_bar_rect_;
    Rect tab_bar_rect_;

    // Configuration state
    bool  inspector_visible_        = false;
    float inspector_width_          = INSPECTOR_DEFAULT_WIDTH;
    bool  nav_rail_visible_         = true;
    bool  nav_rail_expanded_        = false;
    float nav_rail_collapsed_width_ = NAV_RAIL_COLLAPSED_WIDTH;
    float nav_rail_expanded_width_  = NAV_RAIL_EXPANDED_WIDTH;
    bool  tab_bar_visible_          = false;
    float bottom_panel_height_      = 0.0f;   // Timeline panel height (0 when hidden)

    // Animated state (smoothly interpolated toward targets)
    float inspector_anim_width_ = 0.0f;   // 0 when hidden
    float inspector_anim_t_     = 0.0f;   // Linear progress 0=closed, 1=open
    float nav_rail_anim_width_  = NAV_RAIL_COLLAPSED_WIDTH;

    // Inspector resize interaction state
    bool inspector_resize_hovered_ = false;
    bool inspector_resize_active_  = false;

    // External canvas override
    Rect canvas_override_{};
    bool canvas_override_set_ = false;

    bool resize_active_ = false;

    // Helper: exponential smoothing toward target (nav rail)
    static float smooth_toward(float current, float target, float speed, float dt);
    void         update_inspector_animation(float dt);

    // Helper methods
    void compute_zones();
    Rect compute_command_bar() const;
    Rect compute_nav_rail() const;
    Rect compute_canvas() const;
    Rect compute_workspace() const;
    Rect compute_inspector() const;
    Rect compute_status_bar() const;
    Rect compute_tab_bar() const;
};

}   // namespace spectra
