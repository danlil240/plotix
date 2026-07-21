# ADR-001: Qt 6 Widgets + Direct QWindow Vulkan Architecture

**Status:** Accepted  
**Date:** 2026-07-21  
**Decision owner:** Spectra core team  

## Context

Spectra needs a production desktop frontend that supports multiple native OS windows, detachable/dockable panels, native Wayland operation, menus, shortcuts, dialogs, accessibility, and high-DPI behavior across Windows, macOS, and Linux.

The current frontend uses GLFW/SDL3 for window creation and ImGui for application chrome, docking, panels, and menus. This requires substantial platform-specific code for window lifecycle, tab tear-off, global mouse coordinates, focus tracking, DPI handling, and native dialogs.

The Vulkan renderer and `spectra-core` are already framework-neutral. `platform::SurfaceHost` isolates surface creation, and a working `QtRuntime` with multi-`QWindow` Vulkan canvases already exists.

## Decision

Adopt **Qt 6.8.x Widgets** as the production desktop application shell with **direct Vulkan rendering into `QWindow` canvases**.

Key principles:

1. **Qt is the desktop platform, not the renderer.** Spectra owns `VkInstance`, `VkDevice`, swapchains, and the render graph. Qt owns the event loop, native window lifecycle, menus, dialogs, and focus.
2. **No `QVulkanWindow` or Qt RHI.** Each canvas is a `QWindow` with `QSurface::VulkanSurface`. Spectra creates and adopts the `QVulkanInstance`.
3. **Framework-neutral `ApplicationServices`** owns renderer, figure registry, commands, shortcuts, undo, plugins, workspace, and automation — no Qt/GLFW/ImGui headers.
4. **Dual-frontend transition.** The legacy GLFW/SDL3 + ImGui frontend remains buildable until Qt parity is demonstrated and a release cycle completes.
5. **Headless, library, Python, and daemon use cases remain Qt-free.**
6. **macOS uses MoltenVK** with `VK_KHR_portability_enumeration` and `VK_KHR_portability_subset`.

## Alternatives Considered

### Qt Quick / QML
- Rejected: Spectra targets scientific/engineering users who need native widget fidelity, complex dock layouts, and accessibility. QML adds a JavaScript engine and declarative UI complexity that does not match the use case.
- QML rendering integration with Vulkan is also less straightforward than widget-based `QWidget::createWindowContainer()`.

### Electron / Web-based
- Rejected: Introduces a Chromium runtime, JavaScript bridge, and memory overhead incompatible with a high-performance Vulkan plotting library.

### Retain GLFW/SDL3 + ImGui
- Rejected as production frontend: requires reimplementing native dialogs, accessibility, Wayland docking, clipboard, drag-and-drop, and high-DPI per-platform. Qt provides these cross-platform abstractions.
- Retained as legacy frontend during transition.

### SDL3 + Custom UI
- Rejected: SDL3 provides windowing but not application chrome, docking, dialogs, or accessibility. Would require the same custom UI code as ImGui.

## Consequences

- **Positive:** Cross-platform native window management, menus, dialogs, accessibility, and high-DPI support without custom platform code.
- **Positive:** Native Wayland support without XWayland fallback.
- **Positive:** Existing Vulkan renderer and core remain unchanged.
- **Negative:** Qt becomes a required dependency for desktop builds (not for headless/library/Python).
- **Negative:** Dual-frontend maintenance burden during transition.
- **Negative:** macOS requires MoltenVK portability subset handling.

## References

- Qt 6.8 supported platforms: <https://doc.qt.io/qt-6.8/supported-platforms.html>
- Qt 6.8 Vulkan integration: <https://doc.qt.io/qt-6.8/vulkan.html>
- Spectra Qt6 Application Migration Plan, Section 1 (Executive decisions)
