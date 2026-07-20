// qt_application.cpp — QtApplicationController implementation.

#include "qt_application.hpp"

#include "docking/main_window_registry.hpp"
#include "docking/docking_host.hpp"
#include "docking/native_qt_docking_host.hpp"
#include "qt_action_bridge.hpp"
#include "figure_canvas_widget.hpp"
#include "qt_frontend_services.hpp"
#include "qt_main_window.hpp"
#include "qt_runtime.hpp"
#include "qt_workspace_bridge.hpp"
#include "qt_automation_adapter.hpp"
#include "qt_ipc_client.hpp"

#include "ui/workspace/workspace.hpp"
#include "ui/workspace/workspace_autosave.hpp"

#include "ipc/figure_snapshot.hpp"

#include "app/inproc_topic_server.hpp"

#include <QObject>
#include <QMessageBox>
#include <QTimer>

#include "app/application_services.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/theme/theme.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <cstdlib>

namespace spectra::adapters::qt
{

QtApplicationController::QtApplicationController() = default;

QtApplicationController::~QtApplicationController()
{
    shutdown();
}

bool QtApplicationController::init()
{
    if (initialized_)
        return true;

    // 1. Create Vulkan backend + renderer (shared infrastructure)
    backend_ = std::make_unique<VulkanBackend>();
    if (!backend_->init(false))
    {
        SPECTRA_LOG_ERROR("qt_app", "Failed to initialize Vulkan backend");
        return false;
    }

    theme_mgr_ = std::make_unique<ui::ThemeManager>();
    ui::ThemeManager::set_current(theme_mgr_.get());
    theme_mgr_->ensure_initialized();

    renderer_ = std::make_unique<Renderer>(*backend_, *theme_mgr_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("qt_app", "Failed to initialize renderer");
        return false;
    }

    // 2. Create Qt runtime (owns QVulkanInstance wrapping our VkInstance)
    runtime_ = std::make_unique<QtRuntime>();
    if (!runtime_->init())
    {
        SPECTRA_LOG_ERROR("qt_app", "Failed to initialize Qt runtime");
        return false;
    }

    // 3. Create ApplicationServices
    // Use a static registry owned by the controller.
    // The registry is populated by user code (App::figure()) or by
    // the QtApplicationController itself for standalone operation.
    services_ = std::make_unique<ApplicationServices>();
    services_->init(figure_registry(), *backend_, *renderer_, *theme_mgr_, 60.0f);

    // 4. Create QtActionBridge from the shared CommandRegistry
    action_bridge_ = std::make_unique<QtActionBridge>(services_->commands());
    action_bridge_->rebuild();

    // 5. Create frontend service implementations and inject them
    dialog_service_    = std::make_unique<QtDialogService>();
    clipboard_service_ = std::make_unique<QtClipboardService>();
    redraw_request_    = std::make_unique<QtRedrawRequest>(
        [this]() {
            // Request redraw on all canvases in the main window
            if (main_window_)
            {
                auto ids = figure_registry().all_ids();
                for (auto id : ids)
                {
                    auto* canvas = main_window_->canvas_for(id);
                    if (canvas && canvas->vulkanWindow())
                        canvas->vulkanWindow()->requestFrame();
                }
            }
        });
    window_service_    = std::make_unique<QtWindowService>();

    // Wire window service callbacks
    window_service_->set_create_window(
        [this](const std::string& title, uint32_t w, uint32_t h) -> FigureId {
            // Create a new figure and add it as a tab
            auto fig = std::make_unique<Figure>();
            fig->set_size(w, h);
            fig->set_tab_title(title);
            FigureId id = figure_registry().register_figure(std::move(fig));
            if (main_window_)
                main_window_->add_figure_tab(id);
            return id;
        });
    window_service_->set_close_window(
        [this](FigureId id) {
            if (main_window_)
                main_window_->close_figure_tab(id);
        });
    window_service_->set_focus_window(
        [](FigureId id) {
            // Focus is handled by Qt's tab widget
            (void)id;
        });
    window_service_->set_window_count(
        [this]() -> size_t {
            return main_window_ ? static_cast<size_t>(main_window_->figure_tab_count()) : 0;
        });

    services_->set_dialog_service(dialog_service_.get());
    services_->set_clipboard_service(clipboard_service_.get());
    services_->set_redraw_request(redraw_request_.get());
    services_->set_window_service(window_service_.get());

    // 6. Create the main window registry (for multi-window support)
    window_registry_ = std::make_unique<MainWindowRegistry>(
        runtime_.get(), &figure_registry(), action_bridge_.get(), services_.get());

    // 6b. Create the workspace bridge for layout save/restore
    workspace_bridge_ = std::make_unique<QtWorkspaceBridge>(window_registry_.get());

    // 6c. Create the automation adapter (starts MCP server if SPECTRA_AUTOMATION is set)
    automation_adapter_ = std::make_unique<QtAutomationAdapter>();
    const char* env_auto = std::getenv("SPECTRA_AUTOMATION");
    if (env_auto && *env_auto)
    {
        automation_adapter_->start(services_.get());
    }

    // 6d. Create the workspace autosave manager
    autosave_ = std::make_unique<WorkspaceAutosave>();
    autosave_->set_serialize_fn([this]() -> std::string {
        WorkspaceData data;
        data.version = WorkspaceData::FORMAT_VERSION;
        // TODO: populate from current application state
        if (workspace_bridge_)
            workspace_bridge_->capture_layout(data);
        return Workspace::serialize_json(data);
    });

    // 7. Create the main window
    main_window_ = std::make_unique<SpectraMainWindow>(
        runtime_.get(), &figure_registry(), action_bridge_.get(), services_.get());

    // 8. Register the main window with the registry
    window_registry_->register_window(main_window_.get());

    // 9. Wire up figure detach requests from the main window
    //    Use main_window_ as context since QtApplicationController is not a QObject.
    QObject::connect(main_window_.get(), &SpectraMainWindow::figure_detach_requested,
            main_window_.get(), [this](FigureId fid) {
                if (window_registry_)
                {
                    HostId new_host = window_registry_->create_detached_window();
                    if (new_host != INVALID_HOST_ID)
                    {
                        auto* host = window_registry_->native_host(new_host);
                        if (host)
                            host->add_figure_tab(fid);
                    }
                }
            });

    initialized_ = true;
    SPECTRA_LOG_INFO("qt_app", "QtApplicationController initialized");
    return true;
}

void QtApplicationController::shutdown()
{
    if (!initialized_)
        return;

    // Destroy main window first (it holds canvas widgets with Vulkan surfaces)
    main_window_.reset();

    // Close all detached windows
    window_registry_.reset();
    workspace_bridge_.reset();

    // Stop automation adapter
    if (automation_adapter_)
        automation_adapter_->stop();
    automation_adapter_.reset();

    // Save workspace on shutdown if autosave is configured
    if (autosave_)
        autosave_->save_now();
    autosave_.reset();

    // Shut down services
    if (services_)
        services_->shutdown();
    services_.reset();

    // Shut down Qt runtime (Vulkan surfaces + swapchains)
    if (runtime_)
        runtime_->shutdown();
    runtime_.reset();

    // Destroy renderer + backend
    renderer_.reset();
    ui::ThemeManager::set_current(nullptr);
    theme_mgr_.reset();

    if (backend_)
    {
        backend_->shutdown();
        backend_.reset();
    }

    // Frontend services
    window_service_.reset();
    redraw_request_.reset();
    clipboard_service_.reset();
    dialog_service_.reset();
    action_bridge_.reset();

    initialized_ = false;
    SPECTRA_LOG_INFO("qt_app", "QtApplicationController shutdown complete");
}

FigureRegistry& QtApplicationController::figure_registry()
{
    // The FigureRegistry is owned by the base App class in the legacy frontend.
    // For standalone Qt operation, we need a registry.  We use a static one
    // here — in the full migration, this will be owned by the controller.
    // TODO: This will be replaced when App is refactored to not own the registry.
    static FigureRegistry s_registry;
    return s_registry;
}

void QtApplicationController::check_crash_recovery()
{
    if (!autosave_ || !autosave_->has_autosave())
        return;

    // Use Qt dialog to prompt the user
    QMessageBox::StandardButton reply = QMessageBox::question(
        main_window_.get(),
        "Spectra — Crash Recovery",
        "An autosave file was found from a previous session.\n"
        "Would you like to restore your workspace?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (reply == QMessageBox::Yes)
    {
        std::string path = autosave_->autosave_path();
        WorkspaceData data;
        if (Workspace::load(path, data))
        {
            if (workspace_bridge_)
                workspace_bridge_->apply_layout(data);
            SPECTRA_LOG_INFO("qt_app", "Restored workspace from autosave: " + path);
        }
        else
        {
            SPECTRA_LOG_WARN("qt_app", "Failed to load autosave: " + path);
        }
    }

    // Clear the autosave regardless — user has been prompted
    Workspace::clear_autosave();
}

std::string QtApplicationController::start_topic_server()
{
#ifndef _WIN32
    if (!services_)
        return {};

    auto* topic_srv = services_->topic_server();
    if (topic_srv && topic_srv->is_running())
        return {};   // already running

    if (services_->start_topic_server(figure_registry()))
    {
        auto* ts = services_->topic_server();
        if (ts)
        {
            SPECTRA_LOG_INFO("qt_app", "InprocTopicServer started");
            return ts->socket_path();
        }
    }
#endif
    return {};
}

bool QtApplicationController::connect_to_daemon(const std::string& socket_path)
{
#ifndef _WIN32
    if (!main_window_)
        return false;

    ipc_client_ = std::make_unique<QtIpcClient>(main_window_.get());
    ipc_client_->set_figure_registry(&figure_registry());

    if (!ipc_client_->connect_to_daemon(socket_path))
    {
        ipc_client_.reset();
        return false;
    }

    // Wire IPC signals to update the main window
    auto* mw = main_window_.get();
    auto* client = ipc_client_.get();

    // On snapshot: rebuild registry and update tabs
    QObject::connect(client, &QtIpcClient::snapshot_received,
        mw, [this, client]() {
            auto& reg = figure_registry();
            auto ids = ipc::rebuild_registry_from_cache(
                reg, client->figure_cache(), 1280, 720);
            client->set_local_ids(ids);

            // Clear existing tabs and add new ones
            if (main_window_)
            {
                for (auto id : reg.all_ids())
                    main_window_->add_figure_tab(id);
            }
        });

    // On diff: request redraw (fast path already applied to live figures)
    QObject::connect(client, &QtIpcClient::diff_applied,
        mw, [this, client](bool needs_rebuild) {
            if (needs_rebuild)
            {
                auto& reg = figure_registry();
                auto ids = ipc::rebuild_registry_from_cache(
                    reg, client->figure_cache(), 1280, 720);
                client->set_local_ids(ids);

                if (main_window_)
                {
                    for (auto id : reg.all_ids())
                        main_window_->add_figure_tab(id);
                }
            }
            else
            {
                // Request redraw on all canvases
                auto ids = figure_registry().all_ids();
                for (auto id : ids)
                {
                    if (main_window_)
                    {
                        auto* canvas = main_window_->canvas_for(id);
                        if (canvas && canvas->vulkanWindow())
                            canvas->vulkanWindow()->requestFrame();
                    }
                }
            }
        });

    // On connection lost: log and optionally close
    QObject::connect(client, &QtIpcClient::connection_lost,
        mw, []() {
            SPECTRA_LOG_WARN("qt_app", "IPC connection to daemon lost");
        });

    // On close requested: close the main window
    QObject::connect(client, &QtIpcClient::close_requested,
        mw, [this]() {
            if (main_window_)
                main_window_->close();
        });

    // If the daemon already has figures, rebuild now
    if (!client->figure_cache().empty())
    {
        auto& reg = figure_registry();
        auto ids = ipc::rebuild_registry_from_cache(
            reg, client->figure_cache(), 1280, 720);
        client->set_local_ids(ids);

        for (auto id : reg.all_ids())
            main_window_->add_figure_tab(id);
    }

    SPECTRA_LOG_INFO("qt_app", "Connected to daemon: {}", socket_path);
    return true;
#else
    (void)socket_path;
    return false;
#endif
}

}   // namespace spectra::adapters::qt
