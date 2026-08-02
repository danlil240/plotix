#pragma once

// TopicListPanel — ImGui topic monitor panel for the ROS2 adapter.
//
// Displays a tree of ROS2 topics grouped by namespace with live Hz and
// bandwidth columns.  Integrates with Spectra's ImGui docking system as a
// standard dockable window.
//
// Features:
//   - Tree view: topics grouped by namespace (collapsible)
//   - Columns: Name, Type, Hz, Pubs, Subs, BW
//   - Live Hz / BW computed over a rolling 1-second window
//   - Search / filter bar (filters by topic name substring)
//   - Status dot: green (active, seen msg in last 2 s), gray (stale/no msg)
//   - notify_message() must be called by the subscriber layer each time a
//     message arrives on a topic so that Hz/BW statistics are maintained
//
// Thread-safety:
//   notify_message() is safe to call from the ROS2 executor thread.
//   draw() must be called from the ImGui render thread only.
//   All shared state is protected by an internal mutex.
//
// Typical usage:
//   TopicListPanel panel;
//   panel.set_topic_discovery(&discovery);   // wire to TopicDiscovery
//
//   // In ROS2 executor callback:
//   panel.notify_message(topic_name, msg_size_bytes);
//
//   // In ImGui render loop:
//   panel.draw();

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "topic_discovery.hpp"
#include "ui/field_drag_drop.hpp"

namespace spectra::adapters::ros2
{

// Forward declarations.
class TopicEchoPanel;
struct EchoMessage;
struct EchoFieldValue;

// ---------------------------------------------------------------------------
// TopicStats — rolling Hz/BW statistics for one topic
// ---------------------------------------------------------------------------

struct TopicStats
{
    // Ring of arrival timestamps (nanoseconds since epoch, rolling 1 s window)
    std::deque<int64_t> timestamps;
    // Ring of message sizes (bytes) — same indices as timestamps
    std::deque<size_t> sizes;

    // Last computed values (updated each draw frame by prune_and_compute())
    double hz{0.0};              // messages per second (raw)
    double displayed_hz{0.0};    // EMA-smoothed Hz shown in the UI
    double bandwidth_bps{0.0};   // bytes per second

    // True if a message arrived within a grace period (adaptive to frequency)
    bool active{false};

    // Timestamp of the most recently received message (ns)
    int64_t last_msg_ns{0};

    // Cumulative counters
    uint64_t total_messages{0};
    uint64_t total_bytes{0};

    // Rolling Hz history for sparkline rendering (sampled once per second,
    // newest at back, capped at HZ_HISTORY_LEN entries).
    static constexpr size_t HZ_HISTORY_LEN = 30;
    std::deque<float>       hz_history;

    // Wall-clock ns of the last time hz_history was sampled.
    int64_t last_hz_sample_ns{0};

    // Push a new arrival.  Called from executor thread (lock held by caller).
    void push(int64_t now_ns, size_t bytes);

    // Remove events older than window_ns, recompute hz/bw/active.
    // Called from render thread (lock held by caller).
    void prune_and_compute(int64_t now_ns, int64_t window_ns = 1'000'000'000LL);
};

// ---------------------------------------------------------------------------
// TopicListPanel
// ---------------------------------------------------------------------------

class TopicListPanel
{
   public:
    enum class TopicCategoryFilter
    {
        All,
        Numeric,
        Images,
        Tf,
        Diagnostics,
        Active,
        HighHz,
    };

    struct ColumnVisibility
    {
        bool show_type{true};
        bool show_hz{true};
        bool show_pubs{true};
        bool show_subs{true};
        bool show_bw{true};
        bool show_age{true};
    };

    TopicListPanel();
    ~TopicListPanel();

    TopicListPanel(const TopicListPanel&)            = delete;
    TopicListPanel& operator=(const TopicListPanel&) = delete;
    TopicListPanel(TopicListPanel&&)                 = delete;
    TopicListPanel& operator=(TopicListPanel&&)      = delete;

    // ---------- wiring -------------------------------------------------------

    // Wire to a TopicDiscovery instance.  The pointer must outlive this panel.
    // If nullptr, the panel renders with whatever topics were manually set.
    void            set_topic_discovery(TopicDiscovery* disc);
    TopicDiscovery* topic_discovery() const { return disc_; }

    // ---------- statistics injection -----------------------------------------

    // Notify the panel that a message arrived on `topic_name` with the given
    // serialised byte length.  Thread-safe — safe to call from executor thread.
    void notify_message(const std::string& topic_name, size_t bytes);

    // ---------- ImGui rendering ----------------------------------------------

    // Render the panel into the current ImGui context.
    // Call once per frame from the render thread.
    // `p_open` — if non-null, a close button is shown; set to false to close.
    void draw(bool* p_open = nullptr);

    // ---------- accessors (render-thread only) --------------------------------

    // Currently selected topic name ("" if none).
    const std::string& selected_topic() const { return selected_topic_; }

    // Callback fired when the user selects a topic (single-click).
    using SelectCallback = std::function<void(const std::string& topic_name)>;
    void set_select_callback(SelectCallback cb) { select_cb_ = std::move(cb); }

    // Callback fired when the user double-clicks a topic (plot request).
    using PlotCallback = std::function<void(const std::string& topic_name)>;
    void set_plot_callback(PlotCallback cb) { plot_cb_ = std::move(cb); }

    using FavoriteQueryCallback  = std::function<bool(const std::string& topic_name)>;
    using FavoriteToggleCallback = std::function<void(const std::string& topic_name)>;
    void set_favorite_callbacks(FavoriteQueryCallback  is_favorite,
                                FavoriteToggleCallback toggle_favorite)
    {
        is_favorite_cb_     = std::move(is_favorite);
        toggle_favorite_cb_ = std::move(toggle_favorite);
    }

    // Fired when user double-clicks a numeric field (topic, field_path, type).
    using FieldPlotCallback = std::function<
        void(const std::string& topic, const std::string& field, const std::string& type)>;
    void set_field_plot_callback(FieldPlotCallback cb) { field_plot_cb_ = std::move(cb); }

    // ---------- drag-and-drop (C3) -------------------------------------------

    // Wire a FieldDragDrop controller so that topic rows become drag sources
    // (dragging a whole topic with empty field_path) and get right-click menus.
    // Pass nullptr to disable.
    void           set_drag_drop(FieldDragDrop* dd) { drag_drop_ = dd; }
    FieldDragDrop* drag_drop() const { return drag_drop_; }

    // ---------- inline echo (rqt-style expand) -------------------------------

    // Wire to an echo panel.  When a topic row is expanded, the latest
    // message from that topic is shown inline.  The pointer must outlive
    // this panel.  Pass nullptr to disable inline echo.
    void            set_echo_panel(TopicEchoPanel* ep) { echo_panel_ = ep; }
    TopicEchoPanel* echo_panel() const { return echo_panel_; }

    // Expanded inline-echo state for render-thread use and tests.
    bool   is_topic_expanded(const std::string& topic_name) const;
    size_t expanded_topic_count() const { return expanded_echo_topics_.size(); }
    void   set_topic_expanded(const std::string& topic_name, bool expanded);

    // ---------- configuration ------------------------------------------------

    // Window title (default: "ROS2 Topics").
    void               set_title(const std::string& title) { title_ = title; }
    const std::string& title() const { return title_; }

    // Stale threshold: topic is considered stale if no message received in
    // this many milliseconds (default 2000 ms).
    void set_stale_threshold_ms(int ms) { stale_threshold_ms_ = ms; }
    int  stale_threshold_ms() const { return stale_threshold_ms_; }

    // Hz/BW rolling window (default 1000 ms).
    void set_stats_window_ms(int ms) { stats_window_ms_ = ms; }
    int  stats_window_ms() const { return stats_window_ms_; }

    // If true, topics are grouped into namespace nodes (default true).
    void set_group_by_namespace(bool v) { group_by_namespace_ = v; }
    bool group_by_namespace() const { return group_by_namespace_; }

    void             set_column_visibility(const ColumnVisibility& visibility);
    ColumnVisibility column_visibility() const;

    // ---------- testing helpers (no ImGui dependency) ------------------------

    // Force-set the known topic list (bypasses TopicDiscovery).
    // Primarily used in unit tests.
    void set_topics(const std::vector<TopicInfo>& topics);

    // Return a snapshot of all stats maps (for testing).
    struct StatsSnapshot
    {
        double   hz;
        double   bandwidth_bps;
        bool     active;
        uint64_t total_messages;
    };
    StatsSnapshot stats_for(const std::string& topic_name) const;

    // Number of topics currently tracked (with or without discovery).
    size_t topic_count() const;

    // Number of topics that match the current filter string.
    size_t filtered_topic_count() const;

    // Current filter string.
    const std::string& filter() const { return filter_str_; }

    // Set filter programmatically (for testing).
    void set_filter(const std::string& f);

    TopicCategoryFilter category_filter() const { return category_filter_; }
    void                set_category_filter(TopicCategoryFilter filter);

   private:
    // ---------- internal helpers ---------------------------------------------

    // Rebuild namespace_tree_ from topics_.  Called whenever topics_ changes.
    void rebuild_tree();

    // Split a topic name into its namespace prefix and leaf name.
    // e.g. "/robot/arm/joint_states" → ("/robot/arm", "joint_states")
    static std::pair<std::string, std::string> split_namespace(const std::string& topic_name);

    // Render one namespace group node (recursive).
    void draw_namespace_node(const std::string& ns, int depth);

    // Render one topic row (columns already pushed).
    void draw_topic_row(const TopicInfo& info, TopicStats& stats);

    // Render one topic in the adaptive compact list layout.
    void draw_topic_card(const TopicInfo& info, const TopicStats& stats);

    // Render a compact card list for narrow side-panel widths.
    void draw_compact_topic_list(float height);

    bool passes_topic_filter(const TopicInfo& info, const TopicStats& stats) const;
    bool matches_text_filter(const TopicInfo& info) const;
    bool is_favorite(const std::string& topic_name) const;

    // Render inline echo fields for the expanded row.
    void draw_inline_echo(const std::string& topic_name);

    // Render one echo field node (reuses echo panel's flat-tree logic).
    void draw_echo_field(const std::string&                 topic_name,
                         EchoFieldValue&                    fv,
                         size_t&                            idx,
                         const std::vector<EchoFieldValue>& all_fields);

    // Format Hz as a compact string ("12.3", "—" if 0).
    static std::string format_hz(double hz);

    // Format bytes/sec as compact string ("1.2 KB/s", "—" if 0).
    static std::string format_bw(double bps);

    static std::string format_age(int64_t now_ns, int64_t last_msg_ns);

    // Returns now in nanoseconds (wall clock).
    static int64_t now_ns();

    // ---------- tree structure -----------------------------------------------

    // One node in the namespace tree.
    struct NamespaceNode
    {
        std::string              ns;            // full namespace prefix
        std::string              label;         // last segment (for display)
        std::vector<std::string> topic_names;   // leaf topics under this ns
        std::vector<std::string> children;      // child namespace prefixes
        bool                     open{true};    // ImGui tree state
    };

    // ---------- data ---------------------------------------------------------

    TopicDiscovery* disc_{nullptr};

    mutable std::mutex                          stats_mutex_;
    std::unordered_map<std::string, TopicStats> stats_map_;   // topic → stats

    mutable std::mutex     topics_mutex_;
    std::vector<TopicInfo> topics_;   // snapshot from last discovery refresh

    // Namespace tree (render-thread only — rebuilt under topics_mutex_).
    std::unordered_map<std::string, NamespaceNode> ns_tree_;
    std::vector<std::string>                       root_namespaces_;   // top-level ns prefixes
    std::vector<std::string>                       root_topics_;       // topics at "/" level

    // UI state (render-thread only).
    std::string         selected_topic_;
    char                filter_buf_[256]{};
    std::string         filter_str_;
    TopicCategoryFilter category_filter_{TopicCategoryFilter::All};
    bool                group_by_namespace_{true};
    int                 stale_threshold_ms_{2000};
    int                 stats_window_ms_{1000};
    std::string         title_{"ROS2 Topics"};

    // Column visibility (render-thread only).
    bool col_show_type_{true};
    bool col_show_hz_{true};
    bool col_show_pubs_{false};
    bool col_show_subs_{false};
    bool col_show_bw_{false};
    bool col_show_age_{true};

    // Cached filtered list (rebuilt each frame if dirty).
    mutable bool                     filter_dirty_{true};
    mutable std::vector<std::string> filtered_names_;   // filtered topic names

    // Callbacks.
    SelectCallback         select_cb_;
    PlotCallback           plot_cb_;
    FieldPlotCallback      field_plot_cb_;
    FavoriteQueryCallback  is_favorite_cb_;
    FavoriteToggleCallback toggle_favorite_cb_;

    // Drag-and-drop controller (optional, not owned).
    FieldDragDrop* drag_drop_{nullptr};

    // Inline echo state (render-thread only).
    TopicEchoPanel*                                               echo_panel_{nullptr};
    std::unordered_set<std::string>                               expanded_echo_topics_;
    std::unordered_map<std::string, std::unique_ptr<EchoMessage>> cached_echo_msgs_;

    // Refreshing state: show skeleton rows during initial topic discovery.
    bool refreshing_{false};
    int  refresh_frames_{0};
};

}   // namespace spectra::adapters::ros2
