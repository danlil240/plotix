#pragma once

#ifdef SPECTRA_USE_GLFW

    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
    #include <cstdlib>
    #include <cstring>
    #include <spectra/logger.hpp>
    #include <stb_image.h>
    #include <vector>

    #include "glfw_platform_policy.hpp"

    #if __has_include("spectra_icon_embedded.hpp")
        #include "spectra_icon_embedded.hpp"
        #define SPECTRA_HAS_EMBEDDED_ICON 1
    #else
        #define SPECTRA_HAS_EMBEDDED_ICON 0
    #endif

namespace spectra
{

// Must run before the first glfwInit(). Vulkan queries can initialize GLFW
// before GlfwAdapter::init(), so every initialization entry point calls this
// idempotent helper.
inline void configure_glfw_platform()
{
    static const bool configured = []()
    {
    #if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
        const char* wayland = std::getenv("WAYLAND_DISPLAY");

        const auto preference = choose_glfw_platform(wayland ? wayland : "");
        switch (preference)
        {
            case GlfwPlatformPreference::X11:
            {
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
                SPECTRA_LOG_INFO(
                    "glfw",
                    "Wayland session detected; using XWayland for correct tab tear-off behavior");
                const char* x11 = std::getenv("DISPLAY");
                if (!x11 || !*x11)
                    SPECTRA_LOG_ERROR("glfw", "XWayland is required but DISPLAY is not set");
                break;
            }
            case GlfwPlatformPreference::Automatic:
                break;
        }
    #endif
        return true;
    }();
    (void)configured;
}

// Downscale RGBA image to target_size x target_size using box filter.
// Returns empty vector if src is already <= target_size.
inline std::vector<unsigned char> downscale_icon(const unsigned char* src,
                                                 int                  src_w,
                                                 int                  src_h,
                                                 int                  target_size)
{
    if (src_w <= target_size && src_h <= target_size)
        return {};

    std::vector<unsigned char> dst(target_size * target_size * 4);
    const float                sx = static_cast<float>(src_w) / target_size;
    const float                sy = static_cast<float>(src_h) / target_size;

    for (int dy = 0; dy < target_size; ++dy)
    {
        for (int dx = 0; dx < target_size; ++dx)
        {
            int x0 = static_cast<int>(dx * sx);
            int y0 = static_cast<int>(dy * sy);
            int x1 = static_cast<int>((dx + 1) * sx);
            int y1 = static_cast<int>((dy + 1) * sy);
            if (x1 > src_w)
                x1 = src_w;
            if (y1 > src_h)
                y1 = src_h;

            float r = 0, g = 0, b = 0, a = 0;
            int   count = 0;
            for (int py = y0; py < y1; ++py)
            {
                for (int px = x0; px < x1; ++px)
                {
                    const unsigned char* p = src + (py * src_w + px) * 4;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    ++count;
                }
            }

            unsigned char* d = dst.data() + (dy * target_size + dx) * 4;
            if (count > 0)
            {
                d[0] = static_cast<unsigned char>(r / count);
                d[1] = static_cast<unsigned char>(g / count);
                d[2] = static_cast<unsigned char>(b / count);
                d[3] = static_cast<unsigned char>(a / count);
            }
            else
            {
                std::memset(d, 0, 4);
            }
        }
    }
    return dst;
}

// Set the Wayland app_id hint before window creation.
// On GNOME/Wayland, the title bar icon comes from the .desktop file
// matching this app_id, not from glfwSetWindowIcon().
// Must be called BEFORE glfwCreateWindow().
inline void set_wayland_app_id()
{
    #ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "spectra");
    #endif
}

inline void set_window_icon(GLFWwindow* window)
{
    if (!window)
        return;

    constexpr int ICON_SIZES[] = {16, 32, 48};
    constexpr int N            = sizeof(ICON_SIZES) / sizeof(ICON_SIZES[0]);

    // Helper: given raw RGBA pixels, set the GLFW window icon and return true.
    auto apply_icon = [&](unsigned char* pixels, int w, int h, bool needs_free) -> bool
    {
        std::vector<unsigned char> buffers[N];
        GLFWimage                  images[N];

        for (int i = 0; i < N; ++i)
        {
            buffers[i] = downscale_icon(pixels, w, h, ICON_SIZES[i]);
            if (!buffers[i].empty())
            {
                images[i].width  = ICON_SIZES[i];
                images[i].height = ICON_SIZES[i];
                images[i].pixels = buffers[i].data();
            }
            else
            {
                images[i].width  = w;
                images[i].height = h;
                images[i].pixels = pixels;
            }
        }

        glfwSetWindowIcon(window, N, images);
        if (needs_free)
            stbi_image_free(pixels);
        return true;
    };

    // 1. Try embedded icon (always available in a normal build)
    #if SPECTRA_HAS_EMBEDDED_ICON
    {
        int            w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(SpectraIcon_png_data,
                                                      static_cast<int>(SpectraIcon_png_size),
                                                      &w,
                                                      &h,
                                                      &channels,
                                                      4);
        if (pixels)
        {
            apply_icon(pixels, w, h, true);
            return;
        }
    }
    #endif

    // 2. Fall back to file on disk (dev tree + installed locations)
    const char* icon_paths[] = {
        "icons/spectra_icon.png",
        "../icons/spectra_icon.png",
        "../../icons/spectra_icon.png",
        "../../../icons/spectra_icon.png",
        "/usr/share/icons/hicolor/256x256/apps/spectra.png",
        "/usr/local/share/icons/hicolor/256x256/apps/spectra.png",
    };

    for (const char* path : icon_paths)
    {
        int            w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load(path, &w, &h, &channels, 4);
        if (pixels)
        {
            apply_icon(pixels, w, h, true);
            return;
        }
    }

    SPECTRA_LOG_WARN("glfw", "Could not load window icon (spectra_icon.png)");
}

}   // namespace spectra

#endif   // SPECTRA_USE_GLFW
