// spectra_menu_strip.cpp — Custom menu button strip implementation.

#include "spectra_menu_strip.hpp"
#include "spectra_design_tokens.hpp"

#include <QHBoxLayout>
#include <QMenu>
#include <QSizePolicy>
#include <QString>

namespace spectra::adapters::qt
{

// ─── SpectraMenuButton ───────────────────────────────────────────────────────

SpectraMenuButton::SpectraMenuButton(const QString& label, QMenu* menu,
                                     QWidget* parent)
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
    setFont(fm.font_base());

    setStyleSheet(QString(
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: %1px;"
        "  color: #C8CDD6;"
        "  padding: 0 12px;"
        "  text-align: left;"
        "}"
        "QPushButton:hover {"
        "  background: #1F2229;"
        "  color: #E8ECF1;"
        "}"
        "QPushButton:pressed {"
        "  background: #23262E;"
        "}"
    ).arg(spectra_geometry().radius_md));

    if (menu_)
        connect(this, &QPushButton::clicked, this, &SpectraMenuButton::show_menu);
}

void SpectraMenuButton::show_menu()
{
    if (menu_)
    {
        QPoint pos(0, height());
        menu_->exec(mapToGlobal(pos));
    }
}

// ─── SpectraMenuStrip ────────────────────────────────────────────────────────

SpectraMenuStrip::~SpectraMenuStrip() = default;

SpectraMenuStrip::SpectraMenuStrip(QWidget* parent)
    : QWidget(parent)
{
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);
}

void SpectraMenuStrip::add_menu_button(const QString& label, QMenu* menu)
{
    auto* btn = new SpectraMenuButton(label, menu, this);
    layout_->addWidget(btn);
    buttons_.append(btn);
}

}   // namespace spectra::adapters::qt
