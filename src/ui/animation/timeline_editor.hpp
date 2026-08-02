#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/animator.hpp>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

class KeyframeInterpolator;

// Playback state for the timeline editor.
enum class PlaybackState
{
    Stopped,
    Playing,
    Paused,
    Recording,
};

// Loop mode for playback.
enum class LoopMode
{
    None,       // Play once and stop
    Loop,       // Loop back to start
    PingPong,   // Reverse direction at each end
};

// Snap mode for playhead and keyframe placement.
enum class SnapMode
{
    None,    // Free positioning
    Frame,   // Snap to frame boundaries
    Beat,    // Snap to beat grid (custom interval)
};

// A single keyframe entry visible in the timeline UI.
struct KeyframeMarker
{
    float    time     = 0.0f;
    uint32_t track_id = 0;
    bool     selected = false;
};

// A named track in the timeline (e.g., "X Position", "Color", "Opacity").
struct TimelineTrack
{
    uint32_t    id = 0;
    std::string name;
    Color       color    = colors::cyan;
    bool        visible  = true;
    bool        locked   = false;
    bool        expanded = true;

    // Stable, frontend-independent identity for the model property driven by
    // this track. Runtime callbacks are rebuilt from this path after undo or
    // workspace restore; they are deliberately not serialized as pointers.
    std::string property_path;

    std::vector<KeyframeMarker> keyframes;
};

// Callback types for timeline editor events.
using PlaybackCallback  = std::function<void(PlaybackState)>;
using ScrubCallback     = std::function<void(float time)>;
using KeyframeCallback  = std::function<void(uint32_t track_id, float time)>;
using SelectionCallback = std::function<void(const std::vector<KeyframeMarker*>&)>;

// TimelineEditor — UI-independent timeline editing logic.
//
// Manages playhead position, playback state, keyframe tracks, selection,
// scrubbing, and snap. The ImGui drawing code (if enabled) is in the .cpp
// behind SPECTRA_USE_IMGUI guards; the pure logic is always available.
//
// Thread-safe: all public methods lock an internal mutex.
class TimelineEditor
{
   public:
    TimelineEditor();
    ~TimelineEditor() = default;

    TimelineEditor(const TimelineEditor&)            = delete;
    TimelineEditor& operator=(const TimelineEditor&) = delete;

    // ─── Playback ────────────────────────────────────────────────────────

    void play();
    void pause();
    void stop();
    void toggle_play();

    PlaybackState playback_state() const;
    bool          is_playing() const;
    bool          is_recording() const;

    // ─── Playhead ────────────────────────────────────────────────────────

    // Current playhead time in seconds.
    float playhead() const;

    // Set playhead (clamped to [0, duration]).
    void set_playhead(float time);

    // Advance playhead by dt seconds (called each frame during playback).
    // Returns true if playback is still active after the advance.
    bool advance(float dt);

    // Scrub to a specific time (triggers scrub callback).
    void scrub_to(float time);

    // Step forward/backward by one frame.
    void step_forward();
    void step_backward();

    // ─── Duration & FPS ──────────────────────────────────────────────────

    float duration() const;
    void  set_duration(float seconds);

    float fps() const;
    void  set_fps(float target_fps);

    // Frame count derived from duration * fps.
    uint32_t frame_count() const;

    // Current frame index (playhead * fps).
    uint32_t current_frame() const;

    // Time for a given frame index.
    float frame_to_time(uint32_t frame) const;

    // Frame index for a given time.
    uint32_t time_to_frame(float time) const;

    // ─── Loop ────────────────────────────────────────────────────────────

    LoopMode loop_mode() const;
    void     set_loop_mode(LoopMode mode);

    // In/out points for loop region (defaults to [0, duration]).
    float loop_in() const;
    float loop_out() const;
    void  set_loop_region(float in, float out);
    void  clear_loop_region();

    // ─── Snap ────────────────────────────────────────────────────────────

    SnapMode snap_mode() const;
    void     set_snap_mode(SnapMode mode);

    float snap_interval() const;
    void  set_snap_interval(float interval);

    // Snap a time value according to current snap settings.
    float snap_time(float time) const;

    // ─── Tracks ──────────────────────────────────────────────────────────

    uint32_t add_track(const std::string& name, Color color = colors::cyan);
    void     remove_track(uint32_t track_id);
    void     rename_track(uint32_t track_id, const std::string& name);

    TimelineTrack*       get_track(uint32_t track_id);
    const TimelineTrack* get_track(uint32_t track_id) const;

    const std::vector<TimelineTrack>& tracks() const;
    size_t                            track_count() const;

    void set_track_visible(uint32_t track_id, bool visible);
    void set_track_locked(uint32_t track_id, bool locked);
    bool set_track_property_path(uint32_t track_id, const std::string& property_path);

    // ─── Keyframes ───────────────────────────────────────────────────────

    // Add a keyframe to a track at the given time.
    void add_keyframe(uint32_t track_id, float time);

    // Remove a keyframe from a track at the given time (within snap tolerance).
    void remove_keyframe(uint32_t track_id, float time);

    // Move a keyframe from old_time to new_time on a track.
    void move_keyframe(uint32_t track_id, float old_time, float new_time);

    // Remove all keyframes from a track.
    void clear_keyframes(uint32_t track_id);

    // Total keyframe count across all tracks.
    size_t total_keyframe_count() const;

    // ─── Selection ───────────────────────────────────────────────────────

    void select_keyframe(uint32_t track_id, float time);
    void deselect_keyframe(uint32_t track_id, float time);
    void select_all_keyframes();
    void deselect_all();
    void select_keyframes_in_range(float t_min, float t_max);

    std::vector<KeyframeMarker*> selected_keyframes();
    size_t                       selected_count() const;

    // Delete all selected keyframes.
    void delete_selected();

    // ─── Zoom & Scroll ───────────────────────────────────────────────────

    // Visible time range in the timeline view.
    float view_start() const;
    float view_end() const;
    void  set_view_range(float start, float end);

    // Zoom level (pixels per second). Default = 100.
    float zoom() const;
    void  set_zoom(float pixels_per_second);
    void  zoom_in();
    void  zoom_out();

    // Scroll to center the playhead in view.
    void scroll_to_playhead();

    // ─── Callbacks ───────────────────────────────────────────────────────

    void set_on_playback_change(PlaybackCallback cb);
    void set_on_scrub(ScrubCallback cb);
    void set_on_keyframe_added(KeyframeCallback cb);
    void set_on_keyframe_removed(KeyframeCallback cb);
    void set_on_selection_change(SelectionCallback cb);

    // ─── KeyframeInterpolator integration (Week 11) ────────────────────

    // Set the KeyframeInterpolator to drive property animation.
    // When set, advance() will also evaluate the interpolator at the playhead.
    void                  set_interpolator(KeyframeInterpolator* interp);
    KeyframeInterpolator* interpolator() const;

    // Evaluate the interpolator at the current playhead time.
    // Called automatically during advance() when an interpolator is set.
    void evaluate_at_playhead();

    // Camera animator integration
    void            set_camera_animator(CameraAnimator* anim);
    CameraAnimator* camera_animator() const;

    // Create a track and a matching interpolator channel, linked by track_id.
    // Returns the track_id (which also serves as the channel_id).
    uint32_t add_animated_track(const std::string& name,
                                float              default_value = 0.0f,
                                Color              color         = colors::cyan);

    // Add a keyframe to both the track (visual marker) and the interpolator channel.
    // interp_mode: 0=Step, 1=Linear, 2=CubicBezier, 3=Spring, 4=EaseIn, 5=EaseOut, 6=EaseInOut
    void add_animated_keyframe(uint32_t track_id, float time, float value, int interp_mode = 1);

    // Edit the value and interpolation of an existing animated keyframe.
    bool set_animated_keyframe(uint32_t track_id, float time, float value, int interp_mode);
    bool set_animated_keyframe_tangents(uint32_t track_id,
                                        float    time,
                                        int      tangent_mode,
                                        float    in_dt,
                                        float    in_dv,
                                        float    out_dt,
                                        float    out_dv);

    // Rebuild visual markers from the authoritative typed channels after a
    // graphical curve edit. Track/channel IDs remain the stable join key.
    void synchronize_markers_from_interpolator();

    // Serialize timeline state + interpolator to a JSON string.
    std::string serialize() const;

    // Deserialize timeline state + interpolator from a JSON string.
    bool deserialize(const std::string& json);

    // ─── ImGui Drawing ───────────────────────────────────────────────────
#ifdef SPECTRA_USE_IMGUI
    // Draw the timeline editor panel. Call once per frame.
    void draw(float width, float height);
#endif

   private:
    mutable std::mutex mutex_;

    // Playback
    PlaybackState state_         = PlaybackState::Stopped;
    float         playhead_      = 0.0f;
    float         duration_      = 10.0f;
    float         fps_           = 60.0f;
    LoopMode      loop_mode_     = LoopMode::None;
    int           ping_pong_dir_ = 1;   // +1 forward, -1 backward

    // Loop region
    float loop_in_         = 0.0f;
    float loop_out_        = 0.0f;   // 0 = use duration
    bool  has_loop_region_ = false;

    // Snap
    SnapMode snap_mode_     = SnapMode::Frame;
    float    snap_interval_ = 0.1f;   // For Beat mode

    // Tracks
    std::vector<TimelineTrack> tracks_;
    uint32_t                   next_track_id_ = 1;

    // View
    float view_start_ = 0.0f;
    float view_end_   = 10.0f;
    float zoom_       = 100.0f;   // pixels per second

    // KeyframeInterpolator (optional, not owned)
    KeyframeInterpolator* interpolator_    = nullptr;
    CameraAnimator*       camera_animator_ = nullptr;

    // Callbacks
    PlaybackCallback  on_playback_change_;
    ScrubCallback     on_scrub_;
    KeyframeCallback  on_keyframe_added_;
    KeyframeCallback  on_keyframe_removed_;
    SelectionCallback on_selection_change_;

    // Internal helpers
    float           effective_loop_out() const;
    void            clamp_playhead();
    void            fire_playback_change();
    void            fire_selection_change();
    KeyframeMarker* find_keyframe(uint32_t track_id, float time, float tolerance = 0.001f);
};

}   // namespace spectra
