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
#include <QPointer>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include <spectra/fwd.hpp>

class QWidget;
class QWindow;

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

enum class QtCaptureScope
{
    Canvas,
    Window,
};

struct QtCaptureResult
{
    std::string path;
    std::string png_base64;
    uint32_t    width  = 0;
    uint32_t    height = 0;

    bool valid() const { return width > 0 && height > 0; }
};

class QtAutomationAdapter : public QObject
{
    Q_OBJECT

   public:
    using ExecuteCommandFn = std::function<bool(const std::string& command_id)>;
    using CreateFigureFn   = std::function<FigureId(uint32_t w, uint32_t h)>;
    using SwitchFigureFn   = std::function<bool(FigureId figure_id)>;
    using GetStateFn       = std::function<std::string()>;
    using ListMenusFn      = std::function<std::string()>;
    using CaptureSurfaceFn =
        std::function<QtCaptureResult(QtCaptureScope scope, const std::string& path)>;
    using ResizeWindowFn    = std::function<void(uint32_t w, uint32_t h)>;
    using GetWindowSizeFn   = std::function<std::pair<uint32_t, uint32_t>()>;
    using GetCanvasWindowFn = std::function<QWindow*()>;
    using PumpFramesFn      = std::function<uint32_t(uint32_t count)>;
    using GetFrameCountFn   = std::function<uint64_t()>;

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
    void set_switch_figure(SwitchFigureFn fn) { switch_figure_fn_ = std::move(fn); }
    void set_get_state(GetStateFn fn) { get_state_fn_ = std::move(fn); }
    void set_list_menus(ListMenusFn fn) { list_menus_fn_ = std::move(fn); }
    void set_capture_surface(CaptureSurfaceFn fn) { capture_fn_ = std::move(fn); }
    void set_resize_window(ResizeWindowFn fn) { resize_fn_ = std::move(fn); }
    void set_get_window_size(GetWindowSizeFn fn) { get_size_fn_ = std::move(fn); }
    void set_frame_callbacks(PumpFramesFn pump_frames, GetFrameCountFn get_frame_count)
    {
        pump_frames_fn_     = std::move(pump_frames);
        get_frame_count_fn_ = std::move(get_frame_count);
    }
    void set_input_target(QWidget* root, GetCanvasWindowFn canvas_window = {});

   private slots:
    void on_poll_timeout();

   private:
    void handle_request(AutomationRequest& request);
    bool handle_input_request(AutomationRequest& request);

    ApplicationServices* services_   = nullptr;
    QTimer*              poll_timer_ = nullptr;
    std::atomic<bool>    running_{false};
    bool                 owns_automation_ = false;

    ExecuteCommandFn  execute_cmd_fn_;
    CreateFigureFn    create_figure_fn_;
    SwitchFigureFn    switch_figure_fn_;
    GetStateFn        get_state_fn_;
    ListMenusFn       list_menus_fn_;
    CaptureSurfaceFn  capture_fn_;
    ResizeWindowFn    resize_fn_;
    GetWindowSizeFn   get_size_fn_;
    PumpFramesFn      pump_frames_fn_;
    GetFrameCountFn   get_frame_count_fn_;
    uint64_t          last_frame_count_ = 0;
    QPointer<QWidget> input_root_;
    GetCanvasWindowFn canvas_window_fn_;
};

}   // namespace spectra::adapters::qt
