// param_editor_panel.cpp — ROS2 Parameter Editor Panel (F3)

#include "ui/param_editor_panel.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <format>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// param_type_name
// ---------------------------------------------------------------------------

const char* param_type_name(ParamType t)
{
    switch (t)
    {
        case ParamType::NotSet:
            return "not_set";
        case ParamType::Bool:
            return "bool";
        case ParamType::Integer:
            return "int64";
        case ParamType::Double:
            return "double";
        case ParamType::String:
            return "string";
        case ParamType::ByteArray:
            return "byte[]";
        case ParamType::BoolArray:
            return "bool[]";
        case ParamType::IntegerArray:
            return "int64[]";
        case ParamType::DoubleArray:
            return "double[]";
        case ParamType::StringArray:
            return "string[]";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// ParamEditorPanel::from_rcl_type
// ---------------------------------------------------------------------------

ParamType ParamEditorPanel::from_rcl_type(uint8_t rcl_type)
{
    using PT = rcl_interfaces::msg::ParameterType;
    switch (rcl_type)
    {
        case PT::PARAMETER_BOOL:
            return ParamType::Bool;
        case PT::PARAMETER_INTEGER:
            return ParamType::Integer;
        case PT::PARAMETER_DOUBLE:
            return ParamType::Double;
        case PT::PARAMETER_STRING:
            return ParamType::String;
        case PT::PARAMETER_BYTE_ARRAY:
            return ParamType::ByteArray;
        case PT::PARAMETER_BOOL_ARRAY:
            return ParamType::BoolArray;
        case PT::PARAMETER_INTEGER_ARRAY:
            return ParamType::IntegerArray;
        case PT::PARAMETER_DOUBLE_ARRAY:
            return ParamType::DoubleArray;
        case PT::PARAMETER_STRING_ARRAY:
            return ParamType::StringArray;
        default:
            return ParamType::NotSet;
    }
}

// ---------------------------------------------------------------------------
// ParamValue
// ---------------------------------------------------------------------------

ParamValue ParamValue::from_msg(const rcl_interfaces::msg::ParameterValue& msg)
{
    ParamValue v;
    v.type = ParamEditorPanel::from_rcl_type(msg.type);
    switch (v.type)
    {
        case ParamType::Bool:
            v.bool_val = msg.bool_value;
            break;
        case ParamType::Integer:
            v.int_val = msg.integer_value;
            break;
        case ParamType::Double:
            v.double_val = msg.double_value;
            break;
        case ParamType::String:
            v.string_val = msg.string_value;
            break;
        case ParamType::ByteArray:
            v.byte_array = msg.byte_array_value;
            break;
        case ParamType::BoolArray:
            v.bool_array =
                std::vector<bool>(msg.bool_array_value.begin(), msg.bool_array_value.end());
            break;
        case ParamType::IntegerArray:
            v.int_array = std::vector<int64_t>(msg.integer_array_value.begin(),
                                               msg.integer_array_value.end());
            break;
        case ParamType::DoubleArray:
            v.double_array = msg.double_array_value;
            break;
        case ParamType::StringArray:
            v.string_array = msg.string_array_value;
            break;
        default:
            break;
    }
    return v;
}

rcl_interfaces::msg::ParameterValue ParamValue::to_msg() const
{
    rcl_interfaces::msg::ParameterValue msg;
    using PT = rcl_interfaces::msg::ParameterType;
    switch (type)
    {
        case ParamType::Bool:
            msg.type       = PT::PARAMETER_BOOL;
            msg.bool_value = bool_val;
            break;
        case ParamType::Integer:
            msg.type          = PT::PARAMETER_INTEGER;
            msg.integer_value = int_val;
            break;
        case ParamType::Double:
            msg.type         = PT::PARAMETER_DOUBLE;
            msg.double_value = double_val;
            break;
        case ParamType::String:
            msg.type         = PT::PARAMETER_STRING;
            msg.string_value = string_val;
            break;
        case ParamType::ByteArray:
            msg.type             = PT::PARAMETER_BYTE_ARRAY;
            msg.byte_array_value = byte_array;
            break;
        case ParamType::BoolArray:
            msg.type             = PT::PARAMETER_BOOL_ARRAY;
            msg.bool_array_value = std::vector<bool>(bool_array.begin(), bool_array.end());
            break;
        case ParamType::IntegerArray:
            msg.type                = PT::PARAMETER_INTEGER_ARRAY;
            msg.integer_array_value = std::vector<int64_t>(int_array.begin(), int_array.end());
            break;
        case ParamType::DoubleArray:
            msg.type               = PT::PARAMETER_DOUBLE_ARRAY;
            msg.double_array_value = double_array;
            break;
        case ParamType::StringArray:
            msg.type               = PT::PARAMETER_STRING_ARRAY;
            msg.string_array_value = string_array;
            break;
        default:
            msg.type = rcl_interfaces::msg::ParameterType::PARAMETER_NOT_SET;
            break;
    }
    return msg;
}

std::string ParamValue::to_display_string(size_t max_len) const
{
    std::string s;
    switch (type)
    {
        case ParamType::NotSet:
            s = "<not_set>";
            break;
        case ParamType::Bool:
            s = bool_val ? "true" : "false";
            break;
        case ParamType::Integer:
            s = std::to_string(int_val);
            break;
        case ParamType::Double:
            s = std::format("{:.8g}", double_val);
            break;
        case ParamType::String:
            s = "\"" + string_val + "\"";
            break;
        case ParamType::ByteArray:
        {
            s = "[";
            for (size_t i = 0; i < byte_array.size() && s.size() < max_len; ++i)
            {
                if (i > 0)
                    s += ",";
                s += std::format("0x{:02x}", byte_array[i]);
            }
            if (byte_array.size() > max_len / 5)
                s += "...";
            s += "]";
            break;
        }
        case ParamType::BoolArray:
        {
            s = "[";
            for (size_t i = 0; i < bool_array.size() && s.size() < max_len; ++i)
            {
                if (i > 0)
                    s += ",";
                s += bool_array[i] ? "T" : "F";
            }
            if (bool_array.size() > max_len)
                s += "...";
            s += "]";
            break;
        }
        case ParamType::IntegerArray:
        {
            s = "[";
            for (size_t i = 0; i < int_array.size() && s.size() < max_len; ++i)
            {
                if (i > 0)
                    s += ",";
                s += std::to_string(int_array[i]);
            }
            if (s.size() >= max_len)
                s += "...";
            s += "]";
            break;
        }
        case ParamType::DoubleArray:
        {
            s = "[";
            for (size_t i = 0; i < double_array.size() && s.size() < max_len; ++i)
            {
                if (i > 0)
                    s += ",";
                s += std::format("{:.4g}", double_array[i]);
            }
            if (s.size() >= max_len)
                s += "...";
            s += "]";
            break;
        }
        case ParamType::StringArray:
        {
            s = "[";
            for (size_t i = 0; i < string_array.size() && s.size() < max_len; ++i)
            {
                if (i > 0)
                    s += ",";
                s += "\"" + string_array[i] + "\"";
            }
            if (s.size() >= max_len)
                s += "...";
            s += "]";
            break;
        }
    }
    if (s.size() > max_len)
    {
        s = s.substr(0, max_len - 3) + "...";
    }
    return s;
}

bool ParamValue::operator==(const ParamValue& o) const
{
    if (type != o.type)
        return false;
    switch (type)
    {
        case ParamType::Bool:
            return bool_val == o.bool_val;
        case ParamType::Integer:
            return int_val == o.int_val;
        case ParamType::Double:
            return double_val == o.double_val;
        case ParamType::String:
            return string_val == o.string_val;
        case ParamType::ByteArray:
            return byte_array == o.byte_array;
        case ParamType::BoolArray:
            return bool_array == o.bool_array;
        case ParamType::IntegerArray:
            return int_array == o.int_array;
        case ParamType::DoubleArray:
            return double_array == o.double_array;
        case ParamType::StringArray:
            return string_array == o.string_array;
        default:
            return true;
    }
}

// ---------------------------------------------------------------------------
// ParamDescriptor
// ---------------------------------------------------------------------------

ParamDescriptor ParamDescriptor::from_msg(const rcl_interfaces::msg::ParameterDescriptor& msg)
{
    ParamDescriptor d;
    d.description    = msg.description;
    d.read_only      = msg.read_only;
    d.dynamic_typing = msg.dynamic_typing;
    if (!msg.floating_point_range.empty())
    {
        const auto& r      = msg.floating_point_range[0];
        d.float_range_min  = r.from_value;
        d.float_range_max  = r.to_value;
        d.float_range_step = r.step;
    }
    if (!msg.integer_range.empty())
    {
        const auto& r        = msg.integer_range[0];
        d.integer_range_min  = r.from_value;
        d.integer_range_max  = r.to_value;
        d.integer_range_step = r.step;
    }
    return d;
}

// ---------------------------------------------------------------------------
// ParamEditorPanel — constructor / destructor
// ---------------------------------------------------------------------------

ParamEditorPanel::ParamEditorPanel(rclcpp::Node::SharedPtr node) : node_(std::move(node))
{
    std::memset(node_input_buf_, 0, sizeof(node_input_buf_));
    std::memset(search_buf_, 0, sizeof(search_buf_));
    std::memset(preset_name_buf_, 0, sizeof(preset_name_buf_));
    std::memset(preset_path_buf_, 0, sizeof(preset_path_buf_));

    // Null-node instances are used by pure logic tests and should not spin up
    // background ROS-related worker threads.
    if (node_)
    {
        set_worker_ = std::thread(&ParamEditorPanel::set_worker_func, this);
    }
}

ParamEditorPanel::~ParamEditorPanel()
{
    // Signal the set-worker thread to stop and join.
    stop_worker_.store(true, std::memory_order_release);
    set_cv_.notify_one();
    if (set_worker_.joinable())
        set_worker_.join();

    std::lock_guard<std::mutex> lk(mutex_);
    destroy_clients();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ParamEditorPanel::set_target_node(const std::string& node_name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (node_name == target_node_)
        return;
    target_node_ = node_name;
    param_names_.clear();
    param_map_.clear();
    last_error_.clear();
    undo_slot_ = UndoEntry{};
    loaded_.store(false);
    destroy_clients();
    if (!node_name.empty())
    {
        create_clients(node_name);
        node_name.copy(node_input_buf_, sizeof(node_input_buf_) - 1);
        node_input_buf_[std::min(node_name.size(), sizeof(node_input_buf_) - 1)] = '\0';
    }
}

std::string ParamEditorPanel::target_node() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return target_node_;
}

void ParamEditorPanel::set_title(const std::string& t)
{
    std::lock_guard<std::mutex> lk(mutex_);
    title_ = t;
}

std::string ParamEditorPanel::title() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return title_;
}

void ParamEditorPanel::set_live_edit(bool live)
{
    std::lock_guard<std::mutex> lk(mutex_);
    live_edit_ = live;
}

bool ParamEditorPanel::live_edit() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return live_edit_;
}

// ---------------------------------------------------------------------------
// Service client management
// ---------------------------------------------------------------------------

void ParamEditorPanel::create_clients(const std::string& node_name)
{
    // Already holding mutex_
    if (!node_)
        return;
    list_client_ =
        node_->create_client<rcl_interfaces::srv::ListParameters>(node_name + "/list_parameters");
    desc_client_ = node_->create_client<rcl_interfaces::srv::DescribeParameters>(
        node_name + "/describe_parameters");
    get_client_ =
        node_->create_client<rcl_interfaces::srv::GetParameters>(node_name + "/get_parameters");
    set_client_ =
        node_->create_client<rcl_interfaces::srv::SetParameters>(node_name + "/set_parameters");
}

void ParamEditorPanel::destroy_clients()
{
    // Already holding mutex_
    list_client_.reset();
    desc_client_.reset();
    get_client_.reset();
    set_client_.reset();
}

// ---------------------------------------------------------------------------
// Refresh (async — spawns detached thread)
// ---------------------------------------------------------------------------

void ParamEditorPanel::refresh()
{
    RefreshDoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = refresh_done_cb_;
        if (target_node_.empty() || !list_client_ || !get_client_)
        {
            loaded_.store(false);
            last_error_ = "no target node";
            if (cb)
                cb(false);
            return;
        }
    }

    bool expected = false;
    if (!refreshing_.compare_exchange_strong(expected, true))
        return;

    std::thread([this]() { do_refresh(); }).detach();
}

void ParamEditorPanel::do_refresh()
{
    using namespace std::chrono_literals;

    std::string                                                        node_name;
    rclcpp::Client<rcl_interfaces::srv::ListParameters>::SharedPtr     lc;
    rclcpp::Client<rcl_interfaces::srv::DescribeParameters>::SharedPtr dc;
    rclcpp::Client<rcl_interfaces::srv::GetParameters>::SharedPtr      gc;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        node_name = target_node_;
        lc        = list_client_;
        dc        = desc_client_;
        gc        = get_client_;
    }

    auto finish = [&](bool ok, const std::string& err = {})
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!err.empty())
                last_error_ = err;
        }
        refreshing_.store(false);
        if (ok)
            loaded_.store(true);
        RefreshDoneCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = refresh_done_cb_;
        }
        if (cb)
            cb(ok);
    };

    if (node_name.empty() || !lc || !gc)
    {
        finish(false, "no target node");
        return;
    }

    // Helper: poll service_is_ready() with sleeps instead of calling
    // wait_for_service().  wait_for_service() internally polls the DDS
    // graph from this detached thread, which fights FastDDS's discovery
    // thread for the participant mutex and deadlocks when namespaced
    // nodes are present.
    auto poll_service_ready = [](auto& client, int max_attempts, int sleep_ms) -> bool
    {
        for (int i = 0; i < max_attempts; ++i)
        {
            if (client->service_is_ready())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }
        return false;
    };

    // 1) ListParameters
    auto list_req   = std::make_shared<rcl_interfaces::srv::ListParameters::Request>();
    list_req->depth = rcl_interfaces::srv::ListParameters::Request::DEPTH_RECURSIVE;
    if (!poll_service_ready(lc, 20, 100))
    {
        finish(false, "ListParameters service unavailable for " + node_name);
        return;
    }
    auto list_future = lc->async_send_request(list_req);
    if (list_future.wait_for(5s) != std::future_status::ready)
    {
        finish(false, "ListParameters timed out");
        return;
    }
    auto list_resp = list_future.get();
    if (!list_resp)
    {
        finish(false, "ListParameters returned null response");
        return;
    }
    std::vector<std::string> names = list_resp->result.names;

    if (names.empty())
    {
        std::lock_guard<std::mutex> lk(mutex_);
        param_names_.clear();
        param_map_.clear();
        finish(true);
        return;
    }

    // 2) DescribeParameters
    std::unordered_map<std::string, ParamDescriptor> descs;
    if (dc && poll_service_ready(dc, 20, 100))
    {
        auto desc_req    = std::make_shared<rcl_interfaces::srv::DescribeParameters::Request>();
        desc_req->names  = names;
        auto desc_future = dc->async_send_request(desc_req);
        if (desc_future.wait_for(5s) == std::future_status::ready)
        {
            auto desc_resp = desc_future.get();
            if (desc_resp)
            {
                for (size_t i = 0; i < names.size() && i < desc_resp->descriptors.size(); ++i)
                {
                    descs[names[i]] = ParamDescriptor::from_msg(desc_resp->descriptors[i]);
                }
            }
        }
    }

    // 3) GetParameters
    if (!poll_service_ready(gc, 20, 100))
    {
        finish(false, "GetParameters service unavailable");
        return;
    }
    auto get_req    = std::make_shared<rcl_interfaces::srv::GetParameters::Request>();
    get_req->names  = names;
    auto get_future = gc->async_send_request(get_req);
    if (get_future.wait_for(5s) != std::future_status::ready)
    {
        finish(false, "GetParameters timed out");
        return;
    }
    auto get_resp = get_future.get();
    if (!get_resp)
    {
        finish(false, "GetParameters returned null response");
        return;
    }

    // Build model
    std::vector<std::string>                    new_names;
    std::unordered_map<std::string, ParamEntry> new_map;
    for (size_t i = 0; i < names.size() && i < get_resp->values.size(); ++i)
    {
        ParamEntry entry;
        entry.name    = names[i];
        entry.current = ParamValue::from_msg(get_resp->values[i]);
        entry.type    = entry.current.type;
        entry.staged  = entry.current;
        if (descs.contains(names[i]))
            entry.descriptor = descs.at(names[i]);
        new_names.push_back(names[i]);
        new_map[names[i]] = std::move(entry);
    }

    // Sort by name
    std::sort(new_names.begin(), new_names.end());

    {
        std::lock_guard<std::mutex> lk(mutex_);
        param_names_ = std::move(new_names);
        param_map_   = std::move(new_map);
        last_error_.clear();
    }

    finish(true);
}

// ---------------------------------------------------------------------------
// Staged / apply / undo
// ---------------------------------------------------------------------------

void ParamEditorPanel::discard_staged()
{
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& kv : param_map_)
    {
        kv.second.staged       = kv.second.current;
        kv.second.staged_dirty = false;
        kv.second.set_error    = false;
    }
}

bool ParamEditorPanel::apply_staged()
{
    std::vector<std::pair<std::string, ParamValue>> to_set;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& name : param_names_)
        {
            auto it = param_map_.find(name);
            if (it == param_map_.end())
                continue;
            if (it->second.staged_dirty)
            {
                to_set.emplace_back(name, it->second.staged);
            }
        }
    }

    // Queue all dirty params for async set (non-blocking).
    for (const auto& [name, value] : to_set)
        queue_set_param(name, value);

    return true;   // result will be reflected asynchronously via set_error flags
}

bool ParamEditorPanel::undo_last()
{
    UndoEntry entry;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!undo_slot_.valid)
            return false;
        entry            = undo_slot_;
        undo_slot_.valid = false;
    }

    // Queue for async set (non-blocking).
    queue_set_param(entry.param_name, entry.before);
    return true;   // result reflected asynchronously
}

bool ParamEditorPanel::can_undo() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return undo_slot_.valid;
}

// ---------------------------------------------------------------------------
// Async set queue — never block the render thread on ROS2 service calls
// ---------------------------------------------------------------------------

void ParamEditorPanel::queue_set_param(const std::string& param_name, const ParamValue& value)
{
    std::lock_guard<std::mutex> lk(set_queue_mutex_);
    // Deduplicate: if there is already a pending set for this param, replace
    // its value so we only send the latest (avoids queue build-up on rapid
    // slider drags).
    for (auto& op : set_queue_)
    {
        if (op.name == param_name)
        {
            op.value = value;
            return;   // already queued, cv already notified from prior push
        }
    }
    set_queue_.push_back({param_name, value});
    set_cv_.notify_one();
}

void ParamEditorPanel::set_worker_func()
{
    while (!stop_worker_.load(std::memory_order_acquire))
    {
        PendingSetOp op;
        {
            std::unique_lock<std::mutex> lk(set_queue_mutex_);
            set_cv_.wait(
                lk,
                [this]
                { return !set_queue_.empty() || stop_worker_.load(std::memory_order_acquire); });
            if (stop_worker_.load(std::memory_order_acquire))
                break;
            op = std::move(set_queue_.front());
            set_queue_.pop_front();
        }

        auto result = set_param_internal(op.name, op.value);

        // On failure, mark the error on the entry so the UI shows it.
        if (!result.ok)
        {
            std::lock_guard<std::mutex> lk(mutex_);
            auto                        it = param_map_.find(op.name);
            if (it != param_map_.end())
            {
                it->second.set_error        = true;
                it->second.set_error_reason = result.reason;
            }
        }
    }
}

void ParamEditorPanel::poll_set_results()
{
    // Currently results are applied directly in set_worker_func via
    // set_param_internal.  This method is a hook for future use (e.g.
    // toasts / status bar notifications).  Called once per frame from draw().
}

// ---------------------------------------------------------------------------
// Internal set (called ONLY from background threads, never render thread)
// ---------------------------------------------------------------------------

ParamSetResult ParamEditorPanel::set_param_internal(const std::string& param_name,
                                                    const ParamValue&  value)
{
    using namespace std::chrono_literals;

    rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr sc;
    std::string                                                   node_name;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sc        = set_client_;
        node_name = target_node_;
    }

    if (!sc)
        return {false, "no set_client"};

    // Poll with sleeps instead of wait_for_service() to avoid DDS
    // participant mutex deadlock with rmw_fastrtps discovery thread.
    {
        bool ready = false;
        for (int i = 0; i < 20 && !ready; ++i)
        {
            ready = sc->service_is_ready();
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!ready)
            return {false, "SetParameters service unavailable"};
    }

    rcl_interfaces::msg::Parameter param_msg;
    param_msg.name  = param_name;
    param_msg.value = value.to_msg();

    auto req = std::make_shared<rcl_interfaces::srv::SetParameters::Request>();
    req->parameters.push_back(param_msg);

    auto future = sc->async_send_request(req);
    if (future.wait_for(5s) != std::future_status::ready)
    {
        return {false, "SetParameters timed out"};
    }
    auto resp = future.get();
    if (!resp || resp->results.empty())
        return {false, "null/empty response"};

    const auto& r = resp->results[0];
    if (!r.successful)
        return {false, r.reason};

    // Success — update model + undo slot
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (param_map_.contains(param_name))
        {
            auto& entry = param_map_[param_name];

            // Record undo before updating current
            undo_slot_.valid      = true;
            undo_slot_.node_name  = node_name;
            undo_slot_.param_name = param_name;
            undo_slot_.before     = entry.current;
            undo_slot_.after      = value;

            entry.current      = value;
            entry.staged       = value;
            entry.staged_dirty = false;
            entry.set_error    = false;
        }
    }

    // Fire callback
    ParamSetCallback cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = param_set_cb_;
    }
    if (cb)
        cb(param_name, value, true);

    return {true, {}};
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::vector<std::string> ParamEditorPanel::param_names() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return param_names_;
}

ParamEntry ParamEditorPanel::param_entry(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto                        it = param_map_.find(name);
    if (it == param_map_.end())
        return {};
    return it->second;
}

bool ParamEditorPanel::is_loaded() const
{
    return loaded_.load();
}
bool ParamEditorPanel::is_refreshing() const
{
    return refreshing_.load();
}

std::string ParamEditorPanel::last_error() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return last_error_;
}

void ParamEditorPanel::clear_error()
{
    std::lock_guard<std::mutex> lk(mutex_);
    last_error_.clear();
}

size_t ParamEditorPanel::staged_count() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    size_t                      n = 0;
    for (const auto& kv : param_map_)
        if (kv.second.staged_dirty)
            ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void ParamEditorPanel::set_on_refresh_done(RefreshDoneCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    refresh_done_cb_ = std::move(cb);
}

void ParamEditorPanel::set_on_param_set(ParamSetCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    param_set_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// YAML helpers
// ---------------------------------------------------------------------------

std::string ParamEditorPanel::yaml_scalar(const ParamValue& v)
{
    switch (v.type)
    {
        case ParamType::Bool:
            return v.bool_val ? "true" : "false";
        case ParamType::Integer:
            return std::to_string(v.int_val);
        case ParamType::Double:
            return std::format("{:.17g}", v.double_val);
        case ParamType::String:
            return v.string_val;
        default:
            return "";
    }
}

bool ParamEditorPanel::parse_yaml_scalar(const std::string& raw, ParamType type, ParamValue& out)
{
    out.type = type;
    try
    {
        switch (type)
        {
            case ParamType::Bool:
                out.bool_val = (raw == "true" || raw == "True" || raw == "1");
                return true;
            case ParamType::Integer:
                out.int_val = std::stoll(raw);
                return true;
            case ParamType::Double:
                out.double_val = std::stod(raw);
                return true;
            case ParamType::String:
                out.string_val = raw;
                return true;
            default:
                return false;
        }
    }
    catch (...)
    {
        return false;
    }
}

std::string ParamEditorPanel::serialize_yaml(
    const std::string&                                 node_name,
    const std::unordered_map<std::string, ParamValue>& params)
{
    std::ostringstream ss;
    // Normalise node name: strip leading slash for YAML key
    std::string key = node_name;
    if (!key.empty() && key[0] == '/')
        key = key.substr(1);
    // Replace remaining slashes with '/'  — keep as-is (ROS2 allows it)

    ss << key << ":\n";
    ss << "  ros__parameters:\n";

    // Collect keys sorted
    std::vector<std::string> sorted_keys;
    sorted_keys.reserve(params.size());
    for (const auto& kv : params)
        sorted_keys.push_back(kv.first);
    std::sort(sorted_keys.begin(), sorted_keys.end());

    for (const auto& name : sorted_keys)
    {
        const auto& v = params.at(name);
        ss << "    " << name << ": ";
        switch (v.type)
        {
            case ParamType::Bool:
            case ParamType::Integer:
            case ParamType::Double:
            case ParamType::String:
                ss << yaml_scalar(v) << "\n";
                break;
            case ParamType::BoolArray:
                ss << "[";
                for (size_t i = 0; i < v.bool_array.size(); ++i)
                {
                    if (i)
                        ss << ", ";
                    ss << (v.bool_array[i] ? "true" : "false");
                }
                ss << "]\n";
                break;
            case ParamType::IntegerArray:
                ss << "[";
                for (size_t i = 0; i < v.int_array.size(); ++i)
                {
                    if (i)
                        ss << ", ";
                    ss << v.int_array[i];
                }
                ss << "]\n";
                break;
            case ParamType::DoubleArray:
                ss << "[";
                for (size_t i = 0; i < v.double_array.size(); ++i)
                {
                    if (i)
                        ss << ", ";
                    ss << std::format("{:.17g}", v.double_array[i]);
                }
                ss << "]\n";
                break;
            case ParamType::StringArray:
                ss << "[";
                for (size_t i = 0; i < v.string_array.size(); ++i)
                {
                    if (i)
                        ss << ", ";
                    ss << "\"" << v.string_array[i] << "\"";
                }
                ss << "]\n";
                break;
            default:
                ss << "null\n";
                break;
        }
    }
    return ss.str();
}

bool ParamEditorPanel::parse_yaml(const std::string& yaml_text,
                                  const std::string& /*node_name*/,
                                  std::unordered_map<std::string, ParamValue>& out,
                                  std::string&                                 error_out)
{
    // Minimal hand-rolled YAML parser for ROS2 param files.
    // Expected format:
    //   <node_name_key>:
    //     ros__parameters:
    //       <param_name>: <scalar_value>
    //
    // Arrays in flow notation: [v1, v2, v3]
    // Strings: bare or "quoted"
    // Only handles scalar + flow-sequence values (no block sequences).

    out.clear();
    enum class State
    {
        Root,
        NodeKey,
        RosParams,
        Params
    };
    State              state = State::Root;
    std::istringstream ss(yaml_text);
    std::string        line;
    int                line_no = 0;

    while (std::getline(ss, line))
    {
        ++line_no;
        // Strip comment
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos)
            line = line.substr(0, comment_pos);
        // Count leading spaces
        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ')
            ++indent;
        std::string trimmed = line.substr(indent);
        if (trimmed.empty())
            continue;

        auto colon = trimmed.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key   = trimmed.substr(0, colon);
        std::string value = (colon + 1 < trimmed.size()) ? trimmed.substr(colon + 1) : "";
        // Trim value
        while (!value.empty() && value[0] == ' ')
            value = value.substr(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\r'))
            value.pop_back();

        if (state == State::Root && indent == 0 && !key.empty())
        {
            state = State::NodeKey;
            continue;
        }
        if (state == State::NodeKey && indent == 2 && key == "ros__parameters")
        {
            state = State::RosParams;
            continue;
        }
        if (state == State::RosParams && indent == 4 && !key.empty())
        {
            // Param entry
            if (value.empty())
                continue;   // skip nested (not supported)

            ParamValue pv;
            // Detect array (flow notation)
            if (!value.empty() && value[0] == '[')
            {
                // Parse flow sequence
                std::string inner = value;
                // Remove brackets
                if (inner.size() >= 2 && inner.back() == ']')
                {
                    inner = inner.substr(1, inner.size() - 2);
                }
                // Split by comma
                std::vector<std::string> elems;
                std::string              elem;
                bool                     in_quote = false;
                char                     qc       = 0;
                for (char c : inner)
                {
                    if (!in_quote && (c == '"' || c == '\''))
                    {
                        in_quote = true;
                        qc       = c;
                        continue;
                    }
                    if (in_quote && c == qc)
                    {
                        in_quote = false;
                        continue;
                    }
                    if (!in_quote && c == ',')
                    {
                        // trim elem
                        while (!elem.empty() && elem[0] == ' ')
                            elem = elem.substr(1);
                        while (!elem.empty() && elem.back() == ' ')
                            elem.pop_back();
                        elems.push_back(elem);
                        elem.clear();
                    }
                    else
                    {
                        elem += c;
                    }
                }
                while (!elem.empty() && elem[0] == ' ')
                    elem = elem.substr(1);
                while (!elem.empty() && elem.back() == ' ')
                    elem.pop_back();
                if (!elem.empty())
                    elems.push_back(elem);

                // Detect array type from first element
                if (elems.empty())
                {
                    pv.type = ParamType::StringArray;
                }
                else
                {
                    const std::string& f = elems[0];
                    bool               is_bool_like =
                        (f == "true" || f == "false" || f == "True" || f == "False");
                    bool is_int_like = false;
                    bool is_dbl_like = false;
                    if (!is_bool_like)
                    {
                        try
                        {
                            size_t pos = 0;
                            std::stoll(f, &pos);
                            is_int_like = (pos == f.size());
                        }
                        catch (...)
                        {
                        }
                        if (!is_int_like)
                        {
                            try
                            {
                                size_t pos = 0;
                                std::stod(f, &pos);
                                is_dbl_like = (pos == f.size());
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                    if (is_bool_like)
                    {
                        pv.type = ParamType::BoolArray;
                        for (const auto& e : elems)
                            pv.bool_array.push_back(e == "true" || e == "True" || e == "1");
                    }
                    else if (is_int_like)
                    {
                        pv.type = ParamType::IntegerArray;
                        for (const auto& e : elems)
                            try
                            {
                                pv.int_array.push_back(std::stoll(e));
                            }
                            catch (...)
                            {
                            }
                    }
                    else if (is_dbl_like)
                    {
                        pv.type = ParamType::DoubleArray;
                        for (const auto& e : elems)
                            try
                            {
                                pv.double_array.push_back(std::stod(e));
                            }
                            catch (...)
                            {
                            }
                    }
                    else
                    {
                        pv.type         = ParamType::StringArray;
                        pv.string_array = elems;
                    }
                }
            }
            else
            {
                // Scalar: detect type
                // Remove surrounding quotes
                std::string raw = value;
                if (raw.size() >= 2 && (raw[0] == '"' || raw[0] == '\'') && raw.back() == raw[0])
                {
                    raw           = raw.substr(1, raw.size() - 2);
                    pv.type       = ParamType::String;
                    pv.string_val = raw;
                }
                else if (raw == "true" || raw == "True")
                {
                    pv.type     = ParamType::Bool;
                    pv.bool_val = true;
                }
                else if (raw == "false" || raw == "False")
                {
                    pv.type     = ParamType::Bool;
                    pv.bool_val = false;
                }
                else
                {
                    // Try int, then double, then string
                    bool parsed = false;
                    try
                    {
                        size_t  pos = 0;
                        int64_t iv  = std::stoll(raw, &pos);
                        if (pos == raw.size())
                        {
                            pv.type    = ParamType::Integer;
                            pv.int_val = iv;
                            parsed     = true;
                        }
                    }
                    catch (...)
                    {
                    }
                    if (!parsed)
                    {
                        try
                        {
                            size_t pos = 0;
                            double dv  = std::stod(raw, &pos);
                            if (pos == raw.size())
                            {
                                pv.type       = ParamType::Double;
                                pv.double_val = dv;
                                parsed        = true;
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                    if (!parsed)
                    {
                        pv.type       = ParamType::String;
                        pv.string_val = raw;
                    }
                }
            }
            out[key] = pv;
        }
    }
    if (out.empty())
    {
        error_out = "no parameters found in YAML (check node name key and ros__parameters nesting)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// YAML preset save / load
// ---------------------------------------------------------------------------

bool ParamEditorPanel::save_preset(const std::string& display_name, const std::string& file_path)
{
    std::unordered_map<std::string, ParamValue> snap;
    std::string                                 node_name;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        node_name = target_node_;
        for (const auto& name : param_names_)
        {
            auto it = param_map_.find(name);
            if (it != param_map_.end())
                snap[name] = it->second.current;
        }
    }
    if (snap.empty())
        return false;

    std::string   yaml = serialize_yaml(node_name, snap);
    std::ofstream ofs(file_path);
    if (!ofs.is_open())
        return false;
    ofs << yaml;
    ofs.close();

    PresetEntry e;
    e.name      = display_name;
    e.node_name = node_name;
    e.yaml_path = file_path;
    add_preset(std::move(e));
    return true;
}

bool ParamEditorPanel::load_preset(const std::string& file_path)
{
    std::ifstream ifs(file_path);
    if (!ifs.is_open())
        return false;
    std::string yaml_text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    std::string node_name;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        node_name = target_node_;
    }

    std::unordered_map<std::string, ParamValue> parsed;
    std::string                                 err;
    if (!parse_yaml(yaml_text, node_name, parsed, err))
        return false;

    bool all_ok = true;
    for (const auto& kv : parsed)
    {
        queue_set_param(kv.first, kv.second);
    }
    return all_ok;
}

void ParamEditorPanel::add_preset(PresetEntry e)
{
    std::lock_guard<std::mutex> lk(mutex_);
    presets_.push_back(std::move(e));
}

void ParamEditorPanel::remove_preset(size_t idx)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (idx < presets_.size())
        presets_.erase(presets_.begin() + static_cast<ptrdiff_t>(idx));
}

const std::vector<PresetEntry>& ParamEditorPanel::presets() const
{
    // NOTE: returns a reference — caller must hold no lock when using this
    // in non-ImGui test code.  ImGui draw path calls this from render thread only.
    return presets_;
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void ParamEditorPanel::draw(bool* p_open)
{
    std::string title_str;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        title_str = title_;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title_str.c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    draw_error_banner();
    draw_toolbar();
    ImGui::Separator();
    draw_param_table();
    draw_preset_popup();
    poll_set_results();

    ImGui::End();
}

void ParamEditorPanel::draw_toolbar()
{
    // Node-name input
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputText("##node",
                         node_input_buf_,
                         sizeof(node_input_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        set_target_node(std::string(node_input_buf_));
        refresh();
    }
    ImGui::SameLine();

    // Auto-refresh: only attempt once when the node name changes.
    // Do NOT auto-fire every frame — that spams detached threads with
    // DDS graph queries that deadlock with rmw_fastrtps discovery.
    bool auto_refresh = false;
    if (!is_loaded() && !is_refreshing() && node_input_buf_[0] != '\0')
    {
        // Only auto-refresh if the target hasn't been attempted yet.
        std::lock_guard<std::mutex> lk(mutex_);
        if (last_error_.empty() && !target_node_.empty())
            auto_refresh = true;
    }

    if (ImGui::Button("Refresh") || auto_refresh)
    {
        set_target_node(std::string(node_input_buf_));
        refresh();
    }
    if (is_refreshing())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(refreshing…)");
    }

    // Live-edit toggle
    ImGui::SameLine();
    bool live = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        live = live_edit_;
    }
    if (ImGui::Checkbox("Live-edit", &live))
        set_live_edit(live);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("If checked, every widget change is sent immediately.\n"
                          "If unchecked (Apply mode), click Apply to commit.");

    // Apply / Discard (apply mode only)
    if (!live)
    {
        ImGui::SameLine();
        size_t            dirty     = staged_count();
        const std::string apply_lbl = std::format("Apply ({})##apply", dirty);
        if (ImGui::Button(apply_lbl.c_str()))
            apply_staged();
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
            discard_staged();
    }

    // Undo
    ImGui::SameLine();
    bool can = can_undo();
    if (!can)
        ImGui::BeginDisabled();
    if (ImGui::Button("Undo"))
        undo_last();
    if (!can)
        ImGui::EndDisabled();

    // Presets
    ImGui::SameLine();
    if (ImGui::Button("Presets…"))
        show_preset_popup_ = true;

    // Search / filter
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##search", "Filter params…", search_buf_, sizeof(search_buf_));
    ImGui::SameLine();
    if (ImGui::Checkbox("Show read-only", &show_read_only_))
    {
    }

    // Sort mode
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    const char* sort_items[] = {"Name", "Type"};
    ImGui::Combo("Sort", &sort_mode_, sort_items, 2);
}

void ParamEditorPanel::draw_error_banner()
{
    std::string err;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        err = last_error_;
    }
    if (err.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
    ImGui::TextWrapped("Error: %s", err.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("X"))
        clear_error();
}

void ParamEditorPanel::draw_param_table()
{
    if (!is_loaded() && !is_refreshing())
    {
        ImGui::TextDisabled("Enter a node name and click Refresh.");
        return;
    }
    if (is_refreshing() && !is_loaded())
    {
        ImGui::TextDisabled("Loading…");
        return;
    }

    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        names = param_names_;
    }

    // Apply search filter
    std::string filter(search_buf_);
    if (!filter.empty())
    {
        names.erase(std::remove_if(names.begin(),
                                   names.end(),
                                   [&](const std::string& n)
                                   { return n.find(filter) == std::string::npos; }),
                    names.end());
    }

    // Sort
    if (sort_mode_ == 1)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        std::sort(names.begin(),
                  names.end(),
                  [this](const std::string& a, const std::string& b)
                  {
                      auto ia = param_map_.find(a);
                      auto ib = param_map_.find(b);
                      if (ia == param_map_.end() || ib == param_map_.end())
                          return a < b;
                      return static_cast<int>(ia->second.type) < static_cast<int>(ib->second.type);
                  });
    }

    if (names.empty())
    {
        ImGui::TextDisabled("No parameters.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                            | ImGuiTableFlags_SizingStretchProp;
    float table_height = ImGui::GetContentRegionAvail().y - 4.0f;
    if (!ImGui::BeginTable("##params", 3, flags, ImVec2(0, table_height)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableHeadersRow();

    for (const auto& name : names)
    {
        ParamEntry* ep = nullptr;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            auto                        it = param_map_.find(name);
            if (it == param_map_.end())
                continue;
            ep = &it->second;
        }
        if (!show_read_only_ && ep->descriptor.read_only)
            continue;
        draw_param_row(*ep);
    }

    ImGui::EndTable();
}

void ParamEditorPanel::draw_param_row(ParamEntry& entry)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    // Name (+ read-only indicator)
    if (entry.descriptor.read_only)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::PopStyleColor();
    }
    else if (entry.staged_dirty)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextUnformatted(entry.name.c_str());
    }
    if (!entry.descriptor.description.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", entry.descriptor.description.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", param_type_name(entry.type));

    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(entry.name.c_str());

    if (entry.set_error)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted("Error");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set failed: %s", entry.set_error_reason.c_str());
    }
    else if (entry.descriptor.read_only)
    {
        // Read-only: display value, no widget
        ImGui::TextDisabled("%s", entry.current.to_display_string(48).c_str());
    }
    else
    {
        switch (entry.type)
        {
            case ParamType::Bool:
                draw_bool_widget(entry);
                break;
            case ParamType::Integer:
                draw_int_widget(entry);
                break;
            case ParamType::Double:
                draw_double_widget(entry);
                break;
            case ParamType::String:
                draw_string_widget(entry);
                break;
            default:
                draw_array_widget(entry);
                break;
        }
    }

    ImGui::PopID();
}

void ParamEditorPanel::draw_bool_widget(ParamEntry& entry)
{
    bool v = entry.staged.bool_val;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Checkbox("##v", &v))
    {
        entry.staged.bool_val = v;
        entry.staged.type     = ParamType::Bool;
        entry.staged_dirty    = (entry.staged != entry.current);
        if (live_edit_)
        {
            queue_set_param(entry.name, entry.staged);
        }
    }
}

void ParamEditorPanel::draw_int_widget(ParamEntry& entry)
{
    int v = static_cast<int>(entry.staged.int_val);
    ImGui::SetNextItemWidth(-1);
    bool changed = false;
    if (entry.descriptor.has_integer_range())
    {
        int lo  = static_cast<int>(entry.descriptor.integer_range_min);
        int hi  = static_cast<int>(entry.descriptor.integer_range_max);
        changed = ImGui::SliderInt("##v", &v, lo, hi);
    }
    else
    {
        changed = ImGui::DragInt("##v", &v, 1.0f);
    }
    if (changed)
    {
        entry.staged.int_val = static_cast<int64_t>(v);
        entry.staged.type    = ParamType::Integer;
        entry.staged_dirty   = (entry.staged != entry.current);
        if (live_edit_)
        {
            queue_set_param(entry.name, entry.staged);
        }
    }
}

void ParamEditorPanel::draw_double_widget(ParamEntry& entry)
{
    auto v = static_cast<float>(entry.staged.double_val);
    ImGui::SetNextItemWidth(-1);
    bool changed = false;
    if (entry.descriptor.has_float_range())
    {
        auto lo = static_cast<float>(entry.descriptor.float_range_min);
        auto hi = static_cast<float>(entry.descriptor.float_range_max);
        changed = ImGui::SliderFloat("##v", &v, lo, hi, "%.6g");
    }
    else
    {
        changed = ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.6g");
    }
    if (changed)
    {
        entry.staged.double_val = static_cast<double>(v);
        entry.staged.type       = ParamType::Double;
        entry.staged_dirty      = (entry.staged != entry.current);
        if (live_edit_)
        {
            queue_set_param(entry.name, entry.staged);
        }
    }
}

void ParamEditorPanel::draw_string_widget(ParamEntry& entry)
{
    // Use a 256-char static buffer keyed by entry name
    // (single param row rendered per frame — safe)
    static char str_buf[256];
    std::strncpy(str_buf, entry.staged.string_val.c_str(), sizeof(str_buf) - 1);
    str_buf[sizeof(str_buf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##v", str_buf, sizeof(str_buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        entry.staged.string_val = str_buf;
        entry.staged.type       = ParamType::String;
        entry.staged_dirty      = (entry.staged != entry.current);
        if (live_edit_)
        {
            queue_set_param(entry.name, entry.staged);
        }
    }
}

void ParamEditorPanel::draw_array_widget(ParamEntry& entry)
{
    // Arrays are display-only (editing large arrays inline is impractical)
    std::string disp = entry.current.to_display_string(56);
    ImGui::TextDisabled("%s", disp.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", entry.current.to_display_string(512).c_str());
}

void ParamEditorPanel::draw_preset_popup()
{
    if (!show_preset_popup_)
        return;
    ImGui::OpenPopup("##presets_popup");
    show_preset_popup_ = false;

    if (!ImGui::BeginPopup("##presets_popup"))
        return;

    ImGui::Text("Presets");
    ImGui::Separator();

    // Saved presets list
    std::vector<PresetEntry> plist;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        plist = presets_;
    }

    if (plist.empty())
    {
        ImGui::TextDisabled("(no presets saved)");
    }
    else
    {
        for (size_t i = 0; i < plist.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(plist[i].name.c_str()))
            {
                load_preset(plist[i].yaml_path);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nNode: %s",
                                  plist[i].yaml_path.c_str(),
                                  plist[i].node_name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                remove_preset(i);
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::Text("Save current parameters:");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##pname", "Preset name", preset_name_buf_, sizeof(preset_name_buf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##ppath",
                             "/tmp/params.yaml",
                             preset_path_buf_,
                             sizeof(preset_path_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        if (preset_name_buf_[0] != '\0' && preset_path_buf_[0] != '\0')
        {
            save_preset(std::string(preset_name_buf_), std::string(preset_path_buf_));
            std::memset(preset_name_buf_, 0, sizeof(preset_name_buf_));
            std::memset(preset_path_buf_, 0, sizeof(preset_path_buf_));
        }
    }

    ImGui::EndPopup();
}

#else   // !SPECTRA_USE_IMGUI

void ParamEditorPanel::draw(bool*) {}
void ParamEditorPanel::draw_toolbar() {}
void ParamEditorPanel::draw_error_banner() {}
void ParamEditorPanel::draw_param_table() {}
void ParamEditorPanel::draw_param_row(ParamEntry&) {}
void ParamEditorPanel::draw_bool_widget(ParamEntry&) {}
void ParamEditorPanel::draw_int_widget(ParamEntry&) {}
void ParamEditorPanel::draw_double_widget(ParamEntry&) {}
void ParamEditorPanel::draw_string_widget(ParamEntry&) {}
void ParamEditorPanel::draw_array_widget(ParamEntry&) {}
void ParamEditorPanel::draw_preset_popup() {}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
