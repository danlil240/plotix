// vk_texture.cpp — Texture create/destroy and Vulkan handle queries.
// Split from vk_backend.cpp (MR-2) for focused module ownership.

#include "vk_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vk_buffer.hpp"

namespace spectra
{

TextureHandle VulkanBackend::create_texture(uint32_t       width,
                                            uint32_t       height,
                                            const uint8_t* rgba_data)
{
    TextureHandle h;
    h.id = next_texture_id_++;
    TextureEntry tex{};

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Compute full mipmap chain depth
    uint32_t mip_levels =
        static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height)))))
        + 1;

    // Create VkImage
    VkImageCreateInfo image_info{};
    image_info.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType   = VK_IMAGE_TYPE_2D;
    image_info.format      = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent      = {width, height, 1};
    image_info.mipLevels   = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples     = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling      = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                       | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx_.device, &image_info, nullptr, &tex.image) != VK_SUCCESS)
    {
        textures_[h.id] = {};
        return h;
    }

    // Allocate device memory for the image
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx_.device, tex.image, &mem_reqs);

    uint32_t mem_type_idx = UINT32_MAX;
    for (uint32_t i = 0; i < ctx_.memory_properties.memoryTypeCount; ++i)
    {
        if ((mem_reqs.memoryTypeBits & (1 << i))
            && (ctx_.memory_properties.memoryTypes[i].propertyFlags
                & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == UINT32_MAX)
    {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type_idx;

    if (vkAllocateMemory(ctx_.device, &alloc_info, nullptr, &tex.memory) != VK_SUCCESS)
    {
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }
    vkBindImageMemory(ctx_.device, tex.image, tex.memory, 0);

    // Upload pixel data via staging buffer
    if (rgba_data)
    {
        auto staging = vk::GpuBuffer::create(
            ctx_.device,
            ctx_.physical_device,
            image_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.upload(rgba_data, image_size);

        // One-shot command buffer for layout transition + copy
        VkCommandBufferAllocateInfo cmd_alloc{};
        cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc.commandPool        = command_pool_;
        cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(ctx_.device, &cmd_alloc, &cmd);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = tex.image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {width, height, 1};

        vkCmdCopyBufferToImage(cmd,
                               staging.buffer(),
                               tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);

        // ── Generate mipmaps via repeated vkCmdBlitImage ──
        auto mip_w = static_cast<int32_t>(width);
        auto mip_h = static_cast<int32_t>(height);

        for (uint32_t i = 1; i < mip_levels; ++i)
        {
            // Transition level i-1: TRANSFER_DST → TRANSFER_SRC
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);

            // Transition level i: UNDEFINED → TRANSFER_DST
            VkImageMemoryBarrier dst_barrier          = barrier;
            dst_barrier.subresourceRange.baseMipLevel = i;
            dst_barrier.oldLayout                     = VK_IMAGE_LAYOUT_UNDEFINED;
            dst_barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dst_barrier.srcAccessMask                 = 0;
            dst_barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &dst_barrier);

            int32_t next_w = mip_w > 1 ? mip_w / 2 : 1;
            int32_t next_h = mip_h > 1 ? mip_h / 2 : 1;

            VkImageBlit blit{};
            blit.srcOffsets[0]                 = {0, 0, 0};
            blit.srcOffsets[1]                 = {mip_w, mip_h, 1};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = 1;
            blit.dstOffsets[0]                 = {0, 0, 0};
            blit.dstOffsets[1]                 = {next_w, next_h, 1};
            blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel       = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount     = 1;

            vkCmdBlitImage(cmd,
                           tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_LINEAR);

            // Transition level i-1: TRANSFER_SRC → SHADER_READ_ONLY
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);

            mip_w = next_w;
            mip_h = next_h;
        }

        // Transition last mip level: TRANSFER_DST → SHADER_READ_ONLY
        barrier.subresourceRange.baseMipLevel = mip_levels - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(ctx_.graphics_queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(ctx_.graphics_queue);

        vkFreeCommandBuffers(ctx_.device, command_pool_, 1, &cmd);
        staging.destroy();
    }

    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = tex.image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(ctx_.device, &view_info, nullptr, &tex.view) != VK_SUCCESS)
    {
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Create sampler
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_LINEAR;
    sampler_info.minFilter    = VK_FILTER_LINEAR;
    sampler_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod       = static_cast<float>(mip_levels);

    if (vkCreateSampler(ctx_.device, &sampler_info, nullptr, &tex.sampler) != VK_SUCCESS)
    {
        vkDestroyImageView(ctx_.device, tex.view, nullptr);
        vkFreeMemory(ctx_.device, tex.memory, nullptr);
        vkDestroyImage(ctx_.device, tex.image, nullptr);
        textures_[h.id] = {};
        return h;
    }

    // Allocate and update descriptor set for this texture
    tex.descriptor_set = allocate_descriptor_set(texture_desc_layout_);
    if (tex.descriptor_set != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo img_info{};
        img_info.sampler     = tex.sampler;
        img_info.imageView   = tex.view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = tex.descriptor_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;

        vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
    }

    textures_[h.id] = tex;
    return h;
}

void VulkanBackend::destroy_texture(TextureHandle handle)
{
    auto it = textures_.find(handle.id);
    if (it != textures_.end())
    {
        auto& tex = it->second;
        if (tex.sampler != VK_NULL_HANDLE)
            vkDestroySampler(ctx_.device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE)
            vkDestroyImageView(ctx_.device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE)
            vkDestroyImage(ctx_.device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)
            vkFreeMemory(ctx_.device, tex.memory, nullptr);
        textures_.erase(it);
    }
}

bool VulkanBackend::texture_vulkan_handles(TextureHandle handle,
                                           VkSampler*    out_sampler,
                                           VkImageView*  out_view) const
{
    auto it = textures_.find(handle.id);
    if (it == textures_.end())
        return false;
    if (out_sampler)
        *out_sampler = it->second.sampler;
    if (out_view)
        *out_view = it->second.view;
    return true;
}

}   // namespace spectra
