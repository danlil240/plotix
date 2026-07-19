#ifdef SPECTRA_USE_IMGUI

    #include <ranges>

    #include "imgui_integration_internal.hpp"

    #include "../../../third_party/tinyfiledialogs.h"

namespace spectra
{

void ImGuiIntegration::draw_csv_dialog()
{
    const auto& colors = ui::theme();

    ImGuiIO& io       = ImGui::GetIO();
    float    dialog_w = 480.0f;
    float    dialog_h = 520.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dialog_w, dialog_h), ImGuiCond_Appearing);

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));

    bool open = csv_dialog_open_;
    if (ImGui::Begin("CSV Column Picker##csv_dialog", &open, ImGuiWindowFlags_NoCollapse))
    {
        // File info
        ImGui::TextColored(
            ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 1.0f),
            "File:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", csv_file_path_.c_str());

        if (!csv_error_.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", csv_error_.c_str());
        }

        if (csv_data_loaded_ && csv_data_.num_cols > 0)
        {
            ImGui::Separator();
            ImGui::Text("Columns: %zu  |  Rows: %zu", csv_data_.num_cols, csv_data_.num_rows);
            ImGui::Spacing();

            // X Column combo
            auto combo_item = [&](const char* label, int* current, bool allow_none = false)
            {
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo(
                        label,
                        (*current >= 0 && *current < static_cast<int>(csv_data_.headers.size()))
                            ? csv_data_.headers[*current].c_str()
                            : (allow_none ? "(none)" : "---")))
                {
                    if (allow_none)
                    {
                        if (ImGui::Selectable("(none)", *current == -1))
                            *current = -1;
                    }
                    for (int c = 0; c < static_cast<int>(csv_data_.headers.size()); ++c)
                    {
                        bool is_sel = (*current == c);
                        if (ImGui::Selectable(csv_data_.headers[c].c_str(), is_sel))
                            *current = c;
                        if (is_sel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            };

            combo_item("X Column (shared)", &csv_col_x_);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(colors.text_secondary.r,
                                      colors.text_secondary.g,
                                      colors.text_secondary.b,
                                      1.0f),
                               "Y Columns (select multiple):");

            // Multi-select Y columns via checkboxes in a scrollable child
            auto is_selected = [&](int col) {
                return std::find(csv_selected_y_.begin(), csv_selected_y_.end(), col)
                       != csv_selected_y_.end();
            };
            auto toggle = [&](int col)
            {
                auto it = std::find(csv_selected_y_.begin(), csv_selected_y_.end(), col);
                if (it != csv_selected_y_.end())
                    csv_selected_y_.erase(it);
                else
                    csv_selected_y_.push_back(col);
            };

            if (ImGui::BeginChild("##csv_y_cols", ImVec2(0, 100), ImGuiChildFlags_Borders))
            {
                for (int c = 0; c < static_cast<int>(csv_data_.headers.size()); ++c)
                {
                    ImGui::PushID(c);
                    bool sel = is_selected(c);
                    if (ImGui::Checkbox(csv_data_.headers[c].c_str(), &sel))
                        toggle(c);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            // Select All / None helper buttons
            if (ImGui::Button("Select All"))
            {
                csv_selected_y_.clear();
                for (int c = 0; c < static_cast<int>(csv_data_.num_cols); ++c)
                    if (c != csv_col_x_)
                        csv_selected_y_.push_back(c);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
                csv_selected_y_.clear();

            ImGui::Spacing();
            combo_item("Z Column (optional)", &csv_col_z_, true);

            ImGui::Spacing();

            // Data preview — show first selected Y column
            if (csv_data_.num_rows > 0 && !csv_selected_y_.empty())
            {
                ImGui::TextColored(ImVec4(colors.text_secondary.r,
                                          colors.text_secondary.g,
                                          colors.text_secondary.b,
                                          1.0f),
                                   "Preview (first 5 rows, X + first selected Y):");
                if (ImGui::BeginChild("##csv_preview", ImVec2(0, 90), ImGuiChildFlags_Borders))
                {
                    size_t preview_rows = std::min(csv_data_.num_rows, size_t(5));
                    int    first_y      = csv_selected_y_[0];
                    for (size_t r = 0; r < preview_rows; ++r)
                    {
                        // Show absolute values: column base offset + stored float
                        const bool x_ok =
                            (csv_col_x_ >= 0 && csv_col_x_ < static_cast<int>(csv_data_.num_cols));
                        double xv =
                            x_ok ? static_cast<double>(csv_data_.columns[csv_col_x_][r]) : 0.0;
                        if (x_ok
                            && static_cast<size_t>(csv_col_x_) < csv_data_.column_offsets.size())
                            xv += csv_data_.column_offsets[csv_col_x_];
                        float yv = (first_y >= 0 && first_y < static_cast<int>(csv_data_.num_cols))
                                       ? csv_data_.columns[first_y][r]
                                       : 0.0f;
                        ImGui::Text("  x=%.4f  y=%.4f", xv, yv);
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();

            // Plot / Cancel buttons
            bool can_plot = (csv_col_x_ >= 0 && csv_col_x_ < static_cast<int>(csv_data_.num_cols)
                             && !csv_selected_y_.empty() && csv_data_.num_rows > 0);

            if (!can_plot)
                ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(colors.accent_hover.r, colors.accent_hover.g, colors.accent_hover.b, 1.0f));
            std::string plot_label =
                csv_selected_y_.size() > 1
                    ? "Plot " + std::to_string(csv_selected_y_.size()) + " Series"
                    : "Plot";
            if (ImGui::Button(plot_label.c_str(), ImVec2(140, 32)))
            {
                if (csv_plot_cb_)
                {
                    const std::vector<float>* z_ptr       = nullptr;
                    const std::string*        z_label_ptr = nullptr;
                    std::string               z_label;
                    if (csv_col_z_ >= 0 && csv_col_z_ < static_cast<int>(csv_data_.num_cols))
                    {
                        z_ptr       = &csv_data_.columns[csv_col_z_];
                        z_label     = csv_data_.headers[csv_col_z_];
                        z_label_ptr = &z_label;
                    }
                    // X column base offset (non-zero for datetime/epoch columns)
                    const double x_offset =
                        (static_cast<size_t>(csv_col_x_) < csv_data_.column_offsets.size())
                            ? csv_data_.column_offsets[csv_col_x_]
                            : 0.0;
                    // Sort selected Y columns for deterministic order
                    std::vector<int> sorted_y = csv_selected_y_;
                    std::sort(sorted_y.begin(), sorted_y.end());
                    for (int y_col : sorted_y)
                    {
                        csv_plot_cb_(csv_file_path_,
                                     csv_data_.columns[csv_col_x_],
                                     csv_data_.columns[y_col],
                                     csv_data_.headers[csv_col_x_],
                                     csv_data_.headers[y_col],
                                     z_ptr,
                                     z_label_ptr,
                                     x_offset);
                    }
                }
                csv_dialog_open_ = false;
            }
            ImGui::PopStyleColor(2);

            if (!can_plot)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 32)))
            {
                csv_dialog_open_ = false;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (!open)
        csv_dialog_open_ = false;
}

void ImGuiIntegration::draw_custom_transform_dialog()
{
    custom_transform_dialog_.draw();
}

void ImGuiIntegration::draw_plot_overlay_dialog()
{
    plot_overlay_dialog_.set_fonts(font_body_, font_heading_);
    plot_overlay_dialog_.draw();
}

void ImGuiIntegration::draw_theme_settings()
{
    const auto& colors        = ui::theme();
    auto&       theme_manager = *theme_mgr_;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 0.0f), ImVec2(720.0f, 900.0f));

    static const std::vector<std::pair<std::string, std::string>> available_themes = {
        {"night", "Night Glass"},
        {"dark", "Dark"},
        {"light", "Light"},
        {"high_contrast", "High Contrast"},
    };

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse
                             | ImGuiWindowFlags_AlwaysAutoResize;

    // Modern modal styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_6, ui::tokens::SPACE_5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 1.0f));

    bool is_open = true;
    if (ImGui::Begin("Theme Settings", &is_open, flags))
    {
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f));
        ImGui::TextUnformatted("Appearance");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));

        // Theme selection buttons — card-like
        for (const auto& [theme_id, display_name] : available_themes)
        {
            bool is_current = (theme_manager.current_theme_name() == theme_id);

            if (is_current)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(colors.accent_muted.r,
                                             colors.accent_muted.g,
                                             colors.accent_muted.b,
                                             0.35f));
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(colors.text_primary.r,
                                             colors.text_primary.g,
                                             colors.text_primary.b,
                                             1.0f));
            }

            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(colors.accent_subtle.r,
                                         colors.accent_subtle.g,
                                         colors.accent_subtle.b,
                                         0.5f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.6f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));

            std::string label =
                is_current ? std::string("  ") + display_name : std::string("    ") + display_name;

            if (ImGui::Button(label.c_str(), ImVec2(-1, 0)))
            {
                theme_manager.set_theme(theme_id);
                theme_manager.reset_glass_defaults();
                theme_manager.save_current_as_default();
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            ImGui::Dummy(ImVec2(0, 2));
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::PushFont(font_heading_);
        ImGui::TextUnformatted("Glass / Transparency");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));

        ui::ThemeGlassSettings glass         = theme_manager.glass();
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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Real backdrop blur is not enabled in this build.\n"
                              "Glass styling uses layered translucency and glow instead.");
        }

        if (glass_changed)
        {
            theme_manager.set_glass_settings(glass, true);
            theme_manager.save_current_as_default();
        }

        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Reset Theme Defaults", ImVec2(-1, 0)))
        {
            theme_manager.reset_glass_defaults();
            theme_manager.save_current_as_default();
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        // Close button — right-aligned, pill-shaped
        float close_w = 90.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - close_w + ImGui::GetCursorPosX());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_PILL);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ui::tokens::SPACE_5, ui::tokens::SPACE_2));
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.5f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
        if (ImGui::Button("Close", ImVec2(close_w, 0)))
        {
            is_open = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (!is_open)
    {
        show_theme_settings_ = false;
    }
}

// ─── Axes Right-Click Context Menu (Axis Linking) ────────────────────────────

void ImGuiIntegration::draw_axes_context_menu(Figure& figure)
{
    if (!input_handler_ || !axis_link_mgr_)
        return;

    // Detect right-click context intent on PRESS, but only open on RELEASE
    // when there was no drag. This prevents right-drag zoom from spawning a
    // popup that steals the next left click (e.g. first play click after zoom).
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
    {
        AxesBase* hit       = input_handler_->hit_test_all_axes(static_cast<double>(io.MousePos.x),
                                                          static_cast<double>(io.MousePos.y));
        context_menu_armed_ = (hit != nullptr);
        context_menu_pressed_axes_ = hit;
        context_menu_press_x_      = io.MousePos.x;
        context_menu_press_y_      = io.MousePos.y;
    }

    if (context_menu_armed_)
    {
        constexpr float kContextClickMaxDragPx = 4.0f;
        float           dx                     = io.MousePos.x - context_menu_press_x_;
        float           dy                     = io.MousePos.y - context_menu_press_y_;
        float           dist2                  = dx * dx + dy * dy;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)
            && dist2 > kContextClickMaxDragPx * kContextClickMaxDragPx)
        {
            // Became a drag (zoom/pan intent), cancel popup arming.
            context_menu_armed_        = false;
            context_menu_pressed_axes_ = nullptr;
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            if (!io.WantCaptureMouse && context_menu_pressed_axes_)
            {
                context_menu_axes_ = context_menu_pressed_axes_;

                // Auto-select nearest series on right-click only in Select mode
                // so series selection behavior is mode-gated (MATLAB-like).
                // Use select_series_no_toggle() to always select (never deselect).
                // This path handles 3D axes (all_axes) and sets axes_base properly.
                if (data_interaction_ && interaction_mode_ == ToolMode::Select)
                {
                    const auto& np = data_interaction_->nearest_point();
                    if (np.found && np.distance_px <= 40.0f && np.series)
                    {
                        int ax_idx = 0;
                        for (auto& axes_ptr : figure.all_axes())
                        {
                            if (!axes_ptr)
                            {
                                ax_idx++;
                                continue;
                            }
                            int s_idx = 0;
                            for (auto& sp : axes_ptr->series())
                            {
                                if (sp.get() == np.series)
                                {
                                    select_series_no_toggle(&figure,
                                                            dynamic_cast<Axes*>(axes_ptr.get()),
                                                            ax_idx,
                                                            sp.get(),
                                                            s_idx);
                                    selection_ctx_.axes_base = axes_ptr.get();
                                    goto found_series_rc;
                                }
                                s_idx++;
                            }
                            ax_idx++;
                        }
                    found_series_rc:;
                    }
                }

                ImGui::OpenPopup("##axes_context_menu");
            }
            context_menu_armed_        = false;
            context_menu_pressed_axes_ = nullptr;
        }
    }

    const auto& colors = ui::theme();

    // Popup styling
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

    if (ImGui::BeginPopup("##axes_context_menu"))
    {
        AxesBase* ax_base = context_menu_axes_;
        if (!ax_base)
        {
            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
            return;
        }

        // Determine if this is a 2D or 3D axes
        Axes* ax_2d = dynamic_cast<Axes*>(ax_base);
        auto* ax_3d = dynamic_cast<Axes3D*>(ax_base);

        // Find axes index in all_axes for display
        int axes_idx = -1;
        for (size_t i = 0; i < figure.all_axes().size(); ++i)
        {
            if (figure.all_axes()[i].get() == ax_base)
            {
                axes_idx = static_cast<int>(i);
                break;
            }
        }
        std::string axes_label =
            (axes_idx >= 0) ? "Subplot " + std::to_string(axes_idx + 1) : "Axes";
        if (ax_3d)
            axes_label += " (3D)";

        // Header
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     0.7f));
        ImGui::TextUnformatted(axes_label.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.3f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));

        // Style for menu items
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        bool is_linked_2d = (ax_2d != nullptr) && axis_link_mgr_->is_linked(ax_2d);
        bool has_multi    = figure.all_axes().size() > 1;

        if (has_multi && ax_2d)
        {
            // 2D axes: Link X / Y / Both
            std::string link_x_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link X-Axis";
            if (ImGui::Selectable(link_x_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax_2d)
                        axis_link_mgr_->link(ax_2d, other.get(), LinkAxis::X);
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked X-axis of subplot " + std::to_string(axes_idx + 1));
            }

            std::string link_y_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link Y-Axis";
            if (ImGui::Selectable(link_y_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax_2d)
                        axis_link_mgr_->link(ax_2d, other.get(), LinkAxis::Y);
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked Y-axis of subplot " + std::to_string(axes_idx + 1));
            }

            std::string link_both_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD")) + "  Link Both Axes";
            if (ImGui::Selectable(link_both_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                for (auto& other : figure.axes_mut())
                {
                    if (other && other.get() != ax_2d)
                        axis_link_mgr_->link(ax_2d, other.get(), LinkAxis::Both);
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked both axes of subplot " + std::to_string(axes_idx + 1));
            }
        }

        if (has_multi && ax_3d)
        {
            // 3D axes: Link XYZ to all other 3D axes
            std::string link_3d_label = std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBD"))
                                        + "  Link 3D Axes (XYZ)";
            if (ImGui::Selectable(link_3d_label.c_str(), false, 0, ImVec2(220, 24)))
            {
                for (auto& ab : figure.all_axes_mut())
                {
                    if (auto* other_3d = dynamic_cast<Axes3D*>(ab.get()))
                    {
                        if (other_3d != ax_3d)
                            axis_link_mgr_->link_3d(ax_3d, other_3d);
                    }
                }
                SPECTRA_LOG_INFO("axes_link",
                                 "Linked 3D axes of subplot " + std::to_string(axes_idx + 1));
            }
        }

        // Show linked status and unlink options
        bool show_unlink = is_linked_2d;
        // For 3D, we don't have is_linked query yet — check if any 3D group contains this axes
        // (simple: try to see if unlink would do anything)

        if (show_unlink)
        {
            if (has_multi)
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

            // Show which groups this axes belongs to
            if (ax_2d)
            {
                auto group_ids = axis_link_mgr_->groups_for(ax_2d);
                for (auto gid : group_ids)
                {
                    const auto* grp = axis_link_mgr_->group(gid);
                    if (!grp)
                        continue;
                    std::string axis_str = (grp->axis == LinkAxis::X)   ? "X"
                                           : (grp->axis == LinkAxis::Y) ? "Y"
                                                                        : "XY";
                    ImU32       grp_col  = IM_COL32(static_cast<uint8_t>(grp->color.r * 255),
                                             static_cast<uint8_t>(grp->color.g * 255),
                                             static_cast<uint8_t>(grp->color.b * 255),
                                             255);

                    ImVec2      cursor = ImGui::GetCursorScreenPos();
                    ImDrawList* dl     = ImGui::GetWindowDrawList();
                    dl->AddCircleFilled(ImVec2(cursor.x + 8, cursor.y + 10), 5.0f, grp_col);
                    ImGui::Dummy(ImVec2(20, 0));
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(colors.text_secondary.r,
                                                 colors.text_secondary.g,
                                                 colors.text_secondary.b,
                                                 0.8f));
                    ImGui::Text("%s (%s, %zu axes)",
                                grp->name.c_str(),
                                axis_str.c_str(),
                                grp->members.size());
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PushStyleColor(ImGuiCol_Separator,
                                  ImVec4(colors.border_subtle.r,
                                         colors.border_subtle.g,
                                         colors.border_subtle.b,
                                         0.3f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 2));

            // "Unlink this axes"
            std::string unlink_label = std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBE"))
                                       + "  Unlink This Subplot";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
            if (ImGui::Selectable(unlink_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                if (ax_2d)
                    axis_link_mgr_->unlink(ax_2d);
                if (ax_3d)
                    axis_link_mgr_->remove_from_all_3d(ax_3d);
                SPECTRA_LOG_INFO("axes_link", "Unlinked subplot " + std::to_string(axes_idx + 1));
            }
            ImGui::PopStyleColor();
        }

        // "Unlink All" — always show if there are any linked axes
        if (has_multi)
        {
            if (!show_unlink)
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

            std::string unlink_all_label =
                std::string(reinterpret_cast<const char*>(u8"\xEE\x80\xBE")) + "  Unlink All";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
            if (ImGui::Selectable(unlink_all_label.c_str(), false, 0, ImVec2(200, 24)))
            {
                // Unlink 2D groups
                auto groups_copy = axis_link_mgr_->groups();
                for (auto& [id, grp] : groups_copy)
                    axis_link_mgr_->remove_group(id);
                // Unlink 3D groups
                for (auto& ab : figure.all_axes_mut())
                {
                    if (auto* a3 = dynamic_cast<Axes3D*>(ab.get()))
                        axis_link_mgr_->remove_from_all_3d(a3);
                }
                axis_link_mgr_->clear_shared_cursor();
                SPECTRA_LOG_INFO("axes_link", "Unlinked all axes");
            }
            ImGui::PopStyleColor();
        }

        // ── Series clipboard operations ──────────────────────────────────
        if (series_clipboard_ && ax_base)
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

            bool   has_sel   = (selection_ctx_.type == ui::SelectionType::Series
                            && !selection_ctx_.selected_series.empty());
            size_t sel_count = selection_ctx_.selected_count();
            bool   is_multi  = selection_ctx_.has_multi_selection();

            if (has_sel)
            {
                // Copy
                std::string copy_label =
                    std::string(ui::icon_str(ui::Icon::Copy))
                    + (is_multi ? "  Copy " + std::to_string(sel_count) + " Series"
                                : "  Copy Series");
                if (ImGui::Selectable(copy_label.c_str(), false, 0, ImVec2(220, 24)))
                {
                    if (is_multi)
                    {
                        std::vector<const Series*> list;
                        for (const auto& e : selection_ctx_.selected_series)
                            if (e.series)
                                list.push_back(e.series);
                        series_clipboard_->copy_multi(list);
                    }
                    else if (selection_ctx_.series)
                    {
                        series_clipboard_->copy(*selection_ctx_.series);
                    }
                }

                // Cut
                std::string cut_label =
                    std::string(ui::icon_str(ui::Icon::Scissors))
                    + (is_multi ? "  Cut " + std::to_string(sel_count) + " Series"
                                : "  Cut Series");
                if (ImGui::Selectable(cut_label.c_str(), false, 0, ImVec2(220, 24)))
                {
                    // Snapshot clipboard data from live series first
                    if (is_multi)
                    {
                        std::vector<const Series*> list;
                        for (const auto& e : selection_ctx_.selected_series)
                            if (e.series)
                                list.push_back(e.series);
                        series_clipboard_->cut_multi(list);
                    }
                    else if (selection_ctx_.series)
                    {
                        series_clipboard_->cut(*selection_ctx_.series);
                    }
                    // Defer removal so on_frame callbacks (which may hold
                    // raw Series& refs, e.g. knob_demo) run safely next frame.
                    auto entries = selection_ctx_.selected_series;
                    selection_ctx_.clear();
                    for (auto& entrie : std::ranges::reverse_view(entries))
                    {
                        AxesBase* owner = entrie.axes_base ? entrie.axes_base
                                                           : static_cast<AxesBase*>(entrie.axes);
                        if (owner && entrie.series)
                            defer_series_removal(owner, const_cast<Series*>(entrie.series));
                    }
                }

                // Delete
                std::string del_label =
                    std::string(ui::icon_str(ui::Icon::Trash))
                    + (is_multi ? "  Delete " + std::to_string(sel_count) + " Series"
                                : "  Delete Series");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
                if (ImGui::Selectable(del_label.c_str(), false, 0, ImVec2(220, 24)))
                {
                    // Defer removal so on_frame callbacks (which may hold
                    // raw Series& refs, e.g. knob_demo) run safely next frame.
                    auto entries = selection_ctx_.selected_series;
                    selection_ctx_.clear();
                    for (auto& entrie : std::ranges::reverse_view(entries))
                    {
                        AxesBase* owner = entrie.axes_base ? entrie.axes_base
                                                           : static_cast<AxesBase*>(entrie.axes);
                        if (owner && entrie.series)
                            defer_series_removal(owner, const_cast<Series*>(entrie.series));
                    }
                }
                ImGui::PopStyleColor();
            }

            // Paste: always available if clipboard has data
            if (series_clipboard_->has_data())
            {
                size_t      clip_count = series_clipboard_->count();
                std::string paste_label =
                    std::string(ui::icon_str(ui::Icon::Duplicate))
                    + (clip_count > 1 ? "  Paste " + std::to_string(clip_count) + " Series"
                                      : "  Paste Series");
                if (ImGui::Selectable(paste_label.c_str(), false, 0, ImVec2(220, 24)))
                {
                    series_clipboard_->paste_all(*ax_base);
                }
            }
        }

        ImGui::PopStyleVar();      // SelectableTextAlign
        ImGui::PopStyleColor(2);   // Header, HeaderHovered

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);   // PopupBg, Border
    ImGui::PopStyleVar(4);     // WindowPadding, PopupRounding, PopupBorderSize, ItemSpacing
}

// ─── Axis Link Indicators (colored chain icon on linked axes) ────────────────

void ImGuiIntegration::draw_axis_link_indicators(Figure& figure)
{
    if (!axis_link_mgr_ || axis_link_mgr_->group_count() == 0)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        Axes* ax = axes_ptr.get();
        if (!axis_link_mgr_->is_linked(ax))
            continue;

        const auto& vp        = ax->viewport();
        auto        group_ids = axis_link_mgr_->groups_for(ax);
        if (group_ids.empty())
            continue;

        // Draw one indicator per group in the top-right corner of the axes
        float icon_x = vp.x + vp.w - 8.0f;
        float icon_y = vp.y + 8.0f;

        for (size_t gi = 0; gi < group_ids.size(); ++gi)
        {
            const auto* grp = axis_link_mgr_->group(group_ids[gi]);
            if (!grp)
                continue;

            ImU32 col    = IM_COL32(static_cast<uint8_t>(grp->color.r * 255),
                                 static_cast<uint8_t>(grp->color.g * 255),
                                 static_cast<uint8_t>(grp->color.b * 255),
                                 200);
            ImU32 bg_col = IM_COL32(0, 0, 0, 100);

            float cx = icon_x - gi * 22.0f;
            float cy = icon_y;

            // Background pill
            dl->AddRectFilled(ImVec2(cx - 10, cy - 8), ImVec2(cx + 10, cy + 8), bg_col, 6.0f);

            // Chain link icon (two interlocking circles)
            dl->AddCircle(ImVec2(cx - 2.5f, cy), 4.5f, col, 0, 1.8f);
            dl->AddCircle(ImVec2(cx + 2.5f, cy), 4.5f, col, 0, 1.8f);

            // Axis label below
            std::string axis_str = (grp->axis == LinkAxis::X)   ? "X"
                                   : (grp->axis == LinkAxis::Y) ? "Y"
                                                                : "XY";
            ImVec2      sz       = ImGui::CalcTextSize(axis_str.c_str());
            dl->AddText(ImVec2(cx - sz.x * 0.5f, cy + 10), col, axis_str.c_str());
        }
    }
}

// ── Tearoff preview card rendering (for borderless preview window) ───────────

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
