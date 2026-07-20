#pragma once

#include <spectra/fwd.hpp>
#include "math/data_transform.hpp"

#include <QDockWidget>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPushButton;
class QCheckBox;
class QLabel;

namespace spectra::adapters::qt
{

class QtTransformWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit QtTransformWidget(FigureRegistry* registry, QWidget* parent = nullptr);
    ~QtTransformWidget() override = default;

    void set_active_figure(FigureId id);

public slots:
    void refresh();
    void refresh_transform_list();

private slots:
    void on_apply_transform();
    void on_add_to_pipeline();
    void on_clear_pipeline();
    void on_apply_pipeline();
    void on_save_pipeline_preset();
    void on_load_pipeline_preset();
    void on_transform_selected(int index);
    void on_remove_pipeline_step();

private:
    void build_ui();

    FigureRegistry* registry_ = nullptr;
    FigureId        active_id_ = INVALID_FIGURE_ID;

    QComboBox*    transform_combo_      = nullptr;
    QLabel*       description_label_    = nullptr;

    QDoubleSpinBox* scale_spin_    = nullptr;
    QDoubleSpinBox* offset_spin_   = nullptr;
    QDoubleSpinBox* clamp_min_spin_ = nullptr;
    QDoubleSpinBox* clamp_max_spin_ = nullptr;
    QCheckBox*      fft_db_check_  = nullptr;
    QDoubleSpinBox* fft_sr_spin_   = nullptr;

    QPushButton* apply_btn_         = nullptr;
    QPushButton* add_to_pipeline_btn_ = nullptr;

    QListWidget* pipeline_list_     = nullptr;
    QPushButton* remove_step_btn_   = nullptr;
    QPushButton* clear_pipeline_btn_ = nullptr;
    QPushButton* apply_pipeline_btn_ = nullptr;

    QComboBox*    preset_name_edit_  = nullptr;
    QPushButton*  save_preset_btn_   = nullptr;
    QPushButton*  load_preset_btn_   = nullptr;

    TransformPipeline pipeline_;
};

}   // namespace spectra::adapters::qt
