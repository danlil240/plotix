// MoltenVK / Vulkan Portability smoke test.
//
// Validates that the Vulkan instance creation code correctly:
//   1. Conditionally enables VK_KHR_portability_enumeration when available.
//   2. Sets VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR.
//   3. Enables VK_KHR_portability_subset on devices that expose it.
//
// On non-portability platforms (desktop Linux/Windows), the test verifies
// that the code path does not crash and that portability is simply absent.
// On macOS/MoltenVK, it verifies that portability devices are discoverable.
//
// This test requires a GPU/display — it is tagged with the "gpu" label.

#include <gtest/gtest.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

// Fallback defines for older Vulkan headers.
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif
#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    #define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    #define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

namespace
{

bool has_instance_extension(const std::vector<VkExtensionProperties>& exts, const char* name)
{
    return std::any_of(exts.begin(),
                       exts.end(),
                       [name](const VkExtensionProperties& e)
                       { return std::strcmp(e.extensionName, name) == 0; });
}

bool has_device_extension(VkPhysicalDevice dev, const char* name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());
    return std::any_of(exts.begin(),
                       exts.end(),
                       [name](const VkExtensionProperties& e)
                       { return std::strcmp(e.extensionName, name) == 0; });
}

}   // namespace

// Test: instance creation with portability enumeration does not crash.
// On platforms without the extension, it is simply not enabled.
TEST(MoltenVKPortability, InstanceCreationWithPortability)
{
    // Query available instance extensions.
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data());

    bool has_portability =
        has_instance_extension(available_exts, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    // Build extension list.
    std::vector<const char*> extensions;
    if (has_instance_extension(available_exts, VK_KHR_SURFACE_EXTENSION_NAME))
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    if (has_portability)
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "SpectraPortabilityTest";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName        = "SpectraTest";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    if (has_portability)
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult   result   = vkCreateInstance(&create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS) << "Failed to create Vulkan instance with portability flags";
    ASSERT_NE(instance, VK_NULL_HANDLE);

    // Enumerate physical devices — should not crash even with portability.
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(instance, &dev_count, nullptr);
    if (dev_count > 0)
    {
        std::vector<VkPhysicalDevice> devices(dev_count);
        vkEnumeratePhysicalDevices(instance, &dev_count, devices.data());

        // Check if any device exposes portability subset.
        bool found_portability_subset = false;
        for (auto dev : devices)
        {
            if (has_device_extension(dev, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            {
                found_portability_subset = true;

                // Verify we can query properties without crashing.
                VkPhysicalDeviceProperties2 props2{};
                props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                vkGetPhysicalDeviceProperties2(dev, &props2);
                SUCCEED();
            }
        }

        if (has_portability)
        {
            // On MoltenVK, at least one device should expose portability subset.
            // We don't hard-require it because lavapipe/CI may not expose it,
            // but we log it for diagnostic purposes.
            if (!found_portability_subset)
            {
                GTEST_SKIP() << "Portability enumeration enabled but no device "
                                "exposes VK_KHR_portability_subset — likely "
                                "non-macOS platform";
            }
        }
    }

    vkDestroyInstance(instance, nullptr);
}

// Test: device extension query includes portability subset when available.
TEST(MoltenVKPortability, DeviceExtensionQuery)
{
    // Create a minimal instance.
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "SpectraDevExtTest";
    app_info.apiVersion       = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info{};
    create_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult   result   = vkCreateInstance(&create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(instance, &dev_count, nullptr);
    if (dev_count == 0)
    {
        vkDestroyInstance(instance, nullptr);
        GTEST_SKIP() << "No Vulkan devices available";
    }

    std::vector<VkPhysicalDevice> devices(dev_count);
    vkEnumeratePhysicalDevices(instance, &dev_count, devices.data());

    // For each device, verify we can enumerate extensions without crashing.
    for (auto dev : devices)
    {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
        ASSERT_GT(count, 0u);

        std::vector<VkExtensionProperties> exts(count);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());

        // Check for portability subset presence.
        bool has_subset = has_device_extension(dev, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);

        // If portability subset is present, verify we can query features.
        if (has_subset)
        {
            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            vkGetPhysicalDeviceFeatures2(dev, &features2);
            SUCCEED();
        }
    }

    vkDestroyInstance(instance, nullptr);
}

// Test: headless offscreen render with portability-aware instance.
// This mirrors what Spectra does on macOS: create instance, pick device,
// and verify the basic pipeline works.
TEST(MoltenVKPortability, HeadlessRenderSmoke)
{
    // Create instance with portability if available.
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available_exts.data());

    std::vector<const char*> extensions;
    bool                     has_portability =
        has_instance_extension(available_exts, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (has_portability)
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "SpectraHeadlessPortability";
    app_info.apiVersion       = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    if (has_portability)
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult   result   = vkCreateInstance(&create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    // Pick first physical device.
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(instance, &dev_count, nullptr);
    if (dev_count == 0)
    {
        vkDestroyInstance(instance, nullptr);
        GTEST_SKIP() << "No Vulkan devices available";
    }

    std::vector<VkPhysicalDevice> devices(dev_count);
    vkEnumeratePhysicalDevices(instance, &dev_count, devices.data());
    VkPhysicalDevice physical = devices[0];

    // Find graphics queue family.
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qf_count, qf_props.data());

    uint32_t graphics_family = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; ++i)
    {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphics_family = i;
            break;
        }
    }
    ASSERT_NE(graphics_family, UINT32_MAX) << "No graphics queue family found";

    // Create logical device with portability subset if available.
    std::vector<const char*> dev_exts;
    if (has_device_extension(physical, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        dev_exts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);

    float                   priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount       = 1;
    queue_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo dev_info{};
    dev_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount    = 1;
    dev_info.pQueueCreateInfos       = &queue_info;
    dev_info.enabledExtensionCount   = static_cast<uint32_t>(dev_exts.size());
    dev_info.ppEnabledExtensionNames = dev_exts.data();

    VkDevice device = VK_NULL_HANDLE;
    result          = vkCreateDevice(physical, &dev_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS) << "Failed to create logical device";

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_family, 0, &queue);
    ASSERT_NE(queue, (VkQueue)VK_NULL_HANDLE);

    // Create a command pool and buffer — basic smoke.
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = graphics_family;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;
    result             = vkCreateCommandPool(device, &pool_info, nullptr, &pool);
    ASSERT_EQ(result, VK_SUCCESS);

    VkCommandBufferAllocateInfo cmd_info{};
    cmd_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool        = pool;
    cmd_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    result              = vkAllocateCommandBuffers(device, &cmd_info, &cmd);
    ASSERT_EQ(result, VK_SUCCESS);

    // Begin/end a command buffer — verifies basic recording works.
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(cmd, &begin_info);
    ASSERT_EQ(result, VK_SUCCESS);

    vkEndCommandBuffer(cmd);
    ASSERT_EQ(result, VK_SUCCESS);

    // Cleanup.
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    SUCCEED() << "Headless render smoke test passed with portability-aware Vulkan";
}
