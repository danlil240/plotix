#pragma once

// SpectraStatusChip — a pill-shaped status bar chip.
//
// Used for cursor coordinates, active tool, zoom, FPS, GPU frame time.

#include <QWidget>

namespace spectra::adapters::qt
{

class SpectraStatusChip : public QWidget
{
    Q_OBJECT
   public:
    enum class Type
    {
        Normal,
        Accent,    // cyan accent
        Success,   // green (FPS)
        Warning,
    };

    explicit SpectraStatusChip(const QString& text,
                               Type           type   = Type::Normal,
                               QWidget*       parent = nullptr);

    void           set_text(const QString& text);
    const QString& text() const { return text_; }
    void           set_type(Type type);

    QSize sizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QString text_;
    Type    type_ = Type::Normal;
};

}   // namespace spectra::adapters::qt
