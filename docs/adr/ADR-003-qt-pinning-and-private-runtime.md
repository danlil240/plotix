# ADR-003: Qt 6.8.x Pinning and Private Runtime Policy

**Status:** Accepted  
**Date:** 2026-07-21  
**Decision owner:** Spectra core team  

## Context

Ubuntu 22.04 ships Qt 6.2.x and Ubuntu 24.04 ships Qt 6.4.x. Both are older than the selected Qt 6.8 application baseline. Official Spectra packages cannot safely link against the distribution's default Qt libraries because:

1. API/ABI differences between Qt 6.2/6.4 and 6.8 can cause runtime failures.
2. Platform plugin behavior differs across Qt minor versions.
3. System Qt upgrades could break installed Spectra without warning.

## Decision

**Pin Qt 6.8.x per release train and ship a private Qt runtime with official packages.**

### Policy

1. **Pin a tested Qt 6.8.x patch version** per Spectra release train.
2. **Build and test Spectra against that exact Qt runtime.**
3. **Ship required Qt libraries and plugins privately** with Spectra under `/usr/lib/spectra/qt/`.
4. **Isolate from system Qt** using `$ORIGIN`-based RPATH/RUNPATH and `qt.conf`.
5. **Do not modify global `LD_LIBRARY_PATH`** or install private Qt into `/usr/lib`.
6. **Source builds** may use another compatible Qt only when explicitly configured and tested.

### Package split (Linux)

```text
spectra
  ├── Qt desktop executable
  ├── depends on spectra-qt-runtime (= matching release)
  └── depends on libspectra runtime components

spectra-qt-runtime
  ├── private Qt Core/Gui/Widgets libraries
  ├── XCB and Wayland QPA plugins
  ├── only the image/icon plugins Spectra uses
  ├── Qt license notices
  └── no global replacement of distribution Qt
```

### Private runtime layout

```text
/usr/bin/spectra
/usr/lib/spectra/
/usr/lib/spectra/qt/lib/
/usr/lib/spectra/qt/plugins/platforms/
/usr/share/spectra/
```

RPATH: `$ORIGIN/../lib/spectra/qt/lib` (Linux), `@executable_path/../lib/spectra/qt/lib` (macOS).

### Windows

Bundle Qt DLLs alongside `spectra.exe` using `windeployqt` output, filtered to only required modules:
- `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`
- `platforms/qwindows.dll`
- Required image/icon plugins

### macOS

Bundle Qt frameworks inside `Spectra.app/Contents/Frameworks/` using `macdeployqt`, plus MoltenVK:
- `QtCore.framework`, `QtGui.framework`, `QtWidgets.framework`
- `libMoltenVK.dylib` or `MoltenVK.framework`
- `PlugIns/platforms/libqcocoa.dylib`

### Separate build baselines

Produce separate `.deb` artifacts for Ubuntu 22.04 and 24.04:
- Build in clean containers per baseline.
- Use the same pinned Qt patch level, built for each baseline.
- A 24.04-built package may not run on 22.04 due to glibc differences.

### Private runtime contents

```text
Qt6Core, Qt6Gui, Qt6Widgets
Qt6DBus (only if used)
Qt6Svg (only if runtime SVG support is used)
QPA plugins: xcb, wayland, wayland-egl
image format plugins actually used
TLS/network plugins only if required
```

System-level libraries remain APT dependencies: glibc, libstdc++, Vulkan loader, XCB/X11, Wayland, xkbcommon, fontconfig/freetype, OpenGL/EGL.

## Alternatives Considered

### Use system Qt
- Rejected: Ubuntu 22.04 (Qt 6.2) and 24.04 (Qt 6.4) are too old. API/ABI mismatches cause runtime failures. System upgrades could break installed Spectra.

### Flatpak/Snap
- Considered as future distribution channel, but does not replace the need for .deb packages for users who prefer APT. Private runtime policy applies regardless of package format.

### Static link Qt
- Rejected: LGPL requires dynamic linking or relinkability. Static linking complicates license compliance.

## Consequences

- **Positive:** One predictable Qt API/ABI across all supported operating systems.
- **Positive:** System Qt upgrades cannot break installed Spectra.
- **Positive:** KDE and other system applications remain unaffected.
- **Negative:** Larger package size (Qt libraries bundled).
- **Negative:** Must rebuild private Qt runtime when upgrading Qt patch versions.
- **Negative:** Packaging validation must verify no system Qt leakage.

### Licensing obligations

- Qt LGPL 3.0: dynamic linking satisfies relinkability requirement; include license notices and corresponding-source information.
- MoltenVK Apache-2.0: include notice.
- KDDockWidgets (if enabled): GPL or commercial license obligations.
- Third-party plugins and fonts: individual license requirements.

## References

- Qt deployment for Linux: <https://doc.qt.io/qt-6/linux-deployment.html>
- Qt deployment for Windows: <https://doc.qt.io/qt-6/windows-deployment.html>
- Qt deployment for macOS: <https://doc.qt.io/qt-6/macos-deployment.html>
- Spectra Qt6 Application Migration Plan, Section 2 (Cross-platform release and Qt runtime policy)
