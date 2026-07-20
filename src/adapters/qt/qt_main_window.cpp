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

#include "app/application_services.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPoint>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

namespace spectra::adapters::qt
{

SpectraMainWindow::~SpectraMainWindow() = default;

SpectraMainWindow::SpectraMainWindow(QtRuntime*           runtime,
                                      FigureRegistry*      registry,
                                      QtActionBridge*      action_bridge,
                                      ApplicationServices* services,
                                      QWidget*             parent)
    : QMainWindow(parent),
      runtime_(runtime),
      registry_(registry),
      action_bridge_(action_bridge),
      services_(services)
{
    setWindowTitle("Spectra");
    resize(1280, 720);

    // Central split-view container for figure documents
    central_view_ = new QtSplitViewContainer(runtime_, registry_, this);
    central_view_->setObjectName("central_view");

    // Forward signals from the split view container
    connect(central_view_, &QtSplitViewContainer::figure_closed,
            this, &SpectraMainWindow::figure_closed);
    connect(central_view_, &QtSplitViewContainer::figure_activated,
            this, [this](FigureId id) {
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
            });
    connect(central_view_, &QtSplitViewContainer::figure_detach_requested,
            this, &SpectraMainWindow::figure_detach_requested);

    setCentralWidget(central_view_);

    build_menus();
    build_toolbar();
    build_status_bar();
    build_panels();
    build_command_palette();

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
        SPECTRA_LOG_INFO("qt_main_window", "Added figure tab: id=" + std::to_string(id));
    }
    return idx;
}

void SpectraMainWindow::close_figure_tab(FigureId id)
{
    if (!central_view_)
        return;
    central_view_->close_figure_tab(id);
    emit figure_closed(id);
    if (central_view_->figure_tab_count() == 0)
        show_welcome_page();
    SPECTRA_LOG_INFO("qt_main_window", "Closed figure tab: id=" + std::to_string(id));
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

    // Standard menus
    menu_file_   = menuBar()->addMenu("&File");
    menu_edit_   = menuBar()->addMenu("&Edit");
    menu_view_   = menuBar()->addMenu("&View");
    menu_figure_ = menuBar()->addMenu("&Figure");
    menu_help_   = menuBar()->addMenu("&Help");

    // Populate menus from CommandRegistry categories
    auto categorized = action_bridge_->actions_by_category();
    for (const auto& [category, actions] : categorized)
    {
        QMenu* target_menu = nullptr;
        if (category == "File")
            target_menu = menu_file_;
        else if (category == "Edit")
            target_menu = menu_edit_;
        else if (category == "View")
            target_menu = menu_view_;
        else if (category == "Figure")
            target_menu = menu_figure_;
        else if (category == "Help" || category == "About")
            target_menu = menu_help_;
        else
        {
            // Create a custom menu for unknown categories
            target_menu = get_or_create_menu(category);
        }

        if (!target_menu)
            continue;

        for (auto* action : actions)
        {
            target_menu->addAction(action);
        }
    }
}

QMenu* SpectraMainWindow::get_or_create_menu(const std::string& path)
{
    // Simple: create top-level menu with the path name
    QString name = QString::fromStdString(path);
    // Check if it already exists
    for (auto* action : menuBar()->actions())
    {
        if (action->text() == name && action->menu())
            return action->menu();
    }
    return menuBar()->addMenu(name);
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
    status_label_ = new QLabel(this);
    status_label_->setObjectName("status_label");
    statusBar()->addWidget(status_label_);
    set_status("Ready");
}

// ── Private: build panels ────────────────────────────────────────────────────

void SpectraMainWindow::build_panels()
{
    if (!services_)
        return;

    // Inspector panel
    inspector_panel_ = new QtInspectorWidget(registry_, this);
    inspector_panel_->setObjectName("inspector_dock");
    addDockWidget(Qt::RightDockWidgetArea, inspector_panel_);

    // Topics / data sources panel
    topics_panel_ = new QtTopicsWidget(&services_->data_sources(), this);
    topics_panel_->setObjectName("topics_dock");
    addDockWidget(Qt::RightDockWidgetArea, topics_panel_);

    // Settings panel
    settings_panel_ = new QtSettingsWidget(&services_->settings(), &services_->theme(), this);
    settings_panel_->setObjectName("settings_dock");
    addDockWidget(Qt::LeftDockWidgetArea, settings_panel_);

    // Timeline panel
    timeline_panel_ = new QtTimelineWidget(nullptr, this);
    timeline_panel_->setObjectName("timeline_dock");
    addDockWidget(Qt::BottomDockWidgetArea, timeline_panel_);

    // Export panel
    export_panel_ = new QtExportWidget(&services_->export_formats(), registry_,
                                       services_->dialog_service(), this);
    export_panel_->setObjectName("export_dock");
    addDockWidget(Qt::RightDockWidgetArea, export_panel_);

    // Shortcut editor panel
    shortcut_panel_ = new QtShortcutWidget(&services_->shortcuts(), this);
    shortcut_panel_->setObjectName("shortcut_dock");
    addDockWidget(Qt::LeftDockWidgetArea, shortcut_panel_);

    // Transform panel
    transform_panel_ = new QtTransformWidget(registry_, this);
    transform_panel_->setObjectName("transform_dock");
    addDockWidget(Qt::RightDockWidgetArea, transform_panel_);

    // Data editor panel
    data_editor_panel_ = new QtDataEditorWidget(registry_, this);
    data_editor_panel_->setObjectName("data_editor_dock");
    addDockWidget(Qt::RightDockWidgetArea, data_editor_panel_);

    // Accessibility panel
    accessibility_panel_ = new QtAccessibilityWidget(registry_, this);
    accessibility_panel_->setObjectName("accessibility_dock");
    addDockWidget(Qt::LeftDockWidgetArea, accessibility_panel_);

    // Plugin UI panel (renders portable plugin UI schemas)
    plugin_panel_ = new QtPluginPanelWidget(&services_->plugin_ui(), this);
    plugin_panel_->setObjectName("plugin_panel_dock");
    addDockWidget(Qt::RightDockWidgetArea, plugin_panel_);

    // Plugins management panel (load/unload/enable/disable)
    plugins_panel_ = new QtPluginsWidget(&services_->plugins(), &services_->plugin_ui(), this);
    plugins_panel_->setObjectName("plugins_mgmt_dock");
    addDockWidget(Qt::LeftDockWidgetArea, plugins_panel_);

    // Add View menu actions for panel toggles
    if (menu_view_)
    {
        menu_view_->addSeparator();

        auto* toggle_inspector = menu_view_->addAction("Inspector");
        toggle_inspector->setObjectName("view_toggle_inspector");
        toggle_inspector->setCheckable(true);
        toggle_inspector->setChecked(true);
        connect(toggle_inspector, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_inspector);

        auto* toggle_topics = menu_view_->addAction("Data Sources");
        toggle_topics->setObjectName("view_toggle_topics");
        toggle_topics->setCheckable(true);
        toggle_topics->setChecked(true);
        connect(toggle_topics, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_topics);

        auto* toggle_settings = menu_view_->addAction("Settings");
        toggle_settings->setObjectName("view_toggle_settings");
        toggle_settings->setCheckable(true);
        toggle_settings->setChecked(false);
        connect(toggle_settings, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_settings);

        auto* toggle_timeline = menu_view_->addAction("Timeline");
        toggle_timeline->setObjectName("view_toggle_timeline");
        toggle_timeline->setCheckable(true);
        toggle_timeline->setChecked(true);
        connect(toggle_timeline, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_timeline);

        auto* toggle_export = menu_view_->addAction("Export");
        toggle_export->setObjectName("view_toggle_export");
        toggle_export->setCheckable(true);
        toggle_export->setChecked(false);
        connect(toggle_export, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_export);

        auto* toggle_shortcuts = menu_view_->addAction("Shortcuts");
        toggle_shortcuts->setObjectName("view_toggle_shortcuts");
        toggle_shortcuts->setCheckable(true);
        toggle_shortcuts->setChecked(false);
        connect(toggle_shortcuts, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_shortcuts);

        auto* toggle_transform = menu_view_->addAction("Transforms");
        toggle_transform->setObjectName("view_toggle_transform");
        toggle_transform->setCheckable(true);
        toggle_transform->setChecked(false);
        connect(toggle_transform, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_transform);

        auto* toggle_data_editor = menu_view_->addAction("Data Editor");
        toggle_data_editor->setObjectName("view_toggle_data_editor");
        toggle_data_editor->setCheckable(true);
        toggle_data_editor->setChecked(false);
        connect(toggle_data_editor, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_data_editor);

        auto* toggle_accessibility = menu_view_->addAction("Accessibility");
        toggle_accessibility->setObjectName("view_toggle_accessibility");
        toggle_accessibility->setCheckable(true);
        toggle_accessibility->setChecked(false);
        connect(toggle_accessibility, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_accessibility);

        auto* toggle_plugin_panel = menu_view_->addAction("Plugin Panels");
        toggle_plugin_panel->setObjectName("view_toggle_plugin_panel");
        toggle_plugin_panel->setCheckable(true);
        toggle_plugin_panel->setChecked(false);
        connect(toggle_plugin_panel, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_plugin_panel);

        auto* toggle_plugins = menu_view_->addAction("Plugins");
        toggle_plugins->setObjectName("view_toggle_plugins");
        toggle_plugins->setCheckable(true);
        toggle_plugins->setChecked(false);
        connect(toggle_plugins, &QAction::toggled,
                this, &SpectraMainWindow::on_toggle_plugins);

        // Split view actions
        menu_view_->addSeparator();

        auto* split_right_action = menu_view_->addAction("Split Right");
        split_right_action->setObjectName("view_split_right");
        split_right_action->setShortcut(QKeySequence("Ctrl+\\"));
        connect(split_right_action, &QAction::triggered,
                this, &SpectraMainWindow::on_split_right);

        auto* split_down_action = menu_view_->addAction("Split Down");
        split_down_action->setObjectName("view_split_down");
        split_down_action->setShortcut(QKeySequence("Ctrl+Shift+\\"));
        connect(split_down_action, &QAction::triggered,
                this, &SpectraMainWindow::on_split_down);

        auto* close_split_action = menu_view_->addAction("Close Split Pane");
        close_split_action->setObjectName("view_close_split");
        connect(close_split_action, &QAction::triggered,
                this, &SpectraMainWindow::on_close_split);

        auto* reset_splits_action = menu_view_->addAction("Reset Splits");
        reset_splits_action->setObjectName("view_reset_splits");
        connect(reset_splits_action, &QAction::triggered,
                this, &SpectraMainWindow::on_reset_splits);
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
    connect(palette_shortcut_, &QShortcut::activated,
            this, &SpectraMainWindow::open_command_palette);

    // Add command palette action to the File menu
    if (menu_file_)
    {
        menu_file_->addSeparator();
        auto* palette_action = menu_file_->addAction("Command Palette...");
        palette_action->setObjectName("file_command_palette");
        palette_action->setShortcut(QKeySequence("Ctrl+K"));
        connect(palette_action, &QAction::triggered,
                this, &SpectraMainWindow::open_command_palette);
    }
}

void SpectraMainWindow::open_command_palette()
{
    if (command_palette_)
        command_palette_->open_palette();
}

// ── Panel toggle slots ──────────────────────────────────────────────────────

void SpectraMainWindow::on_toggle_inspector()
{
    if (inspector_panel_)
        inspector_panel_->setVisible(!inspector_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_topics()
{
    if (topics_panel_)
        topics_panel_->setVisible(!topics_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_settings()
{
    if (settings_panel_)
        settings_panel_->setVisible(!settings_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_timeline()
{
    if (timeline_panel_)
        timeline_panel_->setVisible(!timeline_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_export()
{
    if (export_panel_)
        export_panel_->setVisible(!export_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_shortcuts()
{
    if (shortcut_panel_)
        shortcut_panel_->setVisible(!shortcut_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_transform()
{
    if (transform_panel_)
        transform_panel_->setVisible(!transform_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_data_editor()
{
    if (data_editor_panel_)
        data_editor_panel_->setVisible(!data_editor_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_accessibility()
{
    if (accessibility_panel_)
        accessibility_panel_->setVisible(!accessibility_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_plugin_panel()
{
    if (plugin_panel_)
        plugin_panel_->setVisible(!plugin_panel_->isVisible());
}

void SpectraMainWindow::on_toggle_plugins()
{
    if (plugins_panel_)
        plugins_panel_->setVisible(!plugins_panel_->isVisible());
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

}   // namespace spectra::adapters::qt
