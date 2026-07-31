// qt_main_window.cpp — Production Qt main window for Spectra.

#include "qt_main_window.hpp"

#include "figure_canvas_widget.hpp"
#include "split_view_container.hpp"
#include "panels/command_palette_dialog.hpp"
#include "panels/data_editor_widget.hpp"
#include "panels/export_widget.hpp"
#include "panels/inspector_widget.hpp"
#include "panels/settings_widget.hpp"
#include "panels/shortcut_widget.hpp"
#include "panels/timeline_widget.hpp"
#include "panels/topics_widget.hpp"
#include "panels/transform_widget.hpp"
#include "panels/accessibility_widget.hpp"
#include "panels/plugin_panel_widget.hpp"
#include "panels/plugins_widget.hpp"
#include "qt_action_bridge.hpp"
#include "qt_runtime.hpp"

#include "components/spectra_design_tokens.hpp"
#include "components/spectra_title_bar.hpp"
#include "components/spectra_app_header.hpp"
#include "components/spectra_nav_rail.hpp"
#include "components/spectra_document_tab_bar.hpp"
#include "components/spectra_status_bar.hpp"
#include "components/spectra_inspector_drawer.hpp"
#include "components/spectra_canvas_frame.hpp"
#include "spectra_icon_embedded.hpp"

#include "app/application_services.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/input/input.hpp"
#include "ui/settings/settings_store.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QAction>
#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPoint>
#include <QPixmap>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <sstream>
#include <utility>

namespace spectra::adapters::qt
{
namespace
{

QString automation_menu_label(QString label)
{
    label.remove('&');
    return label;
}

void append_automation_menu_actions(std::ostringstream&    result,
                                    const QList<QAction*>& actions,
                                    bool&                  first_item)
{
    for (const QAction* action : actions)
    {
        if (!action || !action->isVisible())
            continue;
        if (action->isSeparator())
        {
            if (!first_item)
                result << ',';
            first_item = false;
            result << R"({"separator":true})";
            continue;
        }
        if (action->menu())
        {
            append_automation_menu_actions(result, action->menu()->actions(), first_item);
            continue;
        }

        const QString label = automation_menu_label(action->text());
        if (label.isEmpty())
            continue;
        if (!first_item)
            result << ',';
        first_item = false;
        result << R"({"label":")" << json_escape(label.toStdString()) << R"(","enabled":)"
               << (action->isEnabled() ? "true" : "false") << R"(,"checkable":)"
               << (action->isCheckable() ? "true" : "false") << '}';
    }
}

struct ToolUiState
{
    int         nav_index;
    const char* name;
    const char* command_id;
};

ToolUiState tool_ui_state(ToolMode tool)
{
    switch (tool)
    {
        case ToolMode::Select:
            return {0, "Select", "tool.select"};
        case ToolMode::Pan:
            return {1, "Pan", "tool.pan"};
        case ToolMode::BoxZoom:
            return {2, "Zoom", "tool.box_zoom"};
        case ToolMode::Measure:
            return {3, "Measure", "tool.measure"};
        case ToolMode::Annotate:
            return {4, "Annotate", "tool.annotate"};
        case ToolMode::ROI:
            return {5, "ROI", "tool.roi"};
    }
    return {1, "Pan", "tool.pan"};
}

bool tool_mode_for_nav_index(int nav_index, ToolMode& tool)
{
    switch (nav_index)
    {
        case 0:
            tool = ToolMode::Select;
            return true;
        case 1:
            tool = ToolMode::Pan;
            return true;
        case 2:
            tool = ToolMode::BoxZoom;
            return true;
        case 3:
            tool = ToolMode::Measure;
            return true;
        case 4:
            tool = ToolMode::Annotate;
            return true;
        case 5:
            tool = ToolMode::ROI;
            return true;
        default:
            return false;
    }
}

}   // namespace

SpectraMainWindow::~SpectraMainWindow() = default;

SpectraMainWindow::SpectraMainWindow(QtRuntime*           runtime,
                                     FigureRegistry*      registry,
                                     QtActionBridge*      action_bridge,
                                     ApplicationServices* services,
                                     QWidget*             parent)
    : QMainWindow(parent), runtime_(runtime), registry_(registry), action_bridge_(action_bridge),
      services_(services)
{
    setWindowTitle("Spectra");
    QPixmap app_icon;
    app_icon.loadFromData(SpectraIcon_png_data, SpectraIcon_png_size, "PNG");
    setWindowIcon(QIcon(app_icon));
    resize(1280, 720);

    // Load custom fonts before any UI is built
    load_fonts();

    // Load Spectra font manager (Inter + icon font)
    SpectraFontManager::instance().load_fonts();

    // Central split-view container for figure documents
    central_view_ = new QtSplitViewContainer(runtime_, registry_, this);
    central_view_->setObjectName("central_view");

    // Forward signals from the split view container
    connect(central_view_,
            &QtSplitViewContainer::figure_closed,
            this,
            [this](FigureId id)
            {
                if (doc_tab_bar_)
                    doc_tab_bar_->remove_tab(static_cast<int>(id));
                emit figure_closed(id);
            });
    connect(central_view_,
            &QtSplitViewContainer::figure_activated,
            this,
            [this](FigureId id)
            {
                if (doc_tab_bar_)
                    doc_tab_bar_->set_active_tab(static_cast<int>(id));
                emit figure_activated(id);
                if (inspector_panel_)
                    inspector_panel_->set_active_figure(id);
                if (export_panel_)
                    export_panel_->set_active_figure(id);
                if (transform_panel_)
                    transform_panel_->set_active_figure(id);
                if (data_editor_panel_)
                    data_editor_panel_->set_active_figure(id);
                if (accessibility_panel_)
                    accessibility_panel_->set_active_figure(id);
                sync_active_tool_ui();
            });
    connect(central_view_,
            &QtSplitViewContainer::figure_detach_requested,
            this,
            &SpectraMainWindow::figure_detach_requested);
    connect(central_view_,
            &QtSplitViewContainer::canvas_created,
            this,
            [this](FigureId, FigureCanvasWidget* canvas)
            {
                if (!canvas || !canvas->vulkanWindow())
                    return;
                auto* vw = canvas->vulkanWindow();
                vw->setInspectorToggleCallbacks(
                    [this]()
                    {
                        if (spectra_inspector_)
                            spectra_inspector_->toggle();
                    },
                    [this]() { return spectra_inspector_ && spectra_inspector_->is_open(); });

                // Wire cursor and frame stats to the status bar
                if (spectra_status_)
                {
                    connect(vw,
                            &SpectraVulkanWindow::cursorMoved,
                            spectra_status_,
                            &SpectraStatusBar::set_cursor_coords);
                    connect(vw,
                            &SpectraVulkanWindow::frameStats,
                            spectra_status_,
                            [this](int fps, double gpu_ms)
                            {
                                spectra_status_->set_fps(fps);
                                spectra_status_->set_gpu_frame_time(gpu_ms);
                            });
                }
            });

    // Build menus (still needed for QMenu popups used by custom menu strip)
    build_menus();
    build_panels();
    build_command_palette();

    // Build the new Spectra custom UI (title bar, header, nav rail, etc.)
    build_spectra_ui();

    // Apply Spectra dark theme stylesheet
    apply_spectra_style();

    // Invalidate any saved dock state — start with clean default layout
    setDockNestingEnabled(true);

    // Show welcome page initially (no figures open)
    show_welcome_page();
}

// ── Figure tab management ─────────────────────────────────────────────────────

int SpectraMainWindow::add_figure_tab(FigureId id)
{
    if (!central_view_)
        return -1;
    int idx = central_view_->add_figure_tab(id);
    if (idx >= 0)
    {
        hide_welcome_page();
        if (doc_tab_bar_ && registry_)
        {
            if (auto* figure = registry_->get(id))
            {
                std::string title = figure->tab_title();
                if (title.empty())
                    title = "Figure " + std::to_string(id);
                doc_tab_bar_->add_tab(QString::fromStdString(title), static_cast<int>(id));
                doc_tab_bar_->set_active_tab(static_cast<int>(id));
            }
        }
        sync_active_tool_ui();
        SPECTRA_LOG_INFO("qt_main_window", "Added figure tab: id=" + std::to_string(id));
    }
    return idx;
}

bool SpectraMainWindow::close_figure_tab(FigureId id)
{
    if (!central_view_)
        return false;
    if (!central_view_->close_figure_tab(id))
        return false;

    if (central_view_->figure_tab_count() == 0)
        show_welcome_page();
    SPECTRA_LOG_INFO("qt_main_window", "Closed figure tab: id=" + std::to_string(id));
    return true;
}

bool SpectraMainWindow::release_figure_tab(FigureId id)
{
    if (!central_view_ || !central_view_->release_figure_tab(id))
        return false;

    if (doc_tab_bar_)
        doc_tab_bar_->remove_tab(static_cast<int>(id));
    if (central_view_->figure_tab_count() == 0)
        show_welcome_page();
    SPECTRA_LOG_INFO("qt_main_window", "Released figure tab: id=" + std::to_string(id));
    return true;
}

FigureId SpectraMainWindow::active_figure_id() const
{
    return central_view_ ? central_view_->active_figure_id() : INVALID_FIGURE_ID;
}

FigureCanvasWidget* SpectraMainWindow::canvas_for(FigureId id) const
{
    return central_view_ ? central_view_->canvas_for(id) : nullptr;
}

int SpectraMainWindow::figure_tab_count() const
{
    return central_view_ ? central_view_->figure_tab_count() : 0;
}

std::vector<FigureId> SpectraMainWindow::open_figure_ids() const
{
    return central_view_ ? central_view_->open_figure_ids() : std::vector<FigureId>{};
}

std::string SpectraMainWindow::automation_menu_state() const
{
    const std::array<std::pair<const char*, QMenu*>, 9> menus = {{
        {"File", menu_file_},
        {"Edit", menu_edit_},
        {"View", menu_view_},
        {"Tools", menu_tools_},
        {"Plot", menu_figure_},
        {"Data", menu_data_},
        {"Axes", menu_axes_},
        {"Transforms", menu_transforms_},
        {"Help", menu_help_},
    }};

    std::ostringstream result;
    result << R"({"menus":[)";
    bool first_menu = true;
    for (const auto& [name, menu] : menus)
    {
        if (!menu)
            continue;
        if (!first_menu)
            result << ',';
        first_menu = false;
        result << R"({"name":")" << name << R"(","items":[)";
        bool first_item = true;
        append_automation_menu_actions(result, menu->actions(), first_item);
        result << "]}";
    }
    result << "]}";
    return result.str();
}

bool SpectraMainWindow::is_inspector_open() const
{
    return spectra_inspector_ && spectra_inspector_->is_open();
}

bool SpectraMainWindow::is_nav_rail_compact() const
{
    return nav_rail_ && nav_rail_->is_compact();
}

// ── Welcome page ──────────────────────────────────────────────────────────────

void SpectraMainWindow::show_welcome_page()
{
    if (central_view_)
        central_view_->show_welcome_page();
}

void SpectraMainWindow::hide_welcome_page()
{
    if (central_view_)
        central_view_->hide_welcome_page();
}

// ── Status bar ────────────────────────────────────────────────────────────────

void SpectraMainWindow::set_status(const std::string& message)
{
    if (status_label_)
        status_label_->setText(QString::fromStdString(message));
    if (spectra_status_)
        spectra_status_->set_message(QString::fromStdString(message));
}

void SpectraMainWindow::set_active_tool(ToolMode tool)
{
    if (central_view_)
        central_view_->set_active_tool(tool);
    sync_active_tool_ui();
}

void SpectraMainWindow::set_nav_rail_visible(bool visible)
{
    if (nav_rail_)
        nav_rail_->setVisible(visible);
    if (settings_panel_)
        settings_panel_->set_nav_rail_visible(visible);
}

bool SpectraMainWindow::is_nav_rail_visible() const
{
    return nav_rail_ && !nav_rail_->isHidden();
}

void SpectraMainWindow::sync_active_tool_ui()
{
    const ToolUiState state =
        tool_ui_state(central_view_ ? central_view_->active_tool() : ToolMode::Pan);
    if (nav_rail_)
        nav_rail_->set_active_tool(state.nav_index);
    if (spectra_status_)
        spectra_status_->set_active_tool(QString::fromUtf8(state.name));
}

void SpectraMainWindow::set_timeline_visible(bool visible)
{
    if (timeline_panel_)
        timeline_panel_->setHidden(!visible);
    if (auto* action = findChild<QAction*>("view_toggle_timeline"))
    {
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    if (settings_panel_)
        settings_panel_->set_timeline_visible(visible);
}

// ── Private slots ─────────────────────────────────────────────────────────────

void SpectraMainWindow::on_tab_changed(int index)
{
    (void)index;
    // Tab changes are now handled by QtSplitViewContainer signals.
}

void SpectraMainWindow::on_tab_close_requested(int index)
{
    (void)index;
    // Tab close requests are now handled by QtSplitViewContainer internally.
}

// ── Private: build menus ──────────────────────────────────────────────────────

void SpectraMainWindow::build_menus()
{
    if (!action_bridge_)
        return;

    // Standard menus (used as popup QMenu objects for custom menu strip)
    menu_file_       = new QMenu("&File", this);
    menu_edit_       = new QMenu("&Edit", this);
    menu_view_       = new QMenu("&View", this);
    menu_tools_      = new QMenu("&Tools", this);
    menu_figure_     = new QMenu("&Plot", this);
    menu_data_       = new QMenu("&Data", this);
    menu_axes_       = new QMenu("&Axes", this);
    menu_transforms_ = new QMenu("&Transforms", this);
    menu_help_       = new QMenu("&Help", this);
    menu_file_->setObjectName("menu_file");
    menu_edit_->setObjectName("menu_edit");
    menu_view_->setObjectName("menu_view");
    menu_tools_->setObjectName("menu_tools");
    menu_figure_->setObjectName("menu_plot");
    menu_data_->setObjectName("menu_data");
    menu_axes_->setObjectName("menu_axes");
    menu_transforms_->setObjectName("menu_transforms");
    menu_help_->setObjectName("menu_help");

    // Submenus
    menu_view_panels_ = new QMenu("Panels", menu_view_);
    menu_view_panels_->setObjectName("menu_view_panels");
    menu_view_splits_ = new QMenu("Splits", menu_view_);
    menu_view_splits_->setObjectName("menu_view_splits");
    menu_tools_theme_ = new QMenu("Theme", menu_tools_);
    menu_tools_theme_->setObjectName("menu_tools_theme");
    menu_tools_anim_ = new QMenu("Animation", menu_tools_);
    menu_tools_anim_->setObjectName("menu_tools_anim");

    // Route each action by command ID prefix, not just category.
    // This fixes the gap where Figure lifecycle commands appeared under Plot,
    // Help was empty, and View was oversized with duplicated panel toggles.
    auto categorized = action_bridge_->actions_by_category();
    for (const auto& [category, actions] : categorized)
    {
        for (auto* action : actions)
        {
            // Extract command ID from objectName ("action_" prefix).
            const QString objName = action->objectName();
            const QString cmdId   = objName.mid(7);

            QMenu* target = nullptr;

            // ── Route by command ID prefix ───────────────────────────
            if (cmdId.startsWith("figure.new") || cmdId.startsWith("figure.close")
                || cmdId.startsWith("figure.next_tab") || cmdId.startsWith("figure.prev_tab")
                || cmdId.startsWith("figure.tab_") || cmdId.startsWith("figure.move_to_window"))
                target = menu_file_;
            else if (cmdId.startsWith("file."))
                target = menu_file_;
            else if (cmdId == "app.quit" || cmdId == "app.new_window"
                     || cmdId == "app.command_palette")
                target = menu_file_;
            else if (cmdId == "help.show")
                target = menu_help_;
            else if (cmdId.startsWith("edit."))
                target = menu_edit_;
            else if (cmdId.startsWith("series."))
                target = menu_edit_;
            else if (cmdId.startsWith("view.split_") || cmdId.startsWith("view.close_split")
                     || cmdId.startsWith("view.reset_splits")
                     || cmdId.startsWith("view.reset_layout"))
                target = menu_view_splits_;
            else if (cmdId.startsWith("view."))
                target = menu_view_;
            else if (cmdId.startsWith("panel."))
                target = menu_view_panels_;
            else if (cmdId.startsWith("tool."))
                target = menu_tools_;
            else if (cmdId.startsWith("theme."))
                target = menu_tools_theme_;
            else if (cmdId.startsWith("anim."))
                target = menu_tools_anim_;
            else if (cmdId.startsWith("accessibility."))
                target = menu_tools_;
            else if (cmdId.startsWith("plot."))
                target = menu_figure_;
            else if (cmdId.startsWith("data."))
                target = menu_data_;
            else if (category == "Axes")
                target = menu_axes_;
            else if (category == "Transforms")
                target = menu_transforms_;
            else
            {
                target = get_or_create_menu(category);
            }

            if (!target)
                continue;

            target->addAction(action);
            addAction(action);
        }
    }

    // Add submenus to their parents (only if non-empty).
    if (!menu_tools_theme_->actions().isEmpty())
    {
        menu_tools_->addSeparator();
        menu_tools_->addMenu(menu_tools_theme_);
    }
    if (!menu_tools_anim_->actions().isEmpty())
    {
        menu_tools_->addSeparator();
        menu_tools_->addMenu(menu_tools_anim_);
    }
}

QMenu* SpectraMainWindow::get_or_create_menu(const std::string& path)
{
    // Check if it already exists in our menu list
    QString name = QString::fromStdString(path);
    // Not using menuBar() anymore — create standalone menu
    return new QMenu(name, this);
}

// ── Private: build toolbar ────────────────────────────────────────────────────

void SpectraMainWindow::build_toolbar()
{
    toolbar_ = addToolBar("Main");
    toolbar_->setObjectName("main_toolbar");
    toolbar_->setMovable(false);

    // Add key actions to toolbar
    if (action_bridge_)
    {
        // Add common file/view actions
        const std::vector<std::string> toolbar_cmds = {
            "file.new",
            "file.open",
            "file.save",
            "view.reset",
            "view.fit",
        };

        for (const auto& cmd_id : toolbar_cmds)
        {
            QAction* action = action_bridge_->action_for(cmd_id);
            if (action)
                toolbar_->addAction(action);
        }
    }
}

// ── Private: build status bar ─────────────────────────────────────────────────

void SpectraMainWindow::build_status_bar()
{
    // Old status bar replaced by SpectraStatusBar in build_spectra_ui()
    // Keep status_label_ for backward compat with set_status()
    status_label_ = new QLabel(this);
    status_label_->setObjectName("status_label");
}

// ── Private: build panels ────────────────────────────────────────────────────

void SpectraMainWindow::build_panels()
{
    if (!services_)
        return;

    // Inspector panel
    inspector_panel_ = new QtInspectorWidget(registry_, services_, this);
    inspector_panel_->setObjectName("inspector_dock");

    // Topics / data sources panel (hidden by default)
    topics_panel_ = new QtTopicsWidget(&services_->data_sources(), this);
    topics_panel_->setObjectName("topics_dock");
    addDockWidget(Qt::RightDockWidgetArea, topics_panel_);
    topics_panel_->hide();

    // Settings panel (hidden by default)
    settings_panel_ = new QtSettingsWidget(&services_->settings(), &services_->theme(), this);
    settings_panel_->setObjectName("settings_dock");
    addDockWidget(Qt::LeftDockWidgetArea, settings_panel_);
    settings_panel_->hide();
    connect(settings_panel_,
            &QtSettingsWidget::inspector_visibility_changed,
            this,
            &SpectraMainWindow::on_toggle_inspector);
    connect(settings_panel_,
            &QtSettingsWidget::nav_rail_visibility_changed,
            this,
            &SpectraMainWindow::set_nav_rail_visible);
    connect(settings_panel_,
            &QtSettingsWidget::timeline_visibility_changed,
            this,
            &SpectraMainWindow::set_timeline_visible);

    // Timeline panel (hidden by default)
    timeline_panel_ = new QtTimelineWidget(nullptr, this);
    timeline_panel_->setObjectName("timeline_dock");
    addDockWidget(Qt::BottomDockWidgetArea, timeline_panel_);
    timeline_panel_->hide();

    // Export panel (hidden by default)
    export_panel_ = new QtExportWidget(&services_->export_formats(),
                                       registry_,
                                       services_->dialog_service(),
                                       this);
    export_panel_->setObjectName("export_dock");
    addDockWidget(Qt::RightDockWidgetArea, export_panel_);
    export_panel_->hide();

    // Shortcut editor panel (hidden by default)
    shortcut_panel_ = new QtShortcutWidget(&services_->shortcuts(), this);
    shortcut_panel_->setObjectName("shortcut_dock");
    addDockWidget(Qt::LeftDockWidgetArea, shortcut_panel_);
    shortcut_panel_->hide();

    // Transform panel (hidden by default)
    transform_panel_ =
        new QtTransformWidget(registry_, &services_->undo(), services_->redraw_request(), this);
    transform_panel_->setObjectName("transform_dock");
    addDockWidget(Qt::RightDockWidgetArea, transform_panel_);
    transform_panel_->hide();

    // Data editor panel (hidden by default)
    data_editor_panel_ =
        new QtDataEditorWidget(registry_, &services_->undo(), services_->redraw_request(), this);
    data_editor_panel_->setObjectName("data_editor_dock");
    addDockWidget(Qt::RightDockWidgetArea, data_editor_panel_);
    data_editor_panel_->hide();

    // Accessibility panel (hidden by default)
    accessibility_panel_ = new QtAccessibilityWidget(registry_, this);
    accessibility_panel_->setObjectName("accessibility_dock");
    addDockWidget(Qt::LeftDockWidgetArea, accessibility_panel_);
    accessibility_panel_->hide();

    // Plugin UI panel (hidden by default)
    plugin_panel_ = new QtPluginPanelWidget(&services_->plugin_ui(), this);
    plugin_panel_->setObjectName("plugin_panel_dock");
    addDockWidget(Qt::RightDockWidgetArea, plugin_panel_);
    plugin_panel_->hide();

    // Plugins management panel (hidden by default)
    plugins_panel_ = new QtPluginsWidget(&services_->plugins(), &services_->plugin_ui(), this);
    plugins_panel_->setObjectName("plugins_mgmt_dock");
    addDockWidget(Qt::LeftDockWidgetArea, plugins_panel_);
    plugins_panel_->hide();

    // Add panel toggle actions to the Panels submenu (not directly to View)
    if (menu_view_panels_)
    {
        inspector_toggle_action_ = menu_view_panels_->addAction("Inspector");
        inspector_toggle_action_->setObjectName("view_toggle_inspector");
        inspector_toggle_action_->setCheckable(true);
        inspector_toggle_action_->setChecked(false);
        connect(inspector_toggle_action_,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_inspector);

        auto* toggle_topics = menu_view_panels_->addAction("Data Sources");
        toggle_topics->setObjectName("view_toggle_topics");
        toggle_topics->setCheckable(true);
        toggle_topics->setChecked(false);
        connect(toggle_topics, &QAction::toggled, this, &SpectraMainWindow::on_toggle_topics);

        auto* toggle_settings = menu_view_panels_->addAction("Settings");
        toggle_settings->setObjectName("view_toggle_settings");
        toggle_settings->setCheckable(true);
        toggle_settings->setChecked(false);
        connect(toggle_settings, &QAction::toggled, this, &SpectraMainWindow::on_toggle_settings);

        auto* toggle_timeline = menu_view_panels_->addAction("Timeline");
        toggle_timeline->setObjectName("view_toggle_timeline");
        toggle_timeline->setCheckable(true);
        toggle_timeline->setChecked(false);
        connect(toggle_timeline, &QAction::toggled, this, &SpectraMainWindow::on_toggle_timeline);

        auto* toggle_export = menu_view_panels_->addAction("Export");
        toggle_export->setObjectName("view_toggle_export");
        toggle_export->setCheckable(true);
        toggle_export->setChecked(false);
        connect(toggle_export, &QAction::toggled, this, &SpectraMainWindow::on_toggle_export);

        auto* toggle_shortcuts = menu_view_panels_->addAction("Shortcuts");
        toggle_shortcuts->setObjectName("view_toggle_shortcuts");
        toggle_shortcuts->setCheckable(true);
        toggle_shortcuts->setChecked(false);
        connect(toggle_shortcuts, &QAction::toggled, this, &SpectraMainWindow::on_toggle_shortcuts);

        auto* toggle_transform = menu_view_panels_->addAction("Transforms");
        toggle_transform->setObjectName("view_toggle_transform");
        toggle_transform->setCheckable(true);
        toggle_transform->setChecked(false);
        connect(toggle_transform, &QAction::toggled, this, &SpectraMainWindow::on_toggle_transform);

        auto* toggle_data_editor = menu_view_panels_->addAction("Data Editor");
        toggle_data_editor->setObjectName("view_toggle_data_editor");
        toggle_data_editor->setCheckable(true);
        toggle_data_editor->setChecked(false);
        connect(toggle_data_editor,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_data_editor);

        auto* toggle_accessibility = menu_view_panels_->addAction("Accessibility");
        toggle_accessibility->setObjectName("view_toggle_accessibility");
        toggle_accessibility->setCheckable(true);
        toggle_accessibility->setChecked(false);
        connect(toggle_accessibility,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_accessibility);

        auto* toggle_plugin_panel = menu_view_panels_->addAction("Plugin Panels");
        toggle_plugin_panel->setObjectName("view_toggle_plugin_panel");
        toggle_plugin_panel->setCheckable(true);
        toggle_plugin_panel->setChecked(false);
        connect(toggle_plugin_panel,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_plugin_panel);

        auto* toggle_plugins = menu_view_panels_->addAction("Plugins");
        toggle_plugins->setObjectName("view_toggle_plugins");
        toggle_plugins->setCheckable(true);
        toggle_plugins->setChecked(false);
        connect(toggle_plugins, &QAction::toggled, this, &SpectraMainWindow::on_toggle_plugins);
    }

    // Add split actions to the Splits submenu
    if (menu_view_splits_)
    {
        auto* split_right_action = menu_view_splits_->addAction("Split Right");
        split_right_action->setObjectName("view_split_right");
        split_right_action->setShortcut(QKeySequence("Ctrl+\\"));
        connect(split_right_action, &QAction::triggered, this, &SpectraMainWindow::on_split_right);

        auto* split_down_action = menu_view_splits_->addAction("Split Down");
        split_down_action->setObjectName("view_split_down");
        split_down_action->setShortcut(QKeySequence("Ctrl+Shift+\\"));
        connect(split_down_action, &QAction::triggered, this, &SpectraMainWindow::on_split_down);

        auto* close_split_action = menu_view_splits_->addAction("Close Split Pane");
        close_split_action->setObjectName("view_close_split");
        connect(close_split_action, &QAction::triggered, this, &SpectraMainWindow::on_close_split);

        auto* reset_splits_action = menu_view_splits_->addAction("Reset Splits");
        reset_splits_action->setObjectName("view_reset_splits");
        connect(reset_splits_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::on_reset_splits);

        menu_view_splits_->addSeparator();

        auto* reset_layout_action = menu_view_splits_->addAction("Reset Layout");
        reset_layout_action->setObjectName("view_reset_layout");
        connect(reset_layout_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::on_reset_layout);
    }

    // Add submenus to the View menu
    if (menu_view_)
    {
        if (menu_view_panels_ && !menu_view_panels_->actions().isEmpty())
        {
            menu_view_->addSeparator();
            menu_view_->addMenu(menu_view_panels_);
        }
        if (menu_view_splits_ && !menu_view_splits_->actions().isEmpty())
        {
            menu_view_->addSeparator();
            menu_view_->addMenu(menu_view_splits_);
        }
    }
}

// ── Private: build command palette ──────────────────────────────────────────

void SpectraMainWindow::build_command_palette()
{
    if (!services_)
        return;

    command_palette_ = new QtCommandPaletteDialog(services_->commands(), this);
    command_palette_->setObjectName("command_palette");

    // Ctrl+K shortcut to toggle the palette
    palette_shortcut_ = new QShortcut(QKeySequence("Ctrl+K"), this);
    palette_shortcut_->setObjectName("palette_shortcut");
    connect(palette_shortcut_,
            &QShortcut::activated,
            this,
            &SpectraMainWindow::open_command_palette);

    // Add command palette action to the File menu
    if (menu_file_)
    {
        menu_file_->addSeparator();
        auto* palette_action = menu_file_->addAction("Command Palette...");
        palette_action->setObjectName("file_command_palette");
        palette_action->setShortcut(QKeySequence("Ctrl+K"));
        connect(palette_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::open_command_palette);
    }
}

void SpectraMainWindow::open_command_palette()
{
    if (command_palette_)
        command_palette_->open_palette();
}

// ── Panel toggle slots ──────────────────────────────────────────────────────

void SpectraMainWindow::toggle_inspector()
{
    if (spectra_inspector_)
        on_toggle_inspector(!spectra_inspector_->is_open());
}

void SpectraMainWindow::on_toggle_inspector(bool checked)
{
    if (spectra_inspector_)
    {
        if (checked)
            spectra_inspector_->open();
        else
            spectra_inspector_->close();
    }
    if (inspector_toggle_action_)
    {
        const QSignalBlocker blocker(inspector_toggle_action_);
        inspector_toggle_action_->setChecked(checked);
    }
    if (settings_panel_)
        settings_panel_->set_inspector_visible(checked);
}

void SpectraMainWindow::on_toggle_topics()
{
    if (topics_panel_)
        topics_panel_->setHidden(!topics_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_settings()
{
    if (settings_panel_)
        settings_panel_->setHidden(!settings_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_timeline()
{
    if (timeline_panel_)
        set_timeline_visible(timeline_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_export()
{
    if (export_panel_)
        export_panel_->setHidden(!export_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_shortcuts()
{
    if (shortcut_panel_)
        shortcut_panel_->setHidden(!shortcut_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_transform()
{
    if (transform_panel_)
        transform_panel_->setHidden(!transform_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_data_editor()
{
    if (data_editor_panel_)
        data_editor_panel_->setHidden(!data_editor_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_accessibility()
{
    if (accessibility_panel_)
        accessibility_panel_->setHidden(!accessibility_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_plugin_panel()
{
    if (plugin_panel_)
        plugin_panel_->setHidden(!plugin_panel_->isHidden());
}

void SpectraMainWindow::on_toggle_plugins()
{
    if (plugins_panel_)
        plugins_panel_->setHidden(!plugins_panel_->isHidden());
}

// ── Split view ────────────────────────────────────────────────────────────────

bool SpectraMainWindow::split_right()
{
    return central_view_ ? central_view_->split_right() : false;
}

bool SpectraMainWindow::split_down()
{
    return central_view_ ? central_view_->split_down() : false;
}

bool SpectraMainWindow::close_split()
{
    return central_view_ ? central_view_->close_split() : false;
}

void SpectraMainWindow::reset_splits()
{
    if (central_view_)
        central_view_->reset_splits();
}

bool SpectraMainWindow::is_split() const
{
    return central_view_ ? central_view_->is_split() : false;
}

size_t SpectraMainWindow::pane_count() const
{
    return central_view_ ? central_view_->pane_count() : 0;
}

void SpectraMainWindow::on_split_right()
{
    split_right();
}

void SpectraMainWindow::on_split_down()
{
    split_down();
}

void SpectraMainWindow::on_close_split()
{
    close_split();
}

void SpectraMainWindow::on_reset_splits()
{
    reset_splits();
}

// ── Tab context menu (detach support) ──────────────────────────────────────

void SpectraMainWindow::on_tab_context_menu(const QPoint& pos)
{
    (void)pos;
    // Tab context menu is now handled by QtSplitViewContainer internally.
}

void SpectraMainWindow::on_detach_tab()
{
    // Detach is now handled by QtSplitViewContainer's own context menu.
}

// ── Layout reset ───────────────────────────────────────────────────────────────

void SpectraMainWindow::on_reset_layout()
{
    reset_layout();
}

void SpectraMainWindow::reset_layout()
{
    // Hide all docks except Inspector
    if (settings_panel_)
        settings_panel_->hide();
    if (shortcut_panel_)
        shortcut_panel_->hide();
    if (accessibility_panel_)
        accessibility_panel_->hide();
    if (plugins_panel_)
        plugins_panel_->hide();
    if (topics_panel_)
        topics_panel_->hide();
    if (export_panel_)
        export_panel_->hide();
    if (transform_panel_)
        transform_panel_->hide();
    if (data_editor_panel_)
        data_editor_panel_->hide();
    if (plugin_panel_)
        plugin_panel_->hide();
    if (timeline_panel_)
        timeline_panel_->hide();

    // Show inspector (hidden by default per Vision.png)
    if (spectra_inspector_)
        spectra_inspector_->close();

    // Reset splits to single pane
    reset_splits();

    // Reset all panel toggle check states to match the hidden state
    if (menu_view_panels_)
    {
        for (auto* action : menu_view_panels_->actions())
        {
            if (action->isCheckable() && action != inspector_toggle_action_)
            {
                QSignalBlocker blocker(action);
                action->setChecked(false);
            }
        }
    }
    if (inspector_toggle_action_)
    {
        QSignalBlocker blocker(inspector_toggle_action_);
        inspector_toggle_action_->setChecked(false);
    }

    // Restore the legacy desktop client size.
    resize(1280, 720);
}

// ── Build Spectra custom UI ───────────────────────────────────────────────────

void SpectraMainWindow::build_spectra_ui()
{
    // Hide the default Qt menu bar — we use SpectraAppHeader with SpectraMenuStrip
    menuBar()->setVisible(false);

    // Create custom title bar
    title_bar_ = new SpectraTitleBar(this);
    title_bar_->setObjectName("spectra_title_bar");
    title_bar_->set_title("Spectra");
    title_bar_->set_window(this);

    // Create app header with logo, wordmark, menus, and glow
    app_header_ = new SpectraAppHeader(this);
    app_header_->setObjectName("spectra_app_header");

    // Populate menu strip with the QMenu objects from build_menus()
    if (menu_file_)
        app_header_->add_menu("File", menu_file_);
    if (menu_edit_)
        app_header_->add_menu("Edit", menu_edit_);
    if (menu_view_)
        app_header_->add_menu("View", menu_view_);
    if (menu_tools_)
        app_header_->add_menu("Tools", menu_tools_);
    if (menu_figure_)
        app_header_->add_menu("Plot", menu_figure_);
    if (menu_data_)
        app_header_->add_menu("Data", menu_data_);
    if (menu_axes_)
        app_header_->add_menu("Axes", menu_axes_);
    if (menu_transforms_)
        app_header_->add_menu("Transforms", menu_transforms_);
    if (menu_help_)
        app_header_->add_menu("Help", menu_help_);

    // Wire Home button to view.home command
    connect(app_header_,
            &SpectraAppHeader::home_clicked,
            this,
            [this]()
            {
                if (action_bridge_)
                {
                    if (auto* action = action_bridge_->action_for("view.home"))
                    {
                        action->trigger();
                        return;
                    }
                }
                // Fallback: auto-fit active figure
                if (central_view_)
                    central_view_->activate_figure(central_view_->active_figure_id());
            });

    // Create navigation rail
    nav_rail_ = new SpectraNavRail(this);
    nav_rail_->setObjectName("spectra_nav_rail");
    // Hide buttons that have no panel or command wired yet
    nav_rail_->set_button_visible(6, false);    // Markers
    nav_rail_->set_button_visible(10, false);   // Curve Editor
    nav_rail_->set_button_visible(14, false);   // Help (accessible via menu)
    connect(nav_rail_,
            &SpectraNavRail::tool_selected,
            this,
            [this](int tool)
            {
                ToolMode requested_tool = ToolMode::Pan;
                if (tool_mode_for_nav_index(tool, requested_tool))
                {
                    const ToolUiState requested_state = tool_ui_state(requested_tool);
                    if (action_bridge_)
                    {
                        if (auto* action = action_bridge_->action_for(requested_state.command_id))
                        {
                            action->trigger();
                            sync_active_tool_ui();
                            return;
                        }
                    }
                    set_active_tool(requested_tool);
                    return;
                }

                switch (tool)
                {
                    case 7:
                        if (transform_panel_)
                            transform_panel_->show();
                        break;
                    case 8:
                        if (spectra_inspector_)
                            spectra_inspector_->toggle();
                        break;
                    case 9:
                        if (timeline_panel_)
                            timeline_panel_->show();
                        break;
                    case 11:
                        if (plugins_panel_)
                            plugins_panel_->show();
                        break;
                    case 12:
                        if (topics_panel_)
                            topics_panel_->show();
                        break;
                    case 13:
                        if (settings_panel_)
                            settings_panel_->show();
                        break;
                    default:
                        break;
                }
            });

    // Create document tab bar
    doc_tab_bar_ = new SpectraDocumentTabBar(this);
    doc_tab_bar_->setObjectName("spectra_doc_tab_bar");
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_add_requested,
            this,
            [this]()
            {
                // Keep the visible add control on the same command path as
                // Ctrl+T, menus, the command palette, and automation.
                if (action_bridge_)
                {
                    if (auto* action = action_bridge_->action_for("figure.new"))
                    {
                        action->trigger();
                        return;
                    }
                }

                // Preserve the signal fallback for lightweight hosts that do
                // not install a command bridge.
                emit figure_activated(INVALID_FIGURE_ID);
            });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_selected,
            this,
            [this](int id)
            {
                if (central_view_)
                    central_view_->activate_figure(static_cast<FigureId>(id));
            });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_closed,
            this,
            [this](int id) { close_figure_tab(static_cast<FigureId>(id)); });
    connect(doc_tab_bar_,
            &SpectraDocumentTabBar::tab_detach_requested,
            this,
            [this](int id) { emit figure_detach_requested(static_cast<FigureId>(id)); });

    // Create canvas frame wrapping the central view
    canvas_frame_ = new SpectraCanvasFrame(central_view_, this);
    canvas_frame_->setObjectName("spectra_canvas_frame");

    // Create custom status bar
    spectra_status_ = new SpectraStatusBar(this);
    spectra_status_->setObjectName("spectra_status_bar");

    // Create inspector drawer (hidden by default)
    spectra_inspector_ = new SpectraInspectorDrawer(this);
    spectra_inspector_->setObjectName("spectra_inspector");
    spectra_inspector_->set_content_widget(inspector_panel_);
    connect(spectra_inspector_,
            &SpectraInspectorDrawer::opened,
            this,
            [this]()
            {
                if (inspector_toggle_action_)
                {
                    const QSignalBlocker blocker(inspector_toggle_action_);
                    inspector_toggle_action_->setChecked(true);
                }
                if (settings_panel_)
                    settings_panel_->set_inspector_visible(true);
            });
    connect(spectra_inspector_,
            &SpectraInspectorDrawer::closed,
            this,
            [this]()
            {
                if (inspector_toggle_action_)
                {
                    const QSignalBlocker blocker(inspector_toggle_action_);
                    inspector_toggle_action_->setChecked(false);
                }
                if (settings_panel_)
                    settings_panel_->set_inspector_visible(false);
            });
    // Match the legacy shell hierarchy: header, then a full-height rail beside
    // the document tabs and canvas, then the status strip.
    central_container_ = new QWidget(this);
    central_container_->setObjectName("spectra_central_container");

    auto* main_layout = new QVBoxLayout(central_container_);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // Native Qt window decoration replaces the platform decoration owned by
    // GLFW. Do not add a second title strip inside the client area.
    title_bar_->hide();

    // App header below title bar
    main_layout->addWidget(app_header_);

    // Workspace: the rail begins directly below the header. The tab strip is
    // part of the document column and therefore begins at x = rail width.
    auto* middle_layout = new QHBoxLayout();
    middle_layout->setContentsMargins(0, 0, 0, 0);
    middle_layout->setSpacing(0);

    middle_layout->addWidget(nav_rail_);

    auto* document_layout = new QVBoxLayout();
    document_layout->setContentsMargins(0, 0, 0, 0);
    document_layout->setSpacing(0);
    document_layout->addWidget(doc_tab_bar_);
    document_layout->addWidget(canvas_frame_, 1);
    middle_layout->addLayout(document_layout, 1);

    middle_layout->addWidget(spectra_inspector_);

    main_layout->addLayout(middle_layout, 1);

    // Status bar at bottom
    main_layout->addWidget(spectra_status_);

    // Set as central widget (replaces setCentralWidget(central_view_))
    setCentralWidget(central_container_);

    // Inspector is hidden by default
    spectra_inspector_->setVisible(false);

    // Apply persisted shell visibility after all custom surfaces exist.
    if (services_)
    {
        const auto& settings = services_->settings().data();
        set_nav_rail_visible(settings.nav_rail_visible);
        on_toggle_inspector(settings.inspector_visible);
        set_timeline_visible(settings.timeline_visible);
    }
}

void SpectraMainWindow::update_compact_mode()
{
    if (!nav_rail_)
        return;

    bool compact = width() < 1100;
    nav_rail_->set_compact_mode(compact);

    // In compact mode, inspector becomes overlay instead of taking layout space
    if (spectra_inspector_ && spectra_inspector_->is_open())
    {
        // TODO: overlay positioning for compact mode
    }
}

// ── Styling ────────────────────────────────────────────────────────────────────

void SpectraMainWindow::apply_spectra_style()
{
    // Dark theme stylesheet matching Vision.png — suppresses default Qt/KDE styling
    setStyleSheet(R"(
        QMainWindow {
            background-color: #0A0F18;
        }
        QMenuBar {
            background-color: #0A0F18;
            color: #C7D6EB;
            border: none;
            padding: 2px 4px;
            font-family: "Inter";
            font-size: 13px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: #1A2332;
            color: #EDF0F7;
        }
        QMenuBar::item:pressed {
            background-color: #222D3F;
        }
        QMenu {
            background-color: #111827;
            color: #C7D6EB;
            border: 1px solid #2E3D57;
            border-radius: 8px;
            padding: 6px;
            font-family: "Inter";
            font-size: 13px;
        }
        QMenu::item {
            padding: 6px 24px 6px 16px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: #222D3F;
            color: #EDF0F7;
        }
        QMenu::separator {
            height: 1px;
            background: #2E3D57;
            margin: 4px 8px;
        }
        QStatusBar {
            background-color: #0D0E11;
            color: #8A909C;
            border-top: 1px solid #1A1C22;
            font-family: "Inter";
            font-size: 11px;
            min-height: 28px;
        }
        QStatusBar::item { border: none; }
        QDockWidget {
            color: #C8CDD6;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QDockWidget::title {
            background-color: #15171C;
            border: none;
            padding: 6px 12px;
            border-bottom: 1px solid #23262E;
        }
        QDockWidget > QWidget,
        QDockWidget QScrollArea,
        QDockWidget QScrollArea > QWidget > QWidget,
        QDockWidget QTabWidget,
        QDockWidget QTabWidget > QWidget {
            background-color: #15171C;
            color: #C8CDD6;
        }
        QTabWidget::pane {
            border: none;
            background-color: #0D0E11;
        }
        QTabBar::tab {
            background-color: transparent;
            color: #8A909C;
            padding: 6px 16px;
            border: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QTabBar::tab:selected {
            color: #E8ECF1;
            border-bottom: 2px solid #7C5CFC;
        }
        QTabBar::tab:hover:!selected {
            color: #C8CDD6;
        }
        QWidget#welcome_page {
            background-color: #0D0E11;
        }
        QLabel {
            color: #C8CDD6;
            font-family: "Inter";
        }
        QToolBar {
            background-color: #0D0E11;
            border: none;
            spacing: 4px;
            padding: 4px;
        }
        QSplitter::handle {
            background-color: #1A1C22;
        }
        QSplitter::handle:horizontal { width: 1px; }
        QSplitter::handle:vertical { height: 1px; }
        QScrollArea {
            background-color: #15171C;
            border: none;
        }
        QPushButton {
            background-color: #1A1C22;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 6px 14px;
            font-family: "Inter";
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #23262E;
            color: #E8ECF1;
        }
        QPushButton:pressed {
            background-color: #2A2D36;
        }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #15171C;
            color: #E8ECF1;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 5px 8px;
            font-family: "Inter";
            font-size: 13px;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border-color: #7C5CFC;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox QAbstractItemView {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            selection-background-color: #1F2229;
        }
        QCheckBox {
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 13px;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            border: 1px solid #23262E;
            background-color: #15171C;
        }
        QCheckBox::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
        }
        QTreeWidget, QTreeWidget::item {
            background-color: #15171C;
            color: #C8CDD6;
            border: none;
            font-family: "Inter";
            font-size: 12px;
        }
        QTreeWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QHeaderView::section {
            background-color: #15171C;
            color: #8A909C;
            border: none;
            border-bottom: 1px solid #23262E;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 11px;
        }
        QGroupBox {
            color: #8A909C;
            border: 1px solid #23262E;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 8px;
            font-family: "Inter";
            font-size: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #2A2D36;
            border-radius: 4px;
            min-height: 24px;
        }
        QScrollBar::handle:vertical:hover {
            background: #3A3D46;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 8px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #2A2D36;
            border-radius: 4px;
            min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #3A3D46;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }

        /* ── QListWidget ──────────────────────────────────────────── */
        QListWidget {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 4px;
            font-family: "Inter";
            font-size: 12px;
            outline: none;
        }
        QListWidget::item {
            padding: 4px 8px;
            border-radius: 4px;
        }
        QListWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QListWidget::item:hover:!selected {
            background-color: #1A1C22;
        }

        /* ── QTableWidget ─────────────────────────────────────────── */
        QTableWidget {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            gridline-color: #23262E;
            font-family: "Inter";
            font-size: 12px;
            outline: none;
        }
        QTableWidget::item {
            padding: 4px 8px;
        }
        QTableWidget::item:selected {
            background-color: #1F2229;
            color: #E8ECF1;
        }
        QTableCornerButton::section {
            background-color: #15171C;
            border: none;
            border-bottom: 1px solid #23262E;
            border-right: 1px solid #23262E;
        }

        /* ── Disabled states ──────────────────────────────────────── */
        QPushButton:disabled, QLineEdit:disabled, QSpinBox:disabled,
        QDoubleSpinBox:disabled, QComboBox:disabled, QCheckBox:disabled,
        QRadioButton:disabled, QGroupBox:disabled {
            color: #4A4D56;
            background-color: #111316;
            border-color: #1A1C22;
        }
        QCheckBox::indicator:disabled {
            background-color: #111316;
            border-color: #1A1C22;
        }

        /* ── QComboBox down-arrow ─────────────────────────────────── */
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #8A909C;
            width: 0;
            height: 0;
        }
        QComboBox::down-arrow:on {
            border-top: none;
            border-bottom: 5px solid #8A909C;
        }
        QComboBox::down-arrow:disabled {
            border-top-color: #4A4D56;
        }

        /* ── QSpinBox / QDoubleSpinBox buttons ────────────────────── */
        QSpinBox::up-button, QDoubleSpinBox::up-button,
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            background-color: #1A1C22;
            border: none;
            width: 18px;
        }
        QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
        QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
            background-color: #23262E;
        }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
            image: none;
            border-left: 3px solid transparent;
            border-right: 3px solid transparent;
            border-bottom: 4px solid #8A909C;
            width: 0;
            height: 0;
        }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
            image: none;
            border-left: 3px solid transparent;
            border-right: 3px solid transparent;
            border-top: 4px solid #8A909C;
            width: 0;
            height: 0;
        }

        /* ── QCheckBox checkmark ──────────────────────────────────── */
        QCheckBox::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
            image: none;
        }

        /* ── QRadioButton ─────────────────────────────────────────── */
        QRadioButton {
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 13px;
            spacing: 8px;
        }
        QRadioButton::indicator {
            width: 16px;
            height: 16px;
            border-radius: 8px;
            border: 1px solid #23262E;
            background-color: #15171C;
        }
        QRadioButton::indicator:checked {
            background-color: #7C5CFC;
            border-color: #7C5CFC;
        }

        /* ── QMenu checkable indicators ───────────────────────────── */
        QMenu::indicator {
            width: 16px;
            height: 16px;
        }
        QMenu::indicator:checked {
            background-color: #7C5CFC;
            border-radius: 3px;
        }

        /* ── QDockWidget close/float buttons ──────────────────────── */
        QDockWidget::close-button, QDockWidget::float-button {
            background-color: transparent;
            border: none;
            padding: 2px;
        }
        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background-color: #23262E;
            border-radius: 3px;
        }
        QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {
            background-color: #2A2D36;
        }

        /* ── QPlainTextEdit / QTextEdit ───────────────────────────── */
        QPlainTextEdit, QTextEdit {
            background-color: #15171C;
            color: #C8CDD6;
            border: 1px solid #23262E;
            border-radius: 6px;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 12px;
        }
        QPlainTextEdit:focus, QTextEdit:focus {
            border-color: #7C5CFC;
        }

        /* ── QProgressBar ─────────────────────────────────────────── */
        QProgressBar {
            background-color: #15171C;
            border: 1px solid #23262E;
            border-radius: 4px;
            text-align: center;
            color: #C8CDD6;
            font-family: "Inter";
            font-size: 11px;
        }
        QProgressBar::chunk {
            background-color: #7C5CFC;
            border-radius: 3px;
        }

        /* ── QSlider ──────────────────────────────────────────────── */
        QSlider::groove:horizontal {
            background-color: #23262E;
            height: 4px;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background-color: #7C5CFC;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background-color: #8C6CFF;
        }
        QSlider::groove:vertical {
            background-color: #23262E;
            width: 4px;
            border-radius: 2px;
        }
        QSlider::handle:vertical {
            background-color: #7C5CFC;
            width: 14px;
            height: 14px;
            margin: 0 -5px;
            border-radius: 7px;
        }
        QSlider::handle:vertical:hover {
            background-color: #8C6CFF;
        }

        /* ── QToolTip ─────────────────────────────────────────────── */
        QToolTip {
            background-color: #111827;
            color: #C7D6EB;
            border: 1px solid #2E3D57;
            border-radius: 4px;
            padding: 4px 8px;
            font-family: "Inter";
            font-size: 12px;
        }

        /* ── QFrame ───────────────────────────────────────────────── */
        QFrame#spectra_canvas_frame {
            background-color: #0D0E11;
            border: none;
        }
        QWidget#spectra_central_container {
            background-color: #0A0F18;
        }
    )");
}

void SpectraMainWindow::load_fonts()
{
    // Load Inter font from third_party/Inter-Regular.ttf
    QString inter_path = QApplication::applicationDirPath() + "/../third_party/Inter-Regular.ttf";
    int     font_id    = QFontDatabase::addApplicationFont(inter_path);
    if (font_id < 0)
    {
        // Try absolute path relative to source tree
        inter_path = QApplication::applicationDirPath() + "/../../third_party/Inter-Regular.ttf";
        font_id    = QFontDatabase::addApplicationFont(inter_path);
    }
    if (font_id >= 0)
    {
        QStringList families = QFontDatabase::applicationFontFamilies(font_id);
        if (!families.isEmpty())
        {
            QApplication::setFont(QFont(families.first(), 13));
        }
    }
}

void SpectraMainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Update compact mode based on window width
    update_compact_mode();

    // Enforce minimum canvas width: if central widget would be < 640 logical px
    // due to docks, hide non-essential docks to protect canvas space.
    int central_width = central_view_ ? central_view_->width() : 0;
    if (central_width > 0 && central_width < 640)
    {
        // Hide all docks except inspector to give canvas maximum space
        if (topics_panel_ && topics_panel_->isVisible())
            topics_panel_->hide();
        if (settings_panel_ && settings_panel_->isVisible())
            settings_panel_->hide();
        if (timeline_panel_ && timeline_panel_->isVisible())
            timeline_panel_->hide();
        if (export_panel_ && export_panel_->isVisible())
            export_panel_->hide();
        if (shortcut_panel_ && shortcut_panel_->isVisible())
            shortcut_panel_->hide();
        if (transform_panel_ && transform_panel_->isVisible())
            transform_panel_->hide();
        if (data_editor_panel_ && data_editor_panel_->isVisible())
            data_editor_panel_->hide();
        if (accessibility_panel_ && accessibility_panel_->isVisible())
            accessibility_panel_->hide();
        if (plugin_panel_ && plugin_panel_->isVisible())
            plugin_panel_->hide();
        if (plugins_panel_ && plugins_panel_->isVisible())
            plugins_panel_->hide();

        // If still too narrow, hide inspector too
        int new_central = central_view_ ? central_view_->width() : 0;
        if (new_central > 0 && new_central < 640)
        {
            if (spectra_inspector_ && spectra_inspector_->is_open())
                spectra_inspector_->close();
        }
    }
}

}   // namespace spectra::adapters::qt
