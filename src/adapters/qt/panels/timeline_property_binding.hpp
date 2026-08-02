#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{
class Figure;
class TimelineEditor;
}

namespace spectra::adapters::qt
{

struct TimelinePropertyTarget
{
    std::string path;
    std::string label;
    float       current_value = 0.0f;
    float       minimum       = -1.0e12f;
    float       maximum       = 1.0e12f;
};

// Enumerate model properties that can be driven by a numeric timeline track.
// Paths are stable across process restart and are resolved by axes/series index
// on every evaluation, so series vector reallocation cannot stale a callback.
std::vector<TimelinePropertyTarget> timeline_property_targets(Figure& figure);

// Rebuild one or all runtime callbacks from persisted TimelineTrack paths.
// Missing topology is retained as metadata and simply remains unbound.
bool bind_timeline_property(TimelineEditor& timeline, Figure& figure, uint32_t track_id);
void bind_timeline_properties(TimelineEditor& timeline, Figure& figure);

}   // namespace spectra::adapters::qt
