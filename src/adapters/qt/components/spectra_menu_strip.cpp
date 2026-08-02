// spectra_menu_strip.cpp — Custom menu button strip implementation.

#include "spectra_menu_strip.hpp"
#include "spectra_design_tokens.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QSizePolicy>
#include <QString>
#include <QTimer>

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

SpectraMenuStrip::SpectraMenuStrip(QWidget* parent) : QWidget(parent)
{
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);

    hover_timer_ = new QTimer(this);
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
    if (label == "File")
        btn->setFixedWidth(52);
    else if (label == "Edit")
        btn->setFixedWidth(53);
    else if (label == "View")
        btn->setFixedWidth(59);
    else if (label == "Tools")
        btn->setFixedWidth(62);
    else if (label == "Plot")
        btn->setFixedWidth(53);
    else if (label == "Data")
        btn->setFixedWidth(55);
    else if (label == "Axes")
        btn->setFixedWidth(60);
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
    if (!open_menu_)
        return;
    if (open_button_ == button)
        return;
    if (!button || !button->menu())
        return;

    open_menu_->close();
    button->popup_menu();
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

void SpectraMenuStrip::on_menu_about_to_show()
{
    auto* menu = qobject_cast<QMenu*>(sender());
    if (!menu)
        return;
    open_menu_   = menu;
    open_button_ = button_for_menu(menu);
    had_hover_   = is_cursor_over_button(open_button_);
    if (hover_timer_ && !hover_timer_->isActive())
        hover_timer_->start();
}

void SpectraMenuStrip::on_menu_about_to_hide()
{
    auto* menu = qobject_cast<QMenu*>(sender());
    if (open_menu_ == menu)
    {
        open_menu_   = nullptr;
        open_button_ = nullptr;
        had_hover_   = false;
        if (hover_timer_ && hover_timer_->isActive())
            hover_timer_->stop();
    }
}

void SpectraMenuStrip::on_hover_poll()
{
    if (!open_menu_)
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
            on_button_entered(btn);
            return;
        }
    }

    // Track that the cursor has entered the open menu's zone.
    if (open_button_ && is_cursor_over_button(open_button_))
    {
        had_hover_ = true;
        return;
    }

    // Keep open while the cursor is inside the menu (with a small margin to
    // bridge the gap between the button and the popup).
    if (open_menu_)
    {
        QRect     menu_geo = open_menu_->frameGeometry();
        const int margin   = 10;
        menu_geo.adjust(-margin, -margin, margin, margin);
        if (menu_geo.contains(cursor))
        {
            had_hover_ = true;
            return;
        }
    }

    // If the cursor has never entered the menu zone, keep the menu open
    // (e.g. it was opened by a keyboard shortcut or automation call).
    if (!had_hover_)
        return;

    // Cursor has left the menu zone; close it.
    open_menu_->close();
}

}   // namespace spectra::adapters::qt
