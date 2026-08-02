// commands_file.cpp — File command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/export_dialog.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/workspace/plugin_api.hpp"
    #include "ui/workspace/workspace.hpp"
    #include "ui/workspace/figure_serializer.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_file_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    auto&  ui_ctx           = ctx.ui_ctx;
    auto&  registry         = ctx.registry;
    auto*& active_figure    = *ctx.active_figure;
    auto&  imgui_ui         = ui_ctx.imgui_ui;
    auto&  data_interaction = ui_ctx.data_interaction;
    auto&  dock_system      = ui_ctx.dock_system;
    auto&  undo_mgr         = ui_ctx.undo_mgr;
    auto&  fig_mgr          = *ui_ctx.fig_mgr;
    auto&  tm               = *ui_ctx.theme_mgr;

    cmds.push_back({"file.export_png",
                    "Export PNG",
                    "Ctrl+S",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Export),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        const char* patterns[] = {"*.png"};
                        auto        path       = spectra::ask_export_path("Export PNG",
                                                             "spectra_export.png",
                                                             1,
                                                             patterns,
                                                             "PNG Image",
                                                             ui_ctx.last_export_dir);
                        if (!path)
                            return;
                        active_figure->save_png(*path);
                    }});

    cmds.push_back({"file.export_svg",
                    "Export SVG",
                    "Ctrl+Shift+S",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Export),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        const char* patterns[] = {"*.svg"};
                        auto        path       = spectra::ask_export_path("Export SVG",
                                                             "spectra_export.svg",
                                                             1,
                                                             patterns,
                                                             "SVG Image",
                                                             ui_ctx.last_export_dir);
                        if (!path)
                            return;
                        active_figure->save_svg(*path);
                    }});

    cmds.push_back({"file.copy_to_clipboard",
                    "Copy Figure to Clipboard",
                    "Ctrl+Shift+C",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Copy),
                    [&]()
                    {
                        if (!active_figure || !registry.get(registry.find_id(active_figure)))
                            return;
                        active_figure->copy_to_clipboard();
                    }});

    cmds.push_back({"file.save_workspace",
                    "Save Workspace",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Save),
                    [&]()
                    {
                        WorkspaceData::PanelState panels;
                        if (imgui_ui)
                        {
                            auto& lm                 = imgui_ui->get_layout_manager();
                            panels.inspector_visible = lm.is_inspector_visible();
                            panels.inspector_width   = lm.inspector_width();
                            panels.nav_rail_expanded = lm.is_nav_rail_expanded();
                        }

                        std::vector<Figure*> figs;
                        for (auto id : fig_mgr.figure_ids())
                        {
                            Figure* f = registry.get(id);
                            if (f)
                                figs.push_back(f);
                        }
                        auto data = Workspace::capture(figs,
                                                       fig_mgr.active_index(),
                                                       tm.current_theme_name(),
                                                       panels.inspector_visible,
                                                       panels.inspector_width,
                                                       panels.nav_rail_expanded);
                        if (data_interaction)
                        {
                            data.interaction.crosshair_enabled =
                                data_interaction->crosshair_active();
                            data.interaction.tooltip_enabled = data_interaction->tooltip_active();
                            for (const auto& m : data_interaction->markers())
                            {
                                OverlaySnapshot::MarkerEntry me;
                                me.data_x       = m.data_x;
                                me.data_y       = m.data_y;
                                me.series_label = m.series_label;
                                me.point_index  = m.point_index;
                                data.interaction.markers.push_back(std::move(me));
                            }

                            for (const auto& ann : data_interaction->annotations().annotations())
                            {
                                if (ann.editing)
                                    continue;
                                if (ann.text.empty())
                                    continue;
                                OverlaySnapshot::AnnotationEntry ae;
                                ae.data_x     = ann.data_x;
                                ae.data_y     = ann.data_y;
                                ae.text       = ann.text;
                                ae.color      = ann.color;
                                ae.offset_x   = ann.offset_x;
                                ae.offset_y   = ann.offset_y;
                                ae.axes_index = 0;
                                if (ann.axes && !figs.empty())
                                {
                                    for (auto* fig : figs)
                                    {
                                        if (!fig)
                                            continue;
                                        size_t ai = 0;
                                        for (const auto& ax : fig->axes())
                                        {
                                            if (ax.get() == ann.axes)
                                            {
                                                ae.axes_index = ai;
                                                break;
                                            }
                                            ++ai;
                                        }
                                    }
                                }
                                data.interaction.annotations.push_back(std::move(ae));
                            }
                        }
                        for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                        {
                            data.figures[i].custom_tab_title = fig_mgr.get_title(i);
                            data.figures[i].is_modified      = fig_mgr.is_modified(i);
                        }
                        data.undo_count      = undo_mgr.undo_count();
                        data.redo_count      = undo_mgr.redo_count();
                        data.dock_state      = dock_system.serialize();
                        data.last_export_dir = ui_ctx.last_export_dir;
                        if (ui_ctx.plugin_manager)
                            data.plugin_state = ui_ctx.plugin_manager->serialize_state();
                        Workspace::save(Workspace::default_path(), data);
                    }});

    cmds.push_back(
        {"file.load_workspace",
         "Load Workspace",
         "",
         "File",
         static_cast<uint16_t>(ui::Icon::FolderOpen),
         [&]()
         {
             WorkspaceData data;
             if (Workspace::load(Workspace::default_path(), data))
             {
                 if (!active_figure)
                     return;
                 auto                 before_snap = capture_figure_axes(*active_figure);
                 std::vector<Figure*> figs;
                 for (auto id : fig_mgr.figure_ids())
                 {
                     Figure* f = registry.get(id);
                     if (f)
                         figs.push_back(f);
                 }
                 Workspace::apply(data, figs);
                 auto after_snap = capture_figure_axes(*active_figure);
                 undo_mgr.push(UndoAction{"Load workspace",
                                          [before_snap]() { restore_figure_axes(before_snap); },
                                          [after_snap]() { restore_figure_axes(after_snap); }});
                 if (data_interaction)
                 {
                     data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                     data_interaction->set_tooltip(data.interaction.tooltip_enabled);

                     data_interaction->annotations().clear();
                     for (const auto& ae : data.interaction.annotations)
                     {
                         if (ae.text.empty())
                             continue;
                         const Axes* axes_ptr = nullptr;
                         for (auto* fig : figs)
                         {
                             if (!fig)
                                 continue;
                             if (ae.axes_index < fig->axes().size() && fig->axes()[ae.axes_index])
                             {
                                 axes_ptr = fig->axes()[ae.axes_index].get();
                                 break;
                             }
                         }
                         size_t idx =
                             data_interaction->annotations().add(ae.data_x, ae.data_y, axes_ptr);
                         auto& ann    = data_interaction->annotations().annotations_mut()[idx];
                         ann.text     = ae.text;
                         ann.color    = ae.color;
                         ann.offset_x = ae.offset_x;
                         ann.offset_y = ae.offset_y;
                         ann.editing  = false;
                     }
                 }
                 for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                 {
                     if (!data.figures[i].custom_tab_title.empty())
                     {
                         fig_mgr.set_title(i, data.figures[i].custom_tab_title);
                     }
                 }
                 if (data.active_figure_index < fig_mgr.count())
                 {
                     fig_mgr.queue_switch(data.active_figure_index);
                 }
                 if (!data.theme_name.empty())
                 {
                     tm.set_theme(data.theme_name);
                     tm.apply_to_imgui();
                 }
                 if (imgui_ui)
                 {
                     auto& lm = imgui_ui->get_layout_manager();
                     lm.set_inspector_visible(data.panels.inspector_visible);
                     lm.set_nav_rail_expanded(data.panels.nav_rail_expanded);
                 }
                 if (!data.dock_state.empty())
                 {
                     dock_system.deserialize(data.dock_state);
                 }
                 if (ui_ctx.plugin_manager && !data.plugin_state.empty())
                     ui_ctx.plugin_manager->deserialize_state(data.plugin_state);
                 if (!data.last_export_dir.empty())
                     ui_ctx.last_export_dir = data.last_export_dir;
             }
         }});

    cmds.push_back({"file.save_figure",
                    "Save Figure",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::Save),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        if (data_interaction)
                        {
                            auto snap = data_interaction->capture_overlay_snapshot(*active_figure);
                            FigureSerializer::save_with_dialog(*active_figure, &snap);
                        }
                        else
                        {
                            FigureSerializer::save_with_dialog(*active_figure);
                        }
                    }});

    cmds.push_back({"file.load_figure",
                    "Load Figure",
                    "",
                    "File",
                    static_cast<uint16_t>(ui::Icon::FolderOpen),
                    [&]()
                    {
                        if (!active_figure)
                            return;
                        if (data_interaction)
                        {
                            OverlaySnapshot snap;
                            FigureSerializer::load_with_dialog(*active_figure, &snap);
                            data_interaction->restore_overlay_snapshot(snap, *active_figure);
                        }
                        else
                        {
                            FigureSerializer::load_with_dialog(*active_figure);
                        }
                        for (auto& ax : active_figure->all_axes_mut())
                        {
                            if (!ax)
                                continue;
                            for (auto& s : ax->series_mut())
                            {
                                if (s)
                                    s->mark_dirty();
                            }
                        }
                    }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
