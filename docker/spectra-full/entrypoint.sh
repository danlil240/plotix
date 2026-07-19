#!/bin/bash
set -e

# ─── Source ROS2 Jazzy ────────────────────────────────────────────────────────
if [ -f /opt/ros/jazzy/setup.bash ]; then
    source /opt/ros/jazzy/setup.bash
fi

# ─── Runtime directory for IPC sockets ────────────────────────────────────────
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/spectra-runtime}"
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null || true
chmod 1777 "$XDG_RUNTIME_DIR" 2>/dev/null || true

# ─── Vulkan: use lavapipe (software) unless GPU is available ──────────────────
if [ -z "$VK_ICD_FILENAMES" ] && [ -z "$VK_DRIVER_FILES" ]; then
    if [ -e /dev/dri/renderD128 ]; then
        echo "[entrypoint] GPU device detected — using hardware Vulkan"
    else
        LVP_ICD=""
        for d in /usr/share/vulkan/icd.d /etc/vulkan/icd.d; do
            [ -d "$d" ] || continue
            for f in "$d"/lvp_icd*.json "$d"/*lavapipe*.json; do
                [ -f "$f" ] || continue
                LVP_ICD="$f"; break 2
            done
        done
        if [ -z "$LVP_ICD" ]; then
            LVP_ICD="$(grep -rl 'libvulkan_lvp' /usr/share/vulkan/icd.d 2>/dev/null | head -1)"
        fi
        if [ -n "$LVP_ICD" ]; then
            export VK_ICD_FILENAMES="$LVP_ICD"
            export VK_DRIVER_FILES="$LVP_ICD"
            export LIBGL_ALWAYS_SOFTWARE=1
            echo "[entrypoint] Using lavapipe software Vulkan: $LVP_ICD"
        fi
    fi
fi

# ─── Pin the IPC socket to a fixed path for cross-container discovery ─────────
if [ -z "$SPECTRA_SOCKET" ]; then
    export SPECTRA_SOCKET="$XDG_RUNTIME_DIR/spectra-backend.sock"
fi

echo "[entrypoint] XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
echo "[entrypoint] SPECTRA_SOCKET=$SPECTRA_SOCKET"
echo "[entrypoint] ROS_DISTRO=${ROS_DISTRO:-none}"
echo "[entrypoint] DISPLAY=${DISPLAY:-none}"

exec "$@"
