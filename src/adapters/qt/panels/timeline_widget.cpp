// timeline_widget.cpp — Qt timeline / animation panel implementation.

#include "timeline_widget.hpp"

#include "ui/animation/timeline_editor.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

QtTimelineWidget::QtTimelineWidget(TimelineEditor* timeline, QWidget* parent)
    : QDockWidget("Timeline", parent), timeline_(timeline)
{
    setObjectName("timeline_panel");
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* content = new QWidget(this);
    setWidget(content);

    build_ui();
    refresh();

    // UI update timer — 30 Hz for smooth scrubber during playback
    ui_timer_ = new QTimer(this);
    ui_timer_->setInterval(33);
    connect(ui_timer_, &QTimer::timeout, this, &QtTimelineWidget::on_tick);
    last_tick_time_ = std::chrono::steady_clock::now();
}

void QtTimelineWidget::build_ui()
{
    auto* content = widget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ── Playback buttons ──────────────────────────────────────────────
    auto* btn_row = new QHBoxLayout();

    play_btn_ = new QPushButton("Play", content);
    play_btn_->setObjectName("timeline_play");
    btn_row->addWidget(play_btn_);

    pause_btn_ = new QPushButton("Pause", content);
    pause_btn_->setObjectName("timeline_pause");
    pause_btn_->setEnabled(false);
    btn_row->addWidget(pause_btn_);

    stop_btn_ = new QPushButton("Stop", content);
    stop_btn_->setObjectName("timeline_stop");
    btn_row->addWidget(stop_btn_);

    layout->addLayout(btn_row);

    // ── Scrubber ──────────────────────────────────────────────────────
    auto* scrub_row = new QHBoxLayout();
    auto* scrub_label = new QLabel("Time:", content);
    scrub_row->addWidget(scrub_label);

    scrubber_ = new QSlider(Qt::Horizontal, content);
    scrubber_->setObjectName("timeline_scrubber");
    scrubber_->setRange(0, 1000);
    scrub_row->addWidget(scrubber_);

    time_label_ = new QLabel("0.00s", content);
    time_label_->setMinimumWidth(60);
    scrub_row->addWidget(time_label_);

    layout->addLayout(scrub_row);

    // ── Duration / FPS / Loop ─────────────────────────────────────────
    auto* settings_row = new QHBoxLayout();

    auto* dur_label = new QLabel("Dur:", content);
    settings_row->addWidget(dur_label);

    duration_spin_ = new QDoubleSpinBox(content);
    duration_spin_->setObjectName("timeline_duration");
    duration_spin_->setRange(0.1, 3600.0);
    duration_spin_->setDecimals(2);
    duration_spin_->setSuffix("s");
    settings_row->addWidget(duration_spin_);

    auto* fps_label = new QLabel("FPS:", content);
    settings_row->addWidget(fps_label);

    fps_spin_ = new QSpinBox(content);
    fps_spin_->setObjectName("timeline_fps");
    fps_spin_->setRange(1, 240);
    settings_row->addWidget(fps_spin_);

    auto* loop_label = new QLabel("Loop:", content);
    settings_row->addWidget(loop_label);

    loop_combo_ = new QComboBox(content);
    loop_combo_->setObjectName("timeline_loop");
    loop_combo_->addItems({"None", "Loop", "Ping-Pong"});
    settings_row->addWidget(loop_combo_);

    layout->addLayout(settings_row);

    // ── Frame info ────────────────────────────────────────────────────
    frame_label_ = new QLabel("Frame: 0 / 0", content);
    frame_label_->setStyleSheet("color: gray;");
    layout->addWidget(frame_label_);

    // ── Track list ────────────────────────────────────────────────────
    auto* track_label = new QLabel("Tracks:", content);
    layout->addWidget(track_label);

    track_list_ = new QListWidget(content);
    track_list_->setObjectName("timeline_tracks");
    track_list_->setMaximumHeight(120);
    layout->addWidget(track_list_);

    layout->addStretch();

    // ── Connections ───────────────────────────────────────────────────
    connect(play_btn_, &QPushButton::clicked,
            this, &QtTimelineWidget::on_play_clicked);
    connect(pause_btn_, &QPushButton::clicked,
            this, &QtTimelineWidget::on_pause_clicked);
    connect(stop_btn_, &QPushButton::clicked,
            this, &QtTimelineWidget::on_stop_clicked);
    connect(scrubber_, &QSlider::valueChanged,
            this, &QtTimelineWidget::on_scrub_changed);
    connect(duration_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &QtTimelineWidget::on_duration_changed);
    connect(fps_spin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &QtTimelineWidget::on_fps_changed);
    connect(loop_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &QtTimelineWidget::on_loop_mode_changed);
}

void QtTimelineWidget::refresh()
{
    if (!timeline_)
    {
        setEnabled(false);
        return;
    }
    setEnabled(true);

    // Block signals while updating controls
    QSignalBlocker block_scrub(scrubber_);
    QSignalBlocker block_dur(duration_spin_);
    QSignalBlocker block_fps(fps_spin_);
    QSignalBlocker block_loop(loop_combo_);

    duration_spin_->setValue(timeline_->duration());
    fps_spin_->setValue(static_cast<int>(timeline_->fps()));

    int loop_idx = static_cast<int>(timeline_->loop_mode());
    loop_combo_->setCurrentIndex(loop_idx);

    // Update scrubber range based on duration
    float dur = timeline_->duration();
    int scrub_max = static_cast<int>(dur * 100);  // 0.01s resolution
    scrubber_->setRange(0, scrub_max);
    scrubber_->setValue(static_cast<int>(timeline_->playhead() * 100));

    // Update track list
    track_list_->clear();
    for (const auto& track : timeline_->tracks())
    {
        QString label = QString("%1  [%2 kf]")
                            .arg(QString::fromStdString(track.name))
                            .arg(track.keyframes.size());
        if (!track.visible)
            label += "  [hidden]";
        if (track.locked)
            label += "  [locked]";
        track_list_->addItem(label);
    }

    update_playback_state();
}

void QtTimelineWidget::on_tick()
{
    if (!timeline_)
        return;

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_tick_time_).count();
    last_tick_time_ = now;

    if (timeline_->is_playing())
    {
        timeline_->advance(dt);

        // Update scrubber without triggering signal
        QSignalBlocker block(scrubber_);
        scrubber_->setValue(static_cast<int>(timeline_->playhead() * 100));
    }

    // Update time/frame labels
    float t = timeline_->playhead();
    time_label_->setText(QString("%1s").arg(t, 0, 'f', 2));
    frame_label_->setText(QString("Frame: %1 / %2")
                               .arg(timeline_->current_frame())
                               .arg(timeline_->frame_count()));

    update_playback_state();
}

void QtTimelineWidget::on_play_clicked()
{
    if (timeline_)
    {
        timeline_->play();
        if (!ui_timer_->isActive())
        {
            last_tick_time_ = std::chrono::steady_clock::now();
            ui_timer_->start();
        }
    }
    update_playback_state();
}

void QtTimelineWidget::on_pause_clicked()
{
    if (timeline_)
        timeline_->pause();
    update_playback_state();
}

void QtTimelineWidget::on_stop_clicked()
{
    if (timeline_)
    {
        timeline_->stop();
        if (ui_timer_->isActive())
            ui_timer_->stop();
    }
    refresh();
}

void QtTimelineWidget::on_scrub_changed(int value)
{
    if (!timeline_)
        return;
    float time = static_cast<float>(value) / 100.0f;
    timeline_->scrub_to(time);
    time_label_->setText(QString("%1s").arg(time, 0, 'f', 2));
}

void QtTimelineWidget::on_duration_changed(double val)
{
    if (timeline_)
    {
        timeline_->set_duration(static_cast<float>(val));
        refresh();
    }
}

void QtTimelineWidget::on_fps_changed(int val)
{
    if (timeline_)
        timeline_->set_fps(static_cast<float>(val));
}

void QtTimelineWidget::on_loop_mode_changed(int index)
{
    if (!timeline_)
        return;
    timeline_->set_loop_mode(static_cast<LoopMode>(index));
}

void QtTimelineWidget::update_playback_state()
{
    if (!timeline_)
        return;

    bool playing = timeline_->is_playing();
    play_btn_->setEnabled(!playing);
    pause_btn_->setEnabled(playing);

    float t = timeline_->playhead();
    time_label_->setText(QString("%1s").arg(t, 0, 'f', 2));
    frame_label_->setText(QString("Frame: %1 / %2")
                               .arg(timeline_->current_frame())
                               .arg(timeline_->frame_count()));
}

}   // namespace spectra::adapters::qt
