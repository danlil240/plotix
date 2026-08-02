#include "bag_display_sync.hpp"

#include "display/laserscan_display.hpp"
#include "display/path_display.hpp"
#include "display/pointcloud_display.hpp"
#include "display/pose_display.hpp"
#include "messages/laserscan_adapter.hpp"
#include "messages/path_adapter.hpp"
#include "messages/pointcloud_adapter.hpp"
#include "messages/tf_adapter.hpp"

#ifdef SPECTRA_ROS2_BAG

    #include <algorithm>
    #include <cmath>
    #include <cstring>

    #include <nav_msgs/msg/odometry.hpp>
    #include <nav_msgs/msg/path.hpp>
    #include <rclcpp/serialization.hpp>
    #include <sensor_msgs/msg/laser_scan.hpp>
    #include <sensor_msgs/msg/point_cloud2.hpp>
    #include <tf2_msgs/msg/tf_message.hpp>

namespace spectra::adapters::ros2
{

namespace
{

template <typename MsgT>
bool deserialize_cdr(const BagMessage& msg, MsgT& out)
{
    if (msg.serialized_data.size() < 4)
        return false;

    rclcpp::SerializedMessage serialized;
    serialized.reserve(msg.serialized_data.size());
    std::memcpy(serialized.get_rcl_serialized_message().buffer,
                msg.serialized_data.data(),
                msg.serialized_data.size());
    serialized.get_rcl_serialized_message().buffer_length = msg.serialized_data.size();

    try
    {
        rclcpp::Serialization<MsgT> ser;
        ser.deserialize_message(&serialized, &out);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void inject_tf_message(const tf2_msgs::msg::TFMessage& tf_msg, bool is_static, TfBuffer& tf_buffer)
{
    for (const auto& transform : tf_msg.transforms)
        tf_buffer.inject_transform(adapt_tf_transform(transform, is_static));
}

LaserScanDisplay* find_laserscan(std::vector<DisplayPlugin*>& displays, const std::string& topic)
{
    for (auto* display : displays)
    {
        if (!display || display->type_id() != "laserscan" || display->topic() != topic)
            continue;
        return dynamic_cast<LaserScanDisplay*>(display);
    }
    return nullptr;
}

PathDisplay* find_path(std::vector<DisplayPlugin*>& displays, const std::string& topic)
{
    for (auto* display : displays)
    {
        if (!display || display->type_id() != "path" || display->topic() != topic)
            continue;
        return dynamic_cast<PathDisplay*>(display);
    }
    return nullptr;
}

PoseDisplay* find_pose(std::vector<DisplayPlugin*>& displays, const std::string& topic)
{
    for (auto* display : displays)
    {
        if (!display || display->type_id() != "pose" || display->topic() != topic)
            continue;
        return dynamic_cast<PoseDisplay*>(display);
    }
    return nullptr;
}

PointCloudDisplay* find_pointcloud(std::vector<DisplayPlugin*>& displays, const std::string& topic)
{
    for (auto* display : displays)
    {
        if (!display || display->type_id() != "pointcloud" || display->topic() != topic)
            continue;
        return dynamic_cast<PointCloudDisplay*>(display);
    }
    return nullptr;
}

std::optional<PoseFrame> adapt_odometry_pose(const nav_msgs::msg::Odometry& message,
                                             const std::string&             topic)
{
    PoseFrame frame;
    frame.topic            = topic;
    frame.frame_id         = message.header.frame_id;
    frame.stamp_ns         = path_detail::stamp_to_ns(message.header.stamp);
    frame.pose.translation = path_detail::position_to_vec3(message.pose.pose.position);
    frame.pose.rotation    = path_detail::orientation_to_quat(message.pose.pose.orientation);
    return frame;
}

void clear_display_buffers(std::vector<DisplayPlugin*>& displays)
{
    for (auto* display : displays)
    {
        if (!display)
            continue;
        if (auto* ls = dynamic_cast<LaserScanDisplay*>(display))
            ls->clear_scan_history();
        if (auto* path = dynamic_cast<PathDisplay*>(display))
            path->clear_playback_frame();
        if (auto* pose = dynamic_cast<PoseDisplay*>(display))
            pose->clear_playback_frame();
        if (auto* cloud = dynamic_cast<PointCloudDisplay*>(display))
            cloud->clear_playback_frame();
    }
}

}   // namespace

bool BagDisplaySync::open(const std::string& bag_path)
{
    close();
    if (!reader_.open(bag_path))
        return false;

    bag_path_          = bag_path;
    open_              = true;
    last_playhead_sec_ = -1.0;
    static_tf_cache_.clear();

    BagMessage msg;
    while (reader_.read_next(msg))
    {
        if (msg.topic != "/tf_static")
            continue;
        tf2_msgs::msg::TFMessage tf_msg;
        if (!deserialize_cdr(msg, tf_msg))
            continue;
        for (const auto& transform : tf_msg.transforms)
            static_tf_cache_.push_back(adapt_tf_transform(transform, true));
    }

    reader_.seek_begin();
    return true;
}

void BagDisplaySync::close()
{
    reader_.close();
    open_ = false;
    bag_path_.clear();
    static_tf_cache_.clear();
    last_playhead_sec_ = -1.0;
}

void BagDisplaySync::reset_playback_state(TfBuffer&                    tf_buffer,
                                          std::vector<DisplayPlugin*>& displays)
{
    clear_display_buffers(displays);
    tf_buffer.clear();
    for (const auto& stamp : static_tf_cache_)
        tf_buffer.inject_transform(stamp);
    reader_.seek_begin();
}

void BagDisplaySync::sync_to_playhead(double                      playhead_sec,
                                      int64_t                     bag_start_time_ns,
                                      TfBuffer&                   tf_buffer,
                                      std::vector<DisplayPlugin*> displays)
{
    if (!open_)
        return;

    const double playhead_clamped = std::max(0.0, playhead_sec);
    if (std::abs(playhead_clamped - last_playhead_sec_) < 1e-6)
        return;

    reset_playback_state(tf_buffer, displays);

    const int64_t end_ns = bag_start_time_ns + static_cast<int64_t>(playhead_clamped * 1e9);

    BagMessage msg;
    while (reader_.has_next())
    {
        if (!reader_.read_next(msg))
            break;
        if (msg.timestamp_ns > end_ns)
            break;
        process_message(msg,
                        static_cast<double>(bag_start_time_ns),
                        playhead_clamped,
                        tf_buffer,
                        displays);
    }

    last_playhead_sec_ = playhead_clamped;
}

void BagDisplaySync::process_message(const BagMessage&            msg,
                                     double                       bag_start_ns,
                                     double                       playhead_sec,
                                     TfBuffer&                    tf_buffer,
                                     std::vector<DisplayPlugin*>& displays)
{
    const double bag_time_sec = (static_cast<double>(msg.timestamp_ns) - bag_start_ns) * 1e-9;
    if (bag_time_sec > playhead_sec + 1e-9)
        return;

    if (msg.topic == "/tf")
    {
        tf2_msgs::msg::TFMessage tf_msg;
        if (deserialize_cdr(msg, tf_msg))
            inject_tf_message(tf_msg, false, tf_buffer);
        return;
    }

    if (msg.topic == "/tf_static")
        return;

    if (msg.type == "sensor_msgs/msg/LaserScan")
    {
        if (auto* display = find_laserscan(displays, msg.topic))
        {
            sensor_msgs::msg::LaserScan scan;
            if (deserialize_cdr(msg, scan))
            {
                if (auto frame = adapt_laserscan_message(scan, msg.topic, 0.0f, 100.0f))
                    display->ingest_scan_frame(*frame);
            }
        }
        return;
    }

    if (msg.type == "nav_msgs/msg/Path")
    {
        if (auto* display = find_path(displays, msg.topic))
        {
            nav_msgs::msg::Path path;
            if (deserialize_cdr(msg, path))
            {
                if (auto frame = adapt_path_message(path, msg.topic))
                    display->ingest_path_frame(*frame);
            }
        }
        return;
    }

    if (msg.type == "nav_msgs/msg/Odometry")
    {
        if (auto* display = find_pose(displays, msg.topic))
        {
            nav_msgs::msg::Odometry odom;
            if (deserialize_cdr(msg, odom))
            {
                if (auto frame = adapt_odometry_pose(odom, msg.topic))
                    display->ingest_pose_frame(*frame);
            }
        }
        return;
    }

    if (msg.type == "sensor_msgs/msg/PointCloud2")
    {
        if (auto* display = find_pointcloud(displays, msg.topic))
        {
            sensor_msgs::msg::PointCloud2 cloud;
            if (deserialize_cdr(msg, cloud))
            {
                if (auto frame = adapt_pointcloud_message(cloud, msg.topic, 500000))
                    display->ingest_pointcloud_frame(*frame);
            }
        }
    }
}

}   // namespace spectra::adapters::ros2

#else   // !SPECTRA_ROS2_BAG

namespace spectra::adapters::ros2
{

bool BagDisplaySync::open(const std::string&)
{
    return false;
}
void BagDisplaySync::close() {}
void BagDisplaySync::reset_playback_state(TfBuffer&, std::vector<DisplayPlugin*>&) {}
void BagDisplaySync::process_message(const BagMessage&,
                                     double,
                                     double,
                                     TfBuffer&,
                                     std::vector<DisplayPlugin*>&)
{
}
void BagDisplaySync::sync_to_playhead(double, int64_t, TfBuffer&, std::vector<DisplayPlugin*>) {}

}   // namespace spectra::adapters::ros2

#endif
