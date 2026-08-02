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

#include <string>
#include <vector>

class QFormLayout;

namespace spectra
{
class PluginUIRegistry;
struct PluginUISchema;
struct PluginUIElement;
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
    void build_schema_widget(const std::string& schema_id, const spectra::PluginUISchema& schema);
    void build_element_widget(const std::string&             schema_id,
                              const spectra::PluginUISchema& schema,
                              size_t                         element_index,
                              QFormLayout*                   form,
                              QWidget*                       parent,
                              std::vector<size_t>&           ancestry);

    PluginUIRegistry* registry_ = nullptr;
    QWidget*          scroll_content_ = nullptr;
};

}   // namespace spectra::adapters::qt
