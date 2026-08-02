// commands_plot.cpp — Reference lines and function plot commands.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/overlay/plot_overlay_dialog.hpp"
#include "ui/plot/plot_annotations.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/input/input.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_plot_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx        = ctx.ui_ctx;
    auto*& active_figure = *ctx.active_figure;
    auto&  imgui_ui      = ui_ctx.imgui_ui;
    auto&  input_handler = ui_ctx.input_handler;

    cmds.push_back({"plot.hline_zero",
                    "Add Y = 0 Line",
                    "",
                    "Plot",
                    0,
                    [&input_handler, &active_figure]()
                    {
                        Axes* ax = input_handler.active_axes();
                        if (!ax && active_figure && !active_figure->axes().empty())
                            ax = active_figure->axes_mut()[0].get();
                        if (ax)
                        {
                            ui::add_horizontal_reference_line(*ax, 0.0f, "y = 0");
                        }
                    }});

    cmds.push_back({"plot.vline_zero",
                    "Add X = 0 Line",
                    "",
                    "Plot",
                    0,
                    [&input_handler, &active_figure]()
                    {
                        Axes* ax = input_handler.active_axes();
                        if (!ax && active_figure && !active_figure->axes().empty())
                            ax = active_figure->axes_mut()[0].get();
                        if (ax)
                        {
                            ui::add_vertical_reference_line(*ax, 0.0f, "x = 0");
                        }
                    }});

    cmds.push_back({"plot.hline",
                    "Add Horizontal Line...",
                    "",
                    "Plot",
                    0,
                    [&imgui_ui, &input_handler, &active_figure]()
                    {
                        if (!imgui_ui || !imgui_ui->is_initialized())
                            return;
                        Axes* ax = input_handler.active_axes();
                        if (!ax && active_figure && !active_figure->axes().empty())
                            ax = active_figure->axes_mut()[0].get();
                        if (ax)
                            imgui_ui->plot_overlay_dialog().open(
                                ax,
                                ui::PlotOverlayDialog::Mode::HorizontalLine);
                    }});

    cmds.push_back({"plot.vline",
                    "Add Vertical Line...",
                    "",
                    "Plot",
                    0,
                    [&imgui_ui, &input_handler, &active_figure]()
                    {
                        if (!imgui_ui || !imgui_ui->is_initialized())
                            return;
                        Axes* ax = input_handler.active_axes();
                        if (!ax && active_figure && !active_figure->axes().empty())
                            ax = active_figure->axes_mut()[0].get();
                        if (ax)
                            imgui_ui->plot_overlay_dialog().open(
                                ax,
                                ui::PlotOverlayDialog::Mode::VerticalLine);
                    }});

    cmds.push_back(
        {"plot.function",
         "Plot Function...",
         "",
         "Plot",
         0,
         [&imgui_ui, &input_handler, &active_figure]()
         {
             if (!imgui_ui || !imgui_ui->is_initialized())
                 return;
             Axes* ax = input_handler.active_axes();
             if (!ax && active_figure && !active_figure->axes().empty())
                 ax = active_figure->axes_mut()[0].get();
             if (ax)
                 imgui_ui->plot_overlay_dialog().open(ax, ui::PlotOverlayDialog::Mode::Function);
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
