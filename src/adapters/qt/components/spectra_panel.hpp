#pragma once

// SpectraPanel — custom panel container with Spectra styling.
//
// Used as the content area inside inspector drawer and other dock panels.

#include <QWidget>

class QVBoxLayout;

namespace spectra::adapters::qt
{

class SpectraPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SpectraPanel(QWidget* parent = nullptr);
    ~SpectraPanel() override = default;
    SpectraPanel(const SpectraPanel&) = delete;
    SpectraPanel& operator=(const SpectraPanel&) = delete;

    QVBoxLayout* content_layout() const { return layout_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* layout_ = nullptr;
};

}   // namespace spectra::adapters::qt
