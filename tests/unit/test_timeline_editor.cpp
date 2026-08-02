#include <gtest/gtest.h>

#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/animation/timeline_editor.hpp"

using namespace spectra;

// ─── Construction ────────────────────────────────────────────────────────────

TEST(TimelineEditorConstruction, DefaultState)
{
    TimelineEditor te;
    EXPECT_EQ(te.playback_state(), PlaybackState::Stopped);
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
    EXPECT_FLOAT_EQ(te.duration(), 10.0f);
    EXPECT_FLOAT_EQ(te.fps(), 60.0f);
    EXPECT_EQ(te.loop_mode(), LoopMode::None);
    EXPECT_EQ(te.snap_mode(), SnapMode::Frame);
    EXPECT_EQ(te.track_count(), 0u);
    EXPECT_EQ(te.total_keyframe_count(), 0u);
    EXPECT_EQ(te.selected_count(), 0u);
}

TEST(TimelineEditorConstruction, DefaultViewRange)
{
    TimelineEditor te;
    EXPECT_FLOAT_EQ(te.view_start(), 0.0f);
    EXPECT_FLOAT_EQ(te.view_end(), 10.0f);
    EXPECT_FLOAT_EQ(te.zoom(), 100.0f);
}

// ─── Playback ────────────────────────────────────────────────────────────────

TEST(TimelineEditorPlayback, PlayPauseStop)
{
    TimelineEditor te;

    te.play();
    EXPECT_EQ(te.playback_state(), PlaybackState::Playing);
    EXPECT_TRUE(te.is_playing());

    te.pause();
    EXPECT_EQ(te.playback_state(), PlaybackState::Paused);
    EXPECT_FALSE(te.is_playing());

    te.play();
    EXPECT_TRUE(te.is_playing());

    te.stop();
    EXPECT_EQ(te.playback_state(), PlaybackState::Stopped);
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
}

TEST(TimelineEditorPlayback, TogglePlay)
{
    TimelineEditor te;

    te.toggle_play();
    EXPECT_TRUE(te.is_playing());

    te.toggle_play();
    EXPECT_EQ(te.playback_state(), PlaybackState::Paused);

    te.toggle_play();
    EXPECT_TRUE(te.is_playing());
}

TEST(TimelineEditorPlayback, PlayResetsFromStopped)
{
    TimelineEditor te;
    te.set_playhead(5.0f);
    te.play();
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
}

TEST(TimelineEditorPlayback, PausePreservesPlayhead)
{
    TimelineEditor te;
    te.play();
    te.advance(2.5f);
    te.pause();
    EXPECT_NEAR(te.playhead(), 2.5f, 0.01f);
}

TEST(TimelineEditorPlayback, PlaybackCallback)
{
    TimelineEditor te;
    int            call_count = 0;
    PlaybackState  last_state = PlaybackState::Stopped;

    te.set_on_playback_change(
        [&](PlaybackState s)
        {
            call_count++;
            last_state = s;
        });

    te.play();
    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(last_state, PlaybackState::Playing);

    te.pause();
    EXPECT_EQ(call_count, 2);
    EXPECT_EQ(last_state, PlaybackState::Paused);

    te.stop();
    EXPECT_EQ(call_count, 3);
    EXPECT_EQ(last_state, PlaybackState::Stopped);
}

// ─── Playhead & Advance ─────────────────────────────────────────────────────

TEST(TimelineEditorPlayhead, SetAndClamp)
{
    TimelineEditor te;
    te.set_duration(5.0f);

    te.set_playhead(3.0f);
    EXPECT_FLOAT_EQ(te.playhead(), 3.0f);

    te.set_playhead(100.0f);
    EXPECT_FLOAT_EQ(te.playhead(), 5.0f);

    te.set_playhead(-10.0f);
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
}

TEST(TimelineEditorPlayhead, AdvanceNoLoop)
{
    TimelineEditor te;
    te.set_duration(2.0f);
    te.play();

    bool active = te.advance(1.0f);
    EXPECT_TRUE(active);
    EXPECT_NEAR(te.playhead(), 1.0f, 0.01f);

    active = te.advance(1.5f);
    EXPECT_FALSE(active);
    EXPECT_FLOAT_EQ(te.playhead(), 2.0f);
    EXPECT_EQ(te.playback_state(), PlaybackState::Stopped);
}

TEST(TimelineEditorPlayhead, AdvanceLoop)
{
    TimelineEditor te;
    te.set_duration(2.0f);
    te.set_loop_mode(LoopMode::Loop);
    te.play();

    te.advance(1.5f);
    EXPECT_NEAR(te.playhead(), 1.5f, 0.01f);

    bool active = te.advance(1.0f);
    EXPECT_TRUE(active);
    // Should wrap: 1.5 + 1.0 = 2.5 -> 0.5
    EXPECT_NEAR(te.playhead(), 0.5f, 0.01f);
    EXPECT_TRUE(te.is_playing());
}

TEST(TimelineEditorPlayhead, AdvancePingPong)
{
    TimelineEditor te;
    te.set_duration(2.0f);
    te.set_loop_mode(LoopMode::PingPong);
    te.play();

    te.advance(1.8f);
    EXPECT_NEAR(te.playhead(), 1.8f, 0.01f);

    // Overshoot past end -> bounce back
    te.advance(0.5f);
    // 1.8 + 0.5 = 2.3 -> 2.0 - 0.3 = 1.7
    EXPECT_NEAR(te.playhead(), 1.7f, 0.01f);
    EXPECT_TRUE(te.is_playing());
}

TEST(TimelineEditorPlayhead, AdvanceWhileStopped)
{
    TimelineEditor te;
    bool           active = te.advance(1.0f);
    EXPECT_FALSE(active);
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
}

TEST(TimelineEditorPlayhead, Scrub)
{
    TimelineEditor te;
    float          scrubbed_time = -1.0f;
    te.set_on_scrub([&](float t) { scrubbed_time = t; });

    te.scrub_to(3.5f);
    EXPECT_FLOAT_EQ(te.playhead(), 3.5f);
    EXPECT_FLOAT_EQ(scrubbed_time, 3.5f);
}

TEST(TimelineEditorPlayhead, StepForwardBackward)
{
    TimelineEditor te;
    te.set_fps(30.0f);

    te.step_forward();
    EXPECT_NEAR(te.playhead(), 1.0f / 30.0f, 0.001f);

    te.step_forward();
    EXPECT_NEAR(te.playhead(), 2.0f / 30.0f, 0.001f);

    te.step_backward();
    EXPECT_NEAR(te.playhead(), 1.0f / 30.0f, 0.001f);
}

// ─── Duration & FPS ──────────────────────────────────────────────────────────

TEST(TimelineEditorDuration, SetDuration)
{
    TimelineEditor te;
    te.set_duration(5.0f);
    EXPECT_FLOAT_EQ(te.duration(), 5.0f);
}

TEST(TimelineEditorDuration, DurationClampsPlayhead)
{
    TimelineEditor te;
    te.set_playhead(8.0f);
    te.set_duration(3.0f);
    EXPECT_FLOAT_EQ(te.playhead(), 3.0f);
}

TEST(TimelineEditorDuration, NegativeDuration)
{
    TimelineEditor te;
    te.set_duration(-5.0f);
    EXPECT_FLOAT_EQ(te.duration(), 0.0f);
}

TEST(TimelineEditorFPS, SetFPS)
{
    TimelineEditor te;
    te.set_fps(30.0f);
    EXPECT_FLOAT_EQ(te.fps(), 30.0f);
}

TEST(TimelineEditorFPS, MinFPS)
{
    TimelineEditor te;
    te.set_fps(0.5f);
    EXPECT_FLOAT_EQ(te.fps(), 1.0f);
}

TEST(TimelineEditorFPS, FrameCount)
{
    TimelineEditor te;
    te.set_duration(2.0f);
    te.set_fps(30.0f);
    EXPECT_EQ(te.frame_count(), 60u);
}

TEST(TimelineEditorFPS, CurrentFrame)
{
    TimelineEditor te;
    te.set_fps(10.0f);
    te.set_playhead(1.5f);
    EXPECT_EQ(te.current_frame(), 15u);
}

TEST(TimelineEditorFPS, FrameTimeConversion)
{
    TimelineEditor te;
    te.set_fps(30.0f);
    EXPECT_NEAR(te.frame_to_time(30), 1.0f, 0.001f);
    EXPECT_EQ(te.time_to_frame(1.0f), 30u);
}

// ─── Loop ────────────────────────────────────────────────────────────────────

TEST(TimelineEditorLoop, LoopRegion)
{
    TimelineEditor te;
    te.set_duration(10.0f);
    te.set_loop_region(2.0f, 6.0f);

    EXPECT_FLOAT_EQ(te.loop_in(), 2.0f);
    EXPECT_FLOAT_EQ(te.loop_out(), 6.0f);
}

TEST(TimelineEditorLoop, ClearLoopRegion)
{
    TimelineEditor te;
    te.set_loop_region(2.0f, 6.0f);
    te.clear_loop_region();

    EXPECT_FLOAT_EQ(te.loop_in(), 0.0f);
    EXPECT_FLOAT_EQ(te.loop_out(), 10.0f);   // Falls back to duration
}

TEST(TimelineEditorLoop, LoopRegionAdvance)
{
    TimelineEditor te;
    te.set_duration(10.0f);
    te.set_loop_mode(LoopMode::Loop);
    te.set_loop_region(2.0f, 4.0f);
    te.play();

    te.advance(3.5f);
    // 0 + 3.5 = 3.5 (within [2, 4])
    EXPECT_NEAR(te.playhead(), 3.5f, 0.01f);

    te.advance(1.0f);
    // 3.5 + 1.0 = 4.5 -> wraps to 2.0 + 0.5 = 2.5
    EXPECT_NEAR(te.playhead(), 2.5f, 0.01f);
}

// ─── Snap ────────────────────────────────────────────────────────────────────

TEST(TimelineEditorSnap, FrameSnap)
{
    TimelineEditor te;
    te.set_fps(10.0f);
    te.set_snap_mode(SnapMode::Frame);

    EXPECT_NEAR(te.snap_time(0.34f), 0.3f, 0.001f);
    EXPECT_NEAR(te.snap_time(0.36f), 0.4f, 0.001f);
}

TEST(TimelineEditorSnap, BeatSnap)
{
    TimelineEditor te;
    te.set_snap_mode(SnapMode::Beat);
    te.set_snap_interval(0.25f);

    EXPECT_NEAR(te.snap_time(0.37f), 0.25f, 0.001f);
    EXPECT_NEAR(te.snap_time(0.63f), 0.75f, 0.001f);
}

TEST(TimelineEditorSnap, NoSnap)
{
    TimelineEditor te;
    te.set_snap_mode(SnapMode::None);
    EXPECT_FLOAT_EQ(te.snap_time(0.37f), 0.37f);
}

// ─── Tracks ──────────────────────────────────────────────────────────────────

TEST(TimelineEditorTracks, AddRemove)
{
    TimelineEditor te;
    uint32_t       id1 = te.add_track("Position X");
    uint32_t       id2 = te.add_track("Opacity", colors::red);

    EXPECT_EQ(te.track_count(), 2u);
    EXPECT_NE(id1, id2);

    auto* t1 = te.get_track(id1);
    ASSERT_NE(t1, nullptr);
    EXPECT_EQ(t1->name, "Position X");

    te.remove_track(id1);
    EXPECT_EQ(te.track_count(), 1u);
    EXPECT_EQ(te.get_track(id1), nullptr);
}

TEST(TimelineEditorTracks, Rename)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Old Name");
    te.rename_track(id, "New Name");

    auto* t = te.get_track(id);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->name, "New Name");
}

TEST(TimelineEditorTracks, VisibleAndLocked)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.set_track_visible(id, false);
    EXPECT_FALSE(te.get_track(id)->visible);

    te.set_track_locked(id, true);
    EXPECT_TRUE(te.get_track(id)->locked);
}

TEST(TimelineEditorTracks, RemoveNonexistent)
{
    TimelineEditor te;
    te.add_track("Track");
    te.remove_track(999);   // Should not crash
    EXPECT_EQ(te.track_count(), 1u);
}

TEST(TimelineEditorTracks, GetTrackConst)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    const auto& cte = te;
    const auto* t   = cte.get_track(id);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->name, "Track");

    EXPECT_EQ(cte.get_track(999), nullptr);
}

TEST(TimelineEditorSerialization, RestoresTracksKeyframesNamesAndViewState)
{
    TimelineEditor source;
    source.set_duration(12.0f);
    source.set_fps(24.0f);
    source.set_playhead(3.5f);
    source.set_loop_mode(LoopMode::PingPong);
    source.set_snap_mode(SnapMode::Beat);
    source.set_snap_interval(0.25f);
    source.set_view_range(2.0f, 8.0f);
    source.set_zoom(180.0f);
    const auto first  = source.add_track("Position \"X\"");
    const auto second = source.add_track("Opacity");
    ASSERT_TRUE(source.set_track_property_path(second, "axes/0/series/1/opacity"));
    source.add_keyframe(first, 4.0f);
    source.add_keyframe(first, 1.0f);
    source.add_keyframe(second, 2.5f);
    source.set_track_visible(second, false);
    source.set_track_locked(second, true);

    TimelineEditor restored;
    ASSERT_TRUE(restored.deserialize(source.serialize()));
    EXPECT_FLOAT_EQ(restored.duration(), 12.0f);
    EXPECT_FLOAT_EQ(restored.fps(), 24.0f);
    EXPECT_FLOAT_EQ(restored.playhead(), 3.5f);
    EXPECT_EQ(restored.loop_mode(), LoopMode::PingPong);
    EXPECT_EQ(restored.snap_mode(), SnapMode::Beat);
    EXPECT_FLOAT_EQ(restored.snap_interval(), 0.25f);
    EXPECT_FLOAT_EQ(restored.view_start(), 2.0f);
    EXPECT_FLOAT_EQ(restored.view_end(), 8.0f);
    EXPECT_FLOAT_EQ(restored.zoom(), 180.0f);
    ASSERT_EQ(restored.track_count(), 2u);
    ASSERT_NE(restored.get_track(first), nullptr);
    EXPECT_EQ(restored.get_track(first)->name, "Position \"X\"");
    ASSERT_EQ(restored.get_track(first)->keyframes.size(), 2u);
    EXPECT_FLOAT_EQ(restored.get_track(first)->keyframes[0].time, 1.0f);
    EXPECT_FLOAT_EQ(restored.get_track(first)->keyframes[1].time, 4.0f);
    ASSERT_NE(restored.get_track(second), nullptr);
    EXPECT_FALSE(restored.get_track(second)->visible);
    EXPECT_TRUE(restored.get_track(second)->locked);
    EXPECT_EQ(restored.get_track(second)->property_path, "axes/0/series/1/opacity");
    EXPECT_EQ(restored.total_keyframe_count(), 3u);
}

// ─── Keyframes ───────────────────────────────────────────────────────────────

TEST(TimelineEditorSerialization, RestoresTypedKeyframeTangents)
{
    TimelineEditor       source;
    KeyframeInterpolator interpolator;
    source.set_interpolator(&interpolator);
    const auto track = source.add_animated_track("Opacity", 0.0f);
    source.add_animated_keyframe(track, 1.5f, 0.25f, static_cast<int>(InterpMode::CubicBezier));
    ASSERT_TRUE(source.set_animated_keyframe_tangents(track,
                                                      1.5f,
                                                      static_cast<int>(TangentMode::Aligned),
                                                      -0.2f,
                                                      -1.0f,
                                                      0.3f,
                                                      2.0f));

    TimelineEditor       restored;
    KeyframeInterpolator restored_interpolator;
    restored.set_interpolator(&restored_interpolator);
    ASSERT_TRUE(restored.deserialize(source.serialize()));
    const auto* keyframe = restored_interpolator.channel(track)->find_keyframe(1.5f);
    ASSERT_NE(keyframe, nullptr);
    EXPECT_EQ(keyframe->tangent_mode, TangentMode::Aligned);
    EXPECT_FLOAT_EQ(keyframe->in_tangent.dt, -0.2f);
    EXPECT_FLOAT_EQ(keyframe->in_tangent.dv, -1.0f);
    EXPECT_FLOAT_EQ(keyframe->out_tangent.dt, 0.3f);
    EXPECT_FLOAT_EQ(keyframe->out_tangent.dv, 2.0f);

    ASSERT_TRUE(restored.set_animated_keyframe_tangents(track,
                                                        1.5f,
                                                        static_cast<int>(TangentMode::Flat),
                                                        -9.0f,
                                                        -9.0f,
                                                        9.0f,
                                                        9.0f));
    keyframe = restored_interpolator.channel(track)->find_keyframe(1.5f);
    ASSERT_NE(keyframe, nullptr);
    EXPECT_EQ(keyframe->tangent_mode, TangentMode::Flat);
    EXPECT_FLOAT_EQ(keyframe->in_tangent.dv, 0.0f);
    EXPECT_FLOAT_EQ(keyframe->out_tangent.dv, 0.0f);
}

TEST(TimelineEditorKeyframes, AddAndCount)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 0.0f);
    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);

    EXPECT_EQ(te.total_keyframe_count(), 3u);
}

TEST(TimelineEditorKeyframes, AddSorted)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 2.0f);
    te.add_keyframe(id, 0.5f);
    te.add_keyframe(id, 1.0f);

    auto* t = te.get_track(id);
    ASSERT_EQ(t->keyframes.size(), 3u);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 0.5f);
    EXPECT_FLOAT_EQ(t->keyframes[1].time, 1.0f);
    EXPECT_FLOAT_EQ(t->keyframes[2].time, 2.0f);
}

TEST(TimelineEditorKeyframes, NoDuplicates)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 1.0f);   // Duplicate

    EXPECT_EQ(te.total_keyframe_count(), 1u);
}

TEST(TimelineEditorKeyframes, Remove)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);
    te.remove_keyframe(id, 1.0f);

    EXPECT_EQ(te.total_keyframe_count(), 1u);
    auto* t = te.get_track(id);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 2.0f);
}

TEST(TimelineEditorKeyframes, MoveKeyframe)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 1.0f);
    te.move_keyframe(id, 1.0f, 3.0f);

    auto* t = te.get_track(id);
    ASSERT_EQ(t->keyframes.size(), 1u);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 3.0f);
}

TEST(TimelineEditorKeyframes, MoveClampsToDuration)
{
    TimelineEditor te;
    te.set_duration(5.0f);
    uint32_t id = te.add_track("Track");

    te.add_keyframe(id, 1.0f);
    te.move_keyframe(id, 1.0f, 100.0f);

    auto* t = te.get_track(id);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 5.0f);
}

TEST(TimelineEditorKeyframes, ClearKeyframes)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    te.add_keyframe(id, 0.0f);
    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);
    te.clear_keyframes(id);

    EXPECT_EQ(te.total_keyframe_count(), 0u);
}

TEST(TimelineEditorKeyframes, LockedTrackRejectsAdd)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.set_track_locked(id, true);

    te.add_keyframe(id, 1.0f);
    EXPECT_EQ(te.total_keyframe_count(), 0u);
}

TEST(TimelineEditorKeyframes, LockedTrackRejectsRemove)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.set_track_locked(id, true);

    te.remove_keyframe(id, 1.0f);
    EXPECT_EQ(te.total_keyframe_count(), 1u);
}

TEST(TimelineEditorKeyframes, LockedTrackRejectsMove)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.set_track_locked(id, true);

    te.move_keyframe(id, 1.0f, 3.0f);
    auto* t = te.get_track(id);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 1.0f);
}

TEST(TimelineEditorKeyframes, AddCallback)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");

    uint32_t cb_track = 0;
    float    cb_time  = -1.0f;
    te.set_on_keyframe_added(
        [&](uint32_t tid, float t)
        {
            cb_track = tid;
            cb_time  = t;
        });

    te.add_keyframe(id, 2.5f);
    EXPECT_EQ(cb_track, id);
    EXPECT_FLOAT_EQ(cb_time, 2.5f);
}

TEST(TimelineEditorKeyframes, RemoveCallback)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);

    uint32_t cb_track = 0;
    float    cb_time  = -1.0f;
    te.set_on_keyframe_removed(
        [&](uint32_t tid, float t)
        {
            cb_track = tid;
            cb_time  = t;
        });

    te.remove_keyframe(id, 1.0f);
    EXPECT_EQ(cb_track, id);
    EXPECT_FLOAT_EQ(cb_time, 1.0f);
}

TEST(TimelineEditorKeyframes, AddToNonexistentTrack)
{
    TimelineEditor te;
    te.add_keyframe(999, 1.0f);   // Should not crash
    EXPECT_EQ(te.total_keyframe_count(), 0u);
}

TEST(TimelineEditorKeyframes, MultipleTracksIndependent)
{
    TimelineEditor te;
    uint32_t       id1 = te.add_track("Track 1");
    uint32_t       id2 = te.add_track("Track 2");

    te.add_keyframe(id1, 1.0f);
    te.add_keyframe(id1, 2.0f);
    te.add_keyframe(id2, 3.0f);

    EXPECT_EQ(te.total_keyframe_count(), 3u);
    EXPECT_EQ(te.get_track(id1)->keyframes.size(), 2u);
    EXPECT_EQ(te.get_track(id2)->keyframes.size(), 1u);
}

// ─── Selection ───────────────────────────────────────────────────────────────

TEST(TimelineEditorSelection, SelectDeselect)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);

    te.select_keyframe(id, 1.0f);
    EXPECT_EQ(te.selected_count(), 1u);

    te.select_keyframe(id, 2.0f);
    EXPECT_EQ(te.selected_count(), 2u);

    te.deselect_keyframe(id, 1.0f);
    EXPECT_EQ(te.selected_count(), 1u);
}

TEST(TimelineEditorSelection, SelectAll)
{
    TimelineEditor te;
    uint32_t       id1 = te.add_track("Track 1");
    uint32_t       id2 = te.add_track("Track 2");
    te.add_keyframe(id1, 1.0f);
    te.add_keyframe(id2, 2.0f);

    te.select_all_keyframes();
    EXPECT_EQ(te.selected_count(), 2u);
}

TEST(TimelineEditorSelection, DeselectAll)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.select_keyframe(id, 1.0f);

    te.deselect_all();
    EXPECT_EQ(te.selected_count(), 0u);
}

TEST(TimelineEditorSelection, SelectRange)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 0.5f);
    te.add_keyframe(id, 1.5f);
    te.add_keyframe(id, 2.5f);
    te.add_keyframe(id, 3.5f);

    te.select_keyframes_in_range(1.0f, 3.0f);
    EXPECT_EQ(te.selected_count(), 2u);   // 1.5 and 2.5
}

TEST(TimelineEditorSelection, DeleteSelected)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);
    te.add_keyframe(id, 3.0f);

    te.select_keyframe(id, 1.0f);
    te.select_keyframe(id, 3.0f);
    te.delete_selected();

    EXPECT_EQ(te.total_keyframe_count(), 1u);
    auto* t = te.get_track(id);
    EXPECT_FLOAT_EQ(t->keyframes[0].time, 2.0f);
}

TEST(TimelineEditorSelection, DeleteSelectedRespectsLock)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.select_keyframe(id, 1.0f);
    te.set_track_locked(id, true);

    te.delete_selected();
    EXPECT_EQ(te.total_keyframe_count(), 1u);
}

TEST(TimelineEditorSelection, SelectionCallback)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);

    int call_count = 0;
    te.set_on_selection_change([&](const std::vector<KeyframeMarker*>& /*sel*/) { call_count++; });

    te.select_keyframe(id, 1.0f);
    EXPECT_EQ(call_count, 1);

    te.deselect_all();
    EXPECT_EQ(call_count, 2);
}

TEST(TimelineEditorSelection, SelectedKeyframesPointers)
{
    TimelineEditor te;
    uint32_t       id = te.add_track("Track");
    te.add_keyframe(id, 1.0f);
    te.add_keyframe(id, 2.0f);
    te.select_keyframe(id, 2.0f);

    auto sel = te.selected_keyframes();
    ASSERT_EQ(sel.size(), 1u);
    EXPECT_FLOAT_EQ(sel[0]->time, 2.0f);
}

// ─── Zoom & Scroll ───────────────────────────────────────────────────────────

TEST(TimelineEditorView, SetViewRange)
{
    TimelineEditor te;
    te.set_view_range(2.0f, 8.0f);
    EXPECT_FLOAT_EQ(te.view_start(), 2.0f);
    EXPECT_FLOAT_EQ(te.view_end(), 8.0f);
}

TEST(TimelineEditorView, ViewRangeClamp)
{
    TimelineEditor te;
    te.set_view_range(-5.0f, 3.0f);
    EXPECT_FLOAT_EQ(te.view_start(), 0.0f);
}

TEST(TimelineEditorView, SetZoom)
{
    TimelineEditor te;
    te.set_zoom(200.0f);
    EXPECT_FLOAT_EQ(te.zoom(), 200.0f);
}

TEST(TimelineEditorView, ZoomClamp)
{
    TimelineEditor te;
    te.set_zoom(5.0f);
    EXPECT_FLOAT_EQ(te.zoom(), 10.0f);

    te.set_zoom(50000.0f);
    EXPECT_FLOAT_EQ(te.zoom(), 10000.0f);
}

TEST(TimelineEditorView, ZoomInOut)
{
    TimelineEditor te;
    float          initial = te.zoom();

    te.zoom_in();
    EXPECT_GT(te.zoom(), initial);

    float after_in = te.zoom();
    te.zoom_out();
    EXPECT_LT(te.zoom(), after_in);
}

TEST(TimelineEditorView, ScrollToPlayhead)
{
    TimelineEditor te;
    te.set_duration(20.0f);
    te.set_playhead(15.0f);
    te.set_view_range(0.0f, 5.0f);

    te.scroll_to_playhead();
    // Playhead should be roughly centered in view
    float mid = (te.view_start() + te.view_end()) / 2.0f;
    EXPECT_NEAR(mid, 15.0f, 0.1f);
}

// ─── Edge Cases ──────────────────────────────────────────────────────────────

TEST(TimelineEditorEdgeCases, ZeroDuration)
{
    TimelineEditor te;
    te.set_duration(0.0f);
    EXPECT_FLOAT_EQ(te.duration(), 0.0f);
    EXPECT_FLOAT_EQ(te.playhead(), 0.0f);
}

TEST(TimelineEditorEdgeCases, EmptyTracksOperations)
{
    TimelineEditor te;
    // These should all be no-ops, not crash
    te.select_all_keyframes();
    te.deselect_all();
    te.delete_selected();
    te.select_keyframes_in_range(0.0f, 10.0f);
    EXPECT_EQ(te.selected_count(), 0u);
}

TEST(TimelineEditorEdgeCases, RapidPlayPause)
{
    TimelineEditor te;
    for (int i = 0; i < 100; ++i)
    {
        te.toggle_play();
    }
    // Should not crash; even number of toggles = paused
    EXPECT_EQ(te.playback_state(), PlaybackState::Paused);
}

TEST(TimelineEditorEdgeCases, ManyTracks)
{
    TimelineEditor te;
    for (int i = 0; i < 50; ++i)
    {
        uint32_t id = te.add_track("Track " + std::to_string(i));
        te.add_keyframe(id, static_cast<float>(i) * 0.1f);
    }
    EXPECT_EQ(te.track_count(), 50u);
    EXPECT_EQ(te.total_keyframe_count(), 50u);
}

TEST(TimelineEditorEdgeCases, PingPongBounceAtStart)
{
    TimelineEditor te;
    te.set_duration(2.0f);
    te.set_loop_mode(LoopMode::PingPong);
    te.play();

    // Advance to near end, bounce back, then bounce at start
    te.advance(1.9f);
    te.advance(0.3f);   // 1.9+0.3=2.2 -> bounce to 1.8
    EXPECT_NEAR(te.playhead(), 1.8f, 0.05f);

    // Continue backward
    te.advance(2.0f);   // 1.8 - 2.0 = -0.2 -> bounce to 0.2
    EXPECT_NEAR(te.playhead(), 0.2f, 0.05f);
    EXPECT_TRUE(te.is_playing());
}
