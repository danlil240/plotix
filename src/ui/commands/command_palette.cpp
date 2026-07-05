#ifdef SPECTRA_USE_IMGUI

    #include "command_palette.hpp"

    #include <algorithm>
    #include <cstring>
    #include <imgui.h>
    #include <cmath>
    #include <spectra/logger.hpp>

    #include "command_registry.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "shortcut_manager.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

// ─── Open / Close ────────────────────────────────────────────────────────────

void CommandPalette::open()
{
    SPECTRA_LOG_TRACE("ui", "Command palette opened");
    open_          = true;
    focus_input_   = true;
    search_buf_[0] = '\0';
    last_query_.clear();
    selected_index_ = 0;
    results_.clear();
    scroll_offset_   = 0.0f;
    scroll_target_   = 0.0f;
    scroll_velocity_ = 0.0f;
    content_height_  = 0.0f;
    update_search();
}

void CommandPalette::close()
{
    SPECTRA_LOG_TRACE("ui", "Command palette closed");
    open_          = false;
    search_buf_[0] = '\0';
    last_query_.clear();
    results_.clear();
    scroll_offset_   = 0.0f;
    scroll_target_   = 0.0f;
    scroll_velocity_ = 0.0f;
}

void CommandPalette::toggle()
{
    if (open_)
        close();
    else
        open();
}

// ─── Search ──────────────────────────────────────────────────────────────────

void CommandPalette::update_search()
{
    if (!registry_)
        return;

    std::string query(search_buf_);
    if (query == last_query_ && !results_.empty())
        return;
    last_query_ = query;

    if (query.empty())
    {
        // Show recent commands first, then all
        auto recent = registry_->recent_commands(5);
        auto all    = registry_->search("", 50);
        results_.clear();

        // Add recent at top (with boosted score)
        for (const auto* cmd : recent)
        {
            results_.push_back({cmd, 1000});
        }
        // Add remaining (skip duplicates from recent)
        for (const auto& r : all)
        {
            bool is_recent = false;
            for (const auto* rc : recent)
            {
                if (rc->id == r.command->id)
                {
                    is_recent = true;
                    break;
                }
            }
            if (!is_recent)
            {
                results_.push_back(r);
            }
        }
    }
    else
    {
        results_ = registry_->search(query, 50);
    }

    // Clamp selected index
    if (selected_index_ >= static_cast<int>(results_.size()))
    {
        selected_index_ = results_.empty() ? 0 : static_cast<int>(results_.size()) - 1;
    }
}

// ─── Keyboard ────────────────────────────────────────────────────────────────

bool CommandPalette::handle_keyboard()
{
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        close();
        return false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        selected_index_     = std::max(0, selected_index_ - 1);
        scroll_to_selected_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        selected_index_ = std::min(static_cast<int>(results_.size()) - 1, selected_index_ + 1);
        if (selected_index_ < 0)
            selected_index_ = 0;
        scroll_to_selected_ = true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
    {
        if (selected_index_ >= 0 && selected_index_ < static_cast<int>(results_.size()))
        {
            const auto* cmd = results_[selected_index_].command;
            if (cmd && cmd->callback && cmd->enabled)
            {
                std::string cmd_id = cmd->id;
                close();
                registry_->execute(cmd_id);
                return true;
            }
        }
    }

    return false;
}

// ─── Draw ────────────────────────────────────────────────────────────────────

bool CommandPalette::draw(float window_width, float window_height)
{
    if (!open_)
    {
        opacity_            = 0.0f;
        scale_              = 0.98f;
        scrollbar_opacity_  = 0.0f;
        scrollbar_hover_t_  = 0.0f;
        scrollbar_dragging_ = false;
        return false;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f)
        dt = 1.0f / 60.0f;

    // Animate open
    opacity_ = std::min(1.0f, opacity_ + dt * ANIM_SPEED);
    scale_   = scale_ + (1.0f - scale_) * std::min(1.0f, dt * ANIM_SPEED);

    const auto& colors = ui::theme();

    // ─── Compute content height ────────────────────────────────────────────
    // Use measured_content_ from previous frame (actual ImGui cursor delta).
    // Fall back to constant-based estimate when no measurement exists yet.
    float total_content_h = NAN;
    if (measured_content_ > 1.0f)
    {
        total_content_h = measured_content_;
    }
    else
    {
        // Estimate from constants + actual ImGui item spacing (first frame only)
        float sp        = ImGui::GetStyle().ItemSpacing.y;
        total_content_h = 0.0f;
        std::string prev_cat;
        for (auto& result : results_)
        {
            if (!result.command)
                continue;
            if (result.command->category != prev_cat)
            {
                prev_cat = result.command->category;
                // Dummy + text + dummy, each followed by ItemSpacing
                total_content_h += CATEGORY_HEADER_HEIGHT + sp * 2;
            }
            total_content_h += RESULT_ITEM_HEIGHT + sp;
        }
    }
    content_height_ = total_content_h;

    // Compute palette height: overhead (input + separator + padding) + content, capped at max.
    // measured_overhead_ is updated each frame from GetContentRegionAvail; use conservative
    // fallback on first frame (before we have a measurement).
    float overhead  = (measured_overhead_ > 1.0f) ? measured_overhead_ : 80.0f;
    float palette_h = std::min(PALETTE_MAX_HEIGHT, overhead + total_content_h);
    // visible_height_ is refined later via GetContentRegionAvail; use estimate for scrollbar.
    visible_height_  = palette_h - overhead;
    float max_scroll = std::max(0.0f, content_height_ - visible_height_);
    bool  scrollable = max_scroll > 0.5f;

    // ─── Palette geometry (computed early for scrollbar hit-testing) ─────────
    float palette_w = PALETTE_WIDTH * scale_;
    float palette_x = (window_width - palette_w) * 0.5f;
    float palette_y = window_height * 0.2f;

    // Scrollbar geometry
    constexpr float SB_WIDTH_THIN = 4.0f;
    constexpr float SB_WIDTH_WIDE = 7.0f;
    constexpr float SB_MARGIN     = 3.0f;
    constexpr float SB_MIN_THUMB  = 28.0f;
    constexpr float SB_HIT_PAD    = 8.0f;   // Extra hit-test padding

    // Results region screen coords
    float results_top_y = palette_y + INPUT_HEIGHT + ui::tokens::SPACE_2;
    float results_bot_y = results_top_y + visible_height_;

    float sb_track_top = results_top_y + 4.0f;
    float sb_track_bot = results_bot_y - 4.0f;
    float sb_track_h   = sb_track_bot - sb_track_top;

    float sb_thumb_h   = 0.0f;
    float sb_thumb_top = 0.0f;
    if (scrollable && sb_track_h > SB_MIN_THUMB)
    {
        float ratio        = visible_height_ / content_height_;
        sb_thumb_h         = std::max(SB_MIN_THUMB, sb_track_h * ratio);
        float scroll_ratio = (max_scroll > 0.0f) ? (scroll_offset_ / max_scroll) : 0.0f;
        sb_thumb_top       = sb_track_top + scroll_ratio * (sb_track_h - sb_thumb_h);
    }

    float sb_width = SB_WIDTH_THIN + (SB_WIDTH_WIDE - SB_WIDTH_THIN) * scrollbar_hover_t_;
    float sb_right = palette_x + palette_w - SB_MARGIN;
    float sb_left  = sb_right - sb_width;

    // ─── Scrollbar drag handling (before scroll physics) ────────────────────
    {
        ImGuiIO& io    = ImGui::GetIO();
        ImVec2   mouse = io.MousePos;

        // Hit-test scrollbar region (wider than visual for easy grab)
        bool mouse_in_sb = scrollable && sb_thumb_h > 0.0f && mouse.x >= (sb_left - SB_HIT_PAD)
                           && mouse.x <= (sb_right + SB_HIT_PAD) && mouse.y >= sb_track_top
                           && mouse.y <= sb_track_bot;

        if (scrollbar_dragging_)
        {
            if (ImGui::IsMouseDown(0))
            {
                float new_thumb_top = mouse.y - scrollbar_drag_offset_;
                float clamped =
                    std::max(sb_track_top, std::min(new_thumb_top, sb_track_bot - sb_thumb_h));
                float ratio      = (sb_track_h > sb_thumb_h)
                                       ? (clamped - sb_track_top) / (sb_track_h - sb_thumb_h)
                                       : 0.0f;
                scroll_target_   = ratio * max_scroll;
                scroll_velocity_ = 0.0f;
            }
            else
            {
                scrollbar_dragging_ = false;
            }
        }
        else if (mouse_in_sb && ImGui::IsMouseClicked(0))
        {
            // Check if click is on thumb or on track
            if (mouse.y >= sb_thumb_top && mouse.y <= sb_thumb_top + sb_thumb_h)
            {
                scrollbar_dragging_    = true;
                scrollbar_drag_offset_ = mouse.y - sb_thumb_top;
            }
            else
            {
                // Click on track — jump to position
                float ratio =
                    (sb_track_h > sb_thumb_h)
                        ? (mouse.y - sb_track_top - sb_thumb_h * 0.5f) / (sb_track_h - sb_thumb_h)
                        : 0.0f;
                ratio            = std::max(0.0f, std::min(1.0f, ratio));
                scroll_target_   = ratio * max_scroll;
                scroll_velocity_ = 0.0f;
            }
        }

        // Animate scrollbar hover
        float sb_hover_target = (mouse_in_sb || scrollbar_dragging_) ? 1.0f : 0.0f;
        scrollbar_hover_t_ += (sb_hover_target - scrollbar_hover_t_) * std::min(1.0f, 15.0f * dt);
    }

    // ─── Smooth scroll physics ──────────────────────────────────────────────
    bool scrolling = false;
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f)
        {
            float wheel = -io.MouseWheel;
            // Direct target offset — no separate velocity accumulation that causes bounce
            scroll_target_ += wheel * SCROLL_SPEED;
            // Give a gentle velocity for momentum (proportional to wheel, not additive)
            scroll_velocity_ = wheel * SCROLL_SPEED * 4.0f;
            io.MouseWheel    = 0.0f;
            scrolling        = true;
        }
    }

    // Apply inertial velocity to target
    if (std::abs(scroll_velocity_) > SCROLL_VEL_THRESHOLD)
    {
        scroll_target_ += scroll_velocity_ * dt;
        // Dampen
        scroll_velocity_ *= std::max(0.0f, 1.0f - SCROLL_DECEL * dt);
        scrolling = true;

        // Kill velocity at bounds to prevent bounce
        if (scroll_target_ <= 0.0f)
        {
            scroll_target_   = 0.0f;
            scroll_velocity_ = 0.0f;
        }
        else if (scroll_target_ >= max_scroll)
        {
            scroll_target_   = max_scroll;
            scroll_velocity_ = 0.0f;
        }
    }
    else
    {
        scroll_velocity_ = 0.0f;
    }

    // Hard clamp target
    scroll_target_ = std::max(0.0f, std::min(scroll_target_, max_scroll));

    // Smooth interpolation towards target
    float lerp_t = std::min(1.0f, SCROLL_SMOOTHING * dt);
    scroll_offset_ += (scroll_target_ - scroll_offset_) * lerp_t;

    // Snap when very close
    if (std::abs(scroll_offset_ - scroll_target_) < 0.3f)
    {
        scroll_offset_ = scroll_target_;
    }

    // Hard clamp offset too (safety)
    scroll_offset_ = std::max(0.0f, std::min(scroll_offset_, max_scroll));

    // ─── Scrollbar opacity animation ────────────────────────────────────────
    {
        bool sb_active = scrolling || scrollbar_dragging_ || scrollbar_hover_t_ > 0.05f
                         || std::abs(scroll_offset_ - scroll_target_) > 1.0f;
        // float sb_opacity_target = sb_active ? 1.0f : 0.0f;  // Currently unused
        if (sb_active)
        {
            scrollbar_opacity_ += (1.0f - scrollbar_opacity_) * std::min(1.0f, 20.0f * dt);
        }
        else
        {
            scrollbar_opacity_ += (0.0f - scrollbar_opacity_) * std::min(1.0f, 3.0f * dt);
        }
        if (scrollbar_opacity_ < 0.01f)
            scrollbar_opacity_ = 0.0f;
    }

    // ─── Draw overlay on FOREGROUND draw list (above all ImGui windows) ─────
    // NOTE: the foreground draw list renders AFTER every window's own draw list
    // (including this palette's window, drawn below via ImGui::Begin). A filled
    // rect covering the palette bounds here would therefore darken the palette
    // itself. Instead, dim/shadow only the margin OUTSIDE the palette's own
    // bounding rect via 4 surrounding strips ("donut" fill), never touching the
    // interior where the palette window renders.
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    auto draw_donut = [&](ImVec2 outer_min, ImVec2 outer_max, ImU32 col)
    {
        // Top strip
        fg->AddRectFilled(outer_min, ImVec2(outer_max.x, palette_y), col);
        // Bottom strip
        fg->AddRectFilled(
            ImVec2(outer_min.x, palette_y + palette_h), outer_max, col);
        // Left strip
        fg->AddRectFilled(
            ImVec2(outer_min.x, palette_y), ImVec2(palette_x, palette_y + palette_h), col);
        // Right strip
        fg->AddRectFilled(ImVec2(palette_x + palette_w, palette_y),
                          ImVec2(outer_max.x, palette_y + palette_h),
                          col);
    };

    ImU32 overlay_col = IM_COL32(static_cast<int>(colors.bg_overlay.r * 255),
                                 static_cast<int>(colors.bg_overlay.g * 255),
                                 static_cast<int>(colors.bg_overlay.b * 255),
                                 static_cast<int>(colors.bg_overlay.a * opacity_ * 0.5f * 255));
    draw_donut(ImVec2(0, 0), ImVec2(window_width, window_height), overlay_col);

    // Layered shadow (drawn only in the margin around the palette, not over it)
    draw_donut(ImVec2(palette_x - 4, palette_y - 2),
              ImVec2(palette_x + palette_w + 4, palette_y + palette_h + 12),
              IM_COL32(0, 0, 0, static_cast<int>(30 * opacity_)));
    draw_donut(ImVec2(palette_x - 1, palette_y),
              ImVec2(palette_x + palette_w + 1, palette_y + palette_h + 4),
              IM_COL32(0, 0, 0, static_cast<int>(50 * opacity_)));

    // ─── Click outside palette to dismiss ──────────────────────────────────
    if (ImGui::IsMouseClicked(0))
    {
        ImVec2 mp         = ImGui::GetIO().MousePos;
        bool   on_palette = mp.x >= palette_x && mp.x <= palette_x + palette_w && mp.y >= palette_y
                            && mp.y <= palette_y + palette_h;
        if (!on_palette)
        {
            close();
            return false;
        }
    }

    // ─── Palette window ─────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(palette_x, palette_y));
    ImGui::SetNextWindowSize(ImVec2(palette_w, palette_h));
    ImGui::SetNextWindowFocus();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, opacity_));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.border_default.r,
                                 colors.border_default.g,
                                 colors.border_default.b,
                                 opacity_ * 0.8f));

    ImGuiWindowFlags palette_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    bool executed = false;

    if (ImGui::Begin("##command_palette", nullptr, palette_flags))
    {
        // Search input
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_SM);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f));

        ImGui::SetNextItemWidth(palette_w - ui::tokens::SPACE_4);

        if (focus_input_)
        {
            ImGui::SetKeyboardFocusHere();
            focus_input_ = false;
        }

        if (font_body_)
            ImGui::PushFont(font_body_);

        bool input_changed = ImGui::InputTextWithHint("##palette_search",
                                                      "Type a command...",
                                                      search_buf_,
                                                      sizeof(search_buf_),
                                                      ImGuiInputTextFlags_AutoSelectAll);

        if (font_body_)
            ImGui::PopFont();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (input_changed)
        {
            update_search();
            selected_index_   = 0;
            scroll_offset_    = 0.0f;
            scroll_target_    = 0.0f;
            scroll_velocity_  = 0.0f;
            measured_content_ = 0.0f;   // Force re-measurement for new result set
        }

        executed = handle_keyboard();

        // Separator
        ImGui::PushStyleColor(
            ImGuiCol_Separator,
            ImVec4(colors.border_subtle.r, colors.border_subtle.g, colors.border_subtle.b, 0.5f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // ─── Results list ───────────────────────────────────────────────
        if (!results_.empty())
        {
            // Measure actual remaining space — and record overhead for next frame's sizing.
            float avail        = ImGui::GetContentRegionAvail().y;
            measured_overhead_ = palette_h - avail;
            visible_height_    = avail;
            ImGui::BeginChild("##palette_results",
                              ImVec2(0, visible_height_),
                              0,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::SetScrollY(scroll_offset_);

            float       content_start_y = ImGui::GetCursorPosY();
            std::string current_category;

            for (int i = 0; i < static_cast<int>(results_.size()); ++i)
            {
                const auto& result = results_[i];
                if (!result.command)
                    continue;

                // Category header
                if (result.command->category != current_category)
                {
                    current_category = result.command->category;
                    if (font_heading_)
                        ImGui::PushFont(font_heading_);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(colors.text_tertiary.r,
                                                 colors.text_tertiary.g,
                                                 colors.text_tertiary.b,
                                                 0.8f));
                    ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_1));
                    ImGui::TextUnformatted(current_category.c_str());
                    ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_1 * 0.5f));
                    ImGui::PopStyleColor();
                    if (font_heading_)
                        ImGui::PopFont();
                }

                bool   is_selected = (i == selected_index_);
                ImVec2 item_pos    = ImGui::GetCursorScreenPos();
                float  item_w      = ImGui::GetContentRegionAvail().x;

                if (is_selected)
                {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(item_pos.x + 3, item_pos.y + 1),
                        ImVec2(item_pos.x + item_w - 3, item_pos.y + RESULT_ITEM_HEIGHT - 1),
                        IM_COL32(static_cast<int>(colors.accent_muted.r * 255),
                                 static_cast<int>(colors.accent_muted.g * 255),
                                 static_cast<int>(colors.accent_muted.b * 255),
                                 60),
                        ui::tokens::RADIUS_MD);
                }

                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##item", ImVec2(item_w, RESULT_ITEM_HEIGHT)))
                {
                    if (result.command->callback && result.command->enabled)
                    {
                        std::string cmd_id = result.command->id;
                        close();
                        registry_->execute(cmd_id);
                        executed = true;
                    }
                }

                if (ImGui::IsItemHovered())
                {
                    selected_index_ = i;
                }

                ImVec2 text_pos(
                    item_pos.x + ui::tokens::SPACE_3,
                    item_pos.y + (RESULT_ITEM_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);

                if (font_body_)
                    ImGui::PushFont(font_body_);

                ImGui::GetWindowDrawList()->AddText(
                    text_pos,
                    IM_COL32(static_cast<int>(colors.text_primary.r * 255),
                             static_cast<int>(colors.text_primary.g * 255),
                             static_cast<int>(colors.text_primary.b * 255),
                             result.command->enabled ? 255 : 128),
                    result.command->label.c_str());

                if (!result.command->shortcut.empty())
                {
                    ImVec2 shortcut_size = ImGui::CalcTextSize(result.command->shortcut.c_str());
                    float  badge_x = item_pos.x + item_w - shortcut_size.x - ui::tokens::SPACE_4;
                    float  badge_y = text_pos.y;

                    ImVec2 badge_min(badge_x - ui::tokens::SPACE_2, badge_y - 3);
                    ImVec2 badge_max(badge_x + shortcut_size.x + ui::tokens::SPACE_2,
                                     badge_y + shortcut_size.y + 3);

                    ImGui::GetWindowDrawList()->AddRectFilled(
                        badge_min,
                        badge_max,
                        IM_COL32(static_cast<int>(colors.bg_tertiary.r * 255),
                                 static_cast<int>(colors.bg_tertiary.g * 255),
                                 static_cast<int>(colors.bg_tertiary.b * 255),
                                 180),
                        ui::tokens::RADIUS_SM);

                    ImGui::GetWindowDrawList()->AddRect(
                        badge_min,
                        badge_max,
                        IM_COL32(static_cast<int>(colors.border_subtle.r * 255),
                                 static_cast<int>(colors.border_subtle.g * 255),
                                 static_cast<int>(colors.border_subtle.b * 255),
                                 100),
                        ui::tokens::RADIUS_SM);

                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(badge_x, badge_y),
                        IM_COL32(static_cast<int>(colors.text_secondary.r * 255),
                                 static_cast<int>(colors.text_secondary.g * 255),
                                 static_cast<int>(colors.text_secondary.b * 255),
                                 220),
                        result.command->shortcut.c_str());
                }

                if (font_body_)
                    ImGui::PopFont();
                ImGui::PopID();
            }

            // ─── Keyboard scroll-into-view (only on arrow key nav) ────────
            if (scroll_to_selected_ && selected_index_ >= 0
                && selected_index_ < static_cast<int>(results_.size()))
            {
                scroll_to_selected_ = false;
                float       item_y  = 0.0f;
                std::string cat;
                for (int i = 0; i <= selected_index_; ++i)
                {
                    if (!results_[i].command)
                        continue;
                    if (results_[i].command->category != cat)
                    {
                        cat = results_[i].command->category;
                        item_y += CATEGORY_HEADER_HEIGHT;
                    }
                    if (i < selected_index_)
                        item_y += RESULT_ITEM_HEIGHT;
                }
                float item_bottom = item_y + RESULT_ITEM_HEIGHT;

                if (item_y < scroll_target_)
                {
                    scroll_target_   = item_y;
                    scroll_velocity_ = 0.0f;
                }
                else if (item_bottom > scroll_target_ + visible_height_)
                {
                    scroll_target_   = item_bottom - visible_height_;
                    scroll_velocity_ = 0.0f;
                }
                scroll_target_ = std::max(0.0f, std::min(scroll_target_, max_scroll));
            }

            // Measure actual content height for next frame's palette sizing
            measured_content_ = ImGui::GetCursorPosY() - content_start_y;

            ImGui::EndChild();
        }
        else
        {
            if (font_body_)
                ImGui::PushFont(font_body_);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         0.6f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::tokens::SPACE_4);
            ImGui::SetCursorPosX((palette_w - ImGui::CalcTextSize("No matching commands").x)
                                 * 0.5f);
            ImGui::TextUnformatted("No matching commands");
            ImGui::PopStyleColor();
            if (font_body_)
                ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // ─── Draw custom scrollbar on foreground (above everything) ─────────────
    if (scrollable && sb_thumb_h > 0.0f && scrollbar_opacity_ > 0.01f)
    {
        // Recompute thumb position with final scroll_offset_ (may have changed)
        float final_ratio     = (max_scroll > 0.0f) ? (scroll_offset_ / max_scroll) : 0.0f;
        float final_thumb_top = sb_track_top + final_ratio * (sb_track_h - sb_thumb_h);

        // Recalculate width with current hover_t
        float final_sb_w    = SB_WIDTH_THIN + (SB_WIDTH_WIDE - SB_WIDTH_THIN) * scrollbar_hover_t_;
        float final_sb_left = sb_right - final_sb_w;

        int alpha = static_cast<int>(scrollbar_opacity_ * opacity_ * 255);

        // Track (very subtle, only on hover)
        if (scrollbar_hover_t_ > 0.05f)
        {
            int track_alpha = static_cast<int>(scrollbar_hover_t_ * 0.15f * alpha);
            fg->AddRectFilled(ImVec2(final_sb_left, sb_track_top),
                              ImVec2(sb_right, sb_track_bot),
                              IM_COL32(128, 128, 128, track_alpha),
                              final_sb_w * 0.5f);
        }

        // Thumb — pill-shaped
        int thumb_alpha = static_cast<int>((0.35f + 0.35f * scrollbar_hover_t_) * alpha);
        if (scrollbar_dragging_)
            thumb_alpha = static_cast<int>(0.8f * alpha);

        fg->AddRectFilled(ImVec2(final_sb_left, final_thumb_top),
                          ImVec2(sb_right, final_thumb_top + sb_thumb_h),
                          IM_COL32(static_cast<int>(colors.text_secondary.r * 255),
                                   static_cast<int>(colors.text_secondary.g * 255),
                                   static_cast<int>(colors.text_secondary.b * 255),
                                   thumb_alpha),
                          final_sb_w * 0.5f);
    }

    return executed;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
