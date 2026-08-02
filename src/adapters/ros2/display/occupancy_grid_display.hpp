#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "display_plugin.hpp"
#include "messages/occupancy_grid_adapter.hpp"

#ifdef SPECTRA_USE_ROS2
    #include <nav_msgs/msg/occupancy_grid.hpp>
#endif

namespace spectra::adapters::ros2
{

class OccupancyGridDisplay : public DisplayPlugin
{
   public:
    OccupancyGridDisplay();

    std::string type_id() const override { return "occupancy_grid"; }
    std::string display_name() const override { return "OccupancyGrid"; }
    std::string icon() const override { return "OG"; }

    std::vector<std::string> compatible_message_types() const override
    {
        return {"nav_msgs/msg/OccupancyGrid"};
    }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_destroy() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    std::string serialize_config_blob() const override;
    void        deserialize_config_blob(const std::string& blob) override;

   private:
    void                              ensure_subscription();
    std::optional<OccupancyGridFrame> latest_frame() const;

    const TfBuffer*       tf_buffer_{nullptr};
    TopicDiscovery*       topic_discovery_{nullptr};
    std::string           fixed_frame_;
    bool                  resubscribe_requested_{false};
    std::string           subscribed_topic_;
    float                 alpha_{0.85f};
    std::array<char, 256> topic_input_{};

    mutable std::mutex                frame_mutex_;
    std::optional<OccupancyGridFrame> frame_;

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node::SharedPtr                                       node_;
    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr subscription_;
#endif
};

}   // namespace spectra::adapters::ros2
