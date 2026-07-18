#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <format>

    #include "../topics/topics_panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
    #include "ui/shell/status_bar.hpp"
    #include "ui/theme/glass_draw.hpp"

namespace spectra
{

void ImGuiIntegration::draw_tab_bar()
{
    if (!layout_manager_ || !tab_bar_)
        return;
    if (!layout_manager_->is_tab_bar_visible())
        return;

    Rect bounds = layout_manager_->tab_bar_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Create an ImGui window for the tab bar so that GetWindowDrawList(),
    // OpenPopup(), and BeginPopup() all work correctly inside TabBar::draw()
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##tabbar_host", nullptr, flags))
    {
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImVec2      wpos = ImGui::GetWindowPos();
        ImVec2      wsz  = ImGui::GetWindowSize();
        const auto& c    = theme_colors();

        // Subtle shadow beneath the tab bar — shares elevation language with header.
        for (int i = 0; i < 3; ++i)
        {
            float t     = static_cast<float>(i) / 3.0f;
            float alpha = 0.07f * (1.0f - t);
            float off   = 1.0f + t * ui::tokens::ELEVATION_1_SPREAD;
            dl->AddRectFilled(ImVec2(wpos.x, wpos.y + wsz.y),
                              ImVec2(wpos.x + wsz.x, wpos.y + wsz.y + off),
                              IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
        }
        dl->AddLine(ImVec2(wpos.x, std::floor(wpos.y + wsz.y) - 1.0f),
                    ImVec2(wpos.x + wsz.x, std::floor(wpos.y + wsz.y) - 1.0f),
                    IM_COL32(static_cast<uint8_t>(c.border_subtle.r * 255),
                             static_cast<uint8_t>(c.border_subtle.g * 255),
                             static_cast<uint8_t>(c.border_subtle.b * 255),
                             80),
                    1.0f);
        tab_bar_->draw(bounds, dock_system_ != nullptr);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_canvas(Figure& figure)
{
    if (app_shell_)
    {
        app_shell_->ensure_initialized();
        app_shell_->set_current_figure(&figure);
        app_shell_->spectra_canvas_host().draw();
        return;
    }
    draw_canvas_content(figure);
}

void ImGuiIntegration::draw_canvas_content(Figure& figure)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->canvas_rect();

    // Canvas is primarily handled by the Vulkan renderer
    // We just set up the viewport here for ImGui coordination
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    // Show welcome screen only when figure has no axes at all (truly blank).
    // A figure created via "New Figure" gets default axes, so it shows the
    // normal empty plot grid instead of the welcome page.
    bool has_any_axes = !figure.axes().empty() || !figure.all_axes().empty();

    if (!has_any_axes)
    {
        ImGuiWindowFlags empty_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

        ui::Color welcome_bg =
            theme_mgr_ ? theme_mgr_->glass_resolved_plot_background() : theme_colors().bg_canvas;
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(welcome_bg.r, welcome_bg.g, welcome_bg.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        if (ImGui::Begin("##canvas_welcome", nullptr, empty_flags))
        {
            const auto& colors = theme_colors();
            float       cx     = bounds.x + bounds.w * 0.5f;
            float       cy     = bounds.y + bounds.h * 0.5f;

            // Title
            ImGui::PushFont(font_title_);
            const char* title    = "Welcome to Spectra";
            ImVec2      title_sz = ImGui::CalcTextSize(title);
            ImGui::SetCursorScreenPos(ImVec2(cx - title_sz.x * 0.5f, cy - 60.0f));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 0.9f));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // Subtitle
            ImGui::PushFont(font_heading_);
            const char* subtitle    = "Drop a CSV file or use the command palette to get started";
            ImVec2      subtitle_sz = ImGui::CalcTextSize(subtitle);
            ImGui::SetCursorScreenPos(ImVec2(cx - subtitle_sz.x * 0.5f, cy - 28.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.7f));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            // Action buttons
            float btn_w       = 140.0f;
            float btn_h       = 32.0f;
            float btn_spacing = ui::tokens::SPACE_3;
            float total_w     = btn_w * 3.0f + btn_spacing * 2.0f;
            float btn_y       = cy + 10.0f;

            ImGui::PushFont(font_heading_);

            // Button style: accent-tinted with rounded corners
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));

            // Open CSV button — triggers the native file dialog (same as menu Data > Load from CSV)
            ImGui::SetCursorScreenPos(ImVec2(cx - total_w * 0.5f, btn_y));
            if (ImGui::Button("Open CSV", ImVec2(btn_w, btn_h)))
            {
                pending_open_csv_ = true;
            }

            // New Plot button — adds default axes to this figure, dismissing welcome screen
            ImGui::SetCursorScreenPos(ImVec2(cx - total_w * 0.5f + btn_w + btn_spacing, btn_y));
            if (ImGui::Button("New Plot", ImVec2(btn_w, btn_h)))
            {
                figure.subplot(1, 1, 1);
            }

            // Command Palette button
            ImGui::SetCursorScreenPos(
                ImVec2(cx - total_w * 0.5f + (btn_w + btn_spacing) * 2.0f, btn_y));
            if (ImGui::Button("Commands", ImVec2(btn_w, btn_h)))
            {
                if (command_registry_)
                    command_registry_->execute("app.command_palette");
            }

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
            ImGui::PopFont();

            // Keyboard hint
            ImGui::PushFont(font_body_);
            const char* hint = "Ctrl+K  Command Palette  |  Ctrl+T  New Tab  |  Ctrl+S  Export PNG";
            ImVec2      hint_sz = ImGui::CalcTextSize(hint);
            ImGui::SetCursorScreenPos(ImVec2(cx - hint_sz.x * 0.5f, btn_y + btn_h + 24.0f));
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.5f));
            ImGui::TextUnformatted(hint);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        return;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoInputs;

    // Transparent window for canvas area
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    // Canvas border draws into a dedicated overlay window so it respects
    // ImGui z-ordering and renders behind menus/popups.
    ImDrawList* dl = nullptr;
    {
        ImGui::SetNextWindowPos(ImVec2(bounds.x - 8.0f, bounds.y - 8.0f));
        ImGui::SetNextWindowSize(ImVec2(bounds.w + 16.0f, bounds.h + 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags border_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##canvas_border", nullptr, border_flags))
            dl = ImGui::GetWindowDrawList();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    if (ImGui::Begin("##canvas", nullptr, flags))
    {
        // Canvas content is rendered by Vulkan, not ImGui
        // This window is just for input handling coordination
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!dl)
        return;

    const float glow   = theme_mgr_ ? theme_mgr_->effective_glow_intensity() : 0.45f;
    const float master = theme_mgr_ ? theme_mgr_->glass().master_intensity : 0.65f;
    ui::glass_draw::draw_vision_canvas_frame(dl,
                                             ImVec2(bounds.x, bounds.y),
                                             ImVec2(bounds.x + bounds.w, bounds.y + bounds.h),
                                             16.0f,
                                             theme_colors(),
                                             glow,
                                             master);

    // Draw interactive page scrollbar when subplots overflow the visible canvas area
    if (figure.needs_scroll(bounds.h))
    {
        float content_h  = figure.content_height();
        float scroll_off = figure.scroll_offset_y();
        float max_scroll = std::max(0.0f, content_h - bounds.h);

        constexpr float SCROLLBAR_WIDTH = 12.0f;
        constexpr float SCROLLBAR_PAD   = 2.0f;
        constexpr float MIN_THUMB_H     = 20.0f;

        float sb_x = bounds.x + bounds.w - SCROLLBAR_WIDTH - SCROLLBAR_PAD;
        float sb_y = bounds.y + SCROLLBAR_PAD;
        float sb_h = bounds.h - SCROLLBAR_PAD * 2.0f;

        // Thumb size proportional to visible/total ratio
        float ratio   = bounds.h / content_h;
        float thumb_h = std::max(MIN_THUMB_H, sb_h * ratio);
        float track_h = sb_h - thumb_h;
        float thumb_y = sb_y + (max_scroll > 0.0f ? (scroll_off / max_scroll) * track_h : 0.0f);

        // Interactive scrollbar: invisible window over the scrollbar track
        ImGui::SetNextWindowPos(ImVec2(sb_x, sb_y));
        ImGui::SetNextWindowSize(ImVec2(SCROLLBAR_WIDTH, sb_h));
        ImGuiWindowFlags sb_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##page_scrollbar", nullptr, sb_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(sb_x, sb_y));
            ImGui::InvisibleButton("##sb_track", ImVec2(SCROLLBAR_WIDTH, sb_h));
            bool track_hovered = ImGui::IsItemHovered();
            bool track_active  = ImGui::IsItemActive();

            if (track_active && max_scroll > 0.0f)
            {
                float mouse_y    = ImGui::GetIO().MousePos.y;
                float rel        = (mouse_y - sb_y - thumb_h * 0.5f) / track_h;
                rel              = std::clamp(rel, 0.0f, 1.0f);
                float new_scroll = rel * max_scroll;
                figure.set_scroll_offset_y(new_scroll);
                scroll_off = new_scroll;
                thumb_y    = sb_y + rel * track_h;
            }

            // Determine thumb visual state
            bool thumb_hovered = track_hovered && ImGui::GetIO().MousePos.y >= thumb_y
                                 && ImGui::GetIO().MousePos.y <= thumb_y + thumb_h;

            ImDrawList* dl = ImGui::GetForegroundDrawList();

            // Track background
            auto  bg        = theme_colors().bg_elevated;
            ImU32 track_col = IM_COL32(static_cast<uint8_t>(bg.r * 255),
                                       static_cast<uint8_t>(bg.g * 255),
                                       static_cast<uint8_t>(bg.b * 255),
                                       100);
            dl->AddRectFilled(ImVec2(sb_x, sb_y),
                              ImVec2(sb_x + SCROLLBAR_WIDTH, sb_y + sb_h),
                              track_col,
                              SCROLLBAR_WIDTH * 0.5f);

            // Thumb with hover/active highlight
            auto    accent    = theme_colors().accent;
            uint8_t alpha     = track_active ? 240 : (thumb_hovered ? 210 : 180);
            ImU32   thumb_col = IM_COL32(static_cast<uint8_t>(accent.r * 255),
                                       static_cast<uint8_t>(accent.g * 255),
                                       static_cast<uint8_t>(accent.b * 255),
                                       alpha);
            dl->AddRectFilled(ImVec2(sb_x, thumb_y),
                              ImVec2(sb_x + SCROLLBAR_WIDTH, thumb_y + thumb_h),
                              thumb_col,
                              SCROLLBAR_WIDTH * 0.5f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void ImGuiIntegration::draw_inspector(Figure& figure)
{
    if (!layout_manager_)
        return;

    Rect bounds = layout_manager_->inspector_rect();
    if (bounds.w < 1.0f)
        return;   // Fully collapsed

    // Draw resize handle as a separate invisible window so it extends outside the inspector
    {
        float handle_w = LayoutManager::RESIZE_HANDLE_WIDTH;
        float handle_x = bounds.x - handle_w * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(handle_x, bounds.y));
        ImGui::SetNextWindowSize(ImVec2(handle_w, bounds.h));
        ImGuiWindowFlags handle_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##inspector_resize_handle", nullptr, handle_flags))
        {
            ImGui::SetCursorScreenPos(ImVec2(handle_x, bounds.y));
            ImGui::InvisibleButton("##resize_grip", ImVec2(handle_w, bounds.h));
            bool hovered = ImGui::IsItemHovered();
            bool active  = ImGui::IsItemActive();
            layout_manager_->set_inspector_resize_hovered(hovered);

            if (hovered || active)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemClicked())
            {
                layout_manager_->set_inspector_resize_active(true);
            }
            if (active)
            {
                float right_edge = bounds.x + bounds.w;
                float new_width  = right_edge - ImGui::GetIO().MousePos.x;
                layout_manager_->set_inspector_width(new_width);
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                layout_manager_->set_inspector_resize_active(false);
            }

            // Visual resize indicator line
            if (hovered || active)
            {
                ImDrawList* dl       = ImGui::GetWindowDrawList();
                float       line_x   = bounds.x;
                auto        accent   = theme_colors().accent;
                ImU32       line_col = active ? IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   255)
                                              : IM_COL32(uint8_t(accent.r * 255),
                                                   uint8_t(accent.g * 255),
                                                   uint8_t(accent.b * 255),
                                                   120);
                dl->AddLine(ImVec2(line_x, bounds.y),
                            ImVec2(line_x, bounds.y + bounds.h),
                            line_col,
                            active ? 3.0f : 2.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Inspector panel itself
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::PANEL_PADDING + 4.0f, ui::tokens::PANEL_PADDING));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));   // Inner hairline drawn manually

    if (ImGui::Begin("##inspector", nullptr, flags))
    {
        // Floating surface depth cue: soft shadow gradient on left edge
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Multi-layer soft shadow (simulates elevation from canvas)
            float shadow_spread = ui::tokens::ELEVATION_2_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                float t     = static_cast<float>(i) / 4.0f;
                float alpha = 0.12f * (1.0f - t);
                float off   = shadow_spread * t;
                dl->AddRectFilled(ImVec2(bounds.x - off - 1.0f, bounds.y),
                                  ImVec2(bounds.x, bounds.y + bounds.h),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }

            // Crisp material edge: hairline border on left
            dl->AddLine(ImVec2(bounds.x, bounds.y),
                        ImVec2(bounds.x, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors().border_subtle.r,
                                                              theme_colors().border_subtle.g,
                                                              theme_colors().border_subtle.b,
                                                              0.50f)),
                        1.0f);
        }

        // ── Panel eyebrow header ──
        {
            const auto& colors = theme_colors();
            ImGui::PushFont(font_heading_);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.75f));
            ImGui::TextUnformatted("INSPECTOR");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6.0f));
        }

        // ── Inspector tab bar (segmented control) ──
        {
            const auto& colors  = theme_colors();
            float       avail_w = ImGui::GetContentRegionAvail().x;

            struct TabDef
            {
                const char* label;
                ui::Icon    icon;
                Section     section;
            };
            TabDef tabs[] = {
                {"Figure", ui::Icon::ChartLine, Section::Figure},
                {"Series", ui::Icon::Palette, Section::Series},
                {"Axes", ui::Icon::Axes, Section::Axes},
                {"Data", ui::Icon::Database, Section::DataEditor},
            };
            constexpr int tab_count = 4;

            const float track_pad = ui::tokens::SEGMENT_TRACK_PAD;
            const float track_h   = ui::tokens::SEGMENT_TAB_H;
            const float tab_w     = std::floor((avail_w - track_pad * 2.0f) / tab_count);

            ImVec2      track_min = ImGui::GetCursorScreenPos();
            ImVec2      track_max = ImVec2(track_min.x + avail_w, track_min.y + track_h);
            ImDrawList* dl        = ImGui::GetWindowDrawList();

            // Recessed track background — establishes the segmented control surface.
            dl->AddRectFilled(
                track_min,
                track_max,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, 0.55f)),
                ui::tokens::RADIUS_MD);
            dl->AddRect(track_min,
                        track_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                              colors.border_subtle.g,
                                                              colors.border_subtle.b,
                                                              0.45f)),
                        ui::tokens::RADIUS_MD);

            ImGui::PushFont(font_heading_);
            for (int i = 0; i < tab_count; ++i)
            {
                bool   is_active = (active_section_ == tabs[i].section);
                float  seg_x     = track_min.x + track_pad + tab_w * static_cast<float>(i);
                ImVec2 seg_min(seg_x, track_min.y + track_pad);
                ImVec2 seg_max(seg_x + tab_w, track_max.y - track_pad);

                ImGui::SetCursorScreenPos(seg_min);
                const std::string btn_id = std::format("##insp_tab_{}", i);
                if (ImGui::InvisibleButton(btn_id.c_str(), ImVec2(tab_w, seg_max.y - seg_min.y)))
                {
                    active_section_ = tabs[i].section;
                    if (tabs[i].section != Section::Series)
                        selection_ctx_.clear();
                }
                bool hovered = ImGui::IsItemHovered();

                // Active segment: raised accent-tinted pill with soft glow.
                if (is_active)
                {
                    dl->AddRectFilled(
                        seg_min,
                        seg_max,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.20f)),
                        ui::tokens::RADIUS_SM);
                    dl->AddRect(
                        seg_min,
                        seg_max,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.55f)),
                        ui::tokens::RADIUS_SM,
                        0,
                        1.0f);
                }
                else if (hovered)
                {
                    dl->AddRectFilled(seg_min,
                                      seg_max,
                                      ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                                            colors.text_secondary.g,
                                                                            colors.text_secondary.b,
                                                                            0.10f)),
                                      ui::tokens::RADIUS_SM);
                }

                // Centered label.
                ImVec4 txt = is_active
                                 ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f)
                                 : ImVec4(colors.text_secondary.r,
                                          colors.text_secondary.g,
                                          colors.text_secondary.b,
                                          hovered ? 1.0f : 0.80f);
                ImVec2 tsz = ImGui::CalcTextSize(tabs[i].label);
                dl->AddText(ImVec2(seg_x + (tab_w - tsz.x) * 0.5f,
                                   seg_min.y + (seg_max.y - seg_min.y - tsz.y) * 0.5f),
                            ImGui::ColorConvertFloat4ToU32(txt),
                            tabs[i].label);
            }
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(track_min.x, track_max.y));
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_3));
        }

        // Scrollable content area
        ImGui::BeginChild("##inspector_content", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);

        if (panel_open_ || panel_anim_ > 0.01f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panel_anim_);

            // Clear stale selection when switching to a different figure/tab
            if (selection_ctx_.type == ui::SelectionType::Series
                && selection_ctx_.figure != &figure)
            {
                selection_ctx_.clear();
            }

            // Update selection context based on active nav rail section.
            // When the Series section is active and the user has drilled into
            // a specific series, preserve that selection so the properties
            // panel stays visible. Switching to any other section always
            // overrides the selection.
            if (active_section_ == Section::DataEditor)
            {
                // Data editor mode: draw tabular data view instead of inspector
                data_editor_.draw(figure);
            }
            else
            {
                switch (active_section_)
                {
                    case Section::Figure:
                        selection_ctx_.select_figure(&figure);
                        break;
                    case Section::Series:
                        // Only show browser if user hasn't selected a specific series
                        if (selection_ctx_.type != ui::SelectionType::Series)
                        {
                            selection_ctx_.select_series_browser(&figure);
                        }
                        break;
                    case Section::Axes:
                        // Only show browser if user hasn't selected a specific axes.
                        // When switching figures, try to preserve the axes index if valid.
                        if (figure.axes().empty())
                        {
                            // No axes in this figure, clear selection
                            selection_ctx_.clear();
                        }
                        else if (selection_ctx_.type != ui::SelectionType::Axes)
                        {
                            selection_ctx_.select_axes(&figure, figure.axes_mut()[0].get(), 0);
                        }
                        else if (selection_ctx_.figure != &figure)
                        {
                            // User has axes selected but switched to a different figure.
                            // Try to select the same axes index in the new figure.
                            int target_idx = selection_ctx_.axes_index;
                            if (target_idx >= 0
                                && target_idx < static_cast<int>(figure.axes().size()))
                            {
                                selection_ctx_.select_axes(&figure,
                                                           figure.axes_mut()[target_idx].get(),
                                                           target_idx);
                            }
                            else
                            {
                                // Index out of range, fall back to first axes
                                selection_ctx_.select_axes(&figure, figure.axes_mut()[0].get(), 0);
                            }
                        }
                        break;
                    case Section::DataEditor:
                        break;   // Handled above
                }

                inspector_.set_context(selection_ctx_);
                inspector_.draw(figure);

                // Read back context (inspector may change selection, e.g. clicking a series)
                selection_ctx_ = inspector_.context();
            }

            ImGui::PopStyleVar();
        }

        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void ImGuiIntegration::draw_inspector_toggle()
{
    if (!layout_manager_)
        return;

    Rect canvas  = layout_manager_->canvas_rect();
    bool is_open = layout_manager_->is_inspector_visible();

    // Chevron tab on the canvas/inspector seam — wide hit target, compact visual pill.
    constexpr float VIS_W           = 22.0f;
    constexpr float VIS_H           = 52.0f;
    constexpr float BTN_ROUNDING    = 11.0f;
    constexpr float HIT_EXTRA_LEFT  = 12.0f;
    constexpr float HIT_EXTRA_VERT  = 10.0f;
    const float     hit_w           = VIS_W + HIT_EXTRA_LEFT;
    const float     hit_h           = VIS_H + 2.0f * HIT_EXTRA_VERT;

    float vis_x = canvas.x + canvas.w - VIS_W;
    if (is_open)
    {
        Rect insp = layout_manager_->inspector_rect();
        vis_x     = insp.x - VIS_W;
    }
    const float vis_y = canvas.y + (canvas.h - VIS_H) * 0.5f;
    const float win_x = vis_x - HIT_EXTRA_LEFT;
    const float win_y = vis_y - HIT_EXTRA_VERT;

    ImGui::SetNextWindowPos(ImVec2(win_x, win_y));
    ImGui::SetNextWindowSize(ImVec2(hit_w, hit_h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##inspector_toggle", nullptr, flags))
    {
        ImGui::InvisibleButton("##insp_toggle_btn", ImVec2(hit_w, hit_h));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        if (clicked)
        {
            if (is_open)
            {
                layout_manager_->set_inspector_visible(false);
                panel_open_ = false;
            }
            else
            {
                layout_manager_->set_inspector_visible(true);
                panel_open_ = true;
            }
        }

        const auto& colors = theme_colors();
        ImDrawList* dl     = ImGui::GetWindowDrawList();

        // Chevron tab — always visible (Vision.png right-edge collapse control).
        const ImDrawFlags tab_corners = ImDrawFlags_RoundCornersLeft;
        {
            const uint8_t bg_alpha = hovered ? 230 : 175;
            ImU32         bg_col   = IM_COL32(static_cast<uint8_t>(colors.bg_tertiary.r * 255),
                                              static_cast<uint8_t>(colors.bg_tertiary.g * 255),
                                              static_cast<uint8_t>(colors.bg_tertiary.b * 255),
                                              bg_alpha);
            dl->AddRectFilled(ImVec2(vis_x, vis_y),
                              ImVec2(vis_x + VIS_W, vis_y + VIS_H),
                              bg_col,
                              BTN_ROUNDING,
                              tab_corners);

            ui::Color edge = colors.accent.lerp(ui::glass_palette::kAccentMagenta, 0.30f);
            dl->AddRect(ImVec2(vis_x, vis_y),
                        ImVec2(vis_x + VIS_W, vis_y + VIS_H),
                        IM_COL32(static_cast<uint8_t>(edge.r * 255),
                                 static_cast<uint8_t>(edge.g * 255),
                                 static_cast<uint8_t>(edge.b * 255),
                                 hovered ? 210 : 120),
                        BTN_ROUNDING,
                        tab_corners,
                        hovered ? 1.5f : 1.0f);
        }

        // Chevron icon centered in the tab
        ImFont* icon_font_ptr = font_icon_;
        if (icon_font_ptr)
        {
            const char* chevron =
                is_open ? ui::icon_str(ui::Icon::ChevronRight) : ui::icon_str(ui::Icon::Back);

            ImU32 icon_col =
                hovered ? ImGui::ColorConvertFloat4ToU32(
                              ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f))
                        : ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                                colors.text_primary.g,
                                                                colors.text_primary.b,
                                                                0.72f));

            const float icon_sz = icon_font_ptr->LegacySize * 1.15f;
            ImVec2      isz     = icon_font_ptr->CalcTextSizeA(icon_sz, FLT_MAX, 0.0f, chevron);
            const float ix      = std::floor(vis_x + (VIS_W - isz.x) * 0.5f);
            const float iy      = std::floor(vis_y + (VIS_H - icon_sz) * 0.5f);
            dl->AddText(icon_font_ptr, icon_sz, ImVec2(ix, iy), icon_col, chevron);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ImGuiIntegration::draw_status_bar()
{
    if (!layout_manager_)
        return;

    if (app_shell_)
    {
        app_shell_->draw_status_bar();
        return;
    }

    ui::shell::StatusBar bar;
    bar.set_layout_manager(layout_manager_.get());
    populate_status_bar(bar);
    bar.draw();
}

void ImGuiIntegration::populate_status_bar(ui::shell::StatusBar& bar)
{
    if (!layout_manager_)
        return;

    bar.clear();
    bar.set_layout_manager(layout_manager_.get());

    bar.add_segment({.align = ui::shell::StatusAlign::Left,
                     .draw_fn = [this]()
                     {
        const auto& colors = theme_colors();
        ImGui::PushFont(font_heading_);

        // Use the column child window's actual height, not the outer status-bar
        // rect: the bar reserves vertical WindowPadding, so the child is shorter
        // than the full bar height. Centering against the wrong (larger) height
        // pushes pills past the child's clip rect and trims their bottom edge.
        const float bar_h       = ImGui::GetWindowHeight();
        const float text_h      = ImGui::GetTextLineHeight();
        const float pill_h      = ui::tokens::STATUS_BAR_PILL_HEIGHT;
        const float pill_pad_h  = ui::tokens::STATUS_BAR_PILL_PAD_H;
        const float pill_pad_v  = ui::tokens::STATUS_BAR_PILL_PAD_V;
        const float pill_radius = ui::tokens::STATUS_BAR_PILL_RADIUS;

        float y_offset = (bar_h - pill_h) * 0.5f;
        ImGui::SetCursorPosY(y_offset + (pill_h - text_h) * 0.5f);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto draw_pill = [&](const char*      label,
                             const ui::Color& text_col,
                             const ui::Color& bg_col,
                             float            bg_alpha,
                             const ui::Color& border_col)
        {
            ImVec2 text_sz  = ImGui::CalcTextSize(label);
            ImVec2 cursor_p = ImGui::GetCursorScreenPos();
            ImVec2 pill_min(cursor_p.x - pill_pad_h,
                            cursor_p.y - (pill_h - text_h) * 0.5f - pill_pad_v);
            ImVec2 pill_max(cursor_p.x + text_sz.x + pill_pad_h,
                            cursor_p.y + text_h + (pill_h - text_h) * 0.5f + pill_pad_v);

            dl->AddRectFilled(pill_min,
                              pill_max,
                              IM_COL32(static_cast<uint8_t>(bg_col.r * 255),
                                       static_cast<uint8_t>(bg_col.g * 255),
                                       static_cast<uint8_t>(bg_col.b * 255),
                                       static_cast<uint8_t>(bg_alpha * 255)),
                              pill_radius);
            dl->AddRect(pill_min,
                        pill_max,
                        IM_COL32(static_cast<uint8_t>(border_col.r * 255),
                                 static_cast<uint8_t>(border_col.g * 255),
                                 static_cast<uint8_t>(border_col.b * 255),
                                 static_cast<uint8_t>((bg_alpha * 0.60f) * 255)),
                        pill_radius,
                        0,
                        1.0f);

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(text_col.r,
                       text_col.g,
                       text_col.b,
                       ui::tokens::STATUS_BAR_TEXT_ALPHA));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        };

        {
            const std::string cursor_buf =
                cursor_data_valid_
                    ? std::format("X: {:.4f}  Y: {:.4f}", cursor_data_x_, cursor_data_y_)
                    : "X: —  Y: —";
            draw_pill(cursor_buf.c_str(), colors.text_secondary, colors.bg_tertiary, 0.32f, colors.border_subtle);
        }

        ImGui::SameLine(0.0f, ui::tokens::STATUS_BAR_GROUP_GAP);
        {
            const char* mode_label = "Navigate";
            ui::Color   mode_color = colors.text_secondary;
            switch (interaction_mode_)
            {
                case ToolMode::Pan:
                    mode_label = "Pan";
                    mode_color = colors.accent;
                    break;
                case ToolMode::BoxZoom:
                    mode_label = "Box Zoom";
                    mode_color = colors.warning;
                    break;
                case ToolMode::Select:
                    mode_label = "Select";
                    mode_color = colors.info;
                    break;
                case ToolMode::ROI:
                    mode_label = "ROI";
                    mode_color = colors.info;
                    break;
                case ToolMode::Measure:
                    mode_label = "Measure";
                    mode_color = colors.success;
                    break;
                case ToolMode::Annotate:
                    mode_label = "Annotate";
                    mode_color = colors.accent;
                    break;
                default:
                    break;
            }
            draw_pill(mode_label, mode_color, colors.bg_tertiary, 0.20f, mode_color);
        }

        ImGui::SameLine(0.0f, ui::tokens::STATUS_BAR_GROUP_GAP);
        {
            std::string zoom_buf;
            double      zoom_pct = static_cast<double>(zoom_level_) * 100.0;
            if (zoom_pct < 10000.0)
                zoom_buf = std::format("Zoom: {:.0f}%", zoom_pct);
            else if (zoom_pct < 1e7)
                zoom_buf = std::format("Zoom: {:.1f}K%", zoom_pct / 1e3);
            else if (zoom_pct < 1e10)
                zoom_buf = std::format("Zoom: {:.1f}M%", zoom_pct / 1e6);
            else if (zoom_pct < 1e13)
                zoom_buf = std::format("Zoom: {:.1f}G%", zoom_pct / 1e9);
            else
                zoom_buf = std::format("Zoom: {:.2e}%", zoom_pct);
            draw_pill(zoom_buf.c_str(), colors.text_secondary, colors.bg_tertiary, 0.24f, colors.border_subtle);
        }

        ImGui::PopFont();
                     }});

    bar.add_segment({.align = ui::shell::StatusAlign::Right,
                     .draw_fn = [this]()
                     {
        const auto& colors = theme_colors();
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushFont(font_heading_);

        const float text_h      = ImGui::GetTextLineHeight();
        const float pill_h      = ui::tokens::STATUS_BAR_PILL_HEIGHT;
        const float pill_radius = ui::tokens::STATUS_BAR_PILL_RADIUS;
        // See left-segment comment: center against the actual column child
        // height, not the outer bar rect (which includes vertical padding).
        const float bar_h       = ImGui::GetWindowHeight();

        const std::string fps_buf = std::format("{} fps", static_cast<int>(io.Framerate));
        const std::string gpu_buf = std::format("GPU: {:.1f}ms", gpu_time_ms_);
        const float       perf_gap   = ui::tokens::STATUS_BAR_PERF_GAP;
        const float       fps_pad_h  = ui::tokens::STATUS_BAR_FPS_PILL_PAD_H;
        const float       fps_pad_v  = ui::tokens::STATUS_BAR_FPS_PILL_PAD_V;
        const float       right_w    = ImGui::CalcTextSize(fps_buf.c_str()).x + fps_pad_h * 2.0f + perf_gap
                              + ImGui::CalcTextSize(gpu_buf.c_str()).x;
        const float right_x    = ImGui::GetWindowWidth() - right_w - ui::tokens::STATUS_BAR_PADDING_H;
        const float center_end = ImGui::GetCursorPosX();
        const bool  show_perf  = right_x > center_end + ui::tokens::SPACE_3;
        if (!show_perf)
        {
            ImGui::PopFont();
            return;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(right_x);
        ImGui::SetCursorPosY((bar_h - text_h) * 0.5f);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        {
            ImVec2      text_sz  = ImGui::CalcTextSize(fps_buf.c_str());
            ImVec2      cursor_p = ImGui::GetCursorScreenPos();
            ImVec2      pill_min(cursor_p.x - fps_pad_h,
                            cursor_p.y - (pill_h - text_h) * 0.5f - fps_pad_v);
            ImVec2      pill_max(cursor_p.x + text_sz.x + fps_pad_h,
                            cursor_p.y + text_h + (pill_h - text_h) * 0.5f + fps_pad_v);
            const float glow =
                theme_mgr_ ? theme_mgr_->effective_glow_intensity() : 0.0f;
            const ui::Color& fps_fill =
                glow > 0.01f ? ui::glass_palette::kFpsPillGreen : colors.bg_tertiary;
            const ui::Color& fps_border =
                glow > 0.01f ? ui::glass_palette::kFpsPillBorder : colors.border_subtle;
            const ui::Color& fps_text =
                glow > 0.01f ? ui::glass_palette::kFpsPillText : colors.success;

            dl->AddRectFilled(pill_min,
                              pill_max,
                              IM_COL32(static_cast<uint8_t>(fps_fill.r * 255),
                                       static_cast<uint8_t>(fps_fill.g * 255),
                                       static_cast<uint8_t>(fps_fill.b * 255),
                                       255),
                              pill_radius);
            dl->AddRect(pill_min,
                        pill_max,
                        IM_COL32(static_cast<uint8_t>(fps_border.r * 255),
                                 static_cast<uint8_t>(fps_border.g * 255),
                                 static_cast<uint8_t>(fps_border.b * 255),
                                 235),
                        pill_radius,
                        0,
                        1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(fps_text.r, fps_text.g, fps_text.b, 1.0f));
            ImGui::TextUnformatted(fps_buf.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(0.0f, perf_gap);
        const ui::Color& gpu_text =
            (theme_mgr_ && theme_mgr_->effective_glow_intensity() > 0.01f)
                ? ui::glass_palette::kStatusGpuText
                : colors.text_tertiary;
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(gpu_text.r, gpu_text.g, gpu_text.b, 1.0f));
        ImGui::TextUnformatted(gpu_buf.c_str());
        ImGui::PopStyleColor();

        ImGui::PopFont();
                     }});
}


void ImGuiIntegration::draw_split_view_splitters()
{
    if (!dock_system_)
        return;

    auto*  draw_list = ImGui::GetForegroundDrawList();
    auto&  theme     = theme_colors();
    ImVec2 mouse     = ImGui::GetMousePos();

    // ── Non-split drag-to-split overlay ──────────────────────────────────
    // When NOT split and a tab is being dock-dragged, show edge zone
    // highlights to suggest splitting (like VSCode).
    if (!dock_system_->is_split() && dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        // Only show edge zones (Left/Right/Top/Bottom), not Center
        if (target.zone != DropZone::None && target.zone != DropZone::Center)
        {
            Rect  hr               = target.highlight_rect;
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             40);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              160);

            draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                     ImVec2(hr.x + hr.w, hr.y + hr.h),
                                     highlight_color,
                                     4.0f);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               4.0f,
                               0,
                               2.0f);

            // Draw a label indicating the split direction
            const char* label = nullptr;
            switch (target.zone)
            {
                case DropZone::Left:
                    label = "Split Left";
                    break;
                case DropZone::Right:
                    label = "Split Right";
                    break;
                case DropZone::Top:
                    label = "Split Up";
                    break;
                case DropZone::Bottom:
                    label = "Split Down";
                    break;
                default:
                    break;
            }
            if (label)
            {
                ImVec2 lsz = ImGui::CalcTextSize(label);
                float  lx  = hr.x + (hr.w - lsz.x) * 0.5f;
                float  ly  = hr.y + (hr.h - lsz.y) * 0.5f;
                draw_list->AddText(ImVec2(lx, ly),
                                   IM_COL32(static_cast<int>(theme.accent.r * 255),
                                            static_cast<int>(theme.accent.g * 255),
                                            static_cast<int>(theme.accent.b * 255),
                                            200),
                                   label);
            }
        }
        return;   // No splitters to draw in non-split mode
    }

    if (!dock_system_->is_split())
        return;

    // Handle pane activation on mouse click in canvas area
    // (skip if mouse is over a pane tab header — that's handled by draw_pane_tab_headers)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse
        && !pane_tab_hovered_)
    {
        dock_system_->activate_pane_at(mouse.x, mouse.y);
    }

    // Handle splitter interaction
    if (dock_system_->is_over_splitter(mouse.x, mouse.y))
    {
        auto dir = dock_system_->splitter_direction_at(mouse.x, mouse.y);
        ImGui::SetMouseCursor(dir == SplitDirection::Horizontal ? ImGuiMouseCursor_ResizeEW
                                                                : ImGuiMouseCursor_ResizeNS);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            dock_system_->begin_splitter_drag(mouse.x, mouse.y);
        }
    }

    if (dock_system_->is_dragging_splitter())
    {
        auto* sp = dock_system_->split_view().dragging_splitter();
        if (sp)
        {
            float pos = (sp->split_direction() == SplitDirection::Horizontal) ? mouse.x : mouse.y;
            dock_system_->update_splitter_drag(pos);
            ImGui::SetMouseCursor(sp->split_direction() == SplitDirection::Horizontal
                                      ? ImGuiMouseCursor_ResizeEW
                                      : ImGuiMouseCursor_ResizeNS);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            dock_system_->end_splitter_drag();
        }
    }

    // Draw splitter handles for all internal nodes
    auto pane_infos = dock_system_->get_pane_infos();

    // Walk the split tree to find internal nodes and draw their splitters
    std::function<void(SplitPane*)> draw_splitters = [&](SplitPane* node)
    {
        if (!node || node->is_leaf())
            return;

        Rect sr          = node->splitter_rect();
        bool is_dragging = dock_system_->is_dragging_splitter()
                           && dock_system_->split_view().dragging_splitter() == node;

        // Splitter background
        ImU32 splitter_color = 0;
        if (is_dragging)
        {
            splitter_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                      static_cast<int>(theme.accent.g * 255),
                                      static_cast<int>(theme.accent.b * 255),
                                      200);
        }
        else
        {
            splitter_color = IM_COL32(static_cast<int>(theme.border_default.r * 255),
                                      static_cast<int>(theme.border_default.g * 255),
                                      static_cast<int>(theme.border_default.b * 255),
                                      120);
        }

        draw_list->AddRectFilled(ImVec2(sr.x, sr.y),
                                 ImVec2(sr.x + sr.w, sr.y + sr.h),
                                 splitter_color);

        // Draw a subtle grip indicator in the center of the splitter
        float cx         = sr.x + sr.w * 0.5f;
        float cy         = sr.y + sr.h * 0.5f;
        ImU32 grip_color = IM_COL32(static_cast<int>(theme.text_tertiary.r * 255),
                                    static_cast<int>(theme.text_tertiary.g * 255),
                                    static_cast<int>(theme.text_tertiary.b * 255),
                                    150);

        if (node->split_direction() == SplitDirection::Horizontal)
        {
            // Vertical splitter — draw horizontal grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx, cy + i * 6.0f), 1.5f, grip_color);
            }
        }
        else
        {
            // Horizontal splitter — draw vertical grip dots
            for (int i = -2; i <= 2; ++i)
            {
                draw_list->AddCircleFilled(ImVec2(cx + i * 6.0f, cy), 1.5f, grip_color);
            }
        }

        // Recurse into children
        draw_splitters(node->first());
        draw_splitters(node->second());
    };

    draw_splitters(dock_system_->split_view().root());

    // Draw active pane border highlight
    for (const auto& info : pane_infos)
    {
        if (info.is_active && pane_infos.size() > 1)
        {
            ImU32 border_color = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                          static_cast<int>(theme.accent.g * 255),
                                          static_cast<int>(theme.accent.b * 255),
                                          180);
            draw_list->AddRect(ImVec2(info.bounds.x, info.bounds.y),
                               ImVec2(info.bounds.x + info.bounds.w, info.bounds.y + info.bounds.h),
                               border_color,
                               0.0f,
                               0,
                               2.0f);
        }
    }

    // Draw drop zone highlight during drag-to-dock
    if (dock_system_->is_dragging())
    {
        auto target = dock_system_->current_drop_target();
        if (target.zone != DropZone::None)
        {
            Rect  hr               = target.highlight_rect;
            ImU32 highlight_color  = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                             static_cast<int>(theme.accent.g * 255),
                                             static_cast<int>(theme.accent.b * 255),
                                             60);
            ImU32 highlight_border = IM_COL32(static_cast<int>(theme.accent.r * 255),
                                              static_cast<int>(theme.accent.g * 255),
                                              static_cast<int>(theme.accent.b * 255),
                                              180);

            draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                     ImVec2(hr.x + hr.w, hr.y + hr.h),
                                     highlight_color);
            draw_list->AddRect(ImVec2(hr.x, hr.y),
                               ImVec2(hr.x + hr.w, hr.y + hr.h),
                               highlight_border,
                               0.0f,
                               0,
                               2.0f);
        }
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Topics drag-drop target on the canvas.
//
// While the user drags a "SPECTRA_TOPIC" payload from the Topics panel, an
// invisible drop overlay covers the canvas region.  The axes under the mouse
// is hit-tested via input_handler_; on drop, the panel's submit_subscribe()
// fires the subscribe IPC with the resolved axes index.
// ───────────────────────────────────────────────────────────────────────────
void ImGuiIntegration::draw_topic_drop_target(Figure& figure)
{
    if (!topics_panel_ || !layout_manager_ || !input_handler_)
        return;
    if (!topics_panel_->has_subscribe_callback() || topics_panel_->target_figure_id() == 0)
        return;

    // Only show the drop overlay while a topic payload is being dragged.
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(ui::topics::TOPIC_DRAG_TYPE))
        return;

    Rect bounds = layout_manager_->canvas_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Highlight the axes currently under the mouse, so the user sees which
    // subplot the drop will land on.
    ImGuiIO&    io        = ImGui::GetIO();
    AxesBase*   hover_ax  = input_handler_->hit_test_all_axes(static_cast<double>(io.MousePos.x),
                                                           static_cast<double>(io.MousePos.y));
    int         hover_idx = -1;
    const auto& all       = figure.all_axes();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].get() == hover_ax)
        {
            hover_idx = static_cast<int>(i);
            break;
        }
    }

    if (hover_ax && hover_idx >= 0)
    {
        const auto& vp     = hover_ax->viewport();
        ImDrawList* dl     = ImGui::GetForegroundDrawList();
        const auto& th     = theme_colors();
        ImU32       fill   = IM_COL32(static_cast<int>(th.accent.r * 255),
                              static_cast<int>(th.accent.g * 255),
                              static_cast<int>(th.accent.b * 255),
                              40);
        ImU32       border = IM_COL32(static_cast<int>(th.accent.r * 255),
                                static_cast<int>(th.accent.g * 255),
                                static_cast<int>(th.accent.b * 255),
                                220);
        dl->AddRectFilled(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), fill, 4.0f);
        dl->AddRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), border, 4.0f, 0, 2.0f);
    }

    // Invisible drop-target window covering the canvas.  Inputs are enabled
    // only while a drag is in progress, so normal canvas mouse handling is
    // unaffected outside of drag-and-drop.
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##topic_drop_overlay", nullptr, flags))
    {
        ImGui::InvisibleButton("##topic_drop_btn", ImVec2(bounds.w, bounds.h));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ui::topics::TOPIC_DRAG_TYPE))
            {
                if (p->DataSize == sizeof(ui::topics::TopicDragPayload))
                {
                    const auto*    dp = static_cast<const ui::topics::TopicDragPayload*>(p->Data);
                    std::string    name(dp->name);
                    const uint32_t axes_idx =
                        (hover_idx >= 0) ? static_cast<uint32_t>(hover_idx) : 0u;
                    topics_panel_->submit_subscribe(name,
                                                    topics_panel_->target_figure_id(),
                                                    axes_idx);
                    SPECTRA_LOG_INFO(
                        "topics",
                        "Drop subscribe topic=" + name + " axes=" + std::to_string(axes_idx));
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// Welcome-state topic drop target.
//
// Shown when there are no figures yet (build_empty_ui).  The drop zone covers
// the viewport background.  We use BeginDragDropTargetViewport() which is
// independent of ImGui window Z-order: it activates whenever the mouse is
// over the main viewport and NOT hovering any floating window (Topics Panel,
// etc.).  On drop, submit_subscribe() fires with figure_id=0 so the daemon
// auto-creates a new figure for the window.
// ───────────────────────────────────────────────────────────────────────────
void ImGuiIntegration::draw_topic_drop_target_welcome()
{
    if (!topics_panel_ || !topics_panel_->has_subscribe_callback())
        return;

    // Only run while a topic payload is being dragged.
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(ui::topics::TOPIC_DRAG_TYPE))
        return;

    ImGuiIO&    io = ImGui::GetIO();
    const float w  = io.DisplaySize.x;
    const float h  = io.DisplaySize.y;

    // True when the mouse is over the background (no floating window underneath).
    const bool over_bg = !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow
                                                 | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Draw a visual hint so the user can see the drop zone.
    ImDrawList* dl     = ImGui::GetForegroundDrawList();
    const auto& th     = theme_colors();
    const float margin = 20.0f;

    ImU32 fill   = IM_COL32(static_cast<int>(th.accent.r * 255),
                          static_cast<int>(th.accent.g * 255),
                          static_cast<int>(th.accent.b * 255),
                          over_bg ? 40 : 18);
    ImU32 border = IM_COL32(static_cast<int>(th.accent.r * 255),
                            static_cast<int>(th.accent.g * 255),
                            static_cast<int>(th.accent.b * 255),
                            over_bg ? 220 : 90);
    dl->AddRectFilled(ImVec2(margin, margin), ImVec2(w - margin, h - margin), fill, 8.0f);
    dl->AddRect(ImVec2(margin, margin), ImVec2(w - margin, h - margin), border, 8.0f, 0, 2.0f);

    const char* label    = "Drop to create new figure";
    ImVec2      text_sz  = ImGui::CalcTextSize(label);
    ImU32       text_col = IM_COL32(static_cast<int>(th.accent.r * 255),
                              static_cast<int>(th.accent.g * 255),
                              static_cast<int>(th.accent.b * 255),
                              over_bg ? 220 : 100);
    dl->AddText(ImVec2((w - text_sz.x) * 0.5f, h * 0.65f), text_col, label);

    // Viewport-level drop target — bypasses window Z-order requirements.
    // BeginDragDropTargetViewport only checks mouse position vs viewport rect;
    // it does not require the current ImGui window to be the hovered window.
    if (over_bg)
    {
        if (ImGui::BeginDragDropTargetViewport(ImGui::GetMainViewport(), nullptr))
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(ui::topics::TOPIC_DRAG_TYPE))
            {
                if (p->DataSize == sizeof(ui::topics::TopicDragPayload))
                {
                    const auto* dp = static_cast<const ui::topics::TopicDragPayload*>(p->Data);
                    std::string name(dp->name);
                    // figure_id=0 → daemon auto-creates a new figure for this window
                    topics_panel_->submit_subscribe(name, 0, 0);
                    SPECTRA_LOG_INFO("topics",
                                     "Welcome drop: subscribe topic=" + name + " (new figure)");
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
