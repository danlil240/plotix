#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/animator.hpp>
#include <spectra/camera.hpp>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <variant>
#include <vector>

namespace spectra
{

// Interpolation mode for keyframe segments.
enum class InterpMode : uint8_t
{
    Step,          // Hold previous value until next keyframe
    Linear,        // Linear interpolation
    CubicBezier,   // Cubic bezier with tangent handles
    Spring,        // Spring-based overshoot interpolation
    EaseIn,        // Quadratic ease-in
    EaseOut,       // Quadratic ease-out
    EaseInOut,     // Cubic ease-in-out
};

// Tangent handle for cubic bezier interpolation.
// Stored as time/value offsets relative to the keyframe position.
struct TangentHandle
{
    float dt = 0.0f;   // Time offset (always positive for out, negative for in)
    float dv = 0.0f;   // Value offset

    constexpr TangentHandle() = default;
    constexpr TangentHandle(float dt, float dv) : dt(dt), dv(dv) {}
};

// Tangent mode controls how in/out tangents relate to each other.
enum class TangentMode : uint8_t
{
    Free,      // In and out tangents are independent
    Aligned,   // In and out tangents are co-linear (smooth)
    Flat,      // Both tangents are horizontal (zero slope)
    Auto,      // Automatically computed for smooth curves (Catmull-Rom style)
};

// A typed keyframe with value, interpolation mode, and tangent handles.
struct TypedKeyframe
{
    float         time         = 0.0f;
    float         value        = 0.0f;
    InterpMode    interp       = InterpMode::Linear;
    TangentMode   tangent_mode = TangentMode::Auto;
    TangentHandle in_tangent;    // Incoming tangent (from previous keyframe)
    TangentHandle out_tangent;   // Outgoing tangent (to next keyframe)
    bool          selected = false;

    constexpr TypedKeyframe() = default;
    constexpr TypedKeyframe(float t, float v, InterpMode mode = InterpMode::Linear)
        : time(t), value(v), interp(mode)
    {
    }
};

// A single animation channel (e.g., "X Position", "Opacity", "Line Width").
// Stores a sorted list of typed keyframes and provides interpolation.
class AnimationChannel
{
   public:
    AnimationChannel() = default;
    explicit AnimationChannel(const std::string& name, float default_value = 0.0f);

    // ─── Channel metadata ────────────────────────────────────────────
    const std::string& name() const { return name_; }
    void               set_name(const std::string& n) { name_ = n; }

    float default_value() const { return default_value_; }
    void  set_default_value(float v) { default_value_ = v; }

    float min_value() const { return min_value_; }
    float max_value() const { return max_value_; }
    void  set_value_range(float min_val, float max_val);

    bool has_value_range() const { return has_range_; }

    // ─── Keyframe management ─────────────────────────────────────────

    // Add a keyframe. If one exists at the same time (within tolerance), update it.
    void add_keyframe(const TypedKeyframe& kf);

    // Remove keyframe at the given time (within tolerance).
    bool remove_keyframe(float time, float tolerance = 0.001f);

    // Move a keyframe from old_time to new_time.
    bool move_keyframe(float old_time, float new_time, float tolerance = 0.001f);

    // Set the value of a keyframe at the given time.
    bool set_keyframe_value(float time, float value, float tolerance = 0.001f);

    // Set the interpolation mode of a keyframe.
    bool set_keyframe_interp(float time, InterpMode mode, float tolerance = 0.001f);

    // Set tangent handles for a keyframe.
    bool set_keyframe_tangents(float         time,
                               TangentHandle in,
                               TangentHandle out,
                               float         tolerance = 0.001f);

    // Set tangent mode for a keyframe.
    bool set_keyframe_tangent_mode(float time, TangentMode mode, float tolerance = 0.001f);

    // Clear all keyframes.
    void clear();

    // ─── Queries ─────────────────────────────────────────────────────

    const std::vector<TypedKeyframe>& keyframes() const { return keyframes_; }
    size_t                            keyframe_count() const { return keyframes_.size(); }
    bool                              empty() const { return keyframes_.empty(); }

    // Find keyframe at time (within tolerance). Returns nullptr if not found.
    TypedKeyframe*       find_keyframe(float time, float tolerance = 0.001f);
    const TypedKeyframe* find_keyframe(float time, float tolerance = 0.001f) const;

    // Time range spanned by keyframes.
    float start_time() const;
    float end_time() const;

    // ─── Interpolation ───────────────────────────────────────────────

    // Evaluate the channel at a given time. Returns interpolated value.
    float evaluate(float time) const;

    // Evaluate the derivative (velocity) at a given time.
    float evaluate_derivative(float time) const;

    // Sample the channel at regular intervals for curve display.
    // Returns (sample_count) values from start_time to end_time.
    std::vector<float> sample(float start, float end, uint32_t sample_count) const;

    // ─── Auto-tangent computation ────────────────────────────────────

    // Recompute auto tangents for all keyframes that have TangentMode::Auto.
    void compute_auto_tangents();

   private:
    std::string name_;
    float       default_value_ = 0.0f;
    float       min_value_     = 0.0f;
    float       max_value_     = 1.0f;
    bool        has_range_     = false;

    std::vector<TypedKeyframe> keyframes_;   // Always sorted by time

    void sort_keyframes();
    void compute_auto_tangent_at(size_t index);

    // Interpolation helpers
    static float interp_step(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_linear(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_cubic_bezier(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_spring(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_ease_in(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_ease_out(const TypedKeyframe& a, const TypedKeyframe& b, float t);
    static float interp_ease_in_out(const TypedKeyframe& a, const TypedKeyframe& b, float t);
};

// Animatable property target types.
using AnimatableValue = std::variant<float*, Color*, std::function<void(float)>>;

// Property binding: connects an AnimationChannel to a target property.
struct PropertyBinding
{
    uint32_t        channel_id = 0;
    std::string     property_name;
    AnimatableValue target;
    float           scale  = 1.0f;   // Multiplier applied to channel output
    float           offset = 0.0f;   // Offset added after scale
};

// Camera binding: connects 4 channels to camera parameters.
struct CameraBinding
{
    Camera*  target_camera;
    uint32_t azimuth_id   = 0;
    uint32_t elevation_id = 0;
    uint32_t distance_id  = 0;
    uint32_t fov_id       = 0;
};

// KeyframeInterpolator — manages multiple animation channels and property bindings.
//
// This is the core system that bridges TimelineEditor keyframes with actual
// property animation. Each channel stores typed keyframes with interpolation
// modes, and property bindings connect channels to runtime targets.
//
// Thread-safe: all public methods lock an internal mutex.
class KeyframeInterpolator
{
   public:
    KeyframeInterpolator()  = default;
    ~KeyframeInterpolator() = default;

    KeyframeInterpolator(const KeyframeInterpolator&)            = delete;
    KeyframeInterpolator& operator=(const KeyframeInterpolator&) = delete;

    // ─── Channel management ──────────────────────────────────────────

    // Create a new animation channel. Returns channel ID.
    uint32_t add_channel(const std::string& name, float default_value = 0.0f);

    // Create or replace a channel with a caller-owned stable ID.
    uint32_t add_channel_with_id(uint32_t           channel_id,
                                 const std::string& name,
                                 float              default_value = 0.0f);

    // Remove a channel by ID.
    void remove_channel(uint32_t channel_id);

    // Get a channel by ID.
    AnimationChannel*       channel(uint32_t channel_id);
    const AnimationChannel* channel(uint32_t channel_id) const;

    // Get all channels.
    const std::vector<std::pair<uint32_t, AnimationChannel>>& channels() const;

    size_t channel_count() const;

    // ─── Property bindings ───────────────────────────────────────────

    // Bind a channel to a float pointer target.
    void bind(uint32_t           channel_id,
              const std::string& prop_name,
              float*             target,
              float              scale  = 1.0f,
              float              offset = 0.0f);

    // Bind a channel to a Color target (channel controls one RGBA component).
    void bind_color(uint32_t channel_id, const std::string& prop_name, Color* target);

    // Bind a channel to a callback function.
    void bind_callback(uint32_t                   channel_id,
                       const std::string&         prop_name,
                       std::function<void(float)> callback,
                       float                      scale  = 1.0f,
                       float                      offset = 0.0f);

    // Bind channels to a Camera target.
    // Pass 0 for any channel you don't want to bind.
    void bind_camera(Camera*  cam,
                     uint32_t az_ch,
                     uint32_t el_ch,
                     uint32_t dist_ch,
                     uint32_t fov_ch);

    // Remove camera binding.
    void unbind_camera(Camera* cam);

    // Remove all bindings for a channel.
    void unbind(uint32_t channel_id);

    // Remove all bindings.
    void unbind_all();

    const std::vector<PropertyBinding>& bindings() const;

    // ─── Evaluation ──────────────────────────────────────────────────

    // Evaluate all channels at the given time and apply to bound properties.
    void evaluate(float time);

    // Evaluate a single channel (does not apply to bindings).
    float evaluate_channel(uint32_t channel_id, float time) const;

    // ─── Batch operations ────────────────────────────────────────────

    // Add a keyframe to a channel.
    void add_keyframe(uint32_t channel_id, const TypedKeyframe& kf);

    // Remove a keyframe from a channel.
    bool remove_keyframe(uint32_t channel_id, float time);

    // Recompute auto tangents for all channels.
    void compute_all_auto_tangents();

    // ─── Serialization ───────────────────────────────────────────────

    // Serialize all channels and keyframes to a minimal JSON string.
    std::string serialize() const;

    // Deserialize channels and keyframes from a JSON string.
    bool deserialize(const std::string& json);

    // ─── Queries ─────────────────────────────────────────────────────

    // Total duration across all channels.
    float duration() const;

    // Total keyframe count across all channels.
    size_t total_keyframe_count() const;

   private:
    mutable std::mutex mutex_;

    std::vector<std::pair<uint32_t, AnimationChannel>> channels_;
    std::vector<PropertyBinding>                       bindings_;
    std::vector<CameraBinding>                         camera_bindings_;
    uint32_t                                           next_channel_id_ = 1;

    AnimationChannel*       find_channel_unlocked(uint32_t id);
    const AnimationChannel* find_channel_unlocked(uint32_t id) const;
};

// ─── Free functions ──────────────────────────────────────────────────────────

// Convert InterpMode to a human-readable string.
const char* interp_mode_name(InterpMode mode);

// Convert TangentMode to a human-readable string.
const char* tangent_mode_name(TangentMode mode);

}   // namespace spectra
