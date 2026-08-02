#include "display/pose_display.hpp"

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

PoseDisplay::PoseDisplay()
{
    set_topic("/pose");
}

void PoseDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for pose topic";
}

void PoseDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void PoseDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

void PoseDisplay::on_update(float)
{
    ensure_subscription();

    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;

    if (!frame.has_value())
    {
        status_ = DisplayStatus::Warn;
        status_text_ =
            subscribed_topic_.empty() ? "Waiting for pose topic" : "Subscribed, no pose received";
        return;
    }

    status_      = DisplayStatus::Ok;
    status_text_ = "Pose received";
}

void PoseDisplay::submit_renderables(SceneManager& scene)
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
    entity.type         = "pose";
    entity.label        = topic_.empty() ? display_name() : topic_;
    entity.display_name = display_name();
    entity.topic        = frame->topic;
    entity.frame_id     = frame->frame_id;
    entity.transform    = frame_transform.compose(frame->pose);
    entity.scale        = {shaft_length_, shaft_width_, head_length_};
    entity.stamp_ns     = frame->stamp_ns;
    SceneArrow arrow;
    arrow.origin       = {0.0, 0.0, 0.0};
    arrow.direction    = {1.0, 0.0, 0.0};
    arrow.shaft_length = shaft_length_;
    arrow.head_length  = head_length_;
    arrow.head_width   = head_width_;
    entity.arrow       = arrow;
    entity.properties.push_back({"shaft_length", std::to_string(shaft_length_)});
    entity.properties.push_back({"shaft_width", std::to_string(shaft_width_)});
    entity.properties.push_back({"head_length", std::to_string(head_length_)});
    entity.properties.push_back({"head_width", std::to_string(head_width_)});
    scene.add_entity(std::move(entity));
}

void PoseDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    ImGui::SliderFloat("Shaft Length", &shaft_length_, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("Shaft Width", &shaft_width_, 0.01f, 1.0f, "%.2f");
    ImGui::SliderFloat("Head Length", &head_length_, 0.05f, 2.0f, "%.2f");
    ImGui::SliderFloat("Head Width", &head_width_, 0.05f, 2.0f, "%.2f");
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);

    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Frame: %s", frame->frame_id.empty() ? "(none)" : frame->frame_id.c_str());
        ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                    frame->pose.translation.x,
                    frame->pose.translation.y,
                    frame->pose.translation.z);
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void PoseDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    topic_.copy(topic_input_.data(), topic_input_.size() - 1);
    topic_input_[std::min(topic_.size(), topic_input_.size() - 1)] = '\0';
    resubscribe_requested_                                         = true;
}

std::string PoseDisplay::serialize_config_blob() const
{
    return std::format(
        "topic={};shaft_length={:.2f};shaft_width={:.2f};head_length={:.2f};head_width={:.2f};"
        "use_message_stamp={}",
        topic_,
        shaft_length_,
        shaft_width_,
        head_length_,
        head_width_,
        use_message_stamp_ ? 1 : 0);
}

void PoseDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char  topic[256]        = {};
    float shaft_length      = shaft_length_;
    float shaft_width       = shaft_width_;
    float head_length       = head_length_;
    float head_width        = head_width_;
    int   use_message_stamp = use_message_stamp_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "topic=%255[^;];shaft_length=%f;shaft_width=%f;head_length=%f;head_width=%f;"
                    "use_message_stamp=%d",
                    topic,
                    &shaft_length,
                    &shaft_width,
                    &head_length,
                    &head_width,
                    &use_message_stamp)
        >= 1)
    {
        set_topic(topic);
        if (shaft_length > 0.0f)
            shaft_length_ = shaft_length;
        if (shaft_width > 0.0f)
            shaft_width_ = shaft_width;
        if (head_length > 0.0f)
            head_length_ = head_length;
        if (head_width > 0.0f)
            head_width_ = head_width;
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void PoseDisplay::ingest_pose_frame(const PoseFrame& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_ = frame;
}

void PoseDisplay::clear_playback_frame()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
}

std::optional<PoseFrame> PoseDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return latest_frame_;
}

void PoseDisplay::ensure_subscription()
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
    if (info.types.front() != "geometry_msgs/msg/PoseStamped")
    {
        status_      = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<geometry_msgs::msg::PoseStamped>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(8)).best_effort(),
        [this](const geometry_msgs::msg::PoseStamped::SharedPtr msg)
        {
            const auto frame = adapt_pose_stamped_message(*msg, topic_);
            if (frame.has_value())
                ingest_pose_frame(*frame);
        });

    subscribed_topic_ = topic_;
    status_           = DisplayStatus::Ok;
    status_text_      = "Subscribed to " + topic_;
#endif
}

}   // namespace spectra::adapters::ros2
