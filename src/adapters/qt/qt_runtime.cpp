#include "qt_runtime.hpp"

#include "qt_surface_host.hpp"
#include "spectra_vulkan_window.hpp"

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/input/input.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "core/layout.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/overlay/data_interaction.hpp"
#endif

#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <algorithm>
#include <QtGui/QWindow>

#ifdef SPECTRA_USE_IMGUI
    #include <QtGui/QCursor>
    #include <QtGui/QGuiApplication>
#endif

namespace spectra::adapters::qt
{

QtRuntime::QtRuntime() = default;
QtRuntime::~QtRuntime()
{
    shutdown();
}

QtRuntime::WindowState* QtRuntime::find_window_state(QWindow* window)
{
    if (!window)
    {
        return nullptr;
    }
    auto it = window_states_.find(window);
    return it != window_states_.end() ? it->second.get() : nullptr;
}

const QtRuntime::WindowState* QtRuntime::find_window_state(QWindow* window) const
{
    if (!window)
    {
        return nullptr;
    }
    auto it = window_states_.find(window);
    return it != window_states_.end() ? it->second.get() : nullptr;
}

bool QtRuntime::adopt_vulkan_instance(VkInstance instance)
{
    if (instance == VK_NULL_HANDLE)
    {
        return false;
    }

    if (!vulkan_instance_)
    {
        vulkan_instance_ = std::make_unique<QVulkanInstance>();
    }

    if (vulkan_instance_->vkInstance() == VK_NULL_HANDLE)
    {
        vulkan_instance_->setVkInstance(instance);
    }

    return vulkan_instance_->vkInstance() == instance;
}

QVulkanInstance* QtRuntime::vulkan_instance() const
{
    return vulkan_instance_.get();
}

bool QtRuntime::init()
{
    if (initialized_)
    {
        return true;
    }

    // Create Qt surface host for Vulkan extension/surface queries
    surface_host_ = std::make_unique<QtSurfaceHost>();

    // Create Vulkan backend (non-headless — we have a real surface)
    backend_ = std::make_unique<VulkanBackend>();
    backend_->set_surface_host(surface_host_.get());

    if (!backend_->init(false /*headless*/))
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize Vulkan backend");
        return false;
    }

    // Adopt the VkInstance into a QVulkanInstance so Qt can use it
    if (!vulkan_instance_)
    {
        vulkan_instance_ = std::make_unique<QVulkanInstance>();
    }
    if (vulkan_instance_->vkInstance() == VK_NULL_HANDLE)
    {
        vulkan_instance_->setVkInstance(backend_->instance());
    }

    // create() initializes the Qt-side Vulkan wrapper (XCB/Wayland bridge).
    // Must be called after setVkInstance() and before surfaceForWindow().
    if (!vulkan_instance_->isValid())
    {
        if (!vulkan_instance_->create())
        {
            SPECTRA_LOG_ERROR("qt_runtime",
                              "QVulkanInstance::create() failed (VkResult="
                                  + std::to_string(static_cast<int>(vulkan_instance_->errorCode()))
                                  + ")");
            return false;
        }
    }

    // Wire the QVulkanInstance into the surface host so create_surface works
    surface_host_->set_vulkan_instance(vulkan_instance_.get());

    // Register the Qt-owned ThemeManager as the active instance.
    ui::ThemeManager::set_current(&theme_mgr_);

    // Make sure the default theme map (night/dark/light) exists before trying
    // to select one.
    theme_mgr_.ensure_initialized();

    // Apply dark theme by default
    theme_mgr_.set_theme("night");

    // The Qt frontend must use this same backend/renderer/theme graph for
    // application services and every canvas. Creating another stack in the
    // application controller made commands, export, cleanup, and rendering
    // operate on different Vulkan devices.
    renderer_ = std::make_unique<Renderer>(*backend_, theme_mgr_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize shared renderer");
        renderer_.reset();
        backend_->shutdown();
        backend_.reset();
        surface_host_.reset();
        vulkan_instance_.reset();
        ui::ThemeManager::set_current(nullptr);
        return false;
    }

    initialized_ = true;
    SPECTRA_LOG_INFO(
        "qt_runtime",
        "Initialized shared backend/renderer (swapchain/pipelines deferred until attach_window)");
    return true;
}

void QtRuntime::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    if (backend_)
    {
        backend_->wait_idle();
    }

    // Destroy per-window contexts before renderer/backend teardown.
    if (backend_)
    {
        for (auto& [window, state] : window_states_)
        {
            (void)window;
            if (!state || !state->window_ctx)
            {
                continue;
            }
#ifdef SPECTRA_USE_IMGUI
            if (state->input_handler)
                state->input_handler->set_data_interaction(nullptr);
            if (state->imgui_ui)
                state->imgui_ui->set_data_interaction(nullptr);
            state->imgui_ui.reset();
            state->data_interaction.reset();
            state->ui_has_last_frame_time = false;
#endif
            backend_->set_active_window(state->window_ctx.get());
            backend_->destroy_window_context(*state->window_ctx);
        }
        backend_->set_active_window(nullptr);
    }

    window_states_.clear();
    pending_input_handlers_.clear();
    pending_inspector_callbacks_.clear();
    primary_window_       = nullptr;
    current_frame_window_ = nullptr;
    next_window_id_       = 1;

    renderer_.reset();
    backend_.reset();

    surface_host_.reset();
    vulkan_instance_.reset();

    ui::ThemeManager::set_current(nullptr);

    initialized_ = false;
    SPECTRA_LOG_INFO("qt_runtime", "Shutdown complete");
}

bool QtRuntime::attach_window(QWindow* window, uint32_t width, uint32_t height)
{
    if (!initialized_ || !window)
    {
        return false;
    }

    if (has_window(window))
    {
        if (!primary_window_)
        {
            primary_window_ = window;
        }
        return true;
    }

    // Ensure the QWindow has VulkanSurface type
    if (window->surfaceType() != QSurface::VulkanSurface)
    {
        window->setSurfaceType(QSurface::VulkanSurface);
    }

    // Bind QVulkanInstance to the window
    if (window->vulkanInstance() != vulkan_instance_.get())
    {
        window->setVulkanInstance(vulkan_instance_.get());
    }

    auto state                       = std::make_unique<WindowState>();
    state->window_ctx                = VulkanBackend::create_window_context();
    state->window_ctx->id            = next_window_id_++;
    state->window_ctx->native_window = static_cast<void*>(window);

    if (auto pending_it = pending_input_handlers_.find(window);
        pending_it != pending_input_handlers_.end())
    {
        state->input_handler = pending_it->second;
    }
    if (auto pending_it = pending_inspector_callbacks_.find(window);
        pending_it != pending_inspector_callbacks_.end())
    {
        state->inspector_callbacks = pending_it->second;
    }

    if (!backend_->init_window_context(*state->window_ctx, width, height))
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize window context");
        return false;
    }

    // First attached window initializes shared renderer resources.
    if (!renderer_)
    {
        backend_->set_active_window(state->window_ctx.get());
        renderer_ = std::make_unique<Renderer>(*backend_, theme_mgr_);
        if (!renderer_->init())
        {
            SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize renderer");
            backend_->destroy_window_context(*state->window_ctx);
            return false;
        }
    }

    backend_->set_active_window(state->window_ctx.get());
    backend_->ensure_pipelines();

#ifdef SPECTRA_USE_IMGUI
    state->imgui_ui = make_imgui_integration();
    state->imgui_ui->set_theme_manager(&theme_mgr_);
    if (!state->imgui_ui->init_headless(*backend_, width, height))
    {
        SPECTRA_LOG_WARN(
            "qt_runtime",
            "ImGui headless init failed — Qt runtime continues with canvas-only rendering");
        state->imgui_ui.reset();
    }
    else
    {
        state->data_interaction = std::make_unique<DataInteraction>();
        state->data_interaction->set_theme_manager(&theme_mgr_);
        state->imgui_ui->set_data_interaction(state->data_interaction.get());
        if (state->input_handler)
        {
            state->imgui_ui->set_input_handler(state->input_handler);
            state->input_handler->set_data_interaction(state->data_interaction.get());
        }

        auto* imgui_raw      = state->imgui_ui.get();
        auto  sync_selection = [window, imgui_raw]()
        {
            auto* canvas = dynamic_cast<SpectraVulkanWindow*>(window);
            if (!canvas || !imgui_raw)
                return;
            std::vector<SpectraVulkanWindow::SeriesSelection> selection;
            const auto& context = imgui_raw->selection_context();
            selection.reserve(context.selected_series.size());
            for (const auto& entry : context.selected_series)
            {
                selection.push_back(
                    {entry.axes_base ? entry.axes_base : static_cast<AxesBase*>(entry.axes),
                     entry.axes,
                     entry.series,
                     entry.axes_index,
                     entry.series_index});
            }
            canvas->setSeriesSelection(std::move(selection));
        };
        state->data_interaction->set_on_series_selected(
            [imgui_raw, sync_selection](Figure* figure,
                                        Axes*   axes,
                                        int     axes_index,
                                        Series* series,
                                        int     series_index)
            {
                imgui_raw->select_series(figure, axes, axes_index, series, series_index);
                sync_selection();
            });
        state->data_interaction->set_on_series_right_click_selected(
            [imgui_raw, sync_selection](Figure* figure,
                                        Axes*   axes,
                                        int     axes_index,
                                        Series* series,
                                        int     series_index)
            {
                imgui_raw->select_series_no_toggle(figure, axes, axes_index, series, series_index);
                sync_selection();
            });
        state->data_interaction->set_on_series_deselected(
            [imgui_raw, sync_selection]()
            {
                imgui_raw->deselect_series();
                sync_selection();
            });
        state->data_interaction->set_on_rect_series_selected(
            [imgui_raw,
             sync_selection](const std::vector<DataInteraction::RectSelectedEntry>& entries)
            {
                imgui_raw->select_series_in_rect(entries);
                sync_selection();
            });

        // Qt owns the application shell — suppress all legacy ImGui chrome.
        // Only plot overlays (legends, tooltips, data interaction) remain.
        state->imgui_ui->set_command_bar_visible(false);
        state->imgui_ui->set_status_bar_visible(false);
        state->imgui_ui->set_nav_rail_visible(false);
        state->imgui_ui->set_shell_chrome_visible(false);
        state->imgui_ui->set_external_inspector_toggle_callbacks(
            state->inspector_callbacks.toggle,
            state->inspector_callbacks.is_open);
        state->imgui_ui->get_layout_manager().set_inspector_visible(false);
    }
#endif

    window_states_.emplace(window, std::move(state));
    if (!primary_window_)
    {
        primary_window_ = window;
    }

    // Set surface generation — surface is now valid
    auto* new_state = find_window_state(window);
    if (new_state)
    {
        ++new_state->surface_generation;
    }

    SPECTRA_LOG_INFO("qt_runtime",
                     "Window attached: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void QtRuntime::set_inspector_toggle_callbacks(QWindow*              window,
                                               std::function<void()> toggle,
                                               std::function<bool()> is_open)
{
    if (!window)
        return;

    InspectorCallbacks callbacks{std::move(toggle), std::move(is_open)};
    pending_inspector_callbacks_[window] = callbacks;
    auto* state                          = find_window_state(window);
    if (!state)
        return;

    state->inspector_callbacks = std::move(callbacks);
#ifdef SPECTRA_USE_IMGUI
    if (state->imgui_ui)
    {
        state->imgui_ui->set_external_inspector_toggle_callbacks(
            state->inspector_callbacks.toggle,
            state->inspector_callbacks.is_open);
    }
#endif
}

void QtRuntime::detach_window(QWindow* window)
{
    if (!initialized_ || !window)
    {
        return;
    }

    auto it = window_states_.find(window);
    if (it == window_states_.end())
    {
        return;
    }

    auto* state = it->second.get();
#ifdef SPECTRA_USE_IMGUI
    if (state)
    {
        if (state->input_handler)
            state->input_handler->set_data_interaction(nullptr);
        if (state->imgui_ui)
            state->imgui_ui->set_data_interaction(nullptr);
        state->imgui_ui.reset();
        state->data_interaction.reset();
        state->ui_has_last_frame_time = false;
    }
#endif
    if (backend_ && state && state->window_ctx)
    {
        backend_->set_active_window(state->window_ctx.get());
        backend_->destroy_window_context(*state->window_ctx);
    }

    // Invalidate surface generation
    state->surface_generation = 0;

    if (current_frame_window_ == window)
    {
        current_frame_window_ = nullptr;
    }

    pending_input_handlers_.erase(window);
    pending_inspector_callbacks_.erase(window);

    window_states_.erase(it);

    if (primary_window_ == window)
    {
        primary_window_ = window_states_.empty() ? nullptr : window_states_.begin()->first;
    }

    if (backend_)
    {
        WindowContext* next_ctx = nullptr;
        if (auto* primary_state = find_window_state(primary_window_))
        {
            next_ctx = primary_state->window_ctx.get();
        }
        backend_->set_active_window(next_ctx);
    }
}

bool QtRuntime::has_window(QWindow* window) const
{
    return find_window_state(window) != nullptr;
}

bool QtRuntime::resize(QWindow* window, uint32_t width, uint32_t height)
{
    if (!initialized_ || !window || width == 0 || height == 0)
    {
        return false;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx || !backend_)
    {
        return false;
    }

    backend_->set_active_window(state->window_ctx.get());
    return backend_->recreate_swapchain_for(*state->window_ctx, width, height);
}

bool QtRuntime::resize(uint32_t width, uint32_t height)
{
    return resize(primary_window_, width, height);
}

void QtRuntime::mark_swapchain_dirty(QWindow* window)
{
    if (!initialized_ || !window)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    state->resize_pending              = true;
    state->last_resize_request         = std::chrono::steady_clock::now();
    state->window_ctx->swapchain_dirty = true;

    SPECTRA_LOG_DEBUG("qt_runtime",
                      "Swapchain marked dirty for window {} (debounce pending)",
                      state->window_ctx->id);
}

void QtRuntime::mark_swapchain_dirty()
{
    mark_swapchain_dirty(primary_window_);
}

bool QtRuntime::begin_frame(QWindow* window)
{
    if (!initialized_ || !window || !backend_ || !renderer_)
    {
        return false;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return false;
    }

    backend_->set_active_window(state->window_ctx.get());

    // Handle swapchain recreation if needed (dirty from resize or OUT_OF_DATE)
    if (state->window_ctx->swapchain_dirty || state->window_ctx->swapchain_invalidated)
    {
        // Debounce: wait until resize events settle before recreating.
        // OUT_OF_DATE from present is always immediate (must recreate).
        if (state->resize_pending && !state->window_ctx->swapchain_invalidated)
        {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state->last_resize_request);
            if (elapsed < k_resize_debounce)
            {
                ++state->frame_skip_count;
                SPECTRA_LOG_DEBUG(
                    "qt_runtime",
                    "Resize debounce: {}ms < {}ms, skipping frame for window {} (skip #{})",
                    elapsed.count(),
                    k_resize_debounce.count(),
                    state->window_ctx->id,
                    state->frame_skip_count);
                return false;   // Wait for resize events to settle
            }
        }

        platform::SurfaceSize sz{};
        if (surface_host_ && surface_host_->framebuffer_size(static_cast<void*>(window), sz)
            && sz.width > 0 && sz.height > 0)
        {
            ++state->swapchain_recreate_count;
            SPECTRA_LOG_DEBUG("qt_runtime",
                              "Recreating swapchain for window {}: {}x{} (recreate #{}, dirty={}, "
                              "invalidated={})",
                              state->window_ctx->id,
                              sz.width,
                              sz.height,
                              state->swapchain_recreate_count,
                              state->window_ctx->swapchain_dirty,
                              state->window_ctx->swapchain_invalidated);

            if (!backend_->recreate_swapchain_for(*state->window_ctx, sz.width, sz.height))
            {
                SPECTRA_LOG_WARN("qt_runtime",
                                 "Swapchain recreation failed for window {}",
                                 state->window_ctx->id);
                return false;
            }
            state->window_ctx->swapchain_dirty       = false;
            state->window_ctx->swapchain_invalidated = false;
            state->resize_pending                    = false;

#ifdef SPECTRA_USE_IMGUI
            if (state->imgui_ui)
            {
                state->imgui_ui->on_swapchain_recreated(*backend_);
            }
#endif
        }
        else
        {
            ++state->frame_skip_count;
            SPECTRA_LOG_DEBUG(
                "qt_runtime",
                "Surface not ready (minimized?) for window {} — skipping frame (skip #{})",
                state->window_ctx->id,
                state->frame_skip_count);
            return false;   // Surface not ready (minimized, etc.)
        }
    }

    // Wait on all OTHER windows' in-flight fences before recording commands.
    // The Renderer shares text vertex buffers and overlay scratch buffers across
    // windows. Without this wait, this window's host-side uploads (text vertices,
    // tick geometry) overwrite memory that another window's submitted-but-not-yet-
    // executed command buffer is still reading on the GPU — causing tick label
    // flicker and geometry corruption on the other canvas.
    for (auto& [other_win, other_state] : window_states_)
    {
        if (other_win != window && other_state && other_state->window_ctx)
        {
            backend_->wait_window_fences(*other_state->window_ctx);
        }
    }

    if (!backend_->begin_frame())
    {
        return false;
    }

    renderer_->flush_pending_deletions();
    current_frame_window_ = window;
    return true;
}

bool QtRuntime::begin_frame()
{
    return begin_frame(primary_window_);
}

void QtRuntime::render_figure(QWindow* window, Figure& figure)
{
    if (!initialized_ || !window || !backend_ || !renderer_)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    if (current_frame_window_ != window)
    {
        return;
    }

    backend_->set_active_window(state->window_ctx.get());

    auto sw_u = backend_->swapchain_width();
    auto sh_u = backend_->swapchain_height();
    auto sw   = static_cast<float>(sw_u);
    auto sh   = static_cast<float>(sh_u);

    const bool size_changed = (figure.width() != sw_u || figure.height() != sh_u);
    if (size_changed)
    {
        figure.set_size(sw_u, sh_u);
    }

    bool layout_applied = false;

#ifdef SPECTRA_USE_IMGUI
    if (state->imgui_ui)
    {
        if (state->input_handler)
        {
            state->imgui_ui->set_input_handler(state->input_handler);
            if (state->data_interaction)
            {
                state->input_handler->set_data_interaction(state->data_interaction.get());
                const auto& readout = state->input_handler->cursor_readout();
                state->imgui_ui->set_cursor_data(readout.data_x, readout.data_y, readout.valid);
                state->data_interaction->update(readout, figure);
            }
        }
        else
        {
            state->imgui_ui->set_input_handler(nullptr);
            if (state->data_interaction)
            {
                CursorReadout no_cursor;
                no_cursor.valid = false;
                state->data_interaction->update(no_cursor, figure);
                state->imgui_ui->set_cursor_data(0.0f, 0.0f, false);
            }
        }

        float      dt  = 1.0f / 60.0f;
        const auto now = std::chrono::steady_clock::now();
        if (state->ui_has_last_frame_time)
        {
            dt = std::chrono::duration<float>(now - state->ui_last_frame_time).count();
            dt = std::clamp(dt, 1.0f / 240.0f, 0.1f);
        }
        state->ui_last_frame_time     = now;
        state->ui_has_last_frame_time = true;

        ImGuiIntegration::HeadlessFrameInput fi{};
        fi.display_w = sw;
        fi.display_h = sh;
        fi.dt        = dt;
        fi.dpi_scale = static_cast<float>(window->devicePixelRatio());

        const QPoint local_pos = window->mapFromGlobal(QCursor::pos());
        const float  dpr       = std::max(fi.dpi_scale, 1.0f);
        fi.mouse_x             = static_cast<float>(local_pos.x()) * dpr;
        fi.mouse_y             = static_cast<float>(local_pos.y()) * dpr;

        const Qt::MouseButtons buttons = QGuiApplication::mouseButtons();
        fi.mouse_down[0]               = (buttons & Qt::LeftButton) != 0;
        fi.mouse_down[1]               = (buttons & Qt::RightButton) != 0;
        fi.mouse_down[2]               = (buttons & Qt::MiddleButton) != 0;

        state->imgui_ui->new_frame_headless(fi);

        // The QWindow is already the plot canvas: Qt owns the title, menus,
        // document tabs, navigation rail, inspector, and status bar outside
        // this surface. Reusing the legacy layout without an override subtracts
        // its command/status chrome a second time and visibly shifts/shrinks the
        // renderer relative to the GLFW reference.
        state->imgui_ui->get_layout_manager().set_canvas_override(Rect{0.0f, 0.0f, sw, sh});
        state->imgui_ui->build_ui(figure);

        auto& lm = state->imgui_ui->get_layout_manager();

        const Rect  canvas    = lm.canvas_rect();
        const auto& fig_style = figure.style();
        Margins     fig_margins;
        fig_margins.left   = fig_style.margin_left;
        fig_margins.right  = fig_style.margin_right;
        fig_margins.top    = fig_style.margin_top;
        fig_margins.bottom = fig_style.margin_bottom;

        const auto rects = compute_subplot_layout(canvas.w,
                                                  canvas.h,
                                                  figure.grid_rows(),
                                                  figure.grid_cols(),
                                                  fig_margins,
                                                  canvas.x,
                                                  canvas.y);

        for (size_t i = 0; i < figure.axes_mut().size() && i < rects.size(); ++i)
        {
            if (figure.axes_mut()[i])
            {
                figure.axes_mut()[i]->set_viewport(rects[i]);
            }
        }
        for (size_t i = 0; i < figure.all_axes_mut().size() && i < rects.size(); ++i)
        {
            if (figure.all_axes_mut()[i])
            {
                figure.all_axes_mut()[i]->set_viewport(rects[i]);
            }
        }

        layout_applied = true;
    }
#endif

    if (!layout_applied && size_changed)
    {
        // Canvas-only fallback when ImGui integration is unavailable.
        figure.compute_layout();
    }

    // Upload dirty series data
    for (auto& ax : figure.all_axes_mut())
    {
        if (!ax)
            continue;
        for (auto& s : ax->series_mut())
        {
            if (s)
                renderer_->upload_series_data(*s);
        }
    }

    if (state->selected_series.empty())
        renderer_->clear_selected_series();
    else
        renderer_->set_selected_series(state->selected_series);

    // render_figure_content() already calls render_plot_text() and
    // render_plot_geometry() internally — do NOT call them again here.
    renderer_->begin_render_pass();
    renderer_->render_figure_content(figure);
    renderer_->render_text(sw, sh);

#ifdef SPECTRA_USE_IMGUI
    if (state->imgui_ui)
    {
        state->imgui_ui->render(*backend_);
    }
#endif

    renderer_->end_render_pass();
}

void QtRuntime::render_figure(Figure& figure)
{
    render_figure(primary_window_, figure);
}

bool QtRuntime::render_window(QWindow* window, Figure& figure)
{
    if (!begin_frame(window))
    {
        return false;
    }
    render_figure(window, figure);
    end_frame(window);
    return true;
}

bool QtRuntime::render_window(Figure& figure)
{
    return render_window(primary_window_, figure);
}

void QtRuntime::end_frame(QWindow* window)
{
    if (!initialized_ || !window || !backend_)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    if (current_frame_window_ != window)
    {
        return;
    }

    backend_->set_active_window(state->window_ctx.get());
    backend_->end_frame();
    current_frame_window_ = nullptr;
}

void QtRuntime::end_frame()
{
    end_frame(primary_window_);
}

void QtRuntime::set_input_handler(QWindow* window, InputHandler* input)
{
    if (!window)
    {
        return;
    }

    pending_input_handlers_[window] = input;

    if (auto* state = find_window_state(window))
    {
#ifdef SPECTRA_USE_IMGUI
        if (state->input_handler && state->input_handler != input)
            state->input_handler->set_data_interaction(nullptr);
#endif
        state->input_handler = input;

#ifdef SPECTRA_USE_IMGUI
        if (state->data_interaction && input)
        {
            input->set_data_interaction(state->data_interaction.get());
        }
#endif
    }
}

void QtRuntime::set_input_handler(InputHandler* input)
{
    set_input_handler(primary_window_, input);
}

bool QtRuntime::set_crosshair(QWindow* window, bool enabled)
{
#ifdef SPECTRA_USE_IMGUI
    auto* state = find_window_state(window);
    if (!state || !state->data_interaction)
        return false;
    state->data_interaction->set_crosshair(enabled);
    return true;
#else
    (void)window;
    (void)enabled;
    return false;
#endif
}

bool QtRuntime::capture_overlay_snapshot(QWindow*         window,
                                         const Figure&    figure,
                                         OverlaySnapshot& snapshot) const
{
#ifdef SPECTRA_USE_IMGUI
    const auto* state = find_window_state(window);
    if (!state || !state->data_interaction)
        return false;
    snapshot = state->data_interaction->capture_overlay_snapshot(figure);
    if (state->input_handler)
    {
        snapshot.tool_mode = static_cast<uint8_t>(state->input_handler->tool_mode());
        if (state->input_handler->has_measure_result() && state->input_handler->measure_axes())
        {
            snapshot.measurement.valid        = true;
            snapshot.measurement.start_data_x = state->input_handler->measure_start_data_x();
            snapshot.measurement.start_data_y = state->input_handler->measure_start_data_y();
            snapshot.measurement.end_data_x   = state->input_handler->measure_end_data_x();
            snapshot.measurement.end_data_y   = state->input_handler->measure_end_data_y();
            for (size_t i = 0; i < figure.axes().size(); ++i)
            {
                if (figure.axes()[i].get() == state->input_handler->measure_axes())
                {
                    snapshot.measurement.axes_index = i;
                    break;
                }
            }
        }
    }
    return true;
#else
    (void)window;
    (void)figure;
    (void)snapshot;
    return false;
#endif
}

bool QtRuntime::restore_overlay_snapshot(QWindow*               window,
                                         Figure&                figure,
                                         const OverlaySnapshot& snapshot)
{
#ifdef SPECTRA_USE_IMGUI
    auto* state = find_window_state(window);
    if (!state || !state->data_interaction)
        return false;
    if (state->input_handler && state->input_handler->tool_mode() != ToolMode::Pan)
        state->input_handler->set_tool_mode(ToolMode::Pan);
    state->data_interaction->restore_overlay_snapshot(snapshot, figure);
    if (state->input_handler)
    {
        const auto max_tool = static_cast<uint8_t>(ToolMode::ROI);
        const auto tool = snapshot.tool_mode <= max_tool ? static_cast<ToolMode>(snapshot.tool_mode)
                                                         : ToolMode::Pan;
        state->input_handler->set_tool_mode(tool);
        Axes* measure_axes = nullptr;
        if (snapshot.measurement.valid
            && snapshot.measurement.axes_index < figure.axes_mut().size())
        {
            measure_axes = figure.axes_mut()[snapshot.measurement.axes_index].get();
        }
        state->input_handler->restore_measure_result(measure_axes,
                                                     snapshot.measurement.start_data_x,
                                                     snapshot.measurement.start_data_y,
                                                     snapshot.measurement.end_data_x,
                                                     snapshot.measurement.end_data_y);
    }
    return true;
#else
    (void)window;
    (void)figure;
    (void)snapshot;
    return false;
#endif
}

size_t QtRuntime::marker_count(QWindow* window) const
{
#ifdef SPECTRA_USE_IMGUI
    const auto* state = find_window_state(window);
    return state && state->data_interaction ? state->data_interaction->markers().size() : 0;
#else
    (void)window;
    return 0;
#endif
}

bool QtRuntime::clear_markers(QWindow* window)
{
#ifdef SPECTRA_USE_IMGUI
    auto* state = find_window_state(window);
    if (!state || !state->data_interaction || state->data_interaction->markers().empty())
        return false;
    state->data_interaction->clear_markers();
    return true;
#else
    (void)window;
    return false;
#endif
}

void QtRuntime::set_series_selection(QWindow*                                   window,
                                     const std::vector<QtSeriesSelectionEntry>& selected)
{
    auto* state = find_window_state(window);
    if (!state)
        return;

    state->selected_series.clear();
    state->selected_series.reserve(selected.size());
    for (const auto& entry : selected)
    {
        if (entry.series)
            state->selected_series.push_back(entry.series);
    }

#ifdef SPECTRA_USE_IMGUI
    if (state->imgui_ui)
    {
        auto& context = state->imgui_ui->selection_context();
        context.clear();
        for (const auto& entry : selected)
        {
            if (!entry.figure || !entry.owner || !entry.series)
                continue;
            context.add_series(entry.figure,
                               entry.axes,
                               entry.owner,
                               entry.axes_index,
                               entry.series,
                               entry.series_index);
        }
        if (!context.selected_series.empty())
            state->imgui_ui->set_inspector_section_series();
    }
#endif
}

void QtRuntime::notify_series_removed(QWindow* window, const Series* series)
{
    if (!series)
        return;
    if (renderer_)
        renderer_->notify_series_removed(series);

    auto* state = find_window_state(window);
    if (!state)
        return;
    std::erase(state->selected_series, series);
#ifdef SPECTRA_USE_IMGUI
    if (state->data_interaction)
        state->data_interaction->notify_series_removed(series);
    if (state->imgui_ui)
        state->imgui_ui->notify_series_removed(series);
#endif
}

WindowContext* QtRuntime::window_context(QWindow* window) const
{
    if (auto* state = find_window_state(window))
    {
        return state->window_ctx.get();
    }
    return nullptr;
}

WindowContext* QtRuntime::window_context() const
{
    return window_context(primary_window_);
}

// ── Surface generation API ───────────────────────────────────────────────────

uint32_t QtRuntime::surface_generation(QWindow* window) const
{
    const auto* state = find_window_state(window);
    return state ? state->surface_generation : 0;
}

bool QtRuntime::surface_valid(QWindow* window) const
{
    return surface_generation(window) != 0;
}

// ── Explicit render-target API ───────────────────────────────────────────────
// These methods target a specific WindowContext, reducing reliance on the
// global mutable active-window state.  Currently they delegate to the
// QWindow-based overloads, but they establish the API surface for future
// refactoring to eliminate set_active_window() entirely.

bool QtRuntime::begin_frame(WindowContext* wctx)
{
    if (!initialized_ || !wctx || !backend_ || !renderer_)
        return false;

    // Find the QWindow for this WindowContext
    QWindow* target = nullptr;
    for (const auto& [win, state] : window_states_)
    {
        if (state && state->window_ctx.get() == wctx)
        {
            target = win;
            break;
        }
    }

    if (target)
        return begin_frame(target);

    // Fallback: set active window directly
    backend_->set_active_window(wctx);
    return backend_->begin_frame();
}

void QtRuntime::render_figure(WindowContext* wctx, Figure& figure)
{
    if (!initialized_ || !wctx || !backend_ || !renderer_)
        return;

    backend_->set_active_window(wctx);

    auto sw_u = backend_->swapchain_width();
    auto sh_u = backend_->swapchain_height();

    if (figure.width() != sw_u || figure.height() != sh_u)
        figure.set_size(sw_u, sh_u);

    if (figure.width() != sw_u || figure.height() != sh_u)
        figure.compute_layout();

    for (auto& ax : figure.all_axes_mut())
    {
        if (!ax)
            continue;
        for (auto& s : ax->series_mut())
        {
            if (s)
                renderer_->upload_series_data(*s);
        }
    }

    renderer_->begin_render_pass();
    renderer_->render_figure_content(figure);
    renderer_->render_text(static_cast<float>(sw_u), static_cast<float>(sh_u));
    renderer_->end_render_pass();
}

void QtRuntime::end_frame(WindowContext* wctx)
{
    if (!initialized_ || !wctx || !backend_)
        return;

    backend_->set_active_window(wctx);
    backend_->end_frame();
}

}   // namespace spectra::adapters::qt
