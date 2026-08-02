#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <spectra/color.hpp>
#include <spectra/math3d.hpp>

#ifdef SPECTRA_USE_ROS2
    #include <visualization_msgs/msg/marker.hpp>
    #include <visualization_msgs/msg/marker_array.hpp>
#endif

namespace spectra::adapters::ros2
{

enum class MarkerPrimitive
{
    Unknown,
    Arrow,
    Cube,
    Sphere,
    Cylinder,
    LineStrip,
    LineList,
    Points,
    TextViewFacing,
};

struct MarkerData
{
    std::string                topic;
    std::string                ns;
    int32_t                    id{0};
    int32_t                    action{0};
    MarkerPrimitive            primitive{MarkerPrimitive::Unknown};
    std::string                frame_id;
    uint64_t                   stamp_ns{0};
    uint64_t                   lifetime_ns{0};
    bool                       frame_locked{false};
    spectra::Transform         pose{};
    spectra::vec3              scale{1.0, 1.0, 1.0};
    spectra::Color             color{1.0f, 1.0f, 1.0f, 1.0f};
    std::string                text;
    std::vector<spectra::vec3> points;
};

inline const char* marker_primitive_name(MarkerPrimitive primitive)
{
    switch (primitive)
    {
        case MarkerPrimitive::Arrow:
            return "arrow";
        case MarkerPrimitive::Cube:
            return "cube";
        case MarkerPrimitive::Sphere:
            return "sphere";
        case MarkerPrimitive::Cylinder:
            return "cylinder";
        case MarkerPrimitive::LineStrip:
            return "line_strip";
        case MarkerPrimitive::LineList:
            return "line_list";
        case MarkerPrimitive::Points:
            return "points";
        case MarkerPrimitive::TextViewFacing:
            return "text_view_facing";
        case MarkerPrimitive::Unknown:
            break;
    }
    return "unknown";
}

#ifdef SPECTRA_USE_ROS2
inline uint64_t marker_duration_to_ns(const builtin_interfaces::msg::Duration& duration)
{
    return static_cast<uint64_t>(duration.sec) * 1'000'000'000ULL
           + static_cast<uint64_t>(duration.nanosec);
}

inline MarkerPrimitive marker_primitive_from_ros_type(int32_t type)
{
    switch (type)
    {
        case visualization_msgs::msg::Marker::ARROW:
            return MarkerPrimitive::Arrow;
        case visualization_msgs::msg::Marker::CUBE:
            return MarkerPrimitive::Cube;
        case visualization_msgs::msg::Marker::SPHERE:
            return MarkerPrimitive::Sphere;
        case visualization_msgs::msg::Marker::CYLINDER:
            return MarkerPrimitive::Cylinder;
        case visualization_msgs::msg::Marker::LINE_STRIP:
            return MarkerPrimitive::LineStrip;
        case visualization_msgs::msg::Marker::LINE_LIST:
            return MarkerPrimitive::LineList;
        case visualization_msgs::msg::Marker::POINTS:
            return MarkerPrimitive::Points;
        case visualization_msgs::msg::Marker::TEXT_VIEW_FACING:
            return MarkerPrimitive::TextViewFacing;
        default:
            return MarkerPrimitive::Unknown;
    }
}

inline std::optional<MarkerData> adapt_marker_message(const visualization_msgs::msg::Marker& marker,
                                                      const std::string&                     topic)
{
    MarkerData data;
    data.topic     = topic;
    data.ns        = marker.ns;
    data.id        = marker.id;
    data.action    = marker.action;
    data.primitive = marker_primitive_from_ros_type(marker.type);
    data.frame_id  = marker.header.frame_id;
    data.stamp_ns  = static_cast<uint64_t>(marker.header.stamp.sec) * 1'000'000'000ULL
                    + static_cast<uint64_t>(marker.header.stamp.nanosec);
    data.lifetime_ns      = marker_duration_to_ns(marker.lifetime);
    data.frame_locked     = marker.frame_locked;
    data.pose.translation = {
        marker.pose.position.x,
        marker.pose.position.y,
        marker.pose.position.z,
    };
    data.pose.rotation = {
        static_cast<float>(marker.pose.orientation.x),
        static_cast<float>(marker.pose.orientation.y),
        static_cast<float>(marker.pose.orientation.z),
        static_cast<float>(marker.pose.orientation.w),
    };
    data.scale = {marker.scale.x, marker.scale.y, marker.scale.z};
    data.color = {
        marker.color.r,
        marker.color.g,
        marker.color.b,
        marker.color.a,
    };
    data.text = marker.text;
    data.points.reserve(marker.points.size());
    for (const auto& point : marker.points)
        data.points.push_back({point.x, point.y, point.z});
    return data;
}

inline std::vector<MarkerData> adapt_marker_array_message(
    const visualization_msgs::msg::MarkerArray& array,
    const std::string&                          topic)
{
    std::vector<MarkerData> markers;
    markers.reserve(array.markers.size());
    for (const auto& marker : array.markers)
    {
        auto adapted = adapt_marker_message(marker, topic);
        if (adapted.has_value())
            markers.push_back(std::move(*adapted));
    }
    return markers;
}
#endif

}   // namespace spectra::adapters::ros2
