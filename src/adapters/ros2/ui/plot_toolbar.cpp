#include "ui/plot_toolbar.hpp"

#ifdef SPECTRA_USE_IMGUI

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <string>

    #include <imgui.h>

    #include "ui/imgui/widgets.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::adapters::ros2
{

namespace tokens = spectra::ui::tokens;

namespace
{

using spectra::ui::Icon;
using spectra::ui::theme;
using spectra::ui::widgets::icon_button;

ImU32 color_u32(float r, float g, float b, float a)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

template <typename ColorT>
ImU32 color_u32(const ColorT& c, float alpha)
{
    return color_u32(c.r, c.g, c.b, alpha);
}

void toolbar_separator(float height)
{
    const auto& colors = theme();
    ImGui::SameLine(0.0f, tokens::SPACE_2);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 6.0f),
                                        ImVec2(pos.x, pos.y + height - 6.0f),
                                        color_u32(colors.border_subtle, 0.58f),
                                        1.0f);

    ImGui::SameLine(0.0f, tokens::SPACE_2);
}

std::string format_window_value(float seconds)
{
    if (seconds >= 3600.0f)
        return std::format("{:.1f} h", seconds / 3600.0f);
    if (seconds >= 60.0f)
    {
        const float minutes = seconds / 60.0f;
        if (minutes >= 10.0f)
            return std::format("{:.0f} min", minutes);
        return std::format("{:.1f} min", minutes);
    }
    return std::format("{:.0f} s", seconds);
}

bool labeled_icon_button(const char* cmd_id,
                         Icon        icon,
                         const char* label,
                         const char* tooltip,
                         bool        selected);

bool time_preset_button(const char* label, float seconds, float current)
{
    const bool active = std::abs(current - seconds) < 0.5f;
    return labeled_icon_button(std::format("plot.range.{}", label).c_str(),
                               Icon::Clock,
                               label,
                               std::format("Set time range to {}", label).c_str(),
                               active);
}

void toolbar_label(Icon icon, const char* label)
{
    const auto& colors = theme();

    ImFont* icon_f = spectra::ui::icon_font(tokens::ICON_XS);
    if (icon_f)
        ImGui::PushFont(icon_f);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 0.84f));
    ImGui::TextUnformatted(spectra::ui::icon_str(icon));
    ImGui::PopStyleColor();
    if (icon_f)
        ImGui::PopFont();

    ImGui::SameLine(0.0f, tokens::SPACE_1);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.90f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

bool live_button(bool live)
{
    const auto& colors = theme();
    const char* label  = live ? "Live" : "Paused";
    const Icon  icon   = live ? Icon::Pause : Icon::Play;
    const float height = 32.0f;
    const float width  = 96.0f;

    ImGui::PushID("plot.live");
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool   clicked = ImGui::InvisibleButton("##button", ImVec2(width, height));
    const bool   hovered = ImGui::IsItemHovered();
    const bool   active  = ImGui::IsItemActive();

    const auto  fill         = live ? colors.accent : colors.bg_tertiary;
    const float fill_alpha   = live ? (active    ? 0.32f
                                       : hovered ? 0.26f
                                                 : 0.20f)
                                    : (active    ? 0.90f
                                       : hovered ? 0.82f
                                                 : 0.66f);
    const auto  border       = live ? colors.accent : colors.border_subtle;
    const float border_alpha = live ? 0.62f : hovered ? 0.66f : 0.44f;
    const auto  text         = live ? colors.accent : colors.text_secondary;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + width, pos.y + height),
                      color_u32(fill, fill_alpha),
                      tokens::RADIUS_MD);
    dl->AddRect(pos,
                ImVec2(pos.x + width, pos.y + height),
                color_u32(border, border_alpha),
                tokens::RADIUS_MD,
                0,
                1.0f);

    if (live)
    {
        dl->AddCircleFilled(ImVec2(pos.x + 11.0f, pos.y + height * 0.5f),
                            3.5f,
                            color_u32(colors.accent, 0.95f));
    }

    const float icon_x = pos.x + (live ? 22.0f : 12.0f);
    const float text_x = pos.x + (live ? 42.0f : 34.0f);
    const float text_y = pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    ImFont*     icon_f = spectra::ui::icon_font(tokens::ICON_SM);
    if (icon_f)
        dl->AddText(icon_f,
                    tokens::ICON_SM,
                    ImVec2(icon_x, pos.y + 8.0f),
                    color_u32(text, 0.98f),
                    spectra::ui::icon_str(icon));
    dl->AddText(ImVec2(text_x, text_y), color_u32(text, 0.98f), label);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", live ? "Pause live scroll" : "Resume live scroll");

    ImGui::PopID();
    return clicked;
}

float labeled_icon_button_width(const char* label)
{
    return ImGui::CalcTextSize(label).x + 38.0f;
}

bool labeled_icon_button(const char* cmd_id,
                         Icon        icon,
                         const char* label,
                         const char* tooltip,
                         bool        selected = false)
{
    const auto& colors = theme();
    const float height = 32.0f;
    const float width  = labeled_icon_button_width(label);

    ImGui::PushID(cmd_id);
    const ImVec2 pos     = ImGui::GetCursorScreenPos();
    const bool   clicked = ImGui::InvisibleButton("##button", ImVec2(width, height));
    const bool   hovered = ImGui::IsItemHovered();
    const bool   active  = ImGui::IsItemActive();

    const auto  fill         = selected ? colors.accent : colors.bg_tertiary;
    const float fill_alpha   = selected ? (active    ? 0.32f
                                           : hovered ? 0.26f
                                                     : 0.18f)
                                        : (active    ? 0.72f
                                           : hovered ? 0.58f
                                                     : 0.34f);
    const auto  border       = selected ? colors.accent : colors.border_subtle;
    const float border_alpha = selected ? 0.60f : hovered ? 0.58f : 0.34f;
    const auto  text         = selected ? colors.accent : colors.text_secondary;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + width, pos.y + height),
                      color_u32(fill, fill_alpha),
                      tokens::RADIUS_MD);
    dl->AddRect(pos,
                ImVec2(pos.x + width, pos.y + height),
                color_u32(border, border_alpha),
                tokens::RADIUS_MD,
                0,
                1.0f);

    ImFont* icon_f = spectra::ui::icon_font(tokens::ICON_SM);
    if (icon_f)
        dl->AddText(icon_f,
                    tokens::ICON_SM,
                    ImVec2(pos.x + 10.0f, pos.y + 8.0f),
                    color_u32(text, selected ? 0.98f : 0.84f),
                    spectra::ui::icon_str(icon));

    const ImVec2 text_sz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + 30.0f, pos.y + (height - text_sz.y) * 0.5f),
                color_u32(text, selected ? 0.98f : 0.90f),
                label);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);

    ImGui::PopID();
    return clicked;
}

void draw_value_pill(const std::string& value)
{
    const auto&  colors  = theme();
    const ImVec2 text_sz = ImGui::CalcTextSize(value.c_str());
    const float  width   = text_sz.x + tokens::SPACE_3 * 2.0f;
    const float  height  = 32.0f;
    const ImVec2 pos     = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + width, pos.y + height),
                      color_u32(colors.bg_tertiary, 0.72f),
                      tokens::RADIUS_MD);
    dl->AddRect(pos,
                ImVec2(pos.x + width, pos.y + height),
                color_u32(colors.border_subtle, 0.44f),
                tokens::RADIUS_MD);
    dl->AddText(ImVec2(pos.x + tokens::SPACE_3, pos.y + (height - text_sz.y) * 0.5f),
                color_u32(colors.text_primary, 0.92f),
                value.c_str());
}

}   // namespace

float draw_plot_toolbar(PlotToolbarState& state, const PlotToolbarActions& actions)
{
    using spectra::ui::Icon;
    using spectra::ui::theme;
    using spectra::ui::widgets::icon_button;
    const auto& colors            = theme();
    const float avail_w           = ImGui::GetContentRegionAvail().x;
    const bool  tight             = avail_w < 480.0f;
    const bool  compact           = avail_w < 1120.0f;
    const float content_left      = ImGui::GetWindowContentRegionMin().x;
    const float compact_cluster_w = tokens::ICON_BUTTON_HITBOX * 2.0f + tokens::TOOLBAR_BUTTON_GAP;
    const float cluster_x         = ImGui::GetWindowContentRegionMax().x - compact_cluster_w;
    const float tool_btn_w =
        tight ? tokens::ICON_BUTTON_HITBOX : labeled_icon_button_width(compact ? "Fit" : "Fit Y");
    const float tools_row_w = tool_btn_w * 5.0f + tokens::TOOLBAR_BUTTON_GAP * 4.0f;
    const bool  tools_overflow_cluster =
        compact && (content_left + tools_row_w > cluster_x - tokens::SPACE_2);
    const float tools_row_y_offset = tight ? 74.0f : 42.0f;
    const float export_row_y_offset =
        tools_overflow_cluster ? tools_row_y_offset + 36.0f : tools_row_y_offset;
    const float toolbar_h = compact ? (export_row_y_offset + 38.0f) : 44.0f;

    ImVec2      bar_min      = ImGui::GetCursorScreenPos();
    const float tools_row_y  = bar_min.y + tools_row_y_offset;
    const float export_row_y = bar_min.y + export_row_y_offset;
    ImVec2      bar_max(bar_min.x + avail_w, bar_min.y + toolbar_h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(bar_min, bar_max, color_u32(colors.bg_elevated, 0.94f), tokens::RADIUS_LG);
    dl->AddRect(bar_min, bar_max, color_u32(colors.border_subtle, 0.58f), tokens::RADIUS_LG);
    dl->AddLine(ImVec2(bar_min.x + 1.0f, bar_min.y + 1.0f),
                ImVec2(bar_max.x - 1.0f, bar_min.y + 1.0f),
                color_u32(colors.text_primary, 0.05f),
                1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tokens::SPACE_2, tokens::SPACE_1));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tokens::SPACE_1, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(bar_min.x + tokens::SPACE_2, bar_min.y + 6.0f));

    if (live_button(state.live))
    {
        state.live = !state.live;
        if (actions.set_live)
            actions.set_live(state.live);
    }

    toolbar_separator(32.0f);

    if (tight)
    {
        const float slider_w_tight =
            std::max(72.0f, avail_w - tokens::ICON_BUTTON_HITBOX - tokens::SPACE_8);
        ImGui::SetNextItemWidth(slider_w_tight);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_PILL);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, tokens::RADIUS_PILL);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.72f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.95f));
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrabActive,
            ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f));
        if (ImGui::SliderFloat("##plot_tw",
                               &state.time_window_s,
                               1.0f,
                               3600.0f,
                               "%.0fs",
                               ImGuiSliderFlags_Logarithmic))
        {
            if (actions.set_time_window)
                actions.set_time_window(state.time_window_s);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(bar_min.x + tokens::SPACE_2, bar_min.y + 38.0f));
    }

    ImGui::AlignTextToFramePadding();
    toolbar_label(Icon::Clock, "Range");
    ImGui::SameLine(0.0f, tokens::SPACE_2);

    static const struct
    {
        const char* label;
        float       sec;
    } kPresets[] = {{"10s", 10.f}, {"30s", 30.f}, {"60s", 60.f}, {"5m", 300.f}};

    const float preset_right = ImGui::GetWindowContentRegionMax().x - 2.0f;
    for (size_t i = 0; i < std::size(kPresets); ++i)
    {
        const auto& p        = kPresets[i];
        const float preset_w = labeled_icon_button_width(p.label);
        if (i > 0)
        {
            const float next_x = ImGui::GetCursorPosX() + tokens::SPACE_1 + preset_w;
            if (next_x <= preset_right)
                ImGui::SameLine(0.0f, tokens::SPACE_1);
        }
        if (time_preset_button(p.label, p.sec, state.time_window_s))
        {
            state.time_window_s = p.sec;
            if (actions.set_time_window)
                actions.set_time_window(state.time_window_s);
        }
    }

    if (!tight)
    {
        const float slider_w = std::clamp(avail_w * (compact ? 0.11f : 0.18f),
                                          compact ? 88.0f : 112.0f,
                                          compact ? 132.0f : 220.0f);
        ImGui::SameLine(0.0f, tokens::SPACE_1);
        ImGui::SetNextItemWidth(slider_w);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_PILL);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, tokens::RADIUS_PILL);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.72f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.95f));
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrabActive,
            ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f));
        if (ImGui::SliderFloat("##plot_tw",
                               &state.time_window_s,
                               1.0f,
                               3600.0f,
                               "%.0fs",
                               ImGuiSliderFlags_Logarithmic))
        {
            if (actions.set_time_window)
                actions.set_time_window(state.time_window_s);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Visible time window (seconds)");
        if (!compact && avail_w >= 780.0f)
        {
            ImGui::SameLine(0.0f, tokens::SPACE_1);
            draw_value_pill(format_window_value(state.time_window_s));
        }
    }

    if (compact)
    {
        ImGui::SetCursorScreenPos(ImVec2(bar_min.x + tokens::SPACE_2, tools_row_y));
    }
    else
    {
        toolbar_separator(32.0f);
    }

    if (!compact)
    {
        toolbar_label(Icon::Axes, "Tools");
        ImGui::SameLine(0.0f, tokens::SPACE_2);
    }

    auto draw_tool = [&](const char* id,
                         Icon        icon,
                         const char* compact_label,
                         const char* full_label,
                         const char* tooltip,
                         bool        selected,
                         auto&&      on_click)
    {
        bool clicked = false;
        if (tight)
            clicked = icon_button(id, icon, tooltip, selected);
        else
            clicked = labeled_icon_button(id,
                                          icon,
                                          compact ? compact_label : full_label,
                                          tooltip,
                                          selected);
        if (clicked)
            on_click();
        ImGui::SameLine(0.0f, tokens::TOOLBAR_BUTTON_GAP);
    };

    draw_tool("plot.autofit",
              Icon::Maximize,
              "Fit",
              "Fit Y",
              "Auto-fit Y axis",
              false,
              [&]()
              {
                  if (actions.autofit)
                      actions.autofit();
              });
    draw_tool("plot.clear",
              Icon::Trash,
              "Clear",
              "Clear",
              "Clear plot data",
              false,
              [&]()
              {
                  if (actions.clear_plot)
                      actions.clear_plot();
              });
    draw_tool("plot.add_sub",
              Icon::Plus,
              "Add",
              "Add row",
              "Add subplot row",
              false,
              [&]()
              {
                  if (actions.add_subplot)
                      actions.add_subplot();
              });
    draw_tool("plot.remove_sub",
              Icon::Minus,
              "Remove",
              "Remove row",
              "Remove last subplot row",
              false,
              [&]()
              {
                  if (actions.remove_subplot)
                      actions.remove_subplot();
              });
    draw_tool("plot.link_x",
              state.x_links_enabled ? Icon::Link : Icon::Unlink,
              "Link",
              "Link X",
              state.x_links_enabled ? "Unlink X axes" : "Link X axes",
              state.x_links_enabled,
              [&]()
              {
                  state.x_links_enabled = !state.x_links_enabled;
                  if (actions.set_x_links)
                      actions.set_x_links(state.x_links_enabled);
              });

    const float settings_w       = tokens::ICON_BUTTON_HITBOX;
    const float export_label_w   = labeled_icon_button_width("Export");
    const float wide_cluster_w   = export_label_w + tokens::TOOLBAR_BUTTON_GAP + settings_w;
    const float cluster_w        = compact ? compact_cluster_w : wide_cluster_w;
    const float export_cluster_x = ImGui::GetWindowContentRegionMax().x - cluster_w;
    const bool  export_fits_right =
        !compact && export_cluster_x > ImGui::GetCursorPosX() + tokens::SPACE_2;

    if (compact)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + export_cluster_x, export_row_y));
    }
    else if (export_fits_right)
    {
        ImGui::SameLine(export_cluster_x);
    }
    else
    {
        ImGui::SameLine(0.0f, tokens::TOOLBAR_BUTTON_GAP);
    }

    if (compact || !export_fits_right)
    {
        if (icon_button("plot.export", Icon::Export, "Export plot"))
            ImGui::OpenPopup("##plot_export_popup");
    }
    else if (labeled_icon_button("plot.export", Icon::Export, "Export", "Export plot"))
    {
        ImGui::OpenPopup("##plot_export_popup");
    }

    if (ImGui::BeginPopup("##plot_export_popup"))
    {
        if (ImGui::MenuItem("Screenshot PNG"))
        {
            if (actions.export_screenshot)
                actions.export_screenshot();
        }
        if (ImGui::MenuItem("Record video..."))
        {
            if (actions.export_video)
                actions.export_video();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, tokens::TOOLBAR_BUTTON_GAP);
    if (icon_button("plot.settings", Icon::Settings, "Plot settings"))
        ImGui::OpenPopup("##plot_settings_popup");

    if (ImGui::BeginPopup("##plot_settings_popup"))
    {
        if (state.active_slot >= 1)
            ImGui::TextDisabled("Plot %d settings", state.active_slot);
        else
            ImGui::TextDisabled("All plots settings");
        ImGui::Separator();
        if (ImGui::Checkbox("Prune old samples", &state.pruning_enabled))
        {
            if (actions.set_pruning)
                actions.set_pruning(state.pruning_enabled);
        }
        if (!state.pruning_enabled)
            ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Prune buffer", &state.prune_buffer_s, 0.0f, 600.0f, "%.0f s"))
        {
            if (actions.set_prune_buffer)
                actions.set_prune_buffer(state.prune_buffer_s);
        }
        if (!state.pruning_enabled)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_max.y + tokens::SPACE_2));
    return toolbar_h + tokens::SPACE_2;
}

}   // namespace spectra::adapters::ros2

#endif   // SPECTRA_USE_IMGUI
