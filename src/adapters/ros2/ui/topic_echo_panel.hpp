#pragma once

// TopicEchoPanel — ImGui live message echo panel for the ROS2 adapter.
//
// Displays the most recent messages received on a selected topic as an
// expandable field tree.  Mirrors `ros2 topic echo` in a dockable ImGui
// window.
//
// Features:
//   - Subscribes to any topic selected from the topic list panel
//   - Expandable field tree showing every field in the message schema
//   - Numeric fields: value displayed inline
//   - String fields: value displayed inline (truncated at 128 chars)
//   - Array fields: shown as "[N items]" with expandable sub-tree
//   - Keeps a ring buffer of the last 100 received messages
//   - Pause / Resume / Clear controls
//   - Display rate capped at 30 Hz (ImGui throttling, not subscription)
//   - Message index and ROS2 timestamp shown per-message header
//
// Thread-safety:
//   set_topic() and the internal message callback are called from the ROS2
//   executor thread; all shared state is protected by a mutex.
//   draw() must be called from the ImGui render thread only.
//
// Typical usage:
//   MessageIntrospector intr;
//   TopicEchoPanel panel(node, intr);
//   panel.set_topic("/cmd_vel", "geometry_msgs/msg/Twist");
//
//   // In ImGui render loop (once per frame):
//   panel.draw();

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "generic_subscription_compat.hpp"
#include "message_introspector.hpp"
#include "ui/field_drag_drop.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// EchoFieldValue — one extracted field value from a deserialized message.
// ---------------------------------------------------------------------------

struct EchoFieldValue
{
    std::string path;           // dot-separated field path (e.g. "linear.x")
    std::string display_name;   // leaf name for the tree label
    int         depth{0};       // nesting depth (0 = top-level)

    // Value variants (only one is meaningful; discriminated by kind).
    enum class Kind
    {
        Numeric,
        Text,
        ArrayHead,
        ArrayElement,
        NestedHead
    };
    Kind kind{Kind::Numeric};

    double      numeric{0.0};     // for Kind::Numeric / Kind::ArrayElement
    std::string text;             // for Kind::Text
    int         array_len{0};     // for Kind::ArrayHead: element count
    bool        is_open{false};   // tree-node open state (render thread only)
};

// ---------------------------------------------------------------------------
// EchoMessage — one captured message snapshot.
// ---------------------------------------------------------------------------

struct EchoMessage
{
    uint64_t                    seq{0};               // monotonic receive counter
    int64_t                     timestamp_ns{0};      // ROS2 header stamp or wall clock
    double                      wall_time_s{0.0};     // wall clock at receive (for display rate)
    double                      bag_time_sec{-1.0};   // >= 0 when captured from bag playback
    std::vector<EchoFieldValue> fields;               // flat pre-expanded field list
};

// ---------------------------------------------------------------------------
// TopicEchoPanel
// ---------------------------------------------------------------------------

class TopicEchoPanel
{
   public:
    // node  — ROS2 node used to create subscriptions (must outlive this panel)
    // intr  — MessageIntrospector (must outlive this panel)
    TopicEchoPanel(rclcpp::Node::SharedPtr node, MessageIntrospector& intr);
    ~TopicEchoPanel();

    TopicEchoPanel(const TopicEchoPanel&)            = delete;
    TopicEchoPanel& operator=(const TopicEchoPanel&) = delete;
    TopicEchoPanel(TopicEchoPanel&&)                 = delete;
    TopicEchoPanel& operator=(TopicEchoPanel&&)      = delete;

    // ---------- topic selection -------------------------------------------

    // Subscribe to a new topic.  Drops any existing subscription first.
    // topic_name: fully-qualified topic name, e.g. "/cmd_vel"
    // type_name:  ROS2 message type, e.g. "geometry_msgs/msg/Twist"
    // Safe to call from the render thread; subscription is (re)created on the
    // ROS2 executor thread via the node.
    void set_topic(const std::string& topic_name, const std::string& type_name);

    const std::string& topic_name() const { return topic_name_; }
    const std::string& type_name() const { return type_name_; }

    // True if a subscription is currently active.
    bool is_subscribed() const { return static_cast<bool>(subscription_); }

    // ---------- playback controls ----------------------------------------

    // Pause receiving new messages (ring buffer stops growing).
    void pause();

    // Resume receiving messages.
    void resume();

    bool is_paused() const { return paused_.load(std::memory_order_acquire); }

    // Discard all captured messages.
    void clear();

    // ---------- ImGui rendering ------------------------------------------

    // Render the panel into the current ImGui context.
    // Call once per frame from the render thread.
    // p_open — if non-null, a close button is shown.
    void draw(bool* p_open = nullptr);

    // When false (default), follow workspace topic selection via sync_workspace().
    // When true, user pinned a topic manually — auto-follow is suppressed.
    bool manually_pinned() const { return manually_pinned_; }
    void set_manually_pinned(bool pinned) { manually_pinned_ = pinned; }

    // Auto-switch topic when workspace selection changes (unless pinned).
    void sync_workspace(const std::string& topic, const std::string& type, bool selection_changed);

    // ---------- configuration --------------------------------------------

    enum class EchoViewMode : uint8_t
    {
        Tree,
        Raw,
        Json
    };

    EchoViewMode view_mode() const { return view_mode_; }
    bool         echo_live() const { return echo_live_; }

    void set_view_mode(EchoViewMode mode) { view_mode_ = mode; }
    void set_echo_live(bool live)
    {
        echo_live_ = live;
        if (echo_live_)
            selected_msg_idx_ = -1;
    }

    void               set_title(const std::string& t) { title_ = t; }
    const std::string& title() const { return title_; }

    // ---------- drag-and-drop (C3) --------------------------------------

    // Wire a FieldDragDrop controller so that numeric fields become drag
    // sources and get a right-click "Plot" context menu.
    // Pass nullptr to disable drag-and-drop.
    void           set_drag_drop(FieldDragDrop* dd) { drag_drop_ = dd; }
    FieldDragDrop* drag_drop() const { return drag_drop_; }

    // ---------- message arrival callback ---------------------------------

    // Called from the executor thread each time a message arrives on the
    // subscribed topic.  Signature: void(topic_name, serialised_bytes)
    // Thread-safe: the callback is stored atomically-guarded.
    using MessageCallback = std::function<void(const std::string& topic, size_t bytes)>;
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }

    // ---------- hover highlight callback --------------------------------

    // Called when the user hovers a numeric field row.
    // Signature: void(const std::string& topic, const std::string& field_path)
    // Callers can use this to highlight matching plot series.
    using HoverCallback =
        std::function<void(const std::string& topic, const std::string& field_path)>;
    void set_hover_callback(HoverCallback cb) { hover_cb_ = std::move(cb); }

    // Called when the cursor leaves all field rows (field_path will be "").
    // Reuses HoverCallback with an empty field_path to signal "no hover".
    void clear_hover_callback() { hover_cb_ = {}; }

    // Currently hovered field path ("" if none).
    const std::string& hovered_field() const { return hovered_field_; }

    // Maximum messages kept in the ring buffer (default 100).
    void   set_max_messages(size_t n);
    size_t max_messages() const { return max_messages_; }

    // Maximum display rate in Hz (default 30).  Messages are still received
    // at full rate; the panel only redraws at this rate.
    void   set_display_hz(double hz) { display_interval_s_ = (hz > 0.0) ? 1.0 / hz : 0.0; }
    double display_hz() const
    {
        return (display_interval_s_ > 0.0) ? 1.0 / display_interval_s_ : 0.0;
    }

    // ---------- testing helpers (no ImGui dependency) -------------------

    // Number of messages currently in the ring buffer.
    size_t message_count() const;

    // Snapshot of all messages (thread-safe copy).
    std::vector<EchoMessage> messages_snapshot() const;

    // Most recently received message, or nullptr if none.
    // Returns a copy; thread-safe.
    std::unique_ptr<EchoMessage> latest_message() const;

    // Total messages received (including ones pruned from ring buffer).
    uint64_t total_received() const { return total_received_.load(std::memory_order_acquire); }

    // Inject a pre-built message directly (for unit tests without a ROS2 node).
    void inject_message(EchoMessage msg);

    // Bag playback: echo shows the message nearest the shared playhead.
    void   set_bag_echo_mode(bool enabled);
    bool   bag_echo_mode() const { return bag_echo_mode_; }
    void   set_bag_playhead(double bag_time_sec);
    double bag_playhead() const { return bag_playhead_sec_; }

#ifdef SPECTRA_ROS2_BAG
    // Ingest one deserialized bag message (called from BagPlayer on inject).
    void ingest_bag_message(const struct BagMessage& msg,
                            MessageIntrospector&     intr,
                            int64_t                  bag_start_time_ns);
#endif

    // Build an EchoMessage from a raw deserialized message pointer and schema.
    // Public for testability.
    static EchoMessage build_echo_message(const void*          msg_ptr,
                                          const MessageSchema& schema,
                                          uint64_t             seq,
                                          int64_t              timestamp_ns,
                                          double               wall_time_s);

    // Format a timestamp_ns value as "sec.nsec" string.
    static std::string format_timestamp(int64_t ns);

    // Format a numeric value compactly.
    static std::string format_numeric(double v);

    // Serialize an echo snapshot for clipboard / raw view.
    static std::string format_message_raw(const EchoMessage& msg);
    static std::string format_message_json(const EchoMessage& msg);
    static std::string field_value_text(const EchoFieldValue& fv);

   private:
    // Called from executor thread when a message arrives.
    void on_message(sub_compat::SerializedMessageCallbackArg raw_msg);

    // Extract wall-clock time in seconds.
    static double wall_time_s_now();

    // Recursively walk FieldDescriptors, appending EchoFieldValues.
    static void extract_fields(const void*                         msg_ptr,
                               const std::vector<FieldDescriptor>& fields,
                               std::vector<EchoFieldValue>&        out,
                               int                                 depth);

    // Extract a single field value from msg_ptr at given offset/type.
    static EchoFieldValue make_scalar_value(const void*            msg_ptr,
                                            const FieldDescriptor& fd,
                                            int                    depth);

    // ---------- ImGui draw helpers (SPECTRA_USE_IMGUI guard in .cpp) ----

    void draw_controls();
    void draw_view_mode_bar();
    void draw_message_list(const std::vector<EchoMessage>& snap, int display_idx);
    void draw_message_content(const EchoMessage& msg);
    void draw_message_tree(EchoMessage& msg);
    void draw_field_node(EchoFieldValue&                    fv,
                         size_t&                            idx,
                         const std::vector<EchoFieldValue>& all_fields);

    // ---------- members --------------------------------------------------

    rclcpp::Node::SharedPtr node_;
    MessageIntrospector&    intr_;

    std::string topic_name_;
    std::string type_name_;
    std::string title_{"ROS2 Echo"};

    // Subscription (executor thread owns creation/destruction; protected by
    // sub_mutex_).
    mutable std::mutex                     sub_mutex_;
    rclcpp::GenericSubscription::SharedPtr subscription_;
    std::shared_ptr<const MessageSchema>   schema_;

    // Ring buffer of captured messages (protected by ring_mutex_).
    mutable std::mutex       ring_mutex_;
    std::vector<EchoMessage> ring_;   // circular, newest at back
    size_t                   max_messages_{100};

    // Counters.
    std::atomic<uint64_t> total_received_{0};
    uint64_t              next_seq_{0};   // protected by ring_mutex_

    // Pause state.
    std::atomic<bool> paused_{false};

    // Display rate throttle (render thread only).
    double display_interval_s_{1.0 / 30.0};
    double last_draw_time_s_{0.0};

    // Selected message index for detail view (-1 = latest).
    int selected_msg_idx_{-1};

    // Whether to show the detail pane (vs just the list).
    bool show_detail_{true};

    // Drag-and-drop controller (optional, not owned).
    FieldDragDrop* drag_drop_{nullptr};

    // Message arrival callback (fires from executor thread).
    MessageCallback message_cb_;

    // Hover highlight state (render-thread only).
    HoverCallback hover_cb_;
    std::string   hovered_field_;
    std::string   prev_hovered_field_;   // to detect changes and fire callback once

    bool manually_pinned_{false};

    EchoViewMode view_mode_{EchoViewMode::Tree};
    bool         echo_live_{true};
    char         message_filter_[96]{};

    bool   bag_echo_mode_{false};
    double bag_playhead_sec_{0.0};
    bool   bag_playhead_follow_{true};
    int    bag_manual_msg_idx_{-1};

    int find_nearest_bag_message_index(const std::vector<EchoMessage>& snap) const;
};

}   // namespace spectra::adapters::ros2
