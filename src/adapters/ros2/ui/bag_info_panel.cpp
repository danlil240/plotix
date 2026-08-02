#include "ui/bag_info_panel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// BagSummary helpers
// ---------------------------------------------------------------------------

std::string BagSummary::duration_string() const
{
    double total = duration_sec;
    if (total < 0.0)
        total = 0.0;

    const int h  = static_cast<int>(total) / 3600;
    const int m  = (static_cast<int>(total) % 3600) / 60;
    const int s  = static_cast<int>(total) % 60;
    const int ms = static_cast<int>((total - std::floor(total)) * 1000.0);

    if (h > 0)
        return std::format("{}h {:02}m {:02}s", h, m, s);
    if (m > 0)
        return std::format("{}m {:02}s", m, s);
    return std::format("{:d}.{:03} s", s, ms);
}

std::string BagSummary::format_size(uint64_t bytes)
{
    if (bytes == 0)
        return "—";
    if (bytes < 1024ULL)
        return std::format("{} B", bytes);
    if (bytes < 1024ULL * 1024ULL)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    if (bytes < 1024ULL * 1024ULL * 1024ULL)
        return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

std::string BagSummary::format_time(double seconds_epoch)
{
    if (seconds_epoch <= 0.0)
        return "—";

    const auto total_s = static_cast<int64_t>(seconds_epoch);
    const int  ms      = static_cast<int>((seconds_epoch - static_cast<double>(total_s)) * 1000.0);

    const int64_t h = total_s / 3600;
    const int64_t m = (total_s % 3600) / 60;
    const int64_t s = total_s % 60;

    return std::format("{:02}:{:02}:{:02}.{:03}", h % 24, m, s, ms);
}

// ---------------------------------------------------------------------------
// BagInfoPanel construction
// ---------------------------------------------------------------------------

BagInfoPanel::BagInfoPanel() = default;

// ---------------------------------------------------------------------------
// Bag lifecycle
// ---------------------------------------------------------------------------

bool BagInfoPanel::open_bag(const std::string& path)
{
    reader_.close();
    selected_index_ = -1;
    summary_        = BagSummary{};

    if (!reader_.open(path))
    {
        summary_.last_error = reader_.last_error();
        return false;
    }

    refresh_summary();

    if (opened_cb_)
        opened_cb_(path);

    // Release storage — BagPlayer owns live playback; keeping two SQLite readers
    // on the same bag directory can deadlock on some platforms.
    reader_.close();

    return true;
}

void BagInfoPanel::close_bag()
{
    reader_.close();
    selected_index_ = -1;
    summary_        = BagSummary{};
}

bool BagInfoPanel::try_open_file(const std::string& path)
{
    if (!is_bag_path(path))
        return false;
    return open_bag(path);
}

bool BagInfoPanel::is_bag_path(const std::string& path)
{
    if (path.empty())
        return false;

    if (path.size() >= 4)
    {
        std::string ext = path.substr(path.size() - 4);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".db3")
            return true;
    }
    if (path.size() >= 5)
    {
        std::string ext = path.substr(path.size() - 5);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".mcap")
            return true;
    }

    // ROS 2 bag folders (metadata.yaml + storage files).
    namespace fs = std::filesystem;
    const fs::path bag_dir(path);
    if (fs::is_directory(bag_dir) && fs::exists(bag_dir / "metadata.yaml"))
        return true;

    return false;
}

// ---------------------------------------------------------------------------
// refresh_summary — build BagSummary from reader state
// ---------------------------------------------------------------------------

void BagInfoPanel::refresh_summary()
{
    summary_ = BagSummary{};

    if (!reader_.is_open())
    {
        summary_.last_error = reader_.last_error();
        return;
    }

    const BagMetadata& m = reader_.metadata();

    summary_.is_open         = true;
    summary_.path            = m.path;
    summary_.storage_id      = m.storage_id;
    summary_.duration_sec    = m.duration_sec();
    summary_.start_time_sec  = m.start_time_sec();
    summary_.end_time_sec    = m.end_time_sec();
    summary_.message_count   = m.message_count;
    summary_.compressed_size = m.compressed_size;

    summary_.topics.reserve(m.topics.size());
    for (const auto& t : m.topics)
    {
        BagTopicRow row;
        row.name          = t.name;
        row.type          = t.type;
        row.message_count = t.message_count;
        summary_.topics.push_back(std::move(row));
    }

    // Sort topics alphabetically by name for consistent display.
    std::sort(summary_.topics.begin(),
              summary_.topics.end(),
              [](const BagTopicRow& a, const BagTopicRow& b) { return a.name < b.name; });
}

// ---------------------------------------------------------------------------
// select_row / plot_row
// ---------------------------------------------------------------------------

void BagInfoPanel::select_row(int index)
{
    if (index < 0 || index >= static_cast<int>(summary_.topics.size()))
        return;

    selected_index_ = index;

    if (select_cb_)
    {
        const auto& row = summary_.topics[static_cast<size_t>(index)];
        select_cb_(row.name, row.type);
    }
}

void BagInfoPanel::plot_row(int index)
{
    if (index < 0 || index >= static_cast<int>(summary_.topics.size()))
        return;

    if (plot_cb_)
    {
        const auto& row = summary_.topics[static_cast<size_t>(index)];
        plot_cb_(row.name, row.type);
    }
}

// ---------------------------------------------------------------------------
// ImGui draw
// ---------------------------------------------------------------------------

void BagInfoPanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title_.c_str(), p_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    draw_inline();

    ImGui::End();
#else
    (void)p_open;
#endif
}

void BagInfoPanel::draw_inline()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    handle_imgui_drag_drop();

    if (!summary_.is_open)
    {
        draw_no_bag_placeholder();
        return;
    }

    draw_metadata_section();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    draw_topic_table();
#endif
}

// ---------------------------------------------------------------------------
// draw_metadata_section
// ---------------------------------------------------------------------------

void BagInfoPanel::draw_metadata_section()
{
#ifdef SPECTRA_USE_IMGUI
    const float label_w = 130.0f;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted("  Bag Metadata");
    ImGui::PopStyleColor();
    ImGui::Separator();

    auto row = [&](const char* label, const std::string& value)
    {
        ImGui::SetNextItemWidth(label_w);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(label_w + 8.0f);
        ImGui::TextUnformatted(value.c_str());
    };

    // Shorten path for display — show at most the last 50 characters.
    std::string display_path = summary_.path;
    if (display_path.size() > 50)
        display_path = "..." + display_path.substr(display_path.size() - 50);

    row("Path", display_path);
    row("Format", summary_.storage_id.empty() ? "—" : summary_.storage_id);
    row("Duration", summary_.duration_string());
    row("Start", BagSummary::format_time(summary_.start_time_sec));
    row("End", BagSummary::format_time(summary_.end_time_sec));

    {
        row("Messages", std::format("{}", summary_.message_count));
    }
    row("Size", BagSummary::format_size(summary_.compressed_size));

    {
        row("Topics", std::format("{}", summary_.topic_count()));
    }

    // Show last error (e.g. from a failed seek) if any.
    if (!summary_.last_error.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextWrapped("Error: %s", summary_.last_error.c_str());
        ImGui::PopStyleColor();
    }

    // Close button.
    ImGui::Spacing();
    if (ImGui::SmallButton("Close Bag"))
    {
        close_bag();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Close the currently open bag");
#endif
}

// ---------------------------------------------------------------------------
// draw_topic_table
// ---------------------------------------------------------------------------

void BagInfoPanel::draw_topic_table()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted("  Topics");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("(%zu  — click to select, double-click to plot)", summary_.topic_count());
    ImGui::PopStyleColor();
    ImGui::Separator();

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    // Reserve height — leave room for metadata above.
    const float table_height = ImGui::GetContentRegionAvail().y - 4.0f;

    if (!ImGui::BeginTable("##bag_topics", 3, flags, ImVec2(0.0f, table_height)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch, 0.50f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.35f);
    ImGui::TableSetupColumn("Messages", ImGuiTableColumnFlags_WidthStretch, 0.15f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(summary_.topics.size()); ++i)
    {
        const auto& row = summary_.topics[static_cast<size_t>(i)];

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const bool        is_selected = (selected_index_ == i);
        const std::string sel_id      = std::format("##sel_{}", i);

        if (ImGui::Selectable(
                sel_id.c_str(),
                is_selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0.0f, 0.0f)))
        {
            select_row(i);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            plot_row(i);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Double-click to plot  %s", row.name.c_str());

        ImGui::SameLine();
        ImGui::TextUnformatted(row.name.c_str());

        ImGui::TableSetColumnIndex(1);
        // Shorten type string: strip "…_msgs/msg/" prefix for readability.
        std::string short_type = row.type;
        const auto  slash_pos  = row.type.rfind('/');
        if (slash_pos != std::string::npos && slash_pos + 1 < row.type.size())
            short_type = row.type.substr(slash_pos + 1);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted(short_type.c_str());
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(std::format("{}", row.message_count).c_str());
    }

    ImGui::EndTable();
#endif
}

// ---------------------------------------------------------------------------
// draw_no_bag_placeholder
// ---------------------------------------------------------------------------

void BagInfoPanel::draw_no_bag_placeholder() const
{
#ifdef SPECTRA_USE_IMGUI
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  cx    = avail.x * 0.5f;
    const float  cy    = avail.y * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx - 120.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + cy - 32.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    ImGui::TextUnformatted("No bag open");
    ImGui::PopStyleColor();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx - 120.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
    ImGui::TextUnformatted("Drop a .db3 or .mcap file here");
    ImGui::PopStyleColor();

    if (!summary_.last_error.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextWrapped("Error: %s", summary_.last_error.c_str());
        ImGui::PopStyleColor();
    }
#endif
}

// ---------------------------------------------------------------------------
// handle_imgui_drag_drop
// ---------------------------------------------------------------------------

void BagInfoPanel::handle_imgui_drag_drop()
{
#ifdef SPECTRA_USE_IMGUI
    // Draw a full-window invisible button as the drop target.  Without a
    // preceding widget, BeginDragDropTarget() has no item to attach to.
    // SetNextItemAllowOverlap() ensures this button doesn't eat clicks
    // from content drawn on top afterwards.
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const ImVec2 cursor = ImGui::GetCursorPos();

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##bag_drop_zone",
                           ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)));

    if (ImGui::BeginDragDropTarget())
    {
        // Accept an internal "ROS2_BAG_FILE" payload.
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ROS2_BAG_FILE"))
        {
            if (payload->DataSize > 0)
            {
                const std::string path(static_cast<const char*>(payload->Data),
                                       static_cast<size_t>(payload->DataSize - 1));
                try_open_file(path);
            }
        }
        // Also accept a generic "FILES" payload (e.g. from OS-level drop forwarding).
        else if (const ImGuiPayload* files_payload = ImGui::AcceptDragDropPayload("FILES"))
        {
            if (files_payload->DataSize > 0)
            {
                const std::string path(static_cast<const char*>(files_payload->Data),
                                       static_cast<size_t>(files_payload->DataSize - 1));
                try_open_file(path);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Reset cursor so actual content draws on top of the invisible button.
    ImGui::SetCursorPos(cursor);
#endif
}

}   // namespace spectra::adapters::ros2
