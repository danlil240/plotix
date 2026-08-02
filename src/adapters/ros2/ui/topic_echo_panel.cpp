#include "ui/topic_echo_panel.hpp"
#include "ui/field_drag_drop.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/shell/shell_style.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
#endif

// rclcpp deserialization helpers
#include <rclcpp/version.h>
#include <rclcpp/serialization.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

// Humble: get_typesupport_handle; Jazzy (rclcpp >= 28): get_message_typesupport_handle.
namespace spectra::adapters::ros2::detail
{
inline const rosidl_message_type_support_t* get_message_ts_handle(const std::string& type_name,
                                                                  const std::string& ts_id,
                                                                  rcpputils::SharedLibrary& lib)
{
#if defined(RCLCPP_VERSION_MAJOR) && RCLCPP_VERSION_MAJOR >= 28
    return rclcpp::get_message_typesupport_handle(type_name, ts_id, lib);
#else
    return rclcpp::get_typesupport_handle(type_name, ts_id, lib);
#endif
}
}   // namespace spectra::adapters::ros2::detail

#ifdef SPECTRA_ROS2_BAG
    #include "bag_reader.hpp"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/*static*/ double TopicEchoPanel::wall_time_s_now()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

namespace
{

struct RosMessageTypeSupport
{
    std::shared_ptr<rcpputils::SharedLibrary>                   intro_lib;
    const rosidl_message_type_support_t*                        intro_ts{nullptr};
    std::shared_ptr<rcpputils::SharedLibrary>                   cpp_lib;
    const rosidl_message_type_support_t*                        cpp_ts{nullptr};
    const rosidl_typesupport_introspection_cpp::MessageMembers* members{nullptr};
};

std::optional<RosMessageTypeSupport> resolve_message_typesupport(const std::string& type_name)
{
    RosMessageTypeSupport out;
    try
    {
        out.intro_lib =
            rclcpp::get_typesupport_library(type_name, "rosidl_typesupport_introspection_cpp");
        out.intro_ts = detail::get_message_ts_handle(type_name,
                                                     "rosidl_typesupport_introspection_cpp",
                                                     *out.intro_lib);
        out.cpp_lib  = rclcpp::get_typesupport_library(type_name, "rosidl_typesupport_cpp");
        out.cpp_ts =
            detail::get_message_ts_handle(type_name, "rosidl_typesupport_cpp", *out.cpp_lib);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }

    if (!out.intro_ts || !out.cpp_ts)
        return std::nullopt;

    out.members = static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers*>(
        out.intro_ts->data);
    if (!out.members || out.members->size_of_ == 0)
        return std::nullopt;

    return out;
}

}   // namespace

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TopicEchoPanel::TopicEchoPanel(rclcpp::Node::SharedPtr node, MessageIntrospector& intr)
    : node_(std::move(node)), intr_(intr)
{
    ring_.reserve(max_messages_);
}

TopicEchoPanel::~TopicEchoPanel()
{
    std::lock_guard<std::mutex> lk(sub_mutex_);
    subscription_.reset();
}

// ---------------------------------------------------------------------------
// Topic selection
// ---------------------------------------------------------------------------

void TopicEchoPanel::set_topic(const std::string& topic_name, const std::string& type_name)
{
    std::lock_guard<std::mutex> lk(sub_mutex_);

    // Unsubscribe from old topic.
    subscription_.reset();
    schema_.reset();

    topic_name_ = topic_name;
    type_name_  = type_name;

    if (topic_name.empty() || type_name.empty())
        return;

    // Introspect schema.
    schema_ = intr_.introspect(type_name);
    if (!schema_)
        return;

    // Create generic subscription.
    auto qos      = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();
    subscription_ = node_->create_generic_subscription(
        topic_name,
        type_name,
        qos,
        [this](sub_compat::SerializedMessageCallbackArg raw_msg) { on_message(raw_msg); });
}

// ---------------------------------------------------------------------------
// Playback controls
// ---------------------------------------------------------------------------

void TopicEchoPanel::pause()
{
    paused_.store(true, std::memory_order_release);
}

void TopicEchoPanel::resume()
{
    paused_.store(false, std::memory_order_release);
}

void TopicEchoPanel::clear()
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    ring_.clear();
    next_seq_           = 0;
    selected_msg_idx_   = -1;
    bag_manual_msg_idx_ = -1;
}

void TopicEchoPanel::set_bag_echo_mode(bool enabled)
{
    bag_echo_mode_       = enabled;
    bag_playhead_follow_ = true;
    bag_manual_msg_idx_  = -1;
    if (enabled)
        paused_.store(true, std::memory_order_release);
    else
        paused_.store(false, std::memory_order_release);
}

void TopicEchoPanel::set_bag_playhead(double bag_time_sec)
{
    bag_playhead_sec_ = bag_time_sec;
}

int TopicEchoPanel::find_nearest_bag_message_index(const std::vector<EchoMessage>& snap) const
{
    int    best_idx  = -1;
    double best_dist = 1e300;
    for (int i = 0; i < static_cast<int>(snap.size()); ++i)
    {
        const double bag_t = snap[static_cast<size_t>(i)].bag_time_sec;
        if (bag_t < 0.0)
            continue;
        const double dist = std::abs(bag_t - bag_playhead_sec_);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_idx  = i;
        }
    }
    return best_idx;
}

#ifdef SPECTRA_ROS2_BAG
void TopicEchoPanel::ingest_bag_message(const BagMessage&    msg,
                                        MessageIntrospector& intr,
                                        int64_t              bag_start_time_ns)
{
    if (!bag_echo_mode_ || msg.serialized_data.size() < 4)
        return;
    if (!topic_name_.empty() && msg.topic != topic_name_)
        return;

    if (type_name_.empty() || type_name_ != msg.type)
    {
        if (!msg.type.empty())
            set_topic(msg.topic, msg.type);
        else if (topic_name_.empty())
            return;
    }

    auto schema = intr.introspect(type_name_);
    if (!schema)
        return;

    const auto ts = resolve_message_typesupport(type_name_);
    if (!ts)
        return;

    const auto*  members       = ts->members;
    const size_t cdr_body_size = msg.serialized_data.size() - 4;
    if (cdr_body_size < members->size_of_)
        return;

    std::vector<uint8_t> buf(members->size_of_);
    members->init_function(buf.data(), rosidl_runtime_cpp::MessageInitialization::ALL);

    rclcpp::SerializedMessage serialized;
    serialized.reserve(msg.serialized_data.size());
    std::memcpy(serialized.get_rcl_serialized_message().buffer,
                msg.serialized_data.data(),
                msg.serialized_data.size());
    serialized.get_rcl_serialized_message().buffer_length = msg.serialized_data.size();

    rclcpp::SerializationBase serializer(ts->cpp_ts);
    try
    {
        serializer.deserialize_message(&serialized, buf.data());
    }
    catch (...)
    {
        members->fini_function(buf.data());
        return;
    }

    int64_t ts_ns = msg.timestamp_ns;
    {
        const auto* hdr_fd = schema->find_field("header.stamp.sec");
        if (hdr_fd)
        {
            FieldAccessor acc_sec  = intr.make_accessor(*schema, "header.stamp.sec");
            FieldAccessor acc_nsec = intr.make_accessor(*schema, "header.stamp.nanosec");
            if (acc_sec.valid() && acc_nsec.valid())
            {
                const int64_t sec  = acc_sec.extract_int64(buf.data());
                const int64_t nsec = acc_nsec.extract_int64(buf.data());
                ts_ns              = sec * 1'000'000'000LL + nsec;
            }
        }
    }

    const double bag_time_sec = static_cast<double>(msg.timestamp_ns - bag_start_time_ns) * 1e-9;

    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        seq = next_seq_++;
    }

    EchoMessage echo  = build_echo_message(buf.data(), *schema, seq, ts_ns, wall_time_s_now());
    echo.bag_time_sec = bag_time_sec;
    members->fini_function(buf.data());

    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        if (ring_.size() >= max_messages_)
            ring_.erase(ring_.begin());
        ring_.push_back(std::move(echo));
    }

    total_received_.fetch_add(1, std::memory_order_relaxed);
}
#endif   // SPECTRA_ROS2_BAG

void TopicEchoPanel::set_max_messages(size_t n)
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    max_messages_ = (n > 0) ? n : 1;
    while (ring_.size() > max_messages_)
    {
        ring_.erase(ring_.begin());
    }
}

// ---------------------------------------------------------------------------
// Testing helpers
// ---------------------------------------------------------------------------

size_t TopicEchoPanel::message_count() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    return ring_.size();
}

std::vector<EchoMessage> TopicEchoPanel::messages_snapshot() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    return ring_;
}

std::unique_ptr<EchoMessage> TopicEchoPanel::latest_message() const
{
    std::lock_guard<std::mutex> lk(ring_mutex_);
    if (ring_.empty())
        return nullptr;
    return std::make_unique<EchoMessage>(ring_.back());
}

void TopicEchoPanel::inject_message(EchoMessage msg)
{
    if (paused_.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lk(ring_mutex_);
    if (ring_.size() >= max_messages_)
    {
        ring_.erase(ring_.begin());
    }
    ring_.push_back(std::move(msg));
    total_received_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Message reception (executor thread)
// ---------------------------------------------------------------------------

void TopicEchoPanel::on_message(sub_compat::SerializedMessageCallbackArg raw_msg)
{
    if (paused_.load(std::memory_order_acquire))
        return;

    // Snapshot schema/type under sub_mutex_ to avoid TOCTOU with set_topic().
    std::shared_ptr<const MessageSchema> schema_snap;
    std::string                          type_snap;
    {
        std::lock_guard<std::mutex> lk(sub_mutex_);
        schema_snap = schema_;
        type_snap   = type_name_;
    }
    if (!schema_snap || type_snap.empty())
        return;

    const auto ts = resolve_message_typesupport(type_snap);
    if (!ts)
        return;

    const auto* members = ts->members;

    // Allocate and deserialize message.
    std::vector<uint8_t> buf(members->size_of_);
    members->init_function(buf.data(), rosidl_runtime_cpp::MessageInitialization::ALL);

    rclcpp::SerializationBase serializer(ts->cpp_ts);
    try
    {
        serializer.deserialize_message(raw_msg.get(), buf.data());
    }
    catch (...)
    {
        members->fini_function(buf.data());
        return;
    }

    // Extract timestamp from header if present.
    int64_t ts_ns = 0;
    {
        const auto* hdr_fd = schema_snap->find_field("header.stamp.sec");
        if (hdr_fd)
        {
            // Try to read sec + nanosec.
            FieldAccessor acc_sec  = intr_.make_accessor(*schema_snap, "header.stamp.sec");
            FieldAccessor acc_nsec = intr_.make_accessor(*schema_snap, "header.stamp.nanosec");
            if (acc_sec.valid() && acc_nsec.valid())
            {
                const int64_t sec  = acc_sec.extract_int64(buf.data());
                const int64_t nsec = acc_nsec.extract_int64(buf.data());
                ts_ns              = sec * 1'000'000'000LL + nsec;
            }
        }
        if (ts_ns == 0)
        {
            using namespace std::chrono;
            ts_ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        }
    }

    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        seq = next_seq_++;
    }

    EchoMessage msg = build_echo_message(buf.data(), *schema_snap, seq, ts_ns, wall_time_s_now());
    members->fini_function(buf.data());

    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        if (ring_.size() >= max_messages_)
        {
            ring_.erase(ring_.begin());
        }
        ring_.push_back(std::move(msg));
    }

    total_received_.fetch_add(1, std::memory_order_relaxed);

    // Notify external listeners (e.g. topic stats) about the received message.
    if (message_cb_)
    {
        const size_t msg_bytes = raw_msg ? raw_msg->size() : members->size_of_;
        message_cb_(topic_name_, msg_bytes);
    }
}

// ---------------------------------------------------------------------------
// EchoMessage builder
// ---------------------------------------------------------------------------

/*static*/ EchoMessage TopicEchoPanel::build_echo_message(const void*          msg_ptr,
                                                          const MessageSchema& schema,
                                                          uint64_t             seq,
                                                          int64_t              timestamp_ns,
                                                          double               wall_time_s)
{
    EchoMessage msg;
    msg.seq          = seq;
    msg.timestamp_ns = timestamp_ns;
    msg.wall_time_s  = wall_time_s;
    extract_fields(msg_ptr, schema.fields, msg.fields, 0);
    return msg;
}

/*static*/ void TopicEchoPanel::extract_fields(const void*                         msg_ptr,
                                               const std::vector<FieldDescriptor>& fields,
                                               std::vector<EchoFieldValue>&        out,
                                               int                                 depth)
{
    for (const auto& fd : fields)
    {
        if (fd.type == FieldType::Message && !fd.children.empty() && !fd.is_array)
        {
            // Nested message: emit a header entry then recurse.
            EchoFieldValue hdr;
            hdr.path         = fd.full_path;
            hdr.display_name = fd.name;
            hdr.depth        = depth;
            hdr.kind         = EchoFieldValue::Kind::NestedHead;
            out.push_back(std::move(hdr));

            // The nested struct starts at offset fd.offset inside msg_ptr.
            const uint8_t* nested_ptr = static_cast<const uint8_t*>(msg_ptr) + fd.offset;
            extract_fields(nested_ptr, fd.children, out, depth + 1);
            continue;
        }

        if (fd.is_array || fd.is_dynamic_array)
        {
            // Array: emit a header with count, then up to 64 elements.
            const uint8_t* base  = static_cast<const uint8_t*>(msg_ptr) + fd.offset;
            size_t         count = 0;

            // Determine element byte-size from type (needed for dynamic array
            // count computation below).
            size_t elem_sz = 0;
            if (fd.type != FieldType::Message && is_numeric(fd.type))
            {
                elem_sz = 8;   // default: double/int64
                switch (fd.type)
                {
                    case FieldType::Bool:
                    case FieldType::Byte:
                    case FieldType::Char:
                    case FieldType::Int8:
                    case FieldType::Uint8:
                        elem_sz = 1;
                        break;
                    case FieldType::Int16:
                    case FieldType::Uint16:
                        elem_sz = 2;
                        break;
                    case FieldType::Int32:
                    case FieldType::Uint32:
                    case FieldType::Float32:
                        elem_sz = 4;
                        break;
                    case FieldType::Float64:
                    case FieldType::Int64:
                    case FieldType::Uint64:
                    default:
                        elem_sz = 8;
                        break;
                }
            }

            if (fd.is_dynamic_array)
            {
                // std::vector layout (libstdc++/libc++):
                //   offset 0:              _M_start         (T*)
                //   offset sizeof(void*):  _M_finish        (T*)
                //   offset 2*sizeof(void*): _M_end_of_storage (T*)
                // Count = (_M_finish - _M_start) / sizeof(T).
                const uint8_t* start_ptr  = nullptr;
                const uint8_t* finish_ptr = nullptr;
                std::memcpy(&start_ptr, base, sizeof(void*));
                std::memcpy(&finish_ptr, base + sizeof(void*), sizeof(void*));
                if (start_ptr && finish_ptr >= start_ptr && elem_sz > 0)
                {
                    count = static_cast<size_t>(finish_ptr - start_ptr) / elem_sz;
                }
            }
            else
            {
                count = static_cast<size_t>(fd.array_size);
            }

            EchoFieldValue arr_hdr;
            arr_hdr.path         = fd.full_path;
            arr_hdr.display_name = fd.name;
            arr_hdr.depth        = depth;
            arr_hdr.kind         = EchoFieldValue::Kind::ArrayHead;
            arr_hdr.array_len    = static_cast<int>(count);
            out.push_back(std::move(arr_hdr));

            if (fd.type != FieldType::Message && is_numeric(fd.type))
            {
                // Numeric array elements.
                const uint8_t* data_ptr = nullptr;
                if (fd.is_dynamic_array)
                {
                    // First 8 bytes of std::vector are the data pointer.
                    std::memcpy(&data_ptr, base, sizeof(void*));
                }
                else
                {
                    data_ptr = base;
                }

                const size_t max_elems = std::min(count, size_t{64});
                for (size_t i = 0; i < max_elems && data_ptr; ++i)
                {
                    double         val = 0.0;
                    const uint8_t* ep  = data_ptr + i * elem_sz;
                    switch (fd.type)
                    {
                        case FieldType::Bool:
                        {
                            uint8_t v = 0;
                            std::memcpy(&v, ep, 1);
                            val = v ? 1.0 : 0.0;
                            break;
                        }
                        case FieldType::Byte:
                        {
                            uint8_t v = 0;
                            std::memcpy(&v, ep, 1);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Char:
                        {
                            int8_t v = 0;
                            std::memcpy(&v, ep, 1);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Int8:
                        {
                            int8_t v = 0;
                            std::memcpy(&v, ep, 1);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Uint8:
                        {
                            uint8_t v = 0;
                            std::memcpy(&v, ep, 1);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Int16:
                        {
                            int16_t v = 0;
                            std::memcpy(&v, ep, 2);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Uint16:
                        {
                            uint16_t v = 0;
                            std::memcpy(&v, ep, 2);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Int32:
                        {
                            int32_t v = 0;
                            std::memcpy(&v, ep, 4);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Uint32:
                        {
                            uint32_t v = 0;
                            std::memcpy(&v, ep, 4);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Float32:
                        {
                            float v = NAN;
                            std::memcpy(&v, ep, 4);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Float64:
                        {
                            double v = NAN;
                            std::memcpy(&v, ep, 8);
                            val = v;
                            break;
                        }
                        case FieldType::Int64:
                        {
                            int64_t v = 0;
                            std::memcpy(&v, ep, 8);
                            val = static_cast<double>(v);
                            break;
                        }
                        case FieldType::Uint64:
                        {
                            uint64_t v = 0;
                            std::memcpy(&v, ep, 8);
                            val = static_cast<double>(v);
                            break;
                        }
                        default:
                            break;
                    }

                    EchoFieldValue elem;
                    elem.path         = fd.full_path + "[" + std::to_string(i) + "]";
                    elem.display_name = "[" + std::to_string(i) + "]";
                    elem.depth        = depth + 1;
                    elem.kind         = EchoFieldValue::Kind::ArrayElement;
                    elem.numeric      = val;
                    out.push_back(std::move(elem));
                }
            }
            continue;
        }

        // Scalar field.
        out.push_back(make_scalar_value(msg_ptr, fd, depth));
    }
}

/*static*/ EchoFieldValue TopicEchoPanel::make_scalar_value(const void*            msg_ptr,
                                                            const FieldDescriptor& fd,
                                                            int                    depth)
{
    EchoFieldValue fv;
    fv.path         = fd.full_path;
    fv.display_name = fd.name;
    fv.depth        = depth;

    const uint8_t* ptr = static_cast<const uint8_t*>(msg_ptr) + fd.offset;

    switch (fd.type)
    {
        case FieldType::Bool:
        {
            uint8_t v = 0;
            std::memcpy(&v, ptr, 1);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = v ? 1.0 : 0.0;
            break;
        }
        case FieldType::Byte:
        case FieldType::Uint8:
        {
            uint8_t v = 0;
            std::memcpy(&v, ptr, 1);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Char:
        case FieldType::Int8:
        {
            int8_t v = 0;
            std::memcpy(&v, ptr, 1);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int16:
        {
            int16_t v = 0;
            std::memcpy(&v, ptr, 2);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint16:
        {
            uint16_t v = 0;
            std::memcpy(&v, ptr, 2);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int32:
        {
            int32_t v = 0;
            std::memcpy(&v, ptr, 4);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint32:
        {
            uint32_t v = 0;
            std::memcpy(&v, ptr, 4);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Int64:
        {
            int64_t v = 0;
            std::memcpy(&v, ptr, 8);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Uint64:
        {
            uint64_t v = 0;
            std::memcpy(&v, ptr, 8);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Float32:
        {
            float v = 0.0f;
            std::memcpy(&v, ptr, 4);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = static_cast<double>(v);
            break;
        }
        case FieldType::Float64:
        {
            double v = 0.0;
            std::memcpy(&v, ptr, 8);
            fv.kind    = EchoFieldValue::Kind::Numeric;
            fv.numeric = v;
            break;
        }
        case FieldType::String:
        {
            // std::string layout: SSO or heap pointer at offset 0.
            const auto* s = reinterpret_cast<const std::string*>(ptr);
            fv.kind       = EchoFieldValue::Kind::Text;
            if (s->size() > 128)
            {
                fv.text = s->substr(0, 128) + "...";
            }
            else
            {
                fv.text = *s;
            }
            break;
        }
        default:
        {
            fv.kind = EchoFieldValue::Kind::Text;
            fv.text = "(unsupported)";
            break;
        }
    }

    return fv;
}

// ---------------------------------------------------------------------------
// Formatting utilities
// ---------------------------------------------------------------------------

/*static*/ std::string TopicEchoPanel::format_timestamp(int64_t ns)
{
    const int64_t sec  = ns / 1'000'000'000LL;
    const int64_t nsec = ns % 1'000'000'000LL;
    return std::format("{}.{:09}", sec, nsec);
}

/*static*/ std::string TopicEchoPanel::format_numeric(double v)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0.0 ? "inf" : "-inf";

    // Use shorter representation: if integer-valued, show no decimal.
    if (v == std::floor(v) && std::abs(v) < 1e15)
        return std::format("{:.0f}", v);
    return std::format("{}", v);
}

/*static*/ std::string TopicEchoPanel::field_value_text(const EchoFieldValue& fv)
{
    switch (fv.kind)
    {
        case EchoFieldValue::Kind::Numeric:
        case EchoFieldValue::Kind::ArrayElement:
            return format_numeric(fv.numeric);
        case EchoFieldValue::Kind::Text:
            return fv.text;
        default:
            return fv.display_name;
    }
}

/*static*/ std::string TopicEchoPanel::format_message_raw(const EchoMessage& msg)
{
    std::string out;
    out += std::format("seq: {}\n", msg.seq);
    out += std::format("stamp: {}\n", format_timestamp(msg.timestamp_ns));
    for (const auto& fv : msg.fields)
    {
        if (fv.kind == EchoFieldValue::Kind::NestedHead
            || fv.kind == EchoFieldValue::Kind::ArrayHead)
            continue;
        const std::string indent(static_cast<size_t>(fv.depth) * 2, ' ');
        out += indent + fv.path + ": " + field_value_text(fv) + "\n";
    }
    return out;
}

namespace
{

std::string json_escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const unsigned char uc : text)
    {
        const char c = static_cast<char>(uc);
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (uc < 0x20)
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(uc));
                else
                    out += c;
                break;
        }
    }
    return out;
}

}   // namespace

/*static*/ std::string TopicEchoPanel::format_message_json(const EchoMessage& msg)
{
    std::string out = "{\n";
    out += std::format("  \"seq\": {},\n", msg.seq);
    out += std::format("  \"stamp_ns\": {},\n", msg.timestamp_ns);
    out += "  \"fields\": {\n";
    bool first = true;
    for (const auto& fv : msg.fields)
    {
        if (fv.kind == EchoFieldValue::Kind::NestedHead
            || fv.kind == EchoFieldValue::Kind::ArrayHead)
            continue;
        if (!first)
            out += ",\n";
        first                     = false;
        const std::string value   = field_value_text(fv);
        const bool        numeric = fv.kind == EchoFieldValue::Kind::Numeric
                             || fv.kind == EchoFieldValue::Kind::ArrayElement;
        const bool quoted = !numeric || !std::isfinite(fv.numeric);
        out += std::format("    \"{}\": ", json_escape(fv.path));
        if (quoted)
            out += std::format("\"{}\"", json_escape(value));
        else
            out += value;
    }
    out += "\n  }\n}";
    return out;
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

namespace
{

static constexpr ImVec4 kColorPaused   = {0.90f, 0.60f, 0.10f, 1.0f};   // amber
static constexpr ImVec4 kColorLive     = {0.20f, 0.80f, 0.30f, 1.0f};   // green
static constexpr ImVec4 kColorDisabled = {0.50f, 0.50f, 0.50f, 1.0f};   // gray
static constexpr ImVec4 kColorNumeric  = {0.60f, 0.88f, 1.00f, 1.0f};   // light blue
static constexpr ImVec4 kColorText     = {0.90f, 0.90f, 0.65f, 1.0f};   // light yellow
static constexpr ImVec4 kColorNested   = {0.80f, 0.70f, 0.50f, 1.0f};   // tan
static constexpr ImVec4 kColorArray    = {0.75f, 0.60f, 0.90f, 1.0f};   // lavender

}   // anonymous namespace

void TopicEchoPanel::draw(bool* p_open)
{
    if (!ImGui::GetCurrentContext())
        return;

    // Rate throttle: only rebuild/display at display_hz.
    const double now = wall_time_s_now();
    const bool   should_refresh =
        (display_interval_s_ <= 0.0) || (now - last_draw_time_s_ >= display_interval_s_);

    ImGui::SetNextWindowSize(ImVec2(600, 520), ImGuiCond_FirstUseEver);
    if (!spectra::ui::shell::begin_panel(title_.c_str(), p_open))
    {
        spectra::ui::shell::end_panel();
        return;
    }

    draw_controls();
    draw_view_mode_bar();

    ImGui::Separator();

    if (topic_name_.empty())
    {
        ImGui::TextDisabled("Select a topic in Topic Monitor to stream messages here.");
        spectra::ui::shell::end_panel();
        return;
    }

    if (should_refresh)
        last_draw_time_s_ = now;

    std::vector<EchoMessage> snap;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        snap = ring_;
    }

    if (snap.empty())
    {
        ImGui::TextDisabled("Waiting for messages on %s …", topic_name_.c_str());
        spectra::ui::shell::end_panel();
        return;
    }

    int display_idx = selected_msg_idx_;
    if (echo_live_ || display_idx < 0 || display_idx >= static_cast<int>(snap.size()))
        display_idx = static_cast<int>(snap.size()) - 1;

    if (bag_echo_mode_ && bag_playhead_follow_)
    {
        const int nearest = find_nearest_bag_message_index(snap);
        if (nearest >= 0)
            display_idx = nearest;
    }
    else if (bag_echo_mode_ && bag_manual_msg_idx_ >= 0)
    {
        display_idx = bag_manual_msg_idx_;
    }

    const float list_width = 210.0f;
    const float avail_h    = ImGui::GetContentRegionAvail().y;

    draw_message_list(snap, display_idx);

    ImGui::SameLine();
    ImGui::BeginChild("##msg_detail", ImVec2(0, avail_h), ImGuiChildFlags_Borders);

    EchoMessage display_msg = snap[static_cast<size_t>(display_idx)];

    if (bag_echo_mode_)
    {
        ImGui::TextColored(kColorPaused, "Bag playhead  %.3f s", bag_playhead_sec_);
        ImGui::SameLine();
        if (ImGui::SmallButton(bag_playhead_follow_ ? "Follow##bag_echo" : "Pinned##bag_echo"))
            bag_playhead_follow_ = !bag_playhead_follow_;
    }

    ImGui::TextDisabled("msg #%llu  |  t = %s",
                        static_cast<unsigned long long>(display_msg.seq),
                        format_timestamp(display_msg.timestamp_ns).c_str());
    if (display_idx == static_cast<int>(snap.size()) - 1 && echo_live_)
    {
        ImGui::SameLine();
        const auto& th = spectra::ui::theme();
        ImGui::TextColored(ImVec4(th.accent.r, th.accent.g, th.accent.b, 1.0f), "LIVE");
    }
    ImGui::Separator();

    draw_message_content(display_msg);

    ImGui::EndChild();

    spectra::ui::shell::end_panel();
}

void TopicEchoPanel::draw_view_mode_bar()
{
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::min(avail_w * 0.42f, 220.0f));
    ImGui::InputTextWithHint("##echo_filter",
                             "Filter messages…",
                             message_filter_,
                             sizeof(message_filter_));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Filter by sequence number or timestamp");

    ImGui::SameLine();
    if (spectra::ui::widgets::chip("Tree", view_mode_ == EchoViewMode::Tree))
        view_mode_ = EchoViewMode::Tree;
    ImGui::SameLine(0.0f, 4.0f);
    if (spectra::ui::widgets::chip("Raw", view_mode_ == EchoViewMode::Raw))
        view_mode_ = EchoViewMode::Raw;
    ImGui::SameLine(0.0f, 4.0f);
    if (spectra::ui::widgets::chip("JSON", view_mode_ == EchoViewMode::Json))
        view_mode_ = EchoViewMode::Json;

    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 72.0f);
    if (spectra::ui::widgets::icon_button_small(spectra::ui::icon_str(spectra::ui::Icon::Copy),
                                                "Copy latest message"))
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        if (!ring_.empty())
            ImGui::SetClipboardText(format_message_raw(ring_.back()).c_str());
    }
}

void TopicEchoPanel::draw_message_list(const std::vector<EchoMessage>& snap, int display_idx)
{
    const float avail_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##msg_list", ImVec2(210.0f, avail_h), ImGuiChildFlags_Borders);

    ImGui::TextDisabled("%zu / %zu msg", snap.size(), static_cast<size_t>(total_received_.load()));
    ImGui::Separator();

    const std::string filter = message_filter_;
    const auto&       th     = spectra::ui::theme();
    const float row_h = ImGui::GetTextLineHeight() + spectra::ui::tokens::ROW_PADDING_V * 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 6.0f));
    for (int i = static_cast<int>(snap.size()) - 1; i >= 0; --i)
    {
        const auto& msg   = snap[static_cast<size_t>(i)];
        const auto  label = std::format("#{}  {}", msg.seq, format_timestamp(msg.timestamp_ns));

        if (!filter.empty())
        {
            const std::string seq_str = std::to_string(msg.seq);
            if (label.find(filter) == std::string::npos
                && seq_str.find(filter) == std::string::npos)
                continue;
        }

        const bool is_latest = (i == static_cast<int>(snap.size()) - 1);
        const bool is_sel    = (display_idx == i);

        if (is_latest)
        {
            ImGui::PushStyleColor(ImGuiCol_Header,
                                  ImVec4(th.accent.r, th.accent.g, th.accent.b, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                  ImVec4(th.accent.r, th.accent.g, th.accent.b, 0.28f));
        }

        if (ImGui::Selectable(label.c_str(), is_sel, 0, ImVec2(0, row_h)))
        {
            selected_msg_idx_ = i;
            echo_live_        = false;
            if (bag_echo_mode_)
            {
                bag_manual_msg_idx_  = i;
                bag_playhead_follow_ = false;
            }
        }

        if (is_latest)
            ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void TopicEchoPanel::draw_message_content(const EchoMessage& msg)
{
    switch (view_mode_)
    {
        case EchoViewMode::Tree:
        {
            EchoMessage editable = msg;
            draw_message_tree(editable);
            break;
        }
        case EchoViewMode::Raw:
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorText);
            ImGui::TextUnformatted(format_message_raw(msg).c_str());
            ImGui::PopStyleColor();
            break;
        }
        case EchoViewMode::Json:
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorNumeric);
            ImGui::TextUnformatted(format_message_json(msg).c_str());
            ImGui::PopStyleColor();
            break;
        }
    }
}

void TopicEchoPanel::sync_workspace(const std::string& topic,
                                    const std::string& type,
                                    bool               selection_changed)
{
    if (manually_pinned_ || !selection_changed || topic.empty())
        return;
    if (topic == topic_name_)
        return;
    set_topic(topic, type);
}

void TopicEchoPanel::draw_controls()
{
    const auto& th = spectra::ui::theme();

    if (topic_name_.empty())
    {
        ImGui::TextColored(kColorDisabled, "%s No topic", ui::icon_str(ui::Icon::Circle));
        return;
    }

    const ImVec4   status_col  = is_paused() ? kColorPaused : kColorLive;
    const ui::Icon status_icon = is_paused() ? ui::Icon::Pause : ui::Icon::Circle;
    ImGui::TextColored(status_col, "%s %s", ui::icon_str(status_icon), topic_name_.c_str());

    ImGui::SameLine(0.0f, spectra::ui::tokens::SPACE_3);
    if (ui::widgets::icon_button_small(ui::icon_str(ui::Icon::Pin),
                                       manually_pinned_ ? "Unpin topic" : "Pin topic",
                                       manually_pinned_))
        manually_pinned_ = !manually_pinned_;

    ImGui::SameLine(0.0f, spectra::ui::tokens::SPACE_2);
    if (ui::widgets::icon_button_small(ui::icon_str(is_paused() ? ui::Icon::Play : ui::Icon::Pause),
                                       is_paused() ? "Resume receiving" : "Pause receiving",
                                       is_paused()))
    {
        if (is_paused())
            resume();
        else
            pause();
    }

    ImGui::SameLine(0.0f, spectra::ui::tokens::SPACE_2);
    ImGui::BeginDisabled(message_count() == 0);
    if (ui::widgets::icon_button_small(ui::icon_str(ui::Icon::Trash), "Clear captured messages"))
        clear();
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, spectra::ui::tokens::SPACE_3);
    if (ui::widgets::chip("Echo live", echo_live_))
        set_echo_live(!echo_live_);

    if (!type_name_.empty())
    {
        const auto  slash = type_name_.rfind('/');
        const char* short_type =
            (slash != std::string::npos) ? type_name_.c_str() + slash + 1 : type_name_.c_str();
        const float type_w = ImGui::CalcTextSize(short_type).x + 8.0f;
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - type_w);
        ImGui::TextDisabled("%s", short_type);
    }

    (void)th;
}

void TopicEchoPanel::draw_message_tree(EchoMessage& msg)
{
    // We iterate through the flat field list and use depth to maintain a
    // logical tree via ImGui TreeNode.  Array heads track open state per
    // entry so the user can expand/collapse individual arrays.

    // Reset hovered_field_ at frame start — each field node sets it on hover.
    hovered_field_.clear();

    size_t idx = 0;
    while (idx < msg.fields.size())
    {
        draw_field_node(msg.fields[idx], idx, msg.fields);
    }

    // Fire hover callback if the hovered field changed.
    if (hovered_field_ != prev_hovered_field_)
    {
        prev_hovered_field_ = hovered_field_;
        if (hover_cb_)
            hover_cb_(topic_name_, hovered_field_);
    }
}

void TopicEchoPanel::draw_field_node(EchoFieldValue&                    fv,
                                     size_t&                            idx,
                                     const std::vector<EchoFieldValue>& all_fields)
{
    const float indent = static_cast<float>(fv.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f)
        ImGui::Indent(indent);

    switch (fv.kind)
    {
        case EchoFieldValue::Kind::NestedHead:
        {
            // Non-leaf: just a label; children follow at depth+1.
            ImGui::TextColored(kColorNested, "%s:", fv.display_name.c_str());
            ++idx;
            // Consume children (depth > fv.depth).
            const int parent_depth = fv.depth;
            while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
            {
                auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                draw_field_node(child, idx, all_fields);
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayHead:
        {
            const std::string label = std::format("{}  [{} items]", fv.display_name, fv.array_len);
            const bool        open  = ImGui::TreeNodeEx(fv.path.c_str(),
                                                ImGuiTreeNodeFlags_SpanFullWidth,
                                                "%s",
                                                label.c_str());
            fv.is_open              = open;
            ++idx;
            const int parent_depth = fv.depth;
            if (open)
            {
                // Render children (Kind::ArrayElement at depth+1).
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                {
                    auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                    draw_field_node(child, idx, all_fields);
                }
                ImGui::TreePop();
            }
            else
            {
                // Skip children.
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                {
                    ++idx;
                }
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayElement:
        {
            // Invisible selectable so this row gets a drag handle.
            const std::string sel_id = std::format("##sel_{}", fv.path);
            ImGui::Selectable(
                sel_id.c_str(),
                false,
                ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                ImVec2(0, ImGui::GetTextLineHeight()));
            // Hover highlight + copy button.
            if (ImGui::IsItemHovered())
            {
                hovered_field_ = fv.path;
                // Small clipboard icon button on hover.
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18.0f);
                const std::string copy_id = std::format("##cp_{}", fv.path);
                if (ImGui::SmallButton(copy_id.c_str()))
                    ImGui::SetClipboardText(field_value_text(fv).c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Copy value: %s", field_value_text(fv).c_str());
            }
            // Drag source for numeric array element.
            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name_;
                payload.field_path = fv.path;
                payload.type_name  = type_name_;
                payload.label      = FieldDragPayload::make_label(topic_name_, fv.path);
                drag_drop_->begin_drag_source(payload);
                const std::string ctx_id = std::format("##fctx_{}", fv.path);
                drag_drop_->show_context_menu(payload, ctx_id.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(kColorArray, "%-8s", fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorNumeric, "%s", format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Numeric:
        {
            // Invisible selectable so this row gets a drag handle.
            const std::string sel_id = std::format("##sel_{}", fv.path);
            ImGui::Selectable(
                sel_id.c_str(),
                false,
                ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                ImVec2(0, ImGui::GetTextLineHeight()));
            // Hover highlight + copy button.
            if (ImGui::IsItemHovered())
            {
                hovered_field_ = fv.path;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18.0f);
                const std::string copy_id = std::format("##cp_{}", fv.path);
                if (ImGui::SmallButton(copy_id.c_str()))
                    ImGui::SetClipboardText(field_value_text(fv).c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Copy value: %s", field_value_text(fv).c_str());
            }
            // Drag source + right-click menu.
            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name_;
                payload.field_path = fv.path;
                payload.type_name  = type_name_;
                payload.label      = FieldDragPayload::make_label(topic_name_, fv.path);
                drag_drop_->begin_drag_source(payload);
                const std::string ctx_id = std::format("##fctx_{}", fv.path);
                drag_drop_->show_context_menu(payload, ctx_id.c_str());
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorNumeric, "%s", format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Text:
        {
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColorText, "\"%s\"", fv.text.c_str());
            ++idx;
            break;
        }
    }

    if (indent > 0.0f)
        ImGui::Unindent(indent);
}

#else   // !SPECTRA_USE_IMGUI

void TopicEchoPanel::draw(bool* /*p_open*/) {}
void TopicEchoPanel::draw_controls() {}
void TopicEchoPanel::draw_view_mode_bar() {}
void TopicEchoPanel::draw_message_list(const std::vector<EchoMessage>&, int) {}
void TopicEchoPanel::draw_message_content(const EchoMessage&) {}
void TopicEchoPanel::draw_message_tree(EchoMessage& /*msg*/) {}
void TopicEchoPanel::draw_field_node(EchoFieldValue& /*fv*/,
                                     size_t& /*idx*/,
                                     const std::vector<EchoFieldValue>& /*all_fields*/)
{
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
