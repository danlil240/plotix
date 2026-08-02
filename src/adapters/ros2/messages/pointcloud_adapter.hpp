#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <spectra/math3d.hpp>

#ifdef SPECTRA_USE_ROS2
    #include <sensor_msgs/msg/point_cloud2.hpp>
    #include <sensor_msgs/msg/point_field.hpp>
#endif

namespace spectra::adapters::ros2
{

struct PointCloudPoint
{
    spectra::vec3 position{};
    float         intensity{0.0f};
    uint32_t      rgba{0xFFFFFFFFu};
    bool          has_rgb{false};
    bool          has_intensity{false};
};

struct PointCloudFrame
{
    std::string                  topic;
    std::string                  frame_id;
    uint64_t                     stamp_ns{0};
    size_t                       point_count{0};
    size_t                       original_point_count{0};
    std::vector<PointCloudPoint> points;
    spectra::vec3                min_bounds{};
    spectra::vec3                max_bounds{};
    spectra::vec3                centroid{};
    bool                         has_rgb{false};
    bool                         has_intensity{false};
    float                        min_intensity{0.0f};
    float                        max_intensity{0.0f};
};

#ifdef SPECTRA_USE_ROS2
namespace detail
{

inline bool host_is_big_endian()
{
    const uint16_t value = 0x0102;
    return reinterpret_cast<const uint8_t*>(&value)[0] == 0x01;
}

template <typename T>
inline T byteswap_copy(const uint8_t* data)
{
    std::array<uint8_t, sizeof(T)> bytes{};
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = data[sizeof(T) - 1 - i];

    T value{};
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

template <typename T>
inline T read_typed_value(const uint8_t* data, bool swap_endianness)
{
    if (swap_endianness)
        return byteswap_copy<T>(data);

    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

inline bool read_numeric_field(const uint8_t* data,
                               uint8_t        datatype,
                               bool           swap_endianness,
                               double&        value_out)
{
    switch (datatype)
    {
        case sensor_msgs::msg::PointField::INT8:
            value_out = static_cast<double>(read_typed_value<int8_t>(data, false));
            return true;
        case sensor_msgs::msg::PointField::UINT8:
            value_out = static_cast<double>(read_typed_value<uint8_t>(data, false));
            return true;
        case sensor_msgs::msg::PointField::INT16:
            value_out = static_cast<double>(read_typed_value<int16_t>(data, swap_endianness));
            return true;
        case sensor_msgs::msg::PointField::UINT16:
            value_out = static_cast<double>(read_typed_value<uint16_t>(data, swap_endianness));
            return true;
        case sensor_msgs::msg::PointField::INT32:
            value_out = static_cast<double>(read_typed_value<int32_t>(data, swap_endianness));
            return true;
        case sensor_msgs::msg::PointField::UINT32:
            value_out = static_cast<double>(read_typed_value<uint32_t>(data, swap_endianness));
            return true;
        case sensor_msgs::msg::PointField::FLOAT32:
            value_out = static_cast<double>(read_typed_value<float>(data, swap_endianness));
            return true;
        case sensor_msgs::msg::PointField::FLOAT64:
            value_out = read_typed_value<double>(data, swap_endianness);
            return true;
        default:
            return false;
    }
}

inline size_t datatype_size(uint8_t datatype)
{
    switch (datatype)
    {
        case sensor_msgs::msg::PointField::INT8:
        case sensor_msgs::msg::PointField::UINT8:
            return 1;
        case sensor_msgs::msg::PointField::INT16:
        case sensor_msgs::msg::PointField::UINT16:
            return 2;
        case sensor_msgs::msg::PointField::INT32:
        case sensor_msgs::msg::PointField::UINT32:
        case sensor_msgs::msg::PointField::FLOAT32:
            return 4;
        case sensor_msgs::msg::PointField::FLOAT64:
            return 8;
        default:
            return 0;
    }
}

inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8)
           | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

inline bool read_rgb_field(const uint8_t*                      data,
                           const sensor_msgs::msg::PointField& field,
                           bool                                swap_endianness,
                           bool                                source_has_alpha,
                           uint32_t&                           packed_rgba_out)
{
    if (field.datatype == sensor_msgs::msg::PointField::UINT8 && field.count >= 3)
    {
        const uint8_t r = data[0];
        const uint8_t g = data[1];
        const uint8_t b = data[2];
        const uint8_t a = (source_has_alpha && field.count >= 4) ? data[3] : 0xFFu;
        packed_rgba_out = pack_rgba(r, g, b, a);
        return true;
    }

    uint32_t source = 0;
    switch (field.datatype)
    {
        case sensor_msgs::msg::PointField::INT32:
        case sensor_msgs::msg::PointField::UINT32:
        case sensor_msgs::msg::PointField::FLOAT32:
            source = read_typed_value<uint32_t>(data, swap_endianness);
            break;
        default:
            return false;
    }

    const uint8_t b = static_cast<uint8_t>((source >> 0) & 0xFFu);
    const uint8_t g = static_cast<uint8_t>((source >> 8) & 0xFFu);
    const uint8_t r = static_cast<uint8_t>((source >> 16) & 0xFFu);
    const uint8_t a = source_has_alpha ? static_cast<uint8_t>((source >> 24) & 0xFFu) : 0xFFu;
    packed_rgba_out = pack_rgba(r, g, b, a);
    return true;
}

inline const sensor_msgs::msg::PointField* find_field(
    const std::vector<sensor_msgs::msg::PointField>& fields,
    const char*                                      name)
{
    for (const auto& field : fields)
    {
        if (field.name == name)
            return &field;
    }
    return nullptr;
}

}   // namespace detail

inline std::optional<PointCloudFrame> adapt_pointcloud_message(
    const sensor_msgs::msg::PointCloud2& message,
    const std::string&                   topic,
    size_t                               max_points = 500'000)
{
    const auto* field_x = detail::find_field(message.fields, "x");
    const auto* field_y = detail::find_field(message.fields, "y");
    const auto* field_z = detail::find_field(message.fields, "z");
    if (!field_x || !field_y || !field_z || message.point_step == 0)
        return std::nullopt;

    const size_t total_points =
        static_cast<size_t>(message.width) * static_cast<size_t>(message.height);
    if (total_points == 0)
        return std::nullopt;

    const bool  swap_endianness = message.is_bigendian != detail::host_is_big_endian();
    const auto* field_rgb       = detail::find_field(message.fields, "rgb");
    const auto* field_rgba      = detail::find_field(message.fields, "rgba");
    const auto* field_intensity = detail::find_field(message.fields, "intensity");

    PointCloudFrame frame;
    frame.topic    = topic;
    frame.frame_id = message.header.frame_id;
    frame.stamp_ns = static_cast<uint64_t>(message.header.stamp.sec) * 1'000'000'000ULL
                     + static_cast<uint64_t>(message.header.stamp.nanosec);
    frame.original_point_count = total_points;

    spectra::vec3 min_bounds{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 max_bounds{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 centroid_sum{};
    float         min_intensity      = std::numeric_limits<float>::infinity();
    float         max_intensity      = -std::numeric_limits<float>::infinity();
    bool          observed_rgb       = false;
    bool          observed_intensity = false;

    const size_t stride =
        max_points > 0 ? std::max<size_t>(1, (total_points + max_points - 1) / max_points) : 1;
    frame.points.reserve((total_points + stride - 1) / stride);

    const auto field_fits = [&](const sensor_msgs::msg::PointField& field)
    {
        const size_t elem_size = detail::datatype_size(field.datatype);
        if (elem_size == 0)
            return false;
        const size_t width = elem_size * std::max<uint32_t>(1u, field.count);
        return static_cast<size_t>(field.offset) + width <= static_cast<size_t>(message.point_step);
    };

    if (!field_fits(*field_x) || !field_fits(*field_y) || !field_fits(*field_z))
        return std::nullopt;

    for (size_t point_index = 0; point_index < total_points; point_index += stride)
    {
        const size_t base = point_index * static_cast<size_t>(message.point_step);
        if (base + message.point_step > message.data.size())
            break;

        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        if (!detail::read_numeric_field(message.data.data() + base + field_x->offset,
                                        field_x->datatype,
                                        swap_endianness,
                                        x)
            || !detail::read_numeric_field(message.data.data() + base + field_y->offset,
                                           field_y->datatype,
                                           swap_endianness,
                                           y)
            || !detail::read_numeric_field(message.data.data() + base + field_z->offset,
                                           field_z->datatype,
                                           swap_endianness,
                                           z))
        {
            continue;
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            continue;

        PointCloudPoint point;
        point.position = {x, y, z};

        if (field_intensity != nullptr && field_fits(*field_intensity))
        {
            double intensity = 0.0;
            if (detail::read_numeric_field(message.data.data() + base + field_intensity->offset,
                                           field_intensity->datatype,
                                           swap_endianness,
                                           intensity)
                && std::isfinite(intensity))
            {
                point.intensity     = static_cast<float>(intensity);
                point.has_intensity = true;
                min_intensity       = std::min(min_intensity, point.intensity);
                max_intensity       = std::max(max_intensity, point.intensity);
                observed_intensity  = true;
            }
        }

        if (field_rgba != nullptr && field_fits(*field_rgba))
        {
            if (detail::read_rgb_field(message.data.data() + base + field_rgba->offset,
                                       *field_rgba,
                                       swap_endianness,
                                       true,
                                       point.rgba))
            {
                point.has_rgb = true;
                observed_rgb  = true;
            }
        }
        else if (field_rgb != nullptr && field_fits(*field_rgb))
        {
            if (detail::read_rgb_field(message.data.data() + base + field_rgb->offset,
                                       *field_rgb,
                                       swap_endianness,
                                       false,
                                       point.rgba))
            {
                point.has_rgb = true;
                observed_rgb  = true;
            }
        }

        frame.points.push_back(point);
        min_bounds = spectra::vec3_min(min_bounds, point.position);
        max_bounds = spectra::vec3_max(max_bounds, point.position);
        centroid_sum += point.position;
        ++frame.point_count;
    }

    if (frame.point_count == 0)
        return std::nullopt;

    frame.min_bounds    = min_bounds;
    frame.max_bounds    = max_bounds;
    frame.centroid      = centroid_sum / static_cast<double>(frame.point_count);
    frame.has_rgb       = observed_rgb;
    frame.has_intensity = observed_intensity;
    if (observed_intensity)
    {
        frame.min_intensity = min_intensity;
        frame.max_intensity = max_intensity;
    }
    return frame;
}
#endif

}   // namespace spectra::adapters::ros2
