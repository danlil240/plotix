#pragma once

// SpectraDesignTokens — authoritative design token source for all Spectra Qt widgets.
//
// Provides colors, geometry, typography roles, and font management for the
// custom Spectra component system.  All custom widgets reference these tokens
// instead of hardcoding values.

#include <QColor>
#include <QFont>
#include <QString>

namespace spectra::ui
{
struct ThemeColors;
}

namespace spectra::adapters::qt
{

// ─── Color Tokens ───────────────────────────────────────────────────────────

struct SpectraColors
{
    // Exact default-night values from ui/theme/theme.cpp. The Qt shell is a
    // native host for the existing Spectra design; it is not a second theme.
    QColor window_base{0x0A, 0x0F, 0x18};
    QColor bg_canvas{0x0C, 0x12, 0x1C};
    QColor header_surface{0x0A, 0x0F, 0x18};
    QColor workspace_surface{0x10, 0x17, 0x25};
    QColor panel_surface{0x11, 0x18, 0x27};
    QColor bg_tertiary{0x18, 0x20, 0x2E};
    QColor input_surface{0x1E, 0x28, 0x38};
    QColor elevated_surface{0x22, 0x2D, 0x3F};

    // Borders
    QColor border_subtle{0x2E, 0x3D, 0x57};
    QColor border_default{0x38, 0x47, 0x61};
    QColor border_strong{0x59, 0x73, 0x94};

    // Text
    QColor text_primary{0xED, 0xF0, 0xF7};
    QColor text_secondary{0xC7, 0xD6, 0xEB};
    QColor text_muted{0x8C, 0x9E, 0xBD};

    // Accents
    QColor cyan_accent{0x6B, 0xC7, 0xF2};
    QColor cyan_accent_dim{0x2B, 0x50, 0x62};
    QColor purple_ambient{0x94, 0x73, 0xF5};
    QColor purple_dim{0x3B, 0x2E, 0x62};

    // Semantic
    QColor success_green{0x40, 0xBA, 0x4F};
    QColor warning_amber{0xD1, 0x99, 0x21};
    QColor error_red{0xF8, 0x51, 0x49};

    // Glow
    QColor cyan_glow{0x8C, 0xD1, 0xFF, 0x73};
    QColor purple_glow{0x94, 0x73, 0xF5, 0x20};

    // Inspector section header band
    QColor section_header_bg{0xB3, 0xC7, 0xD6, 0x0E};
};

// ─── Geometry Tokens ─────────────────────────────────────────────────────────

struct SpectraGeometry
{
    // Window regions
    // Match the legacy Spectra shell. Window-manager decoration is outside
    // this client geometry; Qt must not insert a second in-app title bar.
    int title_bar_height       = 0;
    int header_height          = 48;
    int nav_rail_width         = 72;
    int nav_rail_width_compact = 48;
    int tab_bar_height         = 26;
    int status_bar_height      = 34;
    int inspector_width        = 340;
    int inspector_width_min    = 300;
    int inspector_width_max    = 380;
    int chevron_width          = 16;

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
    QString family_inter        = "Inter";
    QString family_inter_medium = "Inter Medium";
    QString family_inter_semi   = "Inter Semibold";
    QString family_mono         = "Inter";
    QString family_icon         = "Font Awesome 6 Free";

    // Pixel sizes copied from ImGuiIntegration::load_fonts(). Qt's QFont
    // constructor takes points, so every role below is applied with
    // setPixelSize() in the implementation.
    int font_xs      = 12;
    int font_sm      = 12;
    int font_base    = 16;
    int font_md      = 14;
    int font_lg      = 15;
    int font_xl      = 20;
    int font_2xl     = 16;
    int font_menubar = 13;
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
    QFont font_medium() const;     // Inter Medium
    QFont font_semibold() const;   // Inter Semibold
    QFont font_title() const;      // Inter Semibold, larger
    QFont font_wordmark() const;   // Legacy 17.6px wordmark, letter-spaced
    QFont font_menubar() const;    // Legacy 14px command-bar menus
    QFont font_status() const;     // Legacy 11.5px rail/status labels
    QFont font_icon(int pixel_size = 16) const;
    QFont font_mono() const;

    // Icon font codepoint helpers
    static QString icon_codepoint(uint32_t code);

   private:
    SpectraFontManager() = default;

    bool loaded_             = false;
    bool has_inter_          = false;
    bool has_inter_medium_   = false;
    bool has_inter_semibold_ = false;
    bool has_icon_font_      = false;

    QString inter_family_;
    QString inter_medium_family_;
    QString inter_semibold_family_;
    QString icon_family_;

    void load_from_file(const QString& path, bool is_icon = false);
    void load_embedded();
};

// ─── Global Token Access ─────────────────────────────────────────────────────

// The Qt shell mirrors the process ThemeManager rather than owning a second
// palette. The fallback values above remain useful before a runtime exists.
const SpectraColors&          spectra_colors();
SpectraColors                 spectra_colors_from_theme(const ui::ThemeColors& colors);
void                          set_spectra_colors(const ui::ThemeColors& colors);
inline const SpectraGeometry& spectra_geometry()
{
    static SpectraGeometry g;
    return g;
}
inline const SpectraTypography& spectra_typography()
{
    static SpectraTypography t;
    return t;
}

}   // namespace spectra::adapters::qt
