#include "workspace.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <sstream>

#include "figure_serializer.hpp"

namespace spectra
{

// ─── Simple JSON writer ──────────────────────────────────────────────────────

static std::string escape_json(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
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

static std::string encode_base64(std::string_view input)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3)
    {
        const auto     a    = static_cast<unsigned char>(input[i]);
        const auto     b    = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
        const auto     c    = i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;
        const uint32_t bits = (static_cast<uint32_t>(a) << 16) | (static_cast<uint32_t>(b) << 8)
                              | static_cast<uint32_t>(c);
        output.push_back(alphabet[(bits >> 18) & 0x3f]);
        output.push_back(alphabet[(bits >> 12) & 0x3f]);
        output.push_back(i + 1 < input.size() ? alphabet[(bits >> 6) & 0x3f] : '=');
        output.push_back(i + 2 < input.size() ? alphabet[bits & 0x3f] : '=');
    }
    return output;
}

static bool decode_base64(std::string_view input, std::string& output)
{
    if (input.size() % 4 != 0)
        return false;

    auto value_of = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };

    output.clear();
    output.reserve((input.size() / 4) * 3);
    for (size_t i = 0; i < input.size(); i += 4)
    {
        const bool final_group = i + 4 == input.size();
        const bool pad2        = input[i + 2] == '=';
        const bool pad3        = input[i + 3] == '=';
        if ((!final_group && (pad2 || pad3)) || (pad2 && !pad3))
            return false;

        const int a = value_of(input[i]);
        const int b = value_of(input[i + 1]);
        const int c = pad2 ? 0 : value_of(input[i + 2]);
        const int d = pad3 ? 0 : value_of(input[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return false;

        const uint32_t bits = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12)
                              | (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
        output.push_back(static_cast<char>((bits >> 16) & 0xff));
        if (!pad2)
            output.push_back(static_cast<char>((bits >> 8) & 0xff));
        if (!pad3)
            output.push_back(static_cast<char>(bits & 0xff));
    }
    return true;
}

// static void write_indent(std::ostringstream& os, int depth) {
//     for (int i = 0; i < depth; ++i) os << "  ";
// }

std::string Workspace::serialize_json(const WorkspaceData& data)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": " << data.version << ",\n";
    os << R"(  "theme_name": ")" << escape_json(data.theme_name) << "\",\n";
    os << "  \"active_figure_index\": " << data.active_figure_index << ",\n";

    // Panels
    os << "  \"panels\": {\n";
    os << "    \"inspector_visible\": " << (data.panels.inspector_visible ? "true" : "false")
       << ",\n";
    os << "    \"inspector_width\": " << data.panels.inspector_width << ",\n";
    os << "    \"nav_rail_expanded\": " << (data.panels.nav_rail_expanded ? "true" : "false")
       << "\n";
    os << "  },\n";

    // Figures
    os << "  \"figures\": [\n";
    for (size_t fi = 0; fi < data.figures.size(); ++fi)
    {
        const auto& fig = data.figures[fi];
        os << "    {\n";
        os << "      \"figure_id\": " << fig.figure_id << ",\n";
        os << R"(      "title": ")" << escape_json(fig.title) << "\",\n";
        os << "      \"width\": " << fig.width << ",\n";
        os << "      \"height\": " << fig.height << ",\n";
        os << "      \"grid_rows\": " << fig.grid_rows << ",\n";
        os << "      \"grid_cols\": " << fig.grid_cols << ",\n";
        os << "      \"is_modified\": " << (fig.is_modified ? "true" : "false") << ",\n";
        os << R"(      "custom_tab_title": ")" << escape_json(fig.custom_tab_title) << "\",\n";

        // Axes
        os << "      \"axes\": [\n";
        for (size_t ai = 0; ai < fig.axes.size(); ++ai)
        {
            const auto& ax = fig.axes[ai];
            os << "        {\n";
            os << "          \"x_min\": " << ax.x_min << ",\n";
            os << "          \"x_max\": " << ax.x_max << ",\n";
            os << "          \"y_min\": " << ax.y_min << ",\n";
            os << "          \"y_max\": " << ax.y_max << ",\n";
            os << "          \"auto_fit\": " << (ax.auto_fit ? "true" : "false") << ",\n";
            os << "          \"grid_visible\": " << (ax.grid_visible ? "true" : "false") << ",\n";
            os << R"(          "x_label": ")" << escape_json(ax.x_label) << "\",\n";
            os << R"(          "y_label": ")" << escape_json(ax.y_label) << "\",\n";
            os << R"(          "title": ")" << escape_json(ax.title) << "\",\n";
            os << "          \"is_3d\": " << (ax.is_3d ? "true" : "false") << "\n";
            os << "        }";
            if (ai + 1 < fig.axes.size())
                os << ",";
            os << "\n";
        }
        os << "      ],\n";

        // 3D axes state
        os << "      \"axes_3d\": [\n";
        for (size_t a3i = 0; a3i < fig.axes_3d.size(); ++a3i)
        {
            const auto& a3 = fig.axes_3d[a3i];
            os << "        {\n";
            os << "          \"axes_index\": " << a3.axes_index << ",\n";
            os << "          \"z_min\": " << a3.z_min << ",\n";
            os << "          \"z_max\": " << a3.z_max << ",\n";
            os << R"(          "z_label": ")" << escape_json(a3.z_label) << "\",\n";
            os << R"(          "camera_state": ")" << escape_json(a3.camera_state) << "\",\n";
            os << "          \"grid_planes\": " << a3.grid_planes << ",\n";
            os << "          \"show_bounding_box\": " << (a3.show_bounding_box ? "true" : "false")
               << ",\n";
            os << "          \"lighting_enabled\": " << (a3.lighting_enabled ? "true" : "false")
               << ",\n";
            os << "          \"light_dir_x\": " << a3.light_dir_x << ",\n";
            os << "          \"light_dir_y\": " << a3.light_dir_y << ",\n";
            os << "          \"light_dir_z\": " << a3.light_dir_z << "\n";
            os << "        }";
            if (a3i + 1 < fig.axes_3d.size())
                os << ",";
            os << "\n";
        }
        os << "      ],\n";

        // Series
        os << "      \"series\": [\n";
        for (size_t si = 0; si < fig.series.size(); ++si)
        {
            const auto& s = fig.series[si];
            os << "        {\n";
            os << R"(          "name": ")" << escape_json(s.name) << "\",\n";
            os << R"(          "type": ")" << escape_json(s.type) << "\",\n";
            os << "          \"color_r\": " << s.color_r << ",\n";
            os << "          \"color_g\": " << s.color_g << ",\n";
            os << "          \"color_b\": " << s.color_b << ",\n";
            os << "          \"color_a\": " << s.color_a << ",\n";
            os << "          \"line_width\": " << s.line_width << ",\n";
            os << "          \"marker_size\": " << s.marker_size << ",\n";
            os << "          \"visible\": " << (s.visible ? "true" : "false") << ",\n";
            os << "          \"point_count\": " << s.point_count << ",\n";
            os << "          \"opacity\": " << s.opacity << ",\n";
            os << "          \"line_style\": " << s.line_style << ",\n";
            os << "          \"marker_style\": " << s.marker_style << ",\n";
            os << "          \"colormap_type\": " << s.colormap_type << ",\n";
            os << "          \"ambient\": " << s.ambient << ",\n";
            os << "          \"specular\": " << s.specular << ",\n";
            os << "          \"shininess\": " << s.shininess << ",\n";
            os << "          \"dash_pattern\": [";
            for (size_t di = 0; di < s.dash_pattern.size(); ++di)
            {
                if (di > 0)
                    os << ", ";
                os << s.dash_pattern[di];
            }
            os << "]\n";
            os << "        }";
            if (si + 1 < fig.series.size())
                os << ",";
            os << "\n";
        }
        os << "      ],\n";
        os << R"(      "snapshot_base64": ")" << escape_json(fig.snapshot_base64) << "\",\n";
        os << R"(      "timeline_json": ")" << escape_json(fig.timeline_json) << "\"\n";

        os << "    }";
        if (fi + 1 < data.figures.size())
            os << ",";
        os << "\n";
    }
    os << "  ],\n";

    // Interaction state
    os << "  \"interaction\": {\n";
    os << "    \"crosshair_enabled\": " << (data.interaction.crosshair_enabled ? "true" : "false")
       << ",\n";
    os << "    \"tooltip_enabled\": " << (data.interaction.tooltip_enabled ? "true" : "false")
       << ",\n";
    os << "    \"markers\": [\n";
    for (size_t mi = 0; mi < data.interaction.markers.size(); ++mi)
    {
        const auto& m = data.interaction.markers[mi];
        os << "      {\n";
        os << "        \"data_x\": " << m.data_x << ",\n";
        os << "        \"data_y\": " << m.data_y << ",\n";
        os << R"(        "series_label": ")" << escape_json(m.series_label) << "\",\n";
        os << "        \"point_index\": " << m.point_index << ",\n";
        os << "        \"axes_index\": " << m.axes_index << "\n";
        os << "      }";
        if (mi + 1 < data.interaction.markers.size())
            os << ",";
        os << "\n";
    }
    os << "    ],\n";

    // Annotations
    os << "    \"annotations\": [\n";
    for (size_t ai = 0; ai < data.interaction.annotations.size(); ++ai)
    {
        const auto& a = data.interaction.annotations[ai];
        os << "      {\n";
        os << "        \"data_x\": " << a.data_x << ",\n";
        os << "        \"data_y\": " << a.data_y << ",\n";
        os << R"(        "text": ")" << escape_json(a.text) << "\",\n";
        os << "        \"color_r\": " << a.color.r << ",\n";
        os << "        \"color_g\": " << a.color.g << ",\n";
        os << "        \"color_b\": " << a.color.b << ",\n";
        os << "        \"color_a\": " << a.color.a << ",\n";
        os << "        \"offset_x\": " << a.offset_x << ",\n";
        os << "        \"offset_y\": " << a.offset_y << ",\n";
        os << "        \"axes_index\": " << a.axes_index << "\n";
        os << "      }";
        if (ai + 1 < data.interaction.annotations.size())
            os << ",";
        os << "\n";
    }
    os << "    ]\n";
    os << "  },\n";

    // Undo metadata
    os << "  \"undo_count\": " << data.undo_count << ",\n";
    os << "  \"redo_count\": " << data.redo_count << ",\n";

    // v3: Axis link state
    os << R"(  "axis_link_state": ")" << escape_json(data.axis_link_state) << "\",\n";

    // v3: Data transform pipelines
    os << "  \"transforms\": [\n";
    for (size_t ti = 0; ti < data.transforms.size(); ++ti)
    {
        const auto& t = data.transforms[ti];
        os << "    {\n";
        os << "      \"figure_index\": " << t.figure_index << ",\n";
        os << "      \"axes_index\": " << t.axes_index << ",\n";
        os << "      \"series_index\": " << t.series_index << ",\n";
        os << "      \"all_visible\": " << (t.all_visible ? "true" : "false") << ",\n";
        os << "      \"axes_only\": " << (t.axes_only ? "true" : "false") << ",\n";
        os << R"(      "target": ")" << escape_json(t.target) << "\",\n";
        os << R"(      "name": ")" << escape_json(t.name) << "\",\n";
        os << "      \"steps\": [\n";
        for (size_t si = 0; si < t.steps.size(); ++si)
        {
            const auto& s = t.steps[si];
            os << "        {\"type\": " << s.type << ", \"param\": " << s.param
               << ", \"enabled\": " << (s.enabled ? "true" : "false") << R"(, "name": ")"
               << escape_json(s.name) << R"(", "source": ")" << escape_json(s.source)
               << "\", \"params_version\": " << s.params_version
               << ", \"scale_factor\": " << s.scale_factor
               << ", \"offset_value\": " << s.offset_value << ", \"clamp_min\": " << s.clamp_min
               << ", \"clamp_max\": " << s.clamp_max << ", \"log_base\": " << s.log_base
               << ", \"skip_nan\": " << (s.skip_nan ? "true" : "false")
               << ", \"fft_db\": " << (s.fft_db ? "true" : "false")
               << ", \"fft_sample_rate\": " << s.fft_sample_rate << "}";
            if (si + 1 < t.steps.size())
                os << ",";
            os << "\n";
        }
        os << "      ]\n";
        os << "    }";
        if (ti + 1 < data.transforms.size())
            os << ",";
        os << "\n";
    }
    os << "  ],\n";

    // v3: Shortcut overrides
    os << "  \"shortcut_overrides\": [\n";
    for (size_t si = 0; si < data.shortcut_overrides.size(); ++si)
    {
        const auto& o = data.shortcut_overrides[si];
        os << R"(    {"command": ")" << escape_json(o.command_id) << R"(", "shortcut": ")"
           << escape_json(o.shortcut_str) << R"(", "removed": )" << (o.removed ? "true" : "false")
           << "}";
        if (si + 1 < data.shortcut_overrides.size())
            os << ",";
        os << "\n";
    }
    os << "  ],\n";

    // v3: Timeline state
    os << "  \"timeline\": {\n";
    os << "    \"playhead\": " << data.timeline.playhead << ",\n";
    os << "    \"duration\": " << data.timeline.duration << ",\n";
    os << "    \"fps\": " << data.timeline.fps << ",\n";
    os << "    \"loop_mode\": " << data.timeline.loop_mode << ",\n";
    os << "    \"loop_start\": " << data.timeline.loop_start << ",\n";
    os << "    \"loop_end\": " << data.timeline.loop_end << ",\n";
    os << "    \"playing\": " << (data.timeline.playing ? "true" : "false") << "\n";
    os << "  },\n";

    // v3: Plugin state
    os << R"(  "plugin_state": ")" << escape_json(data.plugin_state) << "\",\n";

    // v3: Data palette name
    os << R"(  "data_palette_name": ")" << escape_json(data.data_palette_name) << "\",\n";

    // v4: Mode transition state
    os << R"(  "mode_transition_state": ")" << escape_json(data.mode_transition_state) << "\",\n";

    // v5: Desktop layout state
    os << "  \"desktop_layout\": {\n";
    os << R"(    "provider": ")" << escape_json(data.desktop_layout.provider) << "\",\n";
    os << R"(    "provider_version": ")" << escape_json(data.desktop_layout.provider_version)
       << "\",\n";
    os << R"(    "main_window_state_base64": ")"
       << escape_json(data.desktop_layout.main_window_state_base64) << "\",\n";
    os << R"(    "main_window_geometry_base64": ")"
       << escape_json(data.desktop_layout.main_window_geometry_base64) << "\",\n";
    os << R"(    "main_window_split_layout": ")"
       << escape_json(data.desktop_layout.main_window_split_layout) << "\",\n";
    os << "    \"main_window_figure_ids\": [";
    for (size_t i = 0; i < data.desktop_layout.main_window_figure_ids.size(); ++i)
    {
        if (i > 0)
            os << ", ";
        os << data.desktop_layout.main_window_figure_ids[i];
    }
    os << "],\n";
    os << R"(    "provider_layout": ")" << escape_json(data.desktop_layout.provider_layout)
       << "\",\n";
    os << "    \"windows\": [\n";
    for (size_t wi = 0; wi < data.desktop_layout.windows.size(); ++wi)
    {
        const auto& w = data.desktop_layout.windows[wi];
        os << "      {\n";
        os << R"(        "state_base64": ")" << escape_json(w.state_base64) << "\",\n";
        os << R"(        "geometry_base64": ")" << escape_json(w.geometry_base64) << "\",\n";
        os << R"(        "title": ")" << escape_json(w.title) << "\",\n";
        os << R"(        "split_layout": ")" << escape_json(w.split_layout) << "\",\n";
        os << "        \"figure_ids\": [";
        for (size_t i = 0; i < w.figure_ids.size(); ++i)
        {
            if (i > 0)
                os << ", ";
            os << w.figure_ids[i];
        }
        os << "]\n";
        os << "      }";
        if (wi + 1 < data.desktop_layout.windows.size())
            os << ",";
        os << "\n";
    }
    os << "    ]\n";
    os << "  },\n";

    // Last export directory
    os << R"(  "last_export_dir": ")" << escape_json(data.last_export_dir) << "\"\n";
    os << "}\n";

    return os.str();
}

// ─── Simple JSON reader ──────────────────────────────────────────────────────

// Minimal JSON parser — handles the specific format we write.
// Not a general-purpose JSON parser.

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string unescape_json(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            switch (s[i + 1])
            {
                case '"':
                    out += '"';
                    ++i;
                    break;
                case '\\':
                    out += '\\';
                    ++i;
                    break;
                case 'n':
                    out += '\n';
                    ++i;
                    break;
                case 'r':
                    out += '\r';
                    ++i;
                    break;
                case 't':
                    out += '\t';
                    ++i;
                    break;
                default:
                    out += s[i];
                    break;
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

std::string Workspace::read_string_value(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return "";

    size_t end = pos + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\')
            break;
        ++end;
    }
    return unescape_json(json.substr(pos + 1, end - pos - 1));
}

double Workspace::read_number_value(const std::string& json,
                                    const std::string& key,
                                    double             default_val)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return default_val;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return default_val;

    // Skip whitespace
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        ++pos;

    // Read number
    size_t end = pos;
    while (end < json.size()
           && (json[end] == '-' || json[end] == '.' || json[end] == '+' || json[end] == 'e'
               || json[end] == 'E' || (json[end] >= '0' && json[end] <= '9')))
        ++end;

    if (end == pos)
        return default_val;
    try
    {
        return std::stod(json.substr(pos, end - pos));
    }
    catch (...)
    {
        return default_val;
    }
}

bool Workspace::read_bool_value(const std::string& json, const std::string& key, bool default_val)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return default_val;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return default_val;

    auto rest = trim(json.substr(pos + 1, 10));
    if (rest.substr(0, 4) == "true")
        return true;
    if (rest.substr(0, 5) == "false")
        return false;
    return default_val;
}

// Parse array of objects from JSON (finds [...] after key)
static std::vector<std::string> parse_json_array(const std::string& json, const std::string& key)
{
    std::vector<std::string> objects;
    std::string              search = "\"" + key + "\"";
    auto                     pos    = json.find(search);
    if (pos == std::string::npos)
        return objects;

    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return objects;

    // Find matching objects within the array
    int    depth     = 0;
    size_t obj_start = 0;
    for (size_t i = pos + 1; i < json.size(); ++i)
    {
        if (json[i] == '{')
        {
            if (depth == 0)
                obj_start = i;
            ++depth;
        }
        else if (json[i] == '}')
        {
            --depth;
            if (depth == 0)
            {
                objects.push_back(json.substr(obj_start, i - obj_start + 1));
            }
        }
        else if (json[i] == ']' && depth == 0)
        {
            break;
        }
    }
    return objects;
}

static std::vector<FigureId> parse_figure_id_array(const std::string& json, const std::string& key)
{
    std::vector<FigureId> result;
    const auto            key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos)
        return result;
    const auto begin = json.find('[', key_pos);
    const auto end   = begin == std::string::npos ? std::string::npos : json.find(']', begin + 1);
    if (begin == std::string::npos || end == std::string::npos)
        return result;

    std::istringstream values(json.substr(begin + 1, end - begin - 1));
    std::string        token;
    try
    {
        while (std::getline(values, token, ','))
        {
            token = trim(token);
            if (!token.empty())
                result.push_back(static_cast<FigureId>(std::stoull(token)));
        }
    }
    catch (...)
    {
        result.clear();
    }
    return result;
}

bool Workspace::deserialize_json(const std::string& json, WorkspaceData& data)
{
    data.version = static_cast<uint32_t>(read_number_value(json, "version", 1));
    if (data.version > WorkspaceData::FORMAT_VERSION)
        return false;

    data.theme_name = read_string_value(json, "theme_name");
    if (data.theme_name.empty())
        data.theme_name = "night";
    data.active_figure_index =
        static_cast<size_t>(read_number_value(json, "active_figure_index", 0));

    // Panels
    data.panels.inspector_visible = read_bool_value(json, "inspector_visible", true);
    data.panels.inspector_width =
        static_cast<float>(read_number_value(json, "inspector_width", 320.0));
    data.panels.nav_rail_expanded = read_bool_value(json, "nav_rail_expanded", false);

    // Figures
    auto fig_objects = parse_json_array(json, "figures");
    for (const auto& fig_json : fig_objects)
    {
        WorkspaceData::FigureState fig;
        fig.figure_id = static_cast<FigureId>(read_number_value(fig_json, "figure_id", 0));
        fig.title     = read_string_value(fig_json, "title");
        fig.width     = static_cast<uint32_t>(read_number_value(fig_json, "width", 1280));
        fig.height    = static_cast<uint32_t>(read_number_value(fig_json, "height", 720));
        fig.grid_rows = static_cast<int>(read_number_value(fig_json, "grid_rows", 1));
        fig.grid_cols = static_cast<int>(read_number_value(fig_json, "grid_cols", 1));

        // v2 fields (graceful defaults for v1 files)
        if (data.version >= 2)
        {
            fig.is_modified      = read_bool_value(fig_json, "is_modified", false);
            fig.custom_tab_title = read_string_value(fig_json, "custom_tab_title");
        }

        // Axes
        auto ax_objects = parse_json_array(fig_json, "axes");
        for (const auto& ax_json : ax_objects)
        {
            WorkspaceData::AxisState ax;
            ax.x_min        = static_cast<float>(read_number_value(ax_json, "x_min", 0));
            ax.x_max        = static_cast<float>(read_number_value(ax_json, "x_max", 1));
            ax.y_min        = static_cast<float>(read_number_value(ax_json, "y_min", 0));
            ax.y_max        = static_cast<float>(read_number_value(ax_json, "y_max", 1));
            ax.auto_fit     = read_bool_value(ax_json, "auto_fit", true);
            ax.grid_visible = read_bool_value(ax_json, "grid_visible", true);
            ax.x_label      = read_string_value(ax_json, "x_label");
            ax.y_label      = read_string_value(ax_json, "y_label");
            ax.title        = read_string_value(ax_json, "title");
            // v4 field
            if (data.version >= 4)
            {
                ax.is_3d = read_bool_value(ax_json, "is_3d", false);
            }
            fig.axes.push_back(std::move(ax));
        }

        // Series
        auto ser_objects = parse_json_array(fig_json, "series");
        for (const auto& ser_json : ser_objects)
        {
            WorkspaceData::SeriesState s;
            s.name        = read_string_value(ser_json, "name");
            s.type        = read_string_value(ser_json, "type");
            s.color_r     = static_cast<float>(read_number_value(ser_json, "color_r", 1));
            s.color_g     = static_cast<float>(read_number_value(ser_json, "color_g", 1));
            s.color_b     = static_cast<float>(read_number_value(ser_json, "color_b", 1));
            s.color_a     = static_cast<float>(read_number_value(ser_json, "color_a", 1));
            s.line_width  = static_cast<float>(read_number_value(ser_json, "line_width", 2));
            s.marker_size = static_cast<float>(read_number_value(ser_json, "marker_size", 6));
            s.visible     = read_bool_value(ser_json, "visible", true);
            s.point_count = static_cast<size_t>(read_number_value(ser_json, "point_count", 0));
            // v2 field
            s.opacity = static_cast<float>(read_number_value(ser_json, "opacity", 1.0));
            // v3 fields
            if (data.version >= 3)
            {
                s.line_style   = static_cast<int>(read_number_value(ser_json, "line_style", 1));
                s.marker_style = static_cast<int>(read_number_value(ser_json, "marker_style", 0));
                // Parse dash_pattern array
                auto dp_pos = ser_json.find("\"dash_pattern\"");
                if (dp_pos != std::string::npos)
                {
                    auto bracket     = ser_json.find('[', dp_pos);
                    auto end_bracket = ser_json.find(']', bracket);
                    if (bracket != std::string::npos && end_bracket != std::string::npos)
                    {
                        std::string dp_str =
                            ser_json.substr(bracket + 1, end_bracket - bracket - 1);
                        size_t p = 0;
                        while (p < dp_str.size())
                        {
                            while (p < dp_str.size() && (dp_str[p] == ' ' || dp_str[p] == ','))
                                ++p;
                            if (p >= dp_str.size())
                                break;
                            char* endp = nullptr;
                            float v    = std::strtof(dp_str.c_str() + p, &endp);
                            if (endp != dp_str.c_str() + p)
                            {
                                s.dash_pattern.push_back(v);
                                p = static_cast<size_t>(endp - dp_str.c_str());
                            }
                            else
                                break;
                        }
                    }
                }
            }
            // v4: 3D series fields
            if (data.version >= 4)
            {
                s.colormap_type = static_cast<int>(read_number_value(ser_json, "colormap_type", 0));
                s.ambient       = static_cast<float>(read_number_value(ser_json, "ambient", 0));
                s.specular      = static_cast<float>(read_number_value(ser_json, "specular", 0));
                s.shininess     = static_cast<float>(read_number_value(ser_json, "shininess", 0));
            }
            fig.series.push_back(std::move(s));
        }

        // v4: 3D axes state
        if (data.version >= 4)
        {
            auto a3_objects = parse_json_array(fig_json, "axes_3d");
            for (const auto& a3_json : a3_objects)
            {
                WorkspaceData::Axes3DState a3;
                a3.axes_index   = static_cast<size_t>(read_number_value(a3_json, "axes_index", 0));
                a3.z_min        = static_cast<float>(read_number_value(a3_json, "z_min", 0));
                a3.z_max        = static_cast<float>(read_number_value(a3_json, "z_max", 1));
                a3.z_label      = read_string_value(a3_json, "z_label");
                a3.camera_state = read_string_value(a3_json, "camera_state");
                a3.grid_planes  = static_cast<int>(read_number_value(a3_json, "grid_planes", 1));
                a3.show_bounding_box = read_bool_value(a3_json, "show_bounding_box", true);
                a3.lighting_enabled  = read_bool_value(a3_json, "lighting_enabled", true);
                a3.light_dir_x = static_cast<float>(read_number_value(a3_json, "light_dir_x", 1));
                a3.light_dir_y = static_cast<float>(read_number_value(a3_json, "light_dir_y", 1));
                a3.light_dir_z = static_cast<float>(read_number_value(a3_json, "light_dir_z", 1));
                fig.axes_3d.push_back(std::move(a3));
            }
        }

        fig.snapshot_base64 = read_string_value(fig_json, "snapshot_base64");
        fig.timeline_json   = read_string_value(fig_json, "timeline_json");

        data.figures.push_back(std::move(fig));
    }

    // v2: Interaction state
    if (data.version >= 2)
    {
        data.interaction.crosshair_enabled = read_bool_value(json, "crosshair_enabled", false);
        data.interaction.tooltip_enabled   = read_bool_value(json, "tooltip_enabled", true);

        // Parse markers array from interaction object
        auto interaction_pos = json.find("\"interaction\"");
        if (interaction_pos != std::string::npos)
        {
            // Extract the interaction object substring
            auto brace = json.find('{', interaction_pos);
            if (brace != std::string::npos)
            {
                int    depth = 0;
                size_t end   = brace;
                for (size_t i = brace; i < json.size(); ++i)
                {
                    if (json[i] == '{')
                        ++depth;
                    else if (json[i] == '}')
                    {
                        --depth;
                        if (depth == 0)
                        {
                            end = i;
                            break;
                        }
                    }
                }
                std::string interaction_json = json.substr(brace, end - brace + 1);
                auto        marker_objects   = parse_json_array(interaction_json, "markers");
                for (const auto& m_json : marker_objects)
                {
                    OverlaySnapshot::MarkerEntry m;
                    m.data_x       = static_cast<float>(read_number_value(m_json, "data_x", 0));
                    m.data_y       = static_cast<float>(read_number_value(m_json, "data_y", 0));
                    m.series_label = read_string_value(m_json, "series_label");
                    m.point_index =
                        static_cast<size_t>(read_number_value(m_json, "point_index", 0));
                    m.axes_index = static_cast<size_t>(read_number_value(m_json, "axes_index", 0));
                    data.interaction.markers.push_back(std::move(m));
                }

                // Parse annotations array
                auto annotation_objects = parse_json_array(interaction_json, "annotations");
                for (const auto& a_json : annotation_objects)
                {
                    OverlaySnapshot::AnnotationEntry a;
                    a.data_x     = static_cast<float>(read_number_value(a_json, "data_x", 0));
                    a.data_y     = static_cast<float>(read_number_value(a_json, "data_y", 0));
                    a.text       = read_string_value(a_json, "text");
                    a.color.r    = static_cast<float>(read_number_value(a_json, "color_r", 1));
                    a.color.g    = static_cast<float>(read_number_value(a_json, "color_g", 1));
                    a.color.b    = static_cast<float>(read_number_value(a_json, "color_b", 1));
                    a.color.a    = static_cast<float>(read_number_value(a_json, "color_a", 1));
                    a.offset_x   = static_cast<float>(read_number_value(a_json, "offset_x", 0));
                    a.offset_y   = static_cast<float>(read_number_value(a_json, "offset_y", -40));
                    a.axes_index = static_cast<size_t>(read_number_value(a_json, "axes_index", 0));
                    data.interaction.annotations.push_back(std::move(a));
                }
            }
        }

        data.undo_count = static_cast<size_t>(read_number_value(json, "undo_count", 0));
        data.redo_count = static_cast<size_t>(read_number_value(json, "redo_count", 0));
    }

    // v3 fields
    if (data.version >= 3)
    {
        data.axis_link_state   = read_string_value(json, "axis_link_state");
        data.data_palette_name = read_string_value(json, "data_palette_name");
        data.plugin_state      = read_string_value(json, "plugin_state");

        // Parse transforms array
        auto transform_objects = parse_json_array(json, "transforms");
        for (const auto& t_json : transform_objects)
        {
            WorkspaceData::TransformState ts;
            ts.figure_index   = static_cast<size_t>(read_number_value(t_json, "figure_index", 0));
            ts.axes_index     = static_cast<size_t>(read_number_value(t_json, "axes_index", 0));
            ts.series_index   = static_cast<size_t>(read_number_value(t_json, "series_index", 0));
            ts.all_visible    = read_bool_value(t_json, "all_visible", false);
            ts.axes_only      = read_bool_value(t_json, "axes_only", false);
            ts.target         = read_string_value(t_json, "target");
            ts.name           = read_string_value(t_json, "name");
            auto step_objects = parse_json_array(t_json, "steps");
            for (const auto& s_json : step_objects)
            {
                WorkspaceData::TransformState::Step step;
                step.type    = static_cast<int>(read_number_value(s_json, "type", 0));
                step.param   = static_cast<float>(read_number_value(s_json, "param", 0));
                step.enabled = read_bool_value(s_json, "enabled", true);
                step.name    = read_string_value(s_json, "name");
                step.source  = read_string_value(s_json, "source");
                step.params_version =
                    static_cast<int>(read_number_value(s_json, "params_version", 0));
                step.scale_factor =
                    static_cast<float>(read_number_value(s_json, "scale_factor", 1.0));
                step.offset_value =
                    static_cast<float>(read_number_value(s_json, "offset_value", 0.0));
                step.clamp_min = static_cast<float>(read_number_value(s_json, "clamp_min", 0.0));
                step.clamp_max = static_cast<float>(read_number_value(s_json, "clamp_max", 1.0));
                step.log_base  = static_cast<float>(read_number_value(s_json, "log_base", 10.0));
                step.skip_nan  = read_bool_value(s_json, "skip_nan", true);
                step.fft_db    = read_bool_value(s_json, "fft_db", false);
                step.fft_sample_rate =
                    static_cast<float>(read_number_value(s_json, "fft_sample_rate", 0.0));
                ts.steps.push_back(step);
            }
            data.transforms.push_back(std::move(ts));
        }

        // Parse shortcut overrides
        auto override_objects = parse_json_array(json, "shortcut_overrides");
        for (const auto& o_json : override_objects)
        {
            WorkspaceData::ShortcutOverride so;
            so.command_id   = read_string_value(o_json, "command");
            so.shortcut_str = read_string_value(o_json, "shortcut");
            so.removed      = read_bool_value(o_json, "removed", false);
            if (!so.command_id.empty())
            {
                data.shortcut_overrides.push_back(std::move(so));
            }
        }

        // Parse timeline state
        auto tl_pos = json.find("\"timeline\"");
        if (tl_pos != std::string::npos)
        {
            auto brace = json.find('{', tl_pos);
            if (brace != std::string::npos)
            {
                int    depth = 0;
                size_t end   = brace;
                for (size_t i = brace; i < json.size(); ++i)
                {
                    if (json[i] == '{')
                        ++depth;
                    else if (json[i] == '}')
                    {
                        --depth;
                        if (depth == 0)
                        {
                            end = i;
                            break;
                        }
                    }
                }
                std::string tl_json = json.substr(brace, end - brace + 1);
                data.timeline.playhead =
                    static_cast<float>(read_number_value(tl_json, "playhead", 0));
                data.timeline.duration =
                    static_cast<float>(read_number_value(tl_json, "duration", 10));
                data.timeline.fps = static_cast<float>(read_number_value(tl_json, "fps", 30));
                data.timeline.loop_mode =
                    static_cast<int>(read_number_value(tl_json, "loop_mode", 0));
                data.timeline.loop_start =
                    static_cast<float>(read_number_value(tl_json, "loop_start", 0));
                data.timeline.loop_end =
                    static_cast<float>(read_number_value(tl_json, "loop_end", 0));
                data.timeline.playing = read_bool_value(tl_json, "playing", false);
            }
        }
    }

    // v4 fields
    if (data.version >= 4)
    {
        data.mode_transition_state = read_string_value(json, "mode_transition_state");
    }

    // v5: Desktop layout state
    if (data.version >= 5)
    {
        // Find the desktop_layout object
        auto dl_pos = json.find("\"desktop_layout\"");
        if (dl_pos != std::string::npos)
        {
            auto brace = json.find('{', dl_pos);
            if (brace != std::string::npos)
            {
                int    depth = 0;
                size_t end   = brace;
                for (size_t i = brace; i < json.size(); ++i)
                {
                    if (json[i] == '{')
                        ++depth;
                    else if (json[i] == '}')
                    {
                        --depth;
                        if (depth == 0)
                        {
                            end = i;
                            break;
                        }
                    }
                }
                std::string dl_json = json.substr(brace, end - brace + 1);

                data.desktop_layout.provider = read_string_value(dl_json, "provider");
                data.desktop_layout.provider_version =
                    read_string_value(dl_json, "provider_version");
                data.desktop_layout.main_window_state_base64 =
                    read_string_value(dl_json, "main_window_state_base64");
                data.desktop_layout.main_window_geometry_base64 =
                    read_string_value(dl_json, "main_window_geometry_base64");
                data.desktop_layout.main_window_split_layout =
                    read_string_value(dl_json, "main_window_split_layout");
                data.desktop_layout.main_window_figure_ids =
                    parse_figure_id_array(dl_json, "main_window_figure_ids");
                data.desktop_layout.provider_layout = read_string_value(dl_json, "provider_layout");

                // Parse windows array
                auto win_objects = parse_json_array(dl_json, "windows");
                for (const auto& w_json : win_objects)
                {
                    WorkspaceData::DesktopLayoutState::WindowState ws;
                    ws.state_base64    = read_string_value(w_json, "state_base64");
                    ws.geometry_base64 = read_string_value(w_json, "geometry_base64");
                    ws.title           = read_string_value(w_json, "title");
                    ws.split_layout    = read_string_value(w_json, "split_layout");
                    ws.figure_ids      = parse_figure_id_array(w_json, "figure_ids");
                    data.desktop_layout.windows.push_back(std::move(ws));
                }
            }
        }
    }

    // Last export directory (versionless — graceful default for old files)
    data.last_export_dir = read_string_value(json, "last_export_dir");

    return true;
}

// ─── Save / Load ─────────────────────────────────────────────────────────────

bool Workspace::save(const std::string& path, const WorkspaceData& data)
{
    std::string   json = serialize_json(data);
    std::ofstream file(path);
    if (!file.is_open())
        return false;
    file << json;
    return file.good();
}

bool Workspace::load(const std::string& path, WorkspaceData& data)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string json = ss.str();

    if (json.empty())
        return false;
    return deserialize_json(json, data);
}

// ─── Capture / Apply ─────────────────────────────────────────────────────────

WorkspaceData Workspace::capture(const std::vector<Figure*>&         figures,
                                 size_t                              active_index,
                                 const std::string&                  theme_name,
                                 bool                                inspector_visible,
                                 float                               inspector_width,
                                 bool                                nav_rail_expanded,
                                 const std::vector<OverlaySnapshot>* overlays)
{
    WorkspaceData data;
    data.theme_name               = theme_name;
    data.active_figure_index      = active_index;
    data.panels.inspector_visible = inspector_visible;
    data.panels.inspector_width   = inspector_width;
    data.panels.nav_rail_expanded = nav_rail_expanded;

    for (size_t figure_index = 0; figure_index < figures.size(); ++figure_index)
    {
        const auto* fig = figures[figure_index];
        if (!fig)
            continue;

        WorkspaceData::FigureState fs;
        fs.title            = fig->tab_title();
        fs.custom_tab_title = fig->tab_title();
        fs.width            = fig->width();
        fs.height           = fig->height();
        fs.grid_rows        = fig->grid_rows();
        fs.grid_cols        = fig->grid_cols();

        std::string            snapshot;
        const OverlaySnapshot* overlay =
            overlays && figure_index < overlays->size() ? &(*overlays)[figure_index] : nullptr;
        if (FigureSerializer::serialize(*fig, snapshot, overlay))
            fs.snapshot_base64 = encode_base64(snapshot);

        // Capture axes: use all_axes() if populated (mixed/3D figures),
        // otherwise fall back to axes() (2D-only figures).
        const bool has_all_axes = !fig->all_axes().empty();

        // Helper lambda to capture a single AxesBase
        auto capture_axes_base = [&](const AxesBase* ax_base, size_t axes_idx)
        {
            if (!ax_base)
                return;
            WorkspaceData::AxisState as;

            if (auto* ax3d = dynamic_cast<const Axes3D*>(ax_base))
            {
                as.is_3d        = true;
                auto xlim       = ax3d->x_limits();
                auto ylim       = ax3d->y_limits();
                auto zlim       = ax3d->z_limits();
                as.x_min        = xlim.min;
                as.x_max        = xlim.max;
                as.y_min        = ylim.min;
                as.y_max        = ylim.max;
                as.auto_fit     = false;
                as.grid_visible = ax3d->grid_enabled();
                as.x_label      = ax3d->get_xlabel();
                as.y_label      = ax3d->get_ylabel();
                as.title        = ax3d->get_title();

                WorkspaceData::Axes3DState a3;
                a3.axes_index        = axes_idx;
                a3.z_min             = zlim.min;
                a3.z_max             = zlim.max;
                a3.z_label           = ax3d->get_zlabel();
                a3.camera_state      = ax3d->camera().serialize();
                a3.grid_planes       = static_cast<int>(ax3d->grid_planes());
                a3.show_bounding_box = ax3d->show_bounding_box();
                a3.lighting_enabled  = ax3d->lighting_enabled();
                auto ld              = ax3d->light_dir();
                a3.light_dir_x       = ld.x;
                a3.light_dir_y       = ld.y;
                a3.light_dir_z       = ld.z;
                fs.axes_3d.push_back(std::move(a3));
            }
            else if (auto* ax2d = dynamic_cast<const Axes*>(ax_base))
            {
                as.is_3d        = false;
                auto xlim       = ax2d->x_limits();
                auto ylim       = ax2d->y_limits();
                as.x_min        = xlim.min;
                as.x_max        = xlim.max;
                as.y_min        = ylim.min;
                as.y_max        = ylim.max;
                as.auto_fit     = false;
                as.grid_visible = ax2d->grid_enabled();
                as.x_label      = ax2d->get_xlabel();
                as.y_label      = ax2d->get_ylabel();
                as.title        = ax2d->get_title();
            }

            fs.axes.push_back(std::move(as));
        };

        if (has_all_axes)
        {
            size_t axes_idx = 0;
            for (const auto& ax_base : fig->all_axes())
            {
                capture_axes_base(ax_base.get(), axes_idx);
                ++axes_idx;
            }
        }
        else
        {
            size_t axes_idx = 0;
            for (const auto& ax : fig->axes())
            {
                capture_axes_base(ax.get(), axes_idx);
                ++axes_idx;
            }
        }

        // Capture series: iterate the same axes list used above
        auto capture_series_from = [&](const AxesBase* ax_base)
        {
            if (!ax_base)
                return;
            for (const auto& s : ax_base->series())
            {
                if (!s)
                    continue;
                WorkspaceData::SeriesState ss;
                ss.name    = s->label();
                ss.visible = s->visible();
                ss.color_r = s->color().r;
                ss.color_g = s->color().g;
                ss.color_b = s->color().b;
                ss.color_a = s->color().a;
                ss.opacity = s->opacity();

                if (auto* ls = dynamic_cast<LineSeries*>(s.get()))
                {
                    ss.type        = "line";
                    ss.line_width  = ls->width();
                    ss.point_count = ls->x_data().size();
                }
                else if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
                {
                    ss.type        = "scatter";
                    ss.marker_size = sc->size();
                    ss.point_count = sc->x_data().size();
                }
                else if (auto* ls3 = dynamic_cast<LineSeries3D*>(s.get()))
                {
                    ss.type        = "line3d";
                    ss.line_width  = ls3->width();
                    ss.point_count = ls3->point_count();
                }
                else if (auto* sc3 = dynamic_cast<ScatterSeries3D*>(s.get()))
                {
                    ss.type        = "scatter3d";
                    ss.marker_size = sc3->size();
                    ss.point_count = sc3->point_count();
                }
                else if (auto* sf = dynamic_cast<SurfaceSeries*>(s.get()))
                {
                    ss.type          = "surface";
                    ss.point_count   = sf->z_values().size();
                    ss.colormap_type = static_cast<int>(sf->colormap_type());
                    ss.ambient       = sf->ambient();
                    ss.specular      = sf->specular();
                    ss.shininess     = sf->shininess();
                }
                else if (auto* ms = dynamic_cast<MeshSeries*>(s.get()))
                {
                    ss.type        = "mesh";
                    ss.point_count = ms->vertex_count();
                    ss.ambient     = ms->ambient();
                    ss.specular    = ms->specular();
                    ss.shininess   = ms->shininess();
                }

                fs.series.push_back(std::move(ss));
            }
        };

        if (has_all_axes)
        {
            for (const auto& ax_base : fig->all_axes())
                capture_series_from(ax_base.get());
        }
        else
        {
            for (const auto& ax : fig->axes())
                capture_series_from(ax.get());
        }

        data.figures.push_back(std::move(fs));
    }

    return data;
}

bool Workspace::apply(const WorkspaceData& data, std::vector<Figure*>& figures)
{
    // Apply axis limits and grid state from workspace data to matching figures
    for (size_t fi = 0; fi < data.figures.size() && fi < figures.size(); ++fi)
    {
        const auto& fs  = data.figures[fi];
        auto*       fig = figures[fi];
        if (!fig)
            continue;

        // Apply axes: use all_axes() if populated, else axes()
        const bool has_all_axes = !fig->all_axes().empty();

        auto apply_to_axes_base = [&](AxesBase* ax_base, size_t ai)
        {
            if (!ax_base || ai >= fs.axes.size())
                return;
            const auto& as = fs.axes[ai];

            if (as.is_3d)
            {
                auto* ax3d = dynamic_cast<Axes3D*>(ax_base);
                if (!ax3d)
                    return;
                ax3d->xlim(as.x_min, as.x_max);
                ax3d->ylim(as.y_min, as.y_max);
                ax3d->set_grid_enabled(as.grid_visible);

                for (const auto& a3 : fs.axes_3d)
                {
                    if (a3.axes_index == ai)
                    {
                        ax3d->zlim(a3.z_min, a3.z_max);
                        ax3d->zlabel(a3.z_label);
                        if (!a3.camera_state.empty())
                            ax3d->camera().deserialize(a3.camera_state);
                        ax3d->set_grid_planes(a3.grid_planes);
                        ax3d->show_bounding_box(a3.show_bounding_box);
                        ax3d->set_lighting_enabled(a3.lighting_enabled);
                        ax3d->set_light_dir(a3.light_dir_x, a3.light_dir_y, a3.light_dir_z);
                        break;
                    }
                }
            }
            else
            {
                auto* ax2d = dynamic_cast<Axes*>(ax_base);
                if (!ax2d)
                    return;
                ax2d->xlim(as.x_min, as.x_max);
                ax2d->ylim(as.y_min, as.y_max);
                ax2d->set_grid_enabled(as.grid_visible);
            }
        };

        if (has_all_axes)
        {
            for (size_t ai = 0; ai < fs.axes.size() && ai < fig->all_axes().size(); ++ai)
                apply_to_axes_base(fig->all_axes()[ai].get(), ai);
        }
        else
        {
            for (size_t ai = 0; ai < fs.axes.size() && ai < fig->axes().size(); ++ai)
                apply_to_axes_base(fig->axes()[ai].get(), ai);
        }

        // Apply series visibility
        size_t si               = 0;
        auto   apply_series_vis = [&](AxesBase* ax_base)
        {
            if (!ax_base)
                return;
            for (auto& s : ax_base->series_mut())
            {
                if (!s || si >= fs.series.size())
                    return;
                s->visible(fs.series[si].visible);
                ++si;
            }
        };

        if (has_all_axes)
        {
            for (const auto& ax_base : fig->all_axes())
                apply_series_vis(ax_base.get());
        }
        else
        {
            for (const auto& ax : fig->axes())
                apply_series_vis(ax.get());
        }
    }
    return true;
}

bool Workspace::restore_figures(const WorkspaceData&                  data,
                                std::vector<std::unique_ptr<Figure>>& figures,
                                std::vector<OverlaySnapshot>*         overlays)
{
    std::vector<std::unique_ptr<Figure>> restored;
    std::vector<OverlaySnapshot>         restored_overlays;
    restored.reserve(data.figures.size());
    restored_overlays.reserve(data.figures.size());

    for (const auto& state : data.figures)
    {
        if (state.snapshot_base64.empty())
            return false;

        std::string bytes;
        if (!decode_base64(state.snapshot_base64, bytes))
            return false;

        auto            figure = std::make_unique<Figure>(FigureConfig{state.width, state.height});
        OverlaySnapshot overlay;
        if (!FigureSerializer::deserialize(bytes, *figure, &overlay))
            return false;
        figure->set_tab_title(state.custom_tab_title.empty() ? state.title
                                                             : state.custom_tab_title);
        restored.push_back(std::move(figure));
        restored_overlays.push_back(std::move(overlay));
    }

    figures = std::move(restored);
    if (overlays)
        *overlays = std::move(restored_overlays);
    return true;
}

// ─── Paths ───────────────────────────────────────────────────────────────────

std::string Workspace::default_path()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = std::getenv("USERPROFILE");
    if (!home)
        return "workspace.spectra";

    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "spectra";
    std::filesystem::create_directories(dir);
    return (dir / "workspace.spectra").string();
}

std::string Workspace::autosave_path()
{
    try
    {
        auto tmp = std::filesystem::temp_directory_path() / "spectra_autosave.spectra";
        return tmp.string();
    }
    catch (...)
    {
        return "spectra_autosave.spectra";
    }
}

// ─── Autosave ─────────────────────────────────────────────────────────────────

bool Workspace::maybe_autosave(const WorkspaceData& data, float interval_seconds)
{
    static auto last_autosave = std::chrono::steady_clock::now();
    auto        now           = std::chrono::steady_clock::now();
    float       elapsed       = std::chrono::duration<float>(now - last_autosave).count();
    if (elapsed < interval_seconds)
        return false;

    last_autosave = now;
    return save(autosave_path(), data);
}

bool Workspace::has_autosave()
{
    try
    {
        return std::filesystem::exists(autosave_path());
    }
    catch (...)
    {
        return false;
    }
}

void Workspace::clear_autosave()
{
    try
    {
        std::filesystem::remove(autosave_path());
    }
    catch (...)
    {
        // Ignore errors
    }
}

WorkspaceValidationResult validate_workspace_data(WorkspaceData& data)
{
    WorkspaceValidationResult result;

    // Version check
    if (data.version == 0 || data.version > WorkspaceData::FORMAT_VERSION)
    {
        result.errors.push_back("Unknown or future workspace version: "
                                + std::to_string(data.version));
        result.valid = false;
        return result;
    }

    // Validate each figure
    for (auto& fig : data.figures)
    {
        // Clamp dimensions to sensible range
        if (fig.width < 100 || fig.width > 16384)
        {
            result.warnings.push_back("Figure width " + std::to_string(fig.width)
                                      + " out of range, clamping to 1280");
            fig.width       = 1280;
            result.repaired = true;
        }
        if (fig.height < 100 || fig.height > 16384)
        {
            result.warnings.push_back("Figure height " + std::to_string(fig.height)
                                      + " out of range, clamping to 720");
            fig.height      = 720;
            result.repaired = true;
        }

        // Validate axes
        for (auto& ax : fig.axes)
        {
            // x range sanity
            if (ax.x_min >= ax.x_max && !ax.auto_fit)
            {
                result.warnings.push_back("Axis has invalid x range [" + std::to_string(ax.x_min)
                                          + ", " + std::to_string(ax.x_max)
                                          + "], resetting to auto-fit");
                ax.auto_fit     = true;
                result.repaired = true;
            }
            // y range sanity
            if (ax.y_min >= ax.y_max && !ax.auto_fit)
            {
                result.warnings.push_back("Axis has invalid y range [" + std::to_string(ax.y_min)
                                          + ", " + std::to_string(ax.y_max)
                                          + "], resetting to auto-fit");
                ax.auto_fit     = true;
                result.repaired = true;
            }
        }

        // Validate series types
        static const std::unordered_set<std::string> kValidTypes = {"line",
                                                                    "scatter",
                                                                    "line3d",
                                                                    "scatter3d",
                                                                    "surface",
                                                                    "mesh",
                                                                    "boxplot",
                                                                    "violin",
                                                                    "histogram",
                                                                    "bar"};
        for (auto& ser : fig.series)
        {
            bool valid_type = ser.type.empty() || kValidTypes.contains(ser.type);
            if (!valid_type)
            {
                result.warnings.push_back("Unknown series type '" + ser.type
                                          + "', treating as 'line'");
                ser.type        = "line";
                result.repaired = true;
            }

            // Color channels must be [0, 1]
            auto  clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
            float cr      = clamp01(ser.color_r);
            float cg      = clamp01(ser.color_g);
            float cb      = clamp01(ser.color_b);
            float ca      = clamp01(ser.color_a);
            if (cr != ser.color_r || cg != ser.color_g || cb != ser.color_b || ca != ser.color_a)
            {
                result.warnings.push_back("Series '" + ser.name
                                          + "' has color channel out of [0,1], clamping");
                ser.color_r     = cr;
                ser.color_g     = cg;
                ser.color_b     = cb;
                ser.color_a     = ca;
                result.repaired = true;
            }
        }
    }

    return result;
}

}   // namespace spectra
