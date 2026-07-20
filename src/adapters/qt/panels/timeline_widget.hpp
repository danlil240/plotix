#pragma once

// QtTimelineWidget — dockable timeline / animation panel for the Qt frontend.
//
// Provides playback controls (play/pause/stop), a scrubber slider, duration
// and FPS spinboxes, loop mode selection, and keyframe track listing.
// Wraps the framework-neutral TimelineEditor — no duplicated business logic.

#include <QDockWidget>

#include <spectra/fwd.hpp>

#include <chrono>

class QSlider;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QListWidget;
class QTimer;

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

   private:
    void build_ui();
    void update_playback_state();

    TimelineEditor* timeline_ = nullptr;

    // Playback controls
    QPushButton* play_btn_   = nullptr;
    QPushButton* pause_btn_  = nullptr;
    QPushButton* stop_btn_   = nullptr;

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
    QListWidget* track_list_ = nullptr;

    // Animation timer for UI updates during playback
    QTimer* ui_timer_ = nullptr;

    std::chrono::steady_clock::time_point last_tick_time_;
};

}   // namespace spectra::adapters::qt
