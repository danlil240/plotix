# Spectra — Third-Party License Manifest

**Spectra version:** 0.3.0  
**Generated:** (auto-generated at build time)  
**License:** MIT (see [LICENSE](../../LICENSE))

---

## Bundled Qt 6.8.x Runtime

Spectra ships a private Qt 6.8.x runtime (Core, Gui, Widgets) and QPA platform
plugins (XCB, Wayland, Cocoa, Windows) as part of its official packages.

- **Qt licensing:** Dual-licensed under LGPL v3 and commercial license.
  Spectra exercises the LGPL v3 option via dynamic linking.
- **LGPL obligations:**
  - Qt libraries are dynamically linked (not statically linked)
  - Users can relink with a modified Qt version
  - Qt source code is available at https://www.qt.io/download-open-source
  - License text: https://www.gnu.org/licenses/lgpl-3.0.html
- **Qt version:** 6.8.x (pinned per release — see `qt-runtime-manifest.json`)
- **Components used:** QtCore, QtGui, QtWidgets
- **QPA plugins:** qxcb, qwayland (Linux); qcocoa (macOS); qwindows (Windows)
- **Image format plugins:** qico, qsvg, qjpeg, qgif, qpng

### Qt license notice

```
This program uses Qt 6.8.x, which is licensed under the GNU LGPL v3.
Qt is a trademark of The Qt Company Ltd.

Source code for Qt is available at:
  https://www.qt.io/download-open-source

To relink with a modified version of Qt, replace the files under:
  /usr/lib/spectra/qt/lib/  (Linux)
  Contents/Frameworks/       (macOS)
  bin/Qt6*.dll               (Windows)
```

---

## MoltenVK (macOS only)

Spectra bundles MoltenVK on macOS to provide Vulkan support via Metal.

- **License:** Apache-2.0
- **Source:** https://github.com/KhronosGroup/MoltenVK
- **Copyright:** Copyright (c) 2015-2024 The Brenwill Workshop Ltd.
- **License text:** https://www.apache.org/licenses/LICENSE-2.0

---

## Vulkan Loader

- **License:** Apache-2.0
- **Source:** https://github.com/KhronosGroup/Vulkan-Loader
- **Note:** System dependency (not bundled); installed via OS package manager.

---

## GLFW (legacy frontend only)

- **License:** zlib/libpng license
- **Source:** https://www.glfw.org/
- **Copyright:** Copyright (c) 2002-2006 Marcus Geelnard, Camilla Berglund
- **Note:** Statically linked into the legacy frontend. Not part of Qt packages.

---

## Dear ImGui (legacy frontend only)

- **License:** MIT
- **Source:** https://github.com/ocornut/imgui
- **Copyright:** (c) 2014-2025 Omar Cornut
- **Note:** Statically linked into the legacy frontend. Not part of Qt packages.

---

## nlohmann/json

- **License:** MIT
- **Source:** https://github.com/nlohmann/json
- **Copyright:** (c) 2013-2024 Niels Lohmann

---

## STB libraries

- **License:** MIT / Public Domain
- **Source:** https://github.com/nothings/stb
- **Copyright:** Sean Barrett

---

## Vulkan Memory Allocator (VMA)

- **License:** MIT
- **Source:** https://github.com/GPUOpen-LibrariesAndSDK/VulkanMemoryAllocator
- **Copyright:** (c) 2017-2024 Advanced Micro Devices, Inc.

---

## FlatBuffers

- **License:** Apache-2.0
- **Source:** https://github.com/google/flatbuffers
- **Copyright:** (c) 2014-2024 Google Inc.

---

## Inter Font

- **License:** SIL Open Font License 1.1
- **Source:** https://rsms.me/inter/
- **Copyright:** Rasmus Andersson

---

## KDDockWidgets (optional, not enabled by default)

- **License:** GPL v3 or commercial
- **Source:** https://github.com/KDAB/KDDockWidgets
- **Status:** Optional docking provider. Not bundled in default packages.
  Enablement requires an explicit licensing decision (ADR pending).

---

## Qt Advanced Docking System (optional, not enabled by default)

- **License:** LGPL v2.1
- **Source:** https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System
- **Status:** Optional docking provider. Not bundled in default packages.

---

## License file locations in packages

| Package | License files location |
|---------|----------------------|
| Linux .deb | `/usr/share/doc/spectra/copyright` and `/usr/share/doc/spectra-qt-runtime/copyright` |
| Windows ZIP | `LICENSES/` directory |
| macOS DMG | `Spectra.app/Contents/Resources/LICENSES/` |
| AppImage | `usr/share/doc/spectra/copyright` |
