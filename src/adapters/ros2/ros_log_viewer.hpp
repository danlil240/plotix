#pragma once

// RosLogViewer — /rosout log subscription + circular buffer backend (F5).
//
// Subscribes to /rosout (rcl_interfaces/msg/Log) and keeps a 10 000-entry
// circular buffer of LogEntry records.  Thread-safe: the ROS2 executor thread
// calls the subscription callback; the render thread reads via snapshot(),
// filtered_snapshot(), or the filter API.
//
// Severity levels match rcl_interfaces/msg/Log constants:
//   UNSET = 0, DEBUG = 10, INFO = 20, WARN = 30, ERROR = 40, FATAL = 50
//
// Filtering:
//   - Minimum severity level gate
//   - Node name substring filter (empty = pass all)
//   - Message regex filter (empty = pass all; invalid regex = pass all)
//   - All three filters are AND-combined
//
// Typical usage (render thread):
//   RosLogViewer viewer(node);
//   viewer.subscribe();
//
//   // per-frame:
//   auto entries = viewer.filtered_snapshot();
//   // or:
//   viewer.for_each_filtered([](const LogEntry& e){ ... });

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

#include <rcl_interfaces/msg/log.hpp>
#include <rclcpp/rclcpp.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// LogSeverity
// ---------------------------------------------------------------------------

enum class LogSeverity : uint8_t
{
    Unset = 0,
    Debug = 10,
    Info  = 20,
    Warn  = 30,
    Error = 40,
    Fatal = 50,
};

// Convert a uint8_t rcl level to LogSeverity (rounds down to known levels).
LogSeverity severity_from_rcl(uint8_t level);

// Short name: "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "UNSET".
const char* severity_name(LogSeverity s);

// Single-character code: 'D', 'I', 'W', 'E', 'F', '?'.
char severity_char(LogSeverity s);

// ---------------------------------------------------------------------------
// LogEntry — one captured /rosout message.
// ---------------------------------------------------------------------------

struct LogEntry
{
    uint64_t    seq{0};             // monotonic receive index (0-based)
    int64_t     stamp_ns{0};        // ROS2 message stamp in nanoseconds
    double      wall_time_s{0.0};   // wall clock at receive (for display)
    LogSeverity severity{LogSeverity::Info};
    std::string node;       // publishing node name
    std::string message;    // log message text
    std::string file;       // source file (may be empty)
    std::string function;   // source function (may be empty)
    uint32_t    line{0};    // source line (may be 0)
};

// ---------------------------------------------------------------------------
// LogFilter — describes active filter state (pure data, no locking).
// ---------------------------------------------------------------------------

struct LogFilter
{
    // Minimum severity to show (inclusive).  Entries below this are hidden.
    LogSeverity min_severity{LogSeverity::Debug};

    // Node name substring filter.  Case-insensitive substring match.
    // Empty string passes all entries.
    std::string node_filter;

    // Message regex filter string.  Empty string passes all entries.
    // Invalid regex is treated as empty (pass all), with error stored in
    // regex_error.
    std::string message_regex_str;

    // (Output) Error from the last time message_regex_str was compiled.
    // Empty = compiled successfully or string is empty.
    mutable std::string regex_error;

    // Test whether an entry passes all active filters.
    // Compiles the regex each call — use cached variant for hot paths.
    bool passes(const LogEntry& e) const;

    // Same as passes() but uses a pre-compiled regex (nullptr = skip regex
    // check).  Call compile_regex() once per filter change.
    bool passes_compiled(const LogEntry& e, const std::regex* compiled_re) const;

    // Compile message_regex_str into out_re.  Returns true on success.
    // On failure fills regex_error and leaves out_re unchanged.
    bool compile_regex(std::regex& out_re) const;
};

// Case-insensitive substring search helper (public for testing).
bool ci_contains(const std::string& haystack, const std::string& needle);

// ---------------------------------------------------------------------------
// RosLogViewer
// ---------------------------------------------------------------------------

class RosLogViewer
{
   public:
    static constexpr size_t DEFAULT_CAPACITY = 10'000;
    static constexpr size_t MIN_CAPACITY     = 1;
    static constexpr size_t MAX_CAPACITY     = 100'000;

    // node — ROS2 node used to create the /rosout subscription.
    //        Must outlive this object.
    explicit RosLogViewer(rclcpp::Node::SharedPtr node);
    ~RosLogViewer();

    RosLogViewer(const RosLogViewer&)            = delete;
    RosLogViewer& operator=(const RosLogViewer&) = delete;
    RosLogViewer(RosLogViewer&&)                 = delete;
    RosLogViewer& operator=(RosLogViewer&&)      = delete;

    // -----------------------------------------------------------------------
    // Subscription lifecycle
    // -----------------------------------------------------------------------

    // Create the /rosout subscription (or named_topic).
    // May be called multiple times; drops and recreates the subscription.
    // topic — topic to subscribe (default "/rosout").
    void subscribe(const std::string& topic = "/rosout");

    // Cancel the subscription (stops receiving new messages).
    void unsubscribe();

    bool is_subscribed() const;

    const std::string& topic() const { return topic_; }

    // -----------------------------------------------------------------------
    // Buffer control
    // -----------------------------------------------------------------------

    // Set ring buffer capacity (clamped to [MIN_CAPACITY, MAX_CAPACITY]).
    // Existing entries beyond new capacity are discarded (oldest first).
    void   set_capacity(size_t n);
    size_t capacity() const { return capacity_; }

    // Discard all buffered entries.
    void clear();

    // Pause / resume receiving new entries into the buffer.
    void pause() { paused_.store(true, std::memory_order_relaxed); }
    void resume() { paused_.store(false, std::memory_order_relaxed); }
    bool is_paused() const { return paused_.load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Filter API (render thread — all filter ops are not locked, set from
    // render thread only while draw() runs)
    // -----------------------------------------------------------------------

    // Replace the entire filter (invalidates compiled regex cache).
    void             set_filter(const LogFilter& f);
    const LogFilter& filter() const { return filter_; }

    // Convenience setters.
    void set_min_severity(LogSeverity s)
    {
        filter_.min_severity = s;
        regex_dirty_         = true;
    }
    void set_node_filter(const std::string& s)
    {
        filter_.node_filter = s;
        regex_dirty_        = true;
    }
    void set_message_regex(const std::string& s)
    {
        filter_.message_regex_str = s;
        regex_dirty_              = true;
    }

    LogSeverity        min_severity() const { return filter_.min_severity; }
    const std::string& node_filter() const { return filter_.node_filter; }
    const std::string& message_regex() const { return filter_.message_regex_str; }
    const std::string& regex_error() const { return filter_.regex_error; }

    // -----------------------------------------------------------------------
    // Read API (render thread — uses internal mutex for thread safety)
    // -----------------------------------------------------------------------

    // Thread-safe snapshot of ALL buffered entries (unfiltered), oldest first.
    std::vector<LogEntry> snapshot() const;

    // Thread-safe snapshot of entries matching the current filter, oldest first.
    std::vector<LogEntry> filtered_snapshot();

    // Iterate all entries that pass the current filter, oldest first.
    // Callback: void(const LogEntry&).
    // Holds the internal lock for the duration — keep callback fast.
    void for_each_filtered(const std::function<void(const LogEntry&)>& cb);

    // -----------------------------------------------------------------------
    // Statistics
    // -----------------------------------------------------------------------

    // Total entries received (including ones overwritten by the ring).
    uint64_t total_received() const { return total_received_.load(std::memory_order_relaxed); }

    // Current count in the ring (≤ capacity).
    size_t entry_count() const;

    // Count entries per severity in the current buffer (unfiltered).
    // Returns array indexed by LogSeverity integer / 10 (0..5).
    std::array<uint32_t, 6> severity_counts() const;

    // -----------------------------------------------------------------------
    // Inject helpers (unit tests — no ROS2 node needed)
    // -----------------------------------------------------------------------

    // Push an entry directly, bypassing subscription (thread-safe).
    void inject(LogEntry e);

    // Build a LogEntry from raw /rosout field values (public for testing).
    static LogEntry make_entry(uint64_t    seq,
                               uint8_t     level,
                               int32_t     stamp_sec,
                               uint32_t    stamp_nanosec,
                               double      wall_time_s,
                               std::string node,
                               std::string message,
                               std::string file     = {},
                               std::string function = {},
                               uint32_t    line     = 0);

    // Format stamp_ns as "sec.nsec" (9 digits).
    static std::string format_stamp(int64_t stamp_ns);

    // Format wall_time_s as "HH:MM:SS.mmm".
    static std::string format_wall_time(double wall_time_s);

   private:
    // Called from executor thread when a /rosout message arrives.
    void on_message(const rcl_interfaces::msg::Log& msg);

    // Recompile the regex if dirty (render thread only).
    void maybe_recompile_regex();

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    rclcpp::Node::SharedPtr node_;
    std::string             topic_{"/rosout"};

    // Subscription (executor thread owns create/destroy, protected by sub_mutex_).
    mutable std::mutex                                        sub_mutex_;
    rclcpp::Subscription<rcl_interfaces::msg::Log>::SharedPtr subscription_;

    // Circular ring buffer (protected by ring_mutex_).
    mutable std::mutex    ring_mutex_;
    std::vector<LogEntry> ring_;           // preallocated to capacity_
    size_t                ring_head_{0};   // next write index
    size_t                ring_size_{0};   // current fill (≤ capacity_)
    size_t                capacity_{DEFAULT_CAPACITY};

    // Counters.
    std::atomic<uint64_t> total_received_{0};
    uint64_t              next_seq_{0};   // protected by ring_mutex_

    // Pause flag.
    std::atomic<bool> paused_{false};

    // Filter (render thread only — no lock needed).
    LogFilter  filter_;
    std::regex compiled_re_;
    bool       regex_valid_{false};   // true if compiled_re_ matches filter_.message_regex_str
    bool       regex_dirty_{true};    // true when recompile needed
};

}   // namespace spectra::adapters::ros2
