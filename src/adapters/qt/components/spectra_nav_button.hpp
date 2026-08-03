#pragma once

// SpectraNavButton — single navigation rail button with icon + label.
//
// A 1:1 port of the legacy `icon_label_button` cell used by
// `ui/shell/nav_rail.cpp`: an interaction-driven glass pill with a layered
// outer glow, a top inner highlight, and an icon-above-label stack. Hover and
// active states are eased instead of snapping, matching the legacy
// exponential smoothing.

#include <QPushButton>

class QLabel;
class QVariantAnimation;

namespace spectra::adapters::qt
{

class SpectraNavButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hover_progress WRITE set_hover_progress)
    Q_PROPERTY(qreal activeProgress READ active_progress WRITE set_active_progress)

   public:
    explicit SpectraNavButton(const QString& icon_codepoint,
                              const QString& label,
                              const QString& shortcut_hint,
                              QWidget*       parent = nullptr);
    ~SpectraNavButton() override                         = default;
    SpectraNavButton(const SpectraNavButton&)            = delete;
    SpectraNavButton& operator=(const SpectraNavButton&) = delete;

    void set_active(bool active);
    bool is_active() const { return active_; }

    void  set_compact_mode(bool compact);
    QSize sizeHint() const override;

    qreal hover_progress() const { return hover_t_; }
    void  set_hover_progress(qreal t);
    qreal active_progress() const { return active_t_; }
    void  set_active_progress(qreal t);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

   private:
    // Legacy cell scale: the rail shrinks cells on short windows but floors the
    // icon and label sizes so they stay legible.
    float cell_scale() const;

    QString            icon_codepoint_;
    QString            label_;
    QString            shortcut_hint_;
    bool               active_      = false;
    bool               compact_     = false;
    qreal              hover_t_     = 0.0;
    qreal              active_t_    = 0.0;
    QVariantAnimation* hover_anim_  = nullptr;
    QVariantAnimation* active_anim_ = nullptr;
};

}   // namespace spectra::adapters::qt
