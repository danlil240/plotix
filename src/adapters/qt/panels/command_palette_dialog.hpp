#pragma once

// QtCommandPaletteDialog — Qt-native command palette (Ctrl+K).
//
// Uses CommandRegistry::search() for fuzzy matching.  Keyboard navigation:
//   Up/Down — move selection
//   Enter   — execute selected command
//   Escape  — close
//
// The same command IDs are shared across menus, toolbars, automation, and
// this palette — CommandRegistry remains the single source of truth.

#include <QDialog>

#include <string>
#include <vector>

namespace spectra
{
class CommandRegistry;
struct CommandSearchResult;
}   // namespace spectra

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace spectra::adapters::qt
{

class QtCommandPaletteDialog : public QDialog
{
    Q_OBJECT

   public:
    explicit QtCommandPaletteDialog(CommandRegistry& registry, QWidget* parent = nullptr);
    ~QtCommandPaletteDialog() override = default;

    QtCommandPaletteDialog(const QtCommandPaletteDialog&)            = delete;
    QtCommandPaletteDialog& operator=(const QtCommandPaletteDialog&) = delete;

   public slots:
    void open_palette();
    void close_palette();

   protected:
    void keyPressEvent(QKeyEvent* event) override;

   private slots:
    void on_search_changed(const QString& text);
    void on_item_activated(QListWidgetItem* item);
    void on_item_selection_changed();

   private:
    void update_results(const std::string& query);
    void execute_selected();

    CommandRegistry& registry_;

    QLineEdit*   search_edit_    = nullptr;
    QListWidget* results_list_   = nullptr;

    // Cached search results parallel to results_list_ items
    std::vector<std::string> result_command_ids_;
};

}   // namespace spectra::adapters::qt
