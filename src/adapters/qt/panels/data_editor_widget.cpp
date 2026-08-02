// data_editor_widget.cpp — Qt data editor panel implementation.

#include "data_editor_widget.hpp"

#include "app/frontend_services.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/data/csv_loader.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <QComboBox>
#include <QAction>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <algorithm>
#include <set>
#include <fstream>
#include <iomanip>

namespace spectra::adapters::qt
{

QtDataEditorWidget::QtDataEditorWidget(FigureRegistry*              registry,
                                       ::spectra::UndoManager*      undo_manager,
                                       ::spectra::RedrawRequest*    redraw,
                                       ::spectra::ClipboardService* clipboard,
                                       ::spectra::DialogService*    dialog,
                                       QWidget*                     parent)
    : QDockWidget("Data Editor", parent), registry_(registry), undo_manager_(undo_manager),
      redraw_(redraw), clipboard_(clipboard), dialog_(dialog)
{
    setObjectName("data_editor_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    build_ui();
}

void QtDataEditorWidget::build_ui()
{
    auto* container = new QWidget(this);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ── Selectors ───────────────────────────────────────────────────────
    selection_group_ = new QGroupBox("Selection", container);
    auto* sel_form   = new QFormLayout(selection_group_);

    axes_combo_ = new QComboBox(selection_group_);
    axes_combo_->setObjectName("de_axes_combo");
    sel_form->addRow("Axes", axes_combo_);

    series_combo_ = new QComboBox(selection_group_);
    series_combo_->setObjectName("de_series_combo");
    sel_form->addRow("Series", series_combo_);

    layout->addWidget(selection_group_);

    empty_state_ = new QWidget(container);
    empty_state_->setObjectName("de_empty_state");
    auto* empty_layout = new QVBoxLayout(empty_state_);
    empty_layout->setContentsMargins(12, 20, 12, 20);
    empty_state_label_ = new QLabel("No editable series", empty_state_);
    empty_state_label_->setObjectName("de_empty_state_label");
    empty_state_label_->setAlignment(Qt::AlignCenter);
    empty_state_label_->setWordWrap(true);
    empty_layout->addWidget(empty_state_label_);
    empty_import_button_ = new QPushButton("Import CSV...", empty_state_);
    empty_import_button_->setObjectName("de_empty_import_csv");
    empty_layout->addWidget(empty_import_button_, 0, Qt::AlignHCenter);
    empty_state_->hide();
    layout->addWidget(empty_state_);

    // ── Data table ──────────────────────────────────────────────────────
    table_group_ = new QGroupBox("Data Points", container);
    table_group_->setObjectName("de_table_group");
    auto* table_layout = new QVBoxLayout(table_group_);

    table_ = new QTableWidget(table_group_);
    table_->setObjectName("de_data_table");
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"X", "Y"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setMaximumHeight(300);
    table_layout->addWidget(table_);

    auto* page_actions    = new QHBoxLayout();
    previous_page_button_ = new QPushButton("Previous", table_group_);
    previous_page_button_->setObjectName("de_previous_page");
    next_page_button_ = new QPushButton("Next", table_group_);
    next_page_button_->setObjectName("de_next_page");
    page_label_ = new QLabel("Rows 0-0 of 0", table_group_);
    page_label_->setObjectName("de_page_label");
    page_actions->addWidget(previous_page_button_);
    page_actions->addWidget(next_page_button_);
    page_actions->addWidget(page_label_);
    page_actions->addStretch();
    table_layout->addLayout(page_actions);

    auto* row_actions = new QHBoxLayout();
    auto* add_row     = new QPushButton("Add Row", table_group_);
    add_row->setObjectName("de_add_row");
    row_actions->addWidget(add_row);
    auto* delete_rows = new QPushButton("Delete", table_group_);
    delete_rows->setObjectName("de_delete_rows");
    row_actions->addWidget(delete_rows);
    auto* move_up = new QPushButton("Move Up", table_group_);
    move_up->setObjectName("de_move_up");
    row_actions->addWidget(move_up);
    auto* move_down = new QPushButton("Move Down", table_group_);
    move_down->setObjectName("de_move_down");
    row_actions->addWidget(move_down);
    auto* paste = new QPushButton("Paste", table_group_);
    paste->setObjectName("de_paste");
    row_actions->addWidget(paste);
    table_layout->addLayout(row_actions);

    auto* file_actions = new QHBoxLayout();
    auto* import_csv   = new QPushButton("Import CSV...", table_group_);
    import_csv->setObjectName("de_import_csv");
    file_actions->addWidget(import_csv);
    auto* export_csv = new QPushButton("Export CSV...", table_group_);
    export_csv->setObjectName("de_export_csv");
    file_actions->addWidget(export_csv);
    file_actions->addStretch();
    table_layout->addLayout(file_actions);

    import_mapping_group_ = new QGroupBox("CSV Column Mapping", table_group_);
    import_mapping_group_->setObjectName("de_import_mapping");
    auto* import_mapping_layout = new QVBoxLayout(import_mapping_group_);
    auto* import_mapping_form   = new QFormLayout();
    import_x_column_            = new QComboBox(import_mapping_group_);
    import_x_column_->setObjectName("de_import_x_column");
    import_mapping_form->addRow("Shared X", import_x_column_);
    import_z_column_ = new QComboBox(import_mapping_group_);
    import_z_column_->setObjectName("de_import_z_column");
    import_z_label_ = new QLabel("Shared Z", import_mapping_group_);
    import_mapping_form->addRow(import_z_label_, import_z_column_);
    import_z_label_->hide();
    import_z_column_->hide();
    import_mapping_layout->addLayout(import_mapping_form);
    import_y_columns_ = new QListWidget(import_mapping_group_);
    import_y_columns_->setObjectName("de_import_y_columns");
    import_y_columns_->setMaximumHeight(120);
    import_mapping_layout->addWidget(new QLabel("Y series", import_mapping_group_));
    import_mapping_layout->addWidget(import_y_columns_);
    auto* import_selection_actions = new QHBoxLayout();
    auto* select_all_y             = new QPushButton("Select All", import_mapping_group_);
    select_all_y->setObjectName("de_import_select_all_y");
    auto* clear_y = new QPushButton("Clear", import_mapping_group_);
    clear_y->setObjectName("de_import_clear_y");
    import_selection_actions->addWidget(select_all_y);
    import_selection_actions->addWidget(clear_y);
    import_selection_actions->addStretch();
    import_mapping_layout->addLayout(import_selection_actions);
    auto* import_mapping_actions = new QHBoxLayout();
    apply_import_columns_        = new QPushButton("Import Selected Series", import_mapping_group_);
    apply_import_columns_->setObjectName("de_apply_import_columns");
    auto* cancel_import = new QPushButton("Cancel", import_mapping_group_);
    cancel_import->setObjectName("de_cancel_import_columns");
    import_mapping_actions->addWidget(apply_import_columns_);
    import_mapping_actions->addWidget(cancel_import);
    import_mapping_layout->addLayout(import_mapping_actions);
    import_mapping_group_->hide();
    table_layout->addWidget(import_mapping_group_);

    info_label_ = new QLabel("No data", table_group_);
    info_label_->setStyleSheet("color: gray; font-size: 11px;");
    table_layout->addWidget(info_label_);

    layout->addWidget(table_group_);
    layout->addStretch();

    setWidget(container);

    // ── Connections ─────────────────────────────────────────────────────
    connect(axes_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtDataEditorWidget::on_axes_selected);
    connect(series_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtDataEditorWidget::on_series_selected);
    connect(table_, &QTableWidget::cellChanged, this, &QtDataEditorWidget::on_cell_changed);
    connect(add_row, &QPushButton::clicked, this, &QtDataEditorWidget::on_add_row);
    connect(delete_rows, &QPushButton::clicked, this, &QtDataEditorWidget::on_delete_rows);
    connect(move_up, &QPushButton::clicked, this, &QtDataEditorWidget::on_move_rows_up);
    connect(move_down, &QPushButton::clicked, this, &QtDataEditorWidget::on_move_rows_down);
    connect(paste, &QPushButton::clicked, this, &QtDataEditorWidget::on_paste_cells);
    connect(previous_page_button_,
            &QPushButton::clicked,
            this,
            &QtDataEditorWidget::on_previous_page);
    connect(next_page_button_, &QPushButton::clicked, this, &QtDataEditorWidget::on_next_page);
    connect(import_csv, &QPushButton::clicked, this, &QtDataEditorWidget::on_import_csv);
    connect(empty_import_button_, &QPushButton::clicked, this, &QtDataEditorWidget::on_import_csv);
    connect(import_x_column_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtDataEditorWidget::on_import_x_column_changed);
    connect(import_z_column_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtDataEditorWidget::on_import_x_column_changed);
    connect(import_y_columns_,
            &QListWidget::itemChanged,
            this,
            &QtDataEditorWidget::on_import_column_changed);
    connect(select_all_y,
            &QPushButton::clicked,
            this,
            [this]()
            {
                for (int row = 0; row < import_y_columns_->count(); ++row)
                {
                    auto* item = import_y_columns_->item(row);
                    if (item && (item->flags() & Qt::ItemIsEnabled))
                        item->setCheckState(Qt::Checked);
                }
            });
    connect(clear_y,
            &QPushButton::clicked,
            this,
            [this]()
            {
                for (int row = 0; row < import_y_columns_->count(); ++row)
                    if (auto* item = import_y_columns_->item(row))
                        item->setCheckState(Qt::Unchecked);
            });
    connect(apply_import_columns_,
            &QPushButton::clicked,
            this,
            &QtDataEditorWidget::on_apply_import_columns);
    connect(cancel_import,
            &QPushButton::clicked,
            this,
            &QtDataEditorWidget::on_cancel_import_columns);
    connect(export_csv, &QPushButton::clicked, this, &QtDataEditorWidget::on_export_csv);

    auto* paste_action = new QAction(table_);
    paste_action->setShortcut(QKeySequence::Paste);
    paste_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    table_->addAction(paste_action);
    connect(paste_action, &QAction::triggered, this, &QtDataEditorWidget::on_paste_cells);
    auto* delete_action = new QAction(table_);
    delete_action->setShortcut(QKeySequence::Delete);
    delete_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    table_->addAction(delete_action);
    connect(delete_action, &QAction::triggered, this, &QtDataEditorWidget::on_delete_rows);
}

void QtDataEditorWidget::set_active_figure(FigureId id)
{
    if (active_id_ == id)
        return;
    if (import_mapping_group_)
        import_mapping_group_->hide();
    import_headers_.clear();
    import_columns_.clear();
    import_column_offsets_.clear();
    import_rows_  = 0;
    paged_series_ = nullptr;
    page_start_   = 0;
    active_id_    = id;
    refresh();
}

void QtDataEditorWidget::show_empty_state(const QString& message, bool can_import)
{
    if (selection_group_)
        selection_group_->setVisible(active_id_ != INVALID_FIGURE_ID);
    if (table_group_)
        table_group_->hide();
    if (empty_state_label_)
        empty_state_label_->setText(message);
    if (empty_import_button_)
        empty_import_button_->setEnabled(can_import);
    if (empty_state_)
        empty_state_->show();
}

void QtDataEditorWidget::show_data_state()
{
    if (selection_group_)
        selection_group_->show();
    if (empty_state_)
        empty_state_->hide();
    if (table_group_)
        table_group_->show();
}

void QtDataEditorWidget::refresh()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
    {
        axes_combo_->clear();
        series_combo_->clear();
        table_->setRowCount(0);
        page_label_->setText("Rows 0-0 of 0");
        previous_page_button_->setEnabled(false);
        next_page_button_->setEnabled(false);
        info_label_->setText("No figure selected");
        show_empty_state("Select a figure to edit its data.", false);
        return;
    }

    Figure* figure = registry_->get(active_id_);
    if (!figure)
    {
        info_label_->setText("Figure not found");
        show_empty_state("The selected figure is no longer available.", false);
        return;
    }

    populate_axes_combo(*figure);
}

void QtDataEditorWidget::populate_axes_combo(Figure& figure)
{
    axes_combo_->blockSignals(true);
    axes_combo_->clear();

    int idx = 0;
    figure.for_each_axes(
        [&](AxesBase* ab)
        {
            auto*   ax = dynamic_cast<Axes*>(ab);
            QString label =
                ax ? QString::fromStdString(ax->title()) : QString("Axes %1").arg(idx + 1);
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
        paged_series_ = nullptr;
        page_start_   = 0;
        page_label_->setText("Rows 0-0 of 0");
        previous_page_button_->setEnabled(false);
        next_page_button_->setEnabled(false);
        info_label_->setText("No axes in figure");
        show_empty_state("This figure has no axes. Add an axes before importing data.", false);
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
        paged_series_ = nullptr;
        page_start_   = 0;
        page_label_->setText("Rows 0-0 of 0");
        previous_page_button_->setEnabled(false);
        next_page_button_->setEnabled(false);
        info_label_->setText("No series in axes");
        show_empty_state("No series in this axes. Import CSV data to create one or more series.",
                         true);
    }
}

void QtDataEditorWidget::populate_data_table(Series& series)
{
    suppress_cell_signal_ = true;

    EditableSeriesData data;
    if (!capture_editable_series_data(series, data))
    {
        table_->setRowCount(0);
        page_label_->setText("Rows 0-0 of 0");
        previous_page_button_->setEnabled(false);
        next_page_button_->setEnabled(false);
        info_label_->setText("Series type not editable");
        show_empty_state("The selected series type cannot be edited in the data table.", true);
        suppress_cell_signal_ = false;
        return;
    }

    show_data_state();

    const bool is_3d = data.is_3d();
    table_->setColumnCount(is_3d ? 3 : 2);
    table_->setHorizontalHeaderLabels(is_3d ? QStringList{"X", "Y", "Z"} : QStringList{"X", "Y"});
    size_t n = std::min(data.x.size(), data.y.size());
    if (is_3d)
        n = std::min(n, data.z.size());
    if (paged_series_ != &series)
    {
        paged_series_ = &series;
        page_start_   = 0;
    }
    if (n == 0)
        page_start_ = 0;
    else if (page_start_ >= n)
        page_start_ = ((n - 1) / page_size_) * page_size_;
    const size_t visible_rows = std::min(page_size_, n - page_start_);
    table_->setRowCount(static_cast<int>(visible_rows));

    for (size_t visible_row = 0; visible_row < visible_rows; ++visible_row)
    {
        const size_t i      = page_start_ + visible_row;
        auto*        x_item = new QTableWidgetItem(
            QString::number(data.x_offset + static_cast<double>(data.x[i]), 'g', 15));
        auto* y_item = new QTableWidgetItem(QString::number(data.y[i], 'g', 6));
        table_->setVerticalHeaderItem(static_cast<int>(visible_row),
                                      new QTableWidgetItem(QString::number(i + 1)));
        table_->setItem(static_cast<int>(visible_row), 0, x_item);
        table_->setItem(static_cast<int>(visible_row), 1, y_item);
        if (is_3d)
            table_->setItem(static_cast<int>(visible_row),
                            2,
                            new QTableWidgetItem(QString::number(data.z[i], 'g', 6)));
    }

    const size_t first_displayed = visible_rows == 0 ? 0 : page_start_ + 1;
    const size_t last_displayed  = page_start_ + visible_rows;
    page_label_->setText(
        QString("Rows %1-%2 of %3").arg(first_displayed).arg(last_displayed).arg(n));
    previous_page_button_->setEnabled(page_start_ > 0);
    next_page_button_->setEnabled(page_start_ + visible_rows < n);

    info_label_->setText(
        data.x_offset == 0.0
            ? QString("%1 data points").arg(n)
            : QString("%1 data points; absolute X base %2").arg(n).arg(data.x_offset, 0, 'g', 15));
    suppress_cell_signal_ = false;
}

void QtDataEditorWidget::on_axes_selected(int index)
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID || index < 0)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    int       idx    = 0;
    AxesBase* target = nullptr;
    figure->for_each_axes(
        [&](AxesBase* ab)
        {
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

    int axes_idx = axes_combo_->currentData().toInt();
    if (axes_idx < 0)
        return;

    int       a_idx       = 0;
    AxesBase* target_axes = nullptr;
    figure->for_each_axes(
        [&](AxesBase* ab)
        {
            if (a_idx == axes_idx)
                target_axes = ab;
            ++a_idx;
        });

    if (!target_axes)
        return;

    const int   series_idx  = series_combo_->itemData(index).toInt();
    const auto& series_list = target_axes->series();
    if (series_idx < 0 || static_cast<size_t>(series_idx) >= series_list.size())
        return;

    auto* s = series_list[static_cast<size_t>(series_idx)].get();
    if (s)
        populate_data_table(*s);
}

Series* QtDataEditorWidget::current_series(size_t& axes_index, size_t& series_index) const
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return nullptr;
    const int axes_value   = axes_combo_->currentData().toInt();
    const int series_value = series_combo_->currentData().toInt();
    if (axes_value < 0 || series_value < 0)
        return nullptr;
    axes_index   = static_cast<size_t>(axes_value);
    series_index = static_cast<size_t>(series_value);
    return find_figure_series(*registry_, active_id_, axes_index, series_index);
}

bool QtDataEditorWidget::current_axes_is_3d() const
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return false;
    Figure*   figure     = registry_->get(active_id_);
    const int axes_index = axes_combo_ ? axes_combo_->currentData().toInt() : -1;
    return figure && axes_index >= 0
           && dynamic_cast<Axes3D*>(find_figure_axes(*figure, static_cast<size_t>(axes_index)));
}

bool QtDataEditorWidget::commit_data(EditableSeriesData data, const std::string& description)
{
    size_t axes_index   = 0;
    size_t series_index = 0;
    if (!current_series(axes_index, series_index))
        return false;

    QPointer<QtDataEditorWidget> self(this);
    const FigureId               figure_id = active_id_;
    const bool                   changed   = undoable_set_series_data(
        undo_manager_,
        *registry_,
        figure_id,
        axes_index,
        series_index,
        std::move(data),
        description,
        [redraw = redraw_, figure_id, axes_index, series_index, self]()
        {
            if (redraw)
                redraw->request_redraw(figure_id);
            if (!self)
                return;
            emit self->data_changed();
            if (self->active_id_ != figure_id)
                return;
            size_t  current_axes   = 0;
            size_t  current_series = 0;
            Series* series = self->current_series(current_axes, current_series);
            if (series && current_axes == axes_index && current_series == series_index)
                self->populate_data_table(*series);
        });
    return changed;
}

std::vector<int> QtDataEditorWidget::selected_rows() const
{
    std::set<int> unique_rows;
    for (const QTableWidgetItem* item : table_->selectedItems())
        if (item)
            unique_rows.insert(static_cast<int>(page_start_) + item->row());
    if (unique_rows.empty() && table_->currentRow() >= 0)
        unique_rows.insert(static_cast<int>(page_start_) + table_->currentRow());
    return {unique_rows.begin(), unique_rows.end()};
}

void QtDataEditorWidget::on_add_row()
{
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    const float next_x = data.x.empty() ? 0.0f : data.x.back() + 1.0f;
    const float next_y = data.y.empty() ? 0.0f : data.y.back();
    data.x.push_back(next_x);
    data.y.push_back(next_y);
    if (data.is_3d())
        data.z.push_back(data.z.empty() ? 0.0f : data.z.back());
    page_start_ = ((data.x.size() - 1) / page_size_) * page_size_;
    if (commit_data(std::move(data), "Add series data row"))
        table_->setCurrentCell(table_->rowCount() - 1, 0);
}

void QtDataEditorWidget::on_delete_rows()
{
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    auto rows = selected_rows();
    if (rows.empty())
        return;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        if (*it < 0 || static_cast<size_t>(*it) >= data.x.size())
            continue;
        data.x.erase(data.x.begin() + *it);
        data.y.erase(data.y.begin() + *it);
        if (data.is_3d())
            data.z.erase(data.z.begin() + *it);
    }
    commit_data(std::move(data), "Delete series data rows");
}

void QtDataEditorWidget::on_move_rows_up()
{
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    auto rows = selected_rows();
    if (rows.empty() || rows.front() <= 0)
        return;
    const std::set<int> selected(rows.begin(), rows.end());
    for (int row : rows)
    {
        if (row <= 0 || selected.contains(row - 1))
            continue;
        std::swap(data.x[static_cast<size_t>(row)], data.x[static_cast<size_t>(row - 1)]);
        std::swap(data.y[static_cast<size_t>(row)], data.y[static_cast<size_t>(row - 1)]);
        if (data.is_3d())
            std::swap(data.z[static_cast<size_t>(row)], data.z[static_cast<size_t>(row - 1)]);
    }
    commit_data(std::move(data), "Move series data rows up");
}

void QtDataEditorWidget::on_move_rows_down()
{
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    auto rows = selected_rows();
    if (rows.empty() || static_cast<size_t>(rows.back() + 1) >= data.x.size())
        return;
    const std::set<int> selected(rows.begin(), rows.end());
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        const int row = *it;
        if (static_cast<size_t>(row + 1) >= data.x.size() || selected.contains(row + 1))
            continue;
        std::swap(data.x[static_cast<size_t>(row)], data.x[static_cast<size_t>(row + 1)]);
        std::swap(data.y[static_cast<size_t>(row)], data.y[static_cast<size_t>(row + 1)]);
        if (data.is_3d())
            std::swap(data.z[static_cast<size_t>(row)], data.z[static_cast<size_t>(row + 1)]);
    }
    commit_data(std::move(data), "Move series data rows down");
}

void QtDataEditorWidget::on_paste_cells()
{
    if (!clipboard_)
    {
        info_label_->setText("Clipboard unavailable");
        return;
    }
    QString text = QString::fromStdString(clipboard_->paste_text());
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    while (text.endsWith('\n'))
        text.chop(1);
    if (text.isEmpty())
    {
        info_label_->setText("Clipboard is empty");
        return;
    }

    std::vector<std::vector<double>> values;
    const QStringList                lines = text.split('\n', Qt::KeepEmptyParts);
    values.reserve(static_cast<size_t>(lines.size()));
    for (const QString& line : lines)
    {
        const QChar         delimiter = line.contains('\t') ? '\t' : ',';
        const QStringList   cells     = line.split(delimiter, Qt::KeepEmptyParts);
        std::vector<double> row_values;
        row_values.reserve(static_cast<size_t>(cells.size()));
        for (const QString& cell : cells)
        {
            bool         ok    = false;
            const double value = cell.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(value))
            {
                info_label_->setText("Paste contains a non-numeric cell");
                return;
            }
            row_values.push_back(value);
        }
        values.push_back(std::move(row_values));
    }

    const int start_row = static_cast<int>(page_start_) + std::max(0, table_->currentRow());
    const int start_col = std::max(0, table_->currentColumn());
    size_t    width     = 0;
    for (const auto& row : values)
        width = std::max(width, row.size());
    if (width == 0 || start_col + static_cast<int>(width) > table_->columnCount())
    {
        info_label_->setText(table_->columnCount() == 3 ? "Paste exceeds the X/Y/Z columns"
                                                        : "Paste exceeds the X/Y columns");
        return;
    }
    constexpr size_t max_rows      = 1'000'000;
    const size_t     required_rows = static_cast<size_t>(start_row) + values.size();
    if (required_rows > max_rows)
    {
        info_label_->setText("Paste exceeds the row limit");
        return;
    }

    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    while (data.x.size() < required_rows)
    {
        data.x.push_back(data.x.empty() ? 0.0f : data.x.back() + 1.0f);
        data.y.push_back(data.y.empty() ? 0.0f : data.y.back());
        if (data.is_3d())
            data.z.push_back(data.z.empty() ? 0.0f : data.z.back());
    }
    for (size_t row = 0; row < values.size(); ++row)
    {
        for (size_t col = 0; col < values[row].size(); ++col)
        {
            const int target_column = start_col + static_cast<int>(col);
            auto&     target = target_column == 0 ? data.x : (target_column == 1 ? data.y : data.z);
            target[static_cast<size_t>(start_row) + row] =
                target_column == 0 ? static_cast<float>(values[row][col] - data.x_offset)
                                   : static_cast<float>(values[row][col]);
        }
    }
    commit_data(std::move(data), "Paste series data cells");
}

void QtDataEditorWidget::on_previous_page()
{
    size_t  axes_index   = 0;
    size_t  series_index = 0;
    Series* series       = current_series(axes_index, series_index);
    if (!series || page_start_ == 0)
        return;
    page_start_ = page_start_ > page_size_ ? page_start_ - page_size_ : 0;
    populate_data_table(*series);
}

void QtDataEditorWidget::on_next_page()
{
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    size_t row_count = std::min(data.x.size(), data.y.size());
    if (data.is_3d())
        row_count = std::min(row_count, data.z.size());
    if (page_start_ + page_size_ >= row_count)
        return;
    page_start_ += page_size_;
    populate_data_table(*series);
}

void QtDataEditorWidget::prepare_column_import(std::vector<std::string>        headers,
                                               std::vector<std::vector<float>> columns,
                                               std::vector<double>             column_offsets,
                                               size_t                          rows)
{
    show_data_state();
    import_headers_        = std::move(headers);
    import_columns_        = std::move(columns);
    import_column_offsets_ = std::move(column_offsets);
    import_rows_           = rows;
    while (import_headers_.size() < import_columns_.size())
        import_headers_.push_back("Column " + std::to_string(import_headers_.size() + 1));
    import_column_offsets_.resize(import_columns_.size(), 0.0);

    import_x_column_->blockSignals(true);
    import_z_column_->blockSignals(true);
    import_y_columns_->blockSignals(true);
    import_x_column_->clear();
    import_z_column_->clear();
    import_y_columns_->clear();
    for (size_t column = 0; column < import_columns_.size(); ++column)
    {
        const QString label = QString::fromStdString(import_headers_[column]);
        import_x_column_->addItem(label, static_cast<int>(column));
        import_z_column_->addItem(label, static_cast<int>(column));
        auto* item = new QListWidgetItem(label, import_y_columns_);
        item->setData(Qt::UserRole, static_cast<int>(column));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(column == 0 ? Qt::Unchecked : Qt::Checked);
    }
    const bool is_3d = current_axes_is_3d();
    import_z_label_->setVisible(is_3d);
    import_z_column_->setVisible(is_3d);
    import_x_column_->setCurrentIndex(0);
    if (is_3d && import_z_column_->count() > 0)
        import_z_column_->setCurrentIndex(std::min(2, import_z_column_->count() - 1));
    import_x_column_->blockSignals(false);
    import_z_column_->blockSignals(false);
    import_y_columns_->blockSignals(false);
    on_import_x_column_changed(0);
    import_mapping_group_->show();
    info_label_->setText(QString("Choose columns: %1 rows, %2 numeric columns")
                             .arg(import_rows_)
                             .arg(import_columns_.size()));
}

void QtDataEditorWidget::on_import_x_column_changed(int)
{
    const int  x_column = import_x_column_->currentData().toInt();
    const bool is_3d    = current_axes_is_3d();
    if (is_3d && import_z_column_->currentData().toInt() == x_column)
    {
        QSignalBlocker blocker(import_z_column_);
        for (int index = 0; index < import_z_column_->count(); ++index)
        {
            if (import_z_column_->itemData(index).toInt() != x_column)
            {
                import_z_column_->setCurrentIndex(index);
                break;
            }
        }
    }
    const int z_column = is_3d ? import_z_column_->currentData().toInt() : -1;
    import_y_columns_->blockSignals(true);
    for (int row = 0; row < import_y_columns_->count(); ++row)
    {
        auto* item = import_y_columns_->item(row);
        if (!item)
            continue;
        const int  column      = item->data(Qt::UserRole).toInt();
        const bool is_reserved = column == x_column || (is_3d && column == z_column);
        item->setFlags(is_reserved ? (item->flags() & ~Qt::ItemIsEnabled)
                                   : (item->flags() | Qt::ItemIsEnabled));
        if (is_reserved)
            item->setCheckState(Qt::Unchecked);
    }
    import_y_columns_->blockSignals(false);
    on_import_column_changed(nullptr);
}

void QtDataEditorWidget::on_import_column_changed(QListWidgetItem* item)
{
    const int x_column = import_x_column_->currentData().toInt();
    const int z_column = current_axes_is_3d() ? import_z_column_->currentData().toInt() : -1;
    if (item
        && (item->data(Qt::UserRole).toInt() == x_column
            || item->data(Qt::UserRole).toInt() == z_column)
        && item->checkState() == Qt::Checked)
    {
        import_y_columns_->blockSignals(true);
        item->setCheckState(Qt::Unchecked);
        import_y_columns_->blockSignals(false);
    }
    size_t selected = 0;
    for (int row = 0; row < import_y_columns_->count(); ++row)
        if (const auto* candidate = import_y_columns_->item(row);
            candidate && candidate->checkState() == Qt::Checked)
            ++selected;
    apply_import_columns_->setEnabled(selected > 0);
    apply_import_columns_->setText(selected == 1 ? "Import Selected Series"
                                                 : QString("Import %1 Series").arg(selected));
}

bool QtDataEditorWidget::commit_imported_series(const std::vector<size_t>& y_columns)
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID || y_columns.empty())
        return false;
    const int x_value    = import_x_column_->currentData().toInt();
    const int axes_value = axes_combo_->currentData().toInt();
    if (x_value < 0 || axes_value < 0 || static_cast<size_t>(x_value) >= import_columns_.size())
        return false;
    Figure*   figure = registry_->get(active_id_);
    AxesBase* axes = figure ? find_figure_axes(*figure, static_cast<size_t>(axes_value)) : nullptr;
    if (!axes || axes->series().size() + y_columns.size() > 200)
    {
        info_label_->setText("Import exceeds the 200-series axes limit");
        return false;
    }
    const auto&  x        = import_columns_[static_cast<size_t>(x_value)];
    const double x_offset = static_cast<size_t>(x_value) < import_column_offsets_.size()
                                ? import_column_offsets_[static_cast<size_t>(x_value)]
                                : 0.0;
    auto*        axes_3d  = dynamic_cast<Axes3D*>(axes);
    auto*        axes_2d  = dynamic_cast<Axes*>(axes);
    if (!axes_2d && !axes_3d)
    {
        info_label_->setText("Selected axes do not support CSV series import");
        return false;
    }
    if (x.size() != import_rows_)
        return false;
    const int z_value = axes_3d ? import_z_column_->currentData().toInt() : -1;
    if (axes_3d
        && (z_value < 0 || z_value == x_value
            || static_cast<size_t>(z_value) >= import_columns_.size()
            || import_columns_[static_cast<size_t>(z_value)].size() != import_rows_))
    {
        info_label_->setText("3D import requires distinct X and Z columns");
        return false;
    }
    for (size_t column : y_columns)
    {
        if (column >= import_columns_.size() || column == static_cast<size_t>(x_value)
            || (axes_3d && column == static_cast<size_t>(z_value))
            || import_columns_[column].size() != import_rows_)
        {
            info_label_->setText("Selected columns have inconsistent row counts");
            return false;
        }
    }

    struct ImportState
    {
        FigureRegistry*             registry   = nullptr;
        FigureId                    figure_id  = INVALID_FIGURE_ID;
        size_t                      axes_index = 0;
        std::vector<SeriesSnapshot> snapshots;
        std::vector<Series*>        live;
    };
    auto state        = std::make_shared<ImportState>();
    state->registry   = registry_;
    state->figure_id  = active_id_;
    state->axes_index = static_cast<size_t>(axes_value);
    for (size_t column : y_columns)
    {
        Series* imported = nullptr;
        if (axes_3d)
        {
            auto& line = axes_3d->line3d(x,
                                         import_columns_[column],
                                         import_columns_[static_cast<size_t>(z_value)]);
            line.label(import_headers_[column]).x_offset(x_offset);
            imported = &line;
        }
        else
        {
            auto& line = axes_2d->plot(x, import_columns_[column]);
            line.label(import_headers_[column]).x_offset(x_offset);
            imported = &line;
        }
        state->live.push_back(imported);
        state->snapshots.push_back(SeriesClipboard::snapshot(*imported));
    }
    axes->auto_fit();

    const FigureId               figure_id = active_id_;
    QPointer<QtDataEditorWidget> self(this);
    auto                         notify = [redraw = redraw_, figure_id, self]()
    {
        if (redraw)
            redraw->request_redraw(figure_id);
        if (self)
        {
            emit self->data_changed();
            if (self->active_id_ == figure_id)
                self->refresh();
        }
    };
    notify();
    if (undo_manager_)
    {
        undo_manager_->push(UndoAction{
            "Import CSV series",
            [state, notify]()
            {
                Figure*   target_figure = state->registry->get(state->figure_id);
                AxesBase* target_axes =
                    target_figure ? find_figure_axes(*target_figure, state->axes_index) : nullptr;
                if (!target_axes)
                    return;
                for (Series* series : state->live)
                {
                    const auto& entries = target_axes->series();
                    for (size_t index = 0; index < entries.size(); ++index)
                    {
                        if (entries[index].get() == series)
                        {
                            target_axes->remove_series(index);
                            break;
                        }
                    }
                }
                state->live.clear();
                target_axes->auto_fit();
                notify();
            },
            [state, notify]()
            {
                Figure*   target_figure = state->registry->get(state->figure_id);
                AxesBase* target_axes =
                    target_figure ? find_figure_axes(*target_figure, state->axes_index) : nullptr;
                if (!target_axes)
                    return;
                state->live.clear();
                for (const auto& snapshot : state->snapshots)
                    if (Series* series = SeriesClipboard::paste_to(*target_axes, snapshot))
                        state->live.push_back(series);
                target_axes->auto_fit();
                notify();
            }});
    }
    return true;
}

void QtDataEditorWidget::on_apply_import_columns()
{
    std::vector<size_t> selected;
    for (int row = 0; row < import_y_columns_->count(); ++row)
    {
        const auto* item = import_y_columns_->item(row);
        if (item && item->checkState() == Qt::Checked)
            selected.push_back(static_cast<size_t>(item->data(Qt::UserRole).toInt()));
    }
    if (!commit_imported_series(selected))
        return;
    import_mapping_group_->hide();
    info_label_->setText(
        QString("Imported %1 series with %2 data points").arg(selected.size()).arg(import_rows_));
    import_headers_.clear();
    import_columns_.clear();
    import_column_offsets_.clear();
    import_rows_ = 0;
}

void QtDataEditorWidget::on_cancel_import_columns()
{
    import_mapping_group_->hide();
    import_headers_.clear();
    import_columns_.clear();
    import_column_offsets_.clear();
    import_rows_ = 0;
    info_label_->setText("CSV import cancelled");
    refresh();
}

void QtDataEditorWidget::on_import_csv()
{
    if (!dialog_)
    {
        info_label_->setText("File dialog unavailable");
        if (empty_state_label_ && empty_state_ && !empty_state_->isHidden())
            empty_state_label_->setText("File dialog unavailable");
        return;
    }
    const auto path = dialog_->file_dialog(DialogService::FileType::Open,
                                           "Import Series Data",
                                           "series.csv",
                                           {{"CSV/TSV Data", "*.csv *.tsv *.txt"}});
    if (!path)
        return;

    const CsvData csv = parse_csv(*path);
    if (!csv.error.empty())
    {
        info_label_->setText(QString::fromStdString(csv.error));
        return;
    }
    if (csv.num_cols < 2 || csv.num_rows == 0 || csv.columns.size() < 2)
    {
        info_label_->setText("Import requires at least two numeric columns");
        return;
    }
    if (current_axes_is_3d() && (csv.num_cols < 3 || csv.columns.size() < 3))
    {
        info_label_->setText("3D import requires at least three numeric columns");
        return;
    }
    size_t selected_axes   = 0;
    size_t selected_series = 0;
    if (current_axes_is_3d() || csv.num_cols > 2 || !current_series(selected_axes, selected_series))
    {
        prepare_column_import(csv.headers, csv.columns, csv.column_offsets, csv.num_rows);
        return;
    }

    EditableSeriesData data;
    data.x        = csv.columns[0];
    data.y        = csv.columns[1];
    data.x_offset = csv.column_offsets.empty() ? 0.0 : csv.column_offsets[0];
    if (data.x.size() != data.y.size())
    {
        info_label_->setText("Imported X/Y columns have different lengths");
        return;
    }
    if (commit_data(std::move(data), "Import series data from CSV"))
        info_label_->setText(QString("Imported %1 data points").arg(csv.num_rows));
}

void QtDataEditorWidget::on_export_csv()
{
    if (!dialog_)
    {
        info_label_->setText("File dialog unavailable");
        return;
    }
    size_t             axes_index   = 0;
    size_t             series_index = 0;
    Series*            series       = current_series(axes_index, series_index);
    EditableSeriesData data;
    if (!series || !capture_editable_series_data(*series, data))
        return;
    const auto path = dialog_->file_dialog(DialogService::FileType::Save,
                                           "Export Series Data",
                                           "series.csv",
                                           {{"CSV Data", "*.csv"}});
    if (!path)
        return;

    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        info_label_->setText("Cannot open export destination");
        return;
    }
    output << (data.is_3d() ? "x,y,z\n" : "x,y\n") << std::setprecision(17);
    size_t rows = std::min(data.x.size(), data.y.size());
    if (data.is_3d())
        rows = std::min(rows, data.z.size());
    for (size_t row = 0; row < rows; ++row)
    {
        output << data.x_offset + static_cast<double>(data.x[row]) << ',' << data.y[row];
        if (data.is_3d())
            output << ',' << data.z[row];
        output << '\n';
    }
    output.flush();
    if (!output)
    {
        info_label_->setText("Failed to write CSV data");
        return;
    }
    info_label_->setText(QString("Exported %1 data points").arg(rows));
}

void QtDataEditorWidget::on_cell_changed(int row, int col)
{
    if (suppress_cell_signal_)
        return;

    size_t  axes_idx   = 0;
    size_t  series_idx = 0;
    Series* s          = current_series(axes_idx, series_idx);
    if (!s)
        return;

    EditableSeriesData data;
    if (!capture_editable_series_data(*s, data))
        return;
    const size_t data_row = page_start_ + static_cast<size_t>(std::max(0, row));

    auto* item = table_->item(row, col);
    if (!item)
        return;

    bool         ok  = false;
    const double val = item->text().toDouble(&ok);
    if (!ok || !std::isfinite(val))
    {
        const auto& current   = col == 0 ? data.x : (col == 1 ? data.y : data.z);
        suppress_cell_signal_ = true;
        if (row >= 0 && data_row < current.size())
            item->setText(
                QString::number(col == 0 ? data.x_offset + current[data_row] : current[data_row],
                                'g',
                                15));
        suppress_cell_signal_ = false;
        info_label_->setText("Invalid numeric value");
        return;
    }

    auto& values = col == 0 ? data.x : (col == 1 ? data.y : data.z);
    if (row < 0 || data_row >= values.size())
        return;
    values[data_row] = col == 0 ? static_cast<float>(val - data.x_offset) : static_cast<float>(val);

    const std::string axis_name = col == 0 ? "X" : (col == 1 ? "Y" : "Z");
    commit_data(std::move(data), "Edit series " + axis_name + " data");
}

}   // namespace spectra::adapters::qt
