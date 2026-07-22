#pragma once

// SpectraDesignTokens — authoritative design token source for all Spectra Qt widgets.
//
// Provides colors, geometry, typography roles, and font management for the
// custom Spectra component system.  All custom widgets reference these tokens
// instead of hardcoding values.

#include <QColor>
#include <QFont>
#include <QString>

namespace spectra::adapters::qt
{

// ─── Color Tokens ───────────────────────────────────────────────────────────

struct SpectraColors
{
    // Surfaces
    QColor window_base      {0x0D, 0x0E, 0x11};   // #0D0E11 — deepest background
    QColor bg_canvas        {0x0A, 0x0B, 0x0E};   // #0A0B0E — plot canvas
    QColor header_surface   {0x11, 0x13, 0x18};   // #111318 — app header
    QColor workspace_surface{0x0F, 0x11, 0x15};   // #0F1115 — workspace
    QColor panel_surface    {0x15, 0x17, 0x1C};   // #15171C — panels
    QColor input_surface    {0x1A, 0x1C, 0x22};   // #1A1C22 — inputs
    QColor elevated_surface {0x1F, 0x22, 0x29};   // #1F2229 — elevated/hover

    // Borders
    QColor border_subtle    {0x1A, 0x1C, 0x22};   // #1A1C22 — hairline
    QColor border_default   {0x23, 0x26, 0x2E};   // #23262E — standard
    QColor border_strong    {0x2A, 0x2D, 0x36};   // #2A2D36 — focused

    // Text
    QColor text_primary     {0xE8, 0xEC, 0xF1};   // #E8ECF1
    QColor text_secondary   {0xC8, 0xCD, 0xD6};   // #C8CDD6
    QColor text_muted       {0x8A, 0x90, 0x9C};   // #8A909C

    // Accents
    QColor cyan_accent      {0x22, 0xD3, 0xEE};   // #22D3EE — primary accent
    QColor cyan_accent_dim  {0x0E, 0x7A, 0x8A};   // dimmed cyan
    QColor purple_ambient   {0x7C, 0x5C, 0xFC};   // #7C5CFC — ambient accent
    QColor purple_dim       {0x4A, 0x3A, 0x9C};   // dimmed purple

    // Semantic
    QColor success_green    {0x4A, 0xDE, 0x80};   // #4ADE80 — FPS / success
    QColor warning_amber    {0xFB, 0xBF, 0x24};   // #FBBF24
    QColor error_red        {0xF8, 0x71, 0x71};   // #F87171

    // Glow
    QColor cyan_glow        {0x22, 0xD3, 0xEE, 0x24};   // 14% alpha
    QColor purple_glow      {0x7C, 0x5C, 0xFC, 0x20};   // 12% alpha
};

// ─── Geometry Tokens ─────────────────────────────────────────────────────────

struct SpectraGeometry
{
    // Window regions
    // Match the legacy Spectra shell. Window-manager decoration is outside
    // this client geometry; Qt must not insert a second in-app title bar.
    int title_bar_height    = 0;
    int header_height       = 48;
    int nav_rail_width      = 72;
    int nav_rail_width_compact = 48;
    int tab_bar_height      = 26;
    int status_bar_height   = 34;
    int inspector_width     = 340;
    int inspector_width_min = 300;
    int inspector_width_max = 380;
    int chevron_width       = 16;

    // Radii
    int radius_sm   = 4;
    int radius_md   = 8;
    int radius_lg   = 12;
    int radius_pill = 999;

    // Spacings
    int space_1 = 4;
    int space_2 = 8;
    int space_3 = 12;
    int space_4 = 16;
    int space_5 = 20;
    int space_6 = 24;

    // Control heights
    int control_height_sm = 28;
    int control_height_md = 32;
    int control_height_lg = 40;

    // Borders
    int border_thin   = 1;
    int border_normal = 1;
    int border_thick  = 2;
};

// ─── Typography Roles ────────────────────────────────────────────────────────

struct SpectraTypography
{
    QString family_inter       = "Inter";
    QString family_inter_medium = "Inter Medium";
    QString family_inter_semi  = "Inter Semibold";
    QString family_mono        = "Inter";
    QString family_icon        = "SpectraIcons";

    int font_xs   = 10;   // status bar, badges
    int font_sm   = 11;   // inspector labels
    int font_base = 13;   // body, controls
    int font_md   = 14;   // input values
    int font_lg   = 15;   // section headers
    int font_xl   = 17;   // panel titles
    int font_2xl  = 20;   // wordmark
};

// ─── Font Manager ────────────────────────────────────────────────────────────

class SpectraFontManager
{
public:
    static SpectraFontManager& instance();

    // Load all Spectra fonts.  Must be called before any UI is built.
    void load_fonts();

    // Query
    bool fonts_loaded() const { return loaded_; }
    bool has_inter() const { return has_inter_; }
    bool has_inter_medium() const { return has_inter_medium_; }
    bool has_inter_semibold() const { return has_inter_semibold_; }
    bool has_icon_font() const { return has_icon_font_; }

    // Get fonts by role
    QFont font_base() const;
    QFont font_small() const;
    QFont font_medium() const;       // Inter Medium
    QFont font_semibold() const;     // Inter Semibold
    QFont font_title() const;        // Inter Semibold, larger
    QFont font_wordmark() const;     // Inter Semibold, 20px, letter-spaced
    QFont font_status() const;       // Inter, 10px
    QFont font_icon(int pixel_size = 16) const;
    QFont font_mono() const;

    // Icon font codepoint helpers
    static QString icon_codepoint(uint32_t code);

private:
    SpectraFontManager() = default;

    bool loaded_            = false;
    bool has_inter_         = false;
    bool has_inter_medium_  = false;
    bool has_inter_semibold_ = false;
    bool has_icon_font_     = false;

    QString inter_family_;
    QString inter_medium_family_;
    QString inter_semibold_family_;
    QString icon_family_;

    void load_from_file(const QString& path, bool is_icon = false);
    void load_embedded();
};

// ─── Global Token Access ─────────────────────────────────────────────────────

inline const SpectraColors&    spectra_colors()    { static SpectraColors c;    return c; }
inline const SpectraGeometry&  spectra_geometry()  { static SpectraGeometry g;  return g; }
inline const SpectraTypography& spectra_typography() { static SpectraTypography t; return t; }

}   // namespace spectra::adapters::qt
