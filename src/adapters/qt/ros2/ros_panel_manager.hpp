#pragma once

// RosPanelManager — composes ROS2 DisplayPlugin instances into Qt dockable
// panels for the Qt frontend.
//
// This is the Qt-side composition framework for ROS2/PX4 displays. It bridges
// the framework-neutral DisplayRegistry/DisplayPlugin system with Qt's
// QDockWidget-based panel system:
//
//   - Creates a QDockWidget wrapper for each DisplayPlugin instance.
//   - Routes Qt inspector UI into the plugin's draw_inspector_ui().
//   - Manages plugin lifecycle (enable/disable/destroy) from Qt.
//   - Provides a display list panel for add/remove/reorder.
//   - Serializes panel layout + plugin configs for workspace persistence.
//
// This component is built only when both SPECTRA_USE_QT and SPECTRA_USE_ROS2
// are enabled. It depends on:
//   - spectra_qt_adapter (Qt panel infrastructure)
//   - spectra_ros2_adapter (DisplayPlugin, DisplayRegistry, SceneManager)
//
// Architecture:
//
//   MainWindow
//     └── RosPanelManager (owned by QtApplicationController)
//           ├── DisplaysListPanel (QDockWidget — add/remove/reorder displays)
//           ├── DisplayInspectorPanel (QDockWidget — properties of selected display)
//           └── Per-display QDockWidget wrappers (one per active DisplayPlugin)
//
// The RosPanelManager does NOT own the SceneManager or DisplayContext — those
// are provided by the ROS2 adapter. It only manages the Qt panel composition.

#include <QDockWidget>
#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <vector>

namespace spectra::adapters::ros2
{
class DisplayRegistry;
class DisplayPlugin;
class SceneManager;
struct DisplayContext;
}   // namespace spectra::adapters::ros2

class QMainWindow;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QComboBox;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace spectra::adapters::qt
{

// A single display entry: plugin instance + Qt panel wrapper.
struct RosDisplayEntry
{
    std::string                          id;        // Unique instance id
    std::string                          type_id;   // Display type
    std::unique_ptr<ros2::DisplayPlugin> plugin;
    QDockWidget*                         dock             = nullptr;
    QWidget*                             inspector_widget = nullptr;
    bool                                 enabled          = true;
};

// Manages ROS2 display plugins as Qt dockable panels.
class RosPanelManager : public QObject
{
    Q_OBJECT

   public:
    explicit RosPanelManager(QMainWindow*           main_window,
                             ros2::DisplayRegistry* registry,
                             ros2::SceneManager*    scene_manager,
                             QObject*               parent = nullptr);
    ~RosPanelManager() override;

    RosPanelManager(const RosPanelManager&)            = delete;
    RosPanelManager& operator=(const RosPanelManager&) = delete;

    // ── Display management ──────────────────────────────────────────────────

    // Add a display of the given type. Returns the instance id, or empty on failure.
    std::string add_display(const std::string& type_id);

    // Remove a display by instance id.
    void remove_display(const std::string& id);

    // Enable/disable a display.
    void set_display_enabled(const std::string& id, bool enabled);

    // Get all display entries.
    const std::vector<RosDisplayEntry>& displays() const { return displays_; }

    // Get a display entry by id. Returns nullptr if not found.
    RosDisplayEntry* find_display(const std::string& id);

    // ── Context ─────────────────────────────────────────────────────────────

    // Set the display context (fixed frame, TF buffer, topic discovery, etc.)
    void set_context(const ros2::DisplayContext& context);

    // Update all enabled displays (called from the frame loop).
    void update(float dt_sec);

    // Submit renderables for all enabled displays to the scene manager.
    void submit_renderables();

    // ── Panel layout ────────────────────────────────────────────────────────

    // Show/hide the displays list panel and inspector panel.
    void show_panels(bool visible);

    // Serialize display list + configs for workspace persistence.
    // Returns a JSON string compatible with WorkspaceData::DesktopLayoutState.
    std::string serialize_layout() const;

    // Restore display list + configs from a serialized layout.
    // Returns true if layout was applied (even partially).
    bool deserialize_layout(const std::string& json);

   public slots:
    // Refresh the displays list tree widget.
    void refresh_display_list();

    // Handle selection change in the displays list.
    void on_display_selected(QTreeWidgetItem* current, QTreeWidgetItem* previous);

    // Handle "Add Display" button click.
    void on_add_display();

    // Handle "Remove Display" button click.
    void on_remove_display();

   private:
    // Create the displays list panel (left dock).
    void create_displays_list_panel();

    // Create the inspector panel (right dock).
    void create_inspector_panel();

    // Create a dock widget wrapper for a display plugin.
    QDockWidget* create_display_dock(ros2::DisplayPlugin* plugin, const std::string& id);

    // Generate a unique display instance id.
    std::string generate_id();

    // Update the inspector panel for the currently selected display.
    void update_inspector();

    QMainWindow*           main_window_   = nullptr;
    ros2::DisplayRegistry* registry_      = nullptr;
    ros2::SceneManager*    scene_manager_ = nullptr;
    ros2::DisplayContext*  context_       = nullptr;

    // Panels
    QDockWidget*    displays_list_dock_ = nullptr;
    QDockWidget*    inspector_dock_     = nullptr;
    QTreeWidget*    displays_tree_      = nullptr;
    QComboBox*      display_type_combo_ = nullptr;
    QPushButton*    add_button_         = nullptr;
    QPushButton*    remove_button_      = nullptr;
    QStackedWidget* inspector_stack_    = nullptr;

    // Display entries
    std::vector<RosDisplayEntry> displays_;
    std::string                  selected_id_;

    // ID counter for unique instance ids
    int id_counter_ = 0;
};

}   // namespace spectra::adapters::qt
