// commands_tools.cpp — Tool mode and utility command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include "ui/input/input.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_tools_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& input_handler = ctx.ui_ctx.input_handler;

    cmds.push_back(
        {"tool.pan", "Pan Tool", "", "Tools", static_cast<uint16_t>(ui::Icon::Hand), [&]() {
             input_handler.set_tool_mode(ToolMode::Pan);
         }});

    cmds.push_back({"tool.box_zoom",
                    "Box Zoom Tool",
                    "",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::ZoomIn),
                    [&]() { input_handler.set_tool_mode(ToolMode::BoxZoom); }});

    cmds.push_back({"tool.select",
                    "Select Tool",
                    "",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Crosshair),
                    [&]() { input_handler.set_tool_mode(ToolMode::Select); }});

    cmds.push_back({"tool.measure",
                    "Measure Tool",
                    "",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Ruler),
                    [&]() { input_handler.set_tool_mode(ToolMode::Measure); }});

    cmds.push_back({"tool.annotate",
                    "Annotate Tool",
                    "",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Comment),
                    [&]() { input_handler.set_tool_mode(ToolMode::Annotate); }});

    cmds.push_back(
        {"tool.roi", "ROI Tool", "", "Tools", static_cast<uint16_t>(ui::Icon::VectorSquare), [&]() {
             input_handler.set_tool_mode(ToolMode::ROI);
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
