#include "plot_annotations.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>
#include <vector>

#include "math/expression_eval.hpp"

namespace spectra::ui
{

LineSeries& add_horizontal_reference_line(Axes& ax, float y, std::string_view label)
{
    auto& line = ax.hline(static_cast<double>(y), "c--");
    line.color(Color(0.75f, 1.0f, 1.0f, 0.85f));
    line.width(0.7f);
    if (label.empty())
        line.label(std::format("y = {:.4g}", y));
    else
        line.label(std::string(label));
    return line;
}

LineSeries& add_vertical_reference_line(Axes& ax, float x, std::string_view label)
{
    auto& line = ax.vline(static_cast<double>(x), "c--");
    line.color(Color(0.75f, 1.0f, 1.0f, 0.85f));
    line.width(0.7f);
    if (label.empty())
        line.label(std::format("x = {:.4g}", x));
    else
        line.label(std::string(label));
    return line;
}

LineSeries& add_function_plot(Axes&            ax,
                              const ExprNode&  ast,
                              float            xmin,
                              float            xmax,
                              int              n,
                              std::string_view label,
                              std::string_view fmt)
{
    n = std::max(n, 2);
    std::vector<float> xs(static_cast<size_t>(n));
    std::vector<float> ys(static_cast<size_t>(n));

    const double dx =
        (static_cast<double>(xmax) - static_cast<double>(xmin)) / static_cast<double>(n - 1);
    for (int i = 0; i < n; ++i)
    {
        const float x = static_cast<float>(static_cast<double>(xmin) + dx * static_cast<double>(i));
        ExprContext ctx;
        ctx.x                      = x;
        ctx.t                      = x;
        ctx.i                      = static_cast<size_t>(i);
        ctx.n                      = static_cast<size_t>(n);
        xs[static_cast<size_t>(i)] = x;
        ys[static_cast<size_t>(i)] = evaluate(ast, ctx);
    }

    auto& line = ax.plot(xs, ys, fmt);
    if (label.empty())
        line.label("f(x)");
    else
        line.label(std::string(label));
    return line;
}

}   // namespace spectra::ui
