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
#include "qt_series_commands.hpp"
#include "panels/function_plot_dialog.hpp"
#include "panels/shortcut_widget.hpp"
#include "panels/timeline_property_binding.hpp"
#include "panels/transform_widget.hpp"
#include "qt_workspace_bridge.hpp"
#include "qt_automation_adapter.hpp"
#include "qt_ipc_client.hpp"
#include "split_view_container.hpp"

#include "ui/workspace/workspace.hpp"
#include "ui/workspace/workspace_autosave.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/app/perf_metrics.hpp"
#include "ui/plot/plot_annotations.hpp"
#include "ui/data/clipboard_export.hpp"
#include "ui/data/html_table_export.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/accessibility/sonification.hpp"
#include "ui/animation/timeline_editor.hpp"
#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/theme/theme.hpp"
#include "ui/settings/settings_store.hpp"

#include "ipc/figure_snapshot.hpp"

#include "app/inproc_topic_server.hpp"

#include <QObject>
#include <QCoreApplication>
#include <QBuffer>
#include <QMessageBox>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QClipboard>
#include <QApplication>

#include "app/application_services.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/input/input.hpp"

#include <spectra/export.hpp>

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/logger.hpp>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <cstdlib>
#include <algorithm>
#include <functional>
#include <sstream>
#include <thread>
#include <unordered_set>

#ifdef __unix__
    #include <unistd.h>
#endif

namespace spectra::adapters::qt
{

namespace
{

bool belongs_to_window(const QWidget* widget, const QWidget* root)
{
    if (!widget || !root)
        return false;
    if (widget == root)
        return true;

    for (const QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
    {
        if (parent == root)
            return true;
    }

    const QWindow* root_window = root->windowHandle();
    for (const QWindow* transient = widget->windowHandle(); transient;
         transient                = transient->transientParent())
    {
        if (transient == root_window)
            return true;
    }
    return false;
}

QRect widget_screen_rect(const QWidget* widget)
{
    return widget ? QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()) : QRect{};
}

QtCaptureResult capture_screen_surface(QWidget*           root,
                                       QWindow*           canvas,
                                       QtCaptureScope     scope,
                                       const std::string& path,
                                       uint32_t           target_width  = 0,
                                       uint32_t           target_height = 0)
{
    QRect    bounds;
    QScreen* screen = nullptr;
    if (scope == QtCaptureScope::Canvas)
    {
        if (!canvas || !canvas->isVisible() || !canvas->isExposed())
            return {};
        bounds = QRect(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
        screen = canvas->screen();
    }
    else
    {
        if (!root || !root->isVisible())
            return {};
        bounds = widget_screen_rect(root);
        screen = root->screen();

        // Dialogs and popups are separate top-level windows. Include the visible
        // surfaces owned by this application window in the captured region.
        for (QWidget* top_level : QApplication::topLevelWidgets())
        {
            if (!top_level || !top_level->isVisible() || !belongs_to_window(top_level, root))
                continue;
            bounds = bounds.united(widget_screen_rect(top_level));
        }
    }

    if (!screen || bounds.isEmpty())
        return {};

    // QWindow-backed Vulkan canvases are native children and are absent from
    // QWidget::grab(). Capture the composed screen pixels instead so the image
    // matches what the user sees, including owned popup/dialog surfaces.
    QPixmap pixmap = screen->grabWindow(0, bounds.x(), bounds.y(), bounds.width(), bounds.height());
    if (pixmap.isNull())
        return {};

    if (target_width > 0 && target_height > 0
        && (pixmap.width() != static_cast<int>(target_width)
            || pixmap.height() != static_cast<int>(target_height)))
    {
        pixmap = pixmap.scaled(static_cast<int>(target_width),
                               static_cast<int>(target_height),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }

    QByteArray png;
    QBuffer    buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !pixmap.save(&buffer, "PNG"))
        return {};

    QtCaptureResult result;
    result.width      = static_cast<uint32_t>(pixmap.width());
    result.height     = static_cast<uint32_t>(pixmap.height());
    result.png_base64 = png.toBase64().toStdString();
    if (!path.empty())
    {
        if (!pixmap.save(QString::fromStdString(path), "PNG"))
            return {};
        result.path = path;
    }
    return result;
}

}   // namespace

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

    series_clipboard_  = std::make_unique<SeriesClipboard>();
    axis_link_manager_ = std::make_unique<AxisLinkManager>();
    register_qt_commands();

    // 4. Create QtActionBridge from the shared CommandRegistry
    action_bridge_ = std::make_unique<QtActionBridge>(services_->commands());
    action_bridge_->rebuild();
    action_bridge_->sync_shortcuts(services_->shortcuts());

    // 5. Create frontend service implementations and inject them
    dialog_service_    = std::make_unique<QtDialogService>();
    clipboard_service_ = std::make_unique<QtClipboardService>();
    redraw_request_    = std::make_unique<QtRedrawRequest>(
        [this]()
        {
            if (autosave_)
                autosave_->mark_dirty();
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
    window_service_ = std::make_unique<QtWindowService>();

    // Wire window service callbacks
    window_service_->set_create_window(
        [this](const std::string& title, uint32_t w, uint32_t h) -> FigureId
        {
            // Create a new figure and add it as a tab
            auto fig = std::make_unique<Figure>();
            fig->set_size(w, h);
            fig->set_tab_title(title);
            fig->subplot(1, 1, 1);
            FigureId id = figure_registry().register_figure(std::move(fig));
            if (main_window_)
                main_window_->add_figure_tab(id);
            if (autosave_)
                autosave_->mark_dirty();
            return id;
        });
    window_service_->set_close_window(
        [this](FigureId id)
        {
            if (main_window_)
                main_window_->close_figure_tab(id);
        });
    window_service_->set_focus_window(
        [](FigureId id)
        {
            // Focus is handled by Qt's tab widget
            (void)id;
        });
    window_service_->set_window_count(
        [this]() -> size_t
        { return main_window_ ? static_cast<size_t>(main_window_->figure_tab_count()) : 0; });

    services_->set_dialog_service(dialog_service_.get());
    services_->set_clipboard_service(clipboard_service_.get());
    services_->set_redraw_request(redraw_request_.get());
    services_->set_window_service(window_service_.get());

    // 6. Create the main window registry (for multi-window support)
    auto timeline_resolver = [this](FigureId id) { return timeline_for(id); };
    window_registry_       = std::make_unique<MainWindowRegistry>(runtime_.get(),
                                                            &figure_registry(),
                                                            action_bridge_.get(),
                                                            services_.get(),
                                                            axis_link_manager_.get(),
                                                            timeline_resolver);

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
    automation_adapter_->set_switch_figure(
        [this](FigureId id) -> bool
        {
            if (!main_window_ || !figure_registry().get(id))
                return false;
            if (window_registry_)
            {
                const HostId host_id = window_registry_->find_host_for_figure(id);
                if (auto* host = window_registry_->native_host(host_id))
                {
                    if (auto* window = host->main_window())
                    {
                        window->show();
                        window->raise();
                        window->activateWindow();
                        return window->central_view()->activate_figure(id);
                    }
                }
            }
            return main_window_->add_figure_tab(id) >= 0;
        });
    automation_adapter_->set_close_figure(
        [this](FigureId id) { return window_registry_ && window_registry_->close_document(id); });
    automation_adapter_->set_detach_figure(
        [this](FigureId id)
        {
            if (!window_registry_)
                return false;
            const HostId source = window_registry_->find_host_for_figure(id);
            return source != INVALID_HOST_ID
                   && window_registry_->detach_document(id, source) != INVALID_HOST_ID;
        });
    automation_adapter_->set_get_state(
        [this]() -> std::string
        {
            const auto         ids = figure_registry().all_ids();
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
                    state << ",\"width\":" << figure->width() << ",\"height\":" << figure->height()
                          << ",\"axes_count\":" << figure->all_axes().size() + figure->axes().size()
                          << ",\"total_series\":" << series_count << ",\"title\":\""
                          << json_escape(figure->tab_title()) << "\"";
                }
                state << '}';
            }
            state << "]}";
            return state.str();
        });
    automation_adapter_->set_list_menus(
        [this]() -> std::string {
            return main_window_ ? main_window_->automation_menu_state()
                                : std::string{R"({"menus":[]})"};
        });
    automation_adapter_->set_capture_surface(
        [this](QtCaptureScope scope, const std::string& path) -> QtCaptureResult
        {
            if (!main_window_)
                return {};
            auto*    canvas        = main_window_->canvas_for(main_window_->active_figure_id());
            QWindow* canvas_window = canvas ? canvas->vulkanWindow() : nullptr;
            return capture_screen_surface(main_window_.get(), canvas_window, scope, path);
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
    automation_adapter_->set_frame_callbacks(
        [this](uint32_t count) -> uint32_t
        {
            if (!main_window_)
                return 0;
            auto* canvas = main_window_->canvas_for(main_window_->active_figure_id());
            if (!canvas || !canvas->vulkanWindow())
                return 0;

            const uint64_t before = PerfMetrics::instance().frame_count();
            for (uint32_t i = 0; i < count; ++i)
                canvas->vulkanWindow()->renderFrame();
            const uint64_t rendered = PerfMetrics::instance().frame_count() - before;
            return static_cast<uint32_t>(std::min<uint64_t>(rendered, count));
        },
        []() -> uint64_t { return PerfMetrics::instance().frame_count(); });
    const char* env_auto = std::getenv("SPECTRA_AUTOMATION");
    if (env_auto && *env_auto)
    {
        automation_adapter_->start(services_.get());
    }

    // 6d. Create the workspace autosave manager
    autosave_                    = std::make_unique<WorkspaceAutosave>();
    auto apply_autosave_duration = [this](const char* name, bool debounce)
    {
        const char* value = std::getenv(name);
        if (!value || !*value)
            return;
        char*           end = nullptr;
        const long long ms  = std::strtoll(value, &end, 10);
        if (!end || *end != '\0' || ms < 0)
            return;
        const auto duration = std::chrono::milliseconds(ms);
        if (debounce)
            autosave_->set_debounce(duration);
        else
            autosave_->set_interval(duration);
    };
    // Deterministic launched-test seam; production defaults remain 60 s / 5 s.
    apply_autosave_duration("SPECTRA_AUTOSAVE_INTERVAL_MS", false);
    apply_autosave_duration("SPECTRA_AUTOSAVE_DEBOUNCE_MS", true);
    autosave_->set_serialize_fn([this]() -> std::string
                                { return Workspace::serialize_json(capture_workspace_data()); });
    window_registry_->set_document_changed_callback(
        [this]()
        {
            if (autosave_)
                autosave_->mark_dirty();
        });
    axis_link_manager_->set_on_change(
        [this]()
        {
            if (autosave_)
                autosave_->mark_dirty();
        });

    // 7. Create the main window
    main_window_ = std::make_unique<SpectraMainWindow>(runtime_.get(),
                                                       &figure_registry(),
                                                       action_bridge_.get(),
                                                       services_.get(),
                                                       timeline_resolver);
    main_window_->set_axis_link_manager(axis_link_manager_.get());
    automation_adapter_->set_input_target(
        main_window_.get(),
        [this]() -> QWindow*
        {
            if (!main_window_)
                return nullptr;
            auto* canvas = main_window_->canvas_for(main_window_->active_figure_id());
            return canvas ? canvas->vulkanWindow() : nullptr;
        });

    // 8. Register the main window with the registry
    window_registry_->register_window(main_window_.get());

    auto* autosave_timer = new QTimer(main_window_.get());
    autosave_timer->setObjectName("workspace_autosave_timer");
    QObject::connect(autosave_timer,
                     &QTimer::timeout,
                     main_window_.get(),
                     [this]()
                     {
                         if (autosave_)
                             autosave_->tick();
                     });
    autosave_timer->start(1000);

    initialized_ = true;
    SPECTRA_LOG_INFO("qt_app", "QtApplicationController initialized");
    return true;
}

void QtApplicationController::register_qt_commands()
{
    auto& commands = services_->commands();

    auto add = [&commands](std::string           id,
                           std::string           label,
                           std::string           shortcut,
                           std::string           category,
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
        auto* window = command_target_window();
        if (!window)
            return nullptr;
        const FigureId id = window->active_figure_id();
        return id == INVALID_FIGURE_ID ? nullptr : figure_registry().get(id);
    };

    auto active_canvas = [this]() -> SpectraVulkanWindow*
    {
        auto* window = command_target_window();
        if (!window)
            return nullptr;
        FigureCanvasWidget* canvas = window->canvas_for(window->active_figure_id());
        return canvas ? canvas->vulkanWindow() : nullptr;
    };

    auto request_redraw = [this]()
    {
        if (redraw_request_)
            redraw_request_->request_redraw();
    };

    add("edit.undo",
        "Undo",
        "Ctrl+Z",
        "Edit",
        [this]()
        {
            if (services_->undo().undo() && redraw_request_)
                redraw_request_->request_redraw();
        });
    add("edit.redo",
        "Redo",
        "Ctrl+Y",
        "Edit",
        [this]()
        {
            if (services_->undo().redo() && redraw_request_)
                redraw_request_->request_redraw();
        });

    add("figure.new",
        "New Figure",
        "Ctrl+T",
        "Figure",
        [this]()
        {
            auto figure = std::make_unique<Figure>();
            figure->subplot(1, 1, 1);
            const FigureId id = figure_registry().register_figure(std::move(figure));
            if (main_window_)
                main_window_->add_figure_tab(id);
            if (autosave_)
                autosave_->mark_dirty();
        });
    add("figure.close",
        "Close Figure",
        "Ctrl+W",
        "Figure",
        [this]()
        {
            if (!main_window_)
                return;
            const FigureId id = main_window_->active_figure_id();
            if (id == INVALID_FIGURE_ID)
                return;
            if (window_registry_)
            {
                if (window_registry_->close_document(id))
                    timelines_.erase(id);
            }
            else
            {
                main_window_->close_figure_tab(id);
                if (Figure* figure = figure_registry().get(id); axis_link_manager_ && figure)
                {
                    for (auto& axes : figure->axes_mut())
                        if (axes)
                            axis_link_manager_->remove_from_all(axes.get());
                    for (auto& axes : figure->all_axes_mut())
                        if (auto* axes3d = dynamic_cast<Axes3D*>(axes.get()))
                            axis_link_manager_->remove_from_all_3d(axes3d);
                }
                figure_registry().unregister_figure(id);
                timelines_.erase(id);
            }
        });

    auto activate_relative = [this](int delta)
    {
        if (!main_window_)
            return;
        const auto ids = main_window_->open_figure_ids();
        if (ids.empty())
            return;
        const auto it      = std::find(ids.begin(), ids.end(), main_window_->active_figure_id());
        const int  current = it == ids.end() ? 0 : static_cast<int>(std::distance(ids.begin(), it));
        const int  count   = static_cast<int>(ids.size());
        const int  next    = (current + delta + count) % count;
        main_window_->central_view()->activate_figure(ids[static_cast<size_t>(next)]);
    };
    add("figure.next_tab",
        "Next Figure Tab",
        "Ctrl+Tab",
        "Figure",
        [activate_relative]() { activate_relative(1); });
    add("figure.prev_tab",
        "Previous Figure Tab",
        "Ctrl+Shift+Tab",
        "Figure",
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
    add("view.autofit",
        "Auto-Fit Active Axes",
        "A",
        "View",
        [this]()
        {
            if (auto* window = command_target_window())
                window->autofit_active_axes();
        });
    add("view.fullscreen",
        "Toggle Fullscreen Canvas",
        "F",
        "View",
        [this]()
        {
            if (auto* window = command_target_window())
                window->toggle_canvas_fullscreen();
        });
    add("view.split_right",
        "Split Right",
        "Ctrl+\\",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->split_right();
        });
    add("view.split_down",
        "Split Down",
        "Ctrl+Shift+\\",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->split_down();
        });
    add("view.close_split",
        "Close Split Pane",
        "",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->close_split();
        });
    add("view.reset_layout",
        "Reset Layout",
        "",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->reset_layout();
        });

    auto set_tool = [this](ToolMode tool)
    {
        if (main_window_)
            main_window_->set_active_tool(tool);
    };
    add("tool.select", "Select Tool", "V", "Tools", [set_tool]() { set_tool(ToolMode::Select); });
    add("tool.pan", "Pan Tool", "H", "Tools", [set_tool]() { set_tool(ToolMode::Pan); });
    add("tool.box_zoom",
        "Box Zoom Tool",
        "Z",
        "Tools",
        [set_tool]() { set_tool(ToolMode::BoxZoom); });
    add("tool.measure",
        "Measure Tool",
        "M",
        "Tools",
        [set_tool]() { set_tool(ToolMode::Measure); });
    add("tool.annotate",
        "Annotate Tool",
        "A",
        "Tools",
        [set_tool]() { set_tool(ToolMode::Annotate); });
    add("tool.roi", "ROI Tool", "Shift+R", "Tools", [set_tool]() { set_tool(ToolMode::ROI); });

    auto invoke_window_slot = [this](const char* slot)
    {
        if (auto* window = command_target_window())
            QMetaObject::invokeMethod(window, slot, Qt::DirectConnection);
    };
    add("panel.toggle_inspector",
        "Toggle Inspector Panel",
        "",
        "Panel",
        [this]()
        {
            if (auto* window = command_target_window())
                window->toggle_inspector();
        });
    add("panel.toggle_topics",
        "Toggle Topics Panel",
        "Ctrl+Shift+T",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_topics"); });
    add("panel.open_settings",
        "Settings",
        "",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_settings"); });
    add("panel.toggle_timeline",
        "Toggle Timeline Panel",
        "T",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_timeline"); });
    add("panel.toggle_curve_editor",
        "Toggle Curve Editor",
        "",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_curve_editor"); });
    add("panel.toggle_data_editor",
        "Toggle Data Editor",
        "",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_data_editor"); });
    add("file.export",
        "Export Figure",
        "Ctrl+S",
        "File",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_export"); });
    add("app.quit", "Quit Spectra", "Ctrl+Q", "File", []() { QCoreApplication::quit(); });

    // ── Theme commands ────────────────────────────────────────────────────
    auto apply_theme = [this](const std::string& name)
    {
        services_->theme().set_theme(name);
        auto& settings                    = services_->settings();
        settings.data_mut().default_theme = name;
        settings.notify_change();
        if (main_window_)
            main_window_->refresh_theme();
        if (redraw_request_)
            redraw_request_->request_redraw();
    };
    add("theme.night",
        "Switch to Night Theme",
        "",
        "Theme",
        [apply_theme]() { apply_theme("night"); });
    add("theme.dark",
        "Switch to Dark Theme",
        "",
        "Theme",
        [apply_theme]() { apply_theme("dark"); });
    add("theme.light",
        "Switch to Light Theme",
        "",
        "Theme",
        [apply_theme]() { apply_theme("light"); });
    add("theme.toggle",
        "Toggle Dark/Light Theme",
        "",
        "Theme",
        [this]()
        {
            auto&       tm      = services_->theme();
            std::string current = tm.current_theme_name();
            std::string next    = (current == "dark") ? "light" : "dark";
            tm.set_theme(next);
            auto& settings                    = services_->settings();
            settings.data_mut().default_theme = next;
            settings.notify_change();
            if (main_window_)
                main_window_->refresh_theme();
            if (redraw_request_)
                redraw_request_->request_redraw();
        });

    // ── View navigation commands ──────────────────────────────────────────
    auto active_axes = [active_figure]() -> Axes*
    {
        Figure* fig = active_figure();
        if (!fig || fig->axes().empty())
            return nullptr;
        return fig->axes_mut()[0].get();
    };

    add("view.home",
        "Home (Restore Original View)",
        "Home",
        "View",
        [active_figure, request_redraw]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            for (auto& ax : fig->axes_mut())
                if (ax)
                    ax->auto_fit();
            for (auto& ax : fig->all_axes_mut())
                if (ax)
                    ax->auto_fit();
            request_redraw();
        });

    add("view.zoom_in",
        "Zoom In",
        "",
        "View",
        [active_axes, request_redraw]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto  xl = ax->x_limits();
            auto  yl = ax->y_limits();
            float xc = (xl.min + xl.max) * 0.5f;
            float yc = (yl.min + yl.max) * 0.5f;
            float xr = (xl.max - xl.min) * 0.375f;
            float yr = (yl.max - yl.min) * 0.375f;
            ax->xlim(xc - xr, xc + xr);
            ax->ylim(yc - yr, yc + yr);
            request_redraw();
        });

    add("view.zoom_out",
        "Zoom Out",
        "",
        "View",
        [active_axes, request_redraw]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto  xl = ax->x_limits();
            auto  yl = ax->y_limits();
            float xc = (xl.min + xl.max) * 0.5f;
            float yc = (yl.min + yl.max) * 0.5f;
            float xr = (xl.max - xl.min) * 0.625f;
            float yr = (yl.max - yl.min) * 0.625f;
            ax->xlim(xc - xr, xc + xr);
            ax->ylim(yc - yr, yc + yr);
            request_redraw();
        });

    auto pan_axes = [active_axes, request_redraw](float dx, float dy)
    {
        Axes* ax = active_axes();
        if (!ax)
            return;
        auto xl = ax->x_limits();
        auto yl = ax->y_limits();
        ax->xlim(xl.min + dx, xl.max + dx);
        ax->ylim(yl.min + dy, yl.max + dy);
        request_redraw();
    };

    add("view.pan_left",
        "Pan Left",
        "Left",
        "View",
        [pan_axes, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto xl = ax->x_limits();
            pan_axes(-(xl.max - xl.min) * 0.1f, 0.0f);
        });
    add("view.pan_right",
        "Pan Right",
        "Right",
        "View",
        [pan_axes, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto xl = ax->x_limits();
            pan_axes((xl.max - xl.min) * 0.1f, 0.0f);
        });
    add("view.pan_up",
        "Pan Up",
        "Up",
        "View",
        [pan_axes, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto yl = ax->y_limits();
            pan_axes(0.0f, (yl.max - yl.min) * 0.1f);
        });
    add("view.pan_down",
        "Pan Down",
        "Down",
        "View",
        [pan_axes, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax)
                return;
            auto yl = ax->y_limits();
            pan_axes(0.0f, -(yl.max - yl.min) * 0.1f);
        });

    add("view.toggle_grid",
        "Toggle Grid",
        "G",
        "View",
        [active_figure, request_redraw]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            for (auto& ax : fig->axes_mut())
                if (ax)
                    ax->grid(!ax->grid_enabled());
            request_redraw();
        });

    add("view.toggle_crosshair",
        "Toggle Crosshair",
        "C",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->toggle_active_crosshair();
        });

    add("view.toggle_legend",
        "Toggle Legend",
        "L",
        "View",
        [active_figure, request_redraw]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            bool current          = fig->legend().visible;
            fig->legend().visible = !current;
            request_redraw();
        });

    add("view.toggle_border",
        "Toggle Border",
        "B",
        "View",
        [active_figure, request_redraw]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            for (auto& ax : fig->axes_mut())
                if (ax)
                    ax->show_border(!ax->border_enabled());
            request_redraw();
        });

    add("view.reset_splits",
        "Reset All Splits",
        "",
        "View",
        [this]()
        {
            if (main_window_)
                main_window_->reset_splits();
        });

    // ── Animation commands ────────────────────────────────────────────────
    add("anim.toggle_play",
        "Toggle Play/Pause",
        "Space",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->toggle_play();
        });
    add("anim.step_back",
        "Step Frame Back",
        "[",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->step_backward();
        });
    add("anim.step_forward",
        "Step Frame Forward",
        "]",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->step_forward();
        });
    add("anim.stop",
        "Stop Playback",
        "",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->stop();
        });
    add("anim.go_to_start",
        "Go to Start",
        "",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->set_playhead(0.0f);
        });
    add("anim.go_to_end",
        "Go to End",
        "",
        "Animation",
        [this]()
        {
            if (auto* timeline = active_timeline())
                timeline->set_playhead(timeline->duration());
        });

    // ── App commands ──────────────────────────────────────────────────────
    add("app.command_palette",
        "Command Palette",
        "Ctrl+K",
        "App",
        [this]()
        {
            if (main_window_)
                main_window_->open_command_palette();
        });
    add("app.cancel",
        "Cancel / Close",
        "Escape",
        "App",
        [this]()
        {
            if (main_window_)
                main_window_->cancel_transient_ui();
        });
    add("app.new_window",
        "New Window",
        "Ctrl+Shift+N",
        "App",
        [this]()
        {
            if (window_registry_)
            {
                HostId new_host = window_registry_->create_detached_window();
                (void)new_host;
            }
        });
    add("help.show",
        "Help / Documentation",
        "F1",
        "App",
        []()
        {
            constexpr const char* url = "https://danlil240.github.io/Spectra/index.html";
#if defined(_WIN32)
            std::thread([url]()
                        { std::system((std::string("start \"\" \"") + url + "\"").c_str()); })
                .detach();
#elif defined(__APPLE__)
            std::thread([url]() { std::system((std::string("open \"") + url + "\"").c_str()); }).detach();
#elif defined(__unix__)
            pid_t pid = fork();
            if (pid == 0)
            {
                execlp("xdg-open", "xdg-open", url, static_cast<char*>(nullptr));
                _exit(127);
            }
#endif
            SPECTRA_LOG_INFO("help", std::string("Opening documentation: ") + url);
        });

    // ── File commands ─────────────────────────────────────────────────────
    add("file.export_png",
        "Export PNG",
        "Ctrl+Shift+S",
        "File",
        [this, active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig || !dialog_service_)
                return;
            auto path = dialog_service_->file_dialog(DialogService::FileType::Save,
                                                     "Export PNG",
                                                     "spectra_export.png",
                                                     {{"PNG Image", "*.png"}});
            if (!path)
                return;
            auto* window = command_target_window();
            auto* canvas = canvas_for_figure(figure_registry().find_id(fig));
            (void)window;
            if (!canvas || !canvas->savePng(*path))
                SPECTRA_LOG_ERROR("qt_export", "Failed to export PNG: {}", *path);
        });

    add("file.export_svg",
        "Export SVG",
        "Ctrl+Shift+Alt+S",
        "File",
        [this, active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig || !dialog_service_)
                return;
            auto path = dialog_service_->file_dialog(DialogService::FileType::Save,
                                                     "Export SVG",
                                                     "spectra_export.svg",
                                                     {{"SVG Image", "*.svg"}});
            if (!path)
                return;
            if (!SvgExporter::write_svg(*path, *fig))
                SPECTRA_LOG_ERROR("qt_export", "Failed to export SVG: {}", *path);
        });

    add("file.copy_to_clipboard",
        "Copy Figure to Clipboard",
        "Ctrl+Shift+C",
        "File",
        [this, active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig || !clipboard_service_)
                return;
            auto* window = command_target_window();
            auto* canvas = canvas_for_figure(figure_registry().find_id(fig));
            (void)window;
            std::vector<uint8_t> pixels;
            uint32_t             width  = 0;
            uint32_t             height = 0;
            if (canvas && canvas->captureRgba(pixels, width, height))
                clipboard_service_->copy_image(
                    ImageExporter::write_png_to_memory(pixels.data(), width, height));
        });

    add("file.save_workspace",
        "Save Workspace",
        "",
        "File",
        [this]() { Workspace::save(Workspace::default_path(), capture_workspace_data()); });

    add("file.load_workspace",
        "Load Workspace",
        "",
        "File",
        [this]()
        {
            WorkspaceData data;
            if (!Workspace::load(Workspace::default_path(), data))
                return;
            restore_workspace_data(data);
        });

    add("file.save_figure",
        "Save Figure",
        "",
        "File",
        [this, active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig || !dialog_service_)
                return;
            auto path = dialog_service_->file_dialog(DialogService::FileType::Save,
                                                     "Save Figure",
                                                     "figure.spectra",
                                                     {{"Spectra Figure", "*.spectra"}});
            if (!path)
                return;
            OverlaySnapshot overlay;
            const FigureId  id          = figure_registry().find_id(fig);
            auto*           canvas      = canvas_for_figure(id);
            const bool      has_overlay = canvas && canvas->captureOverlaySnapshot(overlay);
            if (!FigureSerializer::save(*path, *fig, has_overlay ? &overlay : nullptr))
                SPECTRA_LOG_ERROR("qt_figure", "Failed to save figure: {}", *path);
        });

    add("file.load_figure",
        "Load Figure",
        "",
        "File",
        [this, active_figure, request_redraw]()
        {
            Figure* fig = active_figure();
            if (!fig || !dialog_service_)
                return;
            auto path = dialog_service_->file_dialog(DialogService::FileType::Open,
                                                     "Open Figure",
                                                     "",
                                                     {{"Spectra Figure", "*.spectra"}});
            if (!path)
                return;
            OverlaySnapshot overlay;
            if (!FigureSerializer::load(*path, *fig, &overlay))
            {
                SPECTRA_LOG_ERROR("qt_figure", "Failed to load figure: {}", *path);
                return;
            }
            if (auto* canvas = canvas_for_figure(figure_registry().find_id(fig)))
                canvas->restoreOverlaySnapshot(overlay);
            for (auto& ax : fig->all_axes_mut())
                if (ax)
                    for (auto& s : ax->series_mut())
                        if (s)
                            s->mark_dirty();
            request_redraw();
        });

    // ── Panel commands ────────────────────────────────────────────────────
    add("panel.toggle_nav_rail",
        "Toggle Navigation Rail",
        "",
        "Panel",
        [this]()
        {
            if (auto* window = command_target_window())
                window->set_nav_rail_visible(!window->is_nav_rail_visible());
        });
    add("panel.toggle_plugins",
        "Toggle Plugins Panel",
        "",
        "Panel",
        [invoke_window_slot]() { invoke_window_slot("on_toggle_plugins"); });

    // ── Plot commands ─────────────────────────────────────────────────────
    add("plot.hline_zero",
        "Add Y = 0 Line",
        "",
        "Plot",
        [active_axes, request_redraw]()
        {
            Axes* ax = active_axes();
            if (ax)
            {
                ui::add_horizontal_reference_line(*ax, 0.0f, "y = 0");
                request_redraw();
            }
        });
    add("plot.vline_zero",
        "Add X = 0 Line",
        "",
        "Plot",
        [active_axes, request_redraw]()
        {
            Axes* ax = active_axes();
            if (ax)
            {
                ui::add_vertical_reference_line(*ax, 0.0f, "x = 0");
                request_redraw();
            }
        });
    add("plot.hline",
        "Add Horizontal Line...",
        "",
        "Plot",
        [this, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax || !dialog_service_)
                return;
            auto value = dialog_service_->number_input("Add Horizontal Line",
                                                       "Y value:",
                                                       0.0,
                                                       -2147483647.0,
                                                       2147483647.0,
                                                       3);
            if (value)
                ui::add_horizontal_reference_line(*ax, static_cast<float>(*value));
        });
    add("plot.vline",
        "Add Vertical Line...",
        "",
        "Plot",
        [this, active_axes]()
        {
            Axes* ax = active_axes();
            if (!ax || !dialog_service_)
                return;
            auto value = dialog_service_->number_input("Add Vertical Line",
                                                       "X value:",
                                                       0.0,
                                                       -2147483647.0,
                                                       2147483647.0,
                                                       3);
            if (value)
                ui::add_vertical_reference_line(*ax, static_cast<float>(*value));
        });
    add("plot.function",
        "Plot Function...",
        "",
        "Plot",
        [this]()
        {
            if (!main_window_)
                return;
            const FigureId id = main_window_->active_figure_id();
            if (id == INVALID_FIGURE_ID)
                return;
            if (!function_plot_dialog_)
            {
                function_plot_dialog_ =
                    std::make_unique<QtFunctionPlotDialog>(&figure_registry(),
                                                           redraw_request_.get(),
                                                           main_window_.get());
            }
            function_plot_dialog_->open_for_figure(id);
        });

    // ── Data / Accessibility commands ─────────────────────────────────────
    add("data.copy_to_clipboard",
        "Copy Data to Clipboard (TSV)",
        "Ctrl+Shift+D",
        "Data",
        [active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            std::vector<const Series*> to_export;
            for (auto& ax : fig->axes_mut())
            {
                if (!ax)
                    continue;
                for (const auto& sp : ax->series())
                    if (sp && sp->visible())
                        to_export.push_back(sp.get());
            }
            std::string tsv = series_to_tsv(to_export);
            if (!tsv.empty())
            {
                QClipboard* clip = QApplication::clipboard();
                clip->setText(QString::fromStdString(tsv));
            }
        });

    add("data.export_html_table",
        "Export Accessible HTML Table",
        "",
        "Data",
        [active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            const std::string path = "spectra_data.html";
            if (figure_to_html_table_file(*fig, path))
                SPECTRA_LOG_INFO("accessibility", "HTML table exported to '{}'", path);
            else
                SPECTRA_LOG_WARN("accessibility", "Failed to write HTML table to '{}'", path);
        });

    add("accessibility.sonify_series",
        "Sonify Active Series (Export WAV)",
        "",
        "Accessibility",
        [active_figure]()
        {
            Figure* fig = active_figure();
            if (!fig)
                return;
            for (auto& ax_ptr : fig->axes_mut())
            {
                if (!ax_ptr || ax_ptr->series().empty())
                    continue;
                const std::string path = "spectra_sonify.wav";
                if (sonify_axes_to_wav(*ax_ptr, path))
                    SPECTRA_LOG_INFO("accessibility", "Sonification WAV exported to '{}'", path);
                else
                    SPECTRA_LOG_WARN("accessibility",
                                     "Failed to sonify axes or write WAV to '{}'",
                                     path);
                break;
            }
        });

    // ── Series commands ───────────────────────────────────────────────────
    add("series.cycle_selection",
        "Cycle Series Selection",
        "Tab",
        "Series",
        [active_canvas]()
        {
            if (auto* canvas = active_canvas())
                canvas->cycleSeriesSelection();
        });
    add("series.copy",
        "Copy Series",
        "Ctrl+C",
        "Series",
        [this, active_canvas]()
        {
            auto* canvas = active_canvas();
            if (!canvas || !series_clipboard_)
                return;
            copy_selected_series(canvas->selectedSeries(), *series_clipboard_, false);
        });

    auto remove_selected = [this, active_canvas](bool cut)
    {
        auto* canvas = active_canvas();
        if (!canvas)
            return;
        const auto selected = canvas->selectedSeries();
        if (selected.empty())
            return;

        auto*          window         = command_target_window();
        const FigureId figure_id      = window ? window->active_figure_id() : INVALID_FIGURE_ID;
        auto           notify_removed = [this, figure_id](const Series* series)
        {
            if (auto* target_canvas = canvas_for_figure(figure_id))
                target_canvas->notifySeriesRemoved(series);
        };
        auto notify_changed = [this, figure_id]()
        {
            if (redraw_request_)
                redraw_request_->request_redraw(figure_id);
        };
        auto owner_is_alive = [this, figure_id](const AxesBase* owner)
        {
            const Figure* figure = figure_registry().get(figure_id);
            if (!figure || !owner)
                return false;
            for (const auto& axes : figure->axes())
                if (axes.get() == owner)
                    return true;
            for (const auto& axes : figure->all_axes())
                if (axes.get() == owner)
                    return true;
            return false;
        };
        canvas->deselectSeries();
        if (auto action = remove_selected_series(selected,
                                                 series_clipboard_.get(),
                                                 cut,
                                                 std::move(notify_removed),
                                                 std::move(notify_changed),
                                                 std::move(owner_is_alive)))
            services_->undo().push(std::move(*action));
    };

    add("series.cut",
        "Cut Series",
        "Ctrl+X",
        "Series",
        [remove_selected]() { remove_selected(true); });
    add("series.paste",
        "Paste Series",
        "Ctrl+V",
        "Series",
        [this, active_figure, active_canvas]()
        {
            Figure* figure = active_figure();
            auto*   canvas = active_canvas();
            if (!figure || !canvas || !series_clipboard_ || !series_clipboard_->has_data())
                return;

            AxesBase* target = canvas->selectedSeriesAxes();
            if (!target)
            {
                if (!figure->all_axes().empty())
                    target = figure->all_axes_mut()[0].get();
                else if (!figure->axes().empty())
                    target = figure->axes_mut()[0].get();
            }
            if (!target)
                return;

            auto*          window         = command_target_window();
            const FigureId figure_id      = window ? window->active_figure_id() : INVALID_FIGURE_ID;
            auto           notify_removed = [this, figure_id](const Series* series)
            {
                if (auto* target_canvas = canvas_for_figure(figure_id))
                    target_canvas->notifySeriesRemoved(series);
            };
            auto notify_changed = [this, figure_id]()
            {
                if (redraw_request_)
                    redraw_request_->request_redraw(figure_id);
            };
            auto owner_is_alive = [this, figure_id](const AxesBase* owner)
            {
                const Figure* current = figure_registry().get(figure_id);
                if (!current || !owner)
                    return false;
                for (const auto& axes : current->axes())
                    if (axes.get() == owner)
                        return true;
                for (const auto& axes : current->all_axes())
                    if (axes.get() == owner)
                        return true;
                return false;
            };
            if (auto action = paste_series(*target,
                                           *series_clipboard_,
                                           std::move(notify_removed),
                                           std::move(notify_changed),
                                           std::move(owner_is_alive)))
                services_->undo().push(std::move(*action));
        });
    add("series.delete",
        "Delete Series",
        "Delete",
        "Series",
        [remove_selected]() { remove_selected(false); });
    add("series.deselect",
        "Deselect Series",
        "Escape",
        "Series",
        [active_canvas]()
        {
            if (auto* canvas = active_canvas())
                canvas->deselectSeries();
        });

    // ── Figure move to window ─────────────────────────────────────────────
    add("figure.move_to_window",
        "Move Figure to Window",
        "Ctrl+Shift+M",
        "App",
        [this]()
        {
            auto* target_window = command_target_window();
            if (!target_window || !window_registry_)
                return;
            FigureId fid = target_window->active_figure_id();
            if (fid == INVALID_FIGURE_ID)
                return;
            const HostId source = window_registry_->find_host_for_figure(fid);
            if (source == INVALID_HOST_ID)
                return;
            if (source == window_registry_->primary_host_id())
                window_registry_->detach_document(fid, source);
            else
                window_registry_->redock_document(fid, source);
        });

    services_->shortcuts().register_defaults();
    SPECTRA_LOG_INFO("qt_app", "Registered " + std::to_string(commands.count()) + " Qt commands");
}

void QtApplicationController::shutdown()
{
    if (!initialized_)
        return;

    // Persist while the live windows and docking registry still describe the
    // session. Saving after destroying them drops document/window topology.
    if (autosave_ && autosave_->has_unsaved_changes())
        autosave_->save_now();

    // The modeless function editor is also parented to the main window; release
    // its explicit owner before Qt destroys the child hierarchy.
    function_plot_dialog_.reset();

    // Destroy main window first (it holds canvas widgets with Vulkan surfaces)
    main_window_.reset();

    // Close all detached windows
    window_registry_.reset();
    workspace_bridge_.reset();

    // Stop automation adapter
    if (automation_adapter_)
        automation_adapter_->stop();
    automation_adapter_.reset();

    // Shut down services
    if (services_)
        services_->shutdown();
    services_.reset();
    timelines_.clear();
    interpolators_.clear();
    series_clipboard_.reset();
    axis_link_manager_.reset();

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

    // A successfully completed shutdown is not a crash. Keep the snapshot
    // through teardown so an interruption remains recoverable, then remove it
    // only after every owned frontend/runtime resource has shut down cleanly.
    if (autosave_)
        autosave_->clear_autosave();
    autosave_.reset();
    initialized_ = false;
    SPECTRA_LOG_INFO("qt_app", "QtApplicationController shutdown complete");
}

TimelineEditor* QtApplicationController::timeline_for(FigureId id)
{
    if (id == INVALID_FIGURE_ID)
        return nullptr;
    if (auto it = timelines_.find(id); it != timelines_.end())
        return it->second.get();

    Figure* figure = figure_registry().get(id);
    if (!figure)
        return nullptr;

    auto timeline     = std::make_unique<TimelineEditor>();
    auto interpolator = std::make_unique<KeyframeInterpolator>();
    timeline->set_interpolator(interpolator.get());
    if (figure->anim_duration() > 0.0f)
        timeline->set_duration(figure->anim_duration());
    else if (figure->has_animation())
        timeline->set_duration(60.0f);
    if (figure->anim_fps() > 0.0f)
        timeline->set_fps(figure->anim_fps());
    if (figure->anim_loop())
        timeline->set_loop_mode(LoopMode::Loop);
    if (figure->has_animation())
        timeline->play();
    timeline->set_playhead(figure->anim_.time);

    TimelineEditor* result = timeline.get();
    interpolators_.emplace(id, std::move(interpolator));
    timelines_.emplace(id, std::move(timeline));
    return result;
}

TimelineEditor* QtApplicationController::active_timeline()
{
    auto* window = command_target_window();
    return window ? timeline_for(window->active_figure_id()) : nullptr;
}

SpectraMainWindow* QtApplicationController::command_target_window() const
{
    QWidget* active = QApplication::activeWindow();
    QWidget* focus  = QApplication::focusWidget();
    if (window_registry_)
    {
        for (HostId id : window_registry_->all_hosts())
        {
            auto* host   = window_registry_->native_host(id);
            auto* window = host ? host->main_window() : nullptr;
            if (window && (belongs_to_window(active, window) || belongs_to_window(focus, window)))
                return window;
        }
    }
    return main_window_.get();
}

SpectraVulkanWindow* QtApplicationController::canvas_for_figure(FigureId id) const
{
    if (id == INVALID_FIGURE_ID)
        return nullptr;
    if (window_registry_)
    {
        const HostId host_id = window_registry_->find_host_for_figure(id);
        auto*        host    = window_registry_->native_host(host_id);
        auto*        window  = host ? host->main_window() : nullptr;
        auto*        wrapper = window ? window->canvas_for(id) : nullptr;
        if (wrapper)
            return wrapper->vulkanWindow();
    }
    auto* wrapper = main_window_ ? main_window_->canvas_for(id) : nullptr;
    return wrapper ? wrapper->vulkanWindow() : nullptr;
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

WorkspaceData QtApplicationController::capture_workspace_data()
{
    auto&                        registry = figure_registry();
    const auto                   ids      = registry.all_ids();
    std::vector<Figure*>         figures;
    std::vector<OverlaySnapshot> overlays;
    figures.reserve(ids.size());
    overlays.reserve(ids.size());
    for (FigureId id : ids)
    {
        if (Figure* figure = registry.get(id))
        {
            figures.push_back(figure);
            OverlaySnapshot overlay;
            if (auto* canvas = canvas_for_figure(id))
                canvas->captureOverlaySnapshot(overlay);
            overlays.push_back(std::move(overlay));
        }
    }

    size_t         active_index = 0;
    const FigureId active_id = main_window_ ? main_window_->active_figure_id() : INVALID_FIGURE_ID;
    const auto     active_it = std::find(ids.begin(), ids.end(), active_id);
    if (active_it != ids.end())
        active_index = static_cast<size_t>(active_it - ids.begin());

    const std::string theme        = services_ ? services_->theme().current_theme_name() : "night";
    const bool        inspector    = main_window_ && main_window_->is_inspector_open();
    const bool        nav_expanded = main_window_ && !main_window_->is_nav_rail_compact();
    WorkspaceData     data         = Workspace::capture(figures,
                                            active_index,
                                            theme,
                                            inspector,
                                            300.0f,
                                            nav_expanded,
                                            &overlays);
    if (axis_link_manager_)
    {
        std::vector<Axes*>   axes2d;
        std::vector<Axes3D*> axes3d;
        for (Figure* figure : figures)
        {
            if (!figure)
                continue;
            for (auto& axes : figure->axes_mut())
                if (axes)
                    axes2d.push_back(axes.get());
            for (auto& axes : figure->all_axes_mut())
                if (auto* candidate = dynamic_cast<Axes3D*>(axes.get()))
                    axes3d.push_back(candidate);
        }
        data.axis_link_state = axis_link_manager_->serialize(
            [&axes2d](const Axes* axes)
            {
                const auto found = std::find(axes2d.begin(), axes2d.end(), axes);
                return found == axes2d.end() ? -1 : static_cast<int>(found - axes2d.begin());
            },
            [&axes3d](const Axes3D* axes)
            {
                const auto found = std::find(axes3d.begin(), axes3d.end(), axes);
                return found == axes3d.end() ? -1 : static_cast<int>(found - axes3d.begin());
            });
    }
    for (size_t i = 0; i < data.figures.size() && i < ids.size(); ++i)
    {
        data.figures[i].figure_id = ids[i];
        if (TimelineEditor* timeline = timeline_for(ids[i]))
            data.figures[i].timeline_json = timeline->serialize();
        SpectraMainWindow* window = main_window_.get();
        if (window_registry_)
        {
            const HostId host_id = window_registry_->find_host_for_figure(ids[i]);
            if (auto* host = window_registry_->native_host(host_id); host && host->main_window())
                window = host->main_window();
        }
        if (window && window->transform_panel())
        {
            if (auto state = window->transform_panel()->capture_pipeline_state(ids[i], i))
                data.transforms.push_back(std::move(*state));
        }
    }
    if (TimelineEditor* timeline = timeline_for(active_id))
    {
        data.timeline.playhead   = timeline->playhead();
        data.timeline.duration   = timeline->duration();
        data.timeline.fps        = timeline->fps();
        data.timeline.loop_mode  = static_cast<int>(timeline->loop_mode());
        data.timeline.loop_start = timeline->loop_in();
        data.timeline.loop_end   = timeline->loop_out();
        data.timeline.playing    = timeline->is_playing();
    }
    if (workspace_bridge_)
        workspace_bridge_->capture_layout(data);
    if (services_)
    {
        std::unordered_set<std::string> bound_commands;
        for (const auto& binding : services_->shortcuts().all_bindings())
        {
            if (!services_->commands().find(binding.command_id))
                continue;
            data.shortcut_overrides.push_back(
                {binding.command_id, binding.shortcut.to_string(), false});
            bound_commands.insert(binding.command_id);
        }
        for (const Command* command : services_->commands().all_commands())
        {
            if (command && !bound_commands.contains(command->id))
                data.shortcut_overrides.push_back({command->id, "", true});
        }
    }
    return data;
}

bool QtApplicationController::restore_workspace_data(const WorkspaceData& data)
{
    auto restore_axis_links = [this, &data](const std::vector<FigureId>& ids)
    {
        if (!axis_link_manager_)
            return;
        std::vector<Axes*>   axes2d;
        std::vector<Axes3D*> axes3d;
        for (FigureId id : ids)
        {
            Figure* figure = figure_registry().get(id);
            if (!figure)
                continue;
            for (auto& axes : figure->axes_mut())
                if (axes)
                    axes2d.push_back(axes.get());
            for (auto& axes : figure->all_axes_mut())
                if (auto* candidate = dynamic_cast<Axes3D*>(axes.get()))
                    axes3d.push_back(candidate);
        }
        axis_link_manager_->deserialize(
            data.axis_link_state,
            [&axes2d](int index) -> Axes*
            {
                return index >= 0 && static_cast<size_t>(index) < axes2d.size()
                           ? axes2d[static_cast<size_t>(index)]
                           : nullptr;
            },
            [&axes3d](int index) -> Axes3D*
            {
                return index >= 0 && static_cast<size_t>(index) < axes3d.size()
                           ? axes3d[static_cast<size_t>(index)]
                           : nullptr;
            });
    };

    auto restore_transforms = [this, &data](const std::vector<FigureId>& ids)
    {
        for (const auto& state : data.transforms)
        {
            if (state.figure_index >= ids.size())
                continue;
            const FigureId     id     = ids[state.figure_index];
            SpectraMainWindow* window = main_window_.get();
            if (window_registry_)
            {
                const HostId host_id = window_registry_->find_host_for_figure(id);
                if (auto* host = window_registry_->native_host(host_id);
                    host && host->main_window())
                    window = host->main_window();
            }
            if (window && window->transform_panel())
                window->transform_panel()->restore_pipeline_state(id, state);
        }
    };

    auto restore_shortcuts = [this, &data]()
    {
        if (!services_ || data.shortcut_overrides.empty())
            return;
        services_->shortcuts().clear();
        for (const auto& override : data.shortcut_overrides)
        {
            if (override.removed)
                continue;
            const Shortcut shortcut = Shortcut::from_string(override.shortcut_str);
            if (shortcut.valid() && services_->commands().find(override.command_id))
                services_->shortcuts().bind(shortcut, override.command_id);
        }
        if (action_bridge_)
            action_bridge_->sync_shortcuts(services_->shortcuts());
        if (main_window_ && main_window_->shortcut_panel())
            main_window_->shortcut_panel()->refresh();
    };

    auto restore_timeline = [this, &data](size_t index, FigureId id)
    {
        TimelineEditor* timeline = timeline_for(id);
        if (!timeline)
            return;
        if (index < data.figures.size() && !data.figures[index].timeline_json.empty())
        {
            timeline->deserialize(data.figures[index].timeline_json);
            if (Figure* figure = figure_registry().get(id))
                bind_timeline_properties(*timeline, *figure);
            return;
        }
        if (index != data.active_figure_index)
            return;
        timeline->set_duration(data.timeline.duration);
        timeline->set_fps(data.timeline.fps);
        timeline->set_loop_mode(static_cast<LoopMode>(std::clamp(data.timeline.loop_mode, 0, 2)));
        if (data.timeline.loop_end > data.timeline.loop_start)
            timeline->set_loop_region(data.timeline.loop_start, data.timeline.loop_end);
        else
            timeline->clear_loop_region();
        if (data.timeline.playing)
            timeline->play();
        else
            timeline->stop();
        timeline->set_playhead(data.timeline.playhead);
    };

    std::vector<std::unique_ptr<Figure>> restored;
    std::vector<OverlaySnapshot>         overlays;
    if (!Workspace::restore_figures(data, restored, &overlays))
    {
        auto&                registry = figure_registry();
        std::vector<Figure*> existing;
        for (FigureId id : registry.all_ids())
            if (Figure* figure = registry.get(id))
                existing.push_back(figure);
        const bool applied = Workspace::apply(data, existing);
        if (applied)
        {
            for (size_t i = 0; i < existing.size() && i < data.figures.size(); ++i)
            {
                const FigureId id = registry.find_id(existing[i]);
                restore_timeline(i, id);
            }
        }
        if (workspace_bridge_)
            workspace_bridge_->apply_layout(data);
        if (applied)
        {
            std::vector<FigureId> existing_ids;
            existing_ids.reserve(existing.size());
            for (Figure* figure : existing)
                existing_ids.push_back(registry.find_id(figure));
            restore_transforms(existing_ids);
            restore_axis_links(existing_ids);
            restore_shortcuts();
        }
        return applied;
    }

    if (window_registry_)
    {
        for (HostId host_id : window_registry_->all_hosts())
        {
            auto* host   = window_registry_->native_host(host_id);
            auto* window = host ? host->main_window() : nullptr;
            if (!window)
                continue;
            for (FigureId id : window->open_figure_ids())
                window->release_figure_tab(id);
        }
        window_registry_->close_all_detached();
    }

    if (services_)
        services_->undo().clear();
    timelines_.clear();
    interpolators_.clear();
    if (axis_link_manager_)
        axis_link_manager_->clear();

    auto& registry = figure_registry();
    registry.clear();
    std::unordered_map<FigureId, FigureId> id_map;
    std::vector<FigureId>                  new_ids;
    new_ids.reserve(restored.size());
    for (size_t i = 0; i < restored.size(); ++i)
    {
        const FigureId new_id = registry.register_figure(std::move(restored[i]));
        new_ids.push_back(new_id);
        const FigureId old_id = data.figures[i].figure_id;
        if (old_id != INVALID_FIGURE_ID)
            id_map[old_id] = new_id;
    }

    for (size_t i = 0; i < new_ids.size() && i < data.figures.size(); ++i)
        restore_timeline(i, new_ids[i]);
    restore_axis_links(new_ids);

    bool layout_applied = workspace_bridge_ && workspace_bridge_->apply_layout(data, id_map);
    if (main_window_)
    {
        for (FigureId id : new_ids)
        {
            if (!window_registry_ || window_registry_->find_host_for_figure(id) == INVALID_HOST_ID)
                main_window_->add_figure_tab(id);
        }
        if (!new_ids.empty())
        {
            const size_t active = std::min<size_t>(data.active_figure_index, new_ids.size() - 1);
            main_window_->central_view()->activate_figure(new_ids[active]);
        }

        for (size_t i = 0; i < new_ids.size() && i < overlays.size(); ++i)
        {
            if (auto* canvas = canvas_for_figure(new_ids[i]))
                canvas->restoreOverlaySnapshot(overlays[i]);
        }

        if (main_window_->is_inspector_open() != data.panels.inspector_visible)
            main_window_->toggle_inspector();
    }

    restore_transforms(new_ids);

    if (services_ && !data.theme_name.empty())
    {
        services_->theme().set_theme(data.theme_name);
        if (main_window_)
            main_window_->refresh_theme();
    }
    restore_shortcuts();
    if (redraw_request_)
        redraw_request_->request_redraw();
    return layout_applied || !new_ids.empty() || data.figures.empty();
}

void QtApplicationController::check_crash_recovery()
{
    if (!autosave_ || !autosave_->has_autosave())
        return;

    QMessageBox::StandardButton reply           = QMessageBox::No;
    const char*                 recovery_policy = std::getenv("SPECTRA_RECOVER_AUTOSAVE");
    if (recovery_policy && std::string_view(recovery_policy) == "accept")
    {
        reply = QMessageBox::Yes;
    }
    else if (recovery_policy && std::string_view(recovery_policy) == "decline")
    {
        reply = QMessageBox::No;
    }
    else
    {
        // Interactive production policy. The environment override above is a
        // deterministic launched-test seam and does not change the default UI.
        reply = QMessageBox::question(main_window_.get(),
                                      "Spectra — Crash Recovery",
                                      "An autosave file was found from a previous session.\n"
                                      "Would you like to restore your workspace?",
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::Yes);
    }

    if (reply == QMessageBox::Yes)
    {
        std::string   path = autosave_->autosave_path();
        WorkspaceData data;
        if (Workspace::load(path, data))
        {
            if (restore_workspace_data(data))
                SPECTRA_LOG_INFO("qt_app", "Restored workspace from autosave: " + path);
            else
                SPECTRA_LOG_WARN("qt_app", "Autosave contained no restorable workspace: " + path);
        }
        else
        {
            SPECTRA_LOG_WARN("qt_app", "Failed to load autosave: " + path);
        }
    }

    // Clear the autosave regardless — user has been prompted
    autosave_->clear_autosave();
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
    auto* mw     = main_window_.get();
    auto* client = ipc_client_.get();

    // On snapshot: rebuild registry and update tabs
    QObject::connect(
        client,
        &QtIpcClient::snapshot_received,
        mw,
        [this, client]()
        {
            auto& reg = figure_registry();
            auto  ids = ipc::rebuild_registry_from_cache(reg, client->figure_cache(), 1280, 720);
            client->set_local_ids(ids);

            // Clear existing tabs and add new ones
            if (main_window_)
            {
                for (auto id : reg.all_ids())
                    main_window_->add_figure_tab(id);
            }
        });

    // On diff: request redraw (fast path already applied to live figures)
    QObject::connect(
        client,
        &QtIpcClient::diff_applied,
        mw,
        [this, client](bool needs_rebuild)
        {
            if (needs_rebuild)
            {
                auto& reg = figure_registry();
                auto ids = ipc::rebuild_registry_from_cache(reg, client->figure_cache(), 1280, 720);
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
    QObject::connect(client,
                     &QtIpcClient::connection_lost,
                     mw,
                     []() { SPECTRA_LOG_WARN("qt_app", "IPC connection to daemon lost"); });

    // On close requested: close the main window
    QObject::connect(client,
                     &QtIpcClient::close_requested,
                     mw,
                     [this]()
                     {
                         if (main_window_)
                             main_window_->close();
                     });

    // If the daemon already has figures, rebuild now
    if (!client->figure_cache().empty())
    {
        auto& reg = figure_registry();
        auto  ids = ipc::rebuild_registry_from_cache(reg, client->figure_cache(), 1280, 720);
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
