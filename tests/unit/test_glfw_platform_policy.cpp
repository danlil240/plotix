#include <gtest/gtest.h>

#include "ui/window/glfw_platform_policy.hpp"

namespace spectra
{
namespace
{

TEST(GlfwPlatformPolicy, RequiresXWaylandForWaylandSession)
{
    EXPECT_EQ(choose_glfw_platform("wayland-0"), GlfwPlatformPreference::X11);
}

TEST(GlfwPlatformPolicy, KeepsAutomaticSelectionOutsideWayland)
{
    EXPECT_EQ(choose_glfw_platform(""), GlfwPlatformPreference::Automatic);
}

}   // namespace
}   // namespace spectra
