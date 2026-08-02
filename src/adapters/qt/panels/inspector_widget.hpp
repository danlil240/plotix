#pragma once

// QtInspectorWidget — dockable inspector panel for the Qt frontend.
//
// Displays properties of the active figure: figure title, per-axes
// title/labels/limits/grid.  Updates when the active figure changes.
// Uses the same Axes/Figure API as the ImGui inspector — no duplicated
// business logic.

#include <QWidget>

#include <spectra/fwd.hpp>

#include <vector>

namespace spectra
{
class AxesBase;
class Axes3D;
class FigureRegistry;
class ApplicationServices;
}   // namespace spectra

class QTabWidget;
class QLineEdit;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QGroupBox;
class QVBoxLayout;
class QPushButton;
class QComboBox;
class QSpinBox;

namespace spectra::adapters::qt
{

class QtInspectorWidget : public QWidget
{
    Q_OBJECT

   public:
    QtInspectorWidget(FigureRegistry*      registry,
                      ApplicationServices* services = nullptr,
                      QWidget*             parent   = nullptr);
    ~QtInspectorWidget() override = default;

    QtInspectorWidget(const QtInspectorWidget&)            = delete;
    QtInspectorWidget& operator=(const QtInspectorWidget&) = delete;

   public slots:
    // Update the inspector to show properties for the given figure.
    void set_active_figure(spectra::FigureId id);
    void refresh();
    // Synchronize the existing controls from the authoritative live model.
    // This is intentionally cheap when topology has not changed so a rendered
    // frame can update dimensions without rebuilding the editor under the user.
    void sync_from_model();

   signals:
    void figure_title_changed(spectra::FigureId id, const QString& title);

   private:
    void                            build_axes_tab(spectra::Axes& ax, int index);
    void                            build_axes3d_tab(spectra::Axes3D& ax, int index);
    void                            clear_axes_tabs();
    std::vector<spectra::AxesBase*> active_axes() const;

    FigureRegistry*      registry_  = nullptr;
    ApplicationServices* services_  = nullptr;
    FigureId             active_id_ = INVALID_FIGURE_ID;

    QTabWidget* tab_widget_ = nullptr;

    // Figure tab controls
    QLineEdit* figure_title_edit_ = nullptr;
    QLabel*    figure_size_label_ = nullptr;
    QLabel*    axes_count_label_  = nullptr;
    QCheckBox* legend_check_      = nullptr;

    // Per-axes tab controls (rebuilt on figure change)
    struct AxesControls
    {
        QLineEdit*         title_edit        = nullptr;
        QLineEdit*         xlabel_edit       = nullptr;
        QLineEdit*         ylabel_edit       = nullptr;
        QDoubleSpinBox*    xmin_spin         = nullptr;
        QDoubleSpinBox*    xmax_spin         = nullptr;
        QDoubleSpinBox*    ymin_spin         = nullptr;
        QDoubleSpinBox*    ymax_spin         = nullptr;
        QLineEdit*         zlabel_edit       = nullptr;
        QDoubleSpinBox*    zmin_spin         = nullptr;
        QDoubleSpinBox*    zmax_spin         = nullptr;
        QCheckBox*         grid_check        = nullptr;
        QCheckBox*         border_check      = nullptr;
        QComboBox*         grid_planes_combo = nullptr;
        spectra::AxesBase* model             = nullptr;
    };
    std::vector<AxesControls> axes_controls_;
    std::vector<size_t>       axes_series_counts_;

    // Per-series controls (rebuilt on figure change)
    struct SeriesControls
    {
        QLineEdit*      label_edit         = nullptr;
        QPushButton*    color_btn          = nullptr;
        QDoubleSpinBox* width_spin         = nullptr;
        QDoubleSpinBox* opacity_spin       = nullptr;
        QCheckBox*      visible_check      = nullptr;
        QComboBox*      line_style_combo   = nullptr;
        QComboBox*      marker_style_combo = nullptr;
    };
    std::vector<SeriesControls> series_controls_;

    void build_series_section(spectra::AxesBase& ax,
                              int                axes_idx,
                              QVBoxLayout*       layout,
                              QWidget*           parent);
};

}   // namespace spectra::adapters::qt
