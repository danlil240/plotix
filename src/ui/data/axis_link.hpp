#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

// Which axis dimensions are linked within a group
enum class LinkAxis : uint8_t
{
    X    = 0x01,
    Y    = 0x02,
    Both = 0x03,   // X | Y (2D)
    Z    = 0x04,
    All  = 0x07,   // X | Y | Z (3D)
};

inline LinkAxis operator|(LinkAxis a, LinkAxis b)
{
    return static_cast<LinkAxis>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline LinkAxis operator&(LinkAxis a, LinkAxis b)
{
    return static_cast<LinkAxis>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool has_flag(LinkAxis val, LinkAxis flag)
{
    return (static_cast<uint8_t>(val) & static_cast<uint8_t>(flag)) != 0;
}

// A unique identifier for a link group
using LinkGroupId = uint32_t;

// A group of axes that are linked together
struct LinkGroup
{
    LinkGroupId        id   = 0;
    LinkAxis           axis = LinkAxis::X;
    std::string        name;                   // User-visible label (e.g. "Group 1")
    Color              color = colors::blue;   // Visual indicator color
    std::vector<Axes*> members;

    bool contains(const Axes* ax) const;
    void remove(const Axes* ax);
};

// A group of 3D axes that are linked together (xlim/ylim/zlim)
struct Link3DGroup
{
    LinkGroupId id   = 0;
    LinkAxis    axis = LinkAxis::All;   // X=xlim only, Y=ylim only, Z=zlim only, All=all three
    std::string name;
    Color       color = colors::blue;
    std::vector<Axes3D*> members;

    bool contains(const Axes3D* ax) const
    {
        return std::find(members.begin(), members.end(), ax) != members.end();
    }
    void remove(const Axes3D* ax)
    {
        members.erase(std::remove(members.begin(), members.end(), const_cast<Axes3D*>(ax)),
                      members.end());
    }
};

// Shared cursor state — represents a cursor position broadcast across linked axes.
// Stored in data coordinates of the source axes.
struct SharedCursor
{
    bool        valid       = false;
    float       data_x      = 0.0f;
    float       data_y      = 0.0f;
    double      screen_x    = 0.0;
    double      screen_y    = 0.0;
    const Axes* source_axes = nullptr;   // Which axes generated this cursor
};

// Callback fired when linked axes limits change (for UI redraw notification)
using LinkChangeCallback = std::function<void()>;

// Manages axis linking across subplots and figures.
// Thread-safe: all public methods lock an internal mutex.
//
// Usage:
//   auto group_id = mgr.create_group("Shared X", LinkAxis::X);
//   mgr.add_to_group(group_id, &axes1);
//   mgr.add_to_group(group_id, &axes2);
//   // Now when axes1 X-limits change, axes2 X-limits follow.
//
// Propagation:
//   Call propagate_from(source_axes, old_xlim, old_ylim) after mutating
//   the source's limits. All other members in the same group(s) will be
//   updated to match the source's new limits (for the linked dimensions).
//
class AxisLinkManager
{
   public:
    AxisLinkManager();
    ~AxisLinkManager();

    // ── Group lifecycle ──────────────────────────────────────────────

    // Create a new link group. Returns its ID.
    LinkGroupId create_group(const std::string& name, LinkAxis axis);

    // Remove a group entirely (unlinks all members).
    void remove_group(LinkGroupId id);
    void clear();

    // ── Membership ───────────────────────────────────────────────────

    // Add an axes to an existing group.
    void add_to_group(LinkGroupId id, Axes* ax);

    // Remove an axes from a specific group.
    void remove_from_group(LinkGroupId id, Axes* ax);

    // Remove an axes from ALL groups (e.g. when axes is destroyed).
    void remove_from_all(Axes* ax);

    // ── Convenience: link two axes together ──────────────────────────

    // Link two axes on the given dimension(s). Creates a new group if
    // they don't already share one, or adds to an existing group.
    LinkGroupId link(Axes* a, Axes* b, LinkAxis axis);

    // Unlink an axes from all groups.
    void unlink(Axes* ax);

    // ── 3D axis linking ───────────────────────────────────────────────

    // Link two 3D axes together. axis controls which limits propagate (X/Y/Z/All).
    LinkGroupId link_3d(Axes3D* a, Axes3D* b, LinkAxis axis = LinkAxis::All);

    // Add a 3D axes to an existing group.
    void add_to_group_3d(LinkGroupId id, Axes3D* ax);

    // Remove a 3D axes from all 3D groups.
    void remove_from_all_3d(Axes3D* ax);

    // Propagate 3D limit changes from source to all linked 3D peers.
    void propagate_from_3d(Axes3D* source);

    // ── Propagation ──────────────────────────────────────────────────

    // After mutating source's limits, call this to propagate to linked
    // axes. old_xlim/old_ylim are the limits BEFORE the mutation.
    // The source's current limits are read and applied to all linked peers.
    void propagate_from(Axes* source, AxisLimits old_xlim, AxisLimits old_ylim);

    // Propagate a zoom centered on (data_x, data_y) with the given
    // factor to all axes linked to source. The source itself is NOT
    // modified (caller already did that).
    void propagate_zoom(Axes* source, float data_x, float data_y, float factor);

    // Propagate a pan delta (in data-space) to all axes linked to source.
    void propagate_pan(Axes* source, float dx_data, float dy_data);

    // Propagate absolute limits to all axes linked to source.
    void propagate_limits(Axes* source, AxisLimits new_xlim, AxisLimits new_ylim);

    // ── Queries ──────────────────────────────────────────────────────

    // Get all groups an axes belongs to.
    std::vector<LinkGroupId> groups_for(const Axes* ax) const;

    // Get all axes linked to the given one (across all groups, excluding itself).
    std::vector<Axes*> linked_peers(const Axes* ax) const;

    // Is this axes linked to anything?
    bool is_linked(const Axes* ax) const;

    // Get a group by ID (returns nullptr if not found).
    const LinkGroup* group(LinkGroupId id) const;

    // Get all groups.
    const std::unordered_map<LinkGroupId, LinkGroup>& groups() const;

    // Total number of groups.
    size_t group_count() const;
    size_t group_3d_count() const;

    // ── Serialization ────────────────────────────────────────────────

    // Serialize to minimal JSON string.
    // Axes are identified by their pointer address offset from a base
    // figure's axes vector — caller provides the mapping.
    using AxesToIndex   = std::function<int(const Axes*)>;
    using IndexToAxes   = std::function<Axes*(int)>;
    using Axes3DToIndex = std::function<int(const Axes3D*)>;
    using IndexToAxes3D = std::function<Axes3D*(int)>;

    std::string serialize(const AxesToIndex& mapper, const Axes3DToIndex& mapper_3d = {}) const;
    void        deserialize(const std::string&   json,
                            const IndexToAxes&   mapper,
                            const IndexToAxes3D& mapper_3d = {});

    // ── Callbacks ────────────────────────────────────────────────────

    void set_on_change(LinkChangeCallback cb) { on_change_ = std::move(cb); }

    // ── Shared cursor (Agent E — Week 10) ────────────────────────────

    // Update the shared cursor from a source axes.
    // Broadcasts to all groups containing the source.
    void update_shared_cursor(const SharedCursor& cursor);

    // Get the shared cursor for a given axes.
    // Returns valid cursor only if the axes is in a group with the source.
    SharedCursor shared_cursor_for(const Axes* ax) const;

    // Clear the shared cursor (e.g. mouse left the window).
    void clear_shared_cursor();

   private:
    void notify();

    // Get peers for a given axes in a specific group (excluding the axes itself).
    std::vector<Axes*> peers_in_group_unlocked(const LinkGroup& group, const Axes* ax) const;

    mutable std::mutex                           mutex_;
    std::unordered_map<LinkGroupId, LinkGroup>   groups_;
    std::unordered_map<LinkGroupId, Link3DGroup> groups_3d_;
    LinkGroupId                                  next_id_ = 1;
    LinkChangeCallback                           on_change_;

    // Guard against re-entrant propagation
    bool propagating_ = false;

    // Shared cursor state
    SharedCursor shared_cursor_;
};

}   // namespace spectra
