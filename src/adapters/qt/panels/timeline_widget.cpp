// timeline_widget.cpp — Qt timeline / animation panel implementation.

#include "timeline_widget.hpp"

#include "timeline_property_binding.hpp"

#include "ui/animation/timeline_editor.hpp"
#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/commands/undo_manager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace spectra::adapters::qt
{
namespace
{

QString interpolation_name(InterpMode mode)
{
    switch (mode)
    {
        case InterpMode::Step:
            return "Step";
        case InterpMode::Linear:
            return "Linear";
        case InterpMode::CubicBezier:
            return "Cubic Bezier";
        case InterpMode::Spring:
            return "Spring";
        case InterpMode::EaseIn:
            return "Ease In";
        case InterpMode::EaseOut:
            return "Ease Out";
        case InterpMode::EaseInOut:
            return "Ease In/Out";
    }
    return "Linear";
}

}   // namespace

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
    ui_timer_->start();
}

void QtTimelineWidget::set_timeline(TimelineEditor* timeline)
{
    if (timeline_ == timeline)
    {
        refresh();
        return;
    }
    timeline_ = timeline;
    refresh();
}

void QtTimelineWidget::set_figure(Figure* figure)
{
    figure_ = figure;
    refresh();
}

void QtTimelineWidget::build_ui()
{
    auto* content = widget();
    auto* layout  = new QVBoxLayout(content);
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
    auto* scrub_row   = new QHBoxLayout();
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

    auto* track_edit_row = new QHBoxLayout();
    track_name_edit_     = new QLineEdit(content);
    track_name_edit_->setObjectName("timeline_track_name");
    track_name_edit_->setPlaceholderText("Track name");
    track_edit_row->addWidget(track_name_edit_);
    auto* add_track = new QPushButton("Add Track", content);
    add_track->setObjectName("timeline_add_track");
    track_edit_row->addWidget(add_track);
    auto* rename_track = new QPushButton("Rename", content);
    rename_track->setObjectName("timeline_rename_track");
    track_edit_row->addWidget(rename_track);
    auto* remove_track = new QPushButton("Remove", content);
    remove_track->setObjectName("timeline_remove_track");
    track_edit_row->addWidget(remove_track);
    layout->addLayout(track_edit_row);

    auto* track_state_row = new QHBoxLayout();
    track_visible_check_  = new QCheckBox("Visible", content);
    track_visible_check_->setObjectName("timeline_track_visible");
    track_state_row->addWidget(track_visible_check_);
    track_locked_check_ = new QCheckBox("Locked", content);
    track_locked_check_->setObjectName("timeline_track_locked");
    track_state_row->addWidget(track_locked_check_);
    track_state_row->addStretch();
    layout->addLayout(track_state_row);

    auto* property_row = new QHBoxLayout();
    property_row->addWidget(new QLabel("Property:", content));
    property_combo_ = new QComboBox(content);
    property_combo_->setObjectName("timeline_property_target");
    property_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    property_combo_->setMinimumContentsLength(24);
    property_row->addWidget(property_combo_, 1);
    auto* bind_property = new QPushButton("Bind", content);
    bind_property->setObjectName("timeline_bind_property");
    property_row->addWidget(bind_property);
    layout->addLayout(property_row);
    property_status_ = new QLabel("Select a track and model property.", content);
    property_status_->setObjectName("timeline_property_status");
    property_status_->setWordWrap(true);
    property_status_->setStyleSheet("color: gray;");
    layout->addWidget(property_status_);

    auto* keyframe_label = new QLabel("Keyframes:", content);
    layout->addWidget(keyframe_label);
    keyframe_list_ = new QListWidget(content);
    keyframe_list_->setObjectName("timeline_keyframes");
    keyframe_list_->setMaximumHeight(100);
    layout->addWidget(keyframe_list_);

    auto* keyframe_row  = new QHBoxLayout();
    keyframe_time_spin_ = new QDoubleSpinBox(content);
    keyframe_time_spin_->setObjectName("timeline_keyframe_time");
    keyframe_time_spin_->setDecimals(3);
    keyframe_time_spin_->setRange(0.0, 3600.0);
    keyframe_time_spin_->setSuffix("s");
    keyframe_row->addWidget(keyframe_time_spin_);
    auto* add_keyframe = new QPushButton("Add", content);
    add_keyframe->setObjectName("timeline_add_keyframe");
    keyframe_row->addWidget(add_keyframe);
    auto* move_keyframe = new QPushButton("Move", content);
    move_keyframe->setObjectName("timeline_move_keyframe");
    keyframe_row->addWidget(move_keyframe);
    auto* remove_keyframe = new QPushButton("Remove", content);
    remove_keyframe->setObjectName("timeline_remove_keyframe");
    keyframe_row->addWidget(remove_keyframe);
    layout->addLayout(keyframe_row);

    auto* value_row = new QHBoxLayout();
    value_row->addWidget(new QLabel("Value:", content));
    keyframe_value_spin_ = new QDoubleSpinBox(content);
    keyframe_value_spin_->setObjectName("timeline_keyframe_value");
    keyframe_value_spin_->setRange(-1e12, 1e12);
    keyframe_value_spin_->setDecimals(6);
    value_row->addWidget(keyframe_value_spin_);
    interpolation_combo_ = new QComboBox(content);
    interpolation_combo_->setObjectName("timeline_keyframe_interpolation");
    interpolation_combo_->addItems(
        {"Step", "Linear", "Cubic Bezier", "Spring", "Ease In", "Ease Out", "Ease In/Out"});
    interpolation_combo_->setCurrentIndex(static_cast<int>(InterpMode::Linear));
    value_row->addWidget(interpolation_combo_);
    auto* set_value = new QPushButton("Set Value / Mode", content);
    set_value->setObjectName("timeline_set_keyframe_value");
    value_row->addWidget(set_value);
    layout->addLayout(value_row);

    auto* tangent_mode_row = new QHBoxLayout();
    tangent_mode_row->addWidget(new QLabel("Tangents:", content));
    tangent_mode_combo_ = new QComboBox(content);
    tangent_mode_combo_->setObjectName("timeline_tangent_mode");
    tangent_mode_combo_->addItems({"Free", "Aligned", "Flat", "Auto"});
    tangent_mode_combo_->setCurrentIndex(static_cast<int>(TangentMode::Auto));
    tangent_mode_row->addWidget(tangent_mode_combo_);
    auto make_tangent_spin = [content](const char* name)
    {
        auto* spin = new QDoubleSpinBox(content);
        spin->setObjectName(name);
        spin->setRange(-1e6, 1e6);
        spin->setDecimals(5);
        return spin;
    };
    tangent_mode_row->addWidget(new QLabel("In dt/dv", content));
    in_tangent_dt_spin_ = make_tangent_spin("timeline_in_tangent_dt");
    in_tangent_dv_spin_ = make_tangent_spin("timeline_in_tangent_dv");
    tangent_mode_row->addWidget(in_tangent_dt_spin_);
    tangent_mode_row->addWidget(in_tangent_dv_spin_);
    layout->addLayout(tangent_mode_row);

    auto* tangent_value_row = new QHBoxLayout();
    tangent_value_row->addWidget(new QLabel("Out dt/dv", content));
    out_tangent_dt_spin_ = make_tangent_spin("timeline_out_tangent_dt");
    out_tangent_dv_spin_ = make_tangent_spin("timeline_out_tangent_dv");
    tangent_value_row->addWidget(out_tangent_dt_spin_);
    tangent_value_row->addWidget(out_tangent_dv_spin_);
    auto* set_tangents = new QPushButton("Set Tangents", content);
    set_tangents->setObjectName("timeline_set_tangents");
    tangent_value_row->addWidget(set_tangents);
    layout->addLayout(tangent_value_row);

    layout->addStretch();

    // ── Connections ───────────────────────────────────────────────────
    connect(play_btn_, &QPushButton::clicked, this, &QtTimelineWidget::on_play_clicked);
    connect(pause_btn_, &QPushButton::clicked, this, &QtTimelineWidget::on_pause_clicked);
    connect(stop_btn_, &QPushButton::clicked, this, &QtTimelineWidget::on_stop_clicked);
    connect(scrubber_, &QSlider::valueChanged, this, &QtTimelineWidget::on_scrub_changed);
    connect(duration_spin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            &QtTimelineWidget::on_duration_changed);
    connect(fps_spin_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &QtTimelineWidget::on_fps_changed);
    connect(loop_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtTimelineWidget::on_loop_mode_changed);
    connect(track_list_,
            &QListWidget::itemSelectionChanged,
            this,
            &QtTimelineWidget::on_track_selection_changed);
    connect(keyframe_list_,
            &QListWidget::itemSelectionChanged,
            this,
            &QtTimelineWidget::on_keyframe_selection_changed);
    connect(add_track, &QPushButton::clicked, this, &QtTimelineWidget::on_add_track);
    connect(rename_track, &QPushButton::clicked, this, &QtTimelineWidget::on_rename_track);
    connect(remove_track, &QPushButton::clicked, this, &QtTimelineWidget::on_remove_track);
    connect(add_keyframe, &QPushButton::clicked, this, &QtTimelineWidget::on_add_keyframe);
    connect(move_keyframe, &QPushButton::clicked, this, &QtTimelineWidget::on_move_keyframe);
    connect(remove_keyframe, &QPushButton::clicked, this, &QtTimelineWidget::on_remove_keyframe);
    connect(set_value, &QPushButton::clicked, this, &QtTimelineWidget::on_set_keyframe_value);
    connect(set_tangents, &QPushButton::clicked, this, &QtTimelineWidget::on_set_keyframe_tangents);
    connect(track_visible_check_,
            &QCheckBox::toggled,
            this,
            &QtTimelineWidget::on_track_visible_changed);
    connect(track_locked_check_,
            &QCheckBox::toggled,
            this,
            &QtTimelineWidget::on_track_locked_changed);
    connect(bind_property, &QPushButton::clicked, this, &QtTimelineWidget::on_bind_property);
    connect(property_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &QtTimelineWidget::on_property_selection_changed);
}

void QtTimelineWidget::refresh()
{
    if (!timeline_)
    {
        setEnabled(false);
        return;
    }
    setEnabled(true);

    if (figure_)
        bind_timeline_properties(*timeline_, *figure_);

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
    float dur       = timeline_->duration();
    int   scrub_max = static_cast<int>(dur * 100);   // 0.01s resolution
    scrubber_->setRange(0, scrub_max);
    scrubber_->setValue(static_cast<int>(timeline_->playhead() * 100));

    // Update track list
    const uint32_t previous_track = selected_track_id();
    QSignalBlocker block_tracks(track_list_);
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
        auto* item = new QListWidgetItem(label, track_list_);
        item->setData(Qt::UserRole, track.id);
        if (track.id == previous_track)
            item->setSelected(true);
    }
    if (!track_list_->currentItem() && track_list_->count() > 0)
        track_list_->setCurrentRow(0);
    refresh_keyframes();

    update_playback_state();
}

void QtTimelineWidget::on_tick()
{
    if (!timeline_)
        return;

    // The render/canvas loop owns animation progression. This timer only
    // reflects state changed by the production loop or semantic commands.
    QSignalBlocker block(scrubber_);
    scrubber_->setValue(static_cast<int>(timeline_->playhead() * 100));

    // Update time/frame labels
    float t = timeline_->playhead();
    time_label_->setText(QString("%1s").arg(t, 0, 'f', 2));
    frame_label_->setText(
        QString("Frame: %1 / %2").arg(timeline_->current_frame()).arg(timeline_->frame_count()));

    update_playback_state();
}

void QtTimelineWidget::on_play_clicked()
{
    if (timeline_)
    {
        timeline_->play();
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
        timeline_->stop();
    refresh();
}

void QtTimelineWidget::on_scrub_changed(int value)
{
    if (!timeline_)
        return;
    float time = static_cast<float>(value) / 100.0f;
    timeline_->scrub_to(time);
    timeline_->evaluate_at_playhead();
    time_label_->setText(QString("%1s").arg(time, 0, 'f', 2));
    keyframe_time_spin_->setValue(time);
    emit timeline_changed();
}

void QtTimelineWidget::on_duration_changed(double val)
{
    if (timeline_)
    {
        timeline_->set_duration(static_cast<float>(val));
        refresh();
        emit timeline_changed();
    }
}

void QtTimelineWidget::on_fps_changed(int val)
{
    if (timeline_)
    {
        timeline_->set_fps(static_cast<float>(val));
        emit timeline_changed();
    }
}

void QtTimelineWidget::on_loop_mode_changed(int index)
{
    if (!timeline_)
        return;
    timeline_->set_loop_mode(static_cast<LoopMode>(index));
    emit timeline_changed();
}

uint32_t QtTimelineWidget::selected_track_id() const
{
    const QListWidgetItem* item = track_list_ ? track_list_->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toUInt() : 0;
}

std::optional<float> QtTimelineWidget::selected_keyframe_time() const
{
    const QListWidgetItem* item = keyframe_list_ ? keyframe_list_->currentItem() : nullptr;
    return item ? std::optional<float>(item->data(Qt::UserRole).toFloat()) : std::nullopt;
}

void QtTimelineWidget::refresh_keyframes()
{
    if (!keyframe_list_)
        return;
    const auto     previous = selected_keyframe_time();
    QSignalBlocker block_list(keyframe_list_);
    QSignalBlocker block_visible(track_visible_check_);
    QSignalBlocker block_locked(track_locked_check_);
    QSignalBlocker block_value(keyframe_value_spin_);
    QSignalBlocker block_interp(interpolation_combo_);
    QSignalBlocker block_tangent_mode(tangent_mode_combo_);
    QSignalBlocker block_in_dt(in_tangent_dt_spin_);
    QSignalBlocker block_in_dv(in_tangent_dv_spin_);
    QSignalBlocker block_out_dt(out_tangent_dt_spin_);
    QSignalBlocker block_out_dv(out_tangent_dv_spin_);
    keyframe_list_->clear();
    const TimelineTrack* track = timeline_ ? timeline_->get_track(selected_track_id()) : nullptr;
    const bool           has_track = track != nullptr;
    track_visible_check_->setEnabled(has_track);
    track_locked_check_->setEnabled(has_track);
    keyframe_value_spin_->setEnabled(has_track && timeline_->interpolator());
    interpolation_combo_->setEnabled(has_track && timeline_->interpolator());
    tangent_mode_combo_->setEnabled(has_track && timeline_->interpolator());
    in_tangent_dt_spin_->setEnabled(has_track && timeline_->interpolator());
    in_tangent_dv_spin_->setEnabled(has_track && timeline_->interpolator());
    out_tangent_dt_spin_->setEnabled(has_track && timeline_->interpolator());
    out_tangent_dv_spin_->setEnabled(has_track && timeline_->interpolator());
    if (!track)
    {
        refresh_property_targets();
        return;
    }
    track_name_edit_->setText(QString::fromStdString(track->name));
    track_visible_check_->setChecked(track->visible);
    track_locked_check_->setChecked(track->locked);
    keyframe_time_spin_->setMaximum(timeline_->duration());
    const AnimationChannel* channel =
        timeline_->interpolator() ? timeline_->interpolator()->channel(track->id) : nullptr;
    for (const auto& keyframe : track->keyframes)
    {
        const TypedKeyframe* typed = channel ? channel->find_keyframe(keyframe.time) : nullptr;
        const QString        label = typed ? QString("%1s : %2  [%3]")
                                          .arg(keyframe.time, 0, 'f', 3)
                                          .arg(typed->value, 0, 'g', 7)
                                          .arg(interpolation_name(typed->interp))
                                           : QString("%1s").arg(keyframe.time, 0, 'f', 3);
        auto*                item  = new QListWidgetItem(label, keyframe_list_);
        item->setData(Qt::UserRole, keyframe.time);
        if (previous && std::abs(*previous - keyframe.time) < 0.0001f)
            item->setSelected(true);
    }
    refresh_property_targets();
}

void QtTimelineWidget::refresh_property_targets()
{
    if (!property_combo_ || !property_status_)
        return;
    const TimelineTrack* track = timeline_ ? timeline_->get_track(selected_track_id()) : nullptr;
    const std::string    selected_path = track ? track->property_path : std::string{};
    const auto           targets =
        figure_ ? timeline_property_targets(*figure_) : std::vector<TimelinePropertyTarget>{};

    QSignalBlocker blocker(property_combo_);
    property_combo_->clear();
    property_combo_->addItem("None", QString{});
    int selected_index = selected_path.empty() ? 0 : -1;
    for (const auto& target : targets)
    {
        property_combo_->addItem(QString::fromStdString(target.label),
                                 QString::fromStdString(target.path));
        if (target.path == selected_path)
            selected_index = property_combo_->count() - 1;
    }
    bool unavailable = false;
    if (selected_index < 0 && !selected_path.empty())
    {
        property_combo_->addItem(
            QString("Unavailable: %1").arg(QString::fromStdString(selected_path)),
            QString::fromStdString(selected_path));
        selected_index = property_combo_->count() - 1;
        unavailable    = true;
    }
    property_combo_->setCurrentIndex(std::max(selected_index, 0));
    property_combo_->setEnabled(track && figure_);

    if (!track)
        property_status_->setText("Select a track and model property.");
    else if (!figure_)
        property_status_->setText("No figure is available for property binding.");
    else if (selected_path.empty())
        property_status_->setText("Unbound — this track only stores keyframe values.");
    else if (unavailable)
        property_status_->setText("Target is missing; the binding path is preserved for recovery.");
    else
        property_status_->setText("Bound — scrubbing and playback apply values to the figure.");
}

void QtTimelineWidget::on_track_selection_changed()
{
    refresh_keyframes();
}

void QtTimelineWidget::on_keyframe_selection_changed()
{
    if (const auto selected = selected_keyframe_time())
    {
        keyframe_time_spin_->setValue(*selected);
        const auto* interpolator = timeline_ ? timeline_->interpolator() : nullptr;
        const auto* channel  = interpolator ? interpolator->channel(selected_track_id()) : nullptr;
        const auto* keyframe = channel ? channel->find_keyframe(*selected) : nullptr;
        QSignalBlocker block_value(keyframe_value_spin_);
        QSignalBlocker block_interp(interpolation_combo_);
        QSignalBlocker block_tangent_mode(tangent_mode_combo_);
        QSignalBlocker block_in_dt(in_tangent_dt_spin_);
        QSignalBlocker block_in_dv(in_tangent_dv_spin_);
        QSignalBlocker block_out_dt(out_tangent_dt_spin_);
        QSignalBlocker block_out_dv(out_tangent_dv_spin_);
        keyframe_value_spin_->setEnabled(keyframe != nullptr);
        interpolation_combo_->setEnabled(keyframe != nullptr);
        tangent_mode_combo_->setEnabled(keyframe != nullptr);
        in_tangent_dt_spin_->setEnabled(keyframe != nullptr);
        in_tangent_dv_spin_->setEnabled(keyframe != nullptr);
        out_tangent_dt_spin_->setEnabled(keyframe != nullptr);
        out_tangent_dv_spin_->setEnabled(keyframe != nullptr);
        if (keyframe)
        {
            keyframe_value_spin_->setValue(keyframe->value);
            interpolation_combo_->setCurrentIndex(static_cast<int>(keyframe->interp));
            tangent_mode_combo_->setCurrentIndex(static_cast<int>(keyframe->tangent_mode));
            in_tangent_dt_spin_->setValue(keyframe->in_tangent.dt);
            in_tangent_dv_spin_->setValue(keyframe->in_tangent.dv);
            out_tangent_dt_spin_->setValue(keyframe->out_tangent.dt);
            out_tangent_dv_spin_->setValue(keyframe->out_tangent.dv);
        }
    }
}

void QtTimelineWidget::perform_mutation(const std::string&           description,
                                        const std::function<void()>& mutation)
{
    if (!timeline_ || !mutation)
        return;
    const std::string before = timeline_->serialize();
    mutation();
    const std::string after = timeline_->serialize();
    if (before == after)
        return;
    if (undo_)
    {
        TimelineEditor*            timeline = timeline_;
        QPointer<QtTimelineWidget> self(this);
        undo_->push({description,
                     [timeline, before, self]()
                     {
                         timeline->deserialize(before);
                         if (self)
                         {
                             self->refresh();
                             emit self->timeline_changed();
                         }
                     },
                     [timeline, after, self]()
                     {
                         timeline->deserialize(after);
                         if (self)
                         {
                             self->refresh();
                             emit self->timeline_changed();
                         }
                     }});
    }
    refresh();
    emit timeline_changed();
}

void QtTimelineWidget::on_add_track()
{
    const std::string name   = track_name_edit_->text().trimmed().toStdString();
    uint32_t          new_id = 0;
    perform_mutation(
        "Add timeline track",
        [this, name, &new_id]()
        {
            new_id = timeline_->add_animated_track(
                name.empty() ? "Track " + std::to_string(timeline_->track_count() + 1) : name,
                static_cast<float>(keyframe_value_spin_->value()));
        });
    for (int row = 0; row < track_list_->count(); ++row)
        if (track_list_->item(row)->data(Qt::UserRole).toUInt() == new_id)
            track_list_->setCurrentRow(row);
}

void QtTimelineWidget::on_rename_track()
{
    const uint32_t    id   = selected_track_id();
    const std::string name = track_name_edit_->text().trimmed().toStdString();
    if (id == 0 || name.empty())
        return;
    perform_mutation("Rename timeline track",
                     [this, id, name]() { timeline_->rename_track(id, name); });
}

void QtTimelineWidget::on_remove_track()
{
    const uint32_t id = selected_track_id();
    if (id != 0)
        perform_mutation("Remove timeline track", [this, id]() { timeline_->remove_track(id); });
}

void QtTimelineWidget::on_add_keyframe()
{
    const uint32_t id   = selected_track_id();
    const float    time = static_cast<float>(keyframe_time_spin_->value());
    if (id != 0)
        perform_mutation("Add timeline keyframe",
                         [this, id, time]()
                         {
                             timeline_->add_animated_keyframe(
                                 id,
                                 time,
                                 static_cast<float>(keyframe_value_spin_->value()),
                                 interpolation_combo_->currentIndex());
                         });
}

void QtTimelineWidget::on_move_keyframe()
{
    const uint32_t id       = selected_track_id();
    const auto     old_time = selected_keyframe_time();
    const float    new_time = static_cast<float>(keyframe_time_spin_->value());
    if (id != 0 && old_time)
        perform_mutation("Move timeline keyframe",
                         [this, id, old = *old_time, new_time]()
                         { timeline_->move_keyframe(id, old, new_time); });
}

void QtTimelineWidget::on_remove_keyframe()
{
    const uint32_t id   = selected_track_id();
    const auto     time = selected_keyframe_time();
    if (id != 0 && time)
        perform_mutation("Remove timeline keyframe",
                         [this, id, value = *time]() { timeline_->remove_keyframe(id, value); });
}

void QtTimelineWidget::on_set_keyframe_value()
{
    const uint32_t id   = selected_track_id();
    const auto     time = selected_keyframe_time();
    if (id == 0 || !time)
        return;
    const float value = static_cast<float>(keyframe_value_spin_->value());
    const int   mode  = interpolation_combo_->currentIndex();
    perform_mutation("Edit timeline keyframe value",
                     [this, id, key_time = *time, value, mode]()
                     { timeline_->set_animated_keyframe(id, key_time, value, mode); });
}

void QtTimelineWidget::on_set_keyframe_tangents()
{
    const uint32_t id   = selected_track_id();
    const auto     time = selected_keyframe_time();
    if (id == 0 || !time)
        return;
    const int   mode   = tangent_mode_combo_->currentIndex();
    const float in_dt  = static_cast<float>(in_tangent_dt_spin_->value());
    const float in_dv  = static_cast<float>(in_tangent_dv_spin_->value());
    const float out_dt = static_cast<float>(out_tangent_dt_spin_->value());
    const float out_dv = static_cast<float>(out_tangent_dv_spin_->value());
    perform_mutation(
        "Edit timeline keyframe tangents",
        [this, id, key_time = *time, mode, in_dt, in_dv, out_dt, out_dv]() {
            timeline_
                ->set_animated_keyframe_tangents(id, key_time, mode, in_dt, in_dv, out_dt, out_dv);
        });
}

void QtTimelineWidget::on_track_visible_changed(bool visible)
{
    const uint32_t id = selected_track_id();
    if (id != 0)
        perform_mutation("Change timeline track visibility",
                         [this, id, visible]() { timeline_->set_track_visible(id, visible); });
}

void QtTimelineWidget::on_track_locked_changed(bool locked)
{
    const uint32_t id = selected_track_id();
    if (id != 0)
        perform_mutation("Change timeline track lock",
                         [this, id, locked]() { timeline_->set_track_locked(id, locked); });
}

void QtTimelineWidget::on_bind_property()
{
    const uint32_t id = selected_track_id();
    if (!timeline_ || !figure_ || id == 0 || !property_combo_)
        return;
    const std::string path = property_combo_->currentData().toString().toStdString();
    perform_mutation("Bind timeline property",
                     [this, id, path]() { timeline_->set_track_property_path(id, path); });
    bind_timeline_property(*timeline_, *figure_, id);
    timeline_->evaluate_at_playhead();
    refresh_property_targets();
    emit timeline_changed();
}

void QtTimelineWidget::on_property_selection_changed(int index)
{
    if (!figure_ || index <= 0 || !property_combo_ || !keyframe_value_spin_)
        return;
    const std::string path    = property_combo_->itemData(index).toString().toStdString();
    const auto        targets = timeline_property_targets(*figure_);
    const auto        target  = std::find_if(targets.begin(),
                                     targets.end(),
                                     [&path](const auto& item) { return item.path == path; });
    if (target == targets.end())
        return;
    keyframe_value_spin_->setRange(target->minimum, target->maximum);
    const TimelineTrack* track = timeline_ ? timeline_->get_track(selected_track_id()) : nullptr;
    if (track && track->keyframes.empty())
        keyframe_value_spin_->setValue(target->current_value);
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
    frame_label_->setText(
        QString("Frame: %1 / %2").arg(timeline_->current_frame()).arg(timeline_->frame_count()));
}

}   // namespace spectra::adapters::qt
