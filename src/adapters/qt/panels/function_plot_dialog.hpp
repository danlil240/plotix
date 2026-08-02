#pragma once

#include <QDialog>

#include <spectra/fwd.hpp>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace spectra
{
class FigureRegistry;
class RedrawRequest;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtFunctionPlotDialog final : public QDialog
{
    Q_OBJECT

   public:
    explicit QtFunctionPlotDialog(FigureRegistry* registry,
                                  RedrawRequest*  redraw = nullptr,
                                  QWidget*        parent = nullptr);

    void open_for_figure(FigureId id);

   signals:
    void function_added(FigureId id);

   private slots:
    void validate_expression();
    void add_function();

   private:
    FigureRegistry* registry_  = nullptr;
    RedrawRequest*  redraw_    = nullptr;
    FigureId        figure_id_ = INVALID_FIGURE_ID;

    QLineEdit*      formula_edit_ = nullptr;
    QDoubleSpinBox* xmin_spin_    = nullptr;
    QDoubleSpinBox* xmax_spin_    = nullptr;
    QSpinBox*       samples_spin_ = nullptr;
    QLabel*         error_label_  = nullptr;
    QPushButton*    add_button_   = nullptr;
};

}   // namespace spectra::adapters::qt
