# Spectra Qt 6 Application Migration Plan

**Status:** Proposed  
**Scope:** Production desktop frontend migration  
**Repository baseline:** `main` at `d6fd85633a941440938cff3e44f5c32dc2fed8cc`  
**Primary goal:** Make Qt 6 the production cross-platform desktop platform for Spectra, including multiple native OS windows, detachable/dockable panels, native Wayland operation, menus, shortcuts, dialogs, accessibility, and high-DPI behavior, while retaining Spectra's Vulkan renderer and framework-neutral core.

---

## 1. Executive decision

Implement the desktop application using:

- **Qt 6.8+ Widgets** for the application shell and native desktop integration.
- **Direct Vulkan rendering into `QWindow` canvases**, embedded where needed with `QWidget::createWindowContainer()`.
- **Spectra-owned Vulkan instance/device/renderer**, not `QVulkanWindow`, Qt RHI, or a Qt-owned render graph.
- **Qt-owned event loop and native window lifecycle**.
- **A docking-provider interface**:
  - preferred advanced provider: **KDDockWidgets**, after an explicit licensing decision;
  - mandatory dependency-free fallback: `QMainWindow` + `QDockWidget` + Spectra's own figure-tab/document host;
  - Qt Advanced Docking System may remain an optional X11/Windows-oriented experiment, but must not be the native-Wayland default.
- **A staged dual-frontend transition**. The existing GLFW/SDL3 + ImGui application remains buildable until the Qt frontend reaches parity and passes release gates.

This is **not** a rendering rewrite. The migration should preserve:

- `spectra-core`;
- the Vulkan backend and renderer;
- `Figure`, `Axes`, `Series`, `FigureRegistry`, view-models, transforms, animation, export, IPC, plugins, ROS2/PX4 adapters, Python bridge, automation, and headless operation;
- the current public C++ plotting API;
- the in-process and daemon/window-agent runtime modes.

The migration replaces or retires:

- GLFW/SDL3 as the production desktop shell;
- the custom `WindowManager` as the owner of native desktop windows;
- ImGui as the primary application chrome, docking, panels, menus, dialogs, and settings UI;
- custom cross-window drag logic based on global screen coordinates;
- duplicated platform branches inside `App::init_runtime()`.

---

## 2. Why this migration is justified

Spectra already supports multiple Vulkan swapchains and stable figure ownership, but the current frontend implements desktop behavior itself. That creates substantial platform-specific code for:

- native window creation and destruction;
- tab tear-off and cross-window dragging;
- global mouse and window coordinates;
- custom floating panels and preview windows;
- focus and z-order tracking;
- DPI, monitor movement, resize, and surface lifecycle;
- menus, shortcuts, dialogs, clipboard, drag-and-drop, and accessibility.

Qt already provides the platform abstraction needed for Windows, macOS, X11, and native Wayland. The renderer should consume Qt-created surfaces while remaining independent of Qt above the adapter boundary.

The repository is well-positioned for an incremental migration because it already contains:

1. a real framework-free `spectra-core` library;
2. per-window Vulkan resources in `WindowContext`;
3. an adapter-neutral `platform::SurfaceHost`;
4. a working Qt Vulkan surface implementation;
5. a multi-`QWindow` `QtRuntime`;
6. a substantial Qt Vulkan embed/multi-canvas demonstration;
7. stable `FigureId` ownership and a broad test suite.

---

## 3. Current architecture assessment

### 3.1 Build graph

Current root options include:

```cmake
SPECTRA_USE_GLFW
SPECTRA_USE_SDL3
SPECTRA_USE_IMGUI
SPECTRA_USE_QT
SPECTRA_BUILD_QT_EXAMPLE
```

Current target shape:

```text
spectra-core
  └── framework-free data model, math, animation, I/O

spectra
  ├── Vulkan renderer
  ├── platform sources
  ├── IPC and daemon support
  ├── application runtime
  └── ImGui UI and GLFW/SDL3 adapters

spectra_qt_adapter
  ├── QtSurfaceHost
  └── QtRuntime
```

Problems:

- `spectra` is still a large mixed target containing renderer, application runtime, platform code, automation, and UI.
- `spectra_qt_adapter` links the full `spectra` target instead of a clean renderer/application-services layer.
- the production `spectra-app` target is only created when GLFW or SDL3 is enabled;
- ImGui is only configured when GLFW or SDL3 is enabled;
- many framework-neutral UI services are listed under the ImGui source block;
- the root CMake file is overburdened and makes frontend ownership difficult to reason about.

### 3.2 Application lifecycle

`src/app/main.cpp` currently performs:

- CLI parsing;
- native-dialog automation policy;
- Unix daemon discovery;
- optional re-exec into `spectra-window`;
- `spectra::App` construction and blocking `App::run()`.

`App` already provides:

- in-process and multiprocess dispatch;
- `init_runtime()`, `step()`, and `shutdown_runtime()`;
- headless support;
- stable figure registry access;
- graceful signal-driven shutdown.

The principal lifecycle problem is `src/ui/app/app_step.cpp`. It currently combines:

- backend and renderer construction;
- settings and plugin service setup;
- GLFW/SDL3 initialization;
- window-manager creation;
- UI-context construction;
- ImGui callback wiring;
- drag-and-drop;
- automation/MCP server startup;
- frame scheduling;
- capture/export state.

It also contains nearly duplicated GLFW and SDL3 startup branches. This file must be decomposed before Qt becomes the default application frontend.

### 3.3 Vulkan and native surfaces

The current per-window design is fundamentally correct:

```text
Shared:
  VkInstance
  VkPhysicalDevice
  VkDevice
  queues
  command pool strategy
  pipelines
  descriptor infrastructure
  uploaded series resources
  Renderer

Per canvas/native render window:
  WindowContext
  VkSurfaceKHR
  swapchain
  image views/framebuffers/depth/MSAA
  command buffers
  fences/semaphores
  frame UBO
  resize and surface-generation state
```

`platform::SurfaceHost` already isolates:

- required Vulkan instance extensions;
- surface creation;
- framebuffer size;
- presentation support;
- surface lifecycle/destruction policy.

`QtSurfaceHost` already uses `QWindow`, `QVulkanInstance`, and physical-pixel sizing through the window device-pixel ratio.

The remaining renderer/platform debt is:

- the legacy `WindowContext::glfw_window`;
- ImGui context ownership inside renderer-facing `WindowContext`;
- mutable global `VulkanBackend::set_active_window()` behavior;
- frontend-specific flags such as preview/panel/title-bar drag state inside `WindowContext`;
- incomplete surface-generation handling during Qt platform-surface destruction and recreation.

### 3.4 Existing Qt implementation

The current Qt work is not placeholder-only.

`QtRuntime` already supports:

- one shared Vulkan backend and renderer;
- multiple attached `QWindow` objects;
- a distinct `WindowContext` per `QWindow`;
- per-window begin/render/end APIs;
- resize debounce;
- detach and reattach;
- physical-pixel framebuffer sizing;
- Qt platform-surface ownership.

`examples/qt_embed_demo.cpp` already demonstrates:

- a `QWindow` with `QSurface::VulkanSurface`;
- `QPlatformSurfaceEvent` handling;
- expose/minimize/restore behavior;
- device-pixel-ratio tracking;
- Qt input forwarding;
- multiple Vulkan canvases sharing one runtime;
- detach/reattach behavior.

The correct next step is to **promote and refactor this implementation into production components**, not create a second Qt renderer path.

### 3.5 UI and application services

Useful framework-neutral components already exist:

- `FigureRegistry`;
- `FigureManager` and figure view-models;
- `CommandRegistry`;
- `ShortcutManager`;
- `UndoManager`;
- settings and workspace models;
- animation controllers;
- transforms and export registries;
- data-source and custom-series registries;
- plugin manager;
- automation command handlers.

However, several of these are currently:

- compiled only under `SPECTRA_USE_IMGUI`;
- owned by `WindowUIContext`;
- wired directly to `ImGuiIntegration`;
- represented by immediate-mode `draw()` callbacks.

The migration must extract models/controllers from rendering widgets. Qt widgets should observe and invoke the same underlying services rather than duplicating business logic.

### 3.6 Custom native-window stack to retire

`WindowManager` currently owns:

- GLFW/SDL3 windows;
- lifecycle callbacks and event polling;
- figure detach/move;
- detached panel windows;
- tear-off preview windows;
- cross-window drop-zone detection;
- global cursor queries;
- global window positioning;
- z-order approximation;
- custom title-bar dragging;
- per-window ImGui contexts and UI bundles.

Do not mechanically port this class to Qt.

Retain only the reusable concepts:

- stable `WindowId`/`CanvasId`;
- figure-to-window assignment;
- close-request deferral where required by the renderer;
- shared versus per-window Vulkan resource rules;
- semantic events for figure detach, attach, move, focus, and close.

Qt and the selected docking provider should own native drag, docking, focus, activation, top-level windows, and window-manager interaction.

### 3.7 Workspace and plugins

`WorkspaceData` is currently format version 4 and contains:

- figures and axes;
- panel visibility;
- custom `dock_state`;
- shortcut overrides;
- plugins;
- transforms;
- timeline and theme state.

The Qt migration requires workspace format version 5 with separate fields for:

- framework-neutral document/figure state;
- Qt main-window geometry;
- Qt toolbar/dock state or docking-provider layout;
- multiple main-window topology;
- floating-group topology;
- per-window active figure;
- per-screen restoration hints.

The stable C plugin ABI must remain compatible. Direct plugin UI callbacks are frontend-specific and require a compatibility strategy rather than an immediate ABI break.

---

## 4. Architecture principles

1. **Qt is the desktop platform, not the renderer.**
2. **No Qt types in public core/render headers.**
3. **No GLFW, SDL3, or ImGui types below frontend adapter targets.**
4. **One Vulkan device and renderer per process by default.**
5. **One render context per Vulkan canvas.**
6. **Figures and application services outlive individual windows.**
7. **Native windows are created and destroyed only by Qt/docking code.**
8. **Rendering happens only while a valid Qt platform surface generation exists.**
9. **No production feature may depend on global desktop coordinates.**
10. **Semantic commands are the single source of truth for menus, shortcuts, command palette, automation, and plugins.**
11. **The legacy frontend remains buildable until Qt parity is demonstrated.**
12. **Headless, library, Python, and daemon use cases must not acquire a Qt dependency.**
13. **UI migration is model-first: extract state and operations before replacing views.**
14. **Wayland is a supported native target, not an XWayland fallback mode.**

---

## 5. Target dependency graph

```text
                         ┌────────────────────────────┐
                         │        spectra-core        │
                         │ figures/data/math/anim/I/O │
                         └─────────────┬──────────────┘
                                       │
                  ┌────────────────────┴────────────────────┐
                  │                                         │
      ┌───────────▼────────────┐               ┌────────────▼────────────┐
      │ spectra-render-vulkan  │               │ spectra-app-services    │
      │ backend/renderer/GPU   │               │ session/commands/undo   │
      │ no Qt/GLFW/ImGui       │               │ workspace/plugins/IPC   │
      └───────────┬────────────┘               └────────────┬────────────┘
                  │                                         │
                  └────────────────────┬────────────────────┘
                                       │
                         ┌─────────────▼──────────────┐
                         │    spectra-qt-platform     │
                         │ QVulkanInstance/QWindow    │
                         │ input/DPI/surface lifecycle│
                         └─────────────┬──────────────┘
                                       │
                         ┌─────────────▼──────────────┐
                         │    spectra-qt-widgets      │
                         │ shell/panels/actions/docks │
                         └─────────────┬──────────────┘
                                       │
                         ┌─────────────▼──────────────┐
                         │        spectra-app         │
                         │ QApplication + bootstrap   │
                         └────────────────────────────┘

Optional compatibility frontend:

spectra-legacy-frontend
  └── GLFW or SDL3 + ImGui, linked to the same app services and renderer
```

Recommended target names may be adjusted during implementation, but the dependency direction is mandatory.

---

## 6. Ownership model

### 6.1 Process-scoped ownership

Create a framework-neutral owner, tentatively:

```cpp
class ApplicationServices {
public:
    FigureRegistry& figures();
    SessionRuntime& session();
    CommandRegistry& commands();
    ShortcutManager& shortcuts();
    UndoManager& undo();
    PluginManager& plugins();
    SettingsStore& settings();
    WorkspaceController& workspace();
    AutomationController& automation();
    Renderer& renderer();
    Backend& backend();
};
```

It owns or coordinates:

- renderer/backend;
- figure registry;
- session runtime;
- commands, shortcuts, and undo;
- plugin/data-source/export/custom-series registries;
- workspace/settings;
- automation and MCP services;
- in-process topic server;
- frame scheduler and animator.

It must not include Qt, GLFW, SDL, or ImGui headers.

### 6.2 Qt application ownership

```text
QApplication
  └── QtApplicationController
       ├── ApplicationServices
       ├── QtRenderRuntime
       ├── MainWindowRegistry
       ├── DockingHostFactory
       ├── QtActionBridge
       ├── QtWorkspaceBridge
       └── QtShutdownCoordinator
```

Qt owns:

- `QApplication`;
- `QMainWindow` and floating top-level windows;
- `QWindow` canvases;
- native menus, actions, shortcuts, dialogs, clipboard, drag/drop;
- platform-surface and screen events;
- native focus/activation;
- docking-provider objects.

### 6.3 Canvas ownership

Each visible plot canvas should have:

```text
FigureCanvasWidget
  └── QWidget::createWindowContainer(...)
       └── SpectraVulkanWindow : QWindow
            ├── stable CanvasId
            ├── FigureId or pane/document model
            ├── InputRouter
            └── surface-generation token

QtRenderRuntime
  └── CanvasId -> RenderWindowState
       └── WindowContext
```

Avoid using `QWindow*` as the long-term application identity. Use stable IDs and treat pointers as adapter-local handles.

---

## 7. Docking decision and licensing gate

### 7.1 Required abstraction

Introduce:

```cpp
class DockingHost {
public:
    virtual ~DockingHost() = default;
    virtual PanelHandle add_panel(const PanelDescriptor&) = 0;
    virtual DocumentHandle add_document(const DocumentDescriptor&) = 0;
    virtual void detach_document(DocumentId) = 0;
    virtual void move_document(DocumentId, HostId) = 0;
    virtual DockLayoutState save_layout() const = 0;
    virtual bool restore_layout(const DockLayoutState&) = 0;
};
```

The application must not directly depend on `QDockWidget` or KDDockWidgets APIs outside provider implementations.

### 7.2 Provider A: KDDockWidgets

Preferred for advanced IDE-style behavior:

- grouped floating windows;
- dock widgets inside floating windows;
- redocking groups;
- multiple main windows;
- stronger layout model and customization;
- explicit support for Qt Widgets and native Wayland.

**Mandatory gate:** KDDockWidgets is GPL-2.0/GPL-3.0 or commercially licensed. Spectra is MIT. Before making it a required dependency, choose one:

1. accept GPL distribution requirements for the application frontend;
2. obtain a commercial license;
3. keep it optional and ship the native Qt provider by default.

The repository must document the selected policy in an ADR and packaging metadata.

### 7.3 Provider B: native Qt

Mandatory fallback with no additional docking dependency:

- `QMainWindow`;
- `QDockWidget` for tool panels;
- custom central figure/document tab host;
- detached figures become another `QMainWindow` or top-level document window;
- `QMainWindow::saveState()` and `saveGeometry()` for layout persistence.

This provider may have less sophisticated grouped floating behavior, but it provides a reliable baseline and protects the project from licensing or dependency blockers.

### 7.4 Provider C: Qt Advanced Docking System

Optional only. Do not make it the native-Wayland baseline until its Wayland limitations are independently validated against Spectra's acceptance matrix.

---

## 8. Proposed source layout

```text
src/
  app/
    application_services.hpp/.cpp
    application_bootstrap.hpp/.cpp
    session_controller.hpp/.cpp
    shutdown_coordinator.hpp/.cpp

  ui/
    model/
      panel_descriptor.hpp
      document_model.hpp/.cpp
      main_window_model.hpp/.cpp
      selection_model.hpp/.cpp
      application_state.hpp/.cpp
    controllers/
      figure_controller.hpp/.cpp
      workspace_controller.hpp/.cpp
      panel_controller.hpp/.cpp
      command_controller.hpp/.cpp

  render/
    vulkan/
      render_window_state.hpp
      window_context.hpp
      vk_backend.*
      renderer.*

  adapters/
    qt/
      qt_application.hpp/.cpp
      qt_main_window.hpp/.cpp
      qt_main_window_registry.hpp/.cpp
      qt_render_runtime.hpp/.cpp
      qt_surface_host.hpp/.cpp
      spectra_vulkan_window.hpp/.cpp
      figure_canvas_widget.hpp/.cpp
      qt_render_scheduler.hpp/.cpp
      qt_input_router.hpp/.cpp
      qt_action_bridge.hpp/.cpp
      qt_dialog_service.hpp/.cpp
      qt_clipboard_service.hpp/.cpp
      qt_workspace_bridge.hpp/.cpp
      qt_theme_bridge.hpp/.cpp

      docking/
        docking_host.hpp
        native_qt_docking_host.hpp/.cpp
        kddockwidgets_host.hpp/.cpp

      panels/
        inspector_widget.hpp/.cpp
        topics_widget.hpp/.cpp
        settings_widget.hpp/.cpp
        timeline_widget.hpp/.cpp
        data_sources_widget.hpp/.cpp
        command_palette_dialog.hpp/.cpp
        welcome_widget.hpp/.cpp

  legacy/
    imgui/
    glfw/
    sdl3/
```

Do not move all files at once. Create clean targets and migrate ownership before large directory moves.

---

## 9. CMake plan

### 9.1 New options

```cmake
option(SPECTRA_BUILD_QT_APP "Build the production Qt 6 desktop app" ON)
option(SPECTRA_BUILD_LEGACY_APP "Build the legacy GLFW/SDL3 + ImGui app" ON)
option(SPECTRA_BUILD_QT_TESTS "Build Qt GUI and integration tests" ON)

set(SPECTRA_QT_DOCKING_PROVIDER "native"
    CACHE STRING "Qt docking provider: native | kddockwidgets | qtads")
set_property(CACHE SPECTRA_QT_DOCKING_PROVIDER
             PROPERTY STRINGS native kddockwidgets qtads)
```

During migration, keep the default Qt app option `OFF` for one or more phases if required to protect release packaging. Switch it to `ON` only after the cutover gate.

### 9.2 Qt components

Initial production target:

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Test
)
```

Add `Svg` only when SVG icons are actually used at runtime. Avoid unnecessary modules.

Enable `AUTOMOC`, `AUTOUIC`, and `AUTORCC` only on Qt targets.

### 9.3 Target separation

Required end state:

- `spectra-core`: unchanged framework-free target;
- `spectra-render-vulkan`: Vulkan renderer and surface-host interface;
- `spectra-app-services`: application/session/workspace/plugin/automation services;
- `spectra-qt-platform`: Qt GUI and Vulkan surface/canvas integration;
- `spectra-qt-widgets`: shell, panels, docking, actions;
- `spectra-app`: Qt executable;
- `spectra-legacy-app`: optional compatibility executable.

The installed C++ library package must not require Qt unless consumers explicitly request the Qt adapter component.

### 9.4 Dependency acquisition

For KDDockWidgets:

- prefer `find_package(KDDockWidgets CONFIG)` for system/vcpkg/package-manager builds;
- optionally provide a pinned `FetchContent` path;
- never follow an unpinned branch;
- expose an offline build path;
- record license artifacts in packages;
- make the dependency optional until the licensing gate is closed.

---

## 10. Event loop and frame scheduling

Qt owns the event loop. Remove the concept that the production frontend must run a blocking manual event-poll loop.

### 10.1 Scheduling rules

Use event-driven rendering:

- `QWindow::requestUpdate()` for redraw requests;
- queued Qt signals for thread-to-GUI notifications;
- a precise timer only while animation or high-rate streaming requires continuous frames;
- stop continuous timers while idle, hidden, minimized, or fully occluded where detectable;
- coalesce multiple data and resize notifications into one frame;
- preserve `FrameScheduler` as the policy engine.

Suggested flow:

```text
data/input/animation/workspace event
            │
            ▼
QtRenderScheduler::request_frame(reason)
            │
            ├── coalesce duplicate requests
            ├── select affected canvases
            └── QWindow::requestUpdate()
                         │
                         ▼
SpectraVulkanWindow::event(UpdateRequest)
                         │
                         ▼
QtRenderRuntime::render(canvas_id)
```

### 10.2 Thread policy

Initial implementation:

- all Qt objects remain on the GUI thread;
- Vulkan recording/submission remains on the GUI thread to minimize migration risk;
- data ingestion and existing background services continue on worker threads;
- worker threads publish immutable or synchronized model changes and queued redraw requests.

A dedicated render thread is a later optimization and is explicitly out of scope until the Qt frontend is stable and profiled.

---

## 11. Vulkan lifecycle requirements

### 11.1 QVulkan ownership

Continue using the existing strategy:

- Spectra creates the `VkInstance`;
- the instance is adopted by `QVulkanInstance`;
- each canvas is a `QWindow` with `QSurface::VulkanSurface`;
- Qt creates/owns the platform `VkSurfaceKHR` returned for the `QWindow`;
- Spectra owns swapchain and all device-level resources.

Do not introduce `QVulkanWindow`; it would duplicate or override Spectra's renderer/device/swapchain lifecycle.

### 11.2 Surface generations

Add a monotonically increasing surface generation to each canvas.

On `QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed`:

1. stop scheduling new frames for that canvas;
2. mark generation invalid;
3. wait only for that canvas's relevant fences;
4. destroy swapchain-dependent resources;
5. release the Qt-associated surface reference according to `QtSurfaceHost` ownership rules;
6. retain the `FigureId` and UI document state.

On `SurfaceCreated` or first valid exposure:

1. increment generation;
2. create/retrieve the surface;
3. verify queue-family presentation support;
4. create swapchain resources from physical pixel size;
5. request a full redraw.

Every asynchronous frame request must carry or validate the generation to prevent rendering to a destroyed surface.

### 11.3 Explicit render target

Phase out global mutable active-window state.

Transitional API:

```cpp
RenderWindowScope scope = backend.activate(window_context);
renderer.render(scope, figure, frame_state);
```

Target API:

```cpp
backend.begin_frame(window_context);
renderer.render_figure(window_context, figure, frame_state);
backend.end_frame(window_context);
```

No renderer call should silently depend on a previously selected global window after the migration is complete.

### 11.4 Resize and DPR

- logical Qt size is not swapchain size;
- always calculate physical extent from current `QWindow::devicePixelRatio()`;
- respond to resize, screen change, and device-pixel-ratio change;
- coalesce interactive resize events;
- handle zero-size/minimized windows without swapchain recreation loops;
- recreate immediately for Vulkan `OUT_OF_DATE`;
- recover cleanly from `SURFACE_LOST`;
- do not call `vkDeviceWaitIdle()` for routine per-window resize.

---

## 12. Input and interaction migration

### 12.1 Framework-neutral input vocabulary

Create explicit Spectra input types independent of GLFW numeric values:

```cpp
enum class PointerButton;
enum class Key;
enum class Modifier : uint32_t;
enum class PointerEventType;
enum class KeyEventType;
```

Events should contain:

- logical position;
- physical-pixel position;
- local canvas position;
- pointer type;
- pressure/tilt where available;
- modifiers;
- timestamp;
- device-pixel ratio;
- canvas ID.

### 12.2 Qt input router

`QtInputRouter` maps:

- mouse move/press/release/double-click;
- wheel pixel and angle deltas;
- key press/release;
- text/IME events;
- tablet/stylus events;
- touch/gesture events;
- drag-and-drop;
- focus enter/leave.

Preserve current pan, zoom, selection, measurement, annotations, 3D orbit, and keyboard navigation by routing into existing controllers.

Do not use global cursor position for docking. Qt/docking-provider operations must use native framework drag APIs.

### 12.3 Shortcuts

`CommandRegistry` remains the source of truth.

`QtActionBridge` creates and updates `QAction` objects from command descriptors:

- label;
- category/menu path;
- icon;
- enabled/checked state;
- shortcut;
- application/window/widget shortcut context.

The same command ID remains callable from:

- menus/toolbars;
- command palette;
- plugin API;
- automation/MCP;
- keyboard shortcuts;
- tests.

---

## 13. Qt shell design

### 13.1 Main window

`SpectraMainWindow` should contain:

- native menu bar;
- configurable toolbars;
- central document/figure area;
- dockable panels;
- status bar;
- command palette action;
- welcome page when no figures are open.

No special "primary renderer window" semantics should leak into models. A main window is a host for documents and panels.

### 13.2 Figure documents

Each figure is a document identified by `FigureId`.

Required operations:

- create;
- close;
- duplicate;
- move to another main window;
- detach into a new main window;
- tab with other figures;
- split into multiple canvas panes;
- restore from workspace;
- focus/activate without changing ownership.

The document model must distinguish:

- figure identity;
- view instance identity;
- native main-window identity;
- canvas identity.

This prevents future limitations such as showing the same figure in two synchronized views.

### 13.3 Panels

Convert panels incrementally:

1. Inspector;
2. Topics/data sources;
3. Settings and shortcuts;
4. Timeline/animation;
5. Series/data editor;
6. export/recording;
7. plugin diagnostics;
8. ROS2/PX4-specific tools.

Each panel should use a controller/view-model and avoid direct renderer mutation from widget paint code.

### 13.4 Themes and icons

- map Spectra theme tokens to a Qt palette and style sheet;
- preserve plot/data colors in `ThemeManager`;
- use `QIcon` resources with high-DPI variants;
- avoid hardcoded pixel sizes;
- test system light/dark theme changes;
- retain a Spectra-specific theme override.

---

## 14. Workspace version 5

Add a versioned Qt layout section without corrupting old workspaces.

Suggested structure:

```cpp
struct DesktopLayoutState {
    std::string provider;
    std::string provider_version;
    std::string main_window_state_base64;
    std::string main_window_geometry_base64;
    std::string provider_layout;
    std::vector<WindowState> windows;
};
```

Migration rules:

- v4 files load figure, axis, series, interaction, shortcut, plugin, and theme state;
- legacy ImGui `dock_state` is ignored or converted only where deterministic;
- a sensible default Qt layout is generated;
- v5 files retain a legacy-compatible subset where possible;
- invalid or unavailable monitor geometry is clamped to an available screen;
- native Wayland restores topology and sizes but does not promise exact absolute floating positions;
- layout-provider changes fall back to a default layout while preserving figures and panels.

Add explicit round-trip and corruption tests.

---

## 15. Plugin compatibility strategy

### 15.1 Preserve stable ABI

Do not break plugin API v2.0 merely to migrate the frontend.

Keep:

- command registration;
- transforms;
- overlays;
- exporters;
- data sources;
- custom series;
- backend handle contracts where already supported.

### 15.2 UI callbacks

`SpectraDataSourceBuildUIFn` and any immediate-mode extension points are frontend-specific.

Migration policy:

1. retain the callback in the legacy frontend;
2. in Qt, show a compatibility notice or a generic controls panel when no portable schema exists;
3. introduce a new optional, versioned UI-description API:
   - properties;
   - actions;
   - enum choices;
   - validation;
   - status text;
   - model updates;
4. render that schema in both Qt and legacy frontends;
5. deprecate direct immediate-mode UI callbacks in a future major plugin API version.

Overlay drawing helpers should remain renderer-neutral and continue to avoid requiring plugins to link Qt or ImGui.

---

## 16. Automation, MCP, and testability

The Qt migration must not regress automated control.

### 16.1 Semantic automation first

Prefer operations such as:

```text
execute_command("file.open")
create_figure(...)
move_figure(...)
set_panel_visible(...)
set_axis_limits(...)
capture_canvas(...)
```

over raw pixel clicks.

### 16.2 Qt-specific test seam

Introduce object names and stable test IDs for:

- windows;
- menus and actions;
- panels;
- figure tabs;
- canvas containers;
- dialogs;
- status indicators.

Create a `QtAutomationAdapter` that maps current automation requests to Qt objects while preserving the existing external protocol.

### 16.3 Native dialogs

Continue honoring:

- `--no-native-dialogs`;
- `SPECTRA_NO_NATIVE_DIALOGS`;
- `SPECTRA_AUTOMATION`.

Use injectable dialog services so tests can supply deterministic file paths without opening modal OS dialogs.

---

## 17. Multiprocess, Python, ROS2, and PX4 behavior

### 17.1 In-process application

Qt app hosts:

- application services;
- Qt frontend;
- renderer;
- in-process topic server.

### 17.2 Multiprocess window agent

Replace the GLFW/ImGui window agent frontend with the same Qt application shell configured as an IPC client.

Preserve:

- daemon discovery;
- `SPECTRA_SOCKET`;
- `spectra-window --socket ...`;
- versioned IPC;
- publisher-first attach behavior.

Do not create separate UI implementations for `spectra`, `spectra-window`, `spectra-ros`, and `spectra-px4`. Use shell composition and adapter-provided panel factories.

### 17.3 Python

Python remains independent of Qt:

- plotting/model calls use the existing IPC or in-process API;
- launching the desktop frontend starts the Qt executable;
- headless export remains Qt-free;
- wheel packaging should not unintentionally bundle Qt unless a deliberate optional extra is introduced.

### 17.4 ROS2 and PX4

Move adapter UI code behind frontend-neutral panel/controller interfaces.

The adapters should contribute:

- commands;
- models;
- data sources;
- panel descriptors/factories;
- status providers.

Qt widgets render those models. Core adapter libraries must not include Qt headers unless split into an explicit `*_qt_ui` target.

---

## 18. Migration phases

## Phase 0 — Decisions, baselines, and guardrails

Deliverables:

- ADR: Qt Widgets + direct `QWindow` Vulkan;
- ADR: docking provider and licensing;
- ADR: Qt minimum version and packaging policy;
- baseline performance and startup measurements;
- validated legacy build/test baseline;
- Qt build-only CI job;
- inventory of GLFW/SDL/ImGui type leakage.

Acceptance:

- all architectural decisions documented;
- no code behavior change;
- branch can build legacy and Qt demo configurations;
- licensing decision has a named owner and deadline.

## Phase 1 — Extract application services

Refactor without replacing the visible frontend.

Work:

- create `ApplicationServices`;
- move settings, commands, shortcuts, undo, plugin registries, workspace, and automation ownership out of `WindowUIContext`;
- split `app_step.cpp` into lifecycle/services/frontend pieces;
- move framework-neutral source files out of the ImGui-only CMake block;
- create frontend interfaces for dialogs, clipboard, redraw requests, and windows;
- preserve `App::init_runtime()/step()/shutdown_runtime()`.

Acceptance:

- legacy app behavior unchanged;
- headless tests remain Qt-free;
- `ApplicationServices` compiles without frontend headers;
- no duplicate GLFW/SDL initialization branches in application services.

## Phase 2 — Production Qt Vulkan canvas

Promote the demo implementation.

Work:

- rename/refactor `QtRuntime` into production `QtRenderRuntime`;
- extract `SpectraVulkanWindow`;
- extract `FigureCanvasWidget`;
- add surface-generation tracking;
- add explicit per-window render-target APIs;
- implement event-driven scheduling;
- add Qt input router;
- remove optional ImGui initialization from Qt render runtime;
- add a minimal `spectra-qt-smoke` executable.

Acceptance:

- two or more Qt canvases render concurrently from one Vulkan device;
- independent resize/input/focus;
- detach/reattach and surface recreation pass validation layers;
- no permanent timer while idle;
- no GLFW/SDL symbols in Qt targets.

## Phase 3 — Native Qt shell and command system

Work:

- create `SpectraMainWindow`;
- create welcome page and central figure tab host;
- bind `CommandRegistry` to `QAction`;
- implement native menus/toolbars/status bar;
- implement Qt dialogs and clipboard services;
- implement command palette;
- implement settings persistence;
- add basic Inspector and Topics panels.

Acceptance:

- create/open/close figures;
- open CSV through injected/native dialog paths;
- menus, shortcuts, command palette, and automation execute identical command IDs;
- main window works with no figure;
- keyboard-only navigation smoke test passes.

## Phase 4 — Multi-window and docking

Work:

- implement `DockingHost`;
- implement native Qt provider first;
- optionally implement KDDockWidgets provider after license gate;
- support document detach into a new native main window;
- support moving documents between main windows;
- support detached and grouped tool panels per provider capabilities;
- remove custom preview-window and global-cursor docking dependencies from Qt path.

Acceptance:

- Windows, macOS, X11, KDE Wayland, and GNOME Wayland smoke tests;
- no XWayland requirement;
- no arbitrary top-level positioning requirement;
- layout topology save/restore;
- repeated detach/redock stress test without Vulkan or lifetime errors.

## Phase 5 — Feature parity

Migrate remaining user-facing features:

- split panes/subplots;
- inspector and series controls;
- timeline and animation curves;
- data editor and transforms;
- annotations, measurement, selection, tooltip, crosshair;
- settings and shortcut editor;
- export preview, image copy, video recording;
- plugin/data-source panels;
- accessibility summaries and sonification controls;
- ROS2/PX4 panels.

Acceptance:

- parity checklist signed off;
- no critical workflow requires the legacy frontend;
- golden images remain renderer-equivalent;
- frontend-specific visual tests added for Qt.

## Phase 6 — Workspace, plugins, and automation

Work:

- add workspace v5;
- add provider-specific layout serialization;
- add v4 migration;
- add Qt automation adapter;
- add portable plugin UI schema;
- preserve plugin ABI compatibility;
- add crash-recovery restore.

Acceptance:

- v4 and v5 workspace test fixtures load;
- multi-window layout round-trip;
- provider mismatch degrades safely;
- automation suite passes against Qt frontend;
- existing binary plugins continue to load where ABI-compatible.

## Phase 7 — Runtime variants and adapters

Work:

- Qt in-process app;
- Qt `spectra-window` agent;
- Qt ROS2 shell;
- Qt PX4 shell;
- preserve daemon discovery and IPC;
- remove frontend duplication across executables.

Acceptance:

- Python publisher-first workflow opens Qt frontend;
- multiprocess reconnect/restart works;
- ROS2 and PX4 builds remain optional;
- headless backend packages do not require a display or Qt.

## Phase 8 — CI, packaging, and release hardening

Work:

- platform CI matrix;
- GUI integration tests;
- package Qt runtime dependencies;
- AppImage, `.deb`, `.rpm`, `.dmg`, and Windows zip/installer validation;
- plugin path and icon/theme resource validation;
- startup/performance regression tests;
- Wayland package smoke tests.

Acceptance:

- release artifacts launch on clean systems;
- no missing Qt platform plugins;
- no accidental RPATH/plugin-path dependence on developer machines;
- validation layers clean under stress;
- startup and idle-resource budgets met.

## Phase 9 — Default switch and legacy retirement

Work:

- make Qt app the `spectra` executable;
- retain legacy executable under an explicit name for one release;
- publish migration notes;
- gather issue telemetry;
- remove custom WindowManager and ImGui shell after the deprecation window;
- keep ImGui only where it remains intentionally useful, such as internal debug tooling.

Acceptance:

- Qt frontend is the release default on all desktop platforms;
- legacy frontend has no unique supported workflow;
- all removal conditions below are met.

---

## 19. PR-sized implementation sequence

### PR 1 — Build graph and application-services extraction

Files:

- root `CMakeLists.txt`;
- new `src/app/application_services.*`;
- split portions of `src/ui/app/app_step.cpp`;
- `WindowUIContext` ownership cleanup;
- CMake source-list cleanup.

No visible UI change.

### PR 2 — Production Qt canvas library

Files:

- refactor `src/adapters/qt/qt_runtime.*`;
- extract `spectra_vulkan_window.*`;
- extract `figure_canvas_widget.*`;
- add Qt render scheduler and input router;
- convert demo to use production classes;
- add Qt surface lifecycle tests.

### PR 3 — Minimal Qt application shell

Files:

- Qt `main`;
- `SpectraMainWindow`;
- welcome page;
- central figure tabs;
- `QtActionBridge`;
- native dialog/clipboard services;
- minimal settings.

### PR 4 — Native docking and multiple main windows

Files:

- `DockingHost`;
- native Qt provider;
- document detach/move;
- window registry;
- layout persistence.

### PR 5 — KDDockWidgets provider, conditional

Only after the license ADR is resolved.

### PR 6+ — Panel migrations

One coherent panel/workflow per PR, including controller extraction and tests.

---

## 20. Testing strategy

### 20.1 Unit tests

Add tests for:

- application-services ownership and shutdown order;
- command-to-QAction mapping;
- key/modifier mapping;
- physical-pixel extent calculation;
- workspace v4-to-v5 migration;
- docking layout model;
- window/document registry;
- surface-generation state machine;
- redraw request coalescing;
- provider fallback.

### 20.2 Qt integration tests

Use Qt Test for:

- action invocation;
- panel visibility;
- tab create/close/move;
- detach and attach;
- native-window close order;
- focus switching;
- dialog-service injection;
- shortcut scopes;
- workspace restore.

### 20.3 Vulkan GUI stress tests

Scenarios:

- two to eight concurrent canvases;
- continuous resize for five minutes;
- minimize/restore loops;
- hide/show;
- close during active animation;
- detach during streaming;
- monitor movement across different DPRs;
- display hot-plug where CI/manual infrastructure permits;
- swapchain `OUT_OF_DATE` and `SURFACE_LOST` recovery;
- application exit with multiple floating windows;
- daemon disconnect/reconnect.

### 20.4 Platform matrix

Minimum:

| Platform | Window system | Compiler |
|---|---|---|
| Ubuntu 22.04/24.04 | X11/XCB | GCC + Clang |
| Ubuntu 24.04 | KDE Wayland | GCC |
| Ubuntu 24.04 | GNOME Wayland | GCC |
| Windows 10/11 | Win32 | MSVC |
| macOS supported runner | Cocoa/Metal Vulkan portability | Apple Clang |

Where full GUI CI is unavailable, retain build tests and run a documented manual release checklist.

### 20.5 Performance budgets

Measure against the legacy frontend:

- cold startup;
- first rendered frame;
- idle CPU;
- idle GPU;
- steady 60 FPS frame time;
- input-to-present latency;
- memory per additional canvas;
- detach/redock latency;
- workspace load time.

A Qt frontend regression greater than 10% in steady rendering requires investigation. Shell startup regressions may be accepted only with an explicit rationale.

---

## 21. Packaging and deployment risks

Qt deployment must include the correct:

- platform plugin;
- image/icon plugins actually used;
- Wayland/XCB dependencies on Linux;
- Vulkan loader and portability requirements;
- translations if added;
- styles/themes;
- KDDockWidgets library/license when enabled.

Use platform deployment tooling where appropriate, but validate produced artifacts on clean machines or containers/VMs.

Do not rely on:

- developer `QT_PLUGIN_PATH`;
- source-tree assets;
- absolute RPATHs;
- system Qt versions newer than the declared minimum;
- XWayland for native Linux operation.

---

## 22. Major risks and mitigations

| Risk | Mitigation |
|---|---|
| Big-bang rewrite stalls feature work | Dual frontend and PR-sized vertical slices |
| Renderer duplicated between `App` and `QtRuntime` | Single `ApplicationServices` owner and one backend/renderer |
| Qt surface destruction races GPU work | Surface generations and per-canvas fence shutdown |
| Global active-window state causes cross-window bugs | Explicit `WindowContext` render parameters |
| KDDockWidgets license conflicts with MIT distribution | Mandatory ADR, optional provider, native fallback |
| Wayland breaks custom global-coordinate drag logic | Do not port it; use Qt/provider-native user-driven docking |
| Workspace layouts become incompatible | Version 5 with provider ID and safe v4 migration |
| Plugins contain ImGui-only UI callbacks | Compatibility path plus portable UI schema |
| Automation depends on pixels and ImGui internals | Semantic commands and Qt object/test IDs |
| Qt leaks into headless/Python packages | Separate targets and dependency tests |
| Idle timer wastes resources | Event-driven scheduler with active-animation timer only |
| ROS2/PX4 UI forks the shell | Adapter-contributed models, commands, and panel factories |
| Mixed-DPI windows render incorrectly | Per-window DPR and physical extent tests |
| Native dialogs block tests | Injectable dialog service and existing no-dialog policy |

---

## 23. Removal criteria for legacy frontend

Do not delete the legacy GLFW/SDL3 + ImGui path until all are true:

- Qt app ships successfully on Windows, macOS, X11, and native Wayland;
- all primary workflows have parity;
- multi-window detach/redock is stable;
- workspace migration is released;
- automation passes;
- Python, ROS2, PX4, and multiprocess flows pass;
- packaging is validated;
- no critical unresolved Qt frontend issue remains for one release cycle;
- performance budgets are accepted;
- plugin compatibility policy is published.

---

## 24. Definition of done

The migration is complete when:

1. `spectra` launches the Qt 6 desktop application.
2. Multiple native main windows and render canvases work on all supported platforms.
3. Detachable documents and tool panels use Qt/provider-native behavior.
4. Native Wayland is first-class.
5. Vulkan remains owned by Spectra and uses one shared device by default.
6. Core, renderer, headless, IPC, and Python use cases do not require Qt.
7. Application commands, undo, shortcuts, plugins, and automation use shared semantic services.
8. Workspaces preserve figures and Qt desktop layout through a versioned format.
9. ROS2 and PX4 compose into the same Qt shell.
10. CI, packages, validation-layer tests, and performance gates pass.
11. The custom desktop `WindowManager` and ImGui application shell can be removed without losing a supported feature.

---

## 25. Immediate next actions

1. Approve the architecture and decide the docking license/provider policy.
2. Create the three ADRs from Phase 0.
3. Implement PR 1: application-services extraction and target separation.
4. Convert `qt_embed_demo` production classes into PR 2 without changing renderer output.
5. Add Qt build and surface-lifecycle tests before beginning shell migration.
6. Keep feature development model/controller-oriented so new work can serve both frontends during the transition.

---

## 26. Reference documentation

- Qt 6.8 `QVulkanInstance`: <https://doc.qt.io/qt-6.8/qvulkaninstance.html>
- Qt 6.8 `QWindow`: <https://doc.qt.io/qt-6.8/qwindow.html>
- Qt 6.8 Vulkan examples and classes: <https://doc.qt.io/qt-6.8/vulkan.html>
- Qt 6.8 high-DPI behavior: <https://doc.qt.io/qt-6.8/highdpi.html>
- Qt 6.8 `QMainWindow` state persistence: <https://doc.qt.io/qt-6.8/qmainwindow.html>
- KDDockWidgets documentation: <https://kdab.github.io/KDDockWidgets/>
- KDDockWidgets repository and licensing: <https://github.com/KDAB/KDDockWidgets>
- Qt Advanced Docking System: <https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System>
