#pragma once

// QtExportWidget — dockable export panel for the Qt frontend.
//
// Provides format selection from ExportFormatRegistry, resolution controls,
// output path selection via native file dialog, and an export button.
// Also supports PNG export via Figure::save_png() for the built-in path.
// Uses the same ExportFormatRegistry and Figure APIs as the ImGui frontend.

#include <QDockWidget>

#include <spectra/fwd.hpp>

#include <functional>
#include <string>

class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QLineEdit;

namespace spectra
{
class ExportFormatRegistry;
class FigureRegistry;
class DialogService;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtExportWidget : public QDockWidget
{
    Q_OBJECT

   public:
    using ExportPathCallback = std::function<std::optional<std::string>(
        const std::string& title, const std::string& default_name,
        const std::string& filter)>;

    QtExportWidget(ExportFormatRegistry* formats,
                   FigureRegistry*       registry,
                   DialogService*        dialog_service,
                   QWidget*              parent = nullptr);
    ~QtExportWidget() override = default;

    QtExportWidget(const QtExportWidget&)            = delete;
    QtExportWidget& operator=(const QtExportWidget&) = delete;

   public slots:
    void set_active_figure(spectra::FigureId id);
    void refresh_formats();

   private slots:
    void on_browse_clicked();
    void on_export_clicked();

   private:
    ExportFormatRegistry* formats_   = nullptr;
    FigureRegistry*       registry_  = nullptr;
    DialogService*        dialogs_   = nullptr;

    FigureId active_id_ = INVALID_FIGURE_ID;

    QComboBox*  format_combo_    = nullptr;
    QSpinBox*   width_spin_      = nullptr;
    QSpinBox*   height_spin_     = nullptr;
    QLineEdit*  path_edit_       = nullptr;
    QPushButton* browse_btn_     = nullptr;
    QPushButton* export_btn_     = nullptr;
    QLabel*     status_label_    = nullptr;
};

}   // namespace spectra::adapters::qt
