// qt_ipc_client.cpp — Qt IPC client implementation for multiprocess window agent.

#ifndef _WIN32

    #include "qt_ipc_client.hpp"

    #include <spectra/logger.hpp>

    #include <chrono>
    #include <cstring>

    #ifndef _WIN32
        #include <poll.h>
    #endif

namespace spectra::adapters::qt
{

QtIpcClient::QtIpcClient(QObject* parent)
    : QObject(parent)
{
    QObject::connect(&poll_timer_, &QTimer::timeout, this, &QtIpcClient::poll_messages);
    QObject::connect(&heartbeat_timer_, &QTimer::timeout, this, [this]() { send_heartbeat(); });
}

QtIpcClient::~QtIpcClient()
{
    disconnect_from_daemon();
}

bool QtIpcClient::connect_to_daemon(const std::string& socket_path)
{
    conn_ = ipc::Client::connect(socket_path);
    if (!conn_)
    {
        SPECTRA_LOG_ERROR("qt_ipc", "Failed to connect to {}", socket_path);
        return false;
    }

    SPECTRA_LOG_INFO("qt_ipc", "Connected to backend: {}", socket_path);

    send_hello();
    recv_welcome();
    drain_initial_messages();

    // Start polling for IPC messages (non-blocking, 10ms interval)
    poll_timer_.start(10);

    // Start heartbeat timer
    if (heartbeat_ms_ > 0)
        heartbeat_timer_.start(static_cast<int>(heartbeat_ms_));

    connected_.store(true, std::memory_order_relaxed);
    return true;
}

void QtIpcClient::disconnect_from_daemon()
{
    connected_.store(false, std::memory_order_relaxed);
    poll_timer_.stop();
    heartbeat_timer_.stop();
    if (conn_)
    {
        conn_->close();
        conn_.reset();
    }
}

void QtIpcClient::send_hello()
{
    ipc::HelloPayload hello;
    hello.protocol_major = ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = ipc::PROTOCOL_MINOR;
    hello.agent_build    = "spectra-qt-window/0.1.0";
    hello.capabilities   = 0;

    ipc::Message msg;
    msg.header.type        = ipc::MessageType::HELLO;
    msg.payload            = ipc::encode_hello(hello);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    if (!conn_->send(msg))
    {
        SPECTRA_LOG_ERROR("qt_ipc", "Failed to send HELLO");
    }
}

void QtIpcClient::recv_welcome()
{
    auto msg = conn_->recv();
    if (!msg || msg->header.type != ipc::MessageType::WELCOME)
    {
        SPECTRA_LOG_ERROR("qt_ipc", "Did not receive WELCOME");
        return;
    }

    auto welcome = ipc::decode_welcome(msg->payload);
    if (!welcome)
    {
        SPECTRA_LOG_ERROR("qt_ipc", "Failed to decode WELCOME");
        return;
    }

    session_id_    = welcome->session_id;
    ipc_window_id_ = welcome->window_id;
    heartbeat_ms_  = welcome->heartbeat_ms;

    SPECTRA_LOG_INFO("qt_ipc", "WELCOME: session={} window={} heartbeat={}ms",
                     session_id_, ipc_window_id_, heartbeat_ms_);
}

void QtIpcClient::drain_initial_messages()
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool got_snapshot = false;

    while (!got_snapshot && std::chrono::steady_clock::now() < deadline)
    {
        struct pollfd pfd{};
        pfd.fd     = conn_->fd();
        pfd.events  = POLLIN;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 100) <= 0 || !(pfd.revents & POLLIN))
            continue;

        auto msg_opt = conn_->recv();
        if (!msg_opt)
            break;

        auto& msg = *msg_opt;
        if (msg.header.type == ipc::MessageType::CMD_ASSIGN_FIGURES)
        {
            auto payload = ipc::decode_cmd_assign_figures(msg.payload);
            if (payload)
            {
                assigned_figures_     = payload->figure_ids;
                ipc_active_figure_id_ = payload->active_figure_id;
            }
        }
        else if (msg.header.type == ipc::MessageType::STATE_SNAPSHOT)
        {
            auto snap = ipc::decode_state_snapshot(msg.payload);
            if (snap)
            {
                figure_cache_     = snap->figures;
                knob_cache_       = snap->knobs;
                current_revision_ = snap->revision;
                got_snapshot      = true;

                send_ack(current_revision_);
                SPECTRA_LOG_DEBUG("qt_ipc", "STATE_SNAPSHOT (init): rev={} figures={}",
                                  current_revision_, figure_cache_.size());
            }
        }
    }

    if (!got_snapshot)
        SPECTRA_LOG_WARN("qt_ipc", "No STATE_SNAPSHOT received");
}

void QtIpcClient::poll_messages()
{
    if (!conn_ || !conn_->is_open())
    {
        if (connected_.load(std::memory_order_relaxed))
        {
            SPECTRA_LOG_WARN("qt_ipc", "Backend connection lost");
            connected_.store(false, std::memory_order_relaxed);
            emit connection_lost();
        }
        return;
    }

    for (;;)
    {
        struct pollfd pfd{};
        pfd.fd      = conn_->fd();
        pfd.events   = POLLIN;
        pfd.revents  = 0;
        int ret = ::poll(&pfd, 1, 0);   // non-blocking
        if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR)))
        {
            SPECTRA_LOG_WARN("qt_ipc", "Backend connection lost (poll)");
            connected_.store(false, std::memory_order_relaxed);
            emit connection_lost();
            return;
        }
        if (ret <= 0 || !(pfd.revents & POLLIN))
            break;

        auto msg_opt = conn_->recv();
        if (!msg_opt)
        {
            SPECTRA_LOG_WARN("qt_ipc", "Connection to backend lost");
            connected_.store(false, std::memory_order_relaxed);
            emit connection_lost();
            return;
        }

        auto& msg = *msg_opt;
        switch (msg.header.type)
        {
            case ipc::MessageType::CMD_ASSIGN_FIGURES:
            {
                auto payload = ipc::decode_cmd_assign_figures(msg.payload);
                if (payload)
                {
                    assigned_figures_     = payload->figure_ids;
                    ipc_active_figure_id_ = payload->active_figure_id;
                }
                break;
            }
            case ipc::MessageType::CMD_CLOSE_WINDOW:
                SPECTRA_LOG_DEBUG("qt_ipc", "CMD_CLOSE_WINDOW");
                emit close_requested();
                break;

            case ipc::MessageType::STATE_SNAPSHOT:
            {
                auto snap = ipc::decode_state_snapshot(msg.payload);
                if (snap)
                {
                    figure_cache_     = snap->figures;
                    knob_cache_       = snap->knobs;
                    current_revision_ = snap->revision;
                    send_ack(current_revision_);
                    emit snapshot_received();
                }
                break;
            }
            case ipc::MessageType::STATE_DIFF:
            {
                auto diff = ipc::decode_state_diff(msg.payload);
                if (diff)
                {
                    bool needs_rebuild = false;
                    for (const auto& op : diff->ops)
                    {
                        for (auto& fig : figure_cache_)
                        {
                            if (fig.figure_id == op.figure_id)
                            {
                                ipc::apply_diff_op_to_cache(fig, op);
                                break;
                            }
                        }
                        if (op.type == ipc::DiffOp::Type::ADD_SERIES
                            || op.type == ipc::DiffOp::Type::ADD_AXES)
                        {
                            needs_rebuild = true;
                        }
                        else
                        {
                            // Apply directly to live figure (fast path)
                            for (size_t mi = 0; mi < assigned_figures_.size() && mi < local_ids_.size(); ++mi)
                            {
                                if (assigned_figures_[mi] == op.figure_id)
                                {
                                    auto* live_fig = figure_registry_->get(local_ids_[mi]);
                                    if (live_fig)
                                        ipc::apply_diff_op_to_figure(*live_fig, op);
                                    break;
                                }
                            }
                        }
                    }
                    current_revision_ = diff->new_revision;
                    send_ack(current_revision_);
                    emit diff_applied(needs_rebuild);
                }
                break;
            }
            case ipc::MessageType::RESP_OK:
            case ipc::MessageType::RESP_ERR:
                break;
            default:
                break;
        }
    }
}

void QtIpcClient::send_heartbeat()
{
    if (!conn_ || !conn_->is_open())
        return;

    ipc::Message msg;
    msg.header.type        = ipc::MessageType::EVT_HEARTBEAT;
    msg.header.session_id  = session_id_;
    msg.header.window_id   = ipc_window_id_;
    msg.header.payload_len = 0;
    conn_->send(msg);
}

void QtIpcClient::send_ack(ipc::Revision rev)
{
    ipc::AckStatePayload ack;
    ack.revision = rev;
    ipc::Message msg;
    msg.header.type        = ipc::MessageType::ACK_STATE;
    msg.header.session_id  = session_id_;
    msg.header.window_id   = ipc_window_id_;
    msg.payload            = ipc::encode_ack_state(ack);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    conn_->send(msg);
}

}   // namespace spectra::adapters::qt

#endif   // !_WIN32
