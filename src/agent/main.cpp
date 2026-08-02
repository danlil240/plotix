// spectra-window — Multi-process window agent.
//
// Uses the EXACT SAME UI stack as the in-process runtime (WindowManager,
// SessionRuntime, WindowRuntime, WindowUIContext, ImGui, full command set).
// Figures are populated from IPC snapshots instead of user code — that is
// the ONLY difference from app_inproc.  One line in CMake controls which
// mode is used.

#include <spectra/animator.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "ui/commands/command_queue.hpp"
#include <spectra/figure_registry.hpp>
#include "ui/overlay/knob_manager.hpp"
#include "ui/app/register_commands.hpp"
#include "ui/app/session_runtime.hpp"
#include "ui/app/window_runtime.hpp"
#include "ui/app/window_ui_context_builder.hpp"
#include "ui/app/window_ui_context_runtime.hpp"
#include "ui/app/window_manager_bootstrap.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/topics/topics_panel.hpp"
#endif

#include "ui/settings/settings_store.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "ui/window/glfw_adapter.hpp"
#endif

#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
    #include "ui/window/sdl3_adapter.hpp"
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#if __has_include(<spectra/version.hpp>)
    #include <spectra/version.hpp>
#endif
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
    #include <poll.h>
    #include <unistd.h>
#endif

namespace
{

std::atomic<bool> g_running{true};

void signal_handler(int /*sig*/)
{
    g_running.store(false, std::memory_order_relaxed);
}

// Helper: check if a series type string is a 3D type.
static bool is_3d_series_type(const std::string& t)
{
    return t == "line3d" || t == "scatter3d" || t == "surface" || t == "mesh";
}

// ─── Build a real Figure from a SnapshotFigureState ──────────────────────────
std::unique_ptr<spectra::Figure> build_figure_from_snapshot(
    const spectra::ipc::SnapshotFigureState& snap,
    uint32_t                                 override_width  = 0,
    uint32_t                                 override_height = 0)
{
    spectra::FigureConfig cfg;
    cfg.width  = (override_width > 0) ? override_width : snap.width;
    cfg.height = (override_height > 0) ? override_height : snap.height;
    auto fig   = std::make_unique<spectra::Figure>(cfg);

    int rows = std::max(snap.grid_rows, int32_t(1));
    int cols = std::max(snap.grid_cols, int32_t(1));

    size_t num_axes = std::max(snap.axes.size(), size_t(1));
    for (size_t i = 0; i < num_axes; ++i)
    {
        bool axes_is_3d = (i < snap.axes.size()) && snap.axes[i].is_3d;

        if (axes_is_3d)
        {
            auto&       ax3d = fig->subplot3d(rows, cols, static_cast<int>(i + 1));
            const auto& sa   = snap.axes[i];
            ax3d.xlim(sa.x_min, sa.x_max);
            ax3d.ylim(sa.y_min, sa.y_max);
            ax3d.zlim(sa.z_min, sa.z_max);
            ax3d.grid(sa.grid_visible);
            if (!sa.x_label.empty())
                ax3d.xlabel(sa.x_label);
            if (!sa.y_label.empty())
                ax3d.ylabel(sa.y_label);
            if (!sa.title.empty())
                ax3d.title(sa.title);

            // Add 3D series that belong to this axes
            for (const auto& ss : snap.series)
            {
                if (!is_3d_series_type(ss.type))
                    continue;
                if (ss.axes_index != static_cast<uint32_t>(i))
                    continue;

                // Unpack XYZ stride-3 data
                std::vector<float> xs;
                std::vector<float> ys;
                std::vector<float> zs;
                for (size_t j = 0; j + 2 < ss.data.size(); j += 3)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                    zs.push_back(ss.data[j + 2]);
                }

                if (ss.type == "scatter3d")
                {
                    auto& s = ax3d.scatter3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else if (ss.type == "surface")
                {
                    // SurfaceSeries expects 1D grid vectors (unique sorted X, Y)
                    // plus a rows*cols Z array.  The IPC data is raveled meshgrid
                    // (all X, all Y, all Z each of length rows*cols).
                    // Reconstruct 1D grids by extracting unique sorted values.
                    std::vector<float> ux(xs.begin(), xs.end());
                    std::vector<float> uy(ys.begin(), ys.end());
                    std::sort(ux.begin(), ux.end());
                    ux.erase(std::unique(ux.begin(),
                                         ux.end(),
                                         [](float a, float b) { return std::abs(a - b) < 1e-6f; }),
                             ux.end());
                    std::sort(uy.begin(), uy.end());
                    uy.erase(std::unique(uy.begin(),
                                         uy.end(),
                                         [](float a, float b) { return std::abs(a - b) < 1e-6f; }),
                             uy.end());

                    // Reorder Z into row-major (y-row, x-col) order expected by SurfaceSeries
                    size_t             ncols = ux.size();
                    size_t             nrows = uy.size();
                    std::vector<float> z_grid(nrows * ncols, 0.0f);
                    for (size_t k = 0; k < xs.size(); ++k)
                    {
                        // Find column index for xs[k]
                        auto cit = std::lower_bound(ux.begin(), ux.end(), xs[k] - 1e-6f);
                        auto ci  = static_cast<size_t>(std::distance(ux.begin(), cit));
                        if (ci >= ncols)
                            ci = ncols - 1;
                        // Find row index for ys[k]
                        auto rit = std::lower_bound(uy.begin(), uy.end(), ys[k] - 1e-6f);
                        auto ri  = static_cast<size_t>(std::distance(uy.begin(), rit));
                        if (ri >= nrows)
                            ri = nrows - 1;
                        z_grid[ri * ncols + ci] = zs[k];
                    }

                    auto& s = ax3d.surface(ux, uy, z_grid);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else if (ss.type == "mesh")
                {
                    // mesh expects vertices + indices; for now treat as line3d
                    auto& s = ax3d.line3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else   // "line3d"
                {
                    auto& s = ax3d.line3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
        else
        {
            auto& ax = fig->subplot(rows, cols, static_cast<int>(i + 1));
            if (i < snap.axes.size())
            {
                const auto& sa = snap.axes[i];
                ax.xlim(sa.x_min, sa.x_max);
                ax.ylim(sa.y_min, sa.y_max);
                ax.grid(sa.grid_visible);
                if (!sa.x_label.empty())
                    ax.xlabel(sa.x_label);
                if (!sa.y_label.empty())
                    ax.ylabel(sa.y_label);
                if (!sa.title.empty())
                    ax.title(sa.title);
            }

            // Add 2D series that belong to this axes
            for (const auto& ss : snap.series)
            {
                if (is_3d_series_type(ss.type))
                    continue;
                if (ss.axes_index != static_cast<uint32_t>(i))
                    continue;

                std::vector<float> xs;
                std::vector<float> ys;
                for (size_t j = 0; j + 1 < ss.data.size(); j += 2)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                }
                if (ss.type == "scatter")
                {
                    auto& s = ax.scatter(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else
                {
                    auto& s = ax.line(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
    }

    // Apply live streaming FPS from snapshot
    if (snap.live_fps > 0.0f)
    {
        fig->anim_.live_streaming = true;
        fig->anim_.fps            = snap.live_fps;
    }

    return fig;
}

// ─── Apply a DiffOp to a cached SnapshotFigureState ─────────────────────────
void apply_diff_op_to_cache(spectra::ipc::SnapshotFigureState& fig, const spectra::ipc::DiffOp& op)
{
    switch (op.type)
    {
        case spectra::ipc::DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].x_min = op.f1;
                fig.axes[op.axes_index].x_max = op.f2;
                fig.axes[op.axes_index].y_min = op.f3;
                fig.axes[op.axes_index].y_max = op.f4;
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_COLOR:
            if (op.series_index < fig.series.size())
            {
                fig.series[op.series_index].color_r = op.f1;
                fig.series[op.series_index].color_g = op.f2;
                fig.series[op.series_index].color_b = op.f3;
                fig.series[op.series_index].color_a = op.f4;
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_VISIBLE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].visible = op.bool_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_FIGURE_TITLE:
            fig.title = op.str_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].grid_visible = op.bool_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_LINE_WIDTH:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].line_width = op.f1;
            break;
        case spectra::ipc::DiffOp::Type::SET_MARKER_SIZE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].marker_size = op.f1;
            break;
        case spectra::ipc::DiffOp::Type::SET_OPACITY:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].opacity = op.f1;
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_DATA:
            if (op.series_index < fig.series.size())
            {
                fig.series[op.series_index].data        = op.data;
                fig.series[op.series_index].point_count = static_cast<uint32_t>(op.data.size() / 2);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].z_min = op.f1;
                fig.axes[op.axes_index].z_max = op.f2;
            }
            break;
        case spectra::ipc::DiffOp::Type::ADD_SERIES:
        {
            spectra::ipc::SnapshotSeriesState s;
            s.type       = op.str_val;
            s.axes_index = op.axes_index;
            // Grow series list to accommodate the new index
            while (fig.series.size() <= op.series_index)
                fig.series.push_back({});
            fig.series[op.series_index] = std::move(s);
            break;
        }
        case spectra::ipc::DiffOp::Type::ADD_AXES:
        {
            spectra::ipc::SnapshotAxisState ax;
            ax.is_3d = op.bool_val;
            // Grow axes list to accommodate the new index
            while (fig.axes.size() <= op.axes_index)
                fig.axes.push_back({});
            fig.axes[op.axes_index] = std::move(ax);
            break;
        }
        case spectra::ipc::DiffOp::Type::SET_SERIES_LABEL:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].name = op.str_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_XLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].x_label = op.str_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_YLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].y_label = op.str_val;
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_TITLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].title = op.str_val;
            break;
        default:
            // Includes SET_LIVE_FPS (handled on live Figure, not cache).
            break;
    }
}

// ─── Apply a DiffOp directly to a live Figure object ─────────────────────────
void apply_diff_op_to_figure(spectra::Figure& fig, const spectra::ipc::DiffOp& op)
{
    switch (op.type)
    {
        case spectra::ipc::DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                fig.axes_mut()[op.axes_index]->xlim(op.f1, op.f2);
                fig.axes_mut()[op.axes_index]->ylim(op.f3, op.f4);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                fig.axes_mut()[op.axes_index]->grid(op.bool_val);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(fig.axes_mut()[op.axes_index].get());
                if (ax3d)
                    ax3d->zlim(op.f1, op.f2);
            }
            break;
        case spectra::ipc::DiffOp::Type::ADD_SERIES:
            // Series will be populated by the subsequent SET_SERIES_DATA diff.
            // Add a placeholder so the series_index slot exists in the live figure.
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax   = fig.axes_mut()[op.axes_index].get();
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(ax);
                if (ax3d)
                {
                    if (op.str_val == "scatter3d")
                        ax3d->scatter3d({}, {}, {});
                    else if (op.str_val == "surface")
                        ax3d->surface({}, {}, {});
                    else
                        ax3d->line3d({}, {}, {});
                }
                else
                {
                    if (op.str_val == "scatter")
                        ax->scatter({}, {});
                    else
                        ax->line({}, {});
                }
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_DATA:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto& series_vec = fig.axes_mut()[op.axes_index]->series_mut();
                if (op.series_index < series_vec.size() && series_vec[op.series_index])
                {
                    auto* s = series_vec[op.series_index].get();
                    if (auto* line3d = dynamic_cast<spectra::LineSeries3D*>(s))
                    {
                        size_t             n = op.data.size() / 3;
                        std::vector<float> xv(n);
                        std::vector<float> yv(n);
                        std::vector<float> zv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 3];
                            yv[i] = op.data[i * 3 + 1];
                            zv[i] = op.data[i * 3 + 2];
                        }
                        line3d->set_x(xv);
                        line3d->set_y(yv);
                        line3d->set_z(zv);
                    }
                    else if (auto* scatter3d = dynamic_cast<spectra::ScatterSeries3D*>(s))
                    {
                        size_t             n = op.data.size() / 3;
                        std::vector<float> xv(n);
                        std::vector<float> yv(n);
                        std::vector<float> zv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 3];
                            yv[i] = op.data[i * 3 + 1];
                            zv[i] = op.data[i * 3 + 2];
                        }
                        scatter3d->set_x(xv);
                        scatter3d->set_y(yv);
                        scatter3d->set_z(zv);
                    }
                    else if (auto* line = dynamic_cast<spectra::LineSeries*>(s))
                    {
                        size_t             n = op.data.size() / 2;
                        std::vector<float> xv(n);
                        std::vector<float> yv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 2];
                            yv[i] = op.data[i * 2 + 1];
                        }
                        line->set_x(xv);
                        line->set_y(yv);
                    }
                    else if (auto* scatter = dynamic_cast<spectra::ScatterSeries*>(s))
                    {
                        size_t             n = op.data.size() / 2;
                        std::vector<float> xv(n);
                        std::vector<float> yv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 2];
                            yv[i] = op.data[i * 2 + 1];
                        }
                        scatter->set_x(xv);
                        scatter->set_y(yv);
                    }
                }
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_LABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto& series_vec = fig.axes_mut()[op.axes_index]->series_mut();
                if (op.series_index < series_vec.size() && series_vec[op.series_index])
                    series_vec[op.series_index]->label(op.str_val);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_XLABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->xlabel(op.str_val);
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_YLABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->ylabel(op.str_val);
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_TITLE:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->title(op.str_val);
            break;
        case spectra::ipc::DiffOp::Type::SET_LIVE_FPS:
            fig.anim_.live_streaming = op.bool_val;
            if (op.f1 > 0.0)
                fig.anim_.fps = static_cast<float>(op.f1);
            break;
        default:
            break;
    }
}

// ─── Send an IPC message helper ──────────────────────────────────────────────
bool send_ipc(spectra::ipc::Connection& conn,
              spectra::ipc::MessageType type,
              spectra::ipc::SessionId   session_id,
              spectra::ipc::WindowId    window_id,
              std::vector<uint8_t>      payload = {})
{
    spectra::ipc::Message msg;
    msg.header.type        = type;
    msg.header.session_id  = session_id;
    msg.header.window_id   = window_id;
    msg.payload            = std::move(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// ─── Rebuild FigureRegistry from IPC cache ───────────────────────────────────
// Re-creates Figure objects from snapshot cache and registers them.
// Returns the list of new FigureId values.
std::vector<spectra::FigureId> rebuild_registry_from_cache(
    spectra::FigureRegistry&                              registry,
    const std::vector<spectra::ipc::SnapshotFigureState>& cache,
    uint32_t                                              width,
    uint32_t                                              height)
{
    // Clear existing figures
    for (auto id : registry.all_ids())
        registry.unregister_figure(id);

    std::vector<spectra::FigureId> ids;
    for (const auto& snap : cache)
    {
        auto fig = build_figure_from_snapshot(snap, width, height);
        auto id  = registry.register_figure(std::move(fig));
        ids.push_back(id);
    }
    return ids;
}

}   // namespace

int main(int argc, char* argv[])
{
    // Handle --version and --help before anything else
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)
        {
#ifdef SPECTRA_VERSION_STRING
            std::cout << "spectra-window " << SPECTRA_VERSION_STRING << "\n";
#else
            std::cout << "spectra-window (version unknown)\n";
#endif
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            std::cout << "Usage: spectra-window [OPTIONS]\n"
                      << "\n"
                      << "Options:\n"
                      << "  --socket <path>  Unix socket path to connect to\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

    // Parse --socket <path> argument
    std::string socket_path;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::string(argv[i]) == "--socket")
        {
            socket_path = argv[i + 1];
            break;
        }
    }
    if (socket_path.empty())
    {
        SPECTRA_LOG_ERROR("window", "--socket <path> required");
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Initialize dual logging: console=INFO+, file=TRACE+
    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

    SPECTRA_LOG_INFO("window", "Connecting to backend: {}", socket_path);

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 1: IPC connection + handshake
    // ═══════════════════════════════════════════════════════════════════════

    auto conn = spectra::ipc::Client::connect(socket_path);
    if (!conn)
    {
        SPECTRA_LOG_ERROR("window", "Failed to connect to {}", socket_path);
        return 1;
    }

    SPECTRA_LOG_INFO("window", "Connected to backend");
    SPECTRA_LOG_DEBUG("window", "Backend connection fd={}", conn->fd());

    // Send HELLO
    spectra::ipc::HelloPayload hello;
    hello.protocol_major = spectra::ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = spectra::ipc::PROTOCOL_MINOR;
    hello.agent_build    = "spectra-window/0.1.0";
    hello.capabilities   = 0;
    {
        spectra::ipc::Message hello_msg;
        hello_msg.header.type        = spectra::ipc::MessageType::HELLO;
        hello_msg.payload            = spectra::ipc::encode_hello(hello);
        hello_msg.header.payload_len = static_cast<uint32_t>(hello_msg.payload.size());
        if (!conn->send(hello_msg))
        {
            SPECTRA_LOG_ERROR("window", "Failed to send HELLO");
            return 1;
        }
    }

    // Receive WELCOME
    auto welcome_msg = conn->recv();
    if (!welcome_msg || welcome_msg->header.type != spectra::ipc::MessageType::WELCOME)
    {
        SPECTRA_LOG_ERROR("window", "Did not receive WELCOME");
        return 1;
    }
    auto welcome = spectra::ipc::decode_welcome(welcome_msg->payload);
    if (!welcome)
    {
        SPECTRA_LOG_ERROR("window", "Failed to decode WELCOME");
        return 1;
    }

    spectra::ipc::SessionId session_id    = welcome->session_id;
    spectra::ipc::WindowId  ipc_window_id = welcome->window_id;
    uint32_t                heartbeat_ms  = welcome->heartbeat_ms;

    SPECTRA_LOG_INFO("window",
                     "WELCOME: session={} window={} heartbeat={}ms",
                     session_id,
                     ipc_window_id,
                     heartbeat_ms);

    // Track IPC state
    std::vector<uint64_t>                          assigned_figures;
    uint64_t                                       ipc_active_figure_id = 0;
    std::vector<spectra::ipc::SnapshotFigureState> figure_cache;
    std::vector<spectra::ipc::SnapshotKnobState>   knob_cache;
    spectra::ipc::Revision                         current_revision = 0;
    bool                                           cache_dirty      = false;

    // Drain initial messages (CMD_ASSIGN_FIGURES + STATE_SNAPSHOT)
    {
        auto deadline     = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool got_snapshot = false;
        while (!got_snapshot && std::chrono::steady_clock::now() < deadline)
        {
#ifndef _WIN32
            struct pollfd pfd
            {
            };
            pfd.fd      = conn->fd();
            pfd.events  = POLLIN;
            pfd.revents = 0;
            if (::poll(&pfd, 1, 100) <= 0 || !(pfd.revents & POLLIN))
                continue;
#endif
            auto msg_opt = conn->recv();
            if (!msg_opt)
                break;
            auto& msg = *msg_opt;
            if (msg.header.type == spectra::ipc::MessageType::CMD_ASSIGN_FIGURES)
            {
                auto payload = spectra::ipc::decode_cmd_assign_figures(msg.payload);
                if (payload)
                {
                    assigned_figures     = payload->figure_ids;
                    ipc_active_figure_id = payload->active_figure_id;
                }
            }
            else if (msg.header.type == spectra::ipc::MessageType::STATE_SNAPSHOT)
            {
                auto snap = spectra::ipc::decode_state_snapshot(msg.payload);
                if (snap)
                {
                    figure_cache     = snap->figures;
                    knob_cache       = snap->knobs;
                    current_revision = snap->revision;
                    cache_dirty      = true;
                    got_snapshot     = true;

                    spectra::ipc::AckStatePayload ack;
                    ack.revision = current_revision;
                    send_ipc(*conn,
                             spectra::ipc::MessageType::ACK_STATE,
                             session_id,
                             ipc_window_id,
                             spectra::ipc::encode_ack_state(ack));

                    SPECTRA_LOG_DEBUG("window",
                                      "STATE_SNAPSHOT (init): rev={} figures={}",
                                      current_revision,
                                      figure_cache.size());
                }
            }
        }
        if (!got_snapshot)
            SPECTRA_LOG_WARN("window", "No STATE_SNAPSHOT received");
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 2: Build figures into FigureRegistry
    // ═══════════════════════════════════════════════════════════════════════

    constexpr uint32_t INITIAL_WIDTH  = 1280;
    constexpr uint32_t INITIAL_HEIGHT = 720;

    spectra::FigureRegistry registry;
    auto                    all_ids =
        rebuild_registry_from_cache(registry, figure_cache, INITIAL_WIDTH, INITIAL_HEIGHT);
    cache_dirty = false;

    // If the daemon has no figures yet (e.g. a publisher launched it before
    // any UI was opened), we run with an empty registry and let the shared
    // window runtime render the branded welcome screen (build_empty_ui).
    // The user can drag a topic onto the canvas, or create a figure from the
    // menu, and the daemon will push it back via STATE_DIFF.
    if (registry.count() == 0)
        SPECTRA_LOG_INFO("window", "Daemon has no figures yet — opening welcome window");

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 3: Initialize GPU + WindowManager + SessionRuntime
    //          (identical to App::run_inproc)
    // ═══════════════════════════════════════════════════════════════════════

    spectra::FrameState frame_state;
    {
        // Pick the correct initial active figure based on IPC assignment
        spectra::FigureId initial_active =
            all_ids.empty() ? spectra::INVALID_FIGURE_ID : all_ids[0];
        if (ipc_active_figure_id != 0)
        {
            for (size_t i = 0; i < assigned_figures.size() && i < all_ids.size(); ++i)
            {
                if (assigned_figures[i] == ipc_active_figure_id)
                {
                    initial_active = all_ids[i];
                    break;
                }
            }
        }
        frame_state.active_figure_id = initial_active;
    }
    frame_state.active_figure           = registry.get(frame_state.active_figure_id);
    spectra::Figure*   active_figure    = frame_state.active_figure;
    spectra::FigureId& active_figure_id = frame_state.active_figure_id;

    auto backend = std::make_unique<spectra::VulkanBackend>();
    if (!backend->init(false))
    {
        SPECTRA_LOG_ERROR("window", "Failed to initialize Vulkan backend");
        return 1;
    }

    // Agent-owned ThemeManager (no App in multiproc mode).
    // Register it as the active instance so ThemeManager::instance() returns
    // this object for all subsystems (register_commands, renderers, overlays).
    spectra::ui::ThemeManager theme_mgr;
    spectra::ui::ThemeManager::set_current(&theme_mgr);

    spectra::ui::settings::SettingsStore settings_store;
    settings_store.load();
    settings_store.apply_to(theme_mgr);

    auto renderer_ptr = std::make_unique<spectra::Renderer>(*backend, theme_mgr);
    if (!renderer_ptr->init())
    {
        SPECTRA_LOG_ERROR("window", "Failed to initialize renderer");
        return 1;
    }

    spectra::CommandQueue   cmd_queue;
    spectra::FrameScheduler scheduler(active_figure ? active_figure->anim_fps() : 60.0f);
    // Windowed agent uses VK_PRESENT_MODE_FIFO_KHR (VSync) — don't
    // double-pace with FrameScheduler sleep on top.
    scheduler.set_mode(spectra::FrameScheduler::Mode::VSync);
    spectra::Animator       animator;
    spectra::SessionRuntime session(*backend, *renderer_ptr, registry);

    frame_state.has_animation = active_figure ? active_figure->has_animation() : false;

    spectra::WindowUIContext* ui_ctx_ptr          = nullptr;
    spectra::WindowContext*   initial_wctx        = nullptr;
    bool                      ui_built_by_builder = false;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<spectra::GlfwAdapter>   glfw;
    std::unique_ptr<spectra::WindowManager> window_mgr;

    glfw               = std::make_unique<spectra::GlfwAdapter>();
    uint32_t initial_w = active_figure ? active_figure->width() : INITIAL_WIDTH;
    uint32_t initial_h = active_figure ? active_figure->height() : INITIAL_HEIGHT;
    if (!glfw->init(initial_w, initial_h, "Spectra"))
    {
        SPECTRA_LOG_ERROR("window", "Failed to create GLFW window");
        return 1;
    }

    backend->create_surface(glfw->native_window());
    backend->create_swapchain(initial_w, initial_h);

    {
        spectra::WindowManagerBootstrapOptions wm_opts;
        wm_opts.backend        = static_cast<spectra::VulkanBackend*>(backend.get());
        wm_opts.registry       = &registry;
        wm_opts.renderer       = renderer_ptr.get();
        wm_opts.theme_mgr      = &theme_mgr;
        wm_opts.session        = &session;
        wm_opts.settings_store = &settings_store;
        window_mgr             = spectra::create_configured_window_manager(wm_opts);
    }

    window_mgr->set_interactive_frame_handler(
        [&session, &scheduler, &animator, &window_mgr, &frame_state]()
        { session.pump_interactive_frame(scheduler, animator, window_mgr.get(), frame_state); });

    initial_wctx = window_mgr->create_first_window_with_ui(glfw->native_window(), all_ids);
#elif defined(SPECTRA_USE_SDL3)
    std::unique_ptr<spectra::Sdl3Adapter>   sdl3;
    std::unique_ptr<spectra::WindowManager> window_mgr;

    sdl3               = std::make_unique<spectra::Sdl3Adapter>();
    uint32_t initial_w = active_figure ? active_figure->width() : INITIAL_WIDTH;
    uint32_t initial_h = active_figure ? active_figure->height() : INITIAL_HEIGHT;
    if (!sdl3->init(initial_w, initial_h, "Spectra"))
    {
        SPECTRA_LOG_ERROR("window", "Failed to create SDL3 window");
        return 1;
    }

    backend->create_surface(sdl3->native_window());
    backend->create_swapchain(initial_w, initial_h);

    {
        spectra::WindowManagerBootstrapOptions wm_opts;
        wm_opts.backend        = static_cast<spectra::VulkanBackend*>(backend.get());
        wm_opts.registry       = &registry;
        wm_opts.renderer       = renderer_ptr.get();
        wm_opts.theme_mgr      = &theme_mgr;
        wm_opts.session        = &session;
        wm_opts.settings_store = &settings_store;
        window_mgr             = spectra::create_configured_window_manager(wm_opts);
    }

    initial_wctx = window_mgr->create_first_window_with_ui(sdl3->native_window(), all_ids);
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (initial_wctx && initial_wctx->ui_ctx)
    {
        ui_built_by_builder = true;
        ui_ctx_ptr          = initial_wctx->ui_ctx.get();
        // Set tab titles from snapshot cache (so tabs show "Figure 1", "Figure 2", etc.
        // instead of FigureId-based defaults like "Figure 2", "Figure 3", "Figure 4")
        if (ui_ctx_ptr->fig_mgr)
        {
            for (size_t fi = 0; fi < all_ids.size() && fi < figure_cache.size(); ++fi)
            {
                if (!figure_cache[fi].title.empty())
                    ui_ctx_ptr->fig_mgr->set_title(all_ids[fi], figure_cache[fi].title);
            }
            // Switch to the correct initial active figure
            ui_ctx_ptr->fig_mgr->switch_to(frame_state.active_figure_id);
        }

        // Sync WindowContext active figure
        initial_wctx->active_figure_id = frame_state.active_figure_id;

        // Reconstruct knobs from IPC cache into the window's KnobManager
        if (!knob_cache.empty())
        {
            auto& km = ui_ctx_ptr->knob_manager;
            for (const auto& ks : knob_cache)
            {
                switch (ks.type)
                {
                    case 0:   // Float
                        km.add_float(ks.name, ks.value, ks.min_val, ks.max_val, ks.step);
                        break;
                    case 1:   // Int
                        km.add_int(ks.name,
                                   static_cast<int>(ks.value),
                                   static_cast<int>(ks.min_val),
                                   static_cast<int>(ks.max_val));
                        break;
                    case 2:   // Bool
                        km.add_bool(ks.name, ks.value >= 0.5f);
                        break;
                    case 3:   // Choice
                        km.add_choice(ks.name, ks.choices, static_cast<int>(ks.value));
                        break;
                }
            }
        }

        // Pre-create a hidden preview window so tab tearoff is instant.
        window_mgr->warmup_preview_window();
    }

#endif

    // Headless fallback (no OS window — e.g. builds without GLFW/SDL3)
    std::unique_ptr<spectra::WindowUIContext> headless_ui_ctx;
    if (!ui_ctx_ptr)
    {
        spectra::WindowUIContextBuildOptions headless_opts;
        headless_opts.registry       = &registry;
        headless_opts.theme_mgr      = &theme_mgr;
        headless_opts.mode           = spectra::WindowUIContextBuildMode::Headless;
        headless_opts.settings_store = &settings_store;
        headless_ui_ctx              = spectra::build_window_ui_context(headless_opts);
        ui_ctx_ptr                   = headless_ui_ctx.get();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 4: Wire UI subsystems + register commands
    //          (identical to App::run_inproc)
    // ═══════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_USE_IMGUI
    if (ui_ctx_ptr && ui_ctx_ptr->fig_mgr)
    {
        spectra::WindowUIContextRuntimeWireOptions wire_opts;
        wire_opts.ui_ctx                       = ui_ctx_ptr;
        wire_opts.registry                     = &registry;
        wire_opts.session                      = &session;
        wire_opts.active_figure                = active_figure;
        wire_opts.has_animation                = frame_state.has_animation;
        wire_opts.tab_split_mode               = spectra::TabSplitMode::DuplicateThenSplit;
        wire_opts.tab_drag_already_wired       = ui_built_by_builder;
        wire_opts.wire_demo_animation_channels = false;
        wire_opts.enable_window_tab_callbacks  = true;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        wire_opts.window_manager = window_mgr.get();
    #endif
        spectra::wire_window_ui_runtime(wire_opts);
    }

    if (!ui_built_by_builder && ui_ctx_ptr)
    {
        auto& shortcut_mgr = ui_ctx_ptr->shortcut_mgr;
        auto& cmd_palette  = ui_ctx_ptr->cmd_palette;
        auto& cmd_registry = ui_ctx_ptr->cmd_registry;

        shortcut_mgr.set_command_registry(&cmd_registry);
        shortcut_mgr.register_defaults();
        cmd_palette.set_command_registry(&cmd_registry);
        cmd_palette.set_shortcut_manager(&shortcut_mgr);

        spectra::CommandBindings cb;
        cb.ui_ctx           = ui_ctx_ptr;
        cb.registry         = &registry;
        cb.active_figure    = &active_figure;
        cb.active_figure_id = &active_figure_id;
        cb.session          = &session;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        cb.window_mgr = window_mgr.get();
    #endif
        spectra::register_standard_commands(cb);
    }
#endif

    scheduler.reset();

    SPECTRA_LOG_INFO("window", "Full UI initialized, entering main loop");

    // ── Wire the Topics panel to IPC (Phase 2 of SPECTRA_TOPICS_PLAN). ──
#ifdef SPECTRA_USE_IMGUI
    std::mutex                                         pending_topic_subs_mu;
    std::vector<spectra::ui::topics::SubscribeRequest> pending_topic_subs;
    std::atomic<bool>                                  pending_topic_list_request{true};
    uint32_t                                           next_topic_req_id = 1;

    if (ui_ctx_ptr)
    {
        auto& panel = ui_ctx_ptr->topics_panel;
        panel.set_list_request_callback(
            [&pending_topic_list_request]()
            { pending_topic_list_request.store(true, std::memory_order_relaxed); });
        panel.set_subscribe_request_callback(
            [&pending_topic_subs_mu,
             &pending_topic_subs](const spectra::ui::topics::SubscribeRequest& req)
            {
                std::lock_guard<std::mutex> g(pending_topic_subs_mu);
                pending_topic_subs.push_back(req);
            });
    }
#endif

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 5: Main loop — SessionRuntime + IPC polling
    // ═══════════════════════════════════════════════════════════════════════

    auto last_heartbeat     = std::chrono::steady_clock::now();
    auto heartbeat_interval = std::chrono::milliseconds(heartbeat_ms);

    while (!session.should_exit() && g_running.load(std::memory_order_relaxed))
    {
        // ── Drain all pending IPC messages (non-blocking) ────────────────
#ifndef _WIN32
        for (;;)
        {
            struct pollfd pfd
            {
            };
            pfd.fd       = conn->fd();
            pfd.events   = POLLIN;
            pfd.revents  = 0;
            int poll_ret = ::poll(&pfd, 1, 0);   // non-blocking
            if (poll_ret > 0 && (pfd.revents & (POLLHUP | POLLERR)))
            {
                SPECTRA_LOG_WARN("window", "Backend connection lost");
                session.request_exit();
                break;
            }
            if (poll_ret <= 0 || !(pfd.revents & POLLIN))
                break;

            auto msg_opt = conn->recv();
            if (!msg_opt)
            {
                SPECTRA_LOG_WARN("window", "Connection to backend lost");
                session.request_exit();
                break;
            }

            auto& msg = *msg_opt;
            switch (msg.header.type)
            {
                case spectra::ipc::MessageType::CMD_ASSIGN_FIGURES:
                {
                    auto payload = spectra::ipc::decode_cmd_assign_figures(msg.payload);
                    if (payload)
                    {
                        assigned_figures     = payload->figure_ids;
                        ipc_active_figure_id = payload->active_figure_id;
                    }
                    break;
                }
                case spectra::ipc::MessageType::CMD_CLOSE_WINDOW:
                    SPECTRA_LOG_DEBUG("window", "CMD_CLOSE_WINDOW");
                    session.request_exit();
                    break;

                case spectra::ipc::MessageType::STATE_SNAPSHOT:
                {
                    auto snap = spectra::ipc::decode_state_snapshot(msg.payload);
                    if (snap)
                    {
                        figure_cache     = snap->figures;
                        current_revision = snap->revision;
                        cache_dirty      = true;

                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        send_ipc(*conn,
                                 spectra::ipc::MessageType::ACK_STATE,
                                 session_id,
                                 ipc_window_id,
                                 spectra::ipc::encode_ack_state(ack));
                    }
                    break;
                }

                case spectra::ipc::MessageType::STATE_DIFF:
                {
                    auto diff = spectra::ipc::decode_state_diff(msg.payload);
                    if (diff)
                    {
                        bool needs_rebuild = false;
                        for (const auto& op : diff->ops)
                        {
                            for (auto& fig : figure_cache)
                            {
                                if (fig.figure_id == op.figure_id)
                                {
                                    apply_diff_op_to_cache(fig, op);
                                    break;
                                }
                            }
                            // Structural changes require a full rebuild
                            if (op.type == spectra::ipc::DiffOp::Type::ADD_SERIES
                                || op.type == spectra::ipc::DiffOp::Type::ADD_AXES)
                            {
                                needs_rebuild = true;
                            }
                            else
                            {
                                // Apply directly to the matching live Figure object
                                // (fast path for axis limits, grid toggle, series data).
                                for (size_t mi = 0;
                                     mi < assigned_figures.size() && mi < all_ids.size();
                                     ++mi)
                                {
                                    if (assigned_figures[mi] == op.figure_id)
                                    {
                                        auto* live_fig = registry.get(all_ids[mi]);
                                        if (live_fig)
                                            apply_diff_op_to_figure(*live_fig, op);
                                        break;
                                    }
                                }
                            }
                        }
                        current_revision = diff->new_revision;
                        if (needs_rebuild)
                            cache_dirty = true;

                        // Signal the redraw tracker so the event loop wakes
                        // from glfwWaitEventsTimeout and renders the new data.
                        session.redraw_tracker().mark_dirty("ipc_state_diff");

                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        send_ipc(*conn,
                                 spectra::ipc::MessageType::ACK_STATE,
                                 session_id,
                                 ipc_window_id,
                                 spectra::ipc::encode_ack_state(ack));
                    }
                    break;
                }

                case spectra::ipc::MessageType::RESP_OK:
                case spectra::ipc::MessageType::RESP_ERR:
                    break;

    #ifdef SPECTRA_USE_IMGUI
                case spectra::ipc::MessageType::RESP_TOPIC_LIST:
                {
                    auto resp = spectra::ipc::decode_resp_topic_list(msg.payload);
                    if (resp && ui_ctx_ptr)
                        ui_ctx_ptr->topics_panel.set_topics(std::move(resp->topics));
                    break;
                }
                case spectra::ipc::MessageType::EVT_TOPIC_LIST_CHANGED:
                {
                    pending_topic_list_request.store(true, std::memory_order_relaxed);
                    break;
                }
    #endif

                default:
                    // Includes RESP_SUBSCRIBE_TOPIC (series arrives via STATE_DIFF).
                    break;
            }
        }
#endif

#ifdef SPECTRA_USE_IMGUI
        // ── Drain pending Topics-panel IPC requests ──────────────────────
        if (ui_ctx_ptr)
        {
            // Keep the panel pointed at the currently active figure.  This
            // is the figure id the daemon expects in REQ_SUBSCRIBE_TOPIC.
            // Map the per-window active figure (registry id) back to the
            // IPC figure id by index into assigned_figures.
            uint64_t target_ipc_fig = ipc_active_figure_id;
            for (size_t i = 0; i < all_ids.size() && i < assigned_figures.size(); ++i)
            {
                if (all_ids[i] == frame_state.active_figure_id)
                {
                    target_ipc_fig = assigned_figures[i];
                    break;
                }
            }
            ui_ctx_ptr->topics_panel.set_target_figure_id(target_ipc_fig);

            // 1. Periodic / event-driven list refresh.
            if (pending_topic_list_request.exchange(false, std::memory_order_relaxed))
            {
                spectra::ipc::ReqListTopicsPayload p;
                spectra::ipc::Message              m;
                m.header.type        = spectra::ipc::MessageType::REQ_LIST_TOPICS;
                m.header.session_id  = session_id;
                m.header.window_id   = ipc_window_id;
                m.header.request_id  = next_topic_req_id++;
                m.payload            = spectra::ipc::encode_req_list_topics(p);
                m.header.payload_len = static_cast<uint32_t>(m.payload.size());
                conn->send(m);
            }

            // 2. Subscribe requests committed by panel UI.
            std::vector<spectra::ui::topics::SubscribeRequest> subs;
            {
                std::lock_guard<std::mutex> g(pending_topic_subs_mu);
                subs.swap(pending_topic_subs);
            }
            for (const auto& s : subs)
            {
                spectra::ipc::ReqSubscribeTopicPayload p;
                p.name         = s.topic_name;
                p.figure_id    = s.figure_id;
                p.axes_index   = s.axes_index;
                p.series_index = s.series_index;

                spectra::ipc::Message m;
                m.header.type        = spectra::ipc::MessageType::REQ_SUBSCRIBE_TOPIC;
                m.header.session_id  = session_id;
                m.header.window_id   = ipc_window_id;
                m.header.request_id  = next_topic_req_id++;
                m.payload            = spectra::ipc::encode_req_subscribe_topic(p);
                m.header.payload_len = static_cast<uint32_t>(m.payload.size());
                conn->send(m);
            }
        }
#endif

        // ── Apply full rebuild if snapshot changed ───────────────────────
        if (cache_dirty)
        {
            uint32_t sw = backend->swapchain_width();
            uint32_t sh = backend->swapchain_height();
            all_ids     = rebuild_registry_from_cache(registry, figure_cache, sw, sh);
            if (!all_ids.empty())
            {
                // Use ipc_active_figure_id to find the correct figure.
                // The IPC figure IDs don't match registry IDs (registry
                // assigns new IDs), so match by index in assigned_figures.
                spectra::FigureId target_id = all_ids[0];
                if (ipc_active_figure_id != 0)
                {
                    for (size_t i = 0; i < assigned_figures.size() && i < all_ids.size(); ++i)
                    {
                        if (assigned_figures[i] == ipc_active_figure_id)
                        {
                            target_id = all_ids[i];
                            break;
                        }
                    }
                }
                frame_state.active_figure_id = target_id;
                frame_state.active_figure    = registry.get(target_id);
                active_figure                = frame_state.active_figure;

                // Sync FigureManager so tab bar reflects the new figures
#ifdef SPECTRA_USE_IMGUI
                if (ui_ctx_ptr && ui_ctx_ptr->fig_mgr)
                {
                    auto* fm = ui_ctx_ptr->fig_mgr;
                    // Remove stale figures from FigureManager
                    auto old_ids = fm->figure_ids();
                    for (auto old_id : old_ids)
                    {
                        if (std::find(all_ids.begin(), all_ids.end(), old_id) == all_ids.end())
                            fm->remove_figure(old_id);
                    }
                    // Add new figures that aren't in FigureManager yet
                    for (size_t fi = 0; fi < all_ids.size(); ++fi)
                    {
                        auto new_id = all_ids[fi];
                        if (std::find(old_ids.begin(), old_ids.end(), new_id) == old_ids.end())
                        {
                            spectra::FigureState st;
                            if (fi < figure_cache.size() && !figure_cache[fi].title.empty())
                                st.set_custom_title(figure_cache[fi].title);
                            fm->add_figure(new_id, std::move(st));
                        }
                    }
                    // Set titles from snapshot for existing figures too
                    for (size_t fi = 0; fi < all_ids.size() && fi < figure_cache.size(); ++fi)
                    {
                        if (!figure_cache[fi].title.empty())
                            fm->set_title(all_ids[fi], figure_cache[fi].title);
                    }
                    // Switch to the target figure
                    fm->switch_to(target_id);
                }
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                // Sync WindowContext
                if (window_mgr && !window_mgr->windows().empty())
                {
                    auto* wctx = window_mgr->windows()[0];
                    wctx->assigned_figures.clear();
                    for (auto id : all_ids)
                        wctx->assigned_figures.push_back(id);
                    wctx->active_figure_id = target_id;
                }
#endif

                // CRITICAL: rebuild_registry_from_cache destroys the previous
                // Figure objects.  Any UI subsystem holding raw Figure*/Axes*
                // pointers (input_handler in particular, used by the topics
                // drop target's hit-test) must be re-pointed at the new live
                // figure or it will dereference freed memory on the next
                // interaction.  This was the crash on second topic drop.
#ifdef SPECTRA_USE_IMGUI
                if (ui_ctx_ptr && frame_state.active_figure)
                {
                    ui_ctx_ptr->input_handler.set_figure(frame_state.active_figure);
                    if (!frame_state.active_figure->axes().empty()
                        && frame_state.active_figure->axes()[0])
                    {
                        ui_ctx_ptr->input_handler.set_active_axes(
                            frame_state.active_figure->axes()[0].get());
                        const auto& vp = frame_state.active_figure->axes()[0]->viewport();
                        ui_ctx_ptr->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                    }
                    else
                    {
                        ui_ctx_ptr->input_handler.set_active_axes(nullptr);
                    }
                }
#endif
            }
            cache_dirty = false;
            session.redraw_tracker().mark_dirty("ipc_cache_rebuild");
        }

        // ── Flush knob changes back to app via IPC ─────────────────────
        if (ui_ctx_ptr)
        {
            auto changes = ui_ctx_ptr->knob_manager.take_pending_changes();
            if (!changes.empty())
            {
                spectra::ipc::StateDiffPayload diff;
                for (auto& [name, val] : changes)
                {
                    spectra::ipc::DiffOp op;
                    op.type    = spectra::ipc::DiffOp::Type::SET_KNOB_VALUE;
                    op.str_val = name;
                    op.f1      = val;
                    diff.ops.push_back(std::move(op));
                }
                send_ipc(*conn,
                         spectra::ipc::MessageType::STATE_DIFF,
                         session_id,
                         ipc_window_id,
                         spectra::ipc::encode_state_diff(diff));
            }
        }

        // ── Send heartbeat ───────────────────────────────────────────────
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= heartbeat_interval)
        {
            if (!send_ipc(*conn,
                          spectra::ipc::MessageType::EVT_HEARTBEAT,
                          session_id,
                          ipc_window_id))
            {
                SPECTRA_LOG_WARN("window", "Lost connection to backend");
                session.request_exit();
                break;
            }
            last_heartbeat = now;
        }

        // ── Standard session tick (same as inproc) ───────────────────────
        session.tick(scheduler,
                     animator,
                     cmd_queue,
                     false,
                     ui_ctx_ptr,
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                     window_mgr.get(),
#endif
                     frame_state);
        active_figure = frame_state.active_figure;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        // Auto-exit when the last window has been closed by the user.
        // Without this, closing the window via the X button leaves the agent
        // process alive (with no windows), the daemon never sees a disconnect,
        // and Python clients waiting on EVT_WINDOW_CLOSED hang forever.
        if (window_mgr && window_mgr->windows().empty())
        {
            SPECTRA_LOG_INFO("window", "All windows closed — exiting");
            session.request_exit();
        }
#endif
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 6: Clean shutdown
    // ═══════════════════════════════════════════════════════════════════════

    SPECTRA_LOG_INFO("window", "Shutting down");

    // Notify backend
    send_ipc(*conn, spectra::ipc::MessageType::EVT_WINDOW, session_id, ipc_window_id);

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (window_mgr)
    {
    #ifdef SPECTRA_USE_GLFW
        if (glfw)
            glfw->release_window();
    #elif defined(SPECTRA_USE_SDL3)
        if (sdl3)
            sdl3->release_window();
    #endif
        window_mgr->shutdown();
        window_mgr.reset();
    }
#endif

    if (backend)
        backend->wait_idle();

    renderer_ptr.reset();
    if (backend)
    {
        backend->shutdown();
        backend.reset();
    }

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Windowing adapter destructor handles cleanup
#endif

    conn->close();

    SPECTRA_LOG_INFO("window", "Agent stopped");
    return 0;
}
