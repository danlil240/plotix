#pragma once

#include <spectra/fwd.hpp>

#include <QDockWidget>
#include <QString>

class QComboBox;
class QTableWidget;
class QSpinBox;
class QLabel;

namespace spectra
{
class RedrawRequest;
class UndoManager;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtDataEditorWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtDataEditorWidget(FigureRegistry*           registry,
                                ::spectra::UndoManager*   undo_manager = nullptr,
                                ::spectra::RedrawRequest* redraw       = nullptr,
                                QWidget*                  parent       = nullptr);
    ~QtDataEditorWidget() override = default;

    void set_active_figure(FigureId id);

   public slots:
    void refresh();

   private slots:
    void on_axes_selected(int index);
    void on_series_selected(int index);
    void on_cell_changed(int row, int col);

   private:
    void build_ui();
    void populate_axes_combo(Figure& figure);
    void populate_series_combo(AxesBase& axes);
    void populate_data_table(Series& series);

    FigureRegistry*           registry_     = nullptr;
    ::spectra::UndoManager*   undo_manager_ = nullptr;
    ::spectra::RedrawRequest* redraw_       = nullptr;
    FigureId                  active_id_    = INVALID_FIGURE_ID;

    QComboBox*    axes_combo_   = nullptr;
    QComboBox*    series_combo_ = nullptr;
    QTableWidget* table_        = nullptr;
    QLabel*       info_label_   = nullptr;

    bool suppress_cell_signal_ = false;
};

}   // namespace spectra::adapters::qt
