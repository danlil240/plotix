#pragma once

// qt_ipc_client.hpp — Qt-friendly IPC client for the multiprocess window agent.
//
// Connects to a Spectra daemon via Unix domain socket, performs the
// HELLO/WELCOME handshake, and polls for STATE_SNAPSHOT / STATE_DIFF messages
// using a QTimer integrated with the Qt event loop.
//
// Emits signals when figures need to be rebuilt or updated. The
// QtApplicationController or entry point connects these to the main window
// and figure registry.
//
// Phase 7 component: replaces the legacy GLFW/SDL3 window agent's
// blocking poll loop with Qt event-driven IPC.

#ifndef _WIN32

    #include <QObject>
    #include <QTimer>

    #include <atomic>
    #include <cstdint>
    #include <memory>
    #include <string>
    #include <vector>

    #include "ipc/codec.hpp"
    #include "ipc/figure_snapshot.hpp"
    #include "ipc/message.hpp"
    #include "ipc/transport.hpp"

    #include <spectra/figure_registry.hpp>

namespace spectra::adapters::qt
{

class QtIpcClient : public QObject
{
    Q_OBJECT

   public:
    explicit QtIpcClient(QObject* parent = nullptr);
    ~QtIpcClient();

    // Connect to daemon at the given socket path and perform handshake.
    // Returns true on success.
    bool connect_to_daemon(const std::string& socket_path);

    // Disconnect from daemon.
    void disconnect_from_daemon();

    // Set the figure registry for applying live diffs (fast path).
    void set_figure_registry(spectra::FigureRegistry* reg) { figure_registry_ = reg; }

    bool is_connected() const { return conn_ && conn_->is_open(); }

    // IPC session info (valid after connect)
    ipc::SessionId session_id() const { return session_id_; }
    ipc::WindowId  window_id() const  { return ipc_window_id_; }

    // Assigned figure IDs (IPC figure IDs, not local FigureIds)
    const std::vector<uint64_t>& assigned_figures() const { return assigned_figures_; }

    // Mapping from IPC figure index to local FigureId
    const std::vector<spectra::FigureId>& local_figure_ids() const { return local_ids_; }
    void set_local_ids(std::vector<spectra::FigureId> ids) { local_ids_ = std::move(ids); }

    // Figure cache (latest snapshot state per figure)
    const std::vector<ipc::SnapshotFigureState>& figure_cache() const { return figure_cache_; }

   signals:
    // Emitted when a full snapshot is received and figures need to be rebuilt.
    // The caller should rebuild the FigureRegistry from figure_cache() and
    // update the main window tabs.
    void snapshot_received();

    // Emitted when a diff was applied. If needs_rebuild is true, a full
    // registry rebuild is required (structural change). Otherwise, the diff
    // was applied directly to live figures.
    void diff_applied(bool needs_rebuild);

    // Emitted when the backend connection is lost.
    void connection_lost();

    // Emitted when the backend requests this window to close.
    void close_requested();

   private slots:
    void poll_messages();

   private:
    void send_hello();
    void recv_welcome();
    void drain_initial_messages();
    void send_heartbeat();
    void send_ack(ipc::Revision rev);

    std::unique_ptr<ipc::Connection> conn_;
    QTimer                           poll_timer_;
    QTimer                           heartbeat_timer_;

    ipc::SessionId session_id_    = ipc::INVALID_SESSION;
    ipc::WindowId  ipc_window_id_ = ipc::INVALID_WINDOW;
    uint32_t       heartbeat_ms_  = 5000;

    std::vector<uint64_t>                    assigned_figures_;
    uint64_t                                 ipc_active_figure_id_ = 0;
    std::vector<ipc::SnapshotFigureState>    figure_cache_;
    std::vector<ipc::SnapshotKnobState>      knob_cache_;
    std::vector<spectra::FigureId>           local_ids_;
    ipc::Revision                            current_revision_ = 0;
    std::atomic<bool>                        connected_{false};

    spectra::FigureRegistry* figure_registry_ = nullptr;
};

}   // namespace spectra::adapters::qt

#endif   // !_WIN32
