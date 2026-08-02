#pragma once

// SpectraNavButton — single navigation rail button with icon + label.
//
// Features:
// - Icon (drawn via icon font codepoint or custom paint)
// - Label text below icon
// - Active state with cyan border/glow
// - Keyboard focus state
// - Tooltips containing shortcuts

#include <QPushButton>

class QLabel;

namespace spectra::adapters::qt
{

class SpectraNavButton : public QPushButton
{
    Q_OBJECT
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

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QString icon_codepoint_;
    QString label_;
    QString shortcut_hint_;
    bool    active_  = false;
    bool    compact_ = false;
};

}   // namespace spectra::adapters::qt
