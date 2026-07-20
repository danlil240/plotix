<p align="center">
  <img src="icons/spectra_banner.png" alt="Spectra Banner" width="600">
</p>

<h3 align="center">GPU-accelerated scientific plotting for C++20 and Python</h3>

<p align="center">
  <a href="https://danlil240.github.io/Spectra/">Documentation</a> · <a href="https://danlil240.github.io/Spectra/getting-started.html">Quick Start</a> · <a href="https://danlil240.github.io/Spectra/examples.html">Examples</a> · <a href="https://danlil240.github.io/Spectra/ros2-adapter.html">ROS2 Adapter</a>
</p>

---

## Why Spectra?

Most plotting libraries are CPU-bound, single-threaded, and treat animation as an afterthought. Spectra is different:

- **GPU-first** — Vulkan 1.2 rendering. Anti-aliased lines, 18 SDF markers, and dash patterns run entirely on the GPU.
- **Real-time ready** — Stream live sensor data at 60 fps with O(1) ring-buffer appends and zero-copy NumPy transfers.
- **2D + 3D in one library** — Line, scatter, surface, mesh plots with Blinn-Phong lighting, colormaps, and orbit camera.
- **Feels like MATLAB** — `spectra::plot(x, y, "r--o")` one-liners that scale to multi-window, multi-tab workspaces.
- **C++ and Python** — Native C++20 library with a Python IPC bridge that auto-launches the backend.
- **Headless export** — Render to PNG, GIF, or MP4 without a window — perfect for CI and batch pipelines.

---

## 5-Second Demo

**C++:**
```cpp
#include <spectra/easy.hpp>

int main() {
    spectra::plot({0.f, 1.f, 2.f, 3.f, 4.f},
                  {0.f, 1.f, 0.5f, 1.5f, 2.f}, "c-o");
    spectra::title("Hello Spectra");
    spectra::show();
}
```

**Python:**
```python
import spectra as sp
sp.plot([0, 1, 4, 9, 16, 25])
sp.show()
```

---

## Install

### APT Repository (Ubuntu 22.04 / 24.04)

The fastest way to install on Ubuntu — adds the Spectra repo so `apt` handles
updates automatically:

```bash
# 1. Import the signing key
curl -fsSL https://danlil240.github.io/Spectra/apt/spectra-archive-keyring.asc \
  | sudo gpg --dearmor -o /etc/apt/keyrings/spectra.gpg

# 2. Add the repository (auto-detects your Ubuntu codename)
echo "deb [signed-by=/etc/apt/keyrings/spectra.gpg] https://danlil240.github.io/Spectra/apt $(lsb_release -cs) main" \
  | sudo tee /etc/apt/sources.list.d/spectra.list

# 3. Install
sudo apt update
sudo apt install spectra
```

### Python

```bash
pip install spectra-plot
```

No compiler or Vulkan SDK needed — just a working Vulkan runtime/driver.

### Build from Source

> For contributors and build/test agents: use the canonical environment guide at
> [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md) before configuring CMake.

```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
    libvulkan-dev vulkan-validationlayers mesa-vulkan-drivers \
    glslang-tools libglfw3-dev \
    libwayland-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libxkbcommon-dev libgl1-mesa-dev \
    python3 python3-pillow libeigen3-dev
```

```bash
git clone https://github.com/danlil240/Spectra.git
cd Spectra
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Other Artifacts

- **`.deb`** (direct download): `sudo apt install ./spectra_<version>_amd64.deb`. No `-dev` packages needed; `apt` resolves runtime dependencies.
- **AppImage**: download and run — a working Vulkan-capable driver is required.
- **Python wheel**: `pip install spectra-plot`.

> **Platform-specific setup, CMake options, Eigen integration, and packaging →** [Getting Started Guide](https://danlil240.github.io/Spectra/getting-started.html)

---

## Feature Highlights

| Domain | What you get |
|---|---|
| **Core Rendering** | Vulkan pipeline, MSAA 4x, GPU text, SDF anti-aliasing, format strings (`"r--o"`) |
| **WebGPU / wasm** | Alternative WebGPU backend — same codebase runs in the browser via Emscripten |
| **3D Visualization** | Surface, mesh, scatter, line — with lighting, transparency, wireframe, colormaps |
| **Easy API** | `plot()`, `scatter()`, `subplot()`, `plot3()`, `surf()` — 7 levels of progressive complexity |
| **Animation** | Frame callbacks, timeline editor, 7 keyframe interpolation modes, camera animator |
| **UI** | Command palette, undo/redo, docking/split view, inspector, configurable shortcuts |
| **Data Interaction** | Tooltips, crosshair, markers, linked axes, shared cursor, 14 data transforms |
| **Multi-Window** | Independent OS windows, tab tear-off, per-window Vulkan swapchain |
| **Python** | `spectra.plot()` one-liners, NumPy fast path, live streaming, auto-launch backend |
| **Export** | Headless PNG/GIF/MP4, CMake `find_package`, plugin API, workspace save/load |
| **ROS2** | Topic monitor, live plotter, bag player/recorder, TF tree, node graph, service caller |

> **Full feature breakdown →** [Feature Guide](https://danlil240.github.io/Spectra/features.html)

---

## Python Quick Start

```python
import spectra as sp
import numpy as np

x = np.linspace(0, 10, 500)

sp.subplot(2, 1, 1)
sp.plot(x, np.sin(x), label="sin")
sp.title("Sine")

sp.subplot(2, 1, 2)
sp.plot(x, np.cos(x), label="cos")
sp.title("Cosine")

sp.show()
```

Live streaming, 3D plots, statistical charts, and the Session API are covered in the [documentation](https://danlil240.github.io/Spectra/features.html#python-api).

---

## ROS2 Adapter

One native app for live plotting, bag review, and 3D context — no juggling `rqt`, PlotJuggler, and RViz.

**First plot in ~10 seconds:**

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_ROS2_BAG=ON
cmake --build build --target spectra-ros -j$(nproc)

# Terminal 1 — synthetic publisher
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{ linear: { x: 0.5 } }" --rate 10

# Terminal 2 — plot
./build/spectra-ros --topics /cmd_vel:linear.x
```

**Session presets** (checked into `sessions/presets/`):

```bash
./build/spectra-ros --session sessions/presets/tuning.spectra-ros-session   # IMU / PID
ros2 launch spectra bringup.launch.py                                        # cmd_vel + odom
```

> **Docs →** [ROS2 Adapter](https://danlil240.github.io/Spectra/ros2-adapter.html) · [Tool comparison](https://danlil240.github.io/Spectra/ros2-comparison.html)

---

## Live Topics From Docker

Publishers can start before Spectra. For Docker, share the host runtime
directory so the publisher can discover the `spectra-*.sock` file when the app
opens:

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$XDG_RUNTIME_DIR:$XDG_RUNTIME_DIR" \
  -e XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
  spectra-topic-publisher:local
```

Then start Spectra on the host:

```bash
spectra
```

Do not set `SPECTRA_SOCKET` for this publisher-first workflow; leaving it unset
lets the publisher follow the newest live Spectra socket.

---

## WebGPU / WebAssembly (experimental)

Spectra includes a WebGPU rendering backend that enables the same C++ codebase to run in the browser via Emscripten. All GLSL shaders are ported to WGSL. The WebGPU backend supports line, scatter, grid, text, and statistical series — the same 2D pipeline as the Vulkan backend.

**Native (Dawn):**

```bash
cmake -B build -DSPECTRA_USE_WEBGPU=ON \
      -Ddawn_DIR=/path/to/dawn/install/lib/cmake/dawn
cmake --build build --target webgpu_demo
./build/examples/webgpu_demo
```

**Browser (Emscripten):**

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DSPECTRA_USE_WEBGPU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --target webgpu_demo
# Open build-wasm/examples/webgpu_demo.html in Chrome 113+ or Edge 113+
```

The example source is in [`examples/webgpu_demo.cpp`](examples/webgpu_demo.cpp). It uses the standard `App` + `Figure` + `Axes` API — no WebGPU-specific code needed in user applications.

> **Note:** The WebGPU backend is inproc-only. IPC/multiproc mode is not available on wasm targets (no Unix sockets in the browser). 3D pipeline types are not yet ported to WGSL.

---

## 40+ Examples

The `examples/` directory covers every major feature — from basic line plots to 3D lit surfaces, timeline animation, multi-window tabs, and headless export.

```bash
./build/examples/basic_line
./build/examples/demo_3d
./build/examples/easy_api_demo
```

> **Full example index →** [Examples](https://danlil240.github.io/Spectra/examples.html)

---

## Architecture

Spectra runs in two modes selected at runtime — no `#ifdef`:

- **Inproc** (default) — Single-process: App → WindowManager → Renderer → Vulkan
- **Multiproc** — Daemon (`spectra-backend`) + window agents via versioned TLV IPC protocol

All windows are peer-equivalent. No "primary window" concept. Stable `FigureId` ownership via `FigureRegistry`.

> **System topology, project structure, design decisions →** [Architecture Overview](https://danlil240.github.io/Spectra/architecture.html)

---

## Quality

- **1,200+ unit tests** · **50+ golden image tests** · **100+ benchmarks**
- Cross-platform CI: Linux (GCC + Clang), macOS (ARM), Windows (MSVC)
- ASan + UBSan sanitizer jobs · Headless golden tests via lavapipe
- Release pipeline: `.deb`, `.rpm`, AppImage, `.dmg`, `.zip`, Python wheels → PyPI

```bash
cmake -B build -DSPECTRA_BUILD_TESTS=ON
cmake --build build && cd build && ctest --output-on-failure
```

---

## Contributing

1. **C++20** — No global state, RAII, thread-safe via `std::mutex`
2. **Tests required** — Add unit tests; run `ctest` before submitting
3. **Vulkan safety** — Never destroy resources without waiting on fences
4. **No speculative fixes** — Measure first, then optimize

```bash
make build test    # Build + run tests
make format        # clang-format
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

<p align="center">
  <a href="https://danlil240.github.io/Spectra/">📖 Documentation</a> · <a href="https://danlil240.github.io/Spectra/getting-started.html">🚀 Getting Started</a> · <a href="https://danlil240.github.io/Spectra/features.html">✨ Features</a> · <a href="https://danlil240.github.io/Spectra/examples.html">📋 Examples</a>
</p>
