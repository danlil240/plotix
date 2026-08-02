// app_inproc.cpp — In-process (single-process) run implementation.
// run_inproc() delegates to init_runtime/step/shutdown_runtime (app_step.cpp).
// render_secondary_window() remains here.

#include <spectra/app.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"

#include <chrono>

namespace spectra
{

std::atomic<bool> App::s_exit_requested_{false};

void App::request_exit()
{
    s_exit_requested_.store(true, std::memory_order_relaxed);
}

void App::run_inproc()
{
    init_runtime();
    if (!runtime_)
        return;
    for (;;)
    {
        if (s_exit_requested_.load(std::memory_order_relaxed))
        {
            SPECTRA_LOG_INFO("app", "Exit requested (signal) — shutting down");
            break;
        }
        auto result = step();
        if (result.should_exit)
            break;
    }
    shutdown_runtime();
}

// ─── render_secondary_window ──────────────────────────────────────────────────
// Render a secondary window (no ImGui, figure-only).
void App::render_secondary_window(WindowContext* wctx)
{
    if (!wctx || wctx->should_close)
        return;

    auto* fig = registry_.get(wctx->assigned_figure_index);
    if (!fig)
        return;

    // Secondary windows currently require VulkanBackend.
    if (config_.backend != RenderBackend::Vulkan)
        return;

    auto* vk = static_cast<VulkanBackend*>(backend_.get());

    if (wctx->needs_resize && wctx->pending_width > 0 && wctx->pending_height > 0)
    {
        const uint32_t cur_w        = wctx->swapchain.extent.width;
        const uint32_t cur_h        = wctx->swapchain.extent.height;
        const bool     size_differs = wctx->pending_width != cur_w || wctx->pending_height != cur_h;
        const bool     urgent       = wctx->swapchain_invalidated || wctx->swapchain_dirty;
        static constexpr auto kThrottle = std::chrono::milliseconds(16);
        const auto            now       = std::chrono::steady_clock::now();
        if (size_differs && (urgent || (now - wctx->last_swapchain_recreate_time) >= kThrottle))
        {
            wctx->last_swapchain_recreate_time = now;
            vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
            fig->config_.width  = wctx->pending_width;
            fig->config_.height = wctx->pending_height;
            wctx->needs_resize  = false;
        }
    }

    // Switch active window to this secondary context
    vk->set_active_window(wctx);

    bool sec_ok = backend_->begin_frame();
    if (!sec_ok && wctx->pending_width > 0 && wctx->pending_height > 0)
    {
        // Swapchain out of date — recreate and retry
        vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
        vk->clear_swapchain_dirty();
        sec_ok = backend_->begin_frame();
    }

    if (sec_ok)
    {
        renderer_->begin_render_pass();
        renderer_->render_figure_content(*fig);
        {
            auto sw = static_cast<float>(backend_->swapchain_width());
            auto sh = static_cast<float>(backend_->swapchain_height());
            renderer_->render_text(sw, sh);
        }
        renderer_->end_render_pass();
        backend_->end_frame();

        // Post-present recovery: if present returned OUT_OF_DATE, recreate
        // immediately so the next frame's begin_frame() doesn't loop.
        if (wctx->swapchain_invalidated)
        {
            vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
            vk->clear_swapchain_dirty();
        }
    }
}

}   // namespace spectra
