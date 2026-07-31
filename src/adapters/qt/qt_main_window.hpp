#pragma once

// SpectraMainWindow — Production Qt main window for Spectra.
//
// Phase 3 component: native menu bar, toolbar, central document tabs,
// status bar, welcome page, dockable panels (Inspector, Topics, Settings),
// and command palette (Ctrl+K).  Uses QtActionBridge to create QActions
// from the CommandRegistry.
//
// No primary-renderer-window semantics — all main windows are identical.
// Figure documents are displayed in tabs, each containing a FigureCanvasWidget.

#include <QMainWindow>

#include <string>
#include <vector>

#include <spectra/fwd.hpp>

class QLabel;
class QAction;
class QMenu;
class QToolBar;
class QShortcut;

namespace spectra
{
class FigureRegistry;
class InputHandler;
class CommandRegistry;
class ApplicationServices;
enum class ToolMode;
}   // namespace spectra

namespace spectra::adapters::qt
{
class QtRuntime;
}   // namespace spectra::adapters::qt

namespace spectra::adapters::qt
{

class FigureCanvasWidget;
class QtActionBridge;
class QtRuntime;
class QtInspectorWidget;
class QtTopicsWidget;
class QtSettingsWidget;
class QtCommandPaletteDialog;
class QtTimelineWidget;
class QtExportWidget;
class QtShortcutWidget;
class QtTransformWidget;
class QtDataEditorWidget;
class QtAccessibilityWidget;
class QtPluginPanelWidget;
class QtPluginsWidget;
class QtSplitViewContainer;

// New Spectra custom components (forward declarations)
class SpectraTitleBar;
class SpectraAppHeader;
class SpectraNavRail;
class SpectraDocumentTabBar;
class SpectraStatusBar;
class SpectraInspectorDrawer;
class SpectraCanvasFrame;

class SpectraMainWindow : public QMainWindow
{
    Q_OBJECT

   public:
    SpectraMainWindow(QtRuntime*           runtime,
                      FigureRegistry*      registry,
                      QtActionBridge*      action_bridge,
                      ApplicationServices* services = nullptr,
                      QWidget*             parent   = nullptr);
    ~SpectraMainWindow() override;

    SpectraMainWindow(const SpectraMainWindow&)            = delete;
    SpectraMainWindow& operator=(const SpectraMainWindow&) = delete;
    SpectraMainWindow(SpectraMainWindow&&)                 = delete;
    SpectraMainWindow& operator=(SpectraMainWindow&&)      = delete;

    // ── Figure tab management ──────────────────────────────────────────────

    // Add a figure as a new tab.  Returns the tab index.
    int add_figure_tab(FigureId id);

    // Close the tab containing the given figure and notify the application
    // document lifecycle. Returns false when the figure is not open.
    bool close_figure_tab(FigureId id);

    // Remove a tab without closing its figure model. Used only for a
    // validated cross-window transfer.
    bool release_figure_tab(FigureId id);

    // Get the FigureId of the currently active tab.
    FigureId active_figure_id() const;

    // Get the canvas widget for a figure, or nullptr.
    FigureCanvasWidget* canvas_for(FigureId id) const;

    // Number of open figure tabs.
    int figure_tab_count() const;

    // Get all FigureIds currently open as tabs in this window.
    std::vector<FigureId> open_figure_ids() const;

    // Snapshot the visible Qt menu hierarchy for automation.
    std::string automation_menu_state() const;

    // Workspace state accessors (for autosave)
    bool is_inspector_open() const;
    bool is_nav_rail_compact() const;

    // ── Split view ────────────────────────────────────────────────────────

    QtSplitViewContainer* central_view() { return central_view_; }

    bool   split_right();
    bool   split_down();
    bool   close_split();
    void   reset_splits();
    bool   is_split() const;
    size_t pane_count() const;

    // ── Welcome page ───────────────────────────────────────────────────────

    void show_welcome_page();
    void hide_welcome_page();

    // ── Status bar ─────────────────────────────────────────────────────────

    void set_status(const std::string& message);
    void set_active_tool(ToolMode tool);
    void set_nav_rail_visible(bool visible);
    bool is_nav_rail_visible() const;

    // ── Panels ─────────────────────────────────────────────────────────────

    QtInspectorWidget*     inspector_panel() { return inspector_panel_; }
    QtTopicsWidget*        topics_panel() { return topics_panel_; }
    QtSettingsWidget*      settings_panel() { return settings_panel_; }
    QtTimelineWidget*      timeline_panel() { return timeline_panel_; }
    QtExportWidget*        export_panel() { return export_panel_; }
    QtShortcutWidget*      shortcut_panel() { return shortcut_panel_; }
    QtTransformWidget*     transform_panel() { return transform_panel_; }
    QtDataEditorWidget*    data_editor_panel() { return data_editor_panel_; }
    QtAccessibilityWidget* accessibility_panel() { return accessibility_panel_; }
    QtPluginPanelWidget*   plugin_panel() { return plugin_panel_; }
    QtPluginsWidget*       plugins_panel() { return plugins_panel_; }

    // ── Command palette ────────────────────────────────────────────────────

    void open_command_palette();

    // ── Layout reset ───────────────────────────────────────────────────────

    // Reset dock state, panel visibility, and window layout to defaults.
    void reset_layout();

   public slots:
    // Semantic no-argument toggle used by commands and shortcuts.
    void toggle_inspector();

   signals:
    void figure_closed(FigureId id);
    void figure_activated(FigureId id);
    void figure_detach_requested(FigureId id);

   private slots:
    void on_tab_changed(int index);
    void on_tab_close_requested(int index);
    void on_toggle_inspector(bool checked);
    void on_toggle_topics();
    void on_toggle_settings();
    void on_toggle_timeline();
    void on_toggle_export();
    void on_toggle_shortcuts();
    void on_toggle_transform();
    void on_toggle_data_editor();
    void on_toggle_accessibility();
    void on_toggle_plugin_panel();
    void on_toggle_plugins();
    void on_split_right();
    void on_split_down();
    void on_close_split();
    void on_reset_splits();
    void on_reset_layout();
    void on_tab_context_menu(const QPoint& pos);
    void on_detach_tab();

   private:
    void build_menus();
    void build_toolbar();
    void build_status_bar();
    void build_panels();
    void build_command_palette();
    void apply_spectra_style();
    void load_fonts();
    void build_spectra_ui();
    void update_compact_mode();
    void sync_active_tool_ui();
    void set_timeline_visible(bool visible);

   protected:
    void resizeEvent(QResizeEvent* event) override;

    // Helper: create or get a menu by title path (e.g. "File", "View/Zoom")
    QMenu* get_or_create_menu(const std::string& path);

    QtRuntime*           runtime_       = nullptr;
    FigureRegistry*      registry_      = nullptr;
    QtActionBridge*      action_bridge_ = nullptr;
    ApplicationServices* services_      = nullptr;

    QtSplitViewContainer* central_view_ = nullptr;
    QLabel*               status_label_ = nullptr;

    // New Spectra custom UI components
    SpectraTitleBar*        title_bar_         = nullptr;
    SpectraAppHeader*       app_header_        = nullptr;
    SpectraNavRail*         nav_rail_          = nullptr;
    SpectraDocumentTabBar*  doc_tab_bar_       = nullptr;
    SpectraStatusBar*       spectra_status_    = nullptr;
    SpectraInspectorDrawer* spectra_inspector_ = nullptr;
    SpectraCanvasFrame*     canvas_frame_      = nullptr;
    QWidget*                central_container_ = nullptr;

    // Menu bar
    QMenu*    menu_file_               = nullptr;
    QMenu*    menu_edit_               = nullptr;
    QMenu*    menu_view_               = nullptr;
    QMenu*    menu_figure_             = nullptr;
    QMenu*    menu_tools_              = nullptr;
    QMenu*    menu_data_               = nullptr;
    QMenu*    menu_axes_               = nullptr;
    QMenu*    menu_transforms_         = nullptr;
    QMenu*    menu_help_               = nullptr;
    QToolBar* toolbar_                 = nullptr;
    QAction*  inspector_toggle_action_ = nullptr;

    // Submenus
    QMenu* menu_view_panels_ = nullptr;
    QMenu* menu_view_splits_ = nullptr;
    QMenu* menu_tools_theme_ = nullptr;
    QMenu* menu_tools_anim_  = nullptr;

    // Dockable panels
    QtInspectorWidget*     inspector_panel_     = nullptr;
    QtTopicsWidget*        topics_panel_        = nullptr;
    QtSettingsWidget*      settings_panel_      = nullptr;
    QtTimelineWidget*      timeline_panel_      = nullptr;
    QtExportWidget*        export_panel_        = nullptr;
    QtShortcutWidget*      shortcut_panel_      = nullptr;
    QtTransformWidget*     transform_panel_     = nullptr;
    QtDataEditorWidget*    data_editor_panel_   = nullptr;
    QtAccessibilityWidget* accessibility_panel_ = nullptr;
    QtPluginPanelWidget*   plugin_panel_        = nullptr;
    QtPluginsWidget*       plugins_panel_       = nullptr;

    // Command palette
    QtCommandPaletteDialog* command_palette_  = nullptr;
    QShortcut*              palette_shortcut_ = nullptr;
};

}   // namespace spectra::adapters::qt
