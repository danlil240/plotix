// spectra_menu_strip.cpp — Custom menu button strip implementation.

#include "spectra_menu_strip.hpp"
#include "spectra_design_tokens.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QHash>
#include <QHBoxLayout>
#include <QMenu>
#include <QPoint>
#include <QSizePolicy>
#include <QString>
#include <QTimer>

namespace
{
constexpr int k_menu_exit_threshold = 5;
}   // namespace

namespace spectra::adapters::qt
{

// ─── SpectraMenuButton ───────────────────────────────────────────────────────

SpectraMenuButton::SpectraMenuButton(const QString& label, QMenu* menu, QWidget* parent)
    : QPushButton(label, parent), menu_(menu)
{
    setFlat(true);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    QString object_label = label.toLower();
    object_label.replace(' ', '_');
    setObjectName("spectra_menu_" + object_label);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(34);

    auto& fm = SpectraFontManager::instance();
    setFont(fm.font_menubar());

    setStyleSheet(QString("QPushButton {"
                          "  background: transparent;"
                          "  border: none;"
                          "  border-radius: %1px;"
                          "  color: #C7D6EB;"
                          "  padding: 0 15px;"
                          "  text-align: left;"
                          "}"
                          "QPushButton:hover {"
                          "  background: rgba(26, 35, 50, 180);"
                          "  color: #EDF0F7;"
                          "}"
                          "QPushButton:pressed {"
                          "  background: #222D3F;"
                          "}")
                      .arg(spectra_geometry().radius_md));

    if (menu_)
        connect(this, &QPushButton::clicked, this, &SpectraMenuButton::popup_menu);
}

QMenu* SpectraMenuButton::menu() const
{
    return menu_;
}

void SpectraMenuButton::popup_menu()
{
    if (menu_)
    {
        QPoint pos(0, height());
        menu_->popup(mapToGlobal(pos));
    }
}

void SpectraMenuButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    if (auto* strip = qobject_cast<SpectraMenuStrip*>(parentWidget()))
        strip->on_button_entered(this);
}

// ─── SpectraMenuStrip ────────────────────────────────────────────────────────

SpectraMenuStrip::~SpectraMenuStrip() = default;

SpectraMenuStrip::SpectraMenuStrip(QWidget* parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)), hover_timer_(new QTimer(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);

    hover_timer_->setInterval(30);
    hover_timer_->setSingleShot(false);
    connect(hover_timer_, &QTimer::timeout, this, &SpectraMenuStrip::on_hover_poll);
}

void SpectraMenuStrip::add_menu_button(const QString& label, QMenu* menu)
{
    auto* btn = new SpectraMenuButton(label, menu, this);
    // Widths are the measured legacy ImGui menu advances minus the six-pixel
    // inter-item gap. This keeps every label on the same x coordinate even
    // though Qt and ImGui use different text rasterizers.
    static const QHash<QString, int> k_label_widths = {{"File", 52},
                                                       {"Edit", 53},
                                                       {"View", 59},
                                                       {"Tools", 62},
                                                       {"Plot", 53},
                                                       {"Data", 55},
                                                       {"Axes", 60}};
    if (auto it = k_label_widths.find(label); it != k_label_widths.end())
        btn->setFixedWidth(it.value());
    layout_->addWidget(btn);
    buttons_.append(btn);

    if (menu)
    {
        connect(menu, &QMenu::aboutToShow, this, &SpectraMenuStrip::on_menu_about_to_show);
        connect(menu, &QMenu::aboutToHide, this, &SpectraMenuStrip::on_menu_about_to_hide);
    }
}

void SpectraMenuStrip::on_button_entered(SpectraMenuButton* button)
{
    if (switching_)
        return;
    if (!open_menu_)
        return;
    if (open_button_ == button)
        return;
    if (!button || !button->menu())
        return;

    switching_         = true;
    menu_exit_counter_ = 0;

    SpectraMenuButton* old_button = open_button_;
    open_menu_->close();
    button->popup_menu();

    // The active popup can prevent normal enter/leave events from reaching
    // the QPushButton children, so update their hover state explicitly.
    if (old_button && old_button != button)
    {
        old_button->setAttribute(Qt::WA_UnderMouse, false);
        old_button->update();
    }
    if (is_cursor_over_button(button))
    {
        button->setAttribute(Qt::WA_UnderMouse, true);
        button->update();
    }

    switching_ = false;
}

SpectraMenuButton* SpectraMenuStrip::button_for_menu(QMenu* menu) const
{
    for (auto* btn : buttons_)
        if (btn->menu() == menu)
            return btn;
    return nullptr;
}

bool SpectraMenuStrip::is_cursor_over_button(SpectraMenuButton* button) const
{
    if (!button)
        return false;
    QRect global_rect = button->rect();
    global_rect.moveTopLeft(button->mapToGlobal(global_rect.topLeft()));
    return global_rect.contains(QCursor::pos());
}

bool SpectraMenuStrip::is_cursor_in_top_zone(const QPoint& cursor) const
{
    if (!isVisible() || buttons_.isEmpty())
        return false;
    QRect rect = this->rect();
    rect.moveTopLeft(mapToGlobal(rect.topLeft()));
    const int margin = 20;
    rect.adjust(-margin, -margin, margin, margin);
    return rect.contains(cursor);
}

bool SpectraMenuStrip::is_cursor_in_menu_zone(const QPoint& cursor) const
{
    if (!open_menu_)
        return false;
    QRect     menu_geo = open_menu_->frameGeometry();
    const int margin   = 10;
    menu_geo.adjust(-margin, -margin, margin, margin);
    return menu_geo.contains(cursor);
}

void SpectraMenuStrip::on_menu_about_to_show()
{
    auto* menu = qobject_cast<QMenu*>(sender());
    if (!menu)
        return;
    open_menu_         = menu;
    open_button_       = button_for_menu(menu);
    had_hover_         = is_cursor_over_button(open_button_);
    menu_exit_counter_ = 0;
    if (hover_timer_ && !hover_timer_->isActive())
        hover_timer_->start();

    // Ensure the button under the cursor is highlighted even if the popup
    // prevented the normal enter event from being delivered.
    if (open_button_ && had_hover_ && !open_button_->underMouse())
    {
        open_button_->setAttribute(Qt::WA_UnderMouse, true);
        open_button_->update();
    }
}

void SpectraMenuStrip::on_menu_about_to_hide()
{
    auto* menu = qobject_cast<QMenu*>(sender());
    if (open_menu_ == menu)
    {
        // Clear the highlight from the previously open button if the cursor
        // is no longer over it, otherwise stale hover states can linger.
        if (open_button_ && !is_cursor_over_button(open_button_))
        {
            open_button_->setAttribute(Qt::WA_UnderMouse, false);
            open_button_->update();
        }

        open_menu_         = nullptr;
        open_button_       = nullptr;
        had_hover_         = false;
        menu_exit_counter_ = 0;
        if (hover_timer_ && hover_timer_->isActive())
            hover_timer_->stop();
    }
}

void SpectraMenuStrip::on_hover_poll()
{
    if (!open_menu_ || switching_)
        return;

    const QPoint cursor = QCursor::pos();

    // Switch to a different top-level button if the cursor is over it.
    for (auto* btn : buttons_)
    {
        if (btn == open_button_)
            continue;
        if (!btn || !btn->menu())
            continue;

        QRect global_rect = btn->rect();
        global_rect.moveTopLeft(btn->mapToGlobal(global_rect.topLeft()));
        if (global_rect.contains(cursor))
        {
            menu_exit_counter_ = 0;
            on_button_entered(btn);
            return;
        }
    }

    // Keep the menu open while the cursor is anywhere in the menu bar or
    // inside the open popup. This prevents fast mouse movements from
    // dismissing the menu when the cursor briefly crosses empty space.
    if (is_cursor_in_top_zone(cursor) || is_cursor_in_menu_zone(cursor))
    {
        had_hover_         = true;
        menu_exit_counter_ = 0;
        return;
    }

    // If the cursor has never entered the menu zone, keep the menu open
    // (e.g., it was opened by a keyboard shortcut or automation call).
    if (!had_hover_)
        return;

    // Require the cursor to be outside the menu zone for several polls
    // before closing, so brief overshoots during fast movement do not
    // dismiss the menu.
    if (++menu_exit_counter_ < k_menu_exit_threshold)
        return;

    open_menu_->close();
}

}   // namespace spectra::adapters::qt
