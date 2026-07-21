# Spectra Qt6 Migration & Deployment Notes

**Spectra version:** 0.3.0  
**Date:** 2026-07-20

---

## Overview

Spectra is migrating from a GLFW/ImGui desktop frontend to a Qt 6.8.x Widgets
frontend with direct Vulkan rendering. This document covers the deployment
changes, build system options, and user migration path.

---

## Build System Options

### Frontend selection

| CMake option | Default | Description |
|---|---|---|
| `SPECTRA_USE_QT` | `OFF` | Enable Qt6 adapter library |
| `SPECTRA_BUILD_QT_APP` | `OFF` | Build the production Qt6 desktop app |
| `SPECTRA_BUILD_QT_TESTS` | `OFF` | Build Qt6 integration tests |
| `SPECTRA_DEFAULT_FRONTEND` | `legacy` | Which frontend ships as `spectra`: `legacy` (GLFW/ImGui) or `qt` (Qt6) |
| `SPECTRA_PACKAGE_PRIVATE_QT` | `OFF` | Bundle private Qt runtime in packages |
| `SPECTRA_QT_DOCKING_PROVIDER` | `native` | Docking provider: `native`, `kddockwidgets`, `qtads` |

### Output binary names

| `SPECTRA_DEFAULT_FRONTEND` | Qt app binary name | Legacy app binary name |
|---|---|---|
| `legacy` (default) | `spectra-qt-app` | `spectra` |
| `qt` | `spectra` | `spectra-legacy` |

### Build examples

**Development (legacy default):**
```bash
cmake -B build -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_APP=ON
cmake --build build
# Binaries: build/spectra (legacy), build/spectra-qt-app (Qt)
```

**Production (Qt default):**
```bash
cmake -B build \
  -DSPECTRA_USE_QT=ON \
  -DSPECTRA_BUILD_QT_APP=ON \
  -DSPECTRA_DEFAULT_FRONTEND=qt \
  -DSPECTRA_PACKAGE_PRIVATE_QT=ON
cmake --build build
# Binaries: build/spectra (Qt), build/spectra-legacy (legacy)
```

**Headless / backend only (no Qt):**
```bash
cmake -B build -DSPECTRA_USE_QT=OFF
cmake --build build
# Binary: build/spectra (legacy, if GLFW enabled)
```

---

## Packaging

### Linux (.deb)

Packages are built using Docker containers for each Ubuntu release:

```bash
# Ubuntu 22.04 (Jammy)
docker build -f docker/spectra-jammy/Dockerfile -t spectra-jammy-builder .
docker run --rm spectra-jammy-builder ls /packages/

# Ubuntu 24.04 (Noble)
docker build -f docker/spectra-noble/Dockerfile -t spectra-noble-builder .
docker run --rm spectra-noble-builder ls /packages/
```

Package split:
- `spectra` — main application binary (Qt frontend)
- `spectra-qt-runtime` — private Qt 6.8.x libraries and QPA plugins
- `spectra-backend` — headless daemon (no Qt dependency)

Install:
```bash
sudo apt install spectra
# Qt runtime is pulled in automatically via dependency
```

### AppImage

```bash
cmake -B build \
  -DSPECTRA_USE_QT=ON \
  -DSPECTRA_BUILD_QT_APP=ON \
  -DSPECTRA_DEFAULT_FRONTEND=qt \
  -DSPECTRA_PACKAGE_PRIVATE_QT=ON
cmake --build build
./packaging/AppImage/build-appimage.sh build
```

### Windows

Built in CI via `qt-package-windows` job. Uses `windeployqt` to collect Qt
DLLs and plugins. Output: `spectra-<version>-win64.zip`.

### macOS

Built in CI via `qt-package-macos` job. Uses `macdeployqt` and bundles
MoltenVK for Vulkan support. Output: `spectra-<version>-macos.dmg`.

---

## User Migration Path

### For end users

1. **No action required** — `apt install spectra` or package update pulls in
   the Qt frontend automatically.
2. The `spectra` command now launches the Qt6 desktop app.
3. The legacy GLFW/ImGui frontend remains available as `spectra-legacy` for
   one release cycle.
4. Workspaces saved in v5 format are compatible with both frontends.

### For developers

1. Install Qt 6.8.x development packages:
   - Ubuntu: `sudo apt install qt6-base-dev`
   - macOS: `brew install qt@6`
   - Windows: Use `aqtinstall` or Qt Online Installer
2. Configure with `-DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_APP=ON`
3. Run Qt integration tests: `ctest -L qt --output-on-failure`

### For plugin authors

- Plugins using the framework-neutral UI schema work with both frontends.
- Plugins with ImGui-specific callbacks need to use the compatibility path.
- See the plugin UI schema documentation for the portable interface.

---

## Rollback Procedure

If the Qt frontend has critical issues:

1. **Users:** Run `spectra-legacy` instead of `spectra`.
2. **Package maintainers:** Rebuild with `-DSPECTRA_DEFAULT_FRONTEND=legacy`.
3. **CI:** The `qt-build` job can be disabled without affecting legacy builds.

---

## Private Qt Runtime

Spectra bundles its own Qt 6.8.x runtime to ensure:
- Consistent behavior across platforms
- No dependency on system Qt version
- No conflicts with KDE or other Qt applications

The private runtime is installed to:
- Linux: `/usr/lib/spectra/qt/`
- macOS: `Spectra.app/Contents/Frameworks/`
- Windows: `bin/Qt6*.dll`

A `qt.conf` file redirects Qt to find its plugins and libraries relative to
the Spectra executable, not system paths.

---

## Third-Party Licenses

See `packaging/LICENSES/THIRD_PARTY_LICENSES.md` for the full manifest.
Key points:
- Qt 6.8.x is used under LGPL v3 (dynamic linking)
- MoltenVK is Apache-2.0
- All other bundled libraries are MIT or compatible

---

## CI Pipeline

| Job | Platform | Purpose |
|---|---|---|
| `qt-build` | Ubuntu 24.04 | Build Qt app + run integration tests |
| `qt-package-jammy` | Ubuntu 22.04 | Produce .deb for 22.04 |
| `qt-package-noble` | Ubuntu 24.04 | Produce .deb for 24.04 |
| `qt-package-windows` | Windows 2022 | Produce ZIP with windeployqt |
| `qt-package-macos` | macOS 15 | Produce DMG with macdeployqt + MoltenVK |

---

## Known Limitations

- Pinned Qt 6.8.x from source is not yet implemented (uses system Qt)
- Code signing (Windows) and notarization (macOS) are not yet in CI
- Clean-machine launch tests are manual
- Performance regression benchmarks against legacy frontend are pending
