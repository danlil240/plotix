#pragma once

// BagDisplaySync — replay bag messages into TF + 3D displays at the playhead.
//
// Used in bag mode so LaserScan, Path, Pose, and TF views match plot scrub time.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bag_reader.hpp"
#include "display/display_plugin.hpp"
#include "tf/tf_buffer.hpp"

namespace spectra::adapters::ros2
{

class BagDisplaySync
{
   public:
    BagDisplaySync() = default;

    bool        open(const std::string& bag_path);
    void        close();
    bool        is_open() const noexcept { return open_; }
    std::string bag_path() const { return bag_path_; }

    // Replay messages with bag_time <= playhead_sec into tf_buffer and displays.
    void sync_to_playhead(double                      playhead_sec,
                          int64_t                     bag_start_time_ns,
                          TfBuffer&                   tf_buffer,
                          std::vector<DisplayPlugin*> displays);

   private:
    void reset_playback_state(TfBuffer& tf_buffer, std::vector<DisplayPlugin*>& displays);
    void process_message(const BagMessage&            msg,
                         double                       bag_start_ns,
                         double                       playhead_sec,
                         TfBuffer&                    tf_buffer,
                         std::vector<DisplayPlugin*>& displays);

    BagReader                   reader_;
    bool                        open_{false};
    std::string                 bag_path_;
    std::vector<TransformStamp> static_tf_cache_;
    double                      last_playhead_sec_{-1.0};
};

}   // namespace spectra::adapters::ros2
