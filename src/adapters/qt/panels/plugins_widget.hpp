#pragma once

// QtPluginsWidget — Qt widget for plugin management (load/unload/enable/disable).
//
// This is the Qt equivalent of the ImGui "Plugins" panel.  It uses the
// shared PluginManager (via ApplicationServices) for all operations.

#include <QDockWidget>

#include <string>
#include <vector>

namespace spectra
{
class PluginManager;
class PluginUIRegistry;
class DialogService;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtPluginsWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtPluginsWidget(PluginManager*    mgr,
                             PluginUIRegistry* ui_reg,
                             DialogService*    dialogs,
                             QWidget*          parent = nullptr);
    ~QtPluginsWidget() override;

    void refresh();

   private slots:
    void on_load_plugin();
    void on_add_scan_dir();
    void on_scan_dirs();
    void on_scan_default();

   private:
    PluginManager*           mgr_     = nullptr;
    PluginUIRegistry*        ui_reg_  = nullptr;
    DialogService*           dialogs_ = nullptr;
    std::vector<std::string> scan_dirs_;
    std::string              status_;
};

}   // namespace spectra::adapters::qt
