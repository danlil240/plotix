#include "px4_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>

#if !defined(_WIN32)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#else
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// MAVLink v1/v2 frame constants
// ---------------------------------------------------------------------------

static constexpr uint8_t MAVLINK_STX_V1 = 0xFE;
static constexpr uint8_t MAVLINK_STX_V2 = 0xFD;

// Minimal MAVLink message IDs for common PX4 telemetry.
// Full MAVLink parsing would require the full dialect; we extract a subset.
static constexpr uint32_t MAVLINK_MSG_HEARTBEAT           = 0;
static constexpr uint32_t MAVLINK_MSG_SYS_STATUS          = 1;
static constexpr uint32_t MAVLINK_MSG_ATTITUDE            = 30;
static constexpr uint32_t MAVLINK_MSG_LOCAL_POSITION_NED  = 32;
static constexpr uint32_t MAVLINK_MSG_GLOBAL_POSITION_INT = 33;
static constexpr uint32_t MAVLINK_MSG_GPS_RAW_INT         = 24;
static constexpr uint32_t MAVLINK_MSG_SCALED_IMU          = 26;
static constexpr uint32_t MAVLINK_MSG_RC_CHANNELS         = 65;
static constexpr uint32_t MAVLINK_MSG_SERVO_OUTPUT_RAW    = 36;
static constexpr uint32_t MAVLINK_MSG_VFR_HUD             = 74;
static constexpr uint32_t MAVLINK_MSG_HIGHRES_IMU         = 105;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float read_f32_le(const uint8_t* p)
{
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

static int32_t read_i32_le(const uint8_t* p)
{
    int32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

static uint32_t read_u32_le(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

static uint64_t read_u64_le(const uint8_t* p)
{
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

static int16_t read_i16_le(const uint8_t* p)
{
    int16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

static uint16_t read_u16_le(const uint8_t* p)
{
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

// ---------------------------------------------------------------------------
// BridgeState name
// ---------------------------------------------------------------------------

const char* bridge_state_name(BridgeState s)
{
    switch (s)
    {
        case BridgeState::Disconnected:
            return "Disconnected";
        case BridgeState::Connecting:
            return "Connecting";
        case BridgeState::Connected:
            return "Connected";
        case BridgeState::Receiving:
            return "Receiving";
        case BridgeState::ShuttingDown:
            return "ShuttingDown";
        case BridgeState::Stopped:
            return "Stopped";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// TelemetryChannel
// ---------------------------------------------------------------------------

void TelemetryChannel::push(TelemetryMessage msg)
{
    std::lock_guard lock(mutex);
    if (buffer.size() < capacity)
    {
        buffer.push_back(std::move(msg));
        write_pos = buffer.size();
    }
    else
    {
        size_t idx  = write_pos % capacity;
        buffer[idx] = std::move(msg);
        write_pos++;
    }
    count++;
}

std::vector<TelemetryMessage> TelemetryChannel::snapshot() const
{
    std::lock_guard lock(mutex);
    if (buffer.size() < capacity)
        return buffer;

    // Reconstruct in order.
    std::vector<TelemetryMessage> result;
    result.reserve(capacity);
    size_t start = write_pos % capacity;
    for (size_t i = 0; i < capacity; ++i)
        result.push_back(buffer[(start + i) % capacity]);
    return result;
}

TelemetryMessage TelemetryChannel::latest() const
{
    std::lock_guard lock(mutex);
    if (buffer.empty())
        return {};
    if (buffer.size() < capacity)
        return buffer.back();
    return buffer[(write_pos - 1) % capacity];
}

// ---------------------------------------------------------------------------
// Px4Bridge — construction / destruction
// ---------------------------------------------------------------------------

Px4Bridge::Px4Bridge() = default;

Px4Bridge::~Px4Bridge()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool Px4Bridge::init(const std::string& host, uint16_t port)
{
    if (state() != BridgeState::Disconnected && state() != BridgeState::Stopped)
    {
        last_error_ = "Px4Bridge: already initialised";
        return false;
    }

    host_ = host;
    port_ = port;
    set_state(BridgeState::Disconnected);
    return true;
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool Px4Bridge::start()
{
    if (state() != BridgeState::Disconnected && state() != BridgeState::Stopped)
    {
        last_error_ =
            "Px4Bridge: cannot start from state " + std::string(bridge_state_name(state()));
        return false;
    }

    set_state(BridgeState::Connecting);

#if !defined(_WIN32)
    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0)
    {
        last_error_ = "Px4Bridge: socket creation failed";
        set_state(BridgeState::Disconnected);
        return false;
    }

    // Set receive timeout so the thread can check stop_requested_ periodically.
    struct timeval tv
    {
    };
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;   // 100ms
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr
    {
    };
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        last_error_ = "Px4Bridge: bind failed on port " + std::to_string(port_);
        ::close(socket_fd_);
        socket_fd_ = -1;
        set_state(BridgeState::Disconnected);
        return false;
    }
#else
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    socket_fd_ = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (socket_fd_ < 0)
    {
        last_error_ = "Px4Bridge: socket creation failed";
        set_state(BridgeState::Disconnected);
        return false;
    }

    DWORD timeout_ms = 100;
    setsockopt(socket_fd_,
               SOL_SOCKET,
               SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout_ms),
               sizeof(timeout_ms));

    struct sockaddr_in addr
    {
    };
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        last_error_ = "Px4Bridge: bind failed on port " + std::to_string(port_);
        closesocket(socket_fd_);
        socket_fd_ = -1;
        set_state(BridgeState::Disconnected);
        return false;
    }
#endif

    set_state(BridgeState::Connected);
    stop_requested_.store(false, std::memory_order_relaxed);
    recv_thread_ = std::thread(&Px4Bridge::receive_thread_func, this);

    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void Px4Bridge::shutdown()
{
    if (state() == BridgeState::Disconnected || state() == BridgeState::Stopped)
        return;

    set_state(BridgeState::ShuttingDown);
    stop_requested_.store(true, std::memory_order_relaxed);

    if (recv_thread_.joinable())
        recv_thread_.join();

#if !defined(_WIN32)
    if (socket_fd_ >= 0)
    {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
#else
    if (socket_fd_ >= 0)
    {
        closesocket(socket_fd_);
        socket_fd_ = -1;
    }
    WSACleanup();
#endif

    set_state(BridgeState::Stopped);
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<std::string> Px4Bridge::channel_names() const
{
    std::lock_guard          lock(channels_mutex_);
    std::vector<std::string> names;
    names.reserve(channels_.size());
    for (auto& [name, ch] : channels_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<TelemetryMessage> Px4Bridge::channel_snapshot(const std::string& name) const
{
    std::lock_guard lock(channels_mutex_);
    auto            it = channels_.find(name);
    if (it == channels_.end())
        return {};
    return it->second->snapshot();
}

std::optional<TelemetryMessage> Px4Bridge::channel_latest(const std::string& name) const
{
    std::lock_guard lock(channels_mutex_);
    auto            it = channels_.find(name);
    if (it == channels_.end())
        return std::nullopt;
    auto msg = it->second->latest();
    if (msg.name.empty())
        return std::nullopt;
    return msg;
}

// ---------------------------------------------------------------------------
// receive_thread_func
// ---------------------------------------------------------------------------

void Px4Bridge::receive_thread_func()
{
    set_state(BridgeState::Receiving);

    uint8_t  recv_buf[2048];
    auto     last_rate_time = std::chrono::steady_clock::now();
    uint64_t rate_counter   = 0;

    while (!stop_requested_.load(std::memory_order_relaxed))
    {
        int n = static_cast<int>(::recvfrom(socket_fd_,
                                            reinterpret_cast<char*>(recv_buf),
                                            sizeof(recv_buf),
                                            0,
                                            nullptr,
                                            nullptr));
        if (n <= 0)
            continue;   // timeout or error, retry

        // Scan buffer for MAVLink frames.
        size_t offset = 0;
        while (offset < static_cast<size_t>(n))
        {
            // Find start of MAVLink frame.
            if (recv_buf[offset] != MAVLINK_STX_V1 && recv_buf[offset] != MAVLINK_STX_V2)
            {
                offset++;
                continue;
            }

            TelemetryMessage msg;
            if (decode_mavlink_message(recv_buf + offset, static_cast<size_t>(n) - offset, msg))
            {
                auto& ch = get_channel(msg.name);
                ch.push(std::move(msg));
                total_msg_count_.fetch_add(1, std::memory_order_relaxed);
                rate_counter++;
            }

            // Advance past this frame (min header size).
            offset += (recv_buf[offset] == MAVLINK_STX_V1) ? 8 : 12;
        }

        // Update message rate every second.
        auto now = std::chrono::steady_clock::now();
        auto dt  = std::chrono::duration<double>(now - last_rate_time).count();
        if (dt >= 1.0)
        {
            msg_rate_.store(static_cast<double>(rate_counter) / dt, std::memory_order_relaxed);
            rate_counter   = 0;
            last_rate_time = now;
        }
    }
}

// ---------------------------------------------------------------------------
// decode_mavlink_message
// ---------------------------------------------------------------------------

bool Px4Bridge::decode_mavlink_message(const uint8_t* data, size_t len, TelemetryMessage& out)
{
    if (len < 8)
        return false;

    bool           v2          = (data[0] == MAVLINK_STX_V2);
    uint8_t        payload_len = 0;
    uint32_t       msg_id      = 0;
    const uint8_t* payload     = nullptr;

    if (v2)
    {
        if (len < 12)
            return false;
        payload_len = data[1];
        // data[2] = incompat_flags, data[3] = compat_flags, data[4] = seq
        // data[5] = sysid, data[6] = compid
        msg_id = static_cast<uint32_t>(data[7]) | (static_cast<uint32_t>(data[8]) << 8)
                 | (static_cast<uint32_t>(data[9]) << 16);
        payload = data + 10;

        if (static_cast<size_t>(10) + payload_len + 2u > len)   // payload + CRC
            return false;
    }
    else
    {
        payload_len = data[1];
        // data[2] = seq, data[3] = sysid, data[4] = compid
        msg_id  = data[5];
        payload = data + 6;

        if (static_cast<size_t>(6) + payload_len + 2u > len)   // payload + CRC
            return false;
    }

    out.msg_id = msg_id;
    out.timestamp_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());

    // Decode known message types into named fields.
    switch (msg_id)
    {
        case MAVLINK_MSG_HEARTBEAT:
            out.name = "HEARTBEAT";
            if (payload_len >= 9)
            {
                out.fields.push_back({"custom_mode", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"type", static_cast<double>(payload[4])});
                out.fields.push_back({"autopilot", static_cast<double>(payload[5])});
                out.fields.push_back({"base_mode", static_cast<double>(payload[6])});
                out.fields.push_back({"system_status", static_cast<double>(payload[7])});
                out.fields.push_back({"mavlink_version", static_cast<double>(payload[8])});
            }
            break;

        case MAVLINK_MSG_ATTITUDE:
            out.name = "ATTITUDE";
            if (payload_len >= 28)
            {
                out.fields.push_back({"time_boot_ms", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"roll", static_cast<double>(read_f32_le(payload + 4))});
                out.fields.push_back({"pitch", static_cast<double>(read_f32_le(payload + 8))});
                out.fields.push_back({"yaw", static_cast<double>(read_f32_le(payload + 12))});
                out.fields.push_back({"rollspeed", static_cast<double>(read_f32_le(payload + 16))});
                out.fields.push_back(
                    {"pitchspeed", static_cast<double>(read_f32_le(payload + 20))});
                out.fields.push_back({"yawspeed", static_cast<double>(read_f32_le(payload + 24))});
            }
            break;

        case MAVLINK_MSG_LOCAL_POSITION_NED:
            out.name = "LOCAL_POSITION_NED";
            if (payload_len >= 28)
            {
                out.fields.push_back({"time_boot_ms", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"x", static_cast<double>(read_f32_le(payload + 4))});
                out.fields.push_back({"y", static_cast<double>(read_f32_le(payload + 8))});
                out.fields.push_back({"z", static_cast<double>(read_f32_le(payload + 12))});
                out.fields.push_back({"vx", static_cast<double>(read_f32_le(payload + 16))});
                out.fields.push_back({"vy", static_cast<double>(read_f32_le(payload + 20))});
                out.fields.push_back({"vz", static_cast<double>(read_f32_le(payload + 24))});
            }
            break;

        case MAVLINK_MSG_GLOBAL_POSITION_INT:
            out.name = "GLOBAL_POSITION_INT";
            if (payload_len >= 28)
            {
                out.fields.push_back({"time_boot_ms", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"lat", static_cast<double>(read_i32_le(payload + 4)) * 1e-7});
                out.fields.push_back({"lon", static_cast<double>(read_i32_le(payload + 8)) * 1e-7});
                out.fields.push_back(
                    {"alt", static_cast<double>(read_i32_le(payload + 12)) * 1e-3});
                out.fields.push_back(
                    {"relative_alt", static_cast<double>(read_i32_le(payload + 16)) * 1e-3});
                out.fields.push_back({"vx", static_cast<double>(read_i16_le(payload + 20)) * 0.01});
                out.fields.push_back({"vy", static_cast<double>(read_i16_le(payload + 22)) * 0.01});
                out.fields.push_back({"vz", static_cast<double>(read_i16_le(payload + 24)) * 0.01});
                out.fields.push_back(
                    {"hdg", static_cast<double>(read_u16_le(payload + 26)) * 0.01});
            }
            break;

        case MAVLINK_MSG_GPS_RAW_INT:
            out.name = "GPS_RAW_INT";
            if (payload_len >= 30)
            {
                out.fields.push_back({"time_usec", static_cast<double>(read_u64_le(payload))});
                out.fields.push_back({"lat", static_cast<double>(read_i32_le(payload + 8)) * 1e-7});
                out.fields.push_back(
                    {"lon", static_cast<double>(read_i32_le(payload + 12)) * 1e-7});
                out.fields.push_back(
                    {"alt", static_cast<double>(read_i32_le(payload + 16)) * 1e-3});
                out.fields.push_back({"eph", static_cast<double>(read_u16_le(payload + 20))});
                out.fields.push_back({"epv", static_cast<double>(read_u16_le(payload + 22))});
                out.fields.push_back({"vel", static_cast<double>(read_u16_le(payload + 24))});
                out.fields.push_back({"cog", static_cast<double>(read_u16_le(payload + 26))});
                out.fields.push_back({"fix_type", static_cast<double>(payload[28])});
                out.fields.push_back({"satellites_visible", static_cast<double>(payload[29])});
            }
            break;

        case MAVLINK_MSG_HIGHRES_IMU:
            out.name = "HIGHRES_IMU";
            if (payload_len >= 62)
            {
                out.fields.push_back({"time_usec", static_cast<double>(read_u64_le(payload))});
                out.fields.push_back({"xacc", static_cast<double>(read_f32_le(payload + 8))});
                out.fields.push_back({"yacc", static_cast<double>(read_f32_le(payload + 12))});
                out.fields.push_back({"zacc", static_cast<double>(read_f32_le(payload + 16))});
                out.fields.push_back({"xgyro", static_cast<double>(read_f32_le(payload + 20))});
                out.fields.push_back({"ygyro", static_cast<double>(read_f32_le(payload + 24))});
                out.fields.push_back({"zgyro", static_cast<double>(read_f32_le(payload + 28))});
                out.fields.push_back({"xmag", static_cast<double>(read_f32_le(payload + 32))});
                out.fields.push_back({"ymag", static_cast<double>(read_f32_le(payload + 36))});
                out.fields.push_back({"zmag", static_cast<double>(read_f32_le(payload + 40))});
                out.fields.push_back(
                    {"abs_pressure", static_cast<double>(read_f32_le(payload + 44))});
                out.fields.push_back(
                    {"diff_pressure", static_cast<double>(read_f32_le(payload + 48))});
                out.fields.push_back(
                    {"pressure_alt", static_cast<double>(read_f32_le(payload + 52))});
                out.fields.push_back(
                    {"temperature", static_cast<double>(read_f32_le(payload + 56))});
            }
            break;

        case MAVLINK_MSG_VFR_HUD:
            out.name = "VFR_HUD";
            if (payload_len >= 20)
            {
                out.fields.push_back({"airspeed", static_cast<double>(read_f32_le(payload))});
                out.fields.push_back(
                    {"groundspeed", static_cast<double>(read_f32_le(payload + 4))});
                out.fields.push_back({"alt", static_cast<double>(read_f32_le(payload + 8))});
                out.fields.push_back({"climb", static_cast<double>(read_f32_le(payload + 12))});
                out.fields.push_back({"heading", static_cast<double>(read_i16_le(payload + 16))});
                out.fields.push_back({"throttle", static_cast<double>(read_u16_le(payload + 18))});
            }
            break;

        case MAVLINK_MSG_SYS_STATUS:
            out.name = "SYS_STATUS";
            if (payload_len >= 31)
            {
                out.fields.push_back(
                    {"voltage_battery", static_cast<double>(read_u16_le(payload + 12)) * 0.001});
                out.fields.push_back(
                    {"current_battery", static_cast<double>(read_i16_le(payload + 14)) * 0.01});
                out.fields.push_back({"battery_remaining", static_cast<double>(payload[30])});
                out.fields.push_back({"load", static_cast<double>(read_u16_le(payload + 10))});
            }
            break;

        case MAVLINK_MSG_SCALED_IMU:
            out.name = "SCALED_IMU";
            if (payload_len >= 22)
            {
                out.fields.push_back({"time_boot_ms", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back(
                    {"xacc", static_cast<double>(read_i16_le(payload + 4)) * 0.001});
                out.fields.push_back(
                    {"yacc", static_cast<double>(read_i16_le(payload + 6)) * 0.001});
                out.fields.push_back(
                    {"zacc", static_cast<double>(read_i16_le(payload + 8)) * 0.001});
                out.fields.push_back(
                    {"xgyro", static_cast<double>(read_i16_le(payload + 10)) * 0.001});
                out.fields.push_back(
                    {"ygyro", static_cast<double>(read_i16_le(payload + 12)) * 0.001});
                out.fields.push_back(
                    {"zgyro", static_cast<double>(read_i16_le(payload + 14)) * 0.001});
                out.fields.push_back(
                    {"xmag", static_cast<double>(read_i16_le(payload + 16)) * 0.001});
                out.fields.push_back(
                    {"ymag", static_cast<double>(read_i16_le(payload + 18)) * 0.001});
                out.fields.push_back(
                    {"zmag", static_cast<double>(read_i16_le(payload + 20)) * 0.001});
            }
            break;

        case MAVLINK_MSG_RC_CHANNELS:
            out.name = "RC_CHANNELS";
            if (payload_len >= 42)
            {
                out.fields.push_back({"time_boot_ms", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"chancount", static_cast<double>(payload[4])});
                for (int i = 0; i < 18 && (5 + i * 2 + 2) <= payload_len; ++i)
                {
                    out.fields.push_back({"chan" + std::to_string(i + 1) + "_raw",
                                          static_cast<double>(read_u16_le(payload + 5 + i * 2))});
                }
            }
            break;

        case MAVLINK_MSG_SERVO_OUTPUT_RAW:
            out.name = "SERVO_OUTPUT_RAW";
            if (payload_len >= 21)
            {
                out.fields.push_back({"time_usec", static_cast<double>(read_u32_le(payload))});
                out.fields.push_back({"port", static_cast<double>(payload[4])});
                for (int i = 0; i < 8 && (5 + i * 2 + 2) <= payload_len; ++i)
                {
                    out.fields.push_back({"servo" + std::to_string(i + 1) + "_raw",
                                          static_cast<double>(read_u16_le(payload + 5 + i * 2))});
                }
            }
            break;

        default:
            // Unknown message — record as raw with ID.
            out.name = "MSG_" + std::to_string(msg_id);
            break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// get_channel
// ---------------------------------------------------------------------------

TelemetryChannel& Px4Bridge::get_channel(const std::string& name)
{
    std::lock_guard lock(channels_mutex_);
    auto            it = channels_.find(name);
    if (it != channels_.end())
        return *it->second;

    auto ch         = std::make_unique<TelemetryChannel>();
    ch->name        = name;
    auto& ref       = *ch;
    channels_[name] = std::move(ch);
    return ref;
}

// ---------------------------------------------------------------------------
// set_state
// ---------------------------------------------------------------------------

void Px4Bridge::set_state(BridgeState s)
{
    state_.store(s, std::memory_order_release);
    if (state_cb_)
        state_cb_(s);
}

}   // namespace spectra::adapters::px4
