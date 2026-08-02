#pragma once

// rosbag2 API shim: Humble uses SerializedBagMessage::time_stamp; Jazzy+ uses
// recv_timestamp/send_timestamp. CMake sets SPECTRA_ROSBAG2_RECV_TIMESTAMP when
// the linked rosbag2_storage provides the newer fields (see CMakeLists.txt).

#include <cstdint>

#include <rosbag2_storage/serialized_bag_message.hpp>

namespace spectra::adapters::ros2::bag_compat
{

inline int64_t bag_message_timestamp_ns(const rosbag2_storage::SerializedBagMessage& msg)
{
#if defined(SPECTRA_ROSBAG2_RECV_TIMESTAMP)
    return msg.recv_timestamp;
#else
    return msg.time_stamp;
#endif
}

inline void set_bag_message_timestamp(rosbag2_storage::SerializedBagMessage& msg,
                                      int64_t                                timestamp_ns)
{
#if defined(SPECTRA_ROSBAG2_RECV_TIMESTAMP)
    msg.recv_timestamp = timestamp_ns;
    msg.send_timestamp = timestamp_ns;
#else
    msg.time_stamp = timestamp_ns;
#endif
}

}   // namespace spectra::adapters::ros2::bag_compat
