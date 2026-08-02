#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <format>

    #ifndef M_PI
        #define M_PI 3.14159265358979323846
    #endif

namespace spectra
{

// ─── Timeline Panel ──────────────────────────────────────────────────────────

// Helper: transport icon button with modern styling
static bool transport_button(const char*            icon_label,
                             bool                   active,
                             bool                   accent,
                             ImFont*                font,
                             float                  size,
                             const ui::ThemeColors& colors)
{
    ImGui::PushFont(font);

    ImVec4 bg;
    ImVec4 bg_hover;
    ImVec4 bg_active;
    ImVec4 text_col;
    if (accent)
    {
        bg       = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.9f);
        bg_hover = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
        bg_active =
            ImVec4(colors.accent.r * 0.8f, colors.accent.g * 0.8f, colors.accent.b * 0.8f, 1.0f);
        text_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else if (active)
    {
        bg = ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.35f);
        bg_hover =
            ImVec4(colors.accent_subtle.r, colors.accent_subtle.g, colors.accent_subtle.b, 0.5f);
        bg_active =
            ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.6f);
        text_col = ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f);
    }
    else
    {
        bg = ImVec4(0, 0, 0, 0);
        bg_hover =
            ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.1f);
        bg_active =
            ImVec4(colors.text_secondary.r, colors.text_secondary.g, colors.text_secondary.b, 0.2f);
        text_col = ImVec4(colors.text_secondary.r,
                          colors.text_secondary.g,
                          colors.text_secondary.b,
                          0.85f);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_active);
    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool clicked = ImGui::Button(icon_label, ImVec2(size, size));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    return clicked;
}

void ImGuiIntegration::draw_timeline_panel()
{
    if (!timeline_editor_)
        return;

    const auto& colors = ui::theme();
    ImGuiIO&    io     = ImGui::GetIO();

    float panel_height = layout_manager_ ? layout_manager_->bottom_panel_height() : 200.0f;
    if (panel_height < 1.0f)
        return;   // Animating closed

    float status_bar_h = LayoutManager::STATUS_BAR_HEIGHT;
    float panel_y      = io.DisplaySize.y - status_bar_h - panel_height;
    float nav_w        = layout_manager_ ? layout_manager_->nav_rail_animated_width() : 48.0f;
    float inspector_w  = layout_manager_ ? layout_manager_->inspector_animated_width() : 0.0f;
    float panel_x      = nav_w;
    float panel_w      = io.DisplaySize.x - nav_w - inspector_w;

    // Draw top-border accent line via background draw list
    ImDrawList* bg_dl      = ImGui::GetBackgroundDrawList();
    ImU32       accent_col = IM_COL32(static_cast<int>(colors.accent.r * 255),
                                static_cast<int>(colors.accent.g * 255),
                                static_cast<int>(colors.accent.b * 255),
                                180);
    bg_dl->AddRectFilled(ImVec2(panel_x, panel_y - 1.0f),
                         ImVec2(panel_x + panel_w, panel_y + 1.0f),
                         accent_col);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                             | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
                             | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(colors.border_default.r, colors.border_default.g, colors.border_default.b, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##timeline_panel", nullptr, flags))
    {
        float btn_sz  = 32.0f;
        float btn_gap = 6.0f;

        // ── Panel eyebrow ──
        ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 0.75f));
        ImGui::TextUnformatted("TIMELINE");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4.0f));

        // ── Header row: transport controls (left) + time display (right) ──
        auto pb_state   = timeline_editor_->playback_state();
        bool is_playing = (pb_state == PlaybackState::Playing);
        bool is_paused  = (pb_state == PlaybackState::Paused);

        // Recessed container pill behind the transport cluster (4 buttons).
        {
            constexpr int cluster_btns = 4;
            float         pad          = 5.0f;
            float         cluster_w =
                static_cast<float>(cluster_btns) * btn_sz + (cluster_btns - 1) * btn_gap;
            ImVec2      cp  = ImGui::GetCursorScreenPos();
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            tdl->AddRectFilled(ImVec2(cp.x - pad, cp.y - pad),
                               ImVec2(cp.x + cluster_w + pad, cp.y + btn_sz + pad),
                               IM_COL32(static_cast<int>(colors.bg_primary.r * 255),
                                        static_cast<int>(colors.bg_primary.g * 255),
                                        static_cast<int>(colors.bg_primary.b * 255),
                                        140),
                               ui::tokens::RADIUS_LG);
            tdl->AddRect(ImVec2(cp.x - pad, cp.y - pad),
                         ImVec2(cp.x + cluster_w + pad, cp.y + btn_sz + pad),
                         IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                                  static_cast<int>(colors.border_subtle.g * 255),
                                  static_cast<int>(colors.border_subtle.b * 255),
                                  120),
                         ui::tokens::RADIUS_LG);
        }

        // Step backward
        if (font_icon_)
        {
            if (transport_button(ui::icon_str(ui::Icon::StepBackward),
                                 false,
                                 false,
                                 font_icon_,
                                 btn_sz,
                                 colors))
            {
                timeline_editor_->step_backward();
            }
            ImGui::SameLine(0, btn_gap);

            // Stop
            if (transport_button(ui::icon_str(ui::Icon::Stop),
                                 false,
                                 false,
                                 font_icon_,
                                 btn_sz,
                                 colors))
            {
                timeline_editor_->stop();
            }
            ImGui::SameLine(0, btn_gap);

            // Play/Pause — accent filled when playing
            const char* play_icon =
                is_playing ? ui::icon_str(ui::Icon::Pause) : ui::icon_str(ui::Icon::Play);
            if (transport_button(play_icon, is_paused, is_playing, font_icon_, btn_sz, colors))
            {
                timeline_editor_->toggle_play();
            }
            ImGui::SameLine(0, btn_gap);

            // Step forward
            if (transport_button(ui::icon_str(ui::Icon::StepForward),
                                 false,
                                 false,
                                 font_icon_,
                                 btn_sz,
                                 colors))
            {
                timeline_editor_->step_forward();
            }
        }

        // Time display — right-aligned
        {
            const std::string time_buf = std::format("{:.2f} / {:.2f}",
                                                     timeline_editor_->playhead(),
                                                     timeline_editor_->duration());
            float             time_w   = ImGui::CalcTextSize(time_buf.c_str()).x;
            ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - time_w - 8.0f);

            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_secondary.r,
                                         colors.text_secondary.g,
                                         colors.text_secondary.b,
                                         0.6f));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", time_buf.c_str());
            ImGui::PopStyleColor();
        }

        // Subtle separator
        ImGui::Spacing();
        {
            ImVec2 p  = ImGui::GetCursorScreenPos();
            float  py = std::floor(p.y);
            float  w  = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, py),
                ImVec2(p.x + w, py),
                IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                         static_cast<int>(colors.border_subtle.g * 255),
                         static_cast<int>(colors.border_subtle.b * 255),
                         40));
            ImGui::Dummy(ImVec2(0, 1));
        }

        // Transport row ends with cursor on the right (time display SameLine).
        // Reset to the content left edge so the timeline fills the full panel width.
        // ImGui 1.92+ requires an item after SetCursorPos when extending boundaries.
        ImGui::SetCursorPosX(0.0f);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));

        float remaining_h = ImGui::GetContentRegionAvail().y;
        float content_w   = ImGui::GetContentRegionAvail().x;
        if (remaining_h >= 1.0f && content_w >= 1.0f)
            timeline_editor_->draw(content_w, remaining_h);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ─── Curve Editor Panel ──────────────────────────────────────────────────────

void ImGuiIntegration::draw_curve_editor_panel()
{
    if (!curve_editor_)
        return;

    const auto& colors = ui::theme();
    ImGuiIO&    io     = ImGui::GetIO();

    float win_w    = 560.0f;
    float win_h    = 380.0f;
    float center_x = io.DisplaySize.x * 0.5f - win_w * 0.5f;
    float center_y = io.DisplaySize.y * 0.4f - win_h * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(center_x, center_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 280),
                                        ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_secondary.r, colors.bg_secondary.g, colors.bg_secondary.b, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImVec4(colors.accent.r * 0.15f + colors.bg_tertiary.r * 0.85f,
                                 colors.accent.g * 0.15f + colors.bg_tertiary.g * 0.85f,
                                 colors.accent.b * 0.15f + colors.bg_tertiary.b * 0.85f,
                                 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    bool still_open = show_curve_editor_;
    if (ImGui::Begin("Curve Editor", &still_open, flags))
    {
        if (curve_editor_needs_fit_ && curve_editor_->interpolator()
            && curve_editor_->interpolator()->channel_count() > 0)
        {
            curve_editor_->fit_view();
            curve_editor_needs_fit_ = false;
        }

        float btn_sz  = 24.0f;
        float btn_gap = 4.0f;

        // Toolbar row with icon buttons
        if (font_icon_)
        {
            if (transport_button(ui::icon_str(ui::Icon::Fullscreen),
                                 false,
                                 false,
                                 font_icon_,
                                 btn_sz,
                                 colors))
            {
                curve_editor_->fit_view();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fit View");
            ImGui::SameLine(0, btn_gap);

            if (transport_button(ui::icon_str(ui::Icon::Home),
                                 false,
                                 false,
                                 font_icon_,
                                 btn_sz,
                                 colors))
            {
                curve_editor_->reset_view();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset View");
            ImGui::SameLine(0, 16.0f);
        }

        // Toggle buttons with modern pill style
        bool show_grid     = curve_editor_->show_grid();
        bool show_tangents = curve_editor_->show_tangents();

        auto toggle_pill = [&](const char* label, bool* value)
        {
            ImVec4 bg   = *value ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.15f)
                                 : ImVec4(0, 0, 0, 0);
            ImVec4 text = *value ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f)
                                 : ImVec4(colors.text_secondary.r,
                                          colors.text_secondary.g,
                                          colors.text_secondary.b,
                                          0.7f);

            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(colors.accent_subtle.r,
                                         colors.accent_subtle.g,
                                         colors.accent_subtle.b,
                                         0.3f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_1));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, *value ? 0.0f : 1.0f);
            bool pushed_border = !*value;
            if (pushed_border)
            {
                ImGui::PushStyleColor(ImGuiCol_Border,
                                      ImVec4(colors.border_subtle.r,
                                             colors.border_subtle.g,
                                             colors.border_subtle.b,
                                             0.3f));
            }

            if (ImGui::Button(label))
            {
                *value = !*value;
            }

            if (pushed_border)
                ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        };

        toggle_pill("Grid", &show_grid);
        ImGui::SameLine(0, btn_gap);
        toggle_pill("Tangents", &show_tangents);

        curve_editor_->set_show_grid(show_grid);
        curve_editor_->set_show_tangents(show_tangents);

        // Subtle separator
        ImGui::Spacing();
        {
            ImVec2 p  = ImGui::GetCursorScreenPos();
            float  py = std::floor(p.y);
            float  w  = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, py),
                ImVec2(p.x + w, py),
                IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                         static_cast<int>(colors.border_subtle.g * 255),
                         static_cast<int>(colors.border_subtle.b * 255),
                         40));
            ImGui::Dummy(ImVec2(0, 1));
        }

        // Sync playhead from timeline
        if (timeline_editor_)
        {
            curve_editor_->set_playhead_time(timeline_editor_->playhead());
        }

        // Draw the curve editor
        ImVec2 avail = ImGui::GetContentRegionAvail();
        curve_editor_->draw(avail.x, avail.y);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    show_curve_editor_ = still_open;
}

void ImGuiIntegration::draw_knobs_panel()
{
    if (!knob_manager_ || knob_manager_->empty())
        return;
    if (!knob_manager_->is_visible())
        return;

    auto& theme = ui::theme();
    auto& knobs = knob_manager_->knobs();

    // Initial position: top-right of canvas with padding (user can drag it anywhere).
    float canvas_x = layout_manager_ ? layout_manager_->canvas_rect().x : 0.0f;
    float canvas_y = layout_manager_ ? layout_manager_->canvas_rect().y : 0.0f;
    float canvas_w =
        layout_manager_ ? layout_manager_->canvas_rect().w : ImGui::GetIO().DisplaySize.x;

    float panel_w = 260.0f;
    float pad     = 12.0f;
    float pos_x   = canvas_x + canvas_w - panel_w - pad;
    float pos_y   = canvas_y + pad;

    ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.92f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(theme.border_subtle.r, theme.border_subtle.g, theme.border_subtle.b, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(theme.text_primary.r,
                                 theme.text_primary.g,
                                 theme.text_primary.b,
                                 theme.text_primary.a));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBg,
        ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.95f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgActive,
        ImVec4(theme.accent.r * 0.3f, theme.accent.g * 0.3f, theme.accent.b * 0.3f, 0.95f));
    ImGui::PushStyleColor(
        ImGuiCol_TitleBgCollapsed,
        ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.7f));

    bool collapsed = knob_manager_->is_collapsed();
    ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Once);

    bool panel_open = true;
    if (!ImGui::Begin(" Parameters", &panel_open, flags | ImGuiWindowFlags_NoScrollbar))
    {
        if (!panel_open)
            knob_manager_->set_visible(false);
        // Window is collapsed — record rect (title bar only) and sync state
        ImVec2 wpos        = ImGui::GetWindowPos();
        ImVec2 wsz         = ImGui::GetWindowSize();
        knobs_panel_rect_  = {wpos.x, wpos.y, wsz.x, wsz.y};
        bool now_collapsed = ImGui::IsWindowCollapsed();
        if (now_collapsed != collapsed)
            knob_manager_->set_collapsed(now_collapsed);
        ImGui::End();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
        return;
    }
    if (!panel_open)
    {
        knob_manager_->set_visible(false);
        ImGui::End();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
        return;
    }

    // Record full panel rect for tab-bar occlusion check
    {
        ImVec2 wpos       = ImGui::GetWindowPos();
        ImVec2 wsz        = ImGui::GetWindowSize();
        knobs_panel_rect_ = {wpos.x, wpos.y, wsz.x, wsz.y};
    }

    // Sync collapse state (user may have clicked the collapse arrow)
    {
        bool now_collapsed = ImGui::IsWindowCollapsed();
        if (now_collapsed != collapsed)
            knob_manager_->set_collapsed(now_collapsed);
        collapsed = now_collapsed;
    }

    if (!collapsed)
    {
        bool any_changed = false;

        // Accent color for sliders
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              ImVec4(theme.accent.r, theme.accent.g, theme.accent.b, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_SliderGrabActive,
            ImVec4(theme.accent.r * 0.85f, theme.accent.g * 0.85f, theme.accent.b * 0.85f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.6f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 0.8f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            ImVec4(theme.bg_tertiary.r, theme.bg_tertiary.g, theme.bg_tertiary.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,
                              ImVec4(theme.accent.r, theme.accent.g, theme.accent.b, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, ui::tokens::RADIUS_SM);

        float label_w = 0.0f;
        for (auto& k : knobs)
        {
            float tw = ImGui::CalcTextSize(k.name.c_str()).x;
            if (tw > label_w)
                label_w = tw;
        }
        label_w = std::min(label_w + 8.0f, panel_w * 0.4f);

        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto& k = knobs[i];
            ImGui::PushID(static_cast<int>(i));

            // Label
            ImGui::TextColored(
                ImVec4(theme.text_primary.r, theme.text_primary.g, theme.text_primary.b, 0.9f),
                "%s",
                k.name.c_str());

            // Control on same line or next line depending on type
            float avail = ImGui::GetContentRegionAvail().x;

            switch (k.type)
            {
                case KnobType::Float:
                {
                    ImGui::SetNextItemWidth(avail);
                    float old_val = k.value;
                    if (k.step > 0.0f)
                    {
                        // Discrete stepping — use drag float
                        ImGui::DragFloat("##v", &k.value, k.step, k.min_val, k.max_val, "%.3f");
                    }
                    else
                    {
                        ImGui::SliderFloat("##v", &k.value, k.min_val, k.max_val, "%.3f");
                    }
                    k.value = std::clamp(k.value, k.min_val, k.max_val);
                    if (k.value != old_val)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Int:
                {
                    ImGui::SetNextItemWidth(avail);
                    int iv     = k.int_value();
                    int old_iv = iv;
                    ImGui::SliderInt("##v",
                                     &iv,
                                     static_cast<int>(k.min_val),
                                     static_cast<int>(k.max_val));
                    k.value = static_cast<float>(iv);
                    if (iv != old_iv)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Bool:
                {
                    bool bv     = k.bool_value();
                    bool old_bv = bv;
                    ImGui::Checkbox("##v", &bv);
                    k.value = bv ? 1.0f : 0.0f;
                    if (bv != old_bv)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
                case KnobType::Choice:
                {
                    ImGui::SetNextItemWidth(avail);
                    int ci     = k.choice_index();
                    int old_ci = ci;
                    if (ImGui::BeginCombo("##v",
                                          (ci >= 0 && ci < static_cast<int>(k.choices.size()))
                                              ? k.choices[ci].c_str()
                                              : ""))
                    {
                        for (int j = 0; j < static_cast<int>(k.choices.size()); ++j)
                        {
                            bool selected = (j == ci);
                            if (ImGui::Selectable(k.choices[j].c_str(), selected))
                            {
                                ci      = j;
                                k.value = static_cast<float>(j);
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ci != old_ci)
                    {
                        if (k.on_change)
                            k.on_change(k.value);
                        knob_manager_->mark_dirty(k.name, k.value);
                        any_changed = true;
                    }
                    break;
                }
            }

            // Small spacing between knobs
            if (i + 1 < knobs.size())
                ImGui::Spacing();

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(6);

        if (any_changed)
        {
            knob_manager_->notify_any_changed();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
