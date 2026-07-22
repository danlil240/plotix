#pragma once

// SpectraTitleBar — custom client-side title bar for frameless Spectra windows.
//
// Features:
// - Minimize, maximize/restore, close buttons
// - Double-click to maximize/restore
// - Drag to move (QWindow::startSystemMove)
// - Edge resize (QWindow::startSystemResize)
// - Centered title text
// - Custom-painted with Spectra design tokens

#include <QWidget>

class QLabel;
class QPushButton;

namespace spectra::adapters::qt
{

class SpectraTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit SpectraTitleBar(QWidget* parent = nullptr);
    ~SpectraTitleBar() override;
    SpectraTitleBar(const SpectraTitleBar&) = delete;
    SpectraTitleBar& operator=(const SpectraTitleBar&) = delete;

    void set_title(const QString& title);
    QString title() const { return title_; }

    // Set the window that this title bar controls
    void set_window(QWidget* window);

    int height_hint() const;

signals:
    void minimized();
    void maximized();
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void build_buttons();
    void on_minimize();
    void on_maximize();
    void on_close();

    QWidget*    window_     = nullptr;
    QString     title_      = "Spectra";
    QPushButton* btn_min_   = nullptr;
    QPushButton* btn_max_   = nullptr;
    QPushButton* btn_close_ = nullptr;
    bool         is_maximized_ = false;
};

}   // namespace spectra::adapters::qt
