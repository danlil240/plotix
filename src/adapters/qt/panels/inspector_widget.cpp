// inspector_widget.cpp — Qt inspector panel implementation.

#include "inspector_widget.hpp"

#include "app/application_services.hpp"
#include "ui/commands/undoable_property.hpp"
#include "app/frontend_services.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace spectra::adapters::qt
{

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
    if (active_id_ == id)
        return;

    active_id_ = id;
    refresh();
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

    auto* size_label =
        new QLabel(QString("%1 × %2").arg(figure->width()).arg(figure->height()), fig_group);
    fig_form->addRow("Size", size_label);

    auto* axes_count_label = new QLabel(QString::number(figure->all_axes().size()), fig_group);
    fig_form->addRow("Axes", axes_count_label);

    fig_layout->addWidget(fig_group);

    // Legend group
    auto* legend_group = new QGroupBox("Legend", fig_tab);
    auto* legend_form  = new QFormLayout(legend_group);
    auto* legend_check = new QCheckBox("Visible", legend_group);
    legend_check->setObjectName("figure_legend_visible");
    legend_check->setChecked(figure->legend().visible);
    legend_form->addRow("Show", legend_check);
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
                if (undoable_set_figure_title(undo_mgr, *registry_, fig_id, text.toStdString())
                    && redraw)
                    redraw->request_redraw(fig_id);
            });
    connect(
        legend_check,
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
    int axes_idx = 0;
    figure->for_each_axes(
        [&](AxesBase* ab)
        {
            auto* ax = dynamic_cast<Axes*>(ab);
            if (ax)
                build_axes_tab(*ax, axes_idx);
            ++axes_idx;
        });
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
                [this, axes_idx, series_idx, undo_mgr, redraw, btn = ctrl.color_btn]()
                {
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    Axes* ax = fig->get_axes(axes_idx);
                    if (!ax || series_idx >= ax->series().size())
                        return;
                    Series* s = ax->series()[series_idx].get();
                    QColor  initial(static_cast<int>(s->color().r * 255),
                                   static_cast<int>(s->color().g * 255),
                                   static_cast<int>(s->color().b * 255));
                    QColor  chosen = QColorDialog::getColor(initial,
                                                           nullptr,
                                                           "Series Color",
                                                           QColorDialog::ShowAlphaChannel);
                    if (chosen.isValid())
                    {
                        undoable_set_series_color(undo_mgr,
                                                  *s,
                                                  spectra::Color(chosen.redF(),
                                                                 chosen.greenF(),
                                                                 chosen.blueF(),
                                                                 chosen.alphaF()));
                        btn->setText(chosen.name());
                        btn->setStyleSheet(
                            QString("background-color: %1; min-width: 60px;").arg(chosen.name()));
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
                [this, axes_idx, series_idx, undo_mgr, redraw](double val)
                {
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    Axes* ax = fig->get_axes(axes_idx);
                    if (!ax || series_idx >= ax->series().size())
                        return;
                    Series* s = ax->series()[series_idx].get();
                    undoable_set_opacity(undo_mgr, *s, static_cast<float>(val));
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.line_style_combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this, axes_idx, series_idx, undo_mgr, redraw](int idx)
                {
                    if (idx < 0 || idx >= LINE_STYLE_COUNT)
                        return;
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    Axes* ax = fig->get_axes(axes_idx);
                    if (!ax || series_idx >= ax->series().size())
                        return;
                    Series* s = ax->series()[series_idx].get();
                    undoable_set_line_style(undo_mgr, *s, ALL_LINE_STYLES[idx]);
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.marker_style_combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                [this, axes_idx, series_idx, undo_mgr, redraw](int idx)
                {
                    if (idx < 0 || idx >= MARKER_STYLE_COUNT)
                        return;
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    Axes* ax = fig->get_axes(axes_idx);
                    if (!ax || series_idx >= ax->series().size())
                        return;
                    Series* s = ax->series()[series_idx].get();
                    undoable_set_marker_style(undo_mgr, *s, ALL_MARKER_STYLES[idx]);
                    if (redraw)
                        redraw->request_redraw();
                });

        connect(ctrl.visible_check,
                &QCheckBox::toggled,
                this,
                [this, axes_idx, series_idx, undo_mgr, redraw](bool checked)
                {
                    Figure* fig = registry_ ? registry_->get(active_id_) : nullptr;
                    if (!fig)
                        return;
                    Axes* ax = fig->get_axes(axes_idx);
                    if (!ax || series_idx >= ax->series().size())
                        return;
                    Series* s = ax->series()[series_idx].get();
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
                Axes* ax = fig->get_axes(axes_idx_local);
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
    series_controls_.clear();
    figure_title_edit_ = nullptr;
}

}   // namespace spectra::adapters::qt
