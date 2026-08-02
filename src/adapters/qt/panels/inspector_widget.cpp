// inspector_widget.cpp — Qt inspector panel implementation.

#include "inspector_widget.hpp"

#include "app/application_services.hpp"
#include "ui/commands/undoable_property.hpp"
#include "app/frontend_services.hpp"
#include "../components/spectra_design_tokens.hpp"
#include "ui/theme/design_tokens.hpp"
#include "../components/spectra_inspector_widgets.hpp"
#include "data_editor_widget.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <limits>
#include <numeric>
#include <span>

#include "ui/input/selection_context.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QFont>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{
namespace
{

AxesBase* figure_axes_at(Figure* figure, int index)
{
    if (!figure || index < 0)
        return nullptr;

    AxesBase* result  = nullptr;
    int       current = 0;
    figure->for_each_axes(
        [&](AxesBase* axes)
        {
            if (current == index)
                result = axes;
            ++current;
        });
    return result;
}

void set_line_edit_from_model(QLineEdit* edit, const std::string& value)
{
    if (!edit)
        return;
    const QString text = QString::fromStdString(value);
    if (edit->text() == text)
        return;
    const QSignalBlocker blocker(edit);
    edit->setText(text);
}

void set_spin_from_model(QDoubleSpinBox* spin, double value)
{
    if (!spin || qFuzzyCompare(spin->value() + 1.0, value + 1.0))
        return;
    const QSignalBlocker blocker(spin);
    spin->setValue(value);
}

void request_redraw(RedrawRequest* redraw, FigureId figure_id)
{
    if (redraw)
        redraw->request_redraw(figure_id);
}

// ─── Series data extraction (shared by sparkline + statistics) ───────────────

void get_series_data(const Series&           s,
                     std::span<const float>& x_data,
                     std::span<const float>& y_data,
                     size_t&                 count)
{
    x_data = {};
    y_data = {};
    count  = 0;

    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
    {
        x_data = line->x_data();
        y_data = line->y_data();
        count  = line->point_count();
    }
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
    {
        x_data = scatter->x_data();
        y_data = scatter->y_data();
        count  = scatter->point_count();
    }
    else if (const auto* line3d = dynamic_cast<const LineSeries3D*>(&s))
    {
        x_data = line3d->x_data();
        y_data = line3d->y_data();
        count  = line3d->point_count();
    }
    else if (const auto* scatter3d = dynamic_cast<const ScatterSeries3D*>(&s))
    {
        x_data = scatter3d->x_data();
        y_data = scatter3d->y_data();
        count  = scatter3d->point_count();
    }
}

QString format_stat(double v)
{
    if (!std::isfinite(v))
        return QStringLiteral("—");
    return QString::number(v, 'g', 6);
}

// ─── Reference-line undo helpers ─────────────────────────────────────────────

struct RefLineSnapshot
{
    bool           is_hline = true;
    double         value    = 0.0;
    spectra::Color color    = colors::blue;
    std::string    label;
    LineStyle      line_style   = LineStyle::Solid;
    float          line_width   = 2.0f;
    float          opacity      = 1.0f;
    MarkerStyle    marker_style = MarkerStyle::None;
    float          marker_size  = 5.0f;
};

RefLineSnapshot capture_reference_line(const Series& s)
{
    RefLineSnapshot snap;
    snap.color        = s.color();
    snap.label        = s.label();
    snap.line_style   = s.line_style();
    snap.opacity      = s.opacity();
    snap.marker_style = s.marker_style();
    snap.marker_size  = s.marker_size();

    if (const auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        snap.line_width = ls->width();
        auto xs         = ls->x_data();
        auto ys         = ls->y_data();
        if (xs.size() >= 2 && ys.size() >= 2)
        {
            if (qFuzzyCompare(static_cast<double>(xs[0]) + 1.0, static_cast<double>(xs[1]) + 1.0))
            {
                snap.is_hline = false;
                snap.value    = static_cast<double>(xs[0]);
            }
            else
            {
                snap.is_hline = true;
                snap.value    = static_cast<double>(ys[0]);
            }
        }
    }
    else if (const auto* ls3d = dynamic_cast<const LineSeries3D*>(&s))
    {
        snap.line_width = ls3d->width();
        auto xs         = ls3d->x_data();
        auto ys         = ls3d->y_data();
        if (xs.size() >= 2 && ys.size() >= 2)
        {
            if (qFuzzyCompare(static_cast<double>(xs[0]) + 1.0, static_cast<double>(xs[1]) + 1.0))
            {
                snap.is_hline = false;
                snap.value    = static_cast<double>(xs[0]);
            }
            else
            {
                snap.is_hline = true;
                snap.value    = static_cast<double>(ys[0]);
            }
        }
    }
    return snap;
}

bool matches_reference_line(const Series& s, const RefLineSnapshot& snap)
{
    if (!s.is_reference_line())
        return false;
    const auto candidate = capture_reference_line(s);
    return candidate.is_hline == snap.is_hline
           && qFuzzyCompare(candidate.value + 1.0, snap.value + 1.0);
}

void remove_matching_reference_line(Axes& ax, const RefLineSnapshot& snap)
{
    auto& series = ax.series_mut();
    for (size_t i = series.size(); i-- > 0;)
    {
        if (series[i] && matches_reference_line(*series[i], snap))
        {
            ax.remove_series(i);
            break;
        }
    }
}

void restore_reference_line(Axes& ax, const RefLineSnapshot& snap)
{
    LineSeries& ref = snap.is_hline ? ax.hline(snap.value, "-") : ax.vline(snap.value, "-");
    ref.set_color(snap.color);
    ref.label(snap.label);
    ref.line_style(snap.line_style);
    ref.opacity(snap.opacity);
    ref.marker_style(snap.marker_style);
    ref.marker_size(snap.marker_size);
    if (auto* ls = dynamic_cast<LineSeries*>(&ref))
        ls->width(snap.line_width);
    ref.set_reference_line(true);
    ref.set_show_in_legend(false);
    ref.set_excluded_from_autoscale(true);
}

void undoable_add_hline(UndoManager* mgr, Axes& ax, double y, const std::string& fmt)
{
    ax.hline(y, fmt);
    if (mgr)
    {
        RefLineSnapshot snap;
        snap.is_hline = true;
        snap.value    = y;
        if (!ax.series().empty())
            snap = capture_reference_line(*ax.series().back());

        mgr->push(UndoAction{"Add horizontal reference line",
                             [&ax, snap]() { remove_matching_reference_line(ax, snap); },
                             [&ax, y, fmt]() { ax.hline(y, fmt); }});
    }
}

void undoable_add_vline(UndoManager* mgr, Axes& ax, double x, const std::string& fmt)
{
    ax.vline(x, fmt);
    if (mgr)
    {
        RefLineSnapshot snap;
        snap.is_hline = false;
        snap.value    = x;
        if (!ax.series().empty())
            snap = capture_reference_line(*ax.series().back());

        mgr->push(UndoAction{"Add vertical reference line",
                             [&ax, snap]() { remove_matching_reference_line(ax, snap); },
                             [&ax, x, fmt]() { ax.vline(x, fmt); }});
    }
}

void undoable_delete_reference_line(UndoManager* mgr, Axes& ax, size_t index)
{
    if (index >= ax.series().size())
        return;
    Series* s = ax.series()[index].get();
    if (!s || !s->is_reference_line())
        return;

    const RefLineSnapshot snap = capture_reference_line(*s);
    ax.remove_series(index);
    if (mgr)
    {
        mgr->push(UndoAction{"Delete reference line",
                             [&ax, snap]() { restore_reference_line(ax, snap); },
                             [&ax, snap]() { remove_matching_reference_line(ax, snap); }});
    }
}

enum class Axis3DDimension
{
    X,
    Y,
    Z
};

AxisLimits axes3d_limits(const Axes3D& axes, Axis3DDimension dimension)
{
    switch (dimension)
    {
        case Axis3DDimension::X:
            return axes.x_limits();
        case Axis3DDimension::Y:
            return axes.y_limits();
        case Axis3DDimension::Z:
            return axes.z_limits();
    }
    return {};
}

void set_axes3d_limits(Axes3D& axes, Axis3DDimension dimension, const AxisLimits& limits)
{
    switch (dimension)
    {
        case Axis3DDimension::X:
            axes.xlim(limits.min, limits.max);
            break;
        case Axis3DDimension::Y:
            axes.ylim(limits.min, limits.max);
            break;
        case Axis3DDimension::Z:
            axes.zlim(limits.min, limits.max);
            break;
    }
}

void undoable_set_axes3d_limits(UndoManager*    undo,
                                Axes3D&         axes,
                                Axis3DDimension dimension,
                                AxisLimits      limits)
{
    const AxisLimits before = axes3d_limits(axes, dimension);
    if (qFuzzyCompare(before.min + 1.0, limits.min + 1.0)
        && qFuzzyCompare(before.max + 1.0, limits.max + 1.0))
        return;

    set_axes3d_limits(axes, dimension, limits);
    if (undo)
    {
        auto* target = &axes;
        undo->push(UndoAction{
            "Change 3D axis limits",
            [target, dimension, before]() { set_axes3d_limits(*target, dimension, before); },
            [target, dimension, limits]() { set_axes3d_limits(*target, dimension, limits); }});
    }
}

std::string axes3d_label(const Axes3D& axes, Axis3DDimension dimension)
{
    switch (dimension)
    {
        case Axis3DDimension::X:
            return axes.xlabel();
        case Axis3DDimension::Y:
            return axes.ylabel();
        case Axis3DDimension::Z:
            return axes.zlabel();
    }
    return {};
}

void set_axes3d_label(Axes3D& axes, Axis3DDimension dimension, const std::string& label)
{
    switch (dimension)
    {
        case Axis3DDimension::X:
            axes.xlabel(label);
            break;
        case Axis3DDimension::Y:
            axes.ylabel(label);
            break;
        case Axis3DDimension::Z:
            axes.zlabel(label);
            break;
    }
}

void undoable_set_axes3d_label(UndoManager*       undo,
                               Axes3D&            axes,
                               Axis3DDimension    dimension,
                               const std::string& label)
{
    const std::string before = axes3d_label(axes, dimension);
    if (before == label)
        return;

    set_axes3d_label(axes, dimension, label);
    if (undo)
    {
        auto* target = &axes;
        undo->push(UndoAction{
            "Change 3D axis label",
            [target, dimension, before]() { set_axes3d_label(*target, dimension, before); },
            [target, dimension, label]() { set_axes3d_label(*target, dimension, label); }});
    }
}

QPushButton* make_color_button(QWidget* parent, const spectra::Color& c)
{
    auto* btn = new QPushButton(parent);
    btn->setFlat(true);
    btn->setFixedSize(24, 24);
    const QColor qc = to_qcolor(c);
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
                           .arg(qc.name(QColor::HexArgb))
                           .arg(qc.darker(120).name()));
    return btn;
}

void update_color_button(QPushButton* btn, const spectra::Color& c)
{
    if (!btn)
        return;
    const QColor qc = to_qcolor(c);
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
                           .arg(qc.name(QColor::HexArgb))
                           .arg(qc.darker(120).name()));
}

// ─── Collapsible section using a QToolButton header and a content frame ─────
// Every inspector form shares one rhythm: a token label column, comfortable
// vertical spacing, and fields that shrink with the fixed-width drawer instead
// of forcing a horizontal scrollbar.
QFormLayout* make_inspector_form()
{
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(static_cast<int>(ui::tokens::SPACE_2));
    form->setVerticalSpacing(static_cast<int>(ui::tokens::SPACE_3));
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

// Form labels must read like SpectraPropertyRow labels, and the fields must be
// able to shrink to the fixed drawer width rather than clipping.
void style_inspector_form_row(QFormLayout* form, QWidget* field)
{
    if (!form)
        return;
    if (auto* label = qobject_cast<QLabel*>(form->labelForField(field)))
    {
        label->setFont(SpectraFontManager::instance().font_small());
        label->setStyleSheet(QString("color: %1; background: transparent;")
                                 .arg(spectra_colors().text_secondary.name(QColor::HexArgb)));
        label->setMinimumWidth(static_cast<int>(ui::tokens::INSPECTOR_LABEL_WIDTH) - 12);
    }
    if (auto* edit = qobject_cast<QLineEdit*>(field))
    {
        edit->setMinimumWidth(56);
        edit->setFixedHeight(ui::tokens::INSPECTOR_INPUT_HEIGHT);
    }
    else if (auto* combo = qobject_cast<QComboBox*>(field))
    {
        combo->setMinimumWidth(56);
        combo->setFixedHeight(ui::tokens::INSPECTOR_INPUT_HEIGHT);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }
}

// Applies the row treatment to every row already added to the form.
void style_inspector_form(QFormLayout* form)
{
    if (!form)
        return;
    for (int row = 0; row < form->rowCount(); ++row)
    {
        QLayoutItem* item = form->itemAt(row, QFormLayout::FieldRole);
        style_inspector_form_row(form, item ? item->widget() : nullptr);
    }
}

// Matches SpectraDragSpinBox presentation for the spin boxes that are still
// constructed directly (axes limits, widths, tick lengths).
void configure_inspector_spin(QDoubleSpinBox* spin)
{
    if (!spin)
        return;
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignCenter);
    spin->setFixedHeight(ui::tokens::INSPECTOR_INPUT_HEIGHT);
    spin->setMinimumWidth(56);
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// Wraps the shared SpectraSectionHeader so every inspector section — not just
// the Figure page — gets the legacy band + chevron treatment while keeping the
// existing content_layout() call sites unchanged.
class CollapsibleSection : public QWidget
{
   public:
    CollapsibleSection(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent), header_(new SpectraSectionHeader(title, this)),
          content_(new QWidget(this)), content_layout_(new QVBoxLayout(content_))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        content_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        content_layout_->setContentsMargins(static_cast<int>(ui::tokens::SPACE_2),
                                            static_cast<int>(ui::tokens::SPACE_1),
                                            static_cast<int>(ui::tokens::SPACE_2),
                                            static_cast<int>(ui::tokens::SPACE_2));
        content_layout_->setSpacing(static_cast<int>(ui::tokens::SPACE_2));

        layout->addWidget(header_);
        layout->addWidget(content_);

        connect(header_, &SpectraSectionHeader::toggled, content_, &QWidget::setVisible);
    }

    QVBoxLayout* content_layout() const { return content_layout_; }

   private:
    SpectraSectionHeader* header_         = nullptr;
    QWidget*              content_        = nullptr;
    QVBoxLayout*          content_layout_ = nullptr;
};

}   // namespace

// ─── Sparkline preview widget ────────────────────────────────────────────────
class SparklineWidget : public QWidget
{
   public:
    explicit SparklineWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(80);
        setMaximumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void set_series(const spectra::Series* s)
    {
        series_ = s;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const auto& c = spectra_colors();
        p.fillRect(rect(), c.input_surface);

        if (!series_)
            return;

        std::span<const float> x_data;
        std::span<const float> y_data;
        size_t                 count = 0;
        get_series_data(*series_, x_data, y_data, count);

        if (count == 0 || y_data.empty())
        {
            p.setPen(c.text_muted);
            p.drawText(rect(), Qt::AlignCenter, "No data");
            return;
        }

        const float ymin = *std::min_element(y_data.begin(), y_data.end());
        const float ymax = *std::max_element(y_data.begin(), y_data.end());
        float       y0   = ymin;
        float       y1   = ymax;
        if (qFuzzyCompare(y0 + 1.0f, y1 + 1.0f))
        {
            y0 -= 1.0f;
            y1 += 1.0f;
        }

        constexpr int pad = 4;
        const int     w   = std::max(2, width() - 2 * pad);
        const int     h   = std::max(2, height() - 2 * pad);
        if (w <= 0 || h <= 0)
            return;

        size_t samples = count;
        if (count > static_cast<size_t>(w))
            samples = static_cast<size_t>(w);
        if (samples == 0)
            samples = 1;

        const QColor line_color = to_qcolor(series_->color());
        QPen         pen(line_color);
        pen.setWidthF(2.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        QPolygonF poly;
        for (size_t i = 0; i < samples; ++i)
        {
            const size_t src    = (samples < count) ? (i * count / samples) : i;
            const float  y      = y_data[src];
            const float  y_frac = (y - y0) / (y1 - y0);
            const int    px =
                pad
                + static_cast<int>(static_cast<double>(i) * w / static_cast<double>(samples - 1));
            const int py = pad + static_cast<int>((1.0 - y_frac) * h);
            poly.append(QPointF(px, py));
        }

        if (poly.size() > 1)
            p.drawPolyline(poly);
        else
            p.drawPoint(poly.first());
    }

   private:
    const spectra::Series* series_ = nullptr;
};

QtInspectorWidget::QtInspectorWidget(FigureRegistry*      registry,
                                     ApplicationServices* services,
                                     QWidget*             parent)
    : QWidget(parent), registry_(registry), services_(services),
      ctx_(std::make_unique<ui::SelectionContext>())
{
    setObjectName("inspector_panel");
    build_ui();
}

void QtInspectorWidget::build_ui()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    section_selector_ = new SpectraSegmentedControl(this);
    section_selector_->setObjectName("inspector_section_tabs");
    section_selector_->setItems({"Figure", "Series", "Axes", "Data"});
    section_stack_ = new QStackedWidget(this);
    section_stack_->setObjectName("inspector_section_stack");

    build_figure_page();
    build_series_page();
    build_axes_page();
    build_data_page();

    section_stack_->addWidget(figure_scroll_);
    section_stack_->addWidget(series_scroll_);
    section_stack_->addWidget(axes_scroll_);
    section_stack_->addWidget(data_scroll_);

    layout->addWidget(section_selector_);
    layout->addWidget(section_stack_, 1);

    connect(section_selector_,
            &SpectraSegmentedControl::currentIndexChanged,
            this,
            [this](int index)
            {
                section_ = static_cast<Section>(index);
                section_stack_->setCurrentIndex(index);
            });
}

void QtInspectorWidget::set_section(Section s)
{
    int index = static_cast<int>(s);
    if (section_selector_)
        section_selector_->setCurrentIndex(index);
    if (section_stack_)
        section_stack_->setCurrentIndex(index);
    section_ = s;
}

void QtInspectorWidget::build_figure_page()
{
    figure_scroll_ = new QScrollArea(this);
    figure_scroll_->setWidgetResizable(true);
    figure_scroll_->setFrameShape(QFrame::NoFrame);
    // The inspector is a fixed-width drawer: content must reflow, never pan.
    figure_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget();
    container->setObjectName("inspector_figure_page");
    figure_layout_ = new QVBoxLayout(container);
    figure_layout_->setContentsMargins(12, 12, 12, 12);
    figure_layout_->setSpacing(8);

    // Header: title + subtitle
    figure_title_ = new SpectraPanelTitle(container);
    figure_title_->setTitle("Figure");
    figure_layout_->addWidget(figure_title_);

    // Hidden test-only labels (kept for existing tests; updated in refresh).
    figure_size_label_       = new QLabel(container);
    figure_axes_count_label_ = new QLabel(container);
    figure_size_label_->setObjectName("figure_size");
    figure_axes_count_label_->setObjectName("figure_axes_count");
    figure_size_label_->setVisible(false);
    figure_axes_count_label_->setVisible(false);

    // ── Background section ──
    auto* bg_header  = new SpectraSectionHeader("BACKGROUND", container);
    auto* bg_content = new QWidget(container);
    auto* bg_layout  = new QVBoxLayout(bg_content);
    bg_layout->setContentsMargins(8, 4, 8, 8);
    bg_layout->setSpacing(6);
    figure_bg_color_field_ = new SpectraColorField("Background Color", bg_content);
    figure_bg_color_field_->setObjectName("figure_bg_color");
    bg_layout->addWidget(figure_bg_color_field_);
    connect(bg_header, &SpectraSectionHeader::toggled, bg_content, &QWidget::setVisible);
    figure_layout_->addWidget(bg_header);
    figure_layout_->addWidget(bg_content);

    // ── Margins section ──
    auto* margins_header  = new SpectraSectionHeader("MARGINS", container);
    auto* margins_content = new QWidget(container);
    auto* margins_layout  = new QVBoxLayout(margins_content);
    margins_layout->setContentsMargins(8, 4, 8, 8);
    margins_layout->setSpacing(static_cast<int>(ui::tokens::SPACE_4));

    margin_top_spin_    = new SpectraDragSpinBox(margins_content);
    margin_bottom_spin_ = new SpectraDragSpinBox(margins_content);
    margin_left_spin_   = new SpectraDragSpinBox(margins_content);
    margin_right_spin_  = new SpectraDragSpinBox(margins_content);
    margin_hgap_spin_   = new SpectraDragSpinBox(margins_content);
    margin_vgap_spin_   = new SpectraDragSpinBox(margins_content);
    margin_min_h_spin_  = new SpectraDragSpinBox(margins_content);

    for (auto* spin : {margin_top_spin_,
                       margin_bottom_spin_,
                       margin_left_spin_,
                       margin_right_spin_,
                       margin_hgap_spin_,
                       margin_vgap_spin_,
                       margin_min_h_spin_})
    {
        spin->setRange(0.0, 1000.0);
        spin->setDecimals(0);
        spin->setSuffix(" px");
        spin->setSingleStep(1.0);
    }

    margins_layout->addWidget(new SpectraPropertyRow("Top", margin_top_spin_, margins_content));
    margins_layout->addWidget(
        new SpectraPropertyRow("Bottom", margin_bottom_spin_, margins_content));
    margins_layout->addWidget(new SpectraPropertyRow("Left", margin_left_spin_, margins_content));
    margins_layout->addWidget(new SpectraPropertyRow("Right", margin_right_spin_, margins_content));
    margins_layout->addWidget(new SpectraPropertyRow("H Gap", margin_hgap_spin_, margins_content));
    margins_layout->addWidget(new SpectraPropertyRow("V Gap", margin_vgap_spin_, margins_content));
    margins_layout->addWidget(
        new SpectraPropertyRow("Min Row H", margin_min_h_spin_, margins_content));

    connect(margins_header, &SpectraSectionHeader::toggled, margins_content, &QWidget::setVisible);
    figure_layout_->addWidget(margins_header);
    figure_layout_->addWidget(margins_content);

    // ── Legend section ──
    auto* legend_header  = new SpectraSectionHeader("LEGEND", container);
    auto* legend_content = new QWidget(container);
    auto* legend_layout  = new QVBoxLayout(legend_content);
    legend_layout->setContentsMargins(8, 4, 8, 8);
    legend_layout->setSpacing(static_cast<int>(ui::tokens::SPACE_4));

    legend_visible_check_ = new QCheckBox("Show Legend", legend_content);
    legend_visible_check_->setObjectName("figure_legend_visible");
    legend_layout->addWidget(legend_visible_check_);

    legend_position_combo_ = new QComboBox(legend_content);
    legend_position_combo_->addItem("Top Right");
    legend_position_combo_->addItem("Top Left");
    legend_position_combo_->addItem("Bottom Right");
    legend_position_combo_->addItem("Bottom Left");
    legend_position_combo_->addItem("Hidden");
    legend_layout->addWidget(
        new SpectraPropertyRow("Position", legend_position_combo_, legend_content));

    legend_font_size_spin_ = new SpectraDragSpinBox(legend_content);
    legend_font_size_spin_->setRange(0.0, 72.0);
    legend_font_size_spin_->setDecimals(0);
    legend_font_size_spin_->setSuffix(" px");
    legend_layout->addWidget(
        new SpectraPropertyRow("Font Size", legend_font_size_spin_, legend_content));

    legend_padding_spin_ = new SpectraDragSpinBox(legend_content);
    legend_padding_spin_->setRange(0.0, 40.0);
    legend_padding_spin_->setDecimals(0);
    legend_padding_spin_->setSuffix(" px");
    legend_layout->addWidget(
        new SpectraPropertyRow("Padding", legend_padding_spin_, legend_content));

    legend_bg_color_field_     = new SpectraColorField("Background", legend_content);
    legend_border_color_field_ = new SpectraColorField("Border", legend_content);
    legend_layout->addWidget(legend_bg_color_field_);
    legend_layout->addWidget(legend_border_color_field_);

    connect(legend_header, &SpectraSectionHeader::toggled, legend_content, &QWidget::setVisible);
    figure_layout_->addWidget(legend_header);
    figure_layout_->addWidget(legend_content);

    // ── Quick Actions section ──
    auto* quick_header  = new SpectraSectionHeader("QUICK ACTIONS", container);
    auto* quick_content = new QWidget(container);
    auto* quick_layout  = new QVBoxLayout(quick_content);
    quick_layout->setContentsMargins(8, 4, 8, 8);
    quick_layout->setSpacing(6);
    reset_figure_style_btn_ = new QPushButton("Reset to Defaults", quick_content);
    reset_figure_style_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    quick_layout->addWidget(reset_figure_style_btn_);
    connect(quick_header, &SpectraSectionHeader::toggled, quick_content, &QWidget::setVisible);
    figure_layout_->addWidget(quick_header);
    figure_layout_->addWidget(quick_content);

    figure_layout_->addStretch();
    figure_scroll_->setWidget(container);
    wire_figure_page();
}

void QtInspectorWidget::build_series_page()
{
    series_scroll_ = new QScrollArea(this);
    series_scroll_->setWidgetResizable(true);
    series_scroll_->setFrameShape(QFrame::NoFrame);
    // The inspector is a fixed-width drawer: content must reflow, never pan.
    series_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget();
    container->setObjectName("inspector_series_page");
    series_layout_ = new QVBoxLayout(container);
    series_layout_->setContentsMargins(12, 12, 12, 12);
    series_layout_->setSpacing(10);

    series_title_ = new SpectraPanelTitle(container);
    series_title_->setTitle("Series");
    series_layout_->addWidget(series_title_);

    series_list_ = new SpectraSeriesListView(container);
    series_list_->setObjectName("series_list_view");
    series_layout_->addWidget(series_list_);

    series_props_        = new QWidget(container);
    series_props_layout_ = new QVBoxLayout(series_props_);
    series_props_layout_->setContentsMargins(0, 0, 0, 0);
    series_props_layout_->setSpacing(10);
    series_layout_->addWidget(series_props_);

    series_layout_->addStretch();
    series_scroll_->setWidget(container);
}

void QtInspectorWidget::build_axes_page()
{
    axes_scroll_ = new QScrollArea(this);
    axes_scroll_->setWidgetResizable(true);
    axes_scroll_->setFrameShape(QFrame::NoFrame);
    // The inspector is a fixed-width drawer: content must reflow, never pan.
    axes_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget();
    container->setObjectName("inspector_axes_page");
    axes_layout_ = new QVBoxLayout(container);
    axes_layout_->setContentsMargins(12, 12, 12, 12);
    axes_layout_->setSpacing(10);

    auto* header = new QLabel("Axes", container);
    QFont f      = header->font();
    f.setPointSize(15);
    f.setBold(true);
    header->setFont(f);
    const auto& header_colors = spectra_colors();
    header->setStyleSheet(
        QString("color: %1;").arg(header_colors.text_primary.name(QColor::HexArgb)));
    axes_layout_->addWidget(header);

    axes_tab_widget_ = new QTabWidget(container);
    axes_tab_widget_->setObjectName("inspector_tabs");
    axes_layout_->addWidget(axes_tab_widget_);
    axes_layout_->addStretch();
    axes_scroll_->setWidget(container);
}

void QtInspectorWidget::build_data_page()
{
    data_scroll_ = new QScrollArea(this);
    data_scroll_->setWidgetResizable(true);
    data_scroll_->setFrameShape(QFrame::NoFrame);
    // The inspector is a fixed-width drawer: content must reflow, never pan.
    data_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget();
    container->setObjectName("inspector_data_page");
    data_layout_ = new QVBoxLayout(container);
    data_layout_->setContentsMargins(12, 12, 12, 12);
    data_layout_->setSpacing(10);

    auto* header = new QLabel("Data", container);
    QFont f      = header->font();
    f.setPointSize(15);
    f.setBold(true);
    header->setFont(f);
    const auto& header_colors = spectra_colors();
    header->setStyleSheet(
        QString("color: %1;").arg(header_colors.text_primary.name(QColor::HexArgb)));
    data_layout_->addWidget(header);

    data_editor_ = new QtDataEditorWidget(registry_,
                                          services_ ? &services_->undo() : nullptr,
                                          services_ ? services_->redraw_request() : nullptr,
                                          services_ ? services_->clipboard_service() : nullptr,
                                          services_ ? services_->dialog_service() : nullptr,
                                          container);

    if (auto* editor_widget = data_editor_->widget())
    {
        data_layout_->addWidget(editor_widget);
        data_editor_->hide();
    }

    data_layout_->addStretch();
    data_scroll_->setWidget(container);
}

void QtInspectorWidget::set_active_figure(FigureId id)
{
    active_id_ = id;
    if (data_editor_)
        data_editor_->set_active_figure(id);
    refresh();
}

std::vector<AxesBase*> QtInspectorWidget::active_axes() const
{
    std::vector<AxesBase*> axes;
    Figure*                figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (figure)
        figure->for_each_axes([&](AxesBase* axis) { axes.push_back(axis); });
    return axes;
}

void QtInspectorWidget::clear_pages()
{
    axes_tab_widget_->clear();
    axes_controls_.clear();
    axes_series_counts_.clear();
    series_list_->clear();
    clear_series_properties();
}

void QtInspectorWidget::refresh()
{
    clear_pages();

    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
    {
        section_stack_->setCurrentIndex(0);
        return;
    }

    Figure* figure = registry_->get(active_id_);
    if (!figure)
    {
        section_stack_->setCurrentIndex(0);
        return;
    }

    // Figure tab
    figure_size_label_->setText(QString("%1 × %2").arg(figure->width()).arg(figure->height()));

    const auto   axes       = active_axes();
    const size_t axes_count = axes.size();
    if (figure_axes_count_label_)
        figure_axes_count_label_->setText(QString::number(axes_count));
    if (figure_size_label_)
        figure_size_label_->setText(QString("%1 × %2").arg(figure->width()).arg(figure->height()));
    if (figure_title_)
    {
        size_t total_series = 0;
        for (const auto& ax : axes)
            if (ax)
                total_series += ax->series().size();
        figure_title_->setSubtitle(QString("%1 axes, %2 series").arg(axes_count).arg(total_series));
        if (series_title_)
            series_title_->setSubtitle(QString("%1 series").arg(total_series));
    }

    const auto& sty = figure->style();
    figure_bg_color_field_->setColor(sty.background);
    margin_top_spin_->setValue(sty.margin_top);
    margin_bottom_spin_->setValue(sty.margin_bottom);
    margin_left_spin_->setValue(sty.margin_left);
    margin_right_spin_->setValue(sty.margin_right);
    margin_hgap_spin_->setValue(sty.subplot_hgap);
    margin_vgap_spin_->setValue(sty.subplot_vgap);
    margin_min_h_spin_->setValue(sty.min_subplot_height);

    const auto& leg = figure->legend();
    legend_visible_check_->setChecked(leg.visible);
    legend_position_combo_->setCurrentIndex(static_cast<int>(leg.position));
    legend_font_size_spin_->setValue(leg.font_size);
    legend_padding_spin_->setValue(leg.padding);
    legend_bg_color_field_->setColor(leg.bg_color);
    legend_border_color_field_->setColor(leg.border_color);

    build_series_list();

    for (int axes_idx = 0; axes_idx < static_cast<int>(axes.size()); ++axes_idx)
    {
        if (auto* ax = dynamic_cast<Axes*>(axes[axes_idx]))
            build_axes_tab(*ax, axes_idx);
        else if (auto* ax3d = dynamic_cast<Axes3D*>(axes[axes_idx]))
            build_axes3d_tab(*ax3d, axes_idx);
    }

    if (data_editor_)
        data_editor_->refresh();
}

void QtInspectorWidget::sync_from_model()
{
    Figure* figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (!figure)
    {
        if (section_stack_->currentIndex() != 0)
            refresh();
        return;
    }

    const auto axes = active_axes();
    bool       topology_changed =
        axes.size() != axes_controls_.size() || axes.size() != axes_series_counts_.size();
    if (!topology_changed)
    {
        for (size_t i = 0; i < axes.size(); ++i)
        {
            if (axes_controls_[i].model != axes[i]
                || axes_series_counts_[i] != axes[i]->series().size())
            {
                topology_changed = true;
                break;
            }
        }
    }
    if (topology_changed)
    {
        refresh();
        return;
    }

    if (figure_size_label_)
        figure_size_label_->setText(QString("%1 × %2").arg(figure->width()).arg(figure->height()));
    if (figure_axes_count_label_)
        figure_axes_count_label_->setText(QString::number(axes.size()));
    if (figure_title_)
    {
        size_t total_series = 0;
        for (const auto& ax : axes)
            if (ax)
                total_series += ax->series().size();
        figure_title_->setSubtitle(
            QString("%1 axes, %2 series").arg(axes.size()).arg(total_series));
    }

    const auto& sty = figure->style();
    figure_bg_color_field_->setColor(sty.background);
    set_spin_from_model(margin_top_spin_, sty.margin_top);
    set_spin_from_model(margin_bottom_spin_, sty.margin_bottom);
    set_spin_from_model(margin_left_spin_, sty.margin_left);
    set_spin_from_model(margin_right_spin_, sty.margin_right);
    set_spin_from_model(margin_hgap_spin_, sty.subplot_hgap);
    set_spin_from_model(margin_vgap_spin_, sty.subplot_vgap);
    set_spin_from_model(margin_min_h_spin_, sty.min_subplot_height);

    const auto& leg = figure->legend();
    if (legend_visible_check_ && legend_visible_check_->isChecked() != leg.visible)
    {
        const QSignalBlocker blocker(legend_visible_check_);
        legend_visible_check_->setChecked(leg.visible);
    }
    if (legend_position_combo_)
    {
        const QSignalBlocker blocker(legend_position_combo_);
        legend_position_combo_->setCurrentIndex(static_cast<int>(leg.position));
    }
    set_spin_from_model(legend_font_size_spin_, leg.font_size);
    set_spin_from_model(legend_padding_spin_, leg.padding);
    legend_bg_color_field_->setColor(leg.bg_color);
    legend_border_color_field_->setColor(leg.border_color);

    for (size_t i = 0; i < axes.size(); ++i)
    {
        auto& controls = axes_controls_[i];
        set_line_edit_from_model(controls.title_edit, axes[i]->title());
        if (auto* ax = dynamic_cast<Axes*>(axes[i]))
        {
            set_line_edit_from_model(controls.xlabel_edit, ax->get_xlabel());
            set_line_edit_from_model(controls.ylabel_edit, ax->get_ylabel());
            const auto x_limits = ax->x_limits();
            const auto y_limits = ax->y_limits();
            set_spin_from_model(controls.xmin_spin, x_limits.min);
            set_spin_from_model(controls.xmax_spin, x_limits.max);
            set_spin_from_model(controls.ymin_spin, y_limits.min);
            set_spin_from_model(controls.ymax_spin, y_limits.max);
            if (controls.grid_check && controls.grid_check->isChecked() != ax->grid_enabled())
            {
                const QSignalBlocker blocker(controls.grid_check);
                controls.grid_check->setChecked(ax->grid_enabled());
            }
            if (controls.border_check && controls.border_check->isChecked() != ax->border_enabled())
            {
                const QSignalBlocker blocker(controls.border_check);
                controls.border_check->setChecked(ax->border_enabled());
            }
            if (controls.grid_color_btn)
                update_color_button(controls.grid_color_btn, ax->axis_style().grid_color);
            set_spin_from_model(controls.grid_width_spin_, ax->axis_style().grid_width);
            set_spin_from_model(controls.tick_length_spin_, ax->axis_style().tick_length);
            if (controls.autoscale_combo_)
            {
                const QSignalBlocker blocker(controls.autoscale_combo_);
                controls.autoscale_combo_->setCurrentIndex(
                    static_cast<int>(ax->get_autoscale_mode()));
            }
        }
        else if (auto* ax3d = dynamic_cast<Axes3D*>(axes[i]))
        {
            set_line_edit_from_model(controls.xlabel_edit, ax3d->xlabel());
            set_line_edit_from_model(controls.ylabel_edit, ax3d->ylabel());
            set_line_edit_from_model(controls.zlabel_edit, ax3d->zlabel());
            const auto x_limits = ax3d->x_limits();
            const auto y_limits = ax3d->y_limits();
            const auto z_limits = ax3d->z_limits();
            set_spin_from_model(controls.xmin_spin, x_limits.min);
            set_spin_from_model(controls.xmax_spin, x_limits.max);
            set_spin_from_model(controls.ymin_spin, y_limits.min);
            set_spin_from_model(controls.ymax_spin, y_limits.max);
            set_spin_from_model(controls.zmin_spin, z_limits.min);
            set_spin_from_model(controls.zmax_spin, z_limits.max);
            if (controls.grid_planes_combo)
            {
                const int grid_value = static_cast<int>(ax3d->grid_planes());
                const int index      = controls.grid_planes_combo->findData(grid_value);
                if (index >= 0 && index != controls.grid_planes_combo->currentIndex())
                {
                    const QSignalBlocker blocker(controls.grid_planes_combo);
                    controls.grid_planes_combo->setCurrentIndex(index);
                }
            }
            if (controls.bounding_box_check
                && controls.bounding_box_check->isChecked() != ax3d->show_bounding_box())
            {
                const QSignalBlocker blocker(controls.bounding_box_check);
                controls.bounding_box_check->setChecked(ax3d->show_bounding_box());
            }
        }

        update_axes_statistics(*axes[i], controls);
    }

    if (ctx_ && ctx_->series && series_controls_.series == ctx_->series)
        update_series_preview();
}

void QtInspectorWidget::build_axes_tab(Axes& ax, int index)
{
    auto* tab    = new QWidget(axes_tab_widget_);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    AxesControls ctrl{};
    ctrl.model = &ax;

    int   axes_idx_local = index;
    auto* undo_mgr       = services_ ? &services_->undo() : nullptr;
    auto* redraw         = services_ ? services_->redraw_request() : nullptr;

    // Title
    auto* title_section = new CollapsibleSection("TITLE", tab);
    auto* title_form    = make_inspector_form();
    ctrl.title_edit     = new QLineEdit(tab);
    ctrl.title_edit->setObjectName(QString("axes_%1_title").arg(index));
    ctrl.title_edit->setText(QString::fromStdString(ax.title()));
    title_form->addRow("Title", ctrl.title_edit);
    style_inspector_form(title_form);
    title_section->content_layout()->addLayout(title_form);
    layout->addWidget(title_section);

    // X Axis
    auto* x_section  = new CollapsibleSection("X AXIS", tab);
    auto* x_form     = make_inspector_form();
    ctrl.xlabel_edit = new QLineEdit(tab);
    ctrl.xlabel_edit->setObjectName(QString("axes_%1_x_label").arg(index));
    ctrl.xlabel_edit->setText(QString::fromStdString(ax.get_xlabel()));
    x_form->addRow("Label", ctrl.xlabel_edit);

    auto  xlim           = ax.x_limits();
    auto* x_range_layout = new QHBoxLayout();
    ctrl.xmin_spin       = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.xmin_spin);
    ctrl.xmin_spin->setObjectName(QString("axes_%1_x_min").arg(index));
    ctrl.xmin_spin->setRange(-1e9, 1e9);
    ctrl.xmin_spin->setDecimals(3);
    ctrl.xmin_spin->setValue(xlim.min);
    x_range_layout->addWidget(ctrl.xmin_spin);
    ctrl.xmax_spin = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.xmax_spin);
    ctrl.xmax_spin->setObjectName(QString("axes_%1_x_max").arg(index));
    ctrl.xmax_spin->setRange(-1e9, 1e9);
    ctrl.xmax_spin->setDecimals(3);
    ctrl.xmax_spin->setValue(xlim.max);
    x_range_layout->addWidget(ctrl.xmax_spin);
    x_form->addRow("Range", x_range_layout);
    style_inspector_form(x_form);
    x_section->content_layout()->addLayout(x_form);
    layout->addWidget(x_section);

    // Y Axis
    auto* y_section  = new CollapsibleSection("Y AXIS", tab);
    auto* y_form     = make_inspector_form();
    ctrl.ylabel_edit = new QLineEdit(tab);
    ctrl.ylabel_edit->setObjectName(QString("axes_%1_y_label").arg(index));
    ctrl.ylabel_edit->setText(QString::fromStdString(ax.get_ylabel()));
    y_form->addRow("Label", ctrl.ylabel_edit);

    auto  ylim           = ax.y_limits();
    auto* y_range_layout = new QHBoxLayout();
    ctrl.ymin_spin       = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.ymin_spin);
    ctrl.ymin_spin->setObjectName(QString("axes_%1_y_min").arg(index));
    ctrl.ymin_spin->setRange(-1e9, 1e9);
    ctrl.ymin_spin->setDecimals(3);
    ctrl.ymin_spin->setValue(ylim.min);
    y_range_layout->addWidget(ctrl.ymin_spin);
    ctrl.ymax_spin = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.ymax_spin);
    ctrl.ymax_spin->setObjectName(QString("axes_%1_y_max").arg(index));
    ctrl.ymax_spin->setRange(-1e9, 1e9);
    ctrl.ymax_spin->setDecimals(3);
    ctrl.ymax_spin->setValue(ylim.max);
    y_range_layout->addWidget(ctrl.ymax_spin);
    y_form->addRow("Range", y_range_layout);
    style_inspector_form(y_form);
    y_section->content_layout()->addLayout(y_form);
    layout->addWidget(y_section);

    // Grid & Border
    auto* grid_section = new CollapsibleSection("GRID & BORDER", tab);
    auto* grid_form    = make_inspector_form();
    ctrl.grid_check    = new QCheckBox("Show Grid", tab);
    ctrl.grid_check->setChecked(ax.grid_enabled());
    grid_form->addRow(ctrl.grid_check);
    ctrl.border_check = new QCheckBox("Show Border", tab);
    ctrl.border_check->setChecked(ax.border_enabled());
    grid_form->addRow(ctrl.border_check);

    ctrl.grid_color_btn = make_color_button(tab, ax.axis_style().grid_color);
    grid_form->addRow("Grid Color", ctrl.grid_color_btn);

    ctrl.grid_width_spin_ = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.grid_width_spin_);
    ctrl.grid_width_spin_->setRange(0.1, 5.0);
    ctrl.grid_width_spin_->setDecimals(1);
    ctrl.grid_width_spin_->setSingleStep(0.1);
    ctrl.grid_width_spin_->setValue(ax.axis_style().grid_width);
    grid_form->addRow("Grid Width", ctrl.grid_width_spin_);

    ctrl.tick_length_spin_ = new QDoubleSpinBox(tab);
    configure_inspector_spin(ctrl.tick_length_spin_);
    ctrl.tick_length_spin_->setRange(0.0, 20.0);
    ctrl.tick_length_spin_->setDecimals(0);
    ctrl.tick_length_spin_->setValue(ax.axis_style().tick_length);
    grid_form->addRow("Tick Length", ctrl.tick_length_spin_);
    style_inspector_form(grid_form);
    grid_section->content_layout()->addLayout(grid_form);
    layout->addWidget(grid_section);

    // Autoscale
    auto* auto_section    = new CollapsibleSection("AUTOSCALE", tab);
    auto* auto_form       = make_inspector_form();
    ctrl.autoscale_combo_ = new QComboBox(tab);
    ctrl.autoscale_combo_->addItem("Fit");
    ctrl.autoscale_combo_->addItem("Tight");
    ctrl.autoscale_combo_->addItem("Padded");
    ctrl.autoscale_combo_->addItem("Manual");
    ctrl.autoscale_combo_->setCurrentIndex(static_cast<int>(ax.get_autoscale_mode()));
    auto_form->addRow("Mode", ctrl.autoscale_combo_);
    ctrl.auto_fit_btn_ = new QPushButton("Auto-fit Now", tab);
    auto_form->addRow(ctrl.auto_fit_btn_);
    style_inspector_form(auto_form);
    auto_section->content_layout()->addLayout(auto_form);
    layout->addWidget(auto_section);

    // Statistics
    auto* stats_section = new CollapsibleSection("STATISTICS", tab);
    auto* stats_form    = make_inspector_form();
    stats_form->setSpacing(6);

    ctrl.stats_visible_label = new QLabel(tab);
    ctrl.stats_visible_label->setObjectName(QString("axes_%1_stats_visible").arg(index));
    ctrl.stats_total_points_label = new QLabel(tab);
    ctrl.stats_total_points_label->setObjectName(QString("axes_%1_stats_total_points").arg(index));
    ctrl.stats_x_min_label = new QLabel(tab);
    ctrl.stats_x_min_label->setObjectName(QString("axes_%1_stats_x_min").arg(index));
    ctrl.stats_x_max_label = new QLabel(tab);
    ctrl.stats_x_max_label->setObjectName(QString("axes_%1_stats_x_max").arg(index));
    ctrl.stats_x_mean_label = new QLabel(tab);
    ctrl.stats_x_mean_label->setObjectName(QString("axes_%1_stats_x_mean").arg(index));
    ctrl.stats_y_min_label = new QLabel(tab);
    ctrl.stats_y_min_label->setObjectName(QString("axes_%1_stats_y_min").arg(index));
    ctrl.stats_y_max_label = new QLabel(tab);
    ctrl.stats_y_max_label->setObjectName(QString("axes_%1_stats_y_max").arg(index));
    ctrl.stats_y_mean_label = new QLabel(tab);
    ctrl.stats_y_mean_label->setObjectName(QString("axes_%1_stats_y_mean").arg(index));

    const auto& stat_colors = spectra_colors();
    const auto  value_color = stat_colors.text_primary.name(QColor::HexArgb);
    for (auto* l : {ctrl.stats_visible_label,
                    ctrl.stats_total_points_label,
                    ctrl.stats_x_min_label,
                    ctrl.stats_x_max_label,
                    ctrl.stats_x_mean_label,
                    ctrl.stats_y_min_label,
                    ctrl.stats_y_max_label,
                    ctrl.stats_y_mean_label})
    {
        l->setStyleSheet(QString("color: %1;").arg(value_color));
    }

    stats_form->addRow("Visible", ctrl.stats_visible_label);
    stats_form->addRow("Total Points", ctrl.stats_total_points_label);
    stats_form->addRow("X Min", ctrl.stats_x_min_label);
    stats_form->addRow("X Max", ctrl.stats_x_max_label);
    stats_form->addRow("X Mean", ctrl.stats_x_mean_label);
    stats_form->addRow("Y Min", ctrl.stats_y_min_label);
    stats_form->addRow("Y Max", ctrl.stats_y_max_label);
    stats_form->addRow("Y Mean", ctrl.stats_y_mean_label);
    style_inspector_form(stats_form);
    stats_section->content_layout()->addLayout(stats_form);
    layout->addWidget(stats_section);

    // Reference Lines
    auto* ref_section = new CollapsibleSection("REFERENCE LINES", tab);
    auto* ref_layout  = new QVBoxLayout();
    ref_layout->setContentsMargins(0, 0, 0, 0);
    ref_layout->setSpacing(8);

    // Value/format inputs and the two add buttons need separate rows: four
    // controls abreast exceed the fixed drawer width and clip.
    auto* add_row = new QHBoxLayout();
    add_row->setSpacing(static_cast<int>(ui::tokens::SPACE_2));

    auto* ref_value_spin = new QDoubleSpinBox(tab);
    configure_inspector_spin(ref_value_spin);
    ref_value_spin->setObjectName(QString("axes_%1_ref_value").arg(index));
    ref_value_spin->setRange(-1e9, 1e9);
    ref_value_spin->setDecimals(3);
    ref_value_spin->setValue(0.0);
    add_row->addWidget(ref_value_spin);

    auto* ref_fmt_edit = new QLineEdit("-", tab);
    ref_fmt_edit->setObjectName(QString("axes_%1_ref_fmt").arg(index));
    ref_fmt_edit->setPlaceholderText("Format");
    ref_fmt_edit->setMinimumWidth(56);
    ref_fmt_edit->setFixedHeight(ui::tokens::INSPECTOR_INPUT_HEIGHT);
    add_row->addWidget(ref_fmt_edit);
    ref_layout->addLayout(add_row);

    auto* add_btn_row = new QHBoxLayout();
    add_btn_row->setSpacing(static_cast<int>(ui::tokens::SPACE_2));
    auto* add_hline_btn = new QPushButton("Add HLine", tab);
    add_hline_btn->setObjectName(QString("axes_%1_add_hline").arg(index));
    auto* add_vline_btn = new QPushButton("Add VLine", tab);
    add_vline_btn->setObjectName(QString("axes_%1_add_vline").arg(index));
    for (auto* btn : {add_hline_btn, add_vline_btn})
    {
        btn->setMinimumWidth(0);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        add_btn_row->addWidget(btn);
    }
    ref_layout->addLayout(add_btn_row);

    auto* ref_list = new QWidget(tab);
    ref_list->setObjectName(QString("axes_%1_ref_list").arg(index));
    auto* ref_list_layout = new QVBoxLayout(ref_list);
    ref_list_layout->setContentsMargins(0, 0, 0, 0);
    ref_list_layout->setSpacing(4);
    ref_layout->addWidget(ref_list);

    const auto& series_list = ax.series();
    auto        ref_entries = std::vector<std::pair<size_t, const Series*>>{};
    for (size_t i = 0; i < series_list.size(); ++i)
    {
        if (series_list[i] && series_list[i]->is_reference_line())
            ref_entries.emplace_back(i, series_list[i].get());
    }

    if (ref_entries.empty())
    {
        auto* empty = new QLabel("No reference lines", ref_list);
        empty->setObjectName(QString("axes_%1_ref_empty").arg(index));
        empty->setStyleSheet(
            QString("color: %1;").arg(stat_colors.text_muted.name(QColor::HexArgb)));
        ref_list_layout->addWidget(empty);
    }
    else
    {
        for (const auto& [ref_index, s] : ref_entries)
        {
            QString label_text =
                QString::fromStdString(s->label().empty() ? "Unnamed" : s->label());
            auto* row = new SpectraReferenceLineRow(s->color(), label_text, ref_list);
            row->setObjectName(QString("axes_%1_ref_%2_row").arg(index).arg(ref_index));
            row->deleteButton()->setObjectName(
                QString("axes_%1_ref_%2_delete").arg(index).arg(ref_index));
            ref_list_layout->addWidget(row);

            connect(row,
                    &SpectraReferenceLineRow::deleteClicked,
                    this,
                    [this, axes_idx_local, ref_index, undo_mgr, redraw]()
                    {
                        Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                        if (!fig)
                            return;
                        Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                        if (!ax)
                            return;
                        undoable_delete_reference_line(undo_mgr, *ax, ref_index);
                        if (redraw)
                            redraw->request_redraw();
                        refresh();
                    });
        }
    }

    ref_section->content_layout()->addLayout(ref_layout);
    layout->addWidget(ref_section);

    connect(add_hline_btn,
            &QPushButton::clicked,
            this,
            [this, axes_idx_local, ref_value_spin, ref_fmt_edit, undo_mgr, redraw]()
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_add_hline(undo_mgr,
                                   *ax,
                                   ref_value_spin->value(),
                                   ref_fmt_edit->text().toStdString());
                if (redraw)
                    redraw->request_redraw();
                refresh();
            });

    connect(add_vline_btn,
            &QPushButton::clicked,
            this,
            [this, axes_idx_local, ref_value_spin, ref_fmt_edit, undo_mgr, redraw]()
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_add_vline(undo_mgr,
                                   *ax,
                                   ref_value_spin->value(),
                                   ref_fmt_edit->text().toStdString());
                if (redraw)
                    redraw->request_redraw();
                refresh();
            });

    layout->addStretch();
    axes_tab_widget_->addTab(tab, QString("Axes %1").arg(index + 1));

    connect(ctrl.title_edit,
            &QLineEdit::textChanged,
            this,
            [this, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_set_title(undo_mgr, *ax, text.toStdString());
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.xlabel_edit,
            &QLineEdit::textChanged,
            this,
            [this, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_set_xlabel(undo_mgr, *ax, text.toStdString());
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.ylabel_edit,
            &QLineEdit::textChanged,
            this,
            [this, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_set_ylabel(undo_mgr, *ax, text.toStdString());
                if (redraw)
                    redraw->request_redraw();
            });

    auto* xmin_spin = ctrl.xmin_spin;
    auto* xmax_spin = ctrl.xmax_spin;
    auto* ymin_spin = ctrl.ymin_spin;
    auto* ymax_spin = ctrl.ymax_spin;

    connect(ctrl.xmin_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw, xmax_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_xlim(undo_mgr,
                              *ax,
                              static_cast<float>(val),
                              static_cast<float>(xmax_spin->value()));
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.xmax_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw, xmin_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_xlim(undo_mgr,
                              *ax,
                              static_cast<float>(xmin_spin->value()),
                              static_cast<float>(val));
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.ymin_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw, ymax_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_ylim(undo_mgr,
                              *ax,
                              static_cast<float>(val),
                              static_cast<float>(ymax_spin->value()));
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.ymax_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw, ymin_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                undoable_ylim(undo_mgr,
                              *ax,
                              static_cast<float>(ymin_spin->value()),
                              static_cast<float>(val));
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.grid_check,
            &QCheckBox::toggled,
            this,
            [this, axes_idx_local, undo_mgr, redraw](bool checked)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                ax->grid(checked);
                if (undo_mgr)
                    undo_mgr->push(UndoAction{checked ? "Show grid" : "Hide grid",
                                              [ax, checked]() { ax->grid(!checked); },
                                              [ax, checked]() { ax->grid(checked); }});
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.border_check,
            &QCheckBox::toggled,
            this,
            [this, axes_idx_local, undo_mgr, redraw](bool checked)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                ax->show_border(checked);
                if (undo_mgr)
                    undo_mgr->push(UndoAction{checked ? "Show border" : "Hide border",
                                              [ax, checked]() { ax->show_border(!checked); },
                                              [ax, checked]() { ax->show_border(checked); }});
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.grid_color_btn,
            &QPushButton::clicked,
            this,
            [this, axes_idx_local, undo_mgr, redraw, btn = ctrl.grid_color_btn]()
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                auto* dialogs = services_ ? services_->dialog_service() : nullptr;
                if (!dialogs)
                    return;
                auto chosen = dialogs->color_picker("Grid Color", ax->axis_style().grid_color);
                if (!chosen)
                    return;
                auto before                 = ax->axis_style().grid_color;
                ax->axis_style().grid_color = *chosen;
                if (undo_mgr)
                {
                    auto* target = ax;
                    undo_mgr->push(UndoAction{"Change grid color",
                                              [target, before]()
                                              { target->axis_style().grid_color = before; },
                                              [target, chosen = *chosen]()
                                              { target->axis_style().grid_color = chosen; }});
                }
                update_color_button(btn, ax->axis_style().grid_color);
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.grid_width_spin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                float before                = ax->axis_style().grid_width;
                ax->axis_style().grid_width = static_cast<float>(val);
                if (undo_mgr)
                {
                    auto* target = ax;
                    float value  = static_cast<float>(val);
                    undo_mgr->push(
                        UndoAction{"Change grid width",
                                   [target, before]() { target->axis_style().grid_width = before; },
                                   [target, value]() { target->axis_style().grid_width = value; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(ctrl.tick_length_spin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, axes_idx_local, undo_mgr, redraw](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                float before                 = ax->axis_style().tick_length;
                ax->axis_style().tick_length = static_cast<float>(val);
                if (undo_mgr)
                {
                    auto* target = ax;
                    float value  = static_cast<float>(val);
                    undo_mgr->push(UndoAction{
                        "Change tick length",
                        [target, before]() { target->axis_style().tick_length = before; },
                        [target, value]() { target->axis_style().tick_length = value; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(
        ctrl.autoscale_combo_,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this, axes_idx_local, undo_mgr, redraw](int idx)
        {
            if (idx < 0 || idx > 3)
                return;
            Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
            if (!fig)
                return;
            Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
            if (!ax)
                return;
            auto mode   = static_cast<AutoscaleMode>(idx);
            auto before = ax->get_autoscale_mode();
            ax->autoscale_mode(mode);
            if (undo_mgr)
            {
                auto* target = ax;
                undo_mgr->push(UndoAction{"Change autoscale mode",
                                          [target, before]() { target->autoscale_mode(before); },
                                          [target, mode]() { target->autoscale_mode(mode); }});
            }
            if (redraw)
                redraw->request_redraw();
        });

    connect(ctrl.auto_fit_btn_,
            &QPushButton::clicked,
            this,
            [this, axes_idx_local, redraw]()
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                Axes* ax = dynamic_cast<Axes*>(figure_axes_at(fig, axes_idx_local));
                if (!ax)
                    return;
                ax->auto_fit();
                if (redraw)
                    redraw->request_redraw(active_id_);
            });

    axes_controls_.push_back(ctrl);
    axes_series_counts_.push_back(ax.series().size());
    update_axes_statistics(ax, ctrl);
}

void QtInspectorWidget::update_axes_statistics(spectra::AxesBase& ax, AxesControls& ctrl)
{
    if (!ctrl.stats_visible_label)
        return;

    size_t total_points   = 0;
    size_t visible_series = 0;
    size_t total_series   = ax.series().size();

    double global_xmin = std::numeric_limits<double>::max();
    double global_xmax = std::numeric_limits<double>::lowest();
    double global_ymin = std::numeric_limits<double>::max();
    double global_ymax = std::numeric_limits<double>::lowest();
    double x_sum       = 0.0;
    double y_sum       = 0.0;
    size_t x_count     = 0;
    size_t y_count     = 0;

    for (const auto& s : ax.series())
    {
        if (!s)
            continue;
        if (!s->visible())
            continue;

        ++visible_series;

        std::span<const float> x_data;
        std::span<const float> y_data;
        size_t                 count = 0;
        get_series_data(*s, x_data, y_data, count);
        total_points += count;

        if (!x_data.empty())
        {
            auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
            global_xmin             = std::min(global_xmin, static_cast<double>(*xmin_it));
            global_xmax             = std::max(global_xmax, static_cast<double>(*xmax_it));
            x_sum += std::accumulate(x_data.begin(), x_data.end(), 0.0);
            x_count += x_data.size();
        }
        if (!y_data.empty())
        {
            auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
            global_ymin             = std::min(global_ymin, static_cast<double>(*ymin_it));
            global_ymax             = std::max(global_ymax, static_cast<double>(*ymax_it));
            y_sum += std::accumulate(y_data.begin(), y_data.end(), 0.0);
            y_count += y_data.size();
        }
    }

    const bool has_visible = visible_series > 0;
    ctrl.stats_visible_label->setText(QString("%1 / %2").arg(visible_series).arg(total_series));
    ctrl.stats_total_points_label->setText(QString::number(total_points));

    const auto set_stat = [](QLabel* l, double v, bool valid)
    { l->setText(valid ? format_stat(v) : QStringLiteral("—")); };

    set_stat(ctrl.stats_x_min_label, global_xmin, has_visible);
    set_stat(ctrl.stats_x_max_label, global_xmax, has_visible);
    set_stat(ctrl.stats_x_mean_label,
             x_sum / static_cast<double>(x_count),
             has_visible && x_count > 0);
    set_stat(ctrl.stats_y_min_label, global_ymin, has_visible);
    set_stat(ctrl.stats_y_max_label, global_ymax, has_visible);
    set_stat(ctrl.stats_y_mean_label,
             y_sum / static_cast<double>(y_count),
             has_visible && y_count > 0);
}

void QtInspectorWidget::build_axes3d_tab(Axes3D& ax, int index)
{
    auto* tab    = new QWidget(axes_tab_widget_);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    AxesControls ctrl{};
    ctrl.model = &ax;

    // Title
    auto* title_section = new CollapsibleSection("TITLE", tab);
    auto* title_form    = make_inspector_form();
    ctrl.title_edit     = new QLineEdit(tab);
    ctrl.title_edit->setObjectName(QString("axes_%1_title").arg(index));
    ctrl.title_edit->setText(QString::fromStdString(ax.title()));
    title_form->addRow("Title", ctrl.title_edit);
    style_inspector_form(title_form);
    title_section->content_layout()->addLayout(title_form);
    layout->addWidget(title_section);

    auto add_axis_group = [index, layout, tab](const QString&     name,
                                               const QString&     prefix,
                                               const std::string& label,
                                               const AxisLimits&  limits,
                                               QLineEdit*&        label_edit,
                                               QDoubleSpinBox*&   min_spin,
                                               QDoubleSpinBox*&   max_spin)
    {
        auto* section = new CollapsibleSection(name, tab);
        auto* form    = make_inspector_form();

        label_edit = new QLineEdit(tab);
        label_edit->setObjectName(QString("axes_%1_%2_label").arg(index).arg(prefix));
        label_edit->setText(QString::fromStdString(label));
        form->addRow("Label", label_edit);

        auto* range_layout = new QHBoxLayout();
        min_spin           = new QDoubleSpinBox(tab);
        configure_inspector_spin(min_spin);
        min_spin->setObjectName(QString("axes_%1_%2_min").arg(index).arg(prefix));
        min_spin->setRange(-1e9, 1e9);
        min_spin->setDecimals(3);
        min_spin->setValue(limits.min);
        range_layout->addWidget(min_spin);

        max_spin = new QDoubleSpinBox(tab);
        configure_inspector_spin(max_spin);
        max_spin->setObjectName(QString("axes_%1_%2_max").arg(index).arg(prefix));
        max_spin->setRange(-1e9, 1e9);
        max_spin->setDecimals(3);
        max_spin->setValue(limits.max);
        range_layout->addWidget(max_spin);
        form->addRow("Range", range_layout);
        style_inspector_form(form);
        section->content_layout()->addLayout(form);
        layout->addWidget(section);
    };

    add_axis_group("X AXIS",
                   "x",
                   ax.xlabel(),
                   ax.x_limits(),
                   ctrl.xlabel_edit,
                   ctrl.xmin_spin,
                   ctrl.xmax_spin);
    add_axis_group("Y AXIS",
                   "y",
                   ax.ylabel(),
                   ax.y_limits(),
                   ctrl.ylabel_edit,
                   ctrl.ymin_spin,
                   ctrl.ymax_spin);
    add_axis_group("Z AXIS",
                   "z",
                   ax.zlabel(),
                   ax.z_limits(),
                   ctrl.zlabel_edit,
                   ctrl.zmin_spin,
                   ctrl.zmax_spin);

    // Grid & Bounding Box
    auto* grid_section     = new CollapsibleSection("GRID & BOUNDING BOX", tab);
    auto* grid_form        = make_inspector_form();
    ctrl.grid_planes_combo = new QComboBox(tab);
    ctrl.grid_planes_combo->setObjectName(QString("axes_%1_grid_planes").arg(index));
    ctrl.grid_planes_combo->addItem("None", static_cast<int>(Axes3D::GridPlane::None));
    ctrl.grid_planes_combo->addItem("XY", static_cast<int>(Axes3D::GridPlane::XY));
    ctrl.grid_planes_combo->addItem("XZ", static_cast<int>(Axes3D::GridPlane::XZ));
    ctrl.grid_planes_combo->addItem("YZ", static_cast<int>(Axes3D::GridPlane::YZ));
    ctrl.grid_planes_combo->addItem("All", static_cast<int>(Axes3D::GridPlane::All));
    ctrl.grid_planes_combo->setCurrentIndex(
        ctrl.grid_planes_combo->findData(static_cast<int>(ax.grid_planes())));
    grid_form->addRow("Grid Planes", ctrl.grid_planes_combo);

    ctrl.bounding_box_check = new QCheckBox("Visible", tab);
    ctrl.bounding_box_check->setObjectName(QString("axes_%1_bounding_box").arg(index));
    ctrl.bounding_box_check->setChecked(ax.show_bounding_box());
    grid_form->addRow("Bounding Box", ctrl.bounding_box_check);
    style_inspector_form(grid_form);
    grid_section->content_layout()->addLayout(grid_form);
    layout->addWidget(grid_section);

    layout->addStretch();
    axes_tab_widget_->addTab(tab, QString("Axes %1 (3D)").arg(index + 1));

    auto* undo_mgr     = services_ ? &services_->undo() : nullptr;
    auto* redraw       = services_ ? services_->redraw_request() : nullptr;
    auto  resolve_axes = [this, index]() -> Axes3D*
    {
        Figure* figure = registry_ ? registry_->get(active_id_) : nullptr;
        return dynamic_cast<Axes3D*>(figure_axes_at(figure, index));
    };

    connect(ctrl.title_edit,
            &QLineEdit::textChanged,
            this,
            [this, resolve_axes, undo_mgr, redraw](const QString& text)
            {
                Axes3D* target = resolve_axes();
                if (!target || target->title() == text.toStdString())
                    return;
                const std::string before = target->title();
                const std::string after  = text.toStdString();
                target->title(after);
                if (undo_mgr)
                {
                    undo_mgr->push(UndoAction{"Change 3D axes title",
                                              [target, before]() { target->title(before); },
                                              [target, after]() { target->title(after); }});
                }
                request_redraw(redraw, active_id_);
            });

    auto connect_label =
        [this, resolve_axes, undo_mgr, redraw](QLineEdit* edit, Axis3DDimension dimension)
    {
        connect(edit,
                &QLineEdit::textChanged,
                this,
                [this, resolve_axes, undo_mgr, redraw, dimension](const QString& text)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_label(undo_mgr, *target, dimension, text.toStdString());
                    request_redraw(redraw, active_id_);
                });
    };
    connect_label(ctrl.xlabel_edit, Axis3DDimension::X);
    connect_label(ctrl.ylabel_edit, Axis3DDimension::Y);
    connect_label(ctrl.zlabel_edit, Axis3DDimension::Z);

    auto connect_limits = [this, resolve_axes, undo_mgr, redraw](QDoubleSpinBox* min_spin,
                                                                 QDoubleSpinBox* max_spin,
                                                                 Axis3DDimension dimension)
    {
        connect(min_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this, resolve_axes, undo_mgr, redraw, dimension, max_spin](double value)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_limits(undo_mgr,
                                               *target,
                                               dimension,
                                               {value, max_spin->value()});
                    request_redraw(redraw, active_id_);
                });
        connect(max_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this, resolve_axes, undo_mgr, redraw, dimension, min_spin](double value)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_limits(undo_mgr,
                                               *target,
                                               dimension,
                                               {min_spin->value(), value});
                    request_redraw(redraw, active_id_);
                });
    };
    connect_limits(ctrl.xmin_spin, ctrl.xmax_spin, Axis3DDimension::X);
    connect_limits(ctrl.ymin_spin, ctrl.ymax_spin, Axis3DDimension::Y);
    connect_limits(ctrl.zmin_spin, ctrl.zmax_spin, Axis3DDimension::Z);

    connect(ctrl.grid_planes_combo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this, resolve_axes, undo_mgr, redraw, combo = ctrl.grid_planes_combo](int idx)
            {
                Axes3D* target = resolve_axes();
                if (!target)
                    return;
                int  data   = combo->itemData(idx).toInt();
                auto before = target->grid_planes();
                auto mode   = static_cast<Axes3D::GridPlane>(data);
                target->grid_planes(mode);
                if (undo_mgr)
                {
                    undo_mgr->push(UndoAction{"Change 3D grid planes",
                                              [target, before]() { target->grid_planes(before); },
                                              [target, mode]() { target->grid_planes(mode); }});
                }
                request_redraw(redraw, active_id_);
            });

    connect(ctrl.bounding_box_check,
            &QCheckBox::toggled,
            this,
            [this, resolve_axes, undo_mgr, redraw](bool checked)
            {
                Axes3D* target = resolve_axes();
                if (!target)
                    return;
                bool before = target->show_bounding_box();
                target->show_bounding_box(checked);
                if (undo_mgr)
                {
                    undo_mgr->push(
                        UndoAction{checked ? "Show bounding box" : "Hide bounding box",
                                   [target, before]() { target->show_bounding_box(before); },
                                   [target, checked]() { target->show_bounding_box(checked); }});
                }
                request_redraw(redraw, active_id_);
            });

    axes_controls_.push_back(ctrl);
    axes_series_counts_.push_back(ax.series().size());
}

void QtInspectorWidget::build_series_list()
{
    if (!series_list_)
        return;
    series_list_->clear();
    clear_series_properties();

    Figure* figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (!figure)
        return;

    int ax_idx = 0;
    figure->for_each_axes(
        [&](AxesBase* ax_base)
        {
            if (!ax_base)
            {
                ++ax_idx;
                return;
            }
            int s_idx = 0;
            for (const auto& s : ax_base->series())
            {
                if (!s)
                {
                    ++s_idx;
                    continue;
                }
                QString label = QString::fromStdString(s->label().empty() ? "Unnamed" : s->label());
                auto*   item  = series_list_->addSeries(
                    s->color(),
                    label,
                    s->visible(),
                    static_cast<qulonglong>(reinterpret_cast<quintptr>(s.get())));
                item->setData(Qt::UserRole, ax_idx);
                item->setData(Qt::UserRole + 1, s_idx);
                ++s_idx;
            }
            ++ax_idx;
        });

    if (series_list_conn_)
        disconnect(series_list_conn_);
    series_list_conn_ =
        connect(series_list_,
                &SpectraSeriesListView::currentRowChanged,
                this,
                [this](int row)
                {
                    if (row < 0)
                    {
                        clear_series_properties();
                        return;
                    }
                    auto* item = series_list_->itemAt(row);
                    if (!item)
                        return;
                    int     ax_idx = item->data(Qt::UserRole).toInt();
                    int     s_idx  = item->data(Qt::UserRole + 1).toInt();
                    Figure* fig    = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    AxesBase* ab = figure_axes_at(fig, ax_idx);
                    if (!ab)
                        return;
                    if (static_cast<size_t>(s_idx) >= ab->series().size())
                        return;
                    Series* s = ab->series()[s_idx].get();
                    if (!s)
                        return;
                    build_series_properties(*s);
                    ctx_->select_series(fig, dynamic_cast<Axes*>(ab), ax_idx, s, s_idx);
                    ctx_->axes_base = ab;
                    if (!ctx_->selected_series.empty())
                        ctx_->selected_series[0].axes_base = ab;
                });
}

void QtInspectorWidget::clear_series_properties()
{
    if (!series_props_layout_)
        return;
    while (QLayoutItem* item = series_props_layout_->takeAt(0))
    {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    series_controls_ = {};
}

void QtInspectorWidget::build_series_properties(Series& s)
{
    clear_series_properties();
    if (!series_props_layout_)
        return;

    auto* section = new CollapsibleSection("APPEARANCE", series_props_);
    auto* form    = make_inspector_form();

    series_controls_.label_edit = new QLineEdit(series_props_);
    series_controls_.label_edit->setText(QString::fromStdString(s.label()));
    form->addRow("Label", series_controls_.label_edit);

    series_controls_.color_btn = make_color_button(series_props_, s.color());
    form->addRow("Color", series_controls_.color_btn);

    series_controls_.visible_check = new QCheckBox("Visible", series_props_);
    series_controls_.visible_check->setChecked(s.visible());
    form->addRow(series_controls_.visible_check);

    series_controls_.width_spin = new QDoubleSpinBox(series_props_);
    configure_inspector_spin(series_controls_.width_spin);
    series_controls_.width_spin->setRange(0.1, 20.0);
    series_controls_.width_spin->setDecimals(1);
    series_controls_.width_spin->setSingleStep(0.5);
    series_controls_.width_spin->setValue(s.plot_style().line_width);
    form->addRow("Width", series_controls_.width_spin);

    series_controls_.opacity_spin = new QDoubleSpinBox(series_props_);
    configure_inspector_spin(series_controls_.opacity_spin);
    series_controls_.opacity_spin->setRange(0.0, 1.0);
    series_controls_.opacity_spin->setDecimals(2);
    series_controls_.opacity_spin->setSingleStep(0.05);
    series_controls_.opacity_spin->setValue(s.opacity());
    form->addRow("Opacity", series_controls_.opacity_spin);

    series_controls_.line_style_combo = new QComboBox(series_props_);
    for (int ls = 0; ls < LINE_STYLE_COUNT; ++ls)
        series_controls_.line_style_combo->addItem(line_style_name(ALL_LINE_STYLES[ls]));
    series_controls_.line_style_combo->setCurrentIndex(static_cast<int>(s.line_style()));
    form->addRow("Line", series_controls_.line_style_combo);

    series_controls_.marker_style_combo = new QComboBox(series_props_);
    for (int ms = 0; ms < MARKER_STYLE_COUNT; ++ms)
        series_controls_.marker_style_combo->addItem(marker_style_name(ALL_MARKER_STYLES[ms]));
    series_controls_.marker_style_combo->setCurrentIndex(static_cast<int>(s.marker_style()));
    form->addRow("Marker", series_controls_.marker_style_combo);

    bool has_marker = s.marker_style() != MarkerStyle::None;
    if (has_marker)
    {
        series_controls_.marker_size_spin_ = new QDoubleSpinBox(series_props_);
        configure_inspector_spin(series_controls_.marker_size_spin_);
        series_controls_.marker_size_spin_->setRange(1.0, 30.0);
        series_controls_.marker_size_spin_->setDecimals(1);
        series_controls_.marker_size_spin_->setValue(s.marker_size());
        form->addRow("Marker Size", series_controls_.marker_size_spin_);
    }

    style_inspector_form(form);

    section->content_layout()->addLayout(form);
    series_props_layout_->addWidget(section);

    series_controls_.series = &s;

    // Preview (sparkline)
    auto* preview_section = new CollapsibleSection("PREVIEW", series_props_);
    auto* preview_layout  = new QVBoxLayout();
    preview_layout->setContentsMargins(0, 0, 0, 0);
    series_controls_.sparkline = new SparklineWidget(series_props_);
    series_controls_.sparkline->setObjectName("series_sparkline");
    series_controls_.sparkline->set_series(&s);
    preview_layout->addWidget(series_controls_.sparkline);
    preview_section->content_layout()->addLayout(preview_layout);
    series_props_layout_->addWidget(preview_section);

    // Data statistics
    auto* stats_section = new CollapsibleSection("DATA STATISTICS", series_props_);
    auto* stats_grid    = new QGridLayout();
    stats_grid->setContentsMargins(0, 0, 0, 0);
    stats_grid->setHorizontalSpacing(12);
    stats_grid->setVerticalSpacing(4);

    const auto& stat_colors  = spectra_colors();
    const auto  value_color  = stat_colors.text_primary.name(QColor::HexArgb);
    const auto  header_color = stat_colors.text_secondary.name(QColor::HexArgb);

    auto* x_header = new QLabel("X", series_props_);
    x_header->setStyleSheet(QString("color: %1;").arg(header_color));
    x_header->setAlignment(Qt::AlignCenter);
    auto* y_header = new QLabel("Y", series_props_);
    y_header->setStyleSheet(QString("color: %1;").arg(header_color));
    y_header->setAlignment(Qt::AlignCenter);
    stats_grid->addWidget(new QLabel(""), 0, 0);
    stats_grid->addWidget(x_header, 0, 1);
    stats_grid->addWidget(y_header, 0, 2);

    auto add_stat_row = [&](const QString& name,
                            QLabel*&       x_label,
                            QLabel*&       y_label,
                            const QString& x_name,
                            const QString& y_name)
    {
        auto* label = new QLabel(name, series_props_);
        x_label     = new QLabel(series_props_);
        x_label->setObjectName(x_name);
        x_label->setStyleSheet(QString("color: %1;").arg(value_color));
        x_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        y_label = new QLabel(series_props_);
        y_label->setObjectName(y_name);
        y_label->setStyleSheet(QString("color: %1;").arg(value_color));
        y_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        int row = stats_grid->rowCount();
        stats_grid->addWidget(label, row, 0);
        stats_grid->addWidget(x_label, row, 1);
        stats_grid->addWidget(y_label, row, 2);
    };

    add_stat_row("Count",
                 series_controls_.stats_x_count_label,
                 series_controls_.stats_y_count_label,
                 "series_stats_x_count",
                 "series_stats_y_count");
    add_stat_row("Min",
                 series_controls_.stats_x_min_label,
                 series_controls_.stats_y_min_label,
                 "series_stats_x_min",
                 "series_stats_y_min");
    add_stat_row("Max",
                 series_controls_.stats_x_max_label,
                 series_controls_.stats_y_max_label,
                 "series_stats_x_max",
                 "series_stats_y_max");
    add_stat_row("Mean",
                 series_controls_.stats_x_mean_label,
                 series_controls_.stats_y_mean_label,
                 "series_stats_x_mean",
                 "series_stats_y_mean");
    add_stat_row("Sum",
                 series_controls_.stats_x_sum_label,
                 series_controls_.stats_y_sum_label,
                 "series_stats_x_sum",
                 "series_stats_y_sum");

    stats_section->content_layout()->addLayout(stats_grid);
    series_props_layout_->addWidget(stats_section);

    auto* undo_mgr = services_ ? &services_->undo() : nullptr;
    auto* redraw   = services_ ? services_->redraw_request() : nullptr;

    connect(series_controls_.label_edit,
            &QLineEdit::textChanged,
            this,
            [&s, undo_mgr, redraw](const QString& text)
            {
                if (s.label() == text.toStdString())
                    return;
                const std::string before = s.label();
                const std::string after  = text.toStdString();
                s.label(after);
                if (undo_mgr)
                {
                    undo_mgr->push(UndoAction{"Change series label",
                                              [&s, before]() { s.label(before); },
                                              [&s, after]() { s.label(after); }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.color_btn,
            &QPushButton::clicked,
            this,
            [services_ = services_, &s, undo_mgr, redraw, btn = series_controls_.color_btn]()
            {
                auto* dialogs = services_ ? services_->dialog_service() : nullptr;
                if (!dialogs)
                    return;
                auto chosen = dialogs->color_picker("Series Color", s.color());
                if (!chosen)
                    return;
                undoable_set_series_color(undo_mgr, s, *chosen);
                update_color_button(btn, s.color());
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.visible_check,
            &QCheckBox::toggled,
            this,
            [&s, undo_mgr, redraw](bool checked)
            {
                bool before = s.visible();
                s.visible(checked);
                if (undo_mgr)
                {
                    undo_mgr->push(
                        UndoAction{(checked ? "Show " : "Hide ")
                                       + (s.label().empty() ? std::string("series") : s.label()),
                                   [&s, before]() { s.visible(before); },
                                   [&s, checked]() { s.visible(checked); }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.width_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [&s, undo_mgr, redraw](double val)
            {
                auto* line = dynamic_cast<LineSeries*>(&s);
                if (line)
                    undoable_set_line_width(undo_mgr, *line, static_cast<float>(val));
                else
                    s.plot_style_mut().line_width = static_cast<float>(val);
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.opacity_spin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [&s, undo_mgr, redraw](double val)
            {
                undoable_set_opacity(undo_mgr, s, static_cast<float>(val));
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.line_style_combo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [&s, undo_mgr, redraw](int idx)
            {
                if (idx < 0 || idx >= LINE_STYLE_COUNT)
                    return;
                undoable_set_line_style(undo_mgr, s, ALL_LINE_STYLES[idx]);
                if (redraw)
                    redraw->request_redraw();
            });

    connect(series_controls_.marker_style_combo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [&s, undo_mgr, redraw](int idx)
            {
                if (idx < 0 || idx >= MARKER_STYLE_COUNT)
                    return;
                undoable_set_marker_style(undo_mgr, s, ALL_MARKER_STYLES[idx]);
                if (redraw)
                    redraw->request_redraw();
            });

    if (series_controls_.marker_size_spin_)
    {
        connect(series_controls_.marker_size_spin_,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [&s, undo_mgr, redraw](double val)
                {
                    undoable_set_series_marker_size(undo_mgr, s, static_cast<float>(val));
                    if (redraw)
                        redraw->request_redraw();
                });
    }

    update_series_preview();
}

void QtInspectorWidget::update_series_preview()
{
    if (!series_controls_.series)
        return;

    Series& s = *series_controls_.series;

    std::span<const float> x_data;
    std::span<const float> y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    const bool has_data = count > 0 && !x_data.empty() && !y_data.empty();

    if (series_controls_.sparkline)
        series_controls_.sparkline->set_series(&s);

    const auto set_label = [](QLabel* l, double v, bool valid)
    { l->setText(valid ? format_stat(v) : QStringLiteral("—")); };

    const auto compute = [](std::span<const float> data, double& min, double& max, double& sum)
    {
        min = static_cast<double>(*std::min_element(data.begin(), data.end()));
        max = static_cast<double>(*std::max_element(data.begin(), data.end()));
        sum = std::accumulate(data.begin(), data.end(), 0.0);
    };

    if (has_data)
    {
        double x_min = 0.0;
        double x_max = 0.0;
        double x_sum = 0.0;
        compute(x_data, x_min, x_max, x_sum);
        set_label(series_controls_.stats_x_count_label, static_cast<double>(x_data.size()), true);
        set_label(series_controls_.stats_x_min_label, x_min, true);
        set_label(series_controls_.stats_x_max_label, x_max, true);
        set_label(series_controls_.stats_x_mean_label,
                  x_sum / static_cast<double>(x_data.size()),
                  true);
        set_label(series_controls_.stats_x_sum_label, x_sum, true);

        double y_min = 0.0;
        double y_max = 0.0;
        double y_sum = 0.0;
        compute(y_data, y_min, y_max, y_sum);
        set_label(series_controls_.stats_y_count_label, static_cast<double>(y_data.size()), true);
        set_label(series_controls_.stats_y_min_label, y_min, true);
        set_label(series_controls_.stats_y_max_label, y_max, true);
        set_label(series_controls_.stats_y_mean_label,
                  y_sum / static_cast<double>(y_data.size()),
                  true);
        set_label(series_controls_.stats_y_sum_label, y_sum, true);
    }
    else
    {
        for (auto* l : {series_controls_.stats_x_count_label,
                        series_controls_.stats_x_min_label,
                        series_controls_.stats_x_max_label,
                        series_controls_.stats_x_mean_label,
                        series_controls_.stats_x_sum_label,
                        series_controls_.stats_y_count_label,
                        series_controls_.stats_y_min_label,
                        series_controls_.stats_y_max_label,
                        series_controls_.stats_y_mean_label,
                        series_controls_.stats_y_sum_label})
        {
            if (l)
                l->setText(QStringLiteral("—"));
        }
    }
}

void QtInspectorWidget::wire_figure_page()
{
    auto* undo_mgr = services_ ? &services_->undo() : nullptr;
    auto* redraw   = services_ ? services_->redraw_request() : nullptr;

    figure_bg_color_field_->setColorPicker(
        [this](const QString& title, const spectra::Color& current)
        {
            auto* dialogs = services_ ? services_->dialog_service() : nullptr;
            if (!dialogs)
                return std::optional<spectra::Color>(std::nullopt);
            return dialogs->color_picker(title.toStdString(), current);
        });

    connect(figure_bg_color_field_,
            &SpectraColorField::colorChanged,
            this,
            [this, undo_mgr, redraw](const spectra::Color& color)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                auto before             = fig->style().background;
                fig->style().background = color;
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(
                        UndoAction{"Change background color",
                                   [target, before]() { target->style().background = before; },
                                   [target, color]() { target->style().background = color; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    auto connect_margin = [this, undo_mgr, redraw](QDoubleSpinBox* spin, float FigureStyle::*member)
    {
        connect(spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this, undo_mgr, redraw, member](double val)
                {
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    float before         = fig->style().*member;
                    fig->style().*member = static_cast<float>(val);
                    if (undo_mgr)
                    {
                        auto* target = fig;
                        undo_mgr->push(UndoAction{
                            "Change margin",
                            [target, member, before]() { target->style().*member = before; },
                            [target, member, val]()
                            { target->style().*member = static_cast<float>(val); }});
                    }
                    if (redraw)
                        redraw->request_redraw();
                });
    };
    connect_margin(margin_top_spin_, &FigureStyle::margin_top);
    connect_margin(margin_bottom_spin_, &FigureStyle::margin_bottom);
    connect_margin(margin_left_spin_, &FigureStyle::margin_left);
    connect_margin(margin_right_spin_, &FigureStyle::margin_right);
    connect_margin(margin_hgap_spin_, &FigureStyle::subplot_hgap);
    connect_margin(margin_vgap_spin_, &FigureStyle::subplot_vgap);
    connect_margin(margin_min_h_spin_, &FigureStyle::min_subplot_height);

    connect(
        legend_visible_check_,
        &QCheckBox::toggled,
        this,
        [this, undo_mgr, redraw](bool checked)
        {
            Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
            if (!fig)
                return;
            bool old_val          = fig->legend().visible;
            fig->legend().visible = checked;
            if (undo_mgr)
            {
                Figure* ptr = fig;
                undo_mgr->push(UndoAction{checked ? "Show legend" : "Hide legend",
                                          [ptr, old_val]() { ptr->legend().visible = old_val; },
                                          [ptr, checked]() { ptr->legend().visible = checked; }});
            }
            if (redraw)
                redraw->request_redraw();
        });

    connect(legend_position_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this, undo_mgr, redraw](int idx)
            {
                if (idx < 0 || idx > 4)
                    return;
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                auto before            = fig->legend().position;
                auto pos               = static_cast<LegendPosition>(idx);
                fig->legend().position = pos;
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(
                        UndoAction{"Change legend position",
                                   [target, before]() { target->legend().position = before; },
                                   [target, pos]() { target->legend().position = pos; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(legend_font_size_spin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, undo_mgr, redraw](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                float before            = fig->legend().font_size;
                fig->legend().font_size = static_cast<float>(val);
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(UndoAction{
                        "Change legend font size",
                        [target, before]() { target->legend().font_size = before; },
                        [target, val]() { target->legend().font_size = static_cast<float>(val); }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(legend_padding_spin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this, undo_mgr, redraw](double val)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                float before          = fig->legend().padding;
                fig->legend().padding = static_cast<float>(val);
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(UndoAction{
                        "Change legend padding",
                        [target, before]() { target->legend().padding = before; },
                        [target, val]() { target->legend().padding = static_cast<float>(val); }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    legend_bg_color_field_->setColorPicker(
        [this](const QString& title, const spectra::Color& current)
        {
            auto* dialogs = services_ ? services_->dialog_service() : nullptr;
            if (!dialogs)
                return std::optional<spectra::Color>(std::nullopt);
            return dialogs->color_picker(title.toStdString(), current);
        });

    connect(legend_bg_color_field_,
            &SpectraColorField::colorChanged,
            this,
            [this, undo_mgr, redraw](const spectra::Color& color)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                auto before            = fig->legend().bg_color;
                fig->legend().bg_color = color;
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(
                        UndoAction{"Change legend background color",
                                   [target, before]() { target->legend().bg_color = before; },
                                   [target, color]() { target->legend().bg_color = color; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    legend_border_color_field_->setColorPicker(
        [this](const QString& title, const spectra::Color& current)
        {
            auto* dialogs = services_ ? services_->dialog_service() : nullptr;
            if (!dialogs)
                return std::optional<spectra::Color>(std::nullopt);
            return dialogs->color_picker(title.toStdString(), current);
        });

    connect(legend_border_color_field_,
            &SpectraColorField::colorChanged,
            this,
            [this, undo_mgr, redraw](const spectra::Color& color)
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                auto before                = fig->legend().border_color;
                fig->legend().border_color = color;
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(
                        UndoAction{"Change legend border color",
                                   [target, before]() { target->legend().border_color = before; },
                                   [target, color]() { target->legend().border_color = color; }});
                }
                if (redraw)
                    redraw->request_redraw();
            });

    connect(reset_figure_style_btn_,
            &QPushButton::clicked,
            this,
            [this, undo_mgr, redraw]()
            {
                Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                if (!fig)
                    return;
                FigureStyle  before     = fig->style();
                LegendConfig leg_before = fig->legend();
                fig->style()            = FigureStyle{};
                fig->legend()           = LegendConfig{};
                if (undo_mgr)
                {
                    auto* target = fig;
                    undo_mgr->push(UndoAction{"Reset figure style",
                                              [target, before, leg_before]()
                                              {
                                                  target->style()  = before;
                                                  target->legend() = leg_before;
                                              },
                                              [target]()
                                              {
                                                  target->style()  = FigureStyle{};
                                                  target->legend() = LegendConfig{};
                                              }});
                }
                refresh();
                if (redraw)
                    redraw->request_redraw();
            });
}

}   // namespace spectra::adapters::qt
