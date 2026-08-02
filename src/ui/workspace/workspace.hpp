#pragma once

#include <cstdint>
#include <memory>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

#include "overlay_snapshot.hpp"

namespace spectra
{

class Figure;

// Serializable workspace state: captures all figures, series configurations,
// panel states, zoom levels, and theme for save/load.
// Format: JSON text file with .spectra extension.
struct WorkspaceData
{
    // File format version for migration support
    static constexpr uint32_t FORMAT_VERSION = 5;

    struct AxisState
    {
        float       x_min = 0.0f, x_max = 1.0f;
        float       y_min = 0.0f, y_max = 1.0f;
        bool        auto_fit     = true;
        bool        grid_visible = true;
        std::string x_label;
        std::string y_label;
        std::string title;
        bool        is_3d = false;   // v4: true if this axes is an Axes3D
    };

    // v4: 3D axes state (one per 3D axes, indexed parallel to AxisState)
    struct Axes3DState
    {
        size_t      axes_index = 0;   // Index into FigureState::axes
        float       z_min = 0.0f, z_max = 1.0f;
        std::string z_label;
        std::string camera_state;            // Camera::serialize() JSON
        int         grid_planes       = 1;   // Axes3D::GridPlane bitmask
        bool        show_bounding_box = true;
        bool        lighting_enabled  = true;
        float       light_dir_x = 1.0f, light_dir_y = 1.0f, light_dir_z = 1.0f;
    };

    struct SeriesState
    {
        std::string name;
        std::string type;   // "line", "scatter", "line3d", "scatter3d", "surface", "mesh"
        float       color_r = 1.0f, color_g = 1.0f, color_b = 1.0f, color_a = 1.0f;
        float       line_width  = 2.0f;
        float       marker_size = 6.0f;
        bool        visible     = true;
        // Data is NOT saved (too large); only metadata
        size_t point_count = 0;
        // Series-level opacity (for legend interaction fade)
        float opacity = 1.0f;
        // Plot style (line style + marker style)
        int line_style   = 1;   // LineStyle enum value (default Solid)
        int marker_style = 0;   // MarkerStyle enum value (default None)
        // v3: dash pattern
        std::vector<float> dash_pattern;
        // v4: 3D series fields
        int   colormap_type = 0;   // ColormapType enum
        float ambient       = 0.0f;
        float specular      = 0.0f;
        float shininess     = 0.0f;
    };

    struct FigureState
    {
        FigureId                 figure_id = 0;
        std::string              title;
        uint32_t                 width       = 1280;
        uint32_t                 height      = 720;
        int                      grid_rows   = 1;
        int                      grid_cols   = 1;
        bool                     is_modified = false;
        std::string              custom_tab_title;   // Tab title from FigureManager
        std::vector<AxisState>   axes;
        std::vector<SeriesState> series;
        std::vector<Axes3DState> axes_3d;   // v4: 3D axes state
        // v5 optional extension: complete FigureSerializer payload. This is
        // what makes populated workspaces and autosaves restart-safe.
        std::string snapshot_base64;
        // Per-figure TimelineEditor state, including authored tracks and
        // keyframes. Empty preserves compatibility with older workspaces.
        std::string timeline_json;
    };

    struct PanelState
    {
        bool  inspector_visible = true;
        float inspector_width   = 320.0f;
        bool  nav_rail_expanded = false;
    };

    // Overlay state is stored as a plain-data OverlaySnapshot (shared with
    // the binary figure serializer).
    using InteractionState = OverlaySnapshot;

    uint32_t                 version             = FORMAT_VERSION;
    std::string              theme_name          = "night";
    FigureId                 active_figure_index = 0;
    PanelState               panels;
    InteractionState         interaction;
    std::vector<FigureState> figures;

    // Undo history metadata (not the actual actions — just count for UI display)
    size_t undo_count = 0;
    size_t redo_count = 0;

    // Dock/split view state (serialized JSON from DockSystem)
    std::string dock_state;

    // v3: Axis link groups (serialized JSON from AxisLinkManager)
    std::string axis_link_state;

    // v3: Data transform pipeline presets per-axes
    struct TransformState
    {
        FigureId    figure_index = 0;
        size_t      axes_index   = 0;
        size_t      series_index = 0;
        bool        all_visible  = false;
        bool        axes_only    = false;
        std::string target;
        std::string name;
        struct Step
        {
            int         type    = 0;      // DataTransform::Type enum value
            float       param   = 0.0f;   // Scale/offset/clamp parameter
            bool        enabled = true;
            std::string name;
            std::string source;
            int         params_version  = 0;
            float       scale_factor    = 1.0f;
            float       offset_value    = 0.0f;
            float       clamp_min       = 0.0f;
            float       clamp_max       = 1.0f;
            float       log_base        = 10.0f;
            bool        skip_nan        = true;
            bool        fft_db          = false;
            float       fft_sample_rate = 0.0f;
        };
        std::vector<Step> steps;
    };
    std::vector<TransformState> transforms;

    // v3: Shortcut overrides (user keybinding customizations)
    struct ShortcutOverride
    {
        std::string command_id;
        std::string shortcut_str;   // "" means unbound
        bool        removed = false;
    };
    std::vector<ShortcutOverride> shortcut_overrides;

    // v3: Timeline editor state
    struct TimelineState
    {
        float playhead   = 0.0f;
        float duration   = 10.0f;
        float fps        = 30.0f;
        int   loop_mode  = 0;   // 0=None, 1=Loop, 2=PingPong
        float loop_start = 0.0f;
        float loop_end   = 0.0f;
        bool  playing    = false;
    };
    TimelineState timeline;

    // v3: Plugin enabled/disabled state
    std::string plugin_state;

    // v3: Active data palette name
    std::string data_palette_name;

    // v4: Mode transition state (serialized JSON from ModeTransition)
    std::string mode_transition_state;

    // v5: Qt desktop layout state (provider-specific window/panel arrangement)
    struct DesktopLayoutState
    {
        std::string           provider;   // "native", "kddockwidgets", etc.
        std::string           provider_version;
        std::string           main_window_state_base64;      // QMainWindow::saveState()
        std::string           main_window_geometry_base64;   // QMainWindow::saveGeometry()
        std::string           main_window_split_layout;
        std::vector<FigureId> main_window_figure_ids;
        std::string           provider_layout;   // provider-specific extra layout data

        struct WindowState
        {
            std::string           state_base64;
            std::string           geometry_base64;
            std::string           title;
            std::string           split_layout;
            std::vector<FigureId> figure_ids;
        };
        std::vector<WindowState> windows;   // additional/detached windows
    };
    DesktopLayoutState desktop_layout;

    // Last directory used for image/video export. Empty = use $HOME.
    std::string last_export_dir;
};

/// Result of workspace validation.
struct WorkspaceValidationResult
{
    bool                     valid    = true;    // False if data is structurally corrupt
    bool                     repaired = false;   // True if repairs were made
    std::vector<std::string> warnings;           // Non-fatal issues found
    std::vector<std::string> errors;             // Fatal issues
};

/// Validate the internal consistency of a WorkspaceData object.
/// Checks: version compatibility, non-negative dimensions, valid axis ranges,
/// series count consistent with axes, valid series types.
/// Repairs minor issues (clamps values) when possible.
WorkspaceValidationResult validate_workspace_data(WorkspaceData& data);

// Workspace save/load operations.
// All methods are static — no persistent state needed.
class Workspace
{
   public:
    // Save workspace state to a .spectra file. Returns true on success.
    static bool save(const std::string& path, const WorkspaceData& data);

    // Load workspace state from a .spectra file. Returns true on success.
    static bool load(const std::string& path, WorkspaceData& data);

    // Capture current application state into WorkspaceData.
    // figures: all open figures. active_index: currently active figure.
    static WorkspaceData capture(const std::vector<Figure*>&         figures,
                                 size_t                              active_index,
                                 const std::string&                  theme_name,
                                 bool                                inspector_visible,
                                 float                               inspector_width,
                                 bool                                nav_rail_expanded,
                                 const std::vector<OverlaySnapshot>* overlays = nullptr);

    // Apply loaded workspace data to figures (restores axis limits, etc.).
    // Returns false if data is incompatible.
    static bool apply(const WorkspaceData& data, std::vector<Figure*>& figures);

    // Recreate every figure from its embedded snapshot. The output is only
    // replaced after all payloads have decoded and deserialized successfully.
    // When requested, overlays are returned in the same order as the figures.
    static bool restore_figures(const WorkspaceData&                  data,
                                std::vector<std::unique_ptr<Figure>>& figures,
                                std::vector<OverlaySnapshot>*         overlays = nullptr);

    // Get default workspace file path (in user config directory).
    static std::string default_path();

    // Auto-save path (for crash recovery).
    static std::string autosave_path();

    // Autosave: save workspace to autosave_path() if enough time has elapsed.
    // Returns true if an autosave was performed.
    // interval_seconds: minimum seconds between autosaves (default 60).
    static bool maybe_autosave(const WorkspaceData& data, float interval_seconds = 60.0f);

    // Check if an autosave file exists (for crash recovery prompt).
    static bool has_autosave();

    // Delete the autosave file.
    static void clear_autosave();

    // Serialize workspace data to JSON string.
    static std::string serialize_json(const WorkspaceData& data);

   private:
    // JSON serialization helpers (minimal, no external dependency)
    static bool deserialize_json(const std::string& json, WorkspaceData& data);

    // Simple JSON value parser
    static std::string read_string_value(const std::string& json, const std::string& key);
    static double      read_number_value(const std::string& json,
                                         const std::string& key,
                                         double             default_val = 0.0);
    static bool        read_bool_value(const std::string& json,
                                       const std::string& key,
                                       bool               default_val = false);
};

}   // namespace spectra
