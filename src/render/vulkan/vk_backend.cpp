#include "vk_backend.hpp"

#include <spectra/logger.hpp>

#include "shader_spirv.hpp"
#include "platform/window_system/surface_host.hpp"

#ifdef SPECTRA_USE_GLFW
    #include "platform/window_system/glfw_surface_host.hpp"
#endif
#ifdef SPECTRA_USE_SDL3
    #include "platform/window_system/sdl3_surface_host.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #ifdef SPECTRA_USE_GLFW
        #include <imgui_impl_glfw.h>
    #elif defined(SPECTRA_USE_SDL3)
        #include <imgui_impl_sdl3.h>
    #endif
    #include <imgui_impl_vulkan.h>
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

// WindowUIContext must be complete for unique_ptr destructor in WindowContext.
// Must come after GLFW includes to avoid macro conflicts with shortcut_manager.hpp.
#include "../../anim/frame_profiler.hpp"
#include "ui/app/window_ui_context.hpp"

namespace spectra
{

static bool has_device_extension(VkPhysicalDevice physical_device, const char* extension_name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());

    return std::any_of(extensions.begin(),
                       extensions.end(),
                       [extension_name](const VkExtensionProperties& ext)
                       { return std::strcmp(ext.extensionName, extension_name) == 0; });
}

// Out-of-line destructor so unique_ptr<WindowUIContext> sees the complete type.
WindowContext::~WindowContext() = default;

std::unique_ptr<WindowContext> VulkanBackend::create_window_context()
{
    return std::make_unique<WindowContext>();
}

VulkanBackend::VulkanBackend()
    : initial_window_(std::make_unique<WindowContext>()), active_window_(initial_window_.get())
{
#ifdef SPECTRA_USE_GLFW
    static const platform::GlfwSurfaceHost k_default_glfw_surface_host;
    surface_host_ = &k_default_glfw_surface_host;
#elif defined(SPECTRA_USE_SDL3)
    static const platform::Sdl3SurfaceHost k_default_sdl3_surface_host;
    surface_host_ = &k_default_sdl3_surface_host;
#endif
}

VulkanBackend::~VulkanBackend()
{
    try
    {
        shutdown();
    }
    catch (...)
    {
        // Swallow exceptions — destructor is noexcept.
    }
}

bool VulkanBackend::init(bool headless)
{
    headless_ = headless;

    SPECTRA_LOG_INFO(
        "vulkan",
        "Initializing Vulkan backend (headless: " + std::string(headless ? "true" : "false") + ")");

    try
    {
        // Validation layers add several seconds to startup on some drivers
        // (notably ~6s on NVIDIA).  They are now opt-in even in debug builds:
        // set SPECTRA_VALIDATION=1 to enable.  The legacy SPECTRA_NO_VALIDATION
        // variable is still honored for back-compat.
        bool enable_validation = false;
        if (const char* env = std::getenv("SPECTRA_VALIDATION"); env && env[0] == '1')
            enable_validation = true;
        if (const char* env = std::getenv("SPECTRA_NO_VALIDATION"); env && env[0] == '1')
            enable_validation = false;

        SPECTRA_LOG_INFO("vulkan",
                         std::string("Validation layers: ")
                             + (enable_validation ? "enabled" : "disabled")
                             + " (set SPECTRA_VALIDATION=1 to enable)");

        ctx_.instance = vk::create_instance(enable_validation, headless_, surface_host_);

        if (enable_validation)
        {
            ctx_.debug_messenger = vk::create_debug_messenger(ctx_.instance);
            SPECTRA_LOG_DEBUG("vulkan", "Debug messenger created");
        }

        // For headless, pick device without surface
        ctx_.physical_device = vk::pick_physical_device(ctx_.instance, active_window_->surface);
        ctx_.queue_families =
            vk::find_queue_families(ctx_.physical_device, active_window_->surface);

        // When not headless, force swapchain extension even though surface doesn't exist yet
        // (surface is created later by GLFW adapter, but device needs the extension at creation
        // time). Also pick a graphics queue family that supports presentation for windows
        // created by the active surface host, so the later surface-based verification matches
        // and we don't have to fall back to using the graphics queue for present.
        if (!headless_)
        {
            if (surface_host_ && ctx_.queue_families.graphics.has_value()
                && !surface_host_->query_presentation_support(ctx_.instance,
                                                              ctx_.physical_device,
                                                              ctx_.queue_families.graphics.value()))
            {
                uint32_t count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physical_device, &count, nullptr);
                std::vector<VkQueueFamilyProperties> families(count);
                vkGetPhysicalDeviceQueueFamilyProperties(ctx_.physical_device,
                                                         &count,
                                                         families.data());
                for (uint32_t i = 0; i < count; ++i)
                {
                    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        && surface_host_->query_presentation_support(ctx_.instance,
                                                                     ctx_.physical_device,
                                                                     i))
                    {
                        ctx_.queue_families.graphics = i;
                        if (!ctx_.queue_families.transfer.has_value())
                            ctx_.queue_families.transfer = i;
                        break;
                    }
                }
            }
            ctx_.queue_families.present = ctx_.queue_families.graphics;
        }
        ctx_.device =
            vk::create_logical_device(ctx_.physical_device, ctx_.queue_families, enable_validation);

        vkGetDeviceQueue(ctx_.device,
                         ctx_.queue_families.graphics.value(),
                         0,
                         &ctx_.graphics_queue);
        if (ctx_.queue_families.has_present())
        {
            vkGetDeviceQueue(ctx_.device,
                             ctx_.queue_families.present.value(),
                             0,
                             &ctx_.present_queue);
        }

        vkGetPhysicalDeviceProperties(ctx_.physical_device, &ctx_.properties);
        vkGetPhysicalDeviceMemoryProperties(ctx_.physical_device, &ctx_.memory_properties);
        memory_budget_extension_enabled_ =
            has_device_extension(ctx_.physical_device, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

        VmaAllocatorCreateInfo allocator_info{};
        allocator_info.physicalDevice   = ctx_.physical_device;
        allocator_info.device           = ctx_.device;
        allocator_info.instance         = ctx_.instance;
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_2;
        if (memory_budget_extension_enabled_)
        {
            allocator_info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        }
        VkResult allocator_result = vmaCreateAllocator(&allocator_info, &vma_allocator_);
        if (allocator_result != VK_SUCCESS)
        {
            vma_allocator_ = nullptr;
            SPECTRA_LOG_WARN("vulkan",
                             "VMA allocator creation failed; GPU budget telemetry disabled");
        }

        // Query alignment for dynamic UBO offsets — round up FrameUBO size
        // to the device's minUniformBufferOffsetAlignment
        {
            VkDeviceSize align = ctx_.properties.limits.minUniformBufferOffsetAlignment;
            if (align == 0)
                align = 1;
            ubo_slot_alignment_ = (sizeof(FrameUBO) + align - 1) & ~(align - 1);
        }

        create_command_pool();
        create_descriptor_pool();

        // Create descriptor set layouts and pipeline layouts
        frame_desc_layout_  = vk::create_frame_descriptor_layout(ctx_.device);
        series_desc_layout_ = vk::create_series_descriptor_layout(ctx_.device);
        pipeline_layout_ =
            vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, series_desc_layout_});

        // Texture descriptor set layout (combined image sampler at binding 0)
        {
            VkDescriptorSetLayoutBinding sampler_binding{};
            sampler_binding.binding         = 0;
            sampler_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_binding.descriptorCount = 1;
            sampler_binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 1;
            layout_info.pBindings    = &sampler_binding;

            if (vkCreateDescriptorSetLayout(ctx_.device,
                                            &layout_info,
                                            nullptr,
                                            &texture_desc_layout_)
                != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create texture descriptor set layout");
            }
        }

        // Text pipeline layout: set 0 = frame UBO, set 1 = texture sampler
        text_pipeline_layout_ =
            vk::create_pipeline_layout(ctx_.device, {frame_desc_layout_, texture_desc_layout_});

        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Backend init failed: {}", e.what());
        return false;
    }
}

void VulkanBackend::wait_idle()
{
    if (ctx_.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(ctx_.device);
    }
}

void VulkanBackend::shutdown()
{
    if (ctx_.device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(ctx_.device);

    // GPU is fully idle — flush all deferred buffer deletions
    flush_pending_buffer_frees(/*force_all=*/true);

    // Destroy pipelines
    for (auto& [id, pipeline] : pipelines_)
    {
        vkDestroyPipeline(ctx_.device, pipeline, nullptr);
    }
    pipelines_.clear();

    // Destroy buffers (GpuBuffer destructors handle Vulkan cleanup)
    buffers_.clear();

    // Destroy textures
    for (auto& [id, tex] : textures_)
    {
        if (tex.sampler != VK_NULL_HANDLE)
            vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)
            vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)
            vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)
            vkFreeMemory(ctx_.device, tex.memory, nullptr);
    }
    textures_.clear();

    // Destroy sync objects and per-window Vulkan resources.
    // If the window was released to WindowManager, it already cleaned up.
    if (active_window_)
    {
        for (auto sem : active_window_->image_available_semaphores)
            vkDestroySemaphore(ctx_.device, sem, nullptr);
        for (auto sem : active_window_->render_finished_semaphores)
            vkDestroySemaphore(ctx_.device, sem, nullptr);
        for (auto fence : active_window_->in_flight_fences)
            vkDestroyFence(ctx_.device, fence, nullptr);
        active_window_->image_available_semaphores.clear();
        active_window_->render_finished_semaphores.clear();
        active_window_->in_flight_fences.clear();
    }

    // Destroy layouts
    if (text_pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, text_pipeline_layout_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
    if (texture_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, texture_desc_layout_, nullptr);
    if (series_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, series_desc_layout_, nullptr);
    if (frame_desc_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx_.device, frame_desc_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);

    if (command_pool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx_.device, command_pool_, nullptr);

    vk::destroy_offscreen(ctx_.device, offscreen_);

    if (active_window_)
    {
        vk::destroy_swapchain(ctx_.device, active_window_->swapchain);
        destroy_surface_for(*active_window_);
    }

    if (vma_allocator_ != nullptr)
    {
        vmaDestroyAllocator(vma_allocator_);
        vma_allocator_ = nullptr;
    }

    vkDestroyDevice(ctx_.device, nullptr);

    if (ctx_.debug_messenger != VK_NULL_HANDLE)
        vk::destroy_debug_messenger(ctx_.instance, ctx_.debug_messenger);

    vkDestroyInstance(ctx_.instance, nullptr);

    ctx_                             = {};
    memory_budget_extension_enabled_ = false;
}

bool VulkanBackend::create_surface(void* native_window)
{
    if (!native_window || !active_window_)
        return false;
    if (!surface_host_)
        return false;

    active_window_->native_window = native_window;
    if (!surface_host_->create_surface(ctx_.instance, native_window, active_window_->surface))
    {
        SPECTRA_LOG_ERROR(
            "vulkan",
            "Failed to create Vulkan surface with host: " + std::string(surface_host_->name()));
        return false;
    }
    surface_host_->on_surface_created(native_window, active_window_->surface);

    // Re-query present support for the created surface, but keep device-created queue
    // family indices stable. The logical device was created before surface creation,
    // so it may not contain a separately discovered present family index.
    const auto surface_families =
        vk::find_queue_families(ctx_.physical_device, active_window_->surface);
    if (surface_families.has_present()
        && surface_families.present.value() == ctx_.queue_families.graphics.value())
    {
        ctx_.queue_families.present = ctx_.queue_families.graphics;
        vkGetDeviceQueue(ctx_.device, ctx_.queue_families.present.value(), 0, &ctx_.present_queue);
    }
    else
    {
        if (surface_families.has_present())
        {
            SPECTRA_LOG_WARN("vulkan",
                             "Surface present queue family differs from device queue family; "
                             "falling back to graphics queue for present operations");
        }
        ctx_.queue_families.present = ctx_.queue_families.graphics;
        ctx_.present_queue          = ctx_.graphics_queue;
    }

    // Ensure present queue is always valid.
    if (ctx_.present_queue == VK_NULL_HANDLE)
    {
        ctx_.present_queue = ctx_.graphics_queue;
    }

    return true;
}

void VulkanBackend::destroy_surface_for(WindowContext& wctx)
{
    if (wctx.surface == VK_NULL_HANDLE || ctx_.instance == VK_NULL_HANDLE)
    {
        return;
    }

    if (surface_host_)
    {
        surface_host_->on_surface_about_to_destroy(wctx.native_window, wctx.surface);
        surface_host_->destroy_surface(ctx_.instance, wctx.surface);
    }
    else
    {
        vkDestroySurfaceKHR(ctx_.instance, wctx.surface, nullptr);
    }

    wctx.surface = VK_NULL_HANDLE;
}

bool VulkanBackend::query_framebuffer_size(void*     native_window,
                                           uint32_t& width,
                                           uint32_t& height) const
{
    width  = 0;
    height = 0;

    if (!surface_host_ || !native_window)
    {
        return false;
    }

    platform::SurfaceSize size{};
    if (!surface_host_->framebuffer_size(native_window, size))
    {
        return false;
    }

    width  = size.width;
    height = size.height;
    return width > 0 && height > 0;
}

bool VulkanBackend::query_window_framebuffer_size(const WindowContext& wctx,
                                                  uint32_t&            width,
                                                  uint32_t&            height) const
{
    if (query_framebuffer_size(wctx.native_window, width, height))
    {
        return true;
    }

    if (wctx.swapchain.extent.width > 0 && wctx.swapchain.extent.height > 0)
    {
        width  = wctx.swapchain.extent.width;
        height = wctx.swapchain.extent.height;
        return true;
    }

    width  = wctx.pending_width;
    height = wctx.pending_height;
    return width > 0 && height > 0;
}

bool VulkanBackend::create_swapchain(uint32_t width, uint32_t height)
{
    if (active_window_->surface == VK_NULL_HANDLE)
        return false;

    try
    {
        auto vk_msaa              = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        active_window_->swapchain = vk::create_swapchain(
            ctx_.device,
            ctx_.physical_device,
            active_window_->surface,
            width,
            height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            vk_msaa);
        create_command_buffers();
        create_sync_objects();
        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Swapchain creation failed: {}", e.what());
        return false;
    }
}

bool VulkanBackend::recreate_swapchain(uint32_t width, uint32_t height)
{
    SPECTRA_LOG_DEBUG(
        "vulkan",
        "recreate_swapchain called: " + std::to_string(width) + "x" + std::to_string(height));

    // Wait only on in-flight fences instead of vkDeviceWaitIdle (much faster)
    if (!active_window_->in_flight_fences.empty())
    {
        SPECTRA_LOG_DEBUG("vulkan",
                          "Waiting for " + std::to_string(active_window_->in_flight_fences.size())
                              + " in-flight fences before swapchain recreation");
        auto wait_start = std::chrono::high_resolution_clock::now();
        vkWaitForFences(ctx_.device,
                        static_cast<uint32_t>(active_window_->in_flight_fences.size()),
                        active_window_->in_flight_fences.data(),
                        VK_TRUE,
                        UINT64_MAX);
        auto wait_end = std::chrono::high_resolution_clock::now();
        auto wait_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(wait_end - wait_start);
        SPECTRA_LOG_DEBUG(
            "vulkan",
            "Fence wait completed in " + std::to_string(wait_duration.count()) + "ms");
    }

    SPECTRA_LOG_DEBUG("vulkan", "Starting swapchain recreation...");
    auto         old_swapchain = active_window_->swapchain.swapchain;
    auto         old_context   = active_window_->swapchain;   // Copy the entire context
    VkRenderPass reuse_rp      = old_context.render_pass;     // Reuse — format doesn't change

    // Surface lost (compositor restart, display change): the surface itself
    // is dead, so the old swapchain cannot be reused as oldSwapchain and the
    // surface must be recreated from the native window before the new
    // swapchain is created.  Fences were already waited above.
    const bool surface_was_lost = active_window_->surface_lost;
    if (surface_was_lost)
    {
        SPECTRA_LOG_WARN("vulkan", "Surface lost — recreating surface before swapchain");

        // Destroy the old swapchain first (it belongs to the dead surface)
        vk::destroy_swapchain(ctx_.device, old_context, /*skip_render_pass=*/true);
        active_window_->swapchain.swapchain = VK_NULL_HANDLE;
        old_swapchain                       = VK_NULL_HANDLE;

        destroy_surface_for(*active_window_);
        // create_surface() also re-queries present-queue support for the new surface
        if (!create_surface(active_window_->native_window))
        {
            SPECTRA_LOG_ERROR("vulkan", "Surface recreation failed after SURFACE_LOST");
            return false;
        }
        active_window_->surface_lost = false;
    }

    try
    {
        SPECTRA_LOG_DEBUG("vulkan", "Creating new swapchain...");
        auto vk_msaa              = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        active_window_->swapchain = vk::create_swapchain(
            ctx_.device,
            ctx_.physical_device,
            active_window_->surface,
            width,
            height,
            ctx_.queue_families.graphics.value(),
            ctx_.queue_families.present.value_or(ctx_.queue_families.graphics.value()),
            old_swapchain,
            reuse_rp,
            vk_msaa);
        SPECTRA_LOG_DEBUG(
            "vulkan",
            "New swapchain created: " + std::to_string(active_window_->swapchain.extent.width) + "x"
                + std::to_string(active_window_->swapchain.extent.height));

        // Destroy the old swapchain context (skip render pass — we reused it).
        // Already destroyed in the surface-lost path above.
        if (!surface_was_lost)
        {
            SPECTRA_LOG_DEBUG("vulkan", "Destroying old swapchain...");
            vk::destroy_swapchain(ctx_.device, old_context, /*skip_render_pass=*/true);
        }

        // Recreate sync objects only if image count changed (rare during resize)
        if (active_window_->swapchain.images.size() != old_context.images.size())
        {
            SPECTRA_LOG_DEBUG("vulkan",
                              "Image count changed " + std::to_string(old_context.images.size())
                                  + " -> " + std::to_string(active_window_->swapchain.images.size())
                                  + ", recreating sync objects");
            for (auto sem : active_window_->image_available_semaphores)
                vkDestroySemaphore(ctx_.device, sem, nullptr);
            for (auto sem : active_window_->render_finished_semaphores)
                vkDestroySemaphore(ctx_.device, sem, nullptr);
            for (auto fence : active_window_->in_flight_fences)
                vkDestroyFence(ctx_.device, fence, nullptr);
            active_window_->image_available_semaphores.clear();
            active_window_->render_finished_semaphores.clear();
            active_window_->in_flight_fences.clear();
            create_sync_objects();
        }
        active_window_->current_flight_frame  = 0;
        active_window_->swapchain_invalidated = false;

        SPECTRA_LOG_DEBUG("vulkan", "Swapchain recreation completed successfully");
        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Swapchain recreation failed: " + std::string(e.what()));
        return false;
    }
}

bool VulkanBackend::create_offscreen_framebuffer(uint32_t width, uint32_t height)
{
    try
    {
        vk::destroy_offscreen(ctx_.device, offscreen_);
        auto vk_msaa = static_cast<VkSampleCountFlagBits>(msaa_samples_);
        offscreen_   = vk::create_offscreen_framebuffer(ctx_.device,
                                                      ctx_.physical_device,
                                                      width,
                                                      height,
                                                      vk_msaa);
        create_command_buffers();
        create_sync_objects();
        return true;
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Offscreen framebuffer creation failed: {}", e.what());
        return false;
    }
}

PipelineHandle VulkanBackend::create_pipeline(PipelineType type)
{
    PipelineHandle h;
    h.id = next_pipeline_id_++;

    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE)
    {
        // Render pass not yet available — store placeholder, will be created lazily
        pipelines_[h.id]      = VK_NULL_HANDLE;
        pipeline_types_[h.id] = type;
        return h;
    }

    pipelines_[h.id]        = create_pipeline_for_type(type, rp);
    pipeline_layouts_[h.id] = (type == PipelineType::Text || type == PipelineType::TextDepth
                               || type == PipelineType::Image3D)
                                  ? text_pipeline_layout_
                                  : pipeline_layout_;
    return h;
}

PipelineHandle VulkanBackend::create_custom_pipeline(const CustomPipelineDesc& desc)
{
    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE)
    {
        SPECTRA_LOG_WARN("vk_backend", "Cannot create custom pipeline — no render pass");
        return PipelineHandle{};
    }

    if (!desc.vert_spirv || desc.vert_spirv_size == 0 || !desc.frag_spirv
        || desc.frag_spirv_size == 0)
    {
        SPECTRA_LOG_WARN("vk_backend", "Custom pipeline missing SPIR-V shaders");
        return PipelineHandle{};
    }

    vk::PipelineConfig cfg;
    cfg.render_pass          = rp;
    cfg.pipeline_layout      = pipeline_layout_;
    cfg.vert_spirv           = desc.vert_spirv;
    cfg.vert_spirv_size      = desc.vert_spirv_size;
    cfg.frag_spirv           = desc.frag_spirv;
    cfg.frag_spirv_size      = desc.frag_spirv_size;
    cfg.topology             = static_cast<VkPrimitiveTopology>(desc.topology);
    cfg.enable_blending      = desc.enable_blending;
    cfg.enable_depth_test    = desc.enable_depth_test;
    cfg.enable_depth_write   = desc.enable_depth_write;
    cfg.enable_backface_cull = desc.enable_backface_cull;
    cfg.msaa_samples         = static_cast<VkSampleCountFlagBits>(msaa_samples_);

    // Convert vertex bindings
    for (uint32_t i = 0; i < desc.vertex_binding_count && desc.vertex_bindings; ++i)
    {
        const auto&                     b = desc.vertex_bindings[i];
        VkVertexInputBindingDescription vb{};
        vb.binding = b.binding;
        vb.stride  = b.stride;
        vb.inputRate =
            b.input_rate == 0 ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
        cfg.vertex_bindings.push_back(vb);
    }

    // Convert vertex attributes
    for (uint32_t i = 0; i < desc.vertex_attribute_count && desc.vertex_attributes; ++i)
    {
        const auto&                       a = desc.vertex_attributes[i];
        VkVertexInputAttributeDescription va{};
        va.location = a.location;
        va.binding  = a.binding;
        va.format   = static_cast<VkFormat>(a.format);
        va.offset   = a.offset;
        cfg.vertex_attributes.push_back(va);
    }

    VkPipeline vk_pipeline = vk::create_graphics_pipeline(ctx_.device, cfg);
    if (vk_pipeline == VK_NULL_HANDLE)
    {
        return PipelineHandle{};
    }

    PipelineHandle h;
    h.id                    = next_pipeline_id_++;
    pipelines_[h.id]        = vk_pipeline;
    pipeline_layouts_[h.id] = pipeline_layout_;
    return h;
}

void VulkanBackend::destroy_pipeline(PipelineHandle handle)
{
    if (!handle)
        return;

    auto it = pipelines_.find(handle.id);
    if (it != pipelines_.end())
    {
        if (it->second != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx_.device, it->second, nullptr);
        }
        pipelines_.erase(it);
        pipeline_types_.erase(handle.id);
        pipeline_layouts_.erase(handle.id);
    }
}

VkPipeline VulkanBackend::create_pipeline_for_type(PipelineType type, VkRenderPass rp)
{
    vk::PipelineConfig cfg;
    cfg.render_pass     = rp;
    cfg.pipeline_layout = pipeline_layout_;
    cfg.enable_blending = true;
    cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    switch (type)
    {
        case PipelineType::Line:
            cfg.vert_spirv      = shaders::line_vert;
            cfg.vert_spirv_size = shaders::line_vert_size;
            cfg.frag_spirv      = shaders::line_frag;
            cfg.frag_spirv_size = shaders::line_frag_size;
            break;
        case PipelineType::Scatter:
            cfg.vert_spirv      = shaders::scatter_vert;
            cfg.vert_spirv_size = shaders::scatter_vert_size;
            cfg.frag_spirv      = shaders::scatter_frag;
            cfg.frag_spirv_size = shaders::scatter_frag_size;
            break;
        case PipelineType::ScatterColormap:
            cfg.vert_spirv      = shaders::scatter_colormap_vert;
            cfg.vert_spirv_size = shaders::scatter_colormap_vert_size;
            cfg.frag_spirv      = shaders::scatter_frag;
            cfg.frag_spirv_size = shaders::scatter_frag_size;
            break;
        case PipelineType::Grid:
            cfg.vert_spirv      = shaders::grid_vert;
            cfg.vert_spirv_size = shaders::grid_vert_size;
            cfg.frag_spirv      = shaders::grid_frag;
            cfg.frag_spirv_size = shaders::grid_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            // Grid uses vec2 vertex attribute for line endpoints
            cfg.vertex_bindings.push_back({0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            break;
        case PipelineType::Line3D:
            cfg.vert_spirv         = shaders::line3d_vert;
            cfg.vert_spirv_size    = shaders::line3d_vert_size;
            cfg.frag_spirv         = shaders::line3d_frag;
            cfg.frag_spirv_size    = shaders::line3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case PipelineType::Scatter3D:
            cfg.vert_spirv         = shaders::scatter3d_vert;
            cfg.vert_spirv_size    = shaders::scatter3d_vert_size;
            cfg.frag_spirv         = shaders::scatter3d_frag;
            cfg.frag_spirv_size    = shaders::scatter3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            break;
        case PipelineType::Grid3D:
            cfg.vert_spirv         = shaders::grid3d_vert;
            cfg.vert_spirv_size    = shaders::grid3d_vert_size;
            cfg.frag_spirv         = shaders::grid3d_frag;
            cfg.frag_spirv_size    = shaders::grid3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Grid3D uses vec3 vertex attribute for line endpoints
            cfg.vertex_bindings.push_back({0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            break;
        case PipelineType::GridOverlay3D:
            cfg.vert_spirv         = shaders::grid3d_vert;
            cfg.vert_spirv_size    = shaders::grid3d_vert_size;
            cfg.frag_spirv         = shaders::grid3d_frag;
            cfg.frag_spirv_size    = shaders::grid3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = false;
            cfg.enable_depth_write = false;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            break;
        case PipelineType::Surface3D:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Surface vertex: {x,y,z, nx,ny,nz} = 6 floats per vertex, 2 attributes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        case PipelineType::Mesh3D:
            cfg.vert_spirv         = shaders::mesh3d_vert;
            cfg.vert_spirv_size    = shaders::mesh3d_vert_size;
            cfg.frag_spirv         = shaders::mesh3d_frag;
            cfg.frag_spirv_size    = shaders::mesh3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Mesh vertex: same layout as surface {x,y,z, nx,ny,nz}
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        // ── Wireframe 3D pipeline variants (line topology with vertex buffer) ──
        case PipelineType::SurfaceWireframe3D:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::SurfaceWireframe3D_Transparent:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        // ── Transparent 3D pipeline variants (depth test ON, depth write OFF) ──
        case PipelineType::Line3D_Transparent:
            cfg.vert_spirv         = shaders::line3d_vert;
            cfg.vert_spirv_size    = shaders::line3d_vert_size;
            cfg.frag_spirv         = shaders::line3d_frag;
            cfg.frag_spirv_size    = shaders::line3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;   // Don't write depth for transparent
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case PipelineType::Scatter3D_Transparent:
            cfg.vert_spirv         = shaders::scatter3d_vert;
            cfg.vert_spirv_size    = shaders::scatter3d_vert_size;
            cfg.frag_spirv         = shaders::scatter3d_frag;
            cfg.frag_spirv_size    = shaders::scatter3d_frag_size;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case PipelineType::Surface3D_Transparent:
            cfg.vert_spirv         = shaders::surface3d_vert;
            cfg.vert_spirv_size    = shaders::surface3d_vert_size;
            cfg.frag_spirv         = shaders::surface3d_frag;
            cfg.frag_spirv_size    = shaders::surface3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::Mesh3D_Transparent:
            cfg.vert_spirv         = shaders::mesh3d_vert;
            cfg.vert_spirv_size    = shaders::mesh3d_vert_size;
            cfg.frag_spirv         = shaders::mesh3d_frag;
            cfg.frag_spirv_size    = shaders::mesh3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::Heatmap:
            return VK_NULL_HANDLE;   // Not yet implemented
        case PipelineType::Overlay:
            cfg.vert_spirv      = shaders::grid_vert;
            cfg.vert_spirv_size = shaders::grid_vert_size;
            cfg.frag_spirv      = shaders::grid_frag;
            cfg.frag_spirv_size = shaders::grid_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            // Same vec2 vertex attribute as Grid, but triangle topology for filled shapes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
            break;
        case PipelineType::StatFill:
            cfg.vert_spirv      = shaders::stat_fill_vert;
            cfg.vert_spirv_size = shaders::stat_fill_vert_size;
            cfg.frag_spirv      = shaders::stat_fill_frag;
            cfg.frag_spirv_size = shaders::stat_fill_frag_size;
            cfg.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            // vec2 position + float alpha = 3 floats per vertex
            cfg.vertex_bindings.push_back({0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 2)});   // alpha
            break;
        case PipelineType::Arrow3D:
            cfg.vert_spirv         = shaders::arrow3d_vert;
            cfg.vert_spirv_size    = shaders::arrow3d_vert_size;
            cfg.frag_spirv         = shaders::arrow3d_frag;
            cfg.frag_spirv_size    = shaders::arrow3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Arrow vertex: {x,y,z, nx,ny,nz} = 6 floats per vertex, 2 attributes
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back({1,
                                             0,
                                             VK_FORMAT_R32G32B32_SFLOAT,
                                             static_cast<uint32_t>(sizeof(float) * 3)});   // normal
            break;
        case PipelineType::Text:
            cfg.pipeline_layout    = text_pipeline_layout_;
            cfg.vert_spirv         = shaders::text_vert;
            cfg.vert_spirv_size    = shaders::text_vert_size;
            cfg.frag_spirv         = shaders::text_frag;
            cfg.frag_spirv_size    = shaders::text_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = false;
            cfg.enable_depth_write = false;
            // TextVertex: {float x, y, z, float u, v, uint32_t col} = 24 bytes
            cfg.vertex_bindings.push_back(
                {0, sizeof(float) * 5 + sizeof(uint32_t), VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back(
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position (x, y, z)
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});   // uv
            cfg.vertex_attributes.push_back(
                {2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(sizeof(float) * 5)});   // color
            break;
        case PipelineType::TextDepth:
            cfg.pipeline_layout    = text_pipeline_layout_;
            cfg.vert_spirv         = shaders::text_vert;
            cfg.vert_spirv_size    = shaders::text_vert_size;
            cfg.frag_spirv         = shaders::text_frag;
            cfg.frag_spirv_size    = shaders::text_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            // TextVertex: {float x, y, z, float u, v, uint32_t col} = 24 bytes
            cfg.vertex_bindings.push_back(
                {0, sizeof(float) * 5 + sizeof(uint32_t), VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back(
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position (x, y, z)
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});   // uv
            cfg.vertex_attributes.push_back(
                {2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(sizeof(float) * 5)});   // color
            break;
        // ── Marker3D pipeline (instanced marker primitives for ROS displays) ──
        case PipelineType::Marker3D:
            cfg.vert_spirv         = shaders::marker3d_vert;
            cfg.vert_spirv_size    = shaders::marker3d_vert_size;
            cfg.frag_spirv         = shaders::marker3d_frag;
            cfg.frag_spirv_size    = shaders::marker3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Same vertex layout as Mesh3D: {pos.xyz, normal.xyz}
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        case PipelineType::Marker3D_Transparent:
            cfg.vert_spirv         = shaders::marker3d_vert;
            cfg.vert_spirv_size    = shaders::marker3d_vert_size;
            cfg.frag_spirv         = shaders::marker3d_frag;
            cfg.frag_spirv_size    = shaders::marker3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            cfg.vertex_bindings.push_back({0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});
            break;
        // ── Point cloud pipeline (per-point color from SSBO) ──
        case PipelineType::PointCloud:
            cfg.vert_spirv         = shaders::pointcloud_vert;
            cfg.vert_spirv_size    = shaders::pointcloud_vert_size;
            cfg.frag_spirv         = shaders::pointcloud_frag;
            cfg.frag_spirv_size    = shaders::pointcloud_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // No vertex attributes — reads from SSBO via gl_VertexIndex
            break;
        case PipelineType::PointCloud_Transparent:
            cfg.vert_spirv         = shaders::pointcloud_vert;
            cfg.vert_spirv_size    = shaders::pointcloud_vert_size;
            cfg.frag_spirv         = shaders::pointcloud_frag;
            cfg.frag_spirv_size    = shaders::pointcloud_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = false;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        // ── Image3D pipeline (textured 3D billboard quad for camera images) ──
        case PipelineType::Image3D:
            cfg.pipeline_layout    = text_pipeline_layout_;
            cfg.vert_spirv         = shaders::image3d_vert;
            cfg.vert_spirv_size    = shaders::image3d_vert_size;
            cfg.frag_spirv         = shaders::image3d_frag;
            cfg.frag_spirv_size    = shaders::image3d_frag_size;
            cfg.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            cfg.enable_depth_test  = true;
            cfg.enable_depth_write = true;
            cfg.depth_compare_op   = VK_COMPARE_OP_LESS;
            // Image3D vertex: {pos.xyz, uv.xy} = 5 floats per vertex
            cfg.vertex_bindings.push_back({0, sizeof(float) * 5, VK_VERTEX_INPUT_RATE_VERTEX});
            cfg.vertex_attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});   // position
            cfg.vertex_attributes.push_back(
                {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(sizeof(float) * 3)});   // uv
            break;
    }

    // All pipelines must match the render pass sample count
    cfg.msaa_samples = static_cast<VkSampleCountFlagBits>(msaa_samples_);

    try
    {
        return vk::create_graphics_pipeline(ctx_.device, cfg);
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("vulkan", "Pipeline creation failed: {}", e.what());
        return VK_NULL_HANDLE;
    }
}

void VulkanBackend::ensure_pipelines()
{
    VkRenderPass rp = render_pass();
    if (rp == VK_NULL_HANDLE)
        return;

    for (auto& [id, pipeline] : pipelines_)
    {
        if (pipeline == VK_NULL_HANDLE)
        {
            auto it = pipeline_types_.find(id);
            if (it != pipeline_types_.end())
            {
                pipeline = create_pipeline_for_type(it->second, rp);
                pipeline_layouts_[id] =
                    (it->second == PipelineType::Text || it->second == PipelineType::TextDepth
                     || it->second == PipelineType::Image3D)
                        ? text_pipeline_layout_
                        : pipeline_layout_;
            }
        }
    }
}

BufferHandle VulkanBackend::create_buffer(BufferUsage usage, size_t size_bytes)
{
    VkBufferUsageFlags    vk_usage  = 0;
    VkMemoryPropertyFlags mem_props = 0;

    switch (usage)
    {
        case BufferUsage::Vertex:
            vk_usage  = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Index:
            vk_usage  = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Uniform:
            vk_usage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            // Allocate enough room for UBO_MAX_SLOTS dynamic slots
            size_bytes = static_cast<size_t>(ubo_slot_alignment_) * UBO_MAX_SLOTS;
            break;
        case BufferUsage::Storage:
            vk_usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case BufferUsage::Staging:
            vk_usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
    }

    auto buf = vk::GpuBuffer::create(ctx_.device,
                                     ctx_.physical_device,
                                     static_cast<VkDeviceSize>(size_bytes),
                                     vk_usage,
                                     mem_props);

    BufferHandle h;
    h.id = next_buffer_id_++;

    BufferEntry entry;
    entry.gpu_buffer = std::move(buf);
    entry.usage      = usage;

    // Allocate descriptor set for UBO or SSBO buffers
    if (usage == BufferUsage::Uniform)
    {
        entry.descriptor_set = allocate_descriptor_set(frame_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE)
        {
            // Descriptor range = one aligned slot (dynamic offset selects the slot)
            update_ubo_descriptor(entry.descriptor_set,
                                  entry.gpu_buffer.buffer(),
                                  ubo_slot_alignment_);
        }
    }
    else if (usage == BufferUsage::Storage)
    {
        entry.descriptor_set = allocate_descriptor_set(series_desc_layout_);
        if (entry.descriptor_set != VK_NULL_HANDLE)
        {
            update_ssbo_descriptor(entry.descriptor_set,
                                   entry.gpu_buffer.buffer(),
                                   static_cast<VkDeviceSize>(size_bytes));
        }
    }

    buffers_.emplace(h.id, std::move(entry));
    return h;
}

void VulkanBackend::destroy_buffer(BufferHandle handle)
{
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end())
    {
        // Defer both the VkBuffer destruction and descriptor set free.
        // The entry is stamped with the current frame counter and will only
        // be freed once flight_count_ frames have elapsed, guaranteeing
        // every in-flight command buffer has completed.
        // VUID-vkDestroyBuffer-buffer-00922 / VUID-vkFreeDescriptorSets-00309
        pending_buffer_frees_.push_back({std::move(it->second), frame_counter_});
        buffers_.erase(it);
    }
}

void VulkanBackend::flush_pending_buffer_frees(bool force_all)
{
    if (pending_buffer_frees_.empty())
        return;

    // Only free entries that are old enough: destroyed at least
    // flight_count_ frames ago, so every flight slot has cycled.
    uint64_t safe_frame = (frame_counter_ > flight_count_) ? frame_counter_ - flight_count_ : 0;

    size_t write = 0;
    for (size_t read = 0; read < pending_buffer_frees_.size(); ++read)
    {
        auto& d = pending_buffer_frees_[read];
        if (force_all || d.frame_destroyed <= safe_frame)
        {
            // Free descriptor set first (it references the buffer)
            if (d.entry.descriptor_set != VK_NULL_HANDLE && descriptor_pool_ != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(ctx_.device, descriptor_pool_, 1, &d.entry.descriptor_set);
                d.entry.descriptor_set = VK_NULL_HANDLE;
            }
            // GpuBuffer destructor fires when d goes out of scope (overwritten or erased)
        }
        else
        {
            // Keep this entry — not old enough yet
            if (write != read)
                pending_buffer_frees_[write] = std::move(pending_buffer_frees_[read]);
            ++write;
        }
    }
    pending_buffer_frees_.resize(write);
}

void VulkanBackend::advance_deferred_deletion()
{
    // Progress is driven by successful GPU submissions in end_frame().
    // This call is kept as a lightweight periodic flush hook.
    flush_pending_buffer_frees();
}

void VulkanBackend::upload_buffer(BufferHandle handle,
                                  const void*  data,
                                  size_t       size_bytes,
                                  size_t       offset)
{
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end())
        return;

    auto& entry = it->second;
    auto& buf   = entry.gpu_buffer;

    // For dynamic UBO buffers, write to the next aligned slot
    if (entry.usage == BufferUsage::Uniform)
    {
        auto slot_size = static_cast<uint32_t>(ubo_slot_alignment_);
        if (ubo_next_offset_ + slot_size > slot_size * UBO_MAX_SLOTS)
        {
            ubo_next_offset_ = 0;   // wrap around (shouldn't happen with 64 slots)
        }
        ubo_bound_offset_ = ubo_next_offset_;
        buf.upload(data,
                   static_cast<VkDeviceSize>(size_bytes),
                   static_cast<VkDeviceSize>(ubo_next_offset_));
        ubo_next_offset_ += slot_size;
        return;
    }

    // For host-visible buffers, direct upload
    try
    {
        buf.upload(data, static_cast<VkDeviceSize>(size_bytes), static_cast<VkDeviceSize>(offset));
    }
    catch (...)
    {
        // For device-local buffers, use staging
        vk::staging_upload(ctx_.device,
                           ctx_.physical_device,
                           command_pool_,
                           ctx_.graphics_queue,
                           buf.buffer(),
                           data,
                           static_cast<VkDeviceSize>(size_bytes),
                           static_cast<VkDeviceSize>(offset));
    }
}

// --- Texture, frame lifecycle, capture, and multi-window methods are in
// ---   vk_texture.cpp, vk_frame.cpp, vk_capture.cpp, vk_multi_window.cpp

// --- Private helpers ---

void VulkanBackend::create_command_pool()
{
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = ctx_.queue_families.graphics.value();

    if (vkCreateCommandPool(ctx_.device, &info, nullptr, &command_pool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }
}

void VulkanBackend::create_command_buffers()
{
    // Free existing command buffers before allocating new ones (prevents leak on resize)
    if (!active_window_->command_buffers.empty() && command_pool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(ctx_.device,
                             command_pool_,
                             static_cast<uint32_t>(active_window_->command_buffers.size()),
                             active_window_->command_buffers.data());
    }

    uint32_t count = headless_ ? 1 : static_cast<uint32_t>(active_window_->swapchain.images.size());
    active_window_->command_buffers.resize(count);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = command_pool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = count;

    if (vkAllocateCommandBuffers(ctx_.device, &info, active_window_->command_buffers.data())
        != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void VulkanBackend::create_sync_objects()
{
    if (headless_)
        return;

    // Use one set of sync objects per swapchain image to prevent
    // semaphore reuse while the presentation engine still holds them.
    auto count = static_cast<uint32_t>(active_window_->swapchain.images.size());
    active_window_->image_available_semaphores.resize(count);
    active_window_->render_finished_semaphores.resize(count);
    active_window_->in_flight_fences.resize(count);

    // Track actual flight frame count for deferred deletion
    if (count > flight_count_)
        flight_count_ = count;

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < count; ++i)
    {
        if (vkCreateSemaphore(ctx_.device,
                              &sem_info,
                              nullptr,
                              &active_window_->image_available_semaphores[i])
                != VK_SUCCESS
            || vkCreateSemaphore(ctx_.device,
                                 &sem_info,
                                 nullptr,
                                 &active_window_->render_finished_semaphores[i])
                   != VK_SUCCESS
            || vkCreateFence(ctx_.device,
                             &fence_info,
                             nullptr,
                             &active_window_->in_flight_fences[i])
                   != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sync objects");
        }
    }
}

void VulkanBackend::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32},
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = 256;
    info.poolSizeCount = 3;
    info.pPoolSizes    = pool_sizes;

    if (vkCreateDescriptorPool(ctx_.device, &info, nullptr, &descriptor_pool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

VkDescriptorSet VulkanBackend::allocate_descriptor_set(VkDescriptorSetLayout layout)
{
    if (descriptor_pool_ == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx_.device, &alloc_info, &set) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return set;
}

void VulkanBackend::update_ubo_descriptor(VkDescriptorSet set,
                                          VkBuffer        buffer,
                                          VkDeviceSize    size) const
{
    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

void VulkanBackend::update_ssbo_descriptor(VkDescriptorSet set,
                                           VkBuffer        buffer,
                                           VkDeviceSize    size) const
{
    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = buffer;
    buf_info.offset = 0;
    buf_info.range  = size;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo     = &buf_info;

    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}

}   // namespace spectra
