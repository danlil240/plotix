#pragma once

// RAII wrapper for Vulkan validation layer error checking.
// Installs a VkDebugUtilsMessengerEXT that captures validation errors,
// warnings, and performance warnings during its lifetime. On destruction
// (or explicit check), asserts that zero errors were recorded.
//
// Usage:
//   {
//       spectra::test::ValidationGuard guard(vk_instance);
//       // ... do Vulkan work ...
//       EXPECT_TRUE(guard.ok());
//       EXPECT_EQ(guard.error_count(), 0u);
//   }
//
// NOTE: Requires VK_EXT_debug_utils. If the extension is not available
// (e.g. lavapipe without layers), the guard is a no-op and ok() returns true.
//
// Day 0 scaffolding: This header compiles against the current codebase.
// It uses the Vulkan C API directly (no dependency on WindowContext).

#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace spectra::test
{

// Severity filter for which messages to capture
enum class ValidationSeverity : uint32_t
{
    Error   = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    Warning = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
    Info    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
    Verbose = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
    All     = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
    ErrorsAndWarnings = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
};

struct ValidationMessage
{
    VkDebugUtilsMessageSeverityFlagBitsEXT severity;
    VkDebugUtilsMessageTypeFlagsEXT        type;
    std::string                            message_id;
    std::string                            message;
};

class ValidationGuard
{
   public:
    // Construct with a VkInstance. If instance is VK_NULL_HANDLE or the
    // debug utils extension is not loaded, the guard is a no-op.
    explicit ValidationGuard(
        VkInstance         instance,
        ValidationSeverity severity_filter = ValidationSeverity::ErrorsAndWarnings)
        : instance_(instance)
    {
        if (instance_ == VK_NULL_HANDLE)
            return;

        auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        destroy_fn_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));

        if (!create_fn || !destroy_fn_)
        {
            // Extension not available — guard is a no-op
            destroy_fn_ = nullptr;
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT ci{};
        ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity = static_cast<VkDebugUtilsMessageSeverityFlagsEXT>(severity_filter);
        ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = debug_callback;
        ci.pUserData       = this;

        VkResult result = create_fn(instance_, &ci, nullptr, &messenger_);
        if (result != VK_SUCCESS)
        {
            messenger_  = VK_NULL_HANDLE;
            destroy_fn_ = nullptr;
        }
    }

    ~ValidationGuard()
    {
        if (messenger_ != VK_NULL_HANDLE && destroy_fn_)
        {
            destroy_fn_(instance_, messenger_, nullptr);
        }
    }

    // Non-copyable, non-movable
    ValidationGuard(const ValidationGuard&)            = delete;
    ValidationGuard& operator=(const ValidationGuard&) = delete;

    // Returns true if zero errors were recorded
    bool ok() const { return error_count_.load(std::memory_order_relaxed) == 0; }

    // Number of error-severity messages
    uint32_t error_count() const { return error_count_.load(std::memory_order_relaxed); }

    // Number of warning-severity messages
    uint32_t warning_count() const { return warning_count_.load(std::memory_order_relaxed); }

    // Total messages captured
    uint32_t total_count() const { return total_count_.load(std::memory_order_relaxed); }

    // Get all captured messages (thread-safe copy)
    std::vector<ValidationMessage> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    // Clear all captured messages and counters
    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
        error_count_.store(0, std::memory_order_relaxed);
        warning_count_.store(0, std::memory_order_relaxed);
        total_count_.store(0, std::memory_order_relaxed);
    }

    // Print all captured messages to stderr (useful for debugging)
    void dump() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& msg : messages_)
        {
            const char* sev = "???";
            if (msg.severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                sev = "ERROR";
            else if (msg.severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                sev = "WARNING";
            else if (msg.severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
                sev = "INFO";
            else if (msg.severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
                sev = "VERBOSE";

            fprintf(stderr, "[ValidationGuard] %s: %s\n", sev, msg.message.c_str());
        }
    }

    // GTest assertion helper: EXPECT no validation errors
    void expect_no_errors(const char* context = "")
    {
        if (!ok())
        {
            dump();
            ADD_FAILURE() << "Vulkan validation errors detected" << (context[0] ? " during " : "")
                          << context << ": " << error_count() << " error(s), " << warning_count()
                          << " warning(s)";
        }
    }

   private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                   VkDebugUtilsMessageTypeFlagsEXT             type,
                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                   void*                                       user_data)
    {
        auto* self = static_cast<ValidationGuard*>(user_data);

        ValidationMessage msg;
        msg.severity   = severity;
        msg.type       = type;
        msg.message_id = data->pMessageIdName ? data->pMessageIdName : "";
        msg.message    = data->pMessage ? data->pMessage : "";

        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->messages_.push_back(std::move(msg));
        }

        self->total_count_.fetch_add(1, std::memory_order_relaxed);

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            self->error_count_.fetch_add(1, std::memory_order_relaxed);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            self->warning_count_.fetch_add(1, std::memory_order_relaxed);

        return VK_FALSE;   // Don't abort the Vulkan call
    }

    VkInstance                          instance_   = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT            messenger_  = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn_ = nullptr;

    mutable std::mutex             mutex_;
    std::vector<ValidationMessage> messages_;
    std::atomic<uint32_t>          error_count_{0};
    std::atomic<uint32_t>          warning_count_{0};
    std::atomic<uint32_t>          total_count_{0};
};

}   // namespace spectra::test
