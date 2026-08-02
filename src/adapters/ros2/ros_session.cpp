// ros_session.cpp — RosSession save/load implementation (G3).

#include "ros_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace spectra::adapters::ros2
{

namespace
{
using json = nlohmann::json;

std::string panel_legacy_key_to_id_impl(std::string_view legacy_key)
{
    if (legacy_key.starts_with("ros."))
        return std::string(legacy_key);

    static const std::pair<const char*, const char*> kLegacyToId[] = {
        {"topic_list", "ros.topic_list"},
        {"topic_echo", "ros.topic_echo"},
        {"topic_stats", "ros.topic_stats"},
        {"plot_area", "ros.plot_area"},
        {"expression_editor", "ros.expression_editor"},
        {"bag_info", "ros.bag_info"},
        {"bag_playback", "ros.bag_playback"},
        {"log_viewer", "ros.log_viewer"},
        {"diagnostics", "ros.diagnostics"},
        {"node_graph", "ros.node_graph"},
        {"displays_panel", "ros.displays"},
        {"displays", "ros.displays"},
        {"scene_viewport", "ros.scene_viewport"},
        {"inspector_panel", "ros.inspector"},
        {"inspector", "ros.inspector"},
        {"tf_tree", "ros.tf_tree"},
        {"param_editor", "ros.param_editor"},
        {"service_caller", "ros.service_caller"},
    };

    for (const auto& [legacy, id] : kLegacyToId)
    {
        if (legacy_key == legacy)
            return id;
    }
    return std::string(legacy_key);
}

}   // namespace

std::string panel_legacy_key_to_id(std::string_view legacy_key)
{
    return panel_legacy_key_to_id_impl(legacy_key);
}

bool PanelVisibility::visible(std::string_view panel_id, bool default_val) const
{
    const auto it = by_id.find(std::string(panel_id));
    return it != by_id.end() ? it->second : default_val;
}

void PanelVisibility::set_visible(std::string_view panel_id, bool v)
{
    by_id[std::string(panel_id)] = v;
}

PanelVisibility default_panel_visibility()
{
    PanelVisibility panels;
    panels.by_id = {
        {"ros.topic_list", true},
        {"ros.topic_echo", false},
        {"ros.topic_stats", true},
        {"ros.plot_area", true},
        {"ros.expression_editor", false},
        {"ros.bag_info", false},
        {"ros.bag_playback", false},
        {"ros.log_viewer", false},
        {"ros.diagnostics", false},
        {"ros.node_graph", false},
        {"ros.displays", false},
        {"ros.scene_viewport", false},
        {"ros.inspector", false},
        {"ros.tf_tree", false},
        {"ros.param_editor", false},
        {"ros.service_caller", false},
    };
    panels.nav_rail = true;
    return panels;
}

namespace
{

json to_json(const SubscriptionEntry& entry)
{
    return json{
        {"topic", entry.topic},
        {"field_path", entry.field_path},
        {"type_name", entry.type_name},
        {"subplot_slot", entry.subplot_slot},
        {"time_window_s", entry.time_window_s},
        {"scroll_paused", entry.scroll_paused},
        {"axis_mode", axis_mode_name(entry.axis_mode)},
        {"x_field_path", entry.x_field_path},
        {"y_field_path", entry.y_field_path},
    };
}

json to_json(const ExpressionEntry::VarBinding& binding)
{
    return json{
        {"variable", binding.variable},
        {"topic", binding.topic},
        {"field_path", binding.field_path},
    };
}

json to_json(const ExpressionEntry& entry)
{
    json bindings = json::array();
    for (const auto& binding : entry.bindings)
        bindings.push_back(to_json(binding));

    return json{
        {"expression", entry.expression},
        {"label", entry.label},
        {"subplot_slot", entry.subplot_slot},
        {"bindings", std::move(bindings)},
    };
}

json to_json(const ExpressionPresetEntry& entry)
{
    return json{
        {"name", entry.name},
        {"expression", entry.expression},
        {"variables", entry.variables},
    };
}

json to_json(const DisplaySessionEntry& entry)
{
    return json{
        {"type_id", entry.type_id},
        {"topic", entry.topic},
        {"enabled", entry.enabled},
        {"config_blob", entry.config_blob},
    };
}

json to_json(const SceneCameraPose& pose)
{
    return json{
        {"azimuth", pose.azimuth},
        {"elevation", pose.elevation},
        {"distance", pose.distance},
        {"target", pose.target},
        {"projection", pose.projection},
        {"fov", pose.fov},
    };
}

json to_json(const PanelVisibility& panels)
{
    json result = json::object();
    for (const auto& [id, visible] : panels.by_id)
        result[id] = visible;
    result["nav_rail"] = panels.nav_rail;
    return result;
}

json to_json(const TopicMonitorState& topic_monitor)
{
    return json{
        {"show_type", topic_monitor.show_type},
        {"show_hz", topic_monitor.show_hz},
        {"show_pubs", topic_monitor.show_pubs},
        {"show_subs", topic_monitor.show_subs},
        {"show_bw", topic_monitor.show_bw},
    };
}

PanelVisibility panel_visibility_from_json(const json& value)
{
    PanelVisibility panels = default_panel_visibility();
    if (!value.is_object())
        return panels;

    for (auto it = value.begin(); it != value.end(); ++it)
    {
        const std::string key = it.key();
        if (key == "nav_rail")
        {
            if (it->is_boolean())
                panels.nav_rail = it->get<bool>();
            continue;
        }
        if (!it->is_boolean())
            continue;
        panels.by_id[panel_legacy_key_to_id_impl(key)] = it->get<bool>();
    }
    return panels;
}

TopicMonitorState topic_monitor_state_from_json(const json& value)
{
    TopicMonitorState topic_monitor;
    if (!value.is_object())
        return topic_monitor;

    topic_monitor.show_type = value.value("show_type", true);
    topic_monitor.show_hz   = value.value("show_hz", true);
    topic_monitor.show_pubs = value.value("show_pubs", true);
    topic_monitor.show_subs = value.value("show_subs", true);
    topic_monitor.show_bw   = value.value("show_bw", true);
    return topic_monitor;
}

SceneCameraPose scene_camera_pose_from_json(const json& value)
{
    SceneCameraPose pose;
    if (!value.is_object())
        return pose;

    pose.azimuth    = value.value("azimuth", pose.azimuth);
    pose.elevation  = value.value("elevation", pose.elevation);
    pose.distance   = value.value("distance", pose.distance);
    pose.projection = value.value("projection", pose.projection);
    pose.fov        = value.value("fov", pose.fov);

    if (const auto target_it = value.find("target");
        target_it != value.end() && target_it->is_array() && target_it->size() == 3)
    {
        for (size_t i = 0; i < 3; ++i)
            pose.target[i] = (*target_it)[i].get<double>();
    }

    return pose;
}

std::array<double, 4> background_color_from_json(const json& value)
{
    std::array<double, 4> rgba{{0.08, 0.09, 0.12, 1.0}};
    if (!value.is_array() || value.size() != 4)
        return rgba;

    for (size_t i = 0; i < 4; ++i)
        rgba[i] = value[i].get<double>();

    return rgba;
}

json serialize_session_v2_json(const RosSession& session)
{
    json subscriptions = json::array();
    for (const auto& entry : session.subscriptions)
        subscriptions.push_back(to_json(entry));

    json expressions = json::array();
    for (const auto& entry : session.expressions)
        expressions.push_back(to_json(entry));

    json presets = json::array();
    for (const auto& entry : session.expression_presets)
        presets.push_back(to_json(entry));

    json displays = json::array();
    for (const auto& entry : session.displays)
        displays.push_back(to_json(entry));

    return json{
        {"version", SESSION_FORMAT_VERSION},
        {"node_name", session.node_name},
        {"node_ns", session.node_ns},
        {"layout", session.layout},
        {"subplot_rows", session.subplot_rows},
        {"subplot_cols", session.subplot_cols},
        {"time_window_s", session.time_window_s},
        {"pruning_enabled", session.pruning_enabled},
        {"prune_buffer_s", session.prune_buffer_s},
        {"saved_at", session.saved_at},
        {"description", session.description},
        {"subscriptions", std::move(subscriptions)},
        {"expressions", std::move(expressions)},
        {"expression_presets", std::move(presets)},
        {"scene",
         {
             {"fixed_frame", session.fixed_frame},
             {"camera_pose", to_json(session.camera_pose)},
             {"background_color", session.scene_background_color},
             {"displays", std::move(displays)},
         }},
        {"ui",
         {
             {"panels", to_json(session.panels)},
             {"topic_monitor", to_json(session.topic_monitor)},
             {"nav_rail",
              {
                  {"visible", session.panels.nav_rail},
                  {"expanded", session.nav_rail_expanded},
                  {"width", session.nav_rail_width},
              }},
             {"imgui_layout", session.imgui_ini_data},
         }},
    };
}

bool deserialize_session_v2_json(const json& root, RosSession& out, std::string& error_out)
{
    if (!root.is_object())
    {
        error_out = "top-level JSON must be an object";
        return false;
    }

    out                 = RosSession{};
    out.version         = root.value("version", SESSION_FORMAT_VERSION);
    out.node_name       = root.value("node_name", std::string{});
    out.node_ns         = root.value("node_ns", std::string{});
    out.layout          = root.value("layout", std::string{"default"});
    out.subplot_rows    = root.value("subplot_rows", 1);
    out.subplot_cols    = root.value("subplot_cols", 1);
    out.time_window_s   = root.value("time_window_s", 30.0);
    out.pruning_enabled = root.value("pruning_enabled", true);
    out.prune_buffer_s  = root.value("prune_buffer_s", 20.0);
    out.saved_at        = root.value("saved_at", std::string{});
    out.description     = root.value("description", std::string{});

    if (const auto it = root.find("subscriptions"); it != root.end() && it->is_array())
    {
        for (const auto& item : *it)
        {
            if (!item.is_object())
                continue;
            SubscriptionEntry entry;
            entry.topic         = item.value("topic", std::string{});
            entry.field_path    = item.value("field_path", std::string{});
            entry.type_name     = item.value("type_name", std::string{});
            entry.subplot_slot  = item.value("subplot_slot", 0);
            entry.time_window_s = item.value("time_window_s", 0.0);
            entry.scroll_paused = item.value("scroll_paused", false);
            entry.axis_mode =
                axis_mode_from_name(item.value("axis_mode", std::string{"time-series"}));
            entry.x_field_path = item.value("x_field_path", std::string{});
            entry.y_field_path = item.value("y_field_path", std::string{});
            if (!entry.topic.empty())
                out.subscriptions.push_back(std::move(entry));
        }
    }

    if (const auto it = root.find("expressions"); it != root.end() && it->is_array())
    {
        for (const auto& item : *it)
        {
            if (!item.is_object())
                continue;
            ExpressionEntry entry;
            entry.expression   = item.value("expression", std::string{});
            entry.label        = item.value("label", std::string{});
            entry.subplot_slot = item.value("subplot_slot", 0);
            if (const auto bindings_it = item.find("bindings");
                bindings_it != item.end() && bindings_it->is_array())
            {
                for (const auto& binding_item : *bindings_it)
                {
                    if (!binding_item.is_object())
                        continue;
                    ExpressionEntry::VarBinding binding;
                    binding.variable   = binding_item.value("variable", std::string{});
                    binding.topic      = binding_item.value("topic", std::string{});
                    binding.field_path = binding_item.value("field_path", std::string{});
                    if (!binding.variable.empty())
                        entry.bindings.push_back(std::move(binding));
                }
            }
            if (!entry.expression.empty())
                out.expressions.push_back(std::move(entry));
        }
    }

    if (const auto it = root.find("expression_presets"); it != root.end() && it->is_array())
    {
        for (const auto& item : *it)
        {
            if (!item.is_object())
                continue;
            ExpressionPresetEntry entry;
            entry.name       = item.value("name", std::string{});
            entry.expression = item.value("expression", std::string{});
            if (const auto vars_it = item.find("variables");
                vars_it != item.end() && vars_it->is_array())
            {
                for (const auto& variable : *vars_it)
                {
                    if (variable.is_string())
                        entry.variables.push_back(variable.get<std::string>());
                }
            }
            if (!entry.name.empty())
                out.expression_presets.push_back(std::move(entry));
        }
    }

    const json& scene =
        root.contains("scene") && root["scene"].is_object() ? root["scene"] : json::object();
    out.fixed_frame = scene.value("fixed_frame", std::string{});
    if (const auto it = scene.find("camera_pose"); it != scene.end())
        out.camera_pose = scene_camera_pose_from_json(*it);
    if (const auto it = scene.find("background_color"); it != scene.end())
        out.scene_background_color = background_color_from_json(*it);
    if (const auto it = scene.find("displays"); it != scene.end() && it->is_array())
    {
        for (const auto& item : *it)
        {
            if (!item.is_object())
                continue;
            DisplaySessionEntry entry;
            entry.type_id     = item.value("type_id", std::string{});
            entry.topic       = item.value("topic", std::string{});
            entry.enabled     = item.value("enabled", true);
            entry.config_blob = item.value("config_blob", std::string{});
            if (!entry.type_id.empty())
                out.displays.push_back(std::move(entry));
        }
    }

    const json& ui = root.contains("ui") && root["ui"].is_object() ? root["ui"] : json::object();
    if (const auto it = ui.find("panels"); it != ui.end())
        out.panels = panel_visibility_from_json(*it);
    else
        out.panels = panel_visibility_from_json(json::object());
    if (const auto it = ui.find("topic_monitor"); it != ui.end())
        out.topic_monitor = topic_monitor_state_from_json(*it);
    const json& nav_rail =
        ui.contains("nav_rail") && ui["nav_rail"].is_object() ? ui["nav_rail"] : json::object();
    out.panels.nav_rail   = nav_rail.value("visible", out.panels.nav_rail);
    out.nav_rail_expanded = nav_rail.value("expanded", false);
    out.nav_rail_width    = nav_rail.value("width", 220.0);
    out.imgui_ini_data    = ui.value("imgui_layout", std::string{});

    return true;
}

}   // namespace

// ===========================================================================
// RosSessionManager — public API
// ===========================================================================

RosSessionManager::RosSessionManager() = default;

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

SaveResult RosSessionManager::save(const RosSession& session, const std::string& path)
{
    SaveResult result;
    result.path = path;

    if (path.empty())
    {
        result.error = "path is empty";
        return result;
    }

    if (!ensure_directory(path))
    {
        result.error = "failed to create parent directory for: " + path;
        return result;
    }

    // Stamp the saved_at field.
    RosSession stamped = session;
    stamped.saved_at   = current_iso8601();

    std::string json = serialize(stamped);
    if (!write_file(path, json))
    {
        result.error = "failed to write session file: " + path;
        return result;
    }

    last_path_ = path;
    push_recent(path, session.node_name, stamped.saved_at);

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

LoadResult RosSessionManager::load(const std::string& path)
{
    LoadResult result;
    result.path = path;

    if (path.empty())
    {
        result.error = "path is empty";
        return result;
    }

    std::string content;
    if (!read_file(path, content))
    {
        result.error = "failed to read session file: " + path;
        return result;
    }

    std::string err;
    if (!deserialize(content, result.session, err))
    {
        result.error = "JSON parse error: " + err;
        return result;
    }

    last_path_ = path;
    push_recent(path, result.session.node_name, result.session.saved_at);

    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// auto_save
// ---------------------------------------------------------------------------

SaveResult RosSessionManager::auto_save(const RosSession& session)
{
    if (last_path_.empty())
    {
        SaveResult r;
        r.ok    = false;
        r.error = "no last_path set — use save() first or set_last_path()";
        return r;
    }
    return save(session, last_path_);
}

// ---------------------------------------------------------------------------
// recent list
// ---------------------------------------------------------------------------

std::vector<RecentEntry> RosSessionManager::load_recent()
{
    std::string path = default_recent_path();
    std::string content;
    if (!read_file(path, content))
    {
        return {};
    }
    return deserialize_recent(content);
}

bool RosSessionManager::save_recent(const std::vector<RecentEntry>& entries)
{
    std::string path = default_recent_path();
    if (!ensure_directory(path))
    {
        return false;
    }
    std::string json = serialize_recent(entries);
    return write_file(path, json);
}

void RosSessionManager::push_recent(const std::string& path,
                                    const std::string& node,
                                    const std::string& saved_at)
{
    auto list = load_recent();

    // Remove any existing entry with the same path.
    list.erase(std::remove_if(list.begin(),
                              list.end(),
                              [&path](const RecentEntry& e) { return e.path == path; }),
               list.end());

    // Insert at front.
    RecentEntry e;
    e.path     = path;
    e.node     = node;
    e.saved_at = saved_at.empty() ? current_iso8601() : saved_at;
    list.insert(list.begin(), e);

    // Trim to MAX_RECENT.
    if (static_cast<int>(list.size()) > MAX_RECENT)
    {
        list.resize(MAX_RECENT);
    }

    save_recent(list);
}

void RosSessionManager::remove_recent(const std::string& path)
{
    auto list = load_recent();
    list.erase(std::remove_if(list.begin(),
                              list.end(),
                              [&path](const RecentEntry& e) { return e.path == path; }),
               list.end());
    save_recent(list);
}

void RosSessionManager::clear_recent()
{
    save_recent({});
}

std::vector<std::string> RosSessionManager::load_favorites()
{
    std::string content;
    if (!read_file(default_favorites_path(), content))
        return {};

    try
    {
        const json parsed = json::parse(content);
        if (!parsed.is_array())
            return {};
        std::vector<std::string> favorites;
        favorites.reserve(parsed.size());
        for (const auto& item : parsed)
        {
            if (item.is_string())
                favorites.push_back(item.get<std::string>());
        }
        std::sort(favorites.begin(), favorites.end());
        favorites.erase(std::unique(favorites.begin(), favorites.end()), favorites.end());
        return favorites;
    }
    catch (...)
    {
        return {};
    }
}

bool RosSessionManager::save_favorites(const std::vector<std::string>& topics)
{
    std::vector<std::string> sorted = topics;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (!ensure_directory(default_favorites_path()))
        return false;

    json out = json::array();
    for (const auto& topic : sorted)
    {
        if (!topic.empty())
            out.push_back(topic);
    }
    return write_file(default_favorites_path(), out.dump(2));
}

bool RosSessionManager::toggle_favorite(const std::string& topic)
{
    if (topic.empty())
        return false;
    auto       favorites = load_favorites();
    const auto it        = std::find(favorites.begin(), favorites.end(), topic);
    bool       added     = false;
    if (it == favorites.end())
    {
        favorites.push_back(topic);
        added = true;
    }
    else
    {
        favorites.erase(it);
    }
    save_favorites(favorites);
    return added;
}

bool RosSessionManager::is_favorite(const std::string& topic)
{
    const auto favorites = load_favorites();
    return std::find(favorites.begin(), favorites.end(), topic) != favorites.end();
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::string RosSessionManager::default_recent_path()
{
    // Use $HOME/.config/spectra/ros_recent.json
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        home = "/tmp";
    }
    std::string dir = std::string(home) + "/.config/spectra";
    return dir + "/ros_recent.json";
}

std::string RosSessionManager::default_favorites_path()
{
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        home = "/tmp";
    }
    std::string dir = std::string(home) + "/.config/spectra";
    return dir + "/ros_favorites.json";
}

std::string RosSessionManager::default_session_path(const std::string& node_name)
{
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        home = "/tmp";
    }
    std::string name = node_name.empty() ? "default" : node_name;
    // Sanitise: replace '/' with '_'
    for (char& c : name)
    {
        if (c == '/' || c == '\\' || c == ':')
            c = '_';
    }
    return std::string(home) + "/.config/spectra/sessions/" + name + ".spectra-ros-session";
}

// ===========================================================================
// JSON serialization
// ===========================================================================

// ---------------------------------------------------------------------------
// json_escape
// ---------------------------------------------------------------------------

std::string RosSessionManager::json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
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
                if (c < 0x20)
                {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                }
                else
                {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// json_get_* — extract scalar values from a flat JSON object string
// ---------------------------------------------------------------------------

std::string RosSessionManager::json_get_string(const std::string& json, const std::string& key)
{
    // Look for "key":"value" pattern.
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return {};

    pos += pattern.size();
    // Skip whitespace and ':'
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"')
        return {};
    ++pos;   // skip opening quote

    std::string result;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            switch (json[pos])
            {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += json[pos];
                    break;
            }
        }
        else
        {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

int RosSessionManager::json_get_int(const std::string& json,
                                    const std::string& key,
                                    int                default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return default_val;

    pos += pattern.size();
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size())
        return default_val;

    try
    {
        size_t consumed = 0;
        int    val      = std::stoi(json.substr(pos), &consumed);
        (void)consumed;
        return val;
    }
    catch (...)
    {
        return default_val;
    }
}

double RosSessionManager::json_get_double(const std::string& json,
                                          const std::string& key,
                                          double             default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return default_val;

    pos += pattern.size();
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size())
        return default_val;

    try
    {
        size_t consumed = 0;
        double val      = std::stod(json.substr(pos), &consumed);
        (void)consumed;
        return val;
    }
    catch (...)
    {
        return default_val;
    }
}

bool RosSessionManager::json_get_bool(const std::string& json,
                                      const std::string& key,
                                      bool               default_val)
{
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return default_val;

    pos += pattern.size();
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size())
        return default_val;

    if (json.compare(pos, 4, "true") == 0)
        return true;
    if (json.compare(pos, 5, "false") == 0)
        return false;
    return default_val;
}

// ---------------------------------------------------------------------------
// current_iso8601
// ---------------------------------------------------------------------------

std::string RosSessionManager::current_iso8601()
{
    auto        now = std::chrono::system_clock::now();
    std::time_t t   = std::chrono::system_clock::to_time_t(now);
    struct std::tm tm_utc
    {
    };
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
                       tm_utc.tm_year + 1900,
                       tm_utc.tm_mon + 1,
                       tm_utc.tm_mday,
                       tm_utc.tm_hour,
                       tm_utc.tm_min,
                       tm_utc.tm_sec);
}

// ---------------------------------------------------------------------------
// build_object — minimal JSON object builder from initializer list
// ---------------------------------------------------------------------------

std::string RosSessionManager::build_object(
    std::initializer_list<std::pair<std::string, std::string>> kv)
{
    std::string out   = "{";
    bool        first = true;
    for (const auto& [k, v] : kv)
    {
        if (!first)
            out += ",";
        out += "\n  \"";
        out += k;
        out += "\": ";
        out += v;
        first = false;
    }
    out += "\n}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_subscription
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_subscription(const SubscriptionEntry& e)
{
    std::string out = "{";
    out += R"("topic":")" + json_escape(e.topic) + "\",";
    out += R"("field_path":")" + json_escape(e.field_path) + "\",";
    out += R"("type_name":")" + json_escape(e.type_name) + "\",";
    out += "\"subplot_slot\":" + std::to_string(e.subplot_slot) + ",";
    out += "\"time_window_s\":" + std::to_string(e.time_window_s) + ",";
    out += "\"scroll_paused\":" + std::string(e.scroll_paused ? "true" : "false");
    out += "}";
    return out;
}

static std::string serialize_var_binding(const ExpressionEntry::VarBinding& b)
{
    std::string out = "{";
    out += R"("variable":")" + RosSessionManager::json_escape(b.variable) + "\",";
    out += R"("topic":")" + RosSessionManager::json_escape(b.topic) + "\",";
    out += R"("field_path":")" + RosSessionManager::json_escape(b.field_path) + "\"";
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_expression
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_expression(const ExpressionEntry& e)
{
    std::string out = "{";
    out += R"("expression":")" + json_escape(e.expression) + "\",";
    out += R"("label":")" + json_escape(e.label) + "\",";
    out += "\"subplot_slot\":" + std::to_string(e.subplot_slot) + ",";
    out += "\"bindings\":[";
    for (size_t i = 0; i < e.bindings.size(); ++i)
    {
        if (i > 0)
            out += ",";
        out += serialize_var_binding(e.bindings[i]);
    }
    out += "]}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_preset
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_preset(const ExpressionPresetEntry& e)
{
    std::string out = "{";
    out += R"("name":")" + json_escape(e.name) + "\",";
    out += R"("expression":")" + json_escape(e.expression) + "\",";
    out += "\"variables\":[";
    for (size_t i = 0; i < e.variables.size(); ++i)
    {
        if (i > 0)
            out += ",";
        out += "\"" + json_escape(e.variables[i]) + "\"";
    }
    out += "]}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_display
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_display(const DisplaySessionEntry& e)
{
    std::string out = "{";
    out += R"("type_id":")" + json_escape(e.type_id) + "\",";
    out += R"("topic":")" + json_escape(e.topic) + "\",";
    out += "\"enabled\":" + std::string(e.enabled ? "true" : "false") + ",";
    out += R"("config_blob":")" + json_escape(e.config_blob) + "\"";
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// serialize_panels
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_panels(const PanelVisibility& p)
{
    return to_json(p).dump();
}

// ---------------------------------------------------------------------------
// serialize (full session)
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize(const RosSession& session)
{
    RosSession normalized = session;
    normalized.version    = SESSION_FORMAT_VERSION;
    return serialize_session_v2_json(normalized).dump(2) + "\n";
}

// ===========================================================================
// JSON deserialization
// ===========================================================================

// ---------------------------------------------------------------------------
// extract_array — pull the JSON array body for a given key
// ---------------------------------------------------------------------------

std::string RosSessionManager::extract_array(const std::string& json, const std::string& key)
{
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return {};

    pos += pattern.size();
    // Skip whitespace and ':'
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '[')
        return {};

    // Collect until matching ']'
    int    depth = 0;
    size_t start = pos;
    while (pos < json.size())
    {
        char c = json[pos];
        if (c == '[')
            ++depth;
        else if (c == ']')
        {
            --depth;
            if (depth == 0)
            {
                ++pos;
                break;
            }
        }
        else if (c == '"')
        {
            ++pos;
            while (pos < json.size() && json[pos] != '"')
            {
                if (json[pos] == '\\')
                    ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

// ---------------------------------------------------------------------------
// split_objects — split a "[{...},{...}]" string into object strings
// ---------------------------------------------------------------------------

std::vector<std::string> RosSessionManager::split_objects(const std::string& array_body)
{
    std::vector<std::string> result;
    size_t                   pos = 0;
    // Skip leading '['
    while (pos < array_body.size() && array_body[pos] != '{')
        ++pos;

    while (pos < array_body.size())
    {
        if (array_body[pos] != '{')
        {
            ++pos;
            continue;
        }
        int    depth = 0;
        size_t start = pos;
        while (pos < array_body.size())
        {
            char c = array_body[pos];
            if (c == '{')
                ++depth;
            else if (c == '}')
            {
                --depth;
                if (depth == 0)
                {
                    ++pos;
                    break;
                }
            }
            else if (c == '"')
            {
                ++pos;
                while (pos < array_body.size() && array_body[pos] != '"')
                {
                    if (array_body[pos] == '\\')
                        ++pos;
                    ++pos;
                }
            }
            ++pos;
        }
        result.push_back(array_body.substr(start, pos - start));
    }
    return result;
}

// ---------------------------------------------------------------------------
// extract_nested_object — find the value object for a given key
// ---------------------------------------------------------------------------
static std::string extract_nested_object(const std::string& json, const std::string& key)
{
    std::string pattern = "\"" + key + "\"";
    auto        pos     = json.find(pattern);
    if (pos == std::string::npos)
        return {};

    pos += pattern.size();
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
    {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '{')
        return {};

    int    depth = 0;
    size_t start = pos;
    while (pos < json.size())
    {
        char c = json[pos];
        if (c == '{')
            ++depth;
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                ++pos;
                break;
            }
        }
        else if (c == '"')
        {
            ++pos;
            while (pos < json.size() && json[pos] != '"')
            {
                if (json[pos] == '\\')
                    ++pos;
                ++pos;
            }
        }
        ++pos;
    }
    return json.substr(start, pos - start);
}

// ---------------------------------------------------------------------------
// extract_string_array — pull ["a","b",...] for a given key
// ---------------------------------------------------------------------------
static std::vector<std::string> extract_string_array(const std::string& json,
                                                     const std::string& key)
{
    // Inline the array body extraction (avoids calling private static member).
    std::string body;
    {
        std::string pattern = "\"" + key + "\"";
        auto        pos     = json.find(pattern);
        if (pos != std::string::npos)
        {
            pos += pattern.size();
            while (
                pos < json.size()
                && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ':'))
            {
                ++pos;
            }
            if (pos < json.size() && json[pos] == '[')
            {
                int    depth = 0;
                size_t start = pos;
                while (pos < json.size())
                {
                    char c = json[pos];
                    if (c == '[')
                        ++depth;
                    else if (c == ']')
                    {
                        --depth;
                        if (depth == 0)
                        {
                            ++pos;
                            break;
                        }
                    }
                    else if (c == '"')
                    {
                        ++pos;
                        while (pos < json.size() && json[pos] != '"')
                        {
                            if (json[pos] == '\\')
                                ++pos;
                            ++pos;
                        }
                    }
                    ++pos;
                }
                body = json.substr(start, pos - start);
            }
        }
    }
    if (body.empty())
        return {};

    std::vector<std::string> result;
    size_t                   pos = 0;
    while (pos < body.size())
    {
        if (body[pos] != '"')
        {
            ++pos;
            continue;
        }
        ++pos;
        std::string val;
        while (pos < body.size() && body[pos] != '"')
        {
            if (body[pos] == '\\' && pos + 1 < body.size())
            {
                ++pos;
                switch (body[pos])
                {
                    case '"':
                        val += '"';
                        break;
                    case '\\':
                        val += '\\';
                        break;
                    case 'n':
                        val += '\n';
                        break;
                    case 't':
                        val += '\t';
                        break;
                    default:
                        val += body[pos];
                        break;
                }
            }
            else
            {
                val += body[pos];
            }
            ++pos;
        }
        ++pos;   // closing quote
        result.push_back(std::move(val));
    }
    return result;
}

// ---------------------------------------------------------------------------
// deserialize_subscription
// ---------------------------------------------------------------------------

SubscriptionEntry RosSessionManager::deserialize_subscription(const std::string& json)
{
    SubscriptionEntry e;
    e.topic         = json_get_string(json, "topic");
    e.field_path    = json_get_string(json, "field_path");
    e.type_name     = json_get_string(json, "type_name");
    e.subplot_slot  = json_get_int(json, "subplot_slot", 0);
    e.time_window_s = json_get_double(json, "time_window_s", 0.0);
    e.scroll_paused = json_get_bool(json, "scroll_paused", false);
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_expression
// ---------------------------------------------------------------------------

ExpressionEntry RosSessionManager::deserialize_expression(const std::string& json)
{
    ExpressionEntry e;
    e.expression   = json_get_string(json, "expression");
    e.label        = json_get_string(json, "label");
    e.subplot_slot = json_get_int(json, "subplot_slot", 0);

    std::string bindings_body = extract_array(json, "bindings");
    for (const auto& obj : split_objects(bindings_body))
    {
        ExpressionEntry::VarBinding b;
        b.variable   = json_get_string(obj, "variable");
        b.topic      = json_get_string(obj, "topic");
        b.field_path = json_get_string(obj, "field_path");
        if (!b.variable.empty())
        {
            e.bindings.push_back(std::move(b));
        }
    }
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_preset
// ---------------------------------------------------------------------------

ExpressionPresetEntry RosSessionManager::deserialize_preset(const std::string& json)
{
    ExpressionPresetEntry e;
    e.name       = json_get_string(json, "name");
    e.expression = json_get_string(json, "expression");
    e.variables  = extract_string_array(json, "variables");
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_display
// ---------------------------------------------------------------------------

DisplaySessionEntry RosSessionManager::deserialize_display(const std::string& json)
{
    DisplaySessionEntry e;
    e.type_id     = json_get_string(json, "type_id");
    e.topic       = json_get_string(json, "topic");
    e.enabled     = json_get_bool(json, "enabled", true);
    e.config_blob = json_get_string(json, "config_blob");
    return e;
}

// ---------------------------------------------------------------------------
// deserialize_panels
// ---------------------------------------------------------------------------

PanelVisibility RosSessionManager::deserialize_panels(const std::string& json)
{
    PanelVisibility p = default_panel_visibility();
    if (json.empty())
        return p;

    try
    {
        return panel_visibility_from_json(nlohmann::json::parse(json));
    }
    catch (...)
    {
        // Fall back to legacy flat-object parsing for hand-rolled v1 sessions.
    }

    static const std::pair<const char*, bool> kLegacyDefaults[] = {
        {"topic_list", true},
        {"topic_echo", false},
        {"topic_stats", true},
        {"plot_area", true},
        {"expression_editor", false},
        {"bag_info", false},
        {"bag_playback", false},
        {"log_viewer", false},
        {"diagnostics", false},
        {"node_graph", false},
        {"displays_panel", false},
        {"scene_viewport", false},
        {"inspector_panel", false},
        {"tf_tree", false},
        {"param_editor", false},
        {"service_caller", false},
    };

    for (const auto& [key, default_val] : kLegacyDefaults)
        p.by_id[panel_legacy_key_to_id_impl(key)] = json_get_bool(json, key, default_val);

    p.nav_rail = json_get_bool(json, "nav_rail", true);
    return p;
}

static TopicMonitorState deserialize_topic_monitor(const std::string& json)
{
    TopicMonitorState state;
    state.show_type = RosSessionManager::json_get_bool(json, "show_type", true);
    state.show_hz   = RosSessionManager::json_get_bool(json, "show_hz", true);
    state.show_pubs = RosSessionManager::json_get_bool(json, "show_pubs", true);
    state.show_subs = RosSessionManager::json_get_bool(json, "show_subs", true);
    state.show_bw   = RosSessionManager::json_get_bool(json, "show_bw", true);
    return state;
}

// ---------------------------------------------------------------------------
// deserialize (full session)
// ---------------------------------------------------------------------------

bool RosSessionManager::deserialize(const std::string& text,
                                    RosSession&        out,
                                    std::string&       error_out)
{
    if (text.empty())
    {
        error_out = "empty input";
        return false;
    }

    int ver = json_get_int(text, "version", 1);
    if (ver > SESSION_FORMAT_VERSION)
    {
        error_out = "session format version " + std::to_string(ver) + " is newer than supported ("
                    + std::to_string(SESSION_FORMAT_VERSION) + ")";
        return false;
    }

    if (ver <= 1)
    {
        out           = RosSession{};
        out.version   = 1;
        out.node_name = json_get_string(text, "node_name");
        out.node_ns   = json_get_string(text, "node_ns");
        out.layout    = json_get_string(text, "layout");
        if (out.layout.empty())
            out.layout = "default";
        out.subplot_rows      = json_get_int(text, "subplot_rows", 1);
        out.subplot_cols      = json_get_int(text, "subplot_cols", 1);
        out.time_window_s     = json_get_double(text, "time_window_s", 30.0);
        out.pruning_enabled   = json_get_bool(text, "pruning_enabled", true);
        out.prune_buffer_s    = json_get_double(text, "prune_buffer_s", 20.0);
        out.nav_rail_expanded = json_get_bool(text, "nav_rail_expanded", false);
        out.nav_rail_width    = json_get_double(text, "nav_rail_width", 220.0);
        out.fixed_frame       = json_get_string(text, "fixed_frame");
        out.saved_at          = json_get_string(text, "saved_at");
        out.description       = json_get_string(text, "description");

        std::string subs_body = extract_array(text, "subscriptions");
        for (const auto& obj : split_objects(subs_body))
        {
            auto e = deserialize_subscription(obj);
            if (!e.topic.empty())
            {
                out.subscriptions.push_back(std::move(e));
            }
        }

        std::string expr_body = extract_array(text, "expressions");
        for (const auto& obj : split_objects(expr_body))
        {
            auto e = deserialize_expression(obj);
            if (!e.expression.empty())
            {
                out.expressions.push_back(std::move(e));
            }
        }

        std::string presets_body = extract_array(text, "expression_presets");
        for (const auto& obj : split_objects(presets_body))
        {
            auto e = deserialize_preset(obj);
            if (!e.name.empty())
            {
                out.expression_presets.push_back(std::move(e));
            }
        }

        std::string displays_body = extract_array(text, "displays");
        for (const auto& obj : split_objects(displays_body))
        {
            auto e = deserialize_display(obj);
            if (!e.type_id.empty())
            {
                out.displays.push_back(std::move(e));
            }
        }

        std::string panels_obj = extract_nested_object(text, "panels");
        out.panels             = deserialize_panels(panels_obj.empty() ? "{}" : panels_obj);

        std::string topic_monitor_obj = extract_nested_object(text, "topic_monitor");
        if (!topic_monitor_obj.empty())
        {
            out.topic_monitor = deserialize_topic_monitor(topic_monitor_obj);
        }

        out.imgui_ini_data = json_get_string(text, "imgui_layout");
        return true;
    }

    try
    {
        const json root = json::parse(text);
        return deserialize_session_v2_json(root, out, error_out);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        error_out = e.what();
        return false;
    }
    catch (const nlohmann::json::type_error& e)
    {
        error_out = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// serialize_recent
// ---------------------------------------------------------------------------

std::string RosSessionManager::serialize_recent(const std::vector<RecentEntry>& entries)
{
    std::string out = "[\n";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0)
            out += ",\n";
        const auto& e = entries[i];
        out += R"(  {"path":")" + json_escape(e.path)
               + "\","
                 "\"node\":\""
               + json_escape(e.node)
               + "\","
                 "\"saved_at\":\""
               + json_escape(e.saved_at) + "\"}";
    }
    out += "\n]\n";
    return out;
}

// ---------------------------------------------------------------------------
// deserialize_recent
// ---------------------------------------------------------------------------

std::vector<RecentEntry> RosSessionManager::deserialize_recent(const std::string& json)
{
    std::vector<RecentEntry> result;
    // The recent file is a top-level array — parse objects directly.
    for (const auto& obj : split_objects(json))
    {
        RecentEntry e;
        e.path     = json_get_string(obj, "path");
        e.node     = json_get_string(obj, "node");
        e.saved_at = json_get_string(obj, "saved_at");
        if (!e.path.empty())
        {
            result.push_back(std::move(e));
        }
    }
    return result;
}

// ===========================================================================
// File I/O helpers
// ===========================================================================

bool RosSessionManager::ensure_directory(const std::string& path)
{
    try
    {
        std::filesystem::path p(path);
        auto                  dir = p.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir))
        {
            std::filesystem::create_directories(dir);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool RosSessionManager::write_file(const std::string& path, const std::string& content)
{
    // Atomic write: write to a temp file then rename.
    std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!ofs.is_open())
            return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!ofs.good())
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        // Fallback: copy then remove (different filesystems).
        std::filesystem::copy_file(tmp,
                                   path,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        std::filesystem::remove(tmp, ec);
        return !ec;
    }
    return true;
}

bool RosSessionManager::read_file(const std::string& path, std::string& content_out)
{
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    if (!ifs.good() && !ifs.eof())
        return false;
    content_out = ss.str();
    return true;
}

namespace
{

bool same_subscription(const SubscriptionEntry& a, const SubscriptionEntry& b)
{
    return a.topic == b.topic && a.field_path == b.field_path && a.subplot_slot == b.subplot_slot;
}

bool same_expression(const ExpressionEntry& a, const ExpressionEntry& b)
{
    return a.expression == b.expression && a.subplot_slot == b.subplot_slot;
}

}   // namespace

RosSession merge_sessions(const RosSession&   base,
                          const RosSession&   incoming,
                          SessionMergeOptions options)
{
    RosSession out = base;

    if (options.subscriptions)
    {
        for (const auto& entry : incoming.subscriptions)
        {
            const bool exists = std::any_of(out.subscriptions.begin(),
                                            out.subscriptions.end(),
                                            [&](const SubscriptionEntry& existing)
                                            { return same_subscription(existing, entry); });
            if (!exists)
                out.subscriptions.push_back(entry);
        }
    }

    if (options.expressions)
    {
        for (const auto& entry : incoming.expressions)
        {
            const bool exists = std::any_of(out.expressions.begin(),
                                            out.expressions.end(),
                                            [&](const ExpressionEntry& existing)
                                            { return same_expression(existing, entry); });
            if (!exists)
                out.expressions.push_back(entry);
        }
    }

    if (options.expression_presets)
    {
        for (const auto& preset : incoming.expression_presets)
        {
            const bool exists = std::any_of(out.expression_presets.begin(),
                                            out.expression_presets.end(),
                                            [&](const ExpressionPresetEntry& existing)
                                            { return existing.name == preset.name; });
            if (!exists)
                out.expression_presets.push_back(preset);
        }
    }

    if (options.displays)
    {
        for (const auto& display : incoming.displays)
        {
            const bool exists = std::any_of(
                out.displays.begin(),
                out.displays.end(),
                [&](const DisplaySessionEntry& existing)
                { return existing.type_id == display.type_id && existing.topic == display.topic; });
            if (!exists)
                out.displays.push_back(display);
        }
    }

    return out;
}

}   // namespace spectra::adapters::ros2
