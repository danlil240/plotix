// inspector_widget.cpp — Qt inspector panel implementation.

#include "inspector_widget.hpp"

#include "app/application_services.hpp"
#include "ui/commands/undoable_property.hpp"
#include "app/frontend_services.hpp"

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
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

}   // namespace

QtInspectorWidget::QtInspectorWidget(FigureRegistry*      registry,
                                     ApplicationServices* services,
                                     QWidget*             parent)
    : QWidget(parent), registry_(registry), services_(services)
{
    setObjectName("inspector_panel");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tab_widget_ = new QTabWidget(this);
    tab_widget_->setObjectName("inspector_tabs");
    layout->addWidget(tab_widget_);

    // Placeholder when no figure is active
    auto* placeholder = new QLabel("No figure selected", tab_widget_);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: gray; padding: 40px;");
    tab_widget_->addTab(placeholder, "No Figure");
}

void QtInspectorWidget::set_active_figure(FigureId id)
{
    active_id_ = id;
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

void QtInspectorWidget::refresh()
{
    clear_axes_tabs();

    if (!registry_ || active_id_ == INVALID_FIGURE_ID)
    {
        auto* placeholder = new QLabel("No figure selected", tab_widget_);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: gray; padding: 40px;");
        tab_widget_->addTab(placeholder, "No Figure");
        return;
    }

    Figure* figure = registry_->get(active_id_);
    if (!figure)
    {
        auto* placeholder = new QLabel("Figure not found", tab_widget_);
        placeholder->setAlignment(Qt::AlignCenter);
        tab_widget_->addTab(placeholder, "Error");
        return;
    }

    // ── Figure properties tab ─────────────────────────────────────────
    auto* fig_tab    = new QWidget(tab_widget_);
    auto* fig_layout = new QVBoxLayout(fig_tab);
    fig_layout->setContentsMargins(12, 12, 12, 12);
    fig_layout->setSpacing(10);

    auto* fig_group = new QGroupBox("Figure", fig_tab);
    auto* fig_form  = new QFormLayout(fig_group);

    figure_title_edit_ = new QLineEdit(fig_group);
    figure_title_edit_->setObjectName("figure_title");
    // Use tab_title since that's the user-facing name
    figure_title_edit_->setText(QString::fromStdString(figure->tab_title()));
    fig_form->addRow("Title", figure_title_edit_);

    figure_size_label_ =
        new QLabel(QString("%1 × %2").arg(figure->width()).arg(figure->height()), fig_group);
    figure_size_label_->setObjectName("figure_size");
    fig_form->addRow("Size", figure_size_label_);

    const auto axes   = active_axes();
    axes_count_label_ = new QLabel(QString::number(axes.size()), fig_group);
    axes_count_label_->setObjectName("figure_axes_count");
    fig_form->addRow("Axes", axes_count_label_);

    fig_layout->addWidget(fig_group);

    // Legend group
    auto* legend_group = new QGroupBox("Legend", fig_tab);
    auto* legend_form  = new QFormLayout(legend_group);
    legend_check_      = new QCheckBox("Visible", legend_group);
    legend_check_->setObjectName("figure_legend_visible");
    legend_check_->setChecked(figure->legend().visible);
    legend_form->addRow("Show", legend_check_);
    fig_layout->addWidget(legend_group);

    fig_layout->addStretch();

    tab_widget_->addTab(fig_tab, "Figure");

    // Wire figure title change — use FigureId for safe lookup
    FigureId fig_id   = active_id_;
    auto*    undo_mgr = services_ ? &services_->undo() : nullptr;
    auto*    redraw   = services_ ? services_->redraw_request() : nullptr;

    connect(figure_title_edit_,
            &QLineEdit::textChanged,
            this,
            [this, fig_id, undo_mgr, redraw](const QString& text)
            {
                if (!registry_)
                    return;
                if (!undoable_set_figure_title(undo_mgr, *registry_, fig_id, text.toStdString()))
                    return;
                emit figure_title_changed(fig_id, text);
                if (redraw)
                    redraw->request_redraw(fig_id);
            });
    connect(
        legend_check_,
        &QCheckBox::toggled,
        this,
        [this, fig_id, undo_mgr, redraw](bool checked)
        {
            Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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

    // ── Per-axes tabs ─────────────────────────────────────────────────
    for (int axes_idx = 0; axes_idx < static_cast<int>(axes.size()); ++axes_idx)
    {
        if (auto* ax = dynamic_cast<Axes*>(axes[axes_idx]))
            build_axes_tab(*ax, axes_idx);
        else if (auto* ax3d = dynamic_cast<Axes3D*>(axes[axes_idx]))
            build_axes3d_tab(*ax3d, axes_idx);
    }
}

void QtInspectorWidget::sync_from_model()
{
    Figure* figure =
        registry_ && active_id_ != INVALID_FIGURE_ID ? registry_->get(active_id_) : nullptr;
    if (!figure)
    {
        if (tab_widget_->count() != 1 || tab_widget_->tabText(0) == "Figure")
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

    set_line_edit_from_model(figure_title_edit_, figure->tab_title());
    if (figure_size_label_)
        figure_size_label_->setText(QString("%1 × %2").arg(figure->width()).arg(figure->height()));
    if (axes_count_label_)
        axes_count_label_->setText(QString::number(axes.size()));
    if (legend_check_ && legend_check_->isChecked() != figure->legend().visible)
    {
        const QSignalBlocker blocker(legend_check_);
        legend_check_->setChecked(figure->legend().visible);
    }

    for (size_t i = 0; i < axes.size(); ++i)
    {
        auto& controls = axes_controls_[i];
        set_line_edit_from_model(controls.title_edit, axes[i]->title());
        if (auto* ax = dynamic_cast<Axes*>(axes[i]))
        {
            set_line_edit_from_model(controls.xlabel_edit, ax->xlabel());
            set_line_edit_from_model(controls.ylabel_edit, ax->ylabel());
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
            if (controls.border_check
                && controls.border_check->isChecked() != ax3d->show_bounding_box())
            {
                const QSignalBlocker blocker(controls.border_check);
                controls.border_check->setChecked(ax3d->show_bounding_box());
            }
        }
    }
}

void QtInspectorWidget::build_series_section(AxesBase&    ax,
                                             int          axes_idx,
                                             QVBoxLayout* layout,
                                             QWidget*     parent)
{
    const auto& series_list = ax.series();
    if (series_list.empty())
        return;

    auto* series_group  = new QGroupBox("Series", parent);
    auto* series_layout = new QVBoxLayout(series_group);

    const FigureId fig_id = active_id_;
    for (size_t i = 0; i < series_list.size(); ++i)
    {
        auto* s = series_list[i].get();
        if (!s)
            continue;

        SeriesControls ctrl{};

        auto* s_row = new QFormLayout();

        // Label
        ctrl.label_edit = new QLineEdit(series_group);
        ctrl.label_edit->setText(QString::fromStdString(s->label()));
        s_row->addRow("Label", ctrl.label_edit);

        // Color button
        ctrl.color_btn = new QPushButton(series_group);
        ctrl.color_btn->setObjectName(QString("series_color_%1").arg(i));
        QColor qcol(static_cast<int>(s->color().r * 255),
                    static_cast<int>(s->color().g * 255),
                    static_cast<int>(s->color().b * 255));
        ctrl.color_btn->setText(qcol.name());
        ctrl.color_btn->setStyleSheet(
            QString("background-color: %1; min-width: 60px;").arg(qcol.name()));
        s_row->addRow("Color", ctrl.color_btn);

        // Line width
        ctrl.width_spin = new QDoubleSpinBox(series_group);
        ctrl.width_spin->setRange(0.1, 20.0);
        ctrl.width_spin->setDecimals(1);
        ctrl.width_spin->setSingleStep(0.5);
        ctrl.width_spin->setValue(s->plot_style().line_width);
        s_row->addRow("Width", ctrl.width_spin);

        // Opacity
        ctrl.opacity_spin = new QDoubleSpinBox(series_group);
        ctrl.opacity_spin->setRange(0.0, 1.0);
        ctrl.opacity_spin->setDecimals(2);
        ctrl.opacity_spin->setSingleStep(0.05);
        ctrl.opacity_spin->setValue(s->opacity());
        s_row->addRow("Opacity", ctrl.opacity_spin);

        // Line style combo
        ctrl.line_style_combo = new QComboBox(series_group);
        for (int ls = 0; ls < LINE_STYLE_COUNT; ++ls)
            ctrl.line_style_combo->addItem(line_style_name(ALL_LINE_STYLES[ls]));
        ctrl.line_style_combo->setCurrentIndex(static_cast<int>(s->line_style()));
        s_row->addRow("Line", ctrl.line_style_combo);

        // Marker style combo
        ctrl.marker_style_combo = new QComboBox(series_group);
        for (int ms = 0; ms < MARKER_STYLE_COUNT; ++ms)
            ctrl.marker_style_combo->addItem(marker_style_name(ALL_MARKER_STYLES[ms]));
        ctrl.marker_style_combo->setCurrentIndex(static_cast<int>(s->marker_style()));
        s_row->addRow("Marker", ctrl.marker_style_combo);

        // Visible checkbox
        ctrl.visible_check = new QCheckBox("Visible", series_group);
        ctrl.visible_check->setChecked(s->visible());
        s_row->addRow("", ctrl.visible_check);

        series_layout->addLayout(s_row);

        // ── Wire changes — use FigureId + indices for safe lookup ──────
        size_t series_idx = i;
        auto*  undo_mgr   = services_ ? &services_->undo() : nullptr;
        auto*  redraw     = services_ ? services_->redraw_request() : nullptr;

        connect(ctrl.label_edit,
                &QLineEdit::textChanged,
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](const QString& text)
                {
                    if (!registry_)
                        return;
                    if (undoable_set_series_label(undo_mgr,
                                                  *registry_,
                                                  fig_id,
                                                  axes_idx,
                                                  series_idx,
                                                  text.toStdString())
                        && redraw)
                        redraw->request_redraw(fig_id);
                });

        connect(ctrl.color_btn,
                &QPushButton::clicked,
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw, btn = ctrl.color_btn]()
                {
                    Series* s = registry_
                                    ? find_figure_series(*registry_, fig_id, axes_idx, series_idx)
                                    : nullptr;
                    if (!s)
                        return;
                    auto* dialogs = services_ ? services_->dialog_service() : nullptr;
                    if (!dialogs)
                        return;
                    const auto chosen = dialogs->color_picker("Series Color", s->color());
                    if (chosen)
                    {
                        undoable_set_series_color(undo_mgr, *s, *chosen);
                        const QColor qchosen =
                            QColor::fromRgbF(chosen->r, chosen->g, chosen->b, chosen->a);
                        btn->setText(qchosen.name());
                        btn->setStyleSheet(
                            QString("background-color: %1; min-width: 60px;").arg(qchosen.name()));
                        if (redraw)
                            redraw->request_redraw();
                    }
                });

        connect(ctrl.width_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](double val)
                {
                    if (!registry_)
                        return;
                    if (undoable_set_series_line_width(undo_mgr,
                                                       *registry_,
                                                       fig_id,
                                                       axes_idx,
                                                       series_idx,
                                                       static_cast<float>(val))
                        && redraw)
                        redraw->request_redraw(fig_id);
                });

        connect(ctrl.opacity_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](double val)
                {
                    Series* s = registry_
                                    ? find_figure_series(*registry_, fig_id, axes_idx, series_idx)
                                    : nullptr;
                    if (!s)
                        return;
                    undoable_set_opacity(undo_mgr, *s, static_cast<float>(val));
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.line_style_combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](int idx)
                {
                    if (idx < 0 || idx >= LINE_STYLE_COUNT)
                        return;
                    Series* s = registry_
                                    ? find_figure_series(*registry_, fig_id, axes_idx, series_idx)
                                    : nullptr;
                    if (!s)
                        return;
                    undoable_set_line_style(undo_mgr, *s, ALL_LINE_STYLES[idx]);
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.marker_style_combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](int idx)
                {
                    if (idx < 0 || idx >= MARKER_STYLE_COUNT)
                        return;
                    Series* s = registry_
                                    ? find_figure_series(*registry_, fig_id, axes_idx, series_idx)
                                    : nullptr;
                    if (!s)
                        return;
                    undoable_set_marker_style(undo_mgr, *s, ALL_MARKER_STYLES[idx]);
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.visible_check,
                &QCheckBox::toggled,
                this,
                [this, fig_id, axes_idx, series_idx, undo_mgr, redraw](bool checked)
                {
                    Series* s = registry_
                                    ? find_figure_series(*registry_, fig_id, axes_idx, series_idx)
                                    : nullptr;
                    if (!s)
                        return;
                    s->visible(checked);
                    if (undo_mgr)
                        undo_mgr->push(UndoAction{
                            (checked ? "Show " : "Hide ")
                                + (s->label().empty() ? std::string("series") : s->label()),
                            [s, checked]() { s->visible(!checked); },
                            [s, checked]() { s->visible(checked); }});
                    if (redraw)
                        redraw->request_redraw();
                });

        series_controls_.push_back(ctrl);
    }

    layout->addWidget(series_group);
}

void QtInspectorWidget::build_axes_tab(Axes& ax, int index)
{
    auto* tab    = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    AxesControls ctrl{};
    ctrl.model = &ax;

    // ── Title ─────────────────────────────────────────────────────────
    auto* title_group = new QGroupBox("Title", tab);
    auto* title_form  = new QFormLayout(title_group);
    ctrl.title_edit   = new QLineEdit(title_group);
    ctrl.title_edit->setText(QString::fromStdString(ax.title()));
    title_form->addRow("Title", ctrl.title_edit);
    layout->addWidget(title_group);

    // ── X Axis ────────────────────────────────────────────────────────
    auto* x_group = new QGroupBox("X Axis", tab);
    auto* x_form  = new QFormLayout(x_group);

    ctrl.xlabel_edit = new QLineEdit(x_group);
    ctrl.xlabel_edit->setText(QString::fromStdString(ax.xlabel()));
    x_form->addRow("Label", ctrl.xlabel_edit);

    auto  xlim           = ax.x_limits();
    auto* x_range_layout = new QHBoxLayout();
    ctrl.xmin_spin       = new QDoubleSpinBox(x_group);
    ctrl.xmin_spin->setObjectName(QString("axes_%1_x_min").arg(index));
    ctrl.xmin_spin->setRange(-1e9, 1e9);
    ctrl.xmin_spin->setDecimals(3);
    ctrl.xmin_spin->setValue(xlim.min);
    x_range_layout->addWidget(ctrl.xmin_spin);

    ctrl.xmax_spin = new QDoubleSpinBox(x_group);
    ctrl.xmax_spin->setObjectName(QString("axes_%1_x_max").arg(index));
    ctrl.xmax_spin->setRange(-1e9, 1e9);
    ctrl.xmax_spin->setDecimals(3);
    ctrl.xmax_spin->setValue(xlim.max);
    x_range_layout->addWidget(ctrl.xmax_spin);
    x_form->addRow("Range", x_range_layout);

    layout->addWidget(x_group);

    // ── Y Axis ────────────────────────────────────────────────────────
    auto* y_group = new QGroupBox("Y Axis", tab);
    auto* y_form  = new QFormLayout(y_group);

    ctrl.ylabel_edit = new QLineEdit(y_group);
    ctrl.ylabel_edit->setText(QString::fromStdString(ax.ylabel()));
    y_form->addRow("Label", ctrl.ylabel_edit);

    auto  ylim           = ax.y_limits();
    auto* y_range_layout = new QHBoxLayout();
    ctrl.ymin_spin       = new QDoubleSpinBox(y_group);
    ctrl.ymin_spin->setObjectName(QString("axes_%1_y_min").arg(index));
    ctrl.ymin_spin->setRange(-1e9, 1e9);
    ctrl.ymin_spin->setDecimals(3);
    ctrl.ymin_spin->setValue(ylim.min);
    y_range_layout->addWidget(ctrl.ymin_spin);

    ctrl.ymax_spin = new QDoubleSpinBox(y_group);
    ctrl.ymax_spin->setObjectName(QString("axes_%1_y_max").arg(index));
    ctrl.ymax_spin->setRange(-1e9, 1e9);
    ctrl.ymax_spin->setDecimals(3);
    ctrl.ymax_spin->setValue(ylim.max);
    y_range_layout->addWidget(ctrl.ymax_spin);
    y_form->addRow("Range", y_range_layout);

    layout->addWidget(y_group);

    // ── Grid & Border ─────────────────────────────────────────────────
    auto* grid_group  = new QGroupBox("Grid & Border", tab);
    auto* grid_layout = new QVBoxLayout(grid_group);

    ctrl.grid_check = new QCheckBox("Show Grid", grid_group);
    ctrl.grid_check->setChecked(ax.grid_enabled());
    grid_layout->addWidget(ctrl.grid_check);

    ctrl.border_check = new QCheckBox("Show Border", grid_group);
    ctrl.border_check->setChecked(ax.border_enabled());
    grid_layout->addWidget(ctrl.border_check);

    layout->addWidget(grid_group);

    // ── Series count ──────────────────────────────────────────────────
    auto* series_label = new QLabel(QString("%1 series").arg(ax.series().size()), tab);
    series_label->setStyleSheet("color: gray;");
    layout->addWidget(series_label);

    // ── Series controls ───────────────────────────────────────────────
    build_series_section(ax, index, layout, tab);

    layout->addStretch();

    tab_widget_->addTab(tab, QString("Axes %1").arg(index + 1));

    // ── Wire changes — use FigureId + axes index for safe lookup ──────
    FigureId fig_id         = active_id_;
    int      axes_idx_local = index;
    auto*    undo_mgr       = services_ ? &services_->undo() : nullptr;
    auto*    redraw         = services_ ? services_->redraw_request() : nullptr;

    connect(ctrl.title_edit,
            &QLineEdit::textChanged,
            this,
            [this, fig_id, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw](const QString& text)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw, xmax_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw, xmin_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw, ymax_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw, ymin_spin](double val)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw](bool checked)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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
            [this, fig_id, axes_idx_local, undo_mgr, redraw](bool checked)
            {
                Figure* fig = registry_ ? registry_->get(fig_id) : nullptr;
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

    axes_controls_.push_back(ctrl);
    axes_series_counts_.push_back(ax.series().size());
}

void QtInspectorWidget::build_axes3d_tab(Axes3D& ax, int index)
{
    auto* tab    = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    AxesControls ctrl{};
    ctrl.model = &ax;

    auto* title_group = new QGroupBox("Title", tab);
    auto* title_form  = new QFormLayout(title_group);
    ctrl.title_edit   = new QLineEdit(title_group);
    ctrl.title_edit->setObjectName(QString("axes_%1_title").arg(index));
    ctrl.title_edit->setText(QString::fromStdString(ax.title()));
    title_form->addRow("Title", ctrl.title_edit);
    layout->addWidget(title_group);

    auto add_axis_group = [index, layout, tab](const QString&     name,
                                               const QString&     prefix,
                                               const std::string& label,
                                               const AxisLimits&  limits,
                                               QLineEdit*&        label_edit,
                                               QDoubleSpinBox*&   min_spin,
                                               QDoubleSpinBox*&   max_spin)
    {
        auto* group = new QGroupBox(name, tab);
        auto* form  = new QFormLayout(group);

        label_edit = new QLineEdit(group);
        label_edit->setObjectName(QString("axes_%1_%2_label").arg(index).arg(prefix));
        label_edit->setText(QString::fromStdString(label));
        form->addRow("Label", label_edit);

        auto* range_layout = new QHBoxLayout();
        min_spin           = new QDoubleSpinBox(group);
        min_spin->setObjectName(QString("axes_%1_%2_min").arg(index).arg(prefix));
        min_spin->setRange(-1e9, 1e9);
        min_spin->setDecimals(3);
        min_spin->setValue(limits.min);
        range_layout->addWidget(min_spin);

        max_spin = new QDoubleSpinBox(group);
        max_spin->setObjectName(QString("axes_%1_%2_max").arg(index).arg(prefix));
        max_spin->setRange(-1e9, 1e9);
        max_spin->setDecimals(3);
        max_spin->setValue(limits.max);
        range_layout->addWidget(max_spin);
        form->addRow("Range", range_layout);
        layout->addWidget(group);
    };

    add_axis_group("X Axis",
                   "x",
                   ax.xlabel(),
                   ax.x_limits(),
                   ctrl.xlabel_edit,
                   ctrl.xmin_spin,
                   ctrl.xmax_spin);
    add_axis_group("Y Axis",
                   "y",
                   ax.ylabel(),
                   ax.y_limits(),
                   ctrl.ylabel_edit,
                   ctrl.ymin_spin,
                   ctrl.ymax_spin);
    add_axis_group("Z Axis",
                   "z",
                   ax.zlabel(),
                   ax.z_limits(),
                   ctrl.zlabel_edit,
                   ctrl.zmin_spin,
                   ctrl.zmax_spin);

    auto* appearance_group  = new QGroupBox("Grid & Bounding Box", tab);
    auto* appearance_layout = new QFormLayout(appearance_group);
    ctrl.grid_planes_combo  = new QComboBox(appearance_group);
    ctrl.grid_planes_combo->setObjectName(QString("axes_%1_grid_planes").arg(index));
    ctrl.grid_planes_combo->addItem("None", static_cast<int>(Axes3D::GridPlane::None));
    ctrl.grid_planes_combo->addItem("XY", static_cast<int>(Axes3D::GridPlane::XY));
    ctrl.grid_planes_combo->addItem("XZ", static_cast<int>(Axes3D::GridPlane::XZ));
    ctrl.grid_planes_combo->addItem("YZ", static_cast<int>(Axes3D::GridPlane::YZ));
    ctrl.grid_planes_combo->addItem("All", static_cast<int>(Axes3D::GridPlane::All));
    ctrl.grid_planes_combo->setCurrentIndex(
        ctrl.grid_planes_combo->findData(static_cast<int>(ax.grid_planes())));
    appearance_layout->addRow("Grid Planes", ctrl.grid_planes_combo);

    ctrl.border_check = new QCheckBox("Visible", appearance_group);
    ctrl.border_check->setObjectName(QString("axes_%1_bounding_box").arg(index));
    ctrl.border_check->setChecked(ax.show_bounding_box());
    appearance_layout->addRow("Bounding Box", ctrl.border_check);
    layout->addWidget(appearance_group);

    auto* series_label = new QLabel(QString("%1 series").arg(ax.series().size()), tab);
    series_label->setStyleSheet("color: gray;");
    layout->addWidget(series_label);
    build_series_section(ax, index, layout, tab);
    layout->addStretch();
    tab_widget_->addTab(tab, QString("Axes %1 (3D)").arg(index + 1));

    const FigureId fig_id       = active_id_;
    auto*          undo_mgr     = services_ ? &services_->undo() : nullptr;
    auto*          redraw       = services_ ? services_->redraw_request() : nullptr;
    auto           resolve_axes = [this, fig_id, index]() -> Axes3D*
    {
        Figure* figure = registry_ ? registry_->get(fig_id) : nullptr;
        return dynamic_cast<Axes3D*>(figure_axes_at(figure, index));
    };

    connect(ctrl.title_edit,
            &QLineEdit::textChanged,
            this,
            [resolve_axes, undo_mgr, redraw, fig_id](const QString& text)
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
                request_redraw(redraw, fig_id);
            });

    auto connect_label =
        [this, resolve_axes, undo_mgr, redraw, fig_id](QLineEdit* edit, Axis3DDimension dimension)
    {
        connect(edit,
                &QLineEdit::textChanged,
                this,
                [resolve_axes, undo_mgr, redraw, fig_id, dimension](const QString& text)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_label(undo_mgr, *target, dimension, text.toStdString());
                    request_redraw(redraw, fig_id);
                });
    };
    connect_label(ctrl.xlabel_edit, Axis3DDimension::X);
    connect_label(ctrl.ylabel_edit, Axis3DDimension::Y);
    connect_label(ctrl.zlabel_edit, Axis3DDimension::Z);

    auto connect_limits = [this, resolve_axes, undo_mgr, redraw, fig_id](QDoubleSpinBox* min_spin,
                                                                         QDoubleSpinBox* max_spin,
                                                                         Axis3DDimension dimension)
    {
        connect(min_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [resolve_axes, undo_mgr, redraw, fig_id, dimension, max_spin](double value)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_limits(undo_mgr,
                                               *target,
                                               dimension,
                                               {value, max_spin->value()});
                    request_redraw(redraw, fig_id);
                });
        connect(max_spin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [resolve_axes, undo_mgr, redraw, fig_id, dimension, min_spin](double value)
                {
                    Axes3D* target = resolve_axes();
                    if (!target)
                        return;
                    undoable_set_axes3d_limits(undo_mgr,
                                               *target,
                                               dimension,
                                               {min_spin->value(), value});
                    request_redraw(redraw, fig_id);
                });
    };
    connect_limits(ctrl.xmin_spin, ctrl.xmax_spin, Axis3DDimension::X);
    connect_limits(ctrl.ymin_spin, ctrl.ymax_spin, Axis3DDimension::Y);
    connect_limits(ctrl.zmin_spin, ctrl.zmax_spin, Axis3DDimension::Z);

    connect(
        ctrl.grid_planes_combo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [resolve_axes, undo_mgr, redraw, fig_id, combo = ctrl.grid_planes_combo](int combo_index)
        {
            Axes3D* target = resolve_axes();
            if (!target || combo_index < 0)
                return;
            const auto before = target->grid_planes();
            const auto after = static_cast<Axes3D::GridPlane>(combo->itemData(combo_index).toInt());
            if (before == after)
                return;
            target->grid_planes(after);
            if (undo_mgr)
            {
                undo_mgr->push(UndoAction{"Change 3D grid planes",
                                          [target, before]() { target->grid_planes(before); },
                                          [target, after]() { target->grid_planes(after); }});
            }
            request_redraw(redraw, fig_id);
        });
    connect(ctrl.border_check,
            &QCheckBox::toggled,
            this,
            [resolve_axes, undo_mgr, redraw, fig_id](bool checked)
            {
                Axes3D* target = resolve_axes();
                if (!target || target->show_bounding_box() == checked)
                    return;
                const bool before = target->show_bounding_box();
                target->show_bounding_box(checked);
                if (undo_mgr)
                {
                    undo_mgr->push(
                        UndoAction{checked ? "Show 3D bounding box" : "Hide 3D bounding box",
                                   [target, before]() { target->show_bounding_box(before); },
                                   [target, checked]() { target->show_bounding_box(checked); }});
                }
                request_redraw(redraw, fig_id);
            });

    axes_controls_.push_back(ctrl);
    axes_series_counts_.push_back(ax.series().size());
}

void QtInspectorWidget::clear_axes_tabs()
{
    while (tab_widget_->count() > 0)
    {
        QWidget* page = tab_widget_->widget(0);
        tab_widget_->removeTab(0);
        delete page;
    }
    axes_controls_.clear();
    axes_series_counts_.clear();
    series_controls_.clear();
    figure_title_edit_ = nullptr;
    figure_size_label_ = nullptr;
    axes_count_label_  = nullptr;
    legend_check_      = nullptr;
}

}   // namespace spectra::adapters::qt
