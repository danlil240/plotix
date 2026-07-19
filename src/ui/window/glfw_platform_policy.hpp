#pragma once

#include <string_view>

namespace spectra
{

enum class GlfwPlatformPreference
{
    Automatic,
    X11,
};

// Native Wayland deliberately has no API for placing a top-level window at a
// screen coordinate. Spectra's tab tear-off UX needs that guarantee, so when
// running in a Wayland session we require XWayland. This is not user-
// overridable because native Wayland cannot provide correct tear-off placement.
inline GlfwPlatformPreference choose_glfw_platform(std::string_view wayland_display)
{
    if (!wayland_display.empty())
        return GlfwPlatformPreference::X11;

    return GlfwPlatformPreference::Automatic;
}

}   // namespace spectra
