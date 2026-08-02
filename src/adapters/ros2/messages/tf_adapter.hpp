#pragma once

#include <cstdint>

#include "tf/tf_buffer.hpp"

#ifdef SPECTRA_USE_ROS2
    #include <geometry_msgs/msg/transform_stamped.hpp>
#endif

namespace spectra::adapters::ros2
{

#ifdef SPECTRA_USE_ROS2
inline TransformStamp adapt_tf_transform(const geometry_msgs::msg::TransformStamped& transform,
                                         bool                                        is_static)
{
    TransformStamp stamp;
    stamp.parent_frame = transform.header.frame_id;
    stamp.child_frame  = transform.child_frame_id;
    stamp.tx           = transform.transform.translation.x;
    stamp.ty           = transform.transform.translation.y;
    stamp.tz           = transform.transform.translation.z;
    stamp.qx           = transform.transform.rotation.x;
    stamp.qy           = transform.transform.rotation.y;
    stamp.qz           = transform.transform.rotation.z;
    stamp.qw           = transform.transform.rotation.w;
    stamp.recv_ns      = static_cast<uint64_t>(transform.header.stamp.sec) * 1'000'000'000ULL
                    + static_cast<uint64_t>(transform.header.stamp.nanosec);
    stamp.is_static = is_static;
    return stamp;
}
#endif

}   // namespace spectra::adapters::ros2
