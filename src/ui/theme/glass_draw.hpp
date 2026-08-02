#pragma once

#include "glass_tokens.hpp"
#include "theme.hpp"

struct ImDrawList;
struct ImVec2;

namespace spectra::ui::glass_draw
{

inline ImU32 color_u32(const Color& c)
{
    return IM_COL32(static_cast<int>(c.r * 255.0f),
                    static_cast<int>(c.g * 255.0f),
                    static_cast<int>(c.b * 255.0f),
                    static_cast<int>(c.a * 255.0f));
}

// Ambient glass backdrop. Night uses a deep aurora wash; Dark uses graphite
// frost; Light uses a pale cyan/indigo frost.
inline void draw_ambient_gradient(ImDrawList*        dl,
                                  ImVec2             p0,
                                  ImVec2             p1,
                                  const ThemeColors& colors,
                                  float              glow_strength)
{
    const float g = std::clamp(glow_strength, 0.0f, 1.0f);
    if (g <= 0.01f)
    {
        dl->AddRectFilled(p0, p1, color_u32(colors.bg_secondary));
        return;
    }

    const bool light_surface = colors.bg_primary.luminance() > 0.50f;
    const bool night_surface = colors.bg_primary.b > colors.bg_primary.r * 1.30f
                               && colors.bg_primary.b > colors.bg_primary.g * 1.15f;
    if (light_surface)
    {
        const Color top_left     = colors.bg_primary.lerp(colors.bg_secondary, 0.42f);
        const Color top_right    = colors.bg_elevated.lerp(colors.accent, 0.08f);
        const Color bottom_right = colors.bg_secondary.lerp(Color::from_hex(0xC8D6E5), 0.45f);
        const Color bottom_left  = colors.bg_primary.lerp(Color::from_hex(0xCCDCE8), 0.55f);
        dl->AddRectFilledMultiColor(p0,
                                    p1,
                                    color_u32(top_left),
                                    color_u32(top_right),
                                    color_u32(bottom_right),
                                    color_u32(bottom_left));

        const int   teal_a   = static_cast<int>(22.0f + 34.0f * g);
        const int   indigo_a = static_cast<int>(14.0f + 26.0f * g);
        const Color teal     = colors.accent.lerp(Color::from_hex(0x67E8F9), 0.32f);
        const Color indigo   = colors.info.lerp(Color::from_hex(0xA78BFA), 0.40f);
        dl->AddRectFilledMultiColor(
            p0,
            ImVec2(p0.x + (p1.x - p0.x) * 0.42f, p1.y),
            color_u32(teal.with_alpha(static_cast<float>(teal_a) / 255.0f)),
            IM_COL32(255, 255, 255, 0),
            IM_COL32(255, 255, 255, 0),
            color_u32(teal.with_alpha(static_cast<float>(teal_a / 2) / 255.0f)));
        dl->AddRectFilledMultiColor(
            ImVec2(p0.x + (p1.x - p0.x) * 0.55f, p0.y),
            p1,
            IM_COL32(255, 255, 255, 0),
            color_u32(indigo.with_alpha(static_cast<float>(indigo_a) / 255.0f)),
            color_u32(indigo.with_alpha(static_cast<float>(indigo_a / 2) / 255.0f)),
            IM_COL32(255, 255, 255, 0));
        return;
    }

    if (!night_surface)
    {
        const Color top_left     = colors.bg_primary.lerp(colors.bg_secondary, 0.35f);
        const Color top_right    = colors.bg_secondary.lerp(colors.accent, 0.08f);
        const Color bottom_right = colors.bg_primary.lerp(colors.bg_elevated, 0.22f);
        const Color bottom_left  = colors.bg_primary.lerp(Color::from_hex(0x0E1216), 0.35f);
        dl->AddRectFilledMultiColor(p0,
                                    p1,
                                    color_u32(top_left),
                                    color_u32(top_right),
                                    color_u32(bottom_right),
                                    color_u32(bottom_left));

        const int   cool_a  = static_cast<int>(14.0f + 24.0f * g);
        const int   slate_a = static_cast<int>(10.0f + 18.0f * g);
        const Color cool    = colors.accent.lerp(Color::from_hex(0x7DD3FC), 0.24f);
        const Color slate   = colors.border_strong.lerp(Color::from_hex(0xA5B4FC), 0.18f);
        dl->AddRectFilledMultiColor(
            p0,
            ImVec2(p0.x + (p1.x - p0.x) * 0.40f, p1.y),
            color_u32(cool.with_alpha(static_cast<float>(cool_a) / 255.0f)),
            IM_COL32(0, 0, 0, 0),
            IM_COL32(0, 0, 0, 0),
            color_u32(cool.with_alpha(static_cast<float>(cool_a / 2) / 255.0f)));
        dl->AddRectFilledMultiColor(
            ImVec2(p0.x + (p1.x - p0.x) * 0.62f, p0.y),
            p1,
            IM_COL32(0, 0, 0, 0),
            color_u32(slate.with_alpha(static_cast<float>(slate_a) / 255.0f)),
            color_u32(slate.with_alpha(static_cast<float>(slate_a / 2) / 255.0f)),
            IM_COL32(0, 0, 0, 0));
        return;
    }

    dl->AddRectFilledMultiColor(p0,
                                p1,
                                IM_COL32(11, 22, 42, 255),
                                IM_COL32(28, 16, 48, 255),
                                IM_COL32(32, 14, 44, 255),
                                IM_COL32(10, 20, 40, 255));
    const int cyan_a = static_cast<int>(18.0f + 32.0f * g);
    const int mag_a  = static_cast<int>(16.0f + 28.0f * g);
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p0.x + (p1.x - p0.x) * 0.35f, p1.y),
                                IM_COL32(40, 170, 240, cyan_a),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(40, 170, 240, cyan_a / 2));
    dl->AddRectFilledMultiColor(ImVec2(p0.x + (p1.x - p0.x) * 0.65f, p0.y),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(210, 70, 220, mag_a),
                                IM_COL32(210, 70, 220, mag_a / 2),
                                IM_COL32(0, 0, 0, 0));
}

// Vision.png outer window halo — drawn once behind all chrome.
inline void draw_window_edge_glow(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float glow_strength)
{
    const float g = std::clamp(glow_strength, 0.0f, 1.0f);
    if (g <= 0.02f)
        return;
    for (int i = 4; i >= 1; --i)
    {
        float expand = static_cast<float>(i) * 2.0f;
        int   a      = static_cast<int>((8.0f + 14.0f * g) * static_cast<float>(i));
        dl->AddRect(ImVec2(p0.x - expand, p0.y - expand),
                    ImVec2(p1.x + expand, p1.y + expand),
                    IM_COL32(40, 160, 230, a),
                    14.0f + expand,
                    0,
                    1.5f);
        dl->AddRect(ImVec2(p0.x - expand * 0.5f, p0.y - expand * 0.5f),
                    ImVec2(p1.x + expand * 0.5f, p1.y + expand * 0.5f),
                    IM_COL32(200, 70, 210, a / 2),
                    12.0f + expand * 0.5f,
                    0,
                    1.0f);
    }
}

// Frosted glass panel: gradient base + translucent tint + layered highlights,
// inner bottom shadow, and dual hairline border for real depth.
inline void draw_glass_rect(ImDrawList*               dl,
                            ImVec2                    p0,
                            ImVec2                    p1,
                            float                     rounding,
                            const ThemeColors&        colors,
                            const ThemeGlassSettings& glass,
                            GlassSurface              surface,
                            float                     glow_strength)
{
    const float glow   = std::clamp(glow_strength, 0.0f, 1.0f);
    const float master = std::clamp(glass.master_intensity, 0.0f, 1.0f);
    const float alpha  = glow > 0.01f ? glass_surface_alpha_value(glass, surface) : 1.0f;
    const float h      = p1.y - p0.y;

    draw_ambient_gradient(dl, p0, p1, colors, glow);

    Color frost = colors.bg_secondary.lerp(colors.bg_elevated, 0.35f + master * 0.15f);
    if (glow > 0.01f)
        frost = frost.lerp(glass_palette::kAccentCyan, master * 0.06f * glow);
    dl->AddRectFilled(p0, p1, color_u32(frost.with_alpha(alpha)), rounding);

    // Vertical sheen — brighter at top, settling toward the bottom (glass curvature).
    if (glow > 0.01f)
    {
        const int sheen_a = static_cast<int>((20.0f + 14.0f * master) * glow);
        dl->AddRectFilledMultiColor(p0,
                                    ImVec2(p1.x, p0.y + h * 0.5f),
                                    IM_COL32(210, 232, 255, sheen_a),
                                    IM_COL32(210, 232, 255, sheen_a),
                                    IM_COL32(255, 255, 255, 0),
                                    IM_COL32(255, 255, 255, 0));
    }
    // Top inner highlight (glass edge catch-light)
    if (glow > 0.01f)
    {
        const int hi_a = static_cast<int>((30.0f + 26.0f * (1.0f - alpha)) * glow);
        dl->AddLine(ImVec2(p0.x + rounding * 0.5f, p0.y + 1.0f),
                    ImVec2(p1.x - rounding * 0.5f, p0.y + 1.0f),
                    IM_COL32(225, 244, 255, hi_a),
                    1.0f);
    }

    // Inner bottom shadow — grounds the panel (night glass only).
    if (glow > 0.01f)
    {
        dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + h * 0.6f),
                                    p1,
                                    IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, static_cast<int>(26.0f + 20.0f * master)),
                                    IM_COL32(0, 0, 0, static_cast<int>(26.0f + 20.0f * master)));
    }

    // Dual hairline: inner dark seat + outer border.
    if (glow > 0.01f)
    {
        dl->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f),
                    ImVec2(p1.x - 1.0f, p1.y - 1.0f),
                    IM_COL32(0, 0, 0, 50),
                    std::max(0.0f, rounding - 1.0f),
                    0,
                    1.0f);
    }
    Color     border   = glow > 0.01f ? colors.accent.lerp(glass_palette::kAccentMagenta, 0.30f)
                                      : colors.border_subtle;
    const int border_a = glow > 0.01f
                             ? static_cast<int>((52.0f + 90.0f * glow) * (0.40f + master * 0.60f))
                             : static_cast<int>(colors.border_subtle.a * 255.0f);
    dl->AddRect(p0,
                p1,
                color_u32(border.with_alpha(static_cast<float>(border_a) / 255.0f)),
                rounding,
                0,
                1.0f);
    if (glow > 0.08f)
    {
        for (int i = 1; i <= 2; ++i)
        {
            float e = static_cast<float>(i);
            dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                        ImVec2(p1.x + e, p1.y + e),
                        color_u32(border.with_alpha(glow * (0.10f / e) * (0.5f + master * 0.5f))),
                        rounding + e,
                        0,
                        1.5f);
        }
    }
}

// Translucent glass card (legend, tooltip, popups). Caller supplies the rect;
// drop shadow + frosted fill + top highlight + hairline are drawn here so the
// element reads as a floating pane rather than an opaque gray block.
inline void draw_glass_card(ImDrawList*        dl,
                            ImVec2             p0,
                            ImVec2             p1,
                            float              rounding,
                            const ThemeColors& colors,
                            float              fill_alpha,
                            float              glow_strength)
{
    const float glow = std::clamp(glow_strength, 0.0f, 1.0f);
    const float h    = p1.y - p0.y;

    // Multi-layer soft drop shadow.
    for (int i = 4; i >= 1; --i)
    {
        float off = static_cast<float>(i) * 2.0f;
        int   a   = static_cast<int>(14.0f * static_cast<float>(i) / 4.0f);
        dl->AddRectFilled(ImVec2(p0.x - off * 0.4f, p0.y + off * 0.5f),
                          ImVec2(p1.x + off * 0.4f, p1.y + off),
                          IM_COL32(0, 0, 0, a),
                          rounding + off * 0.4f);
    }

    Color frost = colors.bg_elevated.lerp(glass_palette::kAccentCyan, 0.05f);
    dl->AddRectFilled(p0,
                      p1,
                      color_u32(frost.with_alpha(std::clamp(fill_alpha, 0.05f, 1.0f))),
                      rounding);
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p1.x, p0.y + h * 0.5f),
                                IM_COL32(220, 240, 255, 26),
                                IM_COL32(220, 240, 255, 26),
                                IM_COL32(255, 255, 255, 0),
                                IM_COL32(255, 255, 255, 0));
    dl->AddLine(ImVec2(p0.x + rounding * 0.6f, p0.y + 1.0f),
                ImVec2(p1.x - rounding * 0.6f, p0.y + 1.0f),
                IM_COL32(230, 246, 255, 60),
                1.0f);
    Color border = colors.accent.lerp(glass_palette::kAccentMagenta, 0.25f);
    dl->AddRect(p0, p1, color_u32(border.with_alpha(0.30f + glow * 0.25f)), rounding, 0, 1.0f);
}

// Premium floating app-shell frame: luminous outer border + soft inner edge
// vignette glow. Strokes only — never fills the interior (would hide the plot).
// Master intensity scales the effect so dark/light themes stay subtle.
inline void draw_app_shell_frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float glow_strength)
{
    const float g      = std::clamp(glow_strength, 0.0f, 1.0f);
    const float radius = 14.0f;

    if (g <= 0.02f)
        return;   // No glow budget for non-glass themes.

    // Outer luminous border (cyan→violet) with a faint halo — kept restrained.
    for (int i = 3; i >= 1; --i)
    {
        float e = static_cast<float>(i);
        int   a = static_cast<int>((6.0f + 9.0f * g) * (4 - i) / 3.0f);
        dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                    ImVec2(p1.x + e, p1.y + e),
                    IM_COL32(60, 170, 235, a),
                    radius + e,
                    0,
                    1.5f);
    }
    dl->AddRect(p0,
                p1,
                IM_COL32(120, 190, 245, static_cast<int>(34.0f + 38.0f * g)),
                radius,
                0,
                1.25f);

    // Inner edge vignette — soft dark falloff hugging the window border only.
    const float band = 26.0f;
    int         va   = static_cast<int>(26.0f + 20.0f * g);
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p1.x, p0.y + band),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - band),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, va));
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p0.x + band, p1.y),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, va));
    dl->AddRectFilledMultiColor(ImVec2(p1.x - band, p0.y),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, 0));
}

// Refined canvas frame — subtle gradient glow + crisp inner border.
// The plot is the visual hero; the frame should elevate it without competing.
inline void draw_vision_canvas_frame(ImDrawList*        dl,
                                     ImVec2             p0,
                                     ImVec2             p1,
                                     float              rounding,
                                     const ThemeColors& colors,
                                     float              glow_strength,
                                     float              master_intensity)
{
    const float glow   = std::clamp(glow_strength, 0.0f, 1.0f);
    const float master = std::clamp(master_intensity, 0.0f, 1.0f);
    const float inset  = ui::tokens::CANVAS_FRAME_INSET;

    // All four corners are rounded so the canvas reads as a single cohesive
    // panel. The plot is rendered (square) by Vulkan underneath, so the top
    // corners are masked below with the chrome backdrop color — that hides the
    // square plot pixels and lets the rounded corner flow into the bar above.
    const ImDrawFlags corner_flags = ImDrawFlags_RoundCornersAll;

    // Chrome backdrop that sits directly above the canvas (the figure tab bar
    // glass). Computed exactly like draw_glass_rect()'s frost so the masked top
    // corners blend seamlessly with the color on top.
    Color chrome = colors.bg_secondary.lerp(colors.bg_elevated, 0.35f + master * 0.15f);
    if (glow > 0.01f)
        chrome = chrome.lerp(glass_palette::kAccentCyan, master * 0.06f * glow);
    const ImU32 chrome_col = color_u32(chrome);

    // Mask the two square top corners with the chrome color. Each fill covers
    // the wedge between the square corner and the rounded arc, fanned from the
    // corner vertex (star-shaped → safe for PathFillConvex).
    if (rounding > 0.5f)
    {
        constexpr float kPi = 3.14159265358979f;
        // Top-left.
        dl->PathLineTo(ImVec2(p0.x, p0.y));
        dl->PathLineTo(ImVec2(p0.x + rounding, p0.y));
        dl->PathArcTo(ImVec2(p0.x + rounding, p0.y + rounding), rounding, kPi * 1.5f, kPi, 16);
        dl->PathFillConvex(chrome_col);
        // Top-right.
        dl->PathLineTo(ImVec2(p1.x, p0.y));
        dl->PathLineTo(ImVec2(p1.x - rounding, p0.y));
        dl->PathArcTo(ImVec2(p1.x - rounding, p0.y + rounding),
                      rounding,
                      kPi * 1.5f,
                      kPi * 2.0f,
                      16);
        dl->PathFillConvex(chrome_col);
    }

    // Soft shadow drop (grounds the canvas).
    for (int i = 3; i >= 1; --i)
    {
        float expand = static_cast<float>(i) * 1.5f;
        int   a      = static_cast<int>((6.0f + 5.0f * glow) * static_cast<float>(i) / 3.0f);
        dl->AddRect(ImVec2(p0.x - expand, p0.y - expand),
                    ImVec2(p1.x + expand, p1.y + expand),
                    IM_COL32(0, 0, 0, a),
                    rounding + expand,
                    corner_flags);
    }

    Color cyan_edge = colors.accent.lerp(glass_palette::kAccentCyan, 0.35f);
    Color mag_edge  = colors.accent.lerp(glass_palette::kAccentMagenta, 0.35f);
    Color frame_col = glow > 0.01f ? cyan_edge.lerp(mag_edge, 0.45f) : colors.border_default;

    // Single restrained outer glow halo — only visible when glow is high.
    if (glow > 0.05f)
    {
        for (int i = 2; i >= 1; --i)
        {
            float e = static_cast<float>(i) * 1.25f;
            int   a = static_cast<int>(ui::tokens::CANVAS_FRAME_GLOW_ALPHA * 255.0f * glow * master
                                     * (3 - i) / 2.0f);
            dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                        ImVec2(p1.x + e, p1.y + e),
                        color_u32(frame_col.with_alpha(static_cast<float>(a) / 255.0f)),
                        rounding + e,
                        corner_flags,
                        1.25f);
        }
    }

    // Primary border — crisp but quiet.
    const float edge_alpha = ui::tokens::CANVAS_FRAME_BORDER_ALPHA + 0.18f * glow * master;
    dl->AddRect(p0, p1, color_u32(frame_col.with_alpha(edge_alpha)), rounding, corner_flags, 1.25f);

    // Inner rim — gives the plot a recessed, intentional feel.
    dl->AddRect(ImVec2(p0.x + inset, p0.y + inset),
                ImVec2(p1.x - inset, p1.y - inset),
                color_u32(colors.border_subtle.with_alpha(ui::tokens::CANVAS_FRAME_INNER_ALPHA
                                                          + 0.18f * glow)),
                std::max(0.0f, rounding - inset),
                corner_flags,
                1.0f);

    // Subtle inner shadow / vignette to integrate the canvas with the chrome.
    const float vignette_band = std::min(24.0f, (p1.y - p0.y) * 0.10f);
    const int   vignette_a    = static_cast<int>(ui::tokens::CANVAS_VIGNETTE_ALPHA * 255.0f
                                            * (0.7f + 0.3f * glow * master));
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p1.x, p0.y + vignette_band),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - vignette_band),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, vignette_a));
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p0.x + vignette_band, p1.y),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, vignette_a));
    dl->AddRectFilledMultiColor(ImVec2(p1.x - vignette_band, p0.y),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, vignette_a),
                                IM_COL32(0, 0, 0, 0));

    // Top edge catch-light — inset by the corner radius so it hugs the rounded
    // top edge instead of bleeding past the curve.
    dl->AddLine(ImVec2(p0.x + rounding, p0.y + 1.0f),
                ImVec2(p1.x - rounding, p0.y + 1.0f),
                color_u32(cyan_edge.with_alpha(0.18f + 0.14f * glow)),
                1.0f);
}

}   // namespace spectra::ui::glass_draw
