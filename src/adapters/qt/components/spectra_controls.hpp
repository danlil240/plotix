#pragma once

// Custom Spectra-styled Qt controls.
// These wrap standard Qt widgets with Spectra styling via paintEvent/stylesheet.

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>

namespace spectra::adapters::qt
{

class SpectraLineEdit : public QLineEdit
{
    Q_OBJECT
   public:
    explicit SpectraLineEdit(QWidget* parent = nullptr);
};

class SpectraComboBox : public QComboBox
{
    Q_OBJECT
   public:
    explicit SpectraComboBox(QWidget* parent = nullptr);
};

class SpectraCheckBox : public QCheckBox
{
    Q_OBJECT
   public:
    explicit SpectraCheckBox(const QString& text, QWidget* parent = nullptr);
};

class SpectraSpinBox : public QSpinBox
{
    Q_OBJECT
   public:
    explicit SpectraSpinBox(QWidget* parent = nullptr);
};

}   // namespace spectra::adapters::qt
