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
    #include "adapters/qt/qt_application.hpp"
    #include "adapters/qt/qt_main_window.hpp"

    using namespace spectra::adapters::qt;

    #include <spectra/logger.hpp>

    #include <cstdlib>
    #include <cstring>
    #include <iostream>
    #include <string>

    #ifndef _WIN32
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
        main_window->set_status("Connected to daemon");
    }
    else
    {
        // ── In-process mode: start topic server and show the same empty
        //    welcome workspace as the legacy application. Production startup must
        //    never create sample documents or mutate the layout on a timer.
        controller.start_topic_server();
        auto* main_window = controller.main_window();
        main_window->show();

        // Native recovery prompts are intentionally disabled under automation;
        // interactive startup should offer the persisted session.
        const char* automation = std::getenv("SPECTRA_AUTOMATION");
        if (!automation || !*automation)
            controller.check_crash_recovery();
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
