#include "service_caller.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>

#include <rclcpp/rclcpp.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char* call_state_name(CallState s)
{
    switch (s)
    {
        case CallState::Pending:
            return "Pending";
        case CallState::Done:
            return "Done";
        case CallState::TimedOut:
            return "TimedOut";
        case CallState::Error:
            return "Error";
    }
    return "Unknown";
}

static double wall_time_s_impl()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

static std::pair<std::string, std::string> service_message_type_names(
    const std::string& service_type)
{
    std::pair<std::string, std::string> names;
    names.first  = service_type + "_Request";
    names.second = service_type + "_Response";
    return names;
}

static std::pair<std::string, std::string> legacy_service_message_type_names(
    const std::string& service_type)
{
    auto names = service_message_type_names(service_type);
    auto pos   = names.first.find("/srv/");
    if (pos != std::string::npos)
        names.first.replace(pos, 5, "/msg/");
    pos = names.second.find("/srv/");
    if (pos != std::string::npos)
        names.second.replace(pos, 5, "/msg/");
    return names;
}

// ---------------------------------------------------------------------------
// Minimal JSON helpers (no third-party dependency)
// ---------------------------------------------------------------------------

// Escape a string for JSON output.
static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s)
    {
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
            default:
                out += c;
                break;
        }
    }
    return out;
}

// Extract the value for a top-level key in a flat JSON object.
// Very simple: looks for "key": "value" or "key": value (no nesting support).
static std::string json_get_string(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto        pos    = json.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size())
        return {};
    if (json[pos] == '"')
    {
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                ++pos;
            }
            val += json[pos++];
        }
        return val;
    }
    // Numeric / bool / null value: read until , or } or ]
    std::string val;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
        val += json[pos++];
    // Trim trailing whitespace
    while (!val.empty() && val.back() == ' ')
        val.pop_back();
    return val;
}

// Extract a nested JSON object value for a top-level key.
// Returns the raw JSON substring (including braces/brackets).
static std::string json_get_object(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto        pos    = json.find(needle);
    if (pos == std::string::npos)
        return "{}";
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size() || (json[pos] != '{' && json[pos] != '['))
        return "{}";
    char        open_c  = json[pos];
    char        close_c = (open_c == '{') ? '}' : ']';
    int         depth   = 0;
    std::string result;
    while (pos < json.size())
    {
        char c = json[pos++];
        result += c;
        if (c == open_c)
            ++depth;
        if (c == close_c)
        {
            --depth;
            if (depth == 0)
                break;
        }
    }
    return result;
}

// Build a simple flat JSON object from key-value string pairs.
[[maybe_unused]] static std::string build_json_object(
    const std::vector<std::pair<std::string, std::string>>& kv,
    bool                                                    values_are_strings = true)
{
    std::string out = "{";
    for (std::size_t i = 0; i < kv.size(); ++i)
    {
        if (i > 0)
            out += ", ";
        out += "\"" + json_escape(kv[i].first) + "\": ";
        if (values_are_strings)
            out += "\"" + json_escape(kv[i].second) + "\"";
        else
            out += kv[i].second;
    }
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// ServiceCaller — constructor / destructor
// ---------------------------------------------------------------------------

ServiceCaller::ServiceCaller(rclcpp::Node::SharedPtr node,
                             MessageIntrospector*    introspector,
                             TopicDiscovery*         discovery)
    : node_(std::move(node)), introspector_(introspector), discovery_(discovery)
{
}

ServiceCaller::~ServiceCaller() = default;

// ---------------------------------------------------------------------------
// wall_time_s
// ---------------------------------------------------------------------------

double ServiceCaller::wall_time_s()
{
    return wall_time_s_impl();
}

// ---------------------------------------------------------------------------
// Service discovery
// ---------------------------------------------------------------------------

std::vector<ServiceEntry> ServiceCaller::query_services_from_node() const
{
    std::vector<ServiceEntry> result;
    if (!node_)
        return result;
    auto raw = node_->get_service_names_and_types();
    result.reserve(raw.size());
    for (auto& [name, types] : raw)
    {
        ServiceEntry e;
        e.name      = name;
        e.all_types = types;
        e.type      = types.empty() ? "" : types[0];
        result.push_back(std::move(e));
    }
    return result;
}

ServiceEntry ServiceCaller::entry_from_info(const ServiceInfo& info) const
{
    ServiceEntry e;
    e.name      = info.name;
    e.all_types = info.types;
    e.type      = info.types.empty() ? "" : info.types[0];
    return e;
}

void ServiceCaller::refresh_services()
{
    std::vector<ServiceEntry> fresh;

    if (discovery_)
    {
        auto raw = discovery_->services();
        fresh.reserve(raw.size());
        for (auto& si : raw)
            fresh.push_back(entry_from_info(si));
    }
    // Do NOT fall back to query_services_from_node() — that direct DDS
    // graph call can deadlock with rmw_fastrtps's discovery thread when
    // namespaced participants are present.  Return empty and let the
    // caller retry after TopicDiscovery has refreshed.

    std::lock_guard<std::mutex> lk(services_mutex_);
    // Merge: preserve schema_loaded / schema_ok from existing entries.
    std::unordered_map<std::string, ServiceEntry> existing;
    for (auto& e : services_)
        existing[e.name] = std::move(e);

    services_.clear();
    services_.reserve(fresh.size());
    for (auto& ne : fresh)
    {
        auto it = existing.find(ne.name);
        if (it != existing.end())
        {
            ne.schema_loaded   = it->second.schema_loaded;
            ne.schema_ok       = it->second.schema_ok;
            ne.request_schema  = it->second.request_schema;
            ne.response_schema = it->second.response_schema;
        }
        services_.push_back(std::move(ne));
    }
}

std::vector<ServiceEntry> ServiceCaller::services() const
{
    std::lock_guard<std::mutex> lk(services_mutex_);
    return services_;
}

std::size_t ServiceCaller::service_count() const
{
    std::lock_guard<std::mutex> lk(services_mutex_);
    return services_.size();
}

std::optional<ServiceEntry> ServiceCaller::find_service(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(services_mutex_);
    for (auto& e : services_)
        if (e.name == name)
            return e;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Schema introspection
// ---------------------------------------------------------------------------

bool ServiceCaller::load_schema(const std::string& service_name)
{
    if (!introspector_)
        return false;

    // Find the service entry.
    std::string type_str;
    {
        std::lock_guard<std::mutex> lk(services_mutex_);
        for (auto& e : services_)
        {
            if (e.name == service_name)
            {
                if (e.schema_loaded)
                    return e.schema_ok;
                type_str = e.type;
                break;
            }
        }
    }
    if (type_str.empty())
        return false;

    auto [req_type, resp_type] = service_message_type_names(type_str);
    auto req_schema            = introspector_->introspect(req_type);
    auto resp_schema           = introspector_->introspect(resp_type);

    if (!req_schema || !resp_schema)
    {
        auto [legacy_req_type, legacy_resp_type] = legacy_service_message_type_names(type_str);
        if (!req_schema)
            req_schema = introspector_->introspect(legacy_req_type);
        if (!resp_schema)
            resp_schema = introspector_->introspect(legacy_resp_type);
    }

    std::lock_guard<std::mutex> lk(services_mutex_);
    for (auto& e : services_)
    {
        if (e.name == service_name)
        {
            e.schema_loaded   = true;
            e.schema_ok       = (req_schema != nullptr);
            e.request_schema  = req_schema;
            e.response_schema = resp_schema;
            return e.schema_ok;
        }
    }
    return false;
}

// Build editable field list from a schema (depth-first, recursive helper).
static void collect_fields(const std::vector<FieldDescriptor>& descs,
                           int                                 depth,
                           std::vector<ServiceFieldValue>&     out)
{
    for (auto& fd : descs)
    {
        ServiceFieldValue fv;
        fv.path         = fd.full_path;
        fv.display_name = fd.name;
        fv.type         = fd.type;
        fv.depth        = depth;

        if (fd.type == FieldType::Bool)
            fv.value_str = "false";
        else if (fd.type == FieldType::String || fd.type == FieldType::WString
                 || fd.type == FieldType::Message)
            fv.value_str = "";
        else
            fv.value_str = "0";

        out.push_back(std::move(fv));

        if (!fd.children.empty())
            collect_fields(fd.children, depth + 1, out);
    }
}

std::vector<ServiceFieldValue> ServiceCaller::fields_from_schema(const MessageSchema& schema)
{
    std::vector<ServiceFieldValue> result;
    collect_fields(schema.fields, 0, result);
    return result;
}

// ---------------------------------------------------------------------------
// Call dispatch
// ---------------------------------------------------------------------------

CallHandle ServiceCaller::call(const std::string& service_name,
                               const std::string& request_json,
                               double             timeout_s)
{
    if (!node_)
        return INVALID_CALL_HANDLE;

    std::string svc_type;
    {
        std::lock_guard<std::mutex> lk(services_mutex_);
        for (auto& e : services_)
            if (e.name == service_name)
            {
                svc_type = e.type;
                break;
            }
    }

    auto rec          = std::make_shared<CallRecord>();
    rec->id           = next_id();
    rec->service_name = service_name;
    rec->service_type = svc_type;
    rec->request_json = request_json;
    rec->timeout_s    = timeout_s;
    rec->call_time_s  = wall_time_s();
    rec->state.store(CallState::Pending, std::memory_order_release);

    CallHandle h = rec->id;

    // Store in history before dispatching.
    {
        std::lock_guard<std::mutex> lk(history_mutex_);
        history_.push_back(rec);
        handle_map_[h] = rec;
        // Prune if over max.
        while (history_.size() > max_history_)
        {
            handle_map_.erase(history_.front()->id);
            history_.erase(history_.begin());
        }
    }

    dispatch_call(rec);
    return h;
}

void ServiceCaller::dispatch_call(const std::shared_ptr<CallRecord>& rec)
{
    if (!node_)
    {
        rec->error_message = "No ROS2 node";
        rec->state.store(CallState::Error, std::memory_order_release);
        return;
    }

    // rclcpp::GenericClient / create_generic_client are only available in
    // ROS2 Iron and later.  On Humble, generic service calls are not
    // supported via the public API, so we report an informative error
    // instead of a compile-time failure.
    rec->error_message = "Generic service calls require ROS2 Iron or later. "
                         "Service: "
                         + rec->service_name + " [" + rec->service_type + "]";
    rec->state.store(CallState::Error, std::memory_order_release);

    CallDoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        cb = done_cb_;
    }
    if (cb)
        cb(rec->id, *rec);
}

// ---------------------------------------------------------------------------
// Result polling
// ---------------------------------------------------------------------------

const CallRecord* ServiceCaller::record(CallHandle h) const
{
    std::lock_guard<std::mutex> lk(history_mutex_);
    auto                        it = handle_map_.find(h);
    if (it == handle_map_.end())
        return nullptr;
    return it->second.get();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

std::vector<std::shared_ptr<CallRecord>> ServiceCaller::history() const
{
    std::lock_guard<std::mutex> lk(history_mutex_);
    return history_;
}

std::size_t ServiceCaller::history_count() const
{
    std::lock_guard<std::mutex> lk(history_mutex_);
    return history_.size();
}

void ServiceCaller::clear_history()
{
    std::lock_guard<std::mutex> lk(history_mutex_);
    history_.clear();
    handle_map_.clear();
}

void ServiceCaller::prune_history(std::size_t max_n)
{
    std::lock_guard<std::mutex> lk(history_mutex_);
    while (history_.size() > max_n)
    {
        handle_map_.erase(history_.front()->id);
        history_.erase(history_.begin());
    }
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void ServiceCaller::set_call_done_callback(CallDoneCallback cb)
{
    std::lock_guard<std::mutex> lk(cb_mutex_);
    done_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// JSON import / export
// ---------------------------------------------------------------------------

std::string ServiceCaller::record_to_json(const CallRecord& rec)
{
    std::string        state_str = call_state_name(rec.state.load(std::memory_order_acquire));
    std::ostringstream oss;
    oss << "{" << "\"id\": " << rec.id << ", " << R"("service": ")" << json_escape(rec.service_name)
        << "\", " << R"("type": ")" << json_escape(rec.service_type) << "\", "
        << "\"request\": " << (rec.request_json.empty() ? "{}" : rec.request_json) << ", "
        << "\"response\": " << (rec.response_json.empty() ? "{}" : rec.response_json) << ", "
        << R"("state": ")" << json_escape(state_str) << "\", "
        << "\"latency_ms\": " << rec.latency_ms << ", " << "\"call_time\": " << rec.call_time_s
        << ", " << R"("error": ")" << json_escape(rec.error_message) << "\"" << "}";
    return oss.str();
}

bool ServiceCaller::record_from_json(const std::string& json, CallRecord& out)
{
    std::string service = json_get_string(json, "service");
    if (service.empty())
        return false;
    out.service_name  = service;
    out.service_type  = json_get_string(json, "type");
    out.request_json  = json_get_object(json, "request");
    out.response_json = json_get_object(json, "response");
    out.error_message = json_get_string(json, "error");

    std::string state_str = json_get_string(json, "state");
    if (state_str == "Done")
        out.state.store(CallState::Done);
    else if (state_str == "TimedOut")
        out.state.store(CallState::TimedOut);
    else if (state_str == "Error")
        out.state.store(CallState::Error);
    else
        out.state.store(CallState::Pending);

    std::string lat = json_get_string(json, "latency_ms");
    if (!lat.empty())
        out.latency_ms = std::stod(lat);
    std::string ct = json_get_string(json, "call_time");
    if (!ct.empty())
        out.call_time_s = std::stod(ct);
    return true;
}

std::string ServiceCaller::history_to_json() const
{
    std::vector<std::shared_ptr<CallRecord>> snap;
    {
        std::lock_guard<std::mutex> lk(history_mutex_);
        snap = history_;
    }
    std::string out = "[";
    for (std::size_t i = 0; i < snap.size(); ++i)
    {
        if (i > 0)
            out += ", ";
        out += record_to_json(*snap[i]);
    }
    out += "]";
    return out;
}

std::size_t ServiceCaller::history_from_json(const std::string& json)
{
    // Parse a JSON array of record objects.
    // Simple state machine: find '{' … '}' at depth 1.
    std::size_t imported = 0;
    std::size_t pos      = 0;
    while (pos < json.size())
    {
        auto start = json.find('{', pos);
        if (start == std::string::npos)
            break;
        int         depth = 0;
        std::size_t end   = start;
        while (end < json.size())
        {
            if (json[end] == '{')
                ++depth;
            if (json[end] == '}')
            {
                --depth;
                if (depth == 0)
                    break;
            }
            ++end;
        }
        if (depth != 0)
            break;
        std::string obj = json.substr(start, end - start + 1);
        pos             = end + 1;

        auto rec = std::make_shared<CallRecord>();
        rec->id  = next_id();
        if (record_from_json(obj, *rec))
        {
            std::lock_guard<std::mutex> lk(history_mutex_);
            history_.push_back(rec);
            handle_map_[rec->id] = rec;
            ++imported;
        }
    }
    prune_history(max_history_);
    return imported;
}

std::string ServiceCaller::fields_to_json(const std::vector<ServiceFieldValue>& fields)
{
    std::string out   = "{";
    bool        first = true;
    for (auto& fv : fields)
    {
        if (fv.is_struct_head())
            continue;
        if (!first)
            out += ", ";
        first = false;
        out += "\"" + json_escape(fv.path) + "\": ";
        if (fv.type == FieldType::String || fv.type == FieldType::WString)
            out += "\"" + json_escape(fv.value_str) + "\"";
        else if (fv.is_bool())
            out += (fv.value_str == "true") ? "true" : "false";
        else
            out += fv.value_str.empty() ? "0" : fv.value_str;
    }
    out += "}";
    return out;
}

bool ServiceCaller::json_to_fields(const std::string& json, std::vector<ServiceFieldValue>& fields)
{
    if (json.empty() || json == "{}")
        return true;
    // For each field, look up its path in the JSON object.
    for (auto& fv : fields)
    {
        if (fv.is_struct_head())
            continue;
        std::string val = json_get_string(json, fv.path);
        if (!val.empty())
            fv.value_str = val;
    }
    return true;
}

}   // namespace spectra::adapters::ros2
