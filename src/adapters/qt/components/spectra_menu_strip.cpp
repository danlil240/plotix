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
        connect(this, &QPushButton::clicked, this, &SpectraMenuButton::show_menu);
}

void SpectraMenuButton::show_menu()
{
    if (menu_)
    {
        QPoint pos(0, height());
        menu_->popup(mapToGlobal(pos));
    }
}

// ─── SpectraMenuStrip ────────────────────────────────────────────────────────

SpectraMenuStrip::~SpectraMenuStrip() = default;

SpectraMenuStrip::SpectraMenuStrip(QWidget* parent) : QWidget(parent)
{
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);
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
}

}   // namespace spectra::adapters::qt
