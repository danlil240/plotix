// vk_frame.cpp — Frame lifecycle (begin/end frame), render pass, bind, draw, queries.
// Split from vk_backend.cpp (MR-2) for focused module ownership.

#include "vk_backend.hpp"

#include <spectra/logger.hpp>
#include <stdexcept>

#include "../../anim/frame_profiler.hpp"

#include <vk_mem_alloc.h>

namespace spectra
{

bool VulkanBackend::begin_frame(FrameProfiler* profiler)
{
    // NOTE: do NOT reset ubo_next_offset_ here.  In multi-window mode,
    // begin_frame() is called once per window per tick.  Resetting the
    // ring to 0 would cause window B's UBO uploads to overwrite slots
    // that window A's in-flight command buffer is still reading from,
    // corrupting projection matrices (flickering / wrong zoom).
    // The ring wraps automatically in upload_buffer() when it reaches
    // UBO_MAX_SLOTS, giving ~10+ frames of headroom before reuse.

    if (headless_)
    {
        // For offscreen, just allocate a command buffer
        if (active_window_->command_buffers.empty())
        {
            SPECTRA_LOG_ERROR("vulkan", "begin_frame: no command buffers for headless");
            return false;
        }
        active_window_->current_cmd = active_window_->command_buffers[0];

        vkResetCommandBuffer(active_window_->current_cmd, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(active_window_->current_cmd, &begin_info);
        return true;
    }

    // Windowed mode — wait for this slot's previous work to finish.
    // Use a bounded timeout (100ms) to prevent indefinite stalls when
    // the presentation engine is backed up (multi-window, resize storms).
    static constexpr uint64_t FENCE_TIMEOUT_NS   = 100'000'000;   // 100ms
    static constexpr uint64_t ACQUIRE_TIMEOUT_NS = 100'000'000;   // 100ms
    if (profiler)
        profiler->begin_stage("vk_wait_fences");
    VkResult fence_status =
        vkWaitForFences(ctx_.device,
                        1,
                        &active_window_->in_flight_fences[active_window_->current_flight_frame],
                        VK_TRUE,
                        FENCE_TIMEOUT_NS);
    if (profiler)
        profiler->end_stage("vk_wait_fences");
    if (fence_status == VK_ERROR_DEVICE_LOST)
    {
        device_lost_ = true;
        throw std::runtime_error("Vulkan device lost - cannot continue rendering");
    }
    if (fence_status == VK_TIMEOUT)
    {
        // Previous frame's GPU work hasn't finished within the timeout.
        // Skip this window for the current tick to avoid blocking the
        // entire render loop (other windows, input processing, animations).
        SPECTRA_LOG_DEBUG("vulkan", "begin_frame: fence wait timed out, skipping frame");
        return false;
    }

    // Acquire next swapchain image BEFORE resetting fence.
    // Bounded timeout prevents indefinite stalls when the compositor
    // is slow to deliver images (resize storms, minimized windows).
    if (profiler)
        profiler->begin_stage("vk_acquire");
    VkResult result = vkAcquireNextImageKHR(
        ctx_.device,
        active_window_->swapchain.swapchain,
        ACQUIRE_TIMEOUT_NS,
        active_window_->image_available_semaphores[active_window_->current_flight_frame],
        VK_NULL_HANDLE,
        &active_window_->current_image_index);
    if (profiler)
        profiler->end_stage("vk_acquire");

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        active_window_->swapchain_invalidated = true;
        return false;   // Swapchain truly unusable — caller must recreate
    }
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        // The surface itself is gone (compositor restart, display change).
        // Mark for full surface + swapchain recreation.
        active_window_->surface_lost          = true;
        active_window_->swapchain_dirty       = true;
        active_window_->swapchain_invalidated = true;
        SPECTRA_LOG_WARN("vulkan", "begin_frame: acquire returned SURFACE_LOST, will recreate");
        return false;
    }
    if (result == VK_TIMEOUT || result == VK_NOT_READY)
    {
        // Compositor hasn't delivered an image within the timeout.
        // Skip this window for the current tick — don't block the loop.
        SPECTRA_LOG_DEBUG("vulkan", "begin_frame: acquire timed out, skipping frame");
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        // Surface size no longer matches the swapchain.  Skip this frame and
        // let the caller recreate — presenting a stretched image causes
        // visible brightness flicker and misaligned UI chrome during resize.
        active_window_->swapchain_dirty       = true;
        active_window_->swapchain_invalidated = true;
        return false;
    }

    // Only reset fence after successful acquisition
    vkResetFences(ctx_.device,
                  1,
                  &active_window_->in_flight_fences[active_window_->current_flight_frame]);

    active_window_->current_cmd =
        active_window_->command_buffers[active_window_->current_flight_frame];
    vkResetCommandBuffer(active_window_->current_cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(active_window_->current_cmd, &begin_info);

    return true;
}

void VulkanBackend::end_frame(FrameProfiler* profiler)
{
    vkEndCommandBuffer(active_window_->current_cmd);

    if (headless_)
    {
        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &active_window_->current_cmd;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);

        // One completed submission; safe-point progression for deferred frees.
        // Keep headless deletion conservative: defer actual descriptor frees
        // to shutdown/explicit flush points to avoid per-frame free churn on
        // software Vulkan drivers used in CI golden tests.
        ++frame_counter_;
        return;
    }

    // Windowed submit
    // image_available: indexed by active_window_->current_flight_frame (matches acquire)
    // render_finished: indexed by active_window_->current_image_index (tied to swapchain image
    // lifecycle —
    //   only reused when that image is re-acquired, guaranteeing the previous present completed)
    VkSemaphore wait_semaphores[] = {
        active_window_->image_available_semaphores[active_window_->current_flight_frame]};
    VkSemaphore signal_semaphores[] = {
        active_window_->render_finished_semaphores[active_window_->current_image_index]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = wait_semaphores;
    submit.pWaitDstStageMask    = wait_stages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &active_window_->current_cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signal_semaphores;

    if (profiler)
        profiler->begin_stage("vk_submit");
    VkResult submit_result =
        vkQueueSubmit(ctx_.graphics_queue,
                      1,
                      &submit,
                      active_window_->in_flight_fences[active_window_->current_flight_frame]);
    if (profiler)
        profiler->end_stage("vk_submit");

    if (submit_result != VK_SUCCESS)
    {
        if (submit_result == VK_ERROR_DEVICE_LOST)
        {
            device_lost_ = true;
            throw std::runtime_error("Vulkan device lost - submit failed");
        }

        SPECTRA_LOG_ERROR("vulkan",
                          "end_frame: submit failed with result "
                              + std::to_string(static_cast<int>(submit_result)));
        return;
    }

    // Progress deferred deletion only when work was actually submitted.
    ++frame_counter_;
    flush_pending_buffer_frees();

    // Save the image index being presented so readback_framebuffer can
    // read the correct (last-rendered) image after acquire updates current_image_index.
    active_window_->last_presented_image_idx = active_window_->current_image_index;

    // If a framebuffer capture was requested, perform it now — after GPU submit
    // (rendering complete) but before present (image still owned by us).
    if (pending_capture_.buffer)
    {
        do_capture_before_present();
    }

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signal_semaphores;
    present.swapchainCount     = 1;
    present.pSwapchains        = &active_window_->swapchain.swapchain;
    present.pImageIndices      = &active_window_->current_image_index;

    if (profiler)
        profiler->begin_stage("vk_present");
    VkResult result = vkQueuePresentKHR(ctx_.present_queue, &present);
    if (profiler)
        profiler->end_stage("vk_present");

    active_window_->current_flight_frame =
        (active_window_->current_flight_frame + 1)
        % static_cast<uint32_t>(active_window_->in_flight_fences.size());

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        active_window_->swapchain_dirty       = true;
        active_window_->swapchain_invalidated = true;
        SPECTRA_LOG_DEBUG("vulkan", "end_frame: present returned OUT_OF_DATE");
    }
    else if (result == VK_SUBOPTIMAL_KHR)
    {
        active_window_->swapchain_dirty = true;
        SPECTRA_LOG_DEBUG("vulkan", "end_frame: present returned SUBOPTIMAL");
    }
    else if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        // The surface itself is gone (compositor restart, display change).
        // Mark for full surface + swapchain recreation on the next tick
        // instead of erroring forever on a dead surface.
        active_window_->surface_lost          = true;
        active_window_->swapchain_dirty       = true;
        active_window_->swapchain_invalidated = true;
        SPECTRA_LOG_WARN("vulkan", "end_frame: present returned SURFACE_LOST, will recreate");
    }
    else if (result == VK_ERROR_DEVICE_LOST)
    {
        device_lost_ = true;
        throw std::runtime_error("Vulkan device lost - present failed");
    }
    else if (result != VK_SUCCESS)
    {
        SPECTRA_LOG_ERROR(
            "vulkan",
            "end_frame: present failed with result " + std::to_string(static_cast<int>(result)));
    }
}

void VulkanBackend::begin_render_pass(const Color& clear_color)
{
    VkClearValue clear_values[2]{};
    clear_values[0].color        = {{clear_color.r, clear_color.g, clear_color.b, clear_color.a}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.clearValueCount = 2;
    info.pClearValues    = clear_values;

    if (headless_)
    {
        info.renderPass  = offscreen_.render_pass;
        info.framebuffer = offscreen_.framebuffer;
        info.renderArea  = {{0, 0}, offscreen_.extent};
    }
    else
    {
        info.renderPass = active_window_->swapchain.render_pass;
        info.framebuffer =
            active_window_->swapchain.framebuffers[active_window_->current_image_index];
        info.renderArea = {{0, 0}, active_window_->swapchain.extent};
    }

    vkCmdBeginRenderPass(active_window_->current_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBackend::end_render_pass()
{
    vkCmdEndRenderPass(active_window_->current_cmd);
}

void VulkanBackend::bind_pipeline(PipelineHandle handle)
{
    auto it = pipelines_.find(handle.id);
    if (it != pipelines_.end() && it->second != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(active_window_->current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
        auto layout_it = pipeline_layouts_.find(handle.id);
        current_pipeline_layout_ =
            (layout_it != pipeline_layouts_.end()) ? layout_it->second : pipeline_layout_;
    }
}

void VulkanBackend::bind_buffer(BufferHandle handle, uint32_t binding)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;

    VkPipelineLayout layout =
        current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
    if (entry.usage == BufferUsage::Uniform && entry.descriptor_set != VK_NULL_HANDLE)
    {
        uint32_t dynamic_offset = ubo_bound_offset_;
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                0,
                                1,
                                &entry.descriptor_set,
                                1,
                                &dynamic_offset);
    }
    else if (entry.usage == BufferUsage::Storage && entry.descriptor_set != VK_NULL_HANDLE)
    {
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                1,
                                1,
                                &entry.descriptor_set,
                                0,
                                nullptr);
    }
    else if (entry.usage == BufferUsage::Vertex)
    {
        // Only actual vertex buffers may be bound as vertex buffers.
        VkBuffer     bufs[]    = {entry.gpu_buffer.buffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(active_window_->current_cmd, binding, 1, bufs, offsets);
    }
    // Storage/Uniform with NULL descriptor: silently skip (pool exhausted).
}

void VulkanBackend::bind_index_buffer(BufferHandle handle)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;
    vkCmdBindIndexBuffer(active_window_->current_cmd,
                         entry.gpu_buffer.buffer(),
                         0,
                         VK_INDEX_TYPE_UINT32);
}

void VulkanBackend::bind_texture(TextureHandle handle, uint32_t /*binding*/)
{
    auto it = textures_.find(handle.id);
    if (it == textures_.end())
        return;

    auto& tex = it->second;
    if (tex.descriptor_set != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout =
            current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
        vkCmdBindDescriptorSets(active_window_->current_cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout,
                                1,
                                1,
                                &tex.descriptor_set,
                                0,
                                nullptr);
    }
}

void VulkanBackend::push_constants(const SeriesPushConstants& pc)
{
    VkPipelineLayout layout =
        current_pipeline_layout_ ? current_pipeline_layout_ : pipeline_layout_;
    vkCmdPushConstants(active_window_->current_cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(SeriesPushConstants),
                       &pc);
}

void VulkanBackend::set_viewport(float x, float y, float width, float height)
{
    VkViewport vp{};
    vp.x        = x;
    vp.y        = y;
    vp.width    = width;
    vp.height   = height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(active_window_->current_cmd, 0, 1, &vp);
}

void VulkanBackend::set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(active_window_->current_cmd, 0, 1, &scissor);
}

void VulkanBackend::set_line_width(float width)
{
    vkCmdSetLineWidth(active_window_->current_cmd, width);
}

void VulkanBackend::draw(uint32_t vertex_count, uint32_t first_vertex)
{
    vkCmdDraw(active_window_->current_cmd, vertex_count, 1, first_vertex, 0);
}

void VulkanBackend::draw_instanced(uint32_t vertex_count,
                                   uint32_t instance_count,
                                   uint32_t first_vertex,
                                   uint32_t first_instance)
{
    vkCmdDraw(active_window_->current_cmd,
              vertex_count,
              instance_count,
              first_vertex,
              first_instance);
}

void VulkanBackend::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset)
{
    vkCmdDrawIndexed(active_window_->current_cmd, index_count, 1, first_index, vertex_offset, 0);
}

uint32_t VulkanBackend::swapchain_width() const
{
    if (headless_)
        return offscreen_.extent.width;
    return active_window_->swapchain.extent.width;
}

uint32_t VulkanBackend::swapchain_height() const
{
    if (headless_)
        return offscreen_.extent.height;
    return active_window_->swapchain.extent.height;
}

uint32_t VulkanBackend::current_flight_frame() const
{
    return active_window_ ? active_window_->current_flight_frame : 0;
}

uint32_t VulkanBackend::max_frames_in_flight() const
{
    return MAX_FRAMES_IN_FLIGHT;
}

VkRenderPass VulkanBackend::render_pass() const
{
    if (headless_)
        return offscreen_.render_pass;
    return active_window_->swapchain.render_pass;
}

bool VulkanBackend::query_gpu_memory_stats(GpuMemoryStats& stats) const
{
    stats = {};
    if (vma_allocator_ == nullptr)
        return false;

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(vma_allocator_, budgets);

    const uint32_t heap_count =
        std::min<uint32_t>(ctx_.memory_properties.memoryHeapCount, VK_MAX_MEMORY_HEAPS);
    stats.heap_count               = heap_count;
    stats.budget_extension_enabled = memory_budget_extension_enabled_;

    for (uint32_t i = 0; i < heap_count; ++i)
    {
        const VkMemoryHeap& heap = ctx_.memory_properties.memoryHeaps[i];
        stats.total_usage_bytes += budgets[i].usage;
        stats.total_budget_bytes += budgets[i].budget;
        if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
        {
            stats.device_local_usage_bytes += budgets[i].usage;
            stats.device_local_budget_bytes += budgets[i].budget;
        }
    }

    return heap_count > 0;
}

}   // namespace spectra
