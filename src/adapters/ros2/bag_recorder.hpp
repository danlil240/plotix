#pragma once

// BagRecorder — rosbag2 recording backend for Spectra ROS2 adapter.
//
// Records ROS2 topic messages to a .db3 (SQLite) or .mcap bag file.
//
// Features:
//   - start(path, topics) / stop() lifecycle
//   - .db3 or .mcap output (auto-detected from path extension)
//   - Topic filter: record a specific set of topics, or all topics ("*")
//   - Auto-split by file size (bytes) and/or duration (seconds)
//   - Recording indicator: is_recording(), recorded_message_count(),
//     recorded_bytes(), elapsed_seconds()
//   - Thread-safe: all public methods are safe to call from any thread
//   - Split callback: fires each time a new split file is started
//
// Gated behind SPECTRA_ROS2_BAG — if that define is absent the entire header
// compiles to empty stubs so callers can #include it unconditionally.
//
// Usage:
//   BagRecorder recorder(node);
//   recorder.set_max_size_bytes(512 * 1024 * 1024);  // 512 MB auto-split
//   recorder.set_max_duration_seconds(60.0);          // 60 s auto-split
//   if (!recorder.start("/path/to/output.db3", {"topic1", "topic2"})) {
//       // handle error via recorder.last_error()
//   }
//   // ... record frames ...
//   recorder.stop();
//
// Thread-safety:
//   All public methods lock an internal mutex.  start() and stop() are safe
//   to call from any thread.  Callbacks are invoked with the mutex held —
//   keep them short.
//
// Error handling:
//   start() returns false and sets last_error() on failure.
//   Subsequent write errors are accumulated in last_error() and can be
//   checked at any time.

#ifdef SPECTRA_ROS2_BAG

    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <string>
    #include <unordered_map>
    #include <vector>

    #include <rclcpp/rclcpp.hpp>
    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/storage_options.hpp>

    #include "generic_subscription_compat.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// RecordingState — current lifecycle state of the recorder.
// ---------------------------------------------------------------------------

enum class RecordingState
{
    Idle,        // not recording
    Recording,   // actively writing messages
    Stopping,    // stop() called, finishing current file
};

// ---------------------------------------------------------------------------
// RecordingSplitInfo — metadata emitted on each auto-split event.
// ---------------------------------------------------------------------------

struct RecordingSplitInfo
{
    std::string closed_path;             // path of the file just closed
    std::string new_path;                // path of the newly opened file
    uint32_t    split_index{0};          // 0-based split number (0 = no split yet)
    uint64_t    messages_in_closed{0};   // message count in the closed file
    uint64_t    bytes_in_closed{0};      // approximate bytes in the closed file
};

// ---------------------------------------------------------------------------
// BagRecorder — main recorder class.
// ---------------------------------------------------------------------------

class BagRecorder
{
   public:
    // Construct with a shared ROS2 node used to create subscriptions.
    explicit BagRecorder(rclcpp::Node::SharedPtr node);
    ~BagRecorder();

    // Non-copyable, non-movable (owns subscriptions and a writer).
    BagRecorder(const BagRecorder&)            = delete;
    BagRecorder& operator=(const BagRecorder&) = delete;
    BagRecorder(BagRecorder&&)                 = delete;
    BagRecorder& operator=(BagRecorder&&)      = delete;

    // ------------------------------------------------------------------
    // Configuration (must be set before start())
    // ------------------------------------------------------------------

    // Maximum bag file size in bytes before auto-split.
    // 0 = no size limit (default).
    void     set_max_size_bytes(uint64_t bytes) noexcept;
    uint64_t max_size_bytes() const noexcept;

    // Maximum bag duration in seconds before auto-split.
    // 0.0 = no duration limit (default).
    void   set_max_duration_seconds(double seconds) noexcept;
    double max_duration_seconds() const noexcept;

    // Storage plugin: "sqlite3" (default) or "mcap".
    // Overrides auto-detection from path extension when non-empty.
    void               set_storage_id(const std::string& id);
    const std::string& storage_id() const noexcept;

    // QoS reliability for subscriptions: true = reliable (default), false = best-effort.
    void set_reliable_qos(bool reliable) noexcept;
    bool reliable_qos() const noexcept;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    // Start recording to `bag_path`.
    //
    // `topics` is the set of topic names to record.  Pass {"*"} or an
    // empty vector to record all currently-known topics (a wildcard
    // subscription is NOT possible in ROS2 — the recorder subscribes to
    // each topic in `topics` individually; pass discovered topic names).
    //
    // The storage ID is auto-detected from the path extension unless
    // overridden by set_storage_id():
    //   .db3  → "sqlite3"
    //   .mcap → "mcap"
    //   other → "sqlite3" (default)
    //
    // Returns true on success; false on failure (see last_error()).
    // Calling start() while already recording returns false immediately.
    bool start(const std::string& bag_path, const std::vector<std::string>& topics);

    // Stop recording and flush the writer.
    // Blocks until all pending writes complete and the file is finalized.
    // No-op if not recording.
    void stop();

    // ------------------------------------------------------------------
    // State / Statistics
    // ------------------------------------------------------------------

    RecordingState state() const noexcept;
    bool           is_recording() const noexcept;

    // Path passed to the most recent start() call.
    std::string recording_path() const;

    // Current output path (may differ from recording_path() after splits).
    std::string current_path() const;

    // Number of messages written since start().
    uint64_t recorded_message_count() const noexcept;

    // Approximate total bytes written since start().
    uint64_t recorded_bytes() const noexcept;

    // Wall-clock seconds since start() (0.0 if not recording).
    double elapsed_seconds() const noexcept;

    // Current 0-based split index (0 = first file, 1 = first split, …).
    uint32_t split_index() const noexcept;

    // Topics currently being recorded (empty if not recording).
    std::vector<std::string> recorded_topics() const;

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    // Called each time an auto-split occurs (with mutex held — keep short).
    using SplitCallback = std::function<void(const RecordingSplitInfo&)>;
    void set_split_callback(SplitCallback cb);

    // Called on each message write error.
    using ErrorCallback = std::function<void(const std::string& error)>;
    void set_error_callback(ErrorCallback cb);

    // ------------------------------------------------------------------
    // Error handling
    // ------------------------------------------------------------------

    const std::string& last_error() const noexcept;
    void               clear_error() noexcept;

   private:
    // Create subscriptions for each topic in topics_.
    bool subscribe_topics();

    // Message callback for all subscriptions (executor thread).
    void on_message(const std::string&                       topic_name,
                    const std::string&                       message_type,
                    sub_compat::SerializedMessageCallbackArg msg);

    // Check auto-split conditions and perform split if needed.
    // Caller must hold mutex_.
    void check_and_split();

    // Perform the actual split: close current file, open next.
    // Caller must hold mutex_.
    void do_split();

    // Open a new writer at `path` with `storage_id`.
    // Caller must hold mutex_.
    bool open_writer(const std::string& path, const std::string& sid);

    // Close the current writer cleanly.
    // Caller must hold mutex_.
    void close_writer();

    // Derive the split path for split_index_ > 0.
    // E.g. "/path/to/foo.db3" → "/path/to/foo_split001.db3"
    std::string make_split_path(const std::string& base_path, uint32_t idx) const;

    // Detect storage ID from file extension.
    static std::string detect_storage_id(const std::string& path);

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    rclcpp::Node::SharedPtr node_;
    mutable std::mutex      mutex_;

    // Configuration (immutable after start())
    uint64_t    max_size_bytes_{0};
    double      max_duration_seconds_{0.0};
    std::string storage_id_override_;
    bool        reliable_qos_{true};

    // Runtime state
    RecordingState state_{RecordingState::Idle};
    std::string    base_path_;      // path as passed to start()
    std::string    current_path_;   // path of the currently-open file
    uint32_t       split_index_{0};
    uint64_t       message_count_{0};       // total messages since start()
    uint64_t       bytes_total_{0};         // total bytes since start()
    uint64_t       bytes_since_split_{0};   // bytes in the current file

    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point split_start_time_;

    std::vector<std::string> topics_;

    // rosbag2 writer
    std::unique_ptr<rosbag2_cpp::Writer> writer_;

    // Registered topic types (topic_name → message_type)
    std::unordered_map<std::string, std::string> topic_type_map_;

    // Active subscriptions
    std::vector<rclcpp::GenericSubscription::SharedPtr> subscriptions_;

    // Callbacks
    SplitCallback split_cb_;
    ErrorCallback error_cb_;

    // Last error string
    std::string last_error_;
};

}   // namespace spectra::adapters::ros2

#else   // SPECTRA_ROS2_BAG not defined — empty stubs so headers compile cleanly.

    #include <cstdint>
    #include <functional>
    #include <string>
    #include <vector>

namespace spectra::adapters::ros2
{

enum class RecordingState
{
    Idle,
    Recording,
    Stopping
};

struct RecordingSplitInfo
{
    std::string closed_path;
    std::string new_path;
    uint32_t    split_index{0};
    uint64_t    messages_in_closed{0};
    uint64_t    bytes_in_closed{0};
};

class BagRecorder
{
   public:
    // Accept a void* node placeholder so callers don't need rclcpp in scope.
    explicit BagRecorder(void* /*node*/) {}

    void               set_max_size_bytes(uint64_t) noexcept {}
    uint64_t           max_size_bytes() const noexcept { return 0; }
    void               set_max_duration_seconds(double) noexcept {}
    double             max_duration_seconds() const noexcept { return 0.0; }
    void               set_storage_id(const std::string&) {}
    const std::string& storage_id() const noexcept { return storage_id_; }
    void               set_reliable_qos(bool) noexcept {}
    bool               reliable_qos() const noexcept { return true; }

    bool start(const std::string&, const std::vector<std::string>&) { return false; }
    void stop() {}

    RecordingState           state() const noexcept { return RecordingState::Idle; }
    bool                     is_recording() const noexcept { return false; }
    std::string              recording_path() const { return {}; }
    std::string              current_path() const { return {}; }
    uint64_t                 recorded_message_count() const noexcept { return 0; }
    uint64_t                 recorded_bytes() const noexcept { return 0; }
    double                   elapsed_seconds() const noexcept { return 0.0; }
    uint32_t                 split_index() const noexcept { return 0; }
    std::vector<std::string> recorded_topics() const { return {}; }

    using SplitCallback = std::function<void(const RecordingSplitInfo&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    void set_split_callback(SplitCallback) {}
    void set_error_callback(ErrorCallback) {}

    const std::string& last_error() const noexcept { return last_error_; }
    void               clear_error() noexcept { last_error_.clear(); }

   private:
    std::string storage_id_;
    std::string last_error_{"BagRecorder: built without SPECTRA_ROS2_BAG"};
};

}   // namespace spectra::adapters::ros2

#endif   // SPECTRA_ROS2_BAG
