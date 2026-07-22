// spectra_controls.cpp — Custom Spectra-styled Qt controls.

#include "spectra_controls.hpp"
#include "spectra_design_tokens.hpp"

namespace spectra::adapters::qt
{

static QString input_stylesheet()
{
    return QString(
        "QLineEdit, QComboBox, QSpinBox {"
        "  background-color: #1A1C22;"
        "  border: 1px solid #23262E;"
        "  border-radius: 8px;"
        "  padding: 4px 8px;"
        "  color: #E8ECF1;"
        "  font-family: 'Inter';"
        "  font-size: 13px;"
        "  selection-background-color: #22D3EE;"
        "  selection-color: #0D0E11;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus {"
        "  border: 1px solid #22D3EE;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "  width: 20px;"
        "}"
        "QComboBox::down-arrow {"
        "  image: none;"
        "  width: 0;"
        "  height: 0;"
        "  border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid #8A909C;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: #15171C;"
        "  border: 1px solid #23262E;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "  color: #E8ECF1;"
        "  selection-background-color: #1F2229;"
        "}"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  background: transparent;"
        "  border: none;"
        "  width: 16px;"
        "}"
        "QSpinBox::up-arrow {"
        "  image: none;"
        "  width: 0; height: 0;"
        "  border-left: 3px solid transparent;"
        "  border-right: 3px solid transparent;"
        "  border-bottom: 4px solid #8A909C;"
        "}"
        "QSpinBox::down-arrow {"
        "  image: none;"
        "  width: 0; height: 0;"
        "  border-left: 3px solid transparent;"
        "  border-right: 3px solid transparent;"
        "  border-top: 4px solid #8A909C;"
        "}"
    );
}

SpectraLineEdit::SpectraLineEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setStyleSheet(input_stylesheet());
    setFixedHeight(28);
}

SpectraComboBox::SpectraComboBox(QWidget* parent)
    : QComboBox(parent)
{
    setStyleSheet(input_stylesheet());
    setFixedHeight(28);
}

SpectraCheckBox::SpectraCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    setStyleSheet(QString(
        "QCheckBox {"
        "  color: #C8CDD6;"
        "  font-family: 'Inter';"
        "  font-size: 13px;"
        "  spacing: 8px;"
        "}"
        "QCheckBox::indicator {"
        "  width: 16px;"
        "  height: 16px;"
        "  border-radius: 4px;"
        "  border: 1px solid #23262E;"
        "  background: #1A1C22;"
        "}"
        "QCheckBox::indicator:hover {"
        "  border: 1px solid #22D3EE;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: #22D3EE;"
        "  border: 1px solid #22D3EE;"
        "}"
    ));
}

SpectraSpinBox::SpectraSpinBox(QWidget* parent)
    : QSpinBox(parent)
{
    setStyleSheet(input_stylesheet());
    setFixedHeight(28);
}

}   // namespace spectra::adapters::qt
