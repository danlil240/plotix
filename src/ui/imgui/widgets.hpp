#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <span>
    #include <spectra/color.hpp>
    #include <string>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"

struct ImFont;
struct ImVec2;

namespace spectra::ui::widgets
{

// ─── Section Animation State ─────────────────────────────────────────────────
// Tracks per-section animation progress for smooth collapse/expand.

struct SectionAnimState
{
    float anim_t              = 1.0f;   // Linear progress: 0 = collapsed, 1 = expanded
    float content_height      = 0.0f;   // Measured intrinsic height when content is laid out
    float clip_height         = 0.0f;   // Height baseline frozen when a fold animation starts
    bool  target_open         = true;
    bool  was_open            = true;    // Previous frame's open state
    bool  remeasure_on_expand = false;   // One invisible layout pass before height animation
};

// Global section animation registry (keyed by section label pointer or ID).
// Call update_section_animations() once per frame to advance all animations.
void              update_section_animations(float dt);
bool              any_section_animations_active();
SectionAnimState& get_section_anim(const char* id);

// Section header with collapsible state and smooth chevron animation.
// Returns true if the section is open (content should be drawn).
// When animated=true, content fades in/out with smooth height clipping.
bool section_header(const char* label, bool* open, ImFont* font = nullptr);

// Begin/end animated section content. Call after section_header returns true.
// begin_animated_section returns false if the section is fully collapsed
// (caller can skip drawing). end_animated_section must always be called if
// begin returned true.
bool begin_animated_section(const char* id);
void end_animated_section();

// Horizontal separator with theme-aware color
void separator();

// Read-only info row: "Label    Value" (label left-aligned, value right-aligned)
void info_row(const char* label, const char* value);

// Monospace info row for numeric data
void info_row_mono(const char* label, const char* value);

// Color picker field with inline swatch + label
bool color_field(const char* label, spectra::Color& color);

// Float slider with label
bool slider_field(const char* label, float& value, float min, float max, const char* fmt = "%.2f");

// Float drag field with label
bool drag_field(const char* label,
                float&      value,
                float       speed = 0.5f,
                float       min   = 0.0f,
                float       max   = 0.0f,
                const char* fmt   = "%.1f");

// Two-component float drag (e.g. axis limits)
bool drag_field2(const char* label,
                 float&      v0,
                 float&      v1,
                 float       speed = 0.01f,
                 const char* fmt   = "%.3f");

// Checkbox with theme styling
bool checkbox_field(const char* label, bool& value);

// Toggle switch (visual alternative to checkbox)
bool toggle_field(const char* label, bool& value);

// Combo dropdown
bool combo_field(const char* label, int& current, const char* const* items, int count);

// Text input field
bool text_field(const char* label, std::string& value);

// Button spanning full width
bool button_field(const char* label);

// Small inline icon button
bool icon_button_small(const char* icon, const char* tooltip = nullptr, bool active = false);

// Icon button with 32px hitbox, hover background, and active indicator.
// cmdId: unique string ID for ImGui (e.g. "play", "pause", "view.reset")
// icon:  Icon enum value rendered as FA6 glyph
// tooltip: hover tooltip text (nullptr to disable)
// active: whether this button shows the active/selected indicator
// Returns true if clicked.
bool icon_button(const char* cmdId,
                 ui::Icon    icon,
                 const char* tooltip = nullptr,
                 bool        active  = false);

// ─── Panel Component Language ─────────────────────────────────────────────────

// Secondary grouped card / sub-panel. Draws a rounded Surface-2 background with
// a subtle border and internal padding, auto-sizing to its content height.
// Always pair with end_card(). Returns true if the card body is visible (so the
// caller can early-out, mirroring BeginChild semantics).
bool begin_card(const char* id,
                float       rounding = tokens::CARD_RADIUS,
                float       padding  = tokens::CARD_PADDING);
void end_card();

// Interactive chip / pill. Compact rounded control used for quick-insert tokens,
// filters, and tag-like toggles. Returns true when clicked.
bool chip(const char* label, bool active = false);

// Panel title block: large primary title with an optional secondary subtitle,
// used at the top of a primary panel (inspector context header).
void panel_title(const char* title, const char* subtitle = nullptr, ImFont* title_font = nullptr);

struct EmptyStateAction
{
    const char* label{nullptr};
    const char* id{nullptr};
    bool        primary{false};
};

struct PanelHeaderAction
{
    const char* id{nullptr};
    ui::Icon    icon{ui::Icon::Info};
    const char* tooltip{nullptr};
    bool        active{false};
};

// Centered empty state with an icon, title, subtitle, and optional actions.
// Returns the clicked action index, or -1 when no action was activated.
int empty_state(ui::Icon                          icon,
                const char*                       title,
                const char*                       subtitle = nullptr,
                std::span<const EmptyStateAction> actions  = {});

// Compact panel header with optional right-aligned icon actions.
// Returns the clicked action index, or -1 when no action was activated.
int panel_header(const char*                        title,
                 const char*                        subtitle      = nullptr,
                 std::span<const PanelHeaderAction> right_actions = {});

// Square toolbar control used by ROS and plot tooling.
bool toolbar_button(ui::Icon icon, const char* tooltip = nullptr, bool active = false);

// Elevated metric card for dense stat grids.
void stat_card(const char* label,
               const char* value,
               const char* unit  = nullptr,
               const char* trend = nullptr);

// Search input with leading icon and inline clear button.
bool search_box(char* buf, size_t buf_size, const char* placeholder, bool* changed = nullptr);

// Filter chip convenience wrapper over chip().
bool filter_chip(const char* label, bool active);

// Status pill with optional dot.
void status_pill(const char* label, const Color& color = {}, bool dot = true);

struct StatusPillSpec
{
    std::string label;
    Color       color{};
    bool        dot = false;
};

float status_pill_width(const char* label, bool dot = false);
void  draw_status_pills(std::span<const StatusPillSpec> pills,
                        float                           gap = tokens::STATUS_BAR_GROUP_GAP);

// Dashed drag/drop overlay for target feedback.
void drop_zone_overlay(const ImVec2& min, const ImVec2& max, const char* label, bool valid);

// Placeholder rows for non-blocking refresh states.
void skeleton_rows(int count, float row_height = 24.0f);

// Indented group (pushes indent + draws subtle left border)
void begin_group(const char* id);
void end_group();

// Color swatch (small inline preview, no picker)
void color_swatch(const spectra::Color& color, float size = 14.0f);

// Spacing helpers
void small_spacing();
void section_spacing();

// ─── New Widgets (Week 6) ────────────────────────────────────────────────────

// Sparkline: inline mini line chart for data preview
void sparkline(const char*            id,
               std::span<const float> values,
               float                  width  = -1.0f,
               float                  height = 32.0f,
               const spectra::Color&  color  = {});

// Progress bar with label
void progress_bar(const char* label, float fraction, const char* overlay = nullptr);

// Badge / tag (small colored pill with text)
void badge(const char* text, const spectra::Color& bg = {}, const spectra::Color& fg = {});

// Labeled separator (centered text in a horizontal line)
void separator_label(const char* label, ImFont* font = nullptr);

// Integer drag field
bool int_drag_field(const char* label,
                    int&        value,
                    int         speed = 1,
                    int         min   = 0,
                    int         max   = 0,
                    const char* fmt   = "%d");

// Stat row: label + value + optional unit, with monospace value
void stat_row(const char* label, const char* value, const char* unit = nullptr);

// Stat row with color indicator dot
void stat_row_colored(const char*           label,
                      const char*           value,
                      const spectra::Color& dot_color,
                      const char*           unit = nullptr);

// Draw a keyboard focus ring around the last item if it is keyboard-focused.
// Call immediately after the widget you want to decorate.
void draw_focus_ring_if_needed();

// ─── Hover Animation System ─────────────────────────────────────────────────
// Tracks per-widget hover state for smooth 80ms transitions (see theme.hpp
// smooth_hover_state).  Call widget_hover_t() BEFORE drawing to get the
// current interpolation value, then update_widget_hover() AFTER the widget
// to record the new hover state for next frame.
float widget_hover_t(unsigned int id);
void  update_widget_hover(unsigned int id, bool hovered);

}   // namespace spectra::ui::widgets

#endif   // SPECTRA_USE_IMGUI
