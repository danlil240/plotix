#pragma once

// QtSettingsWidget — dockable settings panel for the Qt frontend.
//
// Provides theme selection, data palette selection, and panel visibility
// toggles.  Uses SettingsStore for persistence and ThemeManager for
// applying changes.  No business logic here — just view/controller wiring.

#include <QDockWidget>

namespace spectra
{
class CommandRegistry;

namespace ui
{
class ThemeManager;
}

namespace ui::settings
{
class SettingsStore;
}
}   // namespace spectra

class QComboBox;
class QCheckBox;
class QPushButton;

namespace spectra::adapters::qt
{

class QtSettingsWidget : public QDockWidget
{
    Q_OBJECT

   public:
    QtSettingsWidget(ui::settings::SettingsStore* store,
                     ui::ThemeManager*           theme_mgr,
                     QWidget*                    parent = nullptr);
    ~QtSettingsWidget() override = default;

    QtSettingsWidget(const QtSettingsWidget&)            = delete;
    QtSettingsWidget& operator=(const QtSettingsWidget&) = delete;

   signals:
    void settings_changed();

   private slots:
    void on_theme_changed(int index);
    void on_palette_changed(int index);
    void on_inspector_toggled(bool checked);
    void on_nav_rail_toggled(bool checked);
    void on_timeline_toggled(bool checked);

   private:
    ui::settings::SettingsStore* store_      = nullptr;
    ui::ThemeManager*            theme_mgr_  = nullptr;

    QComboBox*   theme_combo_      = nullptr;
    QComboBox*   palette_combo_    = nullptr;
    QCheckBox*   inspector_check_  = nullptr;
    QCheckBox*   nav_rail_check_   = nullptr;
    QCheckBox*   timeline_check_   = nullptr;
};

}   // namespace spectra::adapters::qt
