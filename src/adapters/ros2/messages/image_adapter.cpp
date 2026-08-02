#include "messages/image_adapter.hpp"

#ifdef SPECTRA_USE_ROS2

    #include <cstring>

    #include "stb_image.h"

namespace spectra::adapters::ros2
{

namespace
{

bool decode_stb_rgba(const uint8_t*        data,
                     int                   size,
                     std::vector<uint8_t>& rgba,
                     uint32_t&             width,
                     uint32_t&             height)
{
    if (!data || size <= 0)
        return false;

    int            w                = 0;
    int            h                = 0;
    int            channels_in_file = 0;
    unsigned char* pixels =
        stbi_load_from_memory(data, size, &w, &h, &channels_in_file, STBI_rgb_alpha);
    if (!pixels || w <= 0 || h <= 0)
    {
        if (pixels)
            stbi_image_free(pixels);
        return false;
    }

    width                   = static_cast<uint32_t>(w);
    height                  = static_cast<uint32_t>(h);
    const size_t byte_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    rgba.assign(pixels, pixels + byte_count);
    stbi_image_free(pixels);
    return true;
}

void fill_preview(ImageFrame& frame, uint32_t preview_max_dim)
{
    const uint32_t max_dim = std::max<uint32_t>(1u, preview_max_dim);
    if (frame.width >= frame.height)
    {
        frame.preview_width  = std::min(frame.width, max_dim);
        frame.preview_height = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>((static_cast<uint64_t>(frame.height) * frame.preview_width)
                                  / std::max<uint32_t>(1u, frame.width)));
    }
    else
    {
        frame.preview_height = std::min(frame.height, max_dim);
        frame.preview_width  = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>((static_cast<uint64_t>(frame.width) * frame.preview_height)
                                  / std::max<uint32_t>(1u, frame.height)));
    }

    frame.preview_rgba.resize(static_cast<size_t>(frame.preview_width)
                              * static_cast<size_t>(frame.preview_height) * 4u);

    double min_intensity  = std::numeric_limits<double>::infinity();
    double max_intensity  = 0.0;
    double sum_intensity  = 0.0;
    size_t sampled_pixels = 0;

    for (uint32_t py = 0; py < frame.preview_height; ++py)
    {
        const uint32_t sy =
            std::min(frame.height - 1,
                     static_cast<uint32_t>((static_cast<uint64_t>(py) * frame.height)
                                           / std::max<uint32_t>(1u, frame.preview_height)));
        for (uint32_t px = 0; px < frame.preview_width; ++px)
        {
            const uint32_t sx =
                std::min(frame.width - 1,
                         static_cast<uint32_t>((static_cast<uint64_t>(px) * frame.width)
                                               / std::max<uint32_t>(1u, frame.preview_width)));
            const size_t src            = (static_cast<size_t>(sy) * frame.width + sx) * 4u;
            const size_t dst            = (static_cast<size_t>(py) * frame.preview_width + px) * 4u;
            frame.preview_rgba[dst + 0] = frame.full_rgba[src + 0];
            frame.preview_rgba[dst + 1] = frame.full_rgba[src + 1];
            frame.preview_rgba[dst + 2] = frame.full_rgba[src + 2];
            frame.preview_rgba[dst + 3] = frame.full_rgba[src + 3];

            const double intensity = (static_cast<double>(frame.full_rgba[src + 0])
                                      + static_cast<double>(frame.full_rgba[src + 1])
                                      + static_cast<double>(frame.full_rgba[src + 2]))
                                     / 3.0;
            min_intensity = std::min(min_intensity, intensity);
            max_intensity = std::max(max_intensity, intensity);
            sum_intensity += intensity;
            ++sampled_pixels;
        }
    }

    if (sampled_pixels == 0)
        return;

    frame.min_intensity  = min_intensity;
    frame.max_intensity  = max_intensity;
    frame.mean_intensity = sum_intensity / static_cast<double>(sampled_pixels);
}

}   // namespace

std::optional<ImageFrame> adapt_image_message_with_encoding(const sensor_msgs::msg::Image& message,
                                                            const std::string&             topic,
                                                            uint32_t    preview_max_dim,
                                                            bool        retain_full_image,
                                                            std::string encoding_override)
{
    if (message.width == 0 || message.height == 0 || message.data.empty())
        return std::nullopt;

    ImageFrame frame;
    frame.topic    = topic;
    frame.frame_id = message.header.frame_id;
    frame.stamp_ns = image_detail::stamp_to_ns(message.header.stamp);
    frame.width    = message.width;
    frame.height   = message.height;
    frame.encoding = encoding_override.empty() ? message.encoding : encoding_override;
    frame.is_color = frame.encoding == "rgb8" || frame.encoding == "bgr8"
                     || frame.encoding == "rgba8" || frame.encoding == "jpeg"
                     || frame.encoding == "png";

    if (frame.encoding == "jpeg" || frame.encoding == "png")
    {
        if (!decode_stb_rgba(message.data.data(),
                             static_cast<int>(message.data.size()),
                             frame.full_rgba,
                             frame.width,
                             frame.height))
        {
            frame.supported_encoding = false;
            frame.warning            = "Failed to decode " + frame.encoding + " payload";
            return frame;
        }

        frame.supported_encoding = true;
        fill_preview(frame, preview_max_dim);
        if (!retain_full_image)
            frame.full_rgba.clear();
        return frame;
    }

    if (!encoding_override.empty() && encoding_override != message.encoding)
    {
        sensor_msgs::msg::Image copy = message;
        copy.encoding                = encoding_override;
        return adapt_image_message(copy, topic, preview_max_dim, retain_full_image);
    }

    return adapt_image_message(message, topic, preview_max_dim, retain_full_image);
}

}   // namespace spectra::adapters::ros2

#endif
