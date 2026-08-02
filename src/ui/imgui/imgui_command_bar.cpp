#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <format>

    #include "../../../third_party/tinyfiledialogs.h"
    #include "../dialog_env_guard.hpp"
    #include "../native_dialog_policy.hpp"
    #include "../topics/topics_panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
    #include "io/export_registry.hpp"
    #include "ui/theme/glass_draw.hpp"
    #include "ui/ui_interaction_log.hpp"
    #include "ui/workspace/plugin_api.hpp"

namespace spectra
{

// ─── Icon sidebar ───────────────────────────────────────────────────────────

// Helper: draw a clickable icon + label button for the Vision nav rail.
// Matches Vision.png: icon centered above a tiny label, subtle pill bg on active,
// very muted inactive state, generous vertical cell height.
static bool icon_label_button(const char* icon_codepoint,
                              const char* label,
                              bool        active,
                              ImFont*     icon_font,
                              ImFont*     label_font,
                              float       width,
                              float       scale = 1.0f)
{
    using namespace ui;

    const auto& colors     = theme();
    auto&       tm         = ui::ThemeManager::instance();
    const float glow_scale = tm.effective_glow_intensity();
    scale                  = std::max(scale,
                     LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT);

    // Rail metrics: icon above label, centered, with consistent spacing tokens.
    // Floors keep icon/label legible even when the rail compresses on short windows.
    float icon_sz =
        std::max(ui::tokens::NAV_RAIL_ICON_SIZE_BASE * scale, ui::tokens::NAV_RAIL_ICON_SIZE_MIN);
    float label_sz =
        std::max(ui::tokens::NAV_RAIL_LABEL_SIZE_BASE * scale, ui::tokens::NAV_RAIL_LABEL_SIZE_MIN);
    float icon_gap = ui::tokens::SPACE_1 * 0.75f * scale;
    float cell_h   = LayoutManager::NAV_RAIL_CELL_HEIGHT * scale;
    float pill_pad = ui::tokens::SPACE_2 * scale;
    float pill_w   = width - pill_pad * 2.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    ImGuiStorage* storage        = ImGui::GetStateStorage();
    ImGuiID       hover_anim_id  = ImGui::GetID("hover_anim");
    ImGuiID       active_anim_id = ImGui::GetID("active_anim");
    ImGui::InvisibleButton("##btn", ImVec2(width, cell_h));
    bool  clicked  = ImGui::IsItemClicked();
    bool  hovered  = ImGui::IsItemHovered();
    float dt       = ImGui::GetIO().DeltaTime;
    float hover_t  = storage->GetFloat(hover_anim_id, hovered ? 1.0f : 0.0f);
    float active_t = storage->GetFloat(active_anim_id, active ? 1.0f : 0.0f);
    hover_t += ((hovered ? 1.0f : 0.0f) - hover_t) * std::min(1.0f, dt * 16.0f);
    active_t += ((active ? 1.0f : 0.0f) - active_t) * std::min(1.0f, dt * 12.0f);
    storage->SetFloat(hover_anim_id, hover_t);
    storage->SetFloat(active_anim_id, active_t);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    float  motion_t = std::max(hover_t, active_t);
    float  lift     = active_t * 1.5f + hover_t * 0.75f;
    ImVec2 pill_min = ImVec2(cursor.x + pill_pad, cursor.y + 3.0f * scale - lift);
    ImVec2 pill_max = ImVec2(cursor.x + pill_pad + pill_w, cursor.y + cell_h - 3.0f * scale - lift);

    if (motion_t > 0.01f || active)
    {
        // Layered outer glow — refined and stronger only on the active tool.
        ui::Color glow_color = ui::control_glow_color(colors);
        if (glow_scale > 0.01f)
            glow_color = glow_color.lerp(glass_palette::kAccentCyan, 0.25f * glow_scale);
        const float glow_a = (ui::tokens::NAV_RAIL_GLOW_ALPHA_HOVER * hover_t
                              + ui::tokens::NAV_RAIL_GLOW_ALPHA_ACTIVE * active_t)
                             * glow_scale;
        for (int gi = 2; gi >= 1; --gi)
        {
            float e = static_cast<float>(gi);
            dl->AddRect(ImVec2(pill_min.x - e, pill_min.y - e),
                        ImVec2(pill_max.x + e, pill_max.y + e),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(glow_color.r, glow_color.g, glow_color.b, glow_a / e)),
                        tokens::RADIUS_MD + e,
                        0,
                        1.5f);
        }

        ui::Color pill_fill = ui::control_surface_color(colors, active, hovered);
        float     pill_a    = ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE
                       + active_t
                             * (ui::tokens::NAV_RAIL_SURFACE_ALPHA_ACTIVE
                                - ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE)
                       + hover_t
                             * (ui::tokens::NAV_RAIL_SURFACE_ALPHA_HOVER
                                - ui::tokens::NAV_RAIL_SURFACE_ALPHA_INACTIVE);
        dl->AddRectFilled(
            pill_min,
            pill_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(pill_fill.r, pill_fill.g, pill_fill.b, pill_a)),
            tokens::RADIUS_MD);

        // Top inner highlight on the pill (night glass only).
        if (glow_scale > 0.01f)
        {
            ui::Color hi   = colors.text_primary;
            int       hi_a = static_cast<int>((24.0f + 18.0f * active_t) * glow_scale);
            dl->AddLine(ImVec2(pill_min.x + 4.0f * scale, pill_min.y + 1.0f),
                        ImVec2(pill_max.x - 4.0f * scale, pill_min.y + 1.0f),
                        IM_COL32(static_cast<uint8_t>(hi.r * 255),
                                 static_cast<uint8_t>(hi.g * 255),
                                 static_cast<uint8_t>(hi.b * 255),
                                 hi_a),
                        1.0f);
        }

        ui::Color border   = ui::control_border_color(colors, active, hovered);
        float     border_a = ui::tokens::NAV_RAIL_BORDER_ALPHA_HOVER * hover_t
                         + ui::tokens::NAV_RAIL_BORDER_ALPHA_ACTIVE * active_t;
        dl->AddRect(pill_min,
                    pill_max,
                    ImGui::ColorConvertFloat4ToU32(ImVec4(border.r, border.g, border.b, border_a)),
                    tokens::RADIUS_MD);
    }

    // Single active state = the glowing pill above. No separate left accent bar
    // (avoids multiple cyan indicators that make several tools look active).

    ui::Color icon_color   = ui::control_text_color(colors, active, hovered);
    ui::Color text_color   = active ? colors.accent_hover : colors.text_secondary;
    float     icon_alpha_v = active ? ui::tokens::NAV_RAIL_ICON_ALPHA_ACTIVE
                                    : (hovered ? ui::tokens::NAV_RAIL_ICON_ALPHA_HOVER
                                               : ui::tokens::NAV_RAIL_ICON_ALPHA_INACTIVE);
    float     text_alpha_v = active ? ui::tokens::NAV_RAIL_LABEL_ALPHA_ACTIVE
                                    : (hovered ? ui::tokens::NAV_RAIL_LABEL_ALPHA_HOVER
                                               : ui::tokens::NAV_RAIL_LABEL_ALPHA_INACTIVE);
    ImU32     icon_col     = ImGui::ColorConvertFloat4ToU32(
        ImVec4(icon_color.r, icon_color.g, icon_color.b, icon_alpha_v));
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(text_color.r, text_color.g, text_color.b, text_alpha_v));

    float icon_draw_sz  = icon_sz * (1.0f + active_t * 0.03f);
    float label_draw_sz = label_sz;
    float content_h     = icon_draw_sz + icon_gap + label_draw_sz;
    float y_start       = std::floor(cursor.y + (cell_h - content_h) * 0.5f - lift * 0.35f);

    if (icon_font)
    {
        ImVec2 isz = icon_font->CalcTextSizeA(icon_draw_sz, FLT_MAX, 0.0f, icon_codepoint);
        float  ix  = std::floor(cursor.x + (width - isz.x) * 0.5f);
        dl->AddText(icon_font, icon_draw_sz, ImVec2(ix, y_start), icon_col, icon_codepoint);
    }

    if (label_font)
    {
        ImVec2 lsz = label_font->CalcTextSizeA(label_draw_sz, FLT_MAX, 0.0f, label);
        float  lx  = std::floor(cursor.x + (width - lsz.x) * 0.5f);
        float  ly  = std::floor(y_start + icon_draw_sz + icon_gap);
        dl->AddText(label_font, label_draw_sz, ImVec2(lx, ly), text_col, label);
    }

    if (clicked)
        ui::log_ui_action("nav_rail", label, "ok");

    return clicked;
}

bool ImGuiIntegration::icon_label_button_rail(const char* icon_codepoint,
                                              const char* label,
                                              bool        active,
                                              ImFont*     icon_font,
                                              ImFont*     label_font,
                                              float       width,
                                              float       scale)
{
    return icon_label_button(icon_codepoint, label, active, icon_font, label_font, width, scale);
}

void ImGuiIntegration::render_menubar_menu(const char* label, const std::vector<MenuItem>& items)
{
    draw_menubar_menu(label, items);
}

// ─── Legacy Methods (removed — use SpectraAppShell / draw_command_bar) ────────

// ─── Legacy Panel Drawing Methods (To be removed after Agent C migration) ───

// Helper for drawing dropdown menus — modern 2026 style with:
//   • auto-close on mouse leave
//   • hover-switch between adjacent menus
//   • popup anchored to button's bottom-left corner
void ImGuiIntegration::draw_menubar_menu(const char* label, const std::vector<MenuItem>& items)
{
    const auto& colors       = theme_colors();
    bool        menu_is_open = open_menu_label_ == label;

    ImGui::PushFont(font_menubar_);
    // Menu labels read at the same weight as body text so navigation feels intentional.
    ui::Color menu_text = menu_is_open ? colors.accent_hover : colors.text_primary;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(menu_text.r,
                                 menu_text.g,
                                 menu_text.b,
                                 menu_is_open ? 1.0f : ui::tokens::COMMAND_BAR_MENU_TEXT_ALPHA));
    // Glass-pill open/hover/active states (translucent accent tint, rounded).
    ui::Color menu_bg = ui::control_surface_color(colors, menu_is_open, false);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(menu_bg.r, menu_bg.g, menu_bg.b, menu_is_open ? 0.80f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.24f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2 + 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);

    // Remember button rect for popup positioning and auto-close
    ImVec2 btn_pos     = ImGui::GetCursorScreenPos();
    bool   clicked     = ImGui::Button(label);
    ImVec2 btn_size    = ImGui::GetItemRectSize();
    ImVec2 btn_max     = ImVec2(btn_pos.x + btn_size.x, btn_pos.y + btn_size.y);
    bool   btn_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // Click opens this menu
    if (clicked)
    {
        SPECTRA_LOG_DEBUG("menu", "Click open: " + std::string(label));
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Hover-switch: if another menu is open and user hovers this button, switch
    if (btn_hovered && !open_menu_label_.empty() && open_menu_label_ != label)
    {
        SPECTRA_LOG_DEBUG("menu",
                          "Hover switch: " + std::string(open_menu_label_) + " -> " + label);
        ImGui::OpenPopup(label);
        open_menu_label_ = label;
    }

    // Anchor popup at button's bottom-left corner (not at mouse position)
    ImGui::SetNextWindowPos(ImVec2(btn_pos.x, btn_max.y + 2.0f));

    // Modern popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_1));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.97f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.4f));

    if (ImGui::BeginPopup(label))
    {
        // Track that this menu is the open one
        open_menu_label_ = label;

        // ── Auto-close: dismiss when mouse moves away from button + popup ──
        ImVec2 mouse      = ImGui::GetIO().MousePos;
        ImVec2 popup_pos  = ImGui::GetWindowPos();
        ImVec2 popup_size = ImGui::GetWindowSize();
        float  margin     = 20.0f;

        // Combined rect of button + popup + margin
        float combined_min_x = std::min(btn_pos.x, popup_pos.x) - margin;
        float combined_min_y = std::min(btn_pos.y, popup_pos.y) - margin;
        float combined_max_x = std::max(btn_max.x, popup_pos.x + popup_size.x) + margin;
        float combined_max_y = std::max(btn_max.y, popup_pos.y + popup_size.y) + margin;

        bool mouse_in_zone = (mouse.x >= combined_min_x && mouse.x <= combined_max_x
                              && mouse.y >= combined_min_y && mouse.y <= combined_max_y);

        if (!mouse_in_zone && !ImGui::IsAnyItemActive())
        {
            SPECTRA_LOG_DEBUG("menu", "Auto-close: " + std::string(label));
            ImGui::CloseCurrentPopup();
            open_menu_label_.clear();
        }

        // Multi-layer soft shadow behind popup (elevation 3)
        ImDrawList* bg_dl        = ImGui::GetBackgroundDrawList();
        float       popup_shadow = ui::tokens::ELEVATION_3_SPREAD;
        for (int si = 1; si <= 4; ++si)
        {
            float st    = static_cast<float>(si) / 4.0f;
            float soff  = popup_shadow * st;
            float salph = 0.12f * (1.0f - st * 0.7f);
            bg_dl->AddRectFilled(
                ImVec2(popup_pos.x + soff * 0.3f, popup_pos.y + soff * 0.5f),
                ImVec2(popup_pos.x + popup_size.x + soff * 0.5f, popup_pos.y + popup_size.y + soff),
                IM_COL32(0, 0, 0, static_cast<int>(salph * 255)),
                ui::tokens::RADIUS_LG + soff * 0.5f);
        }

        for (const auto& item : items)
        {
            if (item.label.empty())
            {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));
            }
            else if (!item.callback)
            {
                // Null callback + non-empty label → disabled / grayed-out text item.
                // text_tertiary is the theme field for placeholders / disabled text.
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_tertiary.r,
                                             colors.text_tertiary.g,
                                             colors.text_tertiary.b,
                                             colors.text_tertiary.a));
                float item_h = ImGui::GetTextLineHeight() + 10.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                ImGui::Selectable(item.label.c_str(),
                                  false,
                                  ImGuiSelectableFlags_Disabled,
                                  ImVec2(0, item_h));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_primary.r,
                                             colors.text_primary.g,
                                             colors.text_primary.b,
                                             colors.text_primary.a));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(colors.accent_subtle.r,
                                             colors.accent_subtle.g,
                                             colors.accent_subtle.b,
                                             0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      ImVec4(colors.accent_muted.r,
                                             colors.accent_muted.g,
                                             colors.accent_muted.b,
                                             0.7f));
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

                float item_h = ImGui::GetTextLineHeight() + 10.0f;
                if (ImGui::Selectable(item.label.c_str(),
                                      false,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(0, item_h)))
                {
                    ui::log_ui_action("menu", item.label.c_str(), "ok");
                    item.callback();
                    open_menu_label_.clear();
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
            }
        }

        ImGui::EndPopup();
    }
    else
    {
        // Popup closed (e.g. by clicking outside) — clear tracking if this was the open one
        if (open_menu_label_ == label)
        {
            open_menu_label_.clear();
        }
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// Helper for drawing toolbar buttons with modern hover styling and themed tooltips
void ImGuiIntegration::draw_toolbar_button(const char*                  icon,
                                           const std::function<void()>& callback,
                                           const char*                  tooltip,
                                           bool                         is_active,
                                           float                        icon_scale)
{
    const auto& colors = theme_colors();
    // Use per-instance font_icon_ (not the IconFont singleton) so that
    // secondary windows use their own atlas font, avoiding TexID mismatch.
    ImGui::PushFont(font_icon_);

    ui::Color bg  = ui::control_surface_color(colors, is_active, false);
    ui::Color txt = ui::control_text_color(colors, is_active, false);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(bg.r, bg.g, bg.b, is_active ? 0.90f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(txt.r, txt.g, txt.b, ui::shell_text_alpha(txt.a)));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.28f));
    const float pad       = ui::tokens::SPACE_2 * icon_scale;
    const float roundness = ui::tokens::RADIUS_MD * icon_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad + 2.0f, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, roundness);

    ImGui::SetWindowFontScale(icon_scale);
    if (ImGui::Button(icon))
    {
        if (callback)
            callback();
    }
    ImGui::SetWindowFontScale(1.0f);

    // Store tooltip for deferred rendering at the end of build_ui
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && tooltip)
    {
        deferred_tooltip_ = tooltip;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

// ─── Layout-Based Drawing Methods ─────────────────────────────────────────────

bool ImGuiIntegration::begin_command_bar()
{
    if (!layout_manager_)
    {
        SPECTRA_LOG_WARN("ui", "begin_command_bar called but layout_manager_ is null");
        command_bar_began_ = false;
        return false;
    }

    SPECTRA_LOG_TRACE("ui", "Beginning command bar");

    Rect bounds = layout_manager_->command_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ui::tokens::COMMAND_BAR_ITEM_SPACING, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(theme_colors().border_subtle.r,
                                 theme_colors().border_subtle.g,
                                 theme_colors().border_subtle.b,
                                 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    command_bar_began_ = ImGui::Begin("##commandbar", nullptr, flags);
    if (!command_bar_began_)
        return false;

    SPECTRA_LOG_TRACE("ui", "Command bar window began successfully");

    // ── Header surface: subtle top accent line + bottom shadow + hairline ──
    {
        ImDrawList* bar_dl = ImGui::GetWindowDrawList();
        ImVec2      wpos   = ImGui::GetWindowPos();
        ImVec2      wsz    = ImGui::GetWindowSize();
        float       bottom = wpos.y + wsz.y;
        const auto& c      = theme_colors();
        const float glow   = theme_mgr_ ? theme_mgr_->effective_glow_intensity() : 0.0f;

        // Very subtle top accent line (cyan/purple) — restrained, theme-aware.
        ui::Color top_line = c.accent.lerp(ui::Color::from_hex(0x60C0FF), 0.35f);
        int       top_a    = static_cast<int>((18.0f + 24.0f * glow) * c.accent_glow.a);
        bar_dl->AddLine(ImVec2(wpos.x, wpos.y),
                        ImVec2(wpos.x + wsz.x, wpos.y),
                        IM_COL32(static_cast<uint8_t>(top_line.r * 255),
                                 static_cast<uint8_t>(top_line.g * 255),
                                 static_cast<uint8_t>(top_line.b * 255),
                                 std::clamp(top_a, 0, 255)),
                        1.0f);

        // Multi-layer soft shadow below the bar
        float shadow_spread = ui::tokens::ELEVATION_2_SPREAD;
        for (int i = 0; i < 4; ++i)
        {
            float t     = static_cast<float>(i) / 4.0f;
            float alpha = 0.10f * (1.0f - t);
            float off   = shadow_spread * t;
            bar_dl->AddRectFilled(ImVec2(wpos.x, bottom),
                                  ImVec2(wpos.x + wsz.x, bottom + off + 1.0f),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
        }

        // Crisp hairline border at bottom edge
        bar_dl->AddLine(ImVec2(wpos.x, std::floor(bottom) - 1.0f),
                        ImVec2(wpos.x + wsz.x, std::floor(bottom) - 1.0f),
                        IM_COL32(static_cast<uint8_t>(c.border_subtle.r * 255),
                                 static_cast<uint8_t>(c.border_subtle.g * 255),
                                 static_cast<uint8_t>(c.border_subtle.b * 255),
                                 90),
                        1.0f);

        // Opaque command-bar base — follows the active theme (not hardcoded Vision navy).
        bar_dl->AddRectFilled(ImVec2(wpos.x, wpos.y),
                              ImVec2(wpos.x + wsz.x, bottom),
                              IM_COL32(static_cast<uint8_t>(c.bg_primary.r * 255),
                                       static_cast<uint8_t>(c.bg_primary.g * 255),
                                       static_cast<uint8_t>(c.bg_primary.b * 255),
                                       255));

        // Night-only Vision chrome: aurora wash + luminous perimeter.
        if (glow > 0.01f)
        {
            float glow_x0 = wpos.x + wsz.x * 0.55f;
            int   gv_a    = static_cast<int>((38.0f + 26.0f * glow) * glow);
            bar_dl->AddRectFilledMultiColor(ImVec2(glow_x0, wpos.y),
                                            ImVec2(wpos.x + wsz.x, bottom),
                                            IM_COL32(150, 60, 140, 0),
                                            IM_COL32(170, 60, 150, gv_a),
                                            IM_COL32(170, 60, 150, gv_a),
                                            IM_COL32(150, 60, 140, 0));

            const float bx0      = wpos.x;
            const float by0      = wpos.y;
            const float bx1      = wpos.x + wsz.x;
            const float by1      = std::floor(bottom) - 1.0f;
            ui::Color   cool     = c.accent.lerp(ui::Color::from_hex(0x5C8CC0), 0.30f);
            int         cool_a   = static_cast<int>((42.0f + 26.0f * glow) * glow);
            const ImU32 cool_col = IM_COL32(static_cast<uint8_t>(cool.r * 255),
                                            static_cast<uint8_t>(cool.g * 255),
                                            static_cast<uint8_t>(cool.b * 255),
                                            std::clamp(cool_a, 0, 255));
            bar_dl->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), cool_col, 0.0f, 0, 1.0f);

            const int   viol_a = static_cast<int>((80.0f + 55.0f * glow) * glow);
            const ImU32 vio    = IM_COL32(175, 105, 195, std::clamp(viol_a, 0, 255));
            const ImU32 vio0   = IM_COL32(165, 95, 180, 0);
            bar_dl->AddRectFilledMultiColor(ImVec2(bx1 - 1.5f, by0),
                                            ImVec2(bx1, by1),
                                            vio,
                                            vio,
                                            vio,
                                            vio);
            bar_dl->AddRectFilledMultiColor(ImVec2(glow_x0, by0),
                                            ImVec2(bx1, by0 + 1.5f),
                                            vio0,
                                            vio,
                                            vio,
                                            vio0);
            bar_dl->AddRectFilledMultiColor(ImVec2(glow_x0, by1 - 1.5f),
                                            ImVec2(bx1, by1),
                                            vio0,
                                            vio,
                                            vio,
                                            vio0);
        }
    }

    return true;
}

void ImGuiIntegration::end_command_bar()
{
    if (command_bar_began_)
    {
        ImGui::End();
        command_bar_began_ = false;
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

void ImGuiIntegration::draw_command_bar()
{
    if (!begin_command_bar())
    {
        end_command_bar();
        return;
    }

    {
        // ── App title/brand on the left — round S icon + clean wordmark ──
        {
            const auto& c      = theme_colors();
            ImDrawList* dl     = ImGui::GetWindowDrawList();
            float       bar_h  = ImGui::GetWindowSize().y;
            ImVec2      cursor = ImGui::GetCursorScreenPos();
            // Center against the true window vertical midline (cursor.y already
            // includes the bar's top padding, which would bias everything down).
            float cy = ImGui::GetWindowPos().y + bar_h * 0.5f;

            // Round Spectra icon, vertically centered in the bar.
            float logo_sz = 26.0f;
            float lx      = std::floor(cursor.x);
            float ly      = std::floor(cy - logo_sz * 0.5f);
            float text_x  = lx + logo_sz + ui::tokens::SPACE_3;

            if (corner_logo_texture_id_)
            {
                dl->AddImage(imgui_texture_id_from_u64(corner_logo_texture_id_),
                             ImVec2(lx, ly),
                             ImVec2(lx + logo_sz, ly + logo_sz));
            }

            ImGui::PushFont(font_title_);
            const char* letters = "SPECTRA";
            float       font_sz = font_title_->LegacySize * 0.88f;
            float       text_y  = cy - font_sz * 0.5f;
            float       spacing = 1.5f;

            float total_w = 0.0f;
            for (const char* p = letters; *p; ++p)
            {
                char ch[2] = {*p, 0};
                total_w += ImGui::CalcTextSize(ch).x + spacing;
            }
            total_w -= spacing;   // no trailing space

            {
                float gx  = text_x;
                int   idx = 0;
                int   len = static_cast<int>(strlen(letters));
                for (const char* p = letters; *p; ++p, ++idx)
                {
                    char  ch[2] = {*p, 0};
                    float cw    = ImGui::CalcTextSize(ch).x;
                    (void)len;
                    (void)idx;
                    dl->AddText(font_title_,
                                font_sz,
                                ImVec2(gx, text_y),
                                IM_COL32(static_cast<uint8_t>(c.text_secondary.r * 255),
                                         static_cast<uint8_t>(c.text_secondary.g * 255),
                                         static_cast<uint8_t>(c.text_secondary.b * 255),
                                         235),
                                ch);
                    gx += cw + spacing;
                }
            }

            // Advance ImGui cursor past the entire brand block (icon + wordmark).
            float brand_w =
                (text_x - cursor.x) + total_w + ui::tokens::COMMAND_BAR_BRAND_TO_HOME_GAP;
            ImGui::Dummy(ImVec2(brand_w, font_sz));
            ImGui::PopFont();
        }

        ImGui::SameLine();

        // Home button — Vision.png: dark glass tile, soft cyan halo, single icon (no double-draw).
        {
            const auto& header_colors = theme_colors();
            const float glow          = theme_mgr_ ? theme_mgr_->effective_glow_intensity() : 0.0f;

            constexpr float kHomeBtn = 28.0f;
            ImGui::PushFont(font_icon_);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

            if (ImGui::Button("##home_btn", ImVec2(kHomeBtn, kHomeBtn)))
            {
                SPECTRA_LOG_DEBUG("ui_button", "Home button clicked - setting reset_view flag");
                reset_view_ = true;
                SPECTRA_LOG_DEBUG("ui_button", "Reset view flag set successfully");
            }

            ImVec2      mn  = ImGui::GetItemRectMin();
            ImVec2      mx  = ImGui::GetItemRectMax();
            ImDrawList* hdl = ImGui::GetWindowDrawList();
            ImVec2      ctr((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            const bool  hovered = ImGui::IsItemHovered();

            // Vision.png: soft light house, faint glow, no tile/border/halo.
            // Only a subtle hover background tile appears on interaction.
            if (hovered)
            {
                ui::Color fill = header_colors.bg_tertiary;
                hdl->AddRectFilled(mn,
                                   mx,
                                   IM_COL32(static_cast<uint8_t>(fill.r * 255),
                                            static_cast<uint8_t>(fill.g * 255),
                                            static_cast<uint8_t>(fill.b * 255),
                                            140),
                                   ui::tokens::RADIUS_MD);
            }

            const char* home_icon = ui::icon_str(ui::Icon::Home);
            const float icon_sz   = font_icon_->LegacySize * 0.70f;
            ImVec2      isz       = font_icon_->CalcTextSizeA(icon_sz, FLT_MAX, 0.0f, home_icon);
            ImVec2      ipos(ctr.x - isz.x * 0.5f, ctr.y - isz.y * 0.5f);

            if (glow > 0.01f)
            {
                ui::Color halo = header_colors.accent.lerp(ui::Color::from_hex(0x96B4E6), 0.35f);
                hdl->AddText(font_icon_,
                             icon_sz,
                             ImVec2(ipos.x, ipos.y),
                             IM_COL32(static_cast<uint8_t>(halo.r * 255),
                                      static_cast<uint8_t>(halo.g * 255),
                                      static_cast<uint8_t>(halo.b * 255),
                                      static_cast<int>((40.0f + 30.0f * glow) * glow)),
                             home_icon);
            }
            ui::Color icon_col = header_colors.text_secondary;
            hdl->AddText(font_icon_,
                         icon_sz,
                         ipos,
                         IM_COL32(static_cast<uint8_t>(icon_col.r * 255),
                                  static_cast<uint8_t>(icon_col.g * 255),
                                  static_cast<uint8_t>(icon_col.b * 255),
                                  255),
                         home_icon);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                deferred_tooltip_ = "Reset View (Home)";
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();
        }

        ImGui::SameLine(0.0f, ui::tokens::COMMAND_BAR_HOME_TO_MENU_GAP);

        if (app_shell_)
        {
            app_shell_->sync_before_frame();
            app_shell_->draw_menus_inline();
        }
        else
        {
            // File menu — build dynamically to include plugin export formats
            std::vector<MenuItem> file_items;
            file_items.emplace_back("New Figure",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("figure.new");
                                    });
            file_items.emplace_back("", nullptr);   // Separator
            file_items.emplace_back("Export PNG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_png");
                                    });
            file_items.emplace_back("Export SVG",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.export_svg");
                                    });
            file_items.emplace_back("Copy as Image\tCtrl+Shift+C",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.copy_to_clipboard");
                                    });

            // Plugin-registered export formats (API v1.3)
            if (export_format_registry_)
            {
                auto formats = export_format_registry_->available_formats();
                if (!formats.empty())
                {
                    file_items.emplace_back("", nullptr);   // Separator
                    for (const auto& fmt : formats)
                    {
                        std::string label    = "Export " + fmt.name + " (." + fmt.extension + ")";
                        std::string fmt_name = fmt.name;
                        file_items.emplace_back(
                            label,
                            [this, fmt_name]()
                            {
                                if (command_registry_)
                                    command_registry_->execute("file.export_plugin." + fmt_name);
                            });
                    }
                }
            }

            file_items.emplace_back("Save Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.save_workspace");
                                    });
            file_items.emplace_back("Load Workspace",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.load_workspace");
                                    });
            file_items.emplace_back("", nullptr);   // Separator
            file_items.emplace_back("Save Figure...",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.save_figure");
                                    });
            file_items.emplace_back("Load Figure...",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("file.load_figure");
                                    });
            file_items.emplace_back("", nullptr);   // Separator
            file_items.emplace_back("Exit",
                                    [this]()
                                    {
                                        if (command_registry_)
                                            command_registry_->execute("app.cancel");
                                    });
            draw_menubar_menu("File", file_items);

            ImGui::SameLine();

            // View menu
            draw_menubar_menu(
                "View",
                {MenuItem("Toggle Inspector",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("panel.toggle_inspector");
                              else
                              {
                                  bool new_vis = !layout_manager_->is_inspector_visible();
                                  layout_manager_->set_inspector_visible(new_vis);
                                  panel_open_ = new_vis;
                              }
                          }),
                 MenuItem("Toggle Navigation Rail",
                          [this]() { set_nav_rail_visible(!show_nav_rail_); }),
                 MenuItem("Zoom to Fit",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("view.autofit");
                          }),
                 MenuItem("Reset View",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("view.reset");
                          }),
                 MenuItem("Toggle Grid",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("view.toggle_grid");
                          }),
                 MenuItem("Toggle Legend",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("view.toggle_legend");
                          }),
                 MenuItem("Remove All Data Tips",
                          [this]()
                          {
                              if (data_interaction_)
                                  data_interaction_->clear_markers();
                          }),
                 MenuItem("", nullptr),   // Separator
                 MenuItem("Toggle Timeline",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("panel.toggle_timeline");
                          }),
                 MenuItem("Toggle Curve Editor",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("panel.toggle_curve_editor");
                          }),
                 MenuItem("Toggle Parameters",
                          [this]()
                          {
                              if (knob_manager_ && !knob_manager_->empty())
                                  knob_manager_->set_visible(!knob_manager_->is_visible());
                          }),
                 MenuItem("Toggle Data Editor",
                          [this]()
                          {
                              if (active_section_ == Section::DataEditor && panel_open_)
                              {
                                  panel_open_ = false;
                                  layout_manager_->set_inspector_visible(false);
                              }
                              else
                              {
                                  active_section_ = Section::DataEditor;
                                  panel_open_     = true;
                                  layout_manager_->set_inspector_visible(true);
                              }
                          }),
                 MenuItem("Toggle Topics",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("panel.toggle_topics");
                          }),
                 MenuItem("Plugins...",
                          [this]()
                          {
                              if (command_registry_)
                                  command_registry_->execute("panel.toggle_plugins");
                              else
                                  show_plugins_panel_ = !show_plugins_panel_;
                          })});

            ImGui::SameLine();

            // Tools menu
            draw_menubar_menu("Tools",
                              {MenuItem("Screenshot (PNG)",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("file.export_png");
                                        }),
                               MenuItem("Undo",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("edit.undo");
                                        }),
                               MenuItem("Redo",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("edit.redo");
                                        }),
                               MenuItem("", nullptr),   // Separator
                               MenuItem("Theme Settings",
                                        [this]() { show_theme_settings_ = !show_theme_settings_; }),
                               MenuItem("Command Palette",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("app.command_palette");
                                        }),
                               MenuItem("", nullptr),   // Separator
    #ifdef SPECTRA_USE_ROS2
                               MenuItem("ROS2 Adapter",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("tools.ros2_adapter");
                                        })
    #else
             // Grayed-out placeholder when compiled without ROS2 support.
             // draw_menubar_menu skips items with a null callback; we render
             // a disabled text label instead via a zero-callback sentinel that
             // is handled specially in draw_menubar_menu.
             MenuItem("\xEF\xA0\xAD ROS2 Adapter (not available)", nullptr)
    #endif
                              });

            ImGui::SameLine();

            // Plot menu — reference lines and function overlays (legacy path without AppShell)
            draw_menubar_menu("Plot",
                              {MenuItem("Y = 0 Line",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("plot.hline_zero");
                                        }),
                               MenuItem("X = 0 Line",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("plot.vline_zero");
                                        }),
                               MenuItem("", nullptr),
                               MenuItem("Horizontal Line...",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("plot.hline");
                                        }),
                               MenuItem("Vertical Line...",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("plot.vline");
                                        }),
                               MenuItem("Plot Function...",
                                        [this]()
                                        {
                                            if (command_registry_)
                                                command_registry_->execute("plot.function");
                                        })});

            ImGui::SameLine();
        }

        ImGui::SameLine();

        // Data menu
        draw_menubar_menu(
            "Data",
            {MenuItem(
                "Load from CSV...",
                [this]()
                {
                    if (!native_dialogs_enabled())
                        return;
                    DialogEnvGuard env_guard;
                    char const*    filters[3] = {"*.csv", "*.tsv", "*.txt"};
                    const char*    home_env   = std::getenv("HOME");
                    std::string    home_dir   = home_env ? std::string(home_env) + "/" : "/";
                    const char*    home       = home_dir.c_str();
                    char const*    result =
                        tinyfd_openFileDialog("Open CSV File", home, 3, filters, "CSV files", 0);
                    if (result)
                    {
                        csv_file_path_   = result;
                        csv_data_        = parse_csv(csv_file_path_);
                        csv_data_loaded_ = csv_data_.error.empty();
                        csv_error_       = csv_data_.error;
                        csv_col_x_       = 0;
                        csv_selected_y_.clear();
                        for (size_t c = 1; c < csv_data_.num_cols; ++c)
                            csv_selected_y_.push_back(static_cast<int>(c));
                        if (csv_selected_y_.empty() && csv_data_.num_cols > 0)
                            csv_selected_y_.push_back(0);
                        csv_col_z_ = -1;
                        if (csv_data_loaded_)
                            csv_dialog_open_ = true;
                    }
                })});

        ImGui::SameLine();

        // Axes menu — link/unlink axes across subplots (2D and 3D)
        {
            std::vector<MenuItem> axes_items;

            auto has_enough_axes = [this]() -> bool
            {
                if (!axis_link_mgr_ || !current_figure_)
                    return false;
                return current_figure_->all_axes().size() >= 2;
            };

            axes_items.emplace_back(
                "Link X Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("X Link", LinkAxis::X);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i]);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X");
                });
            axes_items.emplace_back(
                "Link Y Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("Y Link", LinkAxis::Y);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i]);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on Y");
                });
            axes_items.emplace_back(
                "Link Z Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    std::vector<Axes3D*> axes3d_list;
                    for (auto& ab : current_figure_->all_axes_mut())
                    {
                        if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                            axes3d_list.push_back(a3);
                    }
                    for (size_t i = 1; i < axes3d_list.size(); ++i)
                        axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i], LinkAxis::Z);
                    SPECTRA_LOG_INFO("axes_link", "Linked all 3D axes on Z");
                });
            axes_items.emplace_back(
                "Link All Axes",
                [this, has_enough_axes]()
                {
                    if (!has_enough_axes())
                        return;
                    if (current_figure_->axes().size() >= 2)
                    {
                        auto gid = axis_link_mgr_->create_group("XY Link", LinkAxis::Both);
                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (ax)
                                axis_link_mgr_->add_to_group(gid, ax.get());
                        }
                    }
                    {
                        std::vector<Axes3D*> axes3d_list;
                        for (auto& ab : current_figure_->all_axes_mut())
                        {
                            if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                axes3d_list.push_back(a3);
                        }
                        for (size_t i = 1; i < axes3d_list.size(); ++i)
                            axis_link_mgr_->link_3d(axes3d_list[0], axes3d_list[i], LinkAxis::All);
                    }
                    SPECTRA_LOG_INFO("axes_link", "Linked all axes on X+Y+Z");
                });
            axes_items.emplace_back("", nullptr);
            axes_items.emplace_back("Unlink All",
                                    [this]()
                                    {
                                        if (!axis_link_mgr_)
                                            return;
                                        std::vector<LinkGroupId> ids;
                                        for (auto& [id, group] : axis_link_mgr_->groups())
                                            ids.push_back(id);
                                        for (auto id : ids)
                                            axis_link_mgr_->remove_group(id);
                                        if (current_figure_)
                                        {
                                            for (auto& ab : current_figure_->all_axes_mut())
                                            {
                                                if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                                                    axis_link_mgr_->remove_from_all_3d(a3);
                                            }
                                        }
                                        axis_link_mgr_->clear_shared_cursor();
                                        SPECTRA_LOG_INFO("axes_link", "Unlinked all axes");
                                    });

            draw_menubar_menu("Axes", axes_items);
        }

        ImGui::SameLine();

        {
            std::vector<MenuItem> xform_items;
            auto&                 registry = TransformRegistry::instance();
            auto                  names    = registry.available_transforms();

            xform_items.reserve(names.size());
            for (const auto& name : names)
            {
                xform_items.emplace_back(
                    name,
                    [this, name]()
                    {
                        if (!current_figure_)
                            return;
                        DataTransform xform;
                        if (!TransformRegistry::instance().get_transform(name, xform))
                            return;

                        for (auto& ax : current_figure_->axes_mut())
                        {
                            if (!ax)
                                continue;
                            for (auto& series_ptr : ax->series_mut())
                            {
                                if (!series_ptr || !series_ptr->visible())
                                    continue;

                                if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx;
                                    std::vector<float> ry;
                                    xform.apply_y(ls->x_data(), ls->y_data(), rx, ry);
                                    ls->set_x(rx).set_y(ry);
                                }
                                else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
                                {
                                    std::vector<float> rx;
                                    std::vector<float> ry;
                                    xform.apply_y(sc->x_data(), sc->y_data(), rx, ry);
                                    sc->set_x(rx).set_y(ry);
                                }
                            }
                            ax->auto_fit();
                        }
                        SPECTRA_LOG_INFO("transform", "Applied transform: " + name);
                    });
            }

            xform_items.emplace_back("", nullptr);
            xform_items.emplace_back(
                "Custom Formula...",
                [this]()
                {
                    custom_transform_dialog_.set_fonts(font_body_, font_heading_, font_title_);
                    custom_transform_dialog_.open(current_figure_);
                });

            draw_menubar_menu("Transforms", xform_items);
        }

        (void)font_heading_;
    }

    end_command_bar();
}

void ImGuiIntegration::draw_plugins_panel()
{
    if (!show_plugins_panel_)
        return;

    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", &show_plugins_panel_))
    {
        ImGui::End();
        return;
    }

    if (!plugin_manager_)
    {
        ImGui::TextUnformatted("Plugin manager is not available in this runtime.");
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Manage runtime plugins and extension points.");
    ImGui::Separator();

    if (ImGui::Button("Load Plugin...") && native_dialogs_enabled())
    {
        DialogEnvGuard env_guard;
    #ifdef _WIN32
        static const char* filters[1] = {"*.dll"};
        const char*        path       = tinyfd_openFileDialog("Load Plugin",
                                                 PluginManager::default_plugin_dir().c_str(),
                                                 1,
                                                 filters,
                                                 "Plugins",
                                                 0);
    #elif defined(__APPLE__)
        static const char* filters[1] = {"*.dylib"};
        const char*        path       = tinyfd_openFileDialog("Load Plugin",
                                                 PluginManager::default_plugin_dir().c_str(),
                                                 1,
                                                 filters,
                                                 "Plugins",
                                                 0);
    #else
        static const char* filters[1] = {"*.so"};
        const char*        path       = tinyfd_openFileDialog("Load Plugin",
                                                 PluginManager::default_plugin_dir().c_str(),
                                                 1,
                                                 filters,
                                                 "Plugins",
                                                 0);
    #endif
        if (path && path[0] != '\0')
        {
            if (plugin_manager_->load_plugin(path))
                plugin_panel_status_ = std::string("Loaded: ") + path;
            else
                plugin_panel_status_ = std::string("Failed to load: ") + path;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Scan Plugin Dirs"))
    {
        int loaded = 0;
        int failed = 0;

        std::vector<std::string> dirs;
        dirs.push_back(PluginManager::default_plugin_dir());
        for (const auto& d : plugin_scan_dirs_)
        {
            if (!d.empty())
                dirs.push_back(d);
        }

        for (const auto& dir : dirs)
        {
            auto discovered = plugin_manager_->discover(dir);
            for (const auto& path : discovered)
            {
                if (plugin_manager_->load_plugin(path))
                    ++loaded;
                else
                    ++failed;
            }
        }
        plugin_panel_status_ = "Scan complete: loaded " + std::to_string(loaded)
                               + ", failed/skipped " + std::to_string(failed);
    }

    ImGui::SameLine();
    if (ImGui::Button("Rescan Default"))
    {
        int  loaded     = 0;
        int  failed     = 0;
        auto discovered = plugin_manager_->discover(PluginManager::default_plugin_dir());
        for (const auto& path : discovered)
        {
            if (plugin_manager_->load_plugin(path))
                ++loaded;
            else
                ++failed;
        }
        plugin_panel_status_ = "Default scan: loaded " + std::to_string(loaded)
                               + ", failed/skipped " + std::to_string(failed);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Custom scan directories:");
    ImGui::PushItemWidth(-120.0f);
    ImGui::InputText("##plugin_scan_dir", plugin_scan_dir_buf_, sizeof(plugin_scan_dir_buf_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Dir"))
    {
        std::string dir = plugin_scan_dir_buf_;
        if (!dir.empty())
        {
            if (std::find(plugin_scan_dirs_.begin(), plugin_scan_dirs_.end(), dir)
                == plugin_scan_dirs_.end())
            {
                plugin_scan_dirs_.push_back(dir);
            }
            plugin_scan_dir_buf_[0] = '\0';
        }
    }

    for (size_t i = 0; i < plugin_scan_dirs_.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(plugin_scan_dirs_[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
        {
            plugin_scan_dirs_.erase(plugin_scan_dirs_.begin() + static_cast<long>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (!plugin_panel_status_.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", plugin_panel_status_.c_str());
    }

    ImGui::Separator();

    auto plugins = plugin_manager_->plugins();
    if (plugins.empty())
    {
        ImGui::TextUnformatted("No plugins loaded.");
        ImGui::End();
        return;
    }

    std::string unload_name;
    for (const auto& plugin : plugins)
    {
        ImGui::PushID(plugin.name.c_str());
        ImGui::SeparatorText(plugin.name.c_str());
        ImGui::Text("Version: %s", plugin.version.empty() ? "(unknown)" : plugin.version.c_str());
        ImGui::Text("Author: %s", plugin.author.empty() ? "(unknown)" : plugin.author.c_str());
        ImGui::TextWrapped("Description: %s",
                           plugin.description.empty() ? "(none)" : plugin.description.c_str());
        ImGui::TextWrapped("Path: %s", plugin.path.c_str());

        bool enabled = plugin.enabled;
        if (ImGui::Checkbox("Enabled", &enabled))
            plugin_manager_->set_plugin_enabled(plugin.name, enabled);

        ImGui::SameLine();
        if (ImGui::Button("Unload"))
            unload_name = plugin.name;

        ImGui::PopID();
    }

    if (!unload_name.empty())
    {
        if (plugin_manager_->unload_plugin(unload_name))
            plugin_panel_status_ = "Unloaded: " + unload_name;
        else
            plugin_panel_status_ = "Failed to unload: " + unload_name;
    }

    ImGui::End();
}

void ImGuiIntegration::draw_chrome_backdrops()
{
    if (!layout_manager_ || !theme_mgr_)
        return;

    ImDrawList*                   bg    = ImGui::GetBackgroundDrawList();
    const auto&                   c     = theme_colors();
    const ui::ThemeGlassSettings& glass = theme_mgr_->glass();
    const float                   glow  = theme_mgr_->effective_glow_intensity();

    // Vision.png outer halo + shell frame (strokes only — never fill the canvas rect).
    if (ImGuiViewport* vp = ImGui::GetMainViewport())
    {
        ImVec2 vp_min = vp->Pos;
        ImVec2 vp_max = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
        ui::glass_draw::draw_window_edge_glow(bg, vp_min, vp_max, glow);

        ImVec2 win_min = ImVec2(vp->Pos.x + 1.0f, vp->Pos.y + 1.0f);
        ImVec2 win_max = ImVec2(vp->Pos.x + vp->Size.x - 1.0f, vp->Pos.y + vp->Size.y - 1.0f);
        ui::glass_draw::draw_app_shell_frame(bg, win_min, win_max, glow);
    }

    auto draw_zone = [&](const Rect& zone, ui::GlassSurface surface, float rounding)
    {
        if (zone.w < 1.0f || zone.h < 1.0f)
            return;
        ui::glass_draw::draw_glass_rect(bg,
                                        ImVec2(zone.x, zone.y),
                                        ImVec2(zone.x + zone.w, zone.y + zone.h),
                                        rounding,
                                        c,
                                        glass,
                                        surface,
                                        glow);
    };

    if (command_bar_visible_)
        draw_zone(layout_manager_->command_bar_rect(), ui::GlassSurface::Toolbar, 0.0f);

    if (show_nav_rail_)
    {
        Rect nr = layout_manager_->nav_rail_rect();
        if (glow <= 0.01f)
        {
            // Dark/light: flush rail panel — no floating glass inset.
            draw_zone(nr, ui::GlassSurface::Toolbar, 0.0f);
        }
        else
        {
            const float inset = ui::tokens::PANEL_GAP + 2.0f;
            if (nr.w > inset * 2.0f + 8.0f && nr.h > inset * 2.0f + 8.0f)
            {
                draw_zone(
                    Rect{nr.x + inset, nr.y + inset, nr.w - inset * 2.0f, nr.h - inset * 2.0f},
                    ui::GlassSurface::Toolbar,
                    ui::tokens::RADIUS_LG);
            }
            else
            {
                draw_zone(nr, ui::GlassSurface::Toolbar, ui::tokens::RADIUS_LG);
            }
        }
    }

    if (layout_manager_->is_tab_bar_visible())
        draw_zone(layout_manager_->tab_bar_rect(), ui::GlassSurface::Toolbar, 0.0f);

    if (layout_manager_->is_inspector_visible())
        draw_zone(layout_manager_->inspector_rect(),
                  ui::GlassSurface::Panel,
                  ui::tokens::RADIUS_LG);

    // Status bar uses its own Vision navy + cyan top line in draw_status_bar().
}

void ImGuiIntegration::draw_nav_rail()
{
    // Adapter shells (spectra-ros, spectra-px4) draw their own nav rail in
    // extra_draw_cb_. show_nav_rail_ still reserves layout width for the rail zone.
    if (extra_draw_cb_)
        return;

    if (app_shell_)
    {
        app_shell_->draw_nav_rail();
        return;
    }

    if (!layout_manager_ || !show_nav_rail_)
        return;

    Rect bounds = layout_manager_->nav_rail_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoBackground;

    float rail_w = bounds.w;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags))
    {
        // Rail container: subtle right-edge shadow + hairline separator.
        {
            ImDrawList* dl            = ImGui::GetWindowDrawList();
            float       right_edge    = bounds.x + rail_w;
            float       shadow_spread = ui::tokens::ELEVATION_1_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.09f * (1.0f - t);
                float off   = shadow_spread * t;
                dl->AddRectFilled(ImVec2(right_edge, bounds.y),
                                  ImVec2(right_edge + off + 1.0f, bounds.y + bounds.h),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Hairline border on right edge
            dl->AddLine(ImVec2(right_edge - 1.0f, bounds.y),
                        ImVec2(right_edge - 1.0f, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors().border_subtle.r,
                                                              theme_colors().border_subtle.g,
                                                              theme_colors().border_subtle.b,
                                                              0.52f)),
                        1.0f);
        }

        ImFont* label_font = font_heading_;   // 12.5px — compact labels
        float   btn_w      = rail_w;
        float   btn_scale =
            LayoutManager::nav_rail_scale_for_height(bounds.h,
                                                     LayoutManager::NAV_RAIL_FALLBACK_BUTTON_COUNT,
                                                     LayoutManager::NAV_RAIL_SEPARATOR_COUNT);

        // Separator: subtle hairline with spacing tokens.
        auto draw_separator = [&]()
        {
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * btn_scale));
            float  sep_inset = ui::tokens::SPACE_4 * btn_scale;
            ImVec2 p0        = ImVec2(ImGui::GetWindowPos().x + sep_inset,
                               std::floor(ImGui::GetCursorScreenPos().y));
            ImVec2 p1        = ImVec2(ImGui::GetWindowPos().x + rail_w - sep_inset, p0.y);
            ImGui::GetWindowDrawList()->AddLine(p0,
                                                p1,
                                                IM_COL32(theme_colors().border_subtle.r * 255,
                                                         theme_colors().border_subtle.g * 255,
                                                         theme_colors().border_subtle.b * 255,
                                                         32),
                                                1.0f);
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * btn_scale));
        };

        // ── Tool mode helper ──
        auto tool_btn = [&](ui::Icon icon, const char* label, ToolMode mode)
        {
            bool is_active = (interaction_mode_ == mode);
            if (icon_label_button(ui::icon_str(icon),
                                  label,
                                  is_active,
                                  font_icon_,
                                  label_font,
                                  btn_w,
                                  btn_scale))
            {
                interaction_mode_ = mode;
            }
        };

        // ── Toggle helper (for panel/feature toggles) ──
        auto toggle_btn = [&](ui::Icon                     icon,
                              const char*                  label,
                              bool                         is_active,
                              const std::function<void()>& on_click)
        {
            if (icon_label_button(ui::icon_str(icon),
                                  label,
                                  is_active,
                                  font_icon_,
                                  label_font,
                                  btn_w,
                                  btn_scale))
            {
                on_click();
            }
        };

        // ── Group 1: Navigation tools ──
        tool_btn(ui::Icon::MousePointer, "Select", ToolMode::Select);
        tool_btn(ui::Icon::Hand, "Pan", ToolMode::Pan);
        tool_btn(ui::Icon::ZoomIn, "Zoom", ToolMode::BoxZoom);

        draw_separator();

        // ── Group 2: Analysis tools ──
        tool_btn(ui::Icon::Ruler, "Measure", ToolMode::Measure);
        tool_btn(ui::Icon::Comment, "Annotate", ToolMode::Annotate);
        tool_btn(ui::Icon::VectorSquare, "ROI", ToolMode::ROI);

        draw_separator();

        // ── Group 3: Data tools ──
        toggle_btn(ui::Icon::MapPin,
                   "Markers",
                   (data_interaction_ != nullptr) && !data_interaction_->markers().empty(),
                   [this]()
                   {
                       if (data_interaction_)
                           data_interaction_->clear_markers();
                   });
        toggle_btn(
            ui::Icon::MagicWand,
            "Transform",
            custom_transform_dialog_.is_open(),
            [this]()
            {
                if (!custom_transform_dialog_.is_open())
                {
                    custom_transform_dialog_.set_fonts(font_body_, font_heading_, font_title_);
                    custom_transform_dialog_.open(current_figure_);
                }
            });

        draw_separator();

        // ── Group 4: Panels ──
        toggle_btn(ui::Icon::Database,
                   "Data",
                   panel_open_ && active_section_ == Section::DataEditor,
                   [this]()
                   {
                       bool was_active = panel_open_ && active_section_ == Section::DataEditor;
                       if (was_active)
                       {
                           panel_open_ = false;
                           layout_manager_->set_inspector_visible(false);
                       }
                       else
                       {
                           active_section_ = Section::DataEditor;
                           panel_open_     = true;
                           layout_manager_->set_inspector_visible(true);
                       }
                   });
        toggle_btn(ui::Icon::Timeline,
                   "Timeline",
                   show_timeline_,
                   [this]() { show_timeline_ = !show_timeline_; });

        draw_separator();

        // ── Group 5: Utilities ──
        toggle_btn(ui::Icon::Broadcast,
                   "Topics",
                   (topics_panel_ != nullptr) && topics_panel_->is_visible(),
                   [this]()
                   {
                       if (command_registry_)
                           command_registry_->execute("panel.toggle_topics");
                   });
        toggle_btn(ui::Icon::Help,
                   "Help",
                   false,
                   [this]()
                   {
                       if (command_registry_)
                           command_registry_->execute("help.show");
                   });
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(5);
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
