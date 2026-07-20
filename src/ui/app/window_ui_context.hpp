#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <spectra/fwd.hpp>
#include <unordered_map>

// Framework-neutral includes (always available, no ImGui dependency)
#include <spectra/camera.hpp>
#include "ui/animation/animation_curve_editor.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/docking/dock_system.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/overlay/knob_manager.hpp"
#include "ui/animation/mode_transition.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/commands/shortcut_config.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/figures/tab_bar.hpp"
#include "ui/animation/timeline_editor.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/settings/settings_store.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/input/box_zoom_overlay.hpp"
    #include "ui/commands/command_palette.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/figures/tab_drag_controller.hpp"
    #include "ui/topics/topics_panel.hpp"
    #include "ui/settings/settings_panel.hpp"
    #include "ui/shell/spectra_app_shell.hpp"
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/animation/animation_controller.hpp"
    #include "ui/input/gesture_recognizer.hpp"
    #include "ui/input/input.hpp"
#endif

namespace spectra
{

class PluginManager;

// Per-window UI subsystem bundle.
// Groups all UI objects that were previously stack-local in App::run()
// so they can be instantiated per-window in multi-window mode.
//
// Phase 1 (PR1): Single instance used by App::run() — zero behavior change.
// Phase 2+: One instance per WindowContext for full multi-window support.
struct WindowUIContext
{
    // Injected ThemeManager (not owned).  Set by WindowManager::init_window_ui()
    // from the App-owned instance.  All commands and UI code that previously
    // called ThemeManager::instance() should use this pointer instead.
    ui::ThemeManager* theme_mgr = nullptr;

    // Framework-neutral members (always present)
    FigureManager*                 fig_mgr = nullptr;   // Owned below via unique_ptr
    std::unique_ptr<FigureManager> fig_mgr_owned;

    DockSystem dock_system;
    bool       dock_tab_sync_guard = false;

    AxisLinkManager axis_link_mgr;

    TimelineEditor       timeline_editor;
    KeyframeInterpolator keyframe_interpolator;
    AnimationCurveEditor curve_editor;

    ModeTransition mode_transition;

    CommandRegistry cmd_registry;
    ShortcutManager shortcut_mgr;
    UndoManager     undo_mgr;

    KnobManager knob_manager;

    ShortcutConfig               settings_cfg;
    ui::settings::SettingsStore* settings_store = nullptr;

    // Plugin overlay registry (shared across windows, not owned — owned by PluginManager)
    OverlayRegistry* overlay_registry = nullptr;

    // Plugin manager (shared across windows, not owned — owned by AppRuntime)
    PluginManager* plugin_manager = nullptr;

    // Per-window active figure pointer, updated each frame by the render loop.
    Figure*  per_window_active_figure    = nullptr;
    FigureId per_window_active_figure_id = INVALID_FIGURE_ID;

#ifdef SPECTRA_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
    std::unique_ptr<DataInteraction>  data_interaction;
    std::unique_ptr<TabBar>           figure_tabs;

    BoxZoomOverlay box_zoom_overlay;

    CommandPalette  cmd_palette;

    TabDragController tab_drag_controller;

    // Topics panel (Phase 2 of plans/SPECTRA_TOPICS_PLAN.md).
    ui::topics::TopicsPanel topics_panel;

    // Settings panel — owned by this context, wired to the process-scoped
    // SettingsStore during window_ui_context_builder.
    ui::settings::SettingsPanel  settings_panel;

    std::unique_ptr<ui::shell::SpectraAppShell> app_shell;
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    AnimationController anim_controller;
    GestureRecognizer   gesture;
    InputHandler        input_handler;

    bool                                  needs_resize = false;
    uint32_t                              new_width    = 0;
    uint32_t                              new_height   = 0;
    std::chrono::steady_clock::time_point resize_requested_time;
    std::chrono::steady_clock::time_point last_swapchain_recreate_time;

    // Raw window handle (not owned). Set by app_step after window creation.
    // Used by automation resize_window to call the appropriate resize API.
    void* glfw_window = nullptr;

    // Cached minimum window size — avoids redundant platform calls each frame.
    float applied_min_window_w_ = 0.0f;
    float applied_min_window_h_ = 0.0f;
#endif

    // Runtime copy of last export directory. Synced to WorkspaceData on save/load.
    std::string last_export_dir;

    // Non-copyable, non-movable (contains unique_ptrs and non-movable types)
    WindowUIContext()                                  = default;
    ~WindowUIContext()                                 = default;
    WindowUIContext(const WindowUIContext&)            = delete;
    WindowUIContext& operator=(const WindowUIContext&) = delete;
    WindowUIContext(WindowUIContext&&)                 = delete;
    WindowUIContext& operator=(WindowUIContext&&)      = delete;
};

}   // namespace spectra
