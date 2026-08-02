#include "display/image_display.hpp"

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

#ifdef SPECTRA_USE_IMGUI
void draw_preview(const ImageFrame& frame)
{
    if (frame.preview_rgba.empty() || frame.preview_width == 0 || frame.preview_height == 0)
    {
        ImGui::TextDisabled("No preview available.");
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 8.0f || avail.y <= 8.0f)
    {
        ImGui::TextDisabled("Preview area too small.");
        return;
    }

    const float  scale_x = avail.x / static_cast<float>(frame.preview_width);
    const float  scale_y = avail.y / static_cast<float>(frame.preview_height);
    const float  scale   = std::max(0.5f, std::min(scale_x, scale_y));
    const ImVec2 canvas_size{
        static_cast<float>(frame.preview_width) * scale,
        static_cast<float>(frame.preview_height) * scale,
    };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##image_preview_canvas", canvas_size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (uint32_t y = 0; y < frame.preview_height; ++y)
    {
        for (uint32_t x = 0; x < frame.preview_width; ++x)
        {
            const size_t index = (static_cast<size_t>(y) * frame.preview_width + x) * 4u;
            const ImU32  color = IM_COL32(frame.preview_rgba[index + 0],
                                         frame.preview_rgba[index + 1],
                                         frame.preview_rgba[index + 2],
                                         frame.preview_rgba[index + 3]);
            draw_list->AddRectFilled(ImVec2(origin.x + static_cast<float>(x) * scale,
                                            origin.y + static_cast<float>(y) * scale),
                                     ImVec2(origin.x + static_cast<float>(x + 1u) * scale,
                                            origin.y + static_cast<float>(y + 1u) * scale),
                                     color);
        }
    }
    draw_list->AddRect(origin,
                       ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                       IM_COL32(180, 180, 180, 255));
}
#endif
}   // namespace

ImageDisplay::ImageDisplay()
{
    set_topic("/image");
}

void ImageDisplay::on_enable(const DisplayContext& context)
{
    tf_buffer_       = context.tf_buffer;
    topic_discovery_ = context.topic_discovery;
    fixed_frame_     = context.fixed_frame;
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;
#endif
    resubscribe_requested_ = true;
    status_                = DisplayStatus::Warn;
    status_text_           = "Waiting for image topic";
}

void ImageDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
    node_.reset();
#endif
    subscribed_topic_.clear();
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void ImageDisplay::on_destroy()
{
    on_disable();
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_.reset();
#ifdef SPECTRA_USE_ROS2
    raw_message_.reset();
#endif
}

void ImageDisplay::on_update(float)
{
    ensure_subscription();

    const auto frame = latest_frame();
    if (status_ == DisplayStatus::Disabled)
        return;

    if (!frame.has_value())
    {
        status_ = DisplayStatus::Warn;
        status_text_ =
            subscribed_topic_.empty() ? "Waiting for image topic" : "Subscribed, no image received";
        return;
    }

    if (!frame->supported_encoding)
    {
        status_      = DisplayStatus::Error;
        status_text_ = frame->warning.empty() ? "Unsupported image encoding" : frame->warning;
        return;
    }

    status_ = DisplayStatus::Ok;
    status_text_ =
        std::to_string(frame->width) + "x" + std::to_string(frame->height) + " " + frame->encoding;
}

void ImageDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_ || mode_ == Mode::Panel2D)
        return;

    const auto frame = latest_frame();
    if (!frame.has_value() || !frame->supported_encoding)
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
    entity.type         = "image";
    entity.label        = topic_.empty() ? display_name() : topic_;
    entity.display_name = display_name();
    entity.topic        = frame->topic;
    entity.frame_id     = frame->frame_id;
    entity.transform    = frame_transform;
    entity.scale        = {
        frame->height == 0 ? 1.0
                                  : static_cast<double>(frame->width) / static_cast<double>(frame->height),
        1.0,
        1.0,
    };
    entity.stamp_ns  = frame->stamp_ns;
    entity.billboard = SceneBillboard{
        frame->height == 0 ? 1.0
                           : static_cast<double>(frame->width) / static_cast<double>(frame->height),
        1.0,
    };

    // Attach full-resolution image data for GPU texture upload.
    if (!frame->full_rgba.empty())
    {
        SceneImage img;
        img.width        = frame->width;
        img.height       = frame->height;
        img.rgba_data    = frame->full_rgba;
        img.texture_id   = reinterpret_cast<uint64_t>(this);
        img.needs_upload = true;
        entity.image     = std::move(img);
    }

    entity.properties.push_back({"mode", mode_name(mode_)});
    entity.properties.push_back({"encoding", frame->encoding});
    entity.properties.push_back(
        {"resolution", std::to_string(frame->width) + "x" + std::to_string(frame->height)});
    entity.properties.push_back({"mean_intensity", std::to_string(frame->mean_intensity)});
    entity.properties.push_back({"preview_visible", panel_visible_ ? "true" : "false"});
    scene.add_entity(std::move(entity));
}

void ImageDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    const char* modes[]      = {"2D Panel", "3D Billboard", "Panel + Billboard"};
    int         current_mode = static_cast<int>(mode_);
    if (ImGui::Combo("Mode", &current_mode, modes, 3))
        mode_ = static_cast<Mode>(current_mode);

    const char* encodings[]   = {"Auto", "rgb8", "bgr8", "mono8", "jpeg", "png"};
    int         encoding_mode = static_cast<int>(encoding_override_);
    if (ImGui::Combo("Encoding", &encoding_mode, encodings, 6))
    {
        encoding_override_ = static_cast<EncodingOverride>(encoding_mode);
        refresh_decoded_frame();
    }

    ImGui::Checkbox("Preview Window", &panel_visible_);
    int preview_max_dim = static_cast<int>(preview_max_dim_);
    if (ImGui::SliderInt("Preview Max Dim", &preview_max_dim, 16, 96))
        preview_max_dim_ = static_cast<uint32_t>(preview_max_dim);
    ImGui::Checkbox("Use Message Stamp", &use_message_stamp_);

    const auto frame = latest_frame();
    if (frame.has_value())
    {
        ImGui::Text("Encoding: %s", frame->encoding.c_str());
        ImGui::Text("Resolution: %ux%u", frame->width, frame->height);
        ImGui::Text("Preview: %ux%u", frame->preview_width, frame->preview_height);
        ImGui::Text("Mean intensity: %.1f", frame->mean_intensity);
        if (!frame->warning.empty())
            ImGui::TextWrapped("Warning: %s", frame->warning.c_str());
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

void ImageDisplay::draw_auxiliary_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext() || !enabled_ || !panel_visible_)
        return;
    if (mode_ != Mode::Panel2D && mode_ != Mode::PanelAndBillboard)
        return;

    std::string window_name = display_name();
    if (!topic_.empty())
        window_name += " [" + topic_ + "]";

    if (!ImGui::Begin(window_name.c_str(), &panel_visible_))
    {
        ImGui::End();
        return;
    }

    const auto frame = latest_frame();
    if (!frame.has_value())
    {
        ImGui::TextDisabled("No image received.");
    }
    else if (!frame->supported_encoding)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f), "%s", frame->warning.c_str());
    }
    else
    {
        ImGui::Text("%ux%u  %s", frame->width, frame->height, frame->encoding.c_str());
        ImGui::Text("Frame: %s", frame->frame_id.empty() ? "(none)" : frame->frame_id.c_str());
        ImGui::Separator();
        draw_preview(*frame);
    }

    ImGui::End();
#endif
}

void ImageDisplay::set_topic(const std::string& topic)
{
    topic_ = topic;
    topic_.copy(topic_input_.data(), topic_input_.size() - 1);
    topic_input_[std::min(topic_.size(), topic_input_.size() - 1)] = '\0';
    resubscribe_requested_                                         = true;
}

std::string ImageDisplay::serialize_config_blob() const
{
    return std::format("topic={};mode={};encoding_override={};panel_visible={};preview_max_dim={};"
                       "use_message_stamp={}",
                       topic_,
                       static_cast<int>(mode_),
                       static_cast<int>(encoding_override_),
                       panel_visible_ ? 1 : 0,
                       preview_max_dim_,
                       use_message_stamp_ ? 1 : 0);
}

void ImageDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char       topic[256]            = {};
    int        mode                  = static_cast<int>(mode_);
    int        encoding_override     = static_cast<int>(encoding_override_);
    int        panel_visible         = panel_visible_ ? 1 : 0;
    unsigned   preview_max_dim       = preview_max_dim_;
    int        use_message_stamp     = use_message_stamp_ ? 1 : 0;
    const bool has_encoding_override = blob.find("encoding_override=") != std::string::npos;
    if (has_encoding_override
        && std::sscanf(
               blob.c_str(),
               "topic=%255[^;];mode=%d;encoding_override=%d;panel_visible=%d;preview_max_dim=%u;"
               "use_message_stamp=%d",
               topic,
               &mode,
               &encoding_override,
               &panel_visible,
               &preview_max_dim,
               &use_message_stamp)
               >= 1)
    {
        set_topic(topic);
        mode_              = static_cast<Mode>(std::clamp(mode, 0, 2));
        encoding_override_ = static_cast<EncodingOverride>(std::clamp(encoding_override, 0, 5));
        panel_visible_     = panel_visible != 0;
        preview_max_dim_   = std::clamp(preview_max_dim, 16u, 96u);
        use_message_stamp_ = use_message_stamp != 0;
    }
    else if (std::sscanf(blob.c_str(),
                         "topic=%255[^;];mode=%d;panel_visible=%d;preview_max_dim=%u;"
                         "use_message_stamp=%d",
                         topic,
                         &mode,
                         &panel_visible,
                         &preview_max_dim,
                         &use_message_stamp)
             >= 1)
    {
        set_topic(topic);
        mode_              = static_cast<Mode>(std::clamp(mode, 0, 2));
        panel_visible_     = panel_visible != 0;
        preview_max_dim_   = std::clamp(preview_max_dim, 16u, 96u);
        use_message_stamp_ = use_message_stamp != 0;
    }
}

void ImageDisplay::ingest_image_frame(const ImageFrame& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_ = frame;
}

std::optional<ImageFrame> ImageDisplay::latest_frame() const
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return latest_frame_;
}

void ImageDisplay::ensure_subscription()
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
    if (info.types.front() != "sensor_msgs/msg/Image")
    {
        status_      = DisplayStatus::Error;
        status_text_ = "Incompatible topic type: " + info.types.front();
        return;
    }

    subscription_ = node_->create_subscription<sensor_msgs::msg::Image>(
        topic_,
        rclcpp::QoS(rclcpp::KeepLast(2)).best_effort(),
        [this](const sensor_msgs::msg::Image::SharedPtr msg)
        {
            raw_message_ = *msg;
            refresh_decoded_frame();
        });

    subscribed_topic_ = topic_;
    status_           = DisplayStatus::Ok;
    status_text_      = "Subscribed to " + topic_;
#endif
}

void ImageDisplay::refresh_decoded_frame()
{
#ifdef SPECTRA_USE_ROS2
    if (!raw_message_.has_value())
        return;

    const bool        need_full = (mode_ == Mode::Billboard3D || mode_ == Mode::PanelAndBillboard);
    const std::string override_encoding = encoding_override_string(encoding_override_);
    const auto        frame             = override_encoding.empty()
                                              ? adapt_image_message(*raw_message_, topic_, preview_max_dim_, need_full)
                                              : adapt_image_message_with_encoding(*raw_message_,
                                                               topic_,
                                                               preview_max_dim_,
                                                               need_full,
                                                               override_encoding);
    if (frame.has_value())
        ingest_image_frame(*frame);
#endif
}

const char* ImageDisplay::mode_name(Mode mode)
{
    switch (mode)
    {
        case Mode::Panel2D:
            return "panel";
        case Mode::Billboard3D:
            return "billboard";
        case Mode::PanelAndBillboard:
            return "panel+billboard";
    }
    return "panel";
}

const char* ImageDisplay::encoding_override_name(EncodingOverride mode)
{
    switch (mode)
    {
        case EncodingOverride::Auto:
            return "auto";
        case EncodingOverride::Rgb8:
            return "rgb8";
        case EncodingOverride::Bgr8:
            return "bgr8";
        case EncodingOverride::Mono8:
            return "mono8";
        case EncodingOverride::Jpeg:
            return "jpeg";
        case EncodingOverride::Png:
            return "png";
    }
    return "auto";
}

std::string ImageDisplay::encoding_override_string(EncodingOverride mode)
{
    if (mode == EncodingOverride::Auto)
        return {};
    return encoding_override_name(mode);
}

}   // namespace spectra::adapters::ros2
