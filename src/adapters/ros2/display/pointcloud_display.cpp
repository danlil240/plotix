#include "display/pointcloud_display.hpp"

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

uint32_t pointcloud_default_color(PointCloudDisplay::ColorMode mode)
{
    switch (mode)
    {
        case PointCloudDisplay::ColorMode::Flat:
            return pack_rgba(132, 206, 255, 255);
        case PointCloudDisplay::ColorMode::Intensity:
            return pack_rgba(255, 214, 92, 255);
        case PointCloudDisplay::ColorMode::Height:
            return pack_rgba(170, 228, 130, 255);
        case PointCloudDisplay::ColorMode::RGB:
            return pack_rgba(255, 255, 255, 255);
    }
    return pack_rgba(132, 206, 255, 255);
}
}   // namespace

PointCloudDisplay::PointCloudDisplay()
{
    set_topic("/points");
}

void PointCloudDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for point cloud topic";
}

void PointCloudDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void PointCloudDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

void PointCloudDisplay::on_update(float)
{
    ensure_subscription();

    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;

    if (!frame.has_value())
    {
        status_      = DisplayStatus::Warn;
        status_text_ = subscribed_topic_.empty() ? "Waiting for point cloud topic"
                                                 : "Subscribed, no point cloud received";
        return;
    }

    status_      = DisplayStatus::Ok;
    status_text_ = std::to_string(frame->point_count) + " pts";
    if (frames_decimated_ > 0)
    {
        status_text_ += " (dropped " + std::to_string(points_dropped_total_) + " pts in "
                        + std::to_string(frames_decimated_) + " frames)";
    }
}

void PointCloudDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    const auto frame = latest_frame();
    if (!frame.has_value())
        return;

    bool                     tf_ok           = true;
    const spectra::Transform frame_transform = resolve_frame_transform(tf_buffer_,
                                                                       fixed_frame_,
                                                                       frame->frame_id,
                                                                       frame->stamp_ns,
                                                                       use_message_stamp_,
                                                                       tf_ok);
    if (!tf_ok)
        return;

    SceneEntity entity;
    entity.type                  = "pointcloud";
    entity.label                 = topic_.empty() ? display_name() : topic_;
    entity.display_name          = display_name();
    entity.topic                 = frame->topic;
    entity.frame_id              = frame->frame_id;
    entity.transform             = frame_transform;
    entity.transform.translation = frame_transform.transform_point(frame->centroid);
    entity.scale                 = frame->max_bounds - frame->min_bounds;
    entity.stamp_ns              = frame->stamp_ns;

    const bool want_intensity      = color_mode_ == ColorMode::Intensity && frame->has_intensity;
    const bool want_height         = color_mode_ == ColorMode::Height;
    const bool want_rgb            = color_mode_ == ColorMode::RGB && frame->has_rgb;
    const bool use_per_point_color = want_intensity || want_height || want_rgb;

    ScenePointSet point_set;
    point_set.point_size          = point_size_;
    point_set.default_rgba        = pointcloud_default_color(color_mode_);
    point_set.use_per_point_color = use_per_point_color;
    point_set.points.reserve(frame->points.size());

    for (const auto& point : frame->points)
    {
        ScenePoint scene_point;
        scene_point.position = point.position - frame->centroid;

        if (want_rgb && point.has_rgb)
        {
            scene_point.rgba = point.rgba;
        }
        else if (want_intensity && point.has_intensity)
        {
            scene_point.rgba = gradient_color(
                normalize_range(point.intensity, frame->min_intensity, frame->max_intensity));
        }
        else if (want_height)
        {
            scene_point.rgba =
                gradient_color(normalize_range(static_cast<float>(point.position.z),
                                               static_cast<float>(frame->min_bounds.z),
                                               static_cast<float>(frame->max_bounds.z)));
        }
        else
        {
            scene_point.rgba = point_set.default_rgba;
        }

        point_set.points.push_back(scene_point);
    }
    entity.point_set = std::move(point_set);

    const std::array<float, 4> default_color = unpack_rgba(entity.point_set->default_rgba);
    const std::string          color_str     = std::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}",
                                              default_color[0],
                                              default_color[1],
                                              default_color[2],
                                              default_color[3]);

    entity.properties.push_back({"points", std::to_string(frame->point_count)});
    entity.properties.push_back({"input_points", std::to_string(frame->original_point_count)});
    entity.properties.push_back({"color_mode", color_mode_name(color_mode_)});
    entity.properties.push_back({"gpu_color_mode", use_per_point_color ? "per-point" : "flat"});
    entity.properties.push_back({"point_size", std::to_string(point_size_)});
    entity.properties.push_back({"has_rgb", frame->has_rgb ? "true" : "false"});
    entity.properties.push_back({"has_intensity", frame->has_intensity ? "true" : "false"});
    entity.properties.push_back({"fixed_frame", fixed_frame_.empty() ? "(unset)" : fixed_frame_});
    entity.properties.push_back({"color", color_str});
    scene.add_entity(std::move(entity));
}

void PointCloudDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    const char* modes[]      = {"Flat", "Intensity", "Height", "RGB"};
    int         current_mode = static_cast<int>(color_mode_);
    if (ImGui::Combo("Color Mode", &current_mode, modes, 4))
        color_mode_ = static_cast<ColorMode>(current_mode);

    ImGui::SliderFloat("Point Size", &point_size_, 1.0f, 10.0f, "%.1f");
    ImGui::SliderInt("Max Points", &max_points_, 1'000, 500'000);
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);

    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Latest points: %zu / %zu", frame->point_count, frame->original_point_count);
        ImGui::Text("Frame: %s", frame->frame_id.empty() ? "(none)" : frame->frame_id.c_str());
    }
    ImGui::Text("Dropped points (total): %llu",
                static_cast<unsigned long long>(points_dropped_total_));
    ImGui::Text("Decimated frames: %llu", static_cast<unsigned long long>(frames_decimated_));
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void PointCloudDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    topic_.copy(topic_input_.data(), topic_input_.size() - 1);
    topic_input_[std::min(topic_.size(), topic_input_.size() - 1)] = '\0';
    resubscribe_requested_                                         = true;
}

std::string PointCloudDisplay::serialize_config_blob() const
{
    return std::format(
        "topic={};color_mode={};point_size={:.2f};max_points={};use_message_stamp={}",
        topic_,
        static_cast<int>(color_mode_),
        point_size_,
        max_points_,
        use_message_stamp_ ? 1 : 0);
}

void PointCloudDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char  topic[256]        = {};
    int   color_mode        = static_cast<int>(color_mode_);
    float point_size        = point_size_;
    int   max_points        = max_points_;
    int   use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];color_mode=%d;point_size=%f;max_points=%d;use_message_stamp=%d",
                    topic,
                    &color_mode,
                    &point_size,
                    &max_points,
                    &use_message_stamp)
        >= 1)
    {
        set_topic(topic);
        color_mode_ = static_cast<ColorMode>(std::clamp(color_mode, 0, 3));
        if (point_size > 0.0f)
            point_size_ = point_size;
        if (max_points > 0)
            max_points_ = max_points;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void PointCloudDisplay::ingest_pointcloud_frame(const PointCloudFrame& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (frame.original_point_count > frame.point_count)
    {
        points_dropped_total_ += frame.original_point_count - frame.point_count;
        ++frames_decimated_;
    }
    latest_frame_ = frame;
}

void PointCloudDisplay::clear_playback_frame()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

std::optional<PointCloudFrame> PointCloudDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return latest_frame_;
}

void PointCloudDisplay::ensure_subscription()
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
    if (info.types.front() != "sensor_msgs/msg/PointCloud2")
    {
        status_      = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<sensor_msgs::msg::PointCloud2>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(4)).best_effort(),
        [this](const sensor_msgs::msg::PointCloud2::SharedPtr msg)
        {
            const auto frame =
                adapt_pointcloud_message(*msg,
                                         topic_,
                                         static_cast<size_t>(std::max(1, max_points_)));
            if (frame.has_value())
                ingest_pointcloud_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_           = DisplayStatus::Ok;
    status_text_      = "Subscribed to " + topic_;
#endif
}

const char* PointCloudDisplay::color_mode_name(ColorMode mode)
{
    switch (mode)
    {
        case ColorMode::Flat:
            return "flat";
        case ColorMode::Intensity:
            return "intensity";
        case ColorMode::Height:
            return "height";
        case ColorMode::RGB:
            return "rgb";
    }
    return "flat";
}

}   // namespace spectra::adapters::ros2
