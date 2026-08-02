#pragma once

// SpectraInspectorWidgets — reusable, legacy-styled Qt controls for the inspector.
//
// These are intentionally close to the ImGui widgets in ui/imgui/widgets.cpp so
// the Qt and legacy inspectors share the same layout, colors, and interaction
// language.

#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>

#include <functional>
#include <optional>
#include <vector>

#include <QVariant>

#include <spectra/color.hpp>

namespace spectra::adapters::qt
{

// Helper: switchless 24×24 color swatch with the current Spectra token border.
QColor         to_qcolor(const spectra::Color& c);
spectra::Color from_qcolor(const QColor& c);

// ─── SegmentedControl ─────────────────────────────────────────────────────────
// Pill-style 4-segment bar used for the inspector top tabs (Figure/Series/Axes/Data).

class SpectraSegmentedControl : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraSegmentedControl(QWidget* parent = nullptr);

    void setItems(const QStringList& items);
    int  count() const { return items_.size(); }

    int  currentIndex() const { return current_; }
    void setCurrentIndex(int index);

    QString currentText() const;

   signals:
    void currentIndexChanged(int index);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;

   private:
    QRectF segmentRect(int index) const;

    QStringList items_;
    int         current_ = 0;
    int         hovered_ = -1;
};

// ─── PanelTitle ───────────────────────────────────────────────────────────────
// Bold primary title with an optional muted subtitle (e.g. "Figure" / "1 axes, 0 series").

class SpectraPanelTitle : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraPanelTitle(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    static constexpr int kTitleH    = 28;
    static constexpr int kSubtitleH = 20;

    QString title_;
    QString subtitle_;
};

// ─── SectionHeader ────────────────────────────────────────────────────────────
// Chevron + uppercase label on a rounded surface band. Click to toggle content.

class SpectraSectionHeader : public QPushButton
{
    Q_OBJECT

   public:
    explicit SpectraSectionHeader(const QString& title, QWidget* parent = nullptr);

    bool isOpen() const { return open_; }
    void setOpen(bool open);

   signals:
    void toggled(bool open);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString title_;
    bool    open_ = true;
};

// ─── PropertyRow ──────────────────────────────────────────────────────────────
// "Label    [value]" row used for drag/text/combo/numeric fields. The label is
// left-aligned and the value widget sits inside a rounded token-styled input surface.

class SpectraPropertyRow : public QWidget
{
    Q_OBJECT

   public:
    SpectraPropertyRow(const QString& label, QWidget* value_widget, QWidget* parent = nullptr);

    void     setLabel(const QString& label);
    QWidget* valueWidget() const { return value_; }

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString  label_;
    QWidget* value_ = nullptr;
};

// ─── RangeRow ─────────────────────────────────────────────────────────────────
// Two compact numeric spin boxes in a single row, used for X/Y/Z axis ranges.

class SpectraRangeRow : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraRangeRow(QWidget* parent = nullptr);

    QDoubleSpinBox* minSpin() const { return min_; }
    QDoubleSpinBox* maxSpin() const { return max_; }

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QDoubleSpinBox* min_ = nullptr;
    QDoubleSpinBox* max_ = nullptr;
};

// ─── ColorField ───────────────────────────────────────────────────────────────
// 28×28 rounded color swatch + label. Clicking opens a color picker.

class SpectraColorField : public QWidget
{
    Q_OBJECT

   public:
    using ColorPicker = std::function<std::optional<spectra::Color>(const QString&        title,
                                                                    const spectra::Color& current)>;

    explicit SpectraColorField(const QString& label, QWidget* parent = nullptr);

    void           setColor(const spectra::Color& c);
    spectra::Color color() const { return color_; }
    void           setColorPicker(ColorPicker picker) { picker_ = std::move(picker); }

   signals:
    void colorChanged(const spectra::Color& c);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

   private:
    QString        label_;
    spectra::Color color_ = {1.0f, 1.0f, 1.0f, 1.0f};
    ColorPicker    picker_;
};

// ─── ToggleField ──────────────────────────────────────────────────────────────
// Label on the left, animated pill toggle switch on the right.

class SpectraToggleField : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraToggleField(const QString& label, QWidget* parent = nullptr);

    bool isChecked() const { return checked_; }
    void setChecked(bool checked);

   signals:
    void toggled(bool checked);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

   private:
    QString label_;
    bool    checked_ = false;
};

// ─── ReferenceLineRow ─────────────────────────────────────────────────────────
// Color dot + label + delete button for reference lines.

class SpectraReferenceLineRow : public QWidget
{
    Q_OBJECT

   public:
    SpectraReferenceLineRow(const spectra::Color& color,
                            const QString&        label,
                            QWidget*              parent = nullptr);

    void setColor(const spectra::Color& c);
    void setLabel(const QString& label);

    spectra::Color color() const { return color_; }
    QString        label() const { return label_; }
    QPushButton*   deleteButton() const { return delete_btn_; }

   signals:
    void deleteClicked();

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    void updateDot();

    spectra::Color color_;
    QString        label_;
    QPushButton*   delete_btn_ = nullptr;
    QLabel*        dot_        = nullptr;
    QLabel*        label_lbl_  = nullptr;
};

// ─── SeriesListView ───────────────────────────────────────────────────────────
// Custom series browser: color dot, eye icon, label, and a bulk-action bar.

class SpectraSeriesListView : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraSeriesListView(QWidget* parent = nullptr);

    QListWidget* listWidget() const { return list_; }

    void clear();
    int  count() const;

    QListWidgetItem* addSeries(const spectra::Color& color,
                               const QString&        label,
                               bool                  visible,
                               const QVariant&       user_data = {});

    int                           currentRow() const;
    void                          setCurrentRow(int row);
    QListWidgetItem*              itemAt(int row) const;
    std::vector<int>              selectedRows() const;
    std::vector<QListWidgetItem*> selectedItems() const;

   signals:
    void currentRowChanged(int row);
    void copySelected();
    void cutSelected();
    void deleteSelected();

   private:
    QListWidget* list_       = nullptr;
    QWidget*     bulk_bar_   = nullptr;
    QPushButton* copy_btn_   = nullptr;
    QPushButton* cut_btn_    = nullptr;
    QPushButton* delete_btn_ = nullptr;
};

// ─── DragSpinBox ──────────────────────────────────────────────────────────────
// QDoubleSpinBox styled like the legacy drag field: no up/down buttons, optional suffix.

class SpectraDragSpinBox : public QDoubleSpinBox
{
    Q_OBJECT
   public:
    explicit SpectraDragSpinBox(QWidget* parent = nullptr);
};

}   // namespace spectra::adapters::qt
