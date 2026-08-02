#include <cstring>
#include <gtest/gtest.h>

#include "ui/animation/animation_curve_editor.hpp"
#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/animation/recording_export.hpp"
#include "ui/animation/timeline_editor.hpp"

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Basic
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelBasic, DefaultConstruction)
{
    AnimationChannel ch;
    EXPECT_TRUE(ch.empty());
    EXPECT_EQ(ch.keyframe_count(), 0u);
    EXPECT_FLOAT_EQ(ch.default_value(), 0.0f);
    EXPECT_FLOAT_EQ(ch.start_time(), 0.0f);
    EXPECT_FLOAT_EQ(ch.end_time(), 0.0f);
}

TEST(AnimChannelBasic, NamedConstruction)
{
    AnimationChannel ch("Opacity", 1.0f);
    EXPECT_EQ(ch.name(), "Opacity");
    EXPECT_FLOAT_EQ(ch.default_value(), 1.0f);
}

TEST(AnimChannelBasic, ValueRange)
{
    AnimationChannel ch("Scale");
    EXPECT_FALSE(ch.has_value_range());
    ch.set_value_range(0.0f, 10.0f);
    EXPECT_TRUE(ch.has_value_range());
    EXPECT_FLOAT_EQ(ch.min_value(), 0.0f);
    EXPECT_FLOAT_EQ(ch.max_value(), 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Keyframe Management
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelKeyframes, AddSingle)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    EXPECT_EQ(ch.keyframe_count(), 1u);
    EXPECT_FLOAT_EQ(ch.keyframes()[0].time, 0.0f);
    EXPECT_FLOAT_EQ(ch.keyframes()[0].value, 0.0f);
}

TEST(AnimChannelKeyframes, AddMultipleSorted)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(2.0f, 20.0f));
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_EQ(ch.keyframe_count(), 3u);
    EXPECT_FLOAT_EQ(ch.keyframes()[0].time, 0.0f);
    EXPECT_FLOAT_EQ(ch.keyframes()[1].time, 1.0f);
    EXPECT_FLOAT_EQ(ch.keyframes()[2].time, 2.0f);
}

TEST(AnimChannelKeyframes, DuplicateTimeUpdatesValue)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_EQ(ch.keyframe_count(), 1u);
    EXPECT_FLOAT_EQ(ch.keyframes()[0].value, 10.0f);
}

TEST(AnimChannelKeyframes, Remove)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_TRUE(ch.remove_keyframe(0.0f));
    EXPECT_EQ(ch.keyframe_count(), 1u);
    EXPECT_FLOAT_EQ(ch.keyframes()[0].time, 1.0f);
}

TEST(AnimChannelKeyframes, RemoveNonExistent)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_FALSE(ch.remove_keyframe(5.0f));
    EXPECT_EQ(ch.keyframe_count(), 1u);
}

TEST(AnimChannelKeyframes, Move)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_TRUE(ch.move_keyframe(0.0f, 0.5f));
    EXPECT_FLOAT_EQ(ch.keyframes()[0].time, 0.5f);
    EXPECT_FLOAT_EQ(ch.keyframes()[1].time, 1.0f);
}

TEST(AnimChannelKeyframes, SetValue)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    EXPECT_TRUE(ch.set_keyframe_value(1.0f, 99.0f));
    EXPECT_FLOAT_EQ(ch.keyframes()[0].value, 99.0f);
}

TEST(AnimChannelKeyframes, SetInterp)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    EXPECT_TRUE(ch.set_keyframe_interp(1.0f, InterpMode::CubicBezier));
    EXPECT_EQ(ch.keyframes()[0].interp, InterpMode::CubicBezier);
}

TEST(AnimChannelKeyframes, Clear)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    ch.clear();
    EXPECT_TRUE(ch.empty());
}

TEST(AnimChannelKeyframes, FindKeyframe)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    EXPECT_NE(ch.find_keyframe(1.0f), nullptr);
    EXPECT_EQ(ch.find_keyframe(2.0f), nullptr);
    EXPECT_NE(ch.find_keyframe(1.0005f), nullptr);   // Within tolerance
}

TEST(AnimChannelKeyframes, TimeRange)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(2.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(5.0f, 10.0f));
    EXPECT_FLOAT_EQ(ch.start_time(), 2.0f);
    EXPECT_FLOAT_EQ(ch.end_time(), 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Interpolation: Step
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelInterpStep, HoldsValue)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Step));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f, InterpMode::Step));

    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(0.5f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(0.99f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(1.0f), 10.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Interpolation: Linear
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelInterpLinear, BasicLerp)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f, InterpMode::Linear));

    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(0.5f), 5.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(1.0f), 10.0f);
}

TEST(AnimChannelInterpLinear, MultiSegment)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(2.0f, 5.0f, InterpMode::Linear));

    EXPECT_FLOAT_EQ(ch.evaluate(0.5f), 5.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(1.5f), 7.5f);
}

TEST(AnimChannelInterpLinear, BeforeFirst)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 10.0f);
}

TEST(AnimChannelInterpLinear, AfterLast)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 5.0f));
    EXPECT_FLOAT_EQ(ch.evaluate(100.0f), 5.0f);
}

TEST(AnimChannelInterpLinear, EmptyReturnsDefault)
{
    AnimationChannel ch("X", 42.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 42.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Interpolation: EaseIn/Out/InOut
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelInterpEase, EaseInStartsSlow)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::EaseIn));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    float at_quarter     = ch.evaluate(0.25f);
    float linear_quarter = 2.5f;
    EXPECT_LT(at_quarter, linear_quarter);   // Ease-in is slower at start
}

TEST(AnimChannelInterpEase, EaseOutStartsFast)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::EaseOut));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    float at_quarter     = ch.evaluate(0.25f);
    float linear_quarter = 2.5f;
    EXPECT_GT(at_quarter, linear_quarter);   // Ease-out is faster at start
}

TEST(AnimChannelInterpEase, EaseInOutEndpoints)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::EaseInOut));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(1.0f), 10.0f);
    EXPECT_NEAR(ch.evaluate(0.5f), 5.0f, 0.01f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Interpolation: Spring
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelInterpSpring, Overshoots)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Spring));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    // Spring should overshoot the target at some point
    bool overshot = false;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        if (ch.evaluate(t) > 10.0f)
        {
            overshot = true;
            break;
        }
    }
    EXPECT_TRUE(overshot);
}

TEST(AnimChannelInterpSpring, SettlesToTarget)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Spring));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    EXPECT_NEAR(ch.evaluate(1.0f), 10.0f, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Interpolation: CubicBezier
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelInterpBezier, Endpoints)
{
    AnimationChannel ch("X");
    TypedKeyframe    kf0(0.0f, 0.0f, InterpMode::CubicBezier);
    kf0.out_tangent = TangentHandle{0.3f, 5.0f};
    TypedKeyframe kf1(1.0f, 10.0f, InterpMode::CubicBezier);
    kf1.in_tangent = TangentHandle{-0.3f, -5.0f};

    ch.add_keyframe(kf0);
    ch.add_keyframe(kf1);

    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(1.0f), 10.0f);
}

TEST(AnimChannelInterpBezier, MidpointInfluencedByTangents)
{
    AnimationChannel ch("X");
    TypedKeyframe    kf0(0.0f, 0.0f, InterpMode::CubicBezier);
    kf0.tangent_mode = TangentMode::Free;
    kf0.out_tangent  = TangentHandle{0.3f, 10.0f};   // Strong upward pull
    TypedKeyframe kf1(1.0f, 0.0f, InterpMode::CubicBezier);
    kf1.tangent_mode = TangentMode::Free;
    kf1.in_tangent   = TangentHandle{-0.3f, 10.0f};   // Strong upward pull

    ch.add_keyframe(kf0);
    ch.add_keyframe(kf1);

    // Midpoint should be pulled upward by tangents
    float mid = ch.evaluate(0.5f);
    EXPECT_GT(mid, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Tangent Modes
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelTangents, FlatTangent)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_TRUE(ch.set_keyframe_tangent_mode(0.0f, TangentMode::Flat));
    auto* kf = ch.find_keyframe(0.0f);
    EXPECT_FLOAT_EQ(kf->in_tangent.dv, 0.0f);
    EXPECT_FLOAT_EQ(kf->out_tangent.dv, 0.0f);
}

TEST(AnimChannelTangents, AutoTangentComputed)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    ch.add_keyframe(TypedKeyframe(2.0f, 0.0f));

    // Set middle keyframe to Auto
    ch.set_keyframe_tangent_mode(1.0f, TangentMode::Auto);
    auto* kf = ch.find_keyframe(1.0f);
    EXPECT_NE(kf, nullptr);
    // Catmull-Rom: slope at middle = (0 - 0) / (2 - 0) = 0
    // So tangent dv should be ~0
    EXPECT_NEAR(kf->out_tangent.dv, 0.0f, 0.01f);
}

TEST(AnimChannelTangents, SetCustomTangents)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    TangentHandle in{-0.5f, -2.0f};
    TangentHandle out{0.5f, 3.0f};
    EXPECT_TRUE(ch.set_keyframe_tangents(1.0f, in, out));
    auto* kf = ch.find_keyframe(1.0f);
    EXPECT_FLOAT_EQ(kf->in_tangent.dt, -0.5f);
    EXPECT_FLOAT_EQ(kf->out_tangent.dv, 3.0f);
    EXPECT_EQ(kf->tangent_mode, TangentMode::Free);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationChannel — Derivative & Sampling
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AnimChannelDerivative, LinearSlope)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    float deriv = ch.evaluate_derivative(0.5f);
    EXPECT_NEAR(deriv, 10.0f, 0.1f);   // Slope = 10/1 = 10
}

TEST(AnimChannelSample, CorrectCount)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));

    auto samples = ch.sample(0.0f, 1.0f, 11);
    EXPECT_EQ(samples.size(), 11u);
    EXPECT_FLOAT_EQ(samples[0], 0.0f);
    EXPECT_FLOAT_EQ(samples[10], 10.0f);
    EXPECT_NEAR(samples[5], 5.0f, 0.01f);
}

TEST(AnimChannelSample, SingleSample)
{
    AnimationChannel ch("X", 42.0f);
    auto             samples = ch.sample(0.0f, 1.0f, 1);
    EXPECT_EQ(samples.size(), 1u);
    EXPECT_FLOAT_EQ(samples[0], 42.0f);
}

TEST(AnimChannelSample, ZeroCount)
{
    AnimationChannel ch("X");
    auto             samples = ch.sample(0.0f, 1.0f, 0);
    EXPECT_TRUE(samples.empty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// KeyframeInterpolator — Channel Management
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeyframeInterp, AddChannel)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("Opacity", 1.0f);
    EXPECT_EQ(ki.channel_count(), 1u);
    auto* ch = ki.channel(id);
    EXPECT_NE(ch, nullptr);
    EXPECT_EQ(ch->name(), "Opacity");
    EXPECT_FLOAT_EQ(ch->default_value(), 1.0f);
}

TEST(KeyframeInterp, RemoveChannel)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.remove_channel(id);
    EXPECT_EQ(ki.channel_count(), 0u);
    EXPECT_EQ(ki.channel(id), nullptr);
}

TEST(KeyframeInterp, MultipleChannels)
{
    KeyframeInterpolator ki;
    uint32_t             a = ki.add_channel("X");
    uint32_t             b = ki.add_channel("Y");
    uint32_t             c = ki.add_channel("Z");
    EXPECT_EQ(ki.channel_count(), 3u);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
}

// ═══════════════════════════════════════════════════════════════════════════════
// KeyframeInterpolator — Evaluation
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeyframeInterpEval, EvaluateChannel)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));

    EXPECT_FLOAT_EQ(ki.evaluate_channel(id, 0.5f), 5.0f);
}

TEST(KeyframeInterpEval, EvaluateNonExistent)
{
    KeyframeInterpolator ki;
    EXPECT_FLOAT_EQ(ki.evaluate_channel(999, 0.5f), 0.0f);
}

TEST(KeyframeInterpEval, EvaluateBindFloat)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));

    float target = -1.0f;
    ki.bind(id, "x_pos", &target);
    ki.evaluate(0.5f);
    EXPECT_FLOAT_EQ(target, 5.0f);
}

TEST(KeyframeInterpEval, EvaluateBindWithScale)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 1.0f));

    float target = 0.0f;
    ki.bind(id, "scaled", &target, 100.0f, 5.0f);
    ki.evaluate(0.5f);
    EXPECT_FLOAT_EQ(target, 55.0f);   // 0.5 * 100 + 5
}

TEST(KeyframeInterpEval, EvaluateBindCallback)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));

    float received = -1.0f;
    ki.bind_callback(id, "cb", [&](float v) { received = v; });
    ki.evaluate(0.5f);
    EXPECT_FLOAT_EQ(received, 5.0f);
}

TEST(KeyframeInterpEval, UnbindChannel)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));

    float target = 0.0f;
    ki.bind(id, "x", &target);
    ki.unbind(id);
    ki.evaluate(0.5f);
    EXPECT_FLOAT_EQ(target, 0.0f);   // Not updated after unbind
}

TEST(KeyframeInterpEval, UnbindAll)
{
    KeyframeInterpolator ki;
    uint32_t             a  = ki.add_channel("X");
    uint32_t             b  = ki.add_channel("Y");
    float                tx = 0.0f, ty = 0.0f;
    ki.bind(a, "x", &tx);
    ki.bind(b, "y", &ty);
    ki.unbind_all();
    EXPECT_TRUE(ki.bindings().empty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// KeyframeInterpolator — Batch & Queries
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeyframeInterpBatch, AddKeyframe)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));
    EXPECT_EQ(ki.total_keyframe_count(), 2u);
}

TEST(KeyframeInterpBatch, RemoveKeyframe)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("X");
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 10.0f));
    EXPECT_TRUE(ki.remove_keyframe(id, 0.0f));
    EXPECT_EQ(ki.total_keyframe_count(), 1u);
}

TEST(KeyframeInterpBatch, Duration)
{
    KeyframeInterpolator ki;
    uint32_t             a = ki.add_channel("X");
    uint32_t             b = ki.add_channel("Y");
    ki.add_keyframe(a, TypedKeyframe(0.0f, 0.0f));
    ki.add_keyframe(a, TypedKeyframe(5.0f, 10.0f));
    ki.add_keyframe(b, TypedKeyframe(0.0f, 0.0f));
    ki.add_keyframe(b, TypedKeyframe(3.0f, 10.0f));
    EXPECT_FLOAT_EQ(ki.duration(), 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// KeyframeInterpolator — Serialization
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeyframeInterpSerial, RoundTrip)
{
    KeyframeInterpolator ki;
    uint32_t             id = ki.add_channel("Opacity", 1.0f);
    ki.add_keyframe(id, TypedKeyframe(0.0f, 0.0f, InterpMode::Linear));
    ki.add_keyframe(id, TypedKeyframe(1.0f, 1.0f, InterpMode::EaseOut));

    std::string json = ki.serialize();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("Opacity"), std::string::npos);

    KeyframeInterpolator ki2;
    EXPECT_TRUE(ki2.deserialize(json));
    EXPECT_EQ(ki2.channel_count(), 1u);
    EXPECT_EQ(ki2.total_keyframe_count(), 2u);

    // Verify values match
    auto& channels = ki2.channels();
    EXPECT_EQ(channels[0].second.name(), "Opacity");
    EXPECT_FLOAT_EQ(channels[0].second.keyframes()[0].value, 0.0f);
    EXPECT_FLOAT_EQ(channels[0].second.keyframes()[1].value, 1.0f);
}

TEST(KeyframeInterpSerial, EmptyDeserialize)
{
    KeyframeInterpolator ki;
    EXPECT_FALSE(ki.deserialize(""));
    EXPECT_FALSE(ki.deserialize("{}"));
}

TEST(KeyframeInterpSerial, MultiChannel)
{
    KeyframeInterpolator ki;
    ki.add_channel("X");
    ki.add_channel("Y");
    ki.add_channel("Z");

    std::string          json = ki.serialize();
    KeyframeInterpolator ki2;
    EXPECT_TRUE(ki2.deserialize(json));
    EXPECT_EQ(ki2.channel_count(), 3u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// InterpMode / TangentMode names
// ═══════════════════════════════════════════════════════════════════════════════

TEST(InterpNames, AllModes)
{
    EXPECT_STREQ(interp_mode_name(InterpMode::Step), "Step");
    EXPECT_STREQ(interp_mode_name(InterpMode::Linear), "Linear");
    EXPECT_STREQ(interp_mode_name(InterpMode::CubicBezier), "CubicBezier");
    EXPECT_STREQ(interp_mode_name(InterpMode::Spring), "Spring");
    EXPECT_STREQ(interp_mode_name(InterpMode::EaseIn), "EaseIn");
    EXPECT_STREQ(interp_mode_name(InterpMode::EaseOut), "EaseOut");
    EXPECT_STREQ(interp_mode_name(InterpMode::EaseInOut), "EaseInOut");
}

TEST(TangentNames, AllModes)
{
    EXPECT_STREQ(tangent_mode_name(TangentMode::Free), "Free");
    EXPECT_STREQ(tangent_mode_name(TangentMode::Aligned), "Aligned");
    EXPECT_STREQ(tangent_mode_name(TangentMode::Flat), "Flat");
    EXPECT_STREQ(tangent_mode_name(TangentMode::Auto), "Auto");
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationCurveEditor — View Transform
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CurveViewTransform, TimeToX)
{
    CurveViewTransform v;
    v.time_min = 0.0f;
    v.time_max = 10.0f;
    v.width    = 100.0f;
    v.origin_x = 0.0f;

    EXPECT_FLOAT_EQ(v.time_to_x(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(v.time_to_x(5.0f), 50.0f);
    EXPECT_FLOAT_EQ(v.time_to_x(10.0f), 100.0f);
}

TEST(CurveViewTransform, ValueToY)
{
    CurveViewTransform v;
    v.value_min = 0.0f;
    v.value_max = 1.0f;
    v.height    = 200.0f;
    v.origin_y  = 0.0f;

    // Y is inverted: higher values = lower screen Y
    EXPECT_FLOAT_EQ(v.value_to_y(0.0f), 200.0f);
    EXPECT_FLOAT_EQ(v.value_to_y(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(v.value_to_y(0.5f), 100.0f);
}

TEST(CurveViewTransform, RoundTrip)
{
    CurveViewTransform v;
    v.time_min  = 2.0f;
    v.time_max  = 8.0f;
    v.value_min = -1.0f;
    v.value_max = 1.0f;
    v.width     = 300.0f;
    v.height    = 150.0f;
    v.origin_x  = 10.0f;
    v.origin_y  = 20.0f;

    float t   = 5.0f;
    float val = 0.5f;
    float x   = v.time_to_x(t);
    float y   = v.value_to_y(val);
    EXPECT_NEAR(v.x_to_time(x), t, 0.001f);
    EXPECT_NEAR(v.y_to_value(y), val, 0.001f);
}

TEST(CurveViewTransform, Zoom)
{
    CurveViewTransform v;
    v.time_min  = 0.0f;
    v.time_max  = 10.0f;
    v.value_min = 0.0f;
    v.value_max = 1.0f;
    v.width     = 100.0f;
    v.height    = 100.0f;

    v.zoom_time(2.0f, 5.0f);
    EXPECT_FLOAT_EQ(v.time_min, 2.5f);
    EXPECT_FLOAT_EQ(v.time_max, 7.5f);
}

TEST(CurveViewTransform, Pan)
{
    CurveViewTransform v;
    v.time_min  = 0.0f;
    v.time_max  = 10.0f;
    v.value_min = 0.0f;
    v.value_max = 1.0f;
    v.width     = 100.0f;
    v.height    = 100.0f;

    float old_tmin = v.time_min;
    v.pan(10.0f, 0.0f);                // Pan right by 10px
    EXPECT_LT(v.time_min, old_tmin);   // Time shifts left (pan right = see earlier times)
}

TEST(CurveViewTransform, FitToChannel)
{
    CurveViewTransform v;
    AnimationChannel   ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f));
    ch.add_keyframe(TypedKeyframe(3.0f, 15.0f));

    v.fit_to_channel(ch, 0.1f);
    EXPECT_LT(v.time_min, 1.0f);
    EXPECT_GT(v.time_max, 3.0f);
    EXPECT_LT(v.value_min, 5.0f);
    EXPECT_GT(v.value_max, 15.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationCurveEditor — Hit Testing & Selection
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CurveEditor, HitTestNoInterpolator)
{
    AnimationCurveEditor editor;
    auto                 result = editor.hit_test(50.0f, 50.0f);
    EXPECT_EQ(result.type, CurveHitType::Background);
}

TEST(CurveEditor, SelectCount)
{
    AnimationCurveEditor editor;
    EXPECT_EQ(editor.selected_count(), 0u);
}

TEST(CurveEditor, ChannelVisibility)
{
    AnimationCurveEditor editor;
    editor.set_channel_visible(1, false);
    EXPECT_FALSE(editor.is_channel_visible(1));
    editor.set_channel_visible(1, true);
    EXPECT_TRUE(editor.is_channel_visible(1));
}

TEST(CurveEditor, ChannelColor)
{
    AnimationCurveEditor editor;
    editor.set_channel_color(1, colors::red);
    Color c = editor.channel_color(1);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
}

TEST(CurveEditor, ShowAllAfterSolo)
{
    AnimationCurveEditor editor;
    editor.set_channel_visible(1, true);
    editor.set_channel_visible(2, true);
    editor.solo_channel(1);
    EXPECT_TRUE(editor.is_channel_visible(1));
    EXPECT_FALSE(editor.is_channel_visible(2));
    editor.show_all_channels();
    EXPECT_TRUE(editor.is_channel_visible(1));
    EXPECT_TRUE(editor.is_channel_visible(2));
}

TEST(CurveEditor, DisplayOptions)
{
    AnimationCurveEditor editor;
    editor.set_curve_resolution(500);
    EXPECT_EQ(editor.curve_resolution(), 500u);
    editor.set_show_grid(false);
    EXPECT_FALSE(editor.show_grid());
    editor.set_show_tangents(false);
    EXPECT_FALSE(editor.show_tangents());
    editor.set_show_value_labels(true);
    EXPECT_TRUE(editor.show_value_labels());
    editor.set_playhead_time(5.0f);
    EXPECT_FLOAT_EQ(editor.playhead_time(), 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationCurveEditor — Drag
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CurveEditorDrag, NoDragWithoutInterpolator)
{
    AnimationCurveEditor editor;
    editor.begin_drag(50.0f, 50.0f);
    EXPECT_FALSE(editor.is_dragging());
}

TEST(CurveEditorDrag, CancelDrag)
{
    AnimationCurveEditor editor;
    editor.cancel_drag();
    EXPECT_FALSE(editor.is_dragging());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Timeline + Interpolator Integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TimelineInterpInteg, SetInterpolator)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);
    EXPECT_EQ(te.interpolator(), &ki);
}

TEST(TimelineInterpInteg, NullInterpolatorSafe)
{
    TimelineEditor te;
    te.set_interpolator(nullptr);
    te.evaluate_at_playhead();   // Should not crash
}

TEST(TimelineInterpInteg, AddAnimatedTrack)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);

    uint32_t id = te.add_animated_track("Opacity", 1.0f);
    EXPECT_GT(id, 0u);
    EXPECT_EQ(te.track_count(), 1u);
    EXPECT_EQ(ki.channel_count(), 1u);
}

TEST(TimelineInterpInteg, AddAnimatedKeyframe)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);

    uint32_t id = te.add_animated_track("X");
    te.add_animated_keyframe(id, 0.0f, 0.0f);
    te.add_animated_keyframe(id, 1.0f, 10.0f);

    EXPECT_EQ(te.total_keyframe_count(), 2u);
    EXPECT_EQ(ki.total_keyframe_count(), 2u);
}

TEST(TimelineInterpInteg, AdvanceEvaluates)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);
    te.set_duration(2.0f);

    uint32_t id = te.add_animated_track("X");
    te.add_animated_keyframe(id, 0.0f, 0.0f);
    te.add_animated_keyframe(id, 2.0f, 100.0f);

    float target = -1.0f;
    // The channel ID from add_channel is 1 (first channel)
    ki.bind(1, "x", &target);

    te.play();
    te.advance(1.0f);   // Advance to t=1.0

    // At t=1.0, linear interp from 0 to 100 over 2s = 50
    EXPECT_NEAR(target, 50.0f, 1.0f);
}

TEST(TimelineInterpInteg, Serialize)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);
    te.set_duration(5.0f);
    te.set_fps(30.0f);

    uint32_t id = te.add_animated_track("Opacity");
    te.add_animated_keyframe(id, 0.0f, 0.0f);
    te.add_animated_keyframe(id, 5.0f, 1.0f);

    std::string json = te.serialize();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"duration\":5"), std::string::npos);
    EXPECT_NE(json.find("\"interpolator\""), std::string::npos);
}

TEST(TimelineInterpInteg, Deserialize)
{
    TimelineEditor       te;
    KeyframeInterpolator ki;
    te.set_interpolator(&ki);
    te.set_duration(5.0f);

    uint32_t id = te.add_animated_track("X");
    te.add_animated_keyframe(id, 0.0f, 0.0f);
    te.add_animated_keyframe(id, 5.0f, 10.0f);

    std::string json = te.serialize();

    TimelineEditor       te2;
    KeyframeInterpolator ki2;
    te2.set_interpolator(&ki2);
    EXPECT_TRUE(te2.deserialize(json));
    EXPECT_FLOAT_EQ(te2.duration(), 5.0f);
    EXPECT_EQ(ki2.channel_count(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Multi-Pane Recording
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MultiPaneRecording, BeginWithNullCallback)
{
    RecordingSession rs;
    RecordingConfig  config;
    config.output_path = "/tmp/spectra_test_multipane";
    config.start_time  = 0.0f;
    config.end_time    = 1.0f;
    config.pane_count  = 2;
    EXPECT_FALSE(rs.begin_multi_pane(config, nullptr));
    EXPECT_EQ(rs.state(), RecordingState::Failed);
}

TEST(MultiPaneRecording, AutoGridLayout2Panes)
{
    RecordingSession rs;
    RecordingConfig  config;
    config.output_path = "/tmp/spectra_test_multipane_grid";
    config.start_time  = 0.0f;
    config.end_time    = 0.5f;
    config.fps         = 10.0f;
    config.width       = 100;
    config.height      = 100;
    config.pane_count  = 2;

    std::vector<uint32_t> rendered_panes;
    auto                  pane_cb = [&](uint32_t pane_idx,
                       uint32_t /*frame*/,
                       float /*time*/,
                       uint8_t* rgba,
                       uint32_t w,
                       uint32_t h) -> bool
    {
        rendered_panes.push_back(pane_idx);
        // Fill with pane index as color
        size_t pixels = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < pixels; ++i)
        {
            rgba[i * 4 + 0] = static_cast<uint8_t>(pane_idx * 100);
            rgba[i * 4 + 1] = 0;
            rgba[i * 4 + 2] = 0;
            rgba[i * 4 + 3] = 255;
        }
        return true;
    };

    EXPECT_TRUE(rs.begin_multi_pane(config, pane_cb));
    EXPECT_TRUE(rs.advance());   // First frame
    // Should have rendered 2 panes for the first frame
    EXPECT_GE(rendered_panes.size(), 2u);
    EXPECT_EQ(rendered_panes[0], 0u);
    EXPECT_EQ(rendered_panes[1], 1u);

    rs.cancel();
}

TEST(MultiPaneRecording, CustomPaneRects)
{
    RecordingSession rs;
    RecordingConfig  config;
    config.output_path = "/tmp/spectra_test_multipane_custom";
    config.start_time  = 0.0f;
    config.end_time    = 0.1f;
    config.fps         = 10.0f;
    config.width       = 200;
    config.height      = 100;
    config.pane_count  = 2;
    config.pane_rects  = {
        {0.0f, 0.0f, 0.5f, 1.0f},   // Left half
        {0.5f, 0.0f, 0.5f, 1.0f},   // Right half
    };

    uint32_t pane_call_count = 0;
    auto     pane_cb         = [&](uint32_t /*pane_idx*/,
                       uint32_t /*frame*/,
                       float /*time*/,
                       uint8_t* rgba,
                       uint32_t w,
                       uint32_t h) -> bool
    {
        pane_call_count++;
        std::memset(rgba, 128, static_cast<size_t>(w) * h * 4);
        return true;
    };

    EXPECT_TRUE(rs.begin_multi_pane(config, pane_cb));
    rs.advance();
    EXPECT_EQ(pane_call_count, 2u);
    rs.cancel();
}

TEST(MultiPaneRecording, SinglePaneFallback)
{
    RecordingSession rs;
    RecordingConfig  config;
    config.output_path = "/tmp/spectra_test_multipane_single";
    config.start_time  = 0.0f;
    config.end_time    = 0.1f;
    config.fps         = 10.0f;
    config.width       = 100;
    config.height      = 100;
    config.pane_count  = 1;

    uint32_t calls   = 0;
    auto     pane_cb = [&](uint32_t pane_idx,
                       uint32_t /*frame*/,
                       float /*time*/,
                       uint8_t* rgba,
                       uint32_t w,
                       uint32_t h) -> bool
    {
        EXPECT_EQ(pane_idx, 0u);
        calls++;
        std::memset(rgba, 200, static_cast<size_t>(w) * h * 4);
        return true;
    };

    EXPECT_TRUE(rs.begin_multi_pane(config, pane_cb));
    rs.advance();
    EXPECT_EQ(calls, 1u);
    rs.cancel();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST(EdgeCases, RemoveChannelRemovesBindings)
{
    KeyframeInterpolator ki;
    uint32_t             id     = ki.add_channel("X");
    float                target = 0.0f;
    ki.bind(id, "x", &target);
    EXPECT_EQ(ki.bindings().size(), 1u);
    ki.remove_channel(id);
    EXPECT_TRUE(ki.bindings().empty());
}

TEST(EdgeCases, SingleKeyframeEvaluation)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(5.0f, 42.0f));
    EXPECT_FLOAT_EQ(ch.evaluate(0.0f), 42.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(5.0f), 42.0f);
    EXPECT_FLOAT_EQ(ch.evaluate(10.0f), 42.0f);
}

TEST(EdgeCases, ZeroDurationSegment)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(1.0f, 5.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));   // Same time — updates
    EXPECT_EQ(ch.keyframe_count(), 1u);
    EXPECT_FLOAT_EQ(ch.evaluate(1.0f), 10.0f);
}

TEST(EdgeCases, NegativeValues)
{
    AnimationChannel ch("X");
    ch.add_keyframe(TypedKeyframe(0.0f, -10.0f, InterpMode::Linear));
    ch.add_keyframe(TypedKeyframe(1.0f, 10.0f));
    EXPECT_FLOAT_EQ(ch.evaluate(0.5f), 0.0f);
}

TEST(EdgeCases, LargeKeyframeCount)
{
    AnimationChannel ch("X");
    for (int i = 0; i < 1000; ++i)
    {
        ch.add_keyframe(TypedKeyframe(static_cast<float>(i), static_cast<float>(i * 2)));
    }
    EXPECT_EQ(ch.keyframe_count(), 1000u);
    EXPECT_FLOAT_EQ(ch.evaluate(500.0f), 1000.0f);
}
