# Spectra Qt 6 Application Migration Plan

**Status:** Re-baselined after parity audit — Qt infrastructure exists, but the Qt desktop frontend is **not visually or functionally equivalent** to the legacy Spectra frontend and is not eligible to become the default. Phase 2 provides useful canvas infrastructure; Phases 1 and 3-7 are partial implementations, Phase 5 is reopened, and Phase 9 is blocked. `SPECTRA_DEFAULT_FRONTEND` must remain `legacy` in source builds and release jobs until every parity gate in this document passes.
**Scope:** Production desktop frontend migration and cross-platform release architecture  
**Repository baseline:** `main` at `d6fd85633a941440938cff3e44f5c32dc2fed8cc`  
**Primary goal:** Make Qt 6 the production cross-platform desktop platform for Spectra, including multiple native OS windows, detachable/dockable panels, native Wayland operation, menus, shortcuts, dialogs, accessibility, and high-DPI behavior, while retaining Spectra's Vulkan renderer and framework-neutral core.

**Detailed parity evidence:** [QT6_APPLICATION_PARITY_GAP_REPORT.md](QT6_APPLICATION_PARITY_GAP_REPORT.md)
records the 2026-07-22 live legacy-vs-Qt `spectra-mcp` audit, the exhaustive command and visible-control
gaps, visual measurements, source-confirmed defects, and the required closure tests. Treat that report
as a blocking input to every phase completion and default-frontend decision in this plan.

## 0. Parity audit correction (2026-07-21)

The earlier phase checkmarks measured code presence, widget construction, serialization helpers, and
smoke-test success. They did not demonstrate that the Qt application looks or behaves like Spectra.
That distinction is now a blocking release requirement.

The legacy GLFW/ImGui frontend is the behavioral and visual reference until the migration is signed
off. The migration is a platform-shell replacement, not a redesign. A Qt workflow is incomplete when
it merely exposes a similarly named widget or placeholder; it is complete only when the same user
action produces the same model mutation, rendering result, persistence result, and undo/redo behavior.

### 0.1 Confirmed blockers

| Area | Audit result | Required correction |
|---|---|---|
| Shell composition | Live Qt capture does not reproduce the legacy shell; native docks can force an invalid minimum height, default-white panel content is visible, and custom/Qt tab and inspector surfaces are duplicated. | Use one authoritative shell hierarchy, one document-tab surface, one inspector surface, and prove layout at every reference size/DPR. |
| Plot viewport | The Qt canvas is already inside Qt chrome, while the retained ImGui layout can subtract legacy command/status regions again. | Give the renderer the exact physical canvas rectangle and compare the renderer-owned plot region pixel-for-pixel. |
| Commands and menus | `QtApplicationController` initializes `ApplicationServices`, but the standard command set is not registered before `QtActionBridge::rebuild()`; a live launch reports zero actions. | Make command descriptors frontend-neutral and bind the same command IDs, enable predicates, shortcuts, undo transactions, and handlers in both frontends. |
| Tool rail and input | The custom Qt navigation rail changes its selected paint state and status text but does not select the active canvas `InputHandler::ToolMode`. | Route every tool through one active-document controller and test pan, zoom, select, measure, annotate, and ROI end to end. |
| Overlays | `OverlayDrawList` and adapters exist, but the production overlays still call ImGui/`ImDrawList` directly and the Qt painter adapter is not in the canvas frame path. | Port and exercise crosshair, tooltip, legend, markers, selection, measurement, annotation, and ROI; remove the false completion claim. |
| Split panes | `rebuild_splitter()` destroys pane widgets/canvases and `sync_from_split_view()` is intentionally empty. | Preserve/reparent documents and per-pane state across split, unsplit, move, detach, and restore operations. |
| Multi-canvas state | One runtime-level ImGui integration and `DataInteraction` instance is rebound between canvases. | Make overlay/input/view state document- or canvas-scoped; no active window may mutate another window's interaction state. |
| Renderer/services ownership | The controller and `QtRuntime` construct separate Vulkan backend/renderer/theme stacks. | Use one process-scoped backend, renderer, theme, registries, and deferred-cleanup path. |
| Export/shortcuts/workspace | Qt export readback, shortcut rebinding, and full autosave population contain explicit TODO/placeholder paths. | Complete the workflows and verify their artifacts, not just widget creation. |
| ROS2/PX4 UI | Qt display and inspector areas contain placeholder content while the phase is marked implemented. | Port the supported adapter workflows or keep the phase incomplete and the legacy frontend available. |
| Visual tests | The test named `QtVisualRegression` checks dimensions/widget existence and only verifies that a screenshot is non-null. It has no approved image baseline or pixel comparison. | Add deterministic legacy and Qt captures, image-diff thresholds, failure artifacts, and reference-resolution/DPR coverage. |

### 0.2 Initial remediation in this branch (not parity sign-off)

The audit also produced an initial correction pass. These changes remove obvious invalid states, but
they do not close Phase 5 or authorize a default switch:

- the Qt client geometry and shell regions now follow the current 1280x720 legacy reference instead
  of the unrelated 1604x980 redesign mockup; the duplicate in-client title bar, blank icon glyphs,
  fake performance values, injected demo plots, and timed detach/split demo behavior are removed;
- Qt and `ApplicationServices` now share one Vulkan backend, renderer, and theme stack, and the
  renderer receives the Qt-owned canvas extent without subtracting legacy chrome a second time;
- 35 usable Qt actions are registered before `QtActionBridge::rebuild()` (up from zero), including
  figure lifecycle, tab navigation, view reset/fit, tools, panel access, split commands, and quit;
- visible shell controls use those same commands; in particular, the document-tab `+` control now
  executes `figure.new` instead of emitting an unhandled sentinel activation;
- navigation tool selection reaches the active canvas `InputHandler`;
- split, close-split, and reset-split preserve live canvas objects, documents, the active document,
  and per-document tool state rather than recreating or silently dropping them;
- the Qt test runner now owns `QApplication` with a valid lifetime, all nine Qt integration binaries
  exit cleanly, and the shell test rejects large unthemed light surfaces.

Still blocking: approved legacy-vs-Qt image baselines and pixel diffs, the full command descriptor
set, overlays, export/readback, shortcut rebinding, workspace population, complete panels/adapters,
multi-window interaction isolation, and the complete workflow matrix below.

### 0.3 Non-negotiable acceptance gates

1. **No default switch before parity.** Qt may be opt-in, but official launchers and packages remain
   legacy-first until the complete matrix below is green.
2. **Renderer-region equivalence.** At the same physical canvas extent, theme, figure, and font atlas,
   the renderer-owned plot region must have at most 0.1% pixels differing by more than two 8-bit
   channel values. Any larger intentional change requires explicit approval and a baseline review.
3. **Shell fidelity.** Geometry must match the approved Spectra reference within one logical pixel;
   colors and tokens must come from a shared source; no native-white fallback surfaces, duplicate
   chrome, overlap, clipping, or unexpected scroll/minimum-size expansion are allowed.
4. **Reference matrix.** Capture welcome, 2D line/scatter, multi-subplot, 3D, inspector open, every
   overlay tool, split view, detached window, dialogs, and adapter panels at 1280x720, 1600x900, and
   200% DPR on X11 and Wayland. Windows and macOS captures are required before their release gate.
5. **Workflow equivalence.** Every row in the functional matrix must run against both frontends with
   the same fixture and compare figure state, active tool/document, undo stack, saved workspace, and
   exported artifact.
6. **No placeholders count as delivery.** A TODO, disabled action, label-only panel, adapter object, or
   construction test cannot close a migration item.
7. **Legacy stays buildable and tested.** Removal is a separate post-parity decision after at least one
   release cycle with Qt as an opt-in frontend.

### 0.4 Required functional parity matrix

| Workflow | State/output that must match |
|---|---|
| Figure lifecycle | create, rename, activate, close, reopen, multiple tabs, empty/welcome state |
| Data and series | CSV open, live topics, add/remove/reorder/copy/cut/paste series, style edits |
| 2D interaction | pan, wheel zoom, box zoom, reset, fit, axis links, multi-axes hit testing |
| Plot tools | select, ROI, measure, annotate/edit, markers, tooltip, crosshair, legend interaction |
| 3D interaction | orbit, pan, zoom, axis selection, camera reset, scene/display interaction |
| Documents/windows | split/unsplit, tab reorder, detach/redock, cross-window move, focus and close |
| Commands | menus, palette, shortcuts, enabled state, conflicts, rebinding, undo/redo |
| Panels | inspector, topics, transforms, data editor, timeline/curves, settings, plugins, accessibility |
| Export | PNG/SVG/video, clipboard image/text, preview, cancellation, error reporting |
| Persistence | workspace save/load, autosave/crash recovery, multi-window topology, old-version migration |
| Runtime variants | in-process, daemon/window-agent, Python publisher-first, reconnect/restart |
| Adapters | all supported ROS2 and PX4 discovery, plotting, display, inspector, bag/ULog workflows |

---

## 1. Executive decisions

Implement the production desktop application using:

- **Qt 6.8.x Widgets** for the application shell and native desktop integration.
- **Direct Vulkan rendering into `QWindow` canvases**, embedded where needed with `QWidget::createWindowContainer()`.
- **Spectra-owned Vulkan instance, device, renderer, swapchains, and synchronization**, not `QVulkanWindow`, Qt RHI, or a Qt-owned render graph.
- **Qt-owned event loop and native window lifecycle**.
- **A docking-provider interface**:
  - preferred advanced provider: **KDDockWidgets**, only after an explicit licensing decision;
  - mandatory dependency-free fallback: `QMainWindow` + `QDockWidget` + Spectra's own document/figure host;
  - Qt Advanced Docking System remains optional until independently validated against the native-Wayland acceptance matrix.
- **A staged dual-frontend transition**. The existing GLFW/SDL3 + ImGui application remains buildable until the Qt frontend reaches parity and passes all release gates.
- **A controlled Qt runtime for official releases**. Official packages must not depend on whichever Qt minor version happens to be installed by the operating system.

This is **not** a rendering rewrite. Preserve:

- `spectra-core`;
- the Vulkan backend and renderer;
- `Figure`, `Axes`, `Series`, `FigureRegistry`, view-models, transforms, animation, export, IPC, plugins, ROS2/PX4 adapters, Python bridge, automation, and headless operation;
- the current public C++ plotting API;
- the in-process and daemon/window-agent runtime modes.

Replace or retire:

- GLFW/SDL3 as the production desktop shell;
- the custom `WindowManager` as the owner of native desktop windows;
- ImGui as the primary application chrome, docking, panels, menus, dialogs, and settings UI;
- custom cross-window drag logic based on global screen coordinates;
- duplicated platform branches inside `App::init_runtime()`;
- release packages that rely on an uncontrolled system Qt installation.

---

## 2. Cross-platform release and Qt runtime policy

### 2.1 Supported desktop targets

The initial production support matrix is:

| Platform | Architecture | Native window system | Official artifact |
|---|---:|---|---|
| Ubuntu 22.04 LTS | x86-64 | X11/XCB and Wayland | `.deb`, APT repository, AppImage |
| Ubuntu 24.04 LTS | x86-64 | X11/XCB and Wayland | `.deb`, APT repository, AppImage |
| Ubuntu 24.04 LTS | ARM64 | Wayland/X11 | later phase after x86-64 stabilization |
| Windows 10/11 | x86-64 | Win32 | installer and portable ZIP |
| macOS 12+ | Apple Silicon | Cocoa + MoltenVK | signed/notarized DMG |
| macOS 12+ | Intel | Cocoa + MoltenVK | signed/notarized DMG while maintained |

Windows ARM64 is not part of the first release gate. It may be added as a separate target after x86-64 reaches parity.

### 2.2 Why official packages cannot rely on system Qt

Ubuntu 22.04 and Ubuntu 24.04 provide different Qt minor versions, and both are older than the selected Qt 6.8 application baseline. Therefore, the official Spectra package must not assume that `apt install spectra` can safely link against the distribution's default Qt libraries.

Official release policy:

- pin a tested Qt **6.8.x patch version** per Spectra release train;
- build and test Spectra against that exact runtime;
- ship the required Qt runtime libraries and plugins privately with Spectra;
- isolate them from KDE and other system applications;
- allow source builds to use another compatible Qt only when explicitly configured and tested.

This gives Spectra one predictable Qt API/ABI and platform-plugin behavior across supported operating systems.

### 2.3 Ubuntu packaging model

The user-facing installation remains:

```bash
sudo apt update
sudo apt install spectra
```

The user does **not** manually install Qt development packages or run the Qt installer.

Recommended package split:

```text
spectra
  ├── Qt desktop executable
  ├── depends on spectra-qt-runtime (= matching release)
  ├── depends on libspectra runtime components
  └── depends on Vulkan loader and normal OS libraries

spectra-qt-runtime
  ├── private Qt Core/Gui/Widgets libraries
  ├── XCB and Wayland QPA plugins
  ├── only the image/icon plugins Spectra uses
  ├── Qt license notices
  └── no global replacement of the distribution Qt

spectra-backend
  ├── daemon/headless executable
  └── no Qt dependency

libspectra-core
  └── no Qt dependency

libspectra-dev
  ├── public headers and CMake package
  └── Qt required only for the optional Qt adapter component
```

An equivalent single-package layout is acceptable initially, but the private runtime must remain isolated under a Spectra-owned directory, for example:

```text
/usr/bin/spectra
/usr/lib/spectra/
/usr/lib/spectra/qt/lib/
/usr/lib/spectra/qt/plugins/platforms/
/usr/share/spectra/
```

Use relative runtime search paths such as `$ORIGIN`-based RPATH/RUNPATH. Do not modify global `LD_LIBRARY_PATH` and do not install private Qt libraries into the generic `/usr/lib` namespace.

### 2.4 Separate Ubuntu build baselines

Produce separate `.deb` artifacts for Ubuntu 22.04 and Ubuntu 24.04.

Do not build the only Linux artifact on Ubuntu 24.04 and assume it will execute on Ubuntu 22.04. A newer build environment can introduce a newer glibc and other system-symbol requirements.

Required build strategy:

```text
Ubuntu 22.04 package:
  build in a clean Ubuntu 22.04 container/runner
  use the pinned Qt 6.8.x runtime built for that baseline
  test on Ubuntu 22.04 and Ubuntu 24.04 where useful

Ubuntu 24.04 package:
  build in a clean Ubuntu 24.04 container/runner
  use the same pinned Qt patch level, built for 24.04
  test X11, GNOME Wayland, and KDE Wayland
```

The two packages may contain the same Spectra source revision but are independent binary artifacts.

### 2.5 Linux runtime contents

The private runtime should contain only required modules, typically:

```text
Qt6Core
Qt6Gui
Qt6Widgets
Qt6DBus, only if used
Qt6Svg, only if runtime SVG support is used
QPA platform plugins:
  xcb
  wayland
  wayland-egl or the applicable Qt 6.8 plugin set
image format plugins actually used
TLS/network plugins only if required
```

System-level libraries remain APT dependencies where appropriate:

- glibc and libstdc++;
- Vulkan loader;
- XCB/X11 libraries;
- Wayland client libraries;
- xkbcommon;
- fontconfig/freetype;
- OpenGL/EGL libraries required by Qt platform plugins;
- audio libraries only when used.

Packaging tests must verify that CPack or `dpkg-shlibdeps` does not accidentally convert the private Qt runtime into dependencies on the distribution's older Qt packages.

### 2.6 Windows deployment

Build official Windows packages with MSVC 2022.

Bundle:

```text
spectra.exe
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
platforms/qwindows.dll
required image/icon plugins
Spectra libraries and assets
Vulkan loader/runtime requirements not guaranteed by the OS
```

Use Qt's CMake deployment support or `windeployqt` as an input to packaging, then filter and validate the result. Do not copy an entire Qt installation blindly.

Release artifacts:

- signed installer, preferably WiX, Inno Setup, or an equivalent reproducible pipeline;
- portable ZIP containing the same tested runtime;
- optional symbols package for crash diagnostics.

Test on clean Windows 10 and Windows 11 machines without a developer Qt installation or Vulkan SDK.

### 2.7 macOS deployment

macOS has no native Vulkan driver. Spectra must bundle and validate **MoltenVK**, which maps the Vulkan portability subset onto Metal.

Bundle inside `Spectra.app`:

```text
Contents/MacOS/Spectra
Contents/Frameworks/QtCore.framework
Contents/Frameworks/QtGui.framework
Contents/Frameworks/QtWidgets.framework
Contents/Frameworks/MoltenVK.framework or libMoltenVK.dylib
Contents/PlugIns/platforms/libqcocoa.dylib
Contents/Resources/...
```

Vulkan initialization on macOS must:

- enable `VK_KHR_portability_enumeration` when available;
- set `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`;
- accept devices exposing `VK_KHR_portability_subset`;
- query features instead of assuming full desktop Vulkan support;
- validate formats, MSAA, synchronization, shader behavior, and presentation under MoltenVK.

Use `macdeployqt` or Qt CMake deployment support for the Qt frameworks, then explicitly add MoltenVK and Spectra assets. Sign nested libraries correctly, sign the final application, notarize it, staple the notarization ticket, and create the DMG.

Create native ARM64 and x86-64 artifacts initially. A universal binary is optional after both native builds are stable.

### 2.8 AppImage and portable Linux artifacts

The AppImage must bundle the same controlled Qt version and required QPA plugins. It must be tested under:

- Ubuntu 22.04 X11;
- Ubuntu 22.04 Wayland;
- Ubuntu 24.04 GNOME Wayland;
- Ubuntu 24.04 KDE Wayland.

The AppImage must not silently fall back to XWayland when native Wayland is available unless the user explicitly selects X11.

### 2.9 Licensing obligations

The deployment ADR must cover:

- Qt LGPL/GPL or commercial-license obligations;
- dynamic-linking and replacement/relinkability requirements;
- inclusion of license notices and corresponding-source information where required;
- MoltenVK Apache-2.0 notice;
- KDDockWidgets GPL/commercial implications when enabled;
- third-party plugin and font licenses.

No packaging implementation is complete until license artifacts are present in every release format.

---

## 3. Why the migration is justified

Spectra already supports multiple Vulkan swapchains and stable figure ownership, but the current frontend implements desktop behavior itself. That creates substantial platform-specific code for:

- native window creation and destruction;
- tab tear-off and cross-window dragging;
- global mouse and window coordinates;
- custom floating panels and preview windows;
- focus and z-order tracking;
- DPI, monitor movement, resize, and surface lifecycle;
- menus, shortcuts, dialogs, clipboard, drag-and-drop, and accessibility.

Qt provides the platform abstraction needed for Windows, macOS, X11, and native Wayland. The renderer should consume Qt-created surfaces while remaining independent of Qt above the adapter boundary.

The repository is well-positioned for an incremental migration because it already contains:

1. a real framework-free `spectra-core` library;
2. per-window Vulkan resources in `WindowContext`;
3. an adapter-neutral `platform::SurfaceHost`;
4. a working Qt Vulkan surface implementation;
5. a multi-`QWindow` `QtRuntime`;
6. a substantial Qt Vulkan embed/multi-canvas demonstration;
7. stable `FigureId` ownership and a broad test suite.

---

## 4. Current architecture assessment

### 4.1 Build graph

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

- `spectra` is still a mixed target containing renderer, application runtime, platform code, automation, and UI;
- `spectra_qt_adapter` links the full `spectra` target rather than a clean renderer/application-services layer;
- the production `spectra-app` target is only created when GLFW or SDL3 is enabled;
- many framework-neutral services are compiled under the ImGui source block;
- root CMake is overburdened and frontend ownership is difficult to reason about.

### 4.2 Application lifecycle

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
- stable figure-registry access;
- graceful signal-driven shutdown.

The principal lifecycle problem is `src/ui/app/app_step.cpp`. It currently combines:

- backend and renderer construction;
- settings and plugin-service setup;
- GLFW/SDL3 initialization;
- window-manager creation;
- UI-context construction;
- ImGui callback wiring;
- drag-and-drop;
- automation/MCP server startup;
- frame scheduling;
- capture/export state.

It also contains nearly duplicated GLFW and SDL3 startup branches. Decompose it before Qt becomes the default frontend.

### 4.3 Vulkan and native surfaces

The current resource split is fundamentally correct:

```text
Shared per process:
  VkInstance
  VkPhysicalDevice
  VkDevice
  queues
  command-pool strategy
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
- surface lifecycle and destruction policy.

`QtSurfaceHost` already uses `QWindow`, `QVulkanInstance`, and physical-pixel sizing through device-pixel ratio.

Remaining renderer/platform debt:

- legacy `WindowContext::glfw_window`;
- ImGui ownership inside renderer-facing `WindowContext`;
- global mutable `VulkanBackend::set_active_window()` behavior;
- frontend-only preview/panel/title-bar state in `WindowContext`;
- incomplete surface-generation handling during Qt surface destruction and recreation.

### 4.4 Existing Qt implementation

`QtRuntime` already supports:

- one shared Vulkan backend and renderer;
- multiple attached `QWindow` objects;
- one `WindowContext` per `QWindow`;
- per-window begin/render/end APIs;
- resize debounce;
- detach and reattach;
- physical-pixel framebuffer sizing;
- Qt platform-surface ownership.

`examples/qt_embed_demo.cpp` already demonstrates:

- `QSurface::VulkanSurface`;
- `QPlatformSurfaceEvent` handling;
- expose/minimize/restore behavior;
- DPR tracking;
- Qt input forwarding;
- multiple canvases sharing one runtime;
- detach/reattach behavior.

Promote and refactor this implementation into production components. Do not create a second Qt renderer path.

### 4.5 UI and application services

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
- automation handlers.

Several are currently compiled only under `SPECTRA_USE_IMGUI`, owned by `WindowUIContext`, wired directly to `ImGuiIntegration`, or expressed as immediate-mode `draw()` callbacks.

Extract models/controllers before replacing views. Qt widgets must invoke the same services rather than duplicating business logic.

### 4.6 Native-window stack to retire

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

Retain only reusable concepts:

- stable `WindowId` and `CanvasId`;
- figure-to-window assignment;
- deferred close requests where required by renderer safety;
- shared versus per-window Vulkan resource rules;
- semantic events for figure detach, attach, move, focus, and close.

Qt and the docking provider own native drag, docking, focus, activation, top-level windows, and window-manager interaction.

---

## 5. Architecture principles

1. **Qt is the desktop platform, not the renderer.**
2. **No Qt types in public core/render headers.**
3. **No GLFW, SDL3, or ImGui types below frontend-adapter targets.**
4. **One Vulkan device and renderer per process by default.**
5. **One render context per Vulkan canvas.**
6. **Figures and application services outlive individual windows.**
7. **Native windows are created and destroyed only by Qt/docking code.**
8. **Rendering occurs only while a valid Qt platform-surface generation exists.**
9. **No production feature depends on global desktop coordinates.**
10. **Semantic commands are the source of truth for menus, shortcuts, automation, plugins, and the command palette.**
11. **The legacy frontend remains buildable until Qt parity is demonstrated.**
12. **Headless, library, Python, and daemon use cases remain Qt-free.**
13. **UI migration is model-first.**
14. **Native Wayland is a supported target, not an XWayland fallback.**
15. **Official packages use a pinned, controlled Qt runtime.**
16. **Release artifacts are built and tested separately per supported OS baseline.**

---

## 6. Target dependency graph

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
  └── GLFW or SDL3 + ImGui, linked to the same services and renderer
```

---

## 7. Ownership model

### 7.1 Process-scoped services

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

### 7.2 Qt application ownership

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

### 7.3 Canvas ownership

```text
FigureCanvasWidget
  └── QWidget::createWindowContainer(...)
       └── SpectraVulkanWindow : QWindow
            ├── CanvasId
            ├── FigureId/view model
            ├── QtInputRouter
            └── surface-generation token

QtRenderRuntime
  └── CanvasId -> RenderWindowState
       └── WindowContext
```

Do not use `QWindow*` as application identity. Use stable IDs and keep pointers adapter-local.

---

## 8. Docking decision and licensing gate

### 8.1 Required abstraction

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

The application must not directly depend on provider APIs outside provider implementations.

### 8.2 KDDockWidgets provider

Preferred for advanced IDE-style behavior:

- grouped floating windows;
- docks inside floating windows;
- redocking groups;
- multiple main windows;
- stronger layout model;
- Qt Widgets and native-Wayland support.

**Mandatory gate:** choose and record one policy:

1. accept applicable GPL distribution requirements;
2. obtain a commercial license;
3. keep the provider optional and ship native Qt by default.

### 8.3 Native Qt provider

Mandatory fallback:

- `QMainWindow`;
- `QDockWidget` for tool panels;
- custom central document/figure tab host;
- detached figures become another `QMainWindow` or top-level document window;
- `saveState()` and `saveGeometry()` for persistence.

### 8.4 Qt Advanced Docking System

Optional until native-Wayland detach/redock behavior, licensing, maintenance, and packaging pass Spectra's acceptance matrix.

---

## 9. Proposed source layout

```text
src/
  app/
    application_services.*
    application_bootstrap.*
    session_controller.*
    shutdown_coordinator.*

  ui/
    model/
      panel_descriptor.*
      document_model.*
      main_window_model.*
      selection_model.*
      application_state.*
    controllers/
      figure_controller.*
      workspace_controller.*
      panel_controller.*
      command_controller.*

  render/vulkan/
    render_window_state.*
    window_context.*
    vk_backend.*
    renderer.*

  adapters/qt/
    qt_application.*
    qt_main_window.*
    qt_main_window_registry.*
    qt_render_runtime.*
    qt_surface_host.*
    spectra_vulkan_window.*
    figure_canvas_widget.*
    qt_render_scheduler.*
    qt_input_router.*
    qt_action_bridge.*
    qt_dialog_service.*
    qt_clipboard_service.*
    qt_workspace_bridge.*
    qt_theme_bridge.*

    docking/
      docking_host.hpp
      native_qt_docking_host.*
      kddockwidgets_host.*

    panels/
      inspector_widget.*
      topics_widget.*
      settings_widget.*
      timeline_widget.*
      data_sources_widget.*
      command_palette_dialog.*
      welcome_widget.*

  legacy/
    imgui/
    glfw/
    sdl3/
```

Do not move all files at once. Establish target and ownership boundaries first.

---

## 10. CMake plan

### 10.1 Options

```cmake
option(SPECTRA_BUILD_QT_APP "Build the production Qt 6 desktop app" ON)
option(SPECTRA_BUILD_LEGACY_APP "Build the legacy GLFW/SDL3 + ImGui app" ON)
option(SPECTRA_BUILD_QT_TESTS "Build Qt GUI and integration tests" ON)
option(SPECTRA_PACKAGE_PRIVATE_QT "Package the controlled private Qt runtime" ON)

set(SPECTRA_QT_DOCKING_PROVIDER "native"
    CACHE STRING "Qt docking provider: native | kddockwidgets | qtads")
set_property(CACHE SPECTRA_QT_DOCKING_PROVIDER
             PROPERTY STRINGS native kddockwidgets qtads)
```

During migration, the Qt app may remain default-OFF until the cutover gate. Official release builds must explicitly enable the private-runtime packaging path.

### 10.2 Qt discovery

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Test
)
```

Add modules only when needed. Enable `AUTOMOC`, `AUTOUIC`, and `AUTORCC` only on Qt targets.

Source/developer builds may point `CMAKE_PREFIX_PATH` to a compatible Qt installation. Official release builds must use the pinned Qt toolchain/runtime revision.

### 10.3 Target separation

Required end state:

- `spectra-core`;
- `spectra-render-vulkan`;
- `spectra-app-services`;
- `spectra-qt-platform`;
- `spectra-qt-widgets`;
- `spectra-app`;
- `spectra-legacy-app`;
- optional `spectra-qt-runtime` packaging component.

The installed library and headless packages must not depend on Qt unless consumers request the Qt adapter.

### 10.4 Deployment integration

Add deployment helpers under `cmake/deployment/`:

```text
QtRuntimeManifest.cmake
DeployQtLinux.cmake
DeployQtWindows.cmake
DeployQtMacOS.cmake
ValidateRuntimeClosure.cmake
```

They must:

- collect only required Qt libraries/plugins;
- preserve private runtime layout;
- configure relative RPATH/RUNPATH;
- generate license manifests;
- fail if developer paths leak into artifacts;
- validate missing shared-library dependencies;
- produce deterministic file manifests for CI comparison.

### 10.5 Docking dependency acquisition

For KDDockWidgets:

- prefer `find_package(KDDockWidgets CONFIG)`;
- optionally provide a pinned `FetchContent` source;
- never follow an unpinned branch;
- expose an offline build path;
- include license artifacts;
- keep it optional until the licensing gate closes.

---

## 11. Event loop and frame scheduling

Qt owns the event loop. The production frontend must not run a blocking manual poll loop.

Use event-driven rendering:

- `QWindow::requestUpdate()` for redraw requests;
- queued Qt signals for thread-to-GUI notifications;
- a timer only while animation or high-rate streaming requires continuous frames;
- stop continuous timers while idle, hidden, minimized, or fully occluded where detectable;
- coalesce data and resize notifications;
- preserve `FrameScheduler` as policy engine.

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

Initial threading policy:

- Qt objects stay on the GUI thread;
- Vulkan recording/submission stays on the GUI thread during migration;
- ingestion and background services remain on worker threads;
- workers publish synchronized model updates and queued redraw requests.

A dedicated render thread is out of scope until the Qt frontend is stable and profiled.

---

## 12. Vulkan lifecycle requirements

### 12.1 QVulkan ownership

Continue the existing strategy:

- Spectra creates `VkInstance`;
- `QVulkanInstance` adopts it;
- each canvas is a Vulkan `QWindow`;
- Qt creates/owns the platform surface associated with the `QWindow`;
- Spectra owns swapchain and device-level resources.

Do not introduce `QVulkanWindow`.

### 12.2 Surface generations

Add a monotonically increasing generation to each canvas.

On `SurfaceAboutToBeDestroyed`:

1. stop new frames for that canvas;
2. invalidate its generation;
3. wait only for relevant fences;
4. destroy swapchain-dependent resources;
5. release surface references according to adapter ownership;
6. retain figure and document state.

On surface creation or first exposure:

1. increment generation;
2. create/retrieve the surface;
3. verify presentation support;
4. create swapchain resources from physical-pixel size;
5. request a full redraw.

Every asynchronous frame request validates the generation.

### 12.3 Explicit render target

Phase out global mutable active-window state.

Target API:

```cpp
backend.begin_frame(window_context);
renderer.render_figure(window_context, figure, frame_state);
backend.end_frame(window_context);
```

### 12.4 Resize and DPR

- derive swapchain extent from logical size and current DPR;
- respond to resize, screen, and DPR changes;
- coalesce interactive resize events;
- handle zero-size/minimized windows without recreation loops;
- recreate immediately for `OUT_OF_DATE`;
- recover from `SURFACE_LOST`;
- avoid `vkDeviceWaitIdle()` for routine resize.

### 12.5 MoltenVK portability ✅ (implemented)

Add a portability policy to Vulkan bootstrap:

- ✅ enumerate extensions before instance creation — `vkEnumerateInstanceExtensionProperties` query in `create_instance()`;
- ✅ conditionally enable portability enumeration — `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` set when `VK_KHR_portability_enumeration` is available;
- ✅ select devices exposing portability subset — `VK_KHR_portability_subset` conditionally added in `get_required_device_extensions()`;
- ✅ make unsupported optional features degrade explicitly — fallback `#define` macros for older Vulkan headers;
- [ ] add macOS renderer capability reports to diagnostics;
- ✅ maintain a MoltenVK-specific test list — `tests/unit/test_moltenvk_portability.cpp` smoke test covering instance creation, device enumeration, portability extension queries, and headless rendering.

---

## 13. Input and interaction migration

Create framework-neutral input types independent of GLFW values:

```cpp
enum class PointerButton;
enum class Key;
enum class Modifier : uint32_t;
enum class PointerEventType;
enum class KeyEventType;
```

Events carry:

- logical and physical-pixel position;
- local canvas position;
- pointer type;
- pressure/tilt where available;
- modifiers;
- timestamp;
- DPR;
- canvas ID.

`QtInputRouter` maps:

- mouse move/press/release/double-click;
- pixel and angle wheel deltas;
- key press/release;
- text and IME;
- tablet/stylus;
- touch/gesture;
- drag/drop;
- focus enter/leave.

Preserve pan, zoom, selection, measurement, annotations, 3D orbit, and keyboard navigation through existing controllers.

Do not use global cursor position for docking.

### 13.1 Shortcuts and actions

`CommandRegistry` remains the source of truth. `QtActionBridge` creates and updates `QAction` objects from command descriptors.

The same command ID remains callable from:

- menus and toolbars;
- command palette;
- plugin API;
- automation/MCP;
- keyboard shortcuts;
- tests.

---

## 14. Qt shell design

### 14.1 Main window

`SpectraMainWindow` contains:

- native menu bar;
- configurable toolbars;
- central document/figure area;
- dockable panels;
- status bar;
- command palette action;
- welcome page when no figures are open.

No primary-renderer-window semantics may leak into models.

### 14.2 Figure documents

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

Distinguish:

- figure identity;
- view-instance identity;
- main-window identity;
- canvas identity.

### 14.3 Panel migration order

1. Inspector;
2. Topics/data sources;
3. Settings and shortcuts;
4. Timeline/animation;
5. Series/data editor;
6. export/recording;
7. plugin diagnostics;
8. ROS2/PX4 tools.

Each panel uses a controller/view-model and avoids direct renderer mutation from widget paint code.

### 14.4 Themes and icons

- map Spectra tokens to Qt palette/style sheet;
- preserve plot/data colors in `ThemeManager`;
- use high-DPI `QIcon` resources;
- avoid fixed pixel assumptions;
- test system theme changes;
- retain Spectra theme override.

---

## 15. Workspace version 5

Add a versioned Qt desktop-layout section without corrupting older workspaces.

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

- v4 loads figure, axis, series, interaction, shortcut, plugin, and theme state;
- legacy ImGui `dock_state` is converted only where deterministic;
- otherwise generate a sensible Qt layout;
- v5 retains a legacy-compatible subset where possible;
- invalid monitor geometry is clamped to available screens;
- native Wayland restores topology and sizes but does not promise exact absolute positions;
- unavailable providers fall back without losing figures/panels.

Add round-trip, corruption, provider-mismatch, and missing-monitor tests.

---

## 16. Plugin compatibility

Do not break plugin API v2.0 merely to migrate the frontend.

Preserve:

- commands;
- transforms;
- overlays;
- exporters;
- data sources;
- custom series;
- current backend-handle contracts.

Immediate-mode plugin UI callbacks are frontend-specific.

Migration policy:

1. retain them in the legacy frontend;
2. show a compatibility notice or generic controls panel in Qt;
3. introduce an optional versioned UI-description API for properties, actions, enums, validation, status, and model updates;
4. render that schema in both frontends;
5. deprecate immediate-mode callbacks only in a future major plugin API.

Overlay helpers remain renderer-neutral and must not require plugin linkage to Qt or ImGui.

---

## 17. Automation and testability

Prefer semantic operations:

```text
execute_command("file.open")
create_figure(...)
move_figure(...)
set_panel_visible(...)
set_axis_limits(...)
capture_canvas(...)
```

Introduce stable Qt object names/test IDs for:

- windows;
- actions;
- panels;
- figure tabs;
- canvases;
- dialogs;
- status indicators.

`QtAutomationAdapter` maps the existing external protocol to Qt objects and shared services.

Continue honoring:

- `--no-native-dialogs`;
- `SPECTRA_NO_NATIVE_DIALOGS`;
- `SPECTRA_AUTOMATION`.

Use injectable dialog services for deterministic tests.

---

## 18. Multiprocess, Python, ROS2, and PX4

### 18.1 Multiprocess

Replace the legacy window agent frontend with the same Qt shell configured as an IPC client.

Preserve:

- daemon discovery;
- `SPECTRA_SOCKET`;
- `spectra-window --socket ...`;
- versioned IPC;
- publisher-first attach behavior.

Do not create separate UI implementations for `spectra`, `spectra-window`, `spectra-ros`, and `spectra-px4`.

### 18.2 Python

Python remains independent of Qt:

- model calls use existing IPC/in-process APIs;
- launching desktop UI starts the Qt executable;
- headless export remains Qt-free;
- wheels do not bundle Qt unless a deliberate optional desktop extra is introduced.

### 18.3 ROS2 and PX4

Adapters contribute:

- commands;
- models;
- data sources;
- panel descriptors/factories;
- status providers.

Qt widgets render those models. Core adapters do not include Qt headers unless split into explicit `*_qt_ui` targets.

---

## 19. Migration phases

### Phase 0 — Decisions, baselines, and packaging guardrails ✅ (ADRs complete)

Deliverables:

- ✅ ADR: Qt Widgets + direct `QWindow` Vulkan — `docs/adr/ADR-001-qt-widgets-vulkan-architecture.md`;
- ✅ ADR: docking provider and licensing — `docs/adr/ADR-002-docking-provider-and-licensing.md`;
- ✅ ADR: Qt 6.8.x pinning and private-runtime policy — `docs/adr/ADR-003-qt-pinning-and-private-runtime.md`;
- [ ] ADR: Ubuntu 22.04/24.04 separate binary baselines;
- [ ] ADR: Windows deployment and signing;
- [ ] ADR: macOS MoltenVK, signing, and notarization;
- ✅ baseline performance/startup measurements — existing benchmarks in `tests/bench/`;
- ✅ legacy build/test baseline;
- ✅ Qt build-only CI jobs;
- ✅ inventory of frontend type leakage;
- ✅ license-manifest prototype — `packaging/LICENSES/THIRD_PARTY_LICENSES.md`.

Acceptance:

- ✅ decisions documented (3 of 6 ADRs complete);
- ✅ no behavior change;
- ✅ legacy and Qt demo builds pass;
- ✅ packaging and docking licensing have owners and deadlines.

### Phase 1 — Extract application services (partial)

Work:

- create `ApplicationServices`;
- move settings, commands, shortcuts, undo, plugins, workspace, and automation ownership out of `WindowUIContext`;
- split `app_step.cpp`;
- move neutral sources out of ImGui-only CMake blocks;
- create interfaces for dialogs, clipboard, redraw, and windows;
- preserve `App::init_runtime()/step()/shutdown_runtime()`.

Acceptance:

- legacy behavior unchanged;
- headless tests remain Qt-free;
- services compile without frontend headers;
- no duplicate platform initialization inside services.

### Phase 2 — Production Qt Vulkan canvas ✅

Work:

- promote `QtRuntime` to production `QtRenderRuntime`;
- extract `SpectraVulkanWindow` and `FigureCanvasWidget`;
- add surface generations;
- add explicit render-target APIs;
- implement event-driven scheduling;
- add Qt input router;
- remove ImGui initialization from Qt runtime;
- add a minimal Qt smoke executable.

Acceptance:

- multiple canvases render from one device;
- independent resize/input/focus;
- detach/reattach passes validation layers;
- no permanent idle timer;
- no GLFW/SDL symbols in Qt targets.

### Phase 3 — Native Qt shell and commands (partial)

Work:

- ✅ create main window and welcome page;
- ✅ create central document tabs;
- ✅ bind commands to actions (QtActionBridge);
- ✅ implement menus/toolbars/status bar;
- ✅ implement dialogs/clipboard (QtDialogService, QtClipboardService);
- ✅ implement command palette (QtCommandPaletteDialog, Ctrl+K shortcut, fuzzy search via CommandRegistry);
- ✅ implement settings panel (QtSettingsWidget, theme/palette selection, panel visibility toggles, SettingsStore persistence);
- ✅ migrate basic Inspector panel (QtInspectorWidget, figure/axes title/labels/limits/grid/border, tabbed per-axes view);
- ✅ migrate basic Topics panel (QtTopicsWidget, data source list, start/stop controls, DataSourceRegistry integration);
- ✅ wire panels into SpectraMainWindow as dockable QDockWidgets;
- ✅ wire ApplicationServices into SpectraMainWindow for panel and palette access;
- ✅ add View menu toggle actions for Inspector/Topics/Settings panels;
- ✅ add File menu action and Ctrl+K shortcut for command palette.

Acceptance:

- create/open/close figures;
- CSV open through native/injected paths;
- command IDs are shared across UI and automation;
- no-figure state works;
- keyboard navigation smoke test passes.

### Phase 4 — Multi-window and docking (partial)

Work:

- ✅ implement `DockingHost` abstract interface (`docking_host.hpp`);
- ✅ implement `NativeQtDockingHost` (native Qt provider: `QMainWindow` + `QDockWidget` + `QTabWidget`);
- ✅ create `MainWindowRegistry` for multi-window tracking and cross-window operations;
- ✅ support document detach into new `SpectraMainWindow` via `detach_document()`;
- ✅ support cross-window document movement via `move_document()`;
- ✅ wire `figure_detach_requested` signal from `SpectraMainWindow` tab context menu;
- ✅ add `open_figure_ids()` accessor to `SpectraMainWindow`;
- ✅ update `QtApplicationController` to own `MainWindowRegistry` and route detach;
- ✅ update `qt_app.cpp` example with second figure and programmatic detach demo;
- ✅ update `CMakeLists.txt` with docking source files;
- optionally implement KDDockWidgets after licensing gate (deferred);
- remove custom preview/global-cursor logic from Qt path (N/A — Qt path has none).

Acceptance:

- Windows, macOS, X11, KDE Wayland, and GNOME Wayland smoke tests (pending platform testing);
- no XWayland requirement (Qt-native Wayland);
- topology save/restore via `save_layout()`/`restore_layout()` (implemented, needs integration testing);
- repeated detach/redock without lifetime or Vulkan errors (needs validation-layer testing).

### Phase 5 — Visual and functional parity (reopened; blocking)

The classes below are prototypes and migration scaffolding. Code presence or widget construction does
not constitute parity. Each item remains open until its legacy-vs-Qt workflow test and visual artifact
pass the gates in Section 0.

Migrate and prove:

- [ ] inspector and complete figure/axes/series controls, including selection, reorder, clipboard, and undo;
- [ ] timeline, animation curves, playback/recording, and animation-state synchronization;
- [ ] shortcut editor with real key capture, conflict handling, persistence, and command execution;
- [ ] export preview, framebuffer readback, PNG/SVG/video, image copy, progress, cancel, and errors;
- [ ] plugins and data sources with equivalent lifecycle, diagnostics, and live-update behavior;
- [ ] split panes/subplots with state-preserving rebuild, nested topology, drag, detach, and restore;
- [ ] data editor and transform pipelines with equivalent validation, presets, and undo behavior;
- [ ] accessibility, table export, focus order, names/roles, and sonification controls;
- [ ] annotations, measurement, selection, ROI, tooltips, legends, markers, and crosshair through a renderer-neutral overlay path used by both frontends;
- [ ] ROS2/PX4 panels and display/inspector surfaces without placeholders.

Acceptance:

- [ ] the Section 0 functional matrix is implemented and signed off;
- [ ] no supported workflow requires the legacy UI;
- [ ] renderer-region golden comparisons satisfy the pixel threshold;
- [ ] shell screenshot comparisons pass the full size/DPR/platform matrix;
- [ ] interaction replay produces the same model, undo, persistence, and export results;
- [ ] Qt tests contain approved image baselines and image diffs, not only widget-existence checks;
- [ ] validation-layer multi-window and repeated detach/redock tests pass;
- [ ] all TODO/placeholder paths in supported Qt workflows are removed or explicitly declared out of scope.

### Phase 6 — Workspace, plugins, and automation

**Status: partial — schema/helper coverage exists; full live-state save/restore and workflow parity do not**

Work:

- [x] workspace v5 and v4 migration — `FORMAT_VERSION` bumped to 5, `DesktopLayoutState` added to `WorkspaceData`, serialize/deserialize implemented;
- [x] provider-specific layout serialization — `QtWorkspaceBridge` converts between `DockLayoutState` and `DesktopLayoutState`, saves/restores main window + detached windows;
- [x] Qt automation adapter — `QtAutomationAdapter` wraps `McpServer` + `AutomationServer`, starts via `SPECTRA_AUTOMATION` env var, Qt timer-based polling;
- [x] portable plugin UI schema — `PluginUIRegistry` with framework-neutral `PluginUISchema` types, C ABI (`spectra_register_plugin_ui`/`spectra_unregister_plugin_ui` in API v2.1), `PluginUIRegistry` wired into `ApplicationServices` and `PluginManager`, Qt rendering via `QtPluginPanelWidget`, plugin management via `QtPluginsWidget`;
- [x] crash-recovery restore — `WorkspaceAutosave` wired into `QtApplicationController`, `check_crash_recovery()` prompts user via `QMessageBox` on startup.

Acceptance:

- [x] v4/v5 fixtures load;
- [x] multi-window round-trip;
- [x] provider mismatch degrades safely (`QtWorkspaceBridge::apply_layout` returns false on provider mismatch);
- [x] automation passes (`qt_test_qt_automation` — start/stop, callback wiring, command execution);
- [x] ABI-compatible plugins load (`qt_test_qt_plugin_ui` — schema register/unregister, property/action callbacks, all element types, enum/color properties).

### Phase 7 — Runtime variants and adapters (partial)

Work:

- ✅ Qt in-process app — `spectra-qt-app` starts `InprocTopicServer` so Python publishers connect directly;
- ✅ Qt window agent — `spectra-qt-app --socket <path>` connects to daemon via `QtIpcClient` (QTimer-based IPC polling, snapshot/diff handling, heartbeat);
- ✅ daemon discovery — auto-discovers live `spectra-*.sock` in `$XDG_RUNTIME_DIR` (mirrors legacy `src/app/main.cpp` logic);
- ✅ IPC preservation — shared `figure_snapshot.hpp/cpp` extracted from `src/agent/main.cpp` for reuse by Qt IPC client;
- ✅ Qt ROS2/PX4 shell composition — `RosPanelManager` in `src/adapters/qt/ros2/` bridges `DisplayRegistry`/`DisplayPlugin` with Qt dockable panels, includes displays list panel, inspector panel, per-display dock widgets, layout serialization, and integration tests (`tests/qt/test_qt_ros_panel.cpp`);
- ✅ backend/headless packages remain Qt-free (Qt adapter is a separate CMake target, gated by `SPECTRA_USE_QT`).

Acceptance:

- ✅ Python publisher-first flow opens Qt frontend (in-process topic server);
- ✅ reconnect/restart works (IPC client handles `STATE_SNAPSHOT` resync);
- ✅ ROS2/PX4 remain optional;
- ✅ backend/headless packages remain Qt-free.

### Phase 8 — Cross-platform packaging and release hardening

**Status: packaging infrastructure implemented; release qualification blocked by Phase 5**

Work:

- ✅ CMake deployment helpers — `cmake/deployment/QtRuntimeManifest.cmake` (collects Qt libs + QPA plugins), `DeployQtLinux.cmake` (private runtime install + qt.conf + symlinks), `DeployQtWindows.cmake` (windeployqt + DLL filtering), `DeployQtMacOS.cmake` (macdeployqt + MoltenVK bundling), `ValidateRuntimeClosure.cmake` (RPATH/ldd/qt.conf validation);
- ✅ CMake options — `SPECTRA_PACKAGE_PRIVATE_QT`, `SPECTRA_QT_DOCKING_PROVIDER` (native | kddockwidgets | qtads) added to root CMakeLists.txt;
- ✅ RPATH configuration — `$ORIGIN/../lib/spectra/qt/lib` (Linux), `@executable_path/../lib/spectra/qt/lib` (macOS);
- ✅ CPack component-based packaging — `spectra` (main app) + `spectra-qt-runtime` (private Qt) split with Debian component-specific package names and dependencies;
- ✅ `spectra-qt-app` install rule added to main CMakeLists.txt;
- ✅ AppImage updated — `AppImageBuilder.yml` now uses `spectra-qt-app` as exec, bundles Qt libs + QPA plugins + image format plugins, sets `QT_PLUGIN_PATH` and `QT_QPA_PLATFORM_PLUGIN_PATH` env vars; `build-appimage.sh` generates `qt.conf` in AppDir;
- ✅ Docker build environments — `docker/spectra-jammy/Dockerfile` (Ubuntu 22.04) and `docker/spectra-noble/Dockerfile` (Ubuntu 24.04) with Qt6, Vulkan, `SPECTRA_PACKAGE_PRIVATE_QT=ON`, `cpack -G DEB`;
- ✅ CI workflow jobs — `qt-build` (build + Qt integration tests offscreen), `qt-package-jammy` (.deb for Ubuntu 22.04), `qt-package-noble` (.deb for Ubuntu 24.04), `qt-package-windows` (ZIP via windeployqt), `qt-package-macos` (DMG via macdeployqt + MoltenVK);
- ✅ Third-party license manifest — `packaging/LICENSES/THIRD_PARTY_LICENSES.md` covering Qt LGPL, MoltenVK Apache-2.0, GLFW, ImGui, nlohmann/json, STB, VMA, FlatBuffers, Inter font, KDDockWidgets, QtADS; installed into `${CMAKE_INSTALL_DOCDIR}`;
- ✅ Packaging files updated — Homebrew formula adds `qt@6` dependency and Qt build flags; Scoop manifest adds `spectra-qt-app.exe` and Vulkan suggestion; AUR PKGBUILD adds `qt6-base` dependency and Qt build flags;
- [ ] Pinned Qt 6.8.x runtime built from source (currently uses system Qt 6.x — production release needs pinned 6.8.x);
- [ ] Clean-machine launch tests on Ubuntu 22.04/24.04, Windows 10/11, macOS;
- [ ] Code signing (Windows) and notarization (macOS) in CI;
- ✅ Performance regression benchmarks against legacy frontend — `tests/bench/bench_qt_frontend.cpp` measures CommandRegistry, UndoManager, headless render, figure creation, and Qt-specific QtActionBridge overhead; integrated into CMake build with optional Qt linking.

Acceptance:

- `apt install spectra` requires no manual Qt installation;
- Ubuntu 22.04 and 24.04 packages launch on clean systems;
- native Wayland and X11 both launch from official packages;
- Windows installer and ZIP launch without Qt/Vulkan SDK;
- macOS ARM64 and Intel apps launch with bundled MoltenVK;
- no missing platform plugins;
- no developer paths;
- license artifacts are complete;
- validation layers and performance budgets pass.

### Phase 9 — Default switch and legacy retirement

**Status: blocked — switch mechanics exist, but the cutover is reverted pending parity**

Work:

- ✅ `SPECTRA_DEFAULT_FRONTEND` CMake option added (`legacy` | `qt`) — controls which frontend ships as the `spectra` binary;
- ✅ Output name switching — when `SPECTRA_DEFAULT_FRONTEND=qt`, Qt app becomes `spectra`, legacy app becomes `spectra-legacy`;
- ✅ Legacy app `src/app/main.cpp` marked DEPRECATED with migration instructions;
- ✅ Migration and deployment notes published — `docs/MIGRATION_NOTES.md` covering build options, packaging, user migration path, rollback procedure, private Qt runtime, third-party licenses, CI pipeline;
- [ ] Keep Qt build/test jobs, but package and launch it under an explicit preview name until parity;
- [ ] Ensure release packaging jobs (jammy, noble, Windows, macOS) keep the legacy frontend as `spectra`;
- [ ] Ensure Docker/release environments do not silently select the Qt frontend before cutover approval;
- [ ] Collect issue telemetry from Qt frontend usage (requires release cycle);
- [ ] Remove custom WindowManager/ImGui shell after deprecation cycle;
- [ ] Retain ImGui only for intentional internal/debug uses.

Acceptance:

- [ ] every Section 0 parity gate passes on all supported platforms;
- [ ] Qt is approved as the release default on all supported platforms;
- [ ] legacy has no unique supported workflow;
- [ ] all removal gates are met after the required opt-in release cycle.

---

## 20. PR-sized implementation sequence

1. **Build graph and application-services extraction** — no visible UI change. ✅
2. **Production Qt canvas library** — surface lifecycle, scheduler, input, tests. ✅
3. **Minimal Qt application shell** — main window, tabs, actions, dialogs. (partial; parity work remains)
4. **Native docking and multiple main windows** — layout persistence. (partial; state-preserving split/detach remains)
5. **Controlled Qt runtime packaging prototype** — Ubuntu 22.04 and 24.04 package skeletons. ✅ (CMake deployment helpers, Docker builders, CI jobs, license manifest implemented; pinned Qt 6.8.x from source pending)
6. **Conditional KDDockWidgets provider** — only after licensing ADR. (deferred)
7. **Panel migrations** — one complete, parity-tested workflow per PR. (in progress)
8. **Workspace v5 and automation**. (schema/helpers implemented; live-state parity pending)
9. **Windows deployment pipeline**. ✅ (CI job implemented; signing pending)
10. **macOS MoltenVK deployment pipeline**. ✅ (CI job implemented; notarization pending; MoltenVK portability support implemented in `vk_device.cpp` with smoke test)
11. **Release cutover and legacy deprecation**. (blocked until Section 0 passes; switch mechanics alone do not count)
12. **ADRs and architecture decisions**. ✅ (ADR-001 Qt Widgets + Vulkan, ADR-002 docking provider/licensing, ADR-003 Qt 6.8 pinning/private runtime)
13. **Performance regression benchmarks**. ✅ (`bench_qt_frontend.cpp` — CommandRegistry, UndoManager, headless render, figure creation, QtActionBridge overhead)
14. **ROS2/PX4 Qt workflows**. (composition scaffolding exists; display and inspector parity pending)

Every PR must include a rollback path and preserve the legacy build until Phase 9.

---

## 21. Testing strategy

### 21.1 Unit tests

Add tests for:

- application-service ownership/shutdown;
- command-to-action mapping;
- key/modifier mapping;
- physical extent calculation;
- workspace v4-to-v5 migration;
- docking layout model;
- window/document registry;
- surface-generation state machine;
- redraw coalescing;
- provider fallback;
- runtime-manifest generation;
- private Qt path resolution;
- MoltenVK capability fallback.
- ✅ MoltenVK portability smoke test (`test_moltenvk_portability` — instance creation with portability enumeration, device extension queries, headless rendering under MoltenVK);

### 21.2 Qt integration tests

Use Qt Test for:

- ✅ action invocation (`qt_test_qt_action_bridge` — QAction creation, metadata, trigger, refresh, categories, disabled state);
- ✅ docking layout (`qt_test_qt_docking` — descriptor defaults, sentinel IDs, MainWindowRegistry tracking, DockLayoutState round-trip);
- ✅ automation (`qt_test_qt_automation` — start/stop lifecycle, callback wiring, command registry integration, multiple cycles);
- ✅ plugin UI schema (`qt_test_qt_plugin_ui` — register/find/replace/unregister, property/action callbacks, change listener, all element types, enum/color);
- ✅ panel visibility (`qt_test_qt_panels` — dock widget show/hide, toggle pattern, multiple dock areas, SpectraMainWindow panels/menus/toolbar/status bar, welcome page, split view initial state, stable object names);
- ✅ tab create/close/move (`qt_test_qt_panels` — tab count, close safety; `qt_test_qt_window_ops` — close figure tab safety, canvas lookup, active figure ID, open figure IDs);
- ✅ detach/attach (`qt_test_qt_window_ops` — MainWindowRegistry detach without runtime, create detached window safety, close all detached);
- ✅ close order (`qt_test_qt_window_ops` — close invalid host, registry destructor safety, close all detached);
- ✅ focus switching (`qt_test_qt_window_ops` — find_host_for_figure with no hosts, active_figure_id initially invalid);
- ✅ dialog injection (`qt_test_qt_dialogs` — NullDialogService, NullClipboardService, NullRedrawRequest, NullWindowService, QtClipboardService copy/paste, QtRedrawRequest callbacks, QtWindowService create/close/focus/count, QtDialogService instantiation);
- ✅ shortcut scopes (`qt_test_qt_window_ops` — Ctrl+K absence without services, QShortcut per-window scoping, command execution via QAction trigger);
- ✅ workspace restore (`qt_test_qt_workspace` — WorkspaceData v5 format version, DesktopLayoutState structure, JSON round-trip, v4-to-v5 migration, QtWorkspaceBridge null registry safety, provider mismatch, empty provider match, validation);
- ✅ platform-surface destroy/recreate (`qt_test_qt_window_ops` — SpectraVulkanWindow creation, surface generation, requestFrame, forceDetach, animation timer start/stop);
- ✅ ROS2 panel composition (`qt_test_qt_ros_panel` — RosPanelManager add/remove/enable/disable displays, layout serialization/deserialization, multiple displays, find display);

**Build:** `cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_TESTS=ON ..`  
**Run:** `ctest -L qt --output-on-failure`  
**Headless:** Tests use `QT_QPA_PLATFORM=offscreen` (set automatically by CTest).  
**CI:** `qt-build` job in `.github/workflows/ci.yml` runs Qt integration tests on every push/PR.  
**Packaging CI:** `qt-package-jammy`, `qt-package-noble`, `qt-package-windows`, `qt-package-macos` jobs produce .deb/ZIP/DMG artifacts.

These tests validate scaffolding and remain useful, but their existing checkmarks do not imply visual
or workflow parity.

### 21.3 Legacy-vs-Qt parity harness

Build one frontend-neutral scenario driver that applies identical fixtures and semantic actions to the
legacy and Qt applications. Each scenario must emit:

- a full-window screenshot and a cropped renderer-region screenshot;
- serialized figure/axes/series and overlay state;
- active window, document, pane, panel, and tool state;
- command enabled/check state, shortcut bindings, and undo/redo stack position;
- workspace/export artifacts and structured errors;
- Vulkan validation messages and frame timing.

The test runner compares these artifacts using the Section 0 thresholds, writes baseline/current/diff
images on failure, and never updates approved baselines implicitly. Baseline updates require an explicit
review that includes both the legacy reference and Qt candidate.

### 21.4 Vulkan GUI stress tests

- two to eight canvases;
- continuous resize for five minutes;
- minimize/restore loops;
- hide/show;
- close during animation;
- detach during streaming;
- mixed-DPI monitor movement;
- display hot-plug where possible;
- `OUT_OF_DATE` and `SURFACE_LOST` recovery;
- application exit with floating windows;
- daemon disconnect/reconnect.

### 21.5 Platform and artifact matrix

| Artifact | Build environment | Required runtime tests |
|---|---|---|
| Ubuntu 22.04 x86-64 `.deb` | clean Ubuntu 22.04 | X11, GNOME Wayland where available, clean APT install |
| Ubuntu 24.04 x86-64 `.deb` | clean Ubuntu 24.04 | X11, GNOME Wayland, KDE Wayland |
| Linux AppImage | oldest supported baseline | Ubuntu 22.04/24.04 X11 and Wayland |
| Windows x86-64 | MSVC 2022 | Windows 10 and 11 clean VMs |
| macOS ARM64 | current supported Xcode | supported minimum macOS and current macOS |
| macOS Intel | supported Xcode/runner | supported minimum macOS and current macOS |

For every packaged artifact:

- launch with no developer Qt installation;
- inspect dynamic dependencies;
- run with `QT_DEBUG_PLUGINS=1` in CI diagnostics;
- verify platform-plugin selection;
- verify Vulkan device and surface creation;
- open multiple windows;
- detach/redock;
- save and restore workspace;
- export an image;
- close cleanly.

### 21.6 Performance budgets

Measure against legacy frontend:

- cold startup;
- first frame;
- idle CPU/GPU;
- 60 FPS frame time;
- input-to-present latency;
- memory per canvas;
- detach/redock latency;
- workspace load time;
- private Qt runtime package size.

A steady-rendering regression over 10% requires investigation. Package-size and shell-startup regressions require explicit rationale.

---

## 22. Packaging validation checklist

### Linux

- package installs with `apt` on a clean image;
- private Qt libraries resolve before system Qt without global environment variables;
- `qxcb` launches under X11;
- Wayland plugin launches natively under GNOME and KDE;
- system Qt/KDE applications remain unaffected;
- `spectra-backend` installs without Qt;
- RPATH/RUNPATH contains only approved relative paths;
- `ldd` contains no build-directory paths;
- package uninstall removes private runtime cleanly;
- APT upgrades replace the Qt runtime atomically with a matching Spectra version.

### Windows

- installer and ZIP contain identical runtime closure;
- `qwindows.dll` is found from packaged plugin path;
- no PATH dependency on Qt or Vulkan SDK;
- signing verifies;
- uninstall is clean;
- user settings/workspaces are preserved according to policy.

### macOS

- `otool -L` contains only system or bundled paths;
- MoltenVK is bundled and discovered;
- app passes `codesign --verify --deep --strict`;
- notarization and stapling pass;
- Gatekeeper launch succeeds on a clean machine;
- both ARM64 and Intel packages render the validation scene.

---

## 23. Major risks and mitigations

| Risk | Mitigation |
|---|---|
| Big-bang rewrite stalls feature work | Dual frontend and PR-sized vertical slices |
| Renderer duplicated between `App` and `QtRuntime` | One `ApplicationServices` owner and one backend/renderer |
| Qt surface destruction races GPU work | Surface generations and per-canvas fence shutdown |
| Global active-window state causes cross-window bugs | Explicit render-window parameters |
| KDDockWidgets license conflicts with MIT | ADR, optional provider, native fallback |
| Wayland breaks global-coordinate drag logic | Use Qt/provider-native user-driven docking |
| Workspace layouts become incompatible | Workspace v5 and safe v4 migration |
| Plugins contain ImGui-only callbacks | Compatibility path plus portable UI schema |
| Automation depends on pixels | Semantic commands and stable Qt object IDs |
| Qt leaks into headless/Python packages | Separate targets and package dependency tests |
| Ubuntu system Qt is older than required | Bundle pinned private Qt runtime |
| Ubuntu 24.04 binary fails on 22.04 | Separate distro build baselines |
| Private Qt conflicts with KDE/system Qt | Isolated paths and relative RPATH; no global environment changes |
| Missing QPA plugins break launch | Runtime manifest and clean-machine smoke tests |
| macOS lacks native Vulkan | Bundle/test MoltenVK portability subset |
| Windows machine lacks Vulkan runtime | Define and package/test loader requirements |
| Qt licensing obligations are missed | License ADR, manifests, legal review, source information |
| Idle timer wastes resources | Event-driven scheduler |
| Mixed-DPI windows render incorrectly | Per-window DPR and physical extent tests |
| Native dialogs block tests | Injectable dialog service |

---

## 24. Legacy removal criteria

Do not delete the GLFW/SDL3 + ImGui frontend until all are true:

- Qt app ships on Ubuntu 22.04, Ubuntu 24.04, Windows, and supported macOS targets;
- X11 and native Wayland packages pass;
- multi-window detach/redock is stable;
- workspace migration is released;
- automation passes (✅ `qt_test_qt_automation`);
- Python, ROS2, PX4, and multiprocess flows pass;
- APT, AppImage, Windows, and macOS packages are validated on clean systems;
- users do not need to install Qt manually;
- no critical Qt issue remains for one release cycle;
- performance budgets are accepted;
- plugin and licensing policies are published.

---

## 25. Definition of done

The migration is complete when:

1. `spectra` launches the Qt 6 desktop application.
2. Multiple native main windows and Vulkan canvases work on every supported platform.
3. Detachable documents and tool panels use Qt/provider-native behavior.
4. Native Wayland is first-class.
5. Spectra owns Vulkan and shares one device by default.
6. Core, renderer, headless, IPC, backend, and Python use cases remain Qt-free.
7. Commands, undo, shortcuts, plugins, and automation use shared semantic services.
8. Workspaces preserve figures and desktop layouts through a versioned format.
9. ROS2 and PX4 compose into the same Qt shell.
10. Ubuntu users install through APT without manually installing Qt.
11. Ubuntu 22.04 and 24.04 receive separately built, pinned-runtime packages.
12. Windows artifacts bundle and validate their Qt runtime closure.
13. macOS artifacts bundle MoltenVK and pass signing/notarization.
14. CI, package, validation-layer, license, and performance gates pass.
15. The custom WindowManager and ImGui application shell can be removed without losing a supported feature.

---

## 26. Immediate next actions

1. ✅ Approve Qt 6.8.x pinning and the private-runtime policy.
2. ✅ Decide whether Linux Qt runtime is embedded in `spectra` or split into `spectra-qt-runtime`. (Split into `spectra-qt-runtime` package)
3. [ ] Create the architecture, docking-license, and deployment ADRs.
4. ✅ Implement application-services extraction.
5. ✅ Promote `qt_embed_demo` classes into production Qt platform components.
6. ✅ Add Ubuntu 22.04 and 24.04 package-build prototypes early, before broad UI migration.
7. [ ] Add a macOS MoltenVK capability smoke target before claiming renderer portability.
8. ✅ Keep new feature work model/controller-oriented for dual-frontend use.
9. [ ] Build pinned Qt 6.8.x from source for production packages.
10. [ ] Clean-machine launch tests on all platforms.
11. [ ] Code signing (Windows) and notarization (macOS) in CI.
12. [ ] Performance regression benchmarks against legacy frontend.
13. [ ] Collect issue telemetry from Qt frontend for one release cycle.
14. [ ] Remove legacy WindowManager/ImGui shell after deprecation cycle.

---

## 27. Reference documentation

- Qt 6.8 supported platforms: <https://doc.qt.io/qt-6.8/supported-platforms.html>
- Qt 6.8 `QVulkanInstance`: <https://doc.qt.io/qt-6.8/qvulkaninstance.html>
- Qt 6.8 `QWindow`: <https://doc.qt.io/qt-6.8/qwindow.html>
- Qt 6.8 Vulkan integration: <https://doc.qt.io/qt-6.8/vulkan.html>
- Qt 6.8 high-DPI behavior: <https://doc.qt.io/qt-6.8/highdpi.html>
- Qt 6.8 `QMainWindow`: <https://doc.qt.io/qt-6.8/qmainwindow.html>
- Qt deployment for Windows: <https://doc.qt.io/qt-6/windows-deployment.html>
- Qt deployment for macOS: <https://doc.qt.io/qt-6/macos-deployment.html>
- Ubuntu 22.04 Qt package baseline: <https://packages.ubuntu.com/jammy/qt6-base-dev>
- Ubuntu 24.04 Qt package baseline: <https://packages.ubuntu.com/noble/qt6-base-dev>
- MoltenVK: <https://github.com/KhronosGroup/MoltenVK>
- KDDockWidgets documentation: <https://kdab.github.io/KDDockWidgets/>
- KDDockWidgets repository/licensing: <https://github.com/KDAB/KDDockWidgets>
- Qt Advanced Docking System: <https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System>
