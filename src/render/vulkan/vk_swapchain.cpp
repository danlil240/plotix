#include "vk_swapchain.hpp"

#include <spectra/logger.hpp>

#include <algorithm>
#include <stdexcept>

namespace spectra::vk
{

SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count > 0)
    {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device,
                                             surface,
                                             &format_count,
                                             details.formats.data());
    }

    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, nullptr);
    if (mode_count > 0)
    {
        details.present_modes.resize(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device,
                                                  surface,
                                                  &mode_count,
                                                  details.present_modes.data());
    }

    return details;
}

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    // Prefer UNORM so theme hex values pass through 1:1 without hardware gamma.
    auto find_format = [&](VkFormat fmt) -> const VkSurfaceFormatKHR*
    {
        for (const auto& f : formats)
        {
            if (f.format == fmt && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return &f;
        }
        return nullptr;
    };

    if (const auto* f = find_format(VK_FORMAT_B8G8R8A8_UNORM))
    {
        SPECTRA_LOG_DEBUG("Vulkan", "swapchain format: B8G8R8A8_UNORM");
        return *f;
    }
    if (const auto* f = find_format(VK_FORMAT_R8G8B8A8_UNORM))
    {
        SPECTRA_LOG_DEBUG("Vulkan", "swapchain format: R8G8B8A8_UNORM");
        return *f;
    }

    // Avoid SRGB swapchain formats — they apply gamma and shift theme colours.
    for (const auto& f : formats)
    {
        if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && f.format != VK_FORMAT_B8G8R8A8_SRGB
            && f.format != VK_FORMAT_R8G8B8A8_SRGB)
        {
            SPECTRA_LOG_WARN("Vulkan",
                             "swapchain format: UNORM unavailable, using format {}",
                             static_cast<int>(f.format));
            return f;
        }
    }

    SPECTRA_LOG_WARN("Vulkan",
                     "swapchain format: falling back to driver default ({})",
                     static_cast<int>(formats[0].format));
    return formats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes)
{
    auto has_mode = [&](VkPresentModeKHR m)
    { return std::find(modes.begin(), modes.end(), m) != modes.end(); };

    auto mode_name = [](VkPresentModeKHR m) -> const char*
    {
        switch (m)
        {
            case VK_PRESENT_MODE_FIFO_KHR:
                return "FIFO (VSync)";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                return "FIFO_RELAXED (VSync, late frames tear)";
            case VK_PRESENT_MODE_MAILBOX_KHR:
                return "MAILBOX (low-latency, tear-free)";
            case VK_PRESENT_MODE_IMMEDIATE_KHR:
                return "IMMEDIATE (no VSync, tearing)";
            default:
                return "UNKNOWN";
        }
    };

    // Log available modes
    for (auto m : modes)
        SPECTRA_LOG_DEBUG("Vulkan", "  available present mode: {}", mode_name(m));

        // Select based on compile-time SPECTRA_PRESENT_MODE_* define
#if defined(SPECTRA_PRESENT_MODE_MAILBOX)
    VkPresentModeKHR preferred = VK_PRESENT_MODE_MAILBOX_KHR;
#elif defined(SPECTRA_PRESENT_MODE_IMMEDIATE)
    VkPresentModeKHR preferred = VK_PRESENT_MODE_IMMEDIATE_KHR;
#else
    VkPresentModeKHR preferred = VK_PRESENT_MODE_FIFO_KHR;
#endif

    if (has_mode(preferred))
    {
        SPECTRA_LOG_DEBUG("Vulkan", "present mode: {} (requested)", mode_name(preferred));
        return preferred;
    }

    // FIFO is guaranteed by the Vulkan spec — always safe fallback
    SPECTRA_LOG_WARN("Vulkan",
                     "requested present mode {} not available, falling back to FIFO",
                     mode_name(preferred));
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities,
                         uint32_t                        width,
                         uint32_t                        height)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }
    VkExtent2D extent = {width, height};
    extent.width      = std::clamp(extent.width,
                              capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height     = std::clamp(extent.height,
                               capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

// Forward declarations for helpers used by create_swapchain
static uint32_t find_memory_type(VkPhysicalDevice      physical_device,
                                 uint32_t              type_filter,
                                 VkMemoryPropertyFlags properties);

VkRenderPass create_render_pass(VkDevice              device,
                                VkFormat              color_format,
                                VkFormat              depth_format,
                                VkSampleCountFlagBits msaa_samples)
{
    bool use_msaa = (msaa_samples != VK_SAMPLE_COUNT_1_BIT);

    if (!use_msaa)
    {
        // Non-MSAA path: 2 attachments (color + depth)
        VkAttachmentDescription color_attachment{};
        color_attachment.format         = color_format;
        color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth_attachment{};
        depth_attachment.format         = depth_format;
        depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass   = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 2;
        info.pAttachments    = attachments;
        info.subpassCount    = 1;
        info.pSubpasses      = &subpass;
        info.dependencyCount = 1;
        info.pDependencies   = &dependency;

        VkRenderPass render_pass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass");
        }
        return render_pass;
    }

    // MSAA path: 3 attachments (MSAA color, MSAA depth, resolve target)
    // Attachment 0: MSAA color (multisampled, not stored — resolved to attachment 2)
    VkAttachmentDescription msaa_color{};
    msaa_color.format         = color_format;
    msaa_color.samples        = msaa_samples;
    msaa_color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    msaa_color.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;   // resolved, not stored
    msaa_color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    msaa_color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    msaa_color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    msaa_color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Attachment 1: MSAA depth (multisampled)
    VkAttachmentDescription msaa_depth{};
    msaa_depth.format         = depth_format;
    msaa_depth.samples        = msaa_samples;
    msaa_depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    msaa_depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    msaa_depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    msaa_depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    msaa_depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    msaa_depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Attachment 2: resolve target (single-sample, presented)
    VkAttachmentDescription resolve_att{};
    resolve_att.format         = color_format;
    resolve_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    resolve_att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    resolve_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve_att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription attachments[] = {msaa_color, msaa_depth, resolve_att};

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolve_ref{};
    resolve_ref.attachment = 2;
    resolve_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    subpass.pResolveAttachments     = &resolve_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 3;
    info.pAttachments    = attachments;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = 1;
    info.pDependencies   = &dependency;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &info, nullptr, &render_pass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MSAA render pass");
    }
    return render_pass;
}

SwapchainContext create_swapchain(VkDevice              device,
                                  VkPhysicalDevice      physical_device,
                                  VkSurfaceKHR          surface,
                                  uint32_t              width,
                                  uint32_t              height,
                                  uint32_t              graphics_family,
                                  uint32_t              present_family,
                                  VkSwapchainKHR        old_swapchain,
                                  VkRenderPass          reuse_render_pass,
                                  VkSampleCountFlagBits msaa_samples)
{
    auto support = query_swapchain_support(physical_device, surface);
    auto format  = choose_surface_format(support.formats);
    auto mode    = choose_present_mode(support.present_modes);
    auto extent  = choose_extent(support.capabilities, width, height);

    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount)
    {
        image_count = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface          = surface;
    create_info.minImageCount    = image_count;
    create_info.imageFormat      = format.format;
    create_info.imageColorSpace  = format.colorSpace;
    create_info.imageExtent      = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create_info.preTransform   = support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode    = mode;
    create_info.clipped        = VK_TRUE;
    create_info.oldSwapchain   = old_swapchain;

    uint32_t family_indices[] = {graphics_family, present_family};
    if (graphics_family != present_family)
    {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = family_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    SwapchainContext ctx;
    ctx.image_format = format.format;
    ctx.extent       = extent;
    ctx.msaa_samples = msaa_samples;
    bool use_msaa    = (msaa_samples != VK_SAMPLE_COUNT_1_BIT);

    if (vkCreateSwapchainKHR(device, &create_info, nullptr, &ctx.swapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain");
    }

    // Get images
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &image_count, nullptr);
    ctx.images.resize(image_count);
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &image_count, ctx.images.data());

    // Create image views
    ctx.image_views.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i)
    {
        VkImageViewCreateInfo view_info{};
        view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image                           = ctx.images[i];
        view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format                          = ctx.image_format;
        view_info.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                                     VK_COMPONENT_SWIZZLE_IDENTITY};
        view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &view_info, nullptr, &ctx.image_views[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }

    // Find supported depth format
    ctx.depth_format = find_depth_format(physical_device);

    // Reuse existing render pass if provided (during resize — format doesn't change,
    // so the old render pass is compatible). This avoids invalidating all pipelines.
    if (reuse_render_pass != VK_NULL_HANDLE)
    {
        ctx.render_pass = reuse_render_pass;
    }
    else
    {
        ctx.render_pass =
            create_render_pass(device, ctx.image_format, ctx.depth_format, msaa_samples);
    }

    // Determine depth sample count: MSAA depth when MSAA is enabled
    VkSampleCountFlagBits depth_samples = use_msaa ? msaa_samples : VK_SAMPLE_COUNT_1_BIT;

    // Create depth image (shared across all framebuffers)
    {
        VkImageCreateInfo depth_img_info{};
        depth_img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_img_info.imageType     = VK_IMAGE_TYPE_2D;
        depth_img_info.format        = ctx.depth_format;
        depth_img_info.extent        = {extent.width, extent.height, 1};
        depth_img_info.mipLevels     = 1;
        depth_img_info.arrayLayers   = 1;
        depth_img_info.samples       = depth_samples;
        depth_img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        depth_img_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        depth_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &depth_img_info, nullptr, &ctx.depth_image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image");
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device, ctx.depth_image, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize  = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                      mem_reqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &alloc_info, nullptr, &ctx.depth_memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate depth image memory");
        }
        vkBindImageMemory(device, ctx.depth_image, ctx.depth_memory, 0);

        VkImageViewCreateInfo depth_view_info{};
        depth_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_info.image                           = ctx.depth_image;
        depth_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format                          = ctx.depth_format;
        depth_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_view_info.subresourceRange.baseMipLevel   = 0;
        depth_view_info.subresourceRange.levelCount     = 1;
        depth_view_info.subresourceRange.baseArrayLayer = 0;
        depth_view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &depth_view_info, nullptr, &ctx.depth_view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image view");
        }
    }

    // Create MSAA color image (shared across all framebuffers) when MSAA is enabled
    if (use_msaa)
    {
        VkImageCreateInfo msaa_img_info{};
        msaa_img_info.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        msaa_img_info.imageType   = VK_IMAGE_TYPE_2D;
        msaa_img_info.format      = ctx.image_format;
        msaa_img_info.extent      = {extent.width, extent.height, 1};
        msaa_img_info.mipLevels   = 1;
        msaa_img_info.arrayLayers = 1;
        msaa_img_info.samples     = msaa_samples;
        msaa_img_info.tiling      = VK_IMAGE_TILING_OPTIMAL;
        msaa_img_info.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        msaa_img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        msaa_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &msaa_img_info, nullptr, &ctx.msaa_color_image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create MSAA color image");
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(device, ctx.msaa_color_image, &mem_reqs);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize  = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                      mem_reqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &alloc_info, nullptr, &ctx.msaa_color_memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate MSAA color image memory");
        }
        vkBindImageMemory(device, ctx.msaa_color_image, ctx.msaa_color_memory, 0);

        VkImageViewCreateInfo msaa_view_info{};
        msaa_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        msaa_view_info.image                           = ctx.msaa_color_image;
        msaa_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        msaa_view_info.format                          = ctx.image_format;
        msaa_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        msaa_view_info.subresourceRange.baseMipLevel   = 0;
        msaa_view_info.subresourceRange.levelCount     = 1;
        msaa_view_info.subresourceRange.baseArrayLayer = 0;
        msaa_view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &msaa_view_info, nullptr, &ctx.msaa_color_view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create MSAA color image view");
        }
    }

    // Create framebuffers
    ctx.framebuffers.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i)
    {
        // Attachments array must outlive the vkCreateFramebuffer call,
        // so declare it at function scope (not inside if/else branches).
        VkImageView fb_attachments[3]{};
        uint32_t    fb_attachment_count = 0;

        if (use_msaa)
        {
            // MSAA: attachment 0 = MSAA color, 1 = MSAA depth, 2 = resolve (swapchain image)
            fb_attachments[0]   = ctx.msaa_color_view;
            fb_attachments[1]   = ctx.depth_view;
            fb_attachments[2]   = ctx.image_views[i];
            fb_attachment_count = 3;
        }
        else
        {
            // Non-MSAA: attachment 0 = color (swapchain image), 1 = depth
            fb_attachments[0]   = ctx.image_views[i];
            fb_attachments[1]   = ctx.depth_view;
            fb_attachment_count = 2;
        }

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass      = ctx.render_pass;
        fb_info.width           = extent.width;
        fb_info.height          = extent.height;
        fb_info.layers          = 1;
        fb_info.attachmentCount = fb_attachment_count;
        fb_info.pAttachments    = fb_attachments;

        if (vkCreateFramebuffer(device, &fb_info, nullptr, &ctx.framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

    return ctx;
}

void destroy_swapchain(VkDevice device, SwapchainContext& ctx, bool skip_render_pass)
{
    for (auto fb : ctx.framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    ctx.framebuffers.clear();

    // Destroy MSAA color resources
    if (ctx.msaa_color_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.msaa_color_view, nullptr);
    ctx.msaa_color_view = VK_NULL_HANDLE;
    if (ctx.msaa_color_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.msaa_color_image, nullptr);
    ctx.msaa_color_image = VK_NULL_HANDLE;
    if (ctx.msaa_color_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.msaa_color_memory, nullptr);
    ctx.msaa_color_memory = VK_NULL_HANDLE;

    // Destroy depth resources
    if (ctx.depth_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.depth_view, nullptr);
    ctx.depth_view = VK_NULL_HANDLE;
    if (ctx.depth_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.depth_image, nullptr);
    ctx.depth_image = VK_NULL_HANDLE;
    if (ctx.depth_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.depth_memory, nullptr);
    ctx.depth_memory = VK_NULL_HANDLE;

    if (!skip_render_pass && ctx.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, ctx.render_pass, nullptr);
    ctx.render_pass = VK_NULL_HANDLE;

    for (auto view : ctx.image_views)
        vkDestroyImageView(device, view, nullptr);
    ctx.image_views.clear();

    if (ctx.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, ctx.swapchain, nullptr);
    ctx.swapchain = VK_NULL_HANDLE;
}

// Helper: find memory type
static uint32_t find_memory_type(VkPhysicalDevice      physical_device,
                                 uint32_t              type_filter,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        if ((type_filter & (1 << i))
            && (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

// Forward declare find_depth_format (defined at end of file)

OffscreenContext create_offscreen_framebuffer(VkDevice              device,
                                              VkPhysicalDevice      physical_device,
                                              uint32_t              width,
                                              uint32_t              height,
                                              VkSampleCountFlagBits msaa_samples)
{
    OffscreenContext ctx;
    // Use UNORM so that theme hex values (stored as sRGB) pass through 1:1
    // without hardware linear→sRGB gamma expansion.  This matches the
    // windowed swapchain format choice (choose_surface_format prefers UNORM).
    // Using SRGB here would cause double-gamma: theme sRGB values treated as
    // linear → converted to sRGB again → visibly lighter/washed-out colours.
    ctx.format       = VK_FORMAT_R8G8B8A8_UNORM;
    ctx.extent       = {width, height};
    ctx.msaa_samples = msaa_samples;
    bool use_msaa    = (msaa_samples != VK_SAMPLE_COUNT_1_BIT);

    // Create color image (resolve target — always single-sample for readback)
    VkImageCreateInfo img_info{};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = ctx.format;
    img_info.extent        = {width, height, 1};
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &img_info, nullptr, &ctx.color_image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create offscreen image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, ctx.color_image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                  mem_reqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &ctx.color_memory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate offscreen image memory");
    }
    vkBindImageMemory(device, ctx.color_image, ctx.color_memory, 0);

    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = ctx.color_image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = ctx.format;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &view_info, nullptr, &ctx.color_view) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create offscreen image view");
    }

    // Determine depth sample count
    VkSampleCountFlagBits depth_samples = use_msaa ? msaa_samples : VK_SAMPLE_COUNT_1_BIT;

    // Create depth image for offscreen
    ctx.depth_format = find_depth_format(physical_device);
    {
        VkImageCreateInfo depth_img_info{};
        depth_img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_img_info.imageType     = VK_IMAGE_TYPE_2D;
        depth_img_info.format        = ctx.depth_format;
        depth_img_info.extent        = {width, height, 1};
        depth_img_info.mipLevels     = 1;
        depth_img_info.arrayLayers   = 1;
        depth_img_info.samples       = depth_samples;
        depth_img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        depth_img_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        depth_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &depth_img_info, nullptr, &ctx.depth_image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen depth image");
        }

        VkMemoryRequirements depth_mem_reqs;
        vkGetImageMemoryRequirements(device, ctx.depth_image, &depth_mem_reqs);

        VkMemoryAllocateInfo depth_alloc_info{};
        depth_alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depth_alloc_info.allocationSize  = depth_mem_reqs.size;
        depth_alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                            depth_mem_reqs.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &depth_alloc_info, nullptr, &ctx.depth_memory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate offscreen depth image memory");
        }
        vkBindImageMemory(device, ctx.depth_image, ctx.depth_memory, 0);

        VkImageViewCreateInfo depth_view_info{};
        depth_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_info.image                           = ctx.depth_image;
        depth_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format                          = ctx.depth_format;
        depth_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_view_info.subresourceRange.baseMipLevel   = 0;
        depth_view_info.subresourceRange.levelCount     = 1;
        depth_view_info.subresourceRange.baseArrayLayer = 0;
        depth_view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &depth_view_info, nullptr, &ctx.depth_view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen depth image view");
        }
    }

    // Create MSAA color image when MSAA is enabled
    if (use_msaa)
    {
        VkImageCreateInfo msaa_img_info{};
        msaa_img_info.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        msaa_img_info.imageType   = VK_IMAGE_TYPE_2D;
        msaa_img_info.format      = ctx.format;
        msaa_img_info.extent      = {width, height, 1};
        msaa_img_info.mipLevels   = 1;
        msaa_img_info.arrayLayers = 1;
        msaa_img_info.samples     = msaa_samples;
        msaa_img_info.tiling      = VK_IMAGE_TILING_OPTIMAL;
        msaa_img_info.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        msaa_img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        msaa_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &msaa_img_info, nullptr, &ctx.msaa_color_image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen MSAA color image");
        }

        VkMemoryRequirements msaa_mem_reqs;
        vkGetImageMemoryRequirements(device, ctx.msaa_color_image, &msaa_mem_reqs);

        VkMemoryAllocateInfo msaa_alloc_info{};
        msaa_alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        msaa_alloc_info.allocationSize  = msaa_mem_reqs.size;
        msaa_alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                           msaa_mem_reqs.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &msaa_alloc_info, nullptr, &ctx.msaa_color_memory)
            != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate offscreen MSAA color image memory");
        }
        vkBindImageMemory(device, ctx.msaa_color_image, ctx.msaa_color_memory, 0);

        VkImageViewCreateInfo msaa_view_info{};
        msaa_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        msaa_view_info.image                           = ctx.msaa_color_image;
        msaa_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        msaa_view_info.format                          = ctx.format;
        msaa_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        msaa_view_info.subresourceRange.baseMipLevel   = 0;
        msaa_view_info.subresourceRange.levelCount     = 1;
        msaa_view_info.subresourceRange.baseArrayLayer = 0;
        msaa_view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &msaa_view_info, nullptr, &ctx.msaa_color_view) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen MSAA color image view");
        }
    }

    // Create render pass
    if (!use_msaa)
    {
        // Non-MSAA: final layout is TRANSFER_SRC for readback
        VkAttachmentDescription color_att{};
        color_att.format         = ctx.format;
        color_att.samples        = VK_SAMPLE_COUNT_1_BIT;
        color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color_att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentDescription depth_att{};
        depth_att.format         = ctx.depth_format;
        depth_att.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_att.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_att.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription rp_attachments[] = {color_att, depth_att};

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass   = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp_info{};
        rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 2;
        rp_info.pAttachments    = rp_attachments;
        rp_info.subpassCount    = 1;
        rp_info.pSubpasses      = &subpass;
        rp_info.dependencyCount = 1;
        rp_info.pDependencies   = &dependency;

        if (vkCreateRenderPass(device, &rp_info, nullptr, &ctx.render_pass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen render pass");
        }
    }
    else
    {
        // MSAA: 3 attachments (MSAA color, MSAA depth, resolve target for readback)
        VkAttachmentDescription msaa_color_att{};
        msaa_color_att.format         = ctx.format;
        msaa_color_att.samples        = msaa_samples;
        msaa_color_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        msaa_color_att.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        msaa_color_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        msaa_color_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        msaa_color_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        msaa_color_att.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription msaa_depth_att{};
        msaa_depth_att.format         = ctx.depth_format;
        msaa_depth_att.samples        = msaa_samples;
        msaa_depth_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        msaa_depth_att.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        msaa_depth_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        msaa_depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        msaa_depth_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        msaa_depth_att.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription resolve_att{};
        resolve_att.format         = ctx.format;
        resolve_att.samples        = VK_SAMPLE_COUNT_1_BIT;
        resolve_att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        resolve_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolve_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        resolve_att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentDescription rp_attachments[] = {msaa_color_att, msaa_depth_att, resolve_att};

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference resolve_ref{};
        resolve_ref.attachment = 2;
        resolve_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;
        subpass.pResolveAttachments     = &resolve_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass   = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                  | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp_info{};
        rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.attachmentCount = 3;
        rp_info.pAttachments    = rp_attachments;
        rp_info.subpassCount    = 1;
        rp_info.pSubpasses      = &subpass;
        rp_info.dependencyCount = 1;
        rp_info.pDependencies   = &dependency;

        if (vkCreateRenderPass(device, &rp_info, nullptr, &ctx.render_pass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen MSAA render pass");
        }
    }

    // Create framebuffer
    {
        VkFramebufferCreateInfo fb_info{};
        fb_info.sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = ctx.render_pass;
        fb_info.width      = width;
        fb_info.height     = height;
        fb_info.layers     = 1;

        // Attachments array must outlive vkCreateFramebuffer call.
        VkImageView fb_attachments[3]{};

        if (use_msaa)
        {
            // MSAA: attachment 0 = MSAA color, 1 = MSAA depth, 2 = resolve (color_view)
            fb_attachments[0]       = ctx.msaa_color_view;
            fb_attachments[1]       = ctx.depth_view;
            fb_attachments[2]       = ctx.color_view;
            fb_info.attachmentCount = 3;
        }
        else
        {
            fb_attachments[0]       = ctx.color_view;
            fb_attachments[1]       = ctx.depth_view;
            fb_info.attachmentCount = 2;
        }

        fb_info.pAttachments = fb_attachments;

        if (vkCreateFramebuffer(device, &fb_info, nullptr, &ctx.framebuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create offscreen framebuffer");
        }
    }

    return ctx;
}

void destroy_offscreen(VkDevice device, OffscreenContext& ctx)
{
    if (ctx.framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, ctx.framebuffer, nullptr);
    if (ctx.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, ctx.render_pass, nullptr);
    // Destroy MSAA resources
    if (ctx.msaa_color_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.msaa_color_view, nullptr);
    if (ctx.msaa_color_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.msaa_color_image, nullptr);
    if (ctx.msaa_color_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.msaa_color_memory, nullptr);
    // Destroy depth resources
    if (ctx.depth_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.depth_view, nullptr);
    if (ctx.depth_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.depth_image, nullptr);
    if (ctx.depth_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.depth_memory, nullptr);
    // Destroy color resources
    if (ctx.color_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, ctx.color_view, nullptr);
    if (ctx.color_image != VK_NULL_HANDLE)
        vkDestroyImage(device, ctx.color_image, nullptr);
    if (ctx.color_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, ctx.color_memory, nullptr);
    // Destroy persistent readback staging buffer
    if (ctx.readback_buffer != VK_NULL_HANDLE)
    {
        if (ctx.readback_mapped_ptr)
            vkUnmapMemory(device, ctx.readback_memory);
        vkDestroyBuffer(device, ctx.readback_buffer, nullptr);
        vkFreeMemory(device, ctx.readback_memory, nullptr);
    }
    ctx = {};
}

VkFormat find_depth_format(VkPhysicalDevice physical_device)
{
    // Prefer D32_SFLOAT, fallback to D32_SFLOAT_S8_UINT, then D24_UNORM_S8_UINT
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (auto format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }

    // Fallback — D32_SFLOAT should be universally supported
    return VK_FORMAT_D32_SFLOAT;
}

}   // namespace spectra::vk
