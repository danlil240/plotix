// data_editor_widget.cpp — Qt data editor panel implementation.

#include "data_editor_widget.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace spectra::adapters::qt
{

QtDataEditorWidget::QtDataEditorWidget(FigureRegistry* registry, QWidget* parent)
    : QDockWidget("Data Editor", parent), registry_(registry)
{
    setObjectName("data_editor_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    build_ui();
}

void QtDataEditorWidget::build_ui()
{
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ── Selectors ───────────────────────────────────────────────────────
    auto* sel_group = new QGroupBox("Selection", container);
    auto* sel_form = new QFormLayout(sel_group);

    axes_combo_ = new QComboBox(sel_group);
    axes_combo_->setObjectName("de_axes_combo");
    sel_form->addRow("Axes", axes_combo_);

    series_combo_ = new QComboBox(sel_group);
    series_combo_->setObjectName("de_series_combo");
    sel_form->addRow("Series", series_combo_);

    layout->addWidget(sel_group);

    // ── Data table ──────────────────────────────────────────────────────
    auto* table_group = new QGroupBox("Data Points", container);
    auto* table_layout = new QVBoxLayout(table_group);

    table_ = new QTableWidget(table_group);
    table_->setObjectName("de_data_table");
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"X", "Y"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMaximumHeight(300);
    table_layout->addWidget(table_);

    info_label_ = new QLabel("No data", table_group);
    info_label_->setStyleSheet("color: gray; font-size: 11px;");
    table_layout->addWidget(info_label_);

    layout->addWidget(table_group);
    layout->addStretch();

    setWidget(container);

    // ── Connections ─────────────────────────────────────────────────────
    connect(axes_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtDataEditorWidget::on_axes_selected);
    connect(series_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtDataEditorWidget::on_series_selected);
    connect(table_, &QTableWidget::cellChanged,
            this, &QtDataEditorWidget::on_cell_changed);
}

void QtDataEditorWidget::set_active_figure(FigureId id)
{
    if (active_id_ == id)
        return;
    active_id_ = id;
    refresh();
}

void QtDataEditorWidget::refresh()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
    {
        axes_combo_->clear();
        series_combo_->clear();
        table_->setRowCount(0);
        info_label_->setText("No figure selected");
        return;
    }

    Figure* figure = registry_->get(active_id_);
    if (!figure)
    {
        info_label_->setText("Figure not found");
        return;
    }

    populate_axes_combo(*figure);
}

void QtDataEditorWidget::populate_axes_combo(Figure& figure)
{
    axes_combo_->blockSignals(true);
    axes_combo_->clear();

    int idx = 0;
    figure.for_each_axes([&](AxesBase* ab) {
        auto* ax = dynamic_cast<Axes*>(ab);
        QString label = ax ? QString::fromStdString(ax->title()) : QString("Axes %1").arg(idx + 1);
        if (label.isEmpty())
            label = QString("Axes %1").arg(idx + 1);
        axes_combo_->addItem(label, idx);
        ++idx;
    });

    axes_combo_->blockSignals(false);
    if (axes_combo_->count() > 0)
        on_axes_selected(0);
    else
    {
        series_combo_->clear();
        table_->setRowCount(0);
        info_label_->setText("No axes in figure");
    }
}

void QtDataEditorWidget::populate_series_combo(AxesBase& axes)
{
    series_combo_->blockSignals(true);
    series_combo_->clear();

    const auto& series_list = axes.series();
    for (size_t i = 0; i < series_list.size(); ++i)
    {
        auto* s = series_list[i].get();
        if (!s)
            continue;
        QString label = QString::fromStdString(s->label());
        if (label.isEmpty())
            label = QString("Series %1").arg(i + 1);
        series_combo_->addItem(label, static_cast<int>(i));
    }

    series_combo_->blockSignals(false);
    if (series_combo_->count() > 0)
        on_series_selected(0);
    else
    {
        table_->setRowCount(0);
        info_label_->setText("No series in axes");
    }
}

void QtDataEditorWidget::populate_data_table(Series& series)
{
    suppress_cell_signal_ = true;

    auto* ls = dynamic_cast<LineSeries*>(&series);
    auto* sc = dynamic_cast<ScatterSeries*>(&series);

    std::span<const float> x_data;
    std::span<const float> y_data;

    if (ls)
    {
        x_data = ls->x_data();
        y_data = ls->y_data();
    }
    else if (sc)
    {
        x_data = sc->x_data();
        y_data = sc->y_data();
    }
    else
    {
        table_->setRowCount(0);
        info_label_->setText("Series type not editable");
        suppress_cell_signal_ = false;
        return;
    }

    size_t n = std::min(x_data.size(), y_data.size());
    table_->setRowCount(static_cast<int>(n));

    for (size_t i = 0; i < n; ++i)
    {
        auto* x_item = new QTableWidgetItem(QString::number(x_data[i], 'g', 6));
        auto* y_item = new QTableWidgetItem(QString::number(y_data[i], 'g', 6));
        table_->setItem(static_cast<int>(i), 0, x_item);
        table_->setItem(static_cast<int>(i), 1, y_item);
    }

    info_label_->setText(QString("%1 data points").arg(n));
    suppress_cell_signal_ = false;
}

void QtDataEditorWidget::on_axes_selected(int index)
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID || index < 0)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    int idx = 0;
    AxesBase* target = nullptr;
    figure->for_each_axes([&](AxesBase* ab) {
        if (idx == index)
            target = ab;
        ++idx;
    });

    if (target)
        populate_series_combo(*target);
}

void QtDataEditorWidget::on_series_selected(int index)
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID || index < 0)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    int axes_idx = axes_combo_->currentIndex();
    if (axes_idx < 0)
        return;

    int a_idx = 0;
    AxesBase* target_axes = nullptr;
    figure->for_each_axes([&](AxesBase* ab) {
        if (a_idx == axes_idx)
            target_axes = ab;
        ++a_idx;
    });

    if (!target_axes)
        return;

    const auto& series_list = target_axes->series();
    if (static_cast<size_t>(index) >= series_list.size())
        return;

    auto* s = series_list[index].get();
    if (s)
        populate_data_table(*s);
}

void QtDataEditorWidget::on_cell_changed(int row, int col)
{
    if (suppress_cell_signal_)
        return;

    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    int axes_idx = axes_combo_->currentIndex();
    int series_idx = series_combo_->currentIndex();
    if (axes_idx < 0 || series_idx < 0)
        return;

    int a_idx = 0;
    AxesBase* target_axes = nullptr;
    figure->for_each_axes([&](AxesBase* ab) {
        if (a_idx == axes_idx)
            target_axes = ab;
        ++a_idx;
    });

    if (!target_axes)
        return;

    const auto& series_list = target_axes->series();
    if (static_cast<size_t>(series_idx) >= series_list.size())
        return;

    auto* s = series_list[series_idx].get();
    if (!s)
        return;

    auto* ls = dynamic_cast<LineSeries*>(s);
    auto* sc = dynamic_cast<ScatterSeries*>(s);
    if (!ls && !sc)
        return;

    auto* item = table_->item(row, col);
    if (!item)
        return;

    bool ok = false;
    float val = item->text().toFloat(&ok);
    if (!ok)
        return;

    // Update the data in-place
    if (ls)
    {
        auto x = ls->x_data();
        auto y = ls->y_data();
        if (col == 0 && static_cast<size_t>(row) < x.size())
        {
            std::vector<float> xv(x.begin(), x.end());
            xv[static_cast<size_t>(row)] = val;
            ls->set_x(xv);
        }
        else if (col == 1 && static_cast<size_t>(row) < y.size())
        {
            std::vector<float> yv(y.begin(), y.end());
            yv[static_cast<size_t>(row)] = val;
            ls->set_y(yv);
        }
    }
    else if (sc)
    {
        auto x = sc->x_data();
        auto y = sc->y_data();
        if (col == 0 && static_cast<size_t>(row) < x.size())
        {
            std::vector<float> xv(x.begin(), x.end());
            xv[static_cast<size_t>(row)] = val;
            sc->set_x(xv);
        }
        else if (col == 1 && static_cast<size_t>(row) < y.size())
        {
            std::vector<float> yv(y.begin(), y.end());
            yv[static_cast<size_t>(row)] = val;
            sc->set_y(yv);
        }
    }
}

}   // namespace spectra::adapters::qt
