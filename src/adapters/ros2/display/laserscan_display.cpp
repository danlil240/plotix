#include "display/laserscan_display.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>

#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"
#include "topic_discovery.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{
uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
           | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

std::array<float, 4> unpack_rgba(uint32_t rgba)
{
    return {
        static_cast<float>((rgba >> 0) & 0xFFu) / 255.0f,
        static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f,
        static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f,
    };
}

uint32_t gradient_color(float t, uint8_t alpha = 0xFFu)
{
    t = std::clamp(t, 0.0f, 1.0f);

    const auto lerp = [](float a, float b, float u) { return a + (b - a) * u; };
    const auto lerp_color =
        [&](const std::array<float, 3>& a, const std::array<float, 3>& b, float u)
    {
        return std::array<uint8_t, 3>{
            static_cast<uint8_t>(std::clamp(lerp(a[0], b[0], u), 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(std::clamp(lerp(a[1], b[1], u), 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(std::clamp(lerp(a[2], b[2], u), 0.0f, 1.0f) * 255.0f),
        };
    };

    constexpr std::array<float, 3> kBlue{0.15f, 0.35f, 0.95f};
    constexpr std::array<float, 3> kCyan{0.10f, 0.85f, 0.95f};
    constexpr std::array<float, 3> kGreen{0.20f, 0.82f, 0.28f};
    constexpr std::array<float, 3> kYellow{0.96f, 0.86f, 0.22f};
    constexpr std::array<float, 3> kRed{0.95f, 0.26f, 0.16f};

    std::array<uint8_t, 3> rgb{};
    if (t < 0.25f)
        rgb = lerp_color(kBlue, kCyan, t / 0.25f);
    else if (t < 0.5f)
        rgb = lerp_color(kCyan, kGreen, (t - 0.25f) / 0.25f);
    else if (t < 0.75f)
        rgb = lerp_color(kGreen, kYellow, (t - 0.5f) / 0.25f);
    else
        rgb = lerp_color(kYellow, kRed, (t - 0.75f) / 0.25f);

    return pack_rgba(rgb[0], rgb[1], rgb[2], alpha);
}

float normalize_range(float value, float min_value, float max_value)
{
    const float denom = max_value - min_value;
    if (!std::isfinite(value) || denom <= 1e-6f)
        return 0.5f;
    return std::clamp((value - min_value) / denom, 0.0f, 1.0f);
}

spectra::Transform resolve_frame_transform(const TfBuffer*    tf_buffer,
                                           const std::string& fixed_frame,
                                           const std::string& frame_id,
                                           uint64_t           stamp_ns,
                                           bool               use_message_stamp,
                                           bool&              ok_out)
{
    ok_out = true;
    spectra::Transform transform{};
    if (!tf_buffer || fixed_frame.empty() || frame_id.empty() || frame_id == fixed_frame)
        return transform;

    const TransformResult result =
        tf_buffer->lookup_transform(fixed_frame, frame_id, use_message_stamp ? stamp_ns : 0);
    if (!result.ok)
    {
        ok_out = false;
        return transform;
    }

    transform.translation = {result.tx, result.ty, result.tz};
    transform.rotation    = {
        static_cast<float>(result.qx),
        static_cast<float>(result.qy),
        static_cast<float>(result.qz),
        static_cast<float>(result.qw),
    };
    return transform;
}

uint32_t laserscan_flat_color(float alpha)
{
    const uint8_t a = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return pack_rgba(128, 226, 255, a);
}
}   // namespace

LaserScanDisplay::LaserScanDisplay()
{
    set_topic("/scan");
}

void LaserScanDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for laser scan topic";
}

void LaserScanDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void LaserScanDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(scans_mutex_);
    scans_.clear();
}

void LaserScanDisplay::on_update(float)
{
    ensure_subscription();

    std::lock_guard<std::mutex> lock(scans_mutex_);
    while (static_cast<int>(scans_.size()) > trail_size_)
        scans_.pop_front();

    if (status_ == DisplayStatus::Disabled)
        return;
    if (scans_.empty())
    {
        status_      = DisplayStatus::Warn;
        status_text_ = subscribed_topic_.empty() ? "Waiting for laser scan topic"
                                                 : "Subscribed, no scans received";
        return;
    }

    status_      = DisplayStatus::Ok;
    status_text_ = std::to_string(scans_.back().point_count) + " points";
    if (scans_.size() > 1)
        status_text_ += " x" + std::to_string(scans_.size());
}

void LaserScanDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    std::deque<LaserScanFrame> scans;
    {
        std::lock_guard<std::mutex> lock(scans_mutex_);
        scans = scans_;
    }

    for (size_t index = 0; index < scans.size(); ++index)
    {
        const auto&              frame           = scans[index];
        bool                     tf_ok           = true;
        const spectra::Transform frame_transform = resolve_frame_transform(tf_buffer_,
                                                                           fixed_frame_,
                                                                           frame.frame_id,
                                                                           frame.stamp_ns,
                                                                           use_message_stamp_,
                                                                           tf_ok);
        if (!tf_ok)
            continue;

        SceneEntity entity;
        entity.type                  = "laserscan";
        entity.label                 = topic_.empty() ? display_name()
                                                      : topic_ + " [" + std::to_string(index + 1) + "/"
                                            + std::to_string(scans.size()) + "]";
        entity.display_name          = display_name();
        entity.topic                 = frame.topic;
        entity.frame_id              = frame.frame_id;
        entity.transform             = frame_transform;
        entity.transform.translation = frame_transform.transform_point(frame.centroid);
        entity.scale                 = frame.max_bounds - frame.min_bounds;
        entity.stamp_ns              = frame.stamp_ns;

        const float fade =
            scans.size() <= 1
                ? 1.0f
                : 0.25f
                      + 0.75f * (static_cast<float>(index) / static_cast<float>(scans.size() - 1));
        const bool use_range_color     = color_mode_ == ColorMode::Range;
        const bool use_intensity_color = color_mode_ == ColorMode::Intensity && frame.has_intensity;
        const uint32_t default_rgba    = laserscan_flat_color(fade);
        const auto     default_color   = unpack_rgba(default_rgba);
        std::string    color_buffer    = std::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}",
                                               default_color[0],
                                               default_color[1],
                                               default_color[2],
                                               default_color[3]);

        if (render_style_ == RenderStyle::Points)
        {
            ScenePointSet point_set;
            point_set.point_size          = 4.0f;
            point_set.default_rgba        = default_rgba;
            point_set.use_per_point_color = use_range_color || use_intensity_color;
            point_set.transparent         = fade < 0.999f;
            point_set.points.reserve(frame.points.size());

            for (const auto& point : frame.points)
            {
                ScenePoint scene_point;
                scene_point.position = point.position - frame.centroid;

                if (use_intensity_color && point.has_intensity)
                {
                    scene_point.rgba = gradient_color(
                        normalize_range(point.intensity, frame.min_intensity, frame.max_intensity),
                        static_cast<uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));
                }
                else if (use_range_color)
                {
                    scene_point.rgba = gradient_color(
                        normalize_range(point.range, frame.min_range, frame.max_range),
                        static_cast<uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));
                }
                else
                {
                    scene_point.rgba = default_rgba;
                }

                point_set.points.push_back(scene_point);
            }
            entity.point_set = std::move(point_set);
        }
        else
        {
            ScenePolyline polyline;
            polyline.points.reserve(frame.points.size());
            for (const auto& point : frame.points)
                polyline.points.push_back(point.position - frame.centroid);
            entity.polyline = std::move(polyline);

            uint32_t line_rgba = default_rgba;
            if (use_intensity_color)
            {
                line_rgba =
                    gradient_color(normalize_range(frame.average_intensity,
                                                   frame.min_intensity,
                                                   frame.max_intensity),
                                   static_cast<uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));
            }
            else if (use_range_color)
            {
                line_rgba = gradient_color(
                    normalize_range(frame.average_range, frame.min_range, frame.max_range),
                    static_cast<uint8_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f));
            }

            const auto line_color = unpack_rgba(line_rgba);
            color_buffer          = std::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}",
                                       line_color[0],
                                       line_color[1],
                                       line_color[2],
                                       line_color[3]);
            entity.properties.push_back({"line_width", "2.0"});
        }

        entity.properties.push_back({"points", std::to_string(frame.point_count)});
        entity.properties.push_back({"render_style", render_style_name(render_style_)});
        entity.properties.push_back({"color_mode", color_mode_name(color_mode_)});
        entity.properties.push_back(
            {"gpu_color_mode",
             (entity.point_set.has_value() && entity.point_set->use_per_point_color) ? "per-point"
                                                                                     : "flat"});
        entity.properties.push_back({"min_range", std::to_string(frame.min_range)});
        entity.properties.push_back({"max_range", std::to_string(frame.max_range)});
        entity.properties.push_back({"average_range", std::to_string(frame.average_range)});
        entity.properties.push_back({"has_intensity", frame.has_intensity ? "true" : "false"});
        entity.properties.push_back({"color", color_buffer});
        scene.add_entity(std::move(entity));
    }
}

void LaserScanDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    const char* render_styles[] = {"Points", "Lines"};
    int         render_style    = static_cast<int>(render_style_);
    if (ImGui::Combo("Render Style", &render_style, render_styles, 2))
        render_style_ = static_cast<RenderStyle>(render_style);

    const char* color_modes[] = {"Flat", "Range", "Intensity"};
    int         color_mode    = static_cast<int>(color_mode_);
    if (ImGui::Combo("Color Mode", &color_mode, color_modes, 3))
        color_mode_ = static_cast<ColorMode>(color_mode);

    ImGui::SliderInt("Trail Size", &trail_size_, 1, 32);
    ImGui::SliderFloat("Min Range", &min_range_filter_, 0.0f, 50.0f, "%.2f");
    ImGui::SliderFloat("Max Range", &max_range_filter_, 0.1f, 200.0f, "%.2f");
    if (max_range_filter_ < min_range_filter_)
        max_range_filter_ = min_range_filter_;
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);
    ImGui::Text("Stored scans: %zu", scan_count());
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void LaserScanDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    topic_.copy(topic_input_.data(), topic_input_.size() - 1);
    topic_input_[std::min(topic_.size(), topic_input_.size() - 1)] = '\0';
    resubscribe_requested_                                         = true;
}

std::string LaserScanDisplay::serialize_config_blob() const
{
    return std::format(
        "topic={};render_style={};color_mode={};trail_size={};min_range={:.3f};max_range={:.3f};"
        "use_message_stamp={}",
        topic_,
        static_cast<int>(render_style_),
        static_cast<int>(color_mode_),
        trail_size_,
        min_range_filter_,
        max_range_filter_,
        use_message_stamp_ ? 1 : 0);
}

void LaserScanDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char  topic[256]        = {};
    int   render_style      = static_cast<int>(render_style_);
    int   color_mode        = static_cast<int>(color_mode_);
    int   trail_size        = trail_size_;
    float min_range         = min_range_filter_;
    float max_range         = max_range_filter_;
    int   use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];render_style=%d;color_mode=%d;trail_size=%d;min_range=%f;max_"
                    "range=%f;use_message_stamp=%d",
                    topic,
                    &render_style,
                    &color_mode,
                    &trail_size,
                    &min_range,
                    &max_range,
                    &use_message_stamp)
        >= 1)
    {
        set_topic(topic);
        render_style_ = static_cast<RenderStyle>(std::clamp(render_style, 0, 1));
        color_mode_   = static_cast<ColorMode>(std::clamp(color_mode, 0, 2));
        if (trail_size > 0)
            trail_size_ = trail_size;
        if (min_range >= 0.0f)
            min_range_filter_ = min_range;
        if (max_range > 0.0f)
            max_range_filter_ = max_range;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void LaserScanDisplay::ingest_scan_frame(const LaserScanFrame& frame)
{
    std::lock_guard<std::mutex> lock(scans_mutex_);
    scans_.push_back(frame);
    while (static_cast<int>(scans_.size()) > trail_size_)
        scans_.pop_front();
}

void LaserScanDisplay::clear_scan_history()
{
    std::lock_guard<std::mutex> lock(scans_mutex_);
    scans_.clear();
}

size_t LaserScanDisplay::scan_count() const
{
    std::lock_guard<std::mutex> lock(scans_mutex_);
    return scans_.size();
}

void LaserScanDisplay::ensure_subscription()
{
#ifdef SPECTRA_USE_ROS2
    if (!resubscribe_requested_)
        return;

    resubscribe_requested_ = false;
    subscription_.reset();
    subscribed_topic_.clear();

    if (!node_ || !topic_discovery_ || topic_.empty())
        return;

    const TopicInfo info = topic_discovery_->topic(topic_);
    if (info.name.empty() || info.types.empty())
    {
        status_      = DisplayStatus::Warn;
        status_text_ = "Topic not discovered yet";
        return;
    }
    if (info.types.front() != "sensor_msgs/msg/LaserScan")
    {
        status_      = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<sensor_msgs::msg::LaserScan>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(32)).best_effort(),
        [this](const sensor_msgs::msg::LaserScan::SharedPtr msg)
        {
            const auto frame =
                adapt_laserscan_message(*msg, topic_, min_range_filter_, max_range_filter_);
            if (frame.has_value())
                ingest_scan_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_           = DisplayStatus::Ok;
    status_text_      = "Subscribed to " + topic_;
#endif
}

const char* LaserScanDisplay::render_style_name(RenderStyle style)
{
    return style == RenderStyle::Lines ? "lines" : "points";
}

const char* LaserScanDisplay::color_mode_name(ColorMode mode)
{
    switch (mode)
    {
        case ColorMode::Flat:
            return "flat";
        case ColorMode::Range:
            return "range";
        case ColorMode::Intensity:
            return "intensity";
    }
    return "flat";
}

}   // namespace spectra::adapters::ros2
