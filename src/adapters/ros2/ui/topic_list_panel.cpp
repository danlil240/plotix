#include "ui/topic_list_panel.hpp"
#include "ui/field_drag_drop.hpp"
#include "ui/topic_echo_panel.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <format>
#include <functional>
#include <iterator>
#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/imgui/widgets.hpp"
    #include "ui/shell/shell_style.hpp"
    #include "ui/theme/icons.hpp"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t wall_clock_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

static int displayed_endpoint_count(int total_count, int local_count)
{
    return std::max(0, total_count - local_count);
}

static std::string lowercase_copy(std::string text)
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

#ifdef SPECTRA_USE_IMGUI
static std::string fit_text(std::string text, float max_width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width)
        return text;
    while (text.size() > 4 && ImGui::CalcTextSize((text + "...").c_str()).x > max_width)
        text.pop_back();
    return text + "...";
}

static float chip_button_width(const char* label)
{
    return ImGui::CalcTextSize(label ? label : "").x + spectra::ui::tokens::CHIP_PADDING_H * 2.0f;
}

static void draw_wrapped_filter_chips(
    const TopicListPanel::TopicCategoryFilter*                      filters,
    const char* const*                                              labels,
    size_t                                                          count,
    TopicListPanel::TopicCategoryFilter                             active,
    const std::function<void(TopicListPanel::TopicCategoryFilter)>& on_select)
{
    constexpr float kSpacing      = 6.0f;
    const float     content_right = ImGui::GetWindowContentRegionMax().x;
    const float     content_left  = ImGui::GetWindowContentRegionMin().x;

    auto draw_chip = [&](size_t index)
    {
        if (spectra::ui::widgets::filter_chip(labels[index], active == filters[index]))
            on_select(filters[index]);
    };

    for (size_t i = 0; i < count;)
    {
        float row_x        = content_left;
        bool  first_in_row = true;
        while (i < count)
        {
            const float chip_w = chip_button_width(labels[i]);
            const float needed = first_in_row ? chip_w : (kSpacing + chip_w);
            if (!first_in_row && row_x + needed > content_right - 2.0f)
                break;
            if (!first_in_row)
                ImGui::SameLine(0.0f, kSpacing);
            draw_chip(i);
            row_x += needed;
            first_in_row = false;
            ++i;
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// TopicStats
// ---------------------------------------------------------------------------

void TopicStats::push(int64_t now_ns, size_t bytes)
{
    timestamps.push_back(now_ns);
    sizes.push_back(bytes);
    last_msg_ns = now_ns;
    ++total_messages;
    total_bytes += bytes;
}

void TopicStats::prune_and_compute(int64_t now_ns, int64_t window_ns)
{
    const int64_t cutoff = now_ns - window_ns;

    // Remove entries older than the window.
    while (!timestamps.empty() && timestamps.front() < cutoff)
    {
        timestamps.pop_front();
        sizes.pop_front();
    }

    const size_t n = timestamps.size();
    if (n < 2)
    {
        hz            = (n == 1) ? 1.0 : 0.0;
        bandwidth_bps = 0.0;
    }
    else
    {
        const double span_s = static_cast<double>(timestamps.back() - timestamps.front()) * 1e-9;
        hz                  = (span_s > 0.0) ? static_cast<double>(n - 1) / span_s : 0.0;

        // BW = total bytes in window / window size in seconds.
        size_t total = 0;
        for (auto s : sizes)
            total += s;
        const double win_s = static_cast<double>(window_ns) * 1e-9;
        bandwidth_bps      = static_cast<double>(total) / win_s;
    }

    // EMA smoothing for displayed Hz — alpha chosen so the display
    // settles in ~2-3 seconds (lower alpha = smoother, slower response).
    constexpr double EMA_ALPHA = 0.15;
    if (displayed_hz <= 0.0 && hz > 0.0)
        displayed_hz = hz;   // initialise on first sample
    else
        displayed_hz = EMA_ALPHA * hz + (1.0 - EMA_ALPHA) * displayed_hz;
    // Snap to zero when raw Hz drops below a tiny threshold.
    if (hz <= 0.0)
        displayed_hz *= (1.0 - EMA_ALPHA);   // decay towards zero
    if (displayed_hz < 0.05)
        displayed_hz = 0.0;

    // Adaptive stale threshold: for low-frequency topics (≤ 2 Hz) give
    // at least 3× the expected period so the dot doesn't flicker.
    int64_t stale_ns = 2'000'000'000LL;   // 2 s default
    if (displayed_hz > 0.0 && displayed_hz <= 2.0)
    {
        const auto period_ns = static_cast<int64_t>(1.0e9 / displayed_hz);
        stale_ns             = std::max(stale_ns, period_ns * 3);
    }
    active = (last_msg_ns > 0) && (now_ns - last_msg_ns < stale_ns);

    // Sample Hz history once per second for sparkline.
    const int64_t sample_interval_ns = 1'000'000'000LL;
    if (last_hz_sample_ns == 0 || (now_ns - last_hz_sample_ns) >= sample_interval_ns)
    {
        hz_history.push_back(static_cast<float>(hz));
        if (hz_history.size() > HZ_HISTORY_LEN)
            hz_history.pop_front();
        last_hz_sample_ns = now_ns;
    }
}

// ---------------------------------------------------------------------------
// TopicListPanel — construction
// ---------------------------------------------------------------------------

TopicListPanel::TopicListPanel()
{
    std::memset(filter_buf_, 0, sizeof(filter_buf_));
}

TopicListPanel::~TopicListPanel() = default;

void TopicListPanel::set_column_visibility(const ColumnVisibility& visibility)
{
    col_show_type_ = visibility.show_type;
    col_show_hz_   = visibility.show_hz;
    col_show_pubs_ = visibility.show_pubs;
    col_show_subs_ = visibility.show_subs;
    col_show_bw_   = visibility.show_bw;
    col_show_age_  = visibility.show_age;
}

TopicListPanel::ColumnVisibility TopicListPanel::column_visibility() const
{
    return {
        .show_type = col_show_type_,
        .show_hz   = col_show_hz_,
        .show_pubs = col_show_pubs_,
        .show_subs = col_show_subs_,
        .show_bw   = col_show_bw_,
        .show_age  = col_show_age_,
    };
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

void TopicListPanel::set_topic_discovery(TopicDiscovery* disc)
{
    disc_ = disc;
}

// ---------------------------------------------------------------------------
// Statistics injection (executor thread)
// ---------------------------------------------------------------------------

void TopicListPanel::notify_message(const std::string& topic_name, size_t bytes)
{
    const int64_t               now = wall_clock_ns();
    std::lock_guard<std::mutex> lk(stats_mutex_);
    stats_map_[topic_name].push(now, bytes);
}

// ---------------------------------------------------------------------------
// Testing helpers
// ---------------------------------------------------------------------------

void TopicListPanel::set_topics(const std::vector<TopicInfo>& topics)
{
    std::lock_guard<std::mutex> lk(topics_mutex_);
    topics_ = topics;
    rebuild_tree();
    filter_dirty_ = true;
}

void TopicListPanel::set_filter(const std::string& f)
{
    filter_str_           = f;
    const size_t copy_len = std::min(f.size(), sizeof(filter_buf_) - 1);
    std::memcpy(filter_buf_, f.c_str(), copy_len);
    filter_buf_[copy_len] = '\0';
    filter_dirty_         = true;
}

void TopicListPanel::set_category_filter(TopicCategoryFilter filter)
{
    if (category_filter_ == filter)
        return;
    category_filter_ = filter;
    filter_dirty_    = true;
}

TopicListPanel::StatsSnapshot TopicListPanel::stats_for(const std::string& topic_name) const
{
    const int64_t               now = wall_clock_ns();
    std::lock_guard<std::mutex> lk(stats_mutex_);
    auto                        it = stats_map_.find(topic_name);
    if (it == stats_map_.end())
    {
        return {0.0, 0.0, false, 0};
    }
    // Compute on a copy so we don't mutate under const.
    TopicStats copy = it->second;
    copy.prune_and_compute(now, static_cast<int64_t>(stats_window_ms_) * 1'000'000LL);
    return {copy.hz, copy.bandwidth_bps, copy.active, copy.total_messages};
}

size_t TopicListPanel::topic_count() const
{
    std::lock_guard<std::mutex> lk(topics_mutex_);
    return topics_.size();
}

size_t TopicListPanel::filtered_topic_count() const
{
    // Rebuild filtered list if dirty.
    if (filter_dirty_)
    {
        filtered_names_.clear();
        std::lock_guard<std::mutex> lk(topics_mutex_);
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto& t : topics_)
        {
            const auto        it = stats_map_.find(t.name);
            const TopicStats  empty_stats;
            const TopicStats& stats = (it != stats_map_.end()) ? it->second : empty_stats;
            if (passes_topic_filter(t, stats))
            {
                filtered_names_.push_back(t.name);
            }
        }
        filter_dirty_ = false;
    }
    return filtered_names_.size();
}

// ---------------------------------------------------------------------------
// Tree building
// ---------------------------------------------------------------------------

/*static*/ std::pair<std::string, std::string> TopicListPanel::split_namespace(
    const std::string& topic_name)
{
    // Find last '/' to split leaf from namespace.
    const auto pos = topic_name.rfind('/');
    if (pos == std::string::npos || pos == 0)
    {
        // No namespace or root-level topic.
        return {"/", topic_name.substr(pos == std::string::npos ? 0 : 1)};
    }
    return {topic_name.substr(0, pos), topic_name.substr(pos + 1)};
}

void TopicListPanel::rebuild_tree()
{
    // Assumes topics_mutex_ is held by caller.
    ns_tree_.clear();
    root_namespaces_.clear();
    root_topics_.clear();

    for (const auto& t : topics_)
    {
        auto [ns, leaf] = split_namespace(t.name);

        if (ns == "/")
        {
            root_topics_.push_back(t.name);
            continue;
        }

        // Ensure the namespace node exists.
        if (ns_tree_.find(ns) == ns_tree_.end())
        {
            NamespaceNode node;
            node.ns = ns;
            // label = last segment of ns.
            const auto slash = ns.rfind('/');
            node.label       = (slash == std::string::npos) ? ns : ns.substr(slash + 1);
            ns_tree_[ns]     = std::move(node);
        }
        ns_tree_[ns].topic_names.push_back(t.name);

        // Register parent relationship walking up.
        std::string child_ns = ns;
        while (true)
        {
            const auto parent_slash = child_ns.rfind('/');
            if (parent_slash == std::string::npos || parent_slash == 0)
            {
                // child_ns is a root-level namespace.
                auto& root_vec = root_namespaces_;
                if (std::find(root_vec.begin(), root_vec.end(), child_ns) == root_vec.end())
                {
                    root_vec.push_back(child_ns);
                }
                break;
            }
            const std::string parent_ns = child_ns.substr(0, parent_slash);
            if (ns_tree_.find(parent_ns) == ns_tree_.end())
            {
                NamespaceNode pnode;
                pnode.ns     = parent_ns;
                const auto s = parent_ns.rfind('/');
                pnode.label  = (s == std::string::npos) ? parent_ns : parent_ns.substr(s + 1);
                ns_tree_[parent_ns] = std::move(pnode);
            }
            auto& children = ns_tree_[parent_ns].children;
            if (std::find(children.begin(), children.end(), child_ns) == children.end())
            {
                children.push_back(child_ns);
            }
            child_ns = parent_ns;
        }
    }

    // Deduplicate root_namespaces_.
    std::sort(root_namespaces_.begin(), root_namespaces_.end());
    root_namespaces_.erase(std::unique(root_namespaces_.begin(), root_namespaces_.end()),
                           root_namespaces_.end());
    std::sort(root_topics_.begin(), root_topics_.end());
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/*static*/ std::string TopicListPanel::format_hz(double hz)
{
    if (hz <= 0.0)
        return "-";
    if (hz >= 1000.0)
        return std::format("{:.0f}", hz);
    return std::format("{:.2f}", hz);
}

/*static*/ std::string TopicListPanel::format_bw(double bps)
{
    if (bps <= 0.0)
        return "-";
    if (bps >= 1024.0 * 1024.0)
        return std::format("{:.1f} MB/s", bps / (1024.0 * 1024.0));
    if (bps >= 1024.0)
        return std::format("{:.1f} KB/s", bps / 1024.0);
    return std::format("{:.0f} B/s", bps);
}

/*static*/ std::string TopicListPanel::format_age(int64_t now_ns_val, int64_t last_msg_ns)
{
    if (last_msg_ns <= 0)
        return "-";
    const double age_s = std::max(0.0, static_cast<double>(now_ns_val - last_msg_ns) * 1e-9);
    if (age_s < 1.0)
        return std::format("{:.0f} ms", age_s * 1000.0);
    if (age_s < 60.0)
        return std::format("{:.1f} s", age_s);
    if (age_s < 3600.0)
        return std::format("{:.0f} min", age_s / 60.0);
    return std::format("{:.1f} h", age_s / 3600.0);
}

/*static*/ int64_t TopicListPanel::now_ns()
{
    return wall_clock_ns();
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

// Color constants.
static constexpr ImVec4 kColorActive   = {0.20f, 0.80f, 0.30f, 1.0f};    // green
static constexpr ImVec4 kColorStale    = {0.50f, 0.50f, 0.50f, 1.0f};    // gray
static constexpr ImVec4 kColorSelected = {0.26f, 0.59f, 0.98f, 0.25f};   // blue tint

// Echo field colors (matching TopicEchoPanel for visual consistency).
static constexpr ImVec4 kEchoNumeric = {0.60f, 0.88f, 1.00f, 1.0f};   // light blue
static constexpr ImVec4 kEchoText    = {0.90f, 0.90f, 0.65f, 1.0f};   // light yellow
static constexpr ImVec4 kEchoNested  = {0.80f, 0.70f, 0.50f, 1.0f};   // tan
static constexpr ImVec4 kEchoArray   = {0.75f, 0.60f, 0.90f, 1.0f};   // lavender

// Returns a distinguishing color for a ROS2 message type package prefix.
// e.g. "sensor_msgs/msg/Imu" → sensor category color.
static ImVec4 topic_type_color(const std::string& type_name)
{
    // Match on the package name prefix before "/msg/".
    auto              slash = type_name.find('/');
    const std::string pkg   = (slash != std::string::npos) ? type_name.substr(0, slash) : type_name;

    // Sensor data — orange
    if (pkg == "sensor_msgs")
        return {1.00f, 0.60f, 0.10f, 1.0f};
    // Geometry / transforms — cyan
    if (pkg == "geometry_msgs")
        return {0.20f, 0.85f, 0.90f, 1.0f};
    // Navigation — purple
    if (pkg == "nav_msgs")
        return {0.75f, 0.40f, 0.90f, 1.0f};
    // Diagnostics — yellow
    if (pkg == "diagnostic_msgs")
        return {0.95f, 0.90f, 0.15f, 1.0f};
    // Std messages — light blue
    if (pkg == "std_msgs")
        return {0.40f, 0.70f, 1.00f, 1.0f};
    // Actions / lifecycle — pink
    if (pkg == "action_msgs" || pkg == "lifecycle_msgs")
        return {1.00f, 0.45f, 0.70f, 1.0f};
    // TF — teal
    if (pkg == "tf2_msgs")
        return {0.20f, 0.90f, 0.70f, 1.0f};
    // Default — neutral gray-white
    return {0.75f, 0.75f, 0.75f, 1.0f};
}

bool TopicListPanel::matches_text_filter(const TopicInfo& info) const
{
    if (filter_str_.empty())
        return true;
    const std::string needle = lowercase_copy(filter_str_);
    if (lowercase_copy(info.name).find(needle) != std::string::npos)
        return true;
    for (const auto& type : info.types)
    {
        if (lowercase_copy(type).find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool TopicListPanel::passes_topic_filter(const TopicInfo& info, const TopicStats& stats) const
{
    if (!matches_text_filter(info))
        return false;

    const std::string type =
        info.types.empty() ? std::string{} : lowercase_copy(info.types.front());
    const std::string name = lowercase_copy(info.name);
    auto              has  = [](const std::string& haystack, const char* needle)
    { return haystack.find(needle) != std::string::npos; };

    switch (category_filter_)
    {
        case TopicCategoryFilter::All:
            return true;
        case TopicCategoryFilter::Numeric:
            return has(type, "std_msgs/msg/float") || has(type, "std_msgs/msg/int")
                   || has(type, "std_msgs/msg/u") || has(type, "geometry_msgs/msg/twist")
                   || has(type, "geometry_msgs/msg/vector3") || has(type, "sensor_msgs/msg/imu")
                   || has(type, "sensor_msgs/msg/jointstate") || has(type, "nav_msgs/msg/odometry");
        case TopicCategoryFilter::Images:
            return has(type, "image") || has(type, "camera") || has(name, "image");
        case TopicCategoryFilter::Tf:
            return name == "/tf" || name == "/tf_static" || has(type, "tf2_msgs/");
        case TopicCategoryFilter::Diagnostics:
            return has(type, "diagnostic_msgs/") || has(name, "diagnostic");
        case TopicCategoryFilter::Active:
            return stats.active;
        case TopicCategoryFilter::HighHz:
            return stats.displayed_hz >= 10.0;
    }
    return true;
}

bool TopicListPanel::is_favorite(const std::string& topic_name) const
{
    return is_favorite_cb_ ? is_favorite_cb_(topic_name) : false;
}

// Draw a small inline sparkline using ImDrawList.
// Renders in the current cursor position using width × height pixels.
static void draw_hz_sparkline(const std::deque<float>& history, float width, float height)
{
    if (history.empty())
        return;

    ImVec2      p  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float max_val = 0.0f;
    for (float v : history)
        if (v > max_val)
            max_val = v;
    if (max_val <= 0.0f)
    {
        // No data — draw a flat baseline.
        const float y = p.y + height;
        dl->AddLine({p.x, y}, {p.x + width, y}, IM_COL32(80, 80, 80, 180), 1.0f);
        ImGui::Dummy({width, height});
        return;
    }

    const float inv_max = 1.0f / max_val;
    const int   n       = static_cast<int>(history.size());
    const float step    = (n > 1) ? width / static_cast<float>(n - 1) : width;

    for (int i = 1; i < n; ++i)
    {
        const float x0 = p.x + static_cast<float>(i - 1) * step;
        const float x1 = p.x + static_cast<float>(i) * step;
        const float y0 = p.y + height * (1.0f - history[i - 1] * inv_max);
        const float y1 = p.y + height * (1.0f - history[i] * inv_max);
        dl->AddLine({x0, y0}, {x1, y1}, IM_COL32(80, 200, 120, 200), 1.0f);
    }
    ImGui::Dummy({width, height});
}

void TopicListPanel::draw(bool* p_open)
{
    // Fetch discovery data OUTSIDE any lock to avoid ABBA with
    // TopicDiscovery::mutex_.
    std::vector<TopicInfo> fresh;
    bool                   have_fresh = false;
    if (disc_)
    {
        fresh      = disc_->topics();
        have_fresh = true;
    }

    // Prune and compute stats under stats_mutex_ once per frame.
    // This lock is scoped — released before topics_mutex_ is acquired.
    const int64_t cur_ns = wall_clock_ns();
    const int64_t win_ns = static_cast<int64_t>(stats_window_ms_) * 1'000'000LL;
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        for (auto& [name, st] : stats_map_)
        {
            st.prune_and_compute(cur_ns, win_ns);
        }
    }

    // Single topics_mutex_ lock: covers discovery update + rendering.
    std::lock_guard<std::mutex> lk_t(topics_mutex_);

    // Apply discovery update (still under topics_mutex_).
    if (have_fresh)
    {
        if (fresh.size() != topics_.size())
        {
            topics_ = std::move(fresh);
            rebuild_tree();
            filter_dirty_ = true;
        }
        else
        {
            bool changed = false;
            for (size_t i = 0; i < topics_.size(); ++i)
            {
                if (topics_[i].name != fresh[i].name)
                {
                    changed = true;
                    break;
                }
            }
            if (changed)
            {
                topics_ = std::move(fresh);
                rebuild_tree();
                filter_dirty_ = true;
            }
        }

        // Drop rolling stats for topics that disappeared from discovery.
        {
            std::lock_guard<std::mutex> lk(stats_mutex_);
            for (auto it = stats_map_.begin(); it != stats_map_.end();)
            {
                const bool still_live =
                    std::any_of(topics_.begin(),
                                topics_.end(),
                                [&](const TopicInfo& info) { return info.name == it->first; });
                if (!still_live)
                    it = stats_map_.erase(it);
                else
                    ++it;
            }
        }
    }

    // --- Window ---
    if (!ImGui::GetCurrentContext())
        return;
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (!spectra::ui::shell::begin_panel(title_.c_str(), p_open))
    {
        spectra::ui::shell::end_panel();
        return;
    }

    // --- Search and category filters ---
    bool filter_changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (spectra::ui::widgets::search_box(filter_buf_,
                                         sizeof(filter_buf_),
                                         "Search topics, types, fields...",
                                         &filter_changed))
    {
        filter_str_   = filter_buf_;
        filter_dirty_ = true;
    }

    TopicCategoryFilter filter_values[] = {
        TopicCategoryFilter::All,
        TopicCategoryFilter::Numeric,
        TopicCategoryFilter::Images,
        TopicCategoryFilter::Tf,
        TopicCategoryFilter::Diagnostics,
        TopicCategoryFilter::Active,
        TopicCategoryFilter::HighHz,
    };
    const char* filter_labels[] = {
        "All",
        "Numeric",
        "Images",
        "TF",
        "Diagnostics",
        "Active",
        "High Hz",
    };
    draw_wrapped_filter_chips(filter_values,
                              filter_labels,
                              std::size(filter_labels),
                              category_filter_,
                              [this](TopicCategoryFilter filter) { set_category_filter(filter); });

    // Column visibility settings gear.
    ImGui::Spacing();
    if (spectra::ui::widgets::toolbar_button(ui::Icon::Settings, "Column visibility"))
        ImGui::OpenPopup("##col_vis");
    if (ImGui::BeginPopup("##col_vis"))
    {
        ImGui::TextDisabled("Visible columns");
        ImGui::Separator();
        ImGui::Checkbox("Type", &col_show_type_);
        ImGui::Checkbox("Hz", &col_show_hz_);
        ImGui::Checkbox("Pubs", &col_show_pubs_);
        ImGui::Checkbox("Subs", &col_show_subs_);
        ImGui::Checkbox("BW", &col_show_bw_);
        ImGui::Checkbox("Age", &col_show_age_);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    spectra::ui::widgets::status_pill(
        std::format("{} topic{}", topics_.size(), topics_.size() == 1 ? "" : "s").c_str(),
        spectra::ui::theme().accent,
        false);

    ImGui::Separator();

    if (topics_.empty())
    {
        // Show skeleton rows during initial discovery (first ~60 frames ≈ 1s).
        if (disc_ && refresh_frames_ < 60)
        {
            refreshing_ = true;
            ++refresh_frames_;
            spectra::ui::widgets::skeleton_rows(6, 24.0f);
            spectra::ui::shell::end_panel();
            return;
        }
        refreshing_ = false;

        ImGui::Dummy(ImVec2(0.0f, std::max(12.0f, ImGui::GetContentRegionAvail().y * 0.20f)));
        const spectra::ui::widgets::EmptyStateAction actions[] = {
            {.label = "Refresh graph", .id = "refresh", .primary = true},
            {.label = "Connection settings", .id = "settings", .primary = false},
        };
        const int action =
            spectra::ui::widgets::empty_state(ui::Icon::Broadcast,
                                              "No topics detected",
                                              "Refresh the graph, check ROS_DOMAIN_ID, or verify "
                                              "that your ROS environment is sourced.",
                                              actions);
        if (action == 0 && disc_)
        {
            disc_->refresh();
            refresh_frames_ = 0;
        }
        spectra::ui::shell::end_panel();
        return;
    }
    refreshing_     = false;
    refresh_frames_ = 60;

    const float footer_h =
        ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const ImVec2 table_sz = {0.0f, std::max(64.0f, ImGui::GetContentRegionAvail().y - footer_h)};
    const bool   compact_list = ImGui::GetContentRegionAvail().x < 360.0f;
    if (compact_list)
    {
        draw_compact_topic_list(table_sz.y);
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        size_t                      active_count = 0;
        for (const auto& [name, st] : stats_map_)
        {
            if (st.active)
                ++active_count;
        }
        ImGui::TextDisabled("Active: %zu / %zu", active_count, topics_.size());
        spectra::ui::shell::end_panel();
        return;
    }

    // --- Column table ---
    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

    // Count visible optional columns: always Topic + optionally Type/Hz/Pubs/Subs/BW.
    const int n_cols = 2 + (col_show_type_ ? 1 : 0) + (col_show_hz_ ? 1 : 0)
                       + (col_show_pubs_ ? 1 : 0) + (col_show_subs_ ? 1 : 0)
                       + (col_show_bw_ ? 1 : 0) + (col_show_age_ ? 1 : 0);

    spectra::ui::shell::push_data_table_style();
    if (!ImGui::BeginTable("##topics", n_cols, kTableFlags, table_sz))
    {
        spectra::ui::shell::pop_data_table_style();
        spectra::ui::shell::end_panel();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);   // freeze header row
    ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch, 5.0f);
    if (col_show_type_)
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    if (col_show_hz_)
        ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    if (col_show_pubs_)
        ImGui::TableSetupColumn("Pubs", ImGuiTableColumnFlags_WidthFixed, 38.0f);
    if (col_show_subs_)
        ImGui::TableSetupColumn("Subs", ImGuiTableColumnFlags_WidthFixed, 38.0f);
    if (col_show_bw_)
        ImGui::TableSetupColumn("BW", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    if (col_show_age_)
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();

    // --- Rows (already holding topics_mutex_) ---
    std::vector<const TopicInfo*> favorite_topics;
    favorite_topics.reserve(topics_.size());
    {
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto& t : topics_)
        {
            TopicStats& st = stats_map_[t.name];
            if (is_favorite(t.name) && passes_topic_filter(t, st))
                favorite_topics.push_back(&t);
        }
    }
    if (!favorite_topics.empty())
    {
        std::sort(favorite_topics.begin(),
                  favorite_topics.end(),
                  [](const TopicInfo* a, const TopicInfo* b) { return a->name < b->name; });
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s Favorites", ui::icon_str(ui::Icon::Star));
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const TopicInfo* t : favorite_topics)
            draw_topic_row(*t, stats_map_[t->name]);
    }

    if (group_by_namespace_)
    {
        // Root-level loose topics first (e.g. /rosout at "/").
        for (const auto& tname : root_topics_)
        {
            if (is_favorite(tname))
                continue;
            auto it = std::find_if(topics_.begin(),
                                   topics_.end(),
                                   [&](const TopicInfo& ti) { return ti.name == tname; });
            if (it == topics_.end())
                continue;
            {
                std::lock_guard<std::mutex> lk_s(stats_mutex_);
                (void)stats_map_[tname];   // ensure entry exists
            }
            std::lock_guard<std::mutex> lk_s(stats_mutex_);
            if (!passes_topic_filter(*it, stats_map_[tname]))
                continue;
            draw_topic_row(*it, stats_map_[tname]);
        }
        // Namespace groups.
        for (const auto& ns : root_namespaces_)
        {
            draw_namespace_node(ns, 0);
        }
    }
    else
    {
        // Flat list, sorted by name.
        std::vector<const TopicInfo*> sorted;
        sorted.reserve(topics_.size());
        {
            std::lock_guard<std::mutex> lk_s(stats_mutex_);
            for (const auto& t : topics_)
            {
                if (is_favorite(t.name))
                    continue;
                if (passes_topic_filter(t, stats_map_[t.name]))
                    sorted.push_back(&t);
            }
        }
        std::sort(sorted.begin(),
                  sorted.end(),
                  [](const TopicInfo* a, const TopicInfo* b) { return a->name < b->name; });

        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto* t : sorted)
        {
            draw_topic_row(*t, stats_map_[t->name]);
        }
    }

    ImGui::EndTable();
    spectra::ui::shell::pop_data_table_style();

    // --- Status bar ---
    {
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        size_t                      active_count = 0;
        for (const auto& [name, st] : stats_map_)
        {
            if (st.active)
                ++active_count;
        }
        ImGui::TextDisabled("Active: %zu / %zu", active_count, topics_.size());
    }

    spectra::ui::shell::end_panel();
}

void TopicListPanel::draw_namespace_node(const std::string& ns, int /*depth*/)
{
    auto it = ns_tree_.find(ns);
    if (it == ns_tree_.end())
        return;

    NamespaceNode& node = it->second;

    // Check if any topic in this subtree passes the filter.
    bool any_visible = false;
    {
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto& tname : node.topic_names)
        {
            if (is_favorite(tname))
                continue;
            auto tit = std::find_if(topics_.begin(),
                                    topics_.end(),
                                    [&](const TopicInfo& ti) { return ti.name == tname; });
            if (tit != topics_.end() && passes_topic_filter(*tit, stats_map_[tname]))
            {
                any_visible = true;
                break;
            }
        }
        if (!any_visible)
        {
            for (const auto& child : node.children)
            {
                // Recurse to check.
                auto cit = ns_tree_.find(child);
                if (cit != ns_tree_.end())
                {
                    for (const auto& tname : cit->second.topic_names)
                    {
                        if (is_favorite(tname))
                            continue;
                        auto tit =
                            std::find_if(topics_.begin(),
                                         topics_.end(),
                                         [&](const TopicInfo& ti) { return ti.name == tname; });
                        if (tit != topics_.end() && passes_topic_filter(*tit, stats_map_[tname]))
                        {
                            any_visible = true;
                            break;
                        }
                    }
                }
                if (any_visible)
                    break;
            }
        }
    }
    if (!any_visible)
        return;

    // Namespace header row spans all columns.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    // Use SPECTRA_USE_IMGUI tree node.
    const ImGuiTreeNodeFlags tree_flags =
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;

    const bool open = ImGui::TreeNodeEx(node.label.c_str(), tree_flags);

    if (open)
    {
        // Leaf topics — lock stats_mutex_ only for this section.
        // Must release before recursing into child namespaces, because
        // std::mutex is non-recursive and the recursive call also locks it.
        {
            std::lock_guard<std::mutex> lk_s(stats_mutex_);
            for (const auto& tname : node.topic_names)
            {
                if (is_favorite(tname))
                    continue;
                auto tit = std::find_if(topics_.begin(),
                                        topics_.end(),
                                        [&](const TopicInfo& ti) { return ti.name == tname; });
                if (tit == topics_.end())
                    continue;
                if (!passes_topic_filter(*tit, stats_map_[tname]))
                    continue;
                draw_topic_row(*tit, stats_map_[tname]);
            }
        }

        // Child namespaces (recursive — stats_mutex_ must NOT be held here).
        std::vector<std::string> sorted_children = node.children;
        std::sort(sorted_children.begin(), sorted_children.end());
        for (const auto& child : sorted_children)
        {
            draw_namespace_node(child, 0);
        }

        ImGui::TreePop();
    }
}

void TopicListPanel::draw_compact_topic_list(float height)
{
    const ImVec2 child_size(0.0f, std::max(64.0f, height));
    ImGui::BeginChild("##compact_topics",
                      child_size,
                      false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    std::vector<const TopicInfo*> favorites;
    std::vector<const TopicInfo*> topics;
    favorites.reserve(topics_.size());
    topics.reserve(topics_.size());
    {
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto& topic : topics_)
        {
            TopicStats& stats = stats_map_[topic.name];
            if (!passes_topic_filter(topic, stats))
                continue;
            if (is_favorite(topic.name))
                favorites.push_back(&topic);
            else
                topics.push_back(&topic);
        }
    }

    auto by_name = [](const TopicInfo* a, const TopicInfo* b) { return a->name < b->name; };
    std::sort(favorites.begin(), favorites.end(), by_name);
    std::sort(topics.begin(), topics.end(), by_name);

    auto draw_section = [&](const char* label, const std::vector<const TopicInfo*>& items)
    {
        if (items.empty())
            return;
        ImGui::TextDisabled("%s", label);
        ImGui::Spacing();
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const TopicInfo* topic : items)
            draw_topic_card(*topic, stats_map_[topic->name]);
        ImGui::Spacing();
    };

    draw_section("Favorites", favorites);
    draw_section(favorites.empty() ? "Topics" : "All topics", topics);

    if (favorites.empty() && topics.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 24.0f));
        spectra::ui::widgets::empty_state(ui::Icon::Search,
                                          "No matching topics",
                                          "Try another search term or category filter.");
    }

    ImGui::Dummy(ImVec2(0.0f, spectra::ui::tokens::SPACE_2));
    ImGui::EndChild();
}

void TopicListPanel::draw_topic_card(const TopicInfo& info, const TopicStats& stats)
{
    const auto&       colors       = spectra::ui::theme();
    const float       width        = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float       title_w      = std::max(40.0f, width - 34.0f);
    const std::string display_name = fit_text(info.name, title_w);
    const ImVec2      title_sz     = ImGui::CalcTextSize(display_name.c_str());
    const float       row_h        = std::max(64.0f, 11.0f + title_sz.y + 6.0f + 24.0f + 9.0f);

    ImGui::PushID(info.name.c_str());
    const bool selected = (info.name == selected_topic_);
    const bool expanded = is_topic_expanded(info.name);

    const bool   pressed = ImGui::InvisibleButton("##topic_card", ImVec2(width, row_h));
    const bool   hovered = ImGui::IsItemHovered();
    const ImVec2 min     = ImGui::GetItemRectMin();
    const ImVec2 max     = ImGui::GetItemRectMax();
    if (pressed)
    {
        selected_topic_ = info.name;
        if (select_cb_)
            select_cb_(info.name);
    }

    if (hovered && ImGui::IsMouseDoubleClicked(0) && plot_cb_)
        plot_cb_(info.name);

    if (ImGui::BeginPopupContextItem("##topic_card_context"))
    {
        if (ImGui::MenuItem("Copy topic name"))
            ImGui::SetClipboardText(info.name.c_str());
        if (!info.types.empty() && ImGui::MenuItem("Copy topic type"))
            ImGui::SetClipboardText(info.types.front().c_str());
        ImGui::EndPopup();
    }

    if (drag_drop_)
    {
        FieldDragPayload payload;
        payload.topic_name = info.name;
        payload.field_path = "";
        payload.type_name  = info.types.empty() ? "" : info.types.front();
        payload.label      = info.name;
        drag_drop_->begin_drag_source(payload);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
        selected ? ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, 0.18f)
        : hovered
            ? ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.62f)
            : ImVec4(colors.bg_tertiary.r, colors.bg_tertiary.g, colors.bg_tertiary.b, 0.34f));
    dl->AddRectFilled(min, max, bg, spectra::ui::tokens::RADIUS_MD);
    dl->AddRect(min,
                max,
                ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                      colors.border_subtle.g,
                                                      colors.border_subtle.b,
                                                      selected ? 0.65f : 0.32f)),
                spectra::ui::tokens::RADIUS_MD);

    const ImVec4 status_color = stats.active ? kColorActive : kColorStale;
    dl->AddCircleFilled(ImVec2(min.x + 12.0f, min.y + 18.0f),
                        4.0f,
                        ImGui::ColorConvertFloat4ToU32(status_color));

    dl->AddText(
        ImVec2(min.x + 24.0f, min.y + 9.0f),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, 1.0f)),
        display_name.c_str());

    std::string type_leaf = info.types.empty() ? "-" : info.types.front();
    const auto  slash     = type_leaf.rfind('/');
    if (slash != std::string::npos)
        type_leaf = type_leaf.substr(slash + 1);
    const float       actions_w = 88.0f;
    const std::string meta =
        fit_text(std::format("{}   {} Hz", type_leaf, format_hz(stats.displayed_hz)),
                 std::max(48.0f, width - actions_w - 30.0f));
    const float meta_y = min.y + 11.0f + title_sz.y + 5.0f;
    dl->AddText(
        ImVec2(min.x + 24.0f, meta_y),
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_tertiary.r, colors.text_tertiary.g, colors.text_tertiary.b, 0.92f)),
        meta.c_str());

    const float button_y     = meta_y - 3.0f;
    float       button_x     = max.x - 8.0f;
    auto        place_button = [&]()
    {
        button_x -= 26.0f;
        ImGui::SetCursorScreenPos(ImVec2(button_x, button_y));
        button_x -= 2.0f;
    };

    if (plot_cb_)
    {
        place_button();
        if (spectra::ui::widgets::icon_button_small(ui::icon_str(ui::Icon::ChartLine),
                                                    "Quick plot"))
            plot_cb_(info.name);
    }
    if (toggle_favorite_cb_)
    {
        place_button();
        const bool fav = is_favorite(info.name);
        if (spectra::ui::widgets::icon_button_small(ui::icon_str(ui::Icon::Star),
                                                    fav ? "Remove favorite" : "Add favorite",
                                                    fav))
            toggle_favorite_cb_(info.name);
    }
    if (echo_panel_)
    {
        place_button();
        if (spectra::ui::widgets::icon_button_small(ui::icon_str(ui::Icon::Command),
                                                    expanded ? "Hide inline echo" : "Quick echo",
                                                    expanded))
        {
            set_topic_expanded(info.name, !expanded);
            selected_topic_ = info.name;
            if (select_cb_)
                select_cb_(info.name);
        }
    }

    if (hovered)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(info.name.c_str());
        if (!info.types.empty())
            ImGui::TextDisabled("%s", info.types.front().c_str());
        ImGui::EndTooltip();
    }

    ImGui::Dummy(ImVec2(width, 5.0f));
    ImGui::PopID();
}

void TopicListPanel::draw_topic_row(const TopicInfo& info, TopicStats& stats)
{
    ImGui::TableNextRow();

    // Column 0: Name
    ImGui::TableSetColumnIndex(0);

    const bool is_selected = (info.name == selected_topic_);
    const bool is_expanded = is_topic_expanded(info.name);
    if (is_selected)
    {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ImGui::ColorConvertFloat4ToU32(kColorSelected));
    }

    const int button_count =
        (plot_cb_ ? 1 : 0) + (echo_panel_ ? 1 : 0) + (toggle_favorite_cb_ ? 1 : 0);
    constexpr float kActionBtnSize = 24.0f;
    constexpr float kActionBtnGap  = 2.0f;
    const float     actions_w      = button_count > 0
                                         ? static_cast<float>(button_count) * kActionBtnSize
                                      + static_cast<float>(button_count - 1) * kActionBtnGap
                                         : 0.0f;

    // Expand/collapse toggle for inline echo (small triangle arrow).
    if (echo_panel_)
    {
        ImGui::PushID((info.name + "#chevron").c_str());
        const char* arrow = is_expanded ? ui::icon_str(ui::Icon::ChevronDown)
                                        : ui::icon_str(ui::Icon::ChevronRight);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        if (ImGui::SmallButton(arrow))
        {
            if (is_expanded)
            {
                set_topic_expanded(info.name, false);
            }
            else
            {
                set_topic_expanded(info.name, true);
                cached_echo_msgs_.erase(info.name);
                selected_topic_ = info.name;
                if (select_cb_)
                    select_cb_(info.name);
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::SameLine();
    }

    const ImVec4 dot_col = stats.active ? kColorActive : kColorStale;
    ImGui::TextColored(dot_col, "%s", ui::icon_str(ui::Icon::Circle));
    ImGui::SameLine();

    const float text_x        = ImGui::GetCursorPosX();
    const float avail_w       = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float actions_right = text_x + avail_w;
    const float name_w        = std::max(16.0f, avail_w - actions_w - 4.0f);

    auto [ns, leaf] = split_namespace(info.name);
    (void)ns;
    const std::string display_name =
        fit_text(group_by_namespace_ ? leaf : info.name, std::max(8.0f, name_w - 4.0f));

    if (ImGui::Selectable(display_name.c_str(), is_selected, 0, ImVec2(name_w, 0)))
    {
        selected_topic_ = info.name;
        if (select_cb_)
            select_cb_(info.name);
    }
    if (ImGui::BeginPopupContextItem("##topic_row_context"))
    {
        if (ImGui::MenuItem("Copy topic name"))
            ImGui::SetClipboardText(info.name.c_str());
        if (!info.types.empty() && ImGui::MenuItem("Copy topic type"))
            ImGui::SetClipboardText(info.types.front().c_str());
        ImGui::EndPopup();
    }

    if (drag_drop_)
    {
        FieldDragPayload payload;
        payload.topic_name = info.name;
        payload.field_path = "";
        payload.type_name  = info.types.empty() ? "" : info.types[0];
        payload.label      = info.name;
        drag_drop_->begin_drag_source(payload);
        std::string ctx_id = std::string("##tctx_") + info.name;
        drag_drop_->show_context_menu(payload, ctx_id.c_str());
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
    {
        if (plot_cb_)
            plot_cb_(info.name);
    }

    const bool name_hovered = ImGui::IsItemHovered();

    if (button_count > 0)
    {
        ImGui::SameLine(0.0f, 4.0f);
        const float actions_x = actions_right - actions_w;
        if (std::abs(ImGui::GetCursorPosX() - actions_x) > 0.5f)
            ImGui::SetCursorPosX(actions_x);
        ImGui::PushID(info.name.c_str());
        if (echo_panel_)
        {
            if (spectra::ui::widgets::icon_button_small(
                    ui::icon_str(ui::Icon::Command),
                    is_expanded ? "Hide inline echo" : "Quick echo",
                    is_expanded))
            {
                const bool next = !is_topic_expanded(info.name);
                set_topic_expanded(info.name, next);
                selected_topic_ = info.name;
                if (select_cb_)
                    select_cb_(info.name);
            }
            if (plot_cb_ || toggle_favorite_cb_)
                ImGui::SameLine(0.0f, 2.0f);
        }
        if (toggle_favorite_cb_)
        {
            const bool fav = is_favorite(info.name);
            if (spectra::ui::widgets::icon_button_small(ui::icon_str(ui::Icon::Star),
                                                        fav ? "Remove favorite" : "Add favorite",
                                                        fav))
                toggle_favorite_cb_(info.name);
            if (plot_cb_)
                ImGui::SameLine(0.0f, 2.0f);
        }
        if (plot_cb_)
        {
            if (spectra::ui::widgets::icon_button_small(ui::icon_str(ui::Icon::ChartLine),
                                                        "Quick plot (default numeric field)"))
                plot_cb_(info.name);
        }
        ImGui::PopID();
    }

    if (name_hovered)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(info.name.c_str());
        if (!info.types.empty())
            ImGui::TextDisabled("%s", info.types[0].c_str());
        ImGui::EndTooltip();
    }

    // Column 1: Type (first type, abbreviated + colored).
    int next_col = 1;
    if (col_show_type_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        if (!info.types.empty())
        {
            const std::string& t         = info.types[0];
            const ImVec4       tc        = topic_type_color(t);
            const auto         slash     = t.rfind('/');
            std::string        leaf_type = (slash != std::string::npos) ? t.substr(slash + 1) : t;
            const float        type_w    = std::max(16.0f, ImGui::GetContentRegionAvail().x - 2.0f);
            leaf_type                    = fit_text(leaf_type, type_w);
            ImGui::TextColored(tc, "%s", leaf_type.c_str());
        }
        else
        {
            ImGui::TextDisabled("-");
        }
    }

    // Column: Hz + sparkline.
    if (col_show_hz_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        const float       col_w   = ImGui::GetContentRegionAvail().x;
        const float       spark_w = std::min(col_w * 0.5f, 36.0f);
        const float       spark_h = ImGui::GetTextLineHeight() * 0.85f;
        const std::string hz_str  = format_hz(stats.displayed_hz);
        if (stats.displayed_hz > 0.0)
            ImGui::TextUnformatted(hz_str.c_str());
        else
            ImGui::TextDisabled("%s", hz_str.c_str());
        if (!stats.hz_history.empty() && spark_w > 4.0f)
        {
            ImGui::SameLine(0.0f, 4.0f);
            draw_hz_sparkline(stats.hz_history, spark_w, spark_h);
        }
    }

    // Column: Pubs.
    if (col_show_pubs_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        ImGui::Text("%d",
                    displayed_endpoint_count(info.publisher_count, info.local_publisher_count));
    }

    // Column: Subs.
    if (col_show_subs_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        ImGui::Text("%d",
                    displayed_endpoint_count(info.subscriber_count, info.local_subscriber_count));
    }

    // Column: BW.
    if (col_show_bw_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        const std::string bw_str = format_bw(stats.bandwidth_bps);
        if (stats.bandwidth_bps > 0.0)
            ImGui::TextUnformatted(bw_str.c_str());
        else
            ImGui::TextDisabled("%s", bw_str.c_str());
    }

    if (col_show_age_)
    {
        ImGui::TableSetColumnIndex(next_col++);
        const std::string age_str = format_age(now_ns(), stats.last_msg_ns);
        if (stats.active)
            ImGui::TextUnformatted(age_str.c_str());
        else
            ImGui::TextDisabled("%s", age_str.c_str());
    }

    // --- Inline echo expansion (rqt-style) ---
    if (is_expanded && echo_panel_)
    {
        draw_inline_echo(info.name);
    }
}

bool TopicListPanel::is_topic_expanded(const std::string& topic_name) const
{
    return expanded_echo_topics_.find(topic_name) != expanded_echo_topics_.end();
}

void TopicListPanel::set_topic_expanded(const std::string& topic_name, bool expanded)
{
    if (expanded)
    {
        expanded_echo_topics_.insert(topic_name);
    }
    else
    {
        expanded_echo_topics_.erase(topic_name);
        cached_echo_msgs_.erase(topic_name);
    }
}

// ---------------------------------------------------------------------------
// draw_inline_echo — render latest echo message fields inside the topic table
// ---------------------------------------------------------------------------

void TopicListPanel::draw_inline_echo(const std::string& topic_name)
{
    // Refresh the cached message from the echo panel.
    if (echo_panel_->topic_name() == topic_name)
    {
        cached_echo_msgs_[topic_name] = echo_panel_->latest_message();
    }

    auto  it              = cached_echo_msgs_.find(topic_name);
    auto* cached_echo_msg = (it != cached_echo_msgs_.end()) ? it->second.get() : nullptr;

    // Insert a full-width row for the echo content.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    if (!cached_echo_msg || cached_echo_msg->fields.empty())
    {
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("(waiting for data...)");
        ImGui::Unindent(24.0f);
        return;
    }

    ImGui::Indent(24.0f);

    size_t idx = 0;
    while (idx < cached_echo_msg->fields.size())
    {
        draw_echo_field(topic_name, cached_echo_msg->fields[idx], idx, cached_echo_msg->fields);
    }

    ImGui::Unindent(24.0f);
}

// ---------------------------------------------------------------------------
// draw_echo_field — render one field node in the inline echo tree
// ---------------------------------------------------------------------------

void TopicListPanel::draw_echo_field(const std::string&                 topic_name,
                                     EchoFieldValue&                    fv,
                                     size_t&                            idx,
                                     const std::vector<EchoFieldValue>& all_fields)
{
    const float indent = static_cast<float>(fv.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f)
        ImGui::Indent(indent);

    switch (fv.kind)
    {
        case EchoFieldValue::Kind::NestedHead:
        {
            ImGui::TextColored(kEchoNested, "%s:", fv.display_name.c_str());
            ++idx;
            const int parent_depth = fv.depth;
            while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
            {
                auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                draw_echo_field(topic_name, child, idx, all_fields);
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayHead:
        {
            const std::string label = std::format("{}  [{} items]", fv.display_name, fv.array_len);
            const std::string tree_id = std::string("##ie_") + topic_name + "_" + fv.path;
            const bool        open    = ImGui::TreeNodeEx(tree_id.c_str(),
                                                ImGuiTreeNodeFlags_SpanFullWidth,
                                                "%s",
                                                label.c_str());
            fv.is_open                = open;
            ++idx;
            const int parent_depth = fv.depth;
            if (open)
            {
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                {
                    auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                    draw_echo_field(topic_name, child, idx, all_fields);
                }
                ImGui::TreePop();
            }
            else
            {
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                    ++idx;
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayElement:
        {
            const std::string sel_id = std::format("##topic_monitor_sel_{}", fv.path);
            ImGui::Selectable(
                sel_id.c_str(),
                false,
                ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                ImVec2(0, ImGui::GetTextLineHeight()));
            if (ImGui::BeginPopupContextItem("##field_context"))
            {
                if (ImGui::MenuItem("Copy field path"))
                    ImGui::SetClipboardText(fv.path.c_str());
                if (ImGui::MenuItem("Copy topic name"))
                    ImGui::SetClipboardText(topic_name.c_str());
                ImGui::EndPopup();
            }

            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name;
                payload.field_path = fv.path;
                payload.type_name  = selected_topic_ == topic_name && echo_panel_
                                         ? echo_panel_->type_name()
                                         : std::string();
                payload.label      = FieldDragPayload::make_label(topic_name, fv.path);
                drag_drop_->begin_drag_source(payload);
                const std::string ctx_id = std::format("##topic_monitor_ctx_{}", fv.path);
                drag_drop_->show_context_menu(payload, ctx_id.c_str());
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && field_plot_cb_)
            {
                const std::string type = selected_topic_ == topic_name && echo_panel_
                                             ? echo_panel_->type_name()
                                             : std::string();
                field_plot_cb_(topic_name, fv.path, type);
            }

            ImGui::SameLine();
            ImGui::TextColored(kEchoArray, "%-8s", fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoNumeric,
                               "%s",
                               TopicEchoPanel::format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Numeric:
        {
            const std::string sel_id = std::format("##topic_monitor_sel_{}", fv.path);
            ImGui::Selectable(
                sel_id.c_str(),
                false,
                ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                ImVec2(0, ImGui::GetTextLineHeight()));
            if (ImGui::BeginPopupContextItem("##field_context"))
            {
                if (ImGui::MenuItem("Copy field path"))
                    ImGui::SetClipboardText(fv.path.c_str());
                if (ImGui::MenuItem("Copy topic name"))
                    ImGui::SetClipboardText(topic_name.c_str());
                ImGui::EndPopup();
            }

            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name;
                payload.field_path = fv.path;
                payload.type_name  = selected_topic_ == topic_name && echo_panel_
                                         ? echo_panel_->type_name()
                                         : std::string();
                payload.label      = FieldDragPayload::make_label(topic_name, fv.path);
                drag_drop_->begin_drag_source(payload);
                const std::string ctx_id = std::format("##topic_monitor_ctx_{}", fv.path);
                drag_drop_->show_context_menu(payload, ctx_id.c_str());
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && field_plot_cb_)
            {
                const std::string type = selected_topic_ == topic_name && echo_panel_
                                             ? echo_panel_->type_name()
                                             : std::string();
                field_plot_cb_(topic_name, fv.path, type);
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoNumeric,
                               "%s",
                               TopicEchoPanel::format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Text:
        {
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoText, "\"%s\"", fv.text.c_str());
            ++idx;
            break;
        }
    }

    if (indent > 0.0f)
        ImGui::Unindent(indent);
}

#else   // !SPECTRA_USE_IMGUI

void TopicListPanel::draw(bool* /*p_open*/) {}
void TopicListPanel::draw_namespace_node(const std::string& /*ns*/, int /*depth*/) {}
void TopicListPanel::draw_topic_row(const TopicInfo& /*info*/, TopicStats& /*stats*/) {}
void TopicListPanel::draw_inline_echo(const std::string& /*topic_name*/) {}
void TopicListPanel::draw_echo_field(const std::string& /*topic_name*/,
                                     EchoFieldValue& /*fv*/,
                                     size_t& /*idx*/,
                                     const std::vector<EchoFieldValue>& /*all*/)
{
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
