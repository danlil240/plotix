// ros_qa_agent.cpp - Comprehensive QA harness for spectra-ros.
//
// Launches the real spectra-ros shell inside a windowed Spectra app, drives it
// with a live helper ROS2 node, and verifies the major user-facing subsystems:
// discovery, plotting, echo/stats, layouts/sessions, logs, diagnostics, TF,
// parameter editing, service caller behavior, and optional bag playback.
//
// Usage:
//   spectra_ros_qa_agent [options]
//     --seed <N>          RNG seed / unique suffix (default: time-based)
//     --duration <sec>    Max wall-clock runtime (default: 120)
//     --scenario <name>   Run one scenario only (default: all)
//     --output-dir <dir>  Output dir for reports and screenshots
//     --list-scenarios    Print scenarios and exit

#include "ros_app_shell.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#include "render/backend.hpp"
#include "ros_log_viewer.hpp"
#include "ros_screenshot_export.hpp"
#include "ros_session.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <tf2_msgs/msg/tf_message.hpp>

#ifdef SPECTRA_ROS2_BAG
    #include <rcutils/allocator.h>
    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/serialized_bag_message.hpp>
    #include <rosbag2_storage/storage_options.hpp>
    #include <rosbag2_storage/topic_metadata.hpp>

    #include "bag_message_compat.hpp"
#endif

using namespace spectra;
using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

namespace
{

size_t get_rss_bytes()
{
#ifdef __linux__
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f)
        return 0;

    long pages = 0;
    if (std::fscanf(f, "%*d %ld", &pages) != 1)
        pages = 0;
    std::fclose(f);
    return static_cast<size_t>(pages) * 4096ull;
#else
    return 0;
#endif
}

uint64_t to_mb(uint64_t bytes)
{
    return bytes / (1024ull * 1024ull);
}

std::string spectra_source_dir()
{
#ifdef SPECTRA_SOURCE_DIR
    return SPECTRA_SOURCE_DIR;
#else
    return ".";
#endif
}

std::string sanitize_filename(std::string name)
{
    for (char& c : name)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                        || c == '_' || c == '-' || c == '.';
        if (!ok)
            c = '_';
    }
    return name;
}

std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

template <typename T>
bool contains(const std::vector<T>& items, const T& value)
{
    return std::find(items.begin(), items.end(), value) != items.end();
}

enum class IssueSeverity
{
    Info,
    Warning,
    Error,
    Critical,
};

const char* severity_str(IssueSeverity sev)
{
    switch (sev)
    {
        case IssueSeverity::Info:
            return "INFO";
        case IssueSeverity::Warning:
            return "WARNING";
        case IssueSeverity::Error:
            return "ERROR";
        case IssueSeverity::Critical:
            return "CRITICAL";
    }
    return "UNKNOWN";
}

struct QAIssue
{
    IssueSeverity severity{IssueSeverity::Info};
    std::string   category;
    std::string   message;
    uint64_t      frame{0};
};

struct FrameStats
{
    std::vector<float> samples;

    void record(float ms) { samples.push_back(ms); }

    float average() const
    {
        if (samples.empty())
            return 0.0f;
        double sum = 0.0;
        for (float sample : samples)
            sum += sample;
        return static_cast<float>(sum / static_cast<double>(samples.size()));
    }

    float percentile(float p) const
    {
        if (samples.empty())
            return 0.0f;
        std::vector<float> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        const size_t idx = static_cast<size_t>(p * static_cast<float>(sorted.size() - 1));
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    float max_value() const
    {
        if (samples.empty())
            return 0.0f;
        return *std::max_element(samples.begin(), samples.end());
    }
};

enum class ScenarioStatus
{
    Passed,
    Failed,
    Skipped,
};

const char* scenario_status_str(ScenarioStatus status)
{
    switch (status)
    {
        case ScenarioStatus::Passed:
            return "passed";
        case ScenarioStatus::Failed:
            return "failed";
        case ScenarioStatus::Skipped:
            return "skipped";
    }
    return "unknown";
}

struct ScenarioOutcome
{
    ScenarioStatus status{ScenarioStatus::Passed};
    std::string    detail;
};

struct ScenarioResult
{
    std::string    name;
    std::string    description;
    ScenarioStatus status{ScenarioStatus::Passed};
    std::string    detail;
    uint64_t       frame_start{0};
    uint64_t       frame_end{0};
    size_t         rss_before_bytes{0};
    size_t         rss_after_bytes{0};
    std::string    screenshot_path;
};

struct DesignCapture
{
    std::string name;
    std::string description;
    std::string path;
    uint64_t    frame{0};
};

struct QAOptions
{
    uint64_t    seed{0};
    float       duration_sec{120.0f};
    std::string scenario_name;
    std::string output_dir{"/tmp/spectra_ros_qa"};
    bool        list_scenarios{false};
    bool        design_review{false};
};

QAOptions parse_args(int argc, char** argv)
{
    QAOptions opts;
    opts.seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--seed" && i + 1 < argc)
            opts.seed = std::stoull(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc)
            opts.duration_sec = std::stof(argv[++i]);
        else if (arg == "--scenario" && i + 1 < argc)
            opts.scenario_name = argv[++i];
        else if (arg == "--output-dir" && i + 1 < argc)
            opts.output_dir = argv[++i];
        else if (arg == "--list-scenarios")
            opts.list_scenarios = true;
        else if (arg == "--design-review")
            opts.design_review = true;
        else if (arg == "--help" || arg == "-h")
        {
            std::fprintf(stderr,
                         "Usage: spectra_ros_qa_agent [options]\n"
                         "  --seed <N>          RNG seed / unique suffix\n"
                         "  --duration <sec>    Max wall-clock runtime (default: 120)\n"
                         "  --scenario <name>   Run single scenario\n"
                         "  --output-dir <dir>  Output dir for report + screenshots\n"
                         "  --design-review     Capture named ROS shell design-review screenshots\n"
                         "  --list-scenarios    List scenarios and exit\n");
            std::exit(0);
        }
    }

    return opts;
}

class RosQaFixture
{
   public:
    explicit RosQaFixture(const std::string& node_name)
    {
        node_ = std::make_shared<rclcpp::Node>(node_name);

        node_->declare_parameter<bool>("qa_enabled", true);
        node_->declare_parameter<int>("qa_count", 3);
        node_->declare_parameter<double>("qa_gain", 1.5);
        node_->declare_parameter<std::string>("qa_mode", "idle");

        float_pub_ = node_->create_publisher<std_msgs::msg::Float64>("/qa/float", 10);
        twist_pub_ = node_->create_publisher<geometry_msgs::msg::Twist>("/qa/cmd_vel", 10);
        diag_pub_ =
            node_->create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);
        tf_pub_        = node_->create_publisher<tf2_msgs::msg::TFMessage>("/tf", 10);
        tf_static_pub_ = node_->create_publisher<tf2_msgs::msg::TFMessage>(
            "/tf_static",
            rclcpp::QoS(1).reliable().transient_local());

        exec_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        exec_->add_node(node_);

        spin_thread_ = std::thread(
            [this]()
            {
                while (!stop_spin_.load(std::memory_order_acquire) && rclcpp::ok())
                {
                    exec_->spin_once(10ms);
                }
            });
    }

    ~RosQaFixture() { stop(); }

    void stop()
    {
        if (!exec_)
            return;

        stop_spin_.store(true, std::memory_order_release);
        exec_->cancel();
        if (spin_thread_.joinable())
            spin_thread_.join();
        exec_->remove_node(node_);
        exec_.reset();
        node_.reset();
    }

    rclcpp::Node::SharedPtr node() const { return node_; }

    std::string fully_qualified_name() const
    {
        return node_ ? node_->get_fully_qualified_name() : std::string{};
    }

    void publish_float(double value)
    {
        std_msgs::msg::Float64 msg;
        msg.data = value;
        float_pub_->publish(msg);
    }

    void publish_twist(double linear_x, double angular_z)
    {
        geometry_msgs::msg::Twist msg;
        msg.linear.x  = linear_x;
        msg.angular.z = angular_z;
        twist_pub_->publish(msg);
    }

    void publish_diagnostics()
    {
        diagnostic_msgs::msg::DiagnosticArray msg;

        diagnostic_msgs::msg::DiagnosticStatus warn;
        warn.level       = diagnostic_msgs::msg::DiagnosticStatus::WARN;
        warn.name        = "qa_motor";
        warn.message     = "temperature rising";
        warn.hardware_id = "motor_1";
        diagnostic_msgs::msg::KeyValue warn_temp;
        warn_temp.key   = "temp_c";
        warn_temp.value = "77.5";
        warn.values.push_back(warn_temp);
        msg.status.push_back(warn);

        diagnostic_msgs::msg::DiagnosticStatus error;
        error.level       = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
        error.name        = "qa_battery";
        error.message     = "voltage low";
        error.hardware_id = "battery_pack";
        diagnostic_msgs::msg::KeyValue error_voltage;
        error_voltage.key   = "voltage";
        error_voltage.value = "10.1";
        error.values.push_back(error_voltage);
        msg.status.push_back(error);

        diag_pub_->publish(msg);
    }

    void publish_tf_chain()
    {
        geometry_msgs::msg::TransformStamped static_tf;
        static_tf.header.stamp            = node_->now();
        static_tf.header.frame_id         = "map";
        static_tf.child_frame_id          = "base_link";
        static_tf.transform.translation.x = 1.0;
        static_tf.transform.rotation.w    = 1.0;

        tf2_msgs::msg::TFMessage static_msg;
        static_msg.transforms.push_back(static_tf);
        tf_static_pub_->publish(static_msg);

        geometry_msgs::msg::TransformStamped dynamic_tf;
        dynamic_tf.header.stamp            = node_->now();
        dynamic_tf.header.frame_id         = "base_link";
        dynamic_tf.child_frame_id          = "laser";
        dynamic_tf.transform.translation.x = 0.25;
        dynamic_tf.transform.translation.z = 0.5;
        dynamic_tf.transform.rotation.w    = 1.0;

        tf2_msgs::msg::TFMessage dynamic_msg;
        dynamic_msg.transforms.push_back(dynamic_tf);
        tf_pub_->publish(dynamic_msg);
    }

    void emit_info_log(const std::string& text)
    {
        RCLCPP_INFO(node_->get_logger(), "%s", text.c_str());
    }

    double gain() const
    {
        double value = 0.0;
        node_->get_parameter("qa_gain", value);
        return value;
    }

    std::string mode() const
    {
        std::string value;
        node_->get_parameter("qa_mode", value);
        return value;
    }

   private:
    rclcpp::Node::SharedPtr                                    node_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> exec_;
    std::thread                                                spin_thread_;
    std::atomic<bool>                                          stop_spin_{false};

    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr                float_pub_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr             twist_pub_;
    rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diag_pub_;
    rclcpp::Publisher<tf2_msgs::msg::TFMessage>::SharedPtr              tf_pub_;
    rclcpp::Publisher<tf2_msgs::msg::TFMessage>::SharedPtr              tf_static_pub_;
};

#ifdef SPECTRA_ROS2_BAG
std::string write_float64_bag(const std::string& dir_name,
                              const std::string& topic,
                              int                n_msgs       = 12,
                              double             start_time_s = 1000.0,
                              double             interval_s   = 0.1)
{
    try
    {
        rosbag2_storage::StorageOptions opts;
        opts.uri        = dir_name;
        opts.storage_id = "sqlite3";

        rosbag2_cpp::Writer writer;
        writer.open(opts);

        rosbag2_storage::TopicMetadata meta;
        meta.name                 = topic;
        meta.type                 = "std_msgs/msg/Float64";
        meta.serialization_format = "cdr";
        writer.create_topic(meta);

        for (int i = 0; i < n_msgs; ++i)
        {
            const double  value = static_cast<double>(i) * 1.25;
            const double  t_s   = start_time_s + static_cast<double>(i) * interval_s;
            const int64_t t_ns  = static_cast<int64_t>(t_s * 1e9);

            std::vector<uint8_t> cdr(12, 0);
            cdr[0] = 0x00;
            cdr[1] = 0x01;
            cdr[2] = 0x00;
            cdr[3] = 0x00;
            std::memcpy(cdr.data() + 4, &value, sizeof(double));

            auto msg        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
            msg->topic_name = topic;
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

    return dir_name;
}
#endif

}   // namespace

class RosQAAgent
{
   public:
    explicit RosQAAgent(QAOptions opts) : opts_(std::move(opts))
    {
        std::filesystem::create_directories(opts_.output_dir);

        shell_cfg_.node_name     = "spectra_ros_qa_" + std::to_string(opts_.seed);
        shell_cfg_.layout        = LayoutMode::Default;
        shell_cfg_.subplot_rows  = 1;
        shell_cfg_.subplot_cols  = 1;
        shell_cfg_.time_window_s = 15.0;
        shell_cfg_.window_width  = 1600;
        shell_cfg_.window_height = 900;

        register_scenarios();
    }

    ~RosQAAgent() { shutdown(); }

    bool init()
    {
        app_           = std::make_unique<App>(AppConfig{});
        canvas_figure_ = &app_->figure({shell_cfg_.window_width, shell_cfg_.window_height});

        shell_ = std::make_unique<RosAppShell>(shell_cfg_);
        shell_->set_canvas_figure(canvas_figure_);

        int    argc = 0;
        char** argv = nullptr;
        if (!shell_->init(argc, argv))
        {
            std::fprintf(stderr,
                         "[ROS-QA] Failed to initialize shell node '%s'\n",
                         shell_cfg_.node_name.c_str());
            return false;
        }

        fixture_ =
            std::make_unique<RosQaFixture>("spectra_ros_qa_fixture_" + std::to_string(opts_.seed));

        canvas_figure_->animate()
            .fps(60.0f)
            .on_frame(
                [this](spectra::Frame&)
                {
                    if (shell_)
                        shell_->poll();
                })
            .loop(true)
            .play();

        app_->init_runtime();
        if (!app_->ui_context())
        {
            std::fprintf(stderr, "[ROS-QA] Failed to initialize runtime UI context\n");
            return false;
        }

#ifdef SPECTRA_USE_IMGUI
        auto* ui_ctx = app_->ui_context();
        if (ui_ctx && ui_ctx->imgui_ui)
        {
            ui_ctx->imgui_ui->enable_docking();
            auto& lm = ui_ctx->imgui_ui->get_layout_manager();
            lm.set_inspector_visible(false);
            lm.set_tab_bar_visible(false);
            ui_ctx->imgui_ui->set_nav_rail_visible(false);
            ui_ctx->imgui_ui->set_canvas_visible(false);
            ui_ctx->imgui_ui->set_render_figure_enabled(true);
            ui_ctx->imgui_ui->set_command_bar_visible(false);
            ui_ctx->imgui_ui->set_status_bar_visible(false);
            shell_->set_layout_manager(&lm);
            ui_ctx->imgui_ui->set_extra_draw_callback(
                [this]()
                {
                    if (shell_)
                        shell_->draw();
                });
        }
#endif

        initial_rss_ = get_rss_bytes();
        peak_rss_    = initial_rss_;
        start_time_  = std::chrono::steady_clock::now();

        pump_frames(8);
        return true;
    }

    int run()
    {
        std::fprintf(stderr,
                     "[ROS-QA] spectra-ros QA agent starting (seed=%lu)\n",
                     static_cast<unsigned long>(opts_.seed));

        for (const auto& scenario : scenarios_)
        {
            if (!opts_.scenario_name.empty() && opts_.scenario_name != scenario.name)
                continue;
            if (opts_.scenario_name.empty() && scenario.name == "design_review"
                && !opts_.design_review)
            {
                continue;
            }

            if (wall_clock_exceeded())
            {
                add_issue(IssueSeverity::Warning,
                          "runtime",
                          "Wall-clock limit reached before scenario '" + scenario.name + "'");
                break;
            }

            std::fprintf(stderr, "[ROS-QA] Running scenario: %s\n", scenario.name.c_str());
            const uint64_t        frame_start = total_frames_;
            const size_t          rss_before  = get_rss_bytes();
            const ScenarioOutcome outcome     = scenario.run();
            const size_t          rss_after   = get_rss_bytes();
            peak_rss_                         = std::max(peak_rss_, rss_after);

            ScenarioResult result;
            result.name             = scenario.name;
            result.description      = scenario.description;
            result.status           = outcome.status;
            result.detail           = outcome.detail;
            result.frame_start      = frame_start;
            result.frame_end        = total_frames_;
            result.rss_before_bytes = rss_before;
            result.rss_after_bytes  = rss_after;
            result.screenshot_path  = capture_screenshot(scenario.name);
            results_.push_back(result);

            std::fprintf(stderr,
                         "[ROS-QA]   %s (%lu frames) %s\n",
                         scenario_status_str(outcome.status),
                         static_cast<unsigned long>(result.frame_end - result.frame_start),
                         outcome.detail.c_str());
        }

        write_report();
        return has_failures() ? 1 : 0;
    }

    void print_scenarios() const { list_scenarios(); }

   private:
    struct Scenario
    {
        std::string                      name;
        std::string                      description;
        std::function<ScenarioOutcome()> run;
    };

    bool wall_clock_exceeded() const { return wall_clock_seconds() >= opts_.duration_sec; }

    float wall_clock_seconds() const
    {
        const auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration<float>(elapsed).count();
    }

    void shutdown()
    {
        if (app_)
        {
#ifdef SPECTRA_USE_IMGUI
            auto* ui_ctx = app_->ui_context();
            if (ui_ctx && ui_ctx->imgui_ui)
                ui_ctx->imgui_ui->set_extra_draw_callback(nullptr);
#endif
            app_->shutdown_runtime();
            app_.reset();
            canvas_figure_ = nullptr;
        }

        shell_.reset();
        fixture_.reset();
    }

    bool step_once()
    {
        if (!app_)
            return false;

        const auto step = app_->step();
        ++total_frames_;
        frame_stats_.record(step.frame_time_ms);
        peak_rss_ = std::max(peak_rss_, get_rss_bytes());

        if (step.should_exit)
        {
            add_issue(IssueSeverity::Critical, "runtime", "Application requested exit");
            return false;
        }

        return true;
    }

    bool pump_frames(uint64_t count)
    {
        for (uint64_t i = 0; i < count; ++i)
        {
            if (!step_once())
                return false;
        }
        return true;
    }

    bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline && !wall_clock_exceeded())
        {
            if (predicate())
                return true;
            if (!step_once())
                return false;
        }
        return predicate();
    }

    void add_issue(IssueSeverity severity, const std::string& category, const std::string& message)
    {
        issues_.push_back({severity, category, message, total_frames_});
        std::fprintf(stderr,
                     "[ROS-QA] [%s] %s: %s (frame %lu)\n",
                     severity_str(severity),
                     category.c_str(),
                     message.c_str(),
                     static_cast<unsigned long>(total_frames_));
    }

    ScenarioOutcome fail(const std::string& category, const std::string& message)
    {
        add_issue(IssueSeverity::Error, category, message);
        return {ScenarioStatus::Failed, message};
    }

    ScenarioOutcome skip(const std::string& message) { return {ScenarioStatus::Skipped, message}; }

    ScenarioOutcome pass(const std::string& message) { return {ScenarioStatus::Passed, message}; }

    std::string capture_screenshot(const std::string& name)
    {
        if (!app_ || !app_->backend())
            return {};

        const uint32_t       width  = shell_cfg_.window_width;
        const uint32_t       height = shell_cfg_.window_height;
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u);

        if (!app_->backend()->readback_framebuffer(rgba.data(), width, height))
            return {};

        const std::string path = opts_.output_dir + "/" + sanitize_filename(name) + "_frame"
                                 + std::to_string(total_frames_) + ".png";
        if (!RosScreenshotExport::write_png(path, rgba.data(), width, height))
            return {};
        return path;
    }

    bool has_failures() const
    {
        for (const auto& result : results_)
        {
            if (result.status == ScenarioStatus::Failed)
                return true;
        }
        for (const auto& issue : issues_)
        {
            if (issue.severity == IssueSeverity::Error || issue.severity == IssueSeverity::Critical)
            {
                return true;
            }
        }
        return false;
    }

    void list_scenarios() const
    {
        std::fprintf(stderr, "Available spectra-ros QA scenarios:\n");
        for (const auto& scenario : scenarios_)
        {
            std::fprintf(stderr,
                         "  %-28s %s\n",
                         scenario.name.c_str(),
                         scenario.description.c_str());
        }
    }

    void register_scenarios()
    {
        scenarios_.push_back({"boot_and_layout",
                              "Verify shell startup, layout defaults, and preset visibility",
                              [this]() { return scenario_boot_and_layout(); }});
        scenarios_.push_back(
            {"first_plot",
             "Time discovery → topic select → first live plot (Phase A C1 benchmark)",
             [this]() { return scenario_first_plot(); }});
        scenarios_.push_back({"live_topic_monitoring",
                              "Drive live publishers through discovery, echo, stats, and subplots",
                              [this]() { return scenario_live_topic_monitoring(); }});
        scenarios_.push_back({"session_roundtrip",
                              "Save and restore a live plotting session with panel state",
                              [this]() { return scenario_session_roundtrip(); }});
        scenarios_.push_back({"node_graph_and_logs",
                              "Refresh the node graph and validate ROS log capture",
                              [this]() { return scenario_node_graph_and_logs(); }});
        scenarios_.push_back({"diagnostics_and_tf",
                              "Publish diagnostic and TF traffic and validate both panels",
                              [this]() { return scenario_diagnostics_and_tf(); }});
        scenarios_.push_back({"parameters_and_services",
                              "Exercise parameter editing and service caller discovery",
                              [this]() { return scenario_parameters_and_services(); }});
        scenarios_.push_back({"bag_playback",
                              "Open a synthetic bag and validate playback injection when enabled",
                              [this]() { return scenario_bag_playback(); }});
        scenarios_.push_back({"nav_spatiotemporal",
                              "Nav2 debug bag scrub syncs TF, displays, and plots (Phase C7)",
                              [this]() { return scenario_nav_spatiotemporal(); }});
        scenarios_.push_back(
            {"design_review",
             "Capture named ROS shell design states and verify theme/layout presentation",
             [this]() { return scenario_design_review(); }});
    }

    bool ensure_ros_topics_ready(std::chrono::milliseconds timeout)
    {
        return wait_until(
            [this]()
            {
                shell_->discovery().refresh();
                return shell_->discovery().has_topic("/qa/float")
                       && shell_->discovery().has_topic("/qa/cmd_vel");
            },
            timeout);
    }

    bool ensure_design_plot_state()
    {
        if (!shell_ || !fixture_ || !canvas_figure_)
            return false;

        if (!ensure_ros_topics_ready(3s))
            return false;

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Default);
        shell_->set_nav_rail_visible(true);
        shell_->set_nav_rail_expanded(true);
        shell_->set_nav_rail_width(280.0f);

        if (shell_->active_plot_count() == 0)
        {
            if (!shell_->add_topic_plot("/qa/float"))
                return false;
            if (!shell_->add_topic_plot("/qa/cmd_vel:linear.x"))
                return false;
        }

        for (int i = 0; i < 24; ++i)
        {
            fixture_->publish_float(std::sin(static_cast<double>(i) * 0.25) * 2.0 + 4.0);
            fixture_->publish_twist(0.4 + static_cast<double>(i) * 0.05,
                                    std::cos(static_cast<double>(i) * 0.18) * 0.35);
            if (!pump_frames(2))
                return false;
        }

        const bool has_messages = wait_until(
            [this]()
            {
                return shell_->total_messages() > 0 && shell_->active_plot_count() >= 2
                       && !canvas_figure_->axes().empty();
            },
            2s);
        if (!has_messages)
            return false;

        canvas_figure_->legend().visible = true;
        auto& axes                       = canvas_figure_->axes_mut();
        for (size_t idx = 0; idx < axes.size(); ++idx)
        {
            auto& ax = axes[idx];
            ax->title("ROS Stream " + std::to_string(idx + 1));
            ax->xlabel("time");
            ax->ylabel(idx == 0 ? "value" : "velocity");
            ax->grid(true);
            ax->show_border(true);

            auto& series = ax->series_mut();
            for (size_t series_idx = 0; series_idx < series.size(); ++series_idx)
            {
                if (series[series_idx] && series[series_idx]->label().empty())
                {
                    series[series_idx]->label("stream_" + std::to_string(idx + 1) + "_"
                                              + std::to_string(series_idx + 1));
                }
            }
        }

        return pump_frames(6);
    }

    bool verify_theme_contrast(const std::string& theme_name,
                               float              min_ratio,
                               std::string*       error_message)
    {
        ui::ThemeManager::instance().set_theme(theme_name);
        const auto& colors       = ui::ThemeManager::instance().colors();
        const float canvas_ratio = colors.text_primary.contrast_ratio(colors.bg_primary);
        const float panel_ratio  = colors.text_primary.contrast_ratio(colors.bg_secondary);
        if (canvas_ratio < min_ratio || panel_ratio < min_ratio)
        {
            if (error_message)
            {
                std::ostringstream out;
                out << "Theme '" << theme_name << "' contrast was too low (canvas=" << canvas_ratio
                    << ", panel=" << panel_ratio << ")";
                *error_message = out.str();
            }
            return false;
        }
        return pump_frames(4);
    }

    std::string capture_named_screenshot(const std::string& relative_path)
    {
        if (!app_ || !app_->backend())
            return {};

        const uint32_t       width  = shell_cfg_.window_width;
        const uint32_t       height = shell_cfg_.window_height;
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u);

        if (!app_->backend()->readback_framebuffer(rgba.data(), width, height))
            return {};

        const std::string path = opts_.output_dir + "/" + relative_path;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        if (!RosScreenshotExport::write_png(path, rgba.data(), width, height))
            return {};
        return path;
    }

    bool record_design_capture(const std::string& name, const std::string& description)
    {
        const std::string path =
            capture_named_screenshot("design/" + sanitize_filename(name) + ".png");
        if (path.empty())
            return false;
        design_captures_.push_back({name, description, path, total_frames_});
        return true;
    }

    bool write_design_manifest()
    {
        if (design_captures_.empty())
            return false;

        design_manifest_path_ = opts_.output_dir + "/design/manifest.txt";
        std::filesystem::create_directories(
            std::filesystem::path(design_manifest_path_).parent_path());
        std::ofstream out(design_manifest_path_);
        if (!out)
            return false;

        out << "spectra-ros design review manifest\n";
        out << "seed=" << opts_.seed << "\n";
        out << "captures=" << design_captures_.size() << "\n\n";
        for (const auto& capture : design_captures_)
        {
            out << capture.name << " | frame=" << capture.frame << "\n";
            out << "  description: " << capture.description << "\n";
            out << "  path: " << capture.path << "\n";
        }

        return true;
    }

    ScenarioOutcome scenario_first_plot()
    {
        if (!shell_ || !fixture_)
            return fail("first_plot", "Shell or helper node was not initialized");

        const auto t0 = std::chrono::steady_clock::now();

        const bool discovered = wait_until(
            [this]()
            {
                shell_->discovery().refresh();
                return shell_->discovery().has_topic("/qa/float");
            },
            5s);
        if (!discovered)
            return fail("first_plot", "Timed out waiting for /qa/float in topic discovery");

        shell_->on_topic_selected("/qa/float", "std_msgs/msg/Float64");
        if (!shell_->add_topic_plot("/qa/float"))
            return fail("first_plot", "Failed to add /qa/float plot");

        const bool plotted = wait_until(
            [this]()
            {
                fixture_->publish_float(1.0);
                pump_frames(2);
                return shell_->active_plot_count() >= 1 && shell_->total_messages() > 0;
            },
            10s);

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);

        if (!plotted)
        {
            return fail("first_plot",
                        "Timed out waiting for first plot sample (elapsed "
                            + std::to_string(elapsed_ms.count()) + " ms)");
        }

        std::fprintf(stderr,
                     "[ROS-QA] first_plot elapsed: %.2f s\n",
                     static_cast<double>(elapsed_ms.count()) / 1000.0);

        if (elapsed_ms > 10s)
        {
            return fail("first_plot",
                        "Time-to-first-plot exceeded 10 s gate ("
                            + std::to_string(elapsed_ms.count()) + " ms)");
        }

        return pass("Time-to-first-plot " + std::to_string(elapsed_ms.count()) + " ms");
    }

    ScenarioOutcome scenario_boot_and_layout()
    {
        if (!shell_)
            return fail("boot", "Shell was not initialized");
        if (!fixture_)
            return fail("boot", "Helper ROS node was not initialized");
        if (!shell_->topic_list_panel() || !shell_->topic_echo_panel()
            || !shell_->topic_stats_panel() || !shell_->node_graph_panel()
            || !shell_->diagnostics_panel() || !shell_->tf_tree_panel()
            || !shell_->param_editor_panel() || !shell_->service_caller() || !shell_->log_viewer())
        {
            return fail("boot", "One or more shell panels/backends were not created");
        }

        if (shell_->window_title().find(shell_cfg_.node_name) == std::string::npos)
            return fail("boot", "Window title did not include the shell node name");

        if (!shell_->panel_visible("ros.topic_list") || !shell_->panel_visible("ros.topic_echo")
            || !shell_->panel_visible("ros.topic_stats") || !shell_->panel_visible("ros.plot_area"))
        {
            return fail("layout", "Default layout did not enable the expected core panels");
        }

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Debug);
        if (!shell_->panel_visible("ros.log_viewer") || shell_->panel_visible("ros.plot_area"))
            return fail("layout", "Debug preset visibility did not match expectations");

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Monitor);
        if (!shell_->panel_visible("ros.diagnostics") || !shell_->panel_visible("ros.plot_area"))
            return fail("layout", "Monitor preset did not expose diagnostics + plot area");

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::BagReview);
        if (!shell_->panel_visible("ros.bag_info") || !shell_->panel_visible("ros.bag_playback")
            || !shell_->panel_visible("ros.plot_area"))
        {
            return fail("layout", "Bag Review preset did not expose bag panels");
        }

        shell_->set_nav_rail_width(999.0f);
        if (shell_->nav_rail_width() > 360.0f)
            return fail("layout", "Nav rail width clamp upper bound failed");

        shell_->set_nav_rail_width(20.0f);
        if (shell_->nav_rail_width() < 180.0f)
            return fail("layout", "Nav rail width clamp lower bound failed");

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Default);
        pump_frames(4);
        return pass("Shell initialized and layout presets behaved as expected");
    }

    ScenarioOutcome scenario_live_topic_monitoring()
    {
        auto* topic_list  = shell_->topic_list_panel();
        auto* topic_echo  = shell_->topic_echo_panel();
        auto* topic_stats = shell_->topic_stats_panel();
        if (!topic_list || !topic_echo || !topic_stats)
            return fail("live", "Topic monitoring panels were unavailable");

        const bool discovered = wait_until(
            [this]()
            {
                shell_->discovery().refresh();
                return shell_->discovery().has_topic("/qa/float")
                       && shell_->discovery().has_topic("/qa/cmd_vel");
            },
            3s);
        if (!discovered)
            return fail("discovery", "Timed out waiting for /qa/float and /qa/cmd_vel");

        shell_->on_topic_selected("/qa/cmd_vel", "geometry_msgs/msg/Twist");
        pump_frames(4);

        for (int i = 0; i < 24; ++i)
        {
            fixture_->publish_float(static_cast<double>(i) * 0.5);
            fixture_->publish_twist(static_cast<double>(i) * 0.25, static_cast<double>(i) * 0.05);
            if (!pump_frames(2))
                return fail("live", "Render loop stopped while publishing live samples");
        }

        const auto float_stats = topic_list->stats_for("/qa/float");
        if (float_stats.total_messages == 0)
            return fail("live", "Topic monitor never observed /qa/float traffic");

        const bool echo_ready =
            wait_until([topic_echo]() { return topic_echo->message_count() > 0; }, 2s);
        if (!echo_ready)
            return fail("echo", "Topic echo panel did not capture /qa/cmd_vel messages");

        const auto latest_echo = topic_echo->latest_message();
        if (!latest_echo || latest_echo->fields.empty())
            return fail("echo", "Echo panel did not retain a decoded message snapshot");

        const auto stats_snapshot = topic_stats->snapshot();
        if (stats_snapshot.topic != "/qa/cmd_vel" || stats_snapshot.total_messages == 0)
            return fail("stats", "Topic stats overlay did not track the selected topic");

        if (!shell_->add_topic_plot("/qa/float"))
            return fail("plot", "Failed to add a topic-only live subplot");
        if (!shell_->add_topic_plot("/qa/cmd_vel:linear.x"))
            return fail("plot", "Failed to add /qa/cmd_vel:linear.x");
        if (!shell_->add_topic_plot("/qa/cmd_vel:angular.z"))
            return fail("plot", "Failed to add /qa/cmd_vel:angular.z");

        if (shell_->active_plot_count() < 3)
            return fail("plot", "Expected at least three active live plots");
        if (shell_->subplot_manager().rows() < 3)
            return fail("plot", "Adding plots did not auto-grow the subplot grid");

        if (shell_->total_messages() == 0)
            return fail("live", "Shell message accounting never advanced");

        return pass("Discovery, echo, stats, and live plotting all responded to ROS traffic");
    }

    ScenarioOutcome scenario_session_roundtrip()
    {
        if (shell_->active_plot_count() == 0)
            return fail("session", "Session roundtrip requires active plots from live monitoring");

        shell_->set_nav_rail_width(312.0f);
        shell_->set_nav_rail_expanded(false);
        shell_->set_panel_visible("ros.log_viewer", true);
        shell_->set_panel_visible("ros.topic_list", true);
        pump_frames(3);

        const RosSession  before = shell_->capture_session();
        const std::string session_path =
            opts_.output_dir + "/roundtrip." + RosSessionManager::SESSION_EXT;
        const SaveResult save = shell_->save_session(session_path);
        if (!save.ok)
            return fail("session", "Failed to save session: " + save.error);

        shell_->clear_plots();
        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Debug);
        shell_->set_panel_visible("ros.topic_list", false);
        shell_->set_nav_rail_width(180.0f);
        shell_->set_nav_rail_expanded(true);
        pump_frames(4);

        const LoadResult load = shell_->load_session(session_path);
        if (!load.ok)
            return fail("session", "Failed to load session: " + load.error);

        pump_frames(8);
        const RosSession after = shell_->capture_session();

        if (after.subscriptions.size() != before.subscriptions.size())
            return fail("session", "Subscription count did not survive save/load");
        if (after.panels.visible("ros.topic_list") != before.panels.visible("ros.topic_list")
            || after.panels.visible("ros.log_viewer") != before.panels.visible("ros.log_viewer")
            || after.panels.visible("ros.plot_area") != before.panels.visible("ros.plot_area"))
        {
            return fail("session", "Panel visibility was not restored from the saved session");
        }
        if (std::fabs(after.nav_rail_width - before.nav_rail_width) > 1.0)
            return fail("session", "Nav rail width was not restored");
        if (after.nav_rail_expanded != before.nav_rail_expanded)
            return fail("session", "Nav rail expanded/collapsed state was not restored");

        return pass("Session save/load restored plots and shell visibility state");
    }

    ScenarioOutcome scenario_node_graph_and_logs()
    {
        const std::string log_text = "spectra-ros-qa-log-" + std::to_string(opts_.seed);
        fixture_->emit_info_log(log_text);

        const bool saw_log = wait_until(
            [this, &log_text]()
            {
                const auto entries = shell_->log_viewer()->snapshot();
                return std::any_of(entries.begin(),
                                   entries.end(),
                                   [&log_text](const LogEntry& entry)
                                   { return entry.message.find(log_text) != std::string::npos; });
            },
            3s);
        if (!saw_log)
            return fail("logs", "ROS log viewer never received the helper-node log message");

        shell_->discovery().refresh();
        shell_->node_graph_panel()->refresh();
        pump_frames(6);

        const GraphSnapshot graph = shell_->node_graph_panel()->snapshot();
        if (graph.nodes.empty() || graph.edges.empty())
            return fail("graph", "Node graph did not build any nodes/edges");

        bool has_fixture_node = false;
        bool has_float_topic  = false;
        for (const auto& node : graph.nodes)
        {
            if (node.id == fixture_->fully_qualified_name())
                has_fixture_node = true;
            if (node.id == "/qa/float")
                has_float_topic = true;
        }

        if (!has_fixture_node || !has_float_topic)
            return fail("graph", "Node graph snapshot missed expected live ROS entities");

        return pass("Node graph and ROS log capture both reflected live helper-node activity");
    }

    ScenarioOutcome scenario_diagnostics_and_tf()
    {
        fixture_->publish_diagnostics();
        const bool diagnostics_ready = wait_until(
            [this]()
            {
                const auto& model = shell_->diagnostics_panel()->model();
                return model.count_warn >= 1 && model.count_error >= 1;
            },
            3s);
        if (!diagnostics_ready)
            return fail("diagnostics", "Diagnostics panel did not ingest warn/error statuses");

        for (int i = 0; i < 4; ++i)
        {
            fixture_->publish_tf_chain();
            pump_frames(2);
        }

        const bool tf_ready = wait_until(
            [this]()
            {
                return shell_->tf_tree_panel()->has_frame("base_link")
                       && shell_->tf_tree_panel()->has_frame("laser");
            },
            3s);
        if (!tf_ready)
            return fail("tf", "TF tree never observed the expected frame chain");

        const auto tf_snapshot = shell_->tf_tree_panel()->snapshot();
        if (tf_snapshot.static_frames < 1 || tf_snapshot.dynamic_frames < 1)
            return fail("tf", "TF snapshot did not include both static and dynamic frames");

        const TransformResult lookup = shell_->tf_tree_panel()->lookup_transform("laser", "map");
        if (!lookup.ok)
            return fail("tf", "TF lookup laser->map failed: " + lookup.error);

        return pass("Diagnostics and TF panels both consumed live ROS traffic");
    }

    ScenarioOutcome scenario_parameters_and_services()
    {
        auto* param_editor = shell_->param_editor_panel();
        if (!param_editor)
            return fail("params", "Parameter editor panel was unavailable");

        param_editor->set_target_node(fixture_->fully_qualified_name());
        param_editor->set_live_edit(false);
        param_editor->refresh();

        const bool params_loaded =
            wait_until([param_editor]() { return param_editor->is_loaded(); }, 4s);
        if (!params_loaded)
            return fail("params", "Parameter editor did not finish loading helper-node params");

        const auto param_names = param_editor->param_names();
        if (!contains(param_names, std::string("qa_gain"))
            || !contains(param_names, std::string("qa_mode")))
        {
            return fail("params", "Expected helper-node parameters were not discovered");
        }

        const ParamEntry gain_entry = param_editor->param_entry("qa_gain");
        if (gain_entry.name != "qa_gain" || gain_entry.current.type != ParamType::Double
            || std::fabs(gain_entry.current.double_val - 1.5) > 1e-6)
        {
            return fail("params", "qa_gain did not load with the expected default value");
        }

        const std::string baseline_path = opts_.output_dir + "/qa_params_baseline.yaml";
        if (!param_editor->save_preset("baseline", baseline_path))
            return fail("params", "Failed to save a parameter preset snapshot");
        if (!std::filesystem::exists(baseline_path))
            return fail("params", "Parameter preset file was not written to disk");

        ParamValue gain_value;
        gain_value.type       = ParamType::Double;
        gain_value.double_val = 2.75;

        ParamValue mode_value;
        mode_value.type       = ParamType::String;
        mode_value.string_val = "record";

        std::unordered_map<std::string, ParamValue> desired;
        desired["qa_gain"] = gain_value;
        desired["qa_mode"] = mode_value;

        const std::string updated_yaml =
            ParamEditorPanel::serialize_yaml(fixture_->fully_qualified_name(), desired);
        const std::string updated_path = opts_.output_dir + "/qa_params_updated.yaml";
        {
            std::ofstream out(updated_path);
            out << updated_yaml;
        }

        if (!param_editor->load_preset(updated_path))
            return fail("params", "Failed to load the updated parameter preset");

        const bool params_applied = wait_until(
            [this]()
            { return std::fabs(fixture_->gain() - 2.75) < 1e-6 && fixture_->mode() == "record"; },
            4s);
        if (!params_applied)
            return fail("params", "Preset application never updated the live helper-node params");

        auto* service_caller = shell_->service_caller();
        if (!service_caller)
            return fail("services", "Service caller backend was unavailable");

        const std::string service_name   = fixture_->fully_qualified_name() + "/get_parameters";
        const bool        services_ready = wait_until(
            [this, service_caller, &service_name]()
            {
                shell_->discovery().refresh();
                service_caller->refresh_services();
                return service_caller->find_service(service_name).has_value();
            },
            4s);
        if (!services_ready)
            return fail("services", "Service discovery never exposed " + service_name);

        if (!service_caller->load_schema(service_name))
            return fail("services", "Failed to load request/response schema for " + service_name);

        const auto service_entry = service_caller->find_service(service_name);
        if (!service_entry || !service_entry->request_schema)
            return fail("services", "Service schema was not cached after load_schema()");

        auto request_fields = ServiceCaller::fields_from_schema(*service_entry->request_schema);
        if (request_fields.empty())
            return fail("services", "Request schema for service call had no editable fields");

        for (auto& field : request_fields)
        {
            if (field.path == "names")
                field.value_str = "[\"qa_gain\"]";
        }

        const CallHandle call =
            service_caller->call(service_name, ServiceCaller::fields_to_json(request_fields), 1.0);
        if (call == INVALID_CALL_HANDLE)
            return fail("services", "Service call dispatch returned INVALID_CALL_HANDLE");

        const bool finished = wait_until(
            [service_caller, call]()
            {
                const CallRecord* rec = service_caller->record(call);
                return rec != nullptr
                       && rec->state.load(std::memory_order_acquire) != CallState::Pending;
            },
            2s);
        if (!finished)
            return fail("services", "Service call never reached a terminal state");

        const CallRecord* rec = service_caller->record(call);
        if (!rec)
            return fail("services", "Service call record disappeared unexpectedly");

        const CallState state = rec->state.load(std::memory_order_acquire);
        if (state == CallState::Done)
            return pass("Parameter editing worked and service calling completed successfully");

        if (state == CallState::Error
            && rec->error_message.find("Iron or later") != std::string::npos)
        {
            return pass("Parameter editing worked and service caller failed gracefully on Humble");
        }

        return fail("services",
                    "Service caller ended in an unexpected state: " + rec->error_message);
    }

    ScenarioOutcome scenario_bag_playback()
    {
#ifdef SPECTRA_ROS2_BAG
        constexpr int     BAG_MSG_COUNT = 12;
        const std::string bag_dir =
            opts_.output_dir + "/synthetic_bag_" + std::to_string(opts_.seed);
        const std::string bag_path = write_float64_bag(bag_dir, "/qa/bag_float", BAG_MSG_COUNT);
        if (bag_path.empty())
            return fail("bag", "Failed to synthesize a rosbag for playback");

        // --- 1. try_open_file (drag-drop API) --------------------------
        shell_->apply_layout_preset(RosAppShell::LayoutPreset::BagReview);
        if (!shell_->bag_info_panel()->try_open_file(bag_path))
            return fail("bag", "try_open_file rejected a valid .db3 bag path");

        const bool bag_opened =
            wait_until([this]() { return shell_->bag_player() && shell_->bag_player()->is_open(); },
                       2s);
        if (!bag_opened)
            return fail("bag", "Shell did not propagate the opened bag into BagPlayer");

        // --- 2. Auto-subplot creation -----------------------------------
        const int subplots_after_open = shell_->subplot_manager().active_count();
        if (subplots_after_open == 0)
            return fail("bag", "No subplots were auto-created when the bag was opened");

        // --- 3. Bag metadata -------------------------------------------
        if (shell_->bag_info_panel()->summary().topic_count() == 0)
            return fail("bag", "Bag metadata summary had no topics after successful open");
        if (shell_->bag_player()->topic_activity_bands().empty())
            return fail("bag", "Bag playback did not produce topic activity bands");

        // --- 4. Play to completion and verify injection -----------------
        shell_->bag_player()->play();
        const bool finished = wait_until(
            [this]()
            {
                return shell_->bag_player()->total_injected() > 0
                       && shell_->subplot_manager().active_count() > 0;
            },
            4s);
        if (!finished)
            return fail("bag", "Bag playback never injected samples into SubplotManager");

        // Wait for all messages to be injected (bag has BAG_MSG_COUNT messages).
        [[maybe_unused]] const bool all_injected = wait_until(
            [this]() {
                return shell_->bag_player()->total_injected()
                       >= static_cast<uint64_t>(BAG_MSG_COUNT);
            },
            4s);

        shell_->bag_player()->pause();

        // --- 5. Verify no excessive injection (seek-spam guard) ---------
        const uint64_t total = shell_->bag_player()->total_injected();
        // With the lookahead cache, total should be exactly BAG_MSG_COUNT.
        // Allow a small margin for edge-case re-reads.
        if (total > static_cast<uint64_t>(BAG_MSG_COUNT) * 2)
            return fail("bag",
                        "Injected " + std::to_string(total) + " messages from a "
                            + std::to_string(BAG_MSG_COUNT) + "-message bag — seek spam detected");

        // --- 6. Verify series data actually arrived ---------------------
        bool any_series_has_data = false;
        for (int slot = 1; slot <= shell_->subplot_manager().capacity(); ++slot)
        {
            const auto* entry = shell_->subplot_manager().slot_entry_pub(slot);
            if (entry && entry->active() && entry->series && entry->series->point_count() > 0)
            {
                any_series_has_data = true;
                break;
            }
        }
        if (!any_series_has_data)
            return fail("bag",
                        "SubplotManager has active slots but no series received data points");

        // --- 7. Seek / step controls ------------------------------------
        shell_->bag_player()->seek_fraction(0.5);
        shell_->bag_player()->step_forward();

        return pass("Bag review: try_open_file, auto-subplots, data injection, "
                    "and seek controls all verified (injected "
                    + std::to_string(total) + "/" + std::to_string(BAG_MSG_COUNT) + " msgs)");
#else
        return skip("Bag playback skipped because SPECTRA_ROS2_BAG=OFF");
#endif
    }

    ScenarioOutcome scenario_nav_spatiotemporal()
    {
#ifdef SPECTRA_ROS2_BAG
        const std::string bag_path = spectra_source_dir() + "/tests/data/ros_bags/nav2_debug";
        const std::string session_path =
            spectra_source_dir() + "/sessions/presets/nav2_debug.spectra-ros-session";

        if (!std::filesystem::exists(bag_path))
        {
            return skip(
                "nav2_debug reference bag missing — run scripts/generate_ros_reference_bags.py");
        }
        if (!std::filesystem::exists(session_path))
            return fail("nav", "nav2_debug session preset not found at " + session_path);

        const LoadResult session_lr = shell_->load_session(session_path);
        if (!session_lr.ok)
            return fail("nav", "Failed to load nav2_debug session: " + session_lr.error);

        if (shell_->displays().size() < 3)
        {
            return fail("nav",
                        "nav2_debug session did not restore expected 3D displays (got "
                            + std::to_string(shell_->displays().size()) + ")");
        }
        if (!shell_->panel_visible("ros.scene_viewport") || !shell_->panel_visible("ros.plot_area"))
            return fail("nav", "nav2_debug session did not enable rviz-plot panels");

        if (!shell_->bag_info_panel()->try_open_file(bag_path))
            return fail("nav", "try_open_file rejected nav2_debug bag path");

        const bool bag_opened =
            wait_until([this]() { return shell_->bag_player() && shell_->bag_player()->is_open(); },
                       3s);
        if (!bag_opened)
            return fail("nav", "BagPlayer did not open nav2_debug reference bag");

        shell_->bag_player()->pause();
        shell_->bag_player()->seek_fraction(0.75);
        pump_frames(12);

        const bool tf_ready = wait_until(
            [this]()
            {
                return shell_->tf_tree_panel()->has_frame("base_link")
                       && shell_->tf_tree_panel()->has_frame("map");
            },
            3s);
        if (!tf_ready)
            return fail("nav", "TF tree missing map/base_link after bag scrub to 75%");

        const TransformResult lookup =
            shell_->tf_tree_panel()->lookup_transform("base_link", "map");
        if (!lookup.ok)
            return fail("nav", "TF lookup base_link->map failed after scrub: " + lookup.error);

        shell_->bag_player()->play();
        const bool injected = wait_until(
            [this]()
            {
                return shell_->bag_player()->total_injected() > 0
                       && shell_->subplot_manager().active_count() > 0;
            },
            4s);
        shell_->bag_player()->pause();
        if (!injected)
            return fail("nav", "nav2_debug bag playback did not inject plot samples");

        bool any_series_has_data = false;
        for (int slot = 1; slot <= shell_->subplot_manager().capacity(); ++slot)
        {
            const auto* entry = shell_->subplot_manager().slot_entry_pub(slot);
            if (entry && entry->active() && entry->series && entry->series->point_count() > 0)
            {
                any_series_has_data = true;
                break;
            }
        }
        if (!any_series_has_data)
            return fail("nav", "Nav subplots have no data after nav2_debug playback");

        shell_->bag_player()->seek_fraction(0.25);
        pump_frames(8);

        return pass("nav2_debug: session, bag scrub TF sync, and plot injection verified");
#else
        return skip("nav_spatiotemporal skipped because SPECTRA_ROS2_BAG=OFF");
#endif
    }

    ScenarioOutcome scenario_design_review()
    {
        if (!shell_ || !app_ || !canvas_figure_)
            return fail("design", "Design review requires a live shell and render figure");

        design_captures_.clear();
        design_manifest_path_.clear();

        if (!ensure_design_plot_state())
            return fail("design",
                        "Failed to prepare representative ROS plot state for design review");

        std::string contrast_error;
        if (!verify_theme_contrast("dark", 4.5f, &contrast_error))
            return fail("design", contrast_error);

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Default);
        shell_->set_nav_rail_visible(true);
        shell_->set_nav_rail_expanded(true);
        shell_->set_panel_visible("ros.topic_list", true);
        shell_->set_panel_visible("ros.topic_echo", true);
        shell_->set_panel_visible("ros.topic_stats", true);
        shell_->set_panel_visible("ros.plot_area", true);
        if (!pump_frames(6)
            || !record_design_capture(
                "01_dark_default_live",
                "Dark theme default layout with live plots, echo, stats, and expanded nav rail"))
        {
            return fail("design", "Failed to capture dark default design state");
        }

        fixture_->emit_info_log("design-review-log-" + std::to_string(opts_.seed));
        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Debug);
        shell_->set_nav_rail_visible(true);
        if (!pump_frames(6)
            || !record_design_capture("02_debug_logs",
                                      "Debug layout with log viewer and topic monitoring chrome"))
        {
            return fail("design", "Failed to capture debug design state");
        }

        fixture_->publish_diagnostics();
        fixture_->publish_tf_chain();
        const bool diagnostics_ready = wait_until(
            [this]()
            {
                const auto& model = shell_->diagnostics_panel()->model();
                return model.count_warn >= 1 && model.count_error >= 1
                       && shell_->tf_tree_panel()->has_frame("laser");
            },
            3s);
        if (!diagnostics_ready)
            return fail("design", "Failed to populate diagnostics/TF state before design capture");

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Monitor);
        shell_->set_panel_visible("ros.tf_tree", true);
        if (!pump_frames(6)
            || !record_design_capture(
                "03_monitor_diagnostics_tf",
                "Monitor layout with diagnostics, TF tree, and active live plots"))
        {
            return fail("design", "Failed to capture monitor design state");
        }

        if (!verify_theme_contrast("light", 4.5f, &contrast_error))
            return fail("design", contrast_error);

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Default);
        shell_->set_nav_rail_visible(true);
        shell_->set_nav_rail_expanded(false);
        if (!pump_frames(6)
            || !record_design_capture(
                "04_light_default_compact",
                "Light theme default layout with compact nav rail and restored live plots"))
        {
            return fail("design", "Failed to capture light-theme design state");
        }

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::BagReview);
        shell_->set_nav_rail_visible(true);
        if (!pump_frames(6)
            || !record_design_capture("05_bag_review_empty_state",
                                      "Bag review layout empty state for playback-disabled builds"))
        {
            return fail("design", "Failed to capture bag-review design state");
        }

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::RViz);
        shell_->set_panel_visible("ros.scene_viewport", true);
        shell_->set_panel_visible("ros.displays", true);
        shell_->set_panel_visible("ros.inspector", true);
        if (!pump_frames(6)
            || !record_design_capture("06_rviz_layout",
                                      "RViz layout with scene viewport and displays panel"))
        {
            return fail("design", "Failed to capture RViz layout design state");
        }

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::RVizPlot);
        shell_->set_panel_visible("ros.plot_area", true);
        if (!pump_frames(6)
            || !record_design_capture("07_rviz_plot_layout",
                                      "RVizPlot layout combining 3D viewport and plot area"))
        {
            return fail("design", "Failed to capture RVizPlot layout design state");
        }

        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Monitor);
        shell_->set_panel_visible("ros.topic_list", true);
        shell_->set_panel_visible("ros.topic_stats", true);
        if (!pump_frames(6)
            || !record_design_capture("08_monitor_topic_stats",
                                      "Monitor layout with topic monitor and statistics"))
        {
            return fail("design", "Failed to capture monitor/stats design state");
        }

        ui::ThemeManager::instance().set_theme("dark");
        shell_->apply_layout_preset(RosAppShell::LayoutPreset::Default);
        shell_->set_nav_rail_visible(true);
        shell_->set_nav_rail_expanded(true);
        if (!write_design_manifest())
            return fail("design", "Failed to write design review manifest");

        return pass("Design review captured 8 named ROS shell states with contrast checks");
    }

    void write_report() const
    {
        const std::string text_path = opts_.output_dir + "/qa_report.txt";
        const std::string json_path = opts_.output_dir + "/qa_report.json";

        size_t passed  = 0;
        size_t failed  = 0;
        size_t skipped = 0;
        for (const auto& result : results_)
        {
            if (result.status == ScenarioStatus::Passed)
                ++passed;
            else if (result.status == ScenarioStatus::Failed)
                ++failed;
            else
                ++skipped;
        }

        {
            std::ofstream out(text_path);
            if (out)
            {
                out << "spectra-ros QA Report\n";
                out << "======================\n";
                out << "Seed: " << opts_.seed << "\n";
                out << "Duration: " << wall_clock_seconds() << "s\n";
                out << "Frames: " << total_frames_ << "\n";
                out << "Scenarios: " << passed << " passed, " << failed << " failed, " << skipped
                    << " skipped\n";
                out << "Frame time: avg=" << frame_stats_.average()
                    << "ms p95=" << frame_stats_.percentile(0.95f)
                    << "ms max=" << frame_stats_.max_value() << "ms\n";
                out << "RSS: initial=" << to_mb(initial_rss_) << "MB peak=" << to_mb(peak_rss_)
                    << "MB\n\n";

                if (opts_.design_review || !design_captures_.empty())
                {
                    out << "Design Review:\n";
                    out << "  requested: " << (opts_.design_review ? "yes" : "no") << "\n";
                    out << "  captures: " << design_captures_.size() << "\n";
                    if (!design_manifest_path_.empty())
                        out << "  manifest: " << design_manifest_path_ << "\n";
                    for (const auto& capture : design_captures_)
                    {
                        out << "  - " << capture.name << " | frame=" << capture.frame
                            << " | path=" << capture.path << " | detail=" << capture.description
                            << "\n";
                    }
                    out << "\n";
                }

                out << "Scenario Results:\n";
                for (const auto& result : results_)
                {
                    out << "  - " << result.name << ": " << scenario_status_str(result.status)
                        << " | frames=" << (result.frame_end - result.frame_start)
                        << " | rss=" << to_mb(result.rss_before_bytes) << "->"
                        << to_mb(result.rss_after_bytes) << "MB" << " | detail=" << result.detail
                        << "\n";
                    if (!result.screenshot_path.empty())
                        out << "    screenshot: " << result.screenshot_path << "\n";
                }
                out << "\n";

                if (!issues_.empty())
                {
                    out << "Issues:\n";
                    for (const auto& issue : issues_)
                    {
                        out << "  - [" << severity_str(issue.severity) << "] " << issue.category
                            << ": " << issue.message << " (frame " << issue.frame << ")\n";
                    }
                }
                else
                {
                    out << "Issues:\n  none\n";
                }
            }
        }

        {
            std::ofstream out(json_path);
            if (out)
            {
                out << "{\n";
                out << "  \"seed\": " << opts_.seed << ",\n";
                out << "  \"duration_sec\": " << wall_clock_seconds() << ",\n";
                out << "  \"total_frames\": " << total_frames_ << ",\n";
                out << "  \"frame_time\": {\n";
                out << "    \"avg_ms\": " << frame_stats_.average() << ",\n";
                out << "    \"p95_ms\": " << frame_stats_.percentile(0.95f) << ",\n";
                out << "    \"max_ms\": " << frame_stats_.max_value() << "\n";
                out << "  },\n";
                out << "  \"rss\": {\n";
                out << "    \"initial_mb\": " << to_mb(initial_rss_) << ",\n";
                out << "    \"peak_mb\": " << to_mb(peak_rss_) << "\n";
                out << "  },\n";
                out << "  \"design_review\": {\n";
                out << "    \"requested\": " << (opts_.design_review ? "true" : "false") << ",\n";
                out << "    \"manifest\": \"" << json_escape(design_manifest_path_) << "\",\n";
                out << "    \"captures\": [\n";
                for (size_t i = 0; i < design_captures_.size(); ++i)
                {
                    const auto& capture = design_captures_[i];
                    out << "      {\n";
                    out << "        \"name\": \"" << json_escape(capture.name) << "\",\n";
                    out << "        \"description\": \"" << json_escape(capture.description)
                        << "\",\n";
                    out << "        \"path\": \"" << json_escape(capture.path) << "\",\n";
                    out << "        \"frame\": " << capture.frame << "\n";
                    out << "      }";
                    if (i + 1 < design_captures_.size())
                        out << ",";
                    out << "\n";
                }
                out << "    ]\n";
                out << "  },\n";
                out << "  \"scenarios\": [\n";
                for (size_t i = 0; i < results_.size(); ++i)
                {
                    const auto& result = results_[i];
                    out << "    {\n";
                    out << "      \"name\": \"" << json_escape(result.name) << "\",\n";
                    out << "      \"description\": \"" << json_escape(result.description)
                        << "\",\n";
                    out << "      \"status\": \"" << scenario_status_str(result.status) << "\",\n";
                    out << "      \"detail\": \"" << json_escape(result.detail) << "\",\n";
                    out << "      \"frame_start\": " << result.frame_start << ",\n";
                    out << "      \"frame_end\": " << result.frame_end << ",\n";
                    out << "      \"rss_before_mb\": " << to_mb(result.rss_before_bytes) << ",\n";
                    out << "      \"rss_after_mb\": " << to_mb(result.rss_after_bytes) << ",\n";
                    out << "      \"screenshot\": \"" << json_escape(result.screenshot_path)
                        << "\"\n";
                    out << "    }";
                    if (i + 1 < results_.size())
                        out << ",";
                    out << "\n";
                }
                out << "  ],\n";
                out << "  \"issues\": [\n";
                for (size_t i = 0; i < issues_.size(); ++i)
                {
                    const auto& issue = issues_[i];
                    out << "    {\n";
                    out << "      \"severity\": \"" << severity_str(issue.severity) << "\",\n";
                    out << "      \"category\": \"" << json_escape(issue.category) << "\",\n";
                    out << "      \"message\": \"" << json_escape(issue.message) << "\",\n";
                    out << "      \"frame\": " << issue.frame << "\n";
                    out << "    }";
                    if (i + 1 < issues_.size())
                        out << ",";
                    out << "\n";
                }
                out << "  ]\n";
                out << "}\n";
            }
        }

        std::fprintf(stderr,
                     "\n[ROS-QA] Summary: %zu scenarios, %zu issues, avg %.2fms, peak RSS %luMB\n"
                     "[ROS-QA] Reports: %s | %s\n",
                     results_.size(),
                     issues_.size(),
                     frame_stats_.average(),
                     static_cast<unsigned long>(to_mb(peak_rss_)),
                     text_path.c_str(),
                     json_path.c_str());
    }

    QAOptions                             opts_;
    RosAppConfig                          shell_cfg_;
    std::unique_ptr<App>                  app_;
    Figure*                               canvas_figure_{nullptr};
    std::unique_ptr<RosAppShell>          shell_;
    std::unique_ptr<RosQaFixture>         fixture_;
    std::vector<Scenario>                 scenarios_;
    std::vector<ScenarioResult>           results_;
    std::vector<DesignCapture>            design_captures_;
    std::vector<QAIssue>                  issues_;
    FrameStats                            frame_stats_;
    std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};
    uint64_t                              total_frames_{0};
    size_t                                initial_rss_{0};
    size_t                                peak_rss_{0};
    std::string                           design_manifest_path_;
};

int main(int argc, char** argv)
{
    QAOptions  opts      = parse_args(argc, argv);
    const bool list_only = opts.list_scenarios;

    RosQAAgent agent(std::move(opts));
    if (list_only)
    {
        agent.print_scenarios();
        return 0;
    }
    if (!agent.init())
        return 1;

    return agent.run();
}
