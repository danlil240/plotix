// app_step.cpp — Frame-by-frame control API for App.
// Extracts init / step / shutdown from app_inproc.cpp so external
// drivers (QA agent, test harnesses) can pump frames individually.

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include <algorithm>

#include "anim/frame_scheduler.hpp"
#include "math/data_transform.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#ifdef SPECTRA_USE_WEBGPU
    #include "render/webgpu/wgpu_backend.hpp"
#endif
#include "session_runtime.hpp"
#include "window_runtime.hpp"
#include "window_ui_context_runtime.hpp"
#include "window_manager_bootstrap.hpp"
#include "window_ui_context_builder.hpp"
#include "window_ui_context.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "ui/window/glfw_adapter.hpp"
#endif

#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
    #include "ui/window/sdl3_adapter.hpp"
#endif

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

#include "register_commands.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/workspace/workspace.hpp"
#endif

#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"

#include "app/application_services.hpp"
#include "app/imgui_frontend_services.hpp"

#ifndef _WIN32
    #include "../../app/inproc_topic_server.hpp"
#endif

#include "perf_metrics.hpp"
#include "platform/clipboard_image.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace spectra
{

// ─── App ctor/dtor (must be here where AppRuntime is complete) ───────────────
App::App(const AppConfig& config) : config_(config)
{
    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());

    SPECTRA_LOG_INFO("app",
                     "Initializing Spectra application (headless: "
                         + std::string(config_.headless ? "true" : "false") + ")");

    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = ((env != nullptr) && env[0] != '\0');
    }
    SPECTRA_LOG_INFO("app", "Runtime mode: " + std::string(multiproc ? "multiproc" : "inproc"));

#ifdef SPECTRA_USE_SDL3
    // SDL must be initialized before VulkanBackend::init() because
    // SDL_Vulkan_GetInstanceExtensions() is called during Vulkan instance
    // creation and requires the SDL video subsystem to be running.
    if (!config_.headless)
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            SPECTRA_LOG_ERROR("app",
                              std::string("Failed to pre-initialize SDL video: ") + SDL_GetError());
            return;
        }
    }
#endif

    backend_ = std::make_unique<VulkanBackend>();
#ifdef SPECTRA_USE_WEBGPU
    if (config_.backend == RenderBackend::WebGPU)
        backend_ = std::make_unique<WebGPUBackend>();
#endif
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    // Create the App-owned ThemeManager and register it as the active instance
    // so ThemeManager::instance() returns this object instead of the fallback
    // singleton for the lifetime of this App.
    theme_mgr_ = std::make_unique<ui::ThemeManager>();
    ui::ThemeManager::set_current(theme_mgr_.get());
    theme_mgr_->ensure_initialized();

    renderer_ = std::make_unique<Renderer>(*backend_, *theme_mgr_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }

    SPECTRA_LOG_INFO("app", "Spectra application initialized successfully");
}

App::~App()
{
    // Guard each destruction step with try-catch.  On CI with lavapipe
    // (software Vulkan), non-deterministic std::system_error("Invalid
    // argument") can be thrown from deep inside member destructors
    // (likely from pthread operations in the software driver).  Since
    // destructors are noexcept, an uncaught throw triggers std::terminate.
    try
    {
        shutdown_runtime();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in shutdown_runtime: ") + e.what());
    }
    catch (...)
    {
    }

    try
    {
        runtime_.reset();
    }
    catch (...)
    {
    }

    try
    {
        renderer_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in ~Renderer: ") + e.what());
    }
    catch (...)
    {
    }

    // Clear the singleton pointer before the ThemeManager member is destroyed
    // to prevent any remaining call sites from dereferencing a dangling pointer.
    ui::ThemeManager::set_current(nullptr);
    theme_mgr_.reset();
    if (backend_)
    {
        try
        {
            backend_->shutdown();
        }
        catch (...)
        {
        }
    }

#ifdef SPECTRA_USE_SDL3
    // Balance the pre-backend SDL_Init(SDL_INIT_VIDEO) call made before
    // VulkanBackend::init().  SdlAdapter::shutdown() already decremented the
    // SDL subsystem reference count once (via SDL_Quit); this call brings
    // the count to zero and fully releases the video subsystem.
    if (!config_.headless && SDL_WasInit(SDL_INIT_VIDEO))
        SDL_Quit();
#endif
}

// ─── AppRuntime: all state that lives across frames ──────────────────────────
struct App::AppRuntime
{
    AppRuntime(const AppRuntime&)            = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    // Framework-neutral service owner — single source of truth for all
    // process-scoped services (commands, shortcuts, undo, plugins, settings,
    // session, scheduler, animator, automation, topic server, etc.)
    std::unique_ptr<ApplicationServices> app_services;

    FrameState frame_state;
    uint64_t   frame_number = 0;

    WindowUIContext*                 ui_ctx_ptr = nullptr;
    std::unique_ptr<WindowUIContext> headless_ui_ctx;

    Figure*  active_figure    = nullptr;
    FigureId active_figure_id = INVALID_FIGURE_ID;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<GlfwAdapter>   glfw;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_SDL3
    std::unique_ptr<Sdl3Adapter>   sdl3;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t>           video_frame_pixels;
    bool                           is_recording = false;
#endif

    // Wall-clock for frame timing
    std::chrono::steady_clock::time_point last_step_time;

    // Frontend service implementations (injected into ApplicationServices).
    // These bridge the framework-neutral service interfaces to the
    // ImGui/GLFW/SDL3 frontend so commands and automation can interact
    // with the frontend without knowing which UI framework is active.
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    std::unique_ptr<ImGuiDialogService>    dialog_service_impl;
    std::unique_ptr<ImGuiClipboardService> clipboard_service_impl;
    std::unique_ptr<ImGuiRedrawRequest>    redraw_request_impl;
    std::unique_ptr<ImGuiWindowService>    window_service_impl;
#endif

    // Pending PNG capture (pre-present path to avoid reading presented images)
    struct PendingPngCapture
    {
        std::vector<uint8_t> pixels;
        std::string          path;
        uint32_t             width                = 0;
        uint32_t             height               = 0;
        bool                 active               = false;
        bool                 copy_to_clipboard    = false;
        bool                 awaiting_gpu_capture = false;
    } pending_png_capture;

    AppRuntime() = default;

    // Explicit noexcept(false) destructor: the implicit default destructor
    // is noexcept, which means any exception thrown during member destruction
    // triggers std::terminate.  On CI with lavapipe (software Vulkan),
    // non-deterministic std::system_error("Invalid argument") can occur
    // during member cleanup.  Making the destructor noexcept(false) lets
    // the exception propagate to callers that have try-catch guards.
    ~AppRuntime() noexcept(false) = default;
};

// ─── init_runtime ────────────────────────────────────────────────────────────
void App::init_runtime()
{
    PerfMetrics::instance().mark_startup_begin();

    if (!backend_ || !renderer_)
    {
        SPECTRA_LOG_ERROR("app", "Cannot run: backend or renderer not initialized");
        return;
    }

    auto       all_ids       = registry_.all_ids();
    auto       window_groups = compute_window_groups();
    const bool empty_start   = all_ids.empty();

    float    init_fps       = 60.0f;
    Figure*  init_active    = nullptr;
    FigureId init_active_id = INVALID_FIGURE_ID;

    if (!empty_start)
    {
        init_active_id = all_ids[0];
        init_active    = registry_.get(init_active_id);
        if (init_active && init_active->anim_.fps > 0.0f)
            init_fps = init_active->anim_.fps;
    }

    uint32_t init_w = init_active ? init_active->width() : 1280;
    uint32_t init_h = init_active ? init_active->height() : 720;

    runtime_ = std::make_unique<AppRuntime>();
    auto& rt = *runtime_;

    // ApplicationServices is the single owner of all process-scoped services.
    rt.app_services = std::make_unique<ApplicationServices>();
    rt.app_services->init(registry_, *backend_, *renderer_, *theme_mgr_, init_fps);

    rt.frame_state.active_figure_id = init_active_id;
    rt.frame_state.active_figure    = init_active;
    rt.active_figure                = init_active;
    rt.active_figure_id             = init_active_id;

    rt.frame_state.has_animation = init_active ? init_active->has_animation() : false;

    if (!config_.headless)
    {
        rt.app_services->scheduler().set_mode(FrameScheduler::Mode::VSync);
    }

#ifdef SPECTRA_USE_FFMPEG
    rt.is_recording = (init_active != nullptr) && !init_active->export_req_.video_path.empty();
#else
    if (init_active && !init_active->export_req_.video_path.empty())
    {
        SPECTRA_LOG_WARN("app", "Video recording requested but SPECTRA_USE_FFMPEG is not enabled");
    }
#endif

#ifdef SPECTRA_USE_FFMPEG
    if (rt.is_recording)
    {
        VideoExporter::Config vcfg;
        vcfg.output_path  = init_active->export_req_.video_path;
        vcfg.width        = init_active->width();
        vcfg.height       = init_active->height();
        vcfg.fps          = init_active->anim_.fps;
        rt.video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!rt.video_exporter->is_open())
        {
            SPECTRA_LOG_ERROR("app",
                              "Failed to open video exporter for: {}",
                              init_active->export_req_.video_path);
            rt.video_exporter.reset();
        }
        else
        {
            rt.video_frame_pixels.resize(static_cast<size_t>(init_active->width())
                                         * init_active->height() * 4);
        }
        if (!config_.headless)
        {
            config_.headless = true;
        }
    }
#endif

    // ── Platform window bootstrap (shared by GLFW and SDL3) ────────────────
    // Extracted from duplicated per-adapter blocks to eliminate the
    // near-identical GLFW/SDL3 startup code.  Each adapter creates its
    // native window, then calls this lambda for the common window-manager
    // and multi-window creation logic.
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    auto bootstrap_platform_windows = [&](void* native_window, bool warmup_preview)
    {
        backend_->create_surface(native_window);
        backend_->create_swapchain(init_w, init_h);

        // WindowManager requires VulkanBackend — skip for other backends.
        if (config_.backend != RenderBackend::Vulkan)
            return;

        WindowManagerBootstrapOptions wm_opts;
        wm_opts.backend                = static_cast<VulkanBackend*>(backend_.get());
        wm_opts.registry               = &registry_;
        wm_opts.renderer               = renderer_.get();
        wm_opts.theme_mgr              = theme_mgr_.get();
        wm_opts.session                = &rt.app_services->session();
        wm_opts.settings_store         = &rt.app_services->settings();
        wm_opts.plugin_manager         = &rt.app_services->plugins();
        wm_opts.export_format_registry = &rt.app_services->export_formats();
        rt.window_mgr                  = create_configured_window_manager(wm_opts);
        rt.window_mgr->set_interactive_frame_handler(
            [&rt]()
            {
                rt.app_services->session().pump_interactive_frame(
                    rt.app_services->scheduler(),
                    rt.app_services->animator(),
                    rt.window_mgr.get(),
                    rt.frame_state);
            });

        std::vector<FigureId> first_group =
            window_groups.empty() ? std::vector<FigureId>{} : window_groups[0];
        auto* initial_wctx =
            rt.window_mgr->create_first_window_with_ui(native_window, first_group);

        if (initial_wctx && initial_wctx->ui_ctx)
        {
            rt.ui_ctx_ptr              = initial_wctx->ui_ctx.get();
            rt.ui_ctx_ptr->glfw_window = initial_wctx->glfw_window;
        }

        if (warmup_preview)
            rt.window_mgr->warmup_preview_window();

        for (size_t gi = 1; gi < window_groups.size(); ++gi)
        {
            auto& group = window_groups[gi];
            if (group.empty())
                continue;

            auto*    fig0 = registry_.get(group[0]);
            uint32_t w    = fig0 ? fig0->width() : 800;
            uint32_t h    = fig0 ? fig0->height() : 600;

            auto* new_wctx =
                rt.window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);

            if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
            {
                for (size_t fi = 1; fi < group.size(); ++fi)
                {
                    new_wctx->ui_ctx->fig_mgr->add_figure(group[fi], FigureState{});
                    new_wctx->assigned_figures.push_back(group[fi]);
                }
            }
        }
    };
#endif

#ifdef SPECTRA_USE_GLFW
    if (!config_.headless)
    {
        rt.glfw = std::make_unique<GlfwAdapter>();
        if (!rt.glfw->init(init_w, init_h, "Spectra"))
        {
            SPECTRA_LOG_ERROR("app", "Failed to create GLFW window");
            rt.glfw.reset();
        }
        else
        {
            bootstrap_platform_windows(rt.glfw->native_window(), true);
        }
    }
#endif

#ifdef SPECTRA_USE_SDL3
    if (!config_.headless)
    {
        rt.sdl3 = std::make_unique<Sdl3Adapter>();
        if (!rt.sdl3->init(init_w, init_h, "Spectra"))
        {
            SPECTRA_LOG_ERROR("app", "Failed to create SDL3 window");
            rt.sdl3.reset();
        }
        else
        {
            bootstrap_platform_windows(rt.sdl3->native_window(), false);
        }
    }
#endif

    if (!rt.ui_ctx_ptr)
    {
        // Headless mode: use the shared builder with `headless = true`
        // for a minimal context (FigureManager + ThemeManager only),
        // avoiding destruction-order issues in rapid create/destroy cycles.
        WindowUIContextBuildOptions headless_opts;
        headless_opts.registry       = &registry_;
        headless_opts.theme_mgr      = theme_mgr_.get();
        headless_opts.mode           = WindowUIContextBuildMode::Headless;
        headless_opts.settings_store = &rt.app_services->settings();
        rt.headless_ui_ctx           = build_window_ui_context(headless_opts);
        rt.ui_ctx_ptr                = rt.headless_ui_ctx.get();
    }

    // ── Inject frontend services into ApplicationServices ──────────────────
    // Create concrete ImGui/GLFW/SDL3 implementations of the framework-neutral
    // service interfaces and inject them so commands and automation can
    // interact with the frontend without knowing which UI framework is active.
    // In headless mode, the null implementations from frontend_services.hpp
    // are used (no injection needed since ApplicationServices defaults to null).
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (!config_.headless && rt.window_mgr)
    {
        rt.dialog_service_impl    = std::make_unique<ImGuiDialogService>();
        rt.clipboard_service_impl = std::make_unique<ImGuiClipboardService>();
        rt.redraw_request_impl    =
            std::make_unique<ImGuiRedrawRequest>(rt.app_services->session());
        rt.window_service_impl    =
            std::make_unique<ImGuiWindowService>(*rt.window_mgr);

        rt.app_services->set_dialog_service(rt.dialog_service_impl.get());
        rt.app_services->set_clipboard_service(rt.clipboard_service_impl.get());
        rt.app_services->set_redraw_request(rt.redraw_request_impl.get());
        rt.app_services->set_window_service(rt.window_service_impl.get());
    }
#endif

    // Wire plugin host services to ApplicationServices process-scoped registries.
    // Plugins use the framework-neutral ApplicationServices registries (which
    // will survive the Qt migration) rather than the per-window UI context's
    // ImGui-specific registries.  The per-window UI context's registries remain
    // for the ImGui UI's own command/shortcut/undo handling.
    auto& plugins = rt.app_services->plugins();
    plugins.set_command_registry(&rt.app_services->commands());
    plugins.set_shortcut_manager(&rt.app_services->shortcuts());
    plugins.set_undo_manager(&rt.app_services->undo());
    plugins.set_transform_registry(&TransformRegistry::instance());
    plugins.set_export_format_registry(&rt.app_services->export_formats());
    plugins.set_data_source_registry(&rt.app_services->data_sources());
    plugins.set_series_type_registry(&rt.app_services->series_types());
    plugins.set_backend(backend_.get());
    renderer_->set_series_type_registry(&rt.app_services->series_types());
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (rt.window_mgr)
        plugins.set_overlay_registry(&rt.window_mgr->overlay_registry());
#endif

    rt.ui_ctx_ptr->plugin_manager = &plugins;
#ifdef SPECTRA_USE_IMGUI
    if (rt.ui_ctx_ptr->imgui_ui)
    {
        rt.ui_ctx_ptr->imgui_ui->set_plugin_manager(&plugins);
        rt.ui_ctx_ptr->imgui_ui->set_export_format_registry(&rt.app_services->export_formats());
    }
#endif

#ifdef SPECTRA_USE_IMGUI
    if (knob_manager_ && !knob_manager_->empty() && rt.ui_ctx_ptr->imgui_ui)
    {
        rt.ui_ctx_ptr->imgui_ui->set_knob_manager(knob_manager_);
    }

    if (rt.ui_ctx_ptr && rt.ui_ctx_ptr->fig_mgr)
    {
        WindowUIContextRuntimeWireOptions wire_opts;
        wire_opts.ui_ctx                       = rt.ui_ctx_ptr;
        wire_opts.registry                     = &registry_;
        wire_opts.session                      = &rt.app_services->session();
        wire_opts.active_figure                = init_active;
        wire_opts.has_animation                = rt.frame_state.has_animation;
        wire_opts.tab_split_mode               = TabSplitMode::SplitPane;
        wire_opts.tab_drag_already_wired       = (rt.window_mgr != nullptr);
        wire_opts.wire_demo_animation_channels = (init_active != nullptr);
        wire_opts.enable_window_tab_callbacks  = !config_.headless;
    #if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        wire_opts.window_manager = rt.window_mgr.get();
    #endif
        wire_window_ui_runtime(wire_opts);
    }

    if (!config_.headless && rt.ui_ctx_ptr && rt.ui_ctx_ptr->imgui_ui)
    {
        auto& fig_mgr  = *rt.ui_ctx_ptr->fig_mgr;
        auto& imgui_ui = rt.ui_ctx_ptr->imgui_ui;
        imgui_ui->set_csv_plot_callback(
            [&fig_mgr, this](const std::string& /*path*/,
                             const std::vector<float>& x,
                             const std::vector<float>& y,
                             const std::string& /* x_label */,
                             const std::string& y_label,
                             const std::vector<float>* /*z*/,
                             const std::string* /*z_label*/,
                             double x_offset)
            {
                FigureId active_id = fig_mgr.active_index();
                Figure*  fig       = registry_.get(active_id);
                if (!fig)
                {
                    active_id = fig_mgr.create_figure(FigureConfig{});
                    fig       = registry_.get(active_id);
                    if (!fig)
                        return;
                }

                auto& ax   = fig->subplot(1, 1, 1);
                auto& line = ax.line(x, y);
                line.label(y_label);
                // Preserve absolute x values (e.g. epoch timestamps): the
                // axis shows x_offset + x[i] at full double precision.
                line.x_offset(x_offset);
                ax.auto_fit();
            });

        // Wire OS drag-and-drop: CSV/TSV/TXT files trigger the Load from CSV flow.
        auto* imgui_ui_ptr = imgui_ui.get();
        if (rt.window_mgr)
        {
            rt.window_mgr->set_file_drop_handler(
                [imgui_ui_ptr](uint32_t /*window_id*/, const std::string& path)
                {
                    namespace fs = std::filesystem;
                    auto ext     = fs::path(path).extension().string();
                    std::transform(ext.begin(),
                                   ext.end(),
                                   ext.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (ext == ".csv" || ext == ".tsv" || ext == ".txt")
                        imgui_ui_ptr->request_csv_load(path);
                });
        }
    }
#endif

    if (config_.headless)
    {
        backend_->create_offscreen_framebuffer(init_w, init_h);
        if (config_.backend == RenderBackend::Vulkan)
            static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();
    }

    rt.app_services->scheduler().reset();

    rt.last_step_time = std::chrono::steady_clock::now();

    // Start automation server for MCP-driven testing.
    // In headless mode, skip unless explicitly requested via environment variable
    // to avoid thread lifecycle issues during rapid create/destroy cycles in tests.
    {
        const char* auto_env = std::getenv("SPECTRA_AUTO_SOCKET");
        bool want_automation = !config_.headless || ((auto_env != nullptr) && auto_env[0] != '\0');

        if (want_automation)
        {
            std::string auto_sock;
            if (auto_env && auto_env[0] != '\0')
                auto_sock = auto_env;
            else
                auto_sock = AutomationServer::default_socket_path();

            std::string mcp_bind = "127.0.0.1";
            if (const char* mcp_bind_env = std::getenv("SPECTRA_MCP_BIND"))
            {
                if (mcp_bind_env[0] != '\0')
                    mcp_bind = mcp_bind_env;
            }

            uint16_t mcp_port        = 8765;
            bool     mcp_port_pinned = false;
            if (const char* mcp_port_env = std::getenv("SPECTRA_MCP_PORT"))
            {
                if (mcp_port_env[0] != '\0')
                {
                    const long parsed_port = std::strtol(mcp_port_env, nullptr, 10);
                    if (parsed_port > 0 && parsed_port <= 65535)
                    {
                        mcp_port        = static_cast<uint16_t>(parsed_port);
                        mcp_port_pinned = true;
                    }
                }
            }

            // Use ApplicationServices::start_automation which handles port probing.
            // We need to pass the pinned port or 0 for auto-probe.
            if (rt.app_services->start_automation(
                    auto_sock, mcp_bind,
                    mcp_port_pinned ? mcp_port : 0))
            {
                SPECTRA_LOG_INFO("app", "Automation server started: " + auto_sock);
                if (rt.app_services->mcp())
                    SPECTRA_LOG_INFO("app", "MCP server started: " + rt.app_services->mcp()->endpoint());
            }
            else
            {
                SPECTRA_LOG_WARN("app", "Automation server failed to start");
            }
        }
    }

    // ── InprocTopicServer ─────────────────────────────────────────────────
    // Start a publisher-facing socket server so external publishers can push
    // topics into this inproc app without a separate spectra-backend daemon.
    // Only in windowed (non-headless) mode to avoid socket leaks in test runs.
#ifndef _WIN32
    if (!config_.headless)
    {
        rt.app_services->start_topic_server(registry_);
#ifdef SPECTRA_USE_IMGUI
        if (rt.ui_ctx_ptr && rt.app_services->topic_server())
            rt.app_services->topic_server()->wire_topics_panel(rt.ui_ctx_ptr->topics_panel, &registry_);
#endif
    }
#endif

    PerfMetrics::instance().mark_startup_end();
    SPECTRA_LOG_INFO("app",
                     "Startup completed in "
                         + std::to_string(PerfMetrics::instance().startup_total_us() / 1000.0)
                         + " ms");
}

// ─── step ────────────────────────────────────────────────────────────────────
App::StepResult App::step()
{
    StepResult result;
    if (!runtime_)
    {
        result.should_exit = true;
        return result;
    }

    auto& rt = *runtime_;

    auto step_start = std::chrono::steady_clock::now();

    // Poll automation server for pending remote commands
    if (rt.app_services->automation())
    {
        rt.app_services->automation()->poll(*this, rt.ui_ctx_ptr);
        // Automation commands will have been drained into cmd_queue;
        // the tick() will mark dirty when it processes them.
    }

    rt.app_services->session().tick(rt.app_services->scheduler(),
                    rt.app_services->animator(),
                    rt.app_services->command_queue(),
                    config_.headless,
                    rt.ui_ctx_ptr,
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
                    rt.window_mgr.get(),
#endif
                    rt.frame_state);
    rt.active_figure = rt.frame_state.active_figure;
    if (rt.active_figure && !registry_.get(rt.frame_state.active_figure_id))
    {
        rt.active_figure                = nullptr;
        rt.frame_state.active_figure    = nullptr;
        rt.frame_state.active_figure_id = INVALID_FIGURE_ID;
    }

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter && rt.video_exporter->is_open() && rt.active_figure)
    {
        if (backend_->readback_framebuffer(rt.video_frame_pixels.data(),
                                           rt.active_figure->width(),
                                           rt.active_figure->height()))
        {
            rt.video_exporter->write_frame(rt.video_frame_pixels.data());
        }
    }
#endif

    // Phase 1: Write PNG from a previously completed pre-present capture.
    if (rt.pending_png_capture.active)
    {
        auto& cap = rt.pending_png_capture;
        auto* vk  = (config_.backend == RenderBackend::Vulkan)
                        ? static_cast<VulkanBackend*>(backend_.get())
                        : nullptr;
        if (cap.awaiting_gpu_capture && vk && vk->has_pending_capture())
        {
            // GPU readback scheduled last frame is still in flight.
        }
        else
        {
            cap.awaiting_gpu_capture = false;
            if (cap.copy_to_clipboard)
            {
                auto png =
                    ImageExporter::write_png_to_memory(cap.pixels.data(), cap.width, cap.height);
                if (!png.empty())
                {
                    if (platform::copy_image_to_clipboard(png.data(), png.size()))
                        SPECTRA_LOG_INFO("export", "Figure copied to clipboard");
                    else
                        SPECTRA_LOG_WARN("export", "Clipboard image copy failed");
                }
            }
            else if (!cap.path.empty())
            {
                if (ImageExporter::write_png(cap.path, cap.pixels.data(), cap.width, cap.height))
                {
                    SPECTRA_LOG_INFO("export", "Saved PNG: " + cap.path);
                }
                else
                {
                    SPECTRA_LOG_ERROR("export", "Failed to write PNG: " + cap.path);
                }
            }
            cap.active            = false;
            cap.copy_to_clipboard = false;
            cap.pixels.clear();
            cap.path.clear();
        }
    }

    // Phase 2: Schedule a pre-present capture for figures with pending export.
    // The capture will execute inside end_frame() (do_capture_before_present),
    // before vkQueuePresentKHR, so the swapchain image contents are valid.
    if (!config_.headless && rt.active_figure
        && (!rt.active_figure->export_req_.png_path.empty()
            || rt.active_figure->export_req_.copy_to_clipboard)
        && !rt.pending_png_capture.active)
    {
        uint32_t ew = rt.active_figure->export_req_.png_width > 0
                          ? rt.active_figure->export_req_.png_width
                          : rt.active_figure->width();
        uint32_t eh = rt.active_figure->export_req_.png_height > 0
                          ? rt.active_figure->export_req_.png_height
                          : rt.active_figure->height();
        if (ew == 0 || eh == 0)
        {
            rt.active_figure->export_req_.png_path.clear();
            rt.active_figure->export_req_.png_width         = 0;
            rt.active_figure->export_req_.png_height        = 0;
            rt.active_figure->export_req_.copy_to_clipboard = false;
        }
        else
        {
            auto& cap = rt.pending_png_capture;
            cap.pixels.resize(static_cast<size_t>(ew) * eh * 4);
            cap.path                 = rt.active_figure->export_req_.png_path;
            cap.width                = ew;
            cap.height               = eh;
            cap.active               = true;
            cap.copy_to_clipboard    = rt.active_figure->export_req_.copy_to_clipboard;
            cap.awaiting_gpu_capture = true;

            auto* vk = (config_.backend == RenderBackend::Vulkan)
                           ? static_cast<VulkanBackend*>(backend_.get())
                           : nullptr;
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
            // Target the window that owns this figure so multi-window captures
            // read the correct swapchain image.
            WindowContext* target_wctx = nullptr;
            if (rt.window_mgr)
            {
                const FigureId export_id = rt.frame_state.active_figure_id;
                for (auto* wctx : rt.window_mgr->windows())
                {
                    const auto& figs = wctx->assigned_figures;
                    if (std::find(figs.begin(), figs.end(), export_id) != figs.end())
                    {
                        target_wctx = wctx;
                        break;
                    }
                }
            }
            if (target_wctx)
                vk->request_framebuffer_capture(cap.pixels.data(), ew, eh, target_wctx);
            else
#endif
                if (vk)
                vk->request_framebuffer_capture(cap.pixels.data(), ew, eh);

            rt.active_figure->export_req_.png_path.clear();
            rt.active_figure->export_req_.png_width         = 0;
            rt.active_figure->export_req_.png_height        = 0;
            rt.active_figure->export_req_.copy_to_clipboard = false;
        }
    }

    // Check animation duration termination
    if (rt.active_figure && rt.active_figure->anim_.duration > 0.0f
        && rt.app_services->scheduler().elapsed_seconds() >= rt.active_figure->anim_.duration
        && !rt.active_figure->anim_.loop)
    {
        rt.app_services->session().request_exit();
    }

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (!rt.window_mgr)
    {
    #ifdef SPECTRA_USE_GLFW
        if (rt.glfw)
        {
            rt.glfw->poll_events();
            if (rt.glfw->should_close())
            {
                SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
                rt.app_services->session().request_exit();
            }
        }
    #elif defined(SPECTRA_USE_SDL3)
        if (rt.sdl3)
        {
            // Process events; should_close is set by process_sdl3_events via SDL_EVENT_QUIT.
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_EVENT_QUIT)
                {
                    SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
                    rt.app_services->session().request_exit();
                }
            }
        }
    #endif
    }
#endif

    rt.frame_number++;
    PerfMetrics::instance().increment_frame_count();

    auto  step_end = std::chrono::steady_clock::now();
    float ms       = std::chrono::duration<float, std::milli>(step_end - step_start).count();

    result.should_exit   = rt.app_services->session().should_exit();
    result.frame_time_ms = ms;
    result.frame_number  = rt.frame_number;

    return result;
}

// ─── shutdown_runtime ────────────────────────────────────────────────────────
void App::shutdown_runtime()
{
    if (!runtime_)
        return;

    auto& rt = *runtime_;

    SPECTRA_LOG_INFO("main_loop", "Exited main render loop");

    // ApplicationServices::shutdown() stops automation, MCP, and topic server.

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter)
    {
        rt.video_exporter->finish();
        rt.video_exporter.reset();
    }
#endif

    // Process exports for all figures (headless batch mode)
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        auto& f = *fig_ptr;

        if (config_.headless && !f.export_req_.png_path.empty())
        {
            uint32_t export_w = f.export_req_.png_width > 0 ? f.export_req_.png_width : f.width();
            uint32_t export_h =
                f.export_req_.png_height > 0 ? f.export_req_.png_height : f.height();

            bool needs_render =
                (&f != rt.active_figure) || (export_w != f.width()) || (export_h != f.height());

            if (needs_render)
            {
                backend_->create_offscreen_framebuffer(export_w, export_h);
                if (config_.backend == RenderBackend::Vulkan)
                    static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

                uint32_t orig_w  = f.config_.width;
                uint32_t orig_h  = f.config_.height;
                f.config_.width  = export_w;
                f.config_.height = export_h;
                f.compute_layout();

                if (backend_->begin_frame())
                {
                    renderer_->render_figure(f);
                    backend_->end_frame();
                }

                f.config_.width  = orig_w;
                f.config_.height = orig_h;
                f.compute_layout();
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
            if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
            {
                if (!ImageExporter::write_png(f.export_req_.png_path,
                                              pixels.data(),
                                              export_w,
                                              export_h))
                {
                    SPECTRA_LOG_ERROR("app", "Failed to write PNG: {}", f.export_req_.png_path);
                }
            }
            else
            {
                SPECTRA_LOG_ERROR("app", "Failed to readback framebuffer");
            }
        }

        if (!f.export_req_.svg_path.empty())
        {
            f.compute_layout();
            if (!SvgExporter::write_svg(f.export_req_.svg_path, f))
            {
                SPECTRA_LOG_ERROR("app", "Failed to write SVG: {}", f.export_req_.svg_path);
            }
        }
    }

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Clear frontend service pointers before destroying the window manager,
    // since the service implementations reference the window manager and session.
    if (rt.app_services)
    {
        rt.app_services->set_dialog_service(nullptr);
        rt.app_services->set_clipboard_service(nullptr);
        rt.app_services->set_redraw_request(nullptr);
        rt.app_services->set_window_service(nullptr);
    }

    if (rt.window_mgr)
    {
    #ifdef SPECTRA_USE_GLFW
        if (rt.glfw)
            rt.glfw->release_window();
    #elif defined(SPECTRA_USE_SDL3)
        if (rt.sdl3)
            rt.sdl3->release_window();
    #endif
        rt.window_mgr->shutdown();
        rt.window_mgr.reset();
    }
#endif

    if (backend_)
    {
        backend_->wait_idle();
    }

    // Renderer holds a non-owning pointer into ApplicationServices::series_types().
    // Destroy custom pipelines while both are still alive, then detach so
    // ~Renderer (in ~App) does not touch freed registry memory.
    if (renderer_ && backend_)
    {
        rt.app_services->series_types().destroy_pipelines(*backend_);
        renderer_->set_series_type_registry(nullptr);
    }
    rt.app_services->plugins().set_series_type_registry(nullptr);

    // Shutdown ApplicationServices (stops automation, topic server, etc.)
    if (rt.app_services)
    {
        rt.app_services->shutdown();
        rt.app_services.reset();
    }

    try
    {
        runtime_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown",
                          std::string("Exception during AppRuntime destruction: ") + e.what());
        // Force-null the pointer to avoid double-free in ~App()
        runtime_.reset();
    }
    catch (...)
    {
        runtime_.reset();
    }
}

// ─── Accessors ───────────────────────────────────────────────────────────────
WindowUIContext* App::ui_context()
{
    if (!runtime_)
        return nullptr;

    auto& rt = *runtime_;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    if (rt.window_mgr)
    {
        for (auto* wctx : rt.window_mgr->windows())
        {
            if (wctx && wctx->ui_ctx)
            {
                rt.ui_ctx_ptr = wctx->ui_ctx.get();
                return rt.ui_ctx_ptr;
            }
        }

        // All windows were closed/destroyed this frame.
        rt.ui_ctx_ptr = nullptr;
        return nullptr;
    }
#endif

    if (rt.headless_ui_ctx)
        rt.ui_ctx_ptr = rt.headless_ui_ctx.get();

    return rt.ui_ctx_ptr;
}

SessionRuntime* App::session()
{
    return runtime_ && runtime_->app_services ? &runtime_->app_services->session() : nullptr;
}

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
WindowManager* App::window_manager()
{
    return runtime_ ? runtime_->window_mgr.get() : nullptr;
}
#endif

ApplicationServices* App::app_services()
{
    return runtime_ ? runtime_->app_services.get() : nullptr;
}

}   // namespace spectra
