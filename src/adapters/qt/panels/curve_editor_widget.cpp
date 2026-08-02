#include "curve_editor_widget.hpp"

#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/animation/timeline_editor.hpp"
#include "ui/commands/undo_manager.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace spectra::adapters::qt
{
namespace
{

QColor curve_color(Color color)
{
    return QColor::fromRgbF(std::clamp(color.r, 0.0f, 1.0f),
                            std::clamp(color.g, 0.0f, 1.0f),
                            std::clamp(color.b, 0.0f, 1.0f),
                            std::clamp(color.a, 0.0f, 1.0f));
}

}   // namespace

class QtCurveCanvas final : public QWidget
{
   public:
    explicit QtCurveCanvas(AnimationCurveEditor* editor, QWidget* parent = nullptr)
        : QWidget(parent), editor_(editor)
    {
        setObjectName("curve_canvas");
        setMinimumSize(320, 180);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    TimelineEditor* timeline = nullptr;
    std::function<void()> begin_edit;
    std::function<void(const std::string&)> commit_edit;
    std::function<void()> cancel_edit;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor background = palette().color(QPalette::Base);
        const QColor foreground = palette().color(QPalette::Text);
        const QColor grid = palette().color(QPalette::Mid);
        painter.fillRect(rect(), background);
        if (!editor_)
            return;

        auto& view    = editor_->view();
        view.origin_x = 46.0f;
        view.origin_y = 10.0f;
        view.width    = std::max(1.0f, static_cast<float>(width()) - 58.0f);
        view.height   = std::max(1.0f, static_cast<float>(height()) - 34.0f);
        const QRectF plot(view.origin_x, view.origin_y, view.width, view.height);
        painter.save();
        painter.setClipRect(plot);

        if (editor_->show_grid())
        {
            QPen grid_pen(grid);
            grid_pen.setWidthF(0.7);
            grid_pen.setColor(QColor(grid.red(), grid.green(), grid.blue(), 90));
            painter.setPen(grid_pen);
            for (int i = 0; i <= 10; ++i)
            {
                const qreal x = plot.left() + plot.width() * i / 10.0;
                painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            }
            for (int i = 0; i <= 8; ++i)
            {
                const qreal y = plot.top() + plot.height() * i / 8.0;
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            }
        }

        KeyframeInterpolator* interpolator = editor_->interpolator();
        if (interpolator)
        {
            for (const auto& [channel_id, channel] : interpolator->channels())
            {
                if (!editor_->is_channel_visible(channel_id) || channel.empty())
                    continue;
                QPen curve_pen(curve_color(editor_->channel_color(channel_id)));
                curve_pen.setWidthF(2.0);
                painter.setPen(curve_pen);
                QPainterPath curve;
                const int samples = std::clamp(static_cast<int>(view.width * 1.5f), 32, 1200);
                for (int sample = 0; sample < samples; ++sample)
                {
                    const float alpha = static_cast<float>(sample) / static_cast<float>(samples - 1);
                    const float time = view.time_min + (view.time_max - view.time_min) * alpha;
                    const QPointF point(view.time_to_x(time), view.value_to_y(channel.evaluate(time)));
                    if (sample == 0)
                        curve.moveTo(point);
                    else
                        curve.lineTo(point);
                }
                painter.drawPath(curve);

                for (const auto& keyframe : channel.keyframes())
                {
                    const QPointF key(view.time_to_x(keyframe.time), view.value_to_y(keyframe.value));
                    if (editor_->show_tangents() && keyframe.selected)
                    {
                        QPen tangent_pen(curve_pen.color());
                        tangent_pen.setWidthF(1.0);
                        painter.setPen(tangent_pen);
                        const QPointF in(view.time_to_x(keyframe.time + keyframe.in_tangent.dt),
                                         view.value_to_y(keyframe.value + keyframe.in_tangent.dv));
                        const QPointF out(view.time_to_x(keyframe.time + keyframe.out_tangent.dt),
                                          view.value_to_y(keyframe.value + keyframe.out_tangent.dv));
                        if (keyframe.in_tangent.dt != 0.0f || keyframe.in_tangent.dv != 0.0f)
                        {
                            painter.drawLine(key, in);
                            painter.drawEllipse(in, 3.0, 3.0);
                        }
                        if (keyframe.out_tangent.dt != 0.0f || keyframe.out_tangent.dv != 0.0f)
                        {
                            painter.drawLine(key, out);
                            painter.drawEllipse(out, 3.0, 3.0);
                        }
                    }
                    painter.setPen(QPen(curve_pen.color(), keyframe.selected ? 2.0 : 1.0));
                    painter.setBrush(keyframe.selected ? curve_pen.color() : background);
                    QPolygonF diamond;
                    diamond << QPointF(key.x(), key.y() - 5.0) << QPointF(key.x() + 5.0, key.y())
                            << QPointF(key.x(), key.y() + 5.0) << QPointF(key.x() - 5.0, key.y());
                    painter.drawPolygon(diamond);
                }
            }
        }

        painter.setPen(QPen(QColor(255, 90, 90, 210), 1.0));
        const float playhead_x = view.time_to_x(editor_->playhead_time());
        painter.drawLine(QPointF(playhead_x, plot.top()), QPointF(playhead_x, plot.bottom()));
        if (box_selecting_)
        {
            QPen selection_pen(palette().color(QPalette::Highlight), 1.0, Qt::DashLine);
            painter.setPen(selection_pen);
            QColor fill = selection_pen.color();
            fill.setAlpha(40);
            painter.setBrush(fill);
            painter.drawRect(QRectF(box_start_, box_current_).normalized());
        }
        painter.restore();

        painter.setPen(QPen(grid, 1.0));
        painter.drawRect(plot);
        painter.setPen(foreground);
        painter.drawText(QRectF(0, plot.top() - 2, 42, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(view.value_max, 'g', 4));
        painter.drawText(QRectF(0, plot.bottom() - 16, 42, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(view.value_min, 'g', 4));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 3, 80, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(view.time_min, 'g', 4) + "s");
        painter.drawText(QRectF(plot.right() - 80, plot.bottom() + 3, 80, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(view.time_max, 'g', 4) + "s");
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!editor_ || !timeline)
            return;
        if (event->button() == Qt::MiddleButton)
        {
            panning_      = true;
            last_mouse_   = event->position();
            event->accept();
            return;
        }
        if (event->button() != Qt::LeftButton)
            return;
        setFocus();
        const QPointF point = event->position();
        active_hit_ = editor_->hit_test(static_cast<float>(point.x()), static_cast<float>(point.y()));
        if (!(event->modifiers() & Qt::ShiftModifier))
            editor_->deselect_all();
        if (active_hit_.type == CurveHitType::Keyframe
            || active_hit_.type == CurveHitType::InTangent
            || active_hit_.type == CurveHitType::OutTangent)
            editor_->select_keyframe(active_hit_.channel_id, active_hit_.keyframe_index);
        if (active_hit_.type == CurveHitType::Keyframe
            || active_hit_.type == CurveHitType::InTangent
            || active_hit_.type == CurveHitType::OutTangent)
        {
            if (begin_edit)
                begin_edit();
            editor_->begin_drag(static_cast<float>(point.x()), static_cast<float>(point.y()));
        }
        else
        {
            box_selecting_ = true;
            box_start_     = point;
            box_current_   = point;
        }
        timeline->synchronize_markers_from_interpolator();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!editor_)
            return;
        const QPointF point = event->position();
        if (panning_ && (event->buttons() & Qt::MiddleButton))
        {
            const QPointF delta = point - last_mouse_;
            editor_->view().pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
            last_mouse_ = point;
            update();
            return;
        }
        if (!editor_->is_dragging() || !(event->buttons() & Qt::LeftButton))
        {
            if (box_selecting_ && (event->buttons() & Qt::LeftButton))
            {
                box_current_ = point;
                update();
            }
            return;
        }
        float x = static_cast<float>(point.x());
        if (active_hit_.type == CurveHitType::Keyframe && timeline)
        {
            const auto& view = editor_->view();
            x = std::clamp(x, view.time_to_x(0.0f), view.time_to_x(timeline->duration()));
        }
        editor_->update_drag(x, static_cast<float>(point.y()));
        timeline->synchronize_markers_from_interpolator();
        timeline->evaluate_at_playhead();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton)
        {
            panning_ = false;
            return;
        }
        if (event->button() != Qt::LeftButton || !editor_)
            return;
        if (box_selecting_)
        {
            box_current_ = event->position();
            const QRectF selection = QRectF(box_start_, box_current_).normalized();
            const auto& view       = editor_->view();
            editor_->select_keyframes_in_rect(view.x_to_time(static_cast<float>(selection.left())),
                                              view.x_to_time(static_cast<float>(selection.right())),
                                              view.y_to_value(static_cast<float>(selection.bottom())),
                                              view.y_to_value(static_cast<float>(selection.top())));
            if (timeline)
                timeline->synchronize_markers_from_interpolator();
            box_selecting_ = false;
            update();
            return;
        }
        if (!editor_->is_dragging())
            return;
        editor_->end_drag();
        if (timeline)
        {
            timeline->synchronize_markers_from_interpolator();
            timeline->evaluate_at_playhead();
        }
        if (commit_edit)
            commit_edit(active_hit_.type == CurveHitType::Keyframe ? "Edit animation curve keyframe"
                                                                   : "Edit animation curve tangent");
        active_hit_ = {};
        update();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!editor_)
            return;
        const float factor = std::pow(1.1f, static_cast<float>(event->angleDelta().y()) / 120.0f);
        editor_->view().zoom(factor,
                             static_cast<float>(event->position().x()),
                             static_cast<float>(event->position().y()));
        update();
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!editor_ || !timeline)
            return;
        if (event->key() == Qt::Key_Escape && editor_->is_dragging())
        {
            editor_->cancel_drag();
            if (cancel_edit)
                cancel_edit();
            update();
            return;
        }
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            && editor_->selected_count() > 0)
        {
            if (begin_edit)
                begin_edit();
            editor_->delete_selected();
            timeline->synchronize_markers_from_interpolator();
            if (commit_edit)
                commit_edit("Delete animation curve keyframes");
            update();
            return;
        }
        QWidget::keyPressEvent(event);
    }

   private:
    AnimationCurveEditor* editor_ = nullptr;
    CurveHitResult        active_hit_;
    bool                  panning_ = false;
    bool                  box_selecting_ = false;
    QPointF               last_mouse_;
    QPointF               box_start_;
    QPointF               box_current_;
};

QtCurveEditorWidget::QtCurveEditorWidget(TimelineEditor* timeline, QWidget* parent)
    : QDockWidget("Curve Editor", parent), timeline_(timeline)
{
    setObjectName("curve_editor_panel");
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    auto* content = new QWidget(this);
    auto* layout  = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* toolbar = new QHBoxLayout();
    auto* fit     = new QPushButton("Fit", content);
    fit->setObjectName("curve_fit");
    auto* reset = new QPushButton("Reset", content);
    reset->setObjectName("curve_reset");
    auto* grid = new QCheckBox("Grid", content);
    grid->setObjectName("curve_show_grid");
    grid->setChecked(true);
    auto* tangents = new QCheckBox("Tangents", content);
    tangents->setObjectName("curve_show_tangents");
    tangents->setChecked(true);
    toolbar->addWidget(fit);
    toolbar->addWidget(reset);
    toolbar->addWidget(grid);
    toolbar->addWidget(tangents);
    toolbar->addStretch();
    layout->addLayout(toolbar);
    canvas_ = new QtCurveCanvas(&editor_, content);
    canvas_->timeline = timeline_;
    canvas_->begin_edit = [this]() { begin_edit(); };
    canvas_->commit_edit = [this](const std::string& description) { commit_edit(description); };
    canvas_->cancel_edit = [this]() { cancel_edit(); };
    layout->addWidget(canvas_, 1);
    status_ = new QLabel(content);
    status_->setObjectName("curve_status");
    layout->addWidget(status_);
    setWidget(content);

    connect(fit, &QPushButton::clicked, this, &QtCurveEditorWidget::fit_view);
    connect(reset, &QPushButton::clicked, this, &QtCurveEditorWidget::reset_view);
    connect(grid, &QCheckBox::toggled, this, [this](bool checked)
            { editor_.set_show_grid(checked); canvas_->update(); });
    connect(tangents, &QCheckBox::toggled, this, [this](bool checked)
            { editor_.set_show_tangents(checked); canvas_->update(); });

    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &QtCurveEditorWidget::refresh);
    timer_->start();
    set_timeline(timeline);
}

QtCurveEditorWidget::~QtCurveEditorWidget() = default;

QWidget* QtCurveEditorWidget::curve_canvas() const
{
    return canvas_;
}

void QtCurveEditorWidget::set_timeline(TimelineEditor* timeline)
{
    timeline_         = timeline;
    canvas_->timeline = timeline;
    editor_.set_interpolator(timeline ? timeline->interpolator() : nullptr);
    setEnabled(timeline && timeline->interpolator());
    fit_view();
    refresh();
}

void QtCurveEditorWidget::refresh()
{
    if (!timeline_ || !timeline_->interpolator())
    {
        status_->setText("No active animation timeline.");
        canvas_->update();
        return;
    }
    editor_.set_playhead_time(timeline_->playhead());
    status_->setText(QString("%1 channel(s), %2 selected")
                         .arg(timeline_->interpolator()->channel_count())
                         .arg(editor_.selected_count()));
    canvas_->update();
}

void QtCurveEditorWidget::fit_view()
{
    editor_.fit_view();
    if (canvas_)
        canvas_->update();
}

void QtCurveEditorWidget::reset_view()
{
    editor_.reset_view();
    if (canvas_)
        canvas_->update();
}

void QtCurveEditorWidget::begin_edit()
{
    edit_before_ = timeline_ ? timeline_->serialize() : std::string{};
}

void QtCurveEditorWidget::commit_edit(const std::string& description)
{
    if (!timeline_ || edit_before_.empty())
        return;
    timeline_->synchronize_markers_from_interpolator();
    const std::string before = std::exchange(edit_before_, {});
    const std::string after  = timeline_->serialize();
    if (before == after)
        return;
    if (undo_)
    {
        TimelineEditor*               timeline = timeline_;
        QPointer<QtCurveEditorWidget> self(this);
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
    emit timeline_changed();
    refresh();
}

void QtCurveEditorWidget::cancel_edit()
{
    if (timeline_ && !edit_before_.empty())
        timeline_->deserialize(edit_before_);
    edit_before_.clear();
    emit timeline_changed();
    refresh();
}

}   // namespace spectra::adapters::qt
