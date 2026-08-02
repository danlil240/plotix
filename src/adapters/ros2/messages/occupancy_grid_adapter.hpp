#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_ROS2
    #include <nav_msgs/msg/occupancy_grid.hpp>
#endif

namespace spectra::adapters::ros2
{

struct OccupancyGridFrame
{
    std::string          topic;
    std::string          frame_id;
    uint64_t             stamp_ns{0};
    uint32_t             width{0};
    uint32_t             height{0};
    double               resolution{0.05};
    double               origin_x{0.0};
    double               origin_y{0.0};
    double               origin_yaw{0.0};
    std::vector<int8_t>  data;
    std::vector<uint8_t> rgba;
    float                alpha{0.85f};
};

#ifdef SPECTRA_USE_ROS2
inline uint64_t occupancy_stamp_ns(const builtin_interfaces::msg::Time& stamp)
{
    return static_cast<uint64_t>(stamp.sec) * 1'000'000'000ULL
           + static_cast<uint64_t>(stamp.nanosec);
}

inline OccupancyGridFrame decode_occupancy_grid(const nav_msgs::msg::OccupancyGrid& msg,
                                                const std::string&                  topic,
                                                float                               alpha = 0.85f)
{
    OccupancyGridFrame frame;
    frame.topic      = topic;
    frame.frame_id   = msg.header.frame_id;
    frame.stamp_ns   = occupancy_stamp_ns(msg.header.stamp);
    frame.width      = msg.info.width;
    frame.height     = msg.info.height;
    frame.resolution = msg.info.resolution;
    frame.origin_x   = msg.info.origin.position.x;
    frame.origin_y   = msg.info.origin.position.y;
    frame.origin_yaw = 0.0;
    frame.alpha      = alpha;
    frame.data       = msg.data;

    if (frame.width == 0 || frame.height == 0 || frame.data.empty())
        return frame;

    frame.rgba.resize(static_cast<size_t>(frame.width) * frame.height * 4u);
    for (size_t i = 0; i < frame.data.size() && i < frame.width * frame.height; ++i)
    {
        const int8_t v = frame.data[i];
        uint8_t      r = 128;
        uint8_t      g = 128;
        uint8_t      b = 128;
        uint8_t      a = static_cast<uint8_t>(alpha * 255.0f);
        if (v < 0)
        {
            r = 80;
            g = 80;
            b = 100;
            a = static_cast<uint8_t>(alpha * 180.0f);
        }
        else if (v == 0)
        {
            r = 240;
            g = 240;
            b = 240;
        }
        else
        {
            const float t = std::min(1.0f, static_cast<float>(v) / 100.0f);
            r             = static_cast<uint8_t>(240.0f * (1.0f - t));
            g             = static_cast<uint8_t>(240.0f * (1.0f - t));
            b             = static_cast<uint8_t>(240.0f * (1.0f - t) + 255.0f * t);
        }
        const size_t o    = i * 4u;
        frame.rgba[o + 0] = r;
        frame.rgba[o + 1] = g;
        frame.rgba[o + 2] = b;
        frame.rgba[o + 3] = a;
    }
    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
