#pragma once

// QtPluginsWidget — Qt widget for plugin management (load/unload/enable/disable).
//
// This is the Qt equivalent of the ImGui "Plugins" panel.  It uses the
// shared PluginManager (via ApplicationServices) for all operations.

#include <QDockWidget>

namespace spectra
{
class PluginManager;
class PluginUIRegistry;
}

namespace spectra::adapters::qt
{

class QtPluginsWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtPluginsWidget(PluginManager* mgr, PluginUIRegistry* ui_reg,
                             QWidget* parent = nullptr);
    ~QtPluginsWidget() override;

    void refresh();

   private slots:
    void on_load_plugin();
    void on_scan_dirs();
    void on_scan_default();

   private:
    PluginManager*   mgr_     = nullptr;
    PluginUIRegistry* ui_reg_ = nullptr;
};

}   // namespace spectra::adapters::qt
