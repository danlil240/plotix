// transform_widget.cpp — Qt transform/editor panel implementation.

#include "transform_widget.hpp"

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
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace spectra::adapters::qt
{

QtTransformWidget::QtTransformWidget(FigureRegistry* registry, QWidget* parent)
    : QDockWidget("Transforms", parent), registry_(registry)
{
    setObjectName("transform_panel");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    build_ui();
    refresh_transform_list();
}

void QtTransformWidget::build_ui()
{
    auto* container = new QWidget(this);
    auto* main_layout = new QVBoxLayout(container);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    // ── Transform selector ─────────────────────────────────────────────
    auto* xform_group = new QGroupBox("Available Transforms", container);
    auto* xform_layout = new QVBoxLayout(xform_group);

    transform_combo_ = new QComboBox(xform_group);
    transform_combo_->setObjectName("transform_combo");
    xform_layout->addWidget(transform_combo_);

    description_label_ = new QLabel("Select a transform to see description", xform_group);
    description_label_->setWordWrap(true);
    description_label_->setStyleSheet("color: gray; font-size: 11px;");
    xform_layout->addWidget(description_label_);

    // ── Parameters ──────────────────────────────────────────────────────
    auto* param_group = new QGroupBox("Parameters", xform_group);
    auto* param_form = new QFormLayout(param_group);

    scale_spin_ = new QDoubleSpinBox(param_group);
    scale_spin_->setRange(-1e9, 1e9);
    scale_spin_->setDecimals(4);
    scale_spin_->setValue(1.0);
    param_form->addRow("Scale Factor", scale_spin_);

    offset_spin_ = new QDoubleSpinBox(param_group);
    offset_spin_->setRange(-1e9, 1e9);
    offset_spin_->setDecimals(4);
    offset_spin_->setValue(0.0);
    param_form->addRow("Offset Value", offset_spin_);

    clamp_min_spin_ = new QDoubleSpinBox(param_group);
    clamp_min_spin_->setRange(-1e9, 1e9);
    clamp_min_spin_->setDecimals(4);
    clamp_min_spin_->setValue(0.0);
    param_form->addRow("Clamp Min", clamp_min_spin_);

    clamp_max_spin_ = new QDoubleSpinBox(param_group);
    clamp_max_spin_->setRange(-1e9, 1e9);
    clamp_max_spin_->setDecimals(4);
    clamp_max_spin_->setValue(1.0);
    param_form->addRow("Clamp Max", clamp_max_spin_);

    fft_db_check_ = new QCheckBox("Output in dB", param_group);
    param_form->addRow("FFT dB", fft_db_check_);

    fft_sr_spin_ = new QDoubleSpinBox(param_group);
    fft_sr_spin_->setRange(0.0, 1e9);
    fft_sr_spin_->setDecimals(2);
    fft_sr_spin_->setValue(0.0);
    param_form->addRow("FFT Sample Rate", fft_sr_spin_);

    xform_layout->addWidget(param_group);

    // ── Action buttons ──────────────────────────────────────────────────
    auto* btn_layout = new QHBoxLayout();
    apply_btn_ = new QPushButton("Apply", xform_group);
    apply_btn_->setObjectName("apply_transform_btn");
    add_to_pipeline_btn_ = new QPushButton("Add to Pipeline", xform_group);
    btn_layout->addWidget(apply_btn_);
    btn_layout->addWidget(add_to_pipeline_btn_);
    xform_layout->addLayout(btn_layout);

    main_layout->addWidget(xform_group);

    // ── Pipeline ────────────────────────────────────────────────────────
    auto* pipeline_group = new QGroupBox("Pipeline", container);
    auto* pipeline_layout = new QVBoxLayout(pipeline_group);

    pipeline_list_ = new QListWidget(pipeline_group);
    pipeline_list_->setObjectName("pipeline_list");
    pipeline_list_->setMaximumHeight(120);
    pipeline_layout->addWidget(pipeline_list_);

    auto* pipe_btn_layout = new QHBoxLayout();
    remove_step_btn_ = new QPushButton("Remove", pipeline_group);
    clear_pipeline_btn_ = new QPushButton("Clear", pipeline_group);
    apply_pipeline_btn_ = new QPushButton("Apply Pipeline", pipeline_group);
    pipe_btn_layout->addWidget(remove_step_btn_);
    pipe_btn_layout->addWidget(clear_pipeline_btn_);
    pipe_btn_layout->addWidget(apply_pipeline_btn_);
    pipeline_layout->addLayout(pipe_btn_layout);

    main_layout->addWidget(pipeline_group);

    // ── Presets ─────────────────────────────────────────────────────────
    auto* preset_group = new QGroupBox("Saved Pipelines", container);
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
    connect(transform_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtTransformWidget::on_transform_selected);
    connect(apply_btn_, &QPushButton::clicked, this, &QtTransformWidget::on_apply_transform);
    connect(add_to_pipeline_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_add_to_pipeline);
    connect(remove_step_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_remove_pipeline_step);
    connect(clear_pipeline_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_clear_pipeline);
    connect(apply_pipeline_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_apply_pipeline);
    connect(save_preset_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_save_pipeline_preset);
    connect(load_preset_btn_, &QPushButton::clicked,
            this, &QtTransformWidget::on_load_pipeline_preset);
}

void QtTransformWidget::set_active_figure(FigureId id)
{
    if (active_id_ == id)
        return;
    active_id_ = id;
}

void QtTransformWidget::refresh()
{
    refresh_transform_list();
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

void QtTransformWidget::on_transform_selected(int index)
{
    if (index < 0 || !transform_combo_)
    {
        if (description_label_)
            description_label_->setText("Select a transform to see description");
        return;
    }

    QString name = transform_combo_->itemText(index);
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
}

static TransformParams collect_params(const QDoubleSpinBox* scale,
                                       const QDoubleSpinBox* offset,
                                       const QDoubleSpinBox* clamp_min,
                                       const QDoubleSpinBox* clamp_max,
                                       const QCheckBox* fft_db,
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

static DataTransform create_transform_from_ui(const QString& name,
                                               const TransformParams& params)
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

static void apply_transform_to_figure(Figure* figure, const DataTransform& xform)
{
    if (!figure)
        return;

    for (auto& ax : figure->axes_mut())
    {
        if (!ax)
            continue;
        for (auto& series_ptr : ax->series_mut())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                xform.apply_y(ls->x_data(), ls->y_data(), rx, ry);
                ls->set_x(rx).set_y(ry);
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                xform.apply_y(sc->x_data(), sc->y_data(), rx, ry);
                sc->set_x(rx).set_y(ry);
            }
        }
        ax->auto_fit();
    }
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

    auto params = collect_params(scale_spin_, offset_spin_, clamp_min_spin_,
                                  clamp_max_spin_, fft_db_check_, fft_sr_spin_);
    DataTransform xform = create_transform_from_ui(name, params);
    apply_transform_to_figure(figure, xform);

    SPECTRA_LOG_INFO("qt_transform", "Applied transform: {}", name.toStdString());
}

void QtTransformWidget::on_add_to_pipeline()
{
    QString name = transform_combo_->currentText();
    if (name.isEmpty())
        return;

    auto params = collect_params(scale_spin_, offset_spin_, clamp_min_spin_,
                                  clamp_max_spin_, fft_db_check_, fft_sr_spin_);
    DataTransform xform = create_transform_from_ui(name, params);
    pipeline_.push_back(xform);

    // Update list widget
    QString entry = QString("%1. %2").arg(pipeline_.step_count()).arg(name);
    pipeline_list_->addItem(entry);
}

void QtTransformWidget::on_remove_pipeline_step()
{
    int row = pipeline_list_->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= pipeline_.step_count())
        return;

    pipeline_.remove(static_cast<size_t>(row));
    delete pipeline_list_->takeItem(row);

    // Re-number remaining items
    for (int i = 0; i < pipeline_list_->count(); ++i)
    {
        QString text = pipeline_list_->item(i)->text();
        int dot_pos = text.indexOf('.');
        if (dot_pos > 0)
            pipeline_list_->item(i)->setText(QString("%1.%2").arg(i + 1).arg(text.mid(dot_pos + 1)));
    }
}

void QtTransformWidget::on_clear_pipeline()
{
    pipeline_.clear();
    pipeline_list_->clear();
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

    for (auto& ax : figure->axes_mut())
    {
        if (!ax)
            continue;
        for (auto& series_ptr : ax->series_mut())
        {
            if (!series_ptr || !series_ptr->visible())
                continue;

            if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                pipeline_.apply(ls->x_data(), ls->y_data(), rx, ry);
                ls->set_x(rx).set_y(ry);
            }
            else if (auto* sc = dynamic_cast<ScatterSeries*>(series_ptr.get()))
            {
                std::vector<float> rx, ry;
                pipeline_.apply(sc->x_data(), sc->y_data(), rx, ry);
                sc->set_x(rx).set_y(ry);
            }
        }
        ax->auto_fit();
    }

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
    pipeline_list_->clear();

    for (size_t i = 0; i < pipeline_.step_count(); ++i)
    {
        const auto& step = pipeline_.step(i);
        QString entry = QString("%1. %2").arg(i + 1).arg(QString::fromStdString(step.name()));
        pipeline_list_->addItem(entry);
    }

    SPECTRA_LOG_INFO("qt_transform", "Loaded pipeline preset: {} ({} steps)",
                     name.toStdString(), pipeline_.step_count());
}

}   // namespace spectra::adapters::qt
