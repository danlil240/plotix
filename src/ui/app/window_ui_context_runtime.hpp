#pragma once

#include <spectra/fwd.hpp>

namespace spectra
{

class FigureRegistry;
class SessionRuntime;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
class WindowManager;
#endif

enum class TabSplitMode
{
    SplitPane,            // Split an existing multi-tab pane (inproc App)
    DuplicateThenSplit,   // Duplicate figure, then split (multiproc agent)
};

struct WindowUIContextRuntimeWireOptions
{
    WindowUIContext* ui_ctx        = nullptr;
    FigureRegistry*  registry      = nullptr;
    SessionRuntime*  session       = nullptr;
    Figure*          active_figure = nullptr;
    bool             has_animation = false;

    TabSplitMode tab_split_mode = TabSplitMode::SplitPane;
    // When true, skip on_drop_outside/on_drop_on_window (builder or WindowManager
    // already wired them). WindowManager pointer is always assigned when provided.
    bool tab_drag_already_wired       = false;
    bool wire_demo_animation_channels = false;
    bool enable_window_tab_callbacks  = true;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    WindowManager* window_manager = nullptr;
#endif
};

#ifdef SPECTRA_USE_IMGUI
void wire_window_ui_runtime(const WindowUIContextRuntimeWireOptions& options);
void capture_figure_home_limits(FigureRegistry& registry, FigureManager& fig_mgr);
#endif

}   // namespace spectra
