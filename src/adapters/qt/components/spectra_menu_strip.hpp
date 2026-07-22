#pragma once

// SpectraMenuStrip — custom menu button strip for the application header.
//
// Contains custom SpectraMenuButton widgets that do NOT look like QMenuBar.
// Each button shows a QMenu popup when clicked.

#include <QPushButton>
#include <QWidget>
#include <QList>

class QMenu;
class QHBoxLayout;

namespace spectra::adapters::qt
{

class SpectraMenuButton : public QPushButton
{
    Q_OBJECT
public:
    explicit SpectraMenuButton(const QString& label, QMenu* menu,
                               QWidget* parent = nullptr);

private slots:
    void show_menu();

private:
    QMenu* menu_ = nullptr;
};

class SpectraMenuStrip : public QWidget
{
    Q_OBJECT

public:
    explicit SpectraMenuStrip(QWidget* parent = nullptr);
    ~SpectraMenuStrip() override;
    SpectraMenuStrip(const SpectraMenuStrip&) = delete;
    SpectraMenuStrip& operator=(const SpectraMenuStrip&) = delete;

    void add_menu_button(const QString& label, QMenu* menu);

private:
    QHBoxLayout*      layout_ = nullptr;
    QList<SpectraMenuButton*> buttons_;
};

}   // namespace spectra::adapters::qt
