#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_ROS2
    #include <sensor_msgs/msg/image.hpp>
#endif

namespace spectra::adapters::ros2
{

struct ImageFrame
{
    std::string          topic;
    std::string          frame_id;
    std::string          encoding;
    uint64_t             stamp_ns{0};
    uint32_t             width{0};
    uint32_t             height{0};
    uint32_t             preview_width{0};
    uint32_t             preview_height{0};
    bool                 supported_encoding{false};
    bool                 is_color{false};
    double               min_intensity{0.0};
    double               max_intensity{0.0};
    double               mean_intensity{0.0};
    std::string          warning;
    std::vector<uint8_t> preview_rgba;
    std::vector<uint8_t> full_rgba;
};

#ifdef SPECTRA_USE_ROS2
namespace image_detail
{

inline uint64_t stamp_to_ns(const builtin_interfaces::msg::Time& stamp)
{
    return static_cast<uint64_t>(stamp.sec) * 1'000'000'000ULL
           + static_cast<uint64_t>(stamp.nanosec);
}

inline bool host_is_big_endian()
{
    const uint16_t value = 0x0102;
    return reinterpret_cast<const uint8_t*>(&value)[0] == 0x01;
}

inline uint16_t read_u16(const uint8_t* data, bool swap_endianness)
{
    uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    if (!swap_endianness)
        return value;
    return static_cast<uint16_t>((value >> 8) | (value << 8));
}

inline bool decode_image_pixel(const sensor_msgs::msg::Image& message,
                               uint32_t                       x,
                               uint32_t                       y,
                               uint8_t                        rgba[4],
                               double&                        intensity_out)
{
    if (x >= message.width || y >= message.height)
        return false;

    const size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(message.step);
    if (row_offset >= message.data.size())
        return false;

    const std::string& encoding = message.encoding;
    if (encoding == "rgb8")
    {
        const size_t offset = row_offset + static_cast<size_t>(x) * 3;
        if (offset + 3 > message.data.size())
            return false;
        rgba[0] = message.data[offset + 0];
        rgba[1] = message.data[offset + 1];
        rgba[2] = message.data[offset + 2];
        rgba[3] = 255;
    }
    else if (encoding == "bgr8")
    {
        const size_t offset = row_offset + static_cast<size_t>(x) * 3;
        if (offset + 3 > message.data.size())
            return false;
        rgba[0] = message.data[offset + 2];
        rgba[1] = message.data[offset + 1];
        rgba[2] = message.data[offset + 0];
        rgba[3] = 255;
    }
    else if (encoding == "rgba8")
    {
        const size_t offset = row_offset + static_cast<size_t>(x) * 4;
        if (offset + 4 > message.data.size())
            return false;
        rgba[0] = message.data[offset + 0];
        rgba[1] = message.data[offset + 1];
        rgba[2] = message.data[offset + 2];
        rgba[3] = message.data[offset + 3];
    }
    else if (encoding == "mono8")
    {
        const size_t offset = row_offset + static_cast<size_t>(x);
        if (offset + 1 > message.data.size())
            return false;
        rgba[0] = rgba[1] = rgba[2] = message.data[offset];
        rgba[3]                     = 255;
    }
    else if (encoding == "16UC1")
    {
        const size_t offset = row_offset + static_cast<size_t>(x) * 2;
        if (offset + 2 > message.data.size())
            return false;
        const bool     swap_endianness = message.is_bigendian != image_detail::host_is_big_endian();
        const uint16_t value =
            image_detail::read_u16(message.data.data() + offset, swap_endianness);
        const uint8_t normalized = static_cast<uint8_t>(std::min<uint32_t>(255u, value / 257u));
        rgba[0] = rgba[1] = rgba[2] = normalized;
        rgba[3]                     = 255;
    }
    else
    {
        return false;
    }

    intensity_out =
        (static_cast<double>(rgba[0]) + static_cast<double>(rgba[1]) + static_cast<double>(rgba[2]))
        / 3.0;
    return true;
}

}   // namespace image_detail

std::optional<ImageFrame> adapt_image_message_with_encoding(const sensor_msgs::msg::Image& message,
                                                            const std::string&             topic,
                                                            uint32_t    preview_max_dim,
                                                            bool        retain_full_image,
                                                            std::string encoding_override);

inline std::optional<ImageFrame> adapt_image_message(const sensor_msgs::msg::Image& message,
                                                     const std::string&             topic,
                                                     uint32_t preview_max_dim   = 48,
                                                     bool     retain_full_image = false)
{
    if (message.width == 0 || message.height == 0 || message.data.empty())
        return std::nullopt;

    if (message.encoding == "jpeg" || message.encoding == "png")
    {
        return adapt_image_message_with_encoding(message,
                                                 topic,
                                                 preview_max_dim,
                                                 retain_full_image,
                                                 message.encoding);
    }

    if (message.step == 0)
        return std::nullopt;

    ImageFrame frame;
    frame.topic    = topic;
    frame.frame_id = message.header.frame_id;
    frame.encoding = message.encoding;
    frame.stamp_ns = image_detail::stamp_to_ns(message.header.stamp);
    frame.width    = message.width;
    frame.height   = message.height;
    frame.is_color =
        message.encoding == "rgb8" || message.encoding == "bgr8" || message.encoding == "rgba8";

    const bool supported = message.encoding == "rgb8" || message.encoding == "bgr8"
                           || message.encoding == "rgba8" || message.encoding == "mono8"
                           || message.encoding == "16UC1" || message.encoding == "jpeg"
                           || message.encoding == "png";
    frame.supported_encoding = supported;
    if (!supported)
    {
        frame.warning = "Unsupported encoding: " + message.encoding;
        return frame;
    }

    const uint32_t max_dim = std::max<uint32_t>(1u, preview_max_dim);
    if (message.width >= message.height)
    {
        frame.preview_width  = std::min(message.width, max_dim);
        frame.preview_height = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>((static_cast<uint64_t>(message.height) * frame.preview_width)
                                  / std::max<uint32_t>(1u, message.width)));
    }
    else
    {
        frame.preview_height = std::min(message.height, max_dim);
        frame.preview_width  = std::max<uint32_t>(
            1u,
            static_cast<uint32_t>((static_cast<uint64_t>(message.width) * frame.preview_height)
                                  / std::max<uint32_t>(1u, message.height)));
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
            std::min(message.height - 1,
                     static_cast<uint32_t>((static_cast<uint64_t>(py) * message.height)
                                           / std::max<uint32_t>(1u, frame.preview_height)));
        for (uint32_t px = 0; px < frame.preview_width; ++px)
        {
            const uint32_t sx =
                std::min(message.width - 1,
                         static_cast<uint32_t>((static_cast<uint64_t>(px) * message.width)
                                               / std::max<uint32_t>(1u, frame.preview_width)));

            uint8_t rgba[4]   = {};
            double  intensity = 0.0;
            if (!image_detail::decode_image_pixel(message, sx, sy, rgba, intensity))
                return std::nullopt;

            const size_t dst            = (static_cast<size_t>(py) * frame.preview_width + px) * 4u;
            frame.preview_rgba[dst + 0] = rgba[0];
            frame.preview_rgba[dst + 1] = rgba[1];
            frame.preview_rgba[dst + 2] = rgba[2];
            frame.preview_rgba[dst + 3] = rgba[3];

            min_intensity = std::min(min_intensity, intensity);
            max_intensity = std::max(max_intensity, intensity);
            sum_intensity += intensity;
            ++sampled_pixels;
        }
    }

    if (sampled_pixels == 0)
        return std::nullopt;

    frame.min_intensity  = min_intensity;
    frame.max_intensity  = max_intensity;
    frame.mean_intensity = sum_intensity / static_cast<double>(sampled_pixels);

    // Optionally retain full-resolution RGBA for GPU upload.
    if (retain_full_image)
    {
        const size_t pixel_count =
            static_cast<size_t>(message.width) * static_cast<size_t>(message.height);
        frame.full_rgba.resize(pixel_count * 4u);

        for (uint32_t y = 0; y < message.height; ++y)
        {
            for (uint32_t x = 0; x < message.width; ++x)
            {
                uint8_t rgba[4]          = {};
                double  intensity_unused = 0.0;
                if (!image_detail::decode_image_pixel(message, x, y, rgba, intensity_unused))
                {
                    // Fill with black on decode failure.
                    rgba[0] = rgba[1] = rgba[2] = 0;
                    rgba[3]                     = 255;
                }
                const size_t dst         = (static_cast<size_t>(y) * message.width + x) * 4u;
                frame.full_rgba[dst + 0] = rgba[0];
                frame.full_rgba[dst + 1] = rgba[1];
                frame.full_rgba[dst + 2] = rgba[2];
                frame.full_rgba[dst + 3] = rgba[3];
            }
        }
    }

    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
