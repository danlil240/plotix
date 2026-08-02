// spectra-ros — standalone ROS2 visualization / debugging application (G1).
//
// Default layout:
//   - Plot area (maximized center canvas)
//   - Auxiliary panels hidden; open from View menu when needed
//
// CLI:
//   --topics TOPIC[:FIELD] ...   subscribe and plot on launch
//   --bag    FILE                open a bag file on launch
//   --session FILE               load a .spectra-ros-session preset at startup
//   --layout default|plot-only|monitor
//   --window-s SECONDS           auto-scroll time window (default 30)
//   --node-name NAME             ROS2 node name (default spectra_ros)
//   --rows N                     subplot grid rows (default 1)
//   --cols N                     subplot grid cols (default 1)
//
// SIGINT terminates cleanly: bridge shuts down, Vulkan resources freed.
//
// ASan note: ROS2 Humble's librcl has a known new-delete-type-mismatch in
// rcl_wait_set_resize.  When running under AddressSanitizer, launch with:
//   ASAN_OPTIONS=new_delete_type_mismatch=0 ./spectra-ros
// rmw_cyclonedds may not release rosidl_typesupport_cpp handles for
// GenericSubscription (~64 B each) at exit; monitor subs are torn down on the
// app thread while the executor is still running to minimise this.
// Debug note: spectra-ros disables Vulkan validation by default in debug
// builds because some drivers spend ~8s in validation startup. Set
// SPECTRA_ENABLE_VALIDATION=1 to turn it back on for renderer debugging.
// When an NVIDIA ICD is detected, debug launches also pin VK_DRIVER_FILES to
// avoid probing every Vulkan driver manifest; set
// SPECTRA_PRESERVE_VULKAN_LOADER_ENV=1 to disable that fast path.

#include "ros2_adapter.hpp"
#include "ros_app_shell.hpp"
#include "scene/scene_renderer.hpp"

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "render/renderer.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
    #include "ui/native_dialog_policy.hpp"
#endif
#ifdef SPECTRA_USE_GLFW
    #include "ui/window/window_manager.hpp"
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// ASan default options: suppress ROS2 Humble's rcl_wait_set_resize
// new-delete-type-mismatch (upstream bug in retyped_reallocate).
// ---------------------------------------------------------------------------
#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define HAS_ASAN 1
    #endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(HAS_ASAN)
    #define HAS_ASAN 1
#endif

#ifdef HAS_ASAN
extern "C" const char* __asan_default_options()
{
    return "new_delete_type_mismatch=0";
}
#endif

// ---------------------------------------------------------------------------
// Global shutdown flag set by the SIGINT handler
// ---------------------------------------------------------------------------

static spectra::adapters::ros2::RosAppShell* g_shell = nullptr;

#if !defined(_WIN32)
static bool set_env_value(const char* name, const char* value)
{
    return ::setenv(name, value, 1) == 0;
}

static bool unset_env_value(const char* name)
{
    return ::unsetenv(name) == 0;
}
#else
static bool set_env_value(const char* name, const char* value)
{
    return _putenv_s(name, value) == 0;
}

static bool unset_env_value(const char* name)
{
    return _putenv_s(name, "") == 0;
}
#endif

static bool set_env_if_unset(const char* name, const char* value)
{
    if (std::getenv(name) != nullptr)
        return false;

    return set_env_value(name, value);
}

#ifndef NDEBUG
static constexpr const char* k_vulkan_startup_reexec_env = "SPECTRA_VULKAN_STARTUP_REEXEC_DONE";
static constexpr const char* k_vulkan_driver_hint_env    = "SPECTRA_VULKAN_DRIVER_FILES_AUTOSET";

static const char* detect_fast_start_vulkan_driver_manifest()
{
    #if defined(_WIN32)
    return nullptr;
    #else
    if (std::getenv("VK_DRIVER_FILES") != nullptr || std::getenv("VK_ICD_FILENAMES") != nullptr)
        return nullptr;

    const std::filesystem::path nvidia_driver = "/proc/driver/nvidia/version";
    const std::filesystem::path nvidia_icd    = "/usr/share/vulkan/icd.d/nvidia_icd.json";
    if (std::filesystem::exists(nvidia_driver) && std::filesystem::is_regular_file(nvidia_icd))
        return "/usr/share/vulkan/icd.d/nvidia_icd.json";

    return nullptr;
    #endif
}

static void maybe_reexec_for_fast_vulkan_startup(int argc, char** argv)
{
    #if defined(_WIN32)
    (void)argc;
    (void)argv;
    #else
    using namespace spectra::adapters::ros2;

    if (std::getenv(k_vulkan_startup_reexec_env) != nullptr)
        return;

    if (!should_trim_vulkan_loader_environment_for_ros_app(
            std::getenv("SPECTRA_PRESERVE_VULKAN_LOADER_ENV")))
    {
        return;
    }

    const char* manifest = detect_fast_start_vulkan_driver_manifest();
    if (manifest == nullptr)
        return;

    if (!set_env_value(k_vulkan_startup_reexec_env, "1"))
        return;
    if (!set_env_value(k_vulkan_driver_hint_env, manifest))
        return;
    if (!set_env_value("VK_DRIVER_FILES", manifest))
        return;

    ::execvp(argv[0], argv);

    std::perror("spectra-ros: execvp");
    (void)unset_env_value("VK_DRIVER_FILES");
    (void)unset_env_value(k_vulkan_driver_hint_env);
    (void)unset_env_value(k_vulkan_startup_reexec_env);
    #endif
}

static void report_fast_vulkan_startup_policy()
{
    const char* manifest = std::getenv(k_vulkan_driver_hint_env);
    if (manifest == nullptr || manifest[0] == '\0')
        return;

    SPECTRA_LOG_INFO("ros",
                     "using {} for faster Vulkan startup. "
                     "Set SPECTRA_PRESERVE_VULKAN_LOADER_ENV=1 to disable.",
                     manifest);

    (void)unset_env_value(k_vulkan_driver_hint_env);
    (void)unset_env_value(k_vulkan_startup_reexec_env);
}

static void apply_debug_startup_policy()
{
    using namespace spectra::adapters::ros2;

    if (!should_skip_debug_validation_for_ros_app(std::getenv("SPECTRA_NO_VALIDATION"),
                                                  std::getenv("SPECTRA_ENABLE_VALIDATION")))
    {
        return;
    }

    if (set_env_if_unset("SPECTRA_NO_VALIDATION", "1"))
    {
        SPECTRA_LOG_INFO("ros",
                         "Vulkan validation disabled by default in debug builds. "
                         "Set SPECTRA_ENABLE_VALIDATION=1 to re-enable.");
    }
}
#endif

static void sigint_handler(int /*sig*/)
{
    if (g_shell)
        g_shell->request_shutdown();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    using namespace spectra::adapters::ros2;

#ifdef SPECTRA_USE_IMGUI
    spectra::init_native_dialog_policy(argc, argv);
#endif

    // Parse CLI args.
    std::string        err;
    const RosAppConfig cfg = parse_args(argc, argv, err);

    if (!err.empty())
    {
        // "--help" writes usage into err; print and exit 0.
        // Any real parse error is also printed here and exits 1.
        const bool is_help = (err.find("Usage:") == 0);
        std::fputs(err.c_str(), is_help ? stdout : stderr);
        std::fputc('\n', is_help ? stdout : stderr);
        return is_help ? 0 : 1;
    }

    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

#ifndef NDEBUG
    maybe_reexec_for_fast_vulkan_startup(argc, argv);
#endif

    SPECTRA_LOG_INFO("ros",
                     "spectra-ros {}  |  layout: {}  |  node: {}",
                     adapter_version(),
                     layout_mode_name(cfg.layout),
                     cfg.node_name);

#ifndef NDEBUG
    report_fast_vulkan_startup_policy();
    apply_debug_startup_policy();
#endif

    // ---------------------------------------------------------------------------
    // Create Spectra App with GLFW + Vulkan + ImGui windowed rendering.
    // ---------------------------------------------------------------------------

    spectra::AppConfig app_cfg;
    spectra::App       app(app_cfg);

    // Create the figure that spectra-ros binds its subplot manager to.
    // The shell will create the actual subplot axes during init().
    spectra::FigureConfig fig_cfg;
    fig_cfg.width  = cfg.window_width;
    fig_cfg.height = cfg.window_height;
    auto& fig      = app.figure(fig_cfg);

    // Create shell and install SIGINT handler before init.
    RosAppShell shell(cfg);
    shell.set_canvas_figure(&fig);   // Bind ROS subplot manager to the visible app figure.
    g_shell = &shell;
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    // Initialise ROS2, discovery, and all ROS panels.
    if (!shell.init(argc, argv))
    {
        SPECTRA_LOG_ERROR("ros", "failed to initialise ROS2 node '{}'", cfg.node_name);
        return 1;
    }

    SPECTRA_LOG_INFO("ros", "node '{}' started.  Ctrl+C to exit.", cfg.node_name);

    if (!cfg.initial_topics.empty())
    {
        std::string topics_str;
        for (size_t i = 0; i < cfg.initial_topics.size(); ++i)
        {
            if (i > 0)
                topics_str += ", ";
            topics_str += cfg.initial_topics[i];
        }
        SPECTRA_LOG_INFO("ros", "Initial topics: {}", topics_str);
    }

    // Set up a perpetual animation callback so the render loop stays active
    // and we can poll ROS2 messages each frame.
    fig.animate()
        .fps(60.0f)
        .on_frame(
            [&shell](spectra::Frame& /*frame*/)
            {
                // Drain ROS2 ring buffers each frame.
                shell.poll();
            })
        .loop(true)
        .play();

    // ---------------------------------------------------------------------------
    // Initialise the Spectra rendering runtime (GLFW window, Vulkan, ImGui).
    // ---------------------------------------------------------------------------
    app.init_runtime();

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Stop ROS plot subscriptions before WindowManager destroys the canvas
    // Figure (window close races the ROS2 executor thread).
    if (auto* wm = app.window_manager())
    {
        wm->set_figure_unregistering_handler(
            [&shell](spectra::FigureId /*id*/, spectra::Figure* fig)
            {
                if (fig && shell.canvas_figure() == fig)
                    shell.detach_canvas_figure();
            });
    }
#endif

    if (auto* exporter = shell.screenshot_export())
    {
        exporter->set_capture_size_callback(
            [&app](uint32_t& w, uint32_t& h) -> bool
            {
                auto* backend = app.backend();
                if (!backend)
                    return false;
                w = backend->swapchain_width();
                h = backend->swapchain_height();
                return w > 0 && h > 0;
            });
        exporter->set_frame_grab_callback(
            [&app](uint8_t* buf, uint32_t w, uint32_t h) -> bool
            {
                auto* backend = app.backend();
                return backend != nullptr && backend->readback_framebuffer(buf, w, h);
            });
        exporter->set_frame_render_callback(
            [&app](uint32_t /*frame_index*/, float /*time*/, uint8_t* buf, uint32_t w, uint32_t h)
                -> bool
            {
                auto* backend = app.backend();
                return backend != nullptr && backend->readback_framebuffer(buf, w, h);
            });
    }

#ifdef SPECTRA_USE_IMGUI
    // Hide Spectra's default chrome — the ROS2 shell provides its own menu bar,
    // status bar, and panel layout.  Keep only the bare canvas.
    auto* ui_ctx = app.ui_context();
    if (ui_ctx && ui_ctx->imgui_ui)
    {
        // Enable ImGui docking BEFORE the first NewFrame() call.
        ui_ctx->imgui_ui->enable_docking();
        auto& lm = ui_ctx->imgui_ui->get_layout_manager();
        lm.set_inspector_visible(false);
        lm.set_tab_bar_visible(false);
        // Reserve nav-rail layout width; core SpectraNavRail is suppressed while
        // extra_draw_cb_ is set (RosAppShell draws the adapter rail instead).
        ui_ctx->imgui_ui->set_nav_rail_visible(shell.nav_rail_visible());
        // Suppress all Spectra chrome — spectra-ros owns its own menu bar,
        // status bar, and docking layout via RosAppShell.
        // Hide the ImGui canvas overlay (Figure 1 tab, etc.) since the ROS
        // shell provides its own Plot Area panel, but keep Vulkan figure
        // rendering enabled so axes/gridlines/series are drawn.  The shell
        // overrides canvas_rect via the LayoutManager each frame.
        ui_ctx->imgui_ui->set_canvas_visible(false);
        ui_ctx->imgui_ui->set_render_figure_enabled(true);
        ui_ctx->imgui_ui->set_command_bar_visible(false);
        ui_ctx->imgui_ui->set_status_bar_visible(false);
        shell.set_layout_manager(&lm);
        shell.bind_imgui(ui_ctx->imgui_ui.get());
        ui_ctx->imgui_ui->set_extra_draw_callback(
            [&shell, ui_ctx]()
            {
                if (ui_ctx && ui_ctx->imgui_ui)
                {
                    // Keep layout-manager rail width in sync when toggled from View menu.
                    ui_ctx->imgui_ui->set_nav_rail_visible(shell.nav_rail_visible());
                    ui_ctx->imgui_ui->set_render_figure_enabled(
                        shell.panel_visible("ros.plot_area"));
                }
                shell.draw();
            });
        // GPU scene render callback — invoked during the active Vulkan render
        // pass (before ImGui overlay) so the 3D scene viewport is drawn with
        // real GPU pipelines instead of the software ImGui preview.
        auto scene_renderer = std::make_shared<spectra::adapters::ros2::SceneRenderer>();
        ui_ctx->imgui_ui->set_scene_render_callback(
            [&shell, scene_renderer](spectra::Renderer& renderer)
            {
                auto* sv = shell.scene_viewport();
                if (!sv || !shell.panel_visible("ros.scene_viewport"))
                    return;
                auto rect = sv->canvas_rect();
                if (rect.w < 1.0f || rect.h < 1.0f)
                    return;
                scene_renderer->render(renderer, shell.scene_manager(), sv->camera(), rect);
            });
    }
#endif

#ifdef SPECTRA_USE_GLFW
    // Forward OS file drops to the bag info panel.
    if (auto* wm = app.window_manager())
    {
        wm->set_file_drop_handler(
            [&shell](uint32_t /*window_id*/, const std::string& path)
            {
                if (auto* panel = shell.bag_info_panel())
                {
                    if (panel->try_open_file(path))
                        shell.set_panel_visible("ros.bag_info", true);
                }
            });
    }
#endif

    // ---------------------------------------------------------------------------
    // Render loop — runs until window is closed or SIGINT received.
    // ---------------------------------------------------------------------------

    for (;;)
    {
        if (shell.shutdown_requested())
            break;

        auto result = app.step();

        if (result.should_exit)
            break;
    }

    // ---------------------------------------------------------------------------
    // Clean shutdown: disconnect ImGui callback, tear down Vulkan, then ROS2.
    // Order matters: the animation callback references shell.poll(), so the
    // Spectra runtime must be destroyed before the ROS2 bridge.
    // ---------------------------------------------------------------------------
    SPECTRA_LOG_INFO("ros", "shutting down.");

#ifdef SPECTRA_USE_IMGUI
    // Disconnect ROS2 draw callback before tearing down the render loop.
    // Guard: the window (and its ImGuiIntegration) may already be destroyed
    // by process_pending_closes() if the user closed the OS window.
    {
        auto* ctx = app.ui_context();
        if (ctx && ctx->imgui_ui)
        {
            ctx->imgui_ui->set_extra_draw_callback(nullptr);
            ctx->imgui_ui->set_scene_render_callback(nullptr);
        }
    }
#endif

    app.shutdown_runtime();
    shell.shutdown();
    g_shell = nullptr;

    if (rclcpp::ok())
        rclcpp::shutdown();

    return 0;
}
