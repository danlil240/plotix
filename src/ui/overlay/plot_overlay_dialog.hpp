#pragma once
#ifdef SPECTRA_USE_IMGUI

    #include <spectra/fwd.hpp>
    #include <string>

struct ImFont;

namespace spectra::ui
{

// Modal for adding reference lines or plotting y = f(x) on the active 2D axes.
class PlotOverlayDialog
{
   public:
    enum class Mode
    {
        HorizontalLine,
        VerticalLine,
        Function,
    };

    void open(Axes* axes, Mode mode);
    void close();
    bool is_open() const { return open_; }
    void draw();
    void set_fonts(ImFont* body, ImFont* heading);

   private:
    void apply();
    void validate_function();

    bool  open_ = false;
    Mode  mode_ = Mode::HorizontalLine;
    Axes* axes_ = nullptr;

    float value_        = 0.0f;
    float xmin_         = -1.0f;
    float xmax_         = 1.0f;
    int   samples_      = 200;
    char  formula_[256] = "x^2";

    std::string validation_error_;
    bool        formula_valid_ = false;

    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;
};

}   // namespace spectra::ui

#endif
