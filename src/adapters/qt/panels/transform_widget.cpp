// transform_widget.cpp — Qt transform/editor panel implementation.

#include "transform_widget.hpp"

#include "app/frontend_services.hpp"
#include "math/expression_eval.hpp"
#include "ui/commands/undoable_property.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include <QPointer>

#include <algorithm>
#include <limits>

namespace spectra::adapters::qt
{

QtTransformWidget::QtTransformWidget(FigureRegistry*           registry,
                                     ::spectra::UndoManager*   undo_manager,
                                     ::spectra::RedrawRequest* redraw,
                                     QWidget*                  parent)
    : QDockWidget("Transforms", parent), registry_(registry), undo_manager_(undo_manager),
      redraw_(redraw)
{
    setObjectName("transform_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    build_ui();
    refresh_transform_list();
}

void QtTransformWidget::build_ui()
{
    auto* container   = new QWidget(this);
    auto* main_layout = new QVBoxLayout(container);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    // ── Transform selector ─────────────────────────────────────────────
    auto* xform_group  = new QGroupBox("Available Transforms", container);
    auto* xform_layout = new QVBoxLayout(xform_group);

    target_combo_ = new QComboBox(xform_group);
    target_combo_->setObjectName("transform_target");
    target_combo_->addItem("All Visible Series", "all");
    xform_layout->addWidget(target_combo_);

    xform_layout->addWidget(new QLabel("Custom series selection (overrides scope)", xform_group));
    target_list_ = new QListWidget(xform_group);
    target_list_->setObjectName("transform_target_list");
    target_list_->setMaximumHeight(110);
    xform_layout->addWidget(target_list_);

    transform_combo_ = new QComboBox(xform_group);
    transform_combo_->setObjectName("transform_combo");
    xform_layout->addWidget(transform_combo_);

    description_label_ = new QLabel("Select a transform to see description", xform_group);
    description_label_->setWordWrap(true);
    description_label_->setStyleSheet("color: gray; font-size: 11px;");
    xform_layout->addWidget(description_label_);

    preview_label_ = new QLabel("Preview unavailable", xform_group);
    preview_label_->setObjectName("transform_preview");
    preview_label_->setWordWrap(true);
    xform_layout->addWidget(preview_label_);

    // ── Parameters ──────────────────────────────────────────────────────
    auto* param_group = new QGroupBox("Parameters", xform_group);
    auto* param_form  = new QFormLayout(param_group);

    scale_spin_ = new QDoubleSpinBox(param_group);
    scale_spin_->setObjectName("transform_scale");
    scale_spin_->setRange(-1e9, 1e9);
    scale_spin_->setDecimals(4);
    scale_spin_->setValue(1.0);
    param_form->addRow("Scale Factor", scale_spin_);

    offset_spin_ = new QDoubleSpinBox(param_group);
    offset_spin_->setObjectName("transform_offset");
    offset_spin_->setRange(-1e9, 1e9);
    offset_spin_->setDecimals(4);
    offset_spin_->setValue(0.0);
    param_form->addRow("Offset Value", offset_spin_);

    clamp_min_spin_ = new QDoubleSpinBox(param_group);
    clamp_min_spin_->setObjectName("transform_clamp_min");
    clamp_min_spin_->setRange(-1e9, 1e9);
    clamp_min_spin_->setDecimals(4);
    clamp_min_spin_->setValue(0.0);
    param_form->addRow("Clamp Min", clamp_min_spin_);

    clamp_max_spin_ = new QDoubleSpinBox(param_group);
    clamp_max_spin_->setObjectName("transform_clamp_max");
    clamp_max_spin_->setRange(-1e9, 1e9);
    clamp_max_spin_->setDecimals(4);
    clamp_max_spin_->setValue(1.0);
    param_form->addRow("Clamp Max", clamp_max_spin_);

    fft_db_check_ = new QCheckBox("Output in dB", param_group);
    fft_db_check_->setObjectName("transform_fft_db");
    param_form->addRow("FFT dB", fft_db_check_);

    fft_sr_spin_ = new QDoubleSpinBox(param_group);
    fft_sr_spin_->setObjectName("transform_fft_sample_rate");
    fft_sr_spin_->setRange(0.0, 1e9);
    fft_sr_spin_->setDecimals(2);
    fft_sr_spin_->setValue(0.0);
    param_form->addRow("FFT Sample Rate", fft_sr_spin_);

    xform_layout->addWidget(param_group);

    // ── Action buttons ──────────────────────────────────────────────────
    auto* btn_layout = new QHBoxLayout();
    apply_btn_       = new QPushButton("Apply", xform_group);
    apply_btn_->setObjectName("apply_transform_btn");
    add_to_pipeline_btn_ = new QPushButton("Add to Pipeline", xform_group);
    add_to_pipeline_btn_->setObjectName("add_transform_pipeline_btn");
    btn_layout->addWidget(apply_btn_);
    btn_layout->addWidget(add_to_pipeline_btn_);
    xform_layout->addLayout(btn_layout);

    auto* formula_group  = new QGroupBox("Custom Formula", xform_group);
    auto* formula_layout = new QVBoxLayout(formula_group);
    formula_edit_        = new QLineEdit(formula_group);
    formula_edit_->setObjectName("transform_formula");
    formula_edit_->setPlaceholderText("Expression using x, y, i, n (for example: y * 2)");
    formula_layout->addWidget(formula_edit_);
    formula_status_ = new QLabel("Enter a formula", formula_group);
    formula_status_->setObjectName("transform_formula_status");
    formula_status_->setWordWrap(true);
    formula_layout->addWidget(formula_status_);
    apply_formula_btn_ = new QPushButton("Apply Formula", formula_group);
    apply_formula_btn_->setObjectName("apply_custom_transform_btn");
    apply_formula_btn_->setEnabled(false);
    formula_layout->addWidget(apply_formula_btn_);
    xform_layout->addWidget(formula_group);

    main_layout->addWidget(xform_group);

    // ── Pipeline ────────────────────────────────────────────────────────
    auto* pipeline_group  = new QGroupBox("Pipeline", container);
    auto* pipeline_layout = new QVBoxLayout(pipeline_group);

    pipeline_list_ = new QListWidget(pipeline_group);
    pipeline_list_->setObjectName("pipeline_list");
    pipeline_list_->setMaximumHeight(120);
    pipeline_layout->addWidget(pipeline_list_);

    auto* pipe_btn_layout = new QHBoxLayout();
    move_step_up_btn_     = new QPushButton("Up", pipeline_group);
    move_step_up_btn_->setObjectName("move_transform_step_up_btn");
    move_step_down_btn_ = new QPushButton("Down", pipeline_group);
    move_step_down_btn_->setObjectName("move_transform_step_down_btn");
    remove_step_btn_      = new QPushButton("Remove", pipeline_group);
    remove_step_btn_->setObjectName("remove_transform_step_btn");
    clear_pipeline_btn_   = new QPushButton("Clear", pipeline_group);
    clear_pipeline_btn_->setObjectName("clear_transform_pipeline_btn");
    apply_pipeline_btn_   = new QPushButton("Apply Pipeline", pipeline_group);
    apply_pipeline_btn_->setObjectName("apply_transform_pipeline_btn");
    pipe_btn_layout->addWidget(move_step_up_btn_);
    pipe_btn_layout->addWidget(move_step_down_btn_);
    pipe_btn_layout->addWidget(remove_step_btn_);
    pipe_btn_layout->addWidget(clear_pipeline_btn_);
    pipe_btn_layout->addWidget(apply_pipeline_btn_);
    pipeline_layout->addLayout(pipe_btn_layout);

    main_layout->addWidget(pipeline_group);

    // ── Presets ─────────────────────────────────────────────────────────
    auto* preset_group  = new QGroupBox("Saved Pipelines", container);
    auto* preset_layout = new QHBoxLayout(preset_group);

    preset_name_edit_ = new QComboBox(preset_group);
    preset_name_edit_->setObjectName("preset_name_combo");
    preset_name_edit_->setEditable(true);
    preset_layout->addWidget(preset_name_edit_);

    save_preset_btn_ = new QPushButton("Save", preset_group);
    load_preset_btn_ = new QPushButton("Load", preset_group);
    preset_layout->addWidget(save_preset_btn_);
    preset_layout->addWidget(load_preset_btn_);

    main_layout->addWidget(preset_group);

    main_layout->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(container);
    scroll->setWidgetResizable(true);
    setWidget(scroll);

    // ── Connections ─────────────────────────────────────────────────────
    connect(transform_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtTransformWidget::on_transform_selected);
    connect(target_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int)
            {
                QSignalBlocker blocker(target_list_);
                for (int row = 0; row < target_list_->count(); ++row)
                    if (auto* item = target_list_->item(row))
                        item->setCheckState(Qt::Unchecked);
            });
    connect(target_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtTransformWidget::update_preview);
    connect(target_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { on_formula_changed(formula_edit_->text()); });
    connect(target_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int)
            {
                const bool has_pipeline = pipeline_.step_count() > 0;
                save_active_pipeline();
                if (has_pipeline)
                    emit data_changed();
            });
    connect(target_list_,
            &QListWidget::itemChanged,
            this,
            [this](QListWidgetItem*)
            {
                update_preview();
                on_formula_changed(formula_edit_->text());
                const bool has_pipeline = pipeline_.step_count() > 0;
                save_active_pipeline();
                if (has_pipeline)
                    emit data_changed();
            });
    for (auto* spin : {scale_spin_, offset_spin_, clamp_min_spin_, clamp_max_spin_, fft_sr_spin_})
        connect(spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                &QtTransformWidget::update_preview);
    connect(fft_db_check_, &QCheckBox::toggled, this, &QtTransformWidget::update_preview);
    connect(formula_edit_, &QLineEdit::textChanged, this, &QtTransformWidget::on_formula_changed);
    connect(apply_formula_btn_, &QPushButton::clicked, this, &QtTransformWidget::on_apply_formula);
    connect(apply_btn_, &QPushButton::clicked, this, &QtTransformWidget::on_apply_transform);
    connect(add_to_pipeline_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_add_to_pipeline);
    connect(remove_step_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_remove_pipeline_step);
    connect(move_step_up_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_move_pipeline_step_up);
    connect(move_step_down_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_move_pipeline_step_down);
    connect(pipeline_list_,
            &QListWidget::itemChanged,
            this,
            &QtTransformWidget::on_pipeline_item_changed);
    connect(clear_pipeline_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_clear_pipeline);
    connect(apply_pipeline_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_apply_pipeline);
    connect(save_preset_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_save_pipeline_preset);
    connect(load_preset_btn_,
            &QPushButton::clicked,
            this,
            &QtTransformWidget::on_load_pipeline_preset);
}

void QtTransformWidget::set_active_figure(FigureId id)
{
    if (active_id_ == id)
    {
        refresh_targets();
        return;
    }
    save_active_pipeline();
    active_id_ = id;
    refresh_targets();
    load_active_pipeline();
}

std::string QtTransformWidget::current_target_key() const
{
    QStringList selected;
    if (target_list_)
    {
        for (int row = 0; row < target_list_->count(); ++row)
        {
            const auto* item = target_list_->item(row);
            if (item && item->checkState() == Qt::Checked)
                selected.push_back(item->data(Qt::UserRole).toString());
        }
    }
    if (!selected.empty())
        return "multi:" + selected.join(',').toStdString();
    return target_combo_ ? target_combo_->currentData().toString().toStdString() : "all";
}

void QtTransformWidget::apply_target_key(const std::string& key)
{
    const QString  target = QString::fromStdString(key);
    QSignalBlocker combo_blocker(target_combo_);
    QSignalBlocker list_blocker(target_list_);
    const bool     is_multi = target.startsWith("multi:");
    const int      combo_index =
        is_multi ? target_combo_->findData("all") : target_combo_->findData(target);
    target_combo_->setCurrentIndex(combo_index >= 0 ? combo_index : 0);
    QSet<QString> selected;
    if (is_multi)
        for (const QString& part : target.sliced(6).split(',', Qt::SkipEmptyParts))
            selected.insert(part);
    for (int row = 0; row < target_list_->count(); ++row)
    {
        auto* item = target_list_->item(row);
        if (item)
            item->setCheckState(selected.contains(item->data(Qt::UserRole).toString())
                                    ? Qt::Checked
                                    : Qt::Unchecked);
    }
}

void QtTransformWidget::save_active_pipeline()
{
    if (active_id_ == INVALID_FIGURE_ID || !target_combo_)
        return;
    if (pipeline_.step_count() == 0)
    {
        figure_pipelines_.erase(active_id_);
        return;
    }
    figure_pipelines_[active_id_] = {pipeline_, current_target_key()};
}

void QtTransformWidget::load_active_pipeline()
{
    pipeline_.clear();
    QString target = "all";
    if (const auto it = figure_pipelines_.find(active_id_); it != figure_pipelines_.end())
    {
        pipeline_ = it->second.pipeline;
        target    = QString::fromStdString(it->second.target);
    }
    rebuild_pipeline_list();
    if (target_combo_ && target_list_)
        apply_target_key(target.toStdString());
    update_preview();
    if (formula_edit_ && !formula_edit_->text().isEmpty())
        on_formula_changed(formula_edit_->text());
}

void QtTransformWidget::rebuild_pipeline_list()
{
    if (!pipeline_list_)
        return;
    pipeline_list_->blockSignals(true);
    pipeline_list_->clear();
    for (size_t index = 0; index < pipeline_.step_count(); ++index)
    {
        const auto& transform = pipeline_.step(index);
        QString     label =
            QString("%1. %2").arg(index + 1).arg(QString::fromStdString(transform.name()));
        if (!transform.available())
            label +=
                transform.source().empty()
                    ? " [Unavailable]"
                    : QString(" [Unavailable: %1]").arg(QString::fromStdString(transform.source()));
        auto* item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(pipeline_.is_enabled(index) ? Qt::Checked : Qt::Unchecked);
        if (!transform.available())
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip("The transform provider is not loaded; this step is preserved and "
                             "will be restored when it becomes available.");
        }
        pipeline_list_->addItem(item);
    }
    pipeline_list_->blockSignals(false);
}

std::optional<WorkspaceData::TransformState> QtTransformWidget::capture_pipeline_state(
    FigureId id,
    size_t   figure_index)
{
    if (id == active_id_)
        save_active_pipeline();
    const auto found = figure_pipelines_.find(id);
    if (found == figure_pipelines_.end() || found->second.pipeline.step_count() == 0)
        return std::nullopt;

    WorkspaceData::TransformState state;
    state.figure_index = figure_index;
    state.name         = found->second.pipeline.name();
    state.target       = found->second.target;
    state.all_visible  = found->second.target == "all";
    if (!state.all_visible && !QString::fromStdString(found->second.target).startsWith("multi:"))
    {
        const QStringList parts     = QString::fromStdString(found->second.target).split(':');
        bool              axes_ok   = false;
        bool              series_ok = false;
        if (parts.size() == 2 && parts[0] == "axes")
        {
            state.axes_index = parts[1].toULongLong(&axes_ok);
            state.axes_only  = axes_ok;
            series_ok        = axes_ok;
        }
        else if (parts.size() == 2)
        {
            state.axes_index   = parts[0].toULongLong(&axes_ok);
            state.series_index = parts[1].toULongLong(&series_ok);
        }
        if (!axes_ok || !series_ok)
            state.all_visible = true;
    }
    for (size_t index = 0; index < found->second.pipeline.step_count(); ++index)
    {
        const DataTransform&                transform = found->second.pipeline.step(index);
        const TransformParams&              params    = transform.params();
        WorkspaceData::TransformState::Step step;
        step.type            = static_cast<int>(transform.type());
        step.name            = transform.name();
        step.source          = transform.source();
        step.params_version  = 1;
        step.scale_factor    = params.scale_factor;
        step.offset_value    = params.offset_value;
        step.clamp_min       = params.clamp_min;
        step.clamp_max       = params.clamp_max;
        step.log_base        = params.log_base;
        step.skip_nan        = params.skip_nan;
        step.fft_db          = params.fft_db;
        step.fft_sample_rate = params.fft_sample_rate;
        step.enabled         = found->second.pipeline.is_enabled(index);
        switch (transform.type())
        {
            case TransformType::Scale:
                step.param = params.scale_factor;
                break;
            case TransformType::Offset:
                step.param = params.offset_value;
                break;
            default:
                break;
        }
        state.steps.push_back(std::move(step));
    }
    return state;
}

void QtTransformWidget::restore_pipeline_state(FigureId                             id,
                                               const WorkspaceData::TransformState& state)
{
    StoredPipeline stored;
    stored.pipeline.set_name(state.name);
    const bool legacy_axes_state = !state.all_visible && !state.axes_only && state.name.empty()
                                   && !state.steps.empty()
                                   && state.steps.front().params_version == 0;
    if (!state.target.empty())
        stored.target = state.target;
    else if (state.all_visible)
        stored.target = "all";
    else if (state.axes_only || legacy_axes_state)
        stored.target = QString("axes:%1").arg(state.axes_index).toStdString();
    else
        stored.target =
            QString("%1:%2").arg(state.axes_index).arg(state.series_index).toStdString();
    for (const auto& step : state.steps)
    {
        TransformParams params;
        if (step.params_version > 0)
        {
            params.scale_factor    = step.scale_factor;
            params.offset_value    = step.offset_value;
            params.clamp_min       = step.clamp_min;
            params.clamp_max       = step.clamp_max;
            params.log_base        = step.log_base;
            params.skip_nan        = step.skip_nan;
            params.fft_db          = step.fft_db;
            params.fft_sample_rate = step.fft_sample_rate;
        }
        else if (step.type == static_cast<int>(TransformType::Scale))
            params.scale_factor = step.param;
        else if (step.type == static_cast<int>(TransformType::Offset))
            params.offset_value = step.param;

        DataTransform transform;
        const auto    type = static_cast<TransformType>(step.type);
        if (type == TransformType::Custom)
        {
            if (step.name.empty()
                || !TransformRegistry::instance().get_transform(step.name, transform))
                transform = DataTransform::unavailable_custom(step.name, step.source);
        }
        else if (step.type < static_cast<int>(TransformType::Identity)
                 || step.type > static_cast<int>(TransformType::FFT))
            continue;
        else
            transform = TransformRegistry::create(type, params);
        stored.pipeline.push_back(std::move(transform));
        stored.pipeline.set_enabled(stored.pipeline.step_count() - 1, step.enabled);
    }
    if (stored.pipeline.step_count() == 0)
        figure_pipelines_.erase(id);
    else
        figure_pipelines_[id] = std::move(stored);
    if (id == active_id_)
        load_active_pipeline();
}

void QtTransformWidget::refresh()
{
    refresh_transform_list();
    refresh_targets();
}

void QtTransformWidget::refresh_targets()
{
    if (!target_combo_ || !target_list_)
        return;
    const std::string previous = current_target_key();
    QSignalBlocker    combo_blocker(target_combo_);
    QSignalBlocker    list_blocker(target_list_);
    target_combo_->clear();
    target_list_->clear();
    target_combo_->addItem("All Visible Series", "all");
    Figure* figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (figure)
    {
        for (size_t axes_index = 0; axes_index < figure->axes().size(); ++axes_index)
        {
            const auto* axes = figure->axes()[axes_index].get();
            if (!axes)
                continue;
            bool has_editable_series = false;
            for (const auto& series : axes->series())
            {
                EditableSeriesData data;
                if (series && capture_editable_series_data(*series, data))
                {
                    has_editable_series = true;
                    break;
                }
            }
            if (has_editable_series)
                target_combo_->addItem(QString("Axes %1 / All Editable Series").arg(axes_index + 1),
                                       QString("axes:%1").arg(axes_index));
            for (size_t series_index = 0; series_index < axes->series().size(); ++series_index)
            {
                const Series*      series = axes->series()[series_index].get();
                EditableSeriesData data;
                if (!series || !capture_editable_series_data(*series, data))
                    continue;
                QString label = QString("Axes %1 / %2")
                                    .arg(axes_index + 1)
                                    .arg(QString::fromStdString(series->label()));
                if (series->label().empty())
                    label =
                        QString("Axes %1 / Series %2").arg(axes_index + 1).arg(series_index + 1);
                const QString key = QString("%1:%2").arg(axes_index).arg(series_index);
                target_combo_->addItem(label, key);
                auto* item = new QListWidgetItem(label, target_list_);
                item->setData(Qt::UserRole, key);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            }
        }
    }
    apply_target_key(previous);
    update_preview();
    if (formula_edit_ && !formula_edit_->text().isEmpty())
        on_formula_changed(formula_edit_->text());
}

void QtTransformWidget::refresh_transform_list()
{
    if (!transform_combo_)
        return;

    transform_combo_->blockSignals(true);
    transform_combo_->clear();

    auto names = TransformRegistry::instance().available_transforms();
    for (const auto& n : names)
        transform_combo_->addItem(QString::fromStdString(n));

    transform_combo_->blockSignals(false);
    on_transform_selected(transform_combo_->currentIndex());
    resolve_available_pipeline_steps();

    // Refresh preset list
    if (preset_name_edit_)
    {
        QString current = preset_name_edit_->currentText();
        preset_name_edit_->blockSignals(true);
        preset_name_edit_->clear();
        auto presets = TransformRegistry::instance().saved_pipelines();
        for (const auto& p : presets)
            preset_name_edit_->addItem(QString::fromStdString(p));
        if (!current.isEmpty())
            preset_name_edit_->setEditText(current);
        preset_name_edit_->blockSignals(false);
    }
}

void QtTransformWidget::resolve_available_pipeline_steps()
{
    bool active_changed = false;
    for (auto& [figure_id, stored] : figure_pipelines_)
    {
        bool changed = false;
        for (size_t index = 0; index < stored.pipeline.step_count(); ++index)
        {
            auto& step = stored.pipeline.step_mut(index);
            if (step.type() != TransformType::Custom || step.available())
                continue;
            DataTransform resolved;
            if (!TransformRegistry::instance().get_transform(step.name(), resolved))
                continue;
            step    = std::move(resolved);
            changed = true;
        }
        active_changed = active_changed || (changed && figure_id == active_id_);
    }
    if (active_changed)
        load_active_pipeline();
}

void QtTransformWidget::on_transform_selected(int index)
{
    if (index < 0 || !transform_combo_)
    {
        if (description_label_)
            description_label_->setText("Select a transform to see description");
        return;
    }

    QString       name = transform_combo_->itemText(index);
    DataTransform xform;
    if (!TransformRegistry::instance().get_transform(name.toStdString(), xform))
    {
        description_label_->setText("Unknown transform");
        return;
    }

    description_label_->setText(QString::fromStdString(xform.description()));

    // Enable/disable parameter fields based on transform type
    bool needs_scale  = (xform.type() == TransformType::Scale);
    bool needs_offset = (xform.type() == TransformType::Offset);
    bool needs_clamp  = (xform.type() == TransformType::Clamp);
    bool needs_fft    = (xform.type() == TransformType::FFT);

    scale_spin_->setEnabled(needs_scale);
    offset_spin_->setEnabled(needs_offset);
    clamp_min_spin_->setEnabled(needs_clamp);
    clamp_max_spin_->setEnabled(needs_clamp);
    fft_db_check_->setEnabled(needs_fft);
    fft_sr_spin_->setEnabled(needs_fft);
    update_preview();
}

static TransformParams collect_params(const QDoubleSpinBox* scale,
                                      const QDoubleSpinBox* offset,
                                      const QDoubleSpinBox* clamp_min,
                                      const QDoubleSpinBox* clamp_max,
                                      const QCheckBox*      fft_db,
                                      const QDoubleSpinBox* fft_sr)
{
    TransformParams p;
    p.scale_factor    = static_cast<float>(scale->value());
    p.offset_value    = static_cast<float>(offset->value());
    p.clamp_min       = static_cast<float>(clamp_min->value());
    p.clamp_max       = static_cast<float>(clamp_max->value());
    p.fft_db          = fft_db->isChecked();
    p.fft_sample_rate = static_cast<float>(fft_sr->value());
    return p;
}

static DataTransform create_transform_from_ui(const QString& name, const TransformParams& params)
{
    DataTransform xform;
    if (!TransformRegistry::instance().get_transform(name.toStdString(), xform))
        return DataTransform(TransformType::Identity);

    // For built-in types, recreate with parameters
    if (xform.type() != TransformType::Custom)
    {
        xform = TransformRegistry::create(xform.type(), params);
    }
    return xform;
}

struct TransformTarget
{
    int                              axes_index   = -1;
    int                              series_index = -1;
    std::vector<std::pair<int, int>> series_targets;

    bool includes(size_t axes, size_t series) const
    {
        if (!series_targets.empty())
            return std::ranges::any_of(series_targets,
                                       [axes, series](const auto& target)
                                       {
                                           return static_cast<size_t>(target.first) == axes
                                                  && static_cast<size_t>(target.second) == series;
                                       });
        return axes_index < 0
               || (static_cast<size_t>(axes_index) == axes
                   && (series_index < 0 || static_cast<size_t>(series_index) == series));
    }
};

static TransformTarget selected_target(const QComboBox* combo, const QListWidget* list)
{
    TransformTarget target;
    if (list)
    {
        for (int row = 0; row < list->count(); ++row)
        {
            const auto* item = list->item(row);
            if (!item || item->checkState() != Qt::Checked)
                continue;
            const QStringList parts     = item->data(Qt::UserRole).toString().split(':');
            bool              axes_ok   = false;
            bool              series_ok = false;
            if (parts.size() == 2)
            {
                const int axes   = parts[0].toInt(&axes_ok);
                const int series = parts[1].toInt(&series_ok);
                if (axes_ok && series_ok && axes >= 0 && series >= 0)
                    target.series_targets.emplace_back(axes, series);
            }
        }
    }
    if (!target.series_targets.empty())
        return target;
    if (!combo || combo->currentData().toString() == "all")
        return target;
    const QStringList parts = combo->currentData().toString().split(':');
    if (parts.size() != 2)
        return target;
    if (parts[0] == "axes")
    {
        bool axes_ok      = false;
        target.axes_index = parts[1].toInt(&axes_ok);
        if (!axes_ok || target.axes_index < 0)
            return {};
        target.series_index = -1;
        return target;
    }
    bool axes_ok        = false;
    bool series_ok      = false;
    target.axes_index   = parts[0].toInt(&axes_ok);
    target.series_index = parts[1].toInt(&series_ok);
    if (!axes_ok || !series_ok || target.axes_index < 0 || target.series_index < 0)
        return {};
    return target;
}

static bool apply_transform_to_figure(Figure*               figure,
                                      const DataTransform&  xform,
                                      const TransformTarget target)
{
    if (!figure)
        return false;

    bool changed = false;
    for (size_t axes_index = 0; axes_index < figure->axes_mut().size(); ++axes_index)
    {
        auto& ax = figure->axes_mut()[axes_index];
        if (!ax)
            continue;
        bool axes_changed = false;
        for (size_t series_index = 0; series_index < ax->series_mut().size(); ++series_index)
        {
            auto& series_ptr = ax->series_mut()[series_index];
            if (!series_ptr || !series_ptr->visible())
                continue;
            if (!target.includes(axes_index, series_index))
                continue;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                xform.apply_y(ls->x_data(), ls->y_data(), rx, ry);
                EditableSeriesData current;
                capture_editable_series_data(*ls, current);
                if (current.x == rx && current.y == ry)
                    continue;
                current.x = std::move(rx);
                current.y = std::move(ry);
                if (xform.type() == TransformType::FFT)
                    current.x_offset = 0.0;
                restore_editable_series_data(*ls, current);
                changed      = true;
                axes_changed = true;
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                xform.apply_y(sc->x_data(), sc->y_data(), rx, ry);
                EditableSeriesData current;
                capture_editable_series_data(*sc, current);
                if (current.x == rx && current.y == ry)
                    continue;
                current.x = std::move(rx);
                current.y = std::move(ry);
                if (xform.type() == TransformType::FFT)
                    current.x_offset = 0.0;
                restore_editable_series_data(*sc, current);
                changed      = true;
                axes_changed = true;
            }
        }
        if (axes_changed)
            ax->auto_fit();
    }
    return changed;
}

void QtTransformWidget::update_preview()
{
    if (!preview_label_ || !registry_ || active_id_ == INVALID_FIGURE_ID)
        return;
    Figure* figure = registry_->get(active_id_);
    if (!figure)
    {
        preview_label_->setText("Preview unavailable");
        return;
    }
    const DataTransform   transform    = create_transform_from_ui(transform_combo_->currentText(),
                                                             collect_params(scale_spin_,
                                                                            offset_spin_,
                                                                            clamp_min_spin_,
                                                                            clamp_max_spin_,
                                                                            fft_db_check_,
                                                                            fft_sr_spin_));
    const TransformTarget target       = selected_target(target_combo_, target_list_);
    size_t                series_count = 0;
    size_t                point_count  = 0;
    float                 minimum      = std::numeric_limits<float>::infinity();
    float                 maximum      = -std::numeric_limits<float>::infinity();
    for (size_t axes_index = 0; axes_index < figure->axes().size(); ++axes_index)
    {
        const auto* axes = figure->axes()[axes_index].get();
        if (!axes)
            continue;
        for (size_t series_index = 0; series_index < axes->series().size(); ++series_index)
        {
            const auto* series = axes->series()[series_index].get();
            if (!series || !series->visible() || !target.includes(axes_index, series_index))
                continue;
            EditableSeriesData before;
            if (!capture_editable_series_data(*series, before))
                continue;
            std::vector<float> preview_x;
            std::vector<float> preview_y;
            transform.apply_y(before.x, before.y, preview_x, preview_y);
            ++series_count;
            point_count += preview_y.size();
            for (float value : preview_y)
            {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
    }
    if (series_count == 0 || point_count == 0)
    {
        preview_label_->setText("No editable points in target");
        return;
    }
    preview_label_->setText(QString("Preview: %1 series, %2 points, output Y [%3, %4]")
                                .arg(series_count)
                                .arg(point_count)
                                .arg(minimum, 0, 'g', 6)
                                .arg(maximum, 0, 'g', 6));
}

void QtTransformWidget::on_formula_changed(const QString& formula)
{
    const ExpressionInfo parsed = parse_expression(formula.trimmed().toStdString());
    if (!parsed.ast)
    {
        apply_formula_btn_->setEnabled(false);
        formula_status_->setText(
            formula.trimmed().isEmpty() ? "Enter a formula" : QString::fromStdString(parsed.error));
        return;
    }

    apply_formula_btn_->setEnabled(true);
    Figure* figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (!figure)
    {
        formula_status_->setText("Valid formula; no active figure");
        return;
    }
    std::vector<EditableSeriesData> reference_data;
    std::vector<std::string>        reference_labels;
    for (const auto& axes : figure->axes())
    {
        if (!axes)
            continue;
        for (const auto& series : axes->series())
        {
            EditableSeriesData data;
            if (series && capture_editable_series_data(*series, data))
            {
                reference_data.push_back(std::move(data));
                reference_labels.push_back(series->label());
            }
        }
    }
    const TransformTarget target = selected_target(target_combo_, target_list_);
    for (size_t axes_index = 0; axes_index < figure->axes().size(); ++axes_index)
    {
        const auto* axes = figure->axes()[axes_index].get();
        if (!axes)
            continue;
        for (size_t series_index = 0; series_index < axes->series().size(); ++series_index)
        {
            const auto* series = axes->series()[series_index].get();
            if (!series || !series->visible() || !target.includes(axes_index, series_index))
                continue;
            EditableSeriesData data;
            if (!capture_editable_series_data(*series, data) || data.x.empty() || data.y.empty())
                continue;
            ExprContext context;
            context.n = std::min(data.x.size(), data.y.size());
            for (size_t ref = 0; ref < reference_data.size(); ++ref)
                context.series_data.push_back(
                    {reference_data[ref].x, reference_data[ref].y, reference_labels[ref]});
            float  minimum      = std::numeric_limits<float>::infinity();
            float  maximum      = -std::numeric_limits<float>::infinity();
            size_t finite_count = 0;
            for (size_t index = 0; index < context.n; ++index)
            {
                context.x          = data.x[index];
                context.y          = data.y[index];
                context.t          = data.x[index];
                context.i          = index;
                const float result = evaluate(*parsed.ast, context);
                if (!std::isfinite(result))
                    continue;
                minimum = std::min(minimum, result);
                maximum = std::max(maximum, result);
                ++finite_count;
            }
            if (finite_count == 0)
                formula_status_->setText("Valid formula; preview contains no finite values");
            else
                formula_status_->setText(QString("Valid preview: %1 values, output Y [%2, %3]")
                                             .arg(context.n)
                                             .arg(minimum, 0, 'g', 6)
                                             .arg(maximum, 0, 'g', 6));
            return;
        }
    }
    formula_status_->setText("Valid formula; no editable points in target");
}

void QtTransformWidget::on_apply_formula()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;
    ExpressionInfo parsed = parse_expression(formula_edit_->text().trimmed().toStdString());
    if (!parsed.ast)
    {
        on_formula_changed(formula_edit_->text());
        return;
    }

    const TransformTarget       target = selected_target(target_combo_, target_list_);
    const auto                  ast    = std::shared_ptr<const ExprNode>(std::move(parsed.ast));
    QPointer<QtTransformWidget> self(this);
    undoable_edit_figure_data(
        undo_manager_,
        *registry_,
        active_id_,
        "Apply custom transform: " + formula_edit_->text().toStdString(),
        [ast, target](Figure& figure)
        {
            std::vector<EditableSeriesData> reference_data;
            std::vector<std::string>        reference_labels;
            for (const auto& axes : figure.axes())
            {
                if (!axes)
                    continue;
                for (const auto& series : axes->series())
                {
                    EditableSeriesData data;
                    if (series && capture_editable_series_data(*series, data))
                    {
                        reference_data.push_back(std::move(data));
                        reference_labels.push_back(series->label());
                    }
                }
            }

            bool changed = false;
            for (size_t axes_index = 0; axes_index < figure.axes_mut().size(); ++axes_index)
            {
                auto& axes = figure.axes_mut()[axes_index];
                if (!axes)
                    continue;
                bool axes_changed = false;
                for (size_t series_index = 0; series_index < axes->series_mut().size();
                     ++series_index)
                {
                    auto& series = axes->series_mut()[series_index];
                    if (!series || !series->visible() || !target.includes(axes_index, series_index))
                        continue;
                    EditableSeriesData before;
                    if (!capture_editable_series_data(*series, before))
                        continue;
                    EditableSeriesData after;
                    after.x        = before.x;
                    after.x_offset = before.x_offset;
                    after.y.resize(std::min(before.x.size(), before.y.size()));
                    after.x.resize(after.y.size());
                    ExprContext context;
                    context.n = after.y.size();
                    for (size_t ref = 0; ref < reference_data.size(); ++ref)
                        context.series_data.push_back(
                            {reference_data[ref].x, reference_data[ref].y, reference_labels[ref]});
                    for (size_t index = 0; index < context.n; ++index)
                    {
                        context.x      = before.x[index];
                        context.y      = before.y[index];
                        context.t      = before.x[index];
                        context.i      = index;
                        after.y[index] = evaluate(*ast, context);
                    }
                    if (before.x == after.x && before.y == after.y)
                        continue;
                    restore_editable_series_data(*series, after);
                    changed      = true;
                    axes_changed = true;
                }
                if (axes_changed)
                    axes->auto_fit();
            }
            return changed;
        },
        [redraw = redraw_, figure_id = active_id_, self]()
        {
            if (redraw)
                redraw->request_redraw(figure_id);
            if (self)
            {
                emit self->data_changed();
                self->on_formula_changed(self->formula_edit_->text());
            }
        });
}

void QtTransformWidget::on_apply_transform()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    QString name = transform_combo_->currentText();
    if (name.isEmpty())
        return;

    auto          params = collect_params(scale_spin_,
                                 offset_spin_,
                                 clamp_min_spin_,
                                 clamp_max_spin_,
                                 fft_db_check_,
                                 fft_sr_spin_);
    DataTransform xform  = create_transform_from_ui(name, params);
    const TransformTarget       target = selected_target(target_combo_, target_list_);
    QPointer<QtTransformWidget> self(this);
    undoable_edit_figure_data(
        undo_manager_,
        *registry_,
        active_id_,
        "Apply transform: " + name.toStdString(),
        [xform, target](Figure& figure)
        { return apply_transform_to_figure(&figure, xform, target); },
        [redraw = redraw_, figure_id = active_id_, self]()
        {
            if (redraw)
                redraw->request_redraw(figure_id);
            if (self)
            {
                emit self->data_changed();
                self->update_preview();
            }
        });

    SPECTRA_LOG_INFO("qt_transform", "Applied transform: {}", name.toStdString());
}

bool QtTransformWidget::apply_named_transform(const std::string& name)
{
    if (!transform_combo_ || !target_combo_ || active_id_ == INVALID_FIGURE_ID)
        return false;
    refresh_transform_list();
    const int index = transform_combo_->findText(QString::fromStdString(name));
    if (index < 0)
        return false;
    const int all_index = target_combo_->findData("all");
    if (all_index >= 0)
        target_combo_->setCurrentIndex(all_index);
    if (target_list_)
        for (int row = 0; row < target_list_->count(); ++row)
            target_list_->item(row)->setCheckState(Qt::Unchecked);
    transform_combo_->setCurrentIndex(index);
    on_apply_transform();
    return true;
}

void QtTransformWidget::focus_custom_formula()
{
    show();
    raise();
    if (formula_edit_)
        formula_edit_->setFocus(Qt::ShortcutFocusReason);
}

void QtTransformWidget::on_add_to_pipeline()
{
    QString name = transform_combo_->currentText();
    if (name.isEmpty())
        return;

    auto          params = collect_params(scale_spin_,
                                 offset_spin_,
                                 clamp_min_spin_,
                                 clamp_max_spin_,
                                 fft_db_check_,
                                 fft_sr_spin_);
    DataTransform xform  = create_transform_from_ui(name, params);
    pipeline_.push_back(xform);

    rebuild_pipeline_list();
    pipeline_list_->setCurrentRow(static_cast<int>(pipeline_.step_count() - 1));
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_remove_pipeline_step()
{
    int row = pipeline_list_->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= pipeline_.step_count())
        return;

    pipeline_.remove(static_cast<size_t>(row));
    rebuild_pipeline_list();
    if (pipeline_.step_count() > 0)
        pipeline_list_->setCurrentRow(std::min(row, static_cast<int>(pipeline_.step_count() - 1)));
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_move_pipeline_step_up()
{
    const int row = pipeline_list_->currentRow();
    if (row <= 0 || static_cast<size_t>(row) >= pipeline_.step_count())
        return;
    pipeline_.move_step(static_cast<size_t>(row), static_cast<size_t>(row - 1));
    rebuild_pipeline_list();
    pipeline_list_->setCurrentRow(row - 1);
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_move_pipeline_step_down()
{
    const int row = pipeline_list_->currentRow();
    if (row < 0 || static_cast<size_t>(row + 1) >= pipeline_.step_count())
        return;
    pipeline_.move_step(static_cast<size_t>(row), static_cast<size_t>(row + 1));
    rebuild_pipeline_list();
    pipeline_list_->setCurrentRow(row + 1);
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_pipeline_item_changed(QListWidgetItem* item)
{
    const int row = pipeline_list_->row(item);
    if (row < 0 || static_cast<size_t>(row) >= pipeline_.step_count())
        return;
    const bool enabled = item->checkState() == Qt::Checked;
    if (pipeline_.is_enabled(static_cast<size_t>(row)) == enabled)
        return;
    pipeline_.set_enabled(static_cast<size_t>(row), enabled);
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_clear_pipeline()
{
    pipeline_.clear();
    pipeline_list_->clear();
    save_active_pipeline();
    emit data_changed();
}

void QtTransformWidget::on_apply_pipeline()
{
    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
        return;

    Figure* figure = registry_->get(active_id_);
    if (!figure)
        return;

    if (pipeline_.is_identity())
        return;

    const TransformPipeline pipeline = pipeline_;
    const TransformTarget       target   = selected_target(target_combo_, target_list_);
    QPointer<QtTransformWidget> self(this);
    undoable_edit_figure_data(
        undo_manager_,
        *registry_,
        active_id_,
        "Apply transform pipeline",
        [pipeline, target](Figure& figure)
        {
            bool resets_x_offset = false;
            for (size_t index = 0; index < pipeline.step_count(); ++index)
                if (pipeline.is_enabled(index) && pipeline.step(index).type() == TransformType::FFT)
                    resets_x_offset = true;
            bool changed = false;
            for (size_t axes_index = 0; axes_index < figure.axes_mut().size(); ++axes_index)
            {
                auto& axes = figure.axes_mut()[axes_index];
                if (!axes)
                    continue;
                bool axes_changed = false;
                for (size_t series_index = 0; series_index < axes->series_mut().size();
                     ++series_index)
                {
                    auto& series = axes->series_mut()[series_index];
                    if (!series || !series->visible())
                        continue;
                    if (!target.includes(axes_index, series_index))
                        continue;

                    std::vector<float> x;
                    std::vector<float> y;
                    if (auto* line = dynamic_cast<LineSeries*>(series.get()))
                        pipeline.apply(line->x_data(), line->y_data(), x, y);
                    else if (auto* scatter = dynamic_cast<ScatterSeries*>(series.get()))
                        pipeline.apply(scatter->x_data(), scatter->y_data(), x, y);
                    else
                        continue;

                    EditableSeriesData current;
                    capture_editable_series_data(*series, current);
                    if (current.x == x && current.y == y)
                        continue;
                    current.x = std::move(x);
                    current.y = std::move(y);
                    if (resets_x_offset)
                        current.x_offset = 0.0;
                    restore_editable_series_data(*series, current);
                    changed      = true;
                    axes_changed = true;
                }
                if (axes_changed)
                    axes->auto_fit();
            }
            return changed;
        },
        [redraw = redraw_, figure_id = active_id_, self]()
        {
            if (redraw)
                redraw->request_redraw(figure_id);
            if (self)
            {
                emit self->data_changed();
                self->update_preview();
            }
        });

    SPECTRA_LOG_INFO("qt_transform", "Applied pipeline with {} steps", pipeline_.step_count());
}

void QtTransformWidget::on_save_pipeline_preset()
{
    QString name = preset_name_edit_->currentText().trimmed();
    if (name.isEmpty() || pipeline_.step_count() == 0)
        return;

    TransformRegistry::instance().save_pipeline(name.toStdString(), pipeline_);
    SPECTRA_LOG_INFO("qt_transform", "Saved pipeline preset: {}", name.toStdString());

    refresh_transform_list();
}

void QtTransformWidget::on_load_pipeline_preset()
{
    QString name = preset_name_edit_->currentText().trimmed();
    if (name.isEmpty())
        return;

    TransformPipeline loaded;
    if (!TransformRegistry::instance().load_pipeline(name.toStdString(), loaded))
        return;

    pipeline_ = std::move(loaded);
    rebuild_pipeline_list();
    save_active_pipeline();
    emit data_changed();

    SPECTRA_LOG_INFO("qt_transform",
                     "Loaded pipeline preset: {} ({} steps)",
                     name.toStdString(),
                     pipeline_.step_count());
}

}   // namespace spectra::adapters::qt
