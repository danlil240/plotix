// spectra_inspector_widgets.cpp — custom Qt widgets that mirror the legacy ImGui inspector style.

#include "spectra_inspector_widgets.hpp"
#include "spectra_design_tokens.hpp"

#include "ui/theme/design_tokens.hpp"
#include "ui/theme/icons.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace spectra::adapters::qt
{

QColor to_qcolor(const spectra::Color& c)
{
    return QColor::fromRgbF(static_cast<double>(c.r),
                            static_cast<double>(c.g),
                            static_cast<double>(c.b),
                            static_cast<double>(c.a));
}

spectra::Color from_qcolor(const QColor& c)
{
    return {static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

// ImGui sizes a font by its ascent+descent, Qt by the em square. Inter's
// ascent+descent is ~1.21 em, so `ImFontConfig::SizePixels = S` renders at the
// same visual size as a Qt pixel size of S / 1.21. Without this conversion every
// inspector string comes out ~20% larger than the legacy panel.
int imgui_font_px(double imgui_size_pixels)
{
    return qRound(imgui_size_pixels / 1.21);
}

static QFont title_font()
{
    auto f = SpectraFontManager::instance().font_semibold();
    f.setPixelSize(imgui_font_px(kImGuiTitleSize));
    return f;
}

// Legacy `Inspector::draw_figure_properties` prints the subtitle with the body
// font in text_secondary, not a small muted caption.
static QFont subtitle_font()
{
    auto f = SpectraFontManager::instance().font_base();
    f.setPixelSize(imgui_font_px(kImGuiBodySize));
    return f;
}

// Every inspector row label, field value, and color-field caption in the legacy
// panel uses the body font.
static QFont row_font()
{
    auto f = SpectraFontManager::instance().font_base();
    f.setPixelSize(imgui_font_px(kImGuiBodySize));
    return f;
}

static QFont header_font()
{
    auto f = SpectraFontManager::instance().font_medium();
    f.setPixelSize(imgui_font_px(kImGuiHeadingSize));
    return f;
}

static QString chevron_icon(bool down)
{
    return SpectraFontManager::icon_codepoint(down ? static_cast<uint32_t>(ui::Icon::ChevronDown)
                                                   : static_cast<uint32_t>(ui::Icon::ChevronRight));
}

// ─── SegmentedControl ─────────────────────────────────────────────────────────

SpectraSegmentedControl::SpectraSegmentedControl(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Legacy draws the track at exactly SEGMENT_TAB_H and insets the segments by
    // SEGMENT_TRACK_PAD inside it, so the widget is the track.
    setFixedHeight(static_cast<int>(ui::tokens::SEGMENT_TAB_H));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void SpectraSegmentedControl::setItems(const QStringList& items)
{
    items_   = items;
    current_ = qBound(0, current_, qMax(0, items_.size() - 1));
    update();
}

void SpectraSegmentedControl::setCurrentIndex(int index)
{
    if (index == current_ || index < 0 || index >= items_.size())
        return;
    current_ = index;
    emit currentIndexChanged(current_);
    update();
}

QString SpectraSegmentedControl::currentText() const
{
    if (current_ < 0 || current_ >= items_.size())
        return {};
    return items_[current_];
}

QRectF SpectraSegmentedControl::segmentRect(int index) const
{
    if (items_.isEmpty())
        return {};

    // Legacy: tab_w = floor((track_w - pad*2) / count) and the first segment
    // starts one pad in; the trailing pad is left as track surface.
    const float pad = ui::tokens::SEGMENT_TRACK_PAD;
    const float sw  = std::floor((static_cast<float>(width()) - pad * 2.0f) / items_.size());
    const float h   = static_cast<float>(height()) - pad * 2.0f;
    const float x   = pad + sw * static_cast<float>(index);
    return QRectF(x, pad, sw, h);
}

void SpectraSegmentedControl::paintEvent(QPaintEvent*)
{
    if (items_.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    // Track background fills the whole control (legacy uses the raw content width).
    QRectF track_rect = QRectF(rect()).adjusted(0, 0, -1, -1);
    QColor track_bg   = c.window_base;
    track_bg.setAlphaF(0.55f);
    p.setBrush(track_bg);
    p.setPen(QPen(c.border_subtle, 1));
    p.drawRoundedRect(track_rect, g.radius_md, g.radius_md);

    // Segments
    for (int i = 0; i < items_.size(); ++i)
    {
        const QRectF seg    = segmentRect(i);
        const bool   active = (i == current_);
        const bool   hover  = (i == hovered_);

        if (active)
        {
            QColor fill = c.cyan_accent;
            fill.setAlphaF(0.20f);
            p.setBrush(fill);
            p.setPen(QPen(c.cyan_accent, 1));
            p.drawRoundedRect(seg, g.radius_sm, g.radius_sm);
        }
        else if (hover)
        {
            QColor fill = c.text_secondary;
            fill.setAlphaF(0.10f);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(seg, g.radius_sm, g.radius_sm);
        }

        // Legacy tab labels use font_heading_.
        p.setFont(header_font());

        QColor text = active ? c.cyan_accent : (hover ? c.text_secondary : c.text_muted);
        if (!active && !hover)
            text.setAlphaF(0.80f);
        p.setPen(text);

        p.drawText(seg, Qt::AlignCenter, items_[i]);
    }
}

void SpectraSegmentedControl::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    for (int i = 0; i < items_.size(); ++i)
    {
        if (segmentRect(i).contains(event->position()))
        {
            setCurrentIndex(i);
            return;
        }
    }
}

void SpectraSegmentedControl::mouseMoveEvent(QMouseEvent* event)
{
    int prev = hovered_;
    hovered_ = -1;
    for (int i = 0; i < items_.size(); ++i)
    {
        if (segmentRect(i).contains(event->position()))
        {
            hovered_ = i;
            break;
        }
    }
    if (hovered_ != prev)
        update();
}

void SpectraSegmentedControl::leaveEvent(QEvent*)
{
    if (hovered_ != -1)
    {
        hovered_ = -1;
        update();
    }
}

void SpectraSegmentedControl::resizeEvent(QResizeEvent*)
{
    update();
}

// ─── PanelTitle ───────────────────────────────────────────────────────────────

SpectraPanelTitle::SpectraPanelTitle(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Legacy `Inspector::draw_figure_properties` renders title, small spacing,
    // subtitle, section spacing, a separator, then section spacing again.
    updateHeight();
}

void SpectraPanelTitle::setTitle(const QString& title)
{
    title_ = title;
    update();
}

void SpectraPanelTitle::setSubtitle(const QString& subtitle)
{
    if (subtitle == subtitle_)
        return;
    const bool had = !subtitle_.isEmpty();
    subtitle_      = subtitle;
    if (had != !subtitle_.isEmpty())
        updateHeight();
    updateGeometry();
    update();
}

void SpectraPanelTitle::updateHeight()
{
    setFixedHeight(subtitle_.isEmpty() ? kSeparatorYNoSub + kTrailingGap
                                       : kSeparatorY + kTrailingGap);
}

void SpectraPanelTitle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Offsets measured from the legacy panel: title box 0..20, subtitle box
    // 44..60, separator at 96, first section band at 132.
    QFont title_f = title_font();
    p.setFont(title_f);
    p.setPen(c.text_primary);
    p.drawText(QRect(0, 0, width(), kTitleH), Qt::AlignLeft | Qt::AlignVCenter, title_);

    if (!subtitle_.isEmpty())
    {
        p.setFont(subtitle_font());
        p.setPen(c.text_secondary);
        p.drawText(QRect(0, kSubtitleY, width(), kSubtitleH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   subtitle_);
    }

    // Hairline separator closing the title block, matching legacy
    // `widgets::separator()` between the panel header and the first section.
    QColor sep(c.border_subtle);
    sep.setAlphaF(0.30f);
    p.setPen(QPen(sep, 1));
    const int sep_y = subtitle_.isEmpty() ? kSeparatorYNoSub : kSeparatorY;
    p.drawLine(0, sep_y, width(), sep_y);
}

// ─── SectionHeader ────────────────────────────────────────────────────────────

SpectraSectionHeader::SpectraSectionHeader(const QString& title, QWidget* parent)
    : QPushButton(parent), title_(title)
{
    setFlat(true);
    setCheckable(true);
    setChecked(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(static_cast<int>(ui::tokens::SECTION_HEADER_HEIGHT));
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(title);
    connect(this, &QPushButton::clicked, this, [this]() { setOpen(!open_); });
}

void SpectraSectionHeader::setOpen(bool open)
{
    if (open == open_)
        return;
    open_ = open;
    setChecked(open_);
    emit toggled(open_);
    update();
}

void SpectraSectionHeader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    bool hovered = underMouse();

    // Subtle band behind the header row.
    float bg_alpha = c.section_header_bg.alphaF();
    if (hovered)
        bg_alpha = std::min(1.0f, bg_alpha + 0.08f);

    QColor bg = c.section_header_bg;
    bg.setAlphaF(bg_alpha);
    p.setBrush(bg);
    if (hovered)
    {
        QColor pen_color(c.border_subtle.red(),
                         c.border_subtle.green(),
                         c.border_subtle.blue(),
                         90);
        p.setPen(QPen(pen_color, 1));
    }
    else
    {
        p.setPen(Qt::NoPen);
    }
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), g.radius_sm, g.radius_sm);

    // Chevron + label vertically centered.
    const int text_h    = QFontMetrics(header_font()).height();
    const int row_y     = std::max(0, (height() - text_h) / 2);
    const int row_x     = static_cast<int>(ui::tokens::SPACE_2);
    const int chevron_x = row_x;

    QString chevron = chevron_icon(open_);
    QFont icon_f = SpectraFontManager::instance().font_icon(static_cast<int>(ui::tokens::ICON_SM));
    p.setFont(icon_f);
    p.setPen(c.text_muted);
    p.drawText(QRect(chevron_x, row_y, text_h, text_h), Qt::AlignCenter, chevron);

    // Uppercase label
    QString upper = title_.toUpper();
    QFont   f     = header_font();
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(c.text_secondary);
    p.drawText(QRect(chevron_x + text_h + static_cast<int>(ui::tokens::SPACE_2),
                     row_y,
                     width() - chevron_x - text_h,
                     text_h),
               Qt::AlignLeft | Qt::AlignVCenter,
               upper);
}

// ─── PropertyRow ──────────────────────────────────────────────────────────────

SpectraPropertyRow::SpectraPropertyRow(const QString& label, QWidget* value_widget, QWidget* parent)
    : QWidget(parent), label_(label), value_(value_widget)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kRowHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* label_lbl = new QLabel(label, this);
    label_lbl->setFont(row_font());
    label_lbl->setStyleSheet(QString("color: %1; background: transparent;")
                                 .arg(spectra_colors().text_secondary.name(QColor::HexArgb)));
    // The row already sits inside the section's 12px group indent, so the label
    // column is INSPECTOR_LABEL_WIDTH minus that indent.
    label_lbl->setFixedWidth(kLabelWidth);
    label_lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(label_lbl);

    if (value_)
    {
        value_->setParent(this);
        value_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        value_->setFixedHeight(kRowHeight);
        layout->addWidget(value_);
    }

    setLayout(layout);
}

void SpectraPropertyRow::setLabel(const QString& label)
{
    label_    = label;
    auto* lbl = findChild<QLabel*>();
    if (lbl)
        lbl->setText(label);
}

void SpectraPropertyRow::paintEvent(QPaintEvent*)
{
    // The input surface is drawn by the stylesheet on the value widget.
    // This paint is a no-op except for the label color, which is set via stylesheet.
    QPainter p(this);
    Q_UNUSED(p)
}

// ─── RangeRow ─────────────────────────────────────────────────────────────────

SpectraRangeRow::SpectraRangeRow(QWidget* parent)
    : QWidget(parent), min_(new QDoubleSpinBox(this)), max_(new QDoubleSpinBox(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(SpectraPropertyRow::row_height());

    min_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    max_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    min_->setFixedHeight(SpectraPropertyRow::row_height());
    max_->setFixedHeight(SpectraPropertyRow::row_height());
    min_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    max_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    min_->setDecimals(3);
    max_->setDecimals(3);
    min_->setRange(-1e9, 1e9);
    max_->setRange(-1e9, 1e9);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(min_);
    layout->addWidget(max_);
}

void SpectraRangeRow::paintEvent(QPaintEvent*)
{
    // No-op: value widgets are styled by the application stylesheet.
    QPainter p(this);
    Q_UNUSED(p)
}

// ─── ColorField ─────────────────────────────────────────────────────────────────

SpectraColorField::SpectraColorField(const QString& label, QWidget* parent)
    : QWidget(parent), label_(label)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Legacy `widgets::color_field` is exactly as tall as its 28px swatch.
    setFixedHeight(28);
    setCursor(Qt::PointingHandCursor);
}

void SpectraColorField::setColor(const spectra::Color& c)
{
    color_ = c;
    update();
}

void SpectraColorField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    // 28×28 swatch with token radius (RADIUS_MD = 8).
    constexpr int swatch_sz = 28;
    const int     swatch_y  = (height() - swatch_sz) / 2;
    QRectF        swatch_rect(0, swatch_y, swatch_sz, swatch_sz);

    // Checkerboard for alpha < 1.
    if (color_.a < 1.0f)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x66, 0x66, 0x66));
        p.drawRoundedRect(swatch_rect, g.radius_md, g.radius_md);
        p.setBrush(QColor(0x99, 0x99, 0x99));
        const float half = swatch_sz * 0.5f;
        p.drawRect(QRectF(swatch_rect.x() + half, swatch_rect.y(), half, half));
        p.drawRect(QRectF(swatch_rect.x(), swatch_rect.y() + half, half, half));
    }

    QColor qc = to_qcolor(color_);
    p.setBrush(qc);
    p.setPen(QPen(c.border_subtle, 1));
    p.drawRoundedRect(swatch_rect, g.radius_md, g.radius_md);

    // Hover highlight
    if (underMouse())
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.cyan_accent, 2));
        p.drawRoundedRect(swatch_rect.adjusted(-1, -1, 1, 1), g.radius_md + 1, g.radius_md + 1);
    }

    // Label — legacy `widgets::color_field` prints it at full text_primary in the
    // 16px body font, 13px to the right of the swatch.
    constexpr int label_gap = 13;
    p.setFont(row_font());
    p.setPen(c.text_primary);
    p.drawText(QRect(swatch_sz + label_gap, 0, width() - swatch_sz - label_gap, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               label_);
}

void SpectraColorField::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    std::optional<spectra::Color> chosen;
    if (picker_)
    {
        chosen = picker_(label_, color_);
    }
    else
    {
        QColor initial = to_qcolor(color_);
        QColor qc      = QColorDialog::getColor(
            initial,
            this,
            label_,
            QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
        if (qc.isValid())
            chosen = from_qcolor(qc);
    }

    if (chosen && to_qcolor(*chosen) != to_qcolor(color_))
    {
        color_ = *chosen;
        emit colorChanged(color_);
        update();
    }
}

// ─── InfoRow ──────────────────────────────────────────────────────────────────

SpectraInfoRow::SpectraInfoRow(const QString& label, const QString& value, QWidget* parent)
    : QWidget(parent), label_(label), value_(value)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kRowHeight);
}

void SpectraInfoRow::setLabel(const QString& label)
{
    label_ = label;
    update();
}

void SpectraInfoRow::setValue(const QString& value)
{
    if (value == value_)
        return;
    value_ = value;
    update();
}

void SpectraInfoRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    p.setFont(row_font());

    // Legacy `widgets::info_row` places the value at 45% of the available width.
    const int split = static_cast<int>(width() * 0.45);

    p.setPen(c.text_secondary);
    p.drawText(QRect(0, 0, split, height()), Qt::AlignLeft | Qt::AlignVCenter, label_);

    p.setPen(c.text_primary);
    p.drawText(QRect(split, 0, width() - split, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               value_);
}

// ─── SliderField ──────────────────────────────────────────────────────────────

SpectraSliderField::SpectraSliderField(const QString& label,
                                       double         minimum,
                                       double         maximum,
                                       int            decimals,
                                       QString        suffix,
                                       QWidget*       parent)
    : QWidget(parent), label_(label), min_(minimum), max_(maximum), value_(minimum),
      decimals_(decimals), suffix_(std::move(suffix))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(SpectraPropertyRow::row_height());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void SpectraSliderField::setValue(double value)
{
    const double clamped = std::clamp(value, min_, max_);
    if (qFuzzyCompare(clamped + 1.0, value_ + 1.0))
        return;
    value_ = clamped;
    emit valueChanged(value_);
    update();
}

QRect SpectraSliderField::trackRect() const
{
    const int label_w = SpectraPropertyRow::kLabelWidth;
    return QRect(label_w, 0, std::max(1, width() - label_w), height());
}

QString SpectraSliderField::formatted() const
{
    return QString::number(value_, 'f', decimals_) + suffix_;
}

void SpectraSliderField::setFromPos(int x)
{
    const QRect track = trackRect();
    if (track.width() <= 0 || max_ <= min_)
        return;
    const double t = std::clamp(static_cast<double>(x - track.left()) / track.width(), 0.0, 1.0);
    setValue(min_ + t * (max_ - min_));
}

void SpectraSliderField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    p.setFont(row_font());

    // Label
    p.setPen(c.text_secondary);
    p.drawText(QRect(0, 0, SpectraPropertyRow::kLabelWidth, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               label_);

    const QRect      track    = trackRect();
    const double     center_y = track.center().y() + 0.5;
    constexpr double track_h  = 4.0;

    // Track background
    QRectF track_bg(track.left(), center_y - track_h * 0.5, track.width(), track_h);
    p.setPen(Qt::NoPen);
    p.setBrush(c.bg_tertiary);
    p.drawRoundedRect(track_bg, track_h * 0.5, track_h * 0.5);

    // Accent fill
    const double t    = (max_ > min_) ? std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0) : 0.0;
    QColor       fill = c.cyan_accent;
    fill.setAlphaF(0.60f);
    QRectF fill_rect(track.left(), center_y - track_h * 0.5, track.width() * t, track_h);
    p.setBrush(fill);
    p.drawRoundedRect(fill_rect, track_h * 0.5, track_h * 0.5);

    // Thumb (14px pill)
    constexpr double thumb_d = 14.0;
    const double     thumb_x = track.left() + thumb_d * 0.5 + (track.width() - thumb_d) * t;
    p.setBrush(dragging_ ? c.cyan_accent.lighter(115) : c.cyan_accent);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(thumb_x, center_y), thumb_d * 0.5, thumb_d * 0.5);

    // Value text, centered in the track like the legacy ImGui slider.
    p.setPen(c.text_primary);
    p.drawText(track, Qt::AlignCenter, formatted());
}

void SpectraSliderField::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = true;
    setFromPos(static_cast<int>(event->position().x()));
    update();
}

void SpectraSliderField::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_)
        return;
    setFromPos(static_cast<int>(event->position().x()));
}

void SpectraSliderField::mouseReleaseEvent(QMouseEvent*)
{
    if (!dragging_)
        return;
    dragging_ = false;
    update();
}

void SpectraSliderField::leaveEvent(QEvent*)
{
    update();
}

// ─── Separator ────────────────────────────────────────────────────────────────

SpectraSeparator::SpectraSeparator(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(1);
}

void SpectraSeparator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QColor   sep = spectra_colors().border_subtle;
    sep.setAlphaF(0.30f);
    p.setPen(QPen(sep, 1));
    p.drawLine(0, 0, width(), 0);
}

// ─── SeparatorLabel ───────────────────────────────────────────────────────────

SpectraSeparatorLabel::SpectraSeparatorLabel(const QString& text, QWidget* parent)
    : QWidget(parent), text_(text)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(20);
}

void SpectraSeparatorLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    QFont f = SpectraFontManager::instance().font_medium();
    f.setPixelSize(imgui_font_px(kImGuiHeadingSize));
    p.setFont(f);

    const QFontMetrics fm(f);
    const int          text_w = fm.horizontalAdvance(text_);
    const int          gap    = static_cast<int>(ui::tokens::SPACE_2);
    const int          text_x = (width() - text_w) / 2;
    const int          line_y = height() / 2;

    p.setPen(QPen(c.border_subtle, 1));
    if (text_x > gap)
        p.drawLine(0, line_y, text_x - gap, line_y);
    const int right_start = text_x + text_w + gap;
    if (right_start < width())
        p.drawLine(right_start, line_y, width(), line_y);

    p.setPen(c.text_muted);
    p.drawText(rect(), Qt::AlignCenter, text_);
}

// ─── SwatchLabel ──────────────────────────────────────────────────────────────

SpectraSwatchLabel::SpectraSwatchLabel(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(20);
}

void SpectraSwatchLabel::setColor(const spectra::Color& c)
{
    color_ = c;
    update();
}

void SpectraSwatchLabel::setText(const QString& text)
{
    text_ = text;
    update();
}

void SpectraSwatchLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    // Legacy `widgets::color_swatch(col, 16.0f)` followed by SPACE_2 and the type name.
    constexpr int sz = 16;
    const int     y  = (height() - sz) / 2;
    QRectF        swatch(0, y, sz, sz);
    p.setBrush(to_qcolor(color_));
    p.setPen(QPen(c.border_subtle, 1));
    p.drawRoundedRect(swatch, g.radius_sm, g.radius_sm);

    p.setFont(row_font());
    p.setPen(c.text_secondary);
    const int text_x = sz + static_cast<int>(ui::tokens::SPACE_2);
    p.drawText(QRect(text_x, 0, width() - text_x, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               text_);
}

// ─── ToggleField ──────────────────────────────────────────────────────────────

SpectraToggleField::SpectraToggleField(const QString& label, QWidget* parent)
    : QWidget(parent), label_(label)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(SpectraPropertyRow::row_height());
    setCursor(Qt::PointingHandCursor);
}

void SpectraToggleField::setChecked(bool checked)
{
    if (checked == checked_)
        return;
    checked_ = checked;
    emit toggled(checked_);
    update();
}

void SpectraToggleField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Label — legacy `widgets::toggle_field` uses the 16px body font.
    p.setFont(row_font());
    p.setPen(c.text_secondary);
    p.drawText(QRect(0, 0, width() - 42, height()), Qt::AlignLeft | Qt::AlignVCenter, label_);

    // Switch track
    constexpr int sw_w = 34;
    constexpr int sw_h = 18;
    const int     sw_x = width() - sw_w - 4;
    const int     sw_y = (height() - sw_h) / 2;
    QRectF        track(sw_x, sw_y, sw_w, sw_h);

    QColor track_bg = checked_ ? c.cyan_accent : c.bg_tertiary;
    if (!checked_ && underMouse())
    {
        track_bg = c.bg_tertiary.lighter(110);
    }
    p.setBrush(track_bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(track, sw_h / 2.0, sw_h / 2.0);

    // Knob
    constexpr int knob_r = sw_h / 2 - 2;
    const int     knob_x = checked_ ? sw_x + sw_w - knob_r - 2 : sw_x + knob_r + 2;
    const int     knob_y = sw_y + sw_h / 2;
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(knob_x, knob_y), knob_r, knob_r);
}

void SpectraToggleField::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        setChecked(!checked_);
}

// ─── ReferenceLineRow ───────────────────────────────────────────────────────────

SpectraReferenceLineRow::SpectraReferenceLineRow(const spectra::Color& color,
                                                 const QString&        label,
                                                 QWidget*              parent)
    : QWidget(parent), color_(color), label_(label), delete_btn_(new QPushButton(this)),
      dot_(new QLabel(this)), label_lbl_(new QLabel(label, this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    dot_->setFixedSize(12, 12);
    updateDot();
    layout->addWidget(dot_);

    label_lbl_->setFont(SpectraFontManager::instance().font_small());
    QPalette pal = label_lbl_->palette();
    pal.setColor(QPalette::WindowText, spectra_colors().text_secondary);
    label_lbl_->setPalette(pal);
    layout->addWidget(label_lbl_, 1);

    delete_btn_->setFixedSize(24, 24);
    delete_btn_->setToolTip("Remove reference line");
    delete_btn_->setFlat(true);

    QFont icon_font = SpectraFontManager::instance().font_icon(12);
    delete_btn_->setFont(icon_font);
    delete_btn_->setText(
        SpectraFontManager::icon_codepoint(static_cast<uint32_t>(ui::Icon::Trash)));

    QPalette btn_pal = delete_btn_->palette();
    btn_pal.setColor(QPalette::ButtonText, spectra_colors().error_red);
    delete_btn_->setPalette(btn_pal);
    delete_btn_->setStyleSheet("QPushButton { border: none; background: transparent; }");

    connect(delete_btn_, &QPushButton::clicked, this, &SpectraReferenceLineRow::deleteClicked);

    layout->addWidget(delete_btn_);
}

void SpectraReferenceLineRow::setColor(const spectra::Color& c)
{
    color_ = c;
    updateDot();
}

void SpectraReferenceLineRow::setLabel(const QString& label)
{
    label_ = label;
    if (label_lbl_)
        label_lbl_->setText(label_);
}

void SpectraReferenceLineRow::updateDot()
{
    if (!dot_)
        return;
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(to_qcolor(color_));
    p.drawRoundedRect(pixmap.rect(), 6, 6);
    dot_->setPixmap(pixmap);
}

void SpectraReferenceLineRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Subtle hover background on the whole row.
    if (underMouse())
    {
        const auto& c = spectra_colors();
        QColor      bg(c.elevated_surface);
        bg.setAlphaF(0.35f);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    }
}

// ─── SeriesListView ────────────────────────────────────────────────────────────

static QPixmap make_series_icon(const spectra::Color& color, bool visible)
{
    constexpr int dot_sz = 10;
    constexpr int icon_w = 44;
    constexpr int icon_h = 20;

    QPixmap pixmap(icon_w, icon_h);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = spectra_colors();

    // Checkerboard for alpha < 1.
    if (color.a < 1.0f)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x66, 0x66, 0x66));
        p.drawRoundedRect(2, 5, dot_sz, dot_sz, dot_sz / 2.0, dot_sz / 2.0);
        p.setBrush(QColor(0x99, 0x99, 0x99));
        const int half = dot_sz / 2;
        p.drawRect(2 + half, 5, half, half);
        p.drawRect(2, 5 + half, half, half);
    }

    QColor qc = to_qcolor(color);
    p.setBrush(qc);
    p.setPen(QPen(c.border_subtle, 1));
    p.drawRoundedRect(2, 5, dot_sz, dot_sz, dot_sz / 2.0, dot_sz / 2.0);

    // Eye
    QFont icon_f = SpectraFontManager::instance().font_icon(12);
    p.setFont(icon_f);
    p.setPen(visible ? c.text_primary : c.text_muted);
    p.drawText(QRect(16, 0, 20, 20),
               Qt::AlignCenter,
               SpectraFontManager::icon_codepoint(
                   static_cast<uint32_t>(visible ? ui::Icon::Eye : ui::Icon::EyeOff)));

    return pixmap;
}

SpectraSeriesListView::SpectraSeriesListView(QWidget* parent)
    : QWidget(parent), list_(new QListWidget(this)), bulk_bar_(new QWidget(this)),
      copy_btn_(new QPushButton(this)), cut_btn_(new QPushButton(this)),
      delete_btn_(new QPushButton(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // Bulk action bar
    auto* bulk_layout = new QHBoxLayout(bulk_bar_);
    bulk_layout->setContentsMargins(0, 0, 0, 0);
    bulk_layout->setSpacing(8);

    QFont icon_f = SpectraFontManager::instance().font_icon(12);
    copy_btn_->setFont(icon_f);
    cut_btn_->setFont(icon_f);
    delete_btn_->setFont(icon_f);

    copy_btn_->setText(
        SpectraFontManager::icon_codepoint(static_cast<uint32_t>(ui::Icon::Duplicate))
        + QStringLiteral(" Copy"));
    cut_btn_->setText(SpectraFontManager::icon_codepoint(static_cast<uint32_t>(ui::Icon::Scissors))
                      + QStringLiteral(" Cut"));
    delete_btn_->setText(SpectraFontManager::icon_codepoint(static_cast<uint32_t>(ui::Icon::Trash))
                         + QStringLiteral(" Delete"));

    const auto& c = spectra_colors();
    const auto& g = spectra_geometry();

    QString btn_style = QString("QPushButton {"
                                "  border: 1px solid %1;"
                                "  border-radius: %2px;"
                                "  padding: 4px 8px;"
                                "  background: %3;"
                                "  color: %4;"
                                "}"
                                "QPushButton:hover {"
                                "  background: %5;"
                                "}")
                            .arg(c.border_subtle.name(QColor::HexArgb))
                            .arg(g.radius_sm)
                            .arg(c.bg_tertiary.name(QColor::HexArgb))
                            .arg(c.text_secondary.name(QColor::HexArgb))
                            .arg(c.elevated_surface.name(QColor::HexArgb));

    copy_btn_->setStyleSheet(btn_style);
    cut_btn_->setStyleSheet(btn_style);
    delete_btn_->setStyleSheet(btn_style);

    bulk_layout->addWidget(copy_btn_);
    bulk_layout->addWidget(cut_btn_);
    bulk_layout->addWidget(delete_btn_);
    bulk_layout->addStretch();

    list_->setObjectName(QStringLiteral("series_list"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setIconSize(QSize(44, 20));
    list_->setSpacing(2);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateListHeight();

    layout->addWidget(bulk_bar_);
    layout->addWidget(list_);

    connect(list_,
            &QListWidget::currentRowChanged,
            this,
            &SpectraSeriesListView::currentRowChanged);
    connect(copy_btn_, &QPushButton::clicked, this, &SpectraSeriesListView::copySelected);
    connect(cut_btn_, &QPushButton::clicked, this, &SpectraSeriesListView::cutSelected);
    connect(delete_btn_, &QPushButton::clicked, this, &SpectraSeriesListView::deleteSelected);
}

void SpectraSeriesListView::updateListHeight()
{
    if (!list_)
        return;
    const int rows      = std::max(1, list_->count());
    const int row_h     = static_cast<int>(ui::tokens::SERIES_ROW_HEIGHT);
    const int spacing   = list_->spacing() * 2;
    const int content_h = rows * (row_h + spacing);
    // Cap so a long series list still leaves the property sections reachable.
    list_->setFixedHeight(std::clamp(content_h + 4, row_h, row_h * 10));
}

void SpectraSeriesListView::clear()
{
    if (list_)
    {
        list_->clear();
        updateListHeight();
    }
}

int SpectraSeriesListView::count() const
{
    return list_ ? list_->count() : 0;
}

QListWidgetItem* SpectraSeriesListView::addSeries(const spectra::Color& color,
                                                  const QString&        label,
                                                  bool                  visible,
                                                  const QVariant&       user_data)
{
    if (!list_)
        return nullptr;

    auto* item = new QListWidgetItem(label, list_);
    item->setIcon(QIcon(make_series_icon(color, visible)));
    item->setData(Qt::UserRole, user_data);
    item->setSizeHint(QSize(0, static_cast<int>(ui::tokens::SERIES_ROW_HEIGHT)));
    updateListHeight();
    return item;
}

int SpectraSeriesListView::currentRow() const
{
    return list_ ? list_->currentRow() : -1;
}

void SpectraSeriesListView::setCurrentRow(int row)
{
    if (list_)
        list_->setCurrentRow(row);
}

QListWidgetItem* SpectraSeriesListView::itemAt(int row) const
{
    return list_ ? list_->item(row) : nullptr;
}

std::vector<int> SpectraSeriesListView::selectedRows() const
{
    std::vector<int> rows;
    if (!list_)
        return rows;
    const auto items = list_->selectedItems();
    rows.reserve(items.size());
    for (auto* item : items)
        rows.push_back(list_->row(item));
    return rows;
}

std::vector<QListWidgetItem*> SpectraSeriesListView::selectedItems() const
{
    if (!list_)
        return {};
    const auto items = list_->selectedItems();
    return std::vector<QListWidgetItem*>(items.begin(), items.end());
}

// ─── DragSpinBox ──────────────────────────────────────────────────────────────

SpectraDragSpinBox::SpectraDragSpinBox(QWidget* parent) : QDoubleSpinBox(parent)
{
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    // Legacy `widgets::drag_field` renders the value centered inside a 36px frame
    // using the 16px body font.
    setAlignment(Qt::AlignCenter);
    setFont(row_font());
    setFixedHeight(SpectraPropertyRow::row_height());
}

}   // namespace spectra::adapters::qt
