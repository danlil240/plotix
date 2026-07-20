#pragma once

#include <functional>
#include <memory>
#include <spectra/fwd.hpp>

namespace spectra
{

class FigureRegistry;
class SessionRuntime;
class ExportFormatRegistry;
class OverlayRegistry;
class PluginManager;
class SeriesClipboard;
class VulkanBackend;

struct WindowContext;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
class WindowManager;
#endif

#ifdef SPECTRA_USE_IMGUI
class ImGuiIntegration;
#endif

namespace ui::settings
{
class SettingsStore;
}

enum class WindowUIContextBuildMode
{
    Full,        // FigureManager, commands, shortcuts, optional ImGuiIntegration object
    Headless,    // FigureManager + ThemeManager only (golden tests, rapid teardown)
    ImGuiOnly,   // ThemeManager + ImGuiIntegration object only (panel/preview windows)
};

struct WindowUIContextBuildOptions
{
    FigureRegistry*   registry          = nullptr;
    ui::ThemeManager* theme_mgr         = nullptr;
    FigureId          initial_figure_id = INVALID_FIGURE_ID;

    Figure**  active_figure    = nullptr;
    FigureId* active_figure_id = nullptr;

    SessionRuntime* session = nullptr;

    PluginManager*        plugin_manager         = nullptr;
    ExportFormatRegistry* export_format_registry = nullptr;
    OverlayRegistry*      overlay_registry       = nullptr;
    SeriesClipboard*      series_clipboard       = nullptr;

    ui::settings::SettingsStore* settings_store = nullptr;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    WindowManager* window_manager = nullptr;
    uint32_t       window_id      = 0;
#endif

    std::function<void()>                  on_window_close_request;
    std::function<void(FigureId, Figure*)> on_figure_closed;

    WindowUIContextBuildMode mode = WindowUIContextBuildMode::Full;

    bool create_imgui_integration = false;

    // Deprecated: prefer `mode = WindowUIContextBuildMode::Headless`.
    bool headless = false;
};

std::unique_ptr<WindowUIContext> build_window_ui_context(
    const WindowUIContextBuildOptions& options);

#ifdef SPECTRA_USE_IMGUI
// Bind ImGuiIntegration to a native window, saving/restoring the backend's
// active window and the thread-local ImGui context.
bool init_window_imgui_integration(VulkanBackend&    backend,
                                   WindowContext&    wctx,
                                   ImGuiIntegration& imgui,
                                   bool              install_callbacks = false);
#endif

}   // namespace spectra
