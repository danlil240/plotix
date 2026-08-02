#include "vk_device.hpp"

#include "spectra/logger.hpp"
#include "platform/window_system/surface_host.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

// Fallback defines for Vulkan headers < 1.3.216 (portability extension macros).
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif
#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    #define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

namespace spectra::vk
{

namespace
{

void append_unique(std::vector<const char*>& extensions, const char* ext)
{
    if (!ext)
    {
        return;
    }
    if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end())
    {
        extensions.push_back(ext);
    }
}

}   // namespace

static const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
               VkDebugUtilsMessageTypeFlagsEXT /*type*/,
               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
               void* /*user_data*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        SPECTRA_LOG_WARN("vulkan", "{}", callback_data->pMessage);
    }
    return VK_FALSE;
}

bool check_validation_layer_support()
{
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available.data());

    for (const char* name : validation_layers)
    {
        bool found = false;
        for (const auto& layer : available)
        {
            if (std::strcmp(name, layer.layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

VkInstance create_instance(bool                         enable_validation,
                           bool                         headless,
                           const platform::SurfaceHost* surface_host)
{
    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "Spectra";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName        = "Spectra Engine";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion         = VK_API_VERSION_1_2;

    std::vector<const char*> extensions;

    // Query available instance extensions
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data());

    auto has_ext = [&](const char* name)
    {
        return std::any_of(available_exts.begin(),
                           available_exts.end(),
                           [name](const VkExtensionProperties& e)
                           { return std::strcmp(e.extensionName, name) == 0; });
    };

    if (enable_validation && has_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        append_unique(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (!headless)
    {
        if (surface_host)
        {
            surface_host->append_instance_extensions(extensions);
        }

        // Fallback: add generic surface extension if available.
        if (has_ext(VK_KHR_SURFACE_EXTENSION_NAME))
        {
            append_unique(extensions, VK_KHR_SURFACE_EXTENSION_NAME);
        }
    }

    // macOS/MoltenVK portability: conditionally enable VK_KHR_portability_enumeration
    // so that devices exposing VK_KHR_portability_subset are discoverable.
    // Per the Vulkan portability extension spec, we must set the enumerate
    // portability bit in VkInstanceCreateInfo when the extension is enabled.
    bool has_portability = has_ext(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (has_portability)
    {
        append_unique(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    if (has_portability)
    {
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    if (enable_validation && check_validation_layer_support())
    {
        create_info.enabledLayerCount   = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkResult   result   = vkCreateInstance(&create_info, nullptr, &instance);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    return instance;
}

VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func)
        return VK_NULL_HANDLE;

    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    func(instance, &info, nullptr, &messenger);
    return messenger;
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger == VK_NULL_HANDLE)
        return;
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func)
        func(instance, messenger, nullptr);
}

QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    // First pass: pick graphics and dedicated transfer queue families.
    for (uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.graphics.has_value())
        {
            indices.graphics = i;
        }

        if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            && !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            indices.transfer = i;
        }
    }

    // Second pass: pick present queue family. Prefer the graphics family to avoid
    // cross-family ownership transfers and a separate present queue.
    if (surface != VK_NULL_HANDLE)
    {
        if (indices.graphics.has_value())
        {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device,
                                                 indices.graphics.value(),
                                                 surface,
                                                 &present_support);
            if (present_support)
            {
                indices.present = indices.graphics;
            }
        }

        if (!indices.present.has_value())
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
                if (present_support)
                {
                    indices.present = i;
                    break;
                }
            }
        }
    }

    // Fallback: use graphics queue for transfer if no dedicated transfer queue
    if (!indices.transfer.has_value() && indices.graphics.has_value())
    {
        indices.transfer = indices.graphics;
    }

    return indices;
}

static int rate_device(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    auto indices = find_queue_families(device, surface);
    if (!indices.is_complete())
        return -1;
    if (surface != VK_NULL_HANDLE && !indices.has_present())
        return -1;

    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 100;

    score += static_cast<int>(props.limits.maxImageDimension2D / 1024);
    return score;
}

VkPhysicalDevice pick_physical_device(VkInstance instance, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
    {
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best       = VK_NULL_HANDLE;
    int              best_score = -1;

    for (auto dev : devices)
    {
        int score = rate_device(dev, surface);
        if (score > best_score)
        {
            best_score = score;
            best       = dev;
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        throw std::runtime_error("No suitable Vulkan GPU found");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(best, &props);
    Logger::instance().log(LogLevel::Info, "vulkan", "Using GPU: " + std::string(props.deviceName));

    // A CPU device means software rasterization (e.g. llvmpipe/lavapipe). This
    // is correct for headless/CI but cripples interactive performance — frame
    // cost scales with primitive count instead of being GPU-bound. It is almost
    // always an environment misconfiguration (VK_ICD_FILENAMES pinned to lvp_icd
    // / LIBGL_ALWAYS_SOFTWARE) masking a real GPU. Warn loudly so it is visible.
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
    {
        Logger::instance().log(
            LogLevel::Warning,
            "vulkan",
            "Selected a CPU (software) Vulkan device '" + std::string(props.deviceName)
                + "' — rendering will be slow and worsen as data grows. If this machine has a "
                  "GPU, unset VK_ICD_FILENAMES and LIBGL_ALWAYS_SOFTWARE before launching.");
    }

    return best;
}

static bool has_device_extension(VkPhysicalDevice physical_device, const char* extension_name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());

    return std::any_of(extensions.begin(),
                       extensions.end(),
                       [extension_name](const VkExtensionProperties& ext)
                       { return std::strcmp(ext.extensionName, extension_name) == 0; });
}

std::vector<const char*> get_required_device_extensions(VkPhysicalDevice physical_device,
                                                        bool             need_swapchain)
{
    std::vector<const char*> exts;
    if (need_swapchain)
    {
        exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    if (has_device_extension(physical_device, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
    {
        exts.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    }

    // macOS/MoltenVK: VK_KHR_portability_subset is mandatory when present.
    // Per the portability subset spec, it must be enabled if available.
    if (has_device_extension(physical_device, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
    {
        exts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    return exts;
}

VkDevice create_logical_device(VkPhysicalDevice          physical_device,
                               const QueueFamilyIndices& indices,
                               bool                      enable_validation)
{
    std::set<uint32_t> unique_families;
    if (indices.graphics.has_value())
        unique_families.insert(indices.graphics.value());
    if (indices.present.has_value())
        unique_families.insert(indices.present.value());

    float                                priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (uint32_t family : unique_families)
    {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queue_infos.push_back(qi);
    }

    VkPhysicalDeviceFeatures supported_features{};
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

    VkPhysicalDeviceFeatures features{};
    if (supported_features.wideLines)
        features.wideLines = VK_TRUE;

    bool need_swapchain = indices.has_present();
    auto extensions     = get_required_device_extensions(physical_device, need_swapchain);

    VkDeviceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_infos.size());
    create_info.pQueueCreateInfos       = queue_infos.data();
    create_info.pEnabledFeatures        = &features;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    if (enable_validation && check_validation_layer_support())
    {
        create_info.enabledLayerCount   = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
    }

    VkDevice device = VK_NULL_HANDLE;
    VkResult result = vkCreateDevice(physical_device, &create_info, nullptr, &device);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    return device;
}

}   // namespace spectra::vk
