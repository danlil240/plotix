#pragma once

#include <memory>
#include <spectra/fwd.hpp>

namespace spectra
{

class ExportFormatRegistry;
class FigureRegistry;
class PluginManager;
class Renderer;
class SessionRuntime;
class VulkanBackend;
class WindowManager;

namespace ui
{
class ThemeManager;
}

namespace ui::settings
{
class SettingsStore;
}

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)

struct WindowManagerBootstrapOptions
{
    VulkanBackend*               backend                = nullptr;
    FigureRegistry*              registry               = nullptr;
    Renderer*                    renderer               = nullptr;
    ui::ThemeManager*            theme_mgr              = nullptr;
    SessionRuntime*              session                = nullptr;
    ui::settings::SettingsStore* settings_store         = nullptr;
    PluginManager*               plugin_manager         = nullptr;
    ExportFormatRegistry*        export_format_registry = nullptr;
};

// Apply shared session/tab/settings wiring used by App and the window agent.
void configure_window_manager(WindowManager& wm, const WindowManagerBootstrapOptions& options);

std::unique_ptr<WindowManager> create_configured_window_manager(
    const WindowManagerBootstrapOptions& options);

#endif

}   // namespace spectra
