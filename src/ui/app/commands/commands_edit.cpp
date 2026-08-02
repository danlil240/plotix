// commands_edit.cpp — Edit command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_edit_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& undo_mgr = ctx.ui_ctx.undo_mgr;

    cmds.push_back(
        {"edit.undo", "Undo", "Ctrl+Z", "Edit", static_cast<uint16_t>(ui::Icon::Undo), [&]() {
             undo_mgr.undo();
         }});

    cmds.push_back(
        {"edit.redo", "Redo", "Ctrl+Y", "Edit", static_cast<uint16_t>(ui::Icon::Redo), [&]() {
             undo_mgr.redo();
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
