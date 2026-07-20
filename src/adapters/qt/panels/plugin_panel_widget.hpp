#pragma once

// QtPluginPanelWidget — Qt widget that renders the portable plugin UI schema.
//
// This widget queries the PluginUIRegistry for registered schemas and
// builds Qt widgets (checkboxes, spin boxes, line edits, combo boxes,
// buttons, labels) from the schema elements.  Property changes and
// action triggers are relayed back to the plugin through the registry.
//
// The widget refreshes when the registry's change listener fires.

#include <QDockWidget>

namespace spectra
{
class PluginUIRegistry;
struct PluginUISchema;
}

namespace spectra::adapters::qt
{

class QtPluginPanelWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtPluginPanelWidget(PluginUIRegistry* registry, QWidget* parent = nullptr);
    ~QtPluginPanelWidget() override;

    // Refresh the panel from the registry (rebuilds all widgets).
    void refresh();

   private:
    void build_schema_widget(const spectra::PluginUISchema& schema);

    PluginUIRegistry* registry_ = nullptr;
    QWidget*          scroll_content_ = nullptr;
};

}   // namespace spectra::adapters::qt
