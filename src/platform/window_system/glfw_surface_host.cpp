#include "glfw_surface_host.hpp"

#include <algorithm>

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "ui/window/glfw_utils.hpp"
#endif

namespace spectra::platform
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

void GlfwSurfaceHost::append_instance_extensions(std::vector<const char*>& extensions) const
{
#ifdef SPECTRA_USE_GLFW
    // Instance creation can happen before GlfwAdapter::init(), so ensure GLFW
    // is initialized before querying required Vulkan instance extensions.
    configure_glfw_platform();
    if (!glfwInit())
    {
        return;
    }

    uint32_t     count = 0;
    const char** exts  = glfwGetRequiredInstanceExtensions(&count);
    if (!exts)
    {
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        append_unique(extensions, exts[i]);
    }
#else
    (void)extensions;
#endif
}

bool GlfwSurfaceHost::create_surface(VkInstance    instance,
                                     void*         native_window,
                                     VkSurfaceKHR& out_surface) const
{
#ifdef SPECTRA_USE_GLFW
    auto* window = static_cast<GLFWwindow*>(native_window);
    if (!window)
    {
        return false;
    }
    out_surface = VK_NULL_HANDLE;
    return glfwCreateWindowSurface(instance, window, nullptr, &out_surface) == VK_SUCCESS;
#else
    (void)instance;
    (void)native_window;
    (void)out_surface;
    return false;
#endif
}

bool GlfwSurfaceHost::framebuffer_size(void* native_window, SurfaceSize& out_size) const
{
#ifdef SPECTRA_USE_GLFW
    auto* window = static_cast<GLFWwindow*>(native_window);
    if (!window)
    {
        return false;
    }

    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    out_size.width  = w > 0 ? static_cast<uint32_t>(w) : 0;
    out_size.height = h > 0 ? static_cast<uint32_t>(h) : 0;
    return true;
#else
    (void)native_window;
    (void)out_size;
    return false;
#endif
}

bool GlfwSurfaceHost::query_presentation_support(VkInstance       instance,
                                                 VkPhysicalDevice physical_device,
                                                 uint32_t         queue_family_index) const
{
#ifdef SPECTRA_USE_GLFW
    if (instance == VK_NULL_HANDLE || physical_device == VK_NULL_HANDLE)
    {
        return false;
    }
    configure_glfw_platform();
    if (!glfwInit())
    {
        return true;   // Can't query — preserve back-compat assumption.
    }
    return glfwGetPhysicalDevicePresentationSupport(instance, physical_device, queue_family_index)
           == GLFW_TRUE;
#else
    (void)instance;
    (void)physical_device;
    (void)queue_family_index;
    return true;
#endif
}

}   // namespace spectra::platform
