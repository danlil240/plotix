// ros2_demo.cpp — Comprehensive ROS2 adapter demonstration
//
// This example exercises every completed mission in the Spectra ROS2 adapter plan:
//
//   Phase A (Foundation & Bridge):
//     A1 — CMake scaffolding (this file compiles under SPECTRA_USE_ROS2)
//     A2 — Ros2Bridge: node lifecycle + background spin thread
//     A3 — TopicDiscovery: periodic graph scan, add/remove callbacks
//     A4 — MessageIntrospector: runtime schema + FieldAccessor
//     A5 — GenericSubscriber: SPSC ring buffer subscriptions
//     A6 — End-to-end: bridge → discovery → subscribe → plot
//
//   Phase B (Topic Monitor Panel):
//     B1 — TopicListPanel: live topic tree with Hz/BW columns
//     B2 — TopicEchoPanel: expandable field tree, 100-msg ring, pause/resume
//     B3 — TopicStatsOverlay: avg/min/max Hz, latency, drop detection
//
//   Phase C (Live Plotting):
//     C1 — RosPlotManager: topic/field → Figure/LineSeries bridge, poll()
//     C2 — Auto-scroll time window (via presented_buffer)
//     C4 — SubplotManager: NxM grid, shared X-axis, shared cursor
//
//   Phase F (Advanced ROS2 Tools):
//     F2 — TfTreePanel: /tf + /tf_static subscriber, frame tree, Hz/age badges
//
// Build (requires sourced ROS2 Humble+):
//   cmake -DSPECTRA_USE_ROS2=ON -DSPECTRA_USE_IMGUI=ON -B build-ros2
//   ninja -C build-ros2 ros2_demo
//
// Run:
//   # In separate terminals (or background):
//   ros2 topic pub /imu sensor_msgs/msg/Imu "{}" --rate 50 &
//   ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{}" --rate 20 &
//   ./build-ros2/examples/ros2_demo
//
// Layout:
//   Left panel  — TopicListPanel (B1): live topic tree, Hz/BW, search
//   Center-top  — SubplotManager 3×1 (C4): IMU linear_acceleration xyz
//   Center-bot  — RosPlotManager plot (C1+C2): /cmd_vel linear.x, auto-scroll
//   Right top   — TopicEchoPanel (B2): expandable field tree for selected topic
//   Right bot   — TopicStatsOverlay (B3): Hz/latency/drop stats
//   Status bar  — topic count, memory usage, scroll state (C2)

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <string>

// ── Core Spectra ─────────────────────────────────────────────────────────────
#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
#endif

// ── ROS2 Adapter — Phase A ───────────────────────────────────────────────────
// Include paths are relative to src/adapters/ros2/ (PUBLIC include dir of
// spectra_ros2_adapter).  These headers are NOT available without a sourced
// ROS2 workspace; clangd will show false-positive errors in the IDE.
#include "ros2_bridge.hpp"
#include "topic_discovery.hpp"
#include "message_introspector.hpp"
#include "generic_subscriber.hpp"

// ── ROS2 Adapter — Phase B (UI panels) ───────────────────────────────────────
#include "ui/topic_list_panel.hpp"
#include "ui/topic_echo_panel.hpp"
#include "ui/topic_stats_overlay.hpp"

// ── ROS2 Adapter — Phase C (plotting) ────────────────────────────────────────
#include "ros_plot_manager.hpp"
#include "subplot_manager.hpp"

// ── ROS2 Adapter — Phase D (bag) ─────────────────────────────────────────────
#include "ui/bag_info_panel.hpp"
#include "bag_recorder.hpp"

// ── ROS2 Adapter — Phase E (export) ──────────────────────────────────────────
#include "ros_screenshot_export.hpp"
#include "bag_player.hpp"
#include "ui/bag_playback_panel.hpp"

// ── ROS2 Adapter — Phase F (advanced tools) ──────────────────────────────────
#include "ui/tf_tree_panel.hpp"
#include "ui/param_editor_panel.hpp"
#include "ros_log_viewer.hpp"
#include "ui/log_viewer_panel.hpp"
#include "ui/diagnostics_panel.hpp"

// ── ImGui (for panel layout) ──────────────────────────────────────────────────
#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace ros2 = spectra::adapters::ros2;

// ============================================================================
// print_progress_summary — called once at startup to visualise plan status
// ============================================================================

static void print_progress_summary()
{
    std::fprintf(stdout,
                 "\n"
                 "+----------------------------------------------------------+\n"
                 "|      Spectra ROS2 Adapter -- Progress Demo               |\n"
                 "+----------------------------------------------------------+\n"
                 "|  Phase A -- Foundation & ROS2 Bridge           DONE [x]  |\n"
                 "|    A1  CMake scaffolding & ROS2 detection                 |\n"
                 "|    A2  Ros2Bridge (node lifecycle, spin thread)           |\n"
                 "|    A3  TopicDiscovery (periodic graph scan, callbacks)    |\n"
                 "|    A4  MessageIntrospector (runtime schema + accessor)    |\n"
                 "|    A5  GenericSubscriber (SPSC ring buffer)               |\n"
                 "|    A6  Phase A integration smoke test (19 tests)          |\n"
                 "+----------------------------------------------------------+\n"
                 "|  Phase B -- Topic Monitor Panel                DONE [x]  |\n"
                 "|    B1  TopicListPanel (tree, Hz/BW, search, select cb)   |\n"
                 "|    B2  TopicEchoPanel (field tree, ring 100 msgs)         |\n"
                 "|    B3  TopicStatsOverlay (avg/min/max Hz, drop detect)    |\n"
                 "+----------------------------------------------------------+\n"
                 "|  Phase C -- Live Plotting Engine               DONE [x]  |\n"
                 "|    C1  RosPlotManager (topic/field -> Figure+Series) [x]  |\n"
                 "|    C2  Auto-scroll time window (presented_buffer)    [x]  |\n"
                 "|    C3  Drag-and-drop field to plot                   [x]  |\n"
                 "|    C4  SubplotManager NxM (shared X-axis+cursor)     [x]  |\n"
                 "|    C5  Expression fields                             [x]  |\n"
                 "|    C6  Phase C integration test                      [x]  |\n"
                 "+----------------------------------------------------------+\n"
                 "|  Phase D -- Rosbag Player & Recorder        PARTIAL [~]  |\n"
                 "|    D1  BagReader (.db3/.mcap, seek, metadata)        [x]  |\n"
                 "|    D2  BagPlayer (play/pause/seek/rate, timeline)    [x]  |\n"
                 "|    D3  BagRecorder (start/stop, auto-split)          [x]  |\n"
                 "|    D4  BagInfoPanel (metadata, topic table, D&D)     [x]  |\n"
                 "+----------------------------------------------------------+\n"
                 "|  Phase E -- Export & Data Tools                           |\n"
                 "|    E1  CSV export with ROS timestamps               [x]  |\n"
                 "|    E2  Clipboard copy (TSV)                        PENDING  |\n"
                 "|    E3  Screenshot / video export (RosScreenshotExport) [x]  |\n"
                 "|  Phase F -- Advanced ROS2 Tools             PARTIAL [~] |\n"
                 "|    F1  NodeGraphPanel (force-directed layout)     PENDING  |\n"
                 "|    F2  TfTreePanel (/tf+/tf_static, Hz/age/stale)    [x]  |\n"
                 "|    F3  ParamEditorPanel (discover/edit/undo/YAML) [x]    |\n"
                 "|    F6  DiagnosticsPanel (/diagnostics dashboard)  [x]   |\n"
                 "|  Phase G -- Application Shell & Integration    PENDING    |\n"
                 "|  Phase H -- Testing & Validation               PENDING    |\n"
                 "|  (D4 BagInfoPanel: open bag, metadata table,              |\n"
                 "|   topic click-to-plot, drag-and-drop .db3/.mcap)          |\n"
                 "+----------------------------------------------------------+\n"
                 "\n"
                 "  Window layout:\n"
                 "    Left  panel  -- TopicListPanel (B1)  : live topic tree\n"
                 "    Center top   -- SubplotManager 3x1   : IMU linear_acceleration (C4)\n"
                 "    Center bot   -- RosPlotManager plot  : /cmd_vel linear.x (C1+C2)\n"
                 "    Right top    -- TopicEchoPanel (B2)  : field tree for selected topic\n"
                 "    Right bot    -- TopicStatsOverlay(B3): Hz/latency/drop stats\n"
                 "    Status bar   -- topic count, mem, scroll state (C2)\n"
                 "\n"
                 "  Keyboard shortcuts:\n"
                 "    H            -- resume auto-scroll after pan/zoom (C2)\n"
                 "    P            -- toggle pause auto-scroll\n"
                 "    Ctrl+Shift+S -- instant screenshot to /tmp (E3)\n"
                 "    Ctrl+Q       -- quit\n"
                 "\n");
}

// ============================================================================
// DemoApp — wraps all lifetime-sensitive ROS2 objects
// ============================================================================

struct DemoApp
{
    // ── Phase A ──────────────────────────────────────────────────────────────
    ros2::Ros2Bridge                      bridge;
    std::unique_ptr<ros2::TopicDiscovery> discovery;   // constructed after bridge.init()
    ros2::MessageIntrospector             intr;

    // ── Phase B ──────────────────────────────────────────────────────────────
    ros2::TopicListPanel topic_list;
    // TopicEchoPanel requires node + intr at construction time (A2 must run first)
    std::unique_ptr<ros2::TopicEchoPanel> echo_panel;
    ros2::TopicStatsOverlay               stats_overlay;

    // ── Phase C ──────────────────────────────────────────────────────────────
    std::unique_ptr<ros2::RosPlotManager> plot_mgr;
    std::unique_ptr<ros2::SubplotManager> subplot_mgr;

    // ── Phase D ──────────────────────────────────────────────────────────────
    std::unique_ptr<ros2::BagInfoPanel>     bag_info_panel;
    std::unique_ptr<ros2::BagRecorder>      bag_recorder;         // D3
    std::unique_ptr<ros2::BagPlayer>        bag_player;           // D2
    std::unique_ptr<ros2::BagPlaybackPanel> bag_playback_panel;   // D2

    // ── Phase E ──────────────────────────────────────────────────────────────
    ros2::RosScreenshotExport screenshot_export;   // E3

    // ── Phase F ──────────────────────────────────────────────────────────────
    ros2::TfTreePanel                       tf_tree_panel;      // F2
    std::unique_ptr<ros2::ParamEditorPanel> param_editor;       // F3
    std::unique_ptr<ros2::RosLogViewer>     log_viewer;         // F5
    std::unique_ptr<ros2::LogViewerPanel>   log_viewer_panel;   // F5
    std::unique_ptr<ros2::DiagnosticsPanel> diag_panel;         // F6

    // Handles / tracking
    ros2::PlotHandle    cmd_vel_handle;
    ros2::SubplotHandle imu_x_handle;
    ros2::SubplotHandle imu_y_handle;
    ros2::SubplotHandle imu_z_handle;

    std::string      selected_topic;
    std::atomic<int> topics_added{0};
    std::atomic<int> topics_removed{0};

    bool ready{false};
};

// ============================================================================
// Phase E — screenshot export demo
// ============================================================================

static void demo_screenshot(DemoApp& d)
{
    // E3 — RosScreenshotExport: take a 320×200 black PNG to /tmp.
    // (No FrameGrabCallback wired — produces a black image, which is
    // correct for the demo: GPU readback is a G2 concern.)
    const std::string path   = ros2::RosScreenshotExport::make_screenshot_path("/tmp", "ros2_demo");
    const auto        result = d.screenshot_export.take_screenshot(path, 320, 200);
    if (result.ok)
        std::fprintf(stdout, "[ros2_demo] E3 OK  screenshot -> %s\n", path.c_str());
    else
        std::fprintf(stderr, "[ros2_demo] E3 ERR screenshot failed: %s\n", result.error.c_str());
}

// ============================================================================
// Phase A — bridge, discovery, introspector
// ============================================================================

static bool init_bridge(DemoApp& d, int argc, char** argv)
{
    // A2 — init node lifecycle + executor thread
    if (!d.bridge.init("spectra_ros2_demo", "/spectra", argc, argv))
    {
        std::fprintf(stderr,
                     "[ros2_demo] Ros2Bridge::init() failed — "
                     "is a ROS2 workspace sourced?\n");
        return false;
    }
    d.bridge.start_spin();
    std::fprintf(stdout,
                 "[ros2_demo] A2 OK  Ros2Bridge spinning  node=%s\n",
                 d.bridge.node()->get_fully_qualified_name());

    // A3 — periodic topic/service/node discovery
    d.discovery = std::make_unique<ros2::TopicDiscovery>(d.bridge.node());
    d.discovery->set_refresh_interval(std::chrono::seconds(2));

    d.discovery->set_topic_callback(
        [&](const ros2::TopicInfo& ti, bool added)
        {
            if (added)
            {
                ++d.topics_added;
                std::fprintf(stdout,
                             "[ros2_demo] A3    topic+  %-36s  [%s]\n",
                             ti.name.c_str(),
                             ti.types.empty() ? "?" : ti.types.front().c_str());
            }
            else
            {
                ++d.topics_removed;
                std::fprintf(stdout, "[ros2_demo] A3    topic-  %s\n", ti.name.c_str());
            }
        });

    d.discovery->start();
    d.discovery->refresh();   // immediate first scan
    std::fprintf(stdout,
                 "[ros2_demo] A3 OK  TopicDiscovery  %zu topics\n",
                 d.discovery->topic_count());

    // A4 — MessageIntrospector (lazy dlopen; no action needed here)
    std::fprintf(stdout, "[ros2_demo] A4 OK  MessageIntrospector ready (lazy)\n");

    return true;
}

// ============================================================================
// Phase B — UI panels
// ============================================================================

static void init_panels(DemoApp& d)
{
    // B1 — TopicListPanel
    d.topic_list.set_topic_discovery(d.discovery.get());
    d.topic_list.set_title("ROS2 Topics");
    d.topic_list.set_group_by_namespace(true);

    // SelectCallback signature: (const std::string& topic_name)
    d.topic_list.set_select_callback(
        [&](const std::string& topic)
        {
            d.selected_topic = topic;

            // Look up message type from discovery
            if (d.discovery && d.discovery->has_topic(topic))
            {
                const auto        ti       = d.discovery->topic(topic);
                const std::string type_str = ti.types.empty() ? "" : ti.types.front();

                // B2 — switch echo panel to the selected topic
                if (d.echo_panel)
                    d.echo_panel->set_topic(topic, type_str);

                // B3 — switch stats overlay to the selected topic
                d.stats_overlay.set_topic(topic);

                std::fprintf(stdout,
                             "[ros2_demo] B1    selected  %s  [%s]\n",
                             topic.c_str(),
                             type_str.c_str());
            }
        });

    // PlotCallback signature: (const std::string& topic_name)
    d.topic_list.set_plot_callback(
        [&](const std::string& topic)
        { std::fprintf(stdout, "[ros2_demo] B1    plot-request  %s\n", topic.c_str()); });

    // B2 — TopicEchoPanel (node + intr available after A2)
    d.echo_panel = std::make_unique<ros2::TopicEchoPanel>(d.bridge.node(), d.intr);
    d.echo_panel->set_max_messages(100);
    d.echo_panel->set_display_hz(30.0);
    d.echo_panel->set_title("Topic Echo");

    // B3 — TopicStatsOverlay
    d.stats_overlay.set_title("Topic Statistics");
    d.stats_overlay.set_window_ms(1000);     // 1-second rolling window
    d.stats_overlay.set_drop_factor(3.0f);   // warn if gap > 3× expected period

    std::fprintf(stdout, "[ros2_demo] B1-B3 OK  UI panels ready\n");
}

// ============================================================================
// Phase C — RosPlotManager (C1+C2) and SubplotManager (C4)
// ============================================================================

static void init_plots(DemoApp& d)
{
    // C1 + C2 — single-series plot: /cmd_vel linear.x with auto-scroll
    d.plot_mgr = std::make_unique<ros2::RosPlotManager>(d.bridge, d.intr);
    d.plot_mgr->set_time_window(30.0);       // C2: 30-second sliding window
    d.plot_mgr->set_auto_fit_samples(100);   // C1: Y auto-fit after 100 points
    d.plot_mgr->set_figure_size(1024, 300);

    // Forward arrivals to TopicListPanel Hz/BW tracker and stats overlay
    d.plot_mgr->set_on_data(
        [&](int /*id*/, double /*t*/, double /*v*/)
        {
            d.topic_list.notify_message("/cmd_vel", sizeof(double));
            d.stats_overlay.notify_message("/cmd_vel", sizeof(double), /*latency_us=*/-1);
        });

    d.cmd_vel_handle = d.plot_mgr->add_plot("/cmd_vel", "linear.x", "geometry_msgs/msg/Twist");

    if (d.cmd_vel_handle.valid())
    {
        d.cmd_vel_handle.axes->title("/cmd_vel  linear.x");
        d.cmd_vel_handle.axes->xlabel("time (s)");
        d.cmd_vel_handle.axes->ylabel("m/s");
        std::fprintf(stdout,
                     "[ros2_demo] C1 OK  RosPlotManager  /cmd_vel/linear.x  "
                     "scroll=%.0fs\n",
                     d.plot_mgr->time_window());
    }
    else
    {
        std::fprintf(stdout,
                     "[ros2_demo] C1     /cmd_vel not yet live "
                     "(will work once publisher starts)\n");
    }

    // C4 — SubplotManager: 3-row × 1-col grid (IMU linear_acceleration xyz)
    d.subplot_mgr =
        std::make_unique<ros2::SubplotManager>(d.bridge, d.intr, /*rows=*/3, /*cols=*/1);
    d.subplot_mgr->set_time_window(30.0);
    d.subplot_mgr->set_auto_fit_samples(100);
    d.subplot_mgr->set_figure_size(1024, 600);

    d.subplot_mgr->set_on_data([&](int /*slot*/, double /*t*/, double /*v*/)
                               { d.topic_list.notify_message("/imu", sizeof(double)); });

    d.imu_x_handle =
        d.subplot_mgr->add_plot(1, "/imu", "linear_acceleration.x", "sensor_msgs/msg/Imu");
    d.imu_y_handle =
        d.subplot_mgr->add_plot(2, "/imu", "linear_acceleration.y", "sensor_msgs/msg/Imu");
    d.imu_z_handle =
        d.subplot_mgr->add_plot(3, "/imu", "linear_acceleration.z", "sensor_msgs/msg/Imu");

    if (d.imu_x_handle.valid())
    {
        d.imu_x_handle.axes->title("IMU — linear_acceleration");
        d.imu_x_handle.axes->ylabel("x (m/s^2)");
    }
    if (d.imu_y_handle.valid())
        d.imu_y_handle.axes->ylabel("y (m/s^2)");
    if (d.imu_z_handle.valid())
    {
        d.imu_z_handle.axes->ylabel("z (m/s^2)");
        d.imu_z_handle.axes->xlabel("time (s)");
    }

    std::fprintf(stdout, "[ros2_demo] C4 OK  SubplotManager 3x1  IMU linear_acceleration\n");

    d.ready = true;
}

// ============================================================================
// Phase D4 — BagInfoPanel demo
// ============================================================================

static void init_bag_info(DemoApp& d)
{
    d.bag_info_panel = std::make_unique<ros2::BagInfoPanel>();
    d.bag_info_panel->set_title("Bag Info (D4)");

    // Wire double-click → plot via the plot_mgr (same as C1).
    d.bag_info_panel->set_topic_plot_callback(
        [&](const std::string& topic, const std::string& /*type*/)
        {
            if (d.plot_mgr)
                d.plot_mgr->add_plot(topic, "");
            std::fprintf(stdout, "[ros2_demo] D4    plot-topic  %s\n", topic.c_str());
        });

    // Log when a bag is opened.
    d.bag_info_panel->set_bag_opened_callback(
        [](const std::string& path)
        { std::fprintf(stdout, "[ros2_demo] D4 OK  bag opened  %s\n", path.c_str()); });

    // Attempt to open bag from CLI arg --bag (if any).
    // The demo accepts --bag <path> as the first positional argument after "--".
    // In practice, bag_file would come from parsed args; here we just log
    // the panel-ready state.
    std::fprintf(stdout,
                 "[ros2_demo] D4 OK  BagInfoPanel ready  "
                 "(drop a .db3/.mcap file onto the panel, or use --bag <path>)\n");
}

// ============================================================================
// Phase D3 — BagRecorder demo
// ============================================================================

#ifdef SPECTRA_ROS2_BAG
static void demo_bag_recorder(DemoApp& d)
{
    if (!d.bridge.node())
    {
        std::fprintf(stdout, "[ros2_demo] D3     bridge not ready\n");
        return;
    }

    d.bag_recorder = std::make_unique<ros2::BagRecorder>(d.bridge.node());

    // Configure: 512 MB size limit, 300 s duration limit
    d.bag_recorder->set_max_size_bytes(512u * 1024u * 1024u);
    d.bag_recorder->set_max_duration_seconds(300.0);
    d.bag_recorder->set_reliable_qos(false);   // best-effort for demo

    // Wire split callback
    d.bag_recorder->set_split_callback(
        [](const ros2::RecordingSplitInfo& info)
        {
            std::fprintf(stdout,
                         "[ros2_demo] D3    auto-split  closed=%s  new=%s  split=%u  "
                         "msgs=%" PRIu64 "  bytes=%" PRIu64 "\n",
                         info.closed_path.c_str(),
                         info.new_path.c_str(),
                         info.split_index,
                         info.messages_in_closed,
                         info.bytes_in_closed);
        });

    // Wire error callback
    d.bag_recorder->set_error_callback(
        [](const std::string& err)
        { std::fprintf(stderr, "[ros2_demo] D3 ERR  %s\n", err.c_str()); });

    // Start recording /cmd_vel and /imu (if they exist in the graph).
    // In a real session pass the desired output path; here we use /tmp.
    const std::string              bag_path   = "/tmp/spectra_ros2_demo_record.db3";
    const std::vector<std::string> rec_topics = {"/cmd_vel", "/imu"};

    // Attempt to start — may fail if topics not yet in the graph; that is fine.
    if (d.bag_recorder->start(bag_path, rec_topics))
    {
        std::fprintf(stdout,
                     "[ros2_demo] D3 OK  BagRecorder started  path=%s  "
                     "topics=[/cmd_vel, /imu]  max_size=512MB  max_dur=300s\n",
                     bag_path.c_str());
    }
    else
    {
        std::fprintf(stdout,
                     "[ros2_demo] D3     BagRecorder not started  "
                     "(topics may not be live yet): %s\n",
                     d.bag_recorder->last_error().c_str());
    }
}
#else
static void demo_bag_recorder(DemoApp&)
{
    std::fprintf(stdout,
                 "[ros2_demo] D3     BagRecorder skipped "
                 "(built without SPECTRA_ROS2_BAG)\n");
}
#endif

// ============================================================================
// Phase D2 — BagPlayer demo
// ============================================================================

#ifdef SPECTRA_ROS2_BAG
static void demo_bag_player(DemoApp& d, const std::string& bag_path)
{
    if (!d.plot_mgr)
    {
        std::fprintf(stdout, "[ros2_demo] D2     plot_mgr not ready\n");
        return;
    }

    d.bag_player         = std::make_unique<ros2::BagPlayer>(*d.plot_mgr, d.intr);
    d.bag_playback_panel = std::make_unique<ros2::BagPlaybackPanel>(d.bag_player.get());
    d.bag_playback_panel->set_title("Bag Playback (D2)");
    d.bag_playback_panel->set_show_loop_button(true);
    d.bag_playback_panel->set_show_rate_slider(true);

    // Wire playhead callback for console progress.
    d.bag_player->set_on_playhead(
        [](double t)
        {
            static double last_log = -1.0;
            if (t - last_log >= 5.0)
            {
                std::fprintf(stdout, "[ros2_demo] D2     playhead  %.1f s\n", t);
                last_log = t;
            }
        });

    // Wire state callback.
    d.bag_player->set_on_state_change(
        [](ros2::PlayerState s)
        {
            const char* label = s == ros2::PlayerState::Playing  ? "Playing"
                                : s == ros2::PlayerState::Paused ? "Paused"
                                                                 : "Stopped";
            std::fprintf(stdout, "[ros2_demo] D2     state  %s\n", label);
        });

    if (bag_path.empty())
    {
        std::fprintf(stdout,
                     "[ros2_demo] D2 OK  BagPlayer ready "
                     "(no bag path supplied; pass --bag <path> to play a bag)\n");
        return;
    }

    if (!d.bag_player->open(bag_path))
    {
        std::fprintf(stdout,
                     "[ros2_demo] D2 ERR  BagPlayer open failed: %s\n",
                     d.bag_player->last_error().c_str());
        return;
    }

    const auto& meta = d.bag_player->metadata();
    std::fprintf(stdout,
                 "[ros2_demo] D2 OK  BagPlayer opened  path=%s  dur=%.2f s  "
                 "topics=%zu  msgs=%" PRIu64 "\n",
                 bag_path.c_str(),
                 meta.duration_sec(),
                 meta.topics.size(),
                 meta.message_count);

    // Log activity bands.
    const auto bands = d.bag_player->topic_activity_bands();
    for (const auto& band : bands)
    {
        std::fprintf(stdout,
                     "[ros2_demo] D2     activity  %-36s  intervals=%zu\n",
                     band.topic.c_str(),
                     band.intervals.size());
    }

    // Start playback at 1× real-time.
    d.bag_player->set_rate(1.0);
    d.bag_player->set_loop(false);
    d.bag_player->play();

    std::fprintf(stdout, "[ros2_demo] D2     Playing bag at 1.0x rate\n");
}
#else
static void demo_bag_player(DemoApp& d, const std::string&)
{
    d.bag_playback_panel = std::make_unique<ros2::BagPlaybackPanel>(nullptr);
    d.bag_playback_panel->set_title("Bag Playback (D2)");
    std::fprintf(stdout,
                 "[ros2_demo] D2     BagPlayer skipped "
                 "(built without SPECTRA_ROS2_BAG)\n");
}
#endif   // SPECTRA_ROS2_BAG

// ============================================================================
// Phase A5 — demonstrate direct GenericSubscriber usage
// ============================================================================

static void demo_generic_subscriber(DemoApp& d)
{
    // A5 — create a raw GenericSubscriber, add a field, start, then stop.
    // This proves the SPSC ring buffer and introspection path works standalone.
    ros2::GenericSubscriber raw_sub(d.bridge.node(),
                                    "/chatter_float",
                                    "std_msgs/msg/Float64",
                                    d.intr);

    int fid = raw_sub.add_field("data");
    if (fid >= 0)
    {
        raw_sub.start();
        std::fprintf(stdout,
                     "[ros2_demo] A5 OK  GenericSubscriber  /chatter_float/data  "
                     "field_id=%d  (ring ready)\n",
                     fid);
        raw_sub.stop();
    }
    else
    {
        std::fprintf(stdout,
                     "[ros2_demo] A5     /chatter_float not yet published "
                     "(introspection will resolve once topic is live)\n");
    }
}

// ============================================================================
// Phase F3 — ParamEditorPanel
// ============================================================================

static void init_param_editor(DemoApp& d)
{
    d.param_editor = std::make_unique<ros2::ParamEditorPanel>(d.bridge.node());
    d.param_editor->set_title("Parameter Editor (F3)");
    d.param_editor->set_live_edit(true);

    // Log every successful parameter set
    d.param_editor->set_on_param_set(
        [](const std::string& name, const ros2::ParamValue& val, bool ok)
        {
            std::fprintf(stdout,
                         "[ros2_demo] F3    param_set  %s = %s  ok=%s\n",
                         name.c_str(),
                         val.to_display_string(48).c_str(),
                         ok ? "true" : "false");
        });

    // Log refresh completion
    d.param_editor->set_on_refresh_done(
        [](bool ok) {
            std::fprintf(stdout, "[ros2_demo] F3    refresh done  ok=%s\n", ok ? "true" : "false");
        });

    // Pre-aim at the demo node itself so the panel is ready to use
    const std::string self_node = "/spectra/spectra_ros2_demo";
    d.param_editor->set_target_node(self_node);

    std::fprintf(stdout,
                 "[ros2_demo] F3 OK  ParamEditorPanel ready  "
                 "target=%s  live_edit=true\n",
                 self_node.c_str());
}

// ============================================================================
// Phase F5 — RosLogViewer + LogViewerPanel
// ============================================================================

static void init_log_viewer(DemoApp& d)
{
    d.log_viewer = std::make_unique<ros2::RosLogViewer>(d.bridge.node());
    d.log_viewer->subscribe("/rosout");
    d.log_viewer_panel = std::make_unique<ros2::LogViewerPanel>(*d.log_viewer);
    d.log_viewer_panel->set_title("ROS2 Log (F5)");
    d.log_viewer_panel->set_display_hz(20.0);

    std::fprintf(stdout,
                 "[ros2_demo] F5 OK  RosLogViewer subscribed  topic=/rosout  "
                 "capacity=%zu\n",
                 d.log_viewer->capacity());
}

// ============================================================================
// Phase F6 — DiagnosticsPanel
// ============================================================================

static void init_diag_panel(DemoApp& d)
{
    d.diag_panel = std::make_unique<ros2::DiagnosticsPanel>();
    d.diag_panel->set_title("Diagnostics (F6)");
    d.diag_panel->set_node(d.bridge.node().get());
    d.diag_panel->set_stale_threshold_s(5.0);
    d.diag_panel->set_alert_callback(
        [](const std::string& name, ros2::DiagLevel level)
        {
            std::fprintf(stdout,
                         "[ros2_demo] F6 ALERT  %s -> %s\n",
                         name.c_str(),
                         ros2::diag_level_name(level));
        });
    d.diag_panel->start();

    std::fprintf(stdout,
                 "[ros2_demo] F6 OK  DiagnosticsPanel started  "
                 "topic=/diagnostics  stale=5.0s\n");
}

// ============================================================================
// ImGui overlay — drawn once per frame inside the render thread
// ============================================================================

#ifdef SPECTRA_USE_IMGUI
static void draw_panels(DemoApp& d)
{
    const ImGuiIO& io = ImGui::GetIO();
    float          W  = io.DisplaySize.x;
    float          H  = io.DisplaySize.y;

    constexpr float LEFT_W  = 300.0f;
    constexpr float RIGHT_W = 340.0f;
    constexpr float SB_H    = 26.0f;

    // ── Left: TopicListPanel (B1) ─────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(LEFT_W, H - SB_H), ImGuiCond_Always);
    bool list_open = true;
    d.topic_list.draw(&list_open);

    // ── Right top: TopicEchoPanel (B2) ────────────────────────────────────
    float right_x = W - RIGHT_W;
    ImGui::SetNextWindowPos(ImVec2(right_x, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(RIGHT_W, H * 0.55f), ImGuiCond_Always);
    if (d.echo_panel)
    {
        bool echo_open = true;
        d.echo_panel->draw(&echo_open);
    }

    // ── Right bot: TopicStatsOverlay (B3) ─────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(right_x, H * 0.55f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(RIGHT_W, H * 0.45f - SB_H), ImGuiCond_Always);
    bool stats_open = true;
    d.stats_overlay.draw(&stats_open);

    // ── D4: BagInfoPanel (floating, user can drag it anywhere) ────────────
    if (d.bag_info_panel)
    {
        ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W + 20, 20), ImGuiCond_FirstUseEver);
        bool bag_open = true;
        d.bag_info_panel->draw(&bag_open);
    }

    // ── F3: ParamEditorPanel (floating) ───────────────────────────────────
    if (d.param_editor)
    {
        ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W + 20, H * 0.5f), ImGuiCond_FirstUseEver);
        bool param_open = true;
        d.param_editor->draw(&param_open);
    }

    // ── D2: BagPlaybackPanel (scrub bar + transport controls) ────────────
    if (d.bag_playback_panel)
    {
        ImGui::SetNextWindowSize(ImVec2(680, 120), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W + 20, H - 160.0f), ImGuiCond_FirstUseEver);
        bool playback_open = true;
        d.bag_playback_panel->draw(&playback_open);
    }

    // ── F2: TfTreePanel (floating panel, lower-center) ────────────────────
    {
        ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W + 20, H * 0.45f), ImGuiCond_FirstUseEver);
        bool tf_open = true;
        d.tf_tree_panel.draw(&tf_open);
    }

    // ── F5: LogViewerPanel (bottom dockable) ──────────────────────────────
    if (d.log_viewer_panel)
    {
        ImGui::SetNextWindowSize(ImVec2(W - LEFT_W - RIGHT_W, 180.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W, H - 180.0f - SB_H), ImGuiCond_FirstUseEver);
        bool log_open = true;
        d.log_viewer_panel->draw(&log_open);
    }

    // ── F6: DiagnosticsPanel (floating) ───────────────────────────────────
    if (d.diag_panel)
    {
        ImGui::SetNextWindowSize(ImVec2(680, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(LEFT_W + 20, 20), ImGuiCond_FirstUseEver);
        bool diag_open = true;
        d.diag_panel->draw(&diag_open);
    }

    // ── Status bar (C2 memory + scroll state) ─────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(LEFT_W, H - SB_H), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W - LEFT_W - RIGHT_W, SB_H), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::Begin(
        "##ros2_status",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    size_t total_mem = 0;
    if (d.plot_mgr)
        total_mem += d.plot_mgr->total_memory_bytes();
    if (d.subplot_mgr)
        total_mem += d.subplot_mgr->total_memory_bytes();

    const char* scroll_lbl = "following";
    if (d.plot_mgr && d.cmd_vel_handle.valid() && d.plot_mgr->is_scroll_paused(d.cmd_vel_handle.id))
        scroll_lbl = "paused [P]";

    ImGui::Text("Topics: %zu  |  Plots: %zu+3  |  Mem: %.1f KB  "
                "|  Scroll: %s  |  Graph: +%d/-%d",
                d.discovery ? d.discovery->topic_count() : 0u,
                d.plot_mgr ? d.plot_mgr->plot_count() : 0u,
                static_cast<double>(total_mem) / 1024.0,
                scroll_lbl,
                d.topics_added.load(),
                d.topics_removed.load());

    ImGui::End();
}
#endif   // SPECTRA_USE_IMGUI

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv)
{
    print_progress_summary();

    // ── Phase A — ROS2 bridge + discovery ────────────────────────────────────
    DemoApp demo;

    if (!init_bridge(demo, argc, argv))
        return 1;

    // ── Phase B — UI panels (needs node from A2) ──────────────────────────────
    init_panels(demo);

    // ── Phase C — plotting engines ────────────────────────────────────────────
    init_plots(demo);

    // ── Phase D4 — BagInfoPanel ───────────────────────────────────────────────
    init_bag_info(demo);

    // ── Phase D3 — BagRecorder demo ──────────────────────────────────────────
    demo_bag_recorder(demo);

    // ── Phase D2 — BagPlayer demo ────────────────────────────────────────────
    // Parse optional --bag <path> argument.
    std::string bag_arg;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--bag" && i + 1 < argc)
            bag_arg = argv[++i];
    }
    demo_bag_player(demo, bag_arg);

    // ── Phase E3 — Screenshot export ─────────────────────────────────────────
    demo_screenshot(demo);

    // ── Phase F2 — TfTreePanel ────────────────────────────────────────────────
    demo.tf_tree_panel.set_node(demo.bridge.node());
    demo.tf_tree_panel.set_title("TF Frames (F2)");
    demo.tf_tree_panel.set_stale_threshold_ms(500);
    demo.tf_tree_panel.start();
    std::fprintf(stdout,
                 "[ros2_demo] F2 OK  TfTreePanel started  "
                 "(/tf + /tf_static)\n");

    // ── Phase A5 — direct GenericSubscriber proof ─────────────────────────────
    demo_generic_subscriber(demo);

    // ── Phase F3 — ParamEditorPanel ───────────────────────────────────────────
    init_param_editor(demo);

    // ── Phase F5 — RosLogViewer (rosout) ─────────────────────────────────────
    init_log_viewer(demo);

    // ── Phase F6 — DiagnosticsPanel ───────────────────────────────────────────
    init_diag_panel(demo);

    // ── Spectra App — step() loop (non-blocking ROS2 + render) ───────────────
    spectra::AppConfig cfg;
    cfg.headless = false;
    spectra::App app(cfg);

    // The IMU subplot figure (C4) is owned by SubplotManager — pass it into
    // a Spectra figure slot so the renderer sees it.
    // The cmd_vel figure (C1) is owned by RosPlotManager — same pattern.
    // In a production app these would be docked panels; here we just call run()
    // which opens a default Spectra window for each created Figure.

    app.init_runtime();

#ifdef SPECTRA_USE_IMGUI
    if (auto* ui_ctx = app.ui_context(); ui_ctx && ui_ctx->imgui_ui)
    {
        ui_ctx->imgui_ui->set_extra_draw_callback([&demo]() { draw_panels(demo); });
    }
#endif

    while (true)
    {
        // Poll ROS2 data into Spectra series (hot path — no locks in poll())
        if (demo.ready)
        {
            demo.plot_mgr->poll();      // C1+C2
            demo.subplot_mgr->poll();   // C4
        }

        // D2 — advance BagPlayer each frame (feeds injected bag messages)
        if (demo.bag_player && demo.bag_player->is_open())
            demo.bag_player->advance(1.0 / 60.0);   // assume ~60 fps step

        auto result = app.step();
        if (result.should_exit)
            break;
    }

    app.shutdown_runtime();

    // ── Shutdown (order: plot managers → discovery → bridge) ─────────────────
    demo.echo_panel.reset();
    demo.subplot_mgr.reset();
    demo.plot_mgr.reset();
    if (demo.discovery)
        demo.discovery->stop();
    demo.bridge.shutdown();

    std::fprintf(stdout, "[ros2_demo] Clean shutdown complete.\n");
    return 0;
}
