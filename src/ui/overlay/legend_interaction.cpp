#ifdef SPECTRA_USE_IMGUI

    #include "legend_interaction.hpp"

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/glass_draw.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

void LegendInteraction::set_fonts(ImFont* body, ImFont* icon)
{
    font_body_ = body;
    font_icon_ = icon;
}

// ─── State management ───────────────────────────────────────────────────────

LegendSeriesState& LegendInteraction::get_state(const Series* s)
{
    auto it = series_states_.find(s);
    if (it != series_states_.end())
        return it->second;
    LegendSeriesState state;
    state.user_visible   = s ? s->visible() : true;
    state.opacity        = state.user_visible ? 1.0f : 0.0f;
    state.target_opacity = state.opacity;
    return series_states_.emplace(s, state).first->second;
}

LegendInteraction::LegendOffset& LegendInteraction::get_offset(uintptr_t figure_id,
                                                               size_t    axes_index)
{
    return legend_offsets_[make_offset_key(figure_id, axes_index)];
}

// ─── Update ─────────────────────────────────────────────────────────────────

void LegendInteraction::update(float dt, Figure& figure)
{
    // Animate opacity for all tracked series
    for (auto& [series_ptr, state] : series_states_)
    {
        float speed = (toggle_duration_ > 0.0f) ? (1.0f / toggle_duration_) : 100.0f;
        float diff  = state.target_opacity - state.opacity;
        if (std::abs(diff) > 0.001f)
        {
            state.opacity += diff * std::min(1.0f, speed * dt);
            if (std::abs(state.opacity - state.target_opacity) < 0.005f)
            {
                state.opacity = state.target_opacity;
            }
        }
    }

    // Clean up stale entries (series that no longer exist)
    // This is a lightweight GC — only runs when map is large
    if (series_states_.size() > 100)
    {
        for (auto it = series_states_.begin(); it != series_states_.end();)
        {
            bool found = false;
            for (auto& axes_ptr : figure.axes())
            {
                if (!axes_ptr)
                    continue;
                for (auto& s : axes_ptr->series())
                {
                    if (s.get() == it->first)
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            if (!found)
            {
                it = series_states_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// ─── Queries ────────────────────────────────────────────────────────────────

float LegendInteraction::series_opacity(const Series* series) const
{
    auto it = series_states_.find(series);
    if (it != series_states_.end())
        return it->second.opacity;
    return series ? (series->visible() ? 1.0f : 0.0f) : 1.0f;
}

bool LegendInteraction::is_series_visible(const Series* series) const
{
    auto it = series_states_.find(series);
    if (it != series_states_.end())
        return it->second.user_visible;
    return series ? series->visible() : true;
}

// ─── Drawing ────────────────────────────────────────────────────────────────

bool LegendInteraction::draw(Axes&               axes,
                             const Rect&         viewport,
                             size_t              axes_index,
                             const LegendConfig& config,
                             uintptr_t           figure_id)
{
    const auto& series_list = axes.series();
    if (series_list.empty())
        return false;

    // Count labeled series (excluding reference lines hidden from legend)
    int labeled_count = 0;
    for (auto& s : series_list)
    {
        if (s && !s->label().empty() && s->show_in_legend())
            ++labeled_count;
    }
    if (labeled_count == 0)
        return false;

    const auto& theme_colors = theme_mgr_->colors();
    ImFont*     font         = font_body_ ? font_body_ : ImGui::GetFont();

    // Use font size from LegendConfig; fall back to font's native size
    float font_size = (config.font_size > 0.0f) ? config.font_size : font->LegacySize;

    // Use padding from LegendConfig
    float           pad         = config.padding;
    float           pad_x       = pad + 2.0f;   // slight extra horizontal padding
    float           pad_y       = pad;
    constexpr float swatch_size = 10.0f;
    constexpr float swatch_gap  = 6.0f;
    float           row_height  = font_size + 6.0f;
    constexpr float eye_width   = 16.0f;

    // Measure legend size
    float max_label_w = 0.0f;
    for (auto& s : series_list)
    {
        if (!s || s->label().empty() || !s->show_in_legend())
            continue;
        ImVec2 sz   = font->CalcTextSizeA(font_size, 300.0f, 0.0f, s->label().c_str());
        max_label_w = std::max(max_label_w, sz.x);
    }

    float legend_w = pad_x * 2.0f + swatch_size + swatch_gap + max_label_w;
    if (toggleable_)
        legend_w += eye_width + 4.0f;
    float legend_h = pad_y * 2.0f + static_cast<float>(labeled_count) * row_height;

    // Clamp legend height: max 8 visible rows, scrollable beyond
    constexpr int MAX_VISIBLE_ROWS = 8;
    float         max_legend_h = pad_y * 2.0f + static_cast<float>(MAX_VISIBLE_ROWS) * row_height;
    bool          scrollable   = (labeled_count > MAX_VISIBLE_ROWS);
    if (scrollable)
    {
        legend_w += 8.0f;   // room for scrollbar
        legend_h = max_legend_h;
    }

    // Compute default position from LegendConfig::position
    constexpr float margin    = 12.0f;
    float           default_x = viewport.x + viewport.w - legend_w - margin;   // TopRight default
    float           default_y = viewport.y + margin;
    switch (config.position)
    {
        case LegendPosition::TopLeft:
            default_x = viewport.x + margin;
            default_y = viewport.y + margin;
            break;
        case LegendPosition::TopRight:
            default_x = viewport.x + viewport.w - legend_w - margin;
            default_y = viewport.y + margin;
            break;
        case LegendPosition::BottomLeft:
            default_x = viewport.x + margin;
            default_y = viewport.y + viewport.h - legend_h - margin;
            break;
        case LegendPosition::BottomRight:
            default_x = viewport.x + viewport.w - legend_w - margin;
            default_y = viewport.y + viewport.h - legend_h - margin;
            break;
        case LegendPosition::None:
            // Hidden position — should have been gated by visible check, but just in case
            return false;
    }

    auto& offset = get_offset(figure_id, axes_index);
    float lx     = default_x + offset.dx;
    float ly     = default_y + offset.dy;

    // Clamp to viewport
    lx = std::max(viewport.x + 4.0f, std::min(lx, viewport.x + viewport.w - legend_w - 4.0f));
    ly = std::max(viewport.y + 4.0f, std::min(ly, viewport.y + viewport.h - legend_h - 4.0f));

    // Draw legend window — use figure_id in the ImGui ID to prevent cross-figure collisions
    const std::string win_id =
        std::format("##legend_{:x}_{}", static_cast<unsigned long>(figure_id), axes_index);

    ImGui::SetNextWindowPos(ImVec2(lx, ly));
    ImGui::SetNextWindowSize(ImVec2(legend_w, legend_h));

    // Use LegendConfig colors; fall back to theme colors if sentinel.
    // Color default-constructs to (0,0,0,1) so check RGB only.
    auto   is_sentinel = [](const Color& c) { return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f; };
    ImVec4 bg_col;
    if (is_sentinel(config.bg_color))
        bg_col = ImVec4(theme_colors.tooltip_bg.r,
                        theme_colors.tooltip_bg.g,
                        theme_colors.tooltip_bg.b,
                        theme_colors.tooltip_bg.a);
    else
        bg_col = ImVec4(config.bg_color.r, config.bg_color.g, config.bg_color.b, config.bg_color.a);

    ImVec4 border_col;
    if (is_sentinel(config.border_color))
        border_col = ImVec4(theme_colors.border_subtle.r,
                            theme_colors.border_subtle.g,
                            theme_colors.border_subtle.b,
                            theme_colors.border_subtle.a);
    else
        border_col = ImVec4(config.border_color.r,
                            config.border_color.g,
                            config.border_color.b,
                            config.border_color.a);

    // Translucent glass card: only honor an explicit user bg_color; otherwise
    // draw a frosted pane manually (see below) and keep the ImGui WindowBg clear.
    const bool user_bg = !is_sentinel(config.bg_color);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x + 4.0f, pad_y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, user_bg ? 0.5f : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, user_bg ? bg_col : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, user_bg ? border_col : ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!scrollable)
    {
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    // Allow moving if draggable
    if (!draggable_)
    {
        flags |= ImGuiWindowFlags_NoMove;
    }

    bool consumed = false;

    if (ImGui::Begin(win_id.c_str(), nullptr, flags))
    {
        // Frosted glass card behind the legend content (skip if user set a bg).
        if (!user_bg)
        {
            ImDrawList* wdl = ImGui::GetWindowDrawList();
            ImVec2      wp0 = ImGui::GetWindowPos();
            ImVec2      wsz = ImGui::GetWindowSize();
            ImVec2      wp1 = ImVec2(wp0.x + wsz.x, wp0.y + wsz.y);
            wdl->PushClipRectFullScreen();
            ui::glass_draw::draw_glass_card(wdl,
                                            wp0,
                                            wp1,
                                            ui::tokens::RADIUS_LG,
                                            theme_colors,
                                            0.52f,
                                            theme_colors.glow_intensity);
            wdl->PopClipRect();
        }

        // Handle legend dragging
        if (draggable_)
        {
            // ImVec2 win_pos = ImGui::GetWindowPos();  // Currently unused
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGui::IsAnyItemHovered())
            {
                dragging_        = true;
                drag_axes_index_ = axes_index;
                drag_start_mx_   = ImGui::GetIO().MousePos.x;
                drag_start_my_   = ImGui::GetIO().MousePos.y;
                drag_start_ox_   = offset.dx;
                drag_start_oy_   = offset.dy;
            }

            if (dragging_ && drag_axes_index_ == axes_index)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    float dmx = ImGui::GetIO().MousePos.x - drag_start_mx_;
                    float dmy = ImGui::GetIO().MousePos.y - drag_start_my_;
                    offset.dx = drag_start_ox_ + dmx;
                    offset.dy = drag_start_oy_ + dmy;
                    consumed  = true;
                }
                else
                {
                    dragging_ = false;
                }
            }
        }

        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      cursor = ImGui::GetCursorScreenPos();

        int row = 0;
        for (auto& s : series_list)
        {
            if (!s || s->label().empty() || !s->show_in_legend())
                continue;

            auto& state = get_state(s.get());

            // Sync legend state with model if visibility was changed externally
            // (e.g. via inspector eye icon). Snap opacity immediately — no fade
            // animation for external changes, since the rendering idle gate would
            // cut the animation short before it completes (only 3 grace frames).
            bool model_vis = s->visible();
            if (state.user_visible != model_vis)
            {
                state.user_visible   = model_vis;
                state.target_opacity = model_vis ? 1.0f : 0.15f;
                state.opacity        = state.target_opacity;
            }

            float row_y = cursor.y + static_cast<float>(row) * row_height;
            float row_x = cursor.x;

            // Determine visual opacity
            float vis_alpha = state.opacity;
            Color sc        = s->color();

            // Color swatch
            ImU32 swatch_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(sc.r, sc.g, sc.b, sc.a * vis_alpha));
            float swatch_y = row_y + (row_height - swatch_size) * 0.5f;
            dl->AddRectFilled(ImVec2(row_x, swatch_y),
                              ImVec2(row_x + swatch_size, swatch_y + swatch_size),
                              swatch_col,
                              2.0f);

            // Series label — spec §4.3: use text_secondary for legend labels
            ImU32 text_col = ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors.text_secondary.r,
                                                                   theme_colors.text_secondary.g,
                                                                   theme_colors.text_secondary.b,
                                                                   vis_alpha));
            float label_x  = row_x + swatch_size + swatch_gap;
            float label_y  = row_y + (row_height - font_size) * 0.5f;
            dl->AddText(font, font_size, ImVec2(label_x, label_y), text_col, s->label().c_str());

            // Click-to-toggle: invisible button over the row
            if (toggleable_)
            {
                ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));
                const std::string btn_id = std::format("##legend_toggle_{:x}_{}_{}",
                                                       static_cast<unsigned long>(figure_id),
                                                       axes_index,
                                                       row);

                float btn_w = swatch_size + swatch_gap + max_label_w;
                if (ImGui::InvisibleButton(btn_id.c_str(), ImVec2(btn_w, row_height)))
                {
                    state.user_visible   = !state.user_visible;
                    state.target_opacity = state.user_visible ? 1.0f : 0.15f;

                    // Apply visibility to the actual series
                    s->visible(state.user_visible);
                    consumed = true;
                }

                // Hover highlight
                if (ImGui::IsItemHovered())
                {
                    ImU32 hover_col =
                        ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors.accent_subtle.r,
                                                              theme_colors.accent_subtle.g,
                                                              theme_colors.accent_subtle.b,
                                                              0.3f));
                    dl->AddRectFilled(ImVec2(row_x - 4.0f, row_y),
                                      ImVec2(row_x + btn_w + 4.0f, row_y + row_height),
                                      hover_col,
                                      3.0f);

                    // Cursor hint
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                // Eye icon (visibility indicator) on the right
                float       eye_x     = row_x + btn_w + 4.0f;
                float       eye_y     = row_y + (row_height - font_size * 0.7f) * 0.5f;
                const char* eye_label = state.user_visible ? "o" : "-";
                ImU32       eye_col =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors.text_tertiary.r,
                                                          theme_colors.text_tertiary.g,
                                                          theme_colors.text_tertiary.b,
                                                          theme_colors.text_tertiary.a));
                dl->AddText(font, font_size * 0.7f, ImVec2(eye_x, eye_y), eye_col, eye_label);
            }

            ++row;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return consumed;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
