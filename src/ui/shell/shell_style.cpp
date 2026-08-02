#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/shell_style.hpp"

    #include "imgui.h"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
namespace
{
constexpr int kPanelStyleVars   = 4;
constexpr int kPanelStyleColors = 3;
constexpr int kDockStyleVars    = 3;
constexpr int kDockStyleColors  = 5;
constexpr int kTableStyleVars   = 2;
constexpr int kTableStyleColors = 2;
}   // namespace

bool begin_panel(const char* title, bool* p_open, ImGuiWindowFlags extra_flags)
{
    const auto& colors = ui::theme();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::PANEL_PADDING, ui::tokens::SPACE_4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.02f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.96f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.55f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.98f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | extra_flags;
    return ImGui::Begin(title, p_open, flags);
}

void end_panel()
{
    ImGui::End();
    ImGui::PopStyleColor(kPanelStyleColors);
    ImGui::PopStyleVar(kPanelStyleVars);
}

void push_dock_style()
{
    const auto& colors = ui::theme();

    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, ui::tokens::TAB_BAR_RADIUS);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 1.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(ui::tokens::TAB_BAR_HORIZONTAL_PADDING * 0.45f, ui::tokens::SPACE_2));

    const ImVec4 tab_idle =
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.82f);
    const ImVec4 tab_hover =
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.95f);
    const ImVec4 tab_selected =
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Tab, tab_idle);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, tab_hover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, tab_selected);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, tab_idle);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, tab_selected);
}

void pop_dock_style()
{
    ImGui::PopStyleColor(kDockStyleColors);
    ImGui::PopStyleVar(kDockStyleVars);
}

void push_data_table_style()
{
    const auto& colors = ui::theme();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(ui::tokens::ROW_PADDING_H, ui::tokens::ROW_PADDING_V + 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_1));

    ImGui::PushStyleColor(
        ImGuiCol_TableHeaderBg,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.95f));
    ImGui::PushStyleColor(
        ImGuiCol_TableRowBgAlt,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.35f));
}

void pop_data_table_style()
{
    ImGui::PopStyleColor(kTableStyleColors);
    ImGui::PopStyleVar(kTableStyleVars);
}

}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
