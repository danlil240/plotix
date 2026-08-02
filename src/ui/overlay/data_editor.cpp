#ifdef SPECTRA_USE_IMGUI

    #include "data_editor.hpp"

    #include <algorithm>
    #include <cstring>
    #include <format>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <spectra/series3d.hpp>

    #include "imgui.h"

    #include "ui/imgui/widgets.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui
{

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

namespace
{

// Detect if an AxesBase is 3D
bool is_axes_3d(AxesBase* ab)
{
    return dynamic_cast<Axes3D*>(ab) != nullptr;
}

// Get series type label
const char* series_type_label(Series* s)
{
    if (dynamic_cast<LineSeries*>(s))
        return "Line";
    if (dynamic_cast<ScatterSeries*>(s))
        return "Scatter";
    if (dynamic_cast<LineSeries3D*>(s))
        return "Line3D";
    if (dynamic_cast<ScatterSeries3D*>(s))
        return "Scatter3D";
    if (dynamic_cast<SurfaceSeries*>(s))
        return "Surface";
    if (dynamic_cast<MeshSeries*>(s))
        return "Mesh";
    return "Unknown";
}

// Get point count for any series type
size_t get_point_count(Series* s)
{
    if (auto* ls = dynamic_cast<LineSeries*>(s))
        return ls->point_count();
    if (auto* ss = dynamic_cast<ScatterSeries*>(s))
        return ss->point_count();
    if (auto* ls3 = dynamic_cast<LineSeries3D*>(s))
        return ls3->point_count();
    if (auto* ss3 = dynamic_cast<ScatterSeries3D*>(s))
        return ss3->point_count();
    return 0;
}

// Check if series has 3D data
bool is_series_3d(Series* s)
{
    return dynamic_cast<LineSeries3D*>(s) != nullptr
           || dynamic_cast<ScatterSeries3D*>(s) != nullptr;
}

// Build a de-duplicated list of all axes (2D from axes_, 3D from all_axes_)
std::vector<AxesBase*> build_unified_axes(Figure& figure)
{
    std::vector<AxesBase*> result;
    for (auto& ax : figure.axes_mut())
    {
        if (ax)
            result.push_back(ax.get());
    }
    for (auto& ax : figure.all_axes_mut())
    {
        if (ax)
        {
            bool already = false;
            for (auto* u : result)
            {
                if (u == ax.get())
                {
                    already = true;
                    break;
                }
            }
            if (!already)
                result.push_back(ax.get());
        }
    }
    return result;
}

}   // namespace

// ────────────────────────────────────────────────────────────────────────────
// Public
// ────────────────────────────────────────────────────────────────────────────

void DataEditor::set_fonts(ImFont* body, ImFont* heading, ImFont* title)
{
    font_body_    = body;
    font_heading_ = heading;
    font_title_   = title;
}

void DataEditor::set_highlighted_point(const Series* series, size_t point_index)
{
    highlighted_series_      = series;
    highlighted_point_index_ = point_index;
}

void DataEditor::draw(Figure& figure)
{
    // Title
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::TextUnformatted("Data Editor");
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();
    widgets::separator();
    widgets::section_spacing();

    // Build a unified list of axes pointers (2D from axes_, 3D from all_axes_)
    std::vector<AxesBase*> unified_axes = build_unified_axes(figure);

    // Axes selector
    draw_axes_selector(figure);

    if (unified_axes.empty())
    {
        ImGui::TextColored(ImVec4(theme().text_secondary.r,
                                  theme().text_secondary.g,
                                  theme().text_secondary.b,
                                  0.7f),
                           "No axes in this figure.");
        return;
    }

    int axes_idx = selected_axes_;
    if (axes_idx < 0 || axes_idx >= static_cast<int>(unified_axes.size()))
        axes_idx = 0;

    AxesBase* ab = unified_axes[axes_idx];
    if (!ab)
        return;

    widgets::small_spacing();

    // Series selector
    draw_series_selector(*ab);

    widgets::small_spacing();
    widgets::separator();
    widgets::section_spacing();

    // Draw data tables
    auto& series_vec = ab->series();
    if (series_vec.empty())
    {
        ImGui::TextColored(ImVec4(theme().text_secondary.r,
                                  theme().text_secondary.g,
                                  theme().text_secondary.b,
                                  0.7f),
                           "No series in this axes.");
        return;
    }

    bool show_3d = is_axes_3d(ab);

    if (selected_series_ >= 0 && selected_series_ < static_cast<int>(series_vec.size()))
    {
        // Single series
        Series* s = series_vec[selected_series_].get();
        if (s)
        {
            if (show_3d || is_series_3d(s))
                draw_data_table_3d(*s, selected_series_);
            else
                draw_data_table_2d(*s, selected_series_);
        }
    }
    else
    {
        // All series
        for (int i = 0; i < static_cast<int>(series_vec.size()); ++i)
        {
            Series* s = series_vec[i].get();
            if (!s)
                continue;

            // Series header
            const char* lbl  = s->label().empty() ? "Unnamed" : s->label().c_str();
            const char* type = series_type_label(s);

            const std::string header_buf =
                std::format("{} ({}) [{} pts]##series_{}", lbl, type, get_point_count(s), i);

            bool sec_open = true;
            if (widgets::section_header(header_buf.c_str(), &sec_open, font_heading_))
            {
                if (widgets::begin_animated_section(header_buf.c_str()))
                {
                    if (show_3d || is_series_3d(s))
                        draw_data_table_3d(*s, i);
                    else
                        draw_data_table_2d(*s, i);
                    widgets::end_animated_section();
                }
            }

            widgets::small_spacing();
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Axes selector
// ────────────────────────────────────────────────────────────────────────────

void DataEditor::draw_axes_selector(Figure& figure)
{
    std::vector<AxesBase*> unified = build_unified_axes(figure);

    if (unified.size() <= 1)
        return;   // No need for selector with single axes

    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::TextColored(
        ImVec4(theme().text_secondary.r, theme().text_secondary.g, theme().text_secondary.b, 1.0f),
        "SUBPLOT");
    if (font_heading_)
        ImGui::PopFont();

    widgets::small_spacing();

    int current = (selected_axes_ >= 0 && selected_axes_ < static_cast<int>(unified.size()))
                      ? selected_axes_
                      : 0;

    // Build combo items
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tokens::SPACE_3, tokens::SPACE_2));

    ImGui::SetNextItemWidth(-1);
    std::string preview;
    {
        AxesBase*   ab    = unified[current];
        bool        is_3d = is_axes_3d(ab);
        const char* title = ab->title().empty() ? "Untitled" : ab->title().c_str();
        preview           = std::format("Axes {}: {}{}", current + 1, title, is_3d ? " (3D)" : "");
    }

    if (ImGui::BeginCombo("##axes_select", preview.c_str()))
    {
        for (int i = 0; i < static_cast<int>(unified.size()); ++i)
        {
            AxesBase*   ab    = unified[i];
            bool        is_3d = is_axes_3d(ab);
            const char* title = ab->title().empty() ? "Untitled" : ab->title().c_str();

            const std::string item_buf = std::format("Axes {}: {}{} ({} series)",
                                                     i + 1,
                                                     title,
                                                     is_3d ? " (3D)" : "",
                                                     ab->series().size());

            bool selected = (i == current);
            if (ImGui::Selectable(item_buf.c_str(), selected))
            {
                selected_axes_   = i;
                selected_series_ = -1;   // Reset series selection
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(2);
}

// ────────────────────────────────────────────────────────────────────────────
// Series selector
// ────────────────────────────────────────────────────────────────────────────

void DataEditor::draw_series_selector(AxesBase& axes)
{
    auto& series_vec = axes.series();
    if (series_vec.empty())
        return;

    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::TextColored(
        ImVec4(theme().text_secondary.r, theme().text_secondary.g, theme().text_secondary.b, 1.0f),
        "SERIES");
    if (font_heading_)
        ImGui::PopFont();

    widgets::small_spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tokens::SPACE_3, tokens::SPACE_2));

    int current = selected_series_;

    std::string preview;
    if (current >= 0 && current < static_cast<int>(series_vec.size()))
    {
        Series*     s   = series_vec[current].get();
        const char* lbl = (s && !s->label().empty()) ? s->label().c_str() : "Unnamed";
        preview         = std::format("{} ({})", lbl, series_type_label(s));
    }
    else
    {
        preview = std::format("All Series ({})", series_vec.size());
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##series_select", preview.c_str()))
    {
        // "All" option
        {
            bool              selected = (current < 0);
            const std::string all_buf  = std::format("All Series ({})", series_vec.size());
            if (ImGui::Selectable(all_buf.c_str(), selected))
                selected_series_ = -1;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(series_vec.size()); ++i)
        {
            Series* s = series_vec[i].get();
            if (!s)
                continue;
            const char* lbl = s->label().empty() ? "Unnamed" : s->label().c_str();

            // Color swatch before label
            auto   c = s->color();
            ImVec4 col(c.r, c.g, c.b, c.a);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted("\xe2\x96\x88");   // Unicode full block
            ImGui::PopStyleColor();
            ImGui::SameLine();

            const std::string item_buf = std::format("{} ({}, {} pts)##s_{}",
                                                     lbl,
                                                     series_type_label(s),
                                                     get_point_count(s),
                                                     i);

            bool selected = (i == current);
            if (ImGui::Selectable(item_buf.c_str(), selected))
                selected_series_ = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(2);
}

// ────────────────────────────────────────────────────────────────────────────
// 2D Data Table (X, Y columns)
// ────────────────────────────────────────────────────────────────────────────

void DataEditor::draw_data_table_2d(Series& series, int series_idx)
{
    // Get data spans
    std::span<const float> x_data;
    std::span<const float> y_data;

    if (auto* ls = dynamic_cast<LineSeries*>(&series))
    {
        x_data = ls->x_data();
        y_data = ls->y_data();
    }
    else if (auto* ss = dynamic_cast<ScatterSeries*>(&series))
    {
        x_data = ss->x_data();
        y_data = ss->y_data();
    }
    else
    {
        ImGui::TextColored(ImVec4(theme().text_secondary.r,
                                  theme().text_secondary.g,
                                  theme().text_secondary.b,
                                  0.7f),
                           "Tabular view not available for this series type.");
        return;
    }

    size_t count = std::min(x_data.size(), y_data.size());
    if (count == 0)
    {
        ImGui::TextDisabled("Empty series");
        return;
    }

    auto select_row = [&](int row)
    {
        highlighted_series_      = &series;
        highlighted_point_index_ = static_cast<size_t>(row);
        if (on_point_selected_)
            on_point_selected_(&series, static_cast<size_t>(row));
    };

    // Info row
    const std::string count_buf = std::format("{} points", count);
    widgets::info_row("Points", count_buf.c_str());
    widgets::small_spacing();

    // Table with clipper for large datasets
    const std::string table_id = std::format("##data_table_2d_{}", series_idx);

    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
                                  | ImGuiTableFlags_SizingStretchProp;

    float avail_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - tokens::SPACE_4);
    avail_h       = std::min(avail_h, 400.0f);

    if (ImGui::BeginTable(table_id.c_str(), 3, table_flags, ImVec2(0, avail_h)))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Use clipper for performance with large datasets
        ImU32            highlight_col = IM_COL32(static_cast<int>(theme().accent_subtle.r * 255),
                                       static_cast<int>(theme().accent_subtle.g * 255),
                                       static_cast<int>(theme().accent_subtle.b * 255),
                                       96);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(count));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                ImGui::TableNextRow();

                bool is_row_highlighted = (highlighted_series_ == &series
                                           && highlighted_point_index_ == static_cast<size_t>(row));
                if (is_row_highlighted)
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_col);
                }

                // Row index
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", row);
                if (ImGui::IsItemClicked())
                    select_row(row);

                // X value
                ImGui::TableSetColumnIndex(1);
                {
                    const std::string cell_id = std::format("##x_{}_{}", series_idx, row);

                    bool is_editing = (editing_ && edit_row_ == row && edit_col_ == 0
                                       && edit_series_ == series_idx);

                    if (is_editing)
                    {
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText(cell_id.c_str(),
                                             edit_buf_,
                                             sizeof(edit_buf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                                 | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            float val = 0.0f;
                            if (std::sscanf(edit_buf_, "%f", &val) == 1)
                            {
                                if (auto* ls = dynamic_cast<LineSeries*>(&series))
                                {
                                    std::vector<float> xv(ls->x_data().begin(), ls->x_data().end());
                                    xv[row] = val;
                                    ls->set_x(xv);
                                }
                                else if (auto* ss = dynamic_cast<ScatterSeries*>(&series))
                                {
                                    std::vector<float> xv(ss->x_data().begin(), ss->x_data().end());
                                    xv[row] = val;
                                    ss->set_x(xv);
                                }
                            }
                            editing_ = false;
                        }
                        if (ImGui::IsItemDeactivated())
                            editing_ = false;
                    }
                    else
                    {
                        const std::string val_buf = std::format("{:.6g}", x_data[row]);
                        ImGui::TextUnformatted(val_buf.c_str());
                        if (ImGui::IsItemClicked())
                        {
                            select_row(row);
                            editing_     = true;
                            edit_row_    = row;
                            edit_col_    = 0;
                            edit_series_ = series_idx;
                            std::strncpy(edit_buf_,
                                         std::format("{:.6g}", x_data[row]).c_str(),
                                         sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                    }
                }

                // Y value
                ImGui::TableSetColumnIndex(2);
                {
                    const std::string cell_id = std::format("##y_{}_{}", series_idx, row);

                    bool is_editing = (editing_ && edit_row_ == row && edit_col_ == 1
                                       && edit_series_ == series_idx);

                    if (is_editing)
                    {
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText(cell_id.c_str(),
                                             edit_buf_,
                                             sizeof(edit_buf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                                 | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            float val = 0.0f;
                            if (std::sscanf(edit_buf_, "%f", &val) == 1)
                            {
                                if (auto* ls = dynamic_cast<LineSeries*>(&series))
                                {
                                    std::vector<float> yv(ls->y_data().begin(), ls->y_data().end());
                                    yv[row] = val;
                                    ls->set_y(yv);
                                }
                                else if (auto* ss = dynamic_cast<ScatterSeries*>(&series))
                                {
                                    std::vector<float> yv(ss->y_data().begin(), ss->y_data().end());
                                    yv[row] = val;
                                    ss->set_y(yv);
                                }
                            }
                            editing_ = false;
                        }
                        if (ImGui::IsItemDeactivated())
                            editing_ = false;
                    }
                    else
                    {
                        const std::string val_buf = std::format("{:.6g}", y_data[row]);
                        ImGui::TextUnformatted(val_buf.c_str());
                        if (ImGui::IsItemClicked())
                        {
                            select_row(row);
                            editing_     = true;
                            edit_row_    = row;
                            edit_col_    = 1;
                            edit_series_ = series_idx;
                            std::strncpy(edit_buf_,
                                         std::format("{:.6g}", y_data[row]).c_str(),
                                         sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                    }
                }
            }
        }
        ImGui::EndTable();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 3D Data Table (X, Y, Z columns)
// ────────────────────────────────────────────────────────────────────────────

void DataEditor::draw_data_table_3d(Series& series, int series_idx)
{
    // Get data spans
    std::span<const float> x_data;
    std::span<const float> y_data;
    std::span<const float> z_data;

    if (auto* ls = dynamic_cast<LineSeries3D*>(&series))
    {
        x_data = ls->x_data();
        y_data = ls->y_data();
        z_data = ls->z_data();
    }
    else if (auto* ss = dynamic_cast<ScatterSeries3D*>(&series))
    {
        x_data = ss->x_data();
        y_data = ss->y_data();
        z_data = ss->z_data();
    }
    else if (auto* surf = dynamic_cast<SurfaceSeries*>(&series))
    {
        // Surface: show grid data (x_grid, y_grid, z_values)
        x_data = surf->x_grid();
        y_data = surf->y_grid();
        z_data = surf->z_values();

        // Surface has different layout — show info
        const std::string info = std::format("{} x {} grid ({} z-values)",
                                             surf->rows(),
                                             surf->cols(),
                                             surf->z_values().size());
        widgets::info_row("Grid", info.c_str());
        widgets::small_spacing();

        // Show z_values table only for surface
        if (z_data.empty())
        {
            ImGui::TextDisabled("Empty surface data");
            return;
        }

        const std::string table_id = std::format("##data_table_surf_{}", series_idx);

        ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
                                      | ImGuiTableFlags_SizingStretchProp;

        float avail_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - tokens::SPACE_4);
        avail_h       = std::min(avail_h, 400.0f);

        if (ImGui::BeginTable(table_id.c_str(), 4, table_flags, ImVec2(0, avail_h)))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Col", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            int cols = surf->cols();
            // int rows = surf->rows();
            int total = static_cast<int>(z_data.size());

            ImGuiListClipper clipper;
            clipper.Begin(total);
            while (clipper.Step())
            {
                for (int idx = clipper.DisplayStart; idx < clipper.DisplayEnd; ++idx)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%d", idx);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", cols > 0 ? idx / cols : 0);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", cols > 0 ? idx % cols : 0);
                    ImGui::TableSetColumnIndex(3);
                    const std::string val_buf = std::format("{:.6g}", z_data[idx]);
                    ImGui::TextUnformatted(val_buf.c_str());
                }
            }
            ImGui::EndTable();
        }
        return;
    }
    else
    {
        ImGui::TextColored(ImVec4(theme().text_secondary.r,
                                  theme().text_secondary.g,
                                  theme().text_secondary.b,
                                  0.7f),
                           "Tabular view not available for this series type.");
        return;
    }

    size_t count = std::min({x_data.size(), y_data.size(), z_data.size()});
    if (count == 0)
    {
        ImGui::TextDisabled("Empty series");
        return;
    }

    auto select_row = [&](int row)
    {
        highlighted_series_      = &series;
        highlighted_point_index_ = static_cast<size_t>(row);
        if (on_point_selected_)
            on_point_selected_(&series, static_cast<size_t>(row));
    };

    // Info row
    const std::string count_buf = std::format("{} points", count);
    widgets::info_row("Points", count_buf.c_str());
    widgets::small_spacing();

    // Table with clipper for large datasets
    const std::string table_id = std::format("##data_table_3d_{}", series_idx);

    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
                                  | ImGuiTableFlags_SizingStretchProp;

    float avail_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - tokens::SPACE_4);
    avail_h       = std::min(avail_h, 400.0f);

    if (ImGui::BeginTable(table_id.c_str(), 4, table_flags, ImVec2(0, avail_h)))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImU32            highlight_col = IM_COL32(static_cast<int>(theme().accent_subtle.r * 255),
                                       static_cast<int>(theme().accent_subtle.g * 255),
                                       static_cast<int>(theme().accent_subtle.b * 255),
                                       96);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(count));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                ImGui::TableNextRow();

                bool is_row_highlighted = (highlighted_series_ == &series
                                           && highlighted_point_index_ == static_cast<size_t>(row));
                if (is_row_highlighted)
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_col);
                }

                // Row index
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d", row);
                if (ImGui::IsItemClicked())
                    select_row(row);

                // X value
                ImGui::TableSetColumnIndex(1);
                {
                    const std::string cell_id = std::format("##x3_{}_{}", series_idx, row);

                    bool is_editing = (editing_ && edit_row_ == row && edit_col_ == 0
                                       && edit_series_ == series_idx);

                    if (is_editing)
                    {
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText(cell_id.c_str(),
                                             edit_buf_,
                                             sizeof(edit_buf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                                 | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            float val = 0.0f;
                            if (std::sscanf(edit_buf_, "%f", &val) == 1)
                            {
                                if (auto* ls = dynamic_cast<LineSeries3D*>(&series))
                                {
                                    std::vector<float> xv(ls->x_data().begin(), ls->x_data().end());
                                    xv[row] = val;
                                    ls->set_x(xv);
                                }
                                else if (auto* ss = dynamic_cast<ScatterSeries3D*>(&series))
                                {
                                    std::vector<float> xv(ss->x_data().begin(), ss->x_data().end());
                                    xv[row] = val;
                                    ss->set_x(xv);
                                }
                            }
                            editing_ = false;
                        }
                        if (ImGui::IsItemDeactivated())
                            editing_ = false;
                    }
                    else
                    {
                        const std::string val_buf = std::format("{:.6g}", x_data[row]);
                        ImGui::TextUnformatted(val_buf.c_str());
                        if (ImGui::IsItemClicked())
                        {
                            select_row(row);
                            editing_     = true;
                            edit_row_    = row;
                            edit_col_    = 0;
                            edit_series_ = series_idx;
                            std::strncpy(edit_buf_,
                                         std::format("{:.6g}", x_data[row]).c_str(),
                                         sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                    }
                }

                // Y value
                ImGui::TableSetColumnIndex(2);
                {
                    const std::string cell_id = std::format("##y3_{}_{}", series_idx, row);

                    bool is_editing = (editing_ && edit_row_ == row && edit_col_ == 1
                                       && edit_series_ == series_idx);

                    if (is_editing)
                    {
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText(cell_id.c_str(),
                                             edit_buf_,
                                             sizeof(edit_buf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                                 | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            float val = 0.0f;
                            if (std::sscanf(edit_buf_, "%f", &val) == 1)
                            {
                                if (auto* ls = dynamic_cast<LineSeries3D*>(&series))
                                {
                                    std::vector<float> yv(ls->y_data().begin(), ls->y_data().end());
                                    yv[row] = val;
                                    ls->set_y(yv);
                                }
                                else if (auto* ss = dynamic_cast<ScatterSeries3D*>(&series))
                                {
                                    std::vector<float> yv(ss->y_data().begin(), ss->y_data().end());
                                    yv[row] = val;
                                    ss->set_y(yv);
                                }
                            }
                            editing_ = false;
                        }
                        if (ImGui::IsItemDeactivated())
                            editing_ = false;
                    }
                    else
                    {
                        const std::string val_buf = std::format("{:.6g}", y_data[row]);
                        ImGui::TextUnformatted(val_buf.c_str());
                        if (ImGui::IsItemClicked())
                        {
                            select_row(row);
                            editing_     = true;
                            edit_row_    = row;
                            edit_col_    = 1;
                            edit_series_ = series_idx;
                            std::strncpy(edit_buf_,
                                         std::format("{:.6g}", y_data[row]).c_str(),
                                         sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                    }
                }

                // Z value
                ImGui::TableSetColumnIndex(3);
                {
                    const std::string cell_id = std::format("##z3_{}_{}", series_idx, row);

                    bool is_editing = (editing_ && edit_row_ == row && edit_col_ == 2
                                       && edit_series_ == series_idx);

                    if (is_editing)
                    {
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText(cell_id.c_str(),
                                             edit_buf_,
                                             sizeof(edit_buf_),
                                             ImGuiInputTextFlags_EnterReturnsTrue
                                                 | ImGuiInputTextFlags_AutoSelectAll))
                        {
                            float val = 0.0f;
                            if (std::sscanf(edit_buf_, "%f", &val) == 1)
                            {
                                if (auto* ls = dynamic_cast<LineSeries3D*>(&series))
                                {
                                    std::vector<float> zv(ls->z_data().begin(), ls->z_data().end());
                                    zv[row] = val;
                                    ls->set_z(zv);
                                }
                                else if (auto* ss = dynamic_cast<ScatterSeries3D*>(&series))
                                {
                                    std::vector<float> zv(ss->z_data().begin(), ss->z_data().end());
                                    zv[row] = val;
                                    ss->set_z(zv);
                                }
                            }
                            editing_ = false;
                        }
                        if (ImGui::IsItemDeactivated())
                            editing_ = false;
                    }
                    else
                    {
                        const std::string val_buf = std::format("{:.6g}", z_data[row]);
                        ImGui::TextUnformatted(val_buf.c_str());
                        if (ImGui::IsItemClicked())
                        {
                            select_row(row);
                            editing_     = true;
                            edit_row_    = row;
                            edit_col_    = 2;
                            edit_series_ = series_idx;
                            std::strncpy(edit_buf_,
                                         std::format("{:.6g}", z_data[row]).c_str(),
                                         sizeof(edit_buf_) - 1);
                            edit_buf_[sizeof(edit_buf_) - 1] = '\0';
                        }
                    }
                }
            }
        }
        ImGui::EndTable();
    }
}

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
