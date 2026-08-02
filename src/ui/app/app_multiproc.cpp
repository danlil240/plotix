// app_multiproc.cpp — Multi-process run implementation.
//
// Auto-spawns spectra-backend (found next to own binary), connects via IPC,
// pushes all figures, and waits for the agent windows to close.
// Single-terminal UX: just call app.run() — no manual backend startup needed.
//
// Always compiled; selected at runtime via AppConfig::socket_path or SPECTRA_SOCKET env var.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "anim/frame_scheduler.hpp"
#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ipc/transport.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/overlay/knob_manager.hpp"

#ifdef _WIN32
    #include <process.h>
    #ifdef __MINGW32__
        #include <sys/types.h>
    #elif defined(_MSC_VER)
using pid_t = int;
    #endif
    #define getpid _getpid
#else
    #include <csignal>
    #include <poll.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace spectra
{
// ─── Find own binary directory ───────────────────────────────────────────────
static std::string self_dir()
{
#if defined(__linux__)
    char    buf[4096] = {};
    ssize_t n         = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        std::string path(buf, static_cast<size_t>(n));
        auto        slash = path.rfind('/');
        if (slash != std::string::npos)
            return path.substr(0, slash + 1);
    }
#elif defined(__APPLE__)
    char     buf[4096] = {};
    uint32_t size      = sizeof(buf);
    if (::_NSGetExecutablePath(buf, &size) == 0)
    {
        std::string path(buf);
        auto        slash = path.rfind('/');
        if (slash != std::string::npos)
            return path.substr(0, slash + 1);
    }
#endif
    return "";
}

// ─── Spawn spectra-backend and return its PID ────────────────────────────────
static pid_t spawn_backend(const std::string& sock_path)
{
#ifndef _WIN32
    std::string dir         = self_dir();
    std::string backend_bin = dir + "spectra-backend";
    if (::access(backend_bin.c_str(), X_OK) != 0)
    {
        // Try one directory up (e.g. binary is in build/examples/, backend in build/)
        if (dir.size() > 1)
        {
            auto parent_slash = dir.rfind('/', dir.size() - 2);
            if (parent_slash != std::string::npos)
            {
                std::string parent    = dir.substr(0, parent_slash + 1);
                std::string candidate = parent + "spectra-backend";
                if (::access(candidate.c_str(), X_OK) == 0)
                    backend_bin = candidate;
            }
        }
        if (::access(backend_bin.c_str(), X_OK) != 0)
            backend_bin = "spectra-backend";   // fall back to PATH
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
        // Child: exec the backend
        ::execlp(backend_bin.c_str(),
                 backend_bin.c_str(),
                 "--socket",
                 sock_path.c_str(),
                 "--idle-exit",
                 nullptr);
        ::_exit(127);
    }
    return pid;
#else
    (void)sock_path;
    return -1;
#endif
}

// ─── Serialize a Figure into a SnapshotFigureState ──────────────────────────
static ipc::SnapshotFigureState figure_to_snapshot(const Figure& fig, uint64_t figure_id)
{
    ipc::SnapshotFigureState snap;
    snap.figure_id = figure_id;
    snap.title     = "";
    snap.width     = fig.width();
    snap.height    = fig.height();
    snap.grid_rows = fig.grid_rows();
    snap.grid_cols = fig.grid_cols();

    for (const auto& ax_ptr : fig.axes())
    {
        if (!ax_ptr)
            continue;
        const auto&            ax = *ax_ptr;
        ipc::SnapshotAxisState sa;
        sa.x_min        = ax.x_limits().min;
        sa.x_max        = ax.x_limits().max;
        sa.y_min        = ax.y_limits().min;
        sa.y_max        = ax.y_limits().max;
        sa.grid_visible = ax.grid_enabled();
        sa.x_label      = ax.xlabel();
        sa.y_label      = ax.ylabel();
        sa.title        = ax.title();
        snap.axes.push_back(std::move(sa));

        for (const auto& s_ptr : ax.series())
        {
            if (!s_ptr)
                continue;
            const auto&              s = *s_ptr;
            ipc::SnapshotSeriesState ss;
            ss.name    = s.label();
            ss.color_r = s.color().r;
            ss.color_g = s.color().g;
            ss.color_b = s.color().b;
            ss.color_a = s.color().a;
            ss.visible = s.visible();
            ss.opacity = s.opacity();

            if (auto* line = dynamic_cast<const LineSeries*>(&s))
            {
                ss.type        = "line";
                ss.line_width  = line->width();
                ss.marker_size = s.marker_size();
                auto xd        = line->x_data();
                auto yd        = line->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }
            else if (auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
            {
                ss.type        = "scatter";
                ss.marker_size = scatter->size();
                ss.line_width  = 2.0f;
                auto xd        = scatter->x_data();
                auto yd        = scatter->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }

            snap.series.push_back(std::move(ss));
        }
    }

    return snap;
}

// ─── Send an IPC message helper ─────────────────────────────────────────────
static bool send_msg(ipc::Connection&     conn,
                     ipc::MessageType     type,
                     ipc::SessionId       session_id,
                     ipc::WindowId        window_id,
                     std::vector<uint8_t> payload = {})
{
    ipc::Message msg;
    msg.header.type        = type;
    msg.header.session_id  = session_id;
    msg.header.window_id   = window_id;
    msg.payload            = std::move(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

void App::run_multiproc()
{
    if (registry_.count() == 0)
    {
        SPECTRA_LOG_WARN("app", "No figures to display");
        return;
    }

    // ── Headless mode: render + export locally (no daemon needed) ─────────
    if (config_.headless)
    {
        if (!backend_ || !renderer_)
        {
            SPECTRA_LOG_ERROR("app", "Cannot run headless: backend or renderer not initialized");
            return;
        }

        for (auto id : registry_.all_ids())
        {
            Figure* fig = registry_.get(id);
            if (!fig)
                continue;
            fig->compute_layout();

            uint32_t export_w =
                fig->export_req_.png_width > 0 ? fig->export_req_.png_width : fig->width();
            uint32_t export_h =
                fig->export_req_.png_height > 0 ? fig->export_req_.png_height : fig->height();

            backend_->create_offscreen_framebuffer(export_w, export_h);
            static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

            uint32_t orig_w     = fig->config_.width;
            uint32_t orig_h     = fig->config_.height;
            fig->config_.width  = export_w;
            fig->config_.height = export_h;
            fig->compute_layout();

            if (backend_->begin_frame())
            {
                renderer_->render_figure(*fig);
                backend_->end_frame();
            }

            fig->config_.width  = orig_w;
            fig->config_.height = orig_h;
            fig->compute_layout();

            if (!fig->export_req_.png_path.empty())
            {
                std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
                if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
                {
                    if (!ImageExporter::write_png(fig->export_req_.png_path,
                                                  pixels.data(),
                                                  export_w,
                                                  export_h))
                        SPECTRA_LOG_ERROR("app",
                                          "Failed to write PNG: {}",
                                          fig->export_req_.png_path);
                }
                else
                {
                    SPECTRA_LOG_ERROR("app", "Failed to readback framebuffer");
                }
            }

            if (!fig->export_req_.svg_path.empty())
            {
                if (!SvgExporter::write_svg(fig->export_req_.svg_path, *fig))
                    SPECTRA_LOG_ERROR("app", "Failed to write SVG: {}", fig->export_req_.svg_path);
            }
        }

        if (backend_)
            backend_->wait_idle();
        return;
    }

    // Use an explicit socket when requested so external publishers can rendezvous
    // with this backend. Otherwise use a per-process unique socket.
    std::string sock = config_.socket_path;
    if (sock.empty())
    {
        if (const char* env = std::getenv("SPECTRA_SOCKET"); env && *env)
            sock = env;
    }
    if (sock.empty())
        sock = "/tmp/spectra-" + std::to_string(::getpid()) + ".sock";

    std::unique_ptr<ipc::Connection> conn;

    // Always spawn a fresh backend for this process.
    {
        pid_t backend_pid = spawn_backend(sock);
        if (backend_pid <= 0)
        {
            SPECTRA_LOG_ERROR("app", "Failed to spawn spectra-backend");
            return;
        }

        // Retry connection with backoff (backend needs a moment to start)
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            conn = ipc::Client::connect(sock);
            if (conn && conn->is_open())
                break;
        }

        if (!conn || !conn->is_open())
        {
            SPECTRA_LOG_ERROR("app", "Timed out waiting for spectra-backend to start");
#ifndef _WIN32
            ::kill(backend_pid, SIGTERM);
#endif
            return;
        }
    }

    // Send HELLO
    ipc::HelloPayload hello;
    hello.agent_build = "spectra-app/0.1.0";
    send_msg(*conn, ipc::MessageType::HELLO, 0, 0, ipc::encode_hello(hello));

    // Wait for WELCOME
    ipc::SessionId session_id = 0;
    ipc::WindowId  window_id  = 0;
    {
        auto welcome_msg = conn->recv();
        if (!welcome_msg || welcome_msg->header.type != ipc::MessageType::WELCOME)
        {
            SPECTRA_LOG_ERROR("app", "Did not receive WELCOME from backend");
            return;
        }
        auto wp = ipc::decode_welcome(welcome_msg->payload);
        if (!wp)
        {
            SPECTRA_LOG_ERROR("app", "Failed to decode WELCOME");
            return;
        }
        session_id = wp->session_id;
        window_id  = wp->window_id;
    }

    // Drain any initial messages (CMD_ASSIGN_FIGURES, STATE_SNAPSHOT for default figure)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < deadline)
        {
#ifndef _WIN32
            struct pollfd pfd
            {
            };
            pfd.fd      = conn->fd();
            pfd.events  = POLLIN;
            pfd.revents = 0;
            if (::poll(&pfd, 1, 50) <= 0 || !(pfd.revents & POLLIN))
                break;
#endif
            auto msg_opt = conn->recv();
            if (!msg_opt)
                break;
        }
    }

    // Serialize and push all figures as a STATE_SNAPSHOT.
    // Assign window_group so the daemon groups sibling figures into one agent.
    ipc::StateSnapshotPayload snap;
    snap.revision   = 1;
    snap.session_id = session_id;

    auto window_groups = compute_window_groups();

    // Map registry FigureId → IPC figure_id (starting at 100)
    std::unordered_map<FigureId, uint64_t> reg_to_ipc;
    uint64_t                               fig_counter = 100;
    for (auto id : registry_.all_ids())
        reg_to_ipc[id] = fig_counter++;

    fig_counter = 100;
    for (auto id : registry_.all_ids())
    {
        Figure* fig = registry_.get(id);
        if (!fig)
            continue;
        fig->compute_layout();
        auto fig_snap  = figure_to_snapshot(*fig, reg_to_ipc[id]);
        fig_snap.title = "Figure " + std::to_string(reg_to_ipc[id] - 99);
        snap.figures.push_back(std::move(fig_snap));
    }

    // Serialize knobs into the snapshot so the agent window can display them
    if (knob_manager_ && !knob_manager_->empty())
    {
        for (const auto& k : knob_manager_->knobs())
        {
            ipc::SnapshotKnobState ks;
            ks.name    = k.name;
            ks.type    = static_cast<uint8_t>(k.type);
            ks.value   = k.value;
            ks.min_val = k.min_val;
            ks.max_val = k.max_val;
            ks.step    = k.step;
            ks.choices = k.choices;
            snap.knobs.push_back(std::move(ks));
        }
    }

    // Assign window_group: figures in the same group get the same non-zero ID
    for (uint32_t gi = 0; gi < window_groups.size(); ++gi)
    {
        uint32_t group_id = gi + 1;   // 1-based group IDs
        for (auto reg_id : window_groups[gi])
        {
            uint64_t ipc_id = reg_to_ipc[reg_id];
            for (auto& fs : snap.figures)
            {
                if (fs.figure_id == ipc_id)
                {
                    fs.window_group = group_id;
                    break;
                }
            }
        }
    }

    send_msg(*conn,
             ipc::MessageType::STATE_SNAPSHOT,
             session_id,
             window_id,
             ipc::encode_state_snapshot(snap));

    // The daemon spawns one agent per figure automatically when it
    // receives the STATE_SNAPSHOT.  No need to send REQ_CREATE_WINDOW.

    bool  has_any_animation = false;
    float max_fps           = 60.0f;
    for (auto id : registry_.all_ids())
    {
        Figure* fig = registry_.get(id);
        if (fig && fig->has_animation())
        {
            has_any_animation = true;
            if (fig->anim_fps() > max_fps)
                max_fps = fig->anim_fps();
        }
    }

    std::unique_ptr<FrameScheduler> scheduler;
    if (has_any_animation)
    {
        scheduler = std::make_unique<FrameScheduler>(max_fps);
    }

    // Track last-sent axis limits so we can emit SET_AXIS_LIMITS diffs
    // when the user callback changes them (e.g. live_ax->xlim(t-10, t)).
    struct AxisLimitsKey
    {
        uint64_t ipc_fig_id;
        uint32_t axes_idx;
    };
    struct AxisLimitsKeyHash
    {
        size_t operator()(const AxisLimitsKey& k) const
        {
            return std::hash<uint64_t>{}(k.ipc_fig_id)
                   ^ (static_cast<size_t>(std::hash<uint32_t>{}(k.axes_idx)) << 16);
        }
    };
    struct AxisLimitsKeyEq
    {
        bool operator()(const AxisLimitsKey& a, const AxisLimitsKey& b) const
        {
            return a.ipc_fig_id == b.ipc_fig_id && a.axes_idx == b.axes_idx;
        }
    };
    struct SentLimits
    {
        double xmin, xmax, ymin, ymax;
    };
    std::unordered_map<AxisLimitsKey, SentLimits, AxisLimitsKeyHash, AxisLimitsKeyEq> sent_limits;

    // Wait until all agent windows are closed (backend sends CMD_CLOSE_WINDOW or drops connection)
    auto                  last_heartbeat     = std::chrono::steady_clock::now();
    static constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(5000);
    while (true)
    {
        if (s_exit_requested_.load(std::memory_order_relaxed))
        {
            SPECTRA_LOG_INFO("app", "Exit requested (signal) — shutting down");
            break;
        }

        if (scheduler)
            scheduler->begin_frame();

#ifndef _WIN32
        struct pollfd pfd
        {
        };
        pfd.fd     = conn->fd();
        pfd.events = POLLIN;

        int  timeout_ms     = scheduler ? 0 : 1000;
        bool exit_requested = false;

        while (true)
        {
            pfd.revents = 0;
            int pr      = ::poll(&pfd, 1, timeout_ms);
            if (pr < 0)
            {
                exit_requested = true;
                break;
            }
            if (pr == 0)
                break;   // timeout

            if (pfd.revents & (POLLHUP | POLLERR))
            {
                exit_requested = true;
                break;
            }
            if (pfd.revents & POLLIN)
            {
                auto msg = conn->recv();
                if (!msg)
                {
                    exit_requested = true;
                    break;
                }
                if (msg->header.type == ipc::MessageType::CMD_CLOSE_WINDOW)
                {
                    exit_requested = true;
                    break;
                }
                // Apply knob value changes from agent UI back to the app's KnobManager
                if (msg->header.type == ipc::MessageType::STATE_DIFF && knob_manager_)
                {
                    auto diff = ipc::decode_state_diff(msg->payload);
                    if (diff)
                    {
                        for (const auto& op : diff->ops)
                        {
                            if (op.type == ipc::DiffOp::Type::SET_KNOB_VALUE)
                                knob_manager_->set_value(op.str_val, op.f1);
                        }
                    }
                }
            }
            else
            {
                break;
            }
            timeout_ms = 0;   // only block on the first iteration
        }
        if (exit_requested)
            break;
#else
        if (!scheduler)
        {
            auto msg = conn->recv();
            if (!msg)
                break;
            if (msg->header.type == ipc::MessageType::CMD_CLOSE_WINDOW)
                break;
        }
#endif

        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL)
        {
            if (!send_msg(*conn, ipc::MessageType::EVT_HEARTBEAT, session_id, window_id, {}))
                break;
            last_heartbeat = now;
        }

        if (scheduler)
        {
            Frame                 frame = scheduler->current_frame();
            ipc::StateDiffPayload diff;

            for (auto id : registry_.all_ids())
            {
                Figure* fig = registry_.get(id);
                if (fig && fig->has_animation())
                {
                    if (fig->anim_.on_frame)
                    {
                        fig->anim_.on_frame(frame);
                    }
                }

                if (fig)
                {
                    uint32_t axes_idx = 0;
                    for (const auto& ax_ptr : fig->axes())
                    {
                        if (!ax_ptr)
                        {
                            axes_idx++;
                            continue;
                        }

                        // Emit SET_AXIS_LIMITS if limits changed since last frame
                        {
                            auto          xlim = ax_ptr->x_limits();
                            auto          ylim = ax_ptr->y_limits();
                            AxisLimitsKey key{reg_to_ipc[id], axes_idx};
                            auto          it = sent_limits.find(key);
                            bool changed = (it == sent_limits.end()) || it->second.xmin != xlim.min
                                           || it->second.xmax != xlim.max
                                           || it->second.ymin != ylim.min
                                           || it->second.ymax != ylim.max;
                            if (changed)
                            {
                                ipc::DiffOp op;
                                op.type       = ipc::DiffOp::Type::SET_AXIS_LIMITS;
                                op.figure_id  = reg_to_ipc[id];
                                op.axes_index = axes_idx;
                                op.f1         = xlim.min;
                                op.f2         = xlim.max;
                                op.f3         = ylim.min;
                                op.f4         = ylim.max;
                                diff.ops.push_back(std::move(op));
                                sent_limits[key] = {xlim.min, xlim.max, ylim.min, ylim.max};
                            }
                        }

                        uint32_t series_idx = 0;
                        for (const auto& s_ptr : ax_ptr->series())
                        {
                            if (s_ptr && s_ptr->is_dirty())
                            {
                                ipc::DiffOp op;
                                op.type         = ipc::DiffOp::Type::SET_SERIES_DATA;
                                op.figure_id    = reg_to_ipc[id];
                                op.axes_index   = axes_idx;
                                op.series_index = series_idx;

                                if (auto* line = dynamic_cast<const LineSeries*>(s_ptr.get()))
                                {
                                    auto xd = line->x_data();
                                    auto yd = line->y_data();
                                    op.data.reserve(xd.size() * 2);
                                    for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                                    {
                                        op.data.push_back(xd[i]);
                                        op.data.push_back(yd[i]);
                                    }
                                }
                                else if (auto* scatter =
                                             dynamic_cast<const ScatterSeries*>(s_ptr.get()))
                                {
                                    auto xd = scatter->x_data();
                                    auto yd = scatter->y_data();
                                    op.data.reserve(xd.size() * 2);
                                    for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                                    {
                                        op.data.push_back(xd[i]);
                                        op.data.push_back(yd[i]);
                                    }
                                }

                                diff.ops.push_back(std::move(op));
                                const_cast<Series*>(s_ptr.get())->clear_dirty();
                            }
                            series_idx++;
                        }
                        axes_idx++;
                    }
                }
            }

            if (!diff.ops.empty())
            {
                send_msg(*conn,
                         ipc::MessageType::STATE_DIFF,
                         session_id,
                         window_id,
                         ipc::encode_state_diff(diff));
            }

            scheduler->end_frame();
        }
    }

    // Notify backend we are done — it will kill all agents and exit
    ipc::ReqCloseWindowPayload close_req;
    close_req.window_id = window_id;
    close_req.reason    = "app_exit";
    send_msg(*conn,
             ipc::MessageType::REQ_CLOSE_WINDOW,
             session_id,
             window_id,
             ipc::encode_req_close_window(close_req));
    conn->close();
}

}   // namespace spectra
