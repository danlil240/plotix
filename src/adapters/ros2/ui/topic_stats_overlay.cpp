#include "ui/topic_stats_overlay.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <cstring>
#include <numeric>

#include "subplot_manager.hpp"

#include <spectra/color.hpp>
#include <spectra/series.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "ui/imgui/widgets.hpp"
    #include "ui/shell/shell_style.hpp"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t wall_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// TopicDetailStats
// ---------------------------------------------------------------------------

void TopicDetailStats::push(int64_t arrival_ns, size_t bytes, int64_t latency_us)
{
    // Drop detection: compute gap before inserting.
    if (last_msg_ns > 0)
    {
        last_gap_ns = arrival_ns - last_msg_ns;
    }
    else
    {
        last_gap_ns = 0;
    }

    samples.push_back({arrival_ns, bytes, latency_us});
    last_msg_ns = arrival_ns;
    ++total_messages;
    total_bytes += bytes;
}

void TopicDetailStats::compute(int64_t now_ns, int64_t window_ns)
{
    const int64_t cutoff = now_ns - window_ns;

    // Prune old samples.
    while (!samples.empty() && samples.front().arrival_ns < cutoff)
    {
        samples.pop_front();
    }

    const size_t n = samples.size();

    // --- Hz ---
    if (n < 2)
    {
        hz_avg = (n == 1) ? 1.0 : 0.0;
        hz_min = hz_avg;
        hz_max = hz_avg;
    }
    else
    {
        // Span-based average Hz.
        const double span_s =
            static_cast<double>(samples.back().arrival_ns - samples.front().arrival_ns) * 1e-9;
        hz_avg = (span_s > 0.0) ? static_cast<double>(n - 1) / span_s : 0.0;

        // Instantaneous Hz between consecutive pairs.
        double inst_min = 1e18;
        double inst_max = 0.0;
        for (size_t i = 1; i < n; ++i)
        {
            const double gap_s =
                static_cast<double>(samples[i].arrival_ns - samples[i - 1].arrival_ns) * 1e-9;
            if (gap_s > 0.0)
            {
                const double inst = 1.0 / gap_s;
                if (inst < inst_min)
                    inst_min = inst;
                if (inst > inst_max)
                    inst_max = inst;
            }
        }
        hz_min = (inst_min < 1e17) ? inst_min : 0.0;
        hz_max = inst_max;
    }

    // --- Bandwidth ---
    if (n < 1)
    {
        bw_bps = 0.0;
    }
    else
    {
        uint64_t total = 0;
        for (const auto& s : samples)
            total += s.bytes;
        const double win_s = static_cast<double>(window_ns) * 1e-9;
        bw_bps             = static_cast<double>(total) / win_s;
    }

    // --- Latency (only samples with valid latency) ---
    {
        double sum   = 0.0;
        double lmin  = 1e18;
        double lmax  = -1e18;
        int    count = 0;
        for (const auto& s : samples)
        {
            if (s.latency_us >= 0)
            {
                const auto v = static_cast<double>(s.latency_us);
                sum += v;
                if (v < lmin)
                    lmin = v;
                if (v > lmax)
                    lmax = v;
                ++count;
            }
        }
        if (count > 0)
        {
            latency_avg_us = sum / count;
            latency_min_us = lmin;
            latency_max_us = lmax;
        }
        else
        {
            latency_avg_us = -1.0;
            latency_min_us = -1.0;
            latency_max_us = -1.0;
        }
    }

    // --- Drop detection ---
    // Reset drop flag; TopicStatsOverlay::compute_now() applies the actual threshold.
    drop_detected = false;
}

void TopicDetailStats::reset_window()
{
    samples.clear();
    hz_avg         = 0.0;
    hz_min         = 0.0;
    hz_max         = 0.0;
    bw_bps         = 0.0;
    latency_avg_us = -1.0;
    latency_min_us = -1.0;
    latency_max_us = -1.0;
    drop_detected  = false;
    last_gap_ns    = 0;
    last_msg_ns    = 0;
}

// ---------------------------------------------------------------------------
// TopicStatsOverlay
// ---------------------------------------------------------------------------

TopicStatsOverlay::TopicStatsOverlay() = default;

void TopicStatsOverlay::set_topic(const std::string& topic_name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (topic_name == current_topic_)
        return;
    current_topic_ = topic_name;
    stats_.reset_window();
    // Keep cumulative counters from previous topic? No — new topic, fresh slate.
    stats_.total_messages = 0;
    stats_.total_bytes    = 0;
}

const std::string& TopicStatsOverlay::topic() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return current_topic_;
}

void TopicStatsOverlay::notify_message(const std::string& topic_name,
                                       size_t             bytes,
                                       int64_t            latency_us)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (topic_name != current_topic_ || current_topic_.empty())
        return;
    stats_.push(wall_ns(), bytes, latency_us);
}

void TopicStatsOverlay::reset_stats()
{
    std::lock_guard<std::mutex> lk(mutex_);
    stats_.reset_window();
    stats_.total_messages = 0;
    stats_.total_bytes    = 0;
}

void TopicStatsOverlay::set_window_ms(int ms)
{
    window_ms_ = (ms > 0) ? ms : 100;
}

void TopicStatsOverlay::set_drop_factor(double factor)
{
    drop_factor_ = (factor > 1.0) ? factor : 2.0;
}

void TopicStatsOverlay::compute_now(int64_t now_ns_val)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const int64_t               window_ns = static_cast<int64_t>(window_ms_) * 1'000'000LL;
    stats_.compute(now_ns_val, window_ns);

    // Apply drop detection using our factor.
    if (stats_.hz_avg > 0.0 && stats_.last_gap_ns > 0)
    {
        const double expected_ns = 1e9 / stats_.hz_avg;
        stats_.drop_detected = static_cast<double>(stats_.last_gap_ns) > drop_factor_ * expected_ns;
    }
    else
    {
        stats_.drop_detected = false;
    }
}

TopicStatsOverlay::StatsSnapshot TopicStatsOverlay::snapshot()
{
    const int64_t               window_ns = static_cast<int64_t>(window_ms_) * 1'000'000LL;
    std::lock_guard<std::mutex> lk(mutex_);
    stats_.compute(wall_ns(), window_ns);
    if (stats_.hz_avg > 0.0 && stats_.last_gap_ns > 0)
    {
        const double expected_ns = 1e9 / stats_.hz_avg;
        stats_.drop_detected = static_cast<double>(stats_.last_gap_ns) > drop_factor_ * expected_ns;
    }
    else
    {
        stats_.drop_detected = false;
    }

    StatsSnapshot snap;
    snap.topic          = current_topic_;
    snap.hz_avg         = stats_.hz_avg;
    snap.hz_min         = stats_.hz_min;
    snap.hz_max         = stats_.hz_max;
    snap.bw_bps         = stats_.bw_bps;
    snap.latency_avg_us = stats_.latency_avg_us;
    snap.latency_min_us = stats_.latency_min_us;
    snap.latency_max_us = stats_.latency_max_us;
    snap.total_messages = stats_.total_messages;
    snap.total_bytes    = stats_.total_bytes;
    snap.drop_detected  = stats_.drop_detected;
    snap.last_gap_ns    = stats_.last_gap_ns;
    snap.last_msg_ns    = stats_.last_msg_ns;
    return snap;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

std::string TopicStatsOverlay::format_hz(double hz)
{
    if (hz <= 0.0)
        return "-";
    return std::format("{:.1f}", hz);
}

std::string TopicStatsOverlay::format_bw(double bps)
{
    if (bps <= 0.0)
        return "-";
    if (bps >= 1024.0 * 1024.0)
        return std::format("{:.2f} MB/s", bps / (1024.0 * 1024.0));
    if (bps >= 1024.0)
        return std::format("{:.1f} KB/s", bps / 1024.0);
    return std::format("{:.0f} B/s", bps);
}

std::string TopicStatsOverlay::format_bytes(uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return std::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    if (bytes >= 1024ULL * 1024ULL)
        return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024ULL)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{} B", bytes);
}

std::string TopicStatsOverlay::format_latency(double us)
{
    if (us < 0.0)
        return "-";
    if (us >= 1000.0)
        return std::format("{:.2f} ms", us / 1000.0);
    return std::format("{:.1f} µs", us);
}

int64_t TopicStatsOverlay::now_ns()
{
    return wall_ns();
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void TopicStatsOverlay::draw_stat_row(const char* label, const std::string& value, bool highlight)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    if (highlight)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextUnformatted(value.c_str());
    }
}

void TopicStatsOverlay::draw_inline()
{
    if (!ImGui::GetCurrentContext())
        return;
    // Throttle updates to ~4 Hz to reduce visual noise in Hz / BW values.
    const int64_t now = now_ns();
    if (now - last_snap_ns_ >= SNAP_INTERVAL_NS || last_snap_ns_ == 0)
    {
        cached_snap_  = snapshot();
        last_snap_ns_ = now;
    }
    const StatsSnapshot& snap = cached_snap_;

    if (snap.topic.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, std::max(8.0f, ImGui::GetContentRegionAvail().y * 0.18f)));
        const spectra::ui::widgets::EmptyStateAction actions[] = {
            {.label = "Browse topics", .id = "browse", .primary = true},
            {.label = "Open echo", .id = "echo", .primary = false},
        };
        const int action = spectra::ui::widgets::empty_state(
            spectra::ui::Icon::BarChart,
            "Select a topic in Topic Monitor",
            "Inspect frequency, bandwidth, latency, and plotted series.",
            actions);
        if (action == 1 && echo_cb_)
            echo_cb_("");
        return;
    }

    // Topic name header.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted(snap.topic.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (plot_cb_)
    {
        if (spectra::ui::widgets::toolbar_button(spectra::ui::Icon::ChartLine,
                                                 "Plot numeric fields"))
            plot_cb_(snap.topic);
        ImGui::SameLine();
    }
    if (echo_cb_)
    {
        if (spectra::ui::widgets::toolbar_button(spectra::ui::Icon::Command, "Echo topic"))
            echo_cb_(snap.topic);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Copy name"))
        ImGui::SetClipboardText(snap.topic.c_str());
    if (favorite_cb_)
    {
        ImGui::SameLine();
        if (spectra::ui::widgets::toolbar_button(spectra::ui::Icon::Star, "Toggle favorite"))
            favorite_cb_(snap.topic);
    }
    ImGui::Spacing();

    // Drop warning banner.
    if (snap.drop_detected)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted("  Drop detected!");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            const std::string tip =
                std::format("Last inter-message gap: {:.1f} ms  (> {:.0f}× expected)",
                            static_cast<double>(snap.last_gap_ns) * 1e-6,
                            drop_factor_);
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::Separator();
    }

    auto age_text = [&]() -> std::string
    {
        if (snap.last_msg_ns <= 0)
            return "-";
        const double age_s = std::max(0.0, static_cast<double>(now - snap.last_msg_ns) * 1e-9);
        if (age_s < 1.0)
            return std::format("{:.0f}", age_s * 1000.0);
        if (age_s < 60.0)
            return std::format("{:.1f}", age_s);
        return std::format("{:.0f}", age_s / 60.0);
    };
    auto age_unit = [&]() -> const char*
    {
        if (snap.last_msg_ns <= 0)
            return "";
        const double age_s = std::max(0.0, static_cast<double>(now - snap.last_msg_ns) * 1e-9);
        if (age_s < 1.0)
            return "ms ago";
        if (age_s < 60.0)
            return "s ago";
        return "min ago";
    };

    if (ImGui::BeginTable("##stats_cards", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card("Average Hz", format_hz(snap.hz_avg).c_str(), "Hz");
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card("Bandwidth", format_bw(snap.bw_bps).c_str(), nullptr);
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card("Messages",
                                        std::format("{}", snap.total_messages).c_str(),
                                        nullptr);
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card("Total bytes",
                                        format_bytes(snap.total_bytes).c_str(),
                                        nullptr);
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card(
            "Latency",
            snap.latency_avg_us >= 0.0 ? format_latency(snap.latency_avg_us).c_str() : "-",
            snap.latency_avg_us >= 0.0 ? "avg" : "no header");
        ImGui::TableNextColumn();
        spectra::ui::widgets::stat_card("Last message", age_text().c_str(), age_unit());
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (spectra::ui::widgets::begin_card("##message_structure_preview"))
    {
        ImGui::TextUnformatted("Message structure");
        ImGui::Separator();
        ImGui::TextWrapped("Use inline echo in Topic Monitor to inspect fields and drag numeric "
                           "paths onto plots.");
        const std::string hz_range =
            std::format("Hz range: {} - {}", format_hz(snap.hz_min), format_hz(snap.hz_max));
        ImGui::TextDisabled("%s", hz_range.c_str());
        const std::string gap_str =
            snap.last_gap_ns > 0
                ? std::format("Last gap: {:.1f} ms", static_cast<double>(snap.last_gap_ns) * 1e-6)
                : "Last gap: -";
        ImGui::TextDisabled("%s", gap_str.c_str());
    }
    spectra::ui::widgets::end_card();
}

void TopicStatsOverlay::draw(bool* p_open, int active_subplot_slot)
{
    if (!ImGui::GetCurrentContext())
        return;
    if (!spectra::ui::shell::begin_panel(title_.c_str(), p_open))
    {
        spectra::ui::shell::end_panel();
        return;
    }
    draw_inline();
    draw_series_controls(subplot_mgr_, active_subplot_slot);
    spectra::ui::shell::end_panel();
}
void TopicStatsOverlay::draw_series_controls(SubplotManager* subplot_mgr, int active_slot)
{
    if (!subplot_mgr || active_slot < 1)
        return;

    const StatsSnapshot snap = snapshot();
    if (snap.topic.empty())
        return;

    const int n_series = subplot_mgr->slot_series_count(active_slot);
    if (n_series <= 0)
        return;

    bool any_for_topic = false;
    for (int i = 0; i < n_series; ++i)
    {
        const SeriesEntry* entry = subplot_mgr->slot_series(active_slot, i);
        if (entry && entry->topic == snap.topic)
        {
            any_for_topic = true;
            break;
        }
    }
    if (!any_for_topic)
        return;

    ImGui::Separator();
    ImGui::TextDisabled("Series on plot %d", active_slot);

    for (int i = 0; i < n_series; ++i)
    {
        const SeriesEntry* entry = subplot_mgr->slot_series(active_slot, i);
        if (!entry || entry->topic != snap.topic || !entry->series)
            continue;

        ImGui::PushID(i);
        auto&                series  = *entry->series;
        const spectra::Color c       = series.color();
        float                rgba[4] = {c.r, c.g, c.b, c.a};
        if (ImGui::ColorEdit4("##color", rgba, ImGuiColorEditFlags_NoInputs))
            series.set_color({rgba[0], rgba[1], rgba[2], rgba[3]});

        ImGui::SameLine();
        ImGui::TextUnformatted(entry->field_path.c_str());

        ImGui::SameLine();
        bool visible = series.visible();
        if (ImGui::Checkbox("##vis", &visible))
            series.visible(visible);

        ImGui::SameLine();
        float width = series.width();
        ImGui::SetNextItemWidth(60.0f);
        if (ImGui::SliderFloat("##width", &width, 0.5f, 6.0f, "%.1f"))
            series.width(width);

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            subplot_mgr->remove_series_from_slot(active_slot, entry->topic, entry->field_path);

        ImGui::PopID();
    }
}

#else   // SPECTRA_USE_IMGUI

void TopicStatsOverlay::draw_stat_row(const char* /*label*/,
                                      const std::string& /*value*/,
                                      bool /*highlight*/)
{
}

void TopicStatsOverlay::draw_inline() {}

void TopicStatsOverlay::draw(bool* /*p_open*/, int /*active_subplot_slot*/) {}
void TopicStatsOverlay::draw_series_controls(SubplotManager*, int) {}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
