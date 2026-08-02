#ifdef SPECTRA_USE_IMGUI

    #include "inspector.hpp"

    #include <algorithm>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <limits>
    #include <numeric>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <vector>

    #include "ui/commands/series_clipboard.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/viewmodel/axes_view_model.hpp"
    #include "ui/viewmodel/series_view_model.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/workspace/plugin_api.hpp"

namespace spectra::ui
{

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void Inspector::set_context(const SelectionContext& ctx)
{
    ctx_ = ctx;
}

void Inspector::set_fonts(ImFont* body, ImFont* heading, ImFont* title)
{
    font_body_    = body;
    font_heading_ = heading;
    font_title_   = title;
}

// ─── Main Draw ──────────────────────────────────────────────────────────────

void Inspector::draw(Figure& figure)
{
    // Context header
    // const auto& c = theme();  // Currently unused

    switch (ctx_.type)
    {
        case SelectionType::None:
        case SelectionType::Figure:
            // Default: show figure properties only
            draw_figure_properties(figure);
            break;

        case SelectionType::SeriesBrowser:
            // Show only series browser (no figure properties)
            draw_series_browser(figure);
            break;

        case SelectionType::Axes:
            if (ctx_.axes)
            {
                draw_axes_properties(*ctx_.axes, ctx_.axes_index);
            }
            break;

        case SelectionType::Series:
            // Always show the series browser so user can Shift+click to multi-select
            draw_series_browser(figure);
            if (ctx_.series)
            {
                widgets::section_spacing();
                widgets::separator();
                widgets::section_spacing();
                draw_series_properties(*ctx_.series, ctx_.series_index);
            }
            break;
    }
}

// ─── Figure Properties ──────────────────────────────────────────────────────

void Inspector::draw_figure_properties(Figure& fig)
{
    const auto& c = theme();

    // Context title
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted("Figure");
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Subtitle with info
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    int total_series = 0;
    for (const auto& ax : fig.axes())
    {
        if (ax)
            total_series += static_cast<int>(ax->series().size());
    }
    const std::string subtitle =
        std::format("{} axes, {} series", static_cast<int>(fig.axes().size()), total_series);
    ImGui::TextUnformatted(subtitle.c_str());
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── Background section
    auto& sty = fig.style();
    if (widgets::section_header("BACKGROUND", &sec_appearance_, font_heading_))
    {
        if (widgets::begin_animated_section("BACKGROUND"))
        {
            widgets::begin_group("bg");
            widgets::color_field("Background Color", sty.background);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Margins section
    if (widgets::section_header("MARGINS", &sec_margins_, font_heading_))
    {
        if (widgets::begin_animated_section("MARGINS"))
        {
            widgets::begin_group("margins");
            widgets::drag_field("Top", sty.margin_top, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Bottom", sty.margin_bottom, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Left", sty.margin_left, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Right", sty.margin_right, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::section_spacing();
            widgets::drag_field("H Gap", sty.subplot_hgap, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("V Gap", sty.subplot_vgap, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Min Row H",
                                sty.min_subplot_height,
                                1.0f,
                                0.0f,
                                1000.0f,
                                "%.0f px");
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Legend section
    auto& leg = fig.legend();
    if (widgets::section_header("LEGEND", &sec_legend_, font_heading_))
    {
        if (widgets::begin_animated_section("LEGEND"))
        {
            widgets::begin_group("legend");
            bool show_legend = leg.visible;
            if (widgets::checkbox_field("Show Legend", show_legend))
            {
                leg.visible = show_legend;
            }

            const char* positions[] = {"Top Right",
                                       "Top Left",
                                       "Bottom Right",
                                       "Bottom Left",
                                       "Hidden"};
            int         pos         = static_cast<int>(leg.position);
            if (widgets::combo_field("Position", pos, positions, 5))
            {
                leg.position = static_cast<LegendPosition>(pos);
            }

            float font_size = leg.font_size;
            if (widgets::drag_field("Font Size", font_size, 0.5f, 6.0f, 32.0f, "%.0f px"))
            {
                leg.font_size = font_size;
            }

            float padding = leg.padding;
            if (widgets::drag_field("Padding", padding, 0.5f, 0.0f, 40.0f, "%.0f px"))
            {
                leg.padding = padding;
            }

            spectra::Color bg_color = leg.bg_color;
            if (widgets::color_field("Background", bg_color))
            {
                leg.bg_color = bg_color;
            }

            spectra::Color border_color = leg.border_color;
            if (widgets::color_field("Border", border_color))
            {
                leg.border_color = border_color;
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Quick Actions
    if (widgets::section_header("QUICK ACTIONS", &sec_quick_, font_heading_))
    {
        if (widgets::begin_animated_section("QUICK ACTIONS"))
        {
            widgets::begin_group("quick");
            if (widgets::button_field("Reset to Defaults"))
            {
                sty = FigureStyle{};
                leg = LegendConfig{};
            }
            widgets::end_group();
            widgets::end_animated_section();
        }
    }
}

// ─── Series Browser ─────────────────────────────────────────────────────────

void Inspector::draw_series_browser(Figure& fig)
{
    const auto& c = theme();

    // ── Header strip: Surface-2 background, uppercase tracking, hairline divider ──
    {
        ImVec2      header_min = ImGui::GetCursorScreenPos();
        float       avail_w    = ImGui::GetContentRegionAvail().x;
        float       header_h   = tokens::INSPECTOR_HEADER_H;
        ImVec2      header_max = ImVec2(header_min.x + avail_w, header_min.y + header_h);
        ImDrawList* dl         = ImGui::GetWindowDrawList();

        // Surface-2 background
        dl->AddRectFilled(header_min,
                          header_max,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.5f)),
                          0.0f);

        // Bottom hairline
        dl->AddLine(ImVec2(header_min.x, header_max.y),
                    ImVec2(header_max.x, header_max.y),
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.4f)),
                    1.0f);

        // Uppercase label with letter-spacing feel
        ImGui::SetCursorScreenPos(
            ImVec2(header_min.x + tokens::ROW_PADDING_H,
                   header_min.y + (header_h - ImGui::GetTextLineHeight()) * 0.5f));
        if (font_heading_)
            ImGui::PushFont(font_heading_);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.8f));
        ImGui::TextUnformatted("SERIES");
        ImGui::PopStyleColor();
        if (font_heading_)
            ImGui::PopFont();

        // Advance cursor past header
        ImGui::SetCursorScreenPos(ImVec2(header_min.x, header_max.y + tokens::ROW_PADDING_V));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // Paste button (shown when clipboard has data)
    if (clipboard_ && clipboard_->has_data())
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

        ImFont* icf = icon_font(tokens::ICON_SM);
        if (icf)
            ImGui::PushFont(icf);
        size_t      clip_n = clipboard_->count();
        std::string paste_lbl =
            std::string(icon_str(Icon::Duplicate))
            + (clip_n > 1 ? "  Paste " + std::to_string(clip_n) + " Series" : "  Paste");
        if (ImGui::Button(paste_lbl.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 24)))
        {
            // Paste into selected axes if available, else first axes
            AxesBase* target = nullptr;
            if (ctx_.type == SelectionType::Series || ctx_.type == SelectionType::Axes)
                target = ctx_.axes_base ? ctx_.axes_base : static_cast<AxesBase*>(ctx_.axes);
            if (!target)
            {
                if (!fig.all_axes().empty())
                {
                    for (auto& ab : fig.all_axes_mut())
                        if (ab)
                        {
                            target = ab.get();
                            break;
                        }
                }
                if (!target)
                {
                    for (auto& ax : fig.axes_mut())
                        if (ax)
                        {
                            target = ax.get();
                            break;
                        }
                }
            }
            if (target)
                clipboard_->paste_all(*target);
        }
        if (icf)
            ImGui::PopFont();
        ImGui::PopStyleColor(3);
        widgets::small_spacing();
    }

    // ── Multi-select bulk action bar ──
    // Shown when 2+ series are selected; replaces per-row buttons for selected rows.
    bool multi_sel = ctx_.has_multi_selection();
    if (multi_sel && clipboard_)
    {
        size_t          n       = ctx_.selected_count();
        const auto&     c2      = c;
        ImDrawList*     dl      = ImGui::GetWindowDrawList();
        float           avail   = ImGui::GetContentRegionAvail().x;
        constexpr float bar_h   = 28.0f;
        constexpr float pad_h   = 6.0f;
        constexpr float gap     = 4.0f;
        float           btn_w   = (avail - pad_h * 2 - gap * 2) / 3.0f;
        ImVec2          bar_min = ImGui::GetCursorScreenPos();
        ImVec2          bar_max = ImVec2(bar_min.x + avail, bar_min.y + bar_h);

        // Subtle surface background
        dl->AddRectFilled(bar_min,
                          bar_max,
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(c2.bg_tertiary.r, c2.bg_tertiary.g, c2.bg_tertiary.b, 0.6f)),
                          tokens::RADIUS_MD);

        ImFont* icf     = icon_font(tokens::ICON_SM);
        ImFont* fnt     = icf ? icf : ImGui::GetFont();
        float   gsz     = tokens::ICON_SM;
        ImU32   hov_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(c2.accent_subtle.r, c2.accent_subtle.g, c2.accent_subtle.b, 0.5f));

        // Helper: draw a labeled icon button inside the bar
        auto bulk_btn = [&](const char* id,
                            const char* glyph,
                            const char* label_suffix,
                            ImVec2      pos,
                            ImVec4      txt_col) -> bool
        {
            ImVec2 bmax(pos.x + btn_w, pos.y + bar_h);
            ImGui::SetCursorScreenPos(pos);
            bool clicked = ImGui::InvisibleButton(id, ImVec2(btn_w, bar_h));
            bool hovered = ImGui::IsItemHovered();
            if (hovered)
                dl->AddRectFilled(pos, bmax, hov_col, tokens::RADIUS_SM);

            // Icon
            ImVec2 tsz     = fnt->CalcTextSizeA(gsz, FLT_MAX, 0.0f, glyph);
            float  cy2     = pos.y + bar_h * 0.5f;
            float  total_w = tsz.x + 3.0f + ImGui::CalcTextSize(label_suffix).x;
            float  tx      = pos.x + (btn_w - total_w) * 0.5f;
            ImU32  col     = ImGui::ColorConvertFloat4ToU32(
                hovered ? ImVec4(txt_col.x, txt_col.y, txt_col.z, 1.0f) : txt_col);
            dl->AddText(fnt, gsz, ImVec2(tx, cy2 - gsz * 0.5f + 1.0f), col, glyph);

            // Text label
            ImGui::SetCursorScreenPos(
                ImVec2(tx + tsz.x + 3.0f, cy2 - ImGui::GetTextLineHeight() * 0.5f));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(txt_col.x, txt_col.y, txt_col.z, hovered ? 1.0f : txt_col.w));
            ImGui::TextUnformatted(label_suffix);
            ImGui::PopStyleColor();

            return clicked;
        };

        const std::string copy_lbl = std::format("Copy {}", n);
        const std::string cut_lbl  = std::format("Cut {}", n);
        const std::string del_lbl  = std::format("Delete {}", n);

        ImVec4 muted(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 0.75f);
        ImVec4 red(0.85f, 0.35f, 0.35f, 0.85f);

        float bx = bar_min.x + pad_h;

        // Copy all selected
        if (bulk_btn("##bulk_cp",
                     icon_str(Icon::Copy),
                     copy_lbl.c_str(),
                     ImVec2(bx, bar_min.y),
                     muted))
        {
            std::vector<const Series*> to_copy;
            for (const auto& e : ctx_.selected_series)
                if (e.series)
                    to_copy.push_back(e.series);
            clipboard_->copy_multi(to_copy);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy all selected series");

        // Cut all selected
        struct PendingCutEntry
        {
            AxesBase* axes;
            Series*   series;
        };
        std::vector<PendingCutEntry> pending_cuts;
        if (bulk_btn("##bulk_ct",
                     icon_str(Icon::Scissors),
                     cut_lbl.c_str(),
                     ImVec2(bx + btn_w + gap, bar_min.y),
                     muted))
        {
            std::vector<const Series*> to_cut;
            for (const auto& e : ctx_.selected_series)
                if (e.series && e.axes_base)
                {
                    to_cut.push_back(e.series);
                    pending_cuts.push_back({e.axes_base, e.series});
                }
            clipboard_->cut_multi(to_cut);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Cut all selected series");

        // Delete all selected
        std::vector<PendingCutEntry> pending_deletes;
        if (bulk_btn("##bulk_dl",
                     icon_str(Icon::Trash),
                     del_lbl.c_str(),
                     ImVec2(bx + (btn_w + gap) * 2, bar_min.y),
                     red))
        {
            for (const auto& e : ctx_.selected_series)
                if (e.series && e.axes_base)
                    pending_deletes.push_back({e.axes_base, e.series});
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete all selected series");

        // Advance cursor past the bar
        ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_max.y));
        widgets::small_spacing();

        // Apply deferred cuts/deletes (safe: done before series iteration)
        for (auto& pc : pending_cuts)
        {
            if (defer_removal_)
                defer_removal_(pc.axes, pc.series);
            else
            {
                auto& sv = pc.axes->series_mut();
                for (size_t i = 0; i < sv.size(); ++i)
                    if (sv[i].get() == pc.series)
                    {
                        pc.axes->remove_series(i);
                        break;
                    }
            }
        }
        for (auto& pd : pending_deletes)
        {
            if (defer_removal_)
                defer_removal_(pd.axes, pd.series);
            else
            {
                auto& sv = pd.axes->series_mut();
                for (size_t i = 0; i < sv.size(); ++i)
                    if (sv[i].get() == pd.series)
                    {
                        pd.axes->remove_series(i);
                        break;
                    }
            }
        }
        if (!pending_cuts.empty() || !pending_deletes.empty())
            ctx_.clear();
    }

    // Helper lambda to draw series rows for any axes (2D or 3D)
    auto draw_axes_series = [&](AxesBase* ax_base, int ax_idx)
    {
        int s_idx = 0;
        for (auto& s : ax_base->series_mut())
        {
            if (!s)
            {
                s_idx++;
                continue;
            }
            ImGui::PushID(ax_idx * 1000 + s_idx);

            const char* name    = s->label().empty() ? "Unnamed" : s->label().c_str();
            bool        unnamed = s->label().empty();
            float       row_h   = tokens::SERIES_ROW_HEIGHT;
            ImVec2      row_min = ImGui::GetCursorScreenPos();
            float       avail_w = ImGui::GetContentRegionAvail().x;
            ImVec2      row_max = ImVec2(row_min.x + avail_w, row_min.y + row_h);

            // Subtle hover background (Surface+1)
            bool row_hovered = ImGui::IsMouseHoveringRect(row_min, row_max);
            if (row_hovered)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    row_min,
                    row_max,
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.4f)),
                    tokens::RADIUS_MD);
            }

            // ── Absolute layout anchors ──
            // All elements positioned from row_min using fixed X offsets.
            // Row:  [8px pad | 12px dot | 8px | 24px eye | 10px | name .... | 24+2+24+2+24 btns |
            // 8px]
            constexpr float pad_l     = 8.0f;
            constexpr float dot_sz    = 12.0f;
            constexpr float gap_1     = 8.0f;   // dot → eye
            constexpr float eye_w     = 24.0f;
            constexpr float gap_2     = 10.0f;   // eye → name
            constexpr float btn_w     = 24.0f;
            constexpr float btn_h     = 24.0f;
            constexpr float btn_gap   = 4.0f;
            constexpr float pad_r     = 8.0f;
            constexpr float cluster_w = btn_w * 3 + btn_gap * 2;

            float x_dot  = row_min.x + pad_l;
            float x_eye  = x_dot + dot_sz + gap_1;
            float x_name = x_eye + eye_w + gap_2;
            float x_btns = row_max.x - cluster_w - pad_r;
            float name_w = x_btns - x_name - 4.0f;
            float cy     = row_min.y + row_h * 0.5f;   // vertical center

            bool is_selected = (ctx_.type == SelectionType::Series && ctx_.is_selected(s.get()));

            // ── Selection border (rounded, not filled) ──
            if (is_selected)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRect(ImVec2(row_min.x + 2.0f, row_min.y + 2.0f),
                            ImVec2(row_max.x - 2.0f, row_max.y - 2.0f),
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.7f)),
                            tokens::RADIUS_MD,
                            0,
                            1.5f);
            }

            // ── Color dot (drawn directly, no Dummy) ──
            {
                const auto& sc    = s->color();
                ImU32       col   = ImGui::ColorConvertFloat4ToU32(ImVec4(sc.r, sc.g, sc.b, sc.a));
                float       dot_y = cy - dot_sz * 0.5f;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x_dot, dot_y),
                                                          ImVec2(x_dot + dot_sz, dot_y + dot_sz),
                                                          col,
                                                          tokens::RADIUS_SM);
            }

            // ── Visibility toggle ──
            bool        vis      = s->visible();
            const char* eye_icon = vis ? icon_str(Icon::Eye) : icon_str(Icon::EyeOff);
            ImFont*     icon_f   = icon_font(tokens::ICON_SM);

            // Use InvisibleButton for the hitbox, draw icon manually centered
            ImGui::SetCursorScreenPos(ImVec2(x_eye, cy - eye_w * 0.5f));
            bool eye_clicked = ImGui::InvisibleButton("##eye", ImVec2(eye_w, eye_w));
            bool eye_hovered = ImGui::IsItemHovered();
            if (eye_clicked)
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_series_vm(s.get()).set_visible(!vis);
                else
                    s->visible(!vis);
            }

            // Draw icon text centered within the button rect
            {
                ImDrawList* dl       = ImGui::GetWindowDrawList();
                ImVec4      icon_col = vis ? ImVec4(c.text_secondary.r,
                                               c.text_secondary.g,
                                               c.text_secondary.b,
                                               eye_hovered ? 1.0f : 0.7f)
                                           : ImVec4(c.text_tertiary.r,
                                               c.text_tertiary.g,
                                               c.text_tertiary.b,
                                               eye_hovered ? 0.7f : 0.35f);
                ImFont*     fnt      = icon_f ? icon_f : ImGui::GetFont();
                float       glyph_sz = tokens::ICON_SM;
                ImVec2      tsz      = fnt->CalcTextSizeA(glyph_sz, FLT_MAX, 0.0f, eye_icon);
                ImVec2      tpos(x_eye + (eye_w - tsz.x) * 0.5f, cy - glyph_sz * 0.5f + 1.0f);
                dl->AddText(fnt,
                            glyph_sz,
                            tpos,
                            ImGui::ColorConvertFloat4ToU32(icon_col),
                            eye_icon);
            }

            // ── Series name (InvisibleButton + manual draw) ──
            float text_h = ImGui::GetTextLineHeight();
            ImGui::SetCursorScreenPos(ImVec2(x_name, cy - text_h * 0.5f));
            ImGui::InvisibleButton("##name", ImVec2(name_w, text_h + 4.0f));
            bool name_clicked = ImGui::IsItemClicked(0);

            if (name_clicked)
            {
                if (ImGui::GetIO().KeyShift)
                {
                    ctx_.toggle_series(&fig,
                                       dynamic_cast<Axes*>(ax_base),
                                       ax_base,
                                       ax_idx,
                                       s.get(),
                                       s_idx);
                }
                else if (is_selected)
                {
                    ctx_.clear();
                }
                else
                {
                    ctx_.select_series(&fig, dynamic_cast<Axes*>(ax_base), ax_idx, s.get(), s_idx);
                    ctx_.axes_base = ax_base;
                    if (!ctx_.selected_series.empty())
                        ctx_.selected_series[0].axes_base = ax_base;
                }
            }

            // Draw name text
            {
                ImVec4 text_col =
                    is_selected ? ImVec4(c.accent.r, c.accent.g, c.accent.b, 1.0f)
                    : unnamed
                        ? ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 0.7f)
                        : ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 1.0f);
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
                                                    ImGui::GetFontSize(),
                                                    ImVec2(x_name, cy - text_h * 0.5f),
                                                    ImGui::ColorConvertFloat4ToU32(text_col),
                                                    name);
            }

            // Drag source: start dragging this series row
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                struct DragPayload
                {
                    AxesBase* axes;
                    int       index;
                };
                DragPayload payload{ax_base, s_idx};
                ImGui::SetDragDropPayload("SERIES_REORDER", &payload, sizeof(payload));
                ImGui::TextUnformatted(name);
                ImGui::EndDragDropSource();
            }
            // Drop target: accept a dragged series and schedule reorder
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SERIES_REORDER"))
                {
                    struct DragPayload
                    {
                        AxesBase* axes;
                        int       index;
                    };
                    auto src = *static_cast<const DragPayload*>(pl->Data);
                    if (src.axes == ax_base && src.index != s_idx)
                    {
                        pending_move_.axes = ax_base;
                        pending_move_.from = src.index;
                        pending_move_.to   = s_idx;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // ── Action buttons: Copy / Cut / Delete ──
            // In multi-select mode, selected rows defer to the bulk action bar above.
            // Use InvisibleButton + manual draw so hover rect exactly matches the icon rect.
            if (clipboard_ && !(multi_sel && is_selected))
            {
                float       btn_y   = cy - btn_h * 0.5f;
                ImFont*     icf     = icon_font(tokens::ICON_SM);
                ImFont*     fnt     = icf ? icf : ImGui::GetFont();
                float       gsz     = tokens::ICON_SM;
                ImDrawList* dl      = ImGui::GetWindowDrawList();
                ImU32       hov_col = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.45f));

                // Helper: draw one icon button, returns true if clicked
                auto icon_btn =
                    [&](const char* id, const char* glyph, ImVec2 pos, ImVec4 normal_col) -> bool
                {
                    ImGui::SetCursorScreenPos(pos);
                    bool clicked = ImGui::InvisibleButton(id, ImVec2(btn_w, btn_h));
                    bool hovered = ImGui::IsItemHovered();
                    if (hovered)
                        dl->AddRectFilled(pos,
                                          ImVec2(pos.x + btn_w, pos.y + btn_h),
                                          hov_col,
                                          tokens::RADIUS_SM);
                    ImVec4 col = hovered ? ImVec4(normal_col.x, normal_col.y, normal_col.z, 1.0f)
                                         : normal_col;
                    ImVec2 tsz = fnt->CalcTextSizeA(gsz, FLT_MAX, 0.0f, glyph);
                    ImVec2 tpos(pos.x + (btn_w - tsz.x) * 0.5f,
                                pos.y + (btn_h - gsz) * 0.5f + 1.0f);
                    dl->AddText(fnt, gsz, tpos, ImGui::ColorConvertFloat4ToU32(col), glyph);
                    return clicked;
                };

                ImVec4 muted(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 0.65f);
                ImVec4 red(0.85f, 0.35f, 0.35f, 0.75f);

                // Copy
                const std::string copy_id = std::format("##cp{}_{}", ax_idx, s_idx);
                if (icon_btn(copy_id.c_str(), icon_str(Icon::Copy), ImVec2(x_btns, btn_y), muted))
                    clipboard_->copy(*s);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Copy");

                // Cut (scissors)
                const std::string cut_id = std::format("##ct{}_{}", ax_idx, s_idx);
                if (icon_btn(cut_id.c_str(),
                             icon_str(Icon::Scissors),
                             ImVec2(x_btns + btn_w + btn_gap, btn_y),
                             muted))
                {
                    clipboard_->cut(*s);
                    if (defer_removal_)
                        defer_removal_(ax_base, s.get());
                    else
                        ax_base->remove_series(static_cast<size_t>(s_idx));
                    ctx_.clear();
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cut");

                // Delete
                const std::string del_id = std::format("##dl{}_{}", ax_idx, s_idx);
                if (icon_btn(del_id.c_str(),
                             icon_str(Icon::Trash),
                             ImVec2(x_btns + (btn_w + btn_gap) * 2, btn_y),
                             red))
                {
                    Series* deleted = s.get();
                    if (defer_removal_)
                        defer_removal_(ax_base, deleted);
                    else
                        ax_base->remove_series(static_cast<size_t>(s_idx));
                    if (ctx_.series == deleted)
                        ctx_.clear();
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete");
            }

            // Advance cursor to next row
            ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_max.y));
            ImGui::Dummy(ImVec2(0, 0));

            ImGui::PopID();
            s_idx++;
        }
    };

    // Iterate all axes (2D + 3D) via the unified all_axes list.
    // If the figure has no all_axes entries, fall back to the 2D-only list.
    int ax_idx = 0;
    if (!fig.all_axes().empty())
    {
        for (auto& ax_base : fig.all_axes_mut())
        {
            if (ax_base)
                draw_axes_series(ax_base.get(), ax_idx);
            ax_idx++;
        }
    }
    else
    {
        for (auto& ax : fig.axes_mut())
        {
            if (ax)
                draw_axes_series(ax.get(), ax_idx);
            ax_idx++;
        }
    }

    // Apply deferred series reorder (safe: iteration is complete)
    if (pending_move_.axes && pending_move_.from >= 0 && pending_move_.to >= 0)
    {
        pending_move_.axes->move_series(static_cast<size_t>(pending_move_.from),
                                        static_cast<size_t>(pending_move_.to));
        pending_move_ = {};
    }
}

// ─── Axes Properties ────────────────────────────────────────────────────────

void Inspector::draw_axes_properties(Axes& ax, int index)
{
    const auto& c = theme();

    // Context title
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    const std::string title = std::format("Axes {}", index + 1);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Subtitle
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    const std::string sub = std::format("{} series", ax.series().size());
    ImGui::TextUnformatted(sub.c_str());
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── X Axis
    if (widgets::section_header("X AXIS", &sec_axis_x_, font_heading_))
    {
        if (widgets::begin_animated_section("X AXIS"))
        {
            widgets::begin_group("xaxis");
            auto xlim   = ax.x_limits();
            auto xmin_f = static_cast<float>(xlim.min);
            auto xmax_f = static_cast<float>(xlim.max);
            if (widgets::drag_field2("Range", xmin_f, xmax_f, 0.01f, "%.3f"))
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_axes_vm(&ax).set_visual_xlim(
                        static_cast<double>(xmin_f),
                        static_cast<double>(xmax_f));
                else
                    ax.xlim(static_cast<double>(xmin_f), static_cast<double>(xmax_f));
            }
            std::string xlabel = ax.get_xlabel();
            if (widgets::text_field("Label", xlabel))
            {
                ax.xlabel(xlabel);
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Y Axis
    if (widgets::section_header("Y AXIS", &sec_axis_y_, font_heading_))
    {
        if (widgets::begin_animated_section("Y AXIS"))
        {
            widgets::begin_group("yaxis");
            auto ylim   = ax.y_limits();
            auto ymin_f = static_cast<float>(ylim.min);
            auto ymax_f = static_cast<float>(ylim.max);
            if (widgets::drag_field2("Range", ymin_f, ymax_f, 0.01f, "%.3f"))
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_axes_vm(&ax).set_visual_ylim(
                        static_cast<double>(ymin_f),
                        static_cast<double>(ymax_f));
                else
                    ax.ylim(static_cast<double>(ymin_f), static_cast<double>(ymax_f));
            }
            std::string ylabel = ax.get_ylabel();
            if (widgets::text_field("Label", ylabel))
            {
                ax.ylabel(ylabel);
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Grid & Border
    if (widgets::section_header("GRID & BORDER", &sec_grid_, font_heading_))
    {
        if (widgets::begin_animated_section("GRID & BORDER"))
        {
            widgets::begin_group("grid");
            bool grid = ax.grid_enabled();
            if (widgets::checkbox_field("Show Grid", grid))
            {
                ax.set_grid_enabled(grid);
            }
            bool border = ax.border_enabled();
            if (widgets::checkbox_field("Show Border", border))
            {
                ax.set_border_enabled(border);
            }

            auto& as = ax.axis_style();
            widgets::color_field("Grid Color", as.grid_color);
            widgets::drag_field("Grid Width", as.grid_width, 0.1f, 0.5f, 5.0f, "%.1f px");
            widgets::drag_field("Tick Length", as.tick_length, 0.5f, 0.0f, 20.0f, "%.0f px");
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Autoscale
    if (widgets::section_header("AUTOSCALE", &sec_style_, font_heading_))
    {
        if (widgets::begin_animated_section("AUTOSCALE"))
        {
            widgets::begin_group("autoscale");
            const char* modes[] = {"Fit", "Tight", "Padded", "Manual"};
            int         mode    = static_cast<int>(ax.get_autoscale_mode());
            if (widgets::combo_field("Mode", mode, modes, 4))
            {
                ax.autoscale_mode(static_cast<AutoscaleMode>(mode));
            }
            if (widgets::button_field("Auto-fit Now"))
            {
                ax.auto_fit();
            }
            widgets::end_group();
            widgets::end_animated_section();
        }
    }

    // ── Axes Statistics (aggregate across all series)
    if (widgets::section_header("STATISTICS", &sec_axes_stats_, font_heading_))
    {
        if (widgets::begin_animated_section("STATISTICS"))
        {
            widgets::begin_group("axes_stats");
            draw_axes_statistics(ax);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Reference Lines (hidden from legend, manageable here)
    if (widgets::section_header("REFERENCE LINES", &sec_ref_lines_, font_heading_))
    {
        if (widgets::begin_animated_section("REFERENCE LINES"))
        {
            widgets::begin_group("ref_lines");
            draw_reference_lines(ax);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }
}

// ─── Reference Lines ─────────────────────────────────────────────────────────

void Inspector::draw_reference_lines(Axes& ax)
{
    const auto& c = theme();

    // Collect reference lines (Axes::hline / Axes::vline only).
    struct RefEntry
    {
        size_t         index;
        std::string    label;
        spectra::Color color;
    };
    std::vector<RefEntry> refs;

    auto& series_list = ax.series_mut();
    for (size_t i = 0; i < series_list.size(); ++i)
    {
        if (series_list[i] && series_list[i]->is_reference_line())
        {
            RefEntry e;
            e.index = i;
            e.label = series_list[i]->label();
            e.color = series_list[i]->color();
            refs.push_back(std::move(e));
        }
    }

    if (refs.empty())
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.7f));
        ImGui::TextUnformatted("No reference lines");
        ImGui::PopStyleColor();
        return;
    }

    ImFont* icf = icon_font(tokens::ICON_SM);
    ImFont* fnt = icf ? icf : ImGui::GetFont();
    float   gsz = tokens::ICON_SM;

    // Deferred removals (avoid mutating during iteration)
    std::vector<size_t> to_remove;

    for (size_t ri = 0; ri < refs.size(); ++ri)
    {
        ImGui::PushID(static_cast<int>(ri));

        auto&  ref     = refs[ri];
        float  row_h   = tokens::SERIES_ROW_HEIGHT;
        ImVec2 row_min = ImGui::GetCursorScreenPos();
        float  avail_w = ImGui::GetContentRegionAvail().x;
        ImVec2 row_max = ImVec2(row_min.x + avail_w, row_min.y + row_h);
        float  cy      = row_min.y + row_h * 0.5f;

        // Hover background
        bool hovered = ImGui::IsMouseHoveringRect(row_min, row_max);
        if (hovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                row_min,
                row_max,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.4f)),
                tokens::RADIUS_MD);
        }

        // Color dot
        constexpr float dot_sz  = 10.0f;
        constexpr float pad_l   = 8.0f;
        float           x_dot   = row_min.x + pad_l;
        ImU32           dot_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(ref.color.r, ref.color.g, ref.color.b, ref.color.a));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x_dot, cy - dot_sz * 0.5f),
                                                  ImVec2(x_dot + dot_sz, cy + dot_sz * 0.5f),
                                                  dot_col,
                                                  tokens::RADIUS_SM);

        // Label
        float label_x = x_dot + dot_sz + 8.0f;
        ImGui::SetCursorScreenPos(ImVec2(label_x, cy - ImGui::GetTextLineHeight() * 0.5f));
        ImGui::TextUnformatted(ref.label.c_str());

        // Delete button (trash icon)
        constexpr float btn_sz = 22.0f;
        float           btn_x  = row_max.x - btn_sz - 8.0f;
        float           btn_y  = cy - btn_sz * 0.5f;

        ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
        std::string del_id = std::format("##refdel_{}", ri);
        if (ImGui::InvisibleButton(del_id.c_str(), ImVec2(btn_sz, btn_sz)))
            to_remove.push_back(ref.index);

        if (ImGui::IsItemHovered())
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(btn_x, btn_y),
                ImVec2(btn_x + btn_sz, btn_y + btn_sz),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.35f, 0.35f, 0.2f)),
                tokens::RADIUS_SM);
            ImGui::SetTooltip("Remove reference line");
        }

        ImVec4 icon_col(0.85f, 0.35f, 0.35f, 0.75f);
        ImVec2 tsz = fnt->CalcTextSizeA(gsz, FLT_MAX, 0.0f, icon_str(Icon::Trash));
        ImGui::GetWindowDrawList()->AddText(
            fnt,
            gsz,
            ImVec2(btn_x + (btn_sz - tsz.x) * 0.5f, btn_y + (btn_sz - gsz) * 0.5f + 1.0f),
            ImGui::ColorConvertFloat4ToU32(icon_col),
            icon_str(Icon::Trash));

        // Advance cursor
        ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_max.y));
        ImGui::Dummy(ImVec2(0, 0));

        ImGui::PopID();
    }

    // Apply deferred removals (reverse order to keep indices valid)
    for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it)
        ax.remove_series(*it);
}

// ─── Series Properties ──────────────────────────────────────────────────────

void Inspector::draw_series_properties(Series& s, int /*index*/)
{
    const auto& c = theme();

    // Context header with color swatch
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));

    // Determine type name
    const char* type_name = "Series";
    if (dynamic_cast<LineSeries*>(&s))
        type_name = "Line Series";
    else if (dynamic_cast<ScatterSeries*>(&s))
        type_name = "Scatter Series";

    const char*       name  = s.label().empty() ? "Unnamed" : s.label().c_str();
    const std::string title = std::format("{}: {}", type_name, name);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Color swatch + type badge
    {
        const auto&    sc = s.color();
        spectra::Color swatch_col{sc.r, sc.g, sc.b, sc.a};
        widgets::color_swatch(swatch_col, 16.0f);
    }
    ImGui::SameLine(0.0f, tokens::SPACE_2);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(type_name);
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── Appearance
    if (widgets::section_header("APPEARANCE", &sec_appearance_, font_heading_))
    {
        if (widgets::begin_animated_section("APPEARANCE"))
        {
            widgets::begin_group("appearance");

            spectra::Color col = s.color();
            if (widgets::color_field("Color", col))
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_series_vm(&s).set_color(col);
                else
                    s.set_color(col);
            }

            bool vis = s.visible();
            if (widgets::toggle_field("Visible", vis))
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_series_vm(&s).set_visible(vis);
                else
                    s.visible(vis);
            }

            // Line style dropdown (all series types)
            {
                static const char* line_style_names[] =
                    {"None", "Solid", "Dashed", "Dotted", "Dash-Dot", "Dash-Dot-Dot"};
                int ls_idx = static_cast<int>(s.line_style());
                if (widgets::combo_field("Line Style", ls_idx, line_style_names, 6))
                {
                    s.line_style(static_cast<spectra::LineStyle>(ls_idx));
                }
            }

            // Marker style dropdown (all series types)
            {
                static const char* marker_style_names[] = {"None",
                                                           "Point",
                                                           "Circle",
                                                           "Plus",
                                                           "Cross",
                                                           "Star",
                                                           "Square",
                                                           "Diamond",
                                                           "Triangle Up",
                                                           "Triangle Down",
                                                           "Triangle Left",
                                                           "Triangle Right",
                                                           "Pentagon",
                                                           "Hexagon",
                                                           "Filled Circle",
                                                           "Filled Square",
                                                           "Filled Diamond",
                                                           "Filled Triangle Up"};
                int                ms_idx               = static_cast<int>(s.marker_style());
                if (widgets::combo_field("Marker", ms_idx, marker_style_names, 18))
                {
                    s.marker_style(static_cast<spectra::MarkerStyle>(ms_idx));
                }
            }

            // Marker size (shown when marker is not None)
            if (s.marker_style() != spectra::MarkerStyle::None)
            {
                float msz = s.marker_size();
                if (widgets::slider_field("Marker Size", msz, 1.0f, 30.0f, "%.1f px"))
                {
                    s.marker_size(msz);
                }
            }

            // Opacity
            {
                float op = s.opacity();
                if (widgets::slider_field("Opacity", op, 0.0f, 1.0f, "%.2f"))
                {
                    if (figure_vm_)
                        figure_vm_->get_or_create_series_vm(&s).set_opacity(op);
                    else
                        s.opacity(op);
                }
            }

            // Type-specific controls
            if (auto* line = dynamic_cast<LineSeries*>(&s))
            {
                float w = line->width();
                if (widgets::slider_field("Line Width", w, 0.5f, 12.0f, "%.1f px"))
                {
                    line->width(w);
                }
            }
            if (auto* scatter = dynamic_cast<ScatterSeries*>(&s))
            {
                float sz = scatter->size();
                if (widgets::slider_field("Point Size", sz, 0.5f, 30.0f, "%.1f px"))
                {
                    scatter->size(sz);
                }
            }

            // Label editing
            std::string lbl = s.label();
            if (widgets::text_field("Label", lbl))
            {
                if (figure_vm_)
                    figure_vm_->get_or_create_series_vm(&s).set_label(lbl);
                else
                    s.label(lbl);
            }

            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Data Preview (sparkline)
    if (widgets::section_header("PREVIEW", &sec_preview_, font_heading_))
    {
        if (widgets::begin_animated_section("PREVIEW"))
        {
            widgets::begin_group("preview");
            draw_series_sparkline(s);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Data Statistics
    if (widgets::section_header("DATA", &sec_stats_, font_heading_))
    {
        if (widgets::begin_animated_section("DATA"))
        {
            widgets::begin_group("data");
            draw_series_statistics(s);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Back button
    widgets::section_spacing();
    if (widgets::button_field("Back to Series List"))
    {
        if (ctx_.figure)
        {
            ctx_.select_figure(ctx_.figure);
        }
        else
        {
            ctx_.clear();
        }
    }
}

// ─── Helper: compute percentile from sorted data ────────────────────────────

static double compute_percentile(const std::vector<float>& sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    if (sorted.size() == 1)
        return static_cast<double>(sorted[0]);
    double idx = p * static_cast<double>(sorted.size() - 1);
    auto   lo  = static_cast<size_t>(idx);
    size_t hi  = lo + 1;
    if (hi >= sorted.size())
        return static_cast<double>(sorted.back());
    double frac = idx - static_cast<double>(lo);
    return static_cast<double>(sorted[lo]) * (1.0 - frac) + static_cast<double>(sorted[hi]) * frac;
}

// ─── Helper: get data spans from any series type ────────────────────────────

static void get_series_data(const Series&           s,
                            std::span<const float>& x_data,
                            std::span<const float>& y_data,
                            size_t&                 count)
{
    x_data = {};
    y_data = {};
    count  = 0;
    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
    {
        x_data = line->x_data();
        y_data = line->y_data();
        count  = line->point_count();
    }
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
    {
        x_data = scatter->x_data();
        y_data = scatter->y_data();
        count  = scatter->point_count();
    }
}

// ─── Series Statistics ──────────────────────────────────────────────────────

void Inspector::draw_series_statistics(const Series& s)
{
    std::span<const float> x_data;
    std::span<const float> y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    // Point count with badge
    widgets::stat_row("Points", std::format("{}", count).c_str());

    if (count == 0)
        return;

    widgets::small_spacing();
    widgets::separator_label("X Axis", font_heading_);
    widgets::small_spacing();

    // X statistics
    if (!x_data.empty())
    {
        auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
        float xmin              = *xmin_it;
        float xmax              = *xmax_it;

        widgets::stat_row("Min", std::format("{:.6g}", static_cast<double>(xmin)).c_str());
        widgets::stat_row("Max", std::format("{:.6g}", static_cast<double>(xmax)).c_str());
        widgets::stat_row("Range", std::format("{:.6g}", static_cast<double>(xmax - xmin)).c_str());

        // X mean
        double x_sum  = std::accumulate(x_data.begin(), x_data.end(), 0.0);
        double x_mean = x_sum / static_cast<double>(count);
        widgets::stat_row("Mean", std::format("{:.6g}", x_mean).c_str());
    }

    widgets::small_spacing();
    widgets::separator_label("Y Axis", font_heading_);
    widgets::small_spacing();

    // Y statistics
    if (!y_data.empty())
    {
        auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
        float ymin              = *ymin_it;
        float ymax              = *ymax_it;

        widgets::stat_row("Min", std::format("{:.6g}", static_cast<double>(ymin)).c_str());
        widgets::stat_row("Max", std::format("{:.6g}", static_cast<double>(ymax)).c_str());
        widgets::stat_row("Range", std::format("{:.6g}", static_cast<double>(ymax - ymin)).c_str());

        // Mean
        double sum  = std::accumulate(y_data.begin(), y_data.end(), 0.0);
        double mean = sum / static_cast<double>(count);
        widgets::stat_row("Mean", std::format("{:.6g}", mean).c_str());

        // Median (requires sorted copy)
        std::vector<float> sorted(y_data.begin(), y_data.end());
        std::sort(sorted.begin(), sorted.end());
        double median = compute_percentile(sorted, 0.5);
        widgets::stat_row("Median", std::format("{:.6g}", median).c_str());

        // Std deviation
        double sq_sum = 0.0;
        for (float v : y_data)
        {
            double diff = static_cast<double>(v) - mean;
            sq_sum += diff * diff;
        }
        double stddev = std::sqrt(sq_sum / static_cast<double>(count));
        widgets::stat_row("Std Dev", std::format("{:.6g}", stddev).c_str());

        // Percentiles (only for datasets with enough points)
        if (count >= 4)
        {
            widgets::small_spacing();
            widgets::separator_label("Percentiles", font_heading_);
            widgets::small_spacing();

            double p25 = compute_percentile(sorted, 0.25);
            double p75 = compute_percentile(sorted, 0.75);
            double p05 = compute_percentile(sorted, 0.05);
            double p95 = compute_percentile(sorted, 0.95);

            widgets::stat_row("P5", std::format("{:.6g}", p05).c_str());
            widgets::stat_row("P25 (Q1)", std::format("{:.6g}", p25).c_str());
            widgets::stat_row("P50 (Med)", std::format("{:.6g}", median).c_str());
            widgets::stat_row("P75 (Q3)", std::format("{:.6g}", p75).c_str());
            widgets::stat_row("P95", std::format("{:.6g}", p95).c_str());

            // IQR
            widgets::stat_row("IQR", std::format("{:.6g}", p75 - p25).c_str());
        }
    }
}

// ─── Series Sparkline ───────────────────────────────────────────────────────

void Inspector::draw_series_sparkline(const Series& s)
{
    std::span<const float> x_data;
    std::span<const float> y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    if (y_data.empty())
    {
        widgets::info_row("Preview", "No data");
        return;
    }

    // Downsample to max ~200 points for the sparkline
    constexpr size_t   MAX_SPARKLINE = 200;
    std::vector<float> downsampled;
    if (count <= MAX_SPARKLINE)
    {
        downsampled.assign(y_data.begin(), y_data.end());
    }
    else
    {
        downsampled.reserve(MAX_SPARKLINE);
        for (size_t i = 0; i < MAX_SPARKLINE; ++i)
        {
            size_t src = i * count / MAX_SPARKLINE;
            downsampled.push_back(y_data[src]);
        }
    }

    const auto&    sc = s.color();
    spectra::Color line_col{sc.r, sc.g, sc.b, sc.a};
    widgets::sparkline("##series_spark", downsampled, -1.0f, 40.0f, line_col);
}

// ─── Axes Statistics ────────────────────────────────────────────────────────

void Inspector::draw_axes_statistics(const Axes& ax)
{
    size_t total_points   = 0;
    size_t visible_series = 0;
    size_t total_series   = ax.series().size();

    double global_ymin = std::numeric_limits<double>::max();
    double global_ymax = std::numeric_limits<double>::lowest();
    double global_xmin = std::numeric_limits<double>::max();
    double global_xmax = std::numeric_limits<double>::lowest();

    for (const auto& s : ax.series())
    {
        if (!s)
            continue;
        if (s->visible())
            visible_series++;

        std::span<const float> x_data;
        std::span<const float> y_data;
        size_t                 count = 0;
        get_series_data(*s, x_data, y_data, count);
        total_points += count;

        if (!x_data.empty())
        {
            auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
            global_xmin             = std::min(global_xmin, static_cast<double>(*xmin_it));
            global_xmax             = std::max(global_xmax, static_cast<double>(*xmax_it));
        }
        if (!y_data.empty())
        {
            auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
            global_ymin             = std::min(global_ymin, static_cast<double>(*ymin_it));
            global_ymax             = std::max(global_ymax, static_cast<double>(*ymax_it));
        }
    }

    widgets::stat_row("Visible", std::format("{} / {}", visible_series, total_series).c_str());
    widgets::stat_row("Total Points", std::format("{}", total_points).c_str());

    if (total_points > 0)
    {
        widgets::small_spacing();

        if (global_xmin <= global_xmax)
        {
            widgets::stat_row("X Extent",
                              std::format("[{:.4g}, {:.4g}]", global_xmin, global_xmax).c_str());
        }
        if (global_ymin <= global_ymax)
        {
            widgets::stat_row("Y Extent",
                              std::format("[{:.4g}, {:.4g}]", global_ymin, global_ymax).c_str());
        }
    }
}

void Inspector::draw_plugin_status(const PluginManager& plugin_mgr)
{
    auto plugins = plugin_mgr.plugins();
    if (plugins.empty())
        return;

    ImGui::SeparatorText("Plugins");

    for (const auto& p : plugins)
    {
        const char* status_text =
            p.diagnostics.quarantined ? "[!]" : (p.diagnostics.fault_count > 0 ? "[~]" : "[ok]");

        ImGui::TextUnformatted(status_text);
        ImGui::SameLine();
        ImGui::TextUnformatted(p.name.c_str());

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Version: %s", p.version.c_str());
            ImGui::Text("API: v%u.%u", SPECTRA_PLUGIN_API_VERSION_MAJOR, p.api_version_minor);
            ImGui::Text("Calls: %zu  Faults: %zu",
                        p.diagnostics.call_count,
                        p.diagnostics.fault_count);
            ImGui::Text("Init time: %zu \xc2\xb5s", p.diagnostics.init_time_us);
            if (!p.diagnostics.last_fault_reason.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("Last fault: %s", p.diagnostics.last_fault_reason.c_str());
            }
            ImGui::EndTooltip();
        }
    }
}

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
