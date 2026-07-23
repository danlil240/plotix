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
#include "ui/input/input.hpp"

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

namespace spectra::adapters::qt
{

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
            &SpectraMainWindow::figure_closed);
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
            });
    connect(central_view_,
            &QtSplitViewContainer::figure_detach_requested,
            this,
            &SpectraMainWindow::figure_detach_requested);

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
        SPECTRA_LOG_INFO("qt_main_window", "Added figure tab: id=" + std::to_string(id));
    }
    return idx;
}

void SpectraMainWindow::close_figure_tab(FigureId id)
{
    if (!central_view_)
        return;
    central_view_->close_figure_tab(id);
    if (doc_tab_bar_)
        doc_tab_bar_->remove_tab(static_cast<int>(id));
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
        else if (category == "Tools")
            target_menu = menu_tools_;
        else if (category == "Plot")
            target_menu = menu_figure_;
        else if (category == "Data")
            target_menu = menu_data_;
        else if (category == "Axes")
            target_menu = menu_axes_;
        else if (category == "Transforms")
            target_menu = menu_transforms_;
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
            // QMenus are shown through the custom header rather than a native
            // QMenuBar. Associate each QAction with the window as well so its
            // WindowShortcut remains active while the popup is closed.
            addAction(action);
        }
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
    transform_panel_ = new QtTransformWidget(registry_, this);
    transform_panel_->setObjectName("transform_dock");
    addDockWidget(Qt::RightDockWidgetArea, transform_panel_);
    transform_panel_->hide();

    // Data editor panel (hidden by default)
    data_editor_panel_ = new QtDataEditorWidget(registry_, this);
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

    // Add View menu actions for panel toggles
    if (menu_view_)
    {
        menu_view_->addSeparator();

        inspector_toggle_action_ = menu_view_->addAction("Inspector");
        inspector_toggle_action_->setObjectName("view_toggle_inspector");
        inspector_toggle_action_->setCheckable(true);
        inspector_toggle_action_->setChecked(false);
        connect(inspector_toggle_action_,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_inspector);

        auto* toggle_topics = menu_view_->addAction("Data Sources");
        toggle_topics->setObjectName("view_toggle_topics");
        toggle_topics->setCheckable(true);
        toggle_topics->setChecked(false);
        connect(toggle_topics, &QAction::toggled, this, &SpectraMainWindow::on_toggle_topics);

        auto* toggle_settings = menu_view_->addAction("Settings");
        toggle_settings->setObjectName("view_toggle_settings");
        toggle_settings->setCheckable(true);
        toggle_settings->setChecked(false);
        connect(toggle_settings, &QAction::toggled, this, &SpectraMainWindow::on_toggle_settings);

        auto* toggle_timeline = menu_view_->addAction("Timeline");
        toggle_timeline->setObjectName("view_toggle_timeline");
        toggle_timeline->setCheckable(true);
        toggle_timeline->setChecked(false);
        connect(toggle_timeline, &QAction::toggled, this, &SpectraMainWindow::on_toggle_timeline);

        auto* toggle_export = menu_view_->addAction("Export");
        toggle_export->setObjectName("view_toggle_export");
        toggle_export->setCheckable(true);
        toggle_export->setChecked(false);
        connect(toggle_export, &QAction::toggled, this, &SpectraMainWindow::on_toggle_export);

        auto* toggle_shortcuts = menu_view_->addAction("Shortcuts");
        toggle_shortcuts->setObjectName("view_toggle_shortcuts");
        toggle_shortcuts->setCheckable(true);
        toggle_shortcuts->setChecked(false);
        connect(toggle_shortcuts, &QAction::toggled, this, &SpectraMainWindow::on_toggle_shortcuts);

        auto* toggle_transform = menu_view_->addAction("Transforms");
        toggle_transform->setObjectName("view_toggle_transform");
        toggle_transform->setCheckable(true);
        toggle_transform->setChecked(false);
        connect(toggle_transform, &QAction::toggled, this, &SpectraMainWindow::on_toggle_transform);

        auto* toggle_data_editor = menu_view_->addAction("Data Editor");
        toggle_data_editor->setObjectName("view_toggle_data_editor");
        toggle_data_editor->setCheckable(true);
        toggle_data_editor->setChecked(false);
        connect(toggle_data_editor,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_data_editor);

        auto* toggle_accessibility = menu_view_->addAction("Accessibility");
        toggle_accessibility->setObjectName("view_toggle_accessibility");
        toggle_accessibility->setCheckable(true);
        toggle_accessibility->setChecked(false);
        connect(toggle_accessibility,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_accessibility);

        auto* toggle_plugin_panel = menu_view_->addAction("Plugin Panels");
        toggle_plugin_panel->setObjectName("view_toggle_plugin_panel");
        toggle_plugin_panel->setCheckable(true);
        toggle_plugin_panel->setChecked(false);
        connect(toggle_plugin_panel,
                &QAction::toggled,
                this,
                &SpectraMainWindow::on_toggle_plugin_panel);

        auto* toggle_plugins = menu_view_->addAction("Plugins");
        toggle_plugins->setObjectName("view_toggle_plugins");
        toggle_plugins->setCheckable(true);
        toggle_plugins->setChecked(false);
        connect(toggle_plugins, &QAction::toggled, this, &SpectraMainWindow::on_toggle_plugins);

        // Split view actions
        menu_view_->addSeparator();

        auto* split_right_action = menu_view_->addAction("Split Right");
        split_right_action->setObjectName("view_split_right");
        split_right_action->setShortcut(QKeySequence("Ctrl+\\"));
        connect(split_right_action, &QAction::triggered, this, &SpectraMainWindow::on_split_right);

        auto* split_down_action = menu_view_->addAction("Split Down");
        split_down_action->setObjectName("view_split_down");
        split_down_action->setShortcut(QKeySequence("Ctrl+Shift+\\"));
        connect(split_down_action, &QAction::triggered, this, &SpectraMainWindow::on_split_down);

        auto* close_split_action = menu_view_->addAction("Close Split Pane");
        close_split_action->setObjectName("view_close_split");
        connect(close_split_action, &QAction::triggered, this, &SpectraMainWindow::on_close_split);

        auto* reset_splits_action = menu_view_->addAction("Reset Splits");
        reset_splits_action->setObjectName("view_reset_splits");
        connect(reset_splits_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::on_reset_splits);

        menu_view_->addSeparator();

        auto* reset_layout_action = menu_view_->addAction("Reset Layout");
        reset_layout_action->setObjectName("view_reset_layout");
        connect(reset_layout_action,
                &QAction::triggered,
                this,
                &SpectraMainWindow::on_reset_layout);
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
        timeline_panel_->setHidden(!timeline_panel_->isHidden());
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

    // Create navigation rail
    nav_rail_ = new SpectraNavRail(this);
    nav_rail_->setObjectName("spectra_nav_rail");
    // Hide buttons that have no panel or command wired yet
    nav_rail_->set_button_visible(6, false);   // Markers
    nav_rail_->set_button_visible(10, false);  // Curve Editor
    nav_rail_->set_button_visible(14, false);  // Help (accessible via menu)
    connect(nav_rail_,
            &SpectraNavRail::tool_selected,
            this,
            [this](int tool)
            {
                static const QStringList tool_names = {"Select",
                                                       "Pan",
                                                       "Zoom",
                                                       "Measure",
                                                       "Annotate",
                                                       "ROI",
                                                       "Markers",
                                                       "Transform",
                                                       "Inspector",
                                                       "Timeline",
                                                       "Curve Editor",
                                                       "Plugins",
                                                       "Topics",
                                                       "Settings",
                                                       "Help"};
                if (spectra_status_ && tool >= 0 && tool < tool_names.size())
                    spectra_status_->set_active_tool(tool_names[tool]);

                switch (tool)
                {
                    case 0:
                        central_view_->set_active_tool(ToolMode::Select);
                        break;
                    case 1:
                        central_view_->set_active_tool(ToolMode::Pan);
                        break;
                    case 2:
                        central_view_->set_active_tool(ToolMode::BoxZoom);
                        break;
                    case 3:
                        central_view_->set_active_tool(ToolMode::Measure);
                        break;
                    case 4:
                        central_view_->set_active_tool(ToolMode::Annotate);
                        break;
                    case 5:
                        central_view_->set_active_tool(ToolMode::ROI);
                        break;
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
            });
    if (runtime_)
    {
        runtime_->set_inspector_toggle_callbacks([this]() { spectra_inspector_->toggle(); },
                                                 [this]()
                                                 { return spectra_inspector_->is_open(); });
    }

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
