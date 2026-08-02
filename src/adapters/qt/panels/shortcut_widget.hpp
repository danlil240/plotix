#pragma once

// QtShortcutWidget — dockable shortcut editor panel for the Qt frontend.
//
// Lists all keyboard shortcut bindings from ShortcutManager, allows viewing
// and rebinding shortcuts.  Uses the same ShortcutManager API as the ImGui
// shortcut editor — no duplicated business logic.

#include <QDockWidget>

#include <spectra/fwd.hpp>

#include <string>

class QTableWidget;
class QPushButton;
class QLabel;
class QKeySequence;

namespace spectra::adapters::qt
{

class QtActionBridge;

class QtShortcutWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtShortcutWidget(ShortcutManager* shortcuts,
                              QtActionBridge*  action_bridge,
                              QWidget*         parent = nullptr);
    ~QtShortcutWidget() override = default;

    QtShortcutWidget(const QtShortcutWidget&)            = delete;
    QtShortcutWidget& operator=(const QtShortcutWidget&) = delete;

   public slots:
    void refresh();

   public:
    bool rebind_command(const std::string&  command_id,
                        const QKeySequence& sequence,
                        std::string*        error = nullptr);
    void reset_to_defaults();

   signals:
    void shortcuts_changed();

   private slots:
    void on_rebind_clicked();
    void on_reset_clicked();

   private:
    ShortcutManager* shortcuts_     = nullptr;
    QtActionBridge*  action_bridge_ = nullptr;

    QTableWidget* table_        = nullptr;
    QPushButton*  rebind_btn_   = nullptr;
    QPushButton*  reset_btn_    = nullptr;
    QLabel*       status_label_ = nullptr;
};

}   // namespace spectra::adapters::qt
