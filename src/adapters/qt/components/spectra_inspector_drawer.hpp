#pragma once

// SpectraInspectorDrawer — custom inspector panel.
//
// Features:
// - Hidden by default
// - Opens via chevron or menu action
// - 300-380 px wide
// - Custom dock header (no default QDockWidget title bar)
// - Hosts the existing functional inspector rather than duplicating controls

#include <QWidget>

class QStackedWidget;
class QVBoxLayout;

namespace spectra::adapters::qt
{

class SpectraDockHeader;
class SpectraInspectorDrawer : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraInspectorDrawer(QWidget* parent = nullptr);
    ~SpectraInspectorDrawer() override;
    SpectraInspectorDrawer(const SpectraInspectorDrawer&) = delete;
    SpectraInspectorDrawer& operator=(const SpectraInspectorDrawer&) = delete;

    bool is_open() const { return open_; }

    void set_content_widget(QWidget* widget);
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
    QWidget*            content_    = nullptr;
    QVBoxLayout*        layout_     = nullptr;
};

}   // namespace spectra::adapters::qt
