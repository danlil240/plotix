#ifdef SPECTRA_USE_GLFW

    #include "glfw_adapter.hpp"

    #include "glfw_utils.hpp"
    #include <spectra/logger.hpp>

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
namespace spectra
{

GlfwAdapter::~GlfwAdapter()
{
    shutdown();
}

bool GlfwAdapter::init(uint32_t width, uint32_t height, const std::string& title)
{
    configure_glfw_platform();
    if (!glfwInit())
    {
        SPECTRA_LOG_ERROR("glfw", "Failed to initialize GLFW");
        return false;
    }

    if (!glfwVulkanSupported())
    {
        SPECTRA_LOG_ERROR("glfw", "GLFW: Vulkan not supported");
        glfwTerminate();
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // No OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    set_wayland_app_id();

    window_ = glfwCreateWindow(static_cast<int>(width),
                               static_cast<int>(height),
                               title.c_str(),
                               nullptr,
                               nullptr);

    if (!window_)
    {
        SPECTRA_LOG_ERROR("glfw", "Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    // NOTE: No GLFW callbacks are installed here.  WindowManager owns all
    // callbacks for every window (initial and secondary).  Installing
    // callbacks here would cause ImGui's callback chaining to invoke them
    // with the wrong user-pointer type (WindowManager* vs GlfwAdapter*),
    // resulting in a segfault.

    set_window_icon(window_);

    return true;
}

void GlfwAdapter::shutdown()
{
    destroy_window();
    terminate();
}

void GlfwAdapter::destroy_window()
{
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

void GlfwAdapter::terminate()
{
    glfwTerminate();
}

void GlfwAdapter::poll_events()
{
    glfwPollEvents();
}

void GlfwAdapter::wait_events()
{
    glfwWaitEvents();
}

bool GlfwAdapter::should_close() const
{
    return (window_ ? glfwWindowShouldClose(window_) : 1) != 0;
}

void* GlfwAdapter::native_window() const
{
    return static_cast<void*>(window_);
}

void GlfwAdapter::framebuffer_size(uint32_t& width, uint32_t& height) const
{
    if (window_)
    {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        width  = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
    }
    else
    {
        width  = 0;
        height = 0;
    }
}

void GlfwAdapter::mouse_position(double& x, double& y) const
{
    if (window_)
    {
        glfwGetCursorPos(window_, &x, &y);
    }
    else
    {
        x = 0.0;
        y = 0.0;
    }
}

void GlfwAdapter::window_pos(int& x, int& y) const
{
    if (window_)
    {
        glfwGetWindowPos(window_, &x, &y);
    }
    else
    {
        x = 0;
        y = 0;
    }
}

void GlfwAdapter::window_size(int& width, int& height) const
{
    if (window_)
    {
        glfwGetWindowSize(window_, &width, &height);
    }
    else
    {
        width  = 0;
        height = 0;
    }
}

void GlfwAdapter::hide_window()
{
    if (window_)
        glfwHideWindow(window_);
}

void GlfwAdapter::show_window()
{
    if (window_)
        glfwShowWindow(window_);
}

bool GlfwAdapter::is_mouse_button_pressed(int button) const
{
    if (window_)
    {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    return false;
}

}   // namespace spectra

#endif   // SPECTRA_USE_GLFW
