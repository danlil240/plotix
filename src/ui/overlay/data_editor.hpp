#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <functional>
    #include <spectra/fwd.hpp>

struct ImFont;

namespace spectra::ui
{

class DataEditor
{
   public:
    using PointSelectedCallback = std::function<void(const Series*, size_t)>;

    DataEditor() = default;

    // Draw the data editor content within the inspector panel.
    // Call between ImGui::Begin/End of the inspector window.
    void draw(Figure& figure);

    // Set fonts (called once after font loading)
    void set_fonts(ImFont* body, ImFont* heading, ImFont* title);

    // Selected axes index (-1 = auto-select first)
    int  selected_axes() const { return selected_axes_; }
    void select_axes(int idx) { selected_axes_ = idx; }

    // Selected series index within selected axes (-1 = show all)
    int  selected_series() const { return selected_series_; }
    void select_series(int idx) { selected_series_ = idx; }

    // Highlight synchronization with canvas point selection
    void set_highlighted_point(const Series* series, size_t point_index);

    // Callback fired when user selects/clicks a row in the table
    void set_on_point_selected(PointSelectedCallback cb) { on_point_selected_ = std::move(cb); }

   private:
    void draw_axes_selector(Figure& figure);
    void draw_series_selector(AxesBase& axes);
    void draw_data_table_2d(Series& series, int series_idx);
    void draw_data_table_3d(Series& series, int series_idx);

    int selected_axes_   = -1;
    int selected_series_ = -1;

    // Row highlight state synchronized with canvas point selection.
    const Series*         highlighted_series_      = nullptr;
    size_t                highlighted_point_index_ = 0;
    PointSelectedCallback on_point_selected_;

    // Edit state
    bool editing_      = false;
    int  edit_row_     = -1;
    int  edit_col_     = -1;
    int  edit_series_  = -1;
    char edit_buf_[64] = {};

    // Scroll tracking for large datasets

    // Fonts
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;
    ImFont* font_title_   = nullptr;
};

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI
