# =============================================================================
# Spectra — Full Build Environment (ROS2 + PX4 + Vulkan + Qt6 + Everything)
#
# Base:  ros:jazzy-ros-base  (Ubuntu 24.04 Noble + ROS2 Jazzy)
# Features enabled:
#   Vulkan (lavapipe)  GLFW  ImGui  Eigen  FFmpeg  ROS2+rosbag2  PX4  Qt6
#   Examples  Tests  Golden tests  Benchmarks  QA agent  Embed shared lib
#
# Build:  docker build -f docker/spectra-full/Dockerfile -t spectra-full .
# Run:    docker run -it --rm spectra-full
# Verify: ctest --test-dir /src/spectra/build -LE "gpu|golden" -j$(nproc)
# =============================================================================

FROM ros:jazzy-ros-base

# ─── 1. System dependencies ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    \
    # Build toolchain
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    \
    # GCC 13 (explicit version for consistent C++20 support)
    gcc-13 \
    g++-13 \
    \
    # Vulkan — headers, loader, validation layers, Mesa lavapipe (software renderer)
    vulkan-tools \
    libvulkan-dev \
    vulkan-validationlayers \
    mesa-vulkan-drivers \
    \
    # Shader compiler (GLSL → SPIR-V)
    glslang-tools \
    \
    # Window system dev headers (GLFW + Qt build against these)
    libwayland-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev \
    libglfw3-dev \
    \
    # Python — icon generation, golden test image comparison, Python bindings
    python3 \
    python3-pillow \
    python3-pip \
    \
    # Eigen3 — math library adapters
    libeigen3-dev \
    \
    # ROS2 additional packages (base image already has rclcpp, std_msgs, etc.)
    nlohmann-json3-dev \
    libtinyxml2-dev \
    ros-jazzy-rosbag2-cpp \
    ros-jazzy-rosbag2-storage \
    ros-jazzy-visualization-msgs \
    ros-jazzy-tf2-msgs \
    ros-jazzy-diagnostic-msgs \
    ros-jazzy-nav-msgs \
    ros-jazzy-rcpputils \
    \
    # Qt6 — embedding adapter + example
    qt6-base-dev \
    \
    # FFmpeg — video export via pipe
    ffmpeg \
    \
    # Xvfb — headless X11 for golden image tests
    xvfb \
    \
    && rm -rf /var/lib/apt/lists/*

# ─── 2. Runtime environment ──────────────────────────────────────────────────
ENV XDG_RUNTIME_DIR=/tmp/spectra-runtime
RUN mkdir -p "$XDG_RUNTIME_DIR" && chmod 1777 "$XDG_RUNTIME_DIR"

# Auto-source ROS2 Jazzy on every new shell
RUN echo 'source /opt/ros/jazzy/setup.bash' >> /root/.bashrc

# ─── 3. Entrypoint script ────────────────────────────────────────────────────
COPY docker/spectra-full/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# ─── 4. Copy source ──────────────────────────────────────────────────────────
COPY . /src/spectra
WORKDIR /src/spectra

# ─── 5. Configure — ALL features ON ──────────────────────────────────────────
# Remove any stale build artifacts that may have leaked through .dockerignore
RUN rm -rf build/CMakeCache.txt build/CMakeFiles

RUN . /opt/ros/jazzy/setup.sh && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=gcc-13 \
        -DCMAKE_CXX_COMPILER=g++-13 \
        \
        -DSPECTRA_USE_GLFW=ON \
        -DSPECTRA_USE_IMGUI=ON \
        -DSPECTRA_USE_FFMPEG=ON \
        -DSPECTRA_USE_EIGEN=ON \
        -DSPECTRA_USE_PX4=ON \
        -DSPECTRA_USE_ROS2=ON \
        -DSPECTRA_ROS2_BAG=ON \
        -DSPECTRA_USE_QT=ON \
        \
        -DSPECTRA_BUILD_EXAMPLES=ON \
        -DSPECTRA_BUILD_TESTS=ON \
        -DSPECTRA_BUILD_GOLDEN_TESTS=ON \
        -DSPECTRA_BUILD_BENCHMARKS=ON \
        -DSPECTRA_BUILD_QA_AGENT=ON \
        -DSPECTRA_BUILD_EMBED_SHARED=ON \
        -DSPECTRA_BUILD_QT_EXAMPLE=ON

# ─── 6. Build ────────────────────────────────────────────────────────────────
RUN . /opt/ros/jazzy/setup.sh && \
    cmake --build build -j"$(nproc)"

# ─── 7. Verify — non-GPU unit tests (must pass) ──────────────────────────────
RUN . /opt/ros/jazzy/setup.sh && \
    ctest --test-dir build -LE "gpu|golden" --output-on-failure -j"$(nproc)"

# ─── 8. Verify — GPU tests via lavapipe (best-effort, non-blocking) ──────────
RUN . /opt/ros/jazzy/setup.sh && \
    LVP_ICD="$(find /usr/share/vulkan/icd.d /etc/vulkan/icd.d 2>/dev/null \
        -name 'lvp_icd*.json' -o -name '*lavapipe*.json' | head -1)" && \
    if [ -z "$LVP_ICD" ]; then \
        LVP_ICD="$(grep -rl 'libvulkan_lvp' /usr/share/vulkan/icd.d 2>/dev/null | head -1)"; \
    fi && \
    if [ -n "$LVP_ICD" ]; then \
        export VK_ICD_FILENAMES="$LVP_ICD" VK_DRIVER_FILES="$LVP_ICD" LIBGL_ALWAYS_SOFTWARE=1 && \
        ctest --test-dir build -L "gpu" -LE "golden" --output-on-failure -j1 || \
        echo "WARNING: some GPU tests failed (lavapipe software rendering — non-fatal)"; \
    else \
        echo "WARNING: lavapipe ICD not found, skipping GPU tests"; \
    fi

# ─── 9. Expected build outputs ───────────────────────────────────────────────
#   build/libspectra.a               — full GPU library (Vulkan + UI + IPC + PX4)
#   build/libspectra-core.a          — headless core (no GPU, no ImGui)
#   build/libspectra_px4_adapter.a   — PX4/ULog adapter
#   build/libspectra_ros2_adapter.a  — ROS2 adapter + rosbag2
#   build/libspectra_embed.so        — shared library for C FFI / Python ctypes
#   build/spectra                    — main application
#   build/spectra-backend            — multi-process daemon
#   build/spectra-window             — multi-process window agent
#   build/spectra-px4                — standalone PX4 flight-log viewer
#   build/spectra-ros                — standalone ROS2 visualizer
#   build/spectra-ros-analyze        — headless bag CSV export tool
#   build/examples/*                 — ~47 runnable demo programs
#   build/tests/*                    — 110+ unit test binaries + golden tests
#   build/tests/bench_*              — performance benchmarks

ENV LIBGL_ALWAYS_SOFTWARE=1

ENTRYPOINT ["/entrypoint.sh"]
CMD ["/bin/bash"]
