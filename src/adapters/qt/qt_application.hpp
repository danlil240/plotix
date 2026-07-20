#pragma once

// QtApplicationController — owns and coordinates the Qt desktop frontend.
//
// Phase 3 component: bootstrap for the production Qt application.
// Owns:
//   - QtRuntime (Vulkan backend + renderer + QVulkanInstance)
//   - ApplicationServices (framework-neutral services)
//   - QtActionBridge (QActions from CommandRegistry)
//   - SpectraMainWindow (native shell)
//   - Qt frontend service implementations (injected into ApplicationServices)
//
// Lifetime: created in main(), lives for the duration of the application.

#include <memory>

#include <spectra/fwd.hpp>

namespace spectra
{
class ApplicationServices;
class FigureRegistry;
class VulkanBackend;
class Renderer;

namespace ui
{
class ThemeManager;
}

namespace adapters::qt
{

class SpectraMainWindow;
class QtActionBridge;
class QtDialogService;
class QtClipboardService;
class QtRedrawRequest;
class QtWindowService;
class QtRuntime;
class MainWindowRegistry;
class QtWorkspaceBridge;
class QtAutomationAdapter;
class QtIpcClient;

}   // namespace adapters::qt

class WorkspaceAutosave;

namespace adapters::qt
{

class QtApplicationController
{
   public:
    QtApplicationController();
    ~QtApplicationController();

    QtApplicationController(const QtApplicationController&)            = delete;
    QtApplicationController& operator=(const QtApplicationController&) = delete;
    QtApplicationController(QtApplicationController&&)                 = delete;
    QtApplicationController& operator=(QtApplicationController&&)      = delete;

    // Initialize Vulkan runtime, services, and main window.
    // Returns true on success.
    bool init();

    // Shut down all resources.  Safe to call multiple times.
    void shutdown();

    bool is_initialized() const { return initialized_; }

    // ── Accessors ───────────────────────────────────────────────────────────

    SpectraMainWindow*    main_window()    { return main_window_.get(); }
    QtRuntime*            runtime()        { return runtime_.get(); }
    ApplicationServices*  services()       { return services_.get(); }
    QtActionBridge*       action_bridge()  { return action_bridge_.get(); }
    MainWindowRegistry*   window_registry() { return window_registry_.get(); }
    QtWorkspaceBridge*    workspace_bridge() { return workspace_bridge_.get(); }
    QtAutomationAdapter*  automation_adapter() { return automation_adapter_.get(); }
    QtIpcClient*          ipc_client()         { return ipc_client_.get(); }
    FigureRegistry&       figure_registry();

    // Check for autosave file and prompt user to restore. Call after init().
    void check_crash_recovery();

    // Start in-process topic server (so Python publishers can connect).
    // Only on non-Windows. Returns the socket path (empty on failure).
    std::string start_topic_server();

    // Connect to a running daemon via IPC socket (multiprocess mode).
    // Returns true on success. After this, IPC messages drive figure updates.
    bool connect_to_daemon(const std::string& socket_path);

   private:
    bool initialized_ = false;

    // Core infrastructure (owned)
    std::unique_ptr<ui::ThemeManager>  theme_mgr_;
    std::unique_ptr<VulkanBackend>     backend_;
    std::unique_ptr<Renderer>          renderer_;
    std::unique_ptr<QtRuntime>         runtime_;
    std::unique_ptr<ApplicationServices> services_;

    // Qt frontend (owned)
    std::unique_ptr<QtActionBridge>      action_bridge_;
    std::unique_ptr<SpectraMainWindow>   main_window_;
    std::unique_ptr<MainWindowRegistry>  window_registry_;
    std::unique_ptr<QtWorkspaceBridge>   workspace_bridge_;
    std::unique_ptr<QtAutomationAdapter> automation_adapter_;
    std::unique_ptr<WorkspaceAutosave>   autosave_;
    std::unique_ptr<QtIpcClient>         ipc_client_;

    // Frontend service implementations (owned, injected into ApplicationServices)
    std::unique_ptr<QtDialogService>     dialog_service_;
    std::unique_ptr<QtClipboardService>  clipboard_service_;
    std::unique_ptr<QtRedrawRequest>     redraw_request_;
    std::unique_ptr<QtWindowService>     window_service_;
};

}   // namespace spectra::adapters::qt
}   // namespace spectra
