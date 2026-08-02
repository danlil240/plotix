#include "axis_link.hpp"

#include <algorithm>
#include <sstream>

namespace spectra
{

// ─── LinkGroup ───────────────────────────────────────────────────────────────

bool LinkGroup::contains(const Axes* ax) const
{
    return std::find(members.begin(), members.end(), ax) != members.end();
}

void LinkGroup::remove(const Axes* ax)
{
    members.erase(std::remove(members.begin(), members.end(), const_cast<Axes*>(ax)),
                  members.end());
}

// ─── AxisLinkManager ─────────────────────────────────────────────────────────

AxisLinkManager::AxisLinkManager()  = default;
AxisLinkManager::~AxisLinkManager() = default;

// ─── Group lifecycle ─────────────────────────────────────────────────────────

LinkGroupId AxisLinkManager::create_group(const std::string& name, LinkAxis axis)
{
    std::lock_guard lock(mutex_);
    LinkGroupId     id = next_id_++;
    LinkGroup       group;
    group.id   = id;
    group.axis = axis;
    group.name = name;
    // Assign a color based on group ID for visual distinction
    static constexpr Color group_colors[] = {
        {0.34f, 0.65f, 0.96f},   // blue
        {0.96f, 0.49f, 0.31f},   // orange
        {0.30f, 0.78f, 0.47f},   // green
        {0.89f, 0.35f, 0.40f},   // red
        {0.58f, 0.40f, 0.74f},   // purple
        {0.09f, 0.75f, 0.81f},   // cyan
        {0.89f, 0.47f, 0.76f},   // pink
        {0.74f, 0.74f, 0.13f},   // olive
    };
    group.color = group_colors[(id - 1) % 8];
    groups_[id] = std::move(group);
    return id;
}

void AxisLinkManager::remove_group(LinkGroupId id)
{
    std::lock_guard lock(mutex_);
    groups_.erase(id);
    notify();
}

void AxisLinkManager::clear()
{
    std::lock_guard lock(mutex_);
    const bool      changed = !groups_.empty() || !groups_3d_.empty() || shared_cursor_.valid;
    groups_.clear();
    groups_3d_.clear();
    shared_cursor_ = {};
    next_id_       = 1;
    if (changed)
        notify();
}

// ─── Membership ──────────────────────────────────────────────────────────────

void AxisLinkManager::add_to_group(LinkGroupId id, Axes* ax)
{
    if (!ax)
        return;
    std::lock_guard lock(mutex_);
    auto            it = groups_.find(id);
    if (it == groups_.end())
        return;
    auto& group = it->second;
    if (!group.contains(ax))
    {
        group.members.push_back(ax);
        notify();
    }
}

void AxisLinkManager::remove_from_group(LinkGroupId id, Axes* ax)
{
    if (!ax)
        return;
    std::lock_guard lock(mutex_);
    auto            it = groups_.find(id);
    if (it == groups_.end())
        return;
    it->second.remove(ax);
    // Remove empty groups
    if (it->second.members.empty())
    {
        groups_.erase(it);
    }
    notify();
}

void AxisLinkManager::remove_from_all(Axes* ax)
{
    if (!ax)
        return;
    std::lock_guard          lock(mutex_);
    bool                     changed = false;
    std::vector<LinkGroupId> empty_groups;
    for (auto& [id, group] : groups_)
    {
        if (group.contains(ax))
        {
            group.remove(ax);
            changed = true;
            if (group.members.empty())
            {
                empty_groups.push_back(id);
            }
        }
    }
    for (auto id : empty_groups)
    {
        groups_.erase(id);
    }
    if (changed)
    {
        notify();
    }
}

// ─── Convenience ─────────────────────────────────────────────────────────────

LinkGroupId AxisLinkManager::link(Axes* a, Axes* b, LinkAxis axis)
{
    if (!a || !b || a == b)
        return 0;
    std::lock_guard lock(mutex_);

    // Check if they already share a group with the same axis type
    for (auto& [id, group] : groups_)
    {
        if (group.axis == axis && group.contains(a) && group.contains(b))
        {
            return id;   // Already linked
        }
        // If a is in a group with this axis type, add b to it
        if (group.axis == axis && group.contains(a) && !group.contains(b))
        {
            group.members.push_back(b);
            notify();
            return id;
        }
        // If b is in a group with this axis type, add a to it
        if (group.axis == axis && group.contains(b) && !group.contains(a))
        {
            group.members.push_back(a);
            notify();
            return id;
        }
    }

    // Create a new group
    LinkGroupId id = next_id_++;
    LinkGroup   group;
    group.id                              = id;
    group.axis                            = axis;
    group.name                            = "Link " + std::to_string(id);
    static constexpr Color group_colors[] = {
        {0.34f, 0.65f, 0.96f},
        {0.96f, 0.49f, 0.31f},
        {0.30f, 0.78f, 0.47f},
        {0.89f, 0.35f, 0.40f},
        {0.58f, 0.40f, 0.74f},
        {0.09f, 0.75f, 0.81f},
        {0.89f, 0.47f, 0.76f},
        {0.74f, 0.74f, 0.13f},
    };
    group.color = group_colors[(id - 1) % 8];
    group.members.push_back(a);
    group.members.push_back(b);
    groups_[id] = std::move(group);
    notify();
    return id;
}

void AxisLinkManager::unlink(Axes* ax)
{
    remove_from_all(ax);
}

// ─── Propagation ─────────────────────────────────────────────────────────────

void AxisLinkManager::propagate_from(Axes* source, AxisLimits /*old_xlim*/, AxisLimits /*old_ylim*/)
{
    if (!source)
        return;
    std::lock_guard lock(mutex_);
    if (propagating_)
        return;   // Guard re-entrant calls
    propagating_ = true;

    auto new_xlim = source->x_limits();
    auto new_ylim = source->y_limits();

    for (auto& [id, group] : groups_)
    {
        if (!group.contains(source))
            continue;

        for (Axes* peer : group.members)
        {
            if (peer == source)
                continue;

            if (has_flag(group.axis, LinkAxis::X))
            {
                peer->xlim(new_xlim.min, new_xlim.max);
            }
            if (has_flag(group.axis, LinkAxis::Y))
            {
                peer->ylim(new_ylim.min, new_ylim.max);
            }
        }
    }

    propagating_ = false;
}

void AxisLinkManager::propagate_zoom(Axes* source, float data_x, float data_y, float factor)
{
    if (!source)
        return;
    std::lock_guard lock(mutex_);
    if (propagating_)
        return;
    propagating_ = true;

    for (auto& [id, group] : groups_)
    {
        if (!group.contains(source))
            continue;

        for (Axes* peer : group.members)
        {
            if (peer == source)
                continue;

            if (has_flag(group.axis, LinkAxis::X))
            {
                auto   xlim     = peer->x_limits();
                double new_xmin = data_x + (xlim.min - data_x) * factor;
                double new_xmax = data_x + (xlim.max - data_x) * factor;
                peer->xlim(new_xmin, new_xmax);
            }
            if (has_flag(group.axis, LinkAxis::Y))
            {
                auto   ylim     = peer->y_limits();
                double new_ymin = data_y + (ylim.min - data_y) * factor;
                double new_ymax = data_y + (ylim.max - data_y) * factor;
                peer->ylim(new_ymin, new_ymax);
            }
        }
    }

    propagating_ = false;
}

void AxisLinkManager::propagate_pan(Axes* source, float dx_data, float dy_data)
{
    if (!source)
        return;
    std::lock_guard lock(mutex_);
    if (propagating_)
        return;
    propagating_ = true;

    for (auto& [id, group] : groups_)
    {
        if (!group.contains(source))
            continue;

        for (Axes* peer : group.members)
        {
            if (peer == source)
                continue;

            if (has_flag(group.axis, LinkAxis::X))
            {
                auto xlim = peer->x_limits();
                peer->xlim(xlim.min + dx_data, xlim.max + dx_data);
            }
            if (has_flag(group.axis, LinkAxis::Y))
            {
                auto ylim = peer->y_limits();
                peer->ylim(ylim.min + dy_data, ylim.max + dy_data);
            }
        }
    }

    propagating_ = false;
}

void AxisLinkManager::propagate_limits(Axes* source, AxisLimits new_xlim, AxisLimits new_ylim)
{
    if (!source)
        return;
    std::lock_guard lock(mutex_);
    if (propagating_)
        return;
    propagating_ = true;

    for (auto& [id, group] : groups_)
    {
        if (!group.contains(source))
            continue;

        for (Axes* peer : group.members)
        {
            if (peer == source)
                continue;

            if (has_flag(group.axis, LinkAxis::X))
            {
                peer->xlim(new_xlim.min, new_xlim.max);
            }
            if (has_flag(group.axis, LinkAxis::Y))
            {
                peer->ylim(new_ylim.min, new_ylim.max);
            }
        }
    }

    propagating_ = false;
}

// ─── Queries ─────────────────────────────────────────────────────────────────

std::vector<LinkGroupId> AxisLinkManager::groups_for(const Axes* ax) const
{
    std::lock_guard          lock(mutex_);
    std::vector<LinkGroupId> result;
    for (const auto& [id, group] : groups_)
    {
        if (group.contains(ax))
        {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<Axes*> AxisLinkManager::linked_peers(const Axes* ax) const
{
    std::lock_guard    lock(mutex_);
    std::vector<Axes*> result;
    for (const auto& [id, group] : groups_)
    {
        if (!group.contains(ax))
            continue;
        for (Axes* member : group.members)
        {
            if (member != ax)
            {
                // Avoid duplicates (axes could be in multiple groups)
                if (std::find(result.begin(), result.end(), member) == result.end())
                {
                    result.push_back(member);
                }
            }
        }
    }
    return result;
}

bool AxisLinkManager::is_linked(const Axes* ax) const
{
    std::lock_guard lock(mutex_);
    for (const auto& [id, group] : groups_)
    {
        if (group.contains(ax) && group.members.size() > 1)
        {
            return true;
        }
    }
    return false;
}

const LinkGroup* AxisLinkManager::group(LinkGroupId id) const
{
    std::lock_guard lock(mutex_);
    auto            it = groups_.find(id);
    return it != groups_.end() ? &it->second : nullptr;
}

const std::unordered_map<LinkGroupId, LinkGroup>& AxisLinkManager::groups() const
{
    // Note: caller must hold awareness that this is a snapshot reference.
    // For thread-safe iteration, use groups_for() or linked_peers() instead.
    return groups_;
}

size_t AxisLinkManager::group_count() const
{
    std::lock_guard lock(mutex_);
    return groups_.size();
}

size_t AxisLinkManager::group_3d_count() const
{
    std::lock_guard lock(mutex_);
    return groups_3d_.size();
}

// ─── Serialization ───────────────────────────────────────────────────────────

std::string AxisLinkManager::serialize(const AxesToIndex&   mapper,
                                       const Axes3DToIndex& mapper_3d) const
{
    std::lock_guard lock(mutex_);
    if (groups_.empty() && groups_3d_.empty())
        return "{}";

    std::ostringstream ss;
    ss << "{\"groups\":[";
    bool first = true;
    for (const auto& [id, group] : groups_)
    {
        if (!first)
            ss << ",";
        first = false;
        ss << "{\"id\":" << id << R"(,"name":")" << group.name << "\""
           << ",\"axis\":" << static_cast<int>(group.axis) << ",\"members\":[";
        bool first_m = true;
        for (const Axes* ax : group.members)
        {
            int idx = mapper(ax);
            if (idx < 0)
                continue;   // Skip unmapped axes
            if (!first_m)
                ss << ",";
            first_m = false;
            ss << idx;
        }
        ss << "]}";
    }
    ss << "],\"groups3d\":[";
    first = true;
    for (const auto& [id, group] : groups_3d_)
    {
        if (!first)
            ss << ",";
        first = false;
        ss << "{\"id\":" << id << R"(,"name":")" << group.name << "\""
           << ",\"axis\":" << static_cast<int>(group.axis) << ",\"members\":[";
        bool first_member = true;
        for (const Axes3D* axes : group.members)
        {
            const int index = mapper_3d ? mapper_3d(axes) : -1;
            if (index < 0)
                continue;
            if (!first_member)
                ss << ",";
            first_member = false;
            ss << index;
        }
        ss << "]}";
    }
    ss << "]}";
    return ss.str();
}

void AxisLinkManager::deserialize(const std::string&   json,
                                  const IndexToAxes&   mapper,
                                  const IndexToAxes3D& mapper_3d)
{
    std::lock_guard lock(mutex_);
    groups_.clear();
    groups_3d_.clear();
    next_id_ = 1;

    if (json.empty() || json == "{}")
        return;

    // Minimal JSON parser — find groups array
    auto find_array = [&](const std::string& key) -> std::pair<size_t, size_t>
    {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos)
            return {std::string::npos, std::string::npos};
        pos = json.find('[', pos);
        if (pos == std::string::npos)
            return {std::string::npos, std::string::npos};
        // Find matching ]
        int    depth = 1;
        size_t end   = pos + 1;
        while (end < json.size() && depth > 0)
        {
            if (json[end] == '[')
                depth++;
            else if (json[end] == ']')
                depth--;
            end++;
        }
        return {pos + 1, end - 1};
    };

    auto [groups_start, groups_end] = find_array("groups");

    // Parse each group object
    size_t pos = groups_start == std::string::npos ? groups_end : groups_start;
    while (pos < groups_end)
    {
        auto obj_start = json.find('{', pos);
        if (obj_start == std::string::npos || obj_start >= groups_end)
            break;

        // Find matching }
        int    depth   = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < json.size() && depth > 0)
        {
            if (json[obj_end] == '{')
                depth++;
            else if (json[obj_end] == '}')
                depth--;
            obj_end++;
        }

        std::string obj = json.substr(obj_start, obj_end - obj_start);

        // Extract fields
        auto extract_int = [&](const std::string& obj_str, const std::string& key) -> int
        {
            auto kpos = obj_str.find("\"" + key + "\"");
            if (kpos == std::string::npos)
                return -1;
            kpos = obj_str.find(':', kpos);
            if (kpos == std::string::npos)
                return -1;
            kpos++;
            while (kpos < obj_str.size() && obj_str[kpos] == ' ')
                kpos++;
            return std::atoi(obj_str.c_str() + kpos);
        };

        auto extract_string = [&](const std::string& obj_str, const std::string& key) -> std::string
        {
            auto kpos = obj_str.find("\"" + key + "\"");
            if (kpos == std::string::npos)
                return "";
            kpos = obj_str.find(':', kpos);
            if (kpos == std::string::npos)
                return "";
            auto qstart = obj_str.find('"', kpos + 1);
            if (qstart == std::string::npos)
                return "";
            auto qend = obj_str.find('"', qstart + 1);
            if (qend == std::string::npos)
                return "";
            return obj_str.substr(qstart + 1, qend - qstart - 1);
        };

        int         gid   = extract_int(obj, "id");
        std::string gname = extract_string(obj, "name");
        int         gaxis = extract_int(obj, "axis");

        if (gid <= 0 || gaxis < 1 || gaxis > 3)
        {
            pos = obj_end;
            continue;
        }

        LinkGroup group;
        group.id   = static_cast<LinkGroupId>(gid);
        group.name = gname;
        group.axis = static_cast<LinkAxis>(gaxis);

        // Parse members array
        auto mem_pos = obj.find("\"members\"");
        if (mem_pos != std::string::npos)
        {
            auto arr_start = obj.find('[', mem_pos);
            auto arr_end   = obj.find(']', arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos)
            {
                std::string        arr = obj.substr(arr_start + 1, arr_end - arr_start - 1);
                std::istringstream iss(arr);
                std::string        token;
                while (std::getline(iss, token, ','))
                {
                    int   idx = std::atoi(token.c_str());
                    Axes* ax  = mapper(idx);
                    if (ax)
                    {
                        group.members.push_back(ax);
                    }
                }
            }
        }

        static constexpr Color group_colors[] = {
            {0.34f, 0.65f, 0.96f},
            {0.96f, 0.49f, 0.31f},
            {0.30f, 0.78f, 0.47f},
            {0.89f, 0.35f, 0.40f},
            {0.58f, 0.40f, 0.74f},
            {0.09f, 0.75f, 0.81f},
            {0.89f, 0.47f, 0.76f},
            {0.74f, 0.74f, 0.13f},
        };
        group.color = group_colors[(group.id - 1) % 8];

        groups_[group.id] = std::move(group);
        if (static_cast<LinkGroupId>(gid) >= next_id_)
        {
            next_id_ = static_cast<LinkGroupId>(gid) + 1;
        }

        pos = obj_end;
    }

    auto [groups3d_start, groups3d_end] = find_array("groups3d");
    pos = groups3d_start == std::string::npos ? groups3d_end : groups3d_start;
    while (pos < groups3d_end)
    {
        const auto object_start = json.find('{', pos);
        if (object_start == std::string::npos || object_start >= groups3d_end)
            break;
        int    depth      = 1;
        size_t object_end = object_start + 1;
        while (object_end < json.size() && depth > 0)
        {
            if (json[object_end] == '{')
                ++depth;
            else if (json[object_end] == '}')
                --depth;
            ++object_end;
        }
        const std::string object      = json.substr(object_start, object_end - object_start);
        auto              extract_int = [&object](const std::string& key)
        {
            size_t key_pos = object.find("\"" + key + "\"");
            if (key_pos == std::string::npos)
                return -1;
            key_pos = object.find(':', key_pos);
            return key_pos == std::string::npos ? -1 : std::atoi(object.c_str() + key_pos + 1);
        };
        auto extract_string = [&object](const std::string& key)
        {
            size_t key_pos = object.find("\"" + key + "\"");
            if (key_pos == std::string::npos)
                return std::string{};
            const size_t begin = object.find('"', object.find(':', key_pos) + 1);
            if (begin == std::string::npos)
                return std::string{};
            const size_t end = object.find('"', begin + 1);
            return end == std::string::npos ? std::string{}
                                            : object.substr(begin + 1, end - begin - 1);
        };
        const int id   = extract_int("id");
        const int axis = extract_int("axis");
        if (id <= 0 || axis < 1 || axis > static_cast<int>(LinkAxis::All))
        {
            pos = object_end;
            continue;
        }
        Link3DGroup group;
        group.id                 = static_cast<LinkGroupId>(id);
        group.name               = extract_string("name");
        group.axis               = static_cast<LinkAxis>(axis);
        const size_t members_key = object.find("\"members\"");
        if (members_key != std::string::npos)
        {
            const size_t begin = object.find('[', members_key);
            const size_t end   = object.find(']', begin);
            if (begin != std::string::npos && end != std::string::npos)
            {
                std::istringstream members(object.substr(begin + 1, end - begin - 1));
                std::string        token;
                while (std::getline(members, token, ','))
                    if (Axes3D* axes = mapper_3d ? mapper_3d(std::atoi(token.c_str())) : nullptr)
                        group.members.push_back(axes);
            }
        }
        static constexpr Color group_colors[] = {
            {0.34f, 0.65f, 0.96f},
            {0.96f, 0.49f, 0.31f},
            {0.30f, 0.78f, 0.47f},
            {0.89f, 0.35f, 0.40f},
            {0.58f, 0.40f, 0.74f},
            {0.09f, 0.75f, 0.81f},
            {0.89f, 0.47f, 0.76f},
            {0.74f, 0.74f, 0.13f},
        };
        group.color          = group_colors[(group.id - 1) % 8];
        groups_3d_[group.id] = std::move(group);
        next_id_             = std::max(next_id_, static_cast<LinkGroupId>(id) + 1);
        pos                  = object_end;
    }
}

// ─── 3D axis linking ─────────────────────────────────────────────────────────

LinkGroupId AxisLinkManager::link_3d(Axes3D* a, Axes3D* b, LinkAxis axis)
{
    if (!a || !b || a == b)
        return 0;
    std::lock_guard lock(mutex_);

    // Check if they already share a 3D group with the same axis flags
    for (auto& [id, group] : groups_3d_)
    {
        if (group.axis != axis)
            continue;
        if (group.contains(a) && group.contains(b))
            return id;
        if (group.contains(a) && !group.contains(b))
        {
            group.members.push_back(b);
            notify();
            return id;
        }
        if (group.contains(b) && !group.contains(a))
        {
            group.members.push_back(a);
            notify();
            return id;
        }
    }

    // Create new 3D group
    LinkGroupId id = next_id_++;
    Link3DGroup group;
    group.id             = id;
    group.axis           = axis;
    std::string axis_str = has_flag(axis, LinkAxis::Z) && !has_flag(axis, LinkAxis::X) ? "Z"
                           : (axis == LinkAxis::All)                                   ? "XYZ"
                                                                                       : "3D";
    group.name           = axis_str + " Link " + std::to_string(id);
    static constexpr Color group_colors[] = {
        {0.34f, 0.65f, 0.96f},
        {0.96f, 0.49f, 0.31f},
        {0.30f, 0.78f, 0.47f},
        {0.89f, 0.35f, 0.40f},
        {0.58f, 0.40f, 0.74f},
        {0.09f, 0.75f, 0.81f},
        {0.89f, 0.47f, 0.76f},
        {0.74f, 0.74f, 0.13f},
    };
    group.color = group_colors[(id - 1) % 8];
    group.members.push_back(a);
    group.members.push_back(b);
    groups_3d_[id] = std::move(group);
    notify();
    return id;
}

void AxisLinkManager::add_to_group_3d(LinkGroupId id, Axes3D* ax)
{
    if (!ax)
        return;
    std::lock_guard lock(mutex_);
    auto            it = groups_3d_.find(id);
    if (it == groups_3d_.end())
        return;
    if (!it->second.contains(ax))
    {
        it->second.members.push_back(ax);
        notify();
    }
}

void AxisLinkManager::remove_from_all_3d(Axes3D* ax)
{
    if (!ax)
        return;
    std::lock_guard          lock(mutex_);
    bool                     changed = false;
    std::vector<LinkGroupId> empty_groups;
    for (auto& [id, group] : groups_3d_)
    {
        if (group.contains(ax))
        {
            group.remove(ax);
            changed = true;
            if (group.members.empty())
                empty_groups.push_back(id);
        }
    }
    for (auto gid : empty_groups)
        groups_3d_.erase(gid);
    if (changed)
        notify();
}

void AxisLinkManager::propagate_from_3d(Axes3D* source)
{
    if (!source)
        return;
    std::lock_guard lock(mutex_);
    if (propagating_)
        return;
    propagating_ = true;

    auto new_xlim = source->x_limits();
    auto new_ylim = source->y_limits();
    auto new_zlim = source->z_limits();

    for (auto& [id, group] : groups_3d_)
    {
        if (!group.contains(source))
            continue;
        for (Axes3D* peer : group.members)
        {
            if (peer == source)
                continue;
            if (has_flag(group.axis, LinkAxis::X))
                peer->xlim(new_xlim.min, new_xlim.max);
            if (has_flag(group.axis, LinkAxis::Y))
                peer->ylim(new_ylim.min, new_ylim.max);
            if (has_flag(group.axis, LinkAxis::Z))
                peer->zlim(new_zlim.min, new_zlim.max);
        }
    }

    propagating_ = false;
}

// ─── Shared cursor (Agent E — Week 10) ───────────────────────────────────────

void AxisLinkManager::update_shared_cursor(const SharedCursor& cursor)
{
    std::lock_guard lock(mutex_);
    shared_cursor_ = cursor;
}

SharedCursor AxisLinkManager::shared_cursor_for(const Axes* ax) const
{
    if (!ax)
        return {};
    std::lock_guard lock(mutex_);

    if (!shared_cursor_.valid || !shared_cursor_.source_axes)
        return {};

    // Source axes always sees its own cursor
    if (shared_cursor_.source_axes == ax)
        return shared_cursor_;

    // Check if ax and the source are in any common group
    for (const auto& [_, group] : groups_)
    {
        if (group.contains(ax) && group.contains(shared_cursor_.source_axes))
        {
            return shared_cursor_;
        }
    }
    return {};
}

void AxisLinkManager::clear_shared_cursor()
{
    std::lock_guard lock(mutex_);
    shared_cursor_ = {};
}

// ─── Internal ────────────────────────────────────────────────────────────────

void AxisLinkManager::notify()
{
    if (on_change_)
    {
        on_change_();
    }
}

std::vector<Axes*> AxisLinkManager::peers_in_group_unlocked(const LinkGroup& group,
                                                            const Axes*      ax) const
{
    std::vector<Axes*> result;
    for (Axes* member : group.members)
    {
        if (member != ax)
        {
            result.push_back(member);
        }
    }
    return result;
}

}   // namespace spectra
