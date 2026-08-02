#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

    #include <format>

namespace spectra
{

// ─── Per-pane tab headers ────────────────────────────────────────────────────
// Draws a compact tab bar above each split pane leaf. Supports:
//  - Click to switch active figure within a pane
//  - Drag tabs between panes (cross-pane drag)
//  - Smooth animated tab positions and drag ghost

void ImGuiIntegration::draw_pane_tab_headers()
{
    if (!dock_system_)
        return;

    // Draw pane tab headers into per-pane ImGui windows so that ImGui's own
    // window z-ordering naturally puts popups (menus, context menus) above
    // the tab headers.  Previously we used GetForegroundDrawList() which
    // renders above ALL windows including popups — causing the z-order bug.

    auto&  theme = ui::theme();
    float  dt    = ImGui::GetIO().DeltaTime;
    ImVec2 mouse = ImGui::GetMousePos();

    constexpr float TAB_H          = SplitPane::PANE_TAB_HEIGHT;
    constexpr float TAB_PAD        = 8.0f;
    constexpr float TAB_MIN_W      = 60.0f;
    constexpr float TAB_MAX_W      = 150.0f;
    constexpr float CLOSE_SZ       = 12.0f;
    constexpr float ADD_BTN_W      = 22.0f;
    constexpr float ANIM_SPEED     = 14.0f;
    constexpr float DRAG_THRESHOLD = 5.0f;

    auto panes = dock_system_->split_view().all_panes();
    (void)dock_system_->active_figure_index();   // Available if needed

    // Helper: get figure title
    auto fig_title = [&](size_t fig_idx) -> std::string
    {
        if (get_figure_title_)
            return get_figure_title_(fig_idx);
        return "Figure " + std::to_string(fig_idx + 1);
    };

    // Helper: ImU32 from theme color
    auto to_col = [](const ui::Color& c, float a = -1.0f) -> ImU32
    {
        float alpha = a >= 0.0f ? a : c.a;
        return IM_COL32(uint8_t(c.r * 255),
                        uint8_t(c.g * 255),
                        uint8_t(c.b * 255),
                        uint8_t(alpha * 255));
    };

    // ── Phase 1: Compute tab layouts per pane ────────────────────────────

    struct TabRect
    {
        size_t figure_index;
        float  x, y, w, h;
        bool   is_active;
        bool   is_hovered;
    };

    struct PaneHeader
    {
        SplitPane*           pane;
        Rect                 header_rect;
        std::vector<TabRect> tabs;
        Rect                 add_button_rect{};
        bool                 show_add_button = false;
    };

    std::vector<PaneHeader> headers;
    headers.reserve(panes.size());

    // Compute insertion gap: when dragging a tab over a pane header,
    // determine which position the tab would be inserted at
    constexpr float GAP_WIDTH        = 60.0f;   // Width of the insertion gap
    bool            has_active_gap   = false;
    uint32_t        gap_pane_id      = 0;
    size_t          gap_insert_after = SIZE_MAX;   // Insert after this local index

    if (pane_tab_drag_.dragging && pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID)
    {
        for (auto* pane_const : panes)
        {
            auto* pane = const_cast<SplitPane*>(pane_const);
            if (!pane->is_leaf())
                continue;
            Rect b = pane->bounds();
            Rect hr{b.x, b.y, b.w, TAB_H};
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                && mouse.y < hr.y + hr.h + 10)
            {
                // Mouse is over this pane's header — compute insertion index
                if (pane->id() != pane_tab_drag_.source_pane_id || pane->figure_count() > 1)
                {
                    gap_pane_id      = pane->id();
                    has_active_gap   = true;
                    gap_insert_after = SIZE_MAX;   // Before first tab by default
                    const auto& figs = pane->figure_indices();
                    float       cx   = hr.x + 2.0f;
                    for (size_t li = 0; li < figs.size(); ++li)
                    {
                        if (figs[li] == pane_tab_drag_.dragged_figure_index)
                        {
                            cx += 0;   // Skip the dragged tab's width
                            continue;
                        }
                        std::string t   = fig_title(figs[li]);
                        ImVec2      tsz = ImGui::CalcTextSize(t.c_str());
                        float w = std::clamp(tsz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
                        if (mouse.x > cx + w * 0.5f)
                        {
                            gap_insert_after = li;
                        }
                        cx += w + 1.0f;
                    }
                }
                break;
            }
        }
    }

    // Update insertion gap animation
    float lerp_t_gap = std::min(1.0f, ANIM_SPEED * dt);
    if (has_active_gap)
    {
        insertion_gap_.target_pane_id   = gap_pane_id;
        insertion_gap_.insert_after_idx = gap_insert_after;
        insertion_gap_.target_gap       = GAP_WIDTH;
    }
    else
    {
        insertion_gap_.target_gap = 0.0f;
    }
    insertion_gap_.current_gap +=
        (insertion_gap_.target_gap - insertion_gap_.current_gap) * lerp_t_gap;
    if (insertion_gap_.current_gap < 0.5f && insertion_gap_.target_gap == 0.0f)
    {
        insertion_gap_.current_gap      = 0.0f;
        insertion_gap_.target_pane_id   = 0;
        insertion_gap_.insert_after_idx = SIZE_MAX;
    }

    for (auto* pane_const : panes)
    {
        auto* pane = const_cast<SplitPane*>(pane_const);
        if (!pane->is_leaf())
            continue;

        Rect b = pane->bounds();
        Rect hr{b.x, b.y, b.w, TAB_H};

        PaneHeader ph{};
        ph.pane        = pane;
        ph.header_rect = hr;

        const auto& figs  = pane->figure_indices();
        float       cur_x = hr.x + 2.0f;

        // Check if this pane has an active insertion gap
        bool pane_has_gap =
            (insertion_gap_.current_gap > 0.1f && pane->id() == insertion_gap_.target_pane_id);

        for (size_t li = 0; li < figs.size(); ++li)
        {
            size_t      fig_idx = figs[li];
            std::string title   = fig_title(fig_idx);

            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float  tw      = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);

            // Add insertion gap before this tab if needed
            bool insert_gap_here = pane_has_gap
                                   && ((insertion_gap_.insert_after_idx == SIZE_MAX && li == 0)
                                       || (li > 0 && (li - 1) == insertion_gap_.insert_after_idx));
            if (insert_gap_here)
            {
                cur_x += insertion_gap_.current_gap;
            }

            // Animate position (keyed by pane+figure to avoid cross-pane interference)
            auto& anim    = pane_tab_anims_[{ph.pane->id(), fig_idx}];
            anim.target_x = cur_x;
            if (anim.current_x == 0.0f && anim.target_x != 0.0f)
            {
                anim.current_x = anim.target_x;   // First frame: snap
            }
            float lerp_t = std::min(1.0f, ANIM_SPEED * dt);
            anim.current_x += (anim.target_x - anim.current_x) * lerp_t;

            float draw_x = anim.current_x;

            bool is_active_local = (li == pane->active_local_index());
            bool hovered         = (mouse.x >= draw_x && mouse.x < draw_x + tw && mouse.y >= hr.y
                            && mouse.y < hr.y + TAB_H);
            anim.target_opacity  = hovered ? 1.0f : (is_active_local ? 0.72f : 0.0f);
            anim.opacity += (anim.target_opacity - anim.opacity) * lerp_t;

            TabRect tr{};
            tr.figure_index = fig_idx;
            tr.x            = draw_x;
            tr.y            = hr.y;
            tr.w            = tw;
            tr.h            = TAB_H;
            tr.is_active    = is_active_local;
            tr.is_hovered   = hovered;
            ph.tabs.push_back(tr);

            cur_x += tw + 1.0f;
        }

        float add_x = figs.empty() ? (hr.x + 4.0f) : (cur_x + 4.0f);
        float add_w = ADD_BTN_W;
        if (add_x + add_w <= hr.x + hr.w - 4.0f)
        {
            ph.show_add_button = true;
            ph.add_button_rect = Rect{add_x, hr.y + 4.0f, add_w, TAB_H - 8.0f};
        }

        headers.push_back(std::move(ph));
    }

    // ── Phase 2: Draw + input handling via per-pane ImGui windows ───────
    // Each pane header is drawn inside its own ImGui window so that ImGui's
    // z-ordering naturally puts popups (menus) above these windows.

    pane_tab_hovered_ = false;

    ImGuiWindowFlags pane_win_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoInputs;

    for (auto& ph : headers)
    {
        Rect hr = ph.header_rect;

        // Create a tiny ImGui window covering this pane's tab header area
        const std::string win_id = std::format("##pane_tab_{}", ph.pane->id());
        ImGui::SetNextWindowPos(ImVec2(hr.x, hr.y));
        ImGui::SetNextWindowSize(ImVec2(hr.w, hr.h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(0, 0, 0, 0));   // transparent — we draw bg ourselves

        if (!ImGui::Begin(win_id.c_str(), nullptr, pane_win_flags))
        {
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            continue;
        }

        auto* draw_list = ImGui::GetWindowDrawList();

        // Draw header background and borders
        draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                 ImVec2(hr.x + hr.w, hr.y + hr.h),
                                 to_col(theme.bg_secondary.lerp(theme.bg_primary, 0.12f)));
        draw_list->AddLine(ImVec2(hr.x, hr.y),
                           ImVec2(hr.x + hr.w, hr.y),
                           to_col(theme.border_subtle, 0.28f),
                           1.0f);
        draw_list->AddLine(ImVec2(hr.x, hr.y + hr.h - 1),
                           ImVec2(hr.x + hr.w, hr.y + hr.h - 1),
                           to_col(theme.border_default, 0.70f),
                           1.0f);
        if (insertion_gap_.current_gap > 0.1f && ph.pane->id() == insertion_gap_.target_pane_id)
        {
            float gap_x = hr.x + 8.0f;
            if (!ph.tabs.empty())
            {
                if (insertion_gap_.insert_after_idx == SIZE_MAX)
                    gap_x = ph.tabs.front().x - insertion_gap_.current_gap * 0.5f;
                else
                {
                    size_t idx = std::min(insertion_gap_.insert_after_idx, ph.tabs.size() - 1);
                    gap_x = ph.tabs[idx].x + ph.tabs[idx].w + insertion_gap_.current_gap * 0.5f;
                }
            }
            draw_list->AddLine(ImVec2(gap_x, hr.y + 5.0f),
                               ImVec2(gap_x, hr.y + hr.h - 5.0f),
                               to_col(theme.accent_hover, 0.92f),
                               2.0f);
            draw_list->AddLine(ImVec2(gap_x, hr.y + 5.0f),
                               ImVec2(gap_x, hr.y + hr.h - 5.0f),
                               to_col(theme.accent_glow, 0.20f),
                               5.0f);
        }

        for (auto& tr : ph.tabs)
        {
            bool is_being_dragged =
                pane_tab_drag_.dragging && pane_tab_drag_.dragged_figure_index == tr.figure_index;

            // Skip drawing the tab in its original position if it's being dragged
            // cross-pane or if the tearoff preview card is active
            if (is_being_dragged && (pane_tab_drag_.cross_pane || pane_tab_drag_.preview_active))
                continue;

            // Tab background
            ImU32 bg         = 0;
            ImU32 border_col = 0;
            bool  is_active_styled =
                tr.is_active && !is_menu_open();   // Don't show active styling when menus are open
            auto& anim     = pane_tab_anims_[{ph.pane->id(), tr.figure_index}];
            float motion_t = std::clamp(anim.opacity, 0.0f, 1.0f);
            float active_t = is_active_styled ? std::max(0.72f, motion_t) : 0.0f;
            float hover_t  = tr.is_hovered ? std::max(0.45f, motion_t) : motion_t * 0.45f;
            if (is_being_dragged)
            {
                bg         = to_col(theme.bg_elevated);
                border_col = to_col(theme.border_strong, 0.88f);
            }
            else if (is_active_styled)
            {
                bg = to_col(theme.bg_tertiary.lerp(theme.accent, 0.10f + active_t * 0.08f), 0.96f);
                border_col = to_col(theme.border_default, 0.92f);
            }
            else if (tr.is_hovered)
            {
                bg         = to_col(theme.bg_tertiary.lerp(theme.accent, 0.06f + hover_t * 0.06f),
                            0.72f + hover_t * 0.18f);
                border_col = to_col(theme.border_subtle, 0.68f + hover_t * 0.14f);
            }
            else
            {
                bg         = to_col(theme.bg_secondary.lerp(theme.bg_tertiary, 0.18f), 0.35f);
                border_col = to_col(theme.border_subtle, 0.20f);
            }

            float  lift    = active_t * 2.0f + hover_t * 0.7f;
            float  inset_y = 4.0f - active_t * 1.4f;
            ImVec2 tl(tr.x, tr.y + inset_y - lift);
            ImVec2 br(tr.x + tr.w, tr.y + tr.h - lift * 0.3f);
            if (is_active_styled || is_being_dragged)
            {
                draw_list->AddRectFilled(ImVec2(tl.x, tl.y + 1.0f),
                                         ImVec2(br.x, br.y + 3.0f),
                                         IM_COL32(0, 0, 0, 44),
                                         6.0f,
                                         ImDrawFlags_RoundCornersTop);
            }
            if (hover_t > 0.01f || active_t > 0.01f)
            {
                draw_list->AddRectFilled(
                    ImVec2(tl.x - 1.0f, tl.y - 1.0f),
                    ImVec2(br.x + 1.0f, br.y + 1.5f),
                    to_col(theme.accent_glow, 0.06f + hover_t * 0.04f + active_t * 0.08f),
                    6.0f,
                    ImDrawFlags_RoundCornersTop);
            }
            draw_list->AddRectFilled(tl, br, bg, 4.0f, ImDrawFlags_RoundCornersTop);
            draw_list->AddRect(tl, br, border_col, 4.0f, ImDrawFlags_RoundCornersTop);

            float underline_t = is_active_styled ? 1.0f : hover_t;
            if (underline_t > 0.01f)
            {
                float cx       = (tl.x + br.x) * 0.5f;
                float half_len = (tr.w - 10.0f) * (0.18f + underline_t * 0.82f) * 0.5f;
                draw_list->AddLine(ImVec2(cx - half_len, br.y - 1.0f),
                                   ImVec2(cx + half_len, br.y - 1.0f),
                                   to_col(is_active_styled ? theme.accent_hover : theme.accent,
                                          0.35f + underline_t * 0.65f),
                                   is_active_styled ? 2.5f : 1.5f);
            }

            // Title text
            std::string title   = fig_title(tr.figure_index);
            ImVec2      text_sz = ImGui::CalcTextSize(title.c_str());
            ImVec2      text_pos(tr.x + TAB_PAD, tr.y + (tr.h - text_sz.y) * 0.5f);

            draw_list->PushClipRect(ImVec2(tr.x, tr.y),
                                    ImVec2(tr.x + tr.w - CLOSE_SZ - 2, tr.y + tr.h),
                                    true);
            draw_list->AddText(
                text_pos,
                is_active_styled ? to_col(theme.text_primary) : to_col(theme.text_secondary),
                title.c_str());
            draw_list->PopClipRect();

            float close_vis = tr.is_hovered ? 1.0f : (tr.is_active ? 0.24f : 0.0f);
            if (close_vis > 0.01f)
            {
                float cx = tr.x + tr.w - CLOSE_SZ * 0.5f - 4.0f;
                float cy = tr.y + tr.h * 0.5f - lift * 0.2f;
                float sz = 3.5f;

                bool close_hovered = (std::abs(mouse.x - cx) < CLOSE_SZ * 0.5f
                                      && std::abs(mouse.y - cy) < CLOSE_SZ * 0.5f);
                if (close_hovered)
                {
                    draw_list->AddCircleFilled(ImVec2(cx, cy),
                                               CLOSE_SZ * 0.5f,
                                               to_col(theme.error, 0.18f));
                }
                ImU32 x_col = close_hovered
                                  ? to_col(theme.error)
                                  : to_col(theme.text_tertiary, 0.45f + close_vis * 0.40f);
                draw_list->AddLine(ImVec2(cx - sz, cy - sz), ImVec2(cx + sz, cy + sz), x_col, 1.5f);
                draw_list->AddLine(ImVec2(cx - sz, cy + sz), ImVec2(cx + sz, cy - sz), x_col, 1.5f);

                // Close click — route through FigureManager callback
                if (close_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (pane_tab_close_cb_)
                        pane_tab_close_cb_(tr.figure_index);
                    pane_tab_hovered_ = true;
                    continue;
                }
            }

            // Click / drag handling
            if (tr.is_hovered)
            {
                pane_tab_hovered_ = true;

                // Middle-click closes the tab (Chrome-style)
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                {
                    if (pane_tab_close_cb_)
                        pane_tab_close_cb_(tr.figure_index);
                    continue;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Activate this tab
                    for (size_t li = 0; li < ph.pane->figure_indices().size(); ++li)
                    {
                        if (ph.pane->figure_indices()[li] == tr.figure_index)
                        {
                            dock_system_->activate_local_tab(ph.pane->id(), li);
                            break;
                        }
                    }
                    // Start potential drag via TabDragController
                    if (tab_drag_controller_)
                    {
                        tab_drag_controller_->on_mouse_down(ph.pane->id(),
                                                            tr.figure_index,
                                                            mouse.x,
                                                            mouse.y);
                        tab_drag_controller_->set_ghost_title(fig_title(tr.figure_index));
                    }
                    // Sync to legacy state for rendering compatibility
                    pane_tab_drag_.dragging             = false;
                    pane_tab_drag_.source_pane_id       = ph.pane->id();
                    pane_tab_drag_.dragged_figure_index = tr.figure_index;
                    pane_tab_drag_.drag_start_x         = mouse.x;
                    pane_tab_drag_.drag_start_y         = mouse.y;
                    pane_tab_drag_.cross_pane           = false;
                    pane_tab_drag_.dock_dragging        = false;
                    // Capture source tab rect for preview animation origin
                    pane_tab_drag_.source_tab_x    = tr.x;
                    pane_tab_drag_.source_tab_y    = tr.y;
                    pane_tab_drag_.source_tab_w    = tr.w;
                    pane_tab_drag_.source_tab_h    = tr.h;
                    pane_tab_drag_.preview_active  = false;
                    pane_tab_drag_.preview_scale   = 0.0f;
                    pane_tab_drag_.preview_opacity = 0.0f;
                    pane_tab_drag_.preview_shadow  = 0.0f;
                }

                // Right-click context menu
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    pane_ctx_menu_fig_  = tr.figure_index;
                    pane_ctx_menu_open_ = true;
                    ImGui::OpenPopup("##pane_tab_ctx");
                }
            }
        }

        if (ph.show_add_button)
        {
            const Rect& add_rect    = ph.add_button_rect;
            bool        add_hovered = (mouse.x >= add_rect.x && mouse.x < add_rect.x + add_rect.w
                                && mouse.y >= add_rect.y && mouse.y < add_rect.y + add_rect.h);

            if (add_hovered)
                pane_tab_hovered_ = true;

            ImU32 add_bg = add_hovered
                               ? to_col(theme.bg_tertiary.lerp(theme.accent, 0.08f), 0.84f)
                               : to_col(theme.bg_secondary.lerp(theme.bg_tertiary, 0.18f), 0.35f);
            draw_list->AddRectFilled(ImVec2(add_rect.x, add_rect.y),
                                     ImVec2(add_rect.x + add_rect.w, add_rect.y + add_rect.h),
                                     add_bg,
                                     4.0f);
            draw_list->AddRect(ImVec2(add_rect.x, add_rect.y),
                               ImVec2(add_rect.x + add_rect.w, add_rect.y + add_rect.h),
                               add_hovered ? to_col(theme.border_default, 0.88f)
                                           : to_col(theme.border_subtle, 0.24f),
                               4.0f);

            ImVec2 center(add_rect.x + add_rect.w * 0.5f, add_rect.y + add_rect.h * 0.5f);
            ImU32  add_col = add_hovered ? to_col(theme.accent) : to_col(theme.text_tertiary);
            float  add_sz  = 4.5f;
            draw_list->AddLine(ImVec2(center.x - add_sz, center.y),
                               ImVec2(center.x + add_sz, center.y),
                               add_col,
                               1.5f);
            draw_list->AddLine(ImVec2(center.x, center.y - add_sz),
                               ImVec2(center.x, center.y + add_sz),
                               add_col,
                               1.5f);

            if (add_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (command_registry_)
                    command_registry_->execute("figure.new");
            }
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    // ── Phase 3: Drag update ─────────────────────────────────────────────
    // The TabDragController manages the state machine (threshold detection,
    // dock-drag transitions, drop/cancel).  We call update() each frame and
    // sync its state back to pane_tab_drag_ for rendering compatibility.

    if (tab_drag_controller_ && tab_drag_controller_->is_active())
    {
        // Compute screen-space cursor position via GLFW (not ImGui).
        // ImGui::GetMousePos() returns garbage when the cursor leaves
        // the GLFW window, causing int overflow → INT_MIN coordinates.
        // GLFW's glfwGetCursorPos works correctly even outside the window.
        float screen_mx = 0.0f;
        float screen_my = 0.0f;
        {
            double sx = 0.0;
            double sy = 0.0;
            if (tab_drag_controller_->get_screen_cursor(sx, sy))
            {
                screen_mx = static_cast<float>(sx);
                screen_my = static_cast<float>(sy);
            }
            else
            {
                ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                screen_mx   = wpos.x + mouse.x;
                screen_my   = wpos.y + mouse.y;
            }
        }

        // Check mouse button across ALL GLFW windows.  On X11, creating
        // a new GLFW window (the preview) during an active drag can break
        // the implicit pointer grab on the source window, causing
        // glfwGetMouseButton on the source to return RELEASE even though
        // the user is still holding the button.  The grab may transfer
        // to the newly created preview window.
        bool mouse_held = tab_drag_controller_->check_mouse_held();
        tab_drag_controller_->update(mouse.x, mouse.y, mouse_held, screen_mx, screen_my);

        // Sync controller state → legacy pane_tab_drag_ for rendering
        if (tab_drag_controller_->is_dragging())
        {
            pane_tab_drag_.dragging      = true;
            pane_tab_drag_.cross_pane    = tab_drag_controller_->is_cross_pane();
            pane_tab_drag_.dock_dragging = tab_drag_controller_->is_dock_dragging();
        }

        // If controller returned to Idle, the drop/cancel already executed
        // via callbacks — reset legacy state.
        if (!tab_drag_controller_->is_active())
        {
            pane_tab_drag_.dragging             = false;
            pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
            pane_tab_drag_.cross_pane           = false;
            pane_tab_drag_.dock_dragging        = false;
        }
    }
    else if (pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
             && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Fallback: no controller — use legacy inline logic
        float dx   = mouse.x - pane_tab_drag_.drag_start_x;
        float dy   = mouse.y - pane_tab_drag_.drag_start_y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (!pane_tab_drag_.dragging && dist > DRAG_THRESHOLD)
        {
            pane_tab_drag_.dragging = true;
        }

        if (pane_tab_drag_.dragging)
        {
            constexpr float DOCK_DRAG_THRESHOLD = 30.0f;
            if (!pane_tab_drag_.dock_dragging && std::abs(dy) > DOCK_DRAG_THRESHOLD)
            {
                bool over_any_header = false;
                for (auto& ph : headers)
                {
                    Rect hr = ph.header_rect;
                    if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                        && mouse.y < hr.y + hr.h + 10)
                    {
                        over_any_header = true;
                        break;
                    }
                }
                if (!over_any_header)
                {
                    pane_tab_drag_.dock_dragging = true;
                    dock_system_->begin_drag(pane_tab_drag_.dragged_figure_index, mouse.x, mouse.y);
                }
            }

            if (pane_tab_drag_.dock_dragging)
            {
                dock_system_->update_drag(mouse.x, mouse.y);
            }
        }
    }

    // Cross-pane detection (shared by both controller and legacy paths)
    if (pane_tab_drag_.dragging)
    {
        bool over_source = false;
        for (auto& ph : headers)
        {
            Rect hr = ph.header_rect;
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y
                && mouse.y < hr.y + hr.h)
            {
                if (ph.pane->id() == pane_tab_drag_.source_pane_id)
                {
                    over_source = true;
                }
                else
                {
                    pane_tab_drag_.cross_pane = true;
                }
                break;
            }
        }
        if (!over_source && !pane_tab_drag_.dock_dragging)
        {
            pane_tab_drag_.cross_pane = true;
        }
        if (tab_drag_controller_)
        {
            tab_drag_controller_->set_cross_pane(pane_tab_drag_.cross_pane);
        }

        // ── Ghost tab / preview sync ──────────────────────────────────
        auto* draw_list   = ImGui::GetForegroundDrawList();   // Ghost/drop overlays always on top
        std::string title = fig_title(pane_tab_drag_.dragged_figure_index);

        // Sync preview_active from TabDragController's preview state
        // (the real preview window is managed by TabDragController + WindowManager)
        if (tab_drag_controller_ && tab_drag_controller_->is_preview_active())
            pane_tab_drag_.preview_active = true;

        if (!pane_tab_drag_.preview_active)
        {
            // Preview window not yet created — draw small ghost tab at cursor
            ImVec2 text_sz = ImGui::CalcTextSize(title.c_str());
            float  ghost_w = std::clamp(text_sz.x + TAB_PAD * 2 + CLOSE_SZ, TAB_MIN_W, TAB_MAX_W);
            float  ghost_h = TAB_H;
            float  ghost_x = mouse.x - ghost_w * 0.5f;
            float  ghost_y = mouse.y - ghost_h * 0.5f;
            draw_list->AddRectFilled(ImVec2(ghost_x + 2, ghost_y + 2),
                                     ImVec2(ghost_x + ghost_w + 2, ghost_y + ghost_h + 2),
                                     IM_COL32(0, 0, 0, 40),
                                     6.0f);
            draw_list->AddRectFilled(ImVec2(ghost_x, ghost_y),
                                     ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                                     to_col(theme.bg_elevated),
                                     6.0f);
            draw_list->AddRect(ImVec2(ghost_x, ghost_y),
                               ImVec2(ghost_x + ghost_w, ghost_y + ghost_h),
                               to_col(theme.accent, 0.6f),
                               6.0f,
                               0,
                               1.5f);
            ImVec2 gtext_pos(ghost_x + TAB_PAD, ghost_y + (ghost_h - text_sz.y) * 0.5f);
            draw_list->AddText(gtext_pos, to_col(theme.text_primary), title.c_str());
        }
        // else: preview is rendered in a separate OS window by WindowManager

        // Draw drop indicator on target pane header
        for (auto& ph : headers)
        {
            if (ph.pane->id() == pane_tab_drag_.source_pane_id && ph.pane->figure_count() <= 1)
                continue;

            Rect hr = ph.header_rect;
            if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                && mouse.y < hr.y + hr.h + 10)
            {
                // Highlight target header
                draw_list->AddRectFilled(ImVec2(hr.x, hr.y),
                                         ImVec2(hr.x + hr.w, hr.y + hr.h),
                                         to_col(theme.accent, 0.08f));

                // Draw insertion line
                float insert_x = hr.x + 4.0f;
                for (auto& tr : ph.tabs)
                {
                    if (mouse.x > tr.x + tr.w * 0.5f)
                    {
                        insert_x = tr.x + tr.w + 1.0f;
                    }
                }
                draw_list->AddLine(ImVec2(insert_x, hr.y + 4),
                                   ImVec2(insert_x, hr.y + hr.h - 4),
                                   to_col(theme.accent),
                                   2.0f);
            }
        }
    }

    // ── Phase 4: Drag end (drop) ─────────────────────────────────────────
    // When TabDragController is active, drop/cancel is handled by its
    // update() call above (callbacks fire on state transitions).
    // The legacy fallback handles the case when no controller is set.

    if (!tab_drag_controller_ && pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (pane_tab_drag_.dragging && pane_tab_drag_.dock_dragging)
        {
            // Check if mouse is outside the window → detach to new window
            ImVec2 display_size = ImGui::GetIO().DisplaySize;
            bool   outside      = (mouse.x < 0 || mouse.y < 0 || mouse.x >= display_size.x
                            || mouse.y >= display_size.y);

            if (outside && pane_tab_detach_cb_)
            {
                dock_system_->cancel_drag();
                ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                pane_tab_detach_cb_(pane_tab_drag_.dragged_figure_index,
                                    wpos.x + mouse.x,
                                    wpos.y + mouse.y);
            }
            else
            {
                // Dock-drag mode: let dock system handle the split
                dock_system_->end_drag(mouse.x, mouse.y);
            }
        }
        else if (pane_tab_drag_.dragging && pane_tab_drag_.cross_pane)
        {
            // Cross-pane tab move: find target pane under mouse
            for (auto& ph : headers)
            {
                Rect hr = ph.header_rect;
                if (mouse.x >= hr.x && mouse.x < hr.x + hr.w && mouse.y >= hr.y - 10
                    && mouse.y < hr.y + hr.h + 10)
                {
                    if (ph.pane->id() != pane_tab_drag_.source_pane_id)
                    {
                        dock_system_->move_figure_to_pane(pane_tab_drag_.dragged_figure_index,
                                                          ph.pane->id());
                    }
                    break;
                }
            }
        }

        // Reset drag state
        pane_tab_drag_.dragging             = false;
        pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
        pane_tab_drag_.cross_pane           = false;
        pane_tab_drag_.dock_dragging        = false;
    }

    // Cancel drag on escape or right-click
    if (pane_tab_drag_.dragged_figure_index != INVALID_FIGURE_ID
        && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        if (tab_drag_controller_ && tab_drag_controller_->is_active())
        {
            tab_drag_controller_->cancel();
        }
        else
        {
            if (pane_tab_drag_.dock_dragging)
            {
                dock_system_->cancel_drag();
            }
        }
        pane_tab_drag_.dragging             = false;
        pane_tab_drag_.dragged_figure_index = INVALID_FIGURE_ID;
        pane_tab_drag_.cross_pane           = false;
        pane_tab_drag_.dock_dragging        = false;
    }

    // ── Phase 5: Right-click context menu ────────────────────────────────
    // OpenPopup/BeginPopup require an active ImGui window context.
    // Create an invisible overlay window for the popup scope.
    {
        ImGuiIO& popup_io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(popup_io.DisplaySize.x, popup_io.DisplaySize.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGuiWindowFlags popup_host_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
        ImGui::Begin("##pane_tab_popup_host", nullptr, popup_host_flags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        // Open the popup if right-click was detected in Phase 2
        if (pane_ctx_menu_open_ && pane_ctx_menu_fig_ != INVALID_FIGURE_ID)
        {
            ImGui::OpenPopup("##pane_tab_ctx");
            pane_ctx_menu_open_ = false;   // Only open once
        }

        // Programmatic close request — handled inside BeginPopup below

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_1));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ui::tokens::SPACE_2, ui::tokens::SPACE_1));
        ImGui::PushStyleColor(
            ImGuiCol_PopupBg,
            ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.98f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(theme.border_default.r, theme.border_default.g, theme.border_default.b, 0.5f));

        if (ImGui::BeginPopup("##pane_tab_ctx"))
        {
            // Programmatic close (QA agent)
            if (pane_ctx_menu_close_requested_)
            {
                pane_ctx_menu_close_requested_ = false;
                ImGui::CloseCurrentPopup();
            }
            else if (pane_ctx_menu_fig_ != INVALID_FIGURE_ID)
            {
                auto menu_item = [&](const char* label) -> bool
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                          ImVec4(theme.accent_subtle.r,
                                                 theme.accent_subtle.g,
                                                 theme.accent_subtle.b,
                                                 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                          ImVec4(theme.accent_muted.r,
                                                 theme.accent_muted.g,
                                                 theme.accent_muted.b,
                                                 0.7f));
                    float item_h  = ImGui::GetTextLineHeight() + 8.0f;
                    bool  clicked = ImGui::Selectable(label,
                                                     false,
                                                     ImGuiSelectableFlags_None,
                                                     ImVec2(0, item_h));
                    ImGui::PopStyleColor(3);
                    return clicked;
                };

                if (menu_item("Rename..."))
                {
                    pane_tab_renaming_   = true;
                    pane_tab_rename_fig_ = pane_ctx_menu_fig_;
                    std::string title    = fig_title(pane_ctx_menu_fig_);
                    strncpy(pane_tab_rename_buf_, title.c_str(), sizeof(pane_tab_rename_buf_) - 1);
                    pane_tab_rename_buf_[sizeof(pane_tab_rename_buf_) - 1] = '\0';
                }

                if (command_registry_ && menu_item("New Tab"))
                {
                    command_registry_->execute("figure.new");
                }

                if (menu_item("Duplicate"))
                {
                    if (pane_tab_duplicate_cb_)
                        pane_tab_duplicate_cb_(pane_ctx_menu_fig_);
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(theme.border_subtle.r,
                                             theme.border_subtle.g,
                                             theme.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));

                if (menu_item("Split Right"))
                {
                    if (pane_tab_split_right_cb_)
                        pane_tab_split_right_cb_(pane_ctx_menu_fig_);
                }

                if (menu_item("Split Down"))
                {
                    if (pane_tab_split_down_cb_)
                        pane_tab_split_down_cb_(pane_ctx_menu_fig_);
                }

                if (menu_item("Detach to Window"))
                {
                    if (pane_tab_detach_cb_)
                    {
                        ImVec2 m    = ImGui::GetMousePos();
                        ImVec2 wpos = ImGui::GetMainViewport()->Pos;
                        pane_tab_detach_cb_(pane_ctx_menu_fig_, wpos.x + m.x, wpos.y + m.y);
                    }
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Separator,
                                      ImVec4(theme.border_subtle.r,
                                             theme.border_subtle.g,
                                             theme.border_subtle.b,
                                             0.3f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));

                if (menu_item("Close"))
                {
                    if (pane_tab_close_cb_)
                        pane_tab_close_cb_(pane_ctx_menu_fig_);
                }

                // Paste Series (from clipboard into first axes of this figure)
                if (series_clipboard_ && series_clipboard_->has_data())
                {
                    ImGui::Dummy(ImVec2(0, 2));
                    ImGui::PushStyleColor(ImGuiCol_Separator,
                                          ImVec4(theme.border_subtle.r,
                                                 theme.border_subtle.g,
                                                 theme.border_subtle.b,
                                                 0.3f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0, 2));

                    if (menu_item("Paste Series"))
                    {
                        // Find the figure and paste into its first axes (2D or 3D)
                        Figure* paste_fig = nullptr;
                        if (get_figure_ptr_)
                            paste_fig = get_figure_ptr_(pane_ctx_menu_fig_);
                        if (paste_fig)
                        {
                            // Prefer all_axes (unified 2D+3D), fall back to 2D-only
                            if (!paste_fig->all_axes().empty())
                                series_clipboard_->paste(*paste_fig->all_axes_mut()[0]);
                            else if (!paste_fig->axes().empty())
                                series_clipboard_->paste(*paste_fig->axes_mut()[0]);
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }
        else
        {
            pane_ctx_menu_open_ = false;
            pane_ctx_menu_fig_  = INVALID_FIGURE_ID;
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        // ── Rename popup ─────────────────────────────────────────────────────

        if (pane_tab_renaming_ && pane_tab_rename_fig_ != INVALID_FIGURE_ID)
        {
            ImGui::OpenPopup("##pane_tab_rename");
            pane_tab_renaming_ = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_3));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(
            ImGuiCol_PopupBg,
            ImVec4(theme.bg_elevated.r, theme.bg_elevated.g, theme.bg_elevated.b, 0.98f));

        if (ImGui::BeginPopup("##pane_tab_rename"))
        {
            ImGui::TextUnformatted("Rename tab");
            ImGui::Spacing();
            bool enter = ImGui::InputText("##pane_rename_input",
                                          pane_tab_rename_buf_,
                                          sizeof(pane_tab_rename_buf_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere(-1);
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ui::tokens::SPACE_4, ui::tokens::SPACE_2));
            if (enter || ImGui::Button("OK"))
            {
                std::string new_title(pane_tab_rename_buf_);
                if (!new_title.empty() && pane_tab_rename_fig_ != INVALID_FIGURE_ID)
                {
                    if (pane_tab_rename_cb_)
                        pane_tab_rename_cb_(pane_tab_rename_fig_, new_title);
                }
                pane_tab_rename_fig_ = INVALID_FIGURE_ID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pane_tab_rename_fig_ = INVALID_FIGURE_ID;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        ImGui::End();   // ##pane_tab_popup_host
    }   // Phase 5 scope
}

void ImGuiIntegration::draw_plot_overlays(Figure& figure)
{
    if (!layout_manager_)
        return;

    ImDrawList* dl     = ImGui::GetBackgroundDrawList();
    const auto& colors = theme_mgr_->colors();

    // ── Subplot separation: draw subtle divider lines between subplot cells ──
    int rows = figure.grid_rows();
    int cols = figure.grid_cols();
    if (rows > 1 || cols > 1)
    {
        Rect  canvas = layout_manager_->canvas_rect();
        float cell_w = canvas.w / static_cast<float>(cols);
        float cell_h = canvas.h / static_cast<float>(rows);

        ImU32 sep_col       = IM_COL32(static_cast<uint8_t>(colors.border_subtle.r * 255),
                                 static_cast<uint8_t>(colors.border_subtle.g * 255),
                                 static_cast<uint8_t>(colors.border_subtle.b * 255),
                                 50);
        float sep_thickness = 1.0f;
        float inset         = 12.0f;

        // Vertical dividers between columns
        for (int c = 1; c < cols; ++c)
        {
            float x = canvas.x + static_cast<float>(c) * cell_w;
            dl->AddLine(ImVec2(x, canvas.y + inset),
                        ImVec2(x, canvas.y + canvas.h - inset),
                        sep_col,
                        sep_thickness);
        }
        // Horizontal dividers between rows
        for (int r = 1; r < rows; ++r)
        {
            float y = canvas.y + static_cast<float>(r) * cell_h;
            dl->AddLine(ImVec2(canvas.x + inset, y),
                        ImVec2(canvas.x + canvas.w - inset, y),
                        sep_col,
                        sep_thickness);
        }
    }

    // ── Live buffer reset chip (top-right) ───────────────────────────────
    bool has_live_axes     = false;
    bool has_live_viewport = false;
    bool live_following    = false;
    Rect live_viewport{};
    for (const auto& ax_ptr : figure.axes())
    {
        if (ax_ptr && ax_ptr->has_presented_buffer())
        {
            has_live_axes     = true;
            live_viewport     = ax_ptr->viewport();
            has_live_viewport = (live_viewport.w > 0.0f && live_viewport.h > 0.0f);
            live_following    = ax_ptr->is_presented_buffer_following();
            break;
        }
    }

    if (has_live_axes)
    {
        Rect   canvas  = layout_manager_->canvas_rect();
        Rect   anchor  = has_live_viewport ? live_viewport : canvas;
        ImVec2 chip_sz = ImVec2(36.0f, 24.0f);

        // Keep the control in top-right, above legend's default top-right slot.
        float chip_x = anchor.x + anchor.w - chip_sz.x - 12.0f;
        float chip_y = anchor.y - chip_sz.y - 8.0f;
        if (chip_y < 4.0f)
            chip_y = 4.0f;
        ImVec2 chip_pos = ImVec2(chip_x, chip_y);

        const std::string overlay_id =
            std::format("##live_overlay_{}", static_cast<void*>(&figure));

        ImGui::SetNextWindowPos(chip_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(chip_sz, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                                 | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
                                 | ImGuiWindowFlags_NoScrollWithMouse
                                 | ImGuiWindowFlags_NoScrollbar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin(overlay_id.c_str(), nullptr, flags))
        {
            ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, chip_sz.y * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  ImVec4(colors.border_subtle.r,
                                         colors.border_subtle.g,
                                         colors.border_subtle.b,
                                         0.85f));
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(colors.accent_muted.r, colors.accent_muted.g, colors.accent_muted.b, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(colors.text_inverse.r, colors.text_inverse.g, colors.text_inverse.b, 1.0f));

            if (ImGui::Button("##live_follow_toggle", chip_sz))
            {
                if (live_following)
                {
                    for (auto& ax : figure.axes_mut())
                    {
                        if (ax && ax->has_presented_buffer())
                        {
                            AxisLimits xlim = ax->x_limits();
                            ax->xlim(xlim.min, xlim.max);
                        }
                    }
                }
                else
                {
                    reset_live_view_ = true;
                }
            }

            // Draw crisp transport glyphs for a cleaner, more professional control.
            ImDrawList* win_dl   = ImGui::GetWindowDrawList();
            ImVec2      rmin     = ImGui::GetItemRectMin();
            ImVec2      rmax     = ImGui::GetItemRectMax();
            ImVec2      center   = ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
            ImU32       icon_col = IM_COL32(static_cast<int>(colors.text_inverse.r * 255.0f),
                                      static_cast<int>(colors.text_inverse.g * 255.0f),
                                      static_cast<int>(colors.text_inverse.b * 255.0f),
                                      255);

            if (live_following)
            {
                float bar_h    = 9.0f;
                float bar_w    = 2.6f;
                float gap      = 3.0f;
                float y0       = center.y - bar_h * 0.5f;
                float y1       = center.y + bar_h * 0.5f;
                float left_x0  = center.x - gap * 0.5f - bar_w;
                float left_x1  = center.x - gap * 0.5f;
                float right_x0 = center.x + gap * 0.5f;
                float right_x1 = center.x + gap * 0.5f + bar_w;
                win_dl->AddRectFilled(ImVec2(left_x0, y0), ImVec2(left_x1, y1), icon_col, 1.0f);
                win_dl->AddRectFilled(ImVec2(right_x0, y0), ImVec2(right_x1, y1), icon_col, 1.0f);
            }
            else
            {
                float w = 8.5f;
                float h = 10.0f;
                win_dl->AddTriangleFilled(ImVec2(center.x - w * 0.45f, center.y - h * 0.5f),
                                          ImVec2(center.x - w * 0.45f, center.y + h * 0.5f),
                                          ImVec2(center.x + w * 0.55f, center.y),
                                          icon_col);
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(live_following ? "Pause live-follow" : "Resume live-follow");
                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // Selection highlight is now rendered by the GPU pipeline
    // (Renderer::render_selection_highlight)

    // ── Plugin overlay callbacks ─────────────────────────────────────────────
    // Invoke registered overlays for each axes viewport so plugins can draw
    // custom annotations (crosshairs, rulers, indicators, etc.) on top of the
    // plot canvas.  The draw_list in OverlayDrawContext maps to the background
    // ImDrawList, which sits above the Vulkan plot surface but below ImGui
    // popups and menus.
    if (overlay_registry_ && overlay_registry_->count() > 0)
    {
        ImDrawList* overlay_dl = ImGui::GetBackgroundDrawList();
        ImGuiIO&    io         = ImGui::GetIO();

        const auto& all_axes = figure.all_axes();
        for (int ai = 0; ai < static_cast<int>(all_axes.size()); ++ai)
        {
            const auto& ax = all_axes[static_cast<size_t>(ai)];
            if (!ax)
                continue;

            Rect vp = ax->viewport();
            if (vp.w <= 0.0f || vp.h <= 0.0f)
                continue;

            bool is_hovered = (io.MousePos.x >= vp.x && io.MousePos.x <= vp.x + vp.w
                               && io.MousePos.y >= vp.y && io.MousePos.y <= vp.y + vp.h);

            OverlayDrawContext ctx{};
            ctx.viewport_x = vp.x;
            ctx.viewport_y = vp.y;
            ctx.viewport_w = vp.w;
            ctx.viewport_h = vp.h;
            ctx.mouse_x    = io.MousePos.x;
            ctx.mouse_y    = io.MousePos.y;
            ctx.is_hovered = is_hovered;
            ctx.figure_id =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&figure) & 0xFFFFFFFF);
            ctx.axes_index   = ai;
            ctx.series_count = static_cast<int>(ax->series().size());
            ctx.draw_list    = static_cast<void*>(overlay_dl);

            overlay_registry_->draw_all(ctx);
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
