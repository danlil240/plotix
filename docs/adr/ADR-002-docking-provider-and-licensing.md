# ADR-002: Docking Provider and Licensing

**Status:** Accepted  
**Date:** 2026-07-21  
**Decision owner:** Spectra core team  

## Context

Spectra's Qt frontend requires IDE-style docking: detachable panels, dockable documents, tab groups, floating windows with nested docks, and layout save/restore. The migration plan defines a `DockingHost` abstraction that isolates the docking provider from application code.

Three providers were evaluated:

1. **KDDockWidgets** — advanced IDE-style docking (KDAB).
2. **Qt Advanced Docking System (QtADS)** — community docking framework.
3. **Native Qt** — `QMainWindow` + `QDockWidget` + custom tab host.

## Decision

Ship **Native Qt as the default docking provider** and keep KDDockWidgets and QtADS as optional, behind explicit CMake flags.

### Rationale

- **Native Qt has no licensing risk.** Qt is already LGPL/GPL/commercial dual-licensed and bundled as a private runtime. `QMainWindow` + `QDockWidget` adds no new license obligations.
- **KDDockWidgets is GPL/commercial dual-licensed.** Enabling it by default would impose GPL distribution requirements on Spectra or require purchasing commercial licenses for all contributors. This is incompatible with Spectra's MIT license for the default build.
- **QtADS is LGPL but has not been independently validated** against Spectra's native-Wayland acceptance matrix, maintenance concerns, and packaging requirements.
- **Native Qt provides sufficient functionality** for the initial release: dockable panels via `QDockWidget`, tabbed documents via `QTabWidget`, detach via `QMainWindow` floating windows, and `saveState()`/`saveGeometry()` for persistence.

### Configuration

```cmake
set(SPECTRA_QT_DOCKING_PROVIDER "native"
    CACHE STRING "Qt docking provider: native | kddockwidgets | qtads")
```

- `native` (default): `QMainWindow` + `QDockWidget` + custom document host.
- `kddockwidgets`: only when a commercial license is obtained or GPL distribution is accepted.
- `qtads`: only after passing the native-Wayland acceptance matrix.

### Future upgrade path

If KDDockWidgets is later licensed commercially or a GPL distribution path is accepted:
1. Implement `KDDockWidgetsHost : DockingHost`.
2. Add `find_package(KDDockWidgets CONFIG)` with a pinned `FetchContent` fallback.
3. Include license artifacts in release packages.
4. Validate against the native-Wayland acceptance matrix.

## Alternatives Considered

### KDDockWidgets as default
- Rejected: GPL/commercial licensing incompatible with MIT default distribution.
- Could be revisited if commercial licenses are purchased for all official release builds.

### QtADS as default
- Rejected: Not yet validated against native Wayland, packaging, and maintenance requirements.
- Can be promoted after validation.

### Custom docking from scratch
- Rejected: Would replicate the same platform-specific code that motivated the Qt migration.

## Consequences

- **Positive:** No additional licensing obligations for the default build.
- **Positive:** `DockingHost` abstraction allows future provider upgrades without application code changes.
- **Negative:** Native Qt docking is less feature-rich than KDDockWidgets (no nested floating docks, no redocking groups).
- **Negative:** Advanced IDE-style behavior requires KDDockWidgets or QtADS opt-in.

## References

- KDDockWidgets documentation: <https://kdab.github.io/KDDockWidgets/>
- KDDockWidgets repository/licensing: <https://github.com/KDAB/KDDockWidgets>
- Qt Advanced Docking System: <https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System>
- Spectra Qt6 Application Migration Plan, Section 8 (Docking decision and licensing gate)
