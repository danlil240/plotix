#pragma once

// QtAutomationAdapter — Maps the external automation/MCP protocol to Qt objects
// and ApplicationServices for the Qt desktop frontend.
//
// Phase 6 component: enables automation, testing, and external control of the
// Qt frontend without depending on ImGui, App, or WindowUIContext.
//
// The adapter:
//   - Reuses the single MCP/automation server pair owned by ApplicationServices
//   - Provides a Qt timer-based polling mechanism (replaces AutomationServer::poll)
//   - Implements key automation methods using ApplicationServices and Qt objects
//   - Exposes stable Qt object names for automation testing
//   - Honors SPECTRA_AUTOMATION and --no-native-dialogs environment variables

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include <spectra/fwd.hpp>

namespace spectra
{
class ApplicationServices;
class FigureRegistry;
struct AutomationRequest;
}   // namespace spectra

namespace spectra::adapters::qt
{

class SpectraMainWindow;
class QtRuntime;

class QtAutomationAdapter : public QObject
{
    Q_OBJECT

   public:
    using ExecuteCommandFn    = std::function<bool(const std::string& command_id)>;
    using CreateFigureFn      = std::function<FigureId(uint32_t w, uint32_t h)>;
    using GetStateFn          = std::function<std::string()>;
    using CaptureScreenshotFn = std::function<std::string(const std::string& path)>;
    using ResizeWindowFn      = std::function<void(uint32_t w, uint32_t h)>;
    using GetWindowSizeFn     = std::function<std::pair<uint32_t, uint32_t>()>;

    explicit QtAutomationAdapter(QObject* parent = nullptr);
    ~QtAutomationAdapter();

    QtAutomationAdapter(const QtAutomationAdapter&)            = delete;
    QtAutomationAdapter& operator=(const QtAutomationAdapter&) = delete;

    // Start or reuse ApplicationServices' MCP endpoint and begin Qt dispatch.
    bool start(ApplicationServices* services, uint16_t port = 0);

    // Stop dispatch and any endpoint started by this adapter.
    void stop();

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    // Set callbacks for automation operations.
    void set_execute_command(ExecuteCommandFn fn) { execute_cmd_fn_ = std::move(fn); }
    void set_create_figure(CreateFigureFn fn) { create_figure_fn_ = std::move(fn); }
    void set_get_state(GetStateFn fn) { get_state_fn_ = std::move(fn); }
    void set_capture_screenshot(CaptureScreenshotFn fn) { capture_fn_ = std::move(fn); }
    void set_resize_window(ResizeWindowFn fn) { resize_fn_ = std::move(fn); }
    void set_get_window_size(GetWindowSizeFn fn) { get_size_fn_ = std::move(fn); }

   private slots:
    void on_poll_timeout();

   private:
    void handle_request(AutomationRequest& request);

    ApplicationServices* services_   = nullptr;
    QTimer*              poll_timer_ = nullptr;
    std::atomic<bool>    running_{false};
    bool                 owns_automation_ = false;

    ExecuteCommandFn    execute_cmd_fn_;
    CreateFigureFn      create_figure_fn_;
    GetStateFn          get_state_fn_;
    CaptureScreenshotFn capture_fn_;
    ResizeWindowFn      resize_fn_;
    GetWindowSizeFn     get_size_fn_;
};

}   // namespace spectra::adapters::qt
