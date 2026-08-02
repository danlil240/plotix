// commands_figure.cpp — Figure management command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#include <string>

namespace spectra
{

std::vector<CommandDescriptor> make_figure_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& fig_mgr = *ctx.ui_ctx.fig_mgr;
    auto* session = ctx.session;

    cmds.push_back({"figure.new",
                    "New Figure",
                    "Ctrl+T",
                    "Figure",
                    static_cast<uint16_t>(ui::Icon::Plus),
                    [&]() { fig_mgr.queue_create(); }});

    cmds.push_back({"figure.close",
                    "Close Figure",
                    "Ctrl+W",
                    "Figure",
                    static_cast<uint16_t>(ui::Icon::Close),
                    [&fig_mgr, session]()
                    {
                        if (fig_mgr.count() > 1)
                        {
                            fig_mgr.queue_close(fig_mgr.active_index());
                        }
                        else if (session)
                        {
                            session->request_exit();
                        }
                    }});

    // Tab switching (1-9)
    for (int i = 0; i < 9; ++i)
    {
        cmds.push_back({"figure.tab_" + std::to_string(i + 1),
                        "Switch to Figure " + std::to_string(i + 1),
                        std::to_string(i + 1),
                        "Figure",
                        0,
                        [&fig_mgr, i]()
                        {
                            const auto& ids = fig_mgr.figure_ids();
                            if (static_cast<size_t>(i) < ids.size())
                                fig_mgr.queue_switch(ids[static_cast<size_t>(i)]);
                        }});
    }

    cmds.push_back({"figure.next_tab", "Next Figure Tab", "Ctrl+Tab", "Figure", 0, [&fig_mgr]() {
                        fig_mgr.switch_to_next();
                    }});

    cmds.push_back(
        {"figure.prev_tab", "Previous Figure Tab", "Ctrl+Shift+Tab", "Figure", 0, [&fig_mgr]() {
             fig_mgr.switch_to_previous();
         }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
