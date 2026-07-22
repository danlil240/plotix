#pragma once

// SpectraAppHeader — custom application header bar.
//
// Reproduces the Vision.png header hierarchy:
// - ~60 logical pixels high
// - Spectra logo (S icon)
// - Spaced SPECTRA wordmark
// - Vertical divider
// - Home button
// - File, Data, View, Axes, Transforms, Tools menu buttons
// - Subtle dark navy surface
// - Cyan and purple ambient glow toward the right
// - Hairline border

#include <QWidget>

class QPushButton;
class QMenu;
class QHBoxLayout;

namespace spectra::adapters::qt
{

class SpectraMenuStrip;

class SpectraAppHeader : public QWidget
{
    Q_OBJECT

public:
    explicit SpectraAppHeader(QWidget* parent = nullptr);
    ~SpectraAppHeader() override;
    SpectraAppHeader(const SpectraAppHeader&) = delete;
    SpectraAppHeader& operator=(const SpectraAppHeader&) = delete;

    // Register a QMenu under one of the header menu buttons
    void set_menu(const QString& label, QMenu* menu);
    // Add a new menu button to the header menu strip
    void add_menu(const QString& label, QMenu* menu);

    int height_hint() const;

signals:
    void home_clicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void build_layout();

    QWidget*      logo_widget_  = nullptr;
    QWidget*      wordmark_widget_ = nullptr;
    QPushButton*  home_btn_     = nullptr;
    SpectraMenuStrip* menu_strip_ = nullptr;
};

}   // namespace spectra::adapters::qt
