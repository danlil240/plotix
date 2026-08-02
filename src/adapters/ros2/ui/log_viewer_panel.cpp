#include "ui/log_viewer_panel.hpp"

#include <chrono>
#include <cstring>
#include <format>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

const char* const LogViewerPanel::severity_labels_[] = {
    "All (UNSET+)",
    "DEBUG+",
    "INFO+",
    "WARN+",
    "ERROR+",
    "FATAL only",
};

const LogSeverity LogViewerPanel::severity_values_[] = {
    LogSeverity::Unset,
    LogSeverity::Debug,
    LogSeverity::Info,
    LogSeverity::Warn,
    LogSeverity::Error,
    LogSeverity::Fatal,
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LogViewerPanel::LogViewerPanel(RosLogViewer& viewer) : viewer_(viewer)
{
    // Mirror initial filter state into UI buffers.
    const LogFilter& f = viewer_.filter();
    std::strncpy(node_filter_buf_, f.node_filter.c_str(), sizeof(node_filter_buf_) - 1);
    std::strncpy(regex_filter_buf_, f.message_regex_str.c_str(), sizeof(regex_filter_buf_) - 1);

    // Find matching severity combo index.

    for (int i = 0; i < severity_count_; ++i)
    {
        if (severity_values_[i] == f.min_severity)
        {
            severity_combo_idx_ = i;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

double LogViewerPanel::wall_time_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string LogViewerPanel::format_row(const LogEntry& e)
{
    return RosLogViewer::format_wall_time(e.wall_time_s) + "\t" + severity_name(e.severity) + "\t"
           + e.node + "\t" + e.message;
}

std::string LogViewerPanel::build_copy_text(const std::vector<LogEntry>& entries) const
{
    if (entries.empty())
        return {};

    std::string out;
    out.reserve(entries.size() * 80);
    out += "Time\tSeverity\tNode\tMessage\n";
    for (const auto& e : entries)
        out += format_row(e) + "\n";
    return out;
}

void LogViewerPanel::set_clipboard(const std::string& text)
{
    last_clipboard_text_ = text;
#ifdef SPECTRA_USE_IMGUI
    ImGui::SetClipboardText(text.c_str());
#endif
}

// ---------------------------------------------------------------------------
// maybe_refresh — throttled snapshot
// ---------------------------------------------------------------------------

void LogViewerPanel::maybe_refresh()
{
    const double now = wall_time_now();
    if (now - last_refresh_time_s_ < display_interval_s_)
        return;
    last_refresh_time_s_ = now;

    cached_entries_ = viewer_.filtered_snapshot();
    visible_count_  = cached_entries_.size();

    // Clamp to max_display_rows_ (show newest).
    if (cached_entries_.size() > max_display_rows_)
        cached_entries_.erase(
            cached_entries_.begin(),
            cached_entries_.begin()
                + static_cast<ptrdiff_t>(cached_entries_.size() - max_display_rows_));

    // Clamp selected row.
    if (selected_row_ >= static_cast<int>(cached_entries_.size()))
        selected_row_ = -1;
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void LogViewerPanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(900.0f, 340.0f), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin(title_.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

    maybe_refresh();

    draw_toolbar();
    draw_filter_bar();

    const float detail_height = show_detail_ && selected_row_ >= 0 ? 110.0f : 0.0f;
    const float status_height = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    const float table_height =
        ImGui::GetContentRegionAvail().y - detail_height - status_height - 4.0f;

    draw_table(cached_entries_);
    (void)table_height;   // used inside draw_table via GetContentRegionAvail

    if (show_detail_ && selected_row_ >= 0
        && selected_row_ < static_cast<int>(cached_entries_.size()))
    {
        ImGui::Separator();
        draw_detail_pane(cached_entries_[selected_row_]);
    }

    ImGui::Separator();
    draw_status_bar(cached_entries_);

    ImGui::End();
#else
    (void)p_open;
    maybe_refresh();
#endif
}

// ---------------------------------------------------------------------------
// draw_toolbar
// ---------------------------------------------------------------------------

void LogViewerPanel::draw_toolbar()
{
#ifdef SPECTRA_USE_IMGUI
    const bool paused = viewer_.is_paused();

    // Pause / Resume
    if (paused)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.0f, 1.0f));
        if (ImGui::Button("Resume"))
            viewer_.resume();
        ImGui::PopStyleColor();
    }
    else
    {
        if (ImGui::Button("Pause"))
            viewer_.pause();
    }
    ImGui::SameLine();

    // Clear
    if (ImGui::Button("Clear"))
    {
        viewer_.clear();
        cached_entries_.clear();
        visible_count_ = 0;
        selected_row_  = -1;
    }
    ImGui::SameLine();

    // Copy all visible rows
    if (ImGui::Button("Copy All"))
    {
        set_clipboard(build_copy_text(cached_entries_));
    }
    ImGui::SameLine();

    // Copy selected row
    if (ImGui::Button("Copy Row") && selected_row_ >= 0
        && selected_row_ < static_cast<int>(cached_entries_.size()))
    {
        set_clipboard(format_row(cached_entries_[selected_row_]));
    }
    ImGui::SameLine();

    // Follow toggle
    if (auto_scroll_)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
        if (ImGui::Button("Following"))
            auto_scroll_ = false;
        ImGui::PopStyleColor();
    }
    else
    {
        if (ImGui::Button("Follow"))
        {
            auto_scroll_      = true;
            scroll_to_bottom_ = true;
        }
    }
    ImGui::SameLine();

    // Detail toggle
    if (ImGui::Button(show_detail_ ? "Hide Detail" : "Show Detail"))
        show_detail_ = !show_detail_;

    // Severity badge counts on the right side.
    const auto counts = viewer_.severity_counts();
    // indices: Debug=1, Info=2, Warn=3, Error=4, Fatal=5
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 280.0f + ImGui::GetCursorPosX());

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("D:%u", counts[1]);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::Text("I:%u", counts[2]);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
    ImGui::Text("W:%u", counts[3]);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::Text("E:%u", counts[4]);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.8f, 1.0f));
    ImGui::Text("F:%u", counts[5]);
    ImGui::PopStyleColor();
#endif
}

// ---------------------------------------------------------------------------
// draw_filter_bar
// ---------------------------------------------------------------------------

void LogViewerPanel::draw_filter_bar()
{
#ifdef SPECTRA_USE_IMGUI
    bool filter_changed = false;

    // Severity pill toggles (DEBUG / INFO / WARN / ERROR / FATAL).
    // Each pill is a small toggle button that shows/hides entries of that level.
    struct PillDef
    {
        const char* label{};
        LogSeverity sev{};
        ImVec4      col_on;
        ImVec4      col_off;
    };
    static const PillDef kPills[] = {
        {"DBG", LogSeverity::Debug, {0.55f, 0.55f, 0.55f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.6f}},
        {"INFO", LogSeverity::Info, {0.85f, 0.85f, 0.85f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.6f}},
        {"WARN", LogSeverity::Warn, {0.95f, 0.75f, 0.10f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.6f}},
        {"ERROR", LogSeverity::Error, {0.95f, 0.30f, 0.30f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.6f}},
        {"FATAL", LogSeverity::Fatal, {1.00f, 0.30f, 0.80f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.6f}},
    };
    for (int pi = 0; pi < 5; ++pi)
    {
        const int     idx = static_cast<int>(kPills[pi].sev);
        const bool    on  = (idx >= 0 && idx < 6) ? severity_pill_visible_[idx] : true;
        const ImVec4& col = on ? kPills[pi].col_on : kPills[pi].col_off;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(col.x * 0.5f, col.y * 0.5f, col.z * 0.5f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(col.x * 0.7f, col.y * 0.7f, col.z * 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        const std::string btn_id = std::format("{}##pill", kPills[pi].label);
        if (ImGui::SmallButton(btn_id.c_str()))
        {
            if (idx >= 0 && idx < 6)
                severity_pill_visible_[idx] = !severity_pill_visible_[idx];
            filter_changed = true;
        }
        ImGui::PopStyleColor(3);
        if (pi < 4)
            ImGui::SameLine(0.0f, 3.0f);
    }
    ImGui::SameLine(0.0f, 8.0f);

    // Node filter.
    ImGui::SetNextItemWidth(160.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
    if (ImGui::InputText("##node", node_filter_buf_, sizeof(node_filter_buf_)))
    {
        viewer_.set_node_filter(node_filter_buf_);
        filter_changed = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Node");
    ImGui::SameLine();

    // Regex filter.
    const bool has_regex_error = !viewer_.regex_error().empty();
    if (has_regex_error)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("##regex", regex_filter_buf_, sizeof(regex_filter_buf_)))
    {
        viewer_.set_message_regex(regex_filter_buf_);
        filter_changed = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted("Regex");

    if (has_regex_error)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted("[bad regex]");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", viewer_.regex_error().c_str());
        ImGui::PopStyleColor();
    }

    // Clear filters button.
    ImGui::SameLine();
    if (ImGui::SmallButton("X##clrfilter"))
    {
        node_filter_buf_[0]  = '\0';
        regex_filter_buf_[0] = '\0';
        severity_combo_idx_  = 0;
        viewer_.set_node_filter("");
        viewer_.set_message_regex("");
        viewer_.set_min_severity(LogSeverity::Unset);
        filter_changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear all filters");

    if (filter_changed)
    {
        // Force immediate refresh after filter change.
        last_refresh_time_s_ = 0.0;
        scroll_to_bottom_    = true;
    }
#endif
}

// ---------------------------------------------------------------------------
// Severity row colours
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI
static ImVec4 severity_row_colour(LogSeverity s)
{
    switch (s)
    {
        case LogSeverity::Debug:
            return {0.55f, 0.55f, 0.55f, 1.0f};
        case LogSeverity::Info:
            return {0.90f, 0.90f, 0.90f, 1.0f};
        case LogSeverity::Warn:
            return {1.00f, 0.85f, 0.10f, 1.0f};
        case LogSeverity::Error:
            return {1.00f, 0.35f, 0.35f, 1.0f};
        case LogSeverity::Fatal:
            return {1.00f, 0.20f, 0.80f, 1.0f};
        default:
            return {0.60f, 0.60f, 0.60f, 1.0f};
    }
}
#endif

// ---------------------------------------------------------------------------
// draw_table
// ---------------------------------------------------------------------------

void LogViewerPanel::draw_table(const std::vector<LogEntry>& entries)
{
#ifdef SPECTRA_USE_IMGUI
    const float detail_height = show_detail_ && selected_row_ >= 0 ? 110.0f : 0.0f;
    const float status_height = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    const float avail         = ImGui::GetContentRegionAvail().y;
    const float table_height  = avail - detail_height - status_height - 4.0f;

    constexpr ImGuiTableFlags tbl_flags =
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
        | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##logtbl", 4, tbl_flags, ImVec2(0.0f, table_height)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 46.0f);
    ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 0.0f);
    ImGui::TableHeadersRow();

    const int n = static_cast<int>(entries.size());
    for (int i = 0; i < n; ++i)
    {
        const LogEntry& e = entries[i];

        // Pill filter: skip entries whose severity is toggled off.
        {
            const int sev_idx = static_cast<int>(e.severity);
            if (sev_idx >= 0 && sev_idx < 6 && !severity_pill_visible_[sev_idx])
                continue;
        }

        ImGui::TableNextRow();

        const bool   is_selected = (selected_row_ == i);
        const ImVec4 col         = severity_row_colour(e.severity);

        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(RosLogViewer::format_wall_time(e.wall_time_s).c_str());
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(severity_name(e.severity));
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(2);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(e.node.c_str());
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(3);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        // Selectable spanning the message column; clickable.
        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::Selectable(e.message.c_str(),
                              is_selected,
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0.0f, row_h - 2.0f)))
        {
            selected_row_     = (is_selected) ? -1 : i;
            scroll_to_bottom_ = false;   // user clicked — stop following
            auto_scroll_      = false;
            // Fire seek callback so subplot panels can jump to this timestamp.
            if (seek_cb_ && !is_selected)
                seek_cb_(e.stamp_ns);
        }
        ImGui::PopStyleColor();

        // Right-click context menu on row.
        if (ImGui::BeginPopupContextItem())
        {
            selected_row_ = i;
            if (ImGui::MenuItem("Copy row"))
                set_clipboard(format_row(e));
            if (ImGui::MenuItem("Copy message"))
                set_clipboard(e.message);
            ImGui::EndPopup();
        }
    }

    // Auto-scroll: scroll to bottom if following and new entries arrived.
    if (auto_scroll_ || scroll_to_bottom_)
    {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom_ = false;
    }

    // Detect if user scrolled up manually.
    if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 1.0f)
        auto_scroll_ = false;

    ImGui::EndTable();
#else
    (void)entries;
#endif
}

// ---------------------------------------------------------------------------
// draw_detail_pane
// ---------------------------------------------------------------------------

void LogViewerPanel::draw_detail_pane(const LogEntry& e)
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.14f, 1.0f));
    ImGui::BeginChild("##detail", ImVec2(0.0f, 106.0f), 0, ImGuiWindowFlags_HorizontalScrollbar);

    const ImVec4 label_col = ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
    const ImVec4 val_col   = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);

    auto row = [&](const char* lbl, const std::string& val)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, label_col);
        ImGui::Text("%-12s", lbl);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, val_col);
        ImGui::TextUnformatted(val.c_str());
        ImGui::PopStyleColor();
    };

    row("Stamp:", RosLogViewer::format_stamp(e.stamp_ns));
    row("Node:", e.node);
    row("Severity:", severity_name(e.severity));
    row("Message:", e.message);
    if (!e.file.empty() || e.line > 0)
    {
        const std::string loc = e.line > 0 ? std::format("{}:{}", e.file, e.line) : e.file;
        row("Location:", loc.c_str());
    }
    if (!e.function.empty())
        row("Function:", e.function);

    ImGui::EndChild();
    ImGui::PopStyleColor();
#else
    (void)e;
#endif
}

// ---------------------------------------------------------------------------
// draw_status_bar
// ---------------------------------------------------------------------------

void LogViewerPanel::draw_status_bar(const std::vector<LogEntry>& entries)
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
    const std::string status =
        std::format("Showing {} / {}  |  Total received: {}  |  Buffer: {} / {}",
                    entries.size(),
                    visible_count_,
                    viewer_.total_received(),
                    viewer_.entry_count(),
                    viewer_.capacity());
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopStyleColor();
#else
    (void)entries;
#endif
}

// ---------------------------------------------------------------------------
// Severity pill helpers (always compiled — no ImGui dependency)
// ---------------------------------------------------------------------------

void LogViewerPanel::set_severity_visible(LogSeverity sev, bool visible)
{
    const int idx = static_cast<int>(sev);
    if (idx >= 0 && idx < 6)
        severity_pill_visible_[idx] = visible;
}

bool LogViewerPanel::severity_visible(LogSeverity sev) const
{
    const int idx = static_cast<int>(sev);
    if (idx >= 0 && idx < 6)
        return severity_pill_visible_[idx];
    return true;
}

}   // namespace spectra::adapters::ros2
