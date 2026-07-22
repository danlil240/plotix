#pragma once

// SpectraDockHeader — custom dock panel header (replaces QDockWidget title bar).
//
// Shows panel title, optional close button, and chevron toggle.

#include <QWidget>

class QLabel;
class QPushButton;

namespace spectra::adapters::qt
{

class SpectraDockHeader : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraDockHeader(const QString& title, QWidget* parent = nullptr);
    ~SpectraDockHeader() override = default;
    SpectraDockHeader(const SpectraDockHeader&) = delete;
    SpectraDockHeader& operator=(const SpectraDockHeader&) = delete;

    void set_title(const QString& title);
    void set_closable(bool closable);
    void set_collapsible(bool collapsible);
    void set_collapsed(bool collapsed);

signals:
    void close_requested();
    void toggle_collapsed();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString     title_;
    bool        closable_    = true;
    bool        collapsible_ = false;
    bool        collapsed_   = false;
    QPushButton* btn_close_  = nullptr;
    QPushButton* btn_chevron_ = nullptr;
};

}   // namespace spectra::adapters::qt
