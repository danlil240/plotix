#pragma once

#include <QDockWidget>

#include "ui/animation/animation_curve_editor.hpp"

#include <string>

class QCheckBox;
class QLabel;
class QTimer;
class QWidget;

namespace spectra
{
class TimelineEditor;
class UndoManager;
}

namespace spectra::adapters::qt
{

class QtCurveCanvas;

class QtCurveEditorWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtCurveEditorWidget(TimelineEditor* timeline, QWidget* parent = nullptr);
    ~QtCurveEditorWidget() override;

    QtCurveEditorWidget(const QtCurveEditorWidget&)            = delete;
    QtCurveEditorWidget& operator=(const QtCurveEditorWidget&) = delete;

    AnimationCurveEditor* curve_editor() { return &editor_; }
    QWidget*              curve_canvas() const;

   public slots:
    void set_timeline(TimelineEditor* timeline);
    void set_undo_manager(UndoManager* undo) { undo_ = undo; }
    void refresh();
    void fit_view();
    void reset_view();

   signals:
    void timeline_changed();

   private:
    void begin_edit();
    void commit_edit(const std::string& description);
    void cancel_edit();

    TimelineEditor*      timeline_ = nullptr;
    UndoManager*         undo_     = nullptr;
    AnimationCurveEditor editor_;
    QtCurveCanvas*       canvas_ = nullptr;
    QLabel*              status_ = nullptr;
    QTimer*              timer_  = nullptr;
    std::string          edit_before_;
};

}   // namespace spectra::adapters::qt
