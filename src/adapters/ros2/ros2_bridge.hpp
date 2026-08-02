#pragma once

// Ros2Bridge — ROS2 node lifecycle wrapper.
//
// Owns a single rclcpp::Node plus a dedicated background spin thread that
// drives the executor.  All ROS2 I/O (subscriptions, service calls, etc.)
// is processed on that thread; the render thread never blocks on ROS2.
//
// Typical usage:
//   Ros2Bridge bridge;
//   bridge.init("spectra_ros", argc, argv);   // idempotent
//   bridge.start_spin();                       // launches bg thread
//   // ... use node() for subscriptions etc.
//   bridge.shutdown();                         // stops thread, destroys node

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

namespace spectra::adapters::ros2
{

// State of the bridge, in strict monotone order.
enum class BridgeState
{
    Uninitialized,
    Initialized,    // node created, executor ready, spin thread NOT yet running
    Spinning,       // background spin thread active
    ShuttingDown,   // shutdown() called, waiting for thread join
    Stopped,        // fully stopped; object may be destroyed or re-init'd
};

class Ros2Bridge
{
   public:
    Ros2Bridge();
    ~Ros2Bridge();

    // Non-copyable, non-movable (owns a thread).
    Ros2Bridge(const Ros2Bridge&)            = delete;
    Ros2Bridge& operator=(const Ros2Bridge&) = delete;
    Ros2Bridge(Ros2Bridge&&)                 = delete;
    Ros2Bridge& operator=(Ros2Bridge&&)      = delete;

    // ---------- lifecycle ------------------------------------------------

    // Initialise rclcpp (calls rclcpp::init if not already done) and create
    // the internal node with the given name and namespace.
    // Safe to call multiple times with the same arguments (idempotent).
    // Returns false if already initialised with a different node name.
    bool init(const std::string& node_name      = "spectra_ros",
              const std::string& node_namespace = "/",
              int                argc           = 0,
              char**             argv           = nullptr);

    // Launch the background spin thread.  Must call init() first.
    // Returns false if already spinning or not yet initialised.
    bool start_spin();

    // Stop the executor thread but keep node() valid so subscriptions can be
    // destroyed cleanly on the caller thread.  Idempotent.
    void stop_spin();

    // Remove the node from the executor while keeping it alive so subscriptions
    // can be destroyed without the spin thread running.  Idempotent.
    void detach_node_from_executor();

    // Stop the spin thread and tear down the node (process-wide rclcpp context
    // is left running; call rclcpp::shutdown() from main when the app exits).
    // Safe to call even if not spinning (no-op).
    void shutdown();

    // ---------- accessors ------------------------------------------------

    BridgeState state() const { return state_.load(std::memory_order_acquire); }
    bool        is_ok() const { return state() == BridgeState::Spinning; }

    // Returns the underlying node.  nullptr if not initialised.
    rclcpp::Node::SharedPtr node() const { return node_; }

    // Returns the executor.  nullptr if not initialised.
    rclcpp::executors::SingleThreadedExecutor* executor() const { return executor_.get(); }

    const std::string& node_name() const { return node_name_; }
    const std::string& node_namespace() const { return node_namespace_; }
    int                domain_id() const;

    // ---------- callbacks ------------------------------------------------

    // Called (from the spin thread) when the node has started spinning.
    using StateCallback = std::function<void(BridgeState)>;
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

   private:
    void spin_thread_func();
    void set_state(BridgeState s);

    std::string node_name_{"spectra_ros"};
    std::string node_namespace_{"/"};

    rclcpp::Node::SharedPtr                                    node_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;

    std::thread              spin_thread_;
    std::atomic<BridgeState> state_{BridgeState::Uninitialized};
    std::atomic<bool>        stop_requested_{false};

    StateCallback state_cb_;
};

}   // namespace spectra::adapters::ros2
