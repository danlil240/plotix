// test_diagnostics_panel.cpp — Unit tests for DiagnosticsPanel (F6).
//
// All tests run without a ROS2 runtime, Vulkan context, or ImGui context.
// CDR parsing, model logic, sparkline history, alert callbacks, and ring
// buffer behaviour are all exercised via the public testing helpers.

#include <gtest/gtest.h>

#include "ui/diagnostics_panel.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static DiagStatus make_status(const std::string& name,
                              DiagLevel          level,
                              const std::string& message = "",
                              const std::string& hw_id   = "")
{
    DiagStatus s;
    s.name        = name;
    s.level       = level;
    s.message     = message;
    s.hardware_id = hw_id;
    s.arrival_ns  = 1'000'000'000LL;
    return s;
}

static DiagStatus make_status_kv(const std::string&                               name,
                                 DiagLevel                                        level,
                                 std::vector<std::pair<std::string, std::string>> kvs)
{
    DiagStatus s = make_status(name, level);
    for (auto& [k, v] : kvs)
        s.values.push_back({k, v});
    return s;
}

// Tiny CDR builder — just enough to produce a valid DiagnosticArray blob
// for parse_diag_array tests.
static void append_u32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void append_string(std::vector<uint8_t>& buf, const std::string& s)
{
    // CDR: 4-byte length including null terminator, then bytes+null
    const uint32_t len = static_cast<uint32_t>(s.size()) + 1;
    append_u32(buf, len);
    for (char c : s)
        buf.push_back(static_cast<uint8_t>(c));
    buf.push_back(0);   // null terminator
    // pad to 4-byte boundary from buffer start
    while (buf.size() % 4 != 0)
        buf.push_back(0);
}

static std::vector<uint8_t> build_diag_array(const std::vector<DiagStatus>& statuses)
{
    std::vector<uint8_t> buf;

    // CDR encapsulation header (little-endian)
    buf.push_back(0x00);
    buf.push_back(0x01);
    buf.push_back(0x00);
    buf.push_back(0x00);

    // std_msgs/Header: stamp (8 bytes) + frame_id string
    append_u32(buf, 0);       // sec
    append_u32(buf, 0);       // nanosec
    append_string(buf, "");   // frame_id

    // status[] count
    append_u32(buf, static_cast<uint32_t>(statuses.size()));

    for (const auto& s : statuses)
    {
        // Align to 4 from element start: write level byte then 3 pad bytes
        const size_t elem_start = buf.size();
        buf.push_back(static_cast<uint8_t>(s.level));
        // pad to 4-byte from elem_start
        while ((buf.size() - elem_start) % 4 != 0)
            buf.push_back(0);

        append_string(buf, s.name);
        append_string(buf, s.message);
        append_string(buf, s.hardware_id);

        // values[]
        append_u32(buf, static_cast<uint32_t>(s.values.size()));
        for (const auto& kv : s.values)
        {
            append_string(buf, kv.key);
            append_string(buf, kv.value);
        }
    }
    return buf;
}

// ===========================================================================
// DiagLevel helpers
// ===========================================================================

TEST(DiagLevelName, AllLevels)
{
    EXPECT_STREQ("OK", diag_level_name(DiagLevel::OK));
    EXPECT_STREQ("WARN", diag_level_name(DiagLevel::Warn));
    EXPECT_STREQ("ERROR", diag_level_name(DiagLevel::Error));
    EXPECT_STREQ("STALE", diag_level_name(DiagLevel::Stale));
}

TEST(DiagLevelShort, AllLevels)
{
    EXPECT_STREQ("OK", DiagnosticsPanel::level_short(DiagLevel::OK));
    EXPECT_STREQ("WARN", DiagnosticsPanel::level_short(DiagLevel::Warn));
    EXPECT_STREQ("ERR", DiagnosticsPanel::level_short(DiagLevel::Error));
    EXPECT_STREQ("STALE", DiagnosticsPanel::level_short(DiagLevel::Stale));
}

TEST(DiagLevelColor, OKIsGreen)
{
    float r, g, b, a;
    DiagnosticsPanel::level_color(DiagLevel::OK, r, g, b, a);
    EXPECT_GT(g, r);
    EXPECT_GT(g, b);
    EXPECT_FLOAT_EQ(1.0f, a);
}

TEST(DiagLevelColor, ErrorIsRed)
{
    float r, g, b, a;
    DiagnosticsPanel::level_color(DiagLevel::Error, r, g, b, a);
    EXPECT_GT(r, g);
    EXPECT_GT(r, b);
}

TEST(DiagLevelColor, WarnIsYellow)
{
    float r, g, b, a;
    DiagnosticsPanel::level_color(DiagLevel::Warn, r, g, b, a);
    EXPECT_GT(r, b);
    EXPECT_GT(g, b);
}

TEST(DiagLevelColor, StaleIsGray)
{
    float r, g, b, a;
    DiagnosticsPanel::level_color(DiagLevel::Stale, r, g, b, a);
    // r ≈ g ≈ b
    EXPECT_NEAR(r, g, 0.15f);
    EXPECT_NEAR(g, b, 0.15f);
}

// ===========================================================================
// DiagComponent
// ===========================================================================

TEST(DiagComponent, InitialLevelIsStale)
{
    DiagComponent c;
    EXPECT_EQ(DiagLevel::Stale, c.level);
    EXPECT_TRUE(c.history.empty());
    EXPECT_EQ(0u, c.transition_count);
    EXPECT_FALSE(c.ever_alerted);
}

TEST(DiagComponent, UpdateSetsFields)
{
    DiagComponent    c;
    const DiagStatus s = make_status("battery", DiagLevel::OK, "Charging", "bat_hw");
    c.update(s);
    EXPECT_EQ(DiagLevel::OK, c.level);
    EXPECT_EQ("battery", c.name);
    EXPECT_EQ("Charging", c.message);
    EXPECT_EQ("bat_hw", c.hardware_id);
    EXPECT_EQ(1u, c.history.size());
}

TEST(DiagComponent, UpdateReturnsTrueOnTransition)
{
    DiagComponent c;
    c.level      = DiagLevel::OK;
    DiagStatus s = make_status("x", DiagLevel::Warn);
    EXPECT_TRUE(c.update(s));
}

TEST(DiagComponent, UpdateReturnsFalseWhenSameLevel)
{
    DiagComponent c;
    c.level      = DiagLevel::OK;
    DiagStatus s = make_status("x", DiagLevel::OK);
    EXPECT_FALSE(c.update(s));
}

TEST(DiagComponent, TransitionCountIncrementsOnChange)
{
    DiagComponent c;
    c.level = DiagLevel::OK;
    c.update(make_status("x", DiagLevel::Warn));
    c.update(make_status("x", DiagLevel::Error));
    EXPECT_EQ(2u, c.transition_count);
}

TEST(DiagComponent, EverAlertedSetOnWarn)
{
    DiagComponent c;
    c.level = DiagLevel::OK;
    c.update(make_status("x", DiagLevel::Warn));
    EXPECT_TRUE(c.ever_alerted);
}

TEST(DiagComponent, EverAlertedSetOnError)
{
    DiagComponent c;
    c.level = DiagLevel::OK;
    c.update(make_status("x", DiagLevel::Error));
    EXPECT_TRUE(c.ever_alerted);
}

TEST(DiagComponent, EverAlertedNotSetOnOK)
{
    DiagComponent c;
    c.level = DiagLevel::Stale;
    c.update(make_status("x", DiagLevel::OK));
    EXPECT_FALSE(c.ever_alerted);
}

TEST(DiagComponent, SparklineCapAtMAX)
{
    DiagComponent c;
    c.level = DiagLevel::OK;
    for (size_t i = 0; i < DiagComponent::MAX_SPARK + 10; ++i)
        c.update(make_status("x", DiagLevel::OK));
    EXPECT_EQ(DiagComponent::MAX_SPARK, c.history.size());
}

TEST(DiagComponent, KeyValuesCopied)
{
    DiagComponent c;
    auto          s = make_status_kv("x", DiagLevel::OK, {{"voltage", "12.3"}, {"temp", "45"}});
    c.update(s);
    ASSERT_EQ(2u, c.values.size());
    EXPECT_EQ("voltage", c.values[0].key);
    EXPECT_EQ("12.3", c.values[0].value);
    EXPECT_EQ("temp", c.values[1].key);
    EXPECT_EQ("45", c.values[1].value);
}

// ===========================================================================
// DiagnosticsModel
// ===========================================================================

TEST(DiagnosticsModel, ApplyCreatesNewComponent)
{
    DiagnosticsModel m;
    m.apply(make_status("cam", DiagLevel::OK));
    EXPECT_EQ(1u, m.components.size());
    EXPECT_EQ(1u, m.order.size());
    EXPECT_EQ("cam", m.order[0]);
}

TEST(DiagnosticsModel, ApplyUpdatesExisting)
{
    DiagnosticsModel m;
    m.apply(make_status("cam", DiagLevel::OK));
    m.apply(make_status("cam", DiagLevel::Warn));
    EXPECT_EQ(1u, m.components.size());
    EXPECT_EQ(DiagLevel::Warn, m.components.at("cam").level);
}

TEST(DiagnosticsModel, ApplyMultipleComponents)
{
    DiagnosticsModel m;
    m.apply(make_status("a", DiagLevel::OK));
    m.apply(make_status("b", DiagLevel::Warn));
    m.apply(make_status("c", DiagLevel::Error));
    EXPECT_EQ(3u, m.components.size());
    EXPECT_EQ(3u, m.order.size());
}

TEST(DiagnosticsModel, ApplyReturnsTransitionedName)
{
    DiagnosticsModel m;
    m.apply(make_status("bat", DiagLevel::OK));
    const std::string t = m.apply(make_status("bat", DiagLevel::Warn));
    EXPECT_EQ("bat", t);
}

TEST(DiagnosticsModel, ApplyReturnsEmptyWhenNoTransition)
{
    DiagnosticsModel m;
    m.apply(make_status("bat", DiagLevel::OK));
    const std::string t = m.apply(make_status("bat", DiagLevel::OK));
    EXPECT_TRUE(t.empty());
}

TEST(DiagnosticsModel, ReCountAllOK)
{
    DiagnosticsModel m;
    m.apply(make_status("a", DiagLevel::OK));
    m.apply(make_status("b", DiagLevel::OK));
    m.recount();
    EXPECT_EQ(2, m.count_ok);
    EXPECT_EQ(0, m.count_warn);
    EXPECT_EQ(0, m.count_error);
    EXPECT_EQ(0, m.count_stale);
}

TEST(DiagnosticsModel, ReCountMixed)
{
    DiagnosticsModel m;
    m.apply(make_status("a", DiagLevel::OK));
    m.apply(make_status("b", DiagLevel::Warn));
    m.apply(make_status("c", DiagLevel::Error));
    m.apply(make_status("d", DiagLevel::Stale));
    m.recount();
    EXPECT_EQ(1, m.count_ok);
    EXPECT_EQ(1, m.count_warn);
    EXPECT_EQ(1, m.count_error);
    EXPECT_EQ(1, m.count_stale);
}

TEST(DiagnosticsModel, PruneStaleMarksOldComponents)
{
    DiagnosticsModel m;
    DiagStatus       s = make_status("lidar", DiagLevel::OK);
    s.arrival_ns       = 1'000'000'000LL;   // 1 second
    m.apply(s);

    const int64_t now_ns   = 10'000'000'000LL;   // 10 s
    const int64_t stale_ns = 5'000'000'000LL;    // 5 s threshold
    m.prune_stale(now_ns, stale_ns);

    EXPECT_EQ(DiagLevel::Stale, m.components.at("lidar").level);
}

TEST(DiagnosticsModel, PruneStaleDoesNotMarkRecent)
{
    DiagnosticsModel m;
    DiagStatus       s = make_status("lidar", DiagLevel::OK);
    s.arrival_ns       = 9'000'000'000LL;   // 9 s
    m.apply(s);

    const int64_t now_ns   = 10'000'000'000LL;
    const int64_t stale_ns = 5'000'000'000LL;
    m.prune_stale(now_ns, stale_ns);

    EXPECT_EQ(DiagLevel::OK, m.components.at("lidar").level);
}

TEST(DiagnosticsModel, TotalMessagesIncrement)
{
    DiagnosticsModel m;
    m.apply(make_status("a", DiagLevel::OK));
    m.apply(make_status("a", DiagLevel::OK));
    m.apply(make_status("b", DiagLevel::Warn));
    EXPECT_EQ(3u, m.total_messages);
}

// ===========================================================================
// CDR parse_diag_array
// ===========================================================================

TEST(ParseDiagArray, EmptyBuffer)
{
    auto r = DiagnosticsPanel::parse_diag_array(nullptr, 0, 0);
    EXPECT_TRUE(r.empty());
}

TEST(ParseDiagArray, TooShortBuffer)
{
    std::vector<uint8_t> buf(3, 0x00);
    auto                 r = DiagnosticsPanel::parse_diag_array(buf.data(), buf.size(), 0);
    EXPECT_TRUE(r.empty());
}

TEST(ParseDiagArray, SingleStatusOK)
{
    DiagStatus s      = make_status("motor", DiagLevel::OK, "Running fine", "motor_0");
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 12345LL);

    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(DiagLevel::OK, result[0].level);
    EXPECT_EQ("motor", result[0].name);
    EXPECT_EQ("Running fine", result[0].message);
    EXPECT_EQ("motor_0", result[0].hardware_id);
    EXPECT_EQ(12345LL, result[0].arrival_ns);
}

TEST(ParseDiagArray, SingleStatusWarn)
{
    DiagStatus s      = make_status("battery", DiagLevel::Warn, "Low voltage");
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(DiagLevel::Warn, result[0].level);
    EXPECT_EQ("Low voltage", result[0].message);
}

TEST(ParseDiagArray, SingleStatusError)
{
    DiagStatus s      = make_status("lidar", DiagLevel::Error, "Timeout");
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(DiagLevel::Error, result[0].level);
}

TEST(ParseDiagArray, SingleStatusStale)
{
    DiagStatus s      = make_status("imu", DiagLevel::Stale);
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(DiagLevel::Stale, result[0].level);
}

TEST(ParseDiagArray, MultipleStatuses)
{
    std::vector<DiagStatus> statuses = {
        make_status("a", DiagLevel::OK),
        make_status("b", DiagLevel::Warn),
        make_status("c", DiagLevel::Error),
    };
    auto blob   = build_diag_array(statuses);
    auto result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(3u, result.size());
    EXPECT_EQ("a", result[0].name);
    EXPECT_EQ("b", result[1].name);
    EXPECT_EQ("c", result[2].name);
}

TEST(ParseDiagArray, KeyValuePairs)
{
    DiagStatus s      = make_status_kv("cam",
                                  DiagLevel::OK,
                                       {{"fps", "30"}, {"exposure", "100us"}, {"gain", "1.0"}});
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    ASSERT_EQ(3u, result[0].values.size());
    EXPECT_EQ("fps", result[0].values[0].key);
    EXPECT_EQ("30", result[0].values[0].value);
    EXPECT_EQ("exposure", result[0].values[1].key);
    EXPECT_EQ("100us", result[0].values[1].value);
    EXPECT_EQ("gain", result[0].values[2].key);
}

TEST(ParseDiagArray, EmptyStatusArray)
{
    auto blob   = build_diag_array({});
    auto result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    EXPECT_TRUE(result.empty());
}

TEST(ParseDiagArray, ArrivalNsPreserved)
{
    DiagStatus s      = make_status("x", DiagLevel::OK);
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 9999LL);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(9999LL, result[0].arrival_ns);
}

TEST(ParseDiagArray, EmptyStringsInStatus)
{
    DiagStatus s      = make_status("", DiagLevel::OK, "", "");
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    EXPECT_TRUE(result[0].name.empty());
    EXPECT_TRUE(result[0].message.empty());
    EXPECT_TRUE(result[0].hardware_id.empty());
}

// ===========================================================================
// DiagnosticsPanel — inject_status and inject_array
// ===========================================================================

TEST(DiagnosticsPanelInject, InjectStatusCreatesComponent)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("lidar", DiagLevel::OK));
    EXPECT_EQ(1u, panel.model().components.size());
}

TEST(DiagnosticsPanelInject, InjectStatusUpdatesLevel)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("lidar", DiagLevel::OK));
    panel.inject_status(make_status("lidar", DiagLevel::Error));
    EXPECT_EQ(DiagLevel::Error, panel.model().components.at("lidar").level);
}

TEST(DiagnosticsPanelInject, InjectArrayMultiple)
{
    DiagnosticsPanel panel;
    panel.inject_array({
        make_status("a", DiagLevel::OK),
        make_status("b", DiagLevel::Warn),
        make_status("c", DiagLevel::Error),
    });
    EXPECT_EQ(3u, panel.model().components.size());
}

TEST(DiagnosticsPanelInject, InjectArraySetsArrivalNs)
{
    DiagnosticsPanel panel;
    DiagStatus       s = make_status("x", DiagLevel::OK);
    s.arrival_ns       = 0;
    panel.inject_array({s}, 7777LL);
    EXPECT_EQ(7777LL, panel.model().components.at("x").last_update_ns);
}

// ===========================================================================
// Alert callback
// ===========================================================================

TEST(DiagnosticsAlert, CallbackFiredOnWarnTransition)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("bat", DiagLevel::OK));

    std::string alerted_name;
    DiagLevel   alerted_level = DiagLevel::OK;
    panel.set_alert_callback(
        [&](const std::string& n, DiagLevel l)
        {
            alerted_name  = n;
            alerted_level = l;
        });

    panel.inject_status(make_status("bat", DiagLevel::Warn));
    EXPECT_EQ("bat", alerted_name);
    EXPECT_EQ(DiagLevel::Warn, alerted_level);
}

TEST(DiagnosticsAlert, CallbackFiredOnErrorTransition)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("cam", DiagLevel::OK));

    int calls = 0;
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls; });

    panel.inject_status(make_status("cam", DiagLevel::Error));
    EXPECT_EQ(1, calls);
}

TEST(DiagnosticsAlert, CallbackNotFiredOnOKTransition)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("cam", DiagLevel::Warn));

    int calls = 0;
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls; });

    panel.inject_status(make_status("cam", DiagLevel::OK));
    EXPECT_EQ(0, calls);
}

TEST(DiagnosticsAlert, CallbackNotFiredWhenLevelUnchanged)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("x", DiagLevel::Warn));

    int calls = 0;
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls; });

    panel.inject_status(make_status("x", DiagLevel::Warn));
    EXPECT_EQ(0, calls);
}

TEST(DiagnosticsAlert, CallbackFiredForEachComponent)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("a", DiagLevel::OK));
    panel.inject_status(make_status("b", DiagLevel::OK));

    int calls = 0;
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls; });

    panel.inject_array({
        make_status("a", DiagLevel::Error),
        make_status("b", DiagLevel::Warn),
    });
    EXPECT_EQ(2, calls);
}

// ===========================================================================
// Poll + model counts
// ===========================================================================

TEST(DiagnosticsPanel, PollUpdatesCounts)
{
    DiagnosticsPanel panel;
    panel.inject_array({
        make_status("a", DiagLevel::OK),
        make_status("b", DiagLevel::OK),
        make_status("c", DiagLevel::Warn),
        make_status("d", DiagLevel::Error),
        make_status("e", DiagLevel::Stale),
    });
    const auto& m = panel.model();
    EXPECT_EQ(2, m.count_ok);
    EXPECT_EQ(1, m.count_warn);
    EXPECT_EQ(1, m.count_error);
    EXPECT_EQ(1, m.count_stale);
}

TEST(DiagnosticsPanel, PollWithNoDataIsNoOp)
{
    DiagnosticsPanel panel;
    panel.poll();
    EXPECT_EQ(0u, panel.model().components.size());
}

// ===========================================================================
// Stale threshold
// ===========================================================================

TEST(DiagnosticsPanel, DefaultStaleThreshold)
{
    DiagnosticsPanel panel;
    EXPECT_DOUBLE_EQ(5.0, panel.stale_threshold_s());
}

TEST(DiagnosticsPanel, SetStaleThreshold)
{
    DiagnosticsPanel panel;
    panel.set_stale_threshold_s(10.0);
    EXPECT_DOUBLE_EQ(10.0, panel.stale_threshold_s());
}

// ===========================================================================
// Configuration
// ===========================================================================

TEST(DiagnosticsPanel, DefaultTopic)
{
    DiagnosticsPanel panel;
    EXPECT_EQ("diagnostics", panel.topic());
}

TEST(DiagnosticsPanel, SetTopic)
{
    DiagnosticsPanel panel;
    panel.set_topic("/diagnostics_agg");
    EXPECT_EQ("/diagnostics_agg", panel.topic());
}

TEST(DiagnosticsPanel, DefaultTitle)
{
    DiagnosticsPanel panel;
    EXPECT_EQ("Diagnostics", panel.title());
}

TEST(DiagnosticsPanel, SetTitle)
{
    DiagnosticsPanel panel;
    panel.set_title("Robot Diagnostics");
    EXPECT_EQ("Robot Diagnostics", panel.title());
}

TEST(DiagnosticsPanel, DefaultRingDepth)
{
    DiagnosticsPanel panel;
    EXPECT_EQ(256u, panel.ring_depth());
}

TEST(DiagnosticsPanel, SetRingDepth)
{
    DiagnosticsPanel panel;
    panel.set_ring_depth(512);
    EXPECT_EQ(512u, panel.ring_depth());
}

// ===========================================================================
// Running state (without ROS2)
// ===========================================================================

TEST(DiagnosticsPanel, NotRunningAtConstruction)
{
    DiagnosticsPanel panel;
    EXPECT_FALSE(panel.is_running());
}

TEST(DiagnosticsPanel, StartWithoutNodeReturnsFalse)
{
    // SPECTRA_USE_ROS2 may or may not be defined; with no node set the
    // test-mode path allocates the ring and returns true in non-ROS builds.
    // We just verify is_running() matches the return value.
    DiagnosticsPanel panel;
#ifdef SPECTRA_USE_ROS2
    // node_ is nullptr → start() returns false
    EXPECT_FALSE(panel.start());
    EXPECT_FALSE(panel.is_running());
#else
    // No ROS2: start() always succeeds
    EXPECT_TRUE(panel.start());
    EXPECT_TRUE(panel.is_running());
    panel.stop();
    EXPECT_FALSE(panel.is_running());
#endif
}

TEST(DiagnosticsPanel, StopIdempotent)
{
    DiagnosticsPanel panel;
    panel.stop();
    panel.stop();
    EXPECT_FALSE(panel.is_running());
}

// ===========================================================================
// PendingRaw (ring buffer)
// ===========================================================================

TEST(DiagnosticsPanel, PendingRawZeroBeforeStart)
{
    DiagnosticsPanel panel;
    EXPECT_EQ(0u, panel.pending_raw());
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(DiagnosticsPanel, InjectEmptyArray)
{
    DiagnosticsPanel panel;
    panel.inject_array({});
    EXPECT_EQ(0u, panel.model().components.size());
}

TEST(DiagnosticsPanel, InjectManyComponents)
{
    DiagnosticsPanel        panel;
    std::vector<DiagStatus> statuses;
    for (int i = 0; i < 100; ++i)
        statuses.push_back(make_status("comp_" + std::to_string(i), DiagLevel::OK));
    panel.inject_array(statuses);
    EXPECT_EQ(100u, panel.model().components.size());
    EXPECT_EQ(100u, panel.model().order.size());
}

TEST(DiagnosticsPanel, SparklineGrowsWithUpdates)
{
    DiagnosticsPanel panel;
    for (int i = 0; i < 10; ++i)
        panel.inject_status(make_status("motor", DiagLevel::OK));
    const auto& comp = panel.model().components.at("motor");
    EXPECT_EQ(10u, comp.history.size());
}

TEST(DiagnosticsPanel, SparklineCapEnforced)
{
    DiagnosticsPanel panel;
    for (size_t i = 0; i < DiagComponent::MAX_SPARK * 2; ++i)
        panel.inject_status(make_status("motor", DiagLevel::OK));
    const auto& comp = panel.model().components.at("motor");
    EXPECT_EQ(DiagComponent::MAX_SPARK, comp.history.size());
}

TEST(DiagnosticsPanel, MultipleAlertCallbacksOnlyLastWins)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("x", DiagLevel::OK));

    int calls_first = 0;
    int calls_last  = 0;
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls_first; });
    panel.set_alert_callback([&](const std::string&, DiagLevel) { ++calls_last; });

    panel.inject_status(make_status("x", DiagLevel::Error));
    EXPECT_EQ(0, calls_first);
    EXPECT_EQ(1, calls_last);
}

TEST(DiagnosticsPanel, ParseRoundTripMultipleKV)
{
    DiagStatus s      = make_status_kv("sensor",
                                  DiagLevel::Warn,
                                       {
                                      {"temperature", "85.3"},
                                      {"voltage", "11.8"},
                                      {"current", "2.1"},
                                      {"rpm", "3000"},
                                      {"status_code", "0x04"},
                                  });
    auto       blob   = build_diag_array({s});
    auto       result = DiagnosticsPanel::parse_diag_array(blob.data(), blob.size(), 0);
    ASSERT_EQ(1u, result.size());
    ASSERT_EQ(5u, result[0].values.size());
    EXPECT_EQ("temperature", result[0].values[0].key);
    EXPECT_EQ("85.3", result[0].values[0].value);
    EXPECT_EQ("status_code", result[0].values[4].key);
    EXPECT_EQ("0x04", result[0].values[4].value);
}

TEST(DiagnosticsPanel, ModelOrderPreservesInsertionOrder)
{
    DiagnosticsPanel panel;
    panel.inject_status(make_status("z_comp", DiagLevel::OK));
    panel.inject_status(make_status("a_comp", DiagLevel::OK));
    panel.inject_status(make_status("m_comp", DiagLevel::OK));
    const auto& order = panel.model().order;
    ASSERT_EQ(3u, order.size());
    EXPECT_EQ("z_comp", order[0]);
    EXPECT_EQ("a_comp", order[1]);
    EXPECT_EQ("m_comp", order[2]);
}
