#pragma once

// SpectraInspectorDrawer — custom inspector panel.
//
// Features:
// - Hidden by default
// - Opens via chevron or menu action
// - 300-380 px wide
// - Custom dock header (no default QDockWidget title bar)
// - Custom controls: SpectraLineEdit, SpectraComboBox, SpectraCheckBox, SpectraSpinBox
// - Figure and axes property tabs

#include <QWidget>

class QStackedWidget;
class QVBoxLayout;

namespace spectra::adapters::qt
{

class SpectraDockHeader;
class SpectraPanel;

class SpectraInspectorDrawer : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraInspectorDrawer(QWidget* parent = nullptr);
    ~SpectraInspectorDrawer() override;
    SpectraInspectorDrawer(const SpectraInspectorDrawer&) = delete;
    SpectraInspectorDrawer& operator=(const SpectraInspectorDrawer&) = delete;

    bool is_open() const { return open_; }

    void open();
    void close();
    void toggle();

    int width_hint() const;

signals:
    void opened();
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void build_ui();

    bool                open_       = false;
    SpectraDockHeader*  header_     = nullptr;
    SpectraPanel*       content_    = nullptr;
    QVBoxLayout*        layout_     = nullptr;
};

}   // namespace spectra::adapters::qt
