#include "theme.hpp"

#include "design_tokens.hpp"
#include "glass_tokens.hpp"

#include <cmath>

#include <algorithm>
#include <format>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>
#include <sstream>
#include <vector>

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
#endif

namespace spectra::ui
{

void ThemeGlassSettings::clamp()
{
    master_intensity = std::clamp(master_intensity, 0.0f, 1.0f);
    panel_alpha      = std::clamp(panel_alpha, 0.0f, 1.0f);
    plot_alpha       = std::clamp(plot_alpha, 0.0f, 1.0f);
    toolbar_alpha    = std::clamp(toolbar_alpha, 0.0f, 1.0f);
    glow_strength    = std::clamp(glow_strength, 0.0f, 1.0f);

    // Preserve readability: panel/toolbar surfaces must stay sufficiently opaque
    // so text and borders remain visible at high transparency settings.
    panel_alpha   = std::max(panel_alpha, 0.35f);
    toolbar_alpha = std::max(toolbar_alpha, 0.30f);
    plot_alpha    = std::max(plot_alpha, 0.25f);
}

ThemeGlassSettings ThemeGlassSettings::night_defaults()
{
    ThemeGlassSettings s;
    s.master_intensity = 0.62f;
    s.panel_alpha      = 0.38f;
    s.plot_alpha       = 0.30f;
    s.toolbar_alpha    = 0.34f;
    s.glow_strength    = 0.68f;
    s.blur_enabled     = false;
    return s;
}

ThemeGlassSettings ThemeGlassSettings::dark_defaults()
{
    ThemeGlassSettings s;
    s.master_intensity = 0.42f;
    s.panel_alpha      = 0.66f;
    s.plot_alpha       = 0.82f;
    s.toolbar_alpha    = 0.60f;
    s.glow_strength    = 0.34f;
    s.blur_enabled     = false;
    return s;
}

ThemeGlassSettings ThemeGlassSettings::light_defaults()
{
    ThemeGlassSettings s;
    s.master_intensity = 0.56f;
    s.panel_alpha      = 0.62f;
    s.plot_alpha       = 0.88f;
    s.toolbar_alpha    = 0.58f;
    s.glow_strength    = 0.38f;
    s.blur_enabled     = false;
    return s;
}

// Redirectable singleton pointer.  When non-null, instance() returns this
// instead of the process-wide static fallback.  App sets/clears this pointer
// in init_runtime() / shutdown_runtime().
static ThemeManager* s_current_instance = nullptr;

void ThemeManager::set_current(ThemeManager* tm)
{
    s_current_instance = tm;
}

void ThemeManager::ensure_initialized()
{
    if (themes_.empty())
    {
        initialize_default_themes();
        initialize_data_palettes();
        glass_settings_ = ThemeGlassSettings::night_defaults();
        set_theme("night");
    }
}

ThemeManager& ThemeManager::instance()
{
    assert(s_current_instance
           && "ThemeManager::instance() called before set_current(). "
              "Every entry point (App, agent, embed, Qt) must create a ThemeManager "
              "and call ThemeManager::set_current() before use.");

    // Lazily initialize the injected instance on first access.
    if (s_current_instance->themes_.empty())
    {
        s_current_instance->initialize_default_themes();
        s_current_instance->initialize_data_palettes();
        s_current_instance->glass_settings_ = ThemeGlassSettings::night_defaults();
        s_current_instance->set_theme("night");
    }
    return *s_current_instance;
}

void ThemeManager::register_theme(const std::string& name, Theme theme)
{
    theme.name    = name;
    themes_[name] = std::move(theme);
    if (!current_theme_)
    {
        set_theme(name);
    }
}

void ThemeManager::set_theme(const std::string& name)
{
    auto it = themes_.find(name);
    if (it != themes_.end())
    {
        SPECTRA_LOG_TRACE("theme", "Theme changed to '{}'", name);
        current_theme_name_ = name;
        current_theme_      = &it->second;
        if (current_theme_)
            current_theme_->use_blur = glass_settings_.blur_enabled;
        ++theme_version_;
#ifdef SPECTRA_USE_IMGUI
        apply_to_imgui();
#endif
        if (event_system_)
            event_system_->theme_changed().emit(spectra::ThemeChangedEvent{name});
    }
}

ThemeGlassSettings ThemeManager::glass_defaults_for(const std::string& theme_name) const
{
    auto it = themes_.find(theme_name);
    if (it != themes_.end())
        return it->second.glass_defaults;
    return ThemeGlassSettings::night_defaults();
}

void ThemeManager::set_glass_settings(const ThemeGlassSettings& settings, bool apply)
{
    glass_settings_ = settings;
    glass_settings_.clamp();
    if (current_theme_)
        current_theme_->use_blur = glass_settings_.blur_enabled;
    if (apply)
    {
        ++theme_version_;
#ifdef SPECTRA_USE_IMGUI
        apply_to_imgui();
#endif
        if (event_system_)
            event_system_->theme_changed().emit(spectra::ThemeChangedEvent{current_theme_name_});
    }
}

void ThemeManager::reset_glass_defaults()
{
    set_glass_settings(glass_defaults_for(current_theme_name_), true);
}

float ThemeManager::glass_surface_alpha(GlassSurface surface) const
{
    return glass_surface_alpha_value(glass_settings_, surface);
}

Color ThemeManager::glass_resolved_surface(Color base, GlassSurface surface) const
{
    return glass_resolved_surface_color(base, glass_settings_, surface);
}

Color ThemeManager::glass_resolved_plot_background() const
{
    const auto& c = colors();
    return ::spectra::ui::glass_resolved_plot_background(c.bg_canvas,
                                                         c.bg_primary,
                                                         glass_settings_);
}

float ThemeManager::effective_glow_intensity() const
{
    if (!current_theme_)
        return 0.0f;
    return glass_effective_glow_strength(glass_settings_, current_theme_->colors.glow_intensity);
}

const Theme& ThemeManager::current() const
{
    static Theme fallback;
    return current_theme_ ? *current_theme_ : fallback;
}

const ThemeColors& ThemeManager::colors() const
{
    if (display_colors_valid_)
        return display_colors_;
    return current().colors;
}

const std::string& ThemeManager::current_theme_name() const
{
    return current_theme_name_;
}

void ThemeManager::set_data_palette(const std::string& palette_name)
{
    auto it = data_palettes_.find(palette_name);
    if (it != data_palettes_.end())
    {
        current_data_palette_name_ = palette_name;
        if (current_theme_)
        {
            current_theme_->data_palette = it->second;
        }
        display_palette_valid_ = false;
        palette_transitioning_ = false;
    }
}

void ThemeManager::register_data_palette(const std::string& name, DataPalette palette)
{
    palette.name         = name;
    data_palettes_[name] = std::move(palette);
    palette_names_dirty_ = true;
}

const DataPalette& ThemeManager::current_data_palette() const
{
    static DataPalette fallback;
    if (display_palette_valid_)
        return display_palette_;
    if (current_theme_)
    {
        return current_theme_->data_palette;
    }
    return fallback;
}

const DataPalette& ThemeManager::get_data_palette(const std::string& name) const
{
    static DataPalette fallback;
    auto               it = data_palettes_.find(name);
    if (it != data_palettes_.end())
        return it->second;
    return fallback;
}

const std::vector<std::string>& ThemeManager::available_data_palettes() const
{
    if (palette_names_dirty_)
    {
        palette_names_cache_.clear();
        for (const auto& pair : data_palettes_)
        {
            palette_names_cache_.push_back(pair.first);
        }
        std::sort(palette_names_cache_.begin(), palette_names_cache_.end());
        palette_names_dirty_ = false;
    }
    return palette_names_cache_;
}

void ThemeManager::transition_palette(const std::string& palette_name, float duration_sec)
{
    auto it = data_palettes_.find(palette_name);
    if (it == data_palettes_.end())
        return;
    if (duration_sec <= 0.0f)
    {
        set_data_palette(palette_name);
        return;
    }

    const auto& current_pal         = current_data_palette();
    palette_start_colors_           = current_pal.colors;
    palette_target_colors_          = it->second.colors;
    palette_transition_target_name_ = palette_name;
    palette_transition_time_        = 0.0f;
    palette_transition_duration_    = duration_sec;
    palette_transitioning_          = true;

    // Initialize display palette from target metadata
    display_palette_        = it->second;
    display_palette_.colors = palette_start_colors_;
    display_palette_valid_  = true;
}

bool ThemeManager::is_palette_transitioning() const
{
    return palette_transitioning_;
}

void ThemeManager::apply_to_imgui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!current_theme_ || ImGui::GetCurrentContext() == nullptr)
        return;

    auto&       style  = ImGui::GetStyle();
    const auto& colors = current_theme_->colors;

    // ── Modern 2026 styling ───────────────────────────────────────────────────────
    style.AntiAliasedLines       = true;
    style.AntiAliasedFill        = true;
    style.AntiAliasedLinesUseTex = true;

    // Window styling — r12 for panels/containers, generous padding
    style.WindowPadding    = ImVec2(tokens::SPACE_5, tokens::SPACE_4);
    style.WindowRounding   = tokens::RADIUS_LG;
    style.WindowBorderSize = 0.0f;   // Hairline borders drawn manually
    style.WindowMinSize    = ImVec2(32, 32);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    // Frame styling — r8 for inputs, comfortable padding for premium feel
    style.FramePadding    = ImVec2(tokens::SPACE_3, tokens::SPACE_2 + 2.0f);
    style.FrameRounding   = tokens::RADIUS_MD;
    style.FrameBorderSize = 0.0f;

    // Item spacing — generous breathing room (non-ImGui-default)
    style.ItemSpacing      = ImVec2(tokens::SPACE_3, tokens::SPACE_3);
    style.ItemInnerSpacing = ImVec2(tokens::SPACE_2 + 2.0f, tokens::SPACE_2);
    style.CellPadding      = ImVec2(tokens::ROW_PADDING_H, tokens::ROW_PADDING_V + 2.0f);

    // Indent and separator
    style.IndentSpacing           = tokens::SPACE_6;
    style.SeparatorTextBorderSize = 0.5f;
    style.SeparatorTextAlign      = ImVec2(0.5f, 0.5f);
    style.SeparatorTextPadding    = ImVec2(tokens::SPACE_6, tokens::SPACE_2);

    // Scrollbar — thin, pill-shaped, modern (appears on hover)
    style.ScrollbarSize     = 6.0f;
    style.ScrollbarRounding = tokens::RADIUS_PILL;

    // Grab — rounded slider handles
    style.GrabMinSize  = tokens::SPACE_4;
    style.GrabRounding = tokens::RADIUS_PILL;

    // Tab — r8 for dock tabs; taller hit target and visible separators
    style.TabRounding                      = tokens::TAB_BAR_RADIUS;
    style.TabBorderSize                    = 0.0f;
    style.TabBarBorderSize                 = 1.0f;
    style.TabBarOverlineSize               = tokens::TAB_BAR_UNDERLINE_HEIGHT;
    style.TabCloseButtonMinWidthSelected   = tokens::TAB_BAR_CLOSE_BTN_SIZE + 4.0f;
    style.TabCloseButtonMinWidthUnselected = tokens::TAB_BAR_CLOSE_BTN_SIZE;
    style.DockingSeparatorSize             = 2.0f;

    // Popup / tooltip — r12 for premium floating surfaces
    style.PopupRounding   = tokens::RADIUS_LG;
    style.PopupBorderSize = 1.0f;

    // Child window
    style.ChildRounding   = tokens::RADIUS_MD;
    style.ChildBorderSize = 0.0f;

    // Button
    style.ButtonTextAlign     = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    // Tooltip — modern, fast-appearing
    style.HoverStationaryDelay = 0.25f;
    style.HoverDelayShort      = 0.12f;
    style.HoverDelayNormal     = 0.35f;

    // Display safe area padding
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);

    // Apply colors
    ImVec4* imgui_colors = style.Colors;

    const bool do_lin = current_theme_->linearize_colors;
    auto       lin    = [do_lin](const Color& c, float alpha_override = -1.0f) -> ImVec4
    {
        float a = alpha_override >= 0.0f ? alpha_override : c.a;
        if (do_lin)
        {
            Color l = c.to_linear();
            return {l.r, l.g, l.b, a};
        }
        return {c.r, c.g, c.b, a};
    };

    Color menu_bar_bg   = colors.bg_primary.lerp(colors.bg_secondary, 0.28f);
    Color frame_hover   = colors.input_bg.lerp(colors.accent, 0.10f);
    Color frame_active  = colors.input_bg.lerp(colors.accent, 0.20f);
    Color button_hover  = colors.bg_tertiary.lerp(colors.accent, 0.16f);
    Color button_active = colors.bg_tertiary.lerp(colors.accent, 0.28f);
    Color header_bg     = colors.bg_secondary.lerp(colors.bg_tertiary, 0.72f);
    Color header_hover  = colors.bg_tertiary.lerp(colors.accent, 0.14f);
    Color header_active = colors.bg_tertiary.lerp(colors.accent, 0.24f);
    Color tab_idle      = colors.bg_secondary.lerp(colors.bg_tertiary, 0.50f);
    Color tab_hover     = colors.bg_tertiary.lerp(colors.accent, 0.22f);
    Color tab_selected  = colors.bg_tertiary.lerp(colors.accent, 0.36f);

    const Color panel_resolved = glass_resolved_surface(colors.bg_secondary, GlassSurface::Panel);
    current_theme_->opacity_panel = std::max(panel_resolved.a, 0.5f);

    // Window and background
    imgui_colors[ImGuiCol_WindowBg]     = lin(panel_resolved);
    imgui_colors[ImGuiCol_ChildBg]      = ImVec4(0, 0, 0, 0);   // Transparent — inherits parent
    imgui_colors[ImGuiCol_PopupBg]      = lin(colors.tooltip_bg);
    imgui_colors[ImGuiCol_Border]       = lin(colors.border_default);
    imgui_colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0.15f);   // Subtle depth shadow

    // Text
    imgui_colors[ImGuiCol_Text]         = lin(colors.text_primary, 1.0f);
    imgui_colors[ImGuiCol_TextDisabled] = lin(colors.text_tertiary, 1.0f);

    // Frame backgrounds — use input_bg token with visible hover lift
    imgui_colors[ImGuiCol_FrameBg]        = lin(colors.input_bg, 1.0f);
    imgui_colors[ImGuiCol_FrameBgHovered] = lin(frame_hover, 1.0f);
    imgui_colors[ImGuiCol_FrameBgActive]  = lin(frame_active, 1.0f);

    // Titles
    imgui_colors[ImGuiCol_TitleBg] = lin(colors.bg_secondary, 1.0f);
    imgui_colors[ImGuiCol_TitleBgActive] =
        lin(colors.bg_secondary.lerp(colors.bg_elevated, 0.55f), 1.0f);
    imgui_colors[ImGuiCol_TitleBgCollapsed] = lin(colors.bg_tertiary, 1.0f);

    // Menu
    imgui_colors[ImGuiCol_MenuBarBg] = lin(menu_bar_bg, 1.0f);

    // Scrollbar — themed track + thumb
    imgui_colors[ImGuiCol_ScrollbarBg]          = lin(colors.scrollbar_track);
    imgui_colors[ImGuiCol_ScrollbarGrab]        = lin(colors.scrollbar_thumb);
    imgui_colors[ImGuiCol_ScrollbarGrabHovered] = lin(colors.text_secondary, 1.0f);
    imgui_colors[ImGuiCol_ScrollbarGrabActive]  = lin(colors.accent, 1.0f);

    // Check mark
    imgui_colors[ImGuiCol_CheckMark] = lin(colors.accent, 1.0f);

    // Buttons — clear state progression: rest → hover → press
    imgui_colors[ImGuiCol_Button]        = lin(colors.bg_tertiary, 0.92f);
    imgui_colors[ImGuiCol_ButtonHovered] = lin(button_hover, 0.98f);
    imgui_colors[ImGuiCol_ButtonActive]  = lin(button_active, 1.0f);

    // Header — visible hover/active for interaction affordance
    imgui_colors[ImGuiCol_Header]        = lin(header_bg, 0.92f);
    imgui_colors[ImGuiCol_HeaderHovered] = lin(header_hover, 0.96f);
    imgui_colors[ImGuiCol_HeaderActive]  = lin(header_active, 1.0f);

    // Separator — hairline dividers
    imgui_colors[ImGuiCol_Separator]        = lin(colors.border_subtle);
    imgui_colors[ImGuiCol_SeparatorHovered] = lin(colors.border_default);
    imgui_colors[ImGuiCol_SeparatorActive]  = lin(colors.accent, 1.0f);

    // Resize grip
    imgui_colors[ImGuiCol_ResizeGrip]        = lin(colors.border_default, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripHovered] = lin(colors.accent, 1.0f);
    imgui_colors[ImGuiCol_ResizeGripActive]  = lin(colors.accent, 1.0f);

    // Tabs — clear active tab with strong contrast, visible hover lift
    imgui_colors[ImGuiCol_Tab]               = lin(tab_idle, 0.90f);
    imgui_colors[ImGuiCol_TabHovered]        = lin(tab_hover, 0.95f);
    imgui_colors[ImGuiCol_TabSelected]       = lin(tab_selected, 1.0f);
    imgui_colors[ImGuiCol_TabDimmed]         = lin(colors.bg_secondary, 0.55f);
    imgui_colors[ImGuiCol_TabDimmedSelected] = lin(tab_selected, 0.82f);

    // Plot lines (for ImGui plot widgets)
    imgui_colors[ImGuiCol_PlotLines]            = lin(colors.accent, 1.0f);
    imgui_colors[ImGuiCol_PlotLinesHovered]     = lin(colors.accent_hover, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogram]        = lin(colors.accent, 1.0f);
    imgui_colors[ImGuiCol_PlotHistogramHovered] = lin(colors.accent_hover, 1.0f);

    // Table headers
    imgui_colors[ImGuiCol_TableHeaderBg]     = lin(colors.bg_tertiary, 1.0f);
    imgui_colors[ImGuiCol_TableBorderStrong] = lin(colors.border_default, 1.0f);
    imgui_colors[ImGuiCol_TableBorderLight]  = lin(colors.border_subtle, 1.0f);
    imgui_colors[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    imgui_colors[ImGuiCol_TableRowBgAlt]     = lin(colors.bg_tertiary, 0.5f);

    // Drag and drop — muted accent, not shouty
    imgui_colors[ImGuiCol_DragDropTarget] = lin(colors.accent, 0.7f);

    // Navigation — strong focus indicators for keyboard navigation
    imgui_colors[ImGuiCol_NavHighlight]          = lin(colors.focus_ring, 0.80f);
    imgui_colors[ImGuiCol_NavWindowingHighlight] = lin(colors.accent, 0.75f);
    imgui_colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.40f);

    // Modal
    imgui_colors[ImGuiCol_ModalWindowDimBg] = lin(colors.bg_overlay, 0.5f);
#endif // SPECTRA_USE_IMGUI
}

void ThemeManager::apply_to_renderer(Renderer& renderer)
{
    if (!current_theme_)
        return;

    // Update renderer with theme-aware plot colors
    // This would require the renderer to have methods to set these colors
    // For now, this is a placeholder for the concept

    // The renderer would need to be updated to use:
    // - colors.bg_primary for canvas background
    // - colors.grid_line for grid lines
    // - colors.axis_line for axis lines
    // - colors.tick_label for tick labels
    // - colors.crosshair for crosshair
    // - colors.selection_fill/border for selections

    (void)renderer;   // Suppress unused parameter warning
}

void ThemeManager::transition_to(const std::string& name, float duration_sec)
{
    auto it = themes_.find(name);
    if (it != themes_.end() && (current_theme_ != nullptr))
    {
        transitioning_           = true;
        transition_time_         = 0.0f;
        transition_duration_     = duration_sec;
        transition_start_colors_ = colors();   // Use display colors (may already be mid-transition)
        transition_target_colors_ = it->second.colors;
        transition_target_name_   = name;
        display_colors_valid_     = true;
        display_colors_           = transition_start_colors_;
    }
}

void ThemeManager::update(float dt)
{
    // Update theme transition
    if (transitioning_)
    {
        transition_time_ += dt;
        float t = std::min(transition_time_ / transition_duration_, 1.0f);

        // Use ease-in-out curve
        t = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        display_colors_ =
            interpolate_colors(transition_start_colors_, transition_target_colors_, t);
        display_colors_valid_ = true;
        ++theme_version_;
#ifdef SPECTRA_USE_IMGUI
        apply_to_imgui();
#endif

        if (transition_time_ >= transition_duration_)
        {
            transitioning_        = false;
            display_colors_valid_ = false;
            // Switch to target theme (stored data is pristine)
            set_theme(transition_target_name_);
        }
    }

    // Update palette transition
    if (palette_transitioning_)
    {
        palette_transition_time_ += dt;
        float t = std::min(palette_transition_time_ / palette_transition_duration_, 1.0f);
        t       = t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        // Interpolate palette colors
        size_t count = std::min(palette_start_colors_.size(), palette_target_colors_.size());
        display_palette_.colors.resize(
            std::max(palette_start_colors_.size(), palette_target_colors_.size()));
        for (size_t i = 0; i < count; ++i)
        {
            display_palette_.colors[i] =
                palette_start_colors_[i].lerp(palette_target_colors_[i], t);
        }
        // Colors beyond the shorter palette fade in/out
        for (size_t i = count; i < palette_target_colors_.size(); ++i)
        {
            display_palette_.colors[i] = palette_target_colors_[i].with_alpha(t);
        }
        display_palette_valid_ = true;

        if (palette_transition_time_ >= palette_transition_duration_)
        {
            palette_transitioning_ = false;
            display_palette_valid_ = false;
            set_data_palette(palette_transition_target_name_);
        }
    }
}

bool ThemeManager::is_transitioning() const
{
    return transitioning_;
}

Color ThemeManager::get_color(const std::string& color_name) const
{
    // Simple color name lookup for convenience
    if (!current_theme_)
        return {};

    const auto& c = current_theme_->colors;
    if (color_name == "accent")
        return c.accent;
    if (color_name == "accent_hover")
        return c.accent_hover;
    if (color_name == "text_primary")
        return c.text_primary;
    if (color_name == "text_secondary")
        return c.text_secondary;
    if (color_name == "bg_canvas")
        return c.bg_canvas;
    if (color_name == "bg_primary")
        return c.bg_primary;
    if (color_name == "bg_secondary")
        return c.bg_secondary;
    if (color_name == "bg_tertiary")
        return c.bg_tertiary;
    if (color_name == "border_default")
        return c.border_default;
    if (color_name == "border_subtle")
        return c.border_subtle;
    if (color_name == "success")
        return c.success;
    if (color_name == "warning")
        return c.warning;
    if (color_name == "error")
        return c.error;
    if (color_name == "info")
        return c.info;
    if (color_name == "grid_major")
        return c.grid_major;
    if (color_name == "grid_minor")
        return c.grid_minor;
    if (color_name == "grid_line")
        return c.grid_line;
    if (color_name == "focus_ring")
        return c.focus_ring;
    if (color_name == "accent_glow")
        return c.accent_glow;

    return {};   // Return transparent if not found
}

Color ThemeManager::lerp_color(const std::string& color_name, const Color& target, float t) const
{
    return get_color(color_name).lerp(target, t);
}

// ─── JSON helpers (minimal, no external deps) ────────────────────────────────

namespace
{

std::string color_to_json(const Color& c)
{
    return std::format("[{:.6f}, {:.6f}, {:.6f}, {:.6f}]", c.r, c.g, c.b, c.a);
}

std::string escape_json_string(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char ch : s)
    {
        if (ch == '"')
            out += "\\\"";
        else if (ch == '\\')
            out += "\\\\";
        else if (ch == '\n')
            out += "\\n";
        else
            out += ch;
    }
    out += '"';
    return out;
}

std::string theme_colors_to_json(const ThemeColors& c, int indent = 4)
{
    std::string        pad(indent, ' ');
    std::ostringstream os;
    os << "{\n";
#define TC_FIELD(name) \
    os << pad << "  " << escape_json_string(#name) << ": " << color_to_json(c.name)
    TC_FIELD(bg_canvas) << ",\n";
    TC_FIELD(bg_primary) << ",\n";
    TC_FIELD(bg_secondary) << ",\n";
    TC_FIELD(bg_tertiary) << ",\n";
    TC_FIELD(bg_elevated) << ",\n";
    TC_FIELD(bg_overlay) << ",\n";
    TC_FIELD(text_primary) << ",\n";
    TC_FIELD(text_secondary) << ",\n";
    TC_FIELD(text_tertiary) << ",\n";
    TC_FIELD(text_inverse) << ",\n";
    TC_FIELD(border_default) << ",\n";
    TC_FIELD(border_subtle) << ",\n";
    TC_FIELD(border_strong) << ",\n";
    TC_FIELD(accent) << ",\n";
    TC_FIELD(accent_hover) << ",\n";
    TC_FIELD(accent_muted) << ",\n";
    TC_FIELD(accent_subtle) << ",\n";
    TC_FIELD(success) << ",\n";
    TC_FIELD(warning) << ",\n";
    TC_FIELD(error) << ",\n";
    TC_FIELD(info) << ",\n";
    TC_FIELD(grid_major) << ",\n";
    TC_FIELD(grid_minor) << ",\n";
    TC_FIELD(grid_line) << ",\n";
    TC_FIELD(axis_line) << ",\n";
    TC_FIELD(tick_label) << ",\n";
    TC_FIELD(crosshair) << ",\n";
    TC_FIELD(selection_fill) << ",\n";
    TC_FIELD(selection_border) << ",\n";
    TC_FIELD(tooltip_bg) << ",\n";
    TC_FIELD(tooltip_border) << ",\n";
    TC_FIELD(accent_glow) << ",\n";
    TC_FIELD(focus_ring) << ",\n";
    TC_FIELD(scrollbar_thumb) << ",\n";
    TC_FIELD(scrollbar_track) << ",\n";
    TC_FIELD(section_header_bg) << ",\n";
    TC_FIELD(input_bg) << ",\n";
    TC_FIELD(hover_highlight) << ",\n";
    TC_FIELD(annotation_bg) << ",\n";
    TC_FIELD(roi_fill) << ",\n";
    TC_FIELD(roi_border) << "\n";
#undef TC_FIELD
    os << pad << "  " << escape_json_string("glow_intensity") << ": " << c.glow_intensity << "\n";
    os << pad << "}";
    return os.str();
}

bool parse_float_array(const std::string& s, size_t& pos, float* out, int count)
{
    while (pos < s.size() && s[pos] != '[')
        ++pos;
    if (pos >= s.size())
        return false;
    ++pos;
    for (int i = 0; i < count; ++i)
    {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == ','))
            ++pos;
        char* end = nullptr;
        out[i]    = std::strtof(s.c_str() + pos, &end);
        pos       = static_cast<size_t>(end - s.c_str());
    }
    while (pos < s.size() && s[pos] != ']')
        ++pos;
    if (pos < s.size())
        ++pos;
    return true;
}

Color parse_color_array(const std::string& s, size_t& pos)
{
    float v[4] = {0, 0, 0, 1};
    parse_float_array(s, pos, v, 4);
    return {v[0], v[1], v[2], v[3]};
}

std::string extract_string_value(const std::string& s, size_t pos)
{
    size_t q1 = s.find('"', pos);
    if (q1 == std::string::npos)
        return "";
    size_t q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return "";
    return s.substr(q1 + 1, q2 - q1 - 1);
}

bool parse_theme_colors_from_json(const std::string& json, ThemeColors& out)
{
#define PARSE_TC(name)                             \
    {                                              \
        size_t p = json.find("\"" #name "\"");     \
        if (p != std::string::npos)                \
        {                                          \
            p += strlen("\"" #name "\"");          \
            out.name = parse_color_array(json, p); \
        }                                          \
    }
    PARSE_TC(bg_canvas);
    PARSE_TC(bg_primary);
    PARSE_TC(bg_secondary);
    PARSE_TC(bg_tertiary);
    PARSE_TC(bg_elevated);
    PARSE_TC(bg_overlay);
    PARSE_TC(text_primary);
    PARSE_TC(text_secondary);
    PARSE_TC(text_tertiary);
    PARSE_TC(text_inverse);
    PARSE_TC(border_default);
    PARSE_TC(border_subtle);
    PARSE_TC(border_strong);
    PARSE_TC(accent);
    PARSE_TC(accent_hover);
    PARSE_TC(accent_muted);
    PARSE_TC(accent_subtle);
    PARSE_TC(success);
    PARSE_TC(warning);
    PARSE_TC(error);
    PARSE_TC(info);
    PARSE_TC(grid_major);
    PARSE_TC(grid_minor);
    PARSE_TC(grid_line);
    PARSE_TC(axis_line);
    PARSE_TC(tick_label);
    PARSE_TC(crosshair);
    PARSE_TC(selection_fill);
    PARSE_TC(selection_border);
    PARSE_TC(tooltip_bg);
    PARSE_TC(tooltip_border);
    PARSE_TC(accent_glow);
    PARSE_TC(focus_ring);
    PARSE_TC(scrollbar_thumb);
    PARSE_TC(scrollbar_track);
    PARSE_TC(section_header_bg);
    PARSE_TC(input_bg);
    PARSE_TC(hover_highlight);
    PARSE_TC(annotation_bg);
    PARSE_TC(roi_fill);
    PARSE_TC(roi_border);
#undef PARSE_TC
    // Parse glow_intensity float
    {
        size_t p = json.find("\"glow_intensity\"");
        if (p != std::string::npos)
        {
            p = json.find(':', p);
            if (p != std::string::npos)
            {
                char* end_ptr = nullptr;
                float v       = std::strtof(json.c_str() + p + 1, &end_ptr);
                if (end_ptr != json.c_str() + p + 1)
                    out.glow_intensity = v;
            }
        }
    }
    return true;
}

}   // anonymous namespace

bool ThemeManager::export_theme(const std::string& path) const
{
    if (!current_theme_)
        return false;

    std::ofstream f(path);
    if (!f.is_open())
        return false;

    const auto& t = *current_theme_;
    f << "{\n";
    f << "  \"name\": " << escape_json_string(t.name) << ",\n";
    f << "  \"version\": 1,\n";
    f << "  \"colors\": " << theme_colors_to_json(t.colors, 2) << ",\n";
    f << "  \"opacity_panel\": " << t.opacity_panel << ",\n";
    f << "  \"opacity_tooltip\": " << t.opacity_tooltip << ",\n";
    f << "  \"shadow_intensity\": " << t.shadow_intensity << ",\n";
    f << "  \"border_width\": " << t.border_width << ",\n";
    f << "  \"animation_speed\": " << t.animation_speed << ",\n";
    f << "  \"enable_animations\": " << (t.enable_animations ? "true" : "false") << ",\n";
    f << "  \"use_blur\": " << (t.use_blur ? "true" : "false") << ",\n";
    const auto& g = glass_settings_;
    f << "  \"glass\": {\n";
    f << "    \"master_intensity\": " << g.master_intensity << ",\n";
    f << "    \"panel_alpha\": " << g.panel_alpha << ",\n";
    f << "    \"plot_alpha\": " << g.plot_alpha << ",\n";
    f << "    \"toolbar_alpha\": " << g.toolbar_alpha << ",\n";
    f << "    \"glow_strength\": " << g.glow_strength << ",\n";
    f << "    \"blur_enabled\": " << (g.blur_enabled ? "true" : "false") << "\n";
    f << "  },\n";

    f << "  \"data_palette\": {\n";
    f << "    \"name\": " << escape_json_string(t.data_palette.name) << ",\n";
    f << "    \"colorblind_safe\": " << (t.data_palette.colorblind_safe ? "true" : "false")
      << ",\n";
    f << "    \"colors\": [\n";
    for (size_t i = 0; i < t.data_palette.colors.size(); ++i)
    {
        f << "      " << color_to_json(t.data_palette.colors[i]);
        if (i + 1 < t.data_palette.colors.size())
            f << ",";
        f << "\n";
    }
    f << "    ]\n";
    f << "  }\n";
    f << "}\n";

    return f.good();
}

bool ThemeManager::import_theme(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (json.empty())
        return false;

    size_t name_pos = json.find("\"name\"");
    if (name_pos == std::string::npos)
        return false;
    std::string name = extract_string_value(json, name_pos + 6);
    if (name.empty())
        return false;

    Theme theme;
    theme.name = name;

    size_t colors_pos = json.find("\"colors\"");
    if (colors_pos != std::string::npos)
    {
        size_t brace = json.find('{', colors_pos + 8);
        if (brace != std::string::npos)
        {
            int    depth = 1;
            size_t end   = brace + 1;
            while (end < json.size() && depth > 0)
            {
                if (json[end] == '{')
                    ++depth;
                else if (json[end] == '}')
                    --depth;
                ++end;
            }
            std::string colors_json = json.substr(brace, end - brace);
            parse_theme_colors_from_json(colors_json, theme.colors);
        }
    }

    auto parse_float = [&](const char* key, float& out)
    {
        std::string needle = std::string("\"") + key + "\"";
        size_t      p      = json.find(needle);
        if (p != std::string::npos)
        {
            p = json.find(':', p);
            if (p != std::string::npos)
            {
                char* end = nullptr;
                float v   = std::strtof(json.c_str() + p + 1, &end);
                if (end != json.c_str() + p + 1)
                    out = v;
            }
        }
    };
    auto parse_bool = [&](const char* key, bool& out)
    {
        std::string needle = std::string("\"") + key + "\"";
        size_t      p      = json.find(needle);
        if (p != std::string::npos)
        {
            size_t t_pos = json.find("true", p);
            size_t f_pos = json.find("false", p);
            out = t_pos != std::string::npos && (f_pos == std::string::npos || t_pos < f_pos);
        }
    };

    parse_float("opacity_panel", theme.opacity_panel);
    parse_float("opacity_tooltip", theme.opacity_tooltip);
    parse_float("shadow_intensity", theme.shadow_intensity);
    parse_float("border_width", theme.border_width);
    parse_float("animation_speed", theme.animation_speed);
    parse_bool("enable_animations", theme.enable_animations);
    parse_bool("use_blur", theme.use_blur);

    register_theme(name, std::move(theme));

    size_t glass_pos = json.find("\"glass\"");
    if (glass_pos != std::string::npos)
    {
        ThemeGlassSettings g                 = glass_settings_;
        auto               parse_glass_float = [&](const char* key, float& out)
        {
            std::string needle = std::string("\"") + key + "\"";
            size_t      p      = json.find(needle, glass_pos);
            if (p != std::string::npos)
            {
                p = json.find(':', p);
                if (p != std::string::npos)
                {
                    char* end = nullptr;
                    float v   = std::strtof(json.c_str() + p + 1, &end);
                    if (end != json.c_str() + p + 1)
                        out = v;
                }
            }
        };
        parse_glass_float("master_intensity", g.master_intensity);
        parse_glass_float("panel_alpha", g.panel_alpha);
        parse_glass_float("plot_alpha", g.plot_alpha);
        parse_glass_float("toolbar_alpha", g.toolbar_alpha);
        parse_glass_float("glow_strength", g.glow_strength);
        parse_bool("blur_enabled", g.blur_enabled);
        set_glass_settings(g, true);
    }
    return true;
}

void ThemeManager::save_current_as_default()
{
    if (default_theme_path_.empty())
    {
        const char* home = std::getenv("HOME");
        if (!home)
            home = std::getenv("USERPROFILE");
        if (home)
        {
            default_theme_path_ = std::string(home) + "/.spectra/default_theme.json";
        }
    }
    if (!default_theme_path_.empty())
    {
        auto dir = std::filesystem::path(default_theme_path_).parent_path();
        std::filesystem::create_directories(dir);
        export_theme(default_theme_path_);
    }
}

void ThemeManager::load_default()
{
    if (default_theme_path_.empty())
    {
        const char* home = std::getenv("HOME");
        if (!home)
            home = std::getenv("USERPROFILE");
        if (home)
        {
            default_theme_path_ = std::string(home) + "/.spectra/default_theme.json";
        }
    }
    if (!default_theme_path_.empty() && std::filesystem::exists(default_theme_path_))
    {
        if (import_theme(default_theme_path_))
        {
            std::ifstream lf(default_theme_path_);
            std::string   json((std::istreambuf_iterator<char>(lf)),
                             std::istreambuf_iterator<char>());
            size_t        np = json.find("\"name\"");
            if (np != std::string::npos)
            {
                std::string n = extract_string_value(json, np + 6);
                if (!n.empty())
                    set_theme(n);
            }
        }
    }
}

void ThemeManager::initialize_default_themes()
{
    // Dark Glass — graphite chrome with restrained cool highlights
    Theme dark;
    dark.name   = "dark";
    dark.colors = {
        // Surfaces — separated graphite layers instead of one flat charcoal
        .bg_canvas    = Color::from_hex(0x1B2026),   // Plot area
        .bg_primary   = Color::from_hex(0x12161A),   // App window background
        .bg_secondary = Color::from_hex(0x1E252B),   // Panels, inspector, rails
        .bg_tertiary  = Color::from_hex(0x29323A),   // Inputs, chips, cards
        .bg_elevated  = Color::from_hex(0x35414A),   // Floating: tooltips, popups
        .bg_overlay   = Color(0.0f, 0.0f, 0.0f, 0.55f),

        // Text — cool gray hierarchy with clearer secondary labels
        .text_primary   = Color::from_hex(0xF1F5F9),
        .text_secondary = Color::from_hex(0xCBD5E1),
        .text_tertiary  = Color::from_hex(0x8A97A6),
        .text_inverse   = Color::from_hex(0x0C1116),

        // Borders — glass rims that define the rail, canvas, and popups
        .border_default = Color(0.38f, 0.47f, 0.54f, 0.54f),
        .border_subtle  = Color(0.25f, 0.32f, 0.38f, 0.38f),
        .border_strong  = Color(0.60f, 0.73f, 0.82f, 0.76f),

        // Interactive — quiet steel-cyan accent for controls and focus
        .accent        = Color::from_hex(0x9FD3E6),
        .accent_hover  = Color::from_hex(0xC6E7F1),
        .accent_muted  = Color(0.62f, 0.83f, 0.90f, 0.18f),
        .accent_subtle = Color(0.62f, 0.83f, 0.90f, 0.08f),

        // Semantic
        .success = Color::from_hex(0x3FB950),
        .warning = Color::from_hex(0xD29922),
        .error   = Color::from_hex(0xFF7575),   // WCAG AA: 4.52:1 on bg_secondary (#2E2E2E)
        .info    = Color::from_hex(0x7DD3FC),

        // Plot-specific — grid recedes, data is hero
        .grid_major       = Color(0.70f, 0.78f, 0.84f, 0.12f),
        .grid_minor       = Color(0.70f, 0.78f, 0.84f, 0.045f),
        .grid_line        = Color(0.70f, 0.78f, 0.84f, 0.12f),   // Compat
        .axis_line        = Color(0.70f, 0.78f, 0.84f, 0.48f),
        .tick_label       = Color::from_hex(0xC7D2DE),
        .crosshair        = Color(0.62f, 0.83f, 0.90f, 0.70f),
        .selection_fill   = Color(0.62f, 0.83f, 0.90f, 0.12f),
        .selection_border = Color::from_hex(0x9FD3E6),
        .tooltip_bg       = Color(0.17f, 0.21f, 0.24f, 0.94f),
        .tooltip_border   = Color(0.52f, 0.64f, 0.72f, 0.42f),

        // Visual effects — subtle glass glow without becoming Night theme
        .accent_glow       = Color(0.55f, 0.84f, 0.94f, 0.34f),
        .glow_intensity    = 0.28f,
        .focus_ring        = Color::from_hex(0x9FD3E6),
        .scrollbar_thumb   = Color(0.70f, 0.78f, 0.84f, 0.24f),
        .scrollbar_track   = Color(0.0f, 0.0f, 0.0f, 0.0f),
        .section_header_bg = Color(0.70f, 0.78f, 0.84f, 0.055f),
        .input_bg          = Color::from_hex(0x273039),
        .hover_highlight   = Color(0.62f, 0.83f, 0.90f, 0.11f),
        .annotation_bg     = Color(0.17f, 0.21f, 0.24f, 0.88f),
        .roi_fill          = Color(0.62f, 0.83f, 0.90f, 0.08f),
        .roi_border        = Color(0.62f, 0.83f, 0.90f, 0.36f)};
    dark.shadow_intensity = 0.55f;
    dark.use_blur         = false;
    dark.glass_defaults   = ThemeGlassSettings::dark_defaults();
    register_theme("dark", dark);

    // Night Glass — premium dark glassmorphism (Vision.png target)
    Theme night;
    night.name             = "night";
    night.colors           = {.bg_canvas    = Color::from_hex(0x0C121C),
                              .bg_primary   = Color::from_hex(0x0A0F18),
                              .bg_secondary = Color::from_hex(0x111827),
                              .bg_tertiary  = Color::from_hex(0x1A2332),
                              .bg_elevated  = Color::from_hex(0x222D3F),
                              .bg_overlay   = Color(0.01f, 0.02f, 0.05f, 0.62f),

                              .text_primary   = glass_palette::kTextPrimary,
                              .text_secondary = Color(0.78f, 0.84f, 0.92f, 0.82f),
                              .text_tertiary  = Color(0.55f, 0.62f, 0.74f, 0.72f),
                              .text_inverse   = Color::from_hex(0x050810),

                              .border_default = Color(0.22f, 0.28f, 0.38f, 0.28f),
                              .border_subtle  = Color(0.18f, 0.24f, 0.34f, 0.22f),
                              .border_strong  = Color(0.35f, 0.45f, 0.58f, 0.45f),

                              .accent        = Color(0.42f, 0.78f, 0.95f),
                              .accent_hover  = Color(0.55f, 0.86f, 1.0f),
                              .accent_muted  = Color(0.42f, 0.78f, 0.95f, 0.20f),
                              .accent_subtle = Color(0.42f, 0.78f, 0.95f, 0.08f),

                              .success = glass_palette::kSuccessGreen,
                              .warning = glass_palette::kWarningAmber,
                              .error   = Color::from_hex(0xF85149),
                              .info    = glass_palette::kAccentBlue,

                              .grid_major       = Color(0.55f, 0.72f, 0.92f, tokens::GRID_MAJOR_ALPHA_NIGHT),
                              .grid_minor       = Color(0.55f, 0.72f, 0.92f, tokens::GRID_MINOR_ALPHA_NIGHT),
                              .grid_line        = Color(0.55f, 0.72f, 0.92f, tokens::GRID_MAJOR_ALPHA_NIGHT),
                              .axis_line        = Color(0.52f, 0.62f, 0.76f, 0.38f),
                              .tick_label       = glass_palette::kTextSecondary,
                              .crosshair        = Color(0.24f, 0.84f, 0.96f, 0.58f),
                              .selection_fill   = Color(0.58f, 0.45f, 0.96f, 0.14f),
                              .selection_border = glass_palette::kAccentViolet,
                              .tooltip_bg       = glass_palette::kSurfacePopup.with_alpha(0.94f),
                              .tooltip_border   = glass_palette::kBorderGlow.with_alpha(0.42f),

                              .accent_glow       = Color(0.55f, 0.82f, 1.0f, 0.45f),
                              .glow_intensity    = 0.55f,
                              .focus_ring        = glass_palette::kAccentCyan,
                              .scrollbar_thumb   = Color(0.45f, 0.62f, 0.82f, 0.30f),
                              .scrollbar_track   = Color(0.0f, 0.0f, 0.0f, 0.0f),
                              .section_header_bg = Color(1.0f, 1.0f, 1.0f, 0.04f),
                              .input_bg          = Color::from_hex(0x1E2838),
                              .hover_highlight   = Color(1.0f, 1.0f, 1.0f, 0.08f),
                              .annotation_bg     = glass_palette::kSurfacePopup.with_alpha(0.90f),
                              .roi_fill          = Color(0.92f, 0.36f, 0.78f, 0.12f),
                              .roi_border        = glass_palette::kAccentMagenta.with_alpha(0.45f)};
    night.shadow_intensity = 1.0f;
    night.use_blur         = false;
    night.glass_defaults   = ThemeGlassSettings::night_defaults();
    register_theme("night", night);

    // Light Glass — frosted blue-slate chrome with restrained aurora accents
    Theme light;
    light.name   = "light";
    light.colors = {
        // Surfaces — still light, but no longer flat white-on-white
        .bg_canvas    = Color::from_hex(0xF7FAFF),   // Plot area: faint cool glass tint
        .bg_primary   = Color::from_hex(0xD9E4EE),   // App chrome backdrop
        .bg_secondary = Color::from_hex(0xEAF1F7),   // Panels, sidebar, rails
        .bg_tertiary  = Color::from_hex(0xD5E3EF),   // Inputs, chips, buttons
        .bg_elevated  = Color::from_hex(0xF8FBFF),   // Tooltips and popups
        .bg_overlay   = Color(0.0f, 0.0f, 0.0f, 0.30f),

        // Text — blue-black ink against frosted surfaces
        .text_primary   = Color::from_hex(0x102033),
        .text_secondary = Color::from_hex(0x33485F),
        .text_tertiary  = Color::from_hex(0x64748B),
        .text_inverse   = Color::from_hex(0xFFFFFF),

        // Borders — cool glass rims with enough contrast to define layers
        .border_default = Color(0.39f, 0.52f, 0.66f, 0.58f),
        .border_subtle  = Color(0.61f, 0.72f, 0.82f, 0.44f),
        .border_strong  = Color(0.17f, 0.33f, 0.48f, 0.80f),

        // Interactive
        .accent        = Color::from_hex(0x0E7490),
        .accent_hover  = Color::from_hex(0x0F5F78),
        .accent_muted  = Color(0.05f, 0.45f, 0.56f, 0.17f),
        .accent_subtle = Color(0.05f, 0.45f, 0.56f, 0.08f),

        // Semantic
        .success = Color::from_hex(0x1A7F37),
        .warning = Color::from_hex(0x7A5000),   // WCAG AA on light surfaces
        .error   = Color::from_hex(0xB91C1C),   // WCAG AA on light surfaces
        .info    = Color::from_hex(0x2563EB),

        // Plot-specific — cool academic gridlines on the tinted canvas
        .grid_major       = Color(0.32f, 0.40f, 0.48f, 0.24f),
        .grid_minor       = Color(0.32f, 0.40f, 0.48f, 0.10f),
        .grid_line        = Color(0.32f, 0.40f, 0.48f, 0.24f),
        .axis_line        = Color(0.08f, 0.16f, 0.23f, 0.78f),
        .tick_label       = Color::from_hex(0x203247),
        .crosshair        = Color(0.05f, 0.45f, 0.56f, 0.70f),
        .selection_fill   = Color(0.05f, 0.45f, 0.56f, 0.12f),
        .selection_border = Color::from_hex(0x0E7490),
        .tooltip_bg       = Color(0.97f, 0.99f, 1.0f, 0.96f),
        .tooltip_border   = Color(0.45f, 0.57f, 0.70f, 0.48f),

        // Visual effects — subtle daylight glow, not night neon
        .accent_glow       = Color(0.25f, 0.73f, 0.88f, 0.38f),
        .glow_intensity    = 0.36f,
        .focus_ring        = Color::from_hex(0x0E7490),
        .scrollbar_thumb   = Color(0.24f, 0.34f, 0.44f, 0.32f),
        .scrollbar_track   = Color(0.0f, 0.0f, 0.0f, 0.0f),
        .section_header_bg = Color(0.33f, 0.50f, 0.64f, 0.10f),
        .input_bg          = Color::from_hex(0xD8E6F2),
        .hover_highlight   = Color(0.05f, 0.45f, 0.56f, 0.13f),
        .annotation_bg     = Color(0.97f, 0.99f, 1.0f, 0.92f),
        .roi_fill          = Color(0.05f, 0.45f, 0.56f, 0.08f),
        .roi_border        = Color(0.05f, 0.45f, 0.56f, 0.42f)};
    light.shadow_intensity = 0.35f;
    light.use_blur         = false;
    light.glass_defaults   = ThemeGlassSettings::light_defaults();
    register_theme("light", light);

    // High contrast theme
    Theme high_contrast;
    high_contrast.name                            = "high_contrast";
    high_contrast.colors                          = {// Surfaces
                                                     .bg_canvas    = Color::from_hex(0x000000),
                                                     .bg_primary   = Color::from_hex(0x000000),
                                                     .bg_secondary = Color::from_hex(0x1C1C1C),
                                                     .bg_tertiary  = Color::from_hex(0x2D2D2D),
                                                     .bg_elevated  = Color::from_hex(0x3D3D3D),
                                                     .bg_overlay   = Color::from_hex(0xCC000000),

                            // Text
                                                     .text_primary   = Color::from_hex(0xFFFFFF),
                                                     .text_secondary = Color::from_hex(0xE0E0E0),
                                                     .text_tertiary  = Color::from_hex(0xB0B0B0),
                                                     .text_inverse   = Color::from_hex(0x000000),

                            // Borders
                                                     .border_default = Color::from_hex(0xFFFFFF),
                                                     .border_subtle  = Color::from_hex(0xCCCCCC),
                                                     .border_strong  = Color::from_hex(0xFFFFFF),

                            // Interactive
                                                     .accent        = Color::from_hex(0xFFD700),
                                                     .accent_hover  = Color::from_hex(0xFFED4E),
                                                     .accent_muted  = Color::from_hex(0x4DFFD700),
                                                     .accent_subtle = Color::from_hex(0x1AFFD700),

                            // Semantic
                                                     .success = Color::from_hex(0x00FF00),
                                                     .warning = Color::from_hex(0xFFFF00),
                                                     .error   = Color::from_hex(0xFF0000),
                                                     .info    = Color::from_hex(0xFFD700),

                            // Plot-specific
                                                     .grid_major       = Color::from_hex(0x666666),
                                                     .grid_minor       = Color::from_hex(0x444444),
                                                     .grid_line        = Color::from_hex(0x666666),
                                                     .axis_line        = Color::from_hex(0xFFFFFF),
                                                     .tick_label       = Color::from_hex(0xFFFFFF),
                                                     .crosshair        = Color::from_hex(0xCCFFD700),
                                                     .selection_fill   = Color::from_hex(0x4DFFD700),
                                                     .selection_border = Color::from_hex(0xFFD700),
                                                     .tooltip_bg       = Color::from_hex(0x1C1C1C),
                                                     .tooltip_border   = Color::from_hex(0xFFFFFF),

                            // Visual effects
                                                     .accent_glow       = Color(0.0f, 0.0f, 0.0f, 0.0f),
                                                     .glow_intensity    = 0.0f,
                                                     .focus_ring        = Color::from_hex(0xFFD700),
                                                     .scrollbar_thumb   = Color(1.0f, 1.0f, 1.0f, 0.50f),
                                                     .scrollbar_track   = Color(0.0f, 0.0f, 0.0f, 0.0f),
                                                     .section_header_bg = Color(1.0f, 1.0f, 1.0f, 0.08f),
                                                     .input_bg          = Color::from_hex(0x2D2D2D),
                                                     .hover_highlight   = Color(1.0f, 0.84f, 0.0f, 0.30f),
                                                     .annotation_bg     = Color(0.11f, 0.11f, 0.11f, 0.90f),
                                                     .roi_fill          = Color(1.0f, 0.84f, 0.0f, 0.15f),
                                                     .roi_border        = Color(1.0f, 0.84f, 0.0f, 0.60f)};
    high_contrast.glass_defaults                  = ThemeGlassSettings::dark_defaults();
    high_contrast.glass_defaults.master_intensity = 0.0f;
    register_theme("high_contrast", high_contrast);
}

void ThemeManager::initialize_data_palettes()
{
    // Default palette (Tableau 10 — perceptually uniform)
    DataPalette default_palette;
    default_palette.name = "default";
    default_palette.description =
        "Spectra vivid — separated from muted UI chrome for scientific plots";
    default_palette.colorblind_safe = false;
    default_palette.colors          = {
        Color::from_hex(0x22D3EE),   // vivid cyan
        Color::from_hex(0xFB923C),   // orange
        Color::from_hex(0xE879F9),   // magenta
        Color::from_hex(0xA3E635),   // lime
        Color::from_hex(0x60A5FA),   // sky blue
        Color::from_hex(0xFB7185),   // rose
        Color::from_hex(0xFACC15),   // amber
        Color::from_hex(0x34D399),   // emerald
        Color::from_hex(0xC084FC),   // violet
        Color::from_hex(0xE5E7EB)    // light gray
    };
    data_palettes_["default"] = default_palette;

    // Okabe-Ito — the gold standard for colorblind-safe palettes
    DataPalette okabe_ito;
    okabe_ito.name            = "colorblind";
    okabe_ito.description     = "Okabe-Ito — universally safe for all CVD types";
    okabe_ito.colorblind_safe = true;
    okabe_ito.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    okabe_ito.colors          = {
        Color::from_hex(0xE69F00),   // orange
        Color::from_hex(0x56B4E9),   // sky blue
        Color::from_hex(0x009E73),   // bluish green
        Color::from_hex(0xF0E442),   // yellow
        Color::from_hex(0x0072B2),   // blue
        Color::from_hex(0xD55E00),   // vermillion
        Color::from_hex(0xCC79A7),   // reddish purple
        Color::from_hex(0x000000)    // black
    };
    data_palettes_["colorblind"] = okabe_ito;

    // Tol Bright — Paul Tol's bright qualitative scheme
    DataPalette tol_bright;
    tol_bright.name            = "tol_bright";
    tol_bright.description     = "Paul Tol Bright — vivid, CVD-safe qualitative palette";
    tol_bright.colorblind_safe = true;
    tol_bright.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    tol_bright.colors          = {
        Color::from_hex(0x4477AA),   // blue
        Color::from_hex(0xEE6677),   // red
        Color::from_hex(0x228833),   // green
        Color::from_hex(0xCCBB44),   // yellow
        Color::from_hex(0x66CCEE),   // cyan
        Color::from_hex(0xAA3377),   // purple
        Color::from_hex(0xBBBBBB)    // grey
    };
    data_palettes_["tol_bright"] = tol_bright;

    // Tol Muted — Paul Tol's muted qualitative scheme
    DataPalette tol_muted;
    tol_muted.name            = "tol_muted";
    tol_muted.description     = "Paul Tol Muted — softer tones, CVD-safe";
    tol_muted.colorblind_safe = true;
    tol_muted.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    tol_muted.colors          = {
        Color::from_hex(0x332288),   // indigo
        Color::from_hex(0x88CCEE),   // cyan
        Color::from_hex(0x44AA99),   // teal
        Color::from_hex(0x117733),   // green
        Color::from_hex(0x999933),   // olive
        Color::from_hex(0xDDCC77),   // sand
        Color::from_hex(0xCC6677),   // rose
        Color::from_hex(0x882255),   // wine
        Color::from_hex(0xAA4499)    // purple
    };
    data_palettes_["tol_muted"] = tol_muted;

    // IBM Design — accessible palette from IBM's design system
    DataPalette ibm;
    ibm.name            = "ibm";
    ibm.description     = "IBM Design Language — enterprise-grade accessible palette";
    ibm.colorblind_safe = true;
    ibm.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia};
    ibm.colors          = {
        Color::from_hex(0x648FFF),   // ultramarine
        Color::from_hex(0x785EF0),   // indigo
        Color::from_hex(0xDC267F),   // magenta
        Color::from_hex(0xFE6100),   // orange
        Color::from_hex(0xFFB000),   // gold
    };
    data_palettes_["ibm"] = ibm;

    // Wong — Bang Wong's Nature Methods palette
    DataPalette wong;
    wong.name            = "wong";
    wong.description     = "Bang Wong (Nature Methods) — optimized for scientific figures";
    wong.colorblind_safe = true;
    wong.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    wong.colors          = {
        Color::from_hex(0x000000),   // black
        Color::from_hex(0xE69F00),   // orange
        Color::from_hex(0x56B4E9),   // sky blue
        Color::from_hex(0x009E73),   // bluish green
        Color::from_hex(0xF0E442),   // yellow
        Color::from_hex(0x0072B2),   // blue
        Color::from_hex(0xD55E00),   // vermillion
        Color::from_hex(0xCC79A7)    // reddish purple
    };
    data_palettes_["wong"] = wong;

    // Viridis-inspired discrete palette (perceptually uniform, CVD-safe)
    DataPalette viridis;
    viridis.name            = "viridis";
    viridis.description     = "Viridis-inspired discrete — perceptually uniform, print-safe";
    viridis.colorblind_safe = true;
    viridis.safe_for        = {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia};
    viridis.colors          = {
        Color::from_hex(0x440154),   // deep purple
        Color::from_hex(0x482878),   // purple
        Color::from_hex(0x3E4989),   // blue-purple
        Color::from_hex(0x31688E),   // blue
        Color::from_hex(0x26828E),   // teal-blue
        Color::from_hex(0x1F9E89),   // teal
        Color::from_hex(0x35B779),   // green
        Color::from_hex(0x6DCD59),   // lime
        Color::from_hex(0xB4DE2C),   // yellow-green
        Color::from_hex(0xFDE725)    // yellow
    };
    data_palettes_["viridis"] = viridis;

    // High-contrast monochrome (for achromatopsia / grayscale printing)
    DataPalette mono;
    mono.name            = "monochrome";
    mono.description     = "Monochrome — grayscale-safe, works for total color blindness";
    mono.colorblind_safe = true;
    mono.safe_for        = {CVDType::Protanopia,
                            CVDType::Deuteranopia,
                            CVDType::Tritanopia,
                            CVDType::Achromatopsia};
    mono.colors          = {
        Color::from_hex(0x000000),   // black
        Color::from_hex(0x404040),   // dark gray
        Color::from_hex(0x808080),   // mid gray
        Color::from_hex(0xB0B0B0),   // light gray
        Color::from_hex(0xD0D0D0),   // very light gray
    };
    data_palettes_["monochrome"] = mono;

    palette_names_dirty_       = true;
    current_data_palette_name_ = "default";
}

// ─── CVD Simulation ──────────────────────────────────────────────────────────
// Uses Brettel/Viénot/Mollon (1997) simulation matrices for dichromacy.
// These are the standard 3x3 linear-RGB transformation matrices.

Color simulate_cvd(const Color& c, CVDType type)
{
    if (type == CVDType::None)
        return c;

    // Work in linear RGB
    Color lin   = c.to_linear();
    float r     = lin.r;
    float g     = lin.g;
    float b     = lin.b;
    float out_r = NAN;
    float out_g = NAN;
    float out_b = NAN;

    switch (type)
    {
        case CVDType::Protanopia:
            // Viénot et al. 1999 protanopia simulation
            out_r = 0.152286f * r + 1.052583f * g - 0.204868f * b;
            out_g = 0.114503f * r + 0.786281f * g + 0.099216f * b;
            out_b = -0.003882f * r - 0.048116f * g + 1.051998f * b;
            break;
        case CVDType::Deuteranopia:
            // Viénot et al. 1999 deuteranopia simulation
            out_r = 0.367322f * r + 0.860646f * g - 0.227968f * b;
            out_g = 0.280085f * r + 0.672501f * g + 0.047413f * b;
            out_b = -0.011820f * r + 0.042940f * g + 0.968881f * b;
            break;
        case CVDType::Tritanopia:
            // Brettel et al. 1997 tritanopia simulation
            out_r = 1.255528f * r - 0.076749f * g - 0.178779f * b;
            out_g = -0.078411f * r + 0.930809f * g + 0.147602f * b;
            out_b = 0.004733f * r + 0.691367f * g + 0.303900f * b;
            break;
        case CVDType::Achromatopsia:
        {
            // Total color blindness — convert to luminance
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            out_r = out_g = out_b = lum;
            break;
        }
        default:
            return c;
    }

    // Clamp and convert back to sRGB
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    return Color(clamp01(out_r), clamp01(out_g), clamp01(out_b), c.a).to_srgb();
}

ThemeColors ThemeManager::interpolate_colors(const ThemeColors& start,
                                             const ThemeColors& end,
                                             float              t) const
{
    ThemeColors result;
    result.bg_canvas    = start.bg_canvas.lerp(end.bg_canvas, t);
    result.bg_primary   = start.bg_primary.lerp(end.bg_primary, t);
    result.bg_secondary = start.bg_secondary.lerp(end.bg_secondary, t);
    result.bg_tertiary  = start.bg_tertiary.lerp(end.bg_tertiary, t);
    result.bg_elevated  = start.bg_elevated.lerp(end.bg_elevated, t);
    result.bg_overlay   = start.bg_overlay.lerp(end.bg_overlay, t);

    result.text_primary   = start.text_primary.lerp(end.text_primary, t);
    result.text_secondary = start.text_secondary.lerp(end.text_secondary, t);
    result.text_tertiary  = start.text_tertiary.lerp(end.text_tertiary, t);
    result.text_inverse   = start.text_inverse.lerp(end.text_inverse, t);

    result.border_default = start.border_default.lerp(end.border_default, t);
    result.border_subtle  = start.border_subtle.lerp(end.border_subtle, t);
    result.border_strong  = start.border_strong.lerp(end.border_strong, t);

    result.accent        = start.accent.lerp(end.accent, t);
    result.accent_hover  = start.accent_hover.lerp(end.accent_hover, t);
    result.accent_muted  = start.accent_muted.lerp(end.accent_muted, t);
    result.accent_subtle = start.accent_subtle.lerp(end.accent_subtle, t);

    result.success = start.success.lerp(end.success, t);
    result.warning = start.warning.lerp(end.warning, t);
    result.error   = start.error.lerp(end.error, t);
    result.info    = start.info.lerp(end.info, t);

    result.grid_major       = start.grid_major.lerp(end.grid_major, t);
    result.grid_minor       = start.grid_minor.lerp(end.grid_minor, t);
    result.grid_line        = start.grid_line.lerp(end.grid_line, t);
    result.axis_line        = start.axis_line.lerp(end.axis_line, t);
    result.tick_label       = start.tick_label.lerp(end.tick_label, t);
    result.crosshair        = start.crosshair.lerp(end.crosshair, t);
    result.selection_fill   = start.selection_fill.lerp(end.selection_fill, t);
    result.selection_border = start.selection_border.lerp(end.selection_border, t);
    result.tooltip_bg       = start.tooltip_bg.lerp(end.tooltip_bg, t);
    result.tooltip_border   = start.tooltip_border.lerp(end.tooltip_border, t);

    result.accent_glow     = start.accent_glow.lerp(end.accent_glow, t);
    result.glow_intensity  = start.glow_intensity + (end.glow_intensity - start.glow_intensity) * t;
    result.focus_ring      = start.focus_ring.lerp(end.focus_ring, t);
    result.scrollbar_thumb = start.scrollbar_thumb.lerp(end.scrollbar_thumb, t);
    result.scrollbar_track = start.scrollbar_track.lerp(end.scrollbar_track, t);
    result.section_header_bg = start.section_header_bg.lerp(end.section_header_bg, t);
    result.input_bg          = start.input_bg.lerp(end.input_bg, t);
    result.hover_highlight   = start.hover_highlight.lerp(end.hover_highlight, t);
    result.annotation_bg     = start.annotation_bg.lerp(end.annotation_bg, t);
    result.roi_fill          = start.roi_fill.lerp(end.roi_fill, t);
    result.roi_border        = start.roi_border.lerp(end.roi_border, t);

    return result;
}

}   // namespace spectra::ui
