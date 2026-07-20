#include "window_ui_context_builder.hpp"

#include <spectra/figure_registry.hpp>

#include "register_commands.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #ifdef SPECTRA_USE_GLFW
        #define GLFW_INCLUDE_NONE
        #define GLFW_INCLUDE_VULKAN
        #include <GLFW/glfw3.h>
    #elif defined(SPECTRA_USE_SDL3)
        #include <SDL3/SDL.h>
    #endif

    #include "ui/figures/figure_manager.hpp"
    #include "ui/figures/tab_bar.hpp"
    #include "ui/imgui/imgui_integration.hpp"
#endif

namespace spectra
{

namespace
{

WindowUIContextBuildMode resolve_build_mode(const WindowUIContextBuildOptions& options)
{
    if (options.headless)
        return WindowUIContextBuildMode::Headless;
    return options.mode;
}

}   // namespace

#ifdef SPECTRA_USE_IMGUI
bool init_window_imgui_integration(VulkanBackend&    backend,
                                   WindowContext&    wctx,
                                   ImGuiIntegration& imgui,
                                   bool              install_callbacks)
{
    if (!wctx.glfw_window)
        return false;

    ImGuiContext* prev_imgui_ctx = ImGui::GetCurrentContext();
    auto*         prev_active    = backend.active_window();
    backend.set_active_window(&wctx);

    bool imgui_ok = false;
    #ifdef SPECTRA_USE_GLFW
    imgui_ok = imgui.init(backend, static_cast<GLFWwindow*>(wctx.glfw_window), install_callbacks);
    #elif defined(SPECTRA_USE_SDL3)
    imgui_ok = imgui.init_sdl3(backend, static_cast<SDL_Window*>(wctx.glfw_window));
    (void)install_callbacks;
    #endif

    if (!imgui_ok)
    {
        backend.set_active_window(prev_active);
        ImGui::SetCurrentContext(prev_imgui_ctx);
        return false;
    }

    wctx.imgui_context = ImGui::GetCurrentContext();
    backend.set_active_window(prev_active);
    ImGui::SetCurrentContext(prev_imgui_ctx);
    return true;
}
#endif

std::unique_ptr<WindowUIContext> build_window_ui_context(const WindowUIContextBuildOptions& options)
{
    if (!options.theme_mgr)
        return nullptr;

    const auto mode = resolve_build_mode(options);

    auto ui       = std::make_unique<WindowUIContext>();
    ui->theme_mgr = options.theme_mgr;

    if (mode == WindowUIContextBuildMode::ImGuiOnly)
    {
#ifdef SPECTRA_USE_IMGUI
        ui->imgui_ui = std::make_unique<ImGuiIntegration>();
        ui->imgui_ui->set_theme_manager(options.theme_mgr);
#endif
        return ui;
    }

    if (!options.registry)
        return nullptr;

    // Headless path: minimal FigureManager-only context.
    if (mode == WindowUIContextBuildMode::Headless)
    {
        ui->fig_mgr_owned = std::make_unique<FigureManager>(*options.registry);
        ui->fig_mgr       = ui->fig_mgr_owned.get();
        return ui;
    }

#ifdef SPECTRA_USE_IMGUI
    Figure** active_figure_ptr =
        options.active_figure ? options.active_figure : &ui->per_window_active_figure;
    FigureId* active_figure_id_ptr =
        options.active_figure_id ? options.active_figure_id : &ui->per_window_active_figure_id;

    ui->fig_mgr_owned = std::make_unique<FigureManager>(*options.registry);
    ui->fig_mgr       = ui->fig_mgr_owned.get();
    {
        auto all = ui->fig_mgr->figure_ids();
        for (auto id : all)
        {
            if (id != options.initial_figure_id)
                ui->fig_mgr->remove_figure(id);
        }
    }

    ui->figure_tabs = std::make_unique<TabBar>();
    ui->fig_mgr->set_tab_bar(ui->figure_tabs.get());

    if (options.on_window_close_request)
        ui->fig_mgr->set_on_window_close_request(options.on_window_close_request);

    auto* registry  = options.registry;
    auto* ui_raw    = ui.get();
    auto  on_closed = options.on_figure_closed;
    ui->fig_mgr->set_on_figure_closed(
        [registry, ui_raw, on_closed](FigureId id)
        {
            Figure* fig = registry ? registry->get(id) : nullptr;
            if (fig)
            {
                if (ui_raw->data_interaction)
                    ui_raw->data_interaction->clear_figure_cache(fig);
                ui_raw->input_handler.clear_figure_cache(fig);
                if (ui_raw->imgui_ui)
                    ui_raw->imgui_ui->clear_figure_cache(fig);
            }

            // Remove figure from the split pane tree so stale tabs don't linger
            auto& sv   = ui_raw->dock_system.split_view();
            auto* root = sv.root();
            if (root)
            {
                auto* pane = root->find_by_figure(id);
                if (pane)
                {
                    if (pane->figure_count() <= 1 && root->is_split())
                    {
                        // Sole figure in a split pane — collapse the split
                        ui_raw->dock_system.close_split(id);
                    }
                    else
                    {
                        // Multiple figures in this pane — just remove the tab
                        pane->remove_figure(id);
                    }
                }
            }

            if (on_closed)
                on_closed(id, fig);
        });

    auto* fig_mgr_ptr = ui->fig_mgr;
    auto* dock_ptr    = &ui->dock_system;
    auto* guard_ptr   = &ui->dock_tab_sync_guard;

    ui->figure_tabs->set_tab_change_callback(
        [fig_mgr_ptr, dock_ptr, guard_ptr](size_t new_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr      = true;
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (new_index < ids.size())
            {
                FigureId fid = ids[new_index];
                fig_mgr_ptr->queue_switch(fid);
                dock_ptr->set_active_figure_index(fid);
            }
            *guard_ptr = false;
        });
    ui->figure_tabs->set_tab_close_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->queue_close(ids[index]);
        });
    ui->figure_tabs->set_tab_add_callback([fig_mgr_ptr]() { fig_mgr_ptr->queue_create(); });
    ui->figure_tabs->set_tab_duplicate_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->duplicate_figure(ids[index]);
        });
    ui->figure_tabs->set_tab_close_all_except_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->close_all_except(ids[index]);
        });
    ui->figure_tabs->set_tab_close_to_right_callback(
        [fig_mgr_ptr](size_t index)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->close_to_right(ids[index]);
        });
    ui->figure_tabs->set_tab_rename_callback(
        [fig_mgr_ptr](size_t index, const std::string& title)
        {
            const auto& ids = fig_mgr_ptr->figure_ids();
            if (index < ids.size())
                fig_mgr_ptr->set_title(ids[index], title);
        });

    ui->figure_tabs->set_tab_drag_out_callback([dock_ptr](size_t index, float mx, float my)
                                               { dock_ptr->begin_drag(index, mx, my); });
    ui->figure_tabs->set_tab_drag_update_callback([dock_ptr](size_t, float mx, float my)
                                                  { dock_ptr->update_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_end_callback([dock_ptr](size_t, float mx, float my)
                                               { dock_ptr->end_drag(mx, my); });
    ui->figure_tabs->set_tab_drag_cancel_callback([dock_ptr](size_t) { dock_ptr->cancel_drag(); });

    ui->overlay_registry = options.overlay_registry;
    ui->plugin_manager   = options.plugin_manager;

    if (options.create_imgui_integration)
    {
        ui->imgui_ui = std::make_unique<ImGuiIntegration>();
        ui->imgui_ui->set_theme_manager(options.theme_mgr);
        ui->imgui_ui->set_dock_system(&ui->dock_system);
        ui->imgui_ui->set_tab_bar(ui->figure_tabs.get());
        ui->imgui_ui->set_command_palette(&ui->cmd_palette);
        ui->imgui_ui->set_command_registry(&ui->cmd_registry);
        ui->imgui_ui->set_shortcut_manager(&ui->shortcut_mgr);
        ui->imgui_ui->set_undo_manager(&ui->undo_mgr);
        ui->imgui_ui->set_axis_link_manager(&ui->axis_link_mgr);
        ui->imgui_ui->set_input_handler(&ui->input_handler);
        ui->imgui_ui->set_timeline_editor(&ui->timeline_editor);
        ui->imgui_ui->set_keyframe_interpolator(&ui->keyframe_interpolator);
        ui->imgui_ui->set_curve_editor(&ui->curve_editor);
        ui->imgui_ui->set_mode_transition(&ui->mode_transition);
        ui->imgui_ui->set_knob_manager(&ui->knob_manager);
        ui->imgui_ui->set_overlay_registry(ui->overlay_registry);
        ui->imgui_ui->set_plugin_manager(ui->plugin_manager);
        ui->imgui_ui->set_export_format_registry(options.export_format_registry);
        ui->imgui_ui->set_series_clipboard(options.series_clipboard);
        ui->imgui_ui->set_topics_panel(&ui->topics_panel);
        ui->app_shell = std::make_unique<ui::shell::SpectraAppShell>(ui->imgui_ui.get());
        ui->imgui_ui->set_app_shell(ui->app_shell.get());
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        ui->tab_drag_controller.set_window_manager(options.window_manager);
        ui->tab_drag_controller.set_dock_system(&ui->dock_system);
        ui->tab_drag_controller.set_source_window_id(options.window_id);
        ui->imgui_ui->set_tab_drag_controller(&ui->tab_drag_controller);
        ui->imgui_ui->set_window_id(options.window_id);
        ui->imgui_ui->set_window_manager(options.window_manager);
    #endif
    }

    ui->data_interaction = std::make_unique<DataInteraction>();
    ui->data_interaction->set_theme_manager(options.theme_mgr);
    ui->data_interaction->set_axis_link_manager(&ui->axis_link_mgr);
    if (ui->imgui_ui)
        ui->imgui_ui->set_data_interaction(ui->data_interaction.get());
    ui->input_handler.set_data_interaction(ui->data_interaction.get());

    ui->box_zoom_overlay.set_input_handler(&ui->input_handler);
    if (ui->imgui_ui)
        ui->imgui_ui->set_box_zoom_overlay(&ui->box_zoom_overlay);

    ui->input_handler.set_animation_controller(&ui->anim_controller);
    ui->input_handler.set_gesture_recognizer(&ui->gesture);
    ui->input_handler.set_shortcut_manager(&ui->shortcut_mgr);
    ui->input_handler.set_undo_manager(&ui->undo_mgr);
    ui->input_handler.set_axis_link_manager(&ui->axis_link_mgr);

    auto* imgui_raw = ui->imgui_ui.get();
    ui->data_interaction->set_on_series_selected(
        [imgui_raw](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
        {
            if (imgui_raw)
                imgui_raw->select_series(fig, ax, ax_idx, s, s_idx);
        });
    ui->data_interaction->set_on_series_right_click_selected(
        [imgui_raw](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx)
        {
            if (imgui_raw)
                imgui_raw->select_series_no_toggle(fig, ax, ax_idx, s, s_idx);
        });
    ui->data_interaction->set_on_series_deselected(
        [imgui_raw]()
        {
            if (imgui_raw)
                imgui_raw->deselect_series();
        });
    ui->data_interaction->set_on_rect_series_selected(
        [imgui_raw](const std::vector<DataInteraction::RectSelectedEntry>& entries)
        {
            if (imgui_raw)
                imgui_raw->select_series_in_rect(entries);
        });

    if (ui->imgui_ui)
    {
        ui->imgui_ui->set_pane_tab_duplicate_cb([fig_mgr_ptr](FigureId index)
                                                { fig_mgr_ptr->duplicate_figure(index); });
        ui->imgui_ui->set_pane_tab_close_cb([fig_mgr_ptr](FigureId index)
                                            { fig_mgr_ptr->queue_close(index); });
        ui->imgui_ui->set_pane_tab_split_right_cb(
            [dock_ptr](FigureId index)
            {
                auto* pane = dock_ptr->split_view().root()
                                 ? dock_ptr->split_view().root()->find_by_figure(index)
                                 : nullptr;
                if (!pane || pane->figure_count() < 2)
                    return;
                auto* new_pane = dock_ptr->split_figure_right(index, index);
                if (!new_pane)
                    return;
                auto* parent = new_pane->parent();
                if (parent && parent->first())
                    parent->first()->remove_figure(index);
                dock_ptr->set_active_figure_index(index);
            });
        ui->imgui_ui->set_pane_tab_split_down_cb(
            [dock_ptr](FigureId index)
            {
                auto* pane = dock_ptr->split_view().root()
                                 ? dock_ptr->split_view().root()->find_by_figure(index)
                                 : nullptr;
                if (!pane || pane->figure_count() < 2)
                    return;
                auto* new_pane = dock_ptr->split_figure_down(index, index);
                if (!new_pane)
                    return;
                auto* parent = new_pane->parent();
                if (parent && parent->first())
                    parent->first()->remove_figure(index);
                dock_ptr->set_active_figure_index(index);
            });
        ui->imgui_ui->set_pane_tab_rename_cb([fig_mgr_ptr](size_t index, const std::string& title)
                                             { fig_mgr_ptr->set_title(index, title); });
        ui->imgui_ui->set_figure_title_callback(
            [fig_mgr_ptr](size_t fig_idx) -> std::string
            { return fig_mgr_ptr->get_title(static_cast<FigureId>(fig_idx)); });
        ui->imgui_ui->set_figure_ptr_callback([fig_mgr_ptr](FigureId id) -> Figure*
                                              { return fig_mgr_ptr->get_figure(id); });
    }

    auto* figure_tabs_raw = ui->figure_tabs.get();
    ui->dock_system.split_view().set_on_active_changed(
        [figure_tabs_raw, fig_mgr_ptr, guard_ptr](FigureId figure_index)
        {
            if (*guard_ptr)
                return;
            *guard_ptr      = true;
            const auto& ids = fig_mgr_ptr->figure_ids();
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (ids[i] == figure_index)
                {
                    if (figure_tabs_raw && i < figure_tabs_raw->get_tab_count())
                        figure_tabs_raw->set_active_tab(i);
                    break;
                }
            }
            fig_mgr_ptr->queue_switch(figure_index);
            *guard_ptr = false;
        });

    ui->timeline_editor.set_interpolator(&ui->keyframe_interpolator);
    ui->curve_editor.set_interpolator(&ui->keyframe_interpolator);

    ui->shortcut_mgr.set_command_registry(&ui->cmd_registry);
    ui->cmd_palette.set_command_registry(&ui->cmd_registry);
    ui->cmd_palette.set_shortcut_manager(&ui->shortcut_mgr);

    Figure* initial_figure          = options.registry->get(options.initial_figure_id);
    ui->per_window_active_figure    = initial_figure;
    ui->per_window_active_figure_id = options.initial_figure_id;
    *active_figure_ptr              = initial_figure;
    *active_figure_id_ptr           = options.initial_figure_id;

    if (initial_figure)
    {
        ui->input_handler.set_figure(initial_figure);
        if (!initial_figure->axes().empty() && initial_figure->axes()[0])
        {
            ui->input_handler.set_active_axes(initial_figure->axes()[0].get());
            const auto& vp = initial_figure->axes()[0]->viewport();
            ui->input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
    }

    CommandBindings bindings;
    bindings.ui_ctx           = ui.get();
    bindings.registry         = options.registry;
    bindings.active_figure    = active_figure_ptr;
    bindings.active_figure_id = active_figure_id_ptr;
    bindings.session          = options.session;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    bindings.window_mgr = options.window_manager;
    #endif
    register_standard_commands(bindings);

    ui->settings_cfg.set_shortcut_manager(&ui->shortcut_mgr);
    ui->settings_cfg.load(ShortcutConfig::default_path());
    ui->settings_cfg.apply_overrides();

    ui->settings_store = options.settings_store;
    ui->settings_panel.set_settings_store(ui->settings_store);
    ui->settings_panel.set_theme_manager(options.theme_mgr);
    ui->settings_panel.set_shortcut_config(&ui->settings_cfg);
    if (ui->imgui_ui)
        ui->imgui_ui->set_settings_panel(&ui->settings_panel);
#endif

    return ui;
}

}   // namespace spectra
