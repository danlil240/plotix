#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "design_tokens.hpp"
#include "theme.hpp"

struct ImVec4;   // Forward declaration for ImGui
struct ImFont;   // Forward declaration for ImGui
struct ImGuiContext;

namespace spectra::ui
{

// ─── Font Awesome 6 Free Solid codepoints ──────────────────────────────────
// Style: Solid (filled). One style only for visual consistency.
// License: SIL OFL 1.1 — see third_party/fa_solid_900.hpp
// Glyph ranges: U+F000–U+F8FF (main), U+E000–U+E0FF (supplemental)

enum class Icon : uint16_t
{
    // Navigation icons
    ChartLine    = 0xF201,   // fa-chart-line
    ScatterChart = 0xE522,   // fa-magnifying-glass-chart
    Axes         = 0xF1DE,   // fa-sliders
    Wrench       = 0xF0AD,   // fa-wrench
    Folder       = 0xF07B,   // fa-folder
    Settings     = 0xF013,   // fa-gear
    Help         = 0xF059,   // fa-circle-question

    // Toolbar icons
    ZoomIn    = 0xF00E,   // fa-magnifying-glass-plus
    Hand      = 0xF256,   // fa-hand
    Ruler     = 0xF546,   // fa-ruler
    Crosshair = 0xF05B,   // fa-crosshairs
    Pin       = 0xF08D,   // fa-thumbtack
    Type      = 0xF031,   // fa-font

    // Action icons
    Export = 0xF56E,   // fa-file-export
    Save   = 0xF0C7,   // fa-floppy-disk
    Copy   = 0xF0C5,   // fa-copy
    Undo   = 0xF0E2,   // fa-rotate-left
    Redo   = 0xF01E,   // fa-rotate-right
    Search = 0xF002,   // fa-magnifying-glass
    Filter = 0xF0B0,   // fa-filter

    // Status icons
    Check   = 0xF058,   // fa-circle-check
    Warning = 0xF071,   // fa-triangle-exclamation
    Error   = 0xF057,   // fa-circle-xmark
    Info    = 0xF05A,   // fa-circle-info

    // UI icons
    ChevronRight = 0xF054,   // fa-chevron-right
    ChevronDown  = 0xF078,   // fa-chevron-down
    Close        = 0xF00D,   // fa-xmark
    Menu         = 0xF0C9,   // fa-bars
    Maximize     = 0xF065,   // fa-expand  (actually up-right-and-down-left-from-center)
    Minimize     = 0xF066,   // fa-compress

    // Series icons
    Eye       = 0xF06E,   // fa-eye
    EyeOff    = 0xF070,   // fa-eye-slash
    Palette   = 0xF53F,   // fa-palette
    LineWidth = 0xF1FC,   // fa-paintbrush

    // Additional icons
    Plus         = 0xF067,   // fa-plus
    Minus        = 0xF068,   // fa-minus
    Play         = 0xF04B,   // fa-play
    Pause        = 0xF04C,   // fa-pause
    Stop         = 0xF04D,   // fa-stop
    StepForward  = 0xF051,   // fa-forward-step
    StepBackward = 0xF048,   // fa-backward-step

    // Theme icons
    Sun      = 0xF185,   // fa-sun
    Moon     = 0xF186,   // fa-moon
    Contrast = 0xF042,   // fa-circle-half-stroke

    // Layout icons
    Layout          = 0xF00A,   // fa-table-cells
    SplitHorizontal = 0xF58D,   // fa-grip-lines
    SplitVertical   = 0xF58E,   // fa-grip-lines-vertical
    Tab             = 0xF0DB,   // fa-columns  (actually table-columns)

    // Data icons
    LineChart = 0xF1FE,   // fa-chart-area
    BarChart  = 0xF080,   // fa-chart-bar
    PieChart  = 0xF200,   // fa-chart-pie
    Heatmap   = 0xE473,   // fa-chart-simple

    // Transform icons
    ArrowUp    = 0xF062,   // fa-arrow-up
    ArrowDown  = 0xF063,   // fa-arrow-down
    ArrowLeft  = 0xF060,   // fa-arrow-left
    ArrowRight = 0xF061,   // fa-arrow-right
    Refresh    = 0xF2F1,   // fa-arrows-rotate

    // Misc
    Clock    = 0xF017,   // fa-clock
    Calendar = 0xF073,   // fa-calendar
    Tag      = 0xF02B,   // fa-tag
    Star     = 0xF005,   // fa-star
    Link     = 0xF0C1,   // fa-link
    Unlink   = 0xF127,   // fa-link-slash
    Lock     = 0xF023,   // fa-lock
    Unlock   = 0xF09C,   // fa-unlock

    // Command palette
    Command  = 0xF120,   // fa-terminal
    Keyboard = 0xF11C,   // fa-keyboard
    Shortcut = 0xF0E7,   // fa-bolt

    // Workspace
    FolderOpen = 0xF07C,   // fa-folder-open
    File       = 0xF15B,   // fa-file
    FileText   = 0xF15C,   // fa-file-lines

    // View modes
    Grid           = 0xF00A,   // fa-table-cells
    List           = 0xF03A,   // fa-list
    Fullscreen     = 0xF065,   // fa-expand
    FullscreenExit = 0xF066,   // fa-compress

    // Editing
    Edit      = 0xF303,   // fa-pen
    Scissors  = 0xF0C4,   // fa-scissors
    Trash     = 0xF1F8,   // fa-trash
    Duplicate = 0xF24D,   // fa-clone

    // Math/analysis
    Function = 0xF698,   // fa-function  (actually fa-f, use superscript)
    Integral = 0xF534,   // fa-infinity
    Sigma    = 0xF12B,   // fa-superscript (placeholder — FA6 has no sigma)
    Sqrt     = 0xF698,   // fa-square-root-variable

    // Markers
    Circle      = 0xF111,   // fa-circle
    Square      = 0xF0C8,   // fa-square
    Triangle    = 0xF0D8,   // fa-caret-up (visual triangle marker)
    Diamond     = 0xF3A5,   // fa-gem
    Cross       = 0xF00D,   // fa-xmark
    PlusMarker  = 0xF067,   // fa-plus
    MinusMarker = 0xF068,   // fa-minus
    Asterisk    = 0xF069,   // fa-asterisk

    // Line styles
    LineSolid   = 0xF068,   // fa-minus (solid line)
    LineDashed  = 0xF141,   // fa-ellipsis-vertical (dashed line placeholder)
    LineDotted  = 0xF142,   // fa-ellipsis (dotted line placeholder)
    LineDashDot = 0xF068,   // fa-minus (fallback)

    // Special
    Home    = 0xF015,   // fa-house
    Back    = 0xF053,   // fa-chevron-left  (actually 0xF053)
    Forward = 0xF054,   // fa-chevron-right
    Up      = 0xF077,   // fa-chevron-up
    Down    = 0xF078,   // fa-chevron-down

    // Vision nav rail icons
    MousePointer = 0xF245,   // fa-hand-pointer (select tool)
    Comment      = 0xF075,   // fa-comment (annotate)
    VectorSquare = 0xF5CB,   // fa-vector-square (ROI)
    MapPin       = 0xF3C5,   // fa-location-dot (markers)
    MagicWand    = 0xF0D0,   // fa-wand-magic-sparkles (transform)
    Database     = 0xF1C0,   // fa-database (data)
    Timeline     = 0xF550,   // fa-stream / fa-timeline
    Code         = 0xF121,   // fa-code (python)
    Broadcast    = 0xE4D9,   // fa-tower-broadcast (topics / pub-sub)
    ChartArea    = 0xF1FE,   // fa-chart-area (analysis)
    WindowIcon   = 0xF2D0,   // fa-window-maximize
    PlayCircle   = 0xF144,   // fa-circle-play (run button)
    UserCircle   = 0xF2BD,   // fa-circle-user (avatar)

    // End marker (not a real glyph)
    Last = 0xF8FF
};

class IconFont
{
   public:
    static IconFont& instance();

    // Initialize the icon font (call once during app startup)
    bool initialize();
    bool is_initialized() const { return initialized_; }
    void release_context(ImGuiContext* context);

    // Get ImGui font for icons
    ImFont* get_font(float size = tokens::ICON_MD) const;

    // Draw an icon at current cursor position
    void draw(Icon icon, float size = tokens::ICON_MD, const Color& color = {}) const;
    void draw(Icon icon, float size, const ImVec4& color) const;

    // Get icon as string (for ImGui text rendering)
    const char* get_icon_string(Icon icon) const;

    // Get icon width for a given size
    float get_width(Icon icon, float size) const;

    // Check if icon exists
    bool has_icon(Icon icon) const;

    // Get all available icons (for debugging)
    const std::vector<Icon>& get_all_icons() const;

   private:
    IconFont() = default;

    struct ContextFonts
    {
        ImFont* font_16 = nullptr;
        ImFont* font_20 = nullptr;
        ImFont* font_24 = nullptr;
        ImFont* font_32 = nullptr;
    };

    bool                                            initialized_ = false;
    std::unordered_map<ImGuiContext*, ContextFonts> fonts_by_context_;

    // Icon to Unicode codepoint mapping
    std::unordered_map<Icon, uint32_t> icon_map_;

    // Unicode codepoint to string mapping
    std::unordered_map<uint32_t, std::string> codepoint_strings_;

    void build_icon_map();
};

// Convenience functions
inline void draw_icon(Icon icon, float size = tokens::ICON_MD, const Color& color = {})
{
    IconFont::instance().draw(icon, size, color);
}

inline void draw_icon(Icon icon, float size, const ImVec4& color)
{
    IconFont::instance().draw(icon, size, color);
}

inline const char* icon_str(Icon icon)
{
    return IconFont::instance().get_icon_string(icon);
}

inline ImFont* icon_font(float size = tokens::ICON_MD)
{
    return IconFont::instance().get_font(size);
}

// Icon helpers for common patterns
inline void draw_nav_icon(Icon icon, bool active = false)
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_LG, active ? colors.accent : colors.text_secondary);
}

inline void draw_toolbar_icon(Icon icon, bool active = false)
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_MD, active ? colors.accent : colors.text_primary);
}

inline void draw_status_icon(Icon icon, const Color& color = {})
{
    const auto& colors = theme();
    draw_icon(icon, tokens::ICON_SM, color.r > 0 ? color : colors.text_secondary);
}

}   // namespace spectra::ui
