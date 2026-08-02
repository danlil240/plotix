#include "diagnostics_panel.hpp"

#include <chrono>
#include <cstring>
#include <format>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <cmath>
    #include "ui/shell/shell_style.hpp"
#endif

#ifdef SPECTRA_USE_ROS2
    #include <diagnostic_msgs/msg/diagnostic_array.hpp>
    #include <rclcpp/serialized_message.hpp>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t wall_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// diag_level_name
// ---------------------------------------------------------------------------

const char* diag_level_name(DiagLevel l)
{
    switch (l)
    {
        case DiagLevel::OK:
            return "OK";
        case DiagLevel::Warn:
            return "WARN";
        case DiagLevel::Error:
            return "ERROR";
        case DiagLevel::Stale:
            return "STALE";
    }
    return "STALE";
}

// ---------------------------------------------------------------------------
// DiagComponent::update
// ---------------------------------------------------------------------------

bool DiagComponent::update(const DiagStatus& s)
{
    const DiagLevel prev = level;

    level          = s.level;
    message        = s.message;
    hardware_id    = s.hardware_id;
    values         = s.values;
    last_update_ns = s.arrival_ns;
    name           = s.name;

    // Append to sparkline history.
    if (history.size() >= MAX_SPARK)
        history.pop_front();
    history.push_back({s.level, s.arrival_ns});

    const bool changed = (prev != level);
    if (changed)
    {
        ++transition_count;
        if (level == DiagLevel::Warn || level == DiagLevel::Error)
            ever_alerted = true;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// DiagnosticsModel
// ---------------------------------------------------------------------------

std::string DiagnosticsModel::apply(const DiagStatus& s)
{
    auto it = components.find(s.name);
    if (it == components.end())
    {
        DiagComponent comp;
        comp.name          = s.name;
        components[s.name] = std::move(comp);
        order.push_back(s.name);
        it = components.find(s.name);
    }

    ++total_messages;
    const bool transitioned = it->second.update(s);
    return transitioned ? s.name : std::string{};
}

void DiagnosticsModel::recount()
{
    count_ok    = 0;
    count_warn  = 0;
    count_error = 0;
    count_stale = 0;
    for (const auto& [name, comp] : components)
    {
        switch (comp.level)
        {
            case DiagLevel::OK:
                ++count_ok;
                break;
            case DiagLevel::Warn:
                ++count_warn;
                break;
            case DiagLevel::Error:
                ++count_error;
                break;
            case DiagLevel::Stale:
                ++count_stale;
                break;
        }
    }
}

void DiagnosticsModel::prune_stale(int64_t now_ns, int64_t stale_ns)
{
    bool changed = false;
    for (auto it = components.begin(); it != components.end();)
    {
        if (it->second.last_update_ns > 0 && now_ns - it->second.last_update_ns > stale_ns)
        {
            it->second.level   = DiagLevel::Stale;
            it->second.message = "(stale — no update)";
            changed            = true;
        }
        ++it;
    }
    if (changed)
        recount();
}

// ---------------------------------------------------------------------------
// DiagnosticsPanel — constructor / destructor
// ---------------------------------------------------------------------------

DiagnosticsPanel::DiagnosticsPanel() = default;

DiagnosticsPanel::~DiagnosticsPanel()
{
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool DiagnosticsPanel::start()
{
    if (running_.load(std::memory_order_acquire))
        return true;

#ifdef SPECTRA_USE_ROS2
    if (!node_)
        return false;

    // Allocate ring buffer (power-of-two).
    size_t cap = 1;
    while (cap < ring_depth_)
        cap <<= 1;
    ring_.resize(cap);
    ring_mask_ = cap - 1;
    ring_head_.store(0, std::memory_order_relaxed);
    ring_tail_.store(0, std::memory_order_relaxed);

    try
    {
        subscription_ = node_->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
            topic_,
            rclcpp::QoS(10),
            [this](diagnostic_msgs::msg::DiagnosticArray::SharedPtr msg)
            {
                if (!msg)
                    return;

                const int64_t           arrival_ns = wall_ns();
                std::vector<DiagStatus> statuses;
                statuses.reserve(msg->status.size());
                for (const auto& status : msg->status)
                {
                    DiagStatus parsed;
                    parsed.level       = static_cast<DiagLevel>(status.level);
                    parsed.name        = status.name;
                    parsed.message     = status.message;
                    parsed.hardware_id = status.hardware_id;
                    parsed.arrival_ns  = arrival_ns;
                    parsed.values.reserve(status.values.size());
                    for (const auto& value : status.values)
                        parsed.values.push_back({value.key, value.value});
                    statuses.push_back(std::move(parsed));
                }

                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_status_batches_.push_back(std::move(statuses));
            });
    }
    catch (const std::exception&)
    {
        return false;
    }

    running_.store(true, std::memory_order_release);
    return true;
#else
    // Without ROS2, start() can still be used in inject_* test mode.
    size_t cap = 1;
    while (cap < ring_depth_)
        cap <<= 1;
    ring_.resize(cap);
    ring_mask_ = cap - 1;
    ring_head_.store(0, std::memory_order_relaxed);
    ring_tail_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    return true;
#endif
}

void DiagnosticsPanel::stop()
{
    if (!running_.load(std::memory_order_acquire))
        return;

#ifdef SPECTRA_USE_ROS2
    subscription_.reset();
#endif

    running_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------------------

void DiagnosticsPanel::ring_push(DiagRawMessage msg)
{
    if (ring_.empty())
        return;

    const size_t head = ring_head_.load(std::memory_order_relaxed);
    const size_t next = (head + 1) & ring_mask_;
    // If full (next == tail), we drop oldest by advancing tail.
    if (next == ring_tail_.load(std::memory_order_acquire))
    {
        // Drop oldest: advance tail.
        ring_tail_.fetch_add(1, std::memory_order_release);
        // Adjust to mask.
        ring_tail_.store(ring_tail_.load() & ring_mask_, std::memory_order_release);
    }
    ring_[head & ring_mask_] = std::move(msg);
    ring_head_.store((head + 1) & ring_mask_, std::memory_order_release);
}

bool DiagnosticsPanel::ring_pop(DiagRawMessage& out)
{
    if (ring_.empty())
        return false;

    const size_t tail = ring_tail_.load(std::memory_order_relaxed);
    if (tail == ring_head_.load(std::memory_order_acquire))
        return false;

    out = std::move(ring_[tail]);
    ring_tail_.store((tail + 1) & ring_mask_, std::memory_order_release);
    return true;
}

size_t DiagnosticsPanel::pending_raw() const
{
    if (ring_.empty())
        return 0;
    const size_t h = ring_head_.load(std::memory_order_acquire);
    const size_t t = ring_tail_.load(std::memory_order_acquire);
    if (h >= t)
        return h - t;
    return ring_.size() - t + h;
}

// ---------------------------------------------------------------------------
// CDR parsing helpers
// ---------------------------------------------------------------------------

uint32_t DiagnosticsPanel::read_u32(const uint8_t* buf, size_t len, size_t& offset)
{
    if (offset + 4 > len)
    {
        offset = len + 1;   // signal underflow
        return 0;
    }
    uint32_t v = 0;
    std::memcpy(&v, buf + offset, 4);
    offset += 4;
    // CDR is LE on x86/ARM; if needed a byte-swap would go here.
    return v;
}

std::string DiagnosticsPanel::read_string(const uint8_t* buf, size_t len, size_t& offset)
{
    const uint32_t slen = read_u32(buf, len, offset);
    if (offset > len)
        return {};
    if (slen == 0)
        return {};
    if (offset + slen > len)
    {
        offset = len + 1;
        return {};
    }

    std::string s(reinterpret_cast<const char*>(buf + offset), slen);
    offset += slen;
    // CDR strings include a null terminator in the length — strip it.
    if (!s.empty() && s.back() == '\0')
        s.pop_back();
    // CDR: pad to 4-byte alignment after string data.
    while (offset % 4 != 0 && offset < len)
        ++offset;
    return s;
}

bool DiagnosticsPanel::read_diag_status(const uint8_t* buf,
                                        size_t         len,
                                        size_t&        offset,
                                        DiagStatus&    out,
                                        int64_t        arrival_ns)
{
    // byte level (but CDR pads to 4 bytes for the sequence element start)
    if (offset + 1 > len)
        return false;
    out.level = static_cast<DiagLevel>(buf[offset]);
    offset += 1;
    // padding: skip 1 byte to align to 2, then name string starts (CDR 4-byte aligned)
    // CDR: level is byte[1] then name is string[4-aligned from message start].
    // Alignment within a sequence element is from the start of the element.
    // byte → 3 bytes pad → string (4-byte aligned from element start).
    while (offset % 4 != 0 && offset < len)
        ++offset;

    out.name = read_string(buf, len, offset);
    if (offset > len)
        return false;
    out.message = read_string(buf, len, offset);
    if (offset > len)
        return false;
    out.hardware_id = read_string(buf, len, offset);
    if (offset > len)
        return false;

    // values[] sequence
    const uint32_t nkv = read_u32(buf, len, offset);
    if (offset > len)
        return false;
    out.values.reserve(nkv);
    for (uint32_t i = 0; i < nkv; ++i)
    {
        DiagKeyValue kv;
        kv.key = read_string(buf, len, offset);
        if (offset > len)
            return false;
        kv.value = read_string(buf, len, offset);
        if (offset > len)
            return false;
        out.values.push_back(std::move(kv));
    }

    out.arrival_ns = arrival_ns;
    return true;
}

// ---------------------------------------------------------------------------
// parse_diag_array — static, fully unit-testable
// ---------------------------------------------------------------------------

std::vector<DiagStatus> DiagnosticsPanel::parse_diag_array(const uint8_t* data,
                                                           size_t         len,
                                                           int64_t        arrival_ns)
{
    std::vector<DiagStatus> result;
    if (!data || len < 4)
        return result;

    size_t offset = 0;

    // CDR encapsulation header: 4 bytes (0x00 0x01 0x00 0x00 for LE).
    // Skip it.
    offset += 4;

    // std_msgs/Header:
    //   uint32 seq (ROS1 only — in ROS2 /Humble it's stamp + frame_id)
    //   builtin_interfaces/Time stamp  (int32 sec, uint32 nanosec) = 8 bytes
    //   string frame_id

    // stamp: sec (4) + nanosec (4) = 8 bytes
    if (offset + 8 > len)
        return result;
    offset += 8;   // skip stamp

    // frame_id string
    read_string(data, len, offset);
    if (offset > len)
        return result;

    // status[] sequence count
    const uint32_t n = read_u32(data, len, offset);
    if (offset > len)
        return result;

    result.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        DiagStatus s;
        if (!read_diag_status(data, len, offset, s, arrival_ns))
            break;
        result.push_back(std::move(s));
    }

    return result;
}

// ---------------------------------------------------------------------------
// poll — drain ring, parse, update model
// ---------------------------------------------------------------------------

void DiagnosticsPanel::poll()
{
    const int64_t now = wall_ns();

    std::vector<std::vector<DiagStatus>> pending_batches;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_batches.swap(pending_status_batches_);
    }

    for (auto& batch : pending_batches)
    {
        for (auto& s : batch)
        {
            const std::string transitioned = model_.apply(s);
            if (!transitioned.empty() && alert_cb_)
            {
                const auto& comp = model_.components.at(transitioned);
                if (comp.level == DiagLevel::Warn || comp.level == DiagLevel::Error)
                {
                    alert_cb_(transitioned, comp.level);
                }
            }
        }
    }

    // Drain ring buffer.
    DiagRawMessage raw;
    while (ring_pop(raw))
    {
        auto statuses = parse_diag_array(raw.data.data(), raw.data.size(), raw.arrival_ns);
        for (auto& s : statuses)
        {
            const std::string transitioned = model_.apply(s);
            if (!transitioned.empty() && alert_cb_)
            {
                const auto& comp = model_.components.at(transitioned);
                if (comp.level == DiagLevel::Warn || comp.level == DiagLevel::Error)
                {
                    alert_cb_(transitioned, comp.level);
                }
            }
        }
    }

    // Mark stale components.
    const auto stale_ns = static_cast<int64_t>(stale_threshold_s_ * 1'000'000'000LL);
    model_.prune_stale(now, stale_ns);

    // Recount badges.
    model_.recount();
}

// ---------------------------------------------------------------------------
// inject helpers (testing)
// ---------------------------------------------------------------------------

void DiagnosticsPanel::inject_status(const DiagStatus& s)
{
    const std::string transitioned = model_.apply(s);
    if (!transitioned.empty() && alert_cb_)
    {
        const auto& comp = model_.components.at(transitioned);
        if (comp.level == DiagLevel::Warn || comp.level == DiagLevel::Error)
            alert_cb_(transitioned, comp.level);
    }
    model_.recount();
}

void DiagnosticsPanel::inject_array(const std::vector<DiagStatus>& statuses, int64_t arrival_ns)
{
    const int64_t ts = (arrival_ns != 0) ? arrival_ns : wall_ns();
    for (const auto& s : statuses)
    {
        DiagStatus s2 = s;
        if (s2.arrival_ns == 0)
            s2.arrival_ns = ts;
        inject_status(s2);
    }
}

// ---------------------------------------------------------------------------
// level_color / level_short
// ---------------------------------------------------------------------------

void DiagnosticsPanel::level_color(DiagLevel l, float& r, float& g, float& b, float& a)
{
    a = 1.0f;
    switch (l)
    {
        case DiagLevel::OK:
            r = 0.20f;
            g = 0.80f;
            b = 0.30f;
            break;
        case DiagLevel::Warn:
            r = 0.95f;
            g = 0.75f;
            b = 0.10f;
            break;
        case DiagLevel::Error:
            r = 0.90f;
            g = 0.20f;
            b = 0.20f;
            break;
        case DiagLevel::Stale:
        default:
            r = 0.50f;
            g = 0.50f;
            b = 0.55f;
            break;
    }
}

const char* DiagnosticsPanel::level_short(DiagLevel l)
{
    switch (l)
    {
        case DiagLevel::OK:
            return "OK";
        case DiagLevel::Warn:
            return "WARN";
        case DiagLevel::Error:
            return "ERR";
        case DiagLevel::Stale:
            return "STALE";
    }
    return "STALE";
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void DiagnosticsPanel::draw(bool* p_open)
{
    ImGui::SetNextWindowSize(ImVec2(680.0f, 480.0f), ImGuiCond_FirstUseEver);

    if (!spectra::ui::shell::begin_panel(title_.c_str(), p_open))
    {
        spectra::ui::shell::end_panel();
        return;
    }

    draw_summary_bar();
    ImGui::Separator();
    draw_filter_bar();
    ImGui::Separator();
    draw_component_table();

    spectra::ui::shell::end_panel();
}

void DiagnosticsPanel::draw_summary_bar()
{
    // Coloured badge: [OK: N] [WARN: N] [ERR: N] [STALE: N]
    const struct
    {
        DiagLevel lvl;
        int       count;
        bool*     show;
    } badges[] = {
        {DiagLevel::OK, model_.count_ok, &show_ok_},
        {DiagLevel::Warn, model_.count_warn, &show_warn_},
        {DiagLevel::Error, model_.count_error, &show_error_},
        {DiagLevel::Stale, model_.count_stale, &show_stale_},
    };

    for (const auto& b : badges)
    {
        float r  = NAN;
        float g  = NAN;
        float bl = NAN;
        float a  = NAN;
        level_color(b.lvl, r, g, bl, a);

        // Dim the badge if the filter is off.
        const float  alpha = *b.show ? 1.0f : 0.35f;
        const ImVec4 col{r * alpha, g * alpha, bl * alpha, 1.0f};
        const ImVec4 col_hov{r, g, bl, 1.0f};

        const std::string buf =
            std::format("{}: {}##badge{}", level_short(b.lvl), b.count, static_cast<int>(b.lvl));

        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::SmallButton(buf.c_str()))
            *b.show = !(*b.show);

        ImGui::PopStyleColor(4);
        ImGui::SameLine(0.0f, 6.0f);
    }

    // Total messages right-aligned.
    const std::string totbuf = std::format("  msgs: {}", model_.total_messages);
    ImGui::TextDisabled("%s", totbuf.c_str());
}

bool DiagnosticsPanel::draw_filter_bar()
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
    bool changed = ImGui::InputText("##diagfilter", filter_buf_, sizeof(filter_buf_));
    if (changed)
        filter_str_ = filter_buf_;

    ImGui::SameLine();
    if (ImGui::SmallButton("Expand All"))
        show_kv_all_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse"))
        show_kv_all_ = false;

    return changed;
}

void DiagnosticsPanel::draw_component_table()
{
    // 4 columns: [status badge] [name] [message] [sparkline]
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    const float avail_h = ImGui::GetContentRegionAvail().y;
    if (!ImGui::BeginTable("##diagtable", 4, flags, ImVec2(0.0f, avail_h)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    for (const auto& name : model_.order)
    {
        auto it = model_.components.find(name);
        if (it == model_.components.end())
            continue;
        DiagComponent& comp = it->second;

        // Level filter.
        switch (comp.level)
        {
            case DiagLevel::OK:
                if (!show_ok_)
                    continue;
                break;
            case DiagLevel::Warn:
                if (!show_warn_)
                    continue;
                break;
            case DiagLevel::Error:
                if (!show_error_)
                    continue;
                break;
            case DiagLevel::Stale:
                if (!show_stale_)
                    continue;
                break;
        }

        // Text filter.
        if (!filter_str_.empty())
        {
            const bool matches = name.find(filter_str_) != std::string::npos
                                 || comp.message.find(filter_str_) != std::string::npos;
            if (!matches)
                continue;
        }

        draw_component_row(comp);
    }

    ImGui::EndTable();
}

void DiagnosticsPanel::draw_component_row(DiagComponent& comp)
{
    float r = NAN;
    float g = NAN;
    float b = NAN;
    float a = NAN;
    level_color(comp.level, r, g, b, a);
    const ImVec4 badge_col{r, g, b, 1.0f};

    ImGui::PushID(comp.name.c_str());

    const bool is_expanded = show_kv_all_ || (expanded_component_ == comp.name);

    // ---- Row: level badge ----
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(badge_col, "%-5s", level_short(comp.level));

    // ---- Name column (selectable to expand) ----
    ImGui::TableSetColumnIndex(1);
    const bool clicked =
        ImGui::Selectable(comp.name.c_str(),
                          is_expanded && !show_kv_all_,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
    if (clicked)
    {
        if (expanded_component_ == comp.name)
            expanded_component_.clear();
        else
            expanded_component_ = comp.name;
    }

    // ---- Message column ----
    ImGui::TableSetColumnIndex(2);
    {
        const int64_t now     = wall_ns();
        const double  since_s = comp.seconds_since_update(now);
        if (comp.level == DiagLevel::Stale && since_s > 0.5)
        {
            // Show "STALE (Xs)" badge in muted orange before the message.
            const std::string stale_buf = since_s < 60.0
                                              ? std::format("STALE ({:.0f}s)", since_s)
                                              : std::format("STALE ({:.0f}min)", since_s / 60.0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
            ImGui::TextUnformatted(stale_buf.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("No update for %.1f seconds", since_s);
            ImGui::SameLine(0.0f, 6.0f);
        }
        ImGui::TextUnformatted(comp.message.c_str());
    }

    // ---- Sparkline column ----
    ImGui::TableSetColumnIndex(3);
    draw_sparkline(comp, 76.0f, 14.0f);

    // ---- Expanded key/value sub-rows ----
    if (is_expanded && !comp.values.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));

        for (const auto& kv : comp.values)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // indent indicator
            ImGui::TextDisabled("  ·");

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("  %s", kv.key.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(kv.value.c_str());
        }

        ImGui::PopStyleColor();
    }

    ImGui::PopID();
}

void DiagnosticsPanel::draw_sparkline(const DiagComponent& comp, float width, float height)
{
    if (comp.history.empty())
    {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));

    const size_t n    = comp.history.size();
    const float  step = (n > 1) ? (width / static_cast<float>(n - 1)) : width;

    for (size_t i = 0; i < n; ++i)
    {
        const DiagSparkEntry& e  = comp.history[i];
        float                 cr = NAN;
        float                 cg = NAN;
        float                 cb = NAN;
        float                 ca = NAN;
        level_color(e.level, cr, cg, cb, ca);

        // Map level to Y: OK=bottom, ERROR=top.
        float y_norm = 0.9f;
        switch (e.level)
        {
            case DiagLevel::OK:
                y_norm = 0.9f;
                break;
            case DiagLevel::Warn:
                y_norm = 0.55f;
                break;
            case DiagLevel::Error:
                y_norm = 0.1f;
                break;
            case DiagLevel::Stale:
                y_norm = 0.75f;
                break;
        }

        const float x   = p0.x + static_cast<float>(i) * step;
        const float y   = p0.y + height * y_norm;
        const ImU32 col = IM_COL32(static_cast<int>(cr * 255),
                                   static_cast<int>(cg * 255),
                                   static_cast<int>(cb * 255),
                                   200);

        // Draw line segment to next point.
        if (i + 1 < n)
        {
            const DiagSparkEntry& ne  = comp.history[i + 1];
            float                 nr  = NAN;
            float                 ng  = NAN;
            float                 nb  = NAN;
            float                 na2 = NAN;
            level_color(ne.level, nr, ng, nb, na2);
            float ny_norm = 0.9f;
            switch (ne.level)
            {
                case DiagLevel::OK:
                    ny_norm = 0.9f;
                    break;
                case DiagLevel::Warn:
                    ny_norm = 0.55f;
                    break;
                case DiagLevel::Error:
                    ny_norm = 0.1f;
                    break;
                case DiagLevel::Stale:
                    ny_norm = 0.75f;
                    break;
            }
            const float nx = p0.x + static_cast<float>(i + 1) * step;
            const float ny = p0.y + height * ny_norm;
            dl->AddLine(ImVec2(x, y), ImVec2(nx, ny), col, 1.5f);
        }

        // Draw circle dot.
        dl->AddCircleFilled(ImVec2(x, y), 2.0f, col);
    }
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
