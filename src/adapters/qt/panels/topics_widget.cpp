// topics_widget.cpp — Qt data sources / topics panel implementation.

#include "topics_widget.hpp"

#include "adapters/data_source_registry.hpp"
#include "adapters/adapter_interface.hpp"

#include <spectra/logger.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtTopicsWidget::QtTopicsWidget(DataSourceRegistry* data_sources, QWidget* parent)
    : QDockWidget("Data Sources", parent), data_sources_(data_sources)
{
    setObjectName("topics_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Sources list
    sources_list_ = new QListWidget(content);
    sources_list_->setObjectName("sources_list");
    layout->addWidget(sources_list_);

    // Status label
    status_label_ = new QLabel(content);
    status_label_->setObjectName("sources_status");
    status_label_->setStyleSheet("color: gray; padding: 4px;");
    layout->addWidget(status_label_);

    // Button row
    auto* button_row = new QHBoxLayout();
    start_button_    = new QPushButton("Start", content);
    start_button_->setObjectName("source_start_btn");
    start_button_->setEnabled(false);
    button_row->addWidget(start_button_);

    stop_button_ = new QPushButton("Stop", content);
    stop_button_->setObjectName("source_stop_btn");
    stop_button_->setEnabled(false);
    button_row->addWidget(stop_button_);

    layout->addLayout(button_row);

    connect(start_button_, &QPushButton::clicked, this, &QtTopicsWidget::on_start_clicked);
    connect(stop_button_, &QPushButton::clicked, this, &QtTopicsWidget::on_stop_clicked);
    connect(sources_list_,
            &QListWidget::itemSelectionChanged,
            this,
            &QtTopicsWidget::on_item_selection_changed);

    refresh();
}

void QtTopicsWidget::refresh()
{
    sources_list_->clear();

    if (!data_sources_)
    {
        status_label_->setText("No data source registry");
        return;
    }

    auto names = data_sources_->source_names();
    for (const auto& name : names)
    {
        auto*   adapter = data_sources_->find(name);
        QString label   = QString::fromStdString(name);
        if (adapter && adapter->is_running())
            label += "  [running]";
        else
            label += "  [stopped]";

        auto* item = new QListWidgetItem(label, sources_list_);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        sources_list_->addItem(item);
    }

    if (names.empty())
        status_label_->setText("No data sources registered");
    else
        status_label_->setText(QString("%1 source(s) registered").arg(names.size()));
}

void QtTopicsWidget::on_start_clicked()
{
    auto* item = sources_list_->currentItem();
    if (!item || !data_sources_)
        return;

    QString name    = item->data(Qt::UserRole).toString();
    auto*   adapter = data_sources_->find(name.toStdString());
    if (adapter && !adapter->is_running())
        adapter->start();

    refresh();
}

void QtTopicsWidget::on_stop_clicked()
{
    auto* item = sources_list_->currentItem();
    if (!item || !data_sources_)
        return;

    QString name    = item->data(Qt::UserRole).toString();
    auto*   adapter = data_sources_->find(name.toStdString());
    if (adapter && adapter->is_running())
        adapter->stop();

    refresh();
}

void QtTopicsWidget::on_item_selection_changed()
{
    auto* item          = sources_list_->currentItem();
    bool  has_selection = (item != nullptr);
    start_button_->setEnabled(has_selection);
    stop_button_->setEnabled(has_selection);
}

}   // namespace spectra::adapters::qt
