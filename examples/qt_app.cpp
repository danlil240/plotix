// qt_app.cpp — Production Qt 6 desktop application entry point.
//
// Supports two runtime modes:
//   1. In-process (default): standalone app with welcome screen, creates its
//      own Vulkan backend, renderer, and InprocTopicServer so Python
//      publishers can connect directly.
//   2. Multiprocess (--socket <path>): connects to a running spectra-backend
//      daemon via IPC, receives figure snapshots/diffs, and renders them
//      using the Qt Vulkan canvas. This is the Qt equivalent of
//      spectra-window (the GLFW/SDL3 agent).
//
// Build (requires Qt6):
//   cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_APP=ON \
//         -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make spectra-qt-app
//
// Usage:
//   ./spectra-qt-app                      # in-process mode
//   ./spectra-qt-app --socket /path.sock  # multiprocess (agent) mode

#ifdef SPECTRA_HAS_QT6

    #include <QApplication>
    #include <QTimer>

    #include "adapters/qt/qt_application.hpp"
    #include "adapters/qt/qt_main_window.hpp"
    #include "adapters/qt/docking/main_window_registry.hpp"
    #include "adapters/qt/docking/docking_host.hpp"
    #include "adapters/qt/docking/native_qt_docking_host.hpp"

    using namespace spectra::adapters::qt;

    #include <spectra/figure.hpp>
    #include <spectra/figure_registry.hpp>
    #include <spectra/logger.hpp>

    #include <cmath>
    #include <cstring>
    #include <iostream>
    #include <string>

    #ifndef _WIN32
        #include <cstdlib>
        #include <filesystem>
        #include <algorithm>
        #include <vector>
        #include <fcntl.h>
        #include <sys/socket.h>
        #include <sys/un.h>
        #include <unistd.h>
    #endif

namespace
{

#ifndef _WIN32

bool socket_is_live(const std::string& path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        ::close(fd);
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

std::string discover_live_daemon_socket()
{
    namespace fs = std::filesystem;
    std::string dir;
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
        dir = xdg;
    else
        dir = "/tmp";

    std::vector<std::pair<std::string, fs::file_time_type>> hits;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
    {
        if (ec) break;
        const auto& p    = e.path();
        const auto  name = p.filename().string();
        if (name.rfind("spectra-", 0) != 0 || p.extension() != ".sock")
            continue;
        auto mtime = fs::last_write_time(p, ec);
        if (ec) { ec.clear(); continue; }
        hits.emplace_back(p.string(), mtime);
    }
    std::sort(hits.begin(), hits.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& h : hits)
    {
        if (socket_is_live(h.first))
            return h.first;
    }
    return {};
}

#endif // !_WIN32

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Spectra");
    app.setOrganizationName("Spectra");

    // Parse --socket <path> argument
    std::string socket_path;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--socket") == 0)
        {
            socket_path = argv[i + 1];
            break;
        }
    }

    // Parse --version / --help
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)
        {
#ifdef SPECTRA_VERSION_STRING
            std::cout << "spectra-qt-app " << SPECTRA_VERSION_STRING << "\n";
#else
            std::cout << "spectra-qt-app (version unknown)\n";
#endif
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            std::cout << "Usage: spectra-qt-app [OPTIONS]\n"
                      << "\n"
                      << "GPU-accelerated scientific plotting application (Qt 6 frontend).\n"
                      << "\n"
                      << "Options:\n"
                      << "  --socket <path>  Connect to daemon at Unix socket path (agent mode)\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

#ifndef _WIN32
    // Auto-discover daemon if no socket specified and discovery not disabled
    if (socket_path.empty())
    {
        const char* off = std::getenv("SPECTRA_NO_DAEMON_DISCOVERY");
        if (!off || off[0] == '\0' || off[0] == '0')
        {
            socket_path = discover_live_daemon_socket();
        }
    }
#endif

    // Initialize the Qt application controller
    spectra::adapters::qt::QtApplicationController controller;
    if (!controller.init())
    {
        std::cerr << "FAIL: QtApplicationController::init() failed\n";
        return 1;
    }
    std::cout << "PASS: QtApplicationController::init()\n";

    const bool multiprocess = !socket_path.empty();

    if (multiprocess)
    {
        // ── Multiprocess mode: connect to daemon via IPC ──
        if (!controller.connect_to_daemon(socket_path))
        {
            std::cerr << "FAIL: Failed to connect to daemon: " << socket_path << "\n";
            return 1;
        }
        std::cout << "PASS: Connected to daemon: " << socket_path << "\n";

        auto* main_window = controller.main_window();
        main_window->show();
        main_window->set_status("Connected to daemon — Spectra Qt frontend (Phase 7: multiprocess IPC)");
    }
    else
    {
        // ── In-process mode: start topic server, create demo figures ──
        controller.start_topic_server();

        auto& registry = controller.figure_registry();

        // Create a sample figure
        auto fig = std::make_unique<spectra::Figure>();
        auto& ax = fig->subplot(1, 1, 1);

        constexpr int k_points = 200;
        std::vector<float> x(k_points), y(k_points);
        for (int i = 0; i < k_points; ++i)
        {
            const float t = static_cast<float>(i) * 0.05f;
            x[i] = t;
            y[i] = std::sin(t) * 0.5f * std::cos(2.0f * t);
        }
        ax.line(x, y).label("signal").color(spectra::colors::cyan).width(2.0f);
        ax.title("Spectra Qt Application");
        ax.xlabel("t");
        ax.ylabel("amplitude");
        ax.grid(true);
        ax.auto_fit();

        auto fig_id = registry.register_figure(std::move(fig));

        auto* main_window = controller.main_window();
        main_window->add_figure_tab(fig_id);

        // Create a second figure to demonstrate multi-tab
        auto fig2 = std::make_unique<spectra::Figure>();
        auto& ax2 = fig2->subplot(1, 1, 1);
        std::vector<float> x2(100), y2(100);
        for (int i = 0; i < 100; ++i)
        {
            x2[i] = static_cast<float>(i) * 0.1f;
            y2[i] = std::cos(x2[i]) * std::exp(-x2[i] * 0.05f);
        }
        ax2.line(x2, y2).label("damped").color(spectra::colors::orange).width(2.0f);
        ax2.title("Damped Cosine");
        ax2.xlabel("t");
        ax2.ylabel("amplitude");
        ax2.grid(true);
        ax2.auto_fit();
        auto fig2_id = registry.register_figure(std::move(fig2));
        main_window->add_figure_tab(fig2_id);

        // Create a third figure for split view demo
        auto fig3 = std::make_unique<spectra::Figure>();
        auto& ax3 = fig3->subplot(1, 1, 1);
        std::vector<float> x3(150), y3(150);
        for (int i = 0; i < 150; ++i)
        {
            x3[i] = static_cast<float>(i) * 0.04f;
            y3[i] = std::sin(x3[i] * 3.0f) * std::exp(-x3[i] * 0.1f);
        }
        ax3.line(x3, y3).label("wavelet").color(spectra::colors::magenta).width(2.0f);
        ax3.title("Damped Sine");
        ax3.xlabel("t");
        ax3.ylabel("amplitude");
        ax3.grid(true);
        ax3.auto_fit();
        auto fig3_id = registry.register_figure(std::move(fig3));
        (void)fig3_id; // used by split_right() demo — picked up from registry

        main_window->show();
        main_window->set_status("Ready — Spectra Qt frontend (Phase 7: in-process + multiprocess IPC, topic server)");

        // Demonstrate programmatic detach after 2 seconds
        QTimer::singleShot(2000, [&controller, fig2_id]() {
            auto* reg = controller.window_registry();
            if (reg)
            {
                auto host_id = reg->find_host_for_figure(fig2_id);
                if (host_id != INVALID_HOST_ID)
                {
                    auto* host = reg->native_host(host_id);
                    if (host)
                    {
                        host->detach_document(static_cast<DocumentId>(fig2_id));
                        std::cout << "PASS: detached figure " << fig2_id
                                  << " into new window\n";
                    }
                }
            }
        });

        // Demonstrate split view after 4 seconds
        QTimer::singleShot(4000, [&controller]() {
            auto* mw = controller.main_window();
            if (mw && mw->split_right())
                std::cout << "PASS: split right\n";
            else
                std::cout << "INFO: split right not applied (need 3+ figures)\n";
        });
    }

    // Enter the Qt event loop
    int result = app.exec();

    // Clean shutdown
    controller.shutdown();
    std::cout << "PASS: QtApplicationController::shutdown()\n";
    return result;
}

#else

    #include <iostream>

int main()
{
    std::cout << "This application requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_APP=ON "
                 "-DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
