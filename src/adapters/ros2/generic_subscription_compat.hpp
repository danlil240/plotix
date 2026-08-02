#pragma once

// rclcpp generic-subscription callback compatibility for Humble and Jazzy.
//
// Jazzy removed AnySubscriptionCallback support for lambdas taking
//   const std::shared_ptr<rclcpp::SerializedMessage>&  (non-const pointee)
// Both distros accept:
//   const std::shared_ptr<const rclcpp::SerializedMessage>&
//
// Use SerializedMessageCallbackArg in create_generic_subscription lambdas and
// matching on_message() handlers. SerializationBase::deserialize_message()
// already takes const SerializedMessage*.

#include <memory>

#include <rclcpp/serialized_message.hpp>

namespace spectra::adapters::ros2::sub_compat
{

using SerializedMessageConstPtr    = std::shared_ptr<const rclcpp::SerializedMessage>;
using SerializedMessageCallbackArg = const SerializedMessageConstPtr&;

}   // namespace spectra::adapters::ros2::sub_compat
