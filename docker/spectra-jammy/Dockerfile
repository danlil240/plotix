# =============================================================================
# Spectra — Ubuntu 22.04 (Jammy) Package Builder
#
# Produces .deb packages for Ubuntu 22.04 with a pinned Qt 6.8.x runtime.
# Official packages must be built in this container to ensure glibc/ABI
# compatibility with the target baseline.
#
# Build:
#   docker build -f docker/spectra-jammy/Dockerfile -t spectra-jammy-builder .
# Run:
#   docker run --rm -v "$PWD:/src" -v "$PWD/build-jammy:/build" spectra-jammy-builder
# =============================================================================

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# ─── 1. Build dependencies ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    \
    # Vulkan
    libvulkan-dev \
    vulkan-tools \
    \
    # Window system
    libwayland-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev \
    libglfw3-dev \
    \
    # Qt 6 — from Ubuntu 22.04 universe (6.2) or PPA for 6.8
    # For production, a pinned Qt 6.8.x should be built from source or
    # installed from a Spectra-controlled PPA. This Dockerfile uses the
    # system Qt for development; the release pipeline overrides with the
    # pinned runtime.
    qt6-base-dev \
    \
    # Shader compiler
    glslang-tools \
    \
    # Python (for icon generation and tests)
    python3 \
    python3-pip \
    python3-pillow \
    \
    # Packaging tools
    dpkg-dev \
    fakeroot \
    && rm -rf /var/lib/apt/lists/*

# ─── 2. Build Spectra with Qt + private runtime packaging ───────────────────
WORKDIR /src

COPY . /src

RUN cmake -B /build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DSPECTRA_USE_QT=ON \
    -DSPECTRA_BUILD_QT_APP=ON \
    -DSPECTRA_BUILD_QT_TESTS=OFF \
    -DSPECTRA_BUILD_EXAMPLES=OFF \
    -DSPECTRA_BUILD_TESTS=OFF \
    -DSPECTRA_BUILD_GOLDEN_TESTS=OFF \
    -DSPECTRA_PACKAGE_PRIVATE_QT=ON \
    -DSPECTRA_DEFAULT_FRONTEND=qt \
    -DSPECTRA_USE_ROS2=OFF \
    -DSPECTRA_USE_PX4=OFF \
    -DSPECTRA_PKG_DISTRO_CODENAME=jammy \
    && cmake --build /build -j"$(nproc)"

# ─── 3. Produce .deb packages ───────────────────────────────────────────────
RUN cd /build \
    && cpack -G DEB \
    && ls -la /build/*.deb

# ─── 4. Output ──────────────────────────────────────────────────────────────
# Copy .deb files to a known location
RUN mkdir -p /packages && cp /build/*.deb /packages/ 2>/dev/null || true

CMD ["bash"]
