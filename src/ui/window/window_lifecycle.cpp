// Window lifecycle: creation, destruction, shutdown, UI initialization.
// Split from window_manager.cpp — see QW-5 in ARCHITECTURE_REVIEW_V2.md.

#include "window_manager.hpp"

#include <spectra/event_bus.hpp>
#include <spectra/logger.hpp>

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context_builder.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/theme/theme.hpp"
#include "io/export_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "glfw_utils.hpp"
    #include "platform/window_system/win32_glfw_hook.hpp"
#endif

#ifdef SPECTRA_USE_SDL3
    #include <SDL3/SDL.h>
    #include <SDL3/SDL_vulkan.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #ifdef SPECTRA_USE_GLFW
        #include <imgui_impl_glfw.h>
    #elif defined(SPECTRA_USE_SDL3)
        #include <imgui_impl_sdl3.h>
    #endif

    #include "ui/app/register_commands.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include <spectra/figure_registry.hpp>
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/figures/tab_bar.hpp"
#endif

#include <algorithm>

namespace spectra
{

WindowManager::~WindowManager()
{
    shutdown();
}

void WindowManager::init(VulkanBackend*    backend,
                         FigureRegistry*   registry,
                         Renderer*         renderer,
                         ui::ThemeManager* theme_mgr)
{
    backend_   = backend;
    registry_  = registry;
    renderer_  = renderer;
    theme_mgr_ = theme_mgr;
}

WindowContext* WindowManager::create_initial_window(void* glfw_window)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_initial_window: not initialized");
        return nullptr;
    }

    // Take ownership of the backend's initial WindowContext (already has
    // surface + swapchain initialized by the App init path).
    auto wctx = backend_->release_initial_window();
    if (!wctx)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_initial_window: no initial window to take");
        return nullptr;
    }

    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_window;
    wctx->glfw_window   = glfw_window;
    wctx->is_focused    = true;

#ifdef SPECTRA_USE_GLFW
    // Set user pointer so WindowManager callbacks can find the manager.
    // Actual callbacks are installed later by install_input_callbacks(),
    // which must run AFTER ImGui init to avoid breaking ImGui's callback
    // chaining (ImGui saves "previous" callbacks during init).
    auto* glfw_win = static_cast<GLFWwindow*>(glfw_window);
    if (glfw_win)
    {
        glfwSetWindowUserPointer(glfw_win, this);
    }
#endif

    // Set active window so the backend can continue operating
    backend_->set_active_window(wctx.get());

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created initial window (id=" + std::to_string(ptr->id) + ")");
    if (event_system_)
        event_system_->window_opened().emit({ptr->id});
    return ptr;
}

WindowContext* WindowManager::create_window(uint32_t           width,
                                            uint32_t           height,
                                            const std::string& title)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: not initialized");
        return nullptr;
    }

    if (backend_->is_headless())
    {
        SPECTRA_LOG_WARN("window_manager",
                         "create_window: cannot create OS windows in headless mode");
        return nullptr;
    }

#ifdef SPECTRA_USE_GLFW
    // Create GLFW window (shared context not needed — Vulkan doesn't use GL contexts)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    set_wayland_app_id();
    GLFWwindow* glfw_win = glfwCreateWindow(static_cast<int>(width),
                                            static_cast<int>(height),
                                            title.c_str(),
                                            nullptr,
                                            nullptr);
    if (!glfw_win)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window: glfwCreateWindow failed");
        return nullptr;
    }

    // Create WindowContext
    auto wctx           = std::make_unique<WindowContext>();
    wctx->id            = next_window_id_++;
    wctx->native_window = glfw_win;
    wctx->glfw_window   = glfw_win;

    // Initialize Vulkan resources (surface, swapchain, cmd buffers, sync)
    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_window: Vulkan resource init failed for window " + std::to_string(wctx->id));
        glfwDestroyWindow(glfw_win);
        return nullptr;
    }

    set_window_icon(glfw_win);

    glfwSetWindowUserPointer(glfw_win, this);
    install_glfw_lifecycle_callbacks(glfw_win);
    glfwSetDropCallback(glfw_win, glfw_drop_callback);

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Created window " + std::to_string(ptr->id) + ": " + std::to_string(width)
                          + "x" + std::to_string(height) + " \"" + title + "\"");
    if (event_system_)
        event_system_->window_opened().emit({ptr->id});
    return ptr;
#elif defined(SPECTRA_USE_SDL3)
    auto* sdl_win = SDL_CreateWindow(title.c_str(),
                                     static_cast<int>(width),
                                     static_cast<int>(height),
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!sdl_win)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          std::string("create_window: SDL_CreateWindow failed: ") + SDL_GetError());
        return nullptr;
    }

    auto wctx           = std::make_unique<WindowContext>();
    wctx->id            = next_window_id_++;
    wctx->native_window = sdl_win;
    wctx->glfw_window   = sdl_win;

    if (!backend_->init_window_context(*wctx, width, height))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_window: Vulkan resource init failed for window " + std::to_string(wctx->id));
        SDL_DestroyWindow(sdl_win);
        return nullptr;
    }

    WindowContext* ptr = wctx.get();
    windows_.push_back(std::move(wctx));
    rebuild_active_list();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Created SDL3 window " + std::to_string(ptr->id) + ": "
                          + std::to_string(width) + "x" + std::to_string(height) + " \"" + title
                          + "\"");
    if (event_system_)
        event_system_->window_opened().emit({ptr->id});
    return ptr;
#else
    (void)width;
    (void)height;
    (void)title;
    SPECTRA_LOG_ERROR("window_manager", "create_window: no windowing backend available");
    return nullptr;
#endif
}

void WindowManager::request_close(uint32_t window_id)
{
    pending_close_ids_.push_back(window_id);
}

void WindowManager::destroy_window(uint32_t window_id)
{
    // Find the window in our managed list
    auto it = std::find_if(windows_.begin(),
                           windows_.end(),
                           [window_id](const auto& w) { return w->id == window_id; });
    if (it == windows_.end())
        return;

    auto& wctx = **it;

    // Window close policy: destroy all figures owned by this window.
    // Closing a window (or its last tab) kills the figures — they do NOT
    // migrate to other windows.
    if (registry_ && !wctx.assigned_figures.empty())
    {
#ifdef SPECTRA_USE_IMGUI
        if (wctx.ui_ctx && wctx.ui_ctx->fig_mgr)
        {
            auto figs_copy = wctx.assigned_figures;
            for (FigureId fig_id : figs_copy)
                wctx.ui_ctx->fig_mgr->remove_figure(fig_id);
        }
#endif
        for (FigureId fig_id : wctx.assigned_figures)
        {
            if (figure_unregistering_handler_)
            {
                if (Figure* fig = registry_->get(fig_id))
                    figure_unregistering_handler_(fig_id, fig);
            }
            registry_->unregister_figure(fig_id);
            SPECTRA_LOG_INFO("window_manager",
                             "Destroyed figure " + std::to_string(fig_id) + " (window "
                                 + std::to_string(window_id) + " closed)");
        }
        wctx.assigned_figures.clear();
    }

    // Wait for all GPU work to complete before destroying any resources.
    // Without this, ImGui shutdown frees descriptor sets / pipelines / buffers
    // that are still referenced by in-flight command buffers.
    if (backend_->device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(backend_->device());

    // Destroy UI context first (shuts down ImGui before Vulkan resources)
    if (wctx.ui_ctx)
    {
#ifdef SPECTRA_USE_IMGUI
        if (wctx.ui_ctx->imgui_ui)
        {
            auto* prev_active = backend_->active_window();
            backend_->set_active_window(&wctx);
            wctx.ui_ctx->imgui_ui->shutdown();
            // Restore previous active window, but NOT if it was the window
            // we are destroying — that would leave a dangling pointer after
            // windows_.erase(it) frees the WindowContext.
            if (prev_active != &wctx)
                backend_->set_active_window(prev_active);
            else
                backend_->set_active_window(nullptr);
        }
        // ImGuiIntegration::shutdown() already destroyed the ImGui context.
        // Null it out so destroy_window_context() doesn't double-shutdown.
        wctx.imgui_context = nullptr;
#endif
        wctx.ui_ctx.reset();
    }

    // Destroy Vulkan resources
    backend_->destroy_window_context(wctx);

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    // Destroy native window
    if (wctx.glfw_window)
    {
    #ifdef SPECTRA_USE_GLFW
        remove_win32_interactive_hook(static_cast<GLFWwindow*>(wctx.glfw_window));
        glfwDestroyWindow(static_cast<GLFWwindow*>(wctx.glfw_window));
    #elif defined(SPECTRA_USE_SDL3)
        SDL_DestroyWindow(static_cast<SDL_Window*>(wctx.glfw_window));
    #endif
        wctx.native_window = nullptr;
        wctx.glfw_window   = nullptr;
    }
#endif

    SPECTRA_LOG_INFO("window_manager", "Destroyed window " + std::to_string(window_id));

    if (event_system_)
        event_system_->window_closed().emit({window_id});

    windows_.erase(it);
    rebuild_active_list();
}

void WindowManager::process_pending_closes()
{
    // Check GLFW should_close flags on all windows
#ifdef SPECTRA_USE_GLFW
    for (auto& wctx : windows_)
    {
        if (wctx->glfw_window && !wctx->should_close)
        {
            if (glfwWindowShouldClose(static_cast<GLFWwindow*>(wctx->glfw_window)))
            {
                wctx->should_close = true;
                pending_close_ids_.push_back(wctx->id);
            }
        }
    }
#endif

    // Process deferred close requests
    if (pending_close_ids_.empty())
        return;

    // Copy and clear to avoid re-entrancy issues
    auto ids = std::move(pending_close_ids_);
    pending_close_ids_.clear();

    for (uint32_t id : ids)
    {
        destroy_window(id);
    }
}

void WindowManager::shutdown()
{
    if (!backend_)
        return;

    // Wait for all GPU work to complete before destroying any window resources.
    if (backend_->device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(backend_->device());

    // Destroy all windows (reverse order)
    while (!windows_.empty())
    {
        auto& wctx = *windows_.back();

        // Destroy UI context first (shuts down ImGui before Vulkan resources)
        if (wctx.ui_ctx)
        {
#ifdef SPECTRA_USE_IMGUI
            if (wctx.ui_ctx->imgui_ui)
            {
                auto* prev_active = backend_->active_window();
                backend_->set_active_window(&wctx);
                wctx.ui_ctx->imgui_ui->shutdown();
                backend_->set_active_window(prev_active);
            }
            // ImGuiIntegration::shutdown() already destroyed the ImGui context.
            // Null it out so destroy_window_context() doesn't double-shutdown.
            wctx.imgui_context = nullptr;
#endif
            wctx.ui_ctx.reset();
        }

        backend_->destroy_window_context(wctx);

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        if (wctx.glfw_window)
        {
    #ifdef SPECTRA_USE_GLFW
            remove_win32_interactive_hook(static_cast<GLFWwindow*>(wctx.glfw_window));
            glfwDestroyWindow(static_cast<GLFWwindow*>(wctx.glfw_window));
    #elif defined(SPECTRA_USE_SDL3)
            SDL_DestroyWindow(static_cast<SDL_Window*>(wctx.glfw_window));
    #endif
            wctx.native_window = nullptr;
            wctx.glfw_window   = nullptr;
        }
#endif

        windows_.pop_back();
    }

    active_ptrs_.clear();
    pending_close_ids_.clear();

    // Destroy pooled preview window if it exists
    if (pooled_preview_)
    {
        if (pooled_preview_->ui_ctx)
        {
#ifdef SPECTRA_USE_IMGUI
            if (pooled_preview_->ui_ctx->imgui_ui)
            {
                auto* prev_active = backend_->active_window();
                backend_->set_active_window(pooled_preview_.get());
                pooled_preview_->ui_ctx->imgui_ui->shutdown();
                backend_->set_active_window(prev_active);
            }
            pooled_preview_->imgui_context = nullptr;
#endif
            pooled_preview_->ui_ctx.reset();
        }
        backend_->destroy_window_context(*pooled_preview_);
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
        if (pooled_preview_->glfw_window)
        {
    #ifdef SPECTRA_USE_GLFW
            remove_win32_interactive_hook(static_cast<GLFWwindow*>(pooled_preview_->glfw_window));
            glfwDestroyWindow(static_cast<GLFWwindow*>(pooled_preview_->glfw_window));
    #elif defined(SPECTRA_USE_SDL3)
            SDL_DestroyWindow(static_cast<SDL_Window*>(pooled_preview_->glfw_window));
    #endif
            pooled_preview_->native_window = nullptr;
            pooled_preview_->glfw_window   = nullptr;
        }
#endif
        pooled_preview_.reset();
    }

    // Null active_window_ before backend_->shutdown() runs (via destructor or
    // explicit call). All WindowContext objects owned by windows_ are now
    // destroyed — active_window_ is a dangling pointer. Clearing it prevents
    // VulkanBackend::shutdown() from accessing freed memory and double-freeing
    // semaphores/fences that destroy_window_context() already cleaned up.
    backend_->set_active_window(nullptr);

    // Mark as shut down so destructor and repeated calls are no-ops.
    backend_ = nullptr;

    SPECTRA_LOG_INFO("window_manager", "Shutdown complete");
}

// ── WindowUIContext assembly helpers ─────────────────────────────────────────

WindowUIContextBuildOptions WindowManager::make_ui_build_options(WindowContext& wctx,
                                                                 FigureId       initial_figure_id)
{
    WindowUIContextBuildOptions options;
    options.registry                = registry_;
    options.theme_mgr               = theme_mgr_;
    options.initial_figure_id       = initial_figure_id;
    options.plugin_manager          = plugin_manager_;
    options.export_format_registry  = export_format_registry_;
    options.overlay_registry        = &shared_overlay_registry_;
    options.series_clipboard        = &shared_clipboard_;
    options.session                 = session_;
    options.settings_store          = settings_store_;
    options.on_window_close_request = [this, window_id = wctx.id]() { request_close(window_id); };
    options.on_figure_closed        = [this](FigureId, Figure* fig)
    {
        if (fig)
            clear_figure_caches(fig);
    };
    options.create_imgui_integration = true;
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    options.window_manager = this;
    options.window_id      = wctx.id;
#endif
    return options;
}

void WindowManager::wire_tab_drag_handlers(WindowUIContext& ui)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ui.fig_mgr)
        return;

    auto* fig_mgr_ptr = ui.fig_mgr;

    if (tab_detach_handler_)
    {
        auto* reg     = registry_;
        auto  handler = tab_detach_handler_;
        ui.tab_drag_controller.set_on_drop_outside(
            [fig_mgr_ptr, reg, handler](FigureId fid, float sx, float sy)
            {
                auto* fig = reg->get(fid);
                if (!fig)
                    return;
                uint32_t    w     = fig->width() > 0 ? fig->width() : 800;
                uint32_t    h     = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr_ptr->get_title(fid);
                handler(fid, w, h, title, static_cast<int>(sx), static_cast<int>(sy));
            });
    }
    if (tab_move_handler_)
    {
        auto  handler = tab_move_handler_;
        auto* wm      = this;
        ui.tab_drag_controller.set_on_drop_on_window(
            [handler, wm](FigureId fid, uint32_t target_wid, float /*sx*/, float /*sy*/)
            {
                auto info = wm->cross_window_drop_info();
                handler(fid, target_wid, info.zone, info.hx, info.hy, info.target_figure_id);
            });
    }
#endif
}

bool WindowManager::init_minimal_window_imgui(WindowContext& wctx)
{
#ifdef SPECTRA_USE_IMGUI
    if (!backend_ || !theme_mgr_)
        return false;

    WindowUIContextBuildOptions options;
    options.theme_mgr = theme_mgr_;
    options.mode      = WindowUIContextBuildMode::ImGuiOnly;

    auto ui = build_window_ui_context(options);
    if (!ui || !ui->imgui_ui)
        return false;

    if (!init_window_imgui_integration(*backend_, wctx, *ui->imgui_ui, false))
        return false;

    wctx.ui_ctx = std::move(ui);
    return true;
#else
    (void)wctx;
    return false;
#endif
}

// ── Panel windows ───────────────────────────────────────────────────────────

WindowContext* WindowManager::create_panel_window(uint32_t              width,
                                                  uint32_t              height,
                                                  const std::string&    title,
                                                  std::function<void()> draw_callback,
                                                  int                   screen_x,
                                                  int                   screen_y)
{
    if (!backend_ || backend_->is_headless())
        return nullptr;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    #ifdef SPECTRA_USE_GLFW
    // Create an undecorated, resizable window with custom title bar.
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    #endif
    auto* wctx = create_window(width, height, title);
    #ifdef SPECTRA_USE_GLFW
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);   // Reset for future windows
    #endif
    if (!wctx)
        return nullptr;

    set_window_position(*wctx, screen_x, screen_y);
    wctx->title               = title;
    wctx->is_panel            = true;
    wctx->panel_draw_callback = std::move(draw_callback);

    // Install full input callbacks (cursor, mouse, key, scroll).
    #ifdef SPECTRA_USE_GLFW
    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    if (glfw_win)
    {
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
        glfwSetDropCallback(glfw_win, glfw_drop_callback);
    }
    #endif
    // SDL3: events are handled centrally via process_sdl3_events()

    if (!init_minimal_window_imgui(*wctx))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_panel_window: ImGui init failed for window " + std::to_string(wctx->id));
        return nullptr;
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Created panel window " + std::to_string(wctx->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height) + " \"" + title
                         + "\"");
    return wctx;
#else
    (void)width;
    (void)height;
    (void)title;
    (void)draw_callback;
    (void)screen_x;
    (void)screen_y;
    return nullptr;
#endif
}

void WindowManager::destroy_panel_window(uint32_t window_id)
{
    destroy_window(window_id);
}

// ── First window with UI ────────────────────────────────────────────────────

WindowContext* WindowManager::create_first_window_with_ui(void*                        glfw_window,
                                                          const std::vector<FigureId>& figure_ids)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: not initialized");
        return nullptr;
    }

    if (!registry_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: no registry");
        return nullptr;
    }

    // Take ownership of the backend's initial WindowContext (already has
    // surface + swapchain initialized by the App init path).
    auto wctx_ptr = backend_->release_initial_window();
    if (!wctx_ptr)
    {
        SPECTRA_LOG_ERROR("window_manager",
                          "create_first_window_with_ui: no initial window to take");
        return nullptr;
    }

    wctx_ptr->id            = next_window_id_++;
    wctx_ptr->native_window = glfw_window;
    wctx_ptr->glfw_window   = glfw_window;
    wctx_ptr->is_focused    = true;

    // Set figure assignments (all figures go to the first window)
    FigureId active_id              = figure_ids.empty() ? INVALID_FIGURE_ID : figure_ids[0];
    wctx_ptr->assigned_figure_index = active_id;
    wctx_ptr->assigned_figures      = figure_ids;
    wctx_ptr->active_figure_id      = active_id;
    wctx_ptr->title                 = "Spectra";

#ifdef SPECTRA_USE_GLFW
    auto* glfw_win = static_cast<GLFWwindow*>(glfw_window);
    if (glfw_win)
    {
        glfwSetWindowUserPointer(glfw_win, this);
    }
#endif

    // Set active window so the backend can continue operating
    backend_->set_active_window(wctx_ptr.get());

    // Ensure pipelines exist before ImGui init (needs render pass)
    backend_->ensure_pipelines();

    // Initialize the full UI subsystem bundle (ImGui, FigureManager, etc.)
    // This is the SAME path that secondary windows use.
    if (!init_window_ui(*wctx_ptr, active_id))
    {
        SPECTRA_LOG_ERROR("window_manager", "create_first_window_with_ui: UI init failed");
    }

    // For the first window, FigureManager should have ALL figures, not just one.
    // init_window_ui() strips all but the initial figure, so re-add the rest.
#ifdef SPECTRA_USE_IMGUI
    if (wctx_ptr->ui_ctx && wctx_ptr->ui_ctx->fig_mgr && !figure_ids.empty())
    {
        auto* fm = wctx_ptr->ui_ctx->fig_mgr;
        for (size_t i = 1; i < figure_ids.size(); ++i)
        {
            fm->add_figure(figure_ids[i], FigureState{});
        }
        // add_figure() calls switch_to() internally, so after the loop
        // the active figure is the LAST one added.  Switch back to the
        // first figure so the window starts on figure_ids[0].
        fm->switch_to(figure_ids[0]);
        wctx_ptr->active_figure_id = fm->active_index();
    }
#endif

#ifdef SPECTRA_USE_GLFW
    // Install full input callbacks (same as secondary windows)
    install_input_callbacks(*wctx_ptr);
#endif

    WindowContext* raw = wctx_ptr.get();
    windows_.push_back(std::move(wctx_ptr));
    rebuild_active_list();

    SPECTRA_LOG_INFO("window_manager",
                     "Created first window with UI (id=" + std::to_string(raw->id)
                         + ", figures=" + std::to_string(figure_ids.size()) + ")");
    return raw;
}

// ── Secondary window with UI ────────────────────────────────────────────────

WindowContext* WindowManager::create_window_with_ui(uint32_t           width,
                                                    uint32_t           height,
                                                    const std::string& title,
                                                    FigureId           initial_figure_id,
                                                    int                screen_x,
                                                    int                screen_y)
{
    if (!backend_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window_with_ui: not initialized");
        return nullptr;
    }

    if (!registry_)
    {
        SPECTRA_LOG_ERROR("window_manager", "create_window_with_ui: no FigureRegistry");
        return nullptr;
    }

    // Create the base window (GLFW + Vulkan resources)
    auto* wctx = create_window(width, height, title);
    if (!wctx)
        return nullptr;

    // Set window position
    set_window_position(*wctx, screen_x, screen_y);

    // Set figure assignment
    wctx->assigned_figure_index = initial_figure_id;
    wctx->assigned_figures      = {initial_figure_id};
    wctx->active_figure_id      = initial_figure_id;
    wctx->title                 = title;

#ifdef SPECTRA_USE_GLFW
    // Install full input callbacks (mouse, key, scroll) in addition to the
    // framebuffer/close/focus callbacks already installed by create_window().
    auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
    if (glfw_win)
    {
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
        glfwSetDropCallback(glfw_win, glfw_drop_callback);
    }
#endif

    // Initialize the full UI subsystem bundle
    if (!init_window_ui(*wctx, initial_figure_id))
    {
        SPECTRA_LOG_ERROR(
            "window_manager",
            "create_window_with_ui: UI init failed for window " + std::to_string(wctx->id));
        // Window still usable as a bare render window — don't destroy it
    }

    SPECTRA_LOG_INFO("window_manager",
                     "Created window with UI " + std::to_string(wctx->id) + ": "
                         + std::to_string(width) + "x" + std::to_string(height) + " \"" + title
                         + "\" figure=" + std::to_string(initial_figure_id));
    return wctx;
}

// ── init_window_ui ──────────────────────────────────────────────────────────

bool WindowManager::init_window_ui(WindowContext& wctx, FigureId initial_figure_id)
{
    if (!registry_)
        return false;

#ifdef SPECTRA_USE_IMGUI
    auto options = make_ui_build_options(wctx, initial_figure_id);
    auto ui      = build_window_ui_context(options);
    if (!ui)
        return false;

    if (wctx.glfw_window && backend_ && ui->imgui_ui)
    {
        if (!init_window_imgui_integration(*backend_, wctx, *ui->imgui_ui, false))
        {
            SPECTRA_LOG_ERROR(
                "window_manager",
                "init_window_ui: ImGui init failed for window " + std::to_string(wctx.id));
            return false;
        }
    }

    wire_tab_drag_handlers(*ui);

    SPECTRA_LOG_DEBUG("imgui", "Created ImGui context for window " + std::to_string(wctx.id));

    wctx.ui_ctx = std::move(ui);
#else
    (void)wctx;
    (void)initial_figure_id;
#endif

    return true;
}

}   // namespace spectra
