#include "display/path_display.hpp"

#include <algorithm>
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
}   // namespace

PathDisplay::PathDisplay()
{
    set_topic("/plan");
}

void PathDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for path topic";
}

void PathDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void PathDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

void PathDisplay::on_update(float)
{
    ensure_subscription();

    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;

    if (!frame.has_value())
    {
        status_ = DisplayStatus::Warn;
        status_text_ =
            subscribed_topic_.empty() ? "Waiting for path topic" : "Subscribed, no path received";
        return;
    }

    status_      = DisplayStatus::Ok;
    status_text_ = std::to_string(frame->pose_count) + " poses";
}

void PathDisplay::submit_renderables(SceneManager& scene)
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
    entity.type                  = "path";
    entity.label                 = topic_.empty() ? display_name() : topic_;
    entity.display_name          = display_name();
    entity.topic                 = frame->topic;
    entity.frame_id              = frame->frame_id;
    entity.transform             = frame_transform;
    entity.transform.translation = frame_transform.transform_point(frame->centroid);
    entity.scale                 = frame->max_bounds - frame->min_bounds;
    entity.stamp_ns              = frame->stamp_ns;
    ScenePolyline polyline;
    polyline.points.reserve(frame->points.size());
    for (const auto& point : frame->points)
        polyline.points.push_back(point - frame->centroid);
    entity.polyline = std::move(polyline);
    entity.properties.push_back({"poses", std::to_string(frame->pose_count)});
    entity.properties.push_back({"length_m", std::to_string(frame->path_length_m)});
    entity.properties.push_back({"line_width", std::to_string(line_width_)});
    entity.properties.push_back({"alpha", std::to_string(alpha_)});
    entity.properties.push_back({"pose_arrows", show_pose_arrows_ ? "true" : "false"});
    scene.add_entity(std::move(entity));
}

void PathDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    ImGui::SliderFloat("Line Width", &line_width_, 1.0f, 8.0f, "%.1f");
    ImGui::SliderFloat("Alpha", &alpha_, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox("Pose Arrows", &show_pose_arrows_);
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);

    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Poses: %zu", frame->pose_count);
        ImGui::Text("Length: %.2f m", frame->path_length_m);
        ImGui::Text("Frame: %s", frame->frame_id.empty() ? "(none)" : frame->frame_id.c_str());
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void PathDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    topic_.copy(topic_input_.data(), topic_input_.size() - 1);
    topic_input_[std::min(topic_.size(), topic_input_.size() - 1)] = '\0';
    resubscribe_requested_                                         = true;
}

std::string PathDisplay::serialize_config_blob() const
{
    return std::format(
        "topic={};line_width={:.2f};alpha={:.2f};pose_arrows={};use_message_stamp={}",
        topic_,
        line_width_,
        alpha_,
        show_pose_arrows_ ? 1 : 0,
        use_message_stamp_ ? 1 : 0);
}

void PathDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char  topic[256]        = {};
    float line_width        = line_width_;
    float alpha             = alpha_;
    int   pose_arrows       = show_pose_arrows_ ? 1 : 0;
    int   use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];line_width=%f;alpha=%f;pose_arrows=%d;use_message_stamp=%d",
                    topic,
                    &line_width,
                    &alpha,
                    &pose_arrows,
                    &use_message_stamp)
        >= 1)
    {
        set_topic(topic);
        if (line_width > 0.0f)
            line_width_ = line_width;
        alpha_             = std::clamp(alpha, 0.1f, 1.0f);
        show_pose_arrows_  = pose_arrows != 0;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void PathDisplay::ingest_path_frame(const PathFrame& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_ = frame;
}

void PathDisplay::clear_playback_frame()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

std::optional<PathFrame> PathDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return latest_frame_;
}

void PathDisplay::ensure_subscription()
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
    if (info.types.front() != "nav_msgs/msg/Path")
    {
        status_      = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<nav_msgs::msg::Path>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(4)).best_effort(),
        [this](const nav_msgs::msg::Path::SharedPtr msg)
        {
            const auto frame = adapt_path_message(*msg, topic_);
            if (frame.has_value())
                ingest_path_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_           = DisplayStatus::Ok;
    status_text_      = "Subscribed to " + topic_;
#endif
}

}   // namespace spectra::adapters::ros2
