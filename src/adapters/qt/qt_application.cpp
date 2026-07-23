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
#include "split_view_container.hpp"

#include "ui/workspace/workspace.hpp"
#include "ui/workspace/workspace_autosave.hpp"
#include "ui/automation/automation_json.hpp"

#include "ipc/figure_snapshot.hpp"

#include "app/inproc_topic_server.hpp"

#include <QObject>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>

#include "app/application_services.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/input/input.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <cstdlib>
#include <algorithm>
#include <functional>
#include <sstream>

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

    // 1. Create the single process-scoped Qt/Vulkan/rendering runtime.
    runtime_ = std::make_unique<QtRuntime>();
    if (!runtime_->init())
    {
        SPECTRA_LOG_ERROR("qt_app", "Failed to initialize Qt runtime");
        return false;
    }

    // 2. Create ApplicationServices on the same backend/renderer/theme used
    // by the canvases. A separate stack here breaks command/export parity and
    // doubles Vulkan ownership.
    // Use a static registry owned by the controller.
    // The registry is populated by user code (App::figure()) or by
    // the QtApplicationController itself for standalone operation.
    services_ = std::make_unique<ApplicationServices>();
    services_->init(figure_registry(),
                    *runtime_->backend(),
                    *runtime_->renderer(),
                    *runtime_->theme_manager(),
                    60.0f);

    register_qt_commands();

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
            fig->subplot(1, 1, 1);
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
    automation_adapter_->set_execute_command(
        [this](const std::string& command_id)
        { return services_ && services_->commands().execute(command_id); });
    automation_adapter_->set_create_figure(
        [this](uint32_t width, uint32_t height) -> FigureId
        {
            auto figure = std::make_unique<Figure>();
            figure->set_size(width, height);
            figure->subplot(1, 1, 1);
            const FigureId id = figure_registry().register_figure(std::move(figure));
            if (main_window_)
                main_window_->add_figure_tab(id);
            return id;
        });
    automation_adapter_->set_get_state(
        [this]() -> std::string
        {
            const auto ids = figure_registry().all_ids();
            std::ostringstream state;
            state << "{\"figure_count\":" << ids.size();
            const FigureId active_id =
                main_window_ ? main_window_->active_figure_id() : INVALID_FIGURE_ID;
            if (active_id == INVALID_FIGURE_ID)
                state << ",\"active_figure_id\":null";
            else
                state << ",\"active_figure_id\":" << active_id;
            state << ",\"figures\":[";
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (i > 0)
                    state << ',';
                Figure* figure = figure_registry().get(ids[i]);
                state << "{\"id\":" << ids[i];
                if (figure)
                {
                    size_t series_count = 0;
                    figure->for_each_axes(
                        [&series_count](AxesBase* axes)
                        {
                            if (axes)
                                series_count += axes->series().size();
                        });
                    state << ",\"width\":" << figure->width() << ",\"height\":"
                          << figure->height() << ",\"axes_count\":"
                          << figure->all_axes().size() + figure->axes().size()
                          << ",\"total_series\":" << series_count << ",\"title\":\""
                          << json_escape(figure->tab_title()) << "\"";
                }
                state << '}';
            }
            state << "]}";
            return state.str();
        });
    automation_adapter_->set_capture_screenshot(
        [this](const std::string& path) -> std::string
        {
            if (!main_window_)
                return {};
            const QPixmap capture = main_window_->grab();
            if (capture.isNull() || !capture.save(QString::fromStdString(path), "PNG"))
                return {};
            return path;
        });
    automation_adapter_->set_resize_window(
        [this](uint32_t width, uint32_t height)
        {
            if (main_window_)
                main_window_->resize(static_cast<int>(width), static_cast<int>(height));
        });
    automation_adapter_->set_get_window_size(
        [this]() -> std::pair<uint32_t, uint32_t>
        {
            if (!main_window_)
                return {0, 0};
            return {static_cast<uint32_t>(main_window_->width()),
                    static_cast<uint32_t>(main_window_->height())};
        });
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

        // Capture figure and series data from the registry
        auto& reg = figure_registry();
        auto  ids = reg.all_ids();
        std::vector<Figure*> figures;
        figures.reserve(ids.size());
        for (auto id : ids)
        {
            Figure* f = reg.get(id);
            if (f)
                figures.push_back(f);
        }

        // Determine active figure index
        size_t active_index = 0;
        if (main_window_)
        {
            FigureId active_id = main_window_->active_figure_id();
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (ids[i] == active_id)
                {
                    active_index = i;
                    break;
                }
            }
        }

        // Get theme name
        std::string theme_name = "dark";
        if (services_)
            theme_name = services_->theme().current_theme_name();

        // Get panel states from main window
        bool  inspector_visible = false;
        float inspector_width   = 300.0f;
        bool  nav_rail_expanded = true;
        if (main_window_)
        {
            inspector_visible = main_window_->is_inspector_open();
            nav_rail_expanded = !main_window_->is_nav_rail_compact();
        }

        data = Workspace::capture(figures, active_index, theme_name,
                                  inspector_visible, inspector_width,
                                  nav_rail_expanded);

        // Also capture desktop layout (docking state)
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

void QtApplicationController::register_qt_commands()
{
    auto& commands = services_->commands();

    auto add = [&commands](std::string id,
                           std::string label,
                           std::string shortcut,
                           std::string category,
                           std::function<void()> callback)
    {
        commands.register_command(std::move(id),
                                  std::move(label),
                                  std::move(callback),
                                  std::move(shortcut),
                                  std::move(category));
    };

    auto active_figure = [this]() -> Figure*
    {
        if (!main_window_)
            return nullptr;
        const FigureId id = main_window_->active_figure_id();
        return id == INVALID_FIGURE_ID ? nullptr : figure_registry().get(id);
    };

    auto request_redraw = [this]()
    {
        if (redraw_request_)
            redraw_request_->request_redraw();
    };

    add("edit.undo", "Undo", "Ctrl+Z", "Edit",
        [this]() { services_->undo().undo(); });
    add("edit.redo", "Redo", "Ctrl+Y", "Edit",
        [this]() { services_->undo().redo(); });

    add("figure.new", "New Figure", "Ctrl+T", "Figure", [this]()
    {
        auto figure = std::make_unique<Figure>();
        figure->subplot(1, 1, 1);
        const FigureId id = figure_registry().register_figure(std::move(figure));
        if (main_window_)
            main_window_->add_figure_tab(id);
    });
    add("figure.close", "Close Figure", "Ctrl+W", "Figure", [this]()
    {
        if (!main_window_)
            return;
        const FigureId id = main_window_->active_figure_id();
        if (id == INVALID_FIGURE_ID)
            return;
        main_window_->close_figure_tab(id);
        figure_registry().unregister_figure(id);
    });

    auto activate_relative = [this](int delta)
    {
        if (!main_window_)
            return;
        const auto ids = main_window_->open_figure_ids();
        if (ids.empty())
            return;
        const auto it = std::find(ids.begin(), ids.end(), main_window_->active_figure_id());
        const int current = it == ids.end()
            ? 0
            : static_cast<int>(std::distance(ids.begin(), it));
        const int count = static_cast<int>(ids.size());
        const int next = (current + delta + count) % count;
        main_window_->central_view()->activate_figure(ids[static_cast<size_t>(next)]);
    };
    add("figure.next_tab", "Next Figure Tab", "Ctrl+Tab", "Figure",
        [activate_relative]() { activate_relative(1); });
    add("figure.prev_tab", "Previous Figure Tab", "Ctrl+Shift+Tab", "Figure",
        [activate_relative]() { activate_relative(-1); });
    for (int i = 0; i < 9; ++i)
    {
        add("figure.tab_" + std::to_string(i + 1),
            "Switch to Figure " + std::to_string(i + 1),
            "Alt+" + std::to_string(i + 1),
            "Figure",
            [this, i]()
            {
                if (!main_window_)
                    return;
                const auto ids = main_window_->open_figure_ids();
                if (static_cast<size_t>(i) < ids.size())
                    main_window_->central_view()->activate_figure(ids[static_cast<size_t>(i)]);
            });
    }

    auto reset_view = [active_figure, request_redraw]()
    {
        Figure* figure = active_figure();
        if (!figure)
            return;
        for (auto& axes : figure->axes_mut())
        {
            if (axes)
                axes->auto_fit();
        }
        for (auto& axes : figure->all_axes_mut())
        {
            if (axes)
                axes->auto_fit();
        }
        request_redraw();
    };
    add("view.reset", "Reset View", "R", "View", reset_view);
    add("view.autofit", "Auto-Fit Active Figure", "Shift+A", "View", reset_view);
    add("view.fullscreen", "Toggle Fullscreen", "F11", "View", [this]()
    {
        if (!main_window_)
            return;
        main_window_->isFullScreen() ? main_window_->showNormal()
                                     : main_window_->showFullScreen();
    });
    add("view.split_right", "Split Right", "Ctrl+\\", "View",
        [this]() { if (main_window_) main_window_->split_right(); });
    add("view.split_down", "Split Down", "Ctrl+Shift+\\", "View",
        [this]() { if (main_window_) main_window_->split_down(); });
    add("view.close_split", "Close Split Pane", "", "View",
        [this]() { if (main_window_) main_window_->close_split(); });
    add("view.reset_layout", "Reset Layout", "", "View",
        [this]() { if (main_window_) main_window_->reset_layout(); });

    auto set_tool = [this](ToolMode tool)
    {
        if (main_window_)
            main_window_->central_view()->set_active_tool(tool);
    };
    add("tool.select", "Select Tool", "V", "Tools",
        [set_tool]() { set_tool(ToolMode::Select); });
    add("tool.pan", "Pan Tool", "H", "Tools",
        [set_tool]() { set_tool(ToolMode::Pan); });
    add("tool.box_zoom", "Box Zoom Tool", "Z", "Tools",
        [set_tool]() { set_tool(ToolMode::BoxZoom); });
    add("tool.measure", "Measure Tool", "M", "Tools",
        [set_tool]() { set_tool(ToolMode::Measure); });
    add("tool.annotate", "Annotate Tool", "A", "Tools",
        [set_tool]() { set_tool(ToolMode::Annotate); });
    add("tool.roi", "ROI Tool", "Shift+R", "Tools",
        [set_tool]() { set_tool(ToolMode::ROI); });

    auto invoke_window_slot = [this](const char* slot)
    {
        if (main_window_)
            QMetaObject::invokeMethod(main_window_.get(), slot, Qt::DirectConnection);
    };
    add("panel.toggle_inspector", "Toggle Inspector", "I", "View",
        [this]() {
            if (main_window_)
                main_window_->toggle_inspector();
        });
    add("panel.toggle_topics", "Toggle Topics", "Ctrl+Shift+T", "View",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_topics"); });
    add("panel.open_settings", "Settings", "Ctrl+,", "View",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_settings"); });
    add("panel.toggle_timeline", "Toggle Timeline", "T", "View",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_timeline"); });
    add("panel.toggle_data_editor", "Toggle Data Editor", "D", "View",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_data_editor"); });
    add("file.export", "Export Figure", "Ctrl+S", "File",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_export"); });
    add("app.quit", "Quit Spectra", "Ctrl+Q", "File",
        []() { QCoreApplication::quit(); });

    services_->shortcuts().register_defaults();
    SPECTRA_LOG_INFO("qt_app",
                     "Registered " + std::to_string(commands.count()) + " Qt commands");
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
    if (autosave_ && autosave_->has_unsaved_changes())
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
