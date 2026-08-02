#pragma once

// QtTimelineWidget — dockable timeline / animation panel for the Qt frontend.
//
// Provides playback controls (play/pause/stop), a scrubber slider, duration
// and FPS spinboxes, loop mode selection, and keyframe track listing.
// Wraps the framework-neutral TimelineEditor — no duplicated business logic.

#include <QDockWidget>

#include <spectra/fwd.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class QSlider;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QListWidget;
class QTimer;
class QLineEdit;
class QCheckBox;

namespace spectra
{
class UndoManager;
}

namespace spectra::adapters::qt
{

class QtTimelineWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtTimelineWidget(TimelineEditor* timeline, QWidget* parent = nullptr);
    ~QtTimelineWidget() override = default;

    QtTimelineWidget(const QtTimelineWidget&)            = delete;
    QtTimelineWidget& operator=(const QtTimelineWidget&) = delete;

   public slots:
    void set_timeline(TimelineEditor* timeline);
    void set_figure(Figure* figure);
    void set_undo_manager(UndoManager* undo) { undo_ = undo; }
    void refresh();
    void on_tick();

   private slots:
    void on_play_clicked();
    void on_pause_clicked();
    void on_stop_clicked();
    void on_scrub_changed(int value);
    void on_duration_changed(double val);
    void on_fps_changed(int val);
    void on_loop_mode_changed(int index);
    void on_track_selection_changed();
    void on_keyframe_selection_changed();
    void on_add_track();
    void on_rename_track();
    void on_remove_track();
    void on_add_keyframe();
    void on_move_keyframe();
    void on_remove_keyframe();
    void on_set_keyframe_value();
    void on_set_keyframe_tangents();
    void on_track_visible_changed(bool visible);
    void on_track_locked_changed(bool locked);
    void on_bind_property();
    void on_property_selection_changed(int index);

   signals:
    void timeline_changed();

   private:
    void                 build_ui();
    void                 update_playback_state();
    void                 refresh_keyframes();
    void                 refresh_property_targets();
    uint32_t             selected_track_id() const;
    std::optional<float> selected_keyframe_time() const;
    void perform_mutation(const std::string& description, const std::function<void()>& mutation);

    TimelineEditor* timeline_ = nullptr;
    Figure*         figure_   = nullptr;
    UndoManager*    undo_     = nullptr;

    // Playback controls
    QPushButton* play_btn_  = nullptr;
    QPushButton* pause_btn_ = nullptr;
    QPushButton* stop_btn_  = nullptr;

    // Scrubber
    QSlider* scrubber_ = nullptr;

    // Time / FPS
    QDoubleSpinBox* duration_spin_ = nullptr;
    QSpinBox*       fps_spin_      = nullptr;
    QLabel*         time_label_    = nullptr;
    QLabel*         frame_label_   = nullptr;

    // Loop mode
    QComboBox* loop_combo_ = nullptr;

    // Track list
    QListWidget*    track_list_          = nullptr;
    QListWidget*    keyframe_list_       = nullptr;
    QLineEdit*      track_name_edit_     = nullptr;
    QDoubleSpinBox* keyframe_time_spin_  = nullptr;
    QDoubleSpinBox* keyframe_value_spin_ = nullptr;
    QComboBox*      interpolation_combo_ = nullptr;
    QComboBox*      tangent_mode_combo_  = nullptr;
    QDoubleSpinBox* in_tangent_dt_spin_  = nullptr;
    QDoubleSpinBox* in_tangent_dv_spin_  = nullptr;
    QDoubleSpinBox* out_tangent_dt_spin_ = nullptr;
    QDoubleSpinBox* out_tangent_dv_spin_ = nullptr;
    QCheckBox*      track_visible_check_ = nullptr;
    QCheckBox*      track_locked_check_  = nullptr;
    QComboBox*      property_combo_      = nullptr;
    QLabel*         property_status_     = nullptr;

    // Animation timer for UI updates during playback
    QTimer* ui_timer_ = nullptr;
};

}   // namespace spectra::adapters::qt
