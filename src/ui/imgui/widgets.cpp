#ifdef SPECTRA_USE_IMGUI

    #include "widgets.hpp"

    #include <algorithm>
    #include <cctype>
    #include <cmath>
    #include <cstdio>
    #include <cstring>
    #include <format>
    #include <imgui.h>
    #include <span>
    #include <unordered_map>
    #include <vector>

    #include "ui/theme/icons.hpp"
    #include "ui/ui_interaction_log.hpp"

    #include <spectra/animator.hpp>

namespace spectra::ui::widgets
{

// ─── Section Animation State ─────────────────────────────────────────────────

static const char* current_animated_section_id_       = nullptr;
static bool        current_section_invisible_measure_ = false;

static float measure_section_content_height()
{
    const float  cursor_h    = ImGui::GetCursorPosY();
    const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
    const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
    const float  region_h    = content_max.y - content_min.y;
    return std::max(cursor_h, region_h);
}

static void begin_section_open_anim(SectionAnimState& anim)
{
    anim.anim_t              = 0.001f;
    anim.remeasure_on_expand = true;
    anim.clip_height         = 0.0f;
}

static std::unordered_map<std::string, SectionAnimState>& section_anim_map()
{
    static std::unordered_map<std::string, SectionAnimState> map;
    return map;
}

SectionAnimState& get_section_anim(const char* id)
{
    return section_anim_map()[id];
}

void update_section_animations(float dt)
{
    for (auto& [key, state] : section_anim_map())
    {
        const float target = state.target_open ? 1.0f : 0.0f;
        if (std::abs(state.anim_t - target) <= 0.001f)
        {
            state.anim_t = target;
            if (state.target_open)
                state.clip_height = state.content_height;
            continue;
        }

        const float     duration         = (target > state.anim_t) ? tokens::DURATION_SECTION_EXPAND
                                                                   : tokens::DURATION_SECTION_COLLAPSE;
        constexpr float kMaxProgressStep = 0.35f;
        const float     step             = std::min(dt / duration, kMaxProgressStep);
        if (target > state.anim_t)
            state.anim_t = std::min(target, state.anim_t + step);
        else
            state.anim_t = std::max(target, state.anim_t - step);
    }
}

bool any_section_animations_active()
{
    for (auto& [key, state] : section_anim_map())
    {
        const float target = state.target_open ? 1.0f : 0.0f;
        if (std::abs(state.anim_t - target) > 0.001f)
            return true;
    }
    return false;
}

// ─── Section Header ─────────────────────────────────────────────────────────

bool section_header(const char* label, bool* open, ImFont* font)
{
    const auto& c = theme();

    ImGui::PushID(label);

    const float  avail  = ImGui::GetContentRegionAvail().x;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float  hdr_h  = tokens::INSPECTOR_HEADER_H;

    // Invisible hit target — avoids ImGui Header colors that render an opaque accent bar.
    ImGui::InvisibleButton("##hdr", ImVec2(avail, hdr_h));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    if (clicked && open)
    {
        log_ui_action("widget", label, "ok", "section_header");
        *open            = !*open;
        auto& anim       = get_section_anim(label);
        anim.target_open = *open;
        if (*open)
            begin_section_open_anim(anim);
        else
        {
            anim.clip_height         = anim.content_height;
            anim.remeasure_on_expand = false;
        }
    }

    // Subtle band behind the header row (rest / hover / pressed).
    {
        float bg_alpha = c.section_header_bg.a;
        if (hovered)
            bg_alpha = std::min(1.0f, bg_alpha + 0.08f);
        if (ImGui::IsItemActive())
            bg_alpha = std::min(1.0f, bg_alpha + 0.12f);

        const ImVec2 hdr_min = cursor;
        const ImVec2 hdr_max(cursor.x + avail, cursor.y + hdr_h);
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(hdr_min,
                          hdr_max,
                          IM_COL32(static_cast<uint8_t>(c.section_header_bg.r * 255),
                                   static_cast<uint8_t>(c.section_header_bg.g * 255),
                                   static_cast<uint8_t>(c.section_header_bg.b * 255),
                                   static_cast<uint8_t>(bg_alpha * 255)),
                          tokens::RADIUS_SM);
        if (hovered || ImGui::IsItemActive())
        {
            dl->AddRect(hdr_min,
                        hdr_max,
                        IM_COL32(static_cast<uint8_t>(c.border_subtle.r * 255),
                                 static_cast<uint8_t>(c.border_subtle.g * 255),
                                 static_cast<uint8_t>(c.border_subtle.b * 255),
                                 hovered ? 90 : 60),
                        tokens::RADIUS_SM);
        }
    }

    // Sync animation state if open changed externally
    if (open)
    {
        auto& anim = get_section_anim(label);
        if (*open != anim.was_open)
        {
            anim.target_open = *open;
            if (*open)
                begin_section_open_anim(anim);
            else
            {
                anim.clip_height         = anim.content_height;
                anim.remeasure_on_expand = false;
            }
            anim.was_open = *open;
        }
    }

    // Chevron + label vertically centered in the header band.
    const float text_h = ImGui::GetTextLineHeight();
    const float row_y  = cursor.y + std::max(0.0f, (hdr_h - text_h) * 0.5f);
    const float row_x  = cursor.x + tokens::SPACE_2;
    ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));

    float chevron_t = 1.0f;
    if (open)
    {
        auto& anim = get_section_anim(label);
        chevron_t  = anim.anim_t;
    }
    const char* chevron =
        (chevron_t > 0.5f) ? icon_str(Icon::ChevronDown) : icon_str(Icon::ChevronRight);
    ImFont* icon_f = icon_font(tokens::ICON_SM);
    if (icon_f)
        ImGui::PushFont(icon_f);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.8f));
    ImGui::TextUnformatted(chevron);
    ImGui::PopStyleColor();
    if (icon_f)
        ImGui::PopFont();

    ImGui::SameLine(0.0f, tokens::SPACE_2);

    char upper_buf[128];
    {
        size_t i = 0;
        for (; label[i] && i < sizeof(upper_buf) - 1; ++i)
            upper_buf[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[i])));
        upper_buf[i] = '\0';
    }
    if (font)
        ImGui::PushFont(font);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(upper_buf);
    ImGui::PopStyleColor();
    if (font)
        ImGui::PopFont();

    // Advance past the header band, then leave a gap before section content.
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + hdr_h));
    ImGui::Dummy(ImVec2(0.0f, tokens::SPACE_1));

    ImGui::PopID();

    if (!open)
        return true;
    auto& anim = get_section_anim(label);
    return *open || anim.anim_t > 0.001f;
}

bool begin_animated_section(const char* id)
{
    auto&       anim                   = get_section_anim(id);
    const float t                      = anim.anim_t;
    current_animated_section_id_       = id;
    current_section_invisible_measure_ = false;

    if (t <= 0.001f)
    {
        current_animated_section_id_ = nullptr;
        return false;
    }

    const float eased = ease::ease_in_out(t);
    const bool  animating =
        std::abs(t - (anim.target_open ? 1.0f : 0.0f)) > 0.001f || anim.remeasure_on_expand;

    if (anim.remeasure_on_expand)
    {
        // One-frame layout at 1px height (invisible) — captures ContentSize without
        // reserving the full expanded height in the parent layout.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
        current_section_invisible_measure_ = true;
        ImGui::BeginChild(id,
                          ImVec2(0.0f, 1.0f),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        return true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * eased);

    const float basis = anim.content_height > 1.0f ? anim.content_height : anim.clip_height;
    if (animating && basis > 1.0f)
    {
        const float visible_h = std::max(1.0f, basis * eased);
        ImGui::BeginChild(id,
                          ImVec2(0.0f, visible_h),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    }
    else
    {
        ImGui::BeginChild(id,
                          ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    }

    return true;
}

void end_animated_section()
{
    const char* id = current_animated_section_id_;
    if (id)
    {
        auto&       anim     = get_section_anim(id);
        const float measured = measure_section_content_height();
        if (measured > 1.0f)
        {
            anim.content_height = measured;
            if (anim.remeasure_on_expand)
            {
                anim.clip_height         = measured;
                anim.remeasure_on_expand = false;
            }
            else if (!anim.target_open)
            {
                anim.clip_height = measured;
            }
        }
    }

    ImGui::EndChild();

    if (current_section_invisible_measure_)
    {
        ImGui::PopStyleVar();   // Alpha (invisible measure)
        current_section_invisible_measure_ = false;
    }
    else
    {
        ImGui::PopStyleVar();   // Alpha (eased fade)
    }

    current_animated_section_id_ = nullptr;
}

// ─── Separator ──────────────────────────────────────────────────────────────

void separator()
{
    const auto& c = theme();
    ImGui::PushStyleColor(ImGuiCol_Separator,
                          ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.3f));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

// ─── Info Row ───────────────────────────────────────────────────────────────

void info_row(const char* label, const char* value)
{
    const auto& c = theme();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

void info_row_mono(const char* label, const char* value)
{
    const auto& c = theme();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

// ─── Color Field ────────────────────────────────────────────────────────────

bool color_field(const char* label, spectra::Color& color)
{
    const auto& c = theme();
    ImGui::PushID(label);

    bool changed = false;

    // 28×28 color swatch button
    constexpr float swatch_sz = 28.0f;
    ImVec2          cursor    = ImGui::GetCursorScreenPos();
    ImU32 col_u32  = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Invisible button for interaction
    if (ImGui::InvisibleButton("##swatch", ImVec2(swatch_sz, swatch_sz)))
    {
        log_ui_action("widget", label, "ok", "color_swatch");
        ImGui::OpenPopup("##color_popover");
    }

    // Draw swatch (checkerboard alpha hint + filled rounded rect)
    float radius = tokens::RADIUS_MD;
    if (color.a < 1.0f)
    {
        // Checkerboard behind translucent colors
        ImU32 ch_a = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        ImU32 ch_b = ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        dl->AddRectFilled(cursor, ImVec2(cursor.x + swatch_sz, cursor.y + swatch_sz), ch_a, radius);
        float half = swatch_sz * 0.5f;
        dl->AddRectFilled(ImVec2(cursor.x + half, cursor.y),
                          ImVec2(cursor.x + swatch_sz, cursor.y + half),
                          ch_b);
        dl->AddRectFilled(ImVec2(cursor.x, cursor.y + half),
                          ImVec2(cursor.x + half, cursor.y + swatch_sz),
                          ch_b);
    }
    dl->AddRectFilled(cursor, ImVec2(cursor.x + swatch_sz, cursor.y + swatch_sz), col_u32, radius);
    // Border
    dl->AddRect(cursor,
                ImVec2(cursor.x + swatch_sz, cursor.y + swatch_sz),
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.5f)),
                radius);

    // Hover highlight
    if (ImGui::IsItemHovered())
    {
        dl->AddRect(
            ImVec2(cursor.x - 1, cursor.y - 1),
            ImVec2(cursor.x + swatch_sz + 1, cursor.y + swatch_sz + 1),
            ImGui::ColorConvertFloat4ToU32(ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.6f)),
            radius + 1,
            0,
            1.5f);
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    // Popover
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tokens::SPACE_3, tokens::SPACE_3));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, c.bg_elevated.a));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.5f));

    if (ImGui::BeginPopup("##color_popover"))
    {
        float col[4] = {color.r, color.g, color.b, color.a};

        // Hue wheel color picker with alpha bar
        if (ImGui::ColorPicker4("##picker",
                                col,
                                ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_AlphaBar
                                    | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB
                                    | ImGuiColorEditFlags_AlphaPreviewHalf))
        {
            color   = spectra::Color{col[0], col[1], col[2], col[3]};
            changed = true;
        }

        // Palette swatches from current data palette
        const auto& palette = spectra::ui::data_palette();
        if (!palette.colors.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.7f));
            ImGui::TextUnformatted("Palette");
            ImGui::PopStyleColor();

            constexpr float sw_sz  = 16.0f;
            constexpr float sw_gap = 3.0f;
            size_t          count  = std::min(palette.colors.size(), size_t(10));
            for (size_t i = 0; i < count; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const auto& pc    = palette.colors[i];
                ImVec4      pcv   = ImVec4(pc.r, pc.g, pc.b, pc.a);
                bool        click = ImGui::ColorButton("##pal", pcv, 0, ImVec2(sw_sz, sw_sz));
                if (click)
                {
                    color   = spectra::Color{pc.r, pc.g, pc.b, color.a};
                    changed = true;
                }
                ImGui::PopID();
                if (i + 1 < count)
                    ImGui::SameLine(0.0f, sw_gap);
            }
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::PopID();
    return changed;
}

// ─── Slider Field ───────────────────────────────────────────────────────────

static std::string format_slider_value(const char* fmt, float value)
{
    if (std::strcmp(fmt, "%.2f") == 0)
        return std::format("{:.2f}", value);
    if (std::strcmp(fmt, "%.1f px") == 0)
        return std::format("{:.1f} px", value);
    return std::format("{:.2f}", value);
}

bool slider_field(const char* label, float& value, float min, float max, const char* fmt)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout: 80px label | remaining width input
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    // Slider visual: 4px track height via reduced frame padding, pill-shaped thumb
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_PILL);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, tokens::RADIUS_PILL);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));   // ~4px track height
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f);                 // 14px thumb diameter

    // Smooth hover transition: lerp track background over 80ms
    ImGuiID wid = ImGui::GetID("##slider");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                          ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                          ImVec4(c.accent_hover.r, c.accent_hover.g, c.accent_hover.b, 1.0f));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::SliderFloat("##slider", &value, min, max, fmt);

    update_widget_hover(wid, ImGui::IsItemHovered());

    // Draw accent fill bar from min to current value position
    if (max > min)
    {
        ImVec2      frame_min = ImGui::GetItemRectMin();
        ImVec2      frame_max = ImGui::GetItemRectMax();
        float       t         = std::clamp((value - min) / (max - min), 0.0f, 1.0f);
        float       fill_w    = (frame_max.x - frame_min.x) * t;
        float       track_h   = 4.0f;
        float       center_y  = (frame_min.y + frame_max.y) * 0.5f;
        ImDrawList* dl        = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            ImVec2(frame_min.x, center_y - track_h * 0.5f),
            ImVec2(frame_min.x + fill_w, center_y + track_h * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.6f)),
            tokens::RADIUS_PILL);

        // Floating value label above the thumb while dragging
        if (ImGui::IsItemActive())
        {
            float             thumb_x = frame_min.x + fill_w;
            const std::string val_buf = format_slider_value(fmt, value);

            ImVec2 val_sz = ImGui::CalcTextSize(val_buf.c_str());
            float  lbl_w  = val_sz.x + tokens::SPACE_2 * 2.0f;
            float  lbl_h  = val_sz.y + tokens::SPACE_1 * 2.0f;
            float  lbl_x  = thumb_x - lbl_w * 0.5f;
            float  lbl_y  = frame_min.y - lbl_h - 4.0f;

            // Clamp horizontally to frame bounds
            lbl_x = std::clamp(lbl_x, frame_min.x, frame_max.x - lbl_w);

            ImU32 lbl_bg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.95f));
            ImU32 lbl_fg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 1.0f));
            ImU32 lbl_border = ImGui::ColorConvertFloat4ToU32(
                ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.3f));

            dl->AddRectFilled(ImVec2(lbl_x, lbl_y),
                              ImVec2(lbl_x + lbl_w, lbl_y + lbl_h),
                              lbl_bg,
                              tokens::RADIUS_SM);
            dl->AddRect(ImVec2(lbl_x, lbl_y),
                        ImVec2(lbl_x + lbl_w, lbl_y + lbl_h),
                        lbl_border,
                        tokens::RADIUS_SM);
            dl->AddText(ImVec2(lbl_x + tokens::SPACE_2, lbl_y + tokens::SPACE_1),
                        lbl_fg,
                        val_buf.c_str());
        }
    }

    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
    ImGui::PopID();
    return changed;
}

// ─── Drag Field ─────────────────────────────────────────────────────────────

bool drag_field(const char* label, float& value, float speed, float min, float max, const char* fmt)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);

    ImGuiID wid = ImGui::GetID("##drag");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::DragFloat("##drag", &value, speed, min, max, fmt);

    update_widget_hover(wid, ImGui::IsItemHovered());
    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Drag Field 2 ───────────────────────────────────────────────────────────

bool drag_field2(const char* label, float& v0, float& v1, float speed, const char* fmt)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);

    ImGuiID wid = ImGui::GetID("##drag2");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    float v[2]    = {v0, v1};
    bool  changed = ImGui::DragFloat2("##drag2", v, speed, 0.0f, 0.0f, fmt);
    if (changed)
    {
        v0 = v[0];
        v1 = v[1];
    }

    update_widget_hover(wid, ImGui::IsItemHovered());
    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Checkbox Field ─────────────────────────────────────────────────────────

bool checkbox_field(const char* label, bool& value)
{
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,
                          ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

    bool changed = ImGui::Checkbox(label, &value);
    if (changed)
        log_ui_action("widget", label, "ok", "checkbox");

    draw_focus_ring_if_needed();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Toggle Field ───────────────────────────────────────────────────────────

bool toggle_field(const char* label, bool& value)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 36.0f);

    // Draw a toggle switch using ImGui draw list
    ImVec2      pos  = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float height = 18.0f;
    float width  = 34.0f;
    float radius = height * 0.5f;

    // Invisible button for interaction
    bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    if (clicked)
    {
        log_ui_action("widget", label, "ok", "toggle");
        value = !value;
    }

    // Smooth animation: track knob position with hover system
    ImGuiID wid = ImGui::GetID("##toggle_anim");
    float   t   = widget_hover_t(wid);
    update_widget_hover(wid, value);

    // Background: lerp between off (tertiary) and on (accent) colors
    float bg_r   = c.bg_tertiary.r + (c.accent.r - c.bg_tertiary.r) * t;
    float bg_g   = c.bg_tertiary.g + (c.accent.g - c.bg_tertiary.g) * t;
    float bg_b   = c.bg_tertiary.b + (c.accent.b - c.bg_tertiary.b) * t;
    float bg_a   = c.bg_tertiary.a + (c.accent.a - c.bg_tertiary.a) * t;
    ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(bg_r, bg_g, bg_b, bg_a));
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_col, radius);

    // Knob: smoothly slide between left and right positions
    float knob_off = pos.x + radius;
    float knob_on  = pos.x + width - radius;
    float knob_x   = knob_off + (knob_on - knob_off) * t;
    draw->AddCircleFilled(ImVec2(knob_x, pos.y + radius),
                          radius - 2.0f,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)));

    ImGui::PopID();
    return clicked;
}

// ─── Combo Field ────────────────────────────────────────────────────────────

bool combo_field(const char* label, int& current, const char* const* items, int count)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, tokens::RADIUS_LG);

    ImGuiID wid = ImGui::GetID("##combo");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::Combo("##combo", &current, items, count);

    update_widget_hover(wid, ImGui::IsItemHovered());
    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

// ─── Text Field ─────────────────────────────────────────────────────────────

bool text_field(const char* label, std::string& value)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);

    ImGuiID wid = ImGui::GetID("##text");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    char buf[256];
    std::strncpy(buf, value.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool changed = ImGui::InputText("##text", buf, sizeof(buf));
    if (changed)
    {
        value = buf;
    }

    update_widget_hover(wid, ImGui::IsItemHovered());
    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Button Field ───────────────────────────────────────────────────────────

bool button_field(const char* label)
{
    const auto& c = theme();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, 0.7f));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));

    bool clicked = ImGui::Button(label, ImVec2(-1, 0));
    if (clicked)
        log_ui_action("widget", label, "ok", "button");

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}

// ─── Icon Button Small ──────────────────────────────────────────────────────

bool icon_button_small(const char* icon, const char* tooltip, bool active)
{
    const auto& c = theme();

    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, 1.0f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 0.55f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ui::tokens::SPACE_1, ui::tokens::SPACE_1));

    ImFont* f = icon_font(tokens::ICON_SM);
    if (f)
        ImGui::PushFont(f);

    bool clicked = ImGui::Button(icon, ImVec2(24, 24));
    if (clicked)
        log_ui_action("widget", tooltip ? tooltip : icon, "ok", "icon_button_small");

    if (f)
        ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_2));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tokens::RADIUS_MD);
        ImGui::PushStyleColor(ImGuiCol_PopupBg,
                              ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.95f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.3f));
        ImGui::SetTooltip("%s", tooltip);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    return clicked;
}

// ─── Icon Button (32px hitbox) ───────────────────────────────────────────────

bool icon_button(const char* cmdId, ui::Icon icon, const char* tooltip, bool active)
{
    const auto& c = theme();

    ImGui::PushID(cmdId);

    constexpr float hitbox = tokens::ICON_BUTTON_HITBOX;   // 32px

    // Style: accent pill when active, transparent otherwise
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, 1.0f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 0.7f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    // Use the icon font for the glyph
    ImFont* f = icon_font(tokens::ICON_MD);
    if (f)
        ImGui::PushFont(f);

    const char* glyph   = icon_str(icon);
    bool        clicked = ImGui::Button(glyph, ImVec2(hitbox, hitbox));
    if (clicked)
        log_ui_action("widget", cmdId, "ok", "icon_button");

    if (f)
        ImGui::PopFont();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);

    // Active indicator: small accent dot below the button
    if (active)
    {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        float  cx   = (rmin.x + rmax.x) * 0.5f;
        float  by   = rmax.y + 1.0f;
        ImU32  dot_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.9f));
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, by + 2.0f), 2.0f, dot_col);
    }

    // Tooltip
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tokens::SPACE_3, tokens::SPACE_2));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tokens::RADIUS_MD);
        ImGui::PushStyleColor(ImGuiCol_PopupBg,
                              ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.95f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.3f));
        ImGui::SetTooltip("%s", tooltip);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::PopID();
    return clicked;
}

// ─── Card / Sub-panel ─────────────────────────────────────────────────────────

bool begin_card(const char* id, float rounding, float padding)
{
    const auto& c = theme();

    // Surface-2 fill with a subtle hairline border, rounded to match the panel
    // language. AutoResizeY lets the card hug its content so groups never leave
    // dead space.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, tokens::BORDER_WIDTH_NORMAL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));

    bool visible =
        ImGui::BeginChild(id,
                          ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    return visible;
}

void end_card()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// ─── Chip / Pill ──────────────────────────────────────────────────────────────

bool chip(const char* label, bool active)
{
    const auto& c = theme();
    ImGui::PushID(label);

    ImVec2 text_sz = ImGui::CalcTextSize(label);
    float  w       = text_sz.x + tokens::CHIP_PADDING_H * 2.0f;
    float  h       = tokens::CHIP_HEIGHT;
    ImVec2 pos     = ImGui::GetCursorScreenPos();

    bool clicked = ImGui::InvisibleButton("##chip", ImVec2(w, h));
    if (clicked)
        log_ui_action("widget", label, "ok", "chip");
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Fill: accent tint when active, faint surface otherwise; lifts on hover.
    Color fill = active ? c.accent.with_alpha(hovered ? 0.30f : 0.22f)
                        : c.bg_elevated.with_alpha(hovered ? 0.85f : 0.55f);
    Color border =
        active ? c.accent.with_alpha(0.65f) : c.border_subtle.with_alpha(hovered ? 0.7f : 0.45f);
    Color text = active ? c.accent
                        : Color{c.text_secondary.r,
                                c.text_secondary.g,
                                c.text_secondary.b,
                                hovered ? 1.0f : 0.85f};

    dl->AddRectFilled(pos,
                      ImVec2(pos.x + w, pos.y + h),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(fill.r, fill.g, fill.b, fill.a)),
                      h * 0.5f);
    dl->AddRect(pos,
                ImVec2(pos.x + w, pos.y + h),
                ImGui::ColorConvertFloat4ToU32(ImVec4(border.r, border.g, border.b, border.a)),
                h * 0.5f,
                0,
                1.0f);
    dl->AddText(ImVec2(pos.x + tokens::CHIP_PADDING_H, pos.y + (h - text_sz.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(text.r, text.g, text.b, text.a)),
                label);

    ImGui::PopID();
    return clicked;
}

// ─── Panel Title ──────────────────────────────────────────────────────────────

void panel_title(const char* title, const char* subtitle, ImFont* title_font)
{
    const auto& c = theme();

    if (title_font)
        ImGui::PushFont(title_font);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (title_font)
        ImGui::PopFont();

    if (subtitle && subtitle[0] != '\0')
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.9f));
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
    }
}

// ─── Phase ROS UX Primitives ────────────────────────────────────────────────

int empty_state(ui::Icon                          icon,
                const char*                       title,
                const char*                       subtitle,
                std::span<const EmptyStateAction> actions)
{
    const auto& c = theme();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float max_w   = std::min(tokens::EMPTY_STATE_MAX_WIDTH, std::max(120.0f, avail_w));
    const float start_x = ImGui::GetCursorPosX() + std::max(0.0f, (avail_w - max_w) * 0.5f);

    ImGui::PushID(title ? title : "empty_state");
    ImGui::SetCursorPosX(start_x);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::BeginGroup();

    if (ImFont* f = icon_font(tokens::EMPTY_STATE_ICON_SIZE))
        ImGui::PushFont(f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.85f));
    const char*  glyph   = icon_str(icon);
    const ImVec2 icon_sz = ImGui::CalcTextSize(glyph);
    ImGui::SetCursorPosX(start_x + (max_w - icon_sz.x) * 0.5f);
    ImGui::TextUnformatted(glyph);
    ImGui::PopStyleColor();
    if (ImFont* f = icon_font(tokens::EMPTY_STATE_ICON_SIZE); f)
        ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, tokens::SPACE_2));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    const ImVec2 title_sz = ImGui::CalcTextSize(title ? title : "");
    ImGui::SetCursorPosX(start_x + (max_w - title_sz.x) * 0.5f);
    ImGui::TextUnformatted(title ? title : "");
    ImGui::PopStyleColor();

    if (subtitle && subtitle[0] != '\0')
    {
        ImGui::Dummy(ImVec2(0.0f, tokens::SPACE_1));
        const float wrap_x = ImGui::GetWindowPos().x + start_x + max_w;
        ImGui::PushTextWrapPos(wrap_x);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.9f));
        ImGui::SetCursorPosX(start_x);
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    int clicked = -1;
    if (!actions.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, tokens::SPACE_2));
        float               total_w = 0.0f;
        std::vector<ImVec2> sizes;
        sizes.reserve(actions.size());
        for (const EmptyStateAction& action : actions)
        {
            const ImVec2 text_sz = ImGui::CalcTextSize(action.label ? action.label : "");
            ImVec2       size(std::max(72.0f, text_sz.x + tokens::SPACE_4 * 2.0f),
                        ImGui::GetFrameHeight());
            sizes.push_back(size);
            total_w += size.x;
        }
        total_w += tokens::EMPTY_STATE_ACTION_GAP * static_cast<float>(actions.size() - 1);
        const bool stack_actions = total_w > max_w;
        if (!stack_actions)
            ImGui::SetCursorPosX(start_x + std::max(0.0f, (max_w - total_w) * 0.5f));

        for (size_t i = 0; i < actions.size(); ++i)
        {
            const EmptyStateAction& action = actions[i];
            if (stack_actions && i > 0)
            {
                ImGui::SetCursorPosX(start_x + std::max(0.0f, (max_w - sizes[i].x) * 0.5f));
            }
            ImGui::PushID(action.id ? action.id : action.label);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
            if (action.primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.85f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(c.accent_hover.r, c.accent_hover.g, c.accent_hover.b, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(c.accent.r, c.accent.g, c.accent.b, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.75f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.25f));
            }
            if (ImGui::Button(action.label ? action.label : "", sizes[i]))
            {
                clicked = static_cast<int>(i);
                log_ui_action("widget",
                              action.label ? action.label : "empty_action",
                              "ok",
                              "empty_state");
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::PopID();
            if (i + 1 < actions.size())
                ImGui::SameLine(0.0f, tokens::EMPTY_STATE_ACTION_GAP);
        }
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
}

int panel_header(const char*                        title,
                 const char*                        subtitle,
                 std::span<const PanelHeaderAction> right_actions)
{
    const auto& c = theme();
    ImGui::PushID(title ? title : "panel_header");

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  w   = ImGui::GetContentRegionAvail().x;
    const float  h   = tokens::PANEL_HEADER_HEIGHT;
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + w, pos.y + h),
                      ImGui::ColorConvertFloat4ToU32(
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.35f)),
                      tokens::RADIUS_MD);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + tokens::SPACE_3, pos.y + tokens::SPACE_1));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 1.0f));
    ImGui::TextUnformatted(title ? title : "");
    ImGui::PopStyleColor();
    if (subtitle && subtitle[0] != '\0')
    {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + tokens::SPACE_3, pos.y + tokens::SPACE_1 + 17.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.9f));
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
    }

    int clicked = -1;
    if (!right_actions.empty())
    {
        float x = pos.x + w - tokens::SPACE_2
                  - static_cast<float>(right_actions.size()) * tokens::TOOLBAR_BUTTON_SIZE
                  - static_cast<float>(right_actions.size() - 1) * tokens::TOOLBAR_BUTTON_GAP;
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y + (h - tokens::TOOLBAR_BUTTON_SIZE) * 0.5f));
        for (size_t i = 0; i < right_actions.size(); ++i)
        {
            const auto& action = right_actions[i];
            ImGui::PushID(action.id ? action.id : action.tooltip);
            if (toolbar_button(action.icon, action.tooltip, action.active))
                clicked = static_cast<int>(i);
            ImGui::PopID();
            if (i + 1 < right_actions.size())
                ImGui::SameLine(0.0f, tokens::TOOLBAR_BUTTON_GAP);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + tokens::SPACE_2));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    return clicked;
}

bool toolbar_button(ui::Icon icon, const char* tooltip, bool active)
{
    const auto& c = theme();
    ImGui::PushID(tooltip ? tooltip : icon_str(icon));

    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, 1.0f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.35f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 0.95f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.28f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    if (ImFont* f = icon_font(tokens::ICON_SM))
        ImGui::PushFont(f);
    const bool clicked =
        ImGui::Button(icon_str(icon),
                      ImVec2(tokens::TOOLBAR_BUTTON_SIZE, tokens::TOOLBAR_BUTTON_SIZE));
    if (ImFont* f = icon_font(tokens::ICON_SM); f)
        ImGui::PopFont();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    if (clicked)
        log_ui_action("widget", tooltip ? tooltip : "toolbar_button", "ok", "toolbar_button");

    ImGui::PopID();
    return clicked;
}

void stat_card(const char* label, const char* value, const char* unit, const char* trend)
{
    const auto&  c   = theme();
    const float  w   = std::max(tokens::STAT_CARD_MIN_WIDTH, ImGui::GetContentRegionAvail().x);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + w, pos.y + tokens::STAT_CARD_HEIGHT),
                      ImGui::ColorConvertFloat4ToU32(
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.58f)),
                      tokens::RADIUS_MD);
    dl->AddRect(pos,
                ImVec2(pos.x + w, pos.y + tokens::STAT_CARD_HEIGHT),
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.45f)),
                tokens::RADIUS_MD);

    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(ImVec2(pos.x + tokens::SPACE_3, pos.y + tokens::SPACE_2));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.9f));
    ImGui::TextUnformatted(label ? label : "");
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(pos.x + tokens::SPACE_3, pos.y + 29.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 1.0f));
    ImGui::TextUnformatted(value ? value : "-");
    ImGui::PopStyleColor();
    if (unit && unit[0] != '\0')
    {
        ImGui::SameLine(0.0f, tokens::SPACE_1);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.85f));
        ImGui::TextUnformatted(unit);
        ImGui::PopStyleColor();
    }
    if (trend && trend[0] != '\0')
    {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + w - ImGui::CalcTextSize(trend).x - tokens::SPACE_3,
                                         pos.y + tokens::SPACE_2));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, 0.9f));
        ImGui::TextUnformatted(trend);
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(
        ImVec2(pos.x, pos.y + tokens::STAT_CARD_HEIGHT + tokens::STAT_CARD_GAP));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool search_box(char* buf, size_t buf_size, const char* placeholder, bool* changed)
{
    const auto& c             = theme();
    bool        local_changed = false;
    ImGui::PushID(placeholder ? placeholder : "search_box");
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(28.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.82f));
    ImGui::PushItemWidth(-1.0f);
    local_changed =
        ImGui::InputTextWithHint("##search", placeholder ? placeholder : "", buf, buf_size);
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(min.x + tokens::SPACE_2,
                       min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.85f)),
                icon_str(ui::Icon::Search));

    if (buf && buf[0] != '\0')
    {
        const float btn = tokens::SEARCH_CLEAR_BTN_SIZE;
        ImGui::SetCursorScreenPos(
            ImVec2(max.x - btn - tokens::SPACE_1, min.y + (max.y - min.y - btn) * 0.5f));
        if (toolbar_button(ui::Icon::Close, "Clear search", false))
        {
            buf[0]        = '\0';
            local_changed = true;
        }
    }

    if (changed)
        *changed = local_changed;
    ImGui::PopID();
    return local_changed;
}

bool filter_chip(const char* label, bool active)
{
    return chip(label, active);
}

void status_pill(const char* label, const Color& color, bool dot)
{
    const auto&  c      = theme();
    const Color  accent = (color.r > 0.0f || color.g > 0.0f || color.b > 0.0f) ? color : c.accent;
    const ImVec2 text   = ImGui::CalcTextSize(label ? label : "");
    const float  dot_w  = dot ? 8.0f + tokens::SPACE_1 : 0.0f;
    const float  w      = text.x + dot_w + tokens::STATUS_BAR_PILL_PAD_H * 2.0f;
    const float  h      = tokens::STATUS_BAR_PILL_HEIGHT;
    const ImVec2 pos    = ImGui::GetCursorScreenPos();
    ImDrawList*  dl     = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + w, pos.y + h),
                      ImGui::ColorConvertFloat4ToU32(
                          ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.62f)),
                      tokens::STATUS_BAR_PILL_RADIUS);
    dl->AddRect(pos,
                ImVec2(pos.x + w, pos.y + h),
                ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 0.38f)),
                tokens::STATUS_BAR_PILL_RADIUS);
    float x = pos.x + tokens::STATUS_BAR_PILL_PAD_H;
    if (dot)
    {
        dl->AddCircleFilled(
            ImVec2(x + 3.0f, pos.y + h * 0.5f),
            3.0f,
            ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 1.0f)));
        x += 8.0f + tokens::SPACE_1;
    }
    dl->AddText(ImVec2(x, pos.y + (h - text.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, 0.95f)),
                label ? label : "");
    ImGui::Dummy(ImVec2(w, h));
}

float status_pill_width(const char* label, bool dot)
{
    const ImVec2 text  = ImGui::CalcTextSize(label ? label : "");
    const float  dot_w = dot ? 8.0f + tokens::SPACE_1 : 0.0f;
    return text.x + dot_w + tokens::STATUS_BAR_PILL_PAD_H * 2.0f;
}

void draw_status_pills(std::span<const StatusPillSpec> pills, float gap)
{
    const float start_x    = ImGui::GetCursorPosX();
    const float right_edge = ImGui::GetWindowContentRegionMax().x;
    const float max_width  = std::max(0.0f, right_edge - start_x);
    float       used_width = 0.0f;
    bool        drew_any   = false;
    for (size_t i = 0; i < pills.size(); ++i)
    {
        const float pill_w = status_pill_width(pills[i].label.c_str(), pills[i].dot);
        const float needed = pill_w + (drew_any ? gap : 0.0f);
        if (drew_any && used_width + needed > max_width - 1.0f)
            break;
        if (!drew_any && pill_w > max_width - 1.0f && max_width > 1.0f)
        {
            status_pill(pills[i].label.c_str(), pills[i].color, pills[i].dot);
            break;
        }
        if (drew_any)
            ImGui::SameLine(0.0f, gap);
        status_pill(pills[i].label.c_str(), pills[i].color, pills[i].dot);
        used_width += needed;
        drew_any = true;
    }
}

void drop_zone_overlay(const ImVec2& min, const ImVec2& max, const char* label, bool valid)
{
    const auto&          c = theme();
    const spectra::Color accent =
        valid ? spectra::Color{c.accent.r, c.accent.g, c.accent.b, c.accent.a}
              : spectra::Color{c.warning.r, c.warning.g, c.warning.b, c.warning.a};
    const float alpha = valid ? tokens::DROP_ZONE_ALPHA_VALID : tokens::DROP_ZONE_ALPHA_INVALID;
    ImDrawList* dl    = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min,
                      max,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, alpha)),
                      tokens::RADIUS_LG);

    const ImU32 border =
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 0.85f));
    const float dash = tokens::DROP_ZONE_DASH_LEN;
    const float gap  = dash * 0.65f;
    for (float x = min.x; x < max.x; x += dash + gap)
    {
        dl->AddLine(ImVec2(x, min.y),
                    ImVec2(std::min(x + dash, max.x), min.y),
                    border,
                    tokens::DROP_ZONE_BORDER_WIDTH);
        dl->AddLine(ImVec2(x, max.y),
                    ImVec2(std::min(x + dash, max.x), max.y),
                    border,
                    tokens::DROP_ZONE_BORDER_WIDTH);
    }
    for (float y = min.y; y < max.y; y += dash + gap)
    {
        dl->AddLine(ImVec2(min.x, y),
                    ImVec2(min.x, std::min(y + dash, max.y)),
                    border,
                    tokens::DROP_ZONE_BORDER_WIDTH);
        dl->AddLine(ImVec2(max.x, y),
                    ImVec2(max.x, std::min(y + dash, max.y)),
                    border,
                    tokens::DROP_ZONE_BORDER_WIDTH);
    }
    if (label && label[0] != '\0')
    {
        const ImVec2 sz = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2((min.x + max.x - sz.x) * 0.5f, (min.y + max.y - sz.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, 0.95f)),
                    label);
    }
}

void skeleton_rows(int count, float row_height)
{
    const auto& c  = theme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w  = ImGui::GetContentRegionAvail().x;
    const float t  = static_cast<float>(std::fmod(ImGui::GetTime() * 1.8, 1.0));
    for (int i = 0; i < count; ++i)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float  alpha =
            0.25f + 0.18f * std::sin((t + static_cast<float>(i) * 0.16f) * 6.28318f);
        dl->AddRectFilled(pos,
                          ImVec2(pos.x + w, pos.y + row_height),
                          ImGui::ColorConvertFloat4ToU32(
                              ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, alpha)),
                          tokens::RADIUS_SM);
        ImGui::Dummy(ImVec2(w, row_height + tokens::SPACE_1));
    }
}

// ─── Group ──────────────────────────────────────────────────────────────────

void begin_group(const char* id)
{
    ImGui::PushID(id);
    ImGui::Indent(tokens::SPACE_3);
}

void end_group()
{
    ImGui::Unindent(tokens::SPACE_3);
    ImGui::PopID();
}

// ─── Color Swatch ───────────────────────────────────────────────────────────

void color_swatch(const spectra::Color& color, float size)
{
    ImVec2      pos  = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32       col  = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col, tokens::RADIUS_SM);
    ImGui::Dummy(ImVec2(size, size));
}

// ─── Spacing Helpers ────────────────────────────────────────────────────────

void small_spacing()
{
    ImGui::Spacing();
}

void section_spacing()
{
    ImGui::Spacing();
    ImGui::Spacing();
}

// ─── Sparkline ──────────────────────────────────────────────────────────────

void sparkline(const char*            id,
               std::span<const float> values,
               float                  width,
               float                  height,
               const spectra::Color&  color)
{
    if (values.empty())
        return;

    const auto& c = theme();
    ImGui::PushID(id);

    float       w    = (width < 0.0f) ? ImGui::GetContentRegionAvail().x : width;
    ImVec2      pos  = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Find data range
    float vmin = values[0];
    float vmax = values[0];
    for (float v : values)
    {
        if (v < vmin)
            vmin = v;
        if (v > vmax)
            vmax = v;
    }
    float range = (vmax - vmin);
    if (range < 1e-10f)
        range = 1.0f;

    // Determine line color
    spectra::Color line_col = (color.r > 0.0f || color.g > 0.0f || color.b > 0.0f)
                                  ? color
                                  : spectra::Color{c.accent.r, c.accent.g, c.accent.b, c.accent.a};
    ImU32          col32 =
        ImGui::ColorConvertFloat4ToU32(ImVec4(line_col.r, line_col.g, line_col.b, line_col.a));
    ImU32 fill_col =
        ImGui::ColorConvertFloat4ToU32(ImVec4(line_col.r, line_col.g, line_col.b, 0.15f));

    // Draw filled area
    size_t n          = values.size();
    float  step       = w / static_cast<float>(n > 1 ? n - 1 : 1);
    float  baseline_y = pos.y + height;

    // Build polyline for fill
    std::vector<ImVec2> fill_pts;
    fill_pts.reserve(n + 2);
    fill_pts.emplace_back(pos.x, baseline_y);
    for (size_t i = 0; i < n; ++i)
    {
        float x = pos.x + static_cast<float>(i) * step;
        float y = pos.y + height - ((values[i] - vmin) / range) * height;
        fill_pts.emplace_back(x, y);
    }
    fill_pts.emplace_back(pos.x + w, baseline_y);
    draw->AddConvexPolyFilled(fill_pts.data(), static_cast<int>(fill_pts.size()), fill_col);

    // Draw line
    for (size_t i = 0; i + 1 < n; ++i)
    {
        float x0 = pos.x + static_cast<float>(i) * step;
        float y0 = pos.y + height - ((values[i] - vmin) / range) * height;
        float x1 = pos.x + static_cast<float>(i + 1) * step;
        float y1 = pos.y + height - ((values[i + 1] - vmin) / range) * height;
        draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col32, 1.5f);
    }

    ImGui::Dummy(ImVec2(w, height));
    ImGui::PopID();
}

// ─── Progress Bar ───────────────────────────────────────────────────────────

void progress_bar(const char* label, float fraction, const char* overlay)
{
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// ─── Badge ──────────────────────────────────────────────────────────────────

void badge(const char* text, const spectra::Color& bg, const spectra::Color& fg)
{
    const auto& c = theme();

    spectra::Color bg_col = (bg.r > 0.0f || bg.g > 0.0f || bg.b > 0.0f)
                                ? bg
                                : spectra::Color{c.accent_muted.r,
                                                 c.accent_muted.g,
                                                 c.accent_muted.b,
                                                 c.accent_muted.a};
    spectra::Color fg_col = (fg.r > 0.0f || fg.g > 0.0f || fg.b > 0.0f)
                                ? fg
                                : spectra::Color{c.accent.r, c.accent.g, c.accent.b, c.accent.a};

    ImVec2 text_size = ImGui::CalcTextSize(text);
    float  pad_x     = tokens::SPACE_2;
    float  pad_y     = 2.0f;
    float  total_w   = text_size.x + pad_x * 2.0f;
    float  total_h   = text_size.y + pad_y * 2.0f;

    ImVec2      pos  = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 bg32 = ImGui::ColorConvertFloat4ToU32(ImVec4(bg_col.r, bg_col.g, bg_col.b, bg_col.a));
    draw->AddRectFilled(pos, ImVec2(pos.x + total_w, pos.y + total_h), bg32, tokens::RADIUS_PILL);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad_x, pos.y + pad_y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fg_col.r, fg_col.g, fg_col.b, fg_col.a));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + total_h + 2.0f));
    ImGui::Dummy(ImVec2(total_w, 0));   // Advance cursor
}

// ─── Separator Label ────────────────────────────────────────────────────────

void separator_label(const char* label, ImFont* font)
{
    const auto& c     = theme();
    float       avail = ImGui::GetContentRegionAvail().x;
    ImVec2      pos   = ImGui::GetCursorScreenPos();

    if (font)
        ImGui::PushFont(font);
    ImVec2 text_size = ImGui::CalcTextSize(label);
    if (font)
        ImGui::PopFont();

    float line_y = std::floor(pos.y + text_size.y * 0.5f);
    float gap    = tokens::SPACE_2;
    float text_x = (avail - text_size.x) * 0.5f;

    ImDrawList* draw     = ImGui::GetWindowDrawList();
    ImU32       line_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, c.border_subtle.a));

    // Left line
    if (text_x > gap)
    {
        draw->AddLine(ImVec2(pos.x, line_y), ImVec2(pos.x + text_x - gap, line_y), line_col);
    }
    // Right line
    float right_start = pos.x + text_x + text_size.x + gap;
    if (right_start < pos.x + avail)
    {
        draw->AddLine(ImVec2(right_start, line_y), ImVec2(pos.x + avail, line_y), line_col);
    }

    // Centered text
    ImGui::SetCursorScreenPos(ImVec2(pos.x + text_x, pos.y));
    if (font)
        ImGui::PushFont(font);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, c.text_tertiary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (font)
        ImGui::PopFont();
}

// ─── Integer Drag Field ─────────────────────────────────────────────────────

bool int_drag_field(const char* label, int& value, int speed, int min, int max, const char* fmt)
{
    const auto& c = theme();
    ImGui::PushID(label);

    // Side-by-side label-value layout
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(tokens::INSPECTOR_LABEL_WIDTH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);

    ImGuiID wid = ImGui::GetID("##idrag");
    float   ht  = widget_hover_t(wid);
    float   hr  = c.bg_tertiary.r + 0.03f * ht;
    float   hg  = c.bg_tertiary.g + 0.03f * ht;
    float   hb  = c.bg_tertiary.b + 0.03f * ht;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(hr, hg, hb, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    auto fspeed  = static_cast<float>(speed);
    bool changed = ImGui::DragInt("##idrag", &value, fspeed, min, max, fmt);

    update_widget_hover(wid, ImGui::IsItemHovered());
    draw_focus_ring_if_needed();

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Stat Row ───────────────────────────────────────────────────────────────

void stat_row(const char* label, const char* value, const char* unit)
{
    const auto& c = theme();

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    if (unit)
    {
        const std::string buf = std::format("{} {}", value, unit);
        ImGui::TextUnformatted(buf.c_str());
    }
    else
    {
        ImGui::TextUnformatted(value);
    }
    ImGui::PopStyleColor();
}

void stat_row_colored(const char*           label,
                      const char*           value,
                      const spectra::Color& dot_color,
                      const char*           unit)
{
    // const auto& c = theme();  // Currently unused

    // Color dot
    ImVec2      pos    = ImGui::GetCursorScreenPos();
    ImDrawList* draw   = ImGui::GetWindowDrawList();
    float       dot_r  = 4.0f;
    float       text_h = ImGui::GetTextLineHeight();
    ImU32       col32 =
        ImGui::ColorConvertFloat4ToU32(ImVec4(dot_color.r, dot_color.g, dot_color.b, dot_color.a));
    draw->AddCircleFilled(ImVec2(pos.x + dot_r, pos.y + text_h * 0.5f), dot_r, col32);
    ImGui::Dummy(ImVec2(dot_r * 2.0f + 4.0f, 0));
    ImGui::SameLine();

    stat_row(label, value, unit);
}

// ─── Focus Ring ─────────────────────────────────────────────────────────────

// ─── Hover Animation System ──────────────────────────────────────────────────

static std::unordered_map<unsigned int, float>& hover_state_map()
{
    static std::unordered_map<unsigned int, float> map;
    return map;
}

float widget_hover_t(unsigned int id)
{
    auto it = hover_state_map().find(id);
    return (it != hover_state_map().end()) ? it->second : 0.0f;
}

void update_widget_hover(unsigned int id, bool hovered)
{
    float  dt = ImGui::GetIO().DeltaTime;
    float& t  = hover_state_map()[id];
    t         = smooth_hover_state(t, dt, hovered);
}

// ─── Focus Ring ──────────────────────────────────────────────────────────────

void draw_focus_ring_if_needed()
{
    // Only show on keyboard navigation, not mouse.
    if (!ImGui::IsItemFocused() || ImGui::IsItemActive())
        return;

    // Check that focus came from keyboard (Tab / arrow keys)
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1])
        return;

    const auto& c      = theme();
    ImVec2      r_min  = ImGui::GetItemRectMin();
    ImVec2      r_max  = ImGui::GetItemRectMax();
    float       offset = tokens::FOCUS_RING_OFFSET;
    float       radius = tokens::RADIUS_MD + offset;

    ImU32 ring_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.focus_ring.r, c.focus_ring.g, c.focus_ring.b, c.focus_ring.a));

    ImGui::GetWindowDrawList()->AddRect(ImVec2(r_min.x - offset, r_min.y - offset),
                                        ImVec2(r_max.x + offset, r_max.y + offset),
                                        ring_col,
                                        radius,
                                        0,
                                        tokens::FOCUS_RING_WIDTH);
}

}   // namespace spectra::ui::widgets

#endif   // SPECTRA_USE_IMGUI
