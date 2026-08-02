#pragma once

// QtActionBridge — creates and manages QAction objects from CommandRegistry
// entries.
//
// The CommandRegistry remains the source of truth.  QtActionBridge creates
// QActions that mirror command metadata (label, shortcut, enabled state)
// and invoke CommandRegistry::execute() when triggered.
//
// The same command ID remains callable from:
//   - menus and toolbars (via QActions created here)
//   - command palette
//   - plugin API
//   - automation/MCP
//   - keyboard shortcuts (via ShortcutManager)
//   - tests

#include <QAction>
#include <QKeySequence>
#include <QObject>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{
class CommandRegistry;
class ShortcutManager;
struct Command;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtActionBridge : public QObject
{
    Q_OBJECT

   public:
    explicit QtActionBridge(CommandRegistry& registry, QObject* parent = nullptr);
    ~QtActionBridge() override = default;

    QtActionBridge(const QtActionBridge&)            = delete;
    QtActionBridge& operator=(const QtActionBridge&) = delete;
    QtActionBridge(QtActionBridge&&)                 = delete;
    QtActionBridge& operator=(QtActionBridge&&)      = delete;

    // Rebuild all QActions from the current CommandRegistry contents.
    // Call after command registration is complete.
    void rebuild();

    // Get the QAction for a command id. Returns nullptr if not found.
    QAction* action_for(const std::string& command_id) const;

    // Get all actions, grouped by category.
    // Returns a list of (category, actions) pairs.
    struct CategoryActions
    {
        std::string           category;
        std::vector<QAction*> actions;
    };
    std::vector<CategoryActions> actions_by_category() const;

    // Get all categories that have at least one action.
    std::vector<std::string> categories() const;

    // Refresh enabled state and labels from the registry.
    void refresh();

    // Make QAction bindings and command metadata match the authoritative
    // shared ShortcutManager, including commands with multiple bindings.
    void sync_shortcuts(const ShortcutManager& shortcuts);

   private:
    // Parse a Spectra shortcut string (e.g. "Ctrl+R") to QKeySequence.
    static QKeySequence shortcut_to_qt(const std::string& shortcut);

    CommandRegistry& registry_;

    // command_id -> QAction (owned by this bridge)
    std::unordered_map<std::string, std::unique_ptr<QAction>> actions_;
};

}   // namespace spectra::adapters::qt
