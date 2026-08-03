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

// Font sizes the legacy inspector loads in `ImGuiIntegration::load_fonts()`.
// ImGui interprets these as ascent+descent while Qt's setPixelSize sets the em
// square, so pass them through `imgui_font_px()` before handing them to QFont.
constexpr double kImGuiBodySize    = 16.0;
constexpr double kImGuiHeadingSize = 11.5;
constexpr double kImGuiTitleSize   = 20.0;

// Converts an ImGui `size_pixels` into the Qt pixel size that renders at the
// same visual size for Inter.
int imgui_font_px(double imgui_size_pixels);

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
    void updateHeight();

    // Offsets measured from the legacy panel (font_title_ 20px, font_body_ 16px,
    // ImGui ItemSpacing 12): title box 0..20, subtitle box 44..60, separator at
    // 96, and the first section band at 132.
    static constexpr int kTitleH          = 20;
    static constexpr int kSubtitleY       = 44;
    static constexpr int kSubtitleH       = 16;
    static constexpr int kSeparatorY      = 96;
    static constexpr int kSeparatorYNoSub = 52;
    static constexpr int kTrailingGap     = 36;

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

    // Legacy `widgets::drag_field` renders a default ImGui frame: 36px tall, with
    // the value input starting INSPECTOR_LABEL_WIDTH from the panel content edge.
    // Rows live inside the section's 12px group indent, so the label column is
    // 80 - 12 = 68.
    static constexpr int kRowHeight  = 36;
    static constexpr int kLabelWidth = 68;

    static constexpr int row_height() { return kRowHeight; }

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

// ─── InfoRow ──────────────────────────────────────────────────────────────────
// Read-only "Label            value" row matching legacy `widgets::info_row`:
// muted label on the left, primary-colored value starting at 45% of the row.

class SpectraInfoRow : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraInfoRow(const QString& label,
                            const QString& value  = {},
                            QWidget*       parent = nullptr);

    void    setLabel(const QString& label);
    void    setValue(const QString& value);
    QString value() const { return value_; }

    // Legacy stat/info rows are plain 16px text lines with no frame.
    static constexpr int kRowHeight = 22;

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString label_;
    QString value_;
};

// ─── SliderField ──────────────────────────────────────────────────────────────
// Label + 4px pill track with an accent fill and a 14px thumb, mirroring
// legacy `widgets::slider_field`. The formatted value is centered in the track.

class SpectraSliderField : public QWidget
{
    Q_OBJECT

   public:
    SpectraSliderField(const QString& label,
                       double         minimum,
                       double         maximum,
                       int            decimals = 1,
                       QString        suffix   = {},
                       QWidget*       parent   = nullptr);

    double value() const { return value_; }
    void   setValue(double value);

   signals:
    void valueChanged(double value);

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

   private:
    QRect   trackRect() const;
    void    setFromPos(int x);
    QString formatted() const;

    QString label_;
    double  min_      = 0.0;
    double  max_      = 1.0;
    double  value_    = 0.0;
    int     decimals_ = 1;
    QString suffix_;
    bool    dragging_ = false;
};

// ─── Separator ────────────────────────────────────────────────────────────────
// Hairline divider matching legacy `widgets::separator()`.

class SpectraSeparator : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraSeparator(QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent*) override;
};

// ─── SeparatorLabel ───────────────────────────────────────────────────────────
// Centered muted caption flanked by hairlines, matching legacy
// `widgets::separator_label` (used for "X Axis" / "Y Axis" stat groups).

class SpectraSeparatorLabel : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraSeparatorLabel(const QString& text, QWidget* parent = nullptr);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString text_;
};

// ─── SwatchLabel ──────────────────────────────────────────────────────────────
// 16px color swatch followed by muted text — the legacy series-properties
// "color swatch + type badge" line.

class SpectraSwatchLabel : public QWidget
{
    Q_OBJECT

   public:
    explicit SpectraSwatchLabel(QWidget* parent = nullptr);

    void setColor(const spectra::Color& c);
    void setText(const QString& text);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    spectra::Color color_ = {1.0f, 1.0f, 1.0f, 1.0f};
    QString        text_;
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
    // The legacy browser draws every row inline and lets the inspector page
    // scroll, so the list sizes to its content instead of expanding.
    void updateListHeight();

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
