// inproc_topic_server.hpp — Topics support for in-process Spectra applications.
//
// Runs a lightweight Unix-socket server inside the process so external
// publishers (spectra::Publisher) can push samples to the running app without
// a separate spectra-backend daemon.
//
// The server:
//   - Listens on $XDG_RUNTIME_DIR/spectra-<pid>.sock (same path pattern as
//     the daemon, so publishers auto-discover it via publisher_client.cpp)
//   - Accepts publisher connections in a background thread
//   - Handles HELLO / REQ_DECLARE_TOPIC / REQ_PUBLISH_TOPIC_SAMPLES /
//     REQ_LIST_TOPICS (all the publisher-side messages)
//   - Pushes samples directly to Series::append() via the thread-safe pending
//     buffer; session_runtime's commit_pending() loop drains them on the
//     main thread
//   - Calls a topic_changed callback so the TopicsPanel stays up-to-date
//
// Subscribe wiring is done from the outside (see wire_topics_panel() below):
//   the panel's subscribe callback looks up the figure/axes by ID, creates a
//   series, enables thread-safe mode, and registers the subscription here.

#pragma once

#ifndef _WIN32

    #include <atomic>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <thread>
    #include <unordered_map>
    #include <vector>

    #include "../daemon/topic_registry.hpp"
    #include "../ipc/message.hpp"
    #include "../ipc/transport.hpp"
    #include "../ui/topics/topics_panel.hpp"

namespace spectra
{

class FigureRegistry;

class InprocTopicServer
{
   public:
    // Called whenever the topic list changes (declare / disconnect / subscribe).
    using TopicChangedFn = std::function<void()>;

    InprocTopicServer();
    ~InprocTopicServer();

    // Start the background listener.  Returns the socket path (empty on error).
    // Typically called once from App::init_inproc().
    std::string start(FigureRegistry* fig_registry);

    // Stop the server and join the background thread.
    void stop();

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    // Returns the socket path the server is listening on (empty if not running).
    const std::string& socket_path() const { return socket_path_; }

    // Set callback fired on topic list changes (declare / disconnect).
    void set_topic_changed_fn(TopicChangedFn fn) { topic_changed_fn_ = std::move(fn); }

    // Wire a TopicsPanel: sets list_request_callback + subscribe_request_callback.
    void wire_topics_panel(ui::topics::TopicsPanel& panel, FigureRegistry* fig_registry);

    // Get a snapshot of current topics (for the panel's list_request callback).
    std::vector<ipc::TopicInfoEntry> topic_snapshot() const;

   private:
    void run_loop();
    void handle_message(int fd, ipc::Connection& conn, uint64_t client_id, const ipc::Message& msg);
    void on_client_disconnected(uint64_t client_id);
    void notify_topic_changed();

    // Send helpers (no daemon dependency).
    static void send_ok(ipc::Connection& conn, ipc::RequestId req_id);
    static void send_err(ipc::Connection& conn, ipc::RequestId req_id, const std::string& reason);
    static void send_welcome(ipc::Connection& conn);
    void        send_topic_list(ipc::Connection& conn, ipc::RequestId req_id);

    ipc::Server       server_;
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::string       socket_path_;

    mutable std::mutex    reg_mu_;
    daemon::TopicRegistry registry_;
    uint64_t              next_client_id_{1};

    // figure_id → axes_index → series_index  (subscriptions registered via UI)
    // Needed to fan out publish calls to the right Series*.
    struct Sub
    {
        uint64_t figure_id    = 0;
        uint32_t axes_index   = 0;
        uint32_t series_index = 0;
    };
    std::unordered_map<std::string /*topic*/, std::vector<Sub>> subs_;

    FigureRegistry* fig_registry_ = nullptr;
    TopicChangedFn  topic_changed_fn_;
};

}   // namespace spectra

#endif   // !_WIN32
