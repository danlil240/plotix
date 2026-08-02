#pragma once

#include <spectra/color.hpp>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace spectra
{

// Plain-data snapshot of all overlay state for serialization.
// No ImGui or raw-pointer dependencies — uses indices and labels
// so it can be produced/consumed by both the binary figure serializer
// and the workspace JSON serializer without pulling in UI headers.
struct OverlaySnapshot
{
    bool crosshair_enabled = false;
    bool tooltip_enabled   = true;
    // Stored as the stable zero-based ToolMode value without depending on
    // input.hpp from this plain-data serialization type.
    uint8_t tool_mode = 0;

    struct MarkerEntry
    {
        float       data_x = 0.0f;
        float       data_y = 0.0f;
        std::string series_label;
        size_t      point_index = 0;
        size_t      axes_index  = 0;
    };
    std::vector<MarkerEntry> markers;

    struct AnnotationEntry
    {
        float       data_x = 0.0f;
        float       data_y = 0.0f;
        std::string text;
        Color       color      = {1.0f, 1.0f, 1.0f, 1.0f};
        float       offset_x   = 0.0f;
        float       offset_y   = -40.0f;
        size_t      axes_index = 0;
    };
    std::vector<AnnotationEntry> annotations;

    struct RegionEntry
    {
        bool   valid      = false;
        float  x_min      = 0.0f;
        float  x_max      = 0.0f;
        float  y_min      = 0.0f;
        float  y_max      = 0.0f;
        size_t axes_index = 0;
    } region;

    struct MeasurementEntry
    {
        bool   valid        = false;
        double start_data_x = 0.0;
        double start_data_y = 0.0;
        double end_data_x   = 0.0;
        double end_data_y   = 0.0;
        size_t axes_index   = 0;
    } measurement;
};

}   // namespace spectra
