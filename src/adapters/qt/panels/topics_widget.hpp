#pragma once

// QtTopicsWidget — dockable data sources / topics panel for the Qt frontend.
//
// Lists registered data sources from DataSourceRegistry, shows their running
// status, and provides start/stop controls.  This is the Qt equivalent of
// the ImGui Topics panel — it uses the same DataSourceRegistry API.

#include <QDockWidget>

namespace spectra
{
class DataSourceRegistry;
}   // namespace spectra

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;

namespace spectra::adapters::qt
{

class QtTopicsWidget : public QDockWidget
{
    Q_OBJECT

   public:
    QtTopicsWidget(DataSourceRegistry* data_sources, QWidget* parent = nullptr);
    ~QtTopicsWidget() override = default;

    QtTopicsWidget(const QtTopicsWidget&)            = delete;
    QtTopicsWidget& operator=(const QtTopicsWidget&) = delete;

   public slots:
    void refresh();

   private slots:
    void on_start_clicked();
    void on_stop_clicked();
    void on_item_selection_changed();

   private:
    DataSourceRegistry* data_sources_ = nullptr;

    QListWidget* sources_list_ = nullptr;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_  = nullptr;
    QLabel*      status_label_ = nullptr;
};

}   // namespace spectra::adapters::qt
