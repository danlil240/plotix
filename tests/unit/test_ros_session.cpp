// test_ros_session.cpp — Unit tests for RosSession save/load (G3).
//
// All tests are pure C++ logic — no ROS2 runtime, no ImGui, no GPU.
// Tests cover: JSON serialization, deserialization, round-trips, recent list,
// auto-save, path helpers, edge cases.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "ros_session.hpp"

namespace ros2 = spectra::adapters::ros2;
using ros2::RosSession;
using ros2::RosSessionManager;
using ros2::SubscriptionEntry;
using ros2::DisplaySessionEntry;
using ros2::ExpressionEntry;
using ros2::ExpressionPresetEntry;
using ros2::PanelVisibility;
using ros2::RecentEntry;
using ros2::SaveResult;
using ros2::LoadResult;
using ros2::TopicMonitorState;

// ===========================================================================
// Helpers
// ===========================================================================

static std::string tmp_path(const std::string& name)
{
    return std::string("/tmp/spectra_test_session_") + name;
}

static RosSession make_session()
{
    RosSession s;
    s.node_name     = "test_node";
    s.node_ns       = "/ns";
    s.layout        = "default";
    s.subplot_rows  = 3;
    s.subplot_cols  = 1;
    s.time_window_s = 60.0;
    s.description   = "Test session";

    SubscriptionEntry sub;
    sub.topic         = "/imu";
    sub.field_path    = "linear_acceleration.x";
    sub.type_name     = "sensor_msgs/msg/Imu";
    sub.subplot_slot  = 1;
    sub.time_window_s = 30.0;
    sub.scroll_paused = false;
    s.subscriptions.push_back(sub);

    ExpressionEntry expr;
    expr.expression   = "sqrt($a.x^2 + $a.y^2)";
    expr.label        = "magnitude";
    expr.subplot_slot = 2;
    ExpressionEntry::VarBinding b1;
    b1.variable   = "$a.x";
    b1.topic      = "/imu";
    b1.field_path = "linear_acceleration.x";
    expr.bindings.push_back(b1);
    s.expressions.push_back(expr);

    ExpressionPresetEntry preset;
    preset.name       = "My Preset";
    preset.expression = "abs($val)";
    preset.variables  = {"$val"};
    s.expression_presets.push_back(preset);

    DisplaySessionEntry display;
    display.type_id     = "grid";
    display.topic       = "";
    display.enabled     = true;
    display.config_blob = "cell_size=0.500;cell_count=42;plane=xy";
    s.displays.push_back(display);

    s.fixed_frame            = "world";
    s.camera_pose.azimuth    = 135.0;
    s.camera_pose.elevation  = 22.5;
    s.camera_pose.distance   = 14.0;
    s.camera_pose.target     = {1.0, 2.0, 3.0};
    s.camera_pose.projection = "orthographic";
    s.camera_pose.fov        = 55.0;
    s.scene_background_color = {0.15, 0.18, 0.24, 1.0};

    s.panels.set_visible("ros.topic_list", true);
    s.panels.set_visible("ros.topic_echo", false);
    s.panels.set_visible("ros.topic_stats", true);
    s.panels.set_visible("ros.plot_area", true);
    s.panels.set_visible("ros.bag_info", false);
    s.panels.set_visible("ros.displays", true);
    s.panels.set_visible("ros.scene_viewport", true);
    s.panels.set_visible("ros.inspector", true);

    s.topic_monitor.show_type = false;
    s.topic_monitor.show_hz   = true;
    s.topic_monitor.show_pubs = false;
    s.topic_monitor.show_subs = true;
    s.topic_monitor.show_bw   = false;

    return s;
}

// ===========================================================================
// Suite 1: JSON escape
// ===========================================================================

TEST(JsonEscape, Empty)
{
    EXPECT_EQ(RosSessionManager::json_escape(""), "");
}

TEST(JsonEscape, PlainAscii)
{
    EXPECT_EQ(RosSessionManager::json_escape("hello"), "hello");
}

TEST(JsonEscape, DoubleQuote)
{
    EXPECT_EQ(RosSessionManager::json_escape("say \"hi\""), "say \\\"hi\\\"");
}

TEST(JsonEscape, Backslash)
{
    EXPECT_EQ(RosSessionManager::json_escape("a\\b"), "a\\\\b");
}

TEST(JsonEscape, Newline)
{
    EXPECT_EQ(RosSessionManager::json_escape("line1\nline2"), "line1\\nline2");
}

TEST(JsonEscape, Tab)
{
    EXPECT_EQ(RosSessionManager::json_escape("col1\tcol2"), "col1\\tcol2");
}

TEST(JsonEscape, MixedSpecial)
{
    std::string s = "path/to\\\"file\"\n";
    std::string e = RosSessionManager::json_escape(s);
    EXPECT_NE(e.find("\\\\"), std::string::npos);
    EXPECT_NE(e.find("\\\""), std::string::npos);
    EXPECT_NE(e.find("\\n"), std::string::npos);
}

// ===========================================================================
// Suite 2: json_get_* helpers
// ===========================================================================

TEST(JsonGetString, BasicKey)
{
    std::string json = R"({"key": "value"})";
    EXPECT_EQ(RosSessionManager::json_get_string(json, "key"), "value");
}

TEST(JsonGetString, MissingKey)
{
    std::string json = R"({"other": "val"})";
    EXPECT_EQ(RosSessionManager::json_get_string(json, "key"), "");
}

TEST(JsonGetString, EscapedValue)
{
    std::string json = "{\"key\": \"say \\\"hi\\\"\"}";
    EXPECT_EQ(RosSessionManager::json_get_string(json, "key"), "say \"hi\"");
}

TEST(JsonGetInt, BasicInt)
{
    std::string json = R"({"n": 42})";
    EXPECT_EQ(RosSessionManager::json_get_int(json, "n", 0), 42);
}

TEST(JsonGetInt, NegativeInt)
{
    std::string json = R"({"n": -7})";
    EXPECT_EQ(RosSessionManager::json_get_int(json, "n", 0), -7);
}

TEST(JsonGetInt, Missing)
{
    std::string json = R"({"x": 1})";
    EXPECT_EQ(RosSessionManager::json_get_int(json, "n", 99), 99);
}

TEST(JsonGetDouble, BasicDouble)
{
    std::string json = R"({"v": 3.14})";
    EXPECT_DOUBLE_EQ(RosSessionManager::json_get_double(json, "v", 0.0), 3.14);
}

TEST(JsonGetDouble, Integer)
{
    std::string json = R"({"v": 30})";
    EXPECT_DOUBLE_EQ(RosSessionManager::json_get_double(json, "v", 0.0), 30.0);
}

TEST(JsonGetDouble, Missing)
{
    std::string json = R"({"x": 1.0})";
    EXPECT_DOUBLE_EQ(RosSessionManager::json_get_double(json, "v", 7.5), 7.5);
}

TEST(JsonGetBool, True)
{
    std::string json = R"({"flag": true})";
    EXPECT_TRUE(RosSessionManager::json_get_bool(json, "flag", false));
}

TEST(JsonGetBool, False)
{
    std::string json = R"({"flag": false})";
    EXPECT_FALSE(RosSessionManager::json_get_bool(json, "flag", true));
}

TEST(JsonGetBool, Missing)
{
    std::string json = R"({"x": 1})";
    EXPECT_TRUE(RosSessionManager::json_get_bool(json, "flag", true));
    EXPECT_FALSE(RosSessionManager::json_get_bool(json, "flag", false));
}

// ===========================================================================
// Suite 3: ISO-8601 timestamp
// ===========================================================================

TEST(Iso8601, NonEmpty)
{
    std::string ts = RosSessionManager::current_iso8601();
    EXPECT_FALSE(ts.empty());
}

TEST(Iso8601, EndsWithZ)
{
    std::string ts = RosSessionManager::current_iso8601();
    EXPECT_EQ(ts.back(), 'Z');
}

TEST(Iso8601, HasTSeparator)
{
    std::string ts = RosSessionManager::current_iso8601();
    EXPECT_NE(ts.find('T'), std::string::npos);
}

TEST(Iso8601, Length20)
{
    std::string ts = RosSessionManager::current_iso8601();
    EXPECT_EQ(ts.size(), 20u);   // "YYYY-MM-DDTHH:MM:SSZ"
}

// ===========================================================================
// Suite 4: serialize / deserialize round-trip
// ===========================================================================

TEST(RoundTrip, EmptySession)
{
    RosSession s;
    s.version        = ros2::SESSION_FORMAT_VERSION;
    std::string json = RosSessionManager::serialize(s);

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;
    EXPECT_EQ(out.version, ros2::SESSION_FORMAT_VERSION);
}

TEST(RoundTrip, NodeName)
{
    RosSession s;
    s.node_name      = "my_robot_node";
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.node_name, "my_robot_node");
}

TEST(RoundTrip, LayoutAndGrid)
{
    RosSession s;
    s.layout         = "monitor";
    s.subplot_rows   = 2;
    s.subplot_cols   = 3;
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.layout, "monitor");
    EXPECT_EQ(out.subplot_rows, 2);
    EXPECT_EQ(out.subplot_cols, 3);
}

TEST(RoundTrip, TimeWindow)
{
    RosSession s;
    s.time_window_s  = 120.0;
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_DOUBLE_EQ(out.time_window_s, 120.0);
}

TEST(RoundTrip, PruneSettings)
{
    RosSession s;
    s.pruning_enabled = false;
    s.prune_buffer_s  = 12.0;
    auto        json  = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.pruning_enabled, s.pruning_enabled);
    EXPECT_DOUBLE_EQ(out.prune_buffer_s, s.prune_buffer_s);
}

TEST(RoundTrip, SingleSubscription)
{
    RosSession        s;
    SubscriptionEntry sub;
    sub.topic         = "/cmd_vel";
    sub.field_path    = "linear.x";
    sub.type_name     = "geometry_msgs/msg/Twist";
    sub.subplot_slot  = 3;
    sub.time_window_s = 45.0;
    sub.scroll_paused = true;
    s.subscriptions.push_back(sub);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    ASSERT_EQ(out.subscriptions.size(), 1u);
    EXPECT_EQ(out.subscriptions[0].topic, "/cmd_vel");
    EXPECT_EQ(out.subscriptions[0].field_path, "linear.x");
    EXPECT_EQ(out.subscriptions[0].type_name, "geometry_msgs/msg/Twist");
    EXPECT_EQ(out.subscriptions[0].subplot_slot, 3);
    EXPECT_DOUBLE_EQ(out.subscriptions[0].time_window_s, 45.0);
    EXPECT_TRUE(out.subscriptions[0].scroll_paused);
}

TEST(RoundTrip, SubscriptionCustomAxisConfig)
{
    RosSession        s;
    SubscriptionEntry sub;
    sub.topic         = "/cmd_vel";
    sub.field_path    = "linear.x";
    sub.type_name     = "geometry_msgs/msg/Twist";
    sub.subplot_slot  = 3;
    sub.time_window_s = 45.0;
    sub.scroll_paused = true;
    sub.axis_mode     = ros2::AxisMode::CustomAxes;
    sub.x_field_path  = "linear.x";
    sub.y_field_path  = "angular.z";
    s.subscriptions.push_back(sub);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    ASSERT_EQ(out.subscriptions.size(), 1u);
    EXPECT_EQ(out.subscriptions[0].axis_mode, ros2::AxisMode::CustomAxes);
    EXPECT_EQ(out.subscriptions[0].x_field_path, "linear.x");
    EXPECT_EQ(out.subscriptions[0].y_field_path, "angular.z");
}

TEST(RoundTrip, MultipleSubscriptions)
{
    RosSession s;
    for (int i = 0; i < 5; ++i)
    {
        SubscriptionEntry sub;
        sub.topic        = "/topic_" + std::to_string(i);
        sub.field_path   = "data";
        sub.subplot_slot = i + 1;
        s.subscriptions.push_back(sub);
    }

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    ASSERT_EQ(out.subscriptions.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(out.subscriptions[i].topic, "/topic_" + std::to_string(i));
        EXPECT_EQ(out.subscriptions[i].subplot_slot, i + 1);
    }
}

TEST(RoundTrip, ExpressionWithBindings)
{
    RosSession      s;
    ExpressionEntry expr;
    expr.expression   = "sqrt($a.x^2 + $a.y^2)";
    expr.label        = "acc_mag";
    expr.subplot_slot = 1;
    ExpressionEntry::VarBinding b;
    b.variable   = "$a.x";
    b.topic      = "/imu";
    b.field_path = "linear_acceleration.x";
    expr.bindings.push_back(b);
    s.expressions.push_back(expr);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    ASSERT_EQ(out.expressions.size(), 1u);
    EXPECT_EQ(out.expressions[0].expression, "sqrt($a.x^2 + $a.y^2)");
    EXPECT_EQ(out.expressions[0].label, "acc_mag");
    EXPECT_EQ(out.expressions[0].subplot_slot, 1);
    ASSERT_EQ(out.expressions[0].bindings.size(), 1u);
    EXPECT_EQ(out.expressions[0].bindings[0].variable, "$a.x");
    EXPECT_EQ(out.expressions[0].bindings[0].topic, "/imu");
    EXPECT_EQ(out.expressions[0].bindings[0].field_path, "linear_acceleration.x");
}

TEST(RoundTrip, ExpressionPresets)
{
    RosSession            s;
    ExpressionPresetEntry p;
    p.name       = "magnitude";
    p.expression = "sqrt($x^2 + $y^2)";
    p.variables  = {"$x", "$y"};
    s.expression_presets.push_back(p);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    ASSERT_EQ(out.expression_presets.size(), 1u);
    EXPECT_EQ(out.expression_presets[0].name, "magnitude");
    EXPECT_EQ(out.expression_presets[0].expression, "sqrt($x^2 + $y^2)");
    ASSERT_EQ(out.expression_presets[0].variables.size(), 2u);
    EXPECT_EQ(out.expression_presets[0].variables[0], "$x");
    EXPECT_EQ(out.expression_presets[0].variables[1], "$y");
}

TEST(RoundTrip, PanelVisibility)
{
    RosSession s;
    s.panels.set_visible("ros.topic_list", false);
    s.panels.set_visible("ros.topic_echo", true);
    s.panels.set_visible("ros.topic_stats", false);
    s.panels.set_visible("ros.plot_area", true);
    s.panels.set_visible("ros.bag_info", true);
    s.panels.set_visible("ros.displays", true);
    s.panels.set_visible("ros.scene_viewport", true);
    s.panels.set_visible("ros.inspector", true);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    EXPECT_FALSE(out.panels.visible("ros.topic_list"));
    EXPECT_TRUE(out.panels.visible("ros.topic_echo"));
    EXPECT_FALSE(out.panels.visible("ros.topic_stats"));
    EXPECT_TRUE(out.panels.visible("ros.plot_area"));
    EXPECT_TRUE(out.panels.visible("ros.bag_info"));
    EXPECT_TRUE(out.panels.visible("ros.displays"));
    EXPECT_TRUE(out.panels.visible("ros.scene_viewport"));
    EXPECT_TRUE(out.panels.visible("ros.inspector"));
}

TEST(RoundTrip, FixedFrameAndDisplays)
{
    RosSession s;
    s.fixed_frame            = "base_link";
    s.camera_pose.azimuth    = 90.0;
    s.camera_pose.elevation  = 15.0;
    s.camera_pose.distance   = 8.5;
    s.camera_pose.target     = {4.0, 5.0, 6.0};
    s.camera_pose.projection = "perspective";
    s.camera_pose.fov        = 70.0;
    s.scene_background_color = {0.25, 0.20, 0.15, 0.95};

    DisplaySessionEntry display;
    display.type_id     = "grid";
    display.topic       = "";
    display.enabled     = false;
    display.config_blob = "cell_size=1.000;cell_count=20;plane=xz";
    s.displays.push_back(display);

    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    EXPECT_EQ(out.fixed_frame, "base_link");
    EXPECT_DOUBLE_EQ(out.camera_pose.azimuth, 90.0);
    EXPECT_DOUBLE_EQ(out.camera_pose.elevation, 15.0);
    EXPECT_DOUBLE_EQ(out.camera_pose.distance, 8.5);
    EXPECT_EQ(out.camera_pose.target[0], 4.0);
    EXPECT_EQ(out.camera_pose.target[1], 5.0);
    EXPECT_EQ(out.camera_pose.target[2], 6.0);
    EXPECT_EQ(out.camera_pose.projection, "perspective");
    EXPECT_DOUBLE_EQ(out.camera_pose.fov, 70.0);
    EXPECT_DOUBLE_EQ(out.scene_background_color[0], 0.25);
    EXPECT_DOUBLE_EQ(out.scene_background_color[3], 0.95);
    ASSERT_EQ(out.displays.size(), 1u);
    EXPECT_EQ(out.displays[0].type_id, "grid");
    EXPECT_FALSE(out.displays[0].enabled);
    EXPECT_NE(out.displays[0].config_blob.find("plane=xz"), std::string::npos);
}

TEST(RoundTrip, TopicMonitorColumnVisibility)
{
    RosSession s;
    s.topic_monitor.show_type = false;
    s.topic_monitor.show_hz   = false;
    s.topic_monitor.show_pubs = true;
    s.topic_monitor.show_subs = false;
    s.topic_monitor.show_bw   = true;

    const auto  json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));

    EXPECT_FALSE(out.topic_monitor.show_type);
    EXPECT_FALSE(out.topic_monitor.show_hz);
    EXPECT_TRUE(out.topic_monitor.show_pubs);
    EXPECT_FALSE(out.topic_monitor.show_subs);
    EXPECT_TRUE(out.topic_monitor.show_bw);
}

TEST(RoundTrip, FullSession)
{
    RosSession  s    = make_session();
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;

    EXPECT_EQ(out.node_name, s.node_name);
    EXPECT_EQ(out.node_ns, s.node_ns);
    EXPECT_EQ(out.layout, s.layout);
    EXPECT_EQ(out.subplot_rows, s.subplot_rows);
    EXPECT_EQ(out.subplot_cols, s.subplot_cols);
    EXPECT_DOUBLE_EQ(out.time_window_s, s.time_window_s);
    EXPECT_EQ(out.pruning_enabled, s.pruning_enabled);
    EXPECT_DOUBLE_EQ(out.prune_buffer_s, s.prune_buffer_s);
    EXPECT_EQ(out.description, s.description);
    EXPECT_EQ(out.fixed_frame, s.fixed_frame);
    EXPECT_DOUBLE_EQ(out.camera_pose.azimuth, s.camera_pose.azimuth);
    EXPECT_DOUBLE_EQ(out.camera_pose.elevation, s.camera_pose.elevation);
    EXPECT_DOUBLE_EQ(out.camera_pose.distance, s.camera_pose.distance);
    EXPECT_EQ(out.camera_pose.target, s.camera_pose.target);
    EXPECT_EQ(out.camera_pose.projection, s.camera_pose.projection);
    EXPECT_DOUBLE_EQ(out.camera_pose.fov, s.camera_pose.fov);
    EXPECT_EQ(out.scene_background_color, s.scene_background_color);
    EXPECT_EQ(out.subscriptions.size(), s.subscriptions.size());
    EXPECT_EQ(out.expressions.size(), s.expressions.size());
    EXPECT_EQ(out.expression_presets.size(), s.expression_presets.size());
    EXPECT_EQ(out.displays.size(), s.displays.size());
}

TEST(RoundTrip, Description)
{
    RosSession s;
    s.description    = "My robot session — quad rotor";
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.description, "My robot session — quad rotor");
}

TEST(RoundTrip, SpecialCharsInTopicName)
{
    RosSession        s;
    SubscriptionEntry sub;
    sub.topic      = "/robot_1/sensor\"data";   // embedded quote
    sub.field_path = "value";
    s.subscriptions.push_back(sub);
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    ASSERT_EQ(out.subscriptions.size(), 1u);
    EXPECT_EQ(out.subscriptions[0].topic, "/robot_1/sensor\"data");
}

// ===========================================================================
// Suite 5: Deserialization error handling
// ===========================================================================

TEST(DeserializeErrors, EmptyInput)
{
    RosSession  out;
    std::string err;
    EXPECT_FALSE(RosSessionManager::deserialize("", out, err));
    EXPECT_FALSE(err.empty());
}

TEST(DeserializeErrors, MissingVersion)
{
    RosSession  out;
    std::string err;
    EXPECT_TRUE(RosSessionManager::deserialize("{\"node_name\": \"x\"}", out, err)) << err;
    EXPECT_EQ(out.version, 1);
    EXPECT_EQ(out.node_name, "x");
}

TEST(DeserializeErrors, FutureVersion)
{
    RosSession  out;
    std::string err;
    std::string json = "{\"version\": 9999, \"node_name\": \"x\"}";
    EXPECT_FALSE(RosSessionManager::deserialize(json, out, err));
    EXPECT_NE(err.find("newer"), std::string::npos);
}

TEST(DeserializeErrors, ValidVersion1)
{
    RosSession  out;
    std::string err;
    std::string json = "{\"version\": 1}";
    EXPECT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;
    EXPECT_EQ(out.version, 1);
}

TEST(Serialize, WritesVersion2NestedSchema)
{
    RosSession        s    = make_session();
    const std::string json = RosSessionManager::serialize(s);

    EXPECT_NE(json.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"scene\""), std::string::npos);
    EXPECT_NE(json.find("\"ui\""), std::string::npos);
    EXPECT_NE(json.find("\"fixed_frame\": \"world\""), std::string::npos);
    EXPECT_NE(json.find("\"camera_pose\""), std::string::npos);
    EXPECT_NE(json.find("\"background_color\""), std::string::npos);
    EXPECT_NE(json.find("\"nav_rail\""), std::string::npos);
    EXPECT_NE(json.find("\"topic_monitor\""), std::string::npos);
    EXPECT_NE(json.find("\"ros.inspector\": true"), std::string::npos);
    EXPECT_EQ(json.find("\"nav_rail_expanded\""), std::string::npos);
    EXPECT_EQ(json.find("\"nav_rail_width\""), std::string::npos);
}

TEST(SessionMigration, Version1LoadSerializesToVersion2)
{
    const std::string v1 = R"({
  "version": 1,
  "node_name": "legacy_nav",
  "layout": "rviz-plot",
  "fixed_frame": "map",
  "subscriptions": [
    {"topic":"/cmd_vel","field_path":"linear.x","type_name":"geometry_msgs/msg/Twist","subplot_slot":1}
  ],
  "displays": [
    {"type_id":"grid","topic":"","enabled":true,"config_blob":""}
  ],
  "panels": {
    "scene_viewport": true,
    "displays_panel": true
  }
})";

    RosSession  loaded;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(v1, loaded, err)) << err;
    EXPECT_EQ(loaded.version, 1);
    ASSERT_EQ(loaded.subscriptions.size(), 1u);
    ASSERT_EQ(loaded.displays.size(), 1u);

    const std::string v2_json = RosSessionManager::serialize(loaded);
    EXPECT_NE(v2_json.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(v2_json.find("legacy_nav"), std::string::npos);
    EXPECT_NE(v2_json.find("/cmd_vel"), std::string::npos);

    RosSession  roundtrip;
    std::string err2;
    ASSERT_TRUE(RosSessionManager::deserialize(v2_json, roundtrip, err2)) << err2;
    EXPECT_EQ(roundtrip.version, 2);
    ASSERT_EQ(roundtrip.subscriptions.size(), 1u);
    ASSERT_EQ(roundtrip.displays.size(), 1u);
    EXPECT_EQ(roundtrip.fixed_frame, "map");
}

TEST(Deserialize, LegacyVersion1SessionRemainsSupported)
{
    const std::string json = R"({
  "version": 1,
  "node_name": "legacy_node",
  "layout": "rviz",
  "subplot_rows": 2,
  "subplot_cols": 3,
  "time_window_s": 15.5,
  "nav_rail_expanded": true,
  "nav_rail_width": 260.0,
  "fixed_frame": "map",
  "subscriptions": [
    {"topic":"/imu","field_path":"linear_acceleration.x","type_name":"sensor_msgs/msg/Imu","subplot_slot":1,"time_window_s":15.5,"scroll_paused":true}
  ],
  "displays": [
    {"type_id":"grid","topic":"","enabled":false,"config_blob":"cell_size=1.000;cell_count=20;plane=xz"}
  ],
  "panels": {
    "topic_list": false,
    "displays_panel": true,
    "scene_viewport": true,
    "inspector_panel": true,
    "nav_rail": false
  },
  "imgui_layout": "[window][legacy]"
})";

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;
    EXPECT_EQ(out.version, 1);
    EXPECT_EQ(out.node_name, "legacy_node");
    EXPECT_EQ(out.layout, "rviz");
    EXPECT_EQ(out.fixed_frame, "map");
    EXPECT_TRUE(out.nav_rail_expanded);
    EXPECT_DOUBLE_EQ(out.nav_rail_width, 260.0);
    ASSERT_EQ(out.subscriptions.size(), 1u);
    EXPECT_EQ(out.subscriptions[0].topic, "/imu");
    ASSERT_EQ(out.displays.size(), 1u);
    EXPECT_EQ(out.displays[0].type_id, "grid");
    EXPECT_FALSE(out.displays[0].enabled);
    EXPECT_TRUE(out.panels.visible("ros.displays"));
    EXPECT_TRUE(out.panels.visible("ros.scene_viewport"));
    EXPECT_TRUE(out.panels.visible("ros.inspector"));
    EXPECT_FALSE(out.panels.nav_rail);
    EXPECT_EQ(out.imgui_ini_data, "[window][legacy]");
}

TEST(Deserialize, MissingAxisFieldsDefaultToTimeSeries)
{
    const std::string json = R"({
  "version": 2,
  "subscriptions": [
    {"topic":"/imu","field_path":"linear_acceleration.x","type_name":"sensor_msgs/msg/Imu","subplot_slot":1}
  ]
})";

    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err)) << err;
    ASSERT_EQ(out.subscriptions.size(), 1u);
    EXPECT_EQ(out.subscriptions[0].axis_mode, ros2::AxisMode::TimeSeries);
    EXPECT_TRUE(out.subscriptions[0].x_field_path.empty());
    EXPECT_TRUE(out.subscriptions[0].y_field_path.empty());
}

// ===========================================================================
// Suite 6: Save / load to disk
// ===========================================================================

TEST(SaveLoad, SaveAndLoadRoundTrip)
{
    RosSessionManager mgr;
    RosSession        s    = make_session();
    std::string       path = tmp_path("save_load.spectra-ros-session");

    SaveResult sr = mgr.save(s, path);
    ASSERT_TRUE(sr.ok) << sr.error;
    EXPECT_EQ(sr.path, path);
    EXPECT_TRUE(std::filesystem::exists(path));

    LoadResult lr = mgr.load(path);
    ASSERT_TRUE(lr.ok) << lr.error;
    EXPECT_EQ(lr.session.node_name, s.node_name);
    EXPECT_EQ(lr.session.layout, s.layout);
    EXPECT_EQ(lr.session.subscriptions.size(), s.subscriptions.size());

    std::filesystem::remove(path);
}

TEST(SaveLoad, SaveStampsTimestamp)
{
    RosSessionManager mgr;
    RosSession        s;
    std::string       path = tmp_path("stamp.spectra-ros-session");

    SaveResult sr = mgr.save(s, path);
    ASSERT_TRUE(sr.ok);

    LoadResult lr = mgr.load(path);
    ASSERT_TRUE(lr.ok);
    EXPECT_FALSE(lr.session.saved_at.empty());
    EXPECT_EQ(lr.session.saved_at.back(), 'Z');

    std::filesystem::remove(path);
}

TEST(SaveLoad, EmptyPathFails)
{
    RosSessionManager mgr;
    RosSession        s;
    SaveResult        sr = mgr.save(s, "");
    EXPECT_FALSE(sr.ok);
    EXPECT_FALSE(sr.error.empty());
}

TEST(SaveLoad, LoadNonexistentFails)
{
    RosSessionManager mgr;
    LoadResult        lr = mgr.load("/tmp/does_not_exist_xyz.spectra-ros-session");
    EXPECT_FALSE(lr.ok);
    EXPECT_FALSE(lr.error.empty());
}

TEST(SaveLoad, SaveSetsLastPath)
{
    RosSessionManager mgr;
    RosSession        s;
    std::string       path = tmp_path("last_path.spectra-ros-session");
    mgr.save(s, path);
    EXPECT_EQ(mgr.last_path(), path);
    std::filesystem::remove(path);
}

TEST(SaveLoad, LoadSetsLastPath)
{
    RosSessionManager mgr;
    RosSession        s;
    std::string       path = tmp_path("load_last_path.spectra-ros-session");
    mgr.save(s, path);
    mgr.set_last_path("");   // reset

    mgr.load(path);
    EXPECT_EQ(mgr.last_path(), path);
    std::filesystem::remove(path);
}

// ===========================================================================
// Suite 7: auto_save
// ===========================================================================

TEST(AutoSave, NoLastPathFails)
{
    RosSessionManager mgr;
    RosSession        s;
    SaveResult        sr = mgr.auto_save(s);
    EXPECT_FALSE(sr.ok);
    EXPECT_FALSE(sr.error.empty());
}

TEST(AutoSave, AutoSaveToLastPath)
{
    RosSessionManager mgr;
    RosSession        s;
    s.node_name      = "auto_save_node";
    std::string path = tmp_path("auto_save.spectra-ros-session");

    mgr.set_last_path(path);
    SaveResult sr = mgr.auto_save(s);
    ASSERT_TRUE(sr.ok) << sr.error;
    EXPECT_TRUE(std::filesystem::exists(path));

    LoadResult lr = mgr.load(path);
    ASSERT_TRUE(lr.ok);
    EXPECT_EQ(lr.session.node_name, "auto_save_node");

    std::filesystem::remove(path);
}

TEST(AutoSave, SaveThenAutoSaveUpdatesFile)
{
    RosSessionManager mgr;
    RosSession        s1, s2;
    s1.node_name     = "first";
    s2.node_name     = "second";
    std::string path = tmp_path("auto_save2.spectra-ros-session");

    mgr.save(s1, path);
    s2.node_name = "second";
    mgr.auto_save(s2);

    LoadResult lr = mgr.load(path);
    ASSERT_TRUE(lr.ok);
    EXPECT_EQ(lr.session.node_name, "second");

    std::filesystem::remove(path);
}

// ===========================================================================
// Suite 8: Recent list serialization
// ===========================================================================

TEST(RecentList, SerializeEmpty)
{
    std::string json = RosSessionManager::serialize_recent({});
    EXPECT_NE(json.find('['), std::string::npos);
    EXPECT_NE(json.find(']'), std::string::npos);
}

TEST(RecentList, SerializeDeserializeRoundTrip)
{
    std::vector<RecentEntry> entries;
    RecentEntry              e1;
    e1.path     = "/home/user/session1.spectra-ros-session";
    e1.node     = "node1";
    e1.saved_at = "2026-03-05T10:00:00Z";
    entries.push_back(e1);

    RecentEntry e2;
    e2.path     = "/home/user/session2.spectra-ros-session";
    e2.node     = "node2";
    e2.saved_at = "2026-03-05T11:00:00Z";
    entries.push_back(e2);

    std::string json   = RosSessionManager::serialize_recent(entries);
    auto        loaded = RosSessionManager::deserialize_recent(json);

    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].path, e1.path);
    EXPECT_EQ(loaded[0].node, e1.node);
    EXPECT_EQ(loaded[0].saved_at, e1.saved_at);
    EXPECT_EQ(loaded[1].path, e2.path);
}

TEST(RecentList, DeserializeEmptyJson)
{
    auto loaded = RosSessionManager::deserialize_recent("[]");
    EXPECT_TRUE(loaded.empty());
}

TEST(RecentList, DeserializeInvalidJson)
{
    auto loaded = RosSessionManager::deserialize_recent("not json");
    EXPECT_TRUE(loaded.empty());
}

// ===========================================================================
// Suite 9: Recent list management (push_recent / remove_recent)
// ===========================================================================

// Use a custom HOME dir to avoid touching the real ~/.config
struct RecentFixture : public ::testing::Test
{
    std::string old_home;
    std::string fake_home;

    void SetUp() override
    {
        fake_home = "/tmp/spectra_test_home_"
                    + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(fake_home);
        old_home = std::getenv("HOME") ? std::getenv("HOME") : "";
        setenv("HOME", fake_home.c_str(), 1);
    }

    void TearDown() override
    {
        setenv("HOME", old_home.c_str(), 1);
        std::filesystem::remove_all(fake_home);
    }
};

TEST_F(RecentFixture, PushAndLoadRecent)
{
    RosSessionManager mgr;
    mgr.push_recent("/path/to/session.spectra-ros-session", "my_node", "2026-03-05T12:00:00Z");

    auto list = mgr.load_recent();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].path, "/path/to/session.spectra-ros-session");
    EXPECT_EQ(list[0].node, "my_node");
}

TEST_F(RecentFixture, PushPromotesExisting)
{
    RosSessionManager mgr;
    mgr.push_recent("/path/a.spectra-ros-session", "a", "2026-01-01T00:00:00Z");
    mgr.push_recent("/path/b.spectra-ros-session", "b", "2026-01-02T00:00:00Z");
    mgr.push_recent("/path/a.spectra-ros-session", "a", "2026-01-03T00:00:00Z");

    auto list = mgr.load_recent();
    // 'a' should be at front after re-push, list should have 2 unique entries
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].path, "/path/a.spectra-ros-session");
}

TEST_F(RecentFixture, TrimsToMaxRecent)
{
    RosSessionManager mgr;
    for (int i = 0; i < RosSessionManager::MAX_RECENT + 5; ++i)
    {
        mgr.push_recent("/path/s" + std::to_string(i) + ".spectra-ros-session",
                        "node" + std::to_string(i),
                        "2026-01-01T00:00:00Z");
    }
    auto list = mgr.load_recent();
    EXPECT_LE(static_cast<int>(list.size()), RosSessionManager::MAX_RECENT);
}

TEST_F(RecentFixture, RemoveRecent)
{
    RosSessionManager mgr;
    mgr.push_recent("/path/a.spectra-ros-session", "a", "");
    mgr.push_recent("/path/b.spectra-ros-session", "b", "");
    mgr.remove_recent("/path/a.spectra-ros-session");

    auto list = mgr.load_recent();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].path, "/path/b.spectra-ros-session");
}

TEST_F(RecentFixture, ClearRecent)
{
    RosSessionManager mgr;
    mgr.push_recent("/path/a.spectra-ros-session", "a", "");
    mgr.push_recent("/path/b.spectra-ros-session", "b", "");
    mgr.clear_recent();

    auto list = mgr.load_recent();
    EXPECT_TRUE(list.empty());
}

TEST_F(RecentFixture, LoadRecentEmpty)
{
    RosSessionManager mgr;
    auto              list = mgr.load_recent();
    EXPECT_TRUE(list.empty());
}

// ===========================================================================
// Suite 10: Path helpers
// ===========================================================================

TEST(PathHelpers, DefaultRecentPathContainsSpectra)
{
    std::string p = RosSessionManager::default_recent_path();
    EXPECT_NE(p.find("spectra"), std::string::npos);
    EXPECT_NE(p.find("ros_recent.json"), std::string::npos);
}

TEST(PathHelpers, DefaultSessionPathContainsNodeName)
{
    std::string p = RosSessionManager::default_session_path("my_robot");
    EXPECT_NE(p.find("my_robot"), std::string::npos);
    EXPECT_NE(p.find(".spectra-ros-session"), std::string::npos);
}

TEST(PathHelpers, DefaultSessionPathSanitisesSlash)
{
    std::string p = RosSessionManager::default_session_path("/ns/my_node");
    EXPECT_EQ(p.find("/ns/my_node"), std::string::npos);
    EXPECT_NE(p.find("_ns_my_node"), std::string::npos);
}

TEST(PathHelpers, DefaultSessionPathEmptyNode)
{
    std::string p = RosSessionManager::default_session_path("");
    EXPECT_NE(p.find("default"), std::string::npos);
}

// ===========================================================================
// Suite 11: SaveResult / LoadResult operator bool
// ===========================================================================

TEST(Results, SaveResultOk)
{
    SaveResult r;
    r.ok = true;
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(Results, SaveResultFail)
{
    SaveResult r;
    r.ok = false;
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(Results, LoadResultOk)
{
    LoadResult r;
    r.ok = true;
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(Results, LoadResultFail)
{
    LoadResult r;
    r.ok = false;
    EXPECT_FALSE(static_cast<bool>(r));
}

// ===========================================================================
// Suite 12: Edge cases
// ===========================================================================

TEST(EdgeCases, EmptySubscriptionsArray)
{
    RosSession  s;
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_TRUE(out.subscriptions.empty());
}

TEST(EdgeCases, EmptyExpressionsArray)
{
    RosSession  s;
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_TRUE(out.expressions.empty());
}

TEST(EdgeCases, MultipleExpressionBindings)
{
    RosSession      s;
    ExpressionEntry expr;
    expr.expression = "atan2($y, $x)";
    for (int i = 0; i < 3; ++i)
    {
        ExpressionEntry::VarBinding b;
        b.variable   = "$v" + std::to_string(i);
        b.topic      = "/t" + std::to_string(i);
        b.field_path = "field";
        expr.bindings.push_back(b);
    }
    s.expressions.push_back(expr);
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    ASSERT_EQ(out.expressions[0].bindings.size(), 3u);
}

TEST(EdgeCases, ZeroSubplotSlotMeansStandalone)
{
    RosSession        s;
    SubscriptionEntry sub;
    sub.topic        = "/data";
    sub.field_path   = "value";
    sub.subplot_slot = 0;
    s.subscriptions.push_back(sub);
    auto        json = RosSessionManager::serialize(s);
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.subscriptions[0].subplot_slot, 0);
}

TEST(EdgeCases, DefaultLayoutOnMissingField)
{
    std::string json = "{\"version\": 1}";
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    EXPECT_EQ(out.layout, "default");
}

TEST(EdgeCases, DefaultPanelsWhenMissing)
{
    std::string json = "{\"version\": 1}";
    RosSession  out;
    std::string err;
    ASSERT_TRUE(RosSessionManager::deserialize(json, out, err));
    // Defaults: topic_list=true, bag_info=false
    EXPECT_TRUE(out.panels.visible("ros.topic_list"));
    EXPECT_FALSE(out.panels.visible("ros.bag_info"));
}

TEST(EdgeCases, SessionExtConst)
{
    EXPECT_STREQ(RosSessionManager::SESSION_EXT, "spectra-ros-session");
}

TEST(EdgeCases, MaxRecentConst)
{
    EXPECT_EQ(RosSessionManager::MAX_RECENT, 10);
}

TEST(EdgeCases, FormatVersionConst)
{
    EXPECT_EQ(ros2::SESSION_FORMAT_VERSION, 2);
}

// ===========================================================================
// Suite: checked-in Phase A session presets
// ===========================================================================

TEST(SessionPresets, TuningPresetLoadsWithPlots)
{
    const std::string path =
        std::string(SPECTRA_SOURCE_DIR) + "/sessions/presets/tuning.spectra-ros-session";
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    RosSessionManager mgr;
    const LoadResult  lr = mgr.load(path);
    ASSERT_TRUE(lr.ok) << lr.error;
    EXPECT_GE(lr.session.subscriptions.size(), 2u);
    EXPECT_EQ(lr.session.layout, "default");
}

TEST(SessionPresets, BagReviewPresetEnablesBagPanels)
{
    const std::string path =
        std::string(SPECTRA_SOURCE_DIR) + "/sessions/presets/bag_review.spectra-ros-session";
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    RosSessionManager mgr;
    const LoadResult  lr = mgr.load(path);
    ASSERT_TRUE(lr.ok) << lr.error;
    EXPECT_TRUE(lr.session.panels.visible("ros.bag_info"));
    EXPECT_TRUE(lr.session.panels.visible("ros.bag_playback"));
    EXPECT_TRUE(lr.session.panels.visible("ros.plot_area"));
}

TEST(SessionPresets, BringupPresetHasCmdVelPlots)
{
    const std::string path =
        std::string(SPECTRA_SOURCE_DIR) + "/sessions/presets/bringup.spectra-ros-session";
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    RosSessionManager mgr;
    const LoadResult  lr = mgr.load(path);
    ASSERT_TRUE(lr.ok) << lr.error;
    ASSERT_GE(lr.session.subscriptions.size(), 2u);
    EXPECT_EQ(lr.session.subscriptions[0].topic, "/cmd_vel");
}
