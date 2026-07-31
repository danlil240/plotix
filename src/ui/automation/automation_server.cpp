// automation_server.cpp — JSON-over-Unix-socket automation endpoint for Spectra.
// See automation_server.hpp for architecture overview.

#include "automation_server.hpp"

#include "automation_dispatch.hpp"
#include "automation_handler.hpp"
#include "automation_json.hpp"

#include <chrono>
#include <spectra/app.hpp>
#include <spectra/logger.hpp>

#include "ui/app/perf_metrics.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#endif

#ifndef _WIN32
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace spectra
{

// ─── AutomationServer lifecycle ──────────────────────────────────────────────

AutomationServer::AutomationServer()
{
    register_handlers();
}

AutomationServer::~AutomationServer()
{
    stop();
}

void AutomationServer::register_handlers()
{
    auto install = [&](std::vector<AutomationHandlerEntry> entries)
    {
        for (auto& e : entries)
        {
            const std::string method = e.method;
            handler_catalog_.push_back(e);
            handlers_.emplace(method, wrap_automation_handler(std::move(e)));
        }
    };
    install(make_command_handlers());
    install(make_input_handlers());
    install(make_figure_handlers());
    install(make_capture_handlers());
    install(make_window_handlers());
    install(make_utility_handlers());
    install(make_fuzz_handlers());

    AutomationHandlerEntry list_methods{
        "list_methods",
        "List all automation methods with metadata (context requirements and parameters).",
        AutomationContextFlag::None,
        {},
        [this](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        {
            req.response_json =
                json_ok(req.id,
                        "{\"methods\":" + serialize_handler_catalog(handler_catalog_) + "}");
        },
    };
    const std::string list_method = list_methods.method;
    handler_catalog_.push_back(list_methods);
    handlers_.emplace(list_method, wrap_automation_handler(std::move(list_methods)));
}

std::string AutomationServer::default_socket_path()
{
#ifndef _WIN32
    return "/tmp/spectra-auto-" + std::to_string(::getpid()) + ".sock";
#else
    return "";
#endif
}

std::string AutomationServer::handler_catalog_json() const
{
    return serialize_handler_catalog(handler_catalog_);
}

bool AutomationServer::start(const std::string& socket_path)
{
#ifdef _WIN32
    (void)socket_path;
    return false;
#else
    if (running_.load())
        return false;
    socket_path_ = socket_path;
    ::unlink(socket_path_.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        SPECTRA_LOG_ERROR("automation", "socket(): " + std::string(strerror(errno)));
        return false;
    }

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path))
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    ::chmod(socket_path_.c_str(), 0700);

    if (::listen(listen_fd_, 4) < 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return false;
    }

    running_.store(true, std::memory_order_release);
    listener_thread_ = std::thread(&AutomationServer::listener_thread_fn, this);
    SPECTRA_LOG_INFO("automation", "Listening on " + socket_path_);
    return true;
#endif
}

void AutomationServer::stop()
{
#ifndef _WIN32
    if (!running_.load(std::memory_order_relaxed))
        return;
    running_.store(false, std::memory_order_release);

    if (listen_fd_ >= 0)
    {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    {
        std::lock_guard lock(clients_mutex_);
        for (int fd : client_fds_)
        {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        client_fds_.clear();
    }

    if (listener_thread_.joinable())
        listener_thread_.join();
    if (!socket_path_.empty())
    {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }
    SPECTRA_LOG_INFO("automation", "Stopped");
#endif
}

std::string AutomationServer::invoke(const std::string&        method,
                                     const std::string&        params_json,
                                     std::chrono::milliseconds timeout)
{
#ifdef _WIN32
    (void)method;
    (void)params_json;
    (void)timeout;
    return json_error(0, "Automation is not supported on this platform");
#else
    if (!running_.load(std::memory_order_relaxed))
        return json_error(0, "Automation server is not running");

    AutomationRequest req;
    req.id                = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    req.method            = method;
    req.params_json       = params_json.empty() ? "{}" : params_json;
    const uint64_t req_id = req.id;

    {
        std::lock_guard lock(pending_mutex_);
        pending_.push_back(std::move(req));
    }

    // Wake the main thread if it's sleeping in glfwWaitEventsTimeout().
    #ifdef SPECTRA_USE_GLFW
    glfwPostEmptyEvent();
    #endif

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (running_.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard lock(pending_mutex_);
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
        {
            if (it->id == req_id && it->responded)
            {
                std::string response = std::move(it->response_json);
                pending_.erase(it);
                return response;
            }
        }
    }

    {
        std::lock_guard lock(pending_mutex_);
        pending_.erase(std::remove_if(pending_.begin(),
                                      pending_.end(),
                                      [req_id](const AutomationRequest& pending_req)
                                      { return pending_req.id == req_id; }),
                       pending_.end());
    }

    return json_error(req_id, "Timeout");
#endif
}

// ─── Listener thread ─────────────────────────────────────────────────────────

void AutomationServer::listener_thread_fn()
{
#ifndef _WIN32
    while (running_.load(std::memory_order_relaxed))
    {
        struct pollfd pfd
        {
        };
        pfd.fd     = listen_fd_;
        pfd.events = POLLIN;
        int ret    = ::poll(&pfd, 1, 200);
        if (ret <= 0 || !(pfd.revents & POLLIN))
            continue;

        struct sockaddr_un ca
        {
        };
        socklen_t cl  = sizeof(ca);
        int       cfd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&ca), &cl);
        if (cfd < 0)
            continue;

        {
            std::lock_guard lock(clients_mutex_);
            client_fds_.push_back(cfd);
        }
        SPECTRA_LOG_DEBUG("automation", "Client connected fd=" + std::to_string(cfd));
        std::thread(&AutomationServer::handle_client, this, cfd).detach();
    }
#endif
}

void AutomationServer::handle_client(int client_fd)
{
#ifndef _WIN32
    std::string buffer;
    char        chunk[4096];

    while (running_.load(std::memory_order_relaxed))
    {
        ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        buffer.append(chunk, static_cast<size_t>(n));

        size_t nl = 0;
        while ((nl = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (line.empty() || line[0] != '{')
                continue;

            AutomationRequest req;
            if (!parse_request(line, req))
            {
                send_response(client_fd, json_error(0, "Invalid JSON"));
                continue;
            }
            uint64_t req_id = req.id;
            req.client_fd   = client_fd;

            {
                std::lock_guard lock(pending_mutex_);
                pending_.push_back(std::move(req));
            }

            // Wake the main thread if it's sleeping in glfwWaitEventsTimeout().
    #ifdef SPECTRA_USE_GLFW
            glfwPostEmptyEvent();
    #endif

            // Wait for main thread to execute (max 30s)
            auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            std::string response;
            bool        got = false;

            while (!got && running_.load(std::memory_order_relaxed)
                   && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard lock(pending_mutex_);
                for (auto it = pending_.begin(); it != pending_.end(); ++it)
                {
                    if (it->id == req_id && it->responded)
                    {
                        response = std::move(it->response_json);
                        got      = true;
                        pending_.erase(it);
                        break;
                    }
                }
            }

            send_response(client_fd, got ? response : json_error(req_id, "Timeout"));
        }
    }

    ::close(client_fd);
    {
        std::lock_guard lock(clients_mutex_);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                          client_fds_.end());
    }
#else
    (void)client_fd;
#endif
}

bool AutomationServer::parse_request(const std::string& json_str, AutomationRequest& req)
{
    req.id          = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    req.method      = json_get_string(json_str, "method");
    req.params_json = json_get_object(json_str, "params");
    return !req.method.empty();
}

void AutomationServer::send_response(int fd, const std::string& json)
{
#ifndef _WIN32
    std::string msg   = json + "\n";
    size_t      total = 0;
    while (total < msg.size())
    {
        ssize_t w = ::write(fd, msg.data() + total, msg.size() - total);
        if (w <= 0)
            break;
        total += static_cast<size_t>(w);
    }
#else
    (void)fd;
    (void)json;
#endif
}

// ─── Main-thread poll ────────────────────────────────────────────────────────

void AutomationServer::poll(App& app, WindowUIContext* ui_ctx)
{
    poll_pending([this, &app, ui_ctx](AutomationRequest& request)
                 { execute(request, app, ui_ctx); },
                 1);
}

void AutomationServer::poll(const RequestDispatcher& dispatcher, uint32_t frames_elapsed)
{
    if (dispatcher)
        poll_pending(dispatcher, frames_elapsed);
}

void AutomationServer::poll_pending(const RequestDispatcher& dispatcher, uint32_t frames_elapsed)
{
    {
        std::lock_guard lock(pending_mutex_);

        // Tick down wait_frames counters for deferred requests
        for (auto& pr : pending_)
        {
            if (pr.wait_frames > 0 && !pr.responded)
            {
                const uint32_t remaining = static_cast<uint32_t>(pr.wait_frames);
                pr.wait_frames =
                    frames_elapsed >= remaining ? 0 : static_cast<int>(remaining - frames_elapsed);
                if (pr.wait_frames <= 0)
                {
                    pr.response_json = json_ok(pr.id, "{\"waited\":true}");
                    pr.responded     = true;
                }
            }
        }

        // Intercept wait_frames requests: parse the count and set it
        // directly on the pending entry so they defer without executing.
        for (auto& pr : pending_)
        {
            if (!pr.responded && pr.wait_frames <= 0 && pr.method == "wait_frames")
            {
                int count = static_cast<int>(json_get_number(pr.params_json, "count", 1));
                if (count < 1)
                    count = 1;
                if (count > 600)
                    count = 600;
                pr.wait_frames = count;
            }
        }
    }

    std::vector<AutomationRequest> to_exec;
    {
        std::lock_guard lock(pending_mutex_);
        for (auto& r : pending_)
            if (!r.responded && r.wait_frames <= 0)
                to_exec.push_back(r);
    }
    for (auto& req : to_exec)
    {
        dispatcher(req);
        std::lock_guard lock(pending_mutex_);
        for (auto& pr : pending_)
        {
            if (pr.id == req.id)
            {
                pr.responded     = true;
                pr.response_json = req.response_json;
                break;
            }
        }
    }
}

// ─── Command dispatch ────────────────────────────────────────────────────────

void AutomationServer::execute(AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
{
    auto t0 = std::chrono::steady_clock::now();
    auto it = handlers_.find(req.method);
    if (it != handlers_.end())
    {
        it->second(req, &app, ui_ctx);
    }
    else
    {
        req.response_json = json_error(req.id, "Unknown method: " + req.method);
    }
    auto elapsed_us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    PerfMetrics::instance().record_automation_latency(req.method, elapsed_us);
}

}   // namespace spectra
