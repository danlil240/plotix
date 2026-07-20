#pragma once

#include <memory>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>

namespace spectra
{

class PluginUIRegistry;
}

namespace spectra
{

class CommandQueue;
class FrameScheduler;
class SessionRuntime;
class Animator;
class PluginManager;
class ExportFormatRegistry;
class DataSourceRegistry;
class SeriesTypeRegistry;
class AutomationServer;
class McpServer;
class InprocTopicServer;
class CommandRegistry;
class ShortcutManager;
class UndoManager;
class CommandPalette;
class DialogService;
class ClipboardService;
class RedrawRequest;
class WindowService;

namespace ui::settings
{
class SettingsStore;
}

namespace ui
{
class ThemeManager;
}

// Framework-neutral owner of process-scoped application services.
//
// Coordinates all shared services that are independent of the frontend
// (ImGui, Qt, GLFW, SDL3).  No Qt, GLFW, SDL, or ImGui headers are
// included — this class is the boundary between the renderer/core and
// the frontend shell.
//
// Lifetime: created during App::init_runtime(), destroyed during
// App::shutdown_runtime().  Outlives individual windows and UI contexts.
class ApplicationServices
{
   public:
    ApplicationServices();
    ~ApplicationServices();

    ApplicationServices(const ApplicationServices&)            = delete;
    ApplicationServices& operator=(const ApplicationServices&) = delete;

    // Initialize core services (settings, registries, command/shortcut/undo).
    // Does NOT create backend/renderer/session — those are injected.
    // fps configures the FrameScheduler target rate.
    void init(FigureRegistry&    registry,
              Backend&           backend,
              Renderer&          renderer,
              ui::ThemeManager&  theme_mgr,
              float              fps = 60.0f);

    // Shut down all services in reverse order.
    void shutdown();

    bool is_initialized() const { return initialized_; }

    // ── Service accessors ───────────────────────────────────────────────────

    FigureRegistry&    figures()    { return *registry_; }
    Backend&           backend()    { return *backend_; }
    Renderer&          renderer()   { return *renderer_; }
    ui::ThemeManager&  theme()      { return *theme_mgr_; }

    SessionRuntime&       session()       { return *session_; }
    FrameScheduler&       scheduler()     { return *scheduler_; }
    Animator&             animator()      { return *animator_; }
    CommandQueue&         command_queue() { return *cmd_queue_; }

    CommandRegistry&      commands()      { return *cmd_registry_; }
    ShortcutManager&      shortcuts()     { return *shortcut_mgr_; }
    UndoManager&          undo()          { return *undo_mgr_; }
#ifdef SPECTRA_USE_IMGUI
    CommandPalette&       command_palette() { return *cmd_palette_; }
#endif

    PluginManager&        plugins()       { return *plugin_mgr_; }
    PluginUIRegistry&     plugin_ui()     { return *plugin_ui_registry_; }
    ExportFormatRegistry& export_formats() { return *export_format_registry_; }
    DataSourceRegistry&   data_sources()  { return *data_source_registry_; }
    SeriesTypeRegistry&   series_types()  { return *series_type_registry_; }

    ui::settings::SettingsStore& settings() { return *settings_store_; }

    // Automation (may be null if not started)
    AutomationServer*    automation()    { return auto_server_.get(); }
    McpServer*           mcp()           { return mcp_server_.get(); }
    InprocTopicServer*   topic_server()  { return topic_server_.get(); }

    // ── Automation lifecycle ────────────────────────────────────────────────

    // Start automation/MCP servers.  Returns true if automation started
    // (or was already running).  Socket path may be empty for default.
    bool start_automation(const std::string& socket_path,
                          const std::string& mcp_bind,
                          uint16_t           mcp_port);

    // Stop automation/MCP servers.
    void stop_automation();

    // ── Inproc topic server ─────────────────────────────────────────────────

#ifndef _WIN32
    bool start_topic_server(FigureRegistry& registry);
    void stop_topic_server();
#endif

    // ── Frontend service injection ──────────────────────────────────────────
    // These are set by the frontend (ImGui, Qt, or test stub) after init().
    // They allow commands and automation to interact with the frontend
    // without knowing which UI framework is active.

    void set_dialog_service(DialogService* svc)    { dialog_service_ = svc; }
    void set_clipboard_service(ClipboardService* svc) { clipboard_service_ = svc; }
    void set_redraw_request(RedrawRequest* req)    { redraw_request_ = req; }
    void set_window_service(WindowService* svc)    { window_service_ = svc; }

    DialogService*    dialog_service()    { return dialog_service_; }
    ClipboardService* clipboard_service() { return clipboard_service_; }
    RedrawRequest*    redraw_request()    { return redraw_request_; }
    WindowService*    window_service()    { return window_service_; }

   private:
    bool initialized_ = false;

    // Injected (not owned)
    FigureRegistry*   registry_  = nullptr;
    Backend*          backend_   = nullptr;
    Renderer*         renderer_  = nullptr;
    ui::ThemeManager* theme_mgr_ = nullptr;

    // Owned services
    std::unique_ptr<CommandQueue>          cmd_queue_;
    std::unique_ptr<FrameScheduler>        scheduler_;
    std::unique_ptr<Animator>              animator_;
    std::unique_ptr<SessionRuntime>        session_;

    std::unique_ptr<CommandRegistry>       cmd_registry_;
    std::unique_ptr<ShortcutManager>       shortcut_mgr_;
    std::unique_ptr<UndoManager>           undo_mgr_;
#ifdef SPECTRA_USE_IMGUI
    std::unique_ptr<CommandPalette>        cmd_palette_;
#endif

    std::unique_ptr<PluginManager>         plugin_mgr_;
    std::unique_ptr<PluginUIRegistry>      plugin_ui_registry_;
    std::unique_ptr<ExportFormatRegistry>  export_format_registry_;
    std::unique_ptr<DataSourceRegistry>    data_source_registry_;
    std::unique_ptr<SeriesTypeRegistry>    series_type_registry_;

    std::unique_ptr<ui::settings::SettingsStore> settings_store_;

    std::unique_ptr<AutomationServer>      auto_server_;
    std::unique_ptr<McpServer>             mcp_server_;
#ifndef _WIN32
    std::unique_ptr<InprocTopicServer>     topic_server_;
#endif

    // Frontend services (not owned — injected by the active frontend)
    DialogService*    dialog_service_    = nullptr;
    ClipboardService* clipboard_service_ = nullptr;
    RedrawRequest*    redraw_request_    = nullptr;
    WindowService*    window_service_    = nullptr;
};

}   // namespace spectra
