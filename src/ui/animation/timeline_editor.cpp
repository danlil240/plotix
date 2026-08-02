#include "ui/animation/timeline_editor.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>
#include <string_view>

#include "ui/animation/camera_animator.hpp"
#include "ui/animation/keyframe_interpolator.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"
#endif

namespace spectra
{
namespace
{

std::string escape_timeline_json(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value)
    {
        switch (c)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

std::string unescape_timeline_json(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    bool escaped = false;
    for (const char c : value)
    {
        if (escaped)
        {
            switch (c)
            {
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += c;
                    break;
            }
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else
        {
            result += c;
        }
    }
    if (escaped)
        result += '\\';
    return result;
}

std::vector<std::string> timeline_object_array(const std::string& json, const std::string& key)
{
    std::vector<std::string> objects;
    const std::string        search = "\"" + key + "\"";
    size_t                   pos    = json.find(search);
    if (pos == std::string::npos)
        return objects;
    pos = json.find('[', pos + search.size());
    if (pos == std::string::npos)
        return objects;

    int    depth     = 0;
    bool   in_string = false;
    bool   escaped   = false;
    size_t start     = std::string::npos;
    for (size_t i = pos + 1; i < json.size(); ++i)
    {
        const char c = json[i];
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
            continue;
        }
        if (c == '"')
        {
            in_string = true;
            continue;
        }
        if (c == '{')
        {
            if (depth++ == 0)
                start = i;
        }
        else if (c == '}' && depth > 0)
        {
            if (--depth == 0 && start != std::string::npos)
            {
                objects.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
        else if (c == ']' && depth == 0)
        {
            break;
        }
    }
    return objects;
}

}   // namespace

TimelineEditor::TimelineEditor() : view_end_(duration_) {}

// ─── Playback ────────────────────────────────────────────────────────────────

void TimelineEditor::play()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Stopped)
    {
        playhead_      = 0.0f;
        ping_pong_dir_ = 1;
    }
    state_ = PlaybackState::Playing;
    fire_playback_change();
}

void TimelineEditor::pause()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing || state_ == PlaybackState::Recording)
    {
        state_ = PlaybackState::Paused;
        fire_playback_change();
    }
}

void TimelineEditor::stop()
{
    std::lock_guard lock(mutex_);
    state_         = PlaybackState::Stopped;
    playhead_      = 0.0f;
    ping_pong_dir_ = 1;
    fire_playback_change();
}

void TimelineEditor::toggle_play()
{
    std::lock_guard lock(mutex_);
    if (state_ == PlaybackState::Playing)
    {
        state_ = PlaybackState::Paused;
    }
    else
    {
        if (state_ == PlaybackState::Stopped)
        {
            playhead_      = 0.0f;
            ping_pong_dir_ = 1;
        }
        state_ = PlaybackState::Playing;
    }
    fire_playback_change();
}

PlaybackState TimelineEditor::playback_state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

bool TimelineEditor::is_playing() const
{
    std::lock_guard lock(mutex_);
    return state_ == PlaybackState::Playing;
}

bool TimelineEditor::is_recording() const
{
    std::lock_guard lock(mutex_);
    return state_ == PlaybackState::Recording;
}

// ─── Playhead ────────────────────────────────────────────────────────────────

float TimelineEditor::playhead() const
{
    std::lock_guard lock(mutex_);
    return playhead_;
}

void TimelineEditor::set_playhead(float time)
{
    std::lock_guard lock(mutex_);
    playhead_ = time;
    clamp_playhead();
}

bool TimelineEditor::advance(float dt)
{
    std::lock_guard lock(mutex_);
    if (state_ != PlaybackState::Playing && state_ != PlaybackState::Recording)
    {
        return false;
    }

    float loop_end   = effective_loop_out();
    float loop_start = has_loop_region_ ? loop_in_ : 0.0f;

    if (loop_mode_ == LoopMode::PingPong)
    {
        playhead_ += dt * static_cast<float>(ping_pong_dir_);

        if (playhead_ >= loop_end)
        {
            playhead_      = loop_end - (playhead_ - loop_end);
            ping_pong_dir_ = -1;
        }
        else if (playhead_ <= loop_start)
        {
            playhead_      = loop_start + (loop_start - playhead_);
            ping_pong_dir_ = 1;
        }
        clamp_playhead();
        if (interpolator_)
            interpolator_->evaluate(playhead_);
        return true;
    }

    playhead_ += dt;

    if (playhead_ >= loop_end)
    {
        if (loop_mode_ == LoopMode::Loop)
        {
            float overshoot = playhead_ - loop_end;
            playhead_       = loop_start + std::fmod(overshoot, loop_end - loop_start);
            clamp_playhead();
            if (interpolator_)
                interpolator_->evaluate(playhead_);
            return true;
        }
        // LoopMode::None — stop at end
        playhead_ = loop_end;
        if (interpolator_)
            interpolator_->evaluate(playhead_);
        state_ = PlaybackState::Stopped;
        fire_playback_change();
        return false;
    }

    if (interpolator_)
        interpolator_->evaluate(playhead_);
    return true;
}

void TimelineEditor::scrub_to(float time)
{
    std::lock_guard lock(mutex_);
    playhead_ = time;
    clamp_playhead();
    if (on_scrub_)
    {
        on_scrub_(playhead_);
    }
}

void TimelineEditor::step_forward()
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return;
    float frame_dur = 1.0f / fps_;
    playhead_ += frame_dur;
    clamp_playhead();
}

void TimelineEditor::step_backward()
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return;
    float frame_dur = 1.0f / fps_;
    playhead_ -= frame_dur;
    clamp_playhead();
}

// ─── Duration & FPS ──────────────────────────────────────────────────────────

float TimelineEditor::duration() const
{
    std::lock_guard lock(mutex_);
    return duration_;
}

void TimelineEditor::set_duration(float seconds)
{
    std::lock_guard lock(mutex_);
    duration_ = std::max(0.0f, seconds);
    if (!has_loop_region_)
    {
        view_end_ = duration_;
    }
    clamp_playhead();
}

float TimelineEditor::fps() const
{
    std::lock_guard lock(mutex_);
    return fps_;
}

void TimelineEditor::set_fps(float target_fps)
{
    std::lock_guard lock(mutex_);
    fps_ = std::max(1.0f, target_fps);
}

uint32_t TimelineEditor::frame_count() const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::ceil(duration_ * fps_));
}

uint32_t TimelineEditor::current_frame() const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::floor(playhead_ * fps_));
}

float TimelineEditor::frame_to_time(uint32_t frame) const
{
    std::lock_guard lock(mutex_);
    if (fps_ <= 0.0f)
        return 0.0f;
    return static_cast<float>(frame) / fps_;
}

uint32_t TimelineEditor::time_to_frame(float time) const
{
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(std::floor(time * fps_));
}

// ─── Loop ────────────────────────────────────────────────────────────────────

LoopMode TimelineEditor::loop_mode() const
{
    std::lock_guard lock(mutex_);
    return loop_mode_;
}

void TimelineEditor::set_loop_mode(LoopMode mode)
{
    std::lock_guard lock(mutex_);
    loop_mode_ = mode;
    if (mode != LoopMode::PingPong)
    {
        ping_pong_dir_ = 1;
    }
}

float TimelineEditor::loop_in() const
{
    std::lock_guard lock(mutex_);
    return has_loop_region_ ? loop_in_ : 0.0f;
}

float TimelineEditor::loop_out() const
{
    std::lock_guard lock(mutex_);
    return effective_loop_out();
}

void TimelineEditor::set_loop_region(float in, float out)
{
    std::lock_guard lock(mutex_);
    loop_in_  = std::max(0.0f, in);
    loop_out_ = std::min(out, duration_);
    if (loop_out_ <= loop_in_)
    {
        loop_out_ = loop_in_ + 0.001f;
    }
    has_loop_region_ = true;
}

void TimelineEditor::clear_loop_region()
{
    std::lock_guard lock(mutex_);
    has_loop_region_ = false;
    loop_in_         = 0.0f;
    loop_out_        = 0.0f;
}

// ─── Snap ────────────────────────────────────────────────────────────────────

SnapMode TimelineEditor::snap_mode() const
{
    std::lock_guard lock(mutex_);
    return snap_mode_;
}

void TimelineEditor::set_snap_mode(SnapMode mode)
{
    std::lock_guard lock(mutex_);
    snap_mode_ = mode;
}

float TimelineEditor::snap_interval() const
{
    std::lock_guard lock(mutex_);
    return snap_interval_;
}

void TimelineEditor::set_snap_interval(float interval)
{
    std::lock_guard lock(mutex_);
    snap_interval_ = std::max(0.001f, interval);
}

float TimelineEditor::snap_time(float time) const
{
    std::lock_guard lock(mutex_);
    switch (snap_mode_)
    {
        case SnapMode::Frame:
        {
            if (fps_ <= 0.0f)
                return time;
            float frame_dur = 1.0f / fps_;
            return std::round(time / frame_dur) * frame_dur;
        }
        case SnapMode::Beat:
        {
            if (snap_interval_ <= 0.0f)
                return time;
            return std::round(time / snap_interval_) * snap_interval_;
        }
        case SnapMode::None:
        default:
            return time;
    }
}

// ─── Tracks ──────────────────────────────────────────────────────────────────

uint32_t TimelineEditor::add_track(const std::string& name, Color color)
{
    std::lock_guard lock(mutex_);
    uint32_t        id = next_track_id_++;
    TimelineTrack   track;
    track.id    = id;
    track.name  = name;
    track.color = color;
    tracks_.push_back(std::move(track));
    return id;
}

void TimelineEditor::remove_track(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(tracks_, [track_id](const TimelineTrack& t) { return t.id == track_id; });
    if (interpolator_)
        interpolator_->remove_channel(track_id);
}

void TimelineEditor::rename_track(uint32_t track_id, const std::string& name)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.name = name;
            if (interpolator_)
                if (auto* channel = interpolator_->channel(track_id))
                    channel->set_name(name);
            return;
        }
    }
}

TimelineTrack* TimelineEditor::get_track(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
            return &t;
    }
    return nullptr;
}

const TimelineTrack* TimelineEditor::get_track(uint32_t track_id) const
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
            return &t;
    }
    return nullptr;
}

const std::vector<TimelineTrack>& TimelineEditor::tracks() const
{
    std::lock_guard lock(mutex_);
    return tracks_;
}

size_t TimelineEditor::track_count() const
{
    std::lock_guard lock(mutex_);
    return tracks_.size();
}

void TimelineEditor::set_track_visible(uint32_t track_id, bool visible)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.visible = visible;
            return;
        }
    }
}

void TimelineEditor::set_track_locked(uint32_t track_id, bool locked)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.locked = locked;
            return;
        }
    }
}

bool TimelineEditor::set_track_property_path(uint32_t track_id, const std::string& property_path)
{
    std::lock_guard lock(mutex_);
    for (auto& track : tracks_)
    {
        if (track.id != track_id)
            continue;
        track.property_path = property_path;
        return true;
    }
    return false;
}

// ─── Keyframes ───────────────────────────────────────────────────────────────

void TimelineEditor::add_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            // Check for duplicate (within tolerance)
            for (const auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < 0.001f)
                    return;
            }

            KeyframeMarker kf;
            kf.time     = time;
            kf.track_id = track_id;
            t.keyframes.push_back(kf);

            // Keep sorted
            std::sort(t.keyframes.begin(),
                      t.keyframes.end(),
                      [](const KeyframeMarker& a, const KeyframeMarker& b)
                      { return a.time < b.time; });

            if (on_keyframe_added_)
            {
                on_keyframe_added_(track_id, time);
            }
            return;
        }
    }
}

void TimelineEditor::remove_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            auto it = std::find_if(t.keyframes.begin(),
                                   t.keyframes.end(),
                                   [time](const KeyframeMarker& kf)
                                   { return std::abs(kf.time - time) < 0.001f; });
            if (it != t.keyframes.end())
            {
                t.keyframes.erase(it);
                if (interpolator_)
                    interpolator_->remove_keyframe(track_id, time);
                if (on_keyframe_removed_)
                {
                    on_keyframe_removed_(track_id, time);
                }
            }
            return;
        }
    }
}

void TimelineEditor::move_keyframe(uint32_t track_id, float old_time, float new_time)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            auto it = std::find_if(t.keyframes.begin(),
                                   t.keyframes.end(),
                                   [old_time](const KeyframeMarker& kf)
                                   { return std::abs(kf.time - old_time) < 0.001f; });
            if (it != t.keyframes.end())
            {
                it->time = std::clamp(new_time, 0.0f, duration_);
                if (interpolator_)
                    if (auto* channel = interpolator_->channel(track_id))
                        channel->move_keyframe(old_time, it->time);
                std::sort(t.keyframes.begin(),
                          t.keyframes.end(),
                          [](const KeyframeMarker& a, const KeyframeMarker& b)
                          { return a.time < b.time; });
            }
            return;
        }
    }
}

void TimelineEditor::clear_keyframes(uint32_t track_id)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            t.keyframes.clear();
            return;
        }
    }
}

size_t TimelineEditor::total_keyframe_count() const
{
    std::lock_guard lock(mutex_);
    size_t          count = 0;
    for (const auto& t : tracks_)
    {
        count += t.keyframes.size();
    }
    return count;
}

// ─── Selection ───────────────────────────────────────────────────────────────

void TimelineEditor::select_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    auto*           kf = find_keyframe(track_id, time);
    if (kf)
    {
        kf->selected = true;
        fire_selection_change();
    }
}

void TimelineEditor::deselect_keyframe(uint32_t track_id, float time)
{
    std::lock_guard lock(mutex_);
    auto*           kf = find_keyframe(track_id, time);
    if (kf)
    {
        kf->selected = false;
        fire_selection_change();
    }
}

void TimelineEditor::select_all_keyframes()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            kf.selected = true;
        }
    }
    fire_selection_change();
}

void TimelineEditor::deselect_all()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            kf.selected = false;
        }
    }
    fire_selection_change();
}

void TimelineEditor::select_keyframes_in_range(float t_min, float t_max)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            if (kf.time >= t_min && kf.time <= t_max)
            {
                kf.selected = true;
            }
        }
    }
    fire_selection_change();
}

std::vector<KeyframeMarker*> TimelineEditor::selected_keyframes()
{
    std::lock_guard              lock(mutex_);
    std::vector<KeyframeMarker*> result;
    for (auto& t : tracks_)
    {
        for (auto& kf : t.keyframes)
        {
            if (kf.selected)
            {
                result.push_back(&kf);
            }
        }
    }
    return result;
}

size_t TimelineEditor::selected_count() const
{
    std::lock_guard lock(mutex_);
    size_t          count = 0;
    for (const auto& t : tracks_)
    {
        for (const auto& kf : t.keyframes)
        {
            if (kf.selected)
                ++count;
        }
    }
    return count;
}

void TimelineEditor::delete_selected()
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        if (t.locked)
            continue;
        std::erase_if(t.keyframes, [](const KeyframeMarker& kf) { return kf.selected; });
    }
    fire_selection_change();
}

// ─── Zoom & Scroll ───────────────────────────────────────────────────────────

float TimelineEditor::view_start() const
{
    std::lock_guard lock(mutex_);
    return view_start_;
}

float TimelineEditor::view_end() const
{
    std::lock_guard lock(mutex_);
    return view_end_;
}

void TimelineEditor::set_view_range(float start, float end)
{
    std::lock_guard lock(mutex_);
    view_start_ = std::max(0.0f, start);
    view_end_   = std::max(view_start_ + 0.01f, end);
}

float TimelineEditor::zoom() const
{
    std::lock_guard lock(mutex_);
    return zoom_;
}

void TimelineEditor::set_zoom(float pixels_per_second)
{
    std::lock_guard lock(mutex_);
    zoom_ = std::clamp(pixels_per_second, 10.0f, 10000.0f);
}

void TimelineEditor::zoom_in()
{
    std::lock_guard lock(mutex_);
    zoom_ = std::min(zoom_ * 1.25f, 10000.0f);
    // Narrow view range around playhead
    float center     = playhead_;
    float half_range = (view_end_ - view_start_) * 0.5f / 1.25f;
    view_start_      = std::max(0.0f, center - half_range);
    view_end_        = center + half_range;
}

void TimelineEditor::zoom_out()
{
    std::lock_guard lock(mutex_);
    zoom_            = std::max(zoom_ / 1.25f, 10.0f);
    float center     = (view_start_ + view_end_) * 0.5f;
    float half_range = (view_end_ - view_start_) * 0.5f * 1.25f;
    view_start_      = std::max(0.0f, center - half_range);
    view_end_        = center + half_range;
}

void TimelineEditor::scroll_to_playhead()
{
    std::lock_guard lock(mutex_);
    float           range = view_end_ - view_start_;
    view_start_           = std::max(0.0f, playhead_ - range * 0.5f);
    view_end_             = view_start_ + range;
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

void TimelineEditor::set_on_playback_change(PlaybackCallback cb)
{
    std::lock_guard lock(mutex_);
    on_playback_change_ = std::move(cb);
}

void TimelineEditor::set_on_scrub(ScrubCallback cb)
{
    std::lock_guard lock(mutex_);
    on_scrub_ = std::move(cb);
}

void TimelineEditor::set_on_keyframe_added(KeyframeCallback cb)
{
    std::lock_guard lock(mutex_);
    on_keyframe_added_ = std::move(cb);
}

void TimelineEditor::set_on_keyframe_removed(KeyframeCallback cb)
{
    std::lock_guard lock(mutex_);
    on_keyframe_removed_ = std::move(cb);
}

void TimelineEditor::set_on_selection_change(SelectionCallback cb)
{
    std::lock_guard lock(mutex_);
    on_selection_change_ = std::move(cb);
}

// ─── Internal helpers ────────────────────────────────────────────────────────

float TimelineEditor::effective_loop_out() const
{
    // Caller must hold mutex_
    if (has_loop_region_ && loop_out_ > loop_in_)
    {
        return loop_out_;
    }
    return duration_;
}

void TimelineEditor::clamp_playhead()
{
    // Caller must hold mutex_
    playhead_ = std::clamp(playhead_, 0.0f, duration_);
}

void TimelineEditor::fire_playback_change()
{
    // Caller must hold mutex_
    if (on_playback_change_)
    {
        on_playback_change_(state_);
    }
}

void TimelineEditor::fire_selection_change()
{
    // Caller must hold mutex_
    if (on_selection_change_)
    {
        std::vector<KeyframeMarker*> sel;
        for (auto& t : tracks_)
        {
            for (auto& kf : t.keyframes)
            {
                if (kf.selected)
                    sel.push_back(&kf);
            }
        }
        on_selection_change_(sel);
    }
}

KeyframeMarker* TimelineEditor::find_keyframe(uint32_t track_id, float time, float tolerance)
{
    // Caller must hold mutex_
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            for (auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < tolerance)
                {
                    return &kf;
                }
            }
            return nullptr;
        }
    }
    return nullptr;
}

// ─── KeyframeInterpolator integration (Week 11) ─────────────────────────────

void TimelineEditor::set_interpolator(KeyframeInterpolator* interp)
{
    std::lock_guard lock(mutex_);
    interpolator_ = interp;
}

KeyframeInterpolator* TimelineEditor::interpolator() const
{
    std::lock_guard lock(mutex_);
    return interpolator_;
}

void TimelineEditor::set_camera_animator(CameraAnimator* anim)
{
    std::lock_guard lock(mutex_);
    camera_animator_ = anim;
}

CameraAnimator* TimelineEditor::camera_animator() const
{
    std::lock_guard lock(mutex_);
    return camera_animator_;
}

void TimelineEditor::evaluate_at_playhead()
{
    std::lock_guard lock(mutex_);
    if (interpolator_)
    {
        interpolator_->evaluate(playhead_);
    }
    if (camera_animator_)
    {
        camera_animator_->evaluate_at(playhead_);
    }
}

uint32_t TimelineEditor::add_animated_track(const std::string& name,
                                            float              default_value,
                                            Color              color)
{
    std::lock_guard lock(mutex_);

    // Add the visual track
    uint32_t      id = next_track_id_++;
    TimelineTrack track;
    track.id    = id;
    track.name  = name;
    track.color = color;
    tracks_.push_back(std::move(track));

    // Add a matching interpolator channel with the same ID
    if (interpolator_)
    {
        interpolator_->add_channel_with_id(id, name, default_value);
    }

    return id;
}

void TimelineEditor::add_animated_keyframe(uint32_t track_id,
                                           float    time,
                                           float    value,
                                           int      interp_mode)
{
    std::lock_guard lock(mutex_);

    // Add visual keyframe marker to the track
    for (auto& t : tracks_)
    {
        if (t.id == track_id)
        {
            if (t.locked)
                return;

            // Check for duplicate
            for (const auto& kf : t.keyframes)
            {
                if (std::abs(kf.time - time) < 0.001f)
                {
                    // Update existing — just update the interpolator channel
                    if (interpolator_)
                    {
                        TypedKeyframe tkf(time, value, static_cast<InterpMode>(interp_mode));
                        interpolator_->add_keyframe(track_id, tkf);
                    }
                    return;
                }
            }

            KeyframeMarker kf;
            kf.time     = time;
            kf.track_id = track_id;
            t.keyframes.push_back(kf);

            std::sort(t.keyframes.begin(),
                      t.keyframes.end(),
                      [](const KeyframeMarker& a, const KeyframeMarker& b)
                      { return a.time < b.time; });

            if (on_keyframe_added_)
            {
                on_keyframe_added_(track_id, time);
            }
            break;
        }
    }

    // Add typed keyframe to the interpolator channel
    if (interpolator_)
    {
        if (!interpolator_->channel(track_id))
        {
            const auto track =
                std::find_if(tracks_.begin(),
                             tracks_.end(),
                             [track_id](const TimelineTrack& item) { return item.id == track_id; });
            interpolator_->add_channel_with_id(track_id,
                                               track == tracks_.end() ? "Track" : track->name,
                                               value);
        }
        TypedKeyframe tkf(time, value, static_cast<InterpMode>(interp_mode));
        interpolator_->add_keyframe(track_id, tkf);
    }
}

bool TimelineEditor::set_animated_keyframe(uint32_t track_id,
                                           float    time,
                                           float    value,
                                           int      interp_mode)
{
    std::lock_guard lock(mutex_);
    if (!interpolator_ || interp_mode < static_cast<int>(InterpMode::Step)
        || interp_mode > static_cast<int>(InterpMode::EaseInOut))
        return false;
    auto* channel = interpolator_->channel(track_id);
    if (!channel || !channel->find_keyframe(time))
        return false;
    const bool value_changed = channel->set_keyframe_value(time, value);
    const bool mode_changed =
        channel->set_keyframe_interp(time, static_cast<InterpMode>(interp_mode));
    return value_changed || mode_changed;
}

bool TimelineEditor::set_animated_keyframe_tangents(uint32_t track_id,
                                                    float    time,
                                                    int      tangent_mode,
                                                    float    in_dt,
                                                    float    in_dv,
                                                    float    out_dt,
                                                    float    out_dv)
{
    std::lock_guard lock(mutex_);
    if (!interpolator_ || tangent_mode < static_cast<int>(TangentMode::Free)
        || tangent_mode > static_cast<int>(TangentMode::Auto))
        return false;
    auto* channel = interpolator_->channel(track_id);
    if (!channel || !channel->find_keyframe(time))
        return false;
    const auto mode            = static_cast<TangentMode>(tangent_mode);
    bool       handles_changed = false;
    if (mode == TangentMode::Free || mode == TangentMode::Aligned)
    {
        handles_changed = channel->set_keyframe_tangents(time,
                                                         TangentHandle{in_dt, in_dv},
                                                         TangentHandle{out_dt, out_dv});
    }
    const bool mode_changed = channel->set_keyframe_tangent_mode(time, mode);
    return mode_changed || handles_changed;
}

void TimelineEditor::synchronize_markers_from_interpolator()
{
    std::lock_guard lock(mutex_);
    if (!interpolator_)
        return;
    for (auto& track : tracks_)
    {
        const AnimationChannel* channel = interpolator_->channel(track.id);
        if (!channel)
            continue;
        track.keyframes.clear();
        track.keyframes.reserve(channel->keyframes().size());
        for (const auto& keyframe : channel->keyframes())
            track.keyframes.push_back({keyframe.time, track.id, keyframe.selected});
    }
}

std::string TimelineEditor::serialize() const
{
    std::lock_guard    lock(mutex_);
    std::ostringstream ss;
    ss << "{\"duration\":" << duration_ << ",\"fps\":" << fps_ << ",\"playhead\":" << playhead_
       << ",\"state\":" << static_cast<int>(state_)
       << ",\"loop_mode\":" << static_cast<int>(loop_mode_)
       << ",\"snap_mode\":" << static_cast<int>(snap_mode_)
       << ",\"snap_interval\":" << snap_interval_ << ",\"view_start\":" << view_start_
       << ",\"view_end\":" << view_end_ << ",\"zoom\":" << zoom_;

    if (has_loop_region_)
    {
        ss << ",\"loop_in\":" << loop_in_ << ",\"loop_out\":" << loop_out_;
    }

    // Serialize tracks
    ss << ",\"tracks\":[";
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        if (i > 0)
            ss << ",";
        const auto& t = tracks_[i];
        ss << "{\"id\":" << t.id << R"(,"name":")" << escape_timeline_json(t.name) << "\""
           << R"(,"property_path":")" << escape_timeline_json(t.property_path) << "\""
           << ",\"color\":[" << t.color.r << "," << t.color.g << "," << t.color.b << ","
           << t.color.a << "]" << ",\"visible\":" << (t.visible ? "true" : "false")
           << ",\"locked\":" << (t.locked ? "true" : "false")
           << ",\"expanded\":" << (t.expanded ? "true" : "false") << ",\"keyframes\":[";
        for (size_t k = 0; k < t.keyframes.size(); ++k)
        {
            if (k > 0)
                ss << ",";
            ss << "{\"t\":" << t.keyframes[k].time << "}";
        }
        ss << "]}";
    }
    ss << "]";

    // Include interpolator data if present
    if (interpolator_)
    {
        ss << ",\"interpolator\":" << interpolator_->serialize();
    }

    ss << "}";
    return ss.str();
}

bool TimelineEditor::deserialize(const std::string& json)
{
    std::lock_guard lock(mutex_);

    if (json.find('{') == std::string::npos || json.find('}') == std::string::npos)
        return false;

    // Minimal parsing — extract key fields
    auto extract_float = [&](const std::string& key, float def) -> float
    {
        std::string search = "\"" + key + "\":";
        auto        pos    = json.find(search);
        if (pos == std::string::npos)
            return def;
        pos += search.size();
        try
        {
            return std::stof(json.substr(pos));
        }
        catch (...)
        {
            return def;
        }
    };

    auto extract_int = [&](const std::string& key, int def) -> int
    {
        std::string search = "\"" + key + "\":";
        auto        pos    = json.find(search);
        if (pos == std::string::npos)
            return def;
        pos += search.size();
        try
        {
            return std::stoi(json.substr(pos));
        }
        catch (...)
        {
            return def;
        }
    };

    auto extract_bool = [](const std::string& object, const std::string& key, bool def)
    {
        const std::string search = "\"" + key + "\":";
        const size_t      pos    = object.find(search);
        if (pos == std::string::npos)
            return def;
        const size_t value_pos = pos + search.size();
        if (object.compare(value_pos, 4, "true") == 0)
            return true;
        if (object.compare(value_pos, 5, "false") == 0)
            return false;
        return def;
    };

    auto extract_string = [](const std::string& object, const std::string& key)
    {
        const std::string search = "\"" + key + "\":";
        size_t            pos    = object.find(search);
        if (pos == std::string::npos)
            return std::string{};
        pos = object.find('"', pos + search.size());
        if (pos == std::string::npos)
            return std::string{};
        const size_t start   = ++pos;
        bool         escaped = false;
        for (; pos < object.size(); ++pos)
        {
            if (escaped)
                escaped = false;
            else if (object[pos] == '\\')
                escaped = true;
            else if (object[pos] == '"')
                return unescape_timeline_json(std::string_view(object).substr(start, pos - start));
        }
        return std::string{};
    };

    auto object_number = [](const std::string& object, const std::string& key, float def)
    {
        const std::string search = "\"" + key + "\":";
        const size_t      pos    = object.find(search);
        if (pos == std::string::npos)
            return def;
        try
        {
            return std::stof(object.substr(pos + search.size()));
        }
        catch (...)
        {
            return def;
        }
    };

    duration_      = std::max(0.1f, extract_float("duration", 10.0f));
    fps_           = std::max(1.0f, extract_float("fps", 60.0f));
    playhead_      = extract_float("playhead", 0.0f);
    state_         = static_cast<PlaybackState>(std::clamp(extract_int("state", 0), 0, 3));
    loop_mode_     = static_cast<LoopMode>(std::clamp(extract_int("loop_mode", 0), 0, 2));
    snap_mode_     = static_cast<SnapMode>(std::clamp(extract_int("snap_mode", 1), 0, 2));
    snap_interval_ = extract_float("snap_interval", 0.1f);

    has_loop_region_ = false;
    float li         = extract_float("loop_in", -1.0f);
    float lo         = extract_float("loop_out", -1.0f);
    if (li >= 0.0f && lo > li)
    {
        loop_in_         = li;
        loop_out_        = lo;
        has_loop_region_ = true;
    }

    view_start_ = extract_float("view_start", 0.0f);
    view_end_   = extract_float("view_end", duration_);
    zoom_       = extract_float("zoom", 100.0f);
    clamp_playhead();

    tracks_.clear();
    next_track_id_ = 1;
    for (const std::string& track_json : timeline_object_array(json, "tracks"))
    {
        TimelineTrack track;
        track.id   = static_cast<uint32_t>(object_number(track_json, "id", next_track_id_));
        track.name = extract_string(track_json, "name");
        track.property_path = extract_string(track_json, "property_path");
        track.visible       = extract_bool(track_json, "visible", true);
        track.locked        = extract_bool(track_json, "locked", false);
        track.expanded      = extract_bool(track_json, "expanded", true);

        const size_t color_key = track_json.find("\"color\":[");
        if (color_key != std::string::npos)
        {
            const size_t begin = track_json.find('[', color_key);
            const size_t end   = track_json.find(']', begin);
            if (begin != std::string::npos && end != std::string::npos)
            {
                std::string color_values = track_json.substr(begin + 1, end - begin - 1);
                std::replace(color_values.begin(), color_values.end(), ',', ' ');
                std::istringstream color_stream(color_values);
                color_stream >> track.color.r >> track.color.g >> track.color.b >> track.color.a;
            }
        }

        for (const std::string& keyframe_json : timeline_object_array(track_json, "keyframes"))
        {
            const float time = std::clamp(object_number(keyframe_json, "t", 0.0f), 0.0f, duration_);
            track.keyframes.push_back({time, track.id, false});
        }
        std::sort(track.keyframes.begin(),
                  track.keyframes.end(),
                  [](const KeyframeMarker& a, const KeyframeMarker& b) { return a.time < b.time; });
        next_track_id_ = std::max(next_track_id_, track.id + 1);
        tracks_.push_back(std::move(track));
    }

    // Deserialize interpolator data if present
    if (interpolator_)
    {
        auto interp_pos = json.find("\"interpolator\":");
        if (interp_pos != std::string::npos)
        {
            interp_pos += 16;   // skip "interpolator":
            // Find matching closing brace
            int    depth = 0;
            size_t start = interp_pos;
            for (size_t i = interp_pos; i < json.size(); ++i)
            {
                if (json[i] == '{')
                    depth++;
                else if (json[i] == '}')
                {
                    depth--;
                    if (depth == 0)
                    {
                        interpolator_->deserialize(json.substr(start, i - start + 1));
                        break;
                    }
                }
            }
        }
    }

    return true;
}

// ─── ImGui Drawing ───────────────────────────────────────────────────────────

#ifdef SPECTRA_USE_IMGUI

void TimelineEditor::draw(float width, float height)
{
    std::lock_guard lock(mutex_);

    namespace tk       = spectra::ui::tokens;
    const auto& colors = spectra::ui::theme();

    // Color → ImU32 helper (generic over spectra::Color and spectra::ui::Color).
    auto col = [](const auto& c, float a) -> ImU32
    {
        return IM_COL32(static_cast<int>(c.r * 255.0f),
                        static_cast<int>(c.g * 255.0f),
                        static_cast<int>(c.b * 255.0f),
                        static_cast<int>(a * 255.0f));
    };

    if (width < 1.0f || height < 1.0f)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##timeline_editor",
                      ImVec2(width, height),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float track_height      = tk::TIMELINE_TRACK_HEIGHT;
    const float ruler_height      = tk::TIMELINE_RULER_HEIGHT;
    const float track_label_width = tk::TIMELINE_LABEL_WIDTH;
    const float label_pad_x       = tk::SPACE_3;   // shared left inset for TRACKS + names

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2      origin    = ImGui::GetCursorScreenPos();
    float       right_x   = origin.x + width;

    float timeline_width = std::max(1.0f, width - track_label_width);
    float time_range     = view_end_ - view_start_;
    if (time_range <= 0.0f)
        time_range = 1.0f;
    float px_per_sec = timeline_width / time_range;

    // Offset from child origin to lane x for time t (excludes origin.x).
    auto time_to_lane_x = [&](float t) -> float
    { return track_label_width + (t - view_start_) * px_per_sec; };
    auto time_to_screen_x = [&](float t) -> float { return origin.x + time_to_lane_x(t); };

    const float tracks_content_h = static_cast<float>(tracks_.size()) * track_height;
    const float tracks_view_h    = std::max(0.0f, height - ruler_height);

    // ─── Time ruler (fixed strip) ─────────────────────────────────────
    const float ruler_y = origin.y;
    draw_list->AddRectFilled(ImVec2(origin.x, ruler_y),
                             ImVec2(origin.x + track_label_width, ruler_y + ruler_height),
                             col(colors.bg_secondary, 0.9f));
    draw_list->AddRectFilled(ImVec2(origin.x + track_label_width, ruler_y),
                             ImVec2(right_x, ruler_y + ruler_height),
                             col(colors.bg_tertiary, 0.85f));
    draw_list->AddText(ImVec2(origin.x + label_pad_x,
                              ruler_y + (ruler_height - ImGui::GetTextLineHeight()) * 0.5f),
                       col(colors.text_tertiary, 0.75f),
                       "TRACKS");
    draw_list->AddLine(ImVec2(origin.x, ruler_y + ruler_height),
                       ImVec2(right_x, ruler_y + ruler_height),
                       col(colors.border_subtle, 0.6f),
                       1.0f);
    // Gutter / lanes divider continues through the track viewport below.
    draw_list->AddLine(ImVec2(origin.x + track_label_width, ruler_y),
                       ImVec2(origin.x + track_label_width, ruler_y + height),
                       col(colors.accent, 0.22f),
                       1.0f);

    // Tick marks
    float tick_spacing = 1.0f;
    if (px_per_sec < 30.0f)
        tick_spacing = 5.0f;
    else if (px_per_sec < 60.0f)
        tick_spacing = 2.0f;
    else if (px_per_sec > 300.0f)
        tick_spacing = 0.5f;
    else if (px_per_sec > 600.0f)
        tick_spacing = 0.1f;

    float t = std::floor(view_start_ / tick_spacing) * tick_spacing;
    while (t <= view_end_)
    {
        float px     = time_to_screen_x(t);
        bool  major  = std::abs(std::fmod(t, tick_spacing * 5.0f)) < 0.001f;
        float tick_h = major ? ruler_height * 0.55f : ruler_height * 0.30f;
        draw_list->AddLine(ImVec2(px, ruler_y + ruler_height - tick_h),
                           ImVec2(px, ruler_y + ruler_height),
                           col(colors.text_tertiary, major ? 0.7f : 0.35f),
                           1.0f);
        if (major)
        {
            const std::string buf = std::format("{:.1f}s", t);
            draw_list->AddText(ImVec2(px + 3.0f, ruler_y + 3.0f),
                               col(colors.text_secondary, 0.85f),
                               buf.c_str());
        }
        t += tick_spacing;
    }

    // Ruler click-to-scrub
    ImGui::SetCursorScreenPos(ImVec2(origin.x + track_label_width, ruler_y));
    ImGui::InvisibleButton("##ruler_scrub", ImVec2(timeline_width, ruler_height));
    if (ImGui::IsItemActive())
    {
        float mx      = ImGui::GetIO().MousePos.x - origin.x;
        float t_click = view_start_ + (mx - track_label_width) / px_per_sec;
        playhead_     = std::clamp(t_click, 0.0f, duration_);
        if (on_scrub_)
            on_scrub_(playhead_);
    }

    // ─── Scrollable track lanes ───────────────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(origin.x, ruler_y + ruler_height));
    ImGui::BeginChild("##timeline_tracks",
                      ImVec2(width, tracks_view_h),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);

    ImDrawList* tracks_dl     = ImGui::GetWindowDrawList();
    ImVec2      tracks_origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tracks_canvas",
                           ImVec2(width, std::max(tracks_view_h, tracks_content_h)));
    const float canvas_bottom = tracks_origin.y + std::max(tracks_view_h, tracks_content_h);

    // Recessed gutter background for the full scrollable content height.
    tracks_dl->AddRectFilled(ImVec2(tracks_origin.x, tracks_origin.y),
                             ImVec2(tracks_origin.x + track_label_width, canvas_bottom),
                             col(colors.bg_primary, 0.45f));

    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        auto& track = tracks_[i];
        float y     = tracks_origin.y + static_cast<float>(i) * track_height;

        ImU32 lane_bg =
            (i % 2 == 0) ? col(colors.bg_secondary, 0.0f) : col(colors.bg_tertiary, 0.25f);
        tracks_dl->AddRectFilled(ImVec2(tracks_origin.x + track_label_width, y),
                                 ImVec2(tracks_origin.x + width, y + track_height),
                                 lane_bg);
        tracks_dl->AddLine(ImVec2(tracks_origin.x, y + track_height),
                           ImVec2(tracks_origin.x + width, y + track_height),
                           col(colors.border_subtle, 0.30f),
                           1.0f);

        // Color accent strip + dot aligned with the TRACKS label column.
        tracks_dl->AddRectFilled(ImVec2(tracks_origin.x, y + 4.0f),
                                 ImVec2(tracks_origin.x + 3.0f, y + track_height - 4.0f),
                                 col(track.color, track.visible ? 0.95f : 0.35f),
                                 1.5f);
        float dot_cx = tracks_origin.x + label_pad_x + 6.0f;
        float dot_cy = y + track_height * 0.5f;
        tracks_dl->AddCircleFilled(ImVec2(dot_cx, dot_cy),
                                   4.0f,
                                   col(track.color, track.visible ? 1.0f : 0.4f));
        ImU32 label_col =
            track.visible ? col(colors.text_primary, 0.95f) : col(colors.text_tertiary, 0.5f);
        tracks_dl->AddText(ImVec2(tracks_origin.x + label_pad_x + 16.0f,
                                  dot_cy - ImGui::GetTextLineHeight() * 0.5f),
                           label_col,
                           track.name.c_str());

        if (track.locked)
        {
            tracks_dl->AddText(ImVec2(tracks_origin.x + track_label_width - 16.0f,
                                      dot_cy - ImGui::GetTextLineHeight() * 0.5f),
                               col(colors.warning, 0.9f),
                               "L");
        }

        ImU32 kf_color = col(track.color, track.color.a);
        for (auto& kf : track.keyframes)
        {
            float kf_px = tracks_origin.x + time_to_lane_x(kf.time);
            if (kf_px < tracks_origin.x + track_label_width)
                continue;
            float kf_cy = y + track_height * 0.5f;
            float sz    = kf.selected ? 6.0f : 4.5f;

            tracks_dl->AddQuadFilled(ImVec2(kf_px, kf_cy - sz),
                                     ImVec2(kf_px + sz, kf_cy),
                                     ImVec2(kf_px, kf_cy + sz),
                                     ImVec2(kf_px - sz, kf_cy),
                                     kf.selected ? col(colors.accent, 1.0f) : kf_color);

            if (kf.selected)
            {
                tracks_dl->AddQuad(ImVec2(kf_px, kf_cy - sz - 1),
                                   ImVec2(kf_px + sz + 1, kf_cy),
                                   ImVec2(kf_px, kf_cy + sz + 1),
                                   ImVec2(kf_px - sz - 1, kf_cy),
                                   col(colors.text_primary, 0.85f));
            }
        }
    }

    // Loop region overlay (tracks viewport only).
    if (has_loop_region_)
    {
        float li_px = tracks_origin.x + time_to_lane_x(loop_in_);
        float lo_px = tracks_origin.x + time_to_lane_x(loop_out_);
        tracks_dl->AddRectFilled(ImVec2(li_px, tracks_origin.y),
                                 ImVec2(lo_px, canvas_bottom),
                                 col(colors.accent, 0.10f));
        tracks_dl->AddLine(ImVec2(li_px, tracks_origin.y),
                           ImVec2(li_px, canvas_bottom),
                           col(colors.accent, 0.5f),
                           1.0f);
        tracks_dl->AddLine(ImVec2(lo_px, tracks_origin.y),
                           ImVec2(lo_px, canvas_bottom),
                           col(colors.accent, 0.5f),
                           1.0f);
    }

    ImGui::EndChild();

    // ─── Playhead (spans ruler + visible track viewport) ──────────────
    float ph_px = time_to_screen_x(playhead_);
    if (ph_px >= origin.x + track_label_width)
    {
        const float ph_bottom = ruler_y + height;
        draw_list->AddLine(ImVec2(ph_px, ruler_y),
                           ImVec2(ph_px, ph_bottom),
                           col(colors.accent, 0.20f),
                           4.0f);
        draw_list->AddLine(ImVec2(ph_px, ruler_y),
                           ImVec2(ph_px, ph_bottom),
                           col(colors.accent, 0.95f),
                           1.5f);
        draw_list->AddTriangleFilled(ImVec2(ph_px - 6.0f, ruler_y),
                                     ImVec2(ph_px + 6.0f, ruler_y),
                                     ImVec2(ph_px, ruler_y + 9.0f),
                                     col(colors.accent, 1.0f));
    }

    // Loop region tint on the ruler strip (matches lane overlay).
    if (has_loop_region_)
    {
        float li_px = time_to_screen_x(loop_in_);
        float lo_px = time_to_screen_x(loop_out_);
        draw_list->AddRectFilled(ImVec2(li_px, ruler_y),
                                 ImVec2(lo_px, ruler_y + ruler_height),
                                 col(colors.accent, 0.10f));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra
