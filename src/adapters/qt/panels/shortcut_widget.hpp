#pragma once

// QtShortcutWidget — dockable shortcut editor panel for the Qt frontend.
//
// Lists all keyboard shortcut bindings from ShortcutManager, allows viewing
// and rebinding shortcuts.  Uses the same ShortcutManager API as the ImGui
// shortcut editor — no duplicated business logic.

#include <QDockWidget>

#include <spectra/fwd.hpp>

class QTableWidget;
class QPushButton;
class QLabel;

namespace spectra::adapters::qt
{

class QtShortcutWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtShortcutWidget(ShortcutManager* shortcuts, QWidget* parent = nullptr);
    ~QtShortcutWidget() override = default;

    QtShortcutWidget(const QtShortcutWidget&)            = delete;
    QtShortcutWidget& operator=(const QtShortcutWidget&) = delete;

   public slots:
    void refresh();

   private slots:
    void on_rebind_clicked();
    void on_reset_clicked();

   private:
    ShortcutManager* shortcuts_ = nullptr;

    QTableWidget* table_       = nullptr;
    QPushButton*  rebind_btn_  = nullptr;
    QPushButton*  reset_btn_   = nullptr;
    QLabel*       status_label_ = nullptr;
};

}   // namespace spectra::adapters::qt
