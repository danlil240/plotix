// spectra-px4 — standalone PX4 ULog visualizer / real-time inspection tool.
//
// Modes:
//   1. Offline: load a .ulg file, browse topics, plot fields
//   2. Real-time: connect via MAVLink UDP, live telemetry plotting
//
// CLI:
//   --ulog FILE       open a ULog file on launch
//   --host HOST       MAVLink UDP host (default 127.0.0.1)
//   --port PORT       MAVLink UDP port (default 14540)
//   --connect         auto-connect to MAVLink on launch
//   --window-s SEC    real-time time window (default 30)
//
// Examples:
//   spectra-px4 flight.ulg                    # open log file
//   spectra-px4 --connect --port 14540        # live SITL inspection
//   spectra-px4 --ulog log.ulg --connect      # both modes simultaneously

#include "px4_adapter.hpp"
#include "px4_app_shell.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
    #include "ui/native_dialog_policy.hpp"
#endif

#include <csignal>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Global shutdown flag
// ---------------------------------------------------------------------------

static spectra::adapters::px4::Px4AppShell* g_shell = nullptr;

static void sigint_handler(int /*sig*/)
{
    if (g_shell)
    {
        g_shell->request_shutdown();
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace spectra::adapters::px4;

#ifdef SPECTRA_USE_IMGUI
    spectra::init_native_dialog_policy(argc, argv);
#endif

    // Parse CLI args.
    std::string        err;
    const Px4AppConfig cfg = parse_px4_args(argc, argv, err);

    if (!err.empty())
    {
        const bool is_help = (err.find("Usage:") == 0);
        std::fputs(err.c_str(), is_help ? stdout : stderr);
        std::fputc('\n', is_help ? stdout : stderr);
        return is_help ? 0 : 1;
    }

    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

    SPECTRA_LOG_INFO("px4", "spectra-px4 {}", adapter_version());

    // Create Spectra App.
    spectra::AppConfig app_cfg;
    spectra::App       app(app_cfg);

    spectra::FigureConfig fig_cfg;
    fig_cfg.width  = cfg.window_width;
    fig_cfg.height = cfg.window_height;
    auto& fig      = app.figure(fig_cfg);

    // Create shell.
    Px4AppShell shell(cfg);
    shell.set_canvas_figure(&fig);
    g_shell = &shell;
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    if (!shell.init())
    {
        SPECTRA_LOG_ERROR("px4", "failed to initialise");
        return 1;
    }

    SPECTRA_LOG_INFO("px4", "ready.  Ctrl+C to exit.");

    // Animation loop.
    fig.animate()
        .fps(60.0f)
        .on_frame([&shell](spectra::Frame& /*frame*/) { shell.poll(); })
        .loop(true)
        .play();

    app.init_runtime();

#ifdef SPECTRA_USE_IMGUI
    // Hide Spectra's default chrome — spectra-px4 owns menu bar, status bar, and dock layout.
    auto* ui_ctx = app.ui_context();
    if (ui_ctx && ui_ctx->imgui_ui)
    {
        ui_ctx->imgui_ui->enable_docking();
        auto& lm = ui_ctx->imgui_ui->get_layout_manager();
        lm.set_inspector_visible(false);
        lm.set_tab_bar_visible(false);
        ui_ctx->imgui_ui->set_nav_rail_visible(false);
        ui_ctx->imgui_ui->set_canvas_visible(false);
        ui_ctx->imgui_ui->set_render_figure_enabled(true);
        ui_ctx->imgui_ui->set_command_bar_visible(false);
        ui_ctx->imgui_ui->set_status_bar_visible(false);
        shell.set_layout_manager(&lm);
        ui_ctx->imgui_ui->set_extra_draw_callback(
            [&shell, ui_ctx]()
            {
                if (ui_ctx && ui_ctx->imgui_ui)
                    ui_ctx->imgui_ui->set_nav_rail_visible(shell.nav_rail_visible());
                shell.draw();
            });
    }

    // Wire WindowManager so panels can create real OS windows on detach.
    if (auto* wm = app.window_manager())
        shell.set_window_manager(wm);
#endif

    // Render loop.
    for (;;)
    {
        if (shell.shutdown_requested())
            break;
        auto result = app.step();
        shell.process_pending_panels();
        if (result.should_exit)
            break;
    }

    // Clean shutdown.
    SPECTRA_LOG_INFO("px4", "shutting down.");

#ifdef SPECTRA_USE_IMGUI
    {
        auto* ctx = app.ui_context();
        if (ctx && ctx->imgui_ui)
            ctx->imgui_ui->set_extra_draw_callback(nullptr);
    }
#endif

    app.shutdown_runtime();
    shell.shutdown();
    g_shell = nullptr;
    return 0;
}
