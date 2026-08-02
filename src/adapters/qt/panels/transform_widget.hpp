#pragma once

#include <spectra/fwd.hpp>
#include "math/data_transform.hpp"
#include "ui/workspace/workspace.hpp"

#include <QDockWidget>
#include <QString>

#include <optional>
#include <string>
#include <unordered_map>

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QCheckBox;
class QLabel;
class QLineEdit;

namespace spectra
{
class RedrawRequest;
class UndoManager;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtTransformWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtTransformWidget(FigureRegistry*           registry,
                               ::spectra::UndoManager*   undo_manager = nullptr,
                               ::spectra::RedrawRequest* redraw       = nullptr,
                               QWidget*                  parent       = nullptr);
    ~QtTransformWidget() override = default;

    void                                         set_active_figure(FigureId id);
    std::optional<WorkspaceData::TransformState> capture_pipeline_state(FigureId id,
                                                                        size_t   figure_index);
    void restore_pipeline_state(FigureId id, const WorkspaceData::TransformState& state);
    bool apply_named_transform(const std::string& name);
    void focus_custom_formula();

   public slots:
    void refresh();
    void refresh_transform_list();

   signals:
    void data_changed();

   private slots:
    void on_apply_transform();
    void on_add_to_pipeline();
    void on_clear_pipeline();
    void on_apply_pipeline();
    void on_save_pipeline_preset();
    void on_load_pipeline_preset();
    void on_transform_selected(int index);
    void on_remove_pipeline_step();
    void on_move_pipeline_step_up();
    void on_move_pipeline_step_down();
    void on_pipeline_item_changed(QListWidgetItem* item);
    void refresh_targets();
    void update_preview();
    void on_formula_changed(const QString& formula);
    void on_apply_formula();

   private:
    void        build_ui();
    void        save_active_pipeline();
    void        load_active_pipeline();
    void        rebuild_pipeline_list();
    void        resolve_available_pipeline_steps();
    std::string current_target_key() const;
    void        apply_target_key(const std::string& key);

    struct StoredPipeline
    {
        TransformPipeline pipeline;
        std::string       target = "all";
    };

    FigureRegistry*           registry_     = nullptr;
    ::spectra::UndoManager*   undo_manager_ = nullptr;
    ::spectra::RedrawRequest* redraw_       = nullptr;
    FigureId                  active_id_    = INVALID_FIGURE_ID;

    QComboBox*   transform_combo_   = nullptr;
    QComboBox*   target_combo_      = nullptr;
    QListWidget* target_list_       = nullptr;
    QLabel*      description_label_ = nullptr;
    QLabel*      preview_label_     = nullptr;
    QLineEdit*   formula_edit_      = nullptr;
    QLabel*      formula_status_    = nullptr;
    QPushButton* apply_formula_btn_ = nullptr;

    QDoubleSpinBox* scale_spin_     = nullptr;
    QDoubleSpinBox* offset_spin_    = nullptr;
    QDoubleSpinBox* clamp_min_spin_ = nullptr;
    QDoubleSpinBox* clamp_max_spin_ = nullptr;
    QCheckBox*      fft_db_check_   = nullptr;
    QDoubleSpinBox* fft_sr_spin_    = nullptr;

    QPushButton* apply_btn_           = nullptr;
    QPushButton* add_to_pipeline_btn_ = nullptr;

    QListWidget* pipeline_list_      = nullptr;
    QPushButton* move_step_up_btn_   = nullptr;
    QPushButton* move_step_down_btn_ = nullptr;
    QPushButton* remove_step_btn_    = nullptr;
    QPushButton* clear_pipeline_btn_ = nullptr;
    QPushButton* apply_pipeline_btn_ = nullptr;

    QComboBox*   preset_name_edit_ = nullptr;
    QPushButton* save_preset_btn_  = nullptr;
    QPushButton* load_preset_btn_  = nullptr;

    TransformPipeline                            pipeline_;
    std::unordered_map<FigureId, StoredPipeline> figure_pipelines_;
};

}   // namespace spectra::adapters::qt
