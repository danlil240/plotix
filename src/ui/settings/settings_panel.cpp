#include "settings_panel.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <cctype>
    #include <cfloat>
    #include <string>
    #include <unordered_map>

    #include <imgui.h>

    #include "settings_store.hpp"
    #include "ui/commands/shortcut_config.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::settings
{

namespace
{

std::string build_capture_shortcut(ImGuiKey key, bool ctrl, bool shift, bool alt)
{
    std::string s;
    if (ctrl)
        s += "Ctrl+";
    if (shift)
        s += "Shift+";
    if (alt)
        s += "Alt+";

    if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
    {
        s += static_cast<char>('A' + (key - ImGuiKey_A));
        return s;
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
    {
        s += static_cast<char>('0' + (key - ImGuiKey_0));
        return s;
    }
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12)
    {
        s += "F" + std::to_string(key - ImGuiKey_F1 + 1);
        return s;
    }
    if (key == ImGuiKey_Space)
    {
        s += "Space";
        return s;
    }
    if (key == ImGuiKey_Enter)
    {
        s += "Enter";
        return s;
    }
    if (key == ImGuiKey_Tab)
    {
        s += "Tab";
        return s;
    }
    if (key == ImGuiKey_Backspace)
    {
        s += "Backspace";
        return s;
    }
    if (key == ImGuiKey_Delete)
    {
        s += "Delete";
        return s;
    }
    if (key == ImGuiKey_Insert)
    {
        s += "Insert";
        return s;
    }
    if (key == ImGuiKey_LeftArrow)
    {
        s += "Left";
        return s;
    }
    if (key == ImGuiKey_RightArrow)
    {
        s += "Right";
        return s;
    }
    if (key == ImGuiKey_UpArrow)
    {
        s += "Up";
        return s;
    }
    if (key == ImGuiKey_DownArrow)
    {
        s += "Down";
        return s;
    }
    if (key == ImGuiKey_PageUp)
    {
        s += "PageUp";
        return s;
    }
    if (key == ImGuiKey_PageDown)
    {
        s += "PageDown";
        return s;
    }
    if (key == ImGuiKey_Home)
    {
        s += "Home";
        return s;
    }
    if (key == ImGuiKey_End)
    {
        s += "End";
        return s;
    }

    return {};
}

static const ImGuiKey k_capturable_keys[] = {
    ImGuiKey_A,       ImGuiKey_B,         ImGuiKey_C,         ImGuiKey_D,
    ImGuiKey_E,       ImGuiKey_F,         ImGuiKey_G,         ImGuiKey_H,
    ImGuiKey_I,       ImGuiKey_J,         ImGuiKey_K,         ImGuiKey_L,
    ImGuiKey_M,       ImGuiKey_N,         ImGuiKey_O,         ImGuiKey_P,
    ImGuiKey_Q,       ImGuiKey_R,         ImGuiKey_S,         ImGuiKey_T,
    ImGuiKey_U,       ImGuiKey_V,         ImGuiKey_W,         ImGuiKey_X,
    ImGuiKey_Y,       ImGuiKey_Z,         ImGuiKey_0,         ImGuiKey_1,
    ImGuiKey_2,       ImGuiKey_3,         ImGuiKey_4,         ImGuiKey_5,
    ImGuiKey_6,       ImGuiKey_7,         ImGuiKey_8,         ImGuiKey_9,
    ImGuiKey_F1,      ImGuiKey_F2,        ImGuiKey_F3,        ImGuiKey_F4,
    ImGuiKey_F5,      ImGuiKey_F6,        ImGuiKey_F7,        ImGuiKey_F8,
    ImGuiKey_F9,      ImGuiKey_F10,       ImGuiKey_F11,       ImGuiKey_F12,
    ImGuiKey_Space,   ImGuiKey_Enter,     ImGuiKey_Tab,       ImGuiKey_Backspace,
    ImGuiKey_Delete,  ImGuiKey_Insert,    ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
    ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp,    ImGuiKey_PageDown,
    ImGuiKey_Home,    ImGuiKey_End,
};

}   // namespace

// ─── SettingsPanel::draw ─────────────────────────────────────────────────────

void SettingsPanel::draw()
{
    if (!visible_)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(FLT_MAX, 900.0f));

    // Push fully-opaque window background so the welcome screen does not bleed through.
    const auto& colors = ui::theme();
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.98f));

    const bool open = ImGui::Begin("Settings##spectra",
                                   &visible_,
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleColor();
    if (!open)
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##settings_tabs"))
    {
        // Apply any programmatic tab switch (e.g. from automation select_tab()).
        // ImGuiTabItemFlags_SetSelected fires TabBarQueueFocus on the matching tab,
        // which switches the visible tab on this same frame.
        const auto tab_flag = [&](int idx) -> ImGuiTabItemFlags
        {
            if (pending_tab_ == idx)
            {
                pending_tab_ = -1;
                return ImGuiTabItemFlags_SetSelected;
            }
            return ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("Appearance", nullptr, tab_flag(0)))
        {
            draw_appearance_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shortcuts", nullptr, tab_flag(1)))
        {
            draw_shortcuts_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("UI Defaults", nullptr, tab_flag(2)))
        {
            draw_ui_defaults_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ─── Appearance tab ───────────────────────────────────────────────────────────

void SettingsPanel::draw_appearance_tab()
{
    if (!store_ || !theme_mgr_)
        return;

    auto& d = store_->data_mut();

    static const char* k_themes[]   = {"night", "dark", "light", "high_contrast"};
    static const char* k_palettes[] = {"default",
                                       "colorblind",
                                       "tol_bright",
                                       "tol_muted",
                                       "ibm",
                                       "wong",
                                       "viridis",
                                       "monochrome"};

    ImGui::Spacing();

    const float label_w =
        std::max(ImGui::CalcTextSize("Theme").x, ImGui::CalcTextSize("Data Palette").x)
        + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Theme");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##theme", d.default_theme.c_str()))
    {
        for (const char* t : k_themes)
        {
            bool selected = (d.default_theme == t);
            if (ImGui::Selectable(t, selected))
            {
                d.default_theme = t;
                theme_mgr_->set_theme(t);
                theme_mgr_->reset_glass_defaults();
                theme_mgr_->save_current_as_default();
                store_->notify_change();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Data Palette");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##palette", d.default_data_palette.c_str()))
    {
        for (const char* p : k_palettes)
        {
            const bool cvd_safe = theme_mgr_->get_data_palette(p).colorblind_safe;
            const bool selected = (d.default_data_palette == p);
            // Append CVD badge so colorblind users can identify safe choices without
            // relying on prior knowledge of palette names (WCAG 1.3.3 / 1.4.1).
            std::string label = std::string(p) + (cvd_safe ? "  (CVD-safe)" : "  (not CVD-safe)");
            if (ImGui::Selectable(label.c_str(), selected))
            {
                d.default_data_palette = p;
                theme_mgr_->set_data_palette(p);
                store_->notify_change();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Glass / Transparency");
    ImGui::Spacing();

    ui::ThemeGlassSettings glass         = theme_mgr_->glass();
    bool                   glass_changed = false;

    auto glass_slider = [&](const char* label, float* value)
    {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##v", value, 0.0f, 1.0f, "%.2f"))
            glass_changed = true;
        ImGui::PopID();
    };

    glass_slider("Master Glass Intensity", &glass.master_intensity);
    glass_slider("Panel Transparency", &glass.panel_alpha);
    glass_slider("Plot Background Transparency", &glass.plot_alpha);
    glass_slider("Toolbar Transparency", &glass.toolbar_alpha);
    glass_slider("Glow Strength", &glass.glow_strength);

    if (ImGui::Checkbox("Blur Enabled (experimental)", &glass.blur_enabled))
        glass_changed = true;

    if (glass_changed)
    {
        theme_mgr_->set_glass_settings(glass, true);
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }

    if (ImGui::Button("Reset Theme Defaults"))
    {
        theme_mgr_->reset_glass_defaults();
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Reset to Defaults"))
    {
        d.default_theme        = "night";
        d.default_data_palette = "default";
        theme_mgr_->set_theme("night");
        theme_mgr_->set_data_palette("default");
        theme_mgr_->reset_glass_defaults();
        theme_mgr_->save_current_as_default();
        store_->notify_change();
    }
}

// ─── Shortcuts tab ────────────────────────────────────────────────────────────

void SettingsPanel::draw_shortcuts_tab()
{
    if (!shortcut_cfg_)
        return;

    auto* mgr = shortcut_cfg_->shortcut_manager();
    if (!mgr)
        return;

    ImGui::Spacing();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("Filter##sc_filter",
                             "type to filter by name...",
                             filter_buf_,
                             sizeof(filter_buf_));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Filter shortcuts by command name.\n"
                          "Tab moves between rows; use the filter to jump to a command.");
    }
    ImGui::Spacing();

    const auto& colors   = ui::theme();
    auto        bindings = mgr->all_bindings();

    // Count shortcut strings to detect conflicts.
    std::unordered_map<std::string, int> shortcut_count;
    for (const auto& b : bindings)
    {
        if (b.shortcut.valid())
            shortcut_count[b.shortcut.to_string()]++;
    }

    std::string filter(filter_buf_);
    for (char& c : filter)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg;

    ImVec2 table_size(0.0f, ImGui::GetContentRegionAvail().y - 40.0f);
    if (ImGui::BeginTable("##shortcuts", 3, kTableFlags, table_size))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableHeadersRow();

        auto& io = ImGui::GetIO();

        for (const auto& b : bindings)
        {
            if (!filter.empty())
            {
                std::string lower_id = b.command_id;
                for (char& c : lower_id)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower_id.find(filter) == std::string::npos)
                    continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(b.command_id.c_str());

            ImGui::TableSetColumnIndex(1);

            bool is_capturing = capturing_shortcut_ && capturing_command_id_ == b.command_id;
            if (is_capturing)
            {
                ImGui::TextColored(
                    ImVec4(colors.warning.r, colors.warning.g, colors.warning.b, 1.0f),
                    "Press key...  (Esc = cancel)");

                // Detect any non-modifier key press.
                for (ImGuiKey key : k_capturable_keys)
                {
                    if (!ImGui::IsKeyPressed(key, false))
                        continue;

                    std::string new_sc =
                        build_capture_shortcut(key, io.KeyCtrl, io.KeyShift, io.KeyAlt);
                    if (!new_sc.empty())
                    {
                        shortcut_cfg_->set_override(b.command_id, new_sc);
                        shortcut_cfg_->apply_overrides();
                        shortcut_cfg_->save(ShortcutConfig::default_path());
                    }
                    capturing_shortcut_ = false;
                    break;
                }

                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    capturing_shortcut_ = false;
            }
            else
            {
                std::string sc_str   = b.shortcut.valid() ? b.shortcut.to_string() : "(none)";
                bool        conflict = b.shortcut.valid()
                                && shortcut_count.contains(b.shortcut.to_string())
                                && shortcut_count.at(b.shortcut.to_string()) > 1;

                if (conflict)
                {
                    // Use theme error token (not hardcoded) and prepend "[!]" so
                    // the conflict is communicated by shape as well as color (WCAG 1.4.1).
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(colors.error.r, colors.error.g, colors.error.b, 1.0f));
                    sc_str.insert(0, "[!] ");
                }
                ImGui::TextUnformatted(sc_str.c_str());
                if (conflict)
                    ImGui::PopStyleColor();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(b.command_id.c_str());

            if (!capturing_shortcut_)
            {
                if (ImGui::SmallButton("Edit"))
                {
                    capturing_shortcut_   = true;
                    capturing_command_id_ = b.command_id;
                }
                if (shortcut_cfg_->has_override(b.command_id))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        shortcut_cfg_->remove_override(b.command_id);
                        shortcut_cfg_->apply_overrides();
                        shortcut_cfg_->save(ShortcutConfig::default_path());
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove custom shortcut override");
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset All Shortcuts"))
    {
        shortcut_cfg_->reset_all();
        shortcut_cfg_->apply_overrides();
        shortcut_cfg_->save(ShortcutConfig::default_path());
    }
}

// ─── UI Defaults tab ──────────────────────────────────────────────────────────

void SettingsPanel::draw_ui_defaults_tab()
{
    if (!store_)
        return;

    auto& d = store_->data_mut();

    ImGui::Spacing();

    if (ImGui::Checkbox("Show Inspector by default", &d.inspector_visible))
        store_->notify_change();

    ImGui::Spacing();

    if (ImGui::Checkbox("Show Navigation Rail by default", &d.nav_rail_visible))
        store_->notify_change();

    ImGui::Spacing();

    if (ImGui::Checkbox("Show Timeline by default", &d.timeline_visible))
        store_->notify_change();
}

}   // namespace spectra::ui::settings

#endif   // SPECTRA_USE_IMGUI
