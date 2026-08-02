#pragma once

// BagPlayer — rosbag2 playback engine for Spectra ROS2 adapter.
//
// Plays back a bag through the same RosPlotManager pipeline as live data,
// with full transport control and TimelineEditor integration for scrubbing.
//
// Architecture overview:
//   - BagReader handles raw bag I/O (sequential + seek)
//   - BagPlayer wraps BagReader with a clock, rate control, and state machine
//   - Messages are fed into RosPlotManager via inject_sample() — bypasses live
//     subscriptions so bag and live data can coexist in the same manager
//   - TimelineEditor (optional) is used as the scrub bar; BagPlayer drives the
//     playhead and registers topic-activity track bands
//
// State machine:
//   Stopped ──play()──► Playing ──pause()──► Paused ──play()──► Playing
//      ▲                   │                    │
//      └─────stop()────────┴────────stop()──────┘
//
// Rate control:
//   set_rate(r): playback speed multiplier, clamped [0.1, 10.0].
//   r=1.0  → real-time
//   r=0.5  → half speed
//   r=2.0  → double speed
//
// Feed mechanism:
//   BagPlayer calls RosPlotManager::inject_sample(topic, field_path, t, value)
//   for every message it reads.  The injected timestamp is the bag message
//   time (converted from ns to seconds) so the X axis shows bag time, not
//   wall-clock time.
//
// Thread-safety:
//   All public API is thread-safe (internal mutex).
//   advance() must be called from the render thread.
//   topic_activity_bands() may be called from any thread.
//
// Gated behind SPECTRA_ROS2_BAG for the full implementation; stubs are
// provided for the no-bag build so callers can include unconditionally.

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bag_reader.hpp"
#include "message_introspector.hpp"

// Forward declarations — avoids pulling ROS2 / Spectra render headers here.
namespace spectra::adapters::ros2
{
class RosPlotManager;
class SubplotManager;
}   // namespace spectra::adapters::ros2

namespace spectra
{
class TimelineEditor;
}

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// PlayerState — bag player state machine.
// ---------------------------------------------------------------------------
enum class PlayerState
{
    Stopped,
    Playing,
    Paused,
};

// ---------------------------------------------------------------------------
// TopicActivityBand — time intervals where a topic has messages in the bag.
// Used to draw coloured bands on the TimelineEditor track.
// ---------------------------------------------------------------------------
struct TopicActivityBand
{
    std::string topic;
    // List of [start_sec, end_sec] intervals (merged, sorted).
    // Populated by scan_activity() after the bag is opened.
    struct Interval
    {
        double start_sec;
        double end_sec;
    };
    std::vector<Interval> intervals;

    // Track id assigned in the TimelineEditor (0 = not registered).
    uint32_t timeline_track_id{0};
};

// ---------------------------------------------------------------------------
// BagPlayerConfig — construction-time options.
// ---------------------------------------------------------------------------
struct BagPlayerConfig
{
    // Playback rate multiplier.  Clamped [0.1, 10.0].
    double rate{1.0};

    // Loop mode: restart at bag start when end is reached.
    bool loop{false};

    // Number of activity-scan buckets per topic for band generation.
    // Higher = more precise bands; 0 = disable activity scan.
    uint32_t activity_buckets{500};

    // Max messages to inject per advance() call.
    // Prevents single-frame bursts from blocking the render thread.
    uint32_t max_inject_per_frame{2000};
};

// ---------------------------------------------------------------------------
// BagPlayer — main playback engine.
// ---------------------------------------------------------------------------
class BagPlayer
{
   public:
    // Minimum / maximum playback rate.
    static constexpr double MIN_RATE = 0.1;
    static constexpr double MAX_RATE = 10.0;

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    // Construct without opening a bag.
    // plot_mgr — must outlive BagPlayer; used to inject bag samples.
    // intr     — must outlive BagPlayer; used to decode message fields.
    // config   — optional player configuration.
    explicit BagPlayer(RosPlotManager&      plot_mgr,
                       MessageIntrospector& intr,
                       BagPlayerConfig      config = {});

    ~BagPlayer();

    BagPlayer(const BagPlayer&)            = delete;
    BagPlayer& operator=(const BagPlayer&) = delete;
    BagPlayer(BagPlayer&&)                 = delete;
    BagPlayer& operator=(BagPlayer&&)      = delete;

    // ------------------------------------------------------------------
    // Bag lifecycle
    // ------------------------------------------------------------------

    // Open a bag file (.db3 or .mcap).
    // Closes any previously open bag first.
    // Optionally restrict playback to the given topics (empty = all).
    // Returns true on success; false sets last_error().
    bool open(const std::string& bag_path, const std::vector<std::string>& topics = {});

    // Close the bag and reset to Stopped state.
    void close();

    bool        is_open() const noexcept;
    std::string bag_path() const;
    std::string last_error() const;

    // Bag metadata accessors (valid after open()).
    const BagMetadata& metadata() const noexcept;
    double             duration_sec() const noexcept;
    double             start_time_sec() const noexcept;

    // ------------------------------------------------------------------
    // Transport controls
    // ------------------------------------------------------------------

    void play();
    void pause();
    void stop();
    void toggle_play();

    PlayerState state() const noexcept;
    bool        is_playing() const noexcept;
    bool        is_paused() const noexcept;
    bool        is_stopped() const noexcept;

    // ------------------------------------------------------------------
    // Seek
    // ------------------------------------------------------------------

    // Seek to an absolute bag time offset (seconds from bag start).
    // Clamps to [0, duration_sec()].
    // Does NOT change state (if Playing, continues from new position).
    bool seek(double offset_sec);

    // Seek to the beginning.
    bool seek_begin();

    // Seek to fraction [0.0, 1.0] of bag duration.
    bool seek_fraction(double fraction);

    // Step forward by one step_size_sec() without starting playback.
    void step_forward();

    // Step backward by one step_size_sec() without starting playback.
    void step_backward();

    // Step size used by step_forward/step_backward.
    double step_size_sec() const { return step_size_sec_; }
    void   set_step_size(double sec);

    // ------------------------------------------------------------------
    // Rate control
    // ------------------------------------------------------------------

    double rate() const noexcept;
    void   set_rate(double r);

    // ------------------------------------------------------------------
    // Loop mode
    // ------------------------------------------------------------------

    bool loop() const noexcept;
    void set_loop(bool enabled);

    // ------------------------------------------------------------------
    // Playhead position (seconds from bag start)
    // ------------------------------------------------------------------

    double playhead_sec() const noexcept;
    double progress() const noexcept;   // [0.0, 1.0]

    // ------------------------------------------------------------------
    // Topic activity bands
    // ------------------------------------------------------------------

    // Returns activity bands for all topics in the bag (populated after open()).
    // Thread-safe snapshot.
    std::vector<TopicActivityBand> topic_activity_bands() const;

    // Returns the activity band for a specific topic, or nullptr if not found.
    const TopicActivityBand* activity_band(const std::string& topic) const;

    // ------------------------------------------------------------------
    // TimelineEditor integration
    // ------------------------------------------------------------------

    // Attach a TimelineEditor for scrub bar display.
    // BagPlayer will:
    //   - Set duration to bag duration
    //   - Register one track per topic (topic activity band)
    //   - Keep the playhead in sync during advance()
    //   - React to scrub callbacks from the editor
    // Pass nullptr to detach.
    void                     set_timeline_editor(spectra::TimelineEditor* editor);
    spectra::TimelineEditor* timeline_editor() const;

    // ------------------------------------------------------------------
    // Frame update (render thread)
    // ------------------------------------------------------------------

    // Advance playback by dt seconds (wall-clock elapsed time).
    // Reads and injects bag messages for the time window [current, current + dt*rate].
    // Called once per frame from the render thread.
    // Returns true if playback is still active (Playing or Paused).
    bool advance(double dt);

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------

    // Called when player state changes.
    using StateCallback = std::function<void(PlayerState)>;
    // Called when the playhead moves (seconds from bag start).
    using PlayheadCallback = std::function<void(double)>;
    // Called each time a message is injected (topic, bag_time_sec, value).
    using MessageCallback =
        std::function<void(const std::string& topic, double bag_time_sec, double value)>;
    // Called once per bag message before field extraction (echo / log panels).
    using RawMessageCallback = std::function<void(const BagMessage&)>;

    void set_on_state_change(StateCallback cb);
    void set_on_playhead(PlayheadCallback cb);
    void set_on_message(MessageCallback cb);
    void set_on_raw_message(RawMessageCallback cb);

    // ------------------------------------------------------------------
    // Configuration accessors
    // ------------------------------------------------------------------

    uint32_t max_inject_per_frame() const { return config_.max_inject_per_frame; }
    void     set_max_inject_per_frame(uint32_t n);

    // Total messages injected since the last open() or seek_begin().
    uint64_t total_injected() const noexcept;

    // Optional: attach a SubplotManager so bag data is injected into
    // the rendered subplots rather than into standalone PlotManager figures.
    void set_subplot_manager(SubplotManager* mgr);

   private:
    // ------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------

    // Read and inject messages from bag_time_ns_cursor_ up to end_ns.
    // Returns the number of messages injected.
    uint32_t inject_until(int64_t end_ns);

    // Inject a single BagMessage via MessageIntrospector → RosPlotManager.
    void inject_message(const BagMessage& msg);

    // Build activity bands from a quick metadata scan (or a full sequential
    // pass when the bag stores no per-topic timing metadata).
    void scan_activity();

    // Register topic bands as tracks in the attached TimelineEditor.
    void register_timeline_tracks();

    // Update TimelineEditor playhead from current bag position.
    void sync_timeline_playhead();

    // State transition helper: sets state_, fires callback.
    void set_state(PlayerState s);

    // Clamp offset_sec to bag range; return clamped value.
    double clamp_offset(double sec) const noexcept;

    // Convert bag start-relative seconds → absolute bag ns.
    int64_t offset_to_ns(double offset_sec) const noexcept;

    // Convert absolute bag ns → start-relative seconds.
    double ns_to_offset(int64_t ns) const noexcept;

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    mutable std::mutex mutex_;

    RosPlotManager&      plot_mgr_;
    MessageIntrospector& intr_;
    BagPlayerConfig      config_;

    BagReader   reader_;
    BagMetadata metadata_;
    bool        open_{false};
    std::string bag_path_;
    std::string last_error_;

    // Playback state
    PlayerState state_{PlayerState::Stopped};
    double      playhead_sec_{0.0};   // seconds from bag start
    double      rate_{1.0};
    bool        loop_{false};
    double      step_size_sec_{0.1};

    // Bag clock: absolute ns of current read position.
    int64_t bag_time_ns_{0};   // == metadata_.start_time_ns + playhead in ns

    // Activity bands (populated in scan_activity()).
    std::vector<TopicActivityBand> activity_bands_;
    // Fast lookup: topic → index in activity_bands_
    std::unordered_map<std::string, size_t> band_index_;

    // TimelineEditor (optional, not owned).
    spectra::TimelineEditor* timeline_editor_{nullptr};
    bool                     timeline_scrub_in_progress_{false};

    // Statistics.
    uint64_t total_injected_{0};

    // Lookahead: cached message read past the time window.
    // Avoids expensive seek-back (rosbag2 reopens the DB on seek).
    std::optional<BagMessage> lookahead_msg_;

    // Optional SubplotManager for rendering integration.
    SubplotManager* subplot_mgr_{nullptr};

    // Callbacks.
    StateCallback      on_state_change_;
    PlayheadCallback   on_playhead_;
    MessageCallback    on_message_;
    RawMessageCallback on_raw_message_;

    // Topic filter applied to the reader.
    std::vector<std::string> topic_filter_;

    // Cached field extractor IDs per topic (built on first message of each topic).
    // topic → vector of (field_path, extractor_id_placeholder)
    // We call intr_ to get numeric fields and inject all of them.
    struct TopicFields
    {
        bool scanned{false};
        // field_path → accessor (cached per topic)
        struct FieldEntry
        {
            FieldAccessor accessor;
        };
        std::unordered_map<std::string, FieldEntry> field_entries;
    };
    std::unordered_map<std::string, TopicFields> topic_fields_;
};

}   // namespace spectra::adapters::ros2
