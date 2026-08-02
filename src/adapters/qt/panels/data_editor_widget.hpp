#pragma once

#include <spectra/fwd.hpp>

#include <QDockWidget>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

class QComboBox;
class QTableWidget;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QGroupBox;

namespace spectra
{
class RedrawRequest;
class UndoManager;
class ClipboardService;
class DialogService;
struct EditableSeriesData;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtDataEditorWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtDataEditorWidget(FigureRegistry*              registry,
                                ::spectra::UndoManager*      undo_manager = nullptr,
                                ::spectra::RedrawRequest*    redraw       = nullptr,
                                ::spectra::ClipboardService* clipboard    = nullptr,
                                ::spectra::DialogService*    dialog       = nullptr,
                                QWidget*                     parent       = nullptr);
    ~QtDataEditorWidget() override = default;

    void set_active_figure(FigureId id);

   public slots:
    void refresh();

   signals:
    void data_changed();

   private slots:
    void on_axes_selected(int index);
    void on_series_selected(int index);
    void on_cell_changed(int row, int col);
    void on_add_row();
    void on_delete_rows();
    void on_move_rows_up();
    void on_move_rows_down();
    void on_paste_cells();
    void on_previous_page();
    void on_next_page();
    void on_import_csv();
    void on_import_x_column_changed(int index);
    void on_import_column_changed(QListWidgetItem* item);
    void on_apply_import_columns();
    void on_cancel_import_columns();
    void on_export_csv();

   private:
    void build_ui();
    void populate_axes_combo(Figure& figure);
    void populate_series_combo(AxesBase& axes);
    void populate_data_table(Series& series);
    void             show_empty_state(const QString& message, bool can_import);
    void             show_data_state();
    Series*          current_series(size_t& axes_index, size_t& series_index) const;
    bool             commit_data(EditableSeriesData data, const std::string& description);
    void             prepare_column_import(std::vector<std::string>        headers,
                                           std::vector<std::vector<float>> columns,
                                           std::vector<double>             column_offsets,
                                           size_t                          rows);
    bool             commit_imported_series(const std::vector<size_t>& y_columns);
    bool             current_axes_is_3d() const;
    std::vector<int> selected_rows() const;

    FigureRegistry*           registry_     = nullptr;
    ::spectra::UndoManager*   undo_manager_ = nullptr;
    ::spectra::RedrawRequest* redraw_       = nullptr;
    ::spectra::ClipboardService* clipboard_    = nullptr;
    ::spectra::DialogService*    dialog_       = nullptr;
    FigureId                  active_id_    = INVALID_FIGURE_ID;

    QComboBox*    axes_combo_   = nullptr;
    QComboBox*    series_combo_ = nullptr;
    QTableWidget* table_        = nullptr;
    QGroupBox*    selection_group_      = nullptr;
    QGroupBox*    table_group_          = nullptr;
    QWidget*      empty_state_          = nullptr;
    QLabel*       empty_state_label_    = nullptr;
    QPushButton*  empty_import_button_  = nullptr;
    QLabel*       info_label_   = nullptr;
    QGroupBox*    import_mapping_group_ = nullptr;
    QComboBox*    import_x_column_      = nullptr;
    QLabel*       import_z_label_       = nullptr;
    QComboBox*    import_z_column_      = nullptr;
    QListWidget*  import_y_columns_     = nullptr;
    QPushButton*  apply_import_columns_ = nullptr;
    QPushButton*  previous_page_button_ = nullptr;
    QPushButton*  next_page_button_     = nullptr;
    QLabel*       page_label_           = nullptr;

    std::vector<std::string>        import_headers_;
    std::vector<std::vector<float>> import_columns_;
    std::vector<double>             import_column_offsets_;
    size_t                          import_rows_  = 0;
    Series*                         paged_series_ = nullptr;
    size_t                          page_start_   = 0;
    static constexpr size_t         page_size_    = 1000;

    bool suppress_cell_signal_ = false;
};

}   // namespace spectra::adapters::qt
