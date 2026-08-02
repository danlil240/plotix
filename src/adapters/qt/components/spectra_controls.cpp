// spectra_controls.cpp — Custom Spectra-styled Qt controls.

#include "spectra_controls.hpp"
#include "spectra_design_tokens.hpp"

namespace spectra::adapters::qt
{

SpectraLineEdit::SpectraLineEdit(QWidget* parent) : QLineEdit(parent)
{
    setFixedHeight(28);
}

SpectraComboBox::SpectraComboBox(QWidget* parent) : QComboBox(parent)
{
    setFixedHeight(28);
}

SpectraCheckBox::SpectraCheckBox(const QString& text, QWidget* parent) : QCheckBox(text, parent) {}

SpectraSpinBox::SpectraSpinBox(QWidget* parent) : QSpinBox(parent)
{
    setFixedHeight(28);
}

}   // namespace spectra::adapters::qt
