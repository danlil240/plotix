#pragma once

#include <spectra/fwd.hpp>
#include <string>
#include <string_view>

namespace spectra
{
struct ExprNode;
}

namespace spectra::ui
{

// Add a dashed horizontal reference line spanning the axes.
LineSeries& add_horizontal_reference_line(Axes& ax, float y, std::string_view label = {});

// Add a dashed vertical reference line spanning the axes.
LineSeries& add_vertical_reference_line(Axes& ax, float x, std::string_view label = {});

// Sample y = f(x) from a parsed expression over [xmin, xmax].
LineSeries& add_function_plot(Axes&            ax,
                              const ExprNode&  ast,
                              float            xmin,
                              float            xmax,
                              int              n     = 200,
                              std::string_view label = {},
                              std::string_view fmt   = "g-");

}   // namespace spectra::ui
