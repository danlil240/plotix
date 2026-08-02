#ifdef SPECTRA_USE_IMGUI

    #include "imgui_integration_internal.hpp"

namespace spectra
{

void ImGuiIntegration::build_preview_ui(const std::string& title, Figure* figure)
{
    const auto& theme = theme_mgr_->colors();
    ImDrawList* dl    = ImGui::GetBackgroundDrawList();
    ImVec2      disp  = ImGui::GetIO().DisplaySize;

    float w = disp.x;
    float h = disp.y;

    constexpr float RADIUS = 10.0f;
    constexpr float TB_H   = 28.0f;
    constexpr float PAD    = 8.0f;

    // Card background (fills entire window)
    dl->AddRectFilled(ImVec2(0, 0),
                      ImVec2(w, h),
                      IM_COL32(static_cast<uint8_t>(theme.bg_primary.r * 255),
                               static_cast<uint8_t>(theme.bg_primary.g * 255),
                               static_cast<uint8_t>(theme.bg_primary.b * 255),
                               255),
                      RADIUS);

    // Border
    dl->AddRect(ImVec2(0, 0),
                ImVec2(w, h),
                IM_COL32(static_cast<uint8_t>(theme.accent.r * 255),
                         static_cast<uint8_t>(theme.accent.g * 255),
                         static_cast<uint8_t>(theme.accent.b * 255),
                         180),
                RADIUS,
                0,
                2.0f);

    // Title bar
    dl->AddRectFilled(ImVec2(1, 1),
                      ImVec2(w - 1, TB_H),
                      IM_COL32(static_cast<uint8_t>(theme.bg_tertiary.r * 255),
                               static_cast<uint8_t>(theme.bg_tertiary.g * 255),
                               static_cast<uint8_t>(theme.bg_tertiary.b * 255),
                               255),
                      RADIUS,
                      ImDrawFlags_RoundCornersTop);

    // Title text centered
    ImVec2 tsz = ImGui::CalcTextSize(title.c_str());
    dl->AddText(ImVec2((w - tsz.x) * 0.5f, (TB_H - tsz.y) * 0.5f),
                IM_COL32(static_cast<uint8_t>(theme.text_primary.r * 255),
                         static_cast<uint8_t>(theme.text_primary.g * 255),
                         static_cast<uint8_t>(theme.text_primary.b * 255),
                         255),
                title.c_str());

    // Separator line below title bar
    dl->AddLine(ImVec2(1, TB_H),
                ImVec2(w - 1, TB_H),
                IM_COL32(static_cast<uint8_t>(theme.border_subtle.r * 255),
                         static_cast<uint8_t>(theme.border_subtle.g * 255),
                         static_cast<uint8_t>(theme.border_subtle.b * 255),
                         200),
                1.0f);

    // Plot area
    float px = PAD;
    float py = TB_H + PAD * 0.5f;
    float pw = w - PAD * 2.0f;
    float ph = h - TB_H - PAD * 1.5f;

    if (pw <= 10.0f || ph <= 10.0f)
        return;

    // Plot background
    dl->AddRectFilled(ImVec2(px, py),
                      ImVec2(px + pw, py + ph),
                      IM_COL32(static_cast<uint8_t>(theme.bg_secondary.r * 255),
                               static_cast<uint8_t>(theme.bg_secondary.g * 255),
                               static_cast<uint8_t>(theme.bg_secondary.b * 255),
                               200),
                      4.0f);

    // Grid lines
    uint8_t ga = 30;
    for (int gi = 1; gi < 4; ++gi)
    {
        float gy = py + ph * (static_cast<float>(gi) / 4.0f);
        dl->AddLine(ImVec2(px, gy), ImVec2(px + pw, gy), IM_COL32(128, 128, 128, ga), 1.0f);
    }
    for (int gi = 1; gi < 5; ++gi)
    {
        float gx = px + pw * (static_cast<float>(gi) / 5.0f);
        dl->AddLine(ImVec2(gx, py), ImVec2(gx, py + ph), IM_COL32(128, 128, 128, ga), 1.0f);
    }

    // Render actual figure data if available
    bool drew_real_data = false;
    if (figure && !figure->axes().empty())
    {
        // Use the first (active) axes
        const auto& ax      = *figure->axes()[0];
        AxisLimits  xl      = ax.x_limits();
        AxisLimits  yl      = ax.y_limits();
        double      x_range = xl.max - xl.min;
        double      y_range = yl.max - yl.min;
        if (x_range <= 0.0)
            x_range = 1.0;
        if (y_range <= 0.0)
            y_range = 1.0;

        // Clip to plot area
        dl->PushClipRect(ImVec2(px, py), ImVec2(px + pw, py + ph), true);

        for (const auto& s : ax.series())
        {
            if (!s || !s->visible())
                continue;

            const Color& sc  = s->color();
            ImU32        col = IM_COL32(static_cast<uint8_t>(sc.r * 255),
                                 static_cast<uint8_t>(sc.g * 255),
                                 static_cast<uint8_t>(sc.b * 255),
                                 static_cast<uint8_t>(sc.a * s->opacity() * 220));

            // Try LineSeries
            auto* ls = dynamic_cast<const LineSeries*>(s.get());
            if (ls && ls->point_count() >= 2)
            {
                drew_real_data = true;
                auto   xd      = ls->x_data();
                auto   yd      = ls->y_data();
                size_t n       = ls->point_count();

                // Downsample to fit preview width (max ~200 segments)
                size_t step = std::max<size_t>(1, n / 200);

                for (size_t i = 0; i + step < n; i += step)
                {
                    size_t j   = std::min(i + step, n - 1);
                    float  sx0 = px + ((xd[i] - xl.min) / x_range) * pw;
                    float  sy0 = py + ph - ((yd[i] - yl.min) / y_range) * ph;
                    float  sx1 = px + ((xd[j] - xl.min) / x_range) * pw;
                    float  sy1 = py + ph - ((yd[j] - yl.min) / y_range) * ph;
                    dl->AddLine(ImVec2(sx0, sy0), ImVec2(sx1, sy1), col, 1.5f);
                }
                continue;
            }

            // Try ScatterSeries
            auto* ss = dynamic_cast<const ScatterSeries*>(s.get());
            if (ss && ss->point_count() >= 1)
            {
                drew_real_data = true;
                auto   xd      = ss->x_data();
                auto   yd      = ss->y_data();
                size_t n       = ss->point_count();

                // Downsample for preview
                size_t step = std::max<size_t>(1, n / 150);
                float  r    = std::max(1.5f, std::min(3.0f, pw / 100.0f));

                for (size_t i = 0; i < n; i += step)
                {
                    float sx = px + ((xd[i] - xl.min) / x_range) * pw;
                    float sy = py + ph - ((yd[i] - yl.min) / y_range) * ph;
                    dl->AddCircleFilled(ImVec2(sx, sy), r, col);
                }
            }
        }

        dl->PopClipRect();
    }

    // Fallback: generic sine wave if no real data
    if (!drew_real_data)
    {
        auto          ar       = static_cast<uint8_t>(theme.accent.r * 255);
        auto          ag       = static_cast<uint8_t>(theme.accent.g * 255);
        auto          ab       = static_cast<uint8_t>(theme.accent.b * 255);
        ImU32         wave_col = IM_COL32(ar, ag, ab, 200);
        constexpr int SEGMENTS = 40;
        for (int si = 0; si < SEGMENTS; ++si)
        {
            float t0 = static_cast<float>(si) / SEGMENTS;
            float t1 = static_cast<float>(si + 1) / SEGMENTS;
            float y0 = py + ph * 0.5f - std::sin(t0 * 6.28f) * ph * 0.3f;
            float y1 = py + ph * 0.5f - std::sin(t1 * 6.28f) * ph * 0.3f;
            dl->AddLine(ImVec2(px + t0 * pw, y0), ImVec2(px + t1 * pw, y1), wave_col, 2.0f);
        }
    }
}

// ─── Knobs Panel ────────────────────────────────────────────────────────────
// Draws an overlay panel on the canvas with interactive parameter controls.
// Positioned at top-right of the canvas area, semi-transparent background.

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
