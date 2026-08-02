#include "function_plot_dialog.hpp"

#include "app/frontend_services.hpp"
#include "math/expression_eval.hpp"
#include "ui/plot/plot_annotations.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

namespace spectra::adapters::qt
{

QtFunctionPlotDialog::QtFunctionPlotDialog(FigureRegistry* registry,
                                           RedrawRequest*  redraw,
                                           QWidget*        parent)
    : QDialog(parent), registry_(registry), redraw_(redraw)
{
    setObjectName("function_plot_dialog");
    setWindowTitle("Plot Function");
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    auto* form   = new QFormLayout();

    formula_edit_ = new QLineEdit("x^2", this);
    formula_edit_->setObjectName("function_formula");
    formula_edit_->setPlaceholderText("e.g. x^2, sin(x), 2*x+1");
    form->addRow("f(x)", formula_edit_);

    constexpr double kLimit = static_cast<double>(std::numeric_limits<float>::max());
    xmin_spin_              = new QDoubleSpinBox(this);
    xmin_spin_->setObjectName("function_x_min");
    xmin_spin_->setRange(-kLimit, kLimit);
    xmin_spin_->setDecimals(6);
    form->addRow("X min", xmin_spin_);

    xmax_spin_ = new QDoubleSpinBox(this);
    xmax_spin_->setObjectName("function_x_max");
    xmax_spin_->setRange(-kLimit, kLimit);
    xmax_spin_->setDecimals(6);
    form->addRow("X max", xmax_spin_);

    samples_spin_ = new QSpinBox(this);
    samples_spin_->setObjectName("function_samples");
    samples_spin_->setRange(2, 1000000);
    samples_spin_->setValue(200);
    form->addRow("Samples", samples_spin_);
    layout->addLayout(form);

    error_label_ = new QLabel(this);
    error_label_->setObjectName("function_validation_error");
    error_label_->setWordWrap(true);
    layout->addWidget(error_label_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    add_button_   = buttons->addButton("Add", QDialogButtonBox::AcceptRole);
    add_button_->setObjectName("function_add");
    layout->addWidget(buttons);

    connect(formula_edit_,
            &QLineEdit::textChanged,
            this,
            &QtFunctionPlotDialog::validate_expression);
    connect(add_button_, &QPushButton::clicked, this, &QtFunctionPlotDialog::add_function);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validate_expression();
}

void QtFunctionPlotDialog::open_for_figure(FigureId id)
{
    Figure* figure = registry_ && id != INVALID_FIGURE_ID ? registry_->get(id) : nullptr;
    if (!figure || figure->axes().empty() || !figure->axes()[0])
        return;

    figure_id_        = id;
    const auto limits = figure->axes()[0]->x_limits();
    xmin_spin_->setValue(limits.min);
    xmax_spin_->setValue(limits.max);
    formula_edit_->setText("x^2");
    samples_spin_->setValue(200);
    validate_expression();
    open();
    raise();
    activateWindow();
    formula_edit_->setFocus();
    formula_edit_->selectAll();
}

void QtFunctionPlotDialog::validate_expression()
{
    const auto parsed = parse_expression(formula_edit_->text().toStdString());
    const bool valid  = parsed.ast != nullptr;
    add_button_->setEnabled(valid);
    error_label_->setText(valid ? QString{} : QString::fromStdString(parsed.error));
}

void QtFunctionPlotDialog::add_function()
{
    Figure* figure =
        registry_ && figure_id_ != INVALID_FIGURE_ID ? registry_->get(figure_id_) : nullptr;
    if (!figure || figure->axes().empty() || !figure->axes()[0])
        return;

    const std::string formula = formula_edit_->text().toStdString();
    const auto        parsed  = parse_expression(formula);
    if (!parsed.ast)
    {
        validate_expression();
        return;
    }

    ui::add_function_plot(*figure->axes_mut()[0],
                          *parsed.ast,
                          static_cast<float>(xmin_spin_->value()),
                          static_cast<float>(xmax_spin_->value()),
                          samples_spin_->value(),
                          formula);
    if (redraw_)
        redraw_->request_redraw(figure_id_);
    emit function_added(figure_id_);
    accept();
}

}   // namespace spectra::adapters::qt
