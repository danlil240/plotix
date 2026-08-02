// commands_panel.cpp — Panel toggle command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/spectra_app_shell.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/topics/topics_panel.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_panel_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto& ui_ctx   = ctx.ui_ctx;
    auto& imgui_ui = ui_ctx.imgui_ui;
    auto& undo_mgr = ui_ctx.undo_mgr;
    auto* shell    = ui_ctx.app_shell.get();

    auto toggle_shell_panel = [&](const char* id, const char* undo_show, const char* undo_hide)
    {
        if (!shell)
            return;
        const bool was = shell->panel_visible(id);
        shell->toggle_panel(id);
        undo_mgr.push(UndoAction{was ? undo_hide : undo_show,
                                 [shell, id, was]() { shell->set_panel_visible(id, was); },
                                 [shell, id, was]() { shell->set_panel_visible(id, !was); }});
    };

    cmds.push_back({"panel.toggle_timeline",
                    "Toggle Timeline Panel",
                    "T",
                    "Panel",
                    static_cast<uint16_t>(ui::Icon::Play),
                    [&]()
                    {
                        if (shell)
                            toggle_shell_panel("core.timeline", "Show timeline", "Hide timeline");
                        else if (imgui_ui)
                            imgui_ui->set_timeline_visible(!imgui_ui->is_timeline_visible());
                    }});

    cmds.push_back(
        {"panel.toggle_curve_editor",
         "Toggle Curve Editor",
         "",
         "Panel",
         0,
         [&]()
         {
             if (shell)
                 toggle_shell_panel("core.curve_editor", "Show curve editor", "Hide curve editor");
             else if (imgui_ui)
                 imgui_ui->set_curve_editor_visible(!imgui_ui->is_curve_editor_visible());
         }});

    cmds.push_back({"panel.toggle_plugins",
                    "Toggle Plugins Panel",
                    "",
                    "Panel",
                    0,
                    [&]()
                    {
                        if (shell)
                            toggle_shell_panel("core.plugins", "Show plugins", "Hide plugins");
                        else if (imgui_ui)
                            imgui_ui->set_plugins_panel_visible(
                                !imgui_ui->is_plugins_panel_visible());
                    }});

    cmds.push_back({"panel.toggle_topics",
                    "Toggle Topics Panel",
                    "Ctrl+Shift+T",
                    "Panel",
                    0,
                    [&]()
                    {
                        if (shell)
                            toggle_shell_panel("core.topics", "Show topics", "Hide topics");
                        else
                            ui_ctx.topics_panel.set_visible(!ui_ctx.topics_panel.is_visible());
                    }});

    cmds.push_back({"panel.open_settings", "Settings", "", "Panel", 0, [&]() {
                        ui_ctx.settings_panel.open();
                    }});

    cmds.push_back(
        {"panel.toggle_inspector",
         "Toggle Inspector Panel",
         "",
         "Panel",
         0,
         [&]()
         {
             if (shell)
             {
                 toggle_shell_panel("core.inspector", "Show inspector", "Hide inspector");
             }
             else if (imgui_ui)
             {
                 auto& lm      = imgui_ui->get_layout_manager();
                 bool  old_val = lm.is_inspector_visible();
                 lm.set_inspector_visible(!old_val);
                 undo_mgr.push(UndoAction{
                     old_val ? "Hide inspector" : "Show inspector",
                     [&imgui_ui, old_val]()
                     {
                         if (imgui_ui)
                             imgui_ui->get_layout_manager().set_inspector_visible(old_val);
                     },
                     [&imgui_ui, old_val]()
                     {
                         if (imgui_ui)
                             imgui_ui->get_layout_manager().set_inspector_visible(!old_val);
                     }});
             }
         }});

    cmds.push_back(
        {"panel.toggle_nav_rail",
         "Toggle Navigation Rail",
         "",
         "Panel",
         static_cast<uint16_t>(ui::Icon::Menu),
         [&]()
         {
             if (imgui_ui)
             {
                 auto& lm      = imgui_ui->get_layout_manager();
                 bool  old_val = lm.is_nav_rail_expanded();
                 lm.set_nav_rail_expanded(!old_val);
                 undo_mgr.push(UndoAction{
                     old_val ? "Collapse nav rail" : "Expand nav rail",
                     [&imgui_ui, old_val]()
                     {
                         if (imgui_ui)
                             imgui_ui->get_layout_manager().set_nav_rail_expanded(old_val);
                     },
                     [&imgui_ui, old_val]()
                     {
                         if (imgui_ui)
                             imgui_ui->get_layout_manager().set_nav_rail_expanded(!old_val);
                     }});
             }
         }});

    cmds.push_back({"panel.toggle_data_editor",
                    "Toggle Data Editor",
                    "",
                    "Panel",
                    static_cast<uint16_t>(ui::Icon::Edit),
                    [&]()
                    {
                        if (imgui_ui)
                        {
                            auto& lm  = imgui_ui->get_layout_manager();
                            bool  vis = lm.is_inspector_visible();
                            if (vis)
                            {
                                lm.set_inspector_visible(false);
                            }
                            else
                            {
                                lm.set_inspector_visible(true);
                            }
                            undo_mgr.push(UndoAction{
                                vis ? "Hide data editor" : "Show data editor",
                                [&imgui_ui, vis]()
                                {
                                    if (imgui_ui)
                                        imgui_ui->get_layout_manager().set_inspector_visible(vis);
                                },
                                [&imgui_ui, vis]()
                                {
                                    if (imgui_ui)
                                        imgui_ui->get_layout_manager().set_inspector_visible(!vis);
                                }});
                        }
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
