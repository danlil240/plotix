#pragma once

// QtAccessibilityWidget — dockable accessibility panel for the Qt frontend.
//
// Provides:
//   - Sonification controls (duration, frequency range, amplitude) with
//     WAV export via the framework-neutral sonify_axes_to_wav().
//   - HTML data-table export via figure_to_html_table_file() for screen
//     reader consumption.
//   - Axes selector for choosing which axes to sonify.
//
// Wraps existing framework-neutral accessibility infrastructure — no
// duplicated business logic.

#include <QDockWidget>

#include <spectra/fwd.hpp>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QGroupBox;

namespace spectra
{
class DialogService;
}

namespace spectra::adapters::qt
{

class QtAccessibilityWidget : public QDockWidget
{
    Q_OBJECT

   public:
    explicit QtAccessibilityWidget(FigureRegistry* registry,
                                   DialogService*  dialogs,
                                   QWidget*        parent = nullptr);
    ~QtAccessibilityWidget() override = default;

    QtAccessibilityWidget(const QtAccessibilityWidget&)            = delete;
    QtAccessibilityWidget& operator=(const QtAccessibilityWidget&) = delete;

   public slots:
    void set_active_figure(FigureId id);
    void refresh_axes_list();

   private slots:
    void on_sonify_clicked();
    void on_export_html_clicked();

   private:
    void build_ui();

    FigureRegistry* registry_  = nullptr;
    DialogService*  dialogs_   = nullptr;
    FigureId        active_id_ = INVALID_FIGURE_ID;

    // Sonification controls
    QComboBox*      axes_combo_     = nullptr;
    QDoubleSpinBox* duration_spin_  = nullptr;
    QDoubleSpinBox* freq_lo_spin_   = nullptr;
    QDoubleSpinBox* freq_hi_spin_   = nullptr;
    QDoubleSpinBox* amplitude_spin_ = nullptr;
    QPushButton*    sonify_btn_     = nullptr;
    QLabel*         sonify_status_  = nullptr;

    // HTML table export
    QPushButton* html_btn_    = nullptr;
    QLabel*      html_status_ = nullptr;
};

}   // namespace spectra::adapters::qt
