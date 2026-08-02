#include "tab_bar.hpp"

#include <spectra/logger.hpp>

#include "ui/theme/design_tokens.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_internal.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace spectra
{

TabBar::TabBar()
{
    // Start with one default tab
    add_tab("Figure 1", true);
}

size_t TabBar::add_tab(const std::string& title, bool can_close)
{
    tabs_.emplace_back(title, can_close);
    size_t new_index = tabs_.size() - 1;
    SPECTRA_LOG_TRACE("tab", "Tab added: index={} title='{}'", new_index, title);

    // Auto-activate the new tab
    set_active_tab(new_index);

    return new_index;
}

void TabBar::remove_tab(size_t index)
{
    if (index >= tabs_.size())
    {
        return;
    }

    if (!tabs_[index].can_close)
    {
        return;   // Can't close this tab
    }

    SPECTRA_LOG_TRACE("tab", "Tab removed: index={} title='{}'", index, tabs_[index].title);

    // Notify callback before removal
    if (on_tab_close_)
    {
        on_tab_close_(index);
    }

    tabs_.erase(tabs_.begin() + index);

    // Adjust active tab if necessary
    if (active_tab_ >= tabs_.size())
    {
        active_tab_ = !tabs_.empty() ? tabs_.size() - 1 : 0;
    }
    else if (active_tab_ > index)
    {
        active_tab_--;
    }

    // Reset interaction state
    hovered_tab_   = SIZE_MAX;
    hovered_close_ = SIZE_MAX;
    is_dragging_   = false;
}

void TabBar::clear_tabs()
{
    tabs_.clear();
    active_tab_    = 0;
    hovered_tab_   = SIZE_MAX;
    hovered_close_ = SIZE_MAX;
    is_dragging_   = false;
}

void TabBar::set_tab_title(size_t index, const std::string& title)
{
    if (index < tabs_.size())
    {
        tabs_[index].title = title;
    }
}

const std::string& TabBar::get_tab_title(size_t index) const
{
    static const std::string empty;
    return (index < tabs_.size()) ? tabs_[index].title : empty;
}

void TabBar::set_active_tab(size_t index)
{
    if (index < tabs_.size() && index != active_tab_)
    {
        SPECTRA_LOG_TRACE("tab",
                          "Active tab changed: {} -> {} title='{}'",
                          active_tab_,
                          index,
                          tabs_[index].title);
        active_tab_ = index;

        // Notify callback
        if (on_tab_change_)
        {
            on_tab_change_(active_tab_);
        }
    }
}

void TabBar::draw(const Rect& bounds, bool menus_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (tabs_.empty())
    {
        return;
    }

    // Set up drawing context
    ImGui::SetCursorScreenPos(ImVec2(bounds.x, bounds.y));
    ImGui::PushClipRect(ImVec2(bounds.x, bounds.y),
                        ImVec2(bounds.x + bounds.w, bounds.y + bounds.h),
                        true);

    // Handle input interactions
    handle_input(bounds);

    // Draw the tabs
    draw_tabs(bounds, menus_open);

    // Draw add button if we have room
    if (tabs_.size() < 20)
    {   // Reasonable limit
        draw_add_button(bounds);
    }

    // Draw scroll buttons if needed
    if (needs_scroll_buttons(bounds))
    {
        draw_scroll_buttons(bounds);
    }

    // Draw context menu (must be outside clip rect)
    ImGui::PopClipRect();
    draw_context_menu();
#endif
}

void TabBar::set_tab_modified(size_t index, bool modified)
{
    if (index < tabs_.size())
    {
        tabs_[index].is_modified = modified;
    }
}

bool TabBar::is_tab_modified(size_t index) const
{
    if (index < tabs_.size())
    {
        return tabs_[index].is_modified;
    }
    return false;
}

bool TabBar::is_tab_hovered(size_t index) const
{
    return hovered_tab_ == index;
}

bool TabBar::is_close_button_hovered(size_t index) const
{
    return hovered_close_ == index;
}

void TabBar::handle_input(const Rect& bounds)
{
#ifdef SPECTRA_USE_IMGUI
    ImVec2 mouse_pos       = ImGui::GetMousePos();
    bool   mouse_in_bounds = (mouse_pos.x >= bounds.x && mouse_pos.x < bounds.x + bounds.w
                            && mouse_pos.y >= bounds.y && mouse_pos.y < bounds.y + bounds.h);

    // Always process ongoing drags, even when mouse is outside tab bar
    if (is_dragging_)
    {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            end_drag();
        }
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            update_drag(mouse_pos.x);
        }
        return;   // Don't process hover/click while dragging
    }

    if (!mouse_in_bounds)
    {
        hovered_tab_   = SIZE_MAX;
        hovered_close_ = SIZE_MAX;
        return;
    }

    // Compute tab layouts
    auto layouts = compute_tab_layouts(bounds);

    // Check hover state
    hovered_tab_   = get_tab_at_position(mouse_pos, layouts);
    hovered_close_ = get_close_button_at_position(mouse_pos, layouts);

    // Handle mouse clicks
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hovered_close_ < tabs_.size())
        {
            // Close button clicked
            remove_tab(hovered_close_);
        }
        else if (hovered_tab_ < tabs_.size())
        {
            // Tab clicked
            set_active_tab(hovered_tab_);
            start_drag(hovered_tab_, mouse_pos.x);
        }
    }

    // Middle-click on a tab closes it (Chrome-style)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        if (hovered_tab_ < tabs_.size())
        {
            remove_tab(hovered_tab_);
        }
    }

    // Handle right-click context menu
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (hovered_tab_ < tabs_.size())
        {
            context_menu_tab_  = hovered_tab_;
            context_menu_open_ = true;
            ImGui::OpenPopup("##tab_context_menu");
        }
    }
#endif
}

#ifdef SPECTRA_USE_IMGUI
static ImU32 to_imcol(const ui::Color& c, float alpha_override = -1.0f)
{
    float a = alpha_override >= 0.0f ? alpha_override : c.a;
    return IM_COL32(uint8_t(c.r * 255), uint8_t(c.g * 255), uint8_t(c.b * 255), uint8_t(a * 255));
}
#endif

void TabBar::draw_tabs(const Rect& bounds, bool menus_open)
{
#ifdef SPECTRA_USE_IMGUI
    auto        layouts   = compute_tab_layouts(bounds);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const auto& colors    = ui::theme();

    // Bottom border line across the full tab bar — quiet separator
    draw_list->AddLine(ImVec2(bounds.x, bounds.y + bounds.h - 1),
                       ImVec2(bounds.x + bounds.w, bounds.y + bounds.h - 1),
                       to_imcol(colors.border_subtle),
                       1.0f);

    for (size_t i = 0; i < layouts.size(); ++i)
    {
        const auto& layout = layouts[i];
        if (!layout.is_visible)
        {
            continue;
        }

        const auto& tab        = tabs_[i];
        bool        is_active  = (i == active_tab_);
        bool        is_hovered = (i == hovered_tab_);
        bool        is_dragged = (is_dragging_ && i == dragged_tab_);

        bool is_active_styled = is_active && !menus_open;

        float  gap           = 3.0f;
        float  selected_lift = is_active_styled ? SELECTED_HEIGHT_EXTRA : 0.0f;
        ImVec2 tl(layout.bounds.x + gap, layout.bounds.y + 3 - selected_lift);
        ImVec2 br(layout.bounds.x + layout.bounds.w - gap, layout.bounds.y + layout.bounds.h - 1);

        // Tab background: shared shell control surface language.
        ui::Color bg = ui::control_surface_color(colors, is_active_styled, is_hovered);
        if (is_dragged)
        {
            bg = colors.bg_elevated;
        }
        float bg_alpha = is_active_styled ? 0.96f : (is_hovered ? 0.78f : 0.52f);
        draw_list->AddRectFilled(tl, br, to_imcol(bg, bg_alpha), TAB_RADIUS);

        // Active tab: refined border + subtle top glow + selected underline.
        if (is_active_styled)
        {
            ui::Color glow = ui::control_glow_color(colors);
            for (int gi = 2; gi >= 1; --gi)
            {
                float e = static_cast<float>(gi);
                draw_list->AddRect(ImVec2(tl.x - e, tl.y - e),
                                   ImVec2(br.x + e, br.y + e),
                                   to_imcol(glow, 0.07f / e),
                                   TAB_RADIUS + e,
                                   0,
                                   1.25f);
            }
            draw_list->AddRect(tl,
                               br,
                               to_imcol(ui::control_border_color(colors, true, false), 0.85f),
                               TAB_RADIUS,
                               0,
                               1.0f);

            // Subtle top highlight
            draw_list->AddLine(
                ImVec2(tl.x + TAB_RADIUS * 0.5f, tl.y + 1.0f),
                ImVec2(br.x - TAB_RADIUS * 0.5f, tl.y + 1.0f),
                to_imcol(colors.accent.lerp(ui::Color(1.0f, 1.0f, 1.0f), 0.75f), 0.36f),
                1.0f);

            // Selected underline accent — centered, pill-ended.
            const float underline_w  = std::max(0.0f, br.x - tl.x - TAB_PADDING * 1.4f);
            const float underline_x0 = tl.x + (br.x - tl.x - underline_w) * 0.5f;
            draw_list->AddRectFilled(ImVec2(underline_x0, br.y - UNDERLINE_HEIGHT + 1.0f),
                                     ImVec2(underline_x0 + underline_w, br.y + 1.0f),
                                     to_imcol(colors.accent),
                                     UNDERLINE_HEIGHT * 0.5f);
        }
        else if (is_hovered)
        {
            draw_list->AddRect(tl,
                               br,
                               to_imcol(ui::control_border_color(colors, false, true), 0.55f),
                               TAB_RADIUS,
                               0,
                               1.0f);
        }

        // Tab title
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        float  title_x   = layout.bounds.x + TAB_PADDING;
        float  title_w   = layout.bounds.w - TAB_PADDING * 2.0f
                        - (tab.can_close ? (CLOSE_BUTTON_SIZE + CLOSE_PAD_RIGHT) : 0.0f);
        ImVec2 text_pos(title_x, tl.y + (br.y - tl.y - text_size.y) * 0.5f + 0.5f);

        ui::Color text_col   = ui::control_text_color(colors, is_active_styled, is_hovered);
        float     text_alpha = is_active_styled ? 1.0f : (is_hovered ? 0.95f : 0.82f);
        draw_list->PushClipRect(ImVec2(title_x - 2, tl.y),
                                ImVec2(title_x + title_w + 2, br.y),
                                true);
        draw_list->AddText(text_pos, to_imcol(text_col, text_alpha), tab.title.c_str());
        draw_list->PopClipRect();

        // Close button (always show for closeable tabs)
        if (tab.can_close)
        {
            bool  close_hovered = (i == hovered_close_);
            ImU32 close_color =
                close_hovered ? to_imcol(colors.error) : to_imcol(colors.text_secondary, 0.72f);

            ImVec2 close_center(
                tl.x + layout.bounds.w - TAB_PADDING * 0.55f - CLOSE_BUTTON_SIZE * 0.5f,
                tl.y + (br.y - tl.y) * 0.5f + 0.5f);

            // Close button hover background
            if (close_hovered)
            {
                draw_list->AddCircleFilled(close_center,
                                           CLOSE_BUTTON_SIZE * 0.55f,
                                           to_imcol(colors.error, 0.18f));
            }

            // Draw 'X'
            float sz = CLOSE_BUTTON_SIZE * 0.30f;
            draw_list->AddLine(ImVec2(close_center.x - sz, close_center.y - sz),
                               ImVec2(close_center.x + sz, close_center.y + sz),
                               close_color,
                               1.5f);
            draw_list->AddLine(ImVec2(close_center.x - sz, close_center.y + sz),
                               ImVec2(close_center.x + sz, close_center.y - sz),
                               close_color,
                               1.5f);
        }

        // Modified indicator dot
        if (tab.is_modified)
        {
            ImVec2 dot_pos(layout.bounds.x + 9, layout.bounds.y + 10);
            draw_list->AddCircleFilled(dot_pos, 2.5f, to_imcol(colors.warning));
        }
    }
#endif
}

void TabBar::draw_add_button(const Rect& bounds)
{
#ifdef SPECTRA_USE_IMGUI
    const auto& colors  = ui::theme();
    auto        layouts = compute_tab_layouts(bounds);

    // Position add button after the last tab
    float last_tab_end = bounds.x;
    if (!layouts.empty())
    {
        auto& last   = layouts.back();
        last_tab_end = last.bounds.x + last.bounds.w;
    }

    float btn_x = last_tab_end + ui::tokens::SPACE_2;
    float btn_h = TAB_HEIGHT + (tabs_.empty() ? 0.0f : SELECTED_HEIGHT_EXTRA);
    float btn_y = bounds.y + (bounds.h - btn_h) * 0.5f + 1.0f;
    float btn_w = ADD_BUTTON_WIDTH;

    // Don't draw if it would overflow
    if (btn_x + btn_w > bounds.x + bounds.w - ui::tokens::SPACE_2)
        return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2      mouse_pos = ImGui::GetMousePos();
    bool hovered = (mouse_pos.x >= btn_x && mouse_pos.x < btn_x + btn_w && mouse_pos.y >= btn_y
                    && mouse_pos.y < btn_y + btn_h);

    ui::Color bg = ui::control_surface_color(colors, false, hovered);
    draw_list->AddRectFilled(ImVec2(btn_x, btn_y),
                             ImVec2(btn_x + btn_w, btn_y + btn_h),
                             to_imcol(bg, hovered ? 0.80f : 0.56f),
                             TAB_RADIUS);
    draw_list->AddRect(
        ImVec2(btn_x, btn_y),
        ImVec2(btn_x + btn_w, btn_y + btn_h),
        to_imcol(ui::control_border_color(colors, false, hovered), hovered ? 0.70f : 0.48f),
        TAB_RADIUS,
        0,
        1.0f);

    // Plus sign
    ImVec2 center(btn_x + btn_w * 0.5f, btn_y + btn_h * 0.5f + 0.5f);
    ImU32  plus_color = hovered ? to_imcol(colors.accent) : to_imcol(colors.text_secondary, 0.88f);
    float  sz         = ui::tokens::TAB_BAR_PLUS_SIZE;
    draw_list->AddLine(ImVec2(center.x - sz, center.y),
                       ImVec2(center.x + sz, center.y),
                       plus_color,
                       1.5f);
    draw_list->AddLine(ImVec2(center.x, center.y - sz),
                       ImVec2(center.x, center.y + sz),
                       plus_color,
                       1.5f);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (on_tab_add_)
        {
            on_tab_add_();
        }
    }
#endif
}

std::vector<TabBar::TabLayout> TabBar::compute_tab_layouts(const Rect& bounds) const
{
    std::vector<TabLayout> layouts;
    layouts.reserve(tabs_.size());

    float current_x       = bounds.x + scroll_offset_;
    float available_width = bounds.w;

    for (const auto& tab : tabs_)
    {
        TabLayout layout;

        // Calculate tab width based on title
#ifdef SPECTRA_USE_IMGUI
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        float  text_w    = text_size.x;
#else
        // Fallback: estimate text width without ImGui
        float text_w = static_cast<float>(tab.title.size()) * 8.0f;
#endif
        float tab_width = std::clamp(
            text_w + TAB_PADDING * 2 + (tab.can_close ? CLOSE_BUTTON_SIZE + CLOSE_PAD_RIGHT : 0),
            TAB_MIN_WIDTH,
            TAB_MAX_WIDTH);

        layout.bounds = Rect{current_x, bounds.y, tab_width, TAB_HEIGHT};

        // Close button bounds (right side of tab)
        if (tab.can_close)
        {
            layout.close_bounds = Rect{current_x + tab_width - CLOSE_BUTTON_SIZE - CLOSE_PAD_RIGHT,
                                       bounds.y + (TAB_HEIGHT - CLOSE_BUTTON_SIZE) * 0.5f,
                                       CLOSE_BUTTON_SIZE,
                                       CLOSE_BUTTON_SIZE};
        }
        else
        {
            layout.close_bounds = Rect{0, 0, 0, 0};
        }

        // Check visibility
        layout.is_visible =
            (current_x + tab_width > bounds.x) && (current_x < bounds.x + available_width);
        layout.is_clipped =
            (current_x < bounds.x) || (current_x + tab_width > bounds.x + available_width);

        layouts.push_back(layout);
        current_x += tab_width;
    }

    return layouts;
}

#ifdef SPECTRA_USE_IMGUI
size_t TabBar::get_tab_at_position(const ImVec2& pos, const std::vector<TabLayout>& layouts) const
{
    for (size_t i = 0; i < layouts.size(); ++i)
    {
        const auto& layout = layouts[i];
        if (!layout.is_visible)
        {
            continue;
        }

        if (pos.x >= layout.bounds.x && pos.x < layout.bounds.x + layout.bounds.w
            && pos.y >= layout.bounds.y && pos.y < layout.bounds.y + layout.bounds.h)
        {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t TabBar::get_close_button_at_position(const ImVec2&                 pos,
                                            const std::vector<TabLayout>& layouts) const
{
    for (size_t i = 0; i < layouts.size(); ++i)
    {
        const auto& layout = layouts[i];
        if (!layout.is_visible || !tabs_[i].can_close)
        {
            continue;
        }

        if (pos.x >= layout.close_bounds.x && pos.x < layout.close_bounds.x + layout.close_bounds.w
            && pos.y >= layout.close_bounds.y
            && pos.y < layout.close_bounds.y + layout.close_bounds.h)
        {
            return i;
        }
    }
    return SIZE_MAX;
}
#endif   // SPECTRA_USE_IMGUI

void TabBar::start_drag(size_t tab_index, float mouse_x)
{
#ifdef SPECTRA_USE_IMGUI
    SPECTRA_LOG_TRACE("ui", "Tab drag started: tab={}", tab_index);
    is_dragging_      = true;
    dragged_tab_      = tab_index;
    drag_offset_x_    = mouse_x;
    drag_start_y_     = ImGui::GetMousePos().y;
    is_dock_dragging_ = false;
#else
    (void)tab_index;
    (void)mouse_x;
#endif
}

void TabBar::update_drag(float mouse_x)
{
#ifdef SPECTRA_USE_IMGUI
    if (dragged_tab_ >= tabs_.size())
        return;

    ImVec2 mouse_pos = ImGui::GetMousePos();
    float  delta_y   = mouse_pos.y - drag_start_y_;

    // If dragged far enough vertically, switch to dock-drag mode
    if (!is_dock_dragging_ && std::abs(delta_y) > 30.0f)
    {
        is_dock_dragging_ = true;
        if (on_tab_drag_out_)
        {
            on_tab_drag_out_(dragged_tab_, mouse_pos.x, mouse_pos.y);
        }
        return;
    }

    if (is_dock_dragging_)
    {
        // In dock-drag mode — forward position updates
        if (on_tab_drag_update_)
        {
            on_tab_drag_update_(dragged_tab_, mouse_pos.x, mouse_pos.y);
        }
        return;
    }

    // Normal horizontal reorder drag
    float delta = mouse_x - drag_offset_x_;
    if (std::abs(delta) < 5.0f)
        return;   // Dead zone

    // Check if we should swap with adjacent tab
    if (delta > 30.0f && dragged_tab_ + 1 < tabs_.size())
    {
        std::swap(tabs_[dragged_tab_], tabs_[dragged_tab_ + 1]);
        if (active_tab_ == dragged_tab_)
            active_tab_++;
        else if (active_tab_ == dragged_tab_ + 1)
            active_tab_--;
        if (on_tab_reorder_)
            on_tab_reorder_(dragged_tab_, dragged_tab_ + 1);
        dragged_tab_++;
        drag_offset_x_ = mouse_x;
    }
    else if (delta < -30.0f && dragged_tab_ > 0)
    {
        std::swap(tabs_[dragged_tab_], tabs_[dragged_tab_ - 1]);
        if (active_tab_ == dragged_tab_)
            active_tab_--;
        else if (active_tab_ == dragged_tab_ - 1)
            active_tab_++;
        if (on_tab_reorder_)
            on_tab_reorder_(dragged_tab_, dragged_tab_ - 1);
        dragged_tab_--;
        drag_offset_x_ = mouse_x;
    }
#else
    (void)mouse_x;
#endif
}

void TabBar::cancel_drag()
{
    if (is_dragging_ && on_tab_drag_cancel_)
        on_tab_drag_cancel_(dragged_tab_);
    is_dragging_      = false;
    is_dock_dragging_ = false;
    dragged_tab_      = SIZE_MAX;
}

void TabBar::end_drag()
{
#ifdef SPECTRA_USE_IMGUI
    SPECTRA_LOG_TRACE("ui", "Tab drag ended: tab={}", dragged_tab_);
    if (is_dock_dragging_ && dragged_tab_ < tabs_.size())
    {
        ImVec2 mouse_pos = ImGui::GetMousePos();

        // Check if mouse is outside the main window — detach to new window
        ImVec2 display_size   = ImGui::GetIO().DisplaySize;
        bool   outside_window = (mouse_pos.x < 0 || mouse_pos.y < 0 || mouse_pos.x >= display_size.x
                               || mouse_pos.y >= display_size.y);

        if (outside_window && on_tab_detach_ && tabs_.size() > 1)
        {
            // Convert to screen coordinates for new window placement
            ImVec2 window_pos = ImGui::GetMainViewport()->Pos;
            float  screen_x   = window_pos.x + mouse_pos.x;
            float  screen_y   = window_pos.y + mouse_pos.y;
            on_tab_detach_(dragged_tab_, screen_x, screen_y);
        }
        else if (on_tab_drag_end_)
        {
            on_tab_drag_end_(dragged_tab_, mouse_pos.x, mouse_pos.y);
        }
    }
#endif
    is_dragging_      = false;
    is_dock_dragging_ = false;
    dragged_tab_      = SIZE_MAX;
}

bool TabBar::needs_scroll_buttons(const Rect& bounds) const
{
    // Check if total tab width exceeds available space
    float total_width = 0.0f;
    for (const auto& tab : tabs_)
    {
#ifdef SPECTRA_USE_IMGUI
        ImVec2 text_size = ImGui::CalcTextSize(tab.title.c_str());
        float  text_w    = text_size.x;
#else
        float text_w = static_cast<float>(tab.title.size()) * 8.0f;
#endif
        float tab_width = std::clamp(
            text_w + TAB_PADDING * 2 + (tab.can_close ? CLOSE_BUTTON_SIZE + CLOSE_PAD_RIGHT : 0),
            TAB_MIN_WIDTH,
            TAB_MAX_WIDTH);
        total_width += tab_width;
    }

    return total_width > bounds.w;
}

void TabBar::draw_scroll_buttons(const Rect& bounds)
{
#ifdef SPECTRA_USE_IMGUI
    const auto& colors    = ui::theme();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float       btn_w     = 20.0f;
    float       btn_h     = bounds.h - 4.0f;
    ImVec2      mouse_pos = ImGui::GetMousePos();

    // Left scroll button
    if (scroll_offset_ < 0.0f)
    {
        float lx   = bounds.x;
        float ly   = bounds.y + 2.0f;
        bool  lhov = (mouse_pos.x >= lx && mouse_pos.x < lx + btn_w && mouse_pos.y >= ly
                     && mouse_pos.y < ly + btn_h);
        draw_list->AddRectFilled(
            ImVec2(lx, ly),
            ImVec2(lx + btn_w, ly + btn_h),
            lhov ? to_imcol(colors.accent_subtle) : to_imcol(colors.bg_elevated),
            ui::tokens::RADIUS_SM);
        ImVec2 arrow_center(lx + btn_w * 0.5f, ly + btn_h * 0.5f);
        draw_list->AddTriangleFilled(ImVec2(arrow_center.x + 4, arrow_center.y - 5),
                                     ImVec2(arrow_center.x + 4, arrow_center.y + 5),
                                     ImVec2(arrow_center.x - 4, arrow_center.y),
                                     to_imcol(lhov ? colors.accent : colors.text_secondary));
        if (lhov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            scroll_offset_ = std::min(scroll_offset_ + 100.0f, 0.0f);
        }
    }

    // Right scroll button
    {
        float rx   = bounds.x + bounds.w - btn_w;
        float ry   = bounds.y + 2.0f;
        bool  rhov = (mouse_pos.x >= rx && mouse_pos.x < rx + btn_w && mouse_pos.y >= ry
                     && mouse_pos.y < ry + btn_h);
        draw_list->AddRectFilled(
            ImVec2(rx, ry),
            ImVec2(rx + btn_w, ry + btn_h),
            rhov ? to_imcol(colors.accent_subtle) : to_imcol(colors.bg_elevated),
            ui::tokens::RADIUS_SM);
        ImVec2 arrow_center(rx + btn_w * 0.5f, ry + btn_h * 0.5f);
        draw_list->AddTriangleFilled(ImVec2(arrow_center.x - 4, arrow_center.y - 5),
                                     ImVec2(arrow_center.x - 4, arrow_center.y + 5),
                                     ImVec2(arrow_center.x + 4, arrow_center.y),
                                     to_imcol(rhov ? colors.accent : colors.text_secondary));
        if (rhov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            scroll_offset_ -= 100.0f;
        }
    }
#else
    (void)bounds;
#endif
}

void TabBar::draw_context_menu()
{
#ifdef SPECTRA_USE_IMGUI
    const auto& colors = ui::theme();

    // Modern popup styling
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

    if (ImGui::BeginPopup("##tab_context_menu"))
    {
        // Auto-close on mouse leave
        ImVec2 mouse      = ImGui::GetIO().MousePos;
        ImVec2 popup_pos  = ImGui::GetWindowPos();
        ImVec2 popup_size = ImGui::GetWindowSize();
        float  margin     = 20.0f;
        bool   mouse_in_zone =
            (mouse.x >= popup_pos.x - margin && mouse.x <= popup_pos.x + popup_size.x + margin
             && mouse.y >= popup_pos.y - margin && mouse.y <= popup_pos.y + popup_size.y + margin);
        if (!mouse_in_zone && !ImGui::IsAnyItemActive())
        {
            ImGui::CloseCurrentPopup();
        }

        // Draw shadow
        ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
        bg_dl->AddRectFilled(ImVec2(popup_pos.x + 2, popup_pos.y + 3),
                             ImVec2(popup_pos.x + popup_size.x + 2, popup_pos.y + popup_size.y + 5),
                             IM_COL32(0, 0, 0, 30),
                             ui::tokens::RADIUS_LG + 2);

        if (context_menu_tab_ < tabs_.size())
        {
            const auto& tab = tabs_[context_menu_tab_];

            auto menu_item = [&](const char* label) -> bool
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(colors.accent_subtle.r,
                                             colors.accent_subtle.g,
                                             colors.accent_subtle.b,
                                             0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      ImVec4(colors.accent_muted.r,
                                             colors.accent_muted.g,
                                             colors.accent_muted.b,
                                             0.75f));
                float item_h = ImGui::GetTextLineHeight() + 8.0f;
                bool  clicked =
                    ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(0, item_h));
                ImGui::PopStyleColor(3);
                return clicked;
            };

            if (menu_item("Rename..."))
            {
                renaming_tab_     = true;
                rename_tab_index_ = context_menu_tab_;
                strncpy(rename_buffer_, tab.title.c_str(), sizeof(rename_buffer_) - 1);
                rename_buffer_[sizeof(rename_buffer_) - 1] = '\0';
            }

            if (menu_item("New Tab"))
            {
                if (on_tab_add_)
                    on_tab_add_();
            }

            if (menu_item("Duplicate"))
            {
                if (on_tab_duplicate_)
                    on_tab_duplicate_(context_menu_tab_);
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

            if (menu_item("Split Right"))
            {
                if (on_tab_split_right_)
                    on_tab_split_right_(context_menu_tab_);
            }

            if (menu_item("Split Down"))
            {
                if (on_tab_split_down_)
                    on_tab_split_down_(context_menu_tab_);
            }

            if (tabs_.size() > 1)
            {
                if (menu_item("Detach to Window"))
                {
                    if (on_tab_detach_)
                    {
                        ImVec2 mouse = ImGui::GetMousePos();
                        on_tab_detach_(context_menu_tab_, mouse.x, mouse.y);
                    }
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

            if (tab.can_close)
            {
                if (menu_item("Close"))
                    remove_tab(context_menu_tab_);
            }

            if (tabs_.size() > 1)
            {
                if (menu_item("Close Others"))
                {
                    if (on_tab_close_all_except_)
                        on_tab_close_all_except_(context_menu_tab_);
                }
            }

            if (context_menu_tab_ + 1 < tabs_.size())
            {
                if (menu_item("Close to the Right"))
                {
                    if (on_tab_close_to_right_)
                        on_tab_close_to_right_(context_menu_tab_);
                }
            }
        }
        ImGui::EndPopup();
    }
    else
    {
        context_menu_open_ = false;
        context_menu_tab_  = SIZE_MAX;
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    // Rename popup — modern styling
    if (renaming_tab_ && rename_tab_index_ < tabs_.size())
    {
        ImGui::OpenPopup("##tab_rename_popup");
        renaming_tab_ = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.98f));

    if (ImGui::BeginPopup("##tab_rename_popup"))
    {
        ImGui::TextUnformatted("Rename tab");
        ImGui::Spacing();
        bool enter_pressed = ImGui::InputText("##rename_input",
                                              rename_buffer_,
                                              sizeof(rename_buffer_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_2));
        if (enter_pressed || ImGui::Button("OK"))
        {
            std::string new_title(rename_buffer_);
            if (!new_title.empty() && rename_tab_index_ < tabs_.size())
            {
                tabs_[rename_tab_index_].title = new_title;
                if (on_tab_rename_)
                    on_tab_rename_(rename_tab_index_, new_title);
            }
            rename_tab_index_ = SIZE_MAX;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            rename_tab_index_ = SIZE_MAX;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
#endif
}

void TabBar::scroll_to_tab(size_t index)
{
#ifdef SPECTRA_USE_IMGUI
    if (index >= tabs_.size())
        return;
    // Compute approximate position of the target tab
    float x = 0.0f;
    for (size_t i = 0; i < index; ++i)
    {
        ImVec2 ts = ImGui::CalcTextSize(tabs_[i].title.c_str());
        float  tw = std::clamp(
            ts.x + TAB_PADDING * 2 + (tabs_[i].can_close ? CLOSE_BUTTON_SIZE + CLOSE_PAD_RIGHT : 0),
            TAB_MIN_WIDTH,
            TAB_MAX_WIDTH);
        x += tw;
    }
    // Adjust scroll so the tab is visible
    scroll_offset_ = -std::max(0.0f, x - 50.0f);
#else
    (void)index;
#endif
}

}   // namespace spectra
