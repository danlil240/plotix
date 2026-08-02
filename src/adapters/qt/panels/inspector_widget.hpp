#pragma once

// QtInspectorWidget — dockable inspector panel for the Qt frontend.
//
// Displays properties of the active figure using the same four top-level
// sections as the legacy ImGui inspector: Figure, Series, Axes, Data.
// Each section uses collapsible property groups and lives model edits.

#include <QWidget>

#include <spectra/fwd.hpp>
#include "ui/input/selection_context.hpp"
#include "../components/spectra_inspector_widgets.hpp"

#include <memory>
#include <vector>

#include <QMetaObject>

namespace spectra
{
class AxesBase;
class FigureRegistry;
class ApplicationServices;
}   // namespace spectra

namespace spectra::ui
{
struct SelectionContext;
}   // namespace spectra::ui

class QTabWidget;
class QLineEdit;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QComboBox;
class QSpinBox;
class QTabBar;
class QStackedWidget;
class QVBoxLayout;
class QScrollArea;
class QToolButton;
class QListWidget;
class QTableWidget;

namespace spectra::adapters::qt
{

class SparklineWidget;

class QtDataEditorWidget;

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

   private:
    enum class Section
    {
        Figure,
        Series,
        Axes,
        Data
    };

    void build_ui();
    void build_figure_page();
    void build_series_page();
    void build_axes_page();
    void build_data_page();
    void clear_pages();
    void set_section(Section s);

    std::vector<spectra::AxesBase*> active_axes() const;

    FigureRegistry*      registry_  = nullptr;
    ApplicationServices* services_  = nullptr;
    FigureId             active_id_ = INVALID_FIGURE_ID;
    Section              section_   = Section::Figure;

    std::unique_ptr<ui::SelectionContext> ctx_;

    // Top-level section navigation.
    SpectraSegmentedControl* section_selector_ = nullptr;
    QStackedWidget*          section_stack_    = nullptr;

    // Per-section scroll pages.
    QScrollArea* figure_scroll_ = nullptr;
    QScrollArea* series_scroll_ = nullptr;
    QScrollArea* axes_scroll_   = nullptr;
    QScrollArea* data_scroll_   = nullptr;

    // Page content layouts.
    QVBoxLayout* figure_layout_ = nullptr;
    QVBoxLayout* series_layout_ = nullptr;
    QVBoxLayout* axes_layout_   = nullptr;
    QVBoxLayout* data_layout_   = nullptr;

    // Figure controls
    SpectraPanelTitle* figure_title_            = nullptr;
    SpectraPanelTitle* series_title_            = nullptr;
    QLabel*            figure_size_label_       = nullptr;   // test-only; hidden from UI
    QLabel*            figure_axes_count_label_ = nullptr;   // test-only; hidden from UI

    SpectraColorField* figure_bg_color_field_ = nullptr;
    QDoubleSpinBox*    margin_top_spin_       = nullptr;
    QDoubleSpinBox*    margin_bottom_spin_    = nullptr;
    QDoubleSpinBox*    margin_left_spin_      = nullptr;
    QDoubleSpinBox*    margin_right_spin_     = nullptr;
    QDoubleSpinBox*    margin_hgap_spin_      = nullptr;
    QDoubleSpinBox*    margin_vgap_spin_      = nullptr;
    QDoubleSpinBox*    margin_min_h_spin_     = nullptr;

    QCheckBox*         legend_visible_check_      = nullptr;
    QComboBox*         legend_position_combo_     = nullptr;
    QDoubleSpinBox*    legend_font_size_spin_     = nullptr;
    QDoubleSpinBox*    legend_padding_spin_       = nullptr;
    SpectraColorField* legend_bg_color_field_     = nullptr;
    SpectraColorField* legend_border_color_field_ = nullptr;
    QPushButton*       reset_figure_style_btn_    = nullptr;

    // Axes controls
    QTabWidget*     axes_tab_widget_ = nullptr;
    QComboBox*      axes_selector_   = nullptr;
    QStackedWidget* axes_stack_      = nullptr;

    struct AxesControls
    {
        QLineEdit*      title_edit         = nullptr;
        QLineEdit*      xlabel_edit        = nullptr;
        QLineEdit*      ylabel_edit        = nullptr;
        QLineEdit*      zlabel_edit        = nullptr;
        QDoubleSpinBox* xmin_spin          = nullptr;
        QDoubleSpinBox* xmax_spin          = nullptr;
        QDoubleSpinBox* ymin_spin          = nullptr;
        QDoubleSpinBox* ymax_spin          = nullptr;
        QDoubleSpinBox* zmin_spin          = nullptr;
        QDoubleSpinBox* zmax_spin          = nullptr;
        QCheckBox*      grid_check         = nullptr;
        QCheckBox*      border_check       = nullptr;
        QPushButton*    grid_color_btn     = nullptr;
        QDoubleSpinBox* grid_width_spin_   = nullptr;
        QDoubleSpinBox* tick_length_spin_  = nullptr;
        QComboBox*      autoscale_combo_   = nullptr;
        QPushButton*    auto_fit_btn_      = nullptr;
        QComboBox*      grid_planes_combo  = nullptr;
        QCheckBox*      bounding_box_check = nullptr;

        // Aggregate statistics labels (2D axes only)
        QLabel* stats_visible_label      = nullptr;
        QLabel* stats_total_points_label = nullptr;
        QLabel* stats_x_min_label        = nullptr;
        QLabel* stats_x_max_label        = nullptr;
        QLabel* stats_x_mean_label       = nullptr;
        QLabel* stats_y_min_label        = nullptr;
        QLabel* stats_y_max_label        = nullptr;
        QLabel* stats_y_mean_label       = nullptr;

        spectra::AxesBase* model = nullptr;
    };
    std::vector<AxesControls> axes_controls_;
    std::vector<size_t>       axes_series_counts_;

    // Series controls
    SpectraSeriesListView*  series_list_ = nullptr;
    QMetaObject::Connection series_list_conn_;
    QWidget*                series_props_        = nullptr;
    QVBoxLayout*            series_props_layout_ = nullptr;

    struct SeriesControls
    {
        spectra::Series* series = nullptr;

        QLineEdit*      label_edit         = nullptr;
        QPushButton*    color_btn          = nullptr;
        QCheckBox*      visible_check      = nullptr;
        QDoubleSpinBox* width_spin         = nullptr;
        QDoubleSpinBox* opacity_spin       = nullptr;
        QDoubleSpinBox* marker_size_spin_  = nullptr;
        QComboBox*      line_style_combo   = nullptr;
        QComboBox*      marker_style_combo = nullptr;

        // Data statistics labels
        QLabel* stats_x_count_label = nullptr;
        QLabel* stats_x_min_label   = nullptr;
        QLabel* stats_x_max_label   = nullptr;
        QLabel* stats_x_mean_label  = nullptr;
        QLabel* stats_x_sum_label   = nullptr;
        QLabel* stats_y_count_label = nullptr;
        QLabel* stats_y_min_label   = nullptr;
        QLabel* stats_y_max_label   = nullptr;
        QLabel* stats_y_mean_label  = nullptr;
        QLabel* stats_y_sum_label   = nullptr;

        SparklineWidget* sparkline = nullptr;
    };
    SeriesControls series_controls_;

    void build_axes_tab(spectra::Axes& ax, int index);
    void build_axes3d_tab(spectra::Axes3D& ax, int index);
    void build_series_list();
    void build_series_properties(spectra::Series& s);
    void clear_series_properties();
    void update_series_preview();
    void update_axes_statistics(spectra::AxesBase& ax, AxesControls& ctrl);
    void wire_figure_page();

    QtDataEditorWidget* data_editor_ = nullptr;
};

}   // namespace spectra::adapters::qt
