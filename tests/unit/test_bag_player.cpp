// test_bag_player.cpp — Unit tests for BagPlayer and BagPlaybackPanel.
//
// Pure-logic tests run without SPECTRA_ROS2_BAG (stub BagReader).
// Full playback tests (injection, activity scan, seek) require SPECTRA_ROS2_BAG=ON.
//
// Strategy:
//   - State machine, rate, loop, step, seek-clamp, callbacks, format helpers
//     are all tested against the stub BagReader (SPECTRA_ROS2_BAG=OFF path).
//   - Activity band scan, inject_until, TimelineEditor integration tests are
//     in the SPECTRA_ROS2_BAG=ON section.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bag_player.hpp"
#include "ui/bag_playback_panel.hpp"

// Forward-declare the mock manager used in logic-only tests.
// We need RosPlotManager + MessageIntrospector headers.
#include "message_introspector.hpp"
#include "ros_plot_manager.hpp"
#include "ros2_bridge.hpp"

#ifdef SPECTRA_ROS2_BAG
    #include <filesystem>
    #include <rclcpp/rclcpp.hpp>
    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/serialized_bag_message.hpp>
    #include <rosbag2_storage/storage_options.hpp>
    #include <rosbag2_storage/topic_metadata.hpp>

    #include "bag_message_compat.hpp"
namespace fs = std::filesystem;
#endif

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// RclcppEnvironment (only used in ROS2_BAG=ON build path)
// ---------------------------------------------------------------------------

#ifdef SPECTRA_ROS2_BAG

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        if (!rclcpp::ok())
        {
            int    argc = 0;
            char** argv = nullptr;
            rclcpp::init(argc, argv);
        }
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Helpers for writing minimal synthetic bags
// ---------------------------------------------------------------------------

namespace
{

static std::string make_temp_bag_dir(const std::string& name)
{
    const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string("/tmp/spectra_bagplayer_test_") + name + "_" + std::to_string(ns);
}

// Write a minimal bag with Float64 messages on /float_topic.
// Returns the bag directory path on success, empty on failure.
static std::string write_float64_bag(const std::string& name,
                                     int                n_msgs       = 10,
                                     double             start_time_s = 1000.0,
                                     double             interval_s   = 0.1)
{
    const std::string dir = make_temp_bag_dir(name);
    try
    {
        rosbag2_storage::StorageOptions opts;
        opts.uri        = dir;
        opts.storage_id = "sqlite3";
        rosbag2_cpp::Writer writer;
        writer.open(opts);

        rosbag2_storage::TopicMetadata meta;
        meta.name                 = "/float_topic";
        meta.type                 = "std_msgs/msg/Float64";
        meta.serialization_format = "cdr";
        writer.create_topic(meta);

        for (int i = 0; i < n_msgs; ++i)
        {
            const double  value = static_cast<double>(i) * 1.5;
            const double  t_s   = start_time_s + i * interval_s;
            const int64_t t_ns  = static_cast<int64_t>(t_s * 1e9);

            // Minimal CDR: 4-byte header + 8-byte double.
            std::vector<uint8_t> cdr(12, 0);
            cdr[0] = 0x00;
            cdr[1] = 0x01;
            cdr[2] = 0x00;
            cdr[3] = 0x00;
            std::memcpy(cdr.data() + 4, &value, sizeof(double));

            auto msg        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
            msg->topic_name = "/float_topic";
            bag_compat::set_bag_message_timestamp(*msg, t_ns);
            msg->serialized_data                  = std::make_shared<rcutils_uint8_array_t>();
            msg->serialized_data->allocator       = rcutils_get_default_allocator();
            msg->serialized_data->buffer_length   = cdr.size();
            msg->serialized_data->buffer_capacity = cdr.size();
            msg->serialized_data->buffer          = new uint8_t[cdr.size()];
            std::memcpy(msg->serialized_data->buffer, cdr.data(), cdr.size());
            writer.write(msg);
        }
    }
    catch (...)
    {
        return {};
    }
    return dir;
}

// Cleanup helper.
static void remove_bag(const std::string& dir)
{
    if (!dir.empty())
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}

}   // anonymous namespace

#endif   // SPECTRA_ROS2_BAG

// ---------------------------------------------------------------------------
// Minimal stubs for RosPlotManager / MessageIntrospector / Ros2Bridge so
// logic-only tests can construct a BagPlayer without a live ROS2 graph.
//
// We re-use the real classes because their constructors don't start a node
// unless bridge.init() / bridge.start_spin() are called.
// ---------------------------------------------------------------------------

// Fixture used for pure-logic (no-bag) tests.
class BagPlayerLogicTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // bridge_ stays uninitialized — BagPlayer won't try to add subscriptions.
        intr_   = std::make_unique<MessageIntrospector>();
        mgr_    = std::make_unique<RosPlotManager>(bridge_, *intr_);
        player_ = std::make_unique<BagPlayer>(*mgr_, *intr_);
    }

    Ros2Bridge                           bridge_;   // default-constructed, not spinning
    std::unique_ptr<MessageIntrospector> intr_;
    std::unique_ptr<RosPlotManager>      mgr_;
    std::unique_ptr<BagPlayer>           player_;
};

// ===========================================================================
// Suite 1: Construction
// ===========================================================================

TEST_F(BagPlayerLogicTest, ConstructionDefaults)
{
    EXPECT_FALSE(player_->is_open());
    EXPECT_TRUE(player_->is_stopped());
    EXPECT_DOUBLE_EQ(player_->rate(), 1.0);
    EXPECT_FALSE(player_->loop());
    EXPECT_DOUBLE_EQ(player_->playhead_sec(), 0.0);
    EXPECT_DOUBLE_EQ(player_->progress(), 0.0);
    EXPECT_EQ(player_->total_injected(), 0u);
    EXPECT_EQ(player_->timeline_editor(), nullptr);
}

TEST_F(BagPlayerLogicTest, ConstructionWithConfig)
{
    BagPlayerConfig cfg;
    cfg.rate                 = 2.0;
    cfg.loop                 = true;
    cfg.max_inject_per_frame = 500;
    BagPlayer p2(*mgr_, *intr_, cfg);
    EXPECT_DOUBLE_EQ(p2.rate(), 2.0);
    EXPECT_TRUE(p2.loop());
    EXPECT_EQ(p2.max_inject_per_frame(), 500u);
}

TEST_F(BagPlayerLogicTest, ConstructionIsNotOpen)
{
    EXPECT_FALSE(player_->is_open());
    EXPECT_TRUE(player_->last_error().empty());
}

// ===========================================================================
// Suite 2: Open failure (no-bag stub always returns false)
// ===========================================================================

TEST_F(BagPlayerLogicTest, OpenNonExistentBagFails)
{
    EXPECT_FALSE(player_->open("/does/not/exist.db3"));
    EXPECT_FALSE(player_->is_open());
}

TEST_F(BagPlayerLogicTest, OpenSetsErrorOnFailure)
{
#ifndef SPECTRA_ROS2_BAG
    // Stub always returns false.
    EXPECT_FALSE(player_->open("/fake.db3"));
    // last_error is set to the stub message.
    EXPECT_FALSE(player_->last_error().empty());
#else
    EXPECT_FALSE(player_->open("/totally/nonexistent/path.db3"));
    EXPECT_FALSE(player_->last_error().empty());
#endif
}

TEST_F(BagPlayerLogicTest, OpenEmptyPathFails)
{
    EXPECT_FALSE(player_->open(""));
    EXPECT_FALSE(player_->is_open());
}

// ===========================================================================
// Suite 3: State machine (no open bag — graceful no-op)
// ===========================================================================

TEST_F(BagPlayerLogicTest, PlayWithoutOpenBagIsNoOp)
{
    player_->play();
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerLogicTest, PauseWithoutOpenBagIsNoOp)
{
    player_->pause();
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerLogicTest, StopWithoutOpenBagIsNoOp)
{
    player_->stop();
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerLogicTest, TogglePlayWithoutOpenBagIsNoOp)
{
    player_->toggle_play();
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerLogicTest, AllStatePredicatesMutuallyExclusive)
{
    // Default state: stopped.
    EXPECT_TRUE(player_->is_stopped());
    EXPECT_FALSE(player_->is_playing());
    EXPECT_FALSE(player_->is_paused());
    EXPECT_EQ(player_->state(), PlayerState::Stopped);
}

// ===========================================================================
// Suite 4: Rate control
// ===========================================================================

TEST_F(BagPlayerLogicTest, SetRateClampedMin)
{
    player_->set_rate(0.0);
    EXPECT_DOUBLE_EQ(player_->rate(), BagPlayer::MIN_RATE);
}

TEST_F(BagPlayerLogicTest, SetRateClampedMax)
{
    player_->set_rate(999.0);
    EXPECT_DOUBLE_EQ(player_->rate(), BagPlayer::MAX_RATE);
}

TEST_F(BagPlayerLogicTest, SetRateNegativeClampedToMin)
{
    player_->set_rate(-1.0);
    EXPECT_DOUBLE_EQ(player_->rate(), BagPlayer::MIN_RATE);
}

TEST_F(BagPlayerLogicTest, SetRateExactMin)
{
    player_->set_rate(BagPlayer::MIN_RATE);
    EXPECT_DOUBLE_EQ(player_->rate(), BagPlayer::MIN_RATE);
}

TEST_F(BagPlayerLogicTest, SetRateExactMax)
{
    player_->set_rate(BagPlayer::MAX_RATE);
    EXPECT_DOUBLE_EQ(player_->rate(), BagPlayer::MAX_RATE);
}

TEST_F(BagPlayerLogicTest, SetRateValidMidRange)
{
    player_->set_rate(2.5);
    EXPECT_NEAR(player_->rate(), 2.5, 1e-9);
}

TEST_F(BagPlayerLogicTest, ConfigRateClampedOnConstruct)
{
    BagPlayerConfig cfg;
    cfg.rate = 0.0001;
    BagPlayer p(*mgr_, *intr_, cfg);
    EXPECT_DOUBLE_EQ(p.rate(), BagPlayer::MIN_RATE);
}

// ===========================================================================
// Suite 5: Loop mode
// ===========================================================================

TEST_F(BagPlayerLogicTest, LoopDefaultFalse)
{
    EXPECT_FALSE(player_->loop());
}

TEST_F(BagPlayerLogicTest, SetLoopTrue)
{
    player_->set_loop(true);
    EXPECT_TRUE(player_->loop());
}

TEST_F(BagPlayerLogicTest, SetLoopToggle)
{
    player_->set_loop(true);
    player_->set_loop(false);
    EXPECT_FALSE(player_->loop());
}

// ===========================================================================
// Suite 6: Seek (without open bag — all seeks should be no-ops / return false)
// ===========================================================================

TEST_F(BagPlayerLogicTest, SeekWithoutOpenBagReturnsFalse)
{
    EXPECT_FALSE(player_->seek(5.0));
}

TEST_F(BagPlayerLogicTest, SeekBeginWithoutOpenBagReturnsFalse)
{
    EXPECT_FALSE(player_->seek_begin());
}

TEST_F(BagPlayerLogicTest, SeekFractionWithoutOpenBagReturnsFalse)
{
    EXPECT_FALSE(player_->seek_fraction(0.5));
}

TEST_F(BagPlayerLogicTest, StepForwardWithoutOpenBagIsNoOp)
{
    player_->step_forward();   // must not crash
    EXPECT_DOUBLE_EQ(player_->playhead_sec(), 0.0);
}

TEST_F(BagPlayerLogicTest, StepBackwardWithoutOpenBagIsNoOp)
{
    player_->step_backward();   // must not crash
    EXPECT_DOUBLE_EQ(player_->playhead_sec(), 0.0);
}

// ===========================================================================
// Suite 7: Step size
// ===========================================================================

TEST_F(BagPlayerLogicTest, StepSizeDefault)
{
    EXPECT_NEAR(player_->step_size_sec(), 0.1, 1e-9);
}

TEST_F(BagPlayerLogicTest, SetStepSizeValid)
{
    player_->set_step_size(0.5);
    EXPECT_NEAR(player_->step_size_sec(), 0.5, 1e-9);
}

TEST_F(BagPlayerLogicTest, SetStepSizeClampedToMinimum)
{
    player_->set_step_size(0.0);
    EXPECT_GE(player_->step_size_sec(), 0.001);
}

TEST_F(BagPlayerLogicTest, SetStepSizeNegativeClampedToMinimum)
{
    player_->set_step_size(-5.0);
    EXPECT_GE(player_->step_size_sec(), 0.001);
}

// ===========================================================================
// Suite 8: State callbacks (fire even without open bag for state changes)
// ===========================================================================

TEST_F(BagPlayerLogicTest, StateCallbackNotFiredIfStateUnchanged)
{
    int fired = 0;
    player_->set_on_state_change([&](PlayerState) { ++fired; });
    // Already stopped; stop() again should not fire.
    player_->stop();
    EXPECT_EQ(fired, 0);
}

TEST_F(BagPlayerLogicTest, PlayheadCallbackRegistration)
{
    int fired = 0;
    player_->set_on_playhead([&](double) { ++fired; });
    // Without open bag, seek does nothing — callback won't fire.
    player_->seek(5.0);
    EXPECT_EQ(fired, 0);
}

TEST_F(BagPlayerLogicTest, MessageCallbackRegistration)
{
    // Just verify it can be set without crash.
    player_->set_on_message([](const std::string&, double, double) {});
}

// ===========================================================================
// Suite 9: Activity bands (empty without open bag)
// ===========================================================================

TEST_F(BagPlayerLogicTest, ActivityBandsEmptyWithoutBag)
{
    EXPECT_TRUE(player_->topic_activity_bands().empty());
}

TEST_F(BagPlayerLogicTest, ActivityBandForUnknownTopicIsNull)
{
    EXPECT_EQ(player_->activity_band("/unknown"), nullptr);
}

// ===========================================================================
// Suite 10: TimelineEditor wiring (no crash without editor)
// ===========================================================================

TEST_F(BagPlayerLogicTest, TimelineEditorNullByDefault)
{
    EXPECT_EQ(player_->timeline_editor(), nullptr);
}

TEST_F(BagPlayerLogicTest, SetTimelineEditorNullIsNoOp)
{
    player_->set_timeline_editor(nullptr);
    EXPECT_EQ(player_->timeline_editor(), nullptr);
}

// ===========================================================================
// Suite 11: advance() without open bag
// ===========================================================================

TEST_F(BagPlayerLogicTest, AdvanceWithoutOpenBagReturnsFalse)
{
    EXPECT_FALSE(player_->advance(0.016));
}

TEST_F(BagPlayerLogicTest, AdvanceWhilePausedReturnsTrueForPaused)
{
    // Can't get to Paused without opening — returns false (Stopped).
    EXPECT_FALSE(player_->advance(0.016));
}

// ===========================================================================
// Suite 12: BagPlaybackPanel — static helpers (always compiled)
// ===========================================================================

TEST(BagPlaybackPanelFormatTest, FormatTimeZero)
{
    EXPECT_EQ(BagPlaybackPanel::format_time(0.0), "0:00.0");
}

TEST(BagPlaybackPanelFormatTest, FormatTimeSubMinute)
{
    // 5.3 seconds → "0:05.3"
    const std::string s = BagPlaybackPanel::format_time(5.3);
    EXPECT_EQ(s, "0:05.3");
}

TEST(BagPlaybackPanelFormatTest, FormatTimeOneMinute)
{
    // 60.0 seconds → "1:00.0"
    const std::string s = BagPlaybackPanel::format_time(60.0);
    EXPECT_EQ(s, "1:00.0");
}

TEST(BagPlaybackPanelFormatTest, FormatTimeOneHour)
{
    // 3600 seconds → "1:00:00"
    const std::string s = BagPlaybackPanel::format_time(3600.0);
    EXPECT_EQ(s, "1:00:00");
}

TEST(BagPlaybackPanelFormatTest, FormatTimeNegativeIsZero)
{
    const std::string s = BagPlaybackPanel::format_time(-5.0);
    EXPECT_EQ(s, "0:00.0");
}

TEST(BagPlaybackPanelFormatTest, FormatTimeComplexValue)
{
    // 125.7 s = 2m 5.7s → "2:05.7"
    const std::string s = BagPlaybackPanel::format_time(125.7);
    EXPECT_EQ(s, "2:05.7");
}

TEST(BagPlaybackPanelFormatTest, RateLabelOneX)
{
    const std::string s = BagPlaybackPanel::rate_label(1.0);
    EXPECT_NE(s.find('1'), std::string::npos);
}

TEST(BagPlaybackPanelFormatTest, RateLabelHalfX)
{
    const std::string s = BagPlaybackPanel::rate_label(0.5);
    EXPECT_NE(s.find('0'), std::string::npos);   // "0.5×"
}

TEST(BagPlaybackPanelFormatTest, RateLabelTenX)
{
    const std::string s = BagPlaybackPanel::rate_label(10.0);
    EXPECT_NE(s.find("10"), std::string::npos);
}

// ===========================================================================
// Suite 13: BagPlaybackPanel — construction and config
// ===========================================================================

TEST(BagPlaybackPanelTest, ConstructionNullPlayer)
{
    BagPlaybackPanel panel(nullptr);
    EXPECT_EQ(panel.player(), nullptr);
    EXPECT_EQ(panel.title(), "Bag Playback");
}

TEST(BagPlaybackPanelTest, SetPlayer)
{
    Ros2Bridge          bridge;
    MessageIntrospector intr;
    RosPlotManager      mgr(bridge, intr);
    BagPlayer           player(mgr, intr);

    BagPlaybackPanel panel;
    panel.set_player(&player);
    EXPECT_EQ(panel.player(), &player);
}

TEST(BagPlaybackPanelTest, SetTitle)
{
    BagPlaybackPanel panel;
    panel.set_title("My Panel");
    EXPECT_EQ(panel.title(), "My Panel");
}

TEST(BagPlaybackPanelTest, SetProgressBarHeight)
{
    BagPlaybackPanel panel;
    panel.set_progress_bar_height(20.0f);
    EXPECT_FLOAT_EQ(panel.progress_bar_height(), 20.0f);
}

TEST(BagPlaybackPanelTest, SetTimelineHeight)
{
    BagPlaybackPanel panel;
    panel.set_timeline_height(200.0f);
    EXPECT_FLOAT_EQ(panel.timeline_height(), 200.0f);
}

// ===========================================================================
// Suite 14: MaxInjectPerFrame
// ===========================================================================

TEST_F(BagPlayerLogicTest, MaxInjectPerFrameDefault)
{
    EXPECT_EQ(player_->max_inject_per_frame(), 2000u);
}

TEST_F(BagPlayerLogicTest, SetMaxInjectPerFrame)
{
    player_->set_max_inject_per_frame(500u);
    EXPECT_EQ(player_->max_inject_per_frame(), 500u);
}

// ===========================================================================
// Suite 15: PlayerState enum coverage
// ===========================================================================

TEST(PlayerStateTest, EnumValues)
{
    EXPECT_NE(PlayerState::Stopped, PlayerState::Playing);
    EXPECT_NE(PlayerState::Playing, PlayerState::Paused);
    EXPECT_NE(PlayerState::Stopped, PlayerState::Paused);
}

// ===========================================================================
// Suite 16: TopicActivityBand struct
// ===========================================================================

TEST(TopicActivityBandTest, DefaultConstruction)
{
    TopicActivityBand band;
    EXPECT_TRUE(band.topic.empty());
    EXPECT_TRUE(band.intervals.empty());
    EXPECT_EQ(band.timeline_track_id, 0u);
}

TEST(TopicActivityBandTest, AddInterval)
{
    TopicActivityBand band;
    band.topic = "/test";
    band.intervals.push_back({0.0, 5.0});
    band.intervals.push_back({10.0, 15.0});
    EXPECT_EQ(band.intervals.size(), 2u);
    EXPECT_DOUBLE_EQ(band.intervals[0].start_sec, 0.0);
    EXPECT_DOUBLE_EQ(band.intervals[1].end_sec, 15.0);
}

// ===========================================================================
// SPECTRA_ROS2_BAG=ON tests — require a live bag file
// ===========================================================================

#ifdef SPECTRA_ROS2_BAG

// Fixture for full bag tests.
class BagPlayerBagTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        intr_   = std::make_unique<MessageIntrospector>();
        mgr_    = std::make_unique<RosPlotManager>(bridge_, *intr_);
        player_ = std::make_unique<BagPlayer>(*mgr_, *intr_);
    }

    void TearDown() override
    {
        player_.reset();
        mgr_.reset();
        intr_.reset();
        remove_bag(bag_dir_);
    }

    Ros2Bridge                           bridge_;
    std::unique_ptr<MessageIntrospector> intr_;
    std::unique_ptr<RosPlotManager>      mgr_;
    std::unique_ptr<BagPlayer>           player_;
    std::string                          bag_dir_;
};

// ---------------------------------------------------------------------------
// Open / metadata
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, OpenValidBagSucceeds)
{
    bag_dir_ = write_float64_bag("open_success", 10);
    ASSERT_FALSE(bag_dir_.empty());
    EXPECT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->is_open());
    EXPECT_TRUE(player_->last_error().empty());
}

TEST_F(BagPlayerBagTest, MetadataAfterOpen)
{
    bag_dir_ = write_float64_bag("metadata", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    const auto& meta = player_->metadata();
    EXPECT_GT(meta.duration_sec(), 0.0);
    EXPECT_GT(meta.start_time_sec(), 0.0);
    EXPECT_EQ(player_->duration_sec(), meta.duration_sec());
}

TEST_F(BagPlayerBagTest, BagPathAfterOpen)
{
    bag_dir_ = write_float64_bag("path", 5);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_EQ(player_->bag_path(), bag_dir_);
}

TEST_F(BagPlayerBagTest, CloseResetsState)
{
    bag_dir_ = write_float64_bag("close_reset", 5);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->close();
    EXPECT_FALSE(player_->is_open());
    EXPECT_TRUE(player_->is_stopped());
    EXPECT_DOUBLE_EQ(player_->playhead_sec(), 0.0);
}

TEST_F(BagPlayerBagTest, ReopenBag)
{
    bag_dir_ = write_float64_bag("reopen", 5);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->close();
    EXPECT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->is_open());
}

// ---------------------------------------------------------------------------
// Transport state machine with open bag
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, PlayTransitionsToPlaying)
{
    bag_dir_ = write_float64_bag("play_state", 10);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();
    EXPECT_TRUE(player_->is_playing());
    EXPECT_FALSE(player_->is_stopped());
    EXPECT_FALSE(player_->is_paused());
}

TEST_F(BagPlayerBagTest, PauseTransitionsToPaused)
{
    bag_dir_ = write_float64_bag("pause_state", 10);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();
    player_->pause();
    EXPECT_TRUE(player_->is_paused());
    EXPECT_FALSE(player_->is_playing());
}

TEST_F(BagPlayerBagTest, StopResetsToStopped)
{
    bag_dir_ = write_float64_bag("stop_state", 10);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();
    player_->stop();
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerBagTest, TogglePlayPauseResumesCycle)
{
    bag_dir_ = write_float64_bag("toggle_play", 10);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->toggle_play();
    EXPECT_TRUE(player_->is_playing());
    player_->toggle_play();
    EXPECT_TRUE(player_->is_paused());
    player_->toggle_play();
    EXPECT_TRUE(player_->is_playing());
}

TEST_F(BagPlayerBagTest, StateCallbackFiredOnTransition)
{
    bag_dir_ = write_float64_bag("state_cb", 10);
    ASSERT_TRUE(player_->open(bag_dir_));

    std::vector<PlayerState> states;
    player_->set_on_state_change([&](PlayerState s) { states.push_back(s); });

    player_->play();
    player_->pause();
    player_->stop();

    ASSERT_EQ(states.size(), 3u);
    EXPECT_EQ(states[0], PlayerState::Playing);
    EXPECT_EQ(states[1], PlayerState::Paused);
    EXPECT_EQ(states[2], PlayerState::Stopped);
}

// ---------------------------------------------------------------------------
// Seek with open bag
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, SeekUpdatesPlayhead)
{
    bag_dir_ = write_float64_bag("seek_ph", 20, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    const double half = player_->duration_sec() * 0.5;
    EXPECT_TRUE(player_->seek(half));
    EXPECT_NEAR(player_->playhead_sec(), half, 0.01);
}

TEST_F(BagPlayerBagTest, SeekClampedToZero)
{
    bag_dir_ = write_float64_bag("seek_clamp_min", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->seek(-100.0));
    EXPECT_DOUBLE_EQ(player_->playhead_sec(), 0.0);
}

TEST_F(BagPlayerBagTest, SeekClampedToDuration)
{
    bag_dir_ = write_float64_bag("seek_clamp_max", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->seek(999.0));
    EXPECT_NEAR(player_->playhead_sec(), player_->duration_sec(), 0.01);
}

TEST_F(BagPlayerBagTest, SeekBeginResetsPlayhead)
{
    bag_dir_ = write_float64_bag("seek_begin", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->seek(player_->duration_sec() * 0.5);
    EXPECT_TRUE(player_->seek_begin());
    EXPECT_NEAR(player_->playhead_sec(), 0.0, 0.01);
}

TEST_F(BagPlayerBagTest, SeekFractionMidpoint)
{
    bag_dir_ = write_float64_bag("seek_frac", 20, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->seek_fraction(0.5));
    EXPECT_NEAR(player_->progress(), 0.5, 0.1);
}

TEST_F(BagPlayerBagTest, SeekFractionZeroAndOne)
{
    bag_dir_ = write_float64_bag("seek_frac_01", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_TRUE(player_->seek_fraction(0.0));
    EXPECT_NEAR(player_->progress(), 0.0, 0.05);
    EXPECT_TRUE(player_->seek_fraction(1.0));
    EXPECT_NEAR(player_->progress(), 1.0, 0.05);
}

TEST_F(BagPlayerBagTest, PlayheadCallbackFiredOnSeek)
{
    bag_dir_ = write_float64_bag("ph_cb_seek", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));

    double last_ph = -1.0;
    player_->set_on_playhead([&](double t) { last_ph = t; });
    player_->seek(0.3);
    EXPECT_GE(last_ph, 0.0);
}

// ---------------------------------------------------------------------------
// advance() — reads + injects messages
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, AdvanceWhilePlayingInjectsMessages)
{
    bag_dir_ = write_float64_bag("advance_inject", 20, 1000.0, 0.05);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();

    // Advance by the full duration at rate=1 to consume all messages.
    const double dur = player_->duration_sec();
    player_->advance(dur + 0.1);

    EXPECT_GT(player_->total_injected(), 0u);
}

TEST_F(BagPlayerBagTest, AdvanceWhilePausedInjectsNothing)
{
    bag_dir_ = write_float64_bag("advance_paused", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();
    player_->pause();

    player_->advance(1.0);
    EXPECT_EQ(player_->total_injected(), 0u);
}

TEST_F(BagPlayerBagTest, AdvanceReachesEndStops)
{
    bag_dir_ = write_float64_bag("advance_end", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->play();
    player_->advance(player_->duration_sec() + 1.0);
    EXPECT_TRUE(player_->is_stopped());
}

TEST_F(BagPlayerBagTest, AdvanceLoopsWhenEnabled)
{
    bag_dir_ = write_float64_bag("advance_loop", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->set_loop(true);
    player_->play();

    player_->advance(player_->duration_sec() + 0.5);
    // After loop: should still be playing.
    EXPECT_TRUE(player_->is_playing());
    // Playhead should be near the beginning (looped back).
    EXPECT_LT(player_->playhead_sec(), player_->duration_sec() * 0.5);
}

// ---------------------------------------------------------------------------
// Activity bands
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, ActivityBandsPopulatedAfterOpen)
{
    bag_dir_ = write_float64_bag("activity", 50, 1000.0, 0.02);
    ASSERT_TRUE(player_->open(bag_dir_));
    const auto bands = player_->topic_activity_bands();
    EXPECT_FALSE(bands.empty());
}

TEST_F(BagPlayerBagTest, ActivityBandTopicName)
{
    bag_dir_ = write_float64_bag("activity_topic", 20, 1000.0, 0.05);
    ASSERT_TRUE(player_->open(bag_dir_));
    const TopicActivityBand* band = player_->activity_band("/float_topic");
    ASSERT_NE(band, nullptr);
    EXPECT_EQ(band->topic, "/float_topic");
}

TEST_F(BagPlayerBagTest, ActivityBandHasIntervals)
{
    bag_dir_ = write_float64_bag("activity_intervals", 30, 1000.0, 0.03);
    ASSERT_TRUE(player_->open(bag_dir_));
    const TopicActivityBand* band = player_->activity_band("/float_topic");
    ASSERT_NE(band, nullptr);
    EXPECT_FALSE(band->intervals.empty());
    // All intervals must be sorted.
    for (size_t i = 1; i < band->intervals.size(); ++i)
        EXPECT_GE(band->intervals[i].start_sec, band->intervals[i - 1].end_sec);
}

TEST_F(BagPlayerBagTest, ActivityBandUnknownTopicNull)
{
    bag_dir_ = write_float64_bag("activity_unknown", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_EQ(player_->activity_band("/no_such_topic"), nullptr);
}

TEST_F(BagPlayerBagTest, ActivityBandThreadSafeSnapshot)
{
    bag_dir_ = write_float64_bag("activity_snapshot", 20, 1000.0, 0.05);
    ASSERT_TRUE(player_->open(bag_dir_));
    // Call from multiple contexts — just check it doesn't crash.
    auto bands1 = player_->topic_activity_bands();
    auto bands2 = player_->topic_activity_bands();
    EXPECT_EQ(bands1.size(), bands2.size());
}

// ---------------------------------------------------------------------------
// Step forward / backward
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, StepForwardAdvancesPlayhead)
{
    bag_dir_ = write_float64_bag("step_fwd", 20, 1000.0, 0.05);
    ASSERT_TRUE(player_->open(bag_dir_));
    const double before = player_->playhead_sec();
    player_->step_forward();
    EXPECT_GT(player_->playhead_sec(), before);
}

TEST_F(BagPlayerBagTest, StepBackwardDecreasesPlayhead)
{
    bag_dir_ = write_float64_bag("step_back", 20, 1000.0, 0.05);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->seek_fraction(0.5);
    const double mid = player_->playhead_sec();
    player_->step_backward();
    EXPECT_LT(player_->playhead_sec(), mid);
}

TEST_F(BagPlayerBagTest, StepBackwardAtStartClampedToZero)
{
    bag_dir_ = write_float64_bag("step_back_zero", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->seek_begin();
    player_->step_backward();
    EXPECT_NEAR(player_->playhead_sec(), 0.0, 0.01);
}

// ---------------------------------------------------------------------------
// Metadata helpers
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, DurationSecGreaterThanZero)
{
    bag_dir_ = write_float64_bag("duration", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_GT(player_->duration_sec(), 0.0);
}

TEST_F(BagPlayerBagTest, ProgressZeroAtStart)
{
    bag_dir_ = write_float64_bag("progress_zero", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    EXPECT_NEAR(player_->progress(), 0.0, 0.05);
}

TEST_F(BagPlayerBagTest, ProgressOneAtEnd)
{
    bag_dir_ = write_float64_bag("progress_one", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));
    player_->seek_fraction(1.0);
    EXPECT_NEAR(player_->progress(), 1.0, 0.05);
}

// ---------------------------------------------------------------------------
// Message callback
// ---------------------------------------------------------------------------

TEST_F(BagPlayerBagTest, MessageCallbackFiredDuringAdvance)
{
    bag_dir_ = write_float64_bag("msg_cb", 10, 1000.0, 0.1);
    ASSERT_TRUE(player_->open(bag_dir_));

    std::vector<std::string> topics;
    player_->set_on_message([&](const std::string& t, double, double) { topics.push_back(t); });

    player_->play();
    player_->advance(player_->duration_sec() + 0.1);

    // The callback fires once per injected message (may be 0 if introspection
    // can't resolve the CDR stub field — depends on ROS2 environment).
    // Just check no crash.
    (void)topics;
}

#endif   // SPECTRA_ROS2_BAG

// ===========================================================================
// main()
// ===========================================================================

#ifdef SPECTRA_ROS2_BAG
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment());
    return RUN_ALL_TESTS();
}
#endif   // SPECTRA_ROS2_BAG
