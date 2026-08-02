#include "window_manager_bootstrap.hpp"

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)

    #include "io/export_registry.hpp"
    #include "render/vulkan/window_context.hpp"
    #include "session_runtime.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/window/window_manager.hpp"
    #include "ui/workspace/plugin_api.hpp"

namespace spectra
{

void configure_window_manager(WindowManager& wm, const WindowManagerBootstrapOptions& options)
{
    if (!options.backend || !options.session)
        return;

    wm.init(options.backend, options.registry, options.renderer, options.theme_mgr);

    wm.set_redraw_request_handler([session = options.session](const char* reason)
                                  { session->redraw_tracker().mark_dirty(reason); });

    if (options.plugin_manager)
        wm.set_plugin_manager(options.plugin_manager);
    if (options.export_format_registry)
        wm.set_export_format_registry(options.export_format_registry);

    wm.set_session_runtime(options.session);

    if (options.settings_store)
        wm.set_settings_store(options.settings_store);

    wm.set_tab_detach_handler([session = options.session](FigureId           fid,
                                                          uint32_t           w,
                                                          uint32_t           h,
                                                          const std::string& title,
                                                          int                sx,
                                                          int                sy)
                              { session->queue_detach({fid, w, h, title, sx, sy}); });

    wm.set_tab_move_handler(
        [session = options.session](FigureId fid,
                                    uint32_t target_wid,
                                    int      drop_zone,
                                    float    local_x,
                                    float    local_y,
                                    FigureId target_figure_id)
        { session->queue_move({fid, target_wid, drop_zone, local_x, local_y, target_figure_id}); });
}

std::unique_ptr<WindowManager> create_configured_window_manager(
    const WindowManagerBootstrapOptions& options)
{
    auto wm = std::make_unique<WindowManager>();
    configure_window_manager(*wm, options);
    return wm;
}

}   // namespace spectra

#endif
