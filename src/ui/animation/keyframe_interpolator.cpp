#include "ui/animation/keyframe_interpolator.hpp"

#include <cmath>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace spectra
{

// ─── InterpMode / TangentMode names ─────────────────────────────────────────

const char* interp_mode_name(InterpMode mode)
{
    switch (mode)
    {
        case InterpMode::Step:
            return "Step";
        case InterpMode::Linear:
            return "Linear";
        case InterpMode::CubicBezier:
            return "CubicBezier";
        case InterpMode::Spring:
            return "Spring";
        case InterpMode::EaseIn:
            return "EaseIn";
        case InterpMode::EaseOut:
            return "EaseOut";
        case InterpMode::EaseInOut:
            return "EaseInOut";
    }
    return "Unknown";
}

const char* tangent_mode_name(TangentMode mode)
{
    switch (mode)
    {
        case TangentMode::Free:
            return "Free";
        case TangentMode::Aligned:
            return "Aligned";
        case TangentMode::Flat:
            return "Flat";
        case TangentMode::Auto:
            return "Auto";
    }
    return "Unknown";
}

// ─── AnimationChannel ────────────────────────────────────────────────────────

AnimationChannel::AnimationChannel(const std::string& name, float default_value)
    : name_(name), default_value_(default_value)
{
}

void AnimationChannel::set_value_range(float min_val, float max_val)
{
    min_value_ = min_val;
    max_value_ = max_val;
    has_range_ = true;
}

// ─── Keyframe management ─────────────────────────────────────────────────────

void AnimationChannel::add_keyframe(const TypedKeyframe& kf)
{
    // Check for existing keyframe at same time
    for (auto& existing : keyframes_)
    {
        if (std::abs(existing.time - kf.time) < 0.001f)
        {
            existing.value        = kf.value;
            existing.interp       = kf.interp;
            existing.tangent_mode = kf.tangent_mode;
            existing.in_tangent   = kf.in_tangent;
            existing.out_tangent  = kf.out_tangent;
            compute_auto_tangents();
            return;
        }
    }

    keyframes_.push_back(kf);
    sort_keyframes();
    compute_auto_tangents();
}

bool AnimationChannel::remove_keyframe(float time, float tolerance)
{
    auto it = std::find_if(keyframes_.begin(),
                           keyframes_.end(),
                           [time, tolerance](const TypedKeyframe& kf)
                           { return std::abs(kf.time - time) < tolerance; });
    if (it != keyframes_.end())
    {
        keyframes_.erase(it);
        compute_auto_tangents();
        return true;
    }
    return false;
}

bool AnimationChannel::move_keyframe(float old_time, float new_time, float tolerance)
{
    auto* kf = find_keyframe(old_time, tolerance);
    if (!kf)
        return false;
    kf->time = new_time;
    sort_keyframes();
    compute_auto_tangents();
    return true;
}

bool AnimationChannel::set_keyframe_value(float time, float value, float tolerance)
{
    auto* kf = find_keyframe(time, tolerance);
    if (!kf)
        return false;
    kf->value = value;
    compute_auto_tangents();
    return true;
}

bool AnimationChannel::set_keyframe_interp(float time, InterpMode mode, float tolerance)
{
    auto* kf = find_keyframe(time, tolerance);
    if (!kf)
        return false;
    kf->interp = mode;
    return true;
}

bool AnimationChannel::set_keyframe_tangents(float         time,
                                             TangentHandle in,
                                             TangentHandle out,
                                             float         tolerance)
{
    auto* kf = find_keyframe(time, tolerance);
    if (!kf)
        return false;
    kf->in_tangent   = in;
    kf->out_tangent  = out;
    kf->tangent_mode = TangentMode::Free;
    return true;
}

bool AnimationChannel::set_keyframe_tangent_mode(float time, TangentMode mode, float tolerance)
{
    auto* kf = find_keyframe(time, tolerance);
    if (!kf)
        return false;
    kf->tangent_mode = mode;
    if (mode == TangentMode::Flat)
    {
        kf->in_tangent  = TangentHandle{0.0f, 0.0f};
        kf->out_tangent = TangentHandle{0.0f, 0.0f};
    }
    else if (mode == TangentMode::Auto)
    {
        // Find index and recompute
        for (size_t i = 0; i < keyframes_.size(); ++i)
        {
            if (&keyframes_[i] == kf)
            {
                compute_auto_tangent_at(i);
                break;
            }
        }
    }
    return true;
}

void AnimationChannel::clear()
{
    keyframes_.clear();
}

// ─── Queries ─────────────────────────────────────────────────────────────────

TypedKeyframe* AnimationChannel::find_keyframe(float time, float tolerance)
{
    for (auto& kf : keyframes_)
    {
        if (std::abs(kf.time - time) < tolerance)
            return &kf;
    }
    return nullptr;
}

const TypedKeyframe* AnimationChannel::find_keyframe(float time, float tolerance) const
{
    for (const auto& kf : keyframes_)
    {
        if (std::abs(kf.time - time) < tolerance)
            return &kf;
    }
    return nullptr;
}

float AnimationChannel::start_time() const
{
    if (keyframes_.empty())
        return 0.0f;
    return keyframes_.front().time;
}

float AnimationChannel::end_time() const
{
    if (keyframes_.empty())
        return 0.0f;
    return keyframes_.back().time;
}

// ─── Interpolation ───────────────────────────────────────────────────────────

float AnimationChannel::evaluate(float time) const
{
    if (keyframes_.empty())
        return default_value_;

    // Before first keyframe
    if (time <= keyframes_.front().time)
        return keyframes_.front().value;

    // After last keyframe
    if (time >= keyframes_.back().time)
        return keyframes_.back().value;

    // Find the segment [i, i+1] containing time
    for (size_t i = 0; i + 1 < keyframes_.size(); ++i)
    {
        const auto& a = keyframes_[i];
        const auto& b = keyframes_[i + 1];

        if (time >= a.time && time <= b.time)
        {
            float segment_duration = b.time - a.time;
            if (segment_duration <= 0.0f)
                return a.value;

            float t = (time - a.time) / segment_duration;

            switch (a.interp)
            {
                case InterpMode::Step:
                    return interp_step(a, b, t);
                case InterpMode::Linear:
                    return interp_linear(a, b, t);
                case InterpMode::CubicBezier:
                    return interp_cubic_bezier(a, b, t);
                case InterpMode::Spring:
                    return interp_spring(a, b, t);
                case InterpMode::EaseIn:
                    return interp_ease_in(a, b, t);
                case InterpMode::EaseOut:
                    return interp_ease_out(a, b, t);
                case InterpMode::EaseInOut:
                    return interp_ease_in_out(a, b, t);
            }
        }
    }

    return keyframes_.back().value;
}

float AnimationChannel::evaluate_derivative(float time) const
{
    // Numerical derivative via central difference
    constexpr float h       = 0.001f;
    float           v_plus  = evaluate(time + h);
    float           v_minus = evaluate(time - h);
    return (v_plus - v_minus) / (2.0f * h);
}

std::vector<float> AnimationChannel::sample(float start, float end, uint32_t sample_count) const
{
    std::vector<float> result;
    if (sample_count == 0)
        return result;
    result.reserve(sample_count);

    if (sample_count == 1)
    {
        result.push_back(evaluate(start));
        return result;
    }

    float step = (end - start) / static_cast<float>(sample_count - 1);
    for (uint32_t i = 0; i < sample_count; ++i)
    {
        float t = start + step * static_cast<float>(i);
        result.push_back(evaluate(t));
    }
    return result;
}

// ─── Auto-tangent computation ────────────────────────────────────────────────

void AnimationChannel::compute_auto_tangents()
{
    for (size_t i = 0; i < keyframes_.size(); ++i)
    {
        if (keyframes_[i].tangent_mode == TangentMode::Auto)
        {
            compute_auto_tangent_at(i);
        }
    }
}

void AnimationChannel::compute_auto_tangent_at(size_t index)
{
    if (index >= keyframes_.size())
        return;

    auto& kf = keyframes_[index];

    // Catmull-Rom style: slope = (next.value - prev.value) / (next.time - prev.time)
    if (keyframes_.size() < 2)
    {
        kf.in_tangent  = TangentHandle{0.0f, 0.0f};
        kf.out_tangent = TangentHandle{0.0f, 0.0f};
        return;
    }

    float slope = 0.0f;

    if (index == 0)
    {
        // First keyframe: use forward difference
        const auto& next = keyframes_[index + 1];
        float       dt   = next.time - kf.time;
        if (dt > 0.0f)
        {
            slope = (next.value - kf.value) / dt;
        }
    }
    else if (index == keyframes_.size() - 1)
    {
        // Last keyframe: use backward difference
        const auto& prev = keyframes_[index - 1];
        float       dt   = kf.time - prev.time;
        if (dt > 0.0f)
        {
            slope = (kf.value - prev.value) / dt;
        }
    }
    else
    {
        // Interior: Catmull-Rom
        const auto& prev = keyframes_[index - 1];
        const auto& next = keyframes_[index + 1];
        float       dt   = next.time - prev.time;
        if (dt > 0.0f)
        {
            slope = (next.value - prev.value) / dt;
        }
    }

    // Set tangent handles: 1/3 of segment length in each direction
    float in_dt  = 0.0f;
    float out_dt = 0.0f;

    if (index > 0)
    {
        in_dt = (kf.time - keyframes_[index - 1].time) / 3.0f;
    }
    if (index + 1 < keyframes_.size())
    {
        out_dt = (keyframes_[index + 1].time - kf.time) / 3.0f;
    }

    kf.in_tangent  = TangentHandle{-in_dt, -slope * in_dt};
    kf.out_tangent = TangentHandle{out_dt, slope * out_dt};
}

// ─── Sorting ─────────────────────────────────────────────────────────────────

void AnimationChannel::sort_keyframes()
{
    std::sort(keyframes_.begin(),
              keyframes_.end(),
              [](const TypedKeyframe& a, const TypedKeyframe& b) { return a.time < b.time; });
}

// ─── Interpolation helpers ───────────────────────────────────────────────────

float AnimationChannel::interp_step(const TypedKeyframe& a, const TypedKeyframe& /*b*/, float /*t*/)
{
    return a.value;
}

float AnimationChannel::interp_linear(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    return a.value + (b.value - a.value) * t;
}

float AnimationChannel::interp_cubic_bezier(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    // Hermite-style cubic using tangent handles
    // P0 = a.value, P1 = a.value + a.out_tangent.dv, P2 = b.value + b.in_tangent.dv, P3 = b.value
    float p0 = a.value;
    float p1 = a.value + a.out_tangent.dv;
    float p2 = b.value + b.in_tangent.dv;
    float p3 = b.value;

    // De Casteljau / cubic Bezier evaluation
    float u   = 1.0f - t;
    float tt  = t * t;
    float uu  = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    return uuu * p0 + 3.0f * uu * t * p1 + 3.0f * u * tt * p2 + ttt * p3;
}

float AnimationChannel::interp_spring(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    // Damped spring: overshoots then settles
    // Based on critically damped spring with slight underdamping
    constexpr float omega = 10.0f;   // Natural frequency
    constexpr float zeta  = 0.6f;    // Damping ratio (< 1 = underdamped)

    float decay   = std::exp(-zeta * omega * t);
    float omega_d = omega * std::sqrt(1.0f - zeta * zeta);
    float spring_t =
        1.0f - decay * (std::cos(omega_d * t) + (zeta * omega / omega_d) * std::sin(omega_d * t));

    return a.value + (b.value - a.value) * spring_t;
}

float AnimationChannel::interp_ease_in(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    float eased = t * t;
    return a.value + (b.value - a.value) * eased;
}

float AnimationChannel::interp_ease_out(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    float eased = 1.0f - (1.0f - t) * (1.0f - t);
    return a.value + (b.value - a.value) * eased;
}

float AnimationChannel::interp_ease_in_out(const TypedKeyframe& a, const TypedKeyframe& b, float t)
{
    float eased = NAN;
    if (t < 0.5f)
    {
        eased = 2.0f * t * t;
    }
    else
    {
        eased = 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    }
    return a.value + (b.value - a.value) * eased;
}

// ─── KeyframeInterpolator ────────────────────────────────────────────────────

uint32_t KeyframeInterpolator::add_channel(const std::string& name, float default_value)
{
    std::lock_guard lock(mutex_);
    uint32_t        id = next_channel_id_++;
    channels_.emplace_back(id, AnimationChannel(name, default_value));
    return id;
}

uint32_t KeyframeInterpolator::add_channel_with_id(uint32_t           channel_id,
                                                   const std::string& name,
                                                   float              default_value)
{
    if (channel_id == 0)
        return 0;
    std::lock_guard lock(mutex_);
    if (auto* existing = find_channel_unlocked(channel_id))
    {
        existing->set_name(name);
        existing->set_default_value(default_value);
        return channel_id;
    }
    channels_.emplace_back(channel_id, AnimationChannel(name, default_value));
    next_channel_id_ = std::max(next_channel_id_, channel_id + 1);
    return channel_id;
}

void KeyframeInterpolator::remove_channel(uint32_t channel_id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(channels_, [channel_id](const auto& pair) { return pair.first == channel_id; });
    // Also remove bindings for this channel
    std::erase_if(bindings_,
                  [channel_id](const PropertyBinding& b) { return b.channel_id == channel_id; });
}

AnimationChannel* KeyframeInterpolator::channel(uint32_t channel_id)
{
    std::lock_guard lock(mutex_);
    return find_channel_unlocked(channel_id);
}

const AnimationChannel* KeyframeInterpolator::channel(uint32_t channel_id) const
{
    std::lock_guard lock(mutex_);
    return find_channel_unlocked(channel_id);
}

const std::vector<std::pair<uint32_t, AnimationChannel>>& KeyframeInterpolator::channels() const
{
    std::lock_guard lock(mutex_);
    return channels_;
}

size_t KeyframeInterpolator::channel_count() const
{
    std::lock_guard lock(mutex_);
    return channels_.size();
}

// ─── Property bindings ───────────────────────────────────────────────────────

void KeyframeInterpolator::bind(uint32_t           channel_id,
                                const std::string& prop_name,
                                float*             target,
                                float              scale,
                                float              offset)
{
    std::lock_guard lock(mutex_);
    PropertyBinding b;
    b.channel_id    = channel_id;
    b.property_name = prop_name;
    b.target        = target;
    b.scale         = scale;
    b.offset        = offset;
    bindings_.push_back(std::move(b));
}

void KeyframeInterpolator::bind_color(uint32_t           channel_id,
                                      const std::string& prop_name,
                                      Color*             target)
{
    std::lock_guard lock(mutex_);
    PropertyBinding b;
    b.channel_id    = channel_id;
    b.property_name = prop_name;
    b.target        = target;
    bindings_.push_back(std::move(b));
}

void KeyframeInterpolator::bind_callback(uint32_t                   channel_id,
                                         const std::string&         prop_name,
                                         std::function<void(float)> callback,
                                         float                      scale,
                                         float                      offset)
{
    std::lock_guard lock(mutex_);
    PropertyBinding b;
    b.channel_id    = channel_id;
    b.property_name = prop_name;
    b.target        = std::move(callback);
    b.scale         = scale;
    b.offset        = offset;
    bindings_.push_back(std::move(b));
}

void KeyframeInterpolator::bind_camera(Camera*  cam,
                                       uint32_t az_ch,
                                       uint32_t el_ch,
                                       uint32_t dist_ch,
                                       uint32_t fov_ch)
{
    std::lock_guard lock(mutex_);
    // Remove existing binding for this camera if any
    std::erase_if(camera_bindings_,
                  [cam](const CameraBinding& b) { return b.target_camera == cam; });

    CameraBinding b;
    b.target_camera = cam;
    b.azimuth_id    = az_ch;
    b.elevation_id  = el_ch;
    b.distance_id   = dist_ch;
    b.fov_id        = fov_ch;
    camera_bindings_.push_back(b);
}

void KeyframeInterpolator::unbind_camera(Camera* cam)
{
    std::lock_guard lock(mutex_);
    std::erase_if(camera_bindings_,
                  [cam](const CameraBinding& b) { return b.target_camera == cam; });
}

void KeyframeInterpolator::unbind(uint32_t channel_id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(bindings_,
                  [channel_id](const PropertyBinding& b) { return b.channel_id == channel_id; });
}

void KeyframeInterpolator::unbind_all()
{
    std::lock_guard lock(mutex_);
    bindings_.clear();
    camera_bindings_.clear();
}

const std::vector<PropertyBinding>& KeyframeInterpolator::bindings() const
{
    std::lock_guard lock(mutex_);
    return bindings_;
}

// ─── Evaluation ──────────────────────────────────────────────────────────────

void KeyframeInterpolator::evaluate(float time)
{
    std::lock_guard lock(mutex_);

    for (const auto& binding : bindings_)
    {
        const auto* ch = find_channel_unlocked(binding.channel_id);
        if (!ch)
            continue;

        float raw   = ch->evaluate(time);
        float value = raw * binding.scale + binding.offset;

        std::visit(
            [&](auto&& target)
            {
                using T = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<T, float*>)
                {
                    if (target)
                        *target = value;
                }
                else if constexpr (std::is_same_v<T, Color*>)
                {
                    if (target)
                    {
                        // Map value [0,1] to color alpha, or use as intensity
                        target->a = std::clamp(value, 0.0f, 1.0f);
                    }
                }
                else if constexpr (std::is_same_v<T, std::function<void(float)>>)
                {
                    if (target)
                        target(value);
                }
            },
            binding.target);
    }

    // Process camera bindings
    for (const auto& binding : camera_bindings_)
    {
        bool changed = false;

        if (binding.azimuth_id != 0)
        {
            if (const auto* ch = find_channel_unlocked(binding.azimuth_id))
            {
                binding.target_camera->azimuth = ch->evaluate(time);
                changed                        = true;
            }
        }
        if (binding.elevation_id != 0)
        {
            if (const auto* ch = find_channel_unlocked(binding.elevation_id))
            {
                binding.target_camera->elevation = ch->evaluate(time);
                changed                          = true;
            }
        }
        if (binding.distance_id != 0)
        {
            if (const auto* ch = find_channel_unlocked(binding.distance_id))
            {
                binding.target_camera->distance = ch->evaluate(time);
                changed                         = true;
            }
        }
        if (binding.fov_id != 0)
        {
            if (const auto* ch = find_channel_unlocked(binding.fov_id))
            {
                binding.target_camera->fov = ch->evaluate(time);
                // No positional update needed for fov, but good to mark
            }
        }

        if (changed)
        {
            binding.target_camera->update_position_from_orbit();
        }
    }
}

float KeyframeInterpolator::evaluate_channel(uint32_t channel_id, float time) const
{
    std::lock_guard lock(mutex_);
    const auto*     ch = find_channel_unlocked(channel_id);
    if (!ch)
        return 0.0f;
    return ch->evaluate(time);
}

// ─── Batch operations ────────────────────────────────────────────────────────

void KeyframeInterpolator::add_keyframe(uint32_t channel_id, const TypedKeyframe& kf)
{
    std::lock_guard lock(mutex_);
    auto*           ch = find_channel_unlocked(channel_id);
    if (ch)
        ch->add_keyframe(kf);
}

bool KeyframeInterpolator::remove_keyframe(uint32_t channel_id, float time)
{
    std::lock_guard lock(mutex_);
    auto*           ch = find_channel_unlocked(channel_id);
    if (!ch)
        return false;
    return ch->remove_keyframe(time);
}

void KeyframeInterpolator::compute_all_auto_tangents()
{
    std::lock_guard lock(mutex_);
    for (auto& [id, ch] : channels_)
    {
        ch.compute_auto_tangents();
    }
}

// ─── Serialization ───────────────────────────────────────────────────────────

namespace
{

void json_escape(std::ostringstream& ss, const std::string& s)
{
    ss << '"';
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                ss << c;
                break;
        }
    }
    ss << '"';
}

// Minimal JSON parser helpers
std::string extract_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos)
        return "";
    return json.substr(pos, end - pos);
}

float extract_float(const std::string& json, const std::string& key, float def = 0.0f)
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
}

int extract_int(const std::string& json, const std::string& key, int def = 0)
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
}

}   // anonymous namespace

std::string KeyframeInterpolator::serialize() const
{
    std::lock_guard    lock(mutex_);
    std::ostringstream ss;
    ss << "{\"channels\":[";

    for (size_t ci = 0; ci < channels_.size(); ++ci)
    {
        if (ci > 0)
            ss << ",";
        const auto& [id, ch] = channels_[ci];
        ss << "{\"id\":" << id << ",\"name\":";
        json_escape(ss, ch.name());
        ss << ",\"default\":" << ch.default_value();
        ss << ",\"keyframes\":[";

        const auto& kfs = ch.keyframes();
        for (size_t ki = 0; ki < kfs.size(); ++ki)
        {
            if (ki > 0)
                ss << ",";
            const auto& kf = kfs[ki];
            ss << "{\"t\":" << kf.time << ",\"v\":" << kf.value
               << ",\"i\":" << static_cast<int>(kf.interp)
               << ",\"tm\":" << static_cast<int>(kf.tangent_mode) << ",\"it\":[" << kf.in_tangent.dt
               << "," << kf.in_tangent.dv << "]"
               << ",\"ot\":[" << kf.out_tangent.dt << "," << kf.out_tangent.dv << "]"
               << "}";
        }
        ss << "]}";
    }

    ss << "]}";
    return ss.str();
}

bool KeyframeInterpolator::deserialize(const std::string& json)
{
    std::lock_guard lock(mutex_);

    // Very minimal JSON parsing — sufficient for our own serialization format
    if (json.find("\"channels\"") == std::string::npos)
        return false;

    channels_.clear();
    bindings_.clear();

    // Find channel blocks
    size_t pos = json.find("[", json.find("\"channels\""));
    if (pos == std::string::npos)
        return false;
    pos++;

    int    depth       = 0;
    size_t block_start = pos;

    while (pos < json.size())
    {
        if (json[pos] == '{')
        {
            if (depth == 0)
                block_start = pos;
            depth++;
        }
        else if (json[pos] == '}')
        {
            depth--;
            if (depth == 0)
            {
                // Parse this channel block
                std::string block = json.substr(block_start, pos - block_start + 1);

                uint32_t    id   = static_cast<uint32_t>(extract_int(block, "id"));
                std::string name = extract_string(block, "name");
                float       def  = extract_float(block, "default");

                AnimationChannel ch(name, def);

                // Parse keyframes within this block
                size_t kf_pos = block.find("\"keyframes\"");
                if (kf_pos != std::string::npos)
                {
                    size_t kf_arr = block.find("[", kf_pos);
                    if (kf_arr != std::string::npos)
                    {
                        kf_arr++;
                        int    kf_depth = 0;
                        size_t kf_start = kf_arr;

                        while (kf_arr < block.size())
                        {
                            if (block[kf_arr] == '{')
                            {
                                if (kf_depth == 0)
                                    kf_start = kf_arr;
                                kf_depth++;
                            }
                            else if (block[kf_arr] == '}')
                            {
                                kf_depth--;
                                if (kf_depth == 0)
                                {
                                    std::string kf_block =
                                        block.substr(kf_start, kf_arr - kf_start + 1);

                                    TypedKeyframe kf;
                                    kf.time   = extract_float(kf_block, "t");
                                    kf.value  = extract_float(kf_block, "v");
                                    kf.interp = static_cast<InterpMode>(extract_int(kf_block, "i"));
                                    kf.tangent_mode =
                                        static_cast<TangentMode>(extract_int(kf_block, "tm"));

                                    // Parse tangent arrays [dt, dv]
                                    auto parse_tangent =
                                        [&](const std::string& key) -> TangentHandle
                                    {
                                        std::string search = "\"" + key + "\":[";
                                        auto        tpos   = kf_block.find(search);
                                        if (tpos == std::string::npos)
                                            return {};
                                        tpos += search.size();
                                        auto comma   = kf_block.find(',', tpos);
                                        auto bracket = kf_block.find(']', tpos);
                                        if (comma == std::string::npos
                                            || bracket == std::string::npos)
                                            return {};
                                        try
                                        {
                                            float dt =
                                                std::stof(kf_block.substr(tpos, comma - tpos));
                                            float dv = std::stof(
                                                kf_block.substr(comma + 1, bracket - comma - 1));
                                            return TangentHandle{dt, dv};
                                        }
                                        catch (...)
                                        {
                                            return {};
                                        }
                                    };

                                    kf.in_tangent  = parse_tangent("it");
                                    kf.out_tangent = parse_tangent("ot");

                                    ch.add_keyframe(kf);
                                }
                            }
                            else if (block[kf_arr] == ']' && kf_depth == 0)
                            {
                                break;
                            }
                            kf_arr++;
                        }
                    }
                }

                if (id >= next_channel_id_)
                    next_channel_id_ = id + 1;
                channels_.emplace_back(id, std::move(ch));
            }
        }
        else if (json[pos] == ']' && depth == 0)
        {
            break;
        }
        pos++;
    }

    return true;
}

// ─── Queries ─────────────────────────────────────────────────────────────────

float KeyframeInterpolator::duration() const
{
    std::lock_guard lock(mutex_);
    float           max_end = 0.0f;
    for (const auto& [id, ch] : channels_)
    {
        max_end = std::max(max_end, ch.end_time());
    }
    return max_end;
}

size_t KeyframeInterpolator::total_keyframe_count() const
{
    std::lock_guard lock(mutex_);
    size_t          count = 0;
    for (const auto& [id, ch] : channels_)
    {
        count += ch.keyframe_count();
    }
    return count;
}

// ─── Internal helpers ────────────────────────────────────────────────────────

AnimationChannel* KeyframeInterpolator::find_channel_unlocked(uint32_t id)
{
    for (auto& [cid, ch] : channels_)
    {
        if (cid == id)
            return &ch;
    }
    return nullptr;
}

const AnimationChannel* KeyframeInterpolator::find_channel_unlocked(uint32_t id) const
{
    for (const auto& [cid, ch] : channels_)
    {
        if (cid == id)
            return &ch;
    }
    return nullptr;
}

}   // namespace spectra
