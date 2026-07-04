#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/status_bar.hpp"

    #include "imgui.h"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
void StatusBar::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
}

spectra::LayoutManager* StatusBar::layout_manager() const
{
    return layout_manager_;
}

void StatusBar::add_segment(StatusSegment segment)
{
    segments_.push_back(std::move(segment));
}

void StatusBar::clear()
{
    segments_.clear();
}

void StatusBar::draw()
{
    if (!layout_manager_)
        return;

    const Rect bounds = layout_manager_->status_bar_rect();
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::STATUS_BAR_PADDING_H, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##AppShellStatusBar", nullptr, flags))
    {
        const auto& colors = ui::theme();
        ImDrawList* bar_dl = ImGui::GetWindowDrawList();
        ImVec2      wpos   = ImGui::GetWindowPos();
        ImVec2      wsz    = ImGui::GetWindowSize();
        ImVec2      wmax(wpos.x + wsz.x, wpos.y + wsz.y);

        bar_dl->AddRectFilled(wpos,
                              wmax,
                              IM_COL32(static_cast<uint8_t>(colors.bg_primary.r * 255),
                                       static_cast<uint8_t>(colors.bg_primary.g * 255),
                                       static_cast<uint8_t>(colors.bg_primary.b * 255),
                                       255));

        bar_dl->AddLine(wpos,
                        ImVec2(wmax.x, wpos.y),
                        IM_COL32(static_cast<uint8_t>(colors.border_subtle.r * 255),
                                 static_cast<uint8_t>(colors.border_subtle.g * 255),
                                 static_cast<uint8_t>(colors.border_subtle.b * 255),
                                 70),
                        1.0f);

        const float col_h = std::max(1.0f, ImGui::GetContentRegionAvail().y);

        const auto draw_column = [&](StatusAlign align, int column_id)
        {
            const float col_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            ImGui::PushID(column_id);
            ImGui::BeginChild("##status_col",
                              ImVec2(col_w, col_h),
                              ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
            bool first = true;
            for (const StatusSegment& segment : segments_)
            {
                if (segment.align != align || !segment.draw_fn)
                    continue;
                if (!first)
                    ImGui::SameLine(0.0f, ui::tokens::STATUS_BAR_GROUP_GAP);
                segment.draw_fn();
                first = false;
            }
            ImGui::EndChild();
            ImGui::PopID();
        };

        if (ImGui::BeginTable("##status_bar_tbl",
                              3,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX
                                  | ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.33f);
            ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.33f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            draw_column(StatusAlign::Left, 0);

            ImGui::TableSetColumnIndex(1);
            draw_column(StatusAlign::Center, 1);

            ImGui::TableSetColumnIndex(2);
            draw_column(StatusAlign::Right, 2);

            ImGui::EndTable();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
