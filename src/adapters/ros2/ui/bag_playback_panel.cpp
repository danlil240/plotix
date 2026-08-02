// BagPlaybackPanel — implementation.
//
// See bag_playback_panel.hpp for design notes.

#include "bag_playback_panel.hpp"

#include <cmath>
#include <format>
#include <string>

#include "../bag_player.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <ui/animation/timeline_editor.hpp>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BagPlaybackPanel::BagPlaybackPanel(BagPlayer* player)
    : player_(player), rate_slider_(player ? static_cast<float>(player->rate()) : 1.0f)
{
}

// ---------------------------------------------------------------------------
// Player wiring
// ---------------------------------------------------------------------------

void BagPlaybackPanel::set_player(BagPlayer* p)
{
    player_            = p;
    rate_slider_       = p ? static_cast<float>(p->rate()) : 1.0f;
    rate_slider_dirty_ = false;
}

// ---------------------------------------------------------------------------
// Static formatting helpers
// ---------------------------------------------------------------------------

std::string BagPlaybackPanel::format_time(double sec)
{
    if (sec < 0.0)
        sec = 0.0;

    const int total_s = static_cast<int>(sec);
    const int h       = total_s / 3600;
    const int m       = (total_s % 3600) / 60;
    const int s       = total_s % 60;
    const int ds      = static_cast<int>(std::lround((sec - std::floor(sec)) * 10.0)) % 10;

    if (h > 0)
        return std::format("{}:{:02}:{:02}", h, m, s);
    return std::format("{}:{:02}.{}", m, s, ds);
}

std::string BagPlaybackPanel::rate_label(double rate)
{
    // Show one decimal place; trim trailing zero for 1.0×, 2.0×, etc.
    if (std::fabs(rate - std::round(rate)) < 0.05)
        return std::format("{:.0f}×", rate);
    return std::format("{:.1f}×", rate);
}

// ---------------------------------------------------------------------------
// draw() — standalone window
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void BagPlaybackPanel::draw(bool* p_open)
{
    ImGui::SetNextWindowSize(ImVec2(700.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }
    draw_inline();
    ImGui::End();
}

void BagPlaybackPanel::draw_inline()
{
    draw_toolbar();

    if (!player_ || !player_->is_open())
    {
        ImGui::TextDisabled("No bag loaded.");
        return;
    }

    const bool has_timeline = player_->timeline_editor() != nullptr;
    if (has_timeline)
        draw_timeline();
    else
        draw_progress_bar();

    draw_status_line();
}

// ---------------------------------------------------------------------------
// draw_toolbar
// ---------------------------------------------------------------------------

void BagPlaybackPanel::draw_toolbar()
{
    const bool has_player = (player_ != nullptr && player_->is_open());

    // Seek-to-start button.
    if (ImGui::Button("##seek_start"))
    {
        if (has_player)
            player_->seek_begin();
    }
    // Draw |<< icon manually.
    {
        ImDrawList*  dl      = ImGui::GetWindowDrawList();
        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();
        const float  cx      = (btn_min.x + btn_max.x) * 0.5f;
        const float  cy      = (btn_min.y + btn_max.y) * 0.5f;
        const float  r       = (btn_max.y - btn_min.y) * 0.30f;
        const ImU32  col     = ImGui::GetColorU32(ImGuiCol_Text);
        // Two left-pointing triangles side by side.
        dl->AddTriangleFilled({cx - r, cy}, {cx, cy - r}, {cx, cy + r}, col);
        dl->AddTriangleFilled({cx - r * 2.0f, cy}, {cx - r, cy - r}, {cx - r, cy + r}, col);
        dl->AddLine({btn_min.x + 2.0f, btn_min.y + 2.0f},
                    {btn_min.x + 2.0f, btn_max.y - 2.0f},
                    col,
                    2.0f);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Seek to start");

    ImGui::SameLine();

    // Step back button.
    if (ImGui::Button("##step_back"))
    {
        if (has_player)
            player_->step_backward();
    }
    {
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        const float  cx   = (bmin.x + bmax.x) * 0.5f;
        const float  cy   = (bmin.y + bmax.y) * 0.5f;
        const float  r    = (bmax.y - bmin.y) * 0.30f;
        const ImU32  col  = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddTriangleFilled({cx, cy}, {cx + r, cy - r}, {cx + r, cy + r}, col);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Step back");

    ImGui::SameLine();

    // Play / Pause toggle button.
    const bool  playing = has_player && player_->is_playing();
    const char* pp_lbl  = playing ? "##pause" : "##play";
    if (ImGui::Button(pp_lbl))
    {
        if (has_player)
            player_->toggle_play();
    }
    {
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        const float  cx   = (bmin.x + bmax.x) * 0.5f;
        const float  cy   = (bmin.y + bmax.y) * 0.5f;
        const float  r    = (bmax.y - bmin.y) * 0.32f;
        const ImU32  col  = ImGui::GetColorU32(ImGuiCol_Text);
        if (playing)
        {
            // Pause icon (two vertical bars).
            const float bw = r * 0.40f;
            dl->AddRectFilled({cx - r, cy - r}, {cx - r + bw, cy + r}, col);
            dl->AddRectFilled({cx + r - bw, cy - r}, {cx + r, cy + r}, col);
        }
        else
        {
            // Play icon (right-pointing triangle).
            dl->AddTriangleFilled({cx - r, cy - r}, {cx + r, cy}, {cx - r, cy + r}, col);
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(playing ? "Pause" : "Play");

    ImGui::SameLine();

    // Stop button.
    if (ImGui::Button("##stop"))
    {
        if (has_player)
            player_->stop();
    }
    {
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        const float  r    = (bmax.y - bmin.y) * 0.28f;
        const float  cx   = (bmin.x + bmax.x) * 0.5f;
        const float  cy   = (bmin.y + bmax.y) * 0.5f;
        dl->AddRectFilled({cx - r, cy - r}, {cx + r, cy + r}, ImGui::GetColorU32(ImGuiCol_Text));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop");

    ImGui::SameLine();

    // Step forward button.
    if (ImGui::Button("##step_fwd"))
    {
        if (has_player)
            player_->step_forward();
    }
    {
        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        const float  cx   = (bmin.x + bmax.x) * 0.5f;
        const float  cy   = (bmin.y + bmax.y) * 0.5f;
        const float  r    = (bmax.y - bmin.y) * 0.30f;
        const ImU32  col  = ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddTriangleFilled({cx, cy}, {cx - r, cy - r}, {cx - r, cy + r}, col);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Step forward");

    ImGui::SameLine();

    // Time display.
    const double cur_sec = has_player ? player_->playhead_sec() : 0.0;
    const double dur_sec = has_player ? player_->duration_sec() : 0.0;
    ImGui::TextDisabled("%s / %s", format_time(cur_sec).c_str(), format_time(dur_sec).c_str());

    ImGui::SameLine();

    // Rate slider.
    if (show_rate_slider_)
    {
        const double cur_rate = has_player ? player_->rate() : 1.0;
        if (!rate_slider_dirty_)
            rate_slider_ = static_cast<float>(cur_rate);

        ImGui::SetNextItemWidth(90.0f);
        ImGui::PushID("##rate_slider");
        if (ImGui::SliderFloat("##r",
                               &rate_slider_,
                               0.1f,
                               10.0f,
                               rate_label(static_cast<double>(rate_slider_)).c_str(),
                               ImGuiSliderFlags_Logarithmic))
        {
            rate_slider_dirty_ = true;
            if (has_player)
                player_->set_rate(static_cast<double>(rate_slider_));
        }
        if (!ImGui::IsItemActive())
            rate_slider_dirty_ = false;
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback rate (0.1× – 10×)");

        ImGui::SameLine();
    }

    // Loop toggle.
    if (show_loop_button_)
    {
        const bool looping = has_player && player_->loop();
        if (looping)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("Loop"))
        {
            if (has_player)
                player_->set_loop(!looping);
        }
        if (looping)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle loop playback");
    }
}

// ---------------------------------------------------------------------------
// draw_progress_bar — compact horizontal scrub bar (no TimelineEditor).
// ---------------------------------------------------------------------------

void BagPlaybackPanel::draw_progress_bar()
{
    if (!player_ || !player_->is_open())
        return;

    const double dur  = player_->duration_sec();
    const double prog = (dur > 0.0) ? player_->playhead_sec() / dur : 0.0;
    const float  frac = static_cast<float>(std::clamp(prog, 0.0, 1.0));

    ImGui::PushID("##prog_bar");

    const float  bar_h = progress_bar_height_;
    const float  bar_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos   = ImGui::GetCursorScreenPos();

    // Invisible button captures clicks for scrubbing.
    ImGui::InvisibleButton("##scrub", ImVec2(bar_w, bar_h));
    if (ImGui::IsItemActive())
    {
        const float  mx = ImGui::GetIO().MousePos.x - pos.x;
        const double f  = std::clamp(static_cast<double>(mx / bar_w), 0.0, 1.0);
        player_->seek_fraction(f);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scrub to position");

    // Draw bar background.
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const ImVec2 bmin = pos;
    const ImVec2 bmax = {pos.x + bar_w, pos.y + bar_h};

    // Track background.
    dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(ImGuiCol_FrameBg), bar_h * 0.5f);

    // Progress fill.
    if (frac > 0.001f)
    {
        const ImVec2 fill_max = {pos.x + bar_w * frac, bmax.y};
        dl->AddRectFilled(bmin, fill_max, ImGui::GetColorU32(ImGuiCol_PlotHistogram), bar_h * 0.5f);
    }

    // Event-marker ticks: small vertical lines above the bar.
    if (!event_markers_.empty())
    {
        const float  tick_h = bar_h * 0.9f;
        const ImVec2 mouse  = ImGui::GetIO().MousePos;
        for (const auto& m : event_markers_)
        {
            const float  tx  = pos.x + bar_w * std::clamp(m.position, 0.0f, 1.0f);
            const ImVec2 tp0 = {tx, bmin.y};
            const ImVec2 tp1 = {tx, bmin.y + tick_h};
            const ImU32  col = IM_COL32(static_cast<int>(m.color[0] * 255),
                                       static_cast<int>(m.color[1] * 255),
                                       static_cast<int>(m.color[2] * 255),
                                       static_cast<int>(m.color[3] * 255));
            dl->AddLine(tp0, tp1, col, 1.5f);

            // Tooltip on hover near tick.
            if (!m.tooltip.empty())
            {
                const float dist = std::abs(mouse.x - tx);
                if (dist < 4.0f && mouse.y >= bmin.y && mouse.y <= bmax.y)
                    ImGui::SetTooltip("%s", m.tooltip.c_str());
            }
        }
    }

    // Playhead thumb (circle).
    const float thumb_x = pos.x + bar_w * frac;
    const float thumb_y = pos.y + bar_h * 0.5f;
    dl->AddCircleFilled({thumb_x, thumb_y}, bar_h * 0.65f, ImGui::GetColorU32(ImGuiCol_SliderGrab));
    dl->AddCircle({thumb_x, thumb_y},
                  bar_h * 0.65f,
                  ImGui::GetColorU32(ImGuiCol_SliderGrabActive),
                  0,
                  1.5f);

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// draw_timeline — full TimelineEditor scrub view.
// ---------------------------------------------------------------------------

void BagPlaybackPanel::draw_timeline()
{
    if (!player_ || !player_->is_open())
        return;

    spectra::TimelineEditor* te = player_->timeline_editor();
    if (!te)
    {
        draw_progress_bar();
        return;
    }

    const float w = ImGui::GetContentRegionAvail().x;
    te->draw(w, timeline_height_);
}

// ---------------------------------------------------------------------------
// draw_status_line — memory + state indicator.
// ---------------------------------------------------------------------------

void BagPlaybackPanel::draw_status_line()
{
    if (!player_ || !player_->is_open())
        return;

    const auto& meta      = player_->metadata();
    const char* state_str = player_->is_playing()  ? "Playing"
                            : player_->is_paused() ? "Paused"
                                                   : "Stopped";

    const uint64_t injected = player_->total_injected();

    ImGui::Separator();
    ImGui::TextDisabled("%s  |  %.1f s  |  %llu msgs  |  %s",
                        meta.path.empty() ? "(no bag)" : meta.path.c_str(),
                        meta.duration_sec(),
                        static_cast<unsigned long long>(injected),
                        state_str);
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
