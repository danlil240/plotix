#include "display/occupancy_grid_display.hpp"

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
spectra::Transform resolve_frame_transform(const TfBuffer*    tf_buffer,
                                           const std::string& fixed_frame,
                                           const std::string& frame_id,
                                           uint64_t           stamp_ns,
                                           bool&              ok_out)
{
    ok_out = true;
    spectra::Transform transform{};
    if (!tf_buffer || fixed_frame.empty() || frame_id.empty() || frame_id == fixed_frame)
        return transform;

    const TransformResult result = tf_buffer->lookup_transform(fixed_frame, frame_id, stamp_ns);
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

OccupancyGridDisplay::OccupancyGridDisplay()
{
    set_topic("/map");
}

void OccupancyGridDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for map topic";
}

void OccupancyGridDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void OccupancyGridDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_.reset();
}

void OccupancyGridDisplay::on_update(float)
{
    ensure_subscription();
    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;
    if (!frame.has_value())
    {
        status_      = DisplayStatus::Warn;
        status_text_ = subscribed_topic_.empty() ? "Waiting for map topic" : "Subscribed, no map";
        return;
    }
    status_      = DisplayStatus::Ok;
    status_text_ = "OK";
}

void OccupancyGridDisplay::submit_renderables(SceneManager& scene)
{
    const auto frame = latest_frame();
    if (!frame.has_value() || frame->rgba.empty() || frame->width == 0 || frame->height == 0)
        return;

    bool                     tf_ok = true;
    const spectra::Transform tf =
        resolve_frame_transform(tf_buffer_, fixed_frame_, frame->frame_id, frame->stamp_ns, tf_ok);
    if (!tf_ok)
    {
        status_      = DisplayStatus::Warn;
        status_text_ = "TF error";
        return;
    }

    const double map_w = static_cast<double>(frame->width) * frame->resolution;
    const double map_h = static_cast<double>(frame->height) * frame->resolution;

    SceneEntity entity;
    entity.type         = "occupancy_grid";
    entity.label        = topic_.empty() ? display_name() : topic_;
    entity.display_name = display_name();
    entity.topic        = frame->topic;
    entity.frame_id     = frame->frame_id;
    entity.transform    = tf;
    entity.scale        = {map_w, map_h, 1.0};
    entity.stamp_ns     = frame->stamp_ns;
    entity.billboard    = SceneBillboard{map_w, map_h};

    SceneImage img;
    img.width        = frame->width;
    img.height       = frame->height;
    img.rgba_data    = frame->rgba;
    img.texture_id   = reinterpret_cast<uint64_t>(this);
    img.needs_upload = true;
    entity.image     = std::move(img);

    entity.properties.push_back({"resolution", std::to_string(frame->resolution)});
    entity.properties.push_back(
        {"size", std::to_string(frame->width) + "x" + std::to_string(frame->height)});
    scene.add_entity(std::move(entity));
}

void OccupancyGridDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::SliderFloat("Alpha", &alpha_, 0.1f, 1.0f);
    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Resolution: %.3f m/px", frame->resolution);
        ImGui::Text("Size: %ux%u", frame->width, frame->height);
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#else
    (void)alpha_;
#endif
}

std::string OccupancyGridDisplay::serialize_config_blob() const
{
    return std::format("alpha={:.3f}", alpha_);
}

void OccupancyGridDisplay::deserialize_config_blob(const std::string& blob)
{
    float a = alpha_;
    if (std::sscanf(blob.c_str(), "alpha=%f", &a) == 1)
        alpha_ = a;
}

std::optional<OccupancyGridFrame> OccupancyGridDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return frame_;
}

void OccupancyGridDisplay::ensure_subscription()
{
#ifdef SPECTRA_USE_ROS2
    if (!node_ || !enabled_)
        return;
    if (!resubscribe_requested_ && !subscribed_topic_.empty())
        return;

    subscription_.reset();
    subscribed_topic_.clear();
    resubscribe_requested_ = false;

    if (topic_.empty())
        return;

    subscription_ = node_->create_subscription<nav_msgs::msg::OccupancyGrid>(
        topic_,
        rclcpp::QoS(1).reliable(),
        [this](const nav_msgs::msg::OccupancyGrid::SharedPtr msg)
        {
            if (!msg)
                return;
            auto                        frame = decode_occupancy_grid(*msg, topic_, alpha_);
            std::lock_guard<std::mutex> lock(frame_mutex_);
            frame_ = std::move(frame);
        });
    subscribed_topic_ = topic_;
#else
    (void)topic_discovery_;
#endif
}

}   // namespace spectra::adapters::ros2
