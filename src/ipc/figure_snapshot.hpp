#pragma once

// figure_snapshot.hpp — Shared utilities for building Figure objects from IPC
// snapshots and applying DiffOps to live figures.
//
// Used by both the legacy window agent (src/agent/main.cpp) and the Qt window
// agent (src/adapters/qt/qt_ipc_client.cpp) to avoid code duplication.

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include "../ipc/message.hpp"

#include <memory>
#include <vector>

namespace spectra::ipc
{

// Check if a series type string is a 3D type.
inline bool is_3d_series_type(const std::string& t)
{
    return t == "line3d" || t == "scatter3d" || t == "surface" || t == "mesh";
}

// Build a real Figure from a SnapshotFigureState.
// Optionally override width/height (0 = use snapshot values).
std::unique_ptr<spectra::Figure> build_figure_from_snapshot(
    const SnapshotFigureState& snap,
    uint32_t                   override_width  = 0,
    uint32_t                   override_height = 0);

// Apply a DiffOp to a cached SnapshotFigureState (for cache maintenance).
void apply_diff_op_to_cache(SnapshotFigureState& fig, const DiffOp& op);

// Apply a DiffOp directly to a live Figure object (fast path for data updates).
void apply_diff_op_to_figure(spectra::Figure& fig, const DiffOp& op);

// Rebuild a FigureRegistry from an IPC snapshot cache.
// Clears existing figures and re-creates them from the cache.
// Returns the list of new FigureId values.
std::vector<spectra::FigureId> rebuild_registry_from_cache(
    spectra::FigureRegistry&                     registry,
    const std::vector<SnapshotFigureState>&      cache,
    uint32_t                                     width,
    uint32_t                                     height);

}   // namespace spectra::ipc
