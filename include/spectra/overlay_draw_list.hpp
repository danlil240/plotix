#pragma once

// OverlayDrawList — framework-neutral 2D immediate-mode drawing interface.
//
// Abstracts the primitives used by overlay rendering (tooltip, crosshair,
// annotations, markers, region selection, box-zoom, etc.) so that overlays
// can be rendered by any backend:
//   - ImGui ImDrawList (legacy frontend)
//   - QPainter (Qt frontend)
//   - Vulkan push-buffer (future renderer-level overlays)
//
// This is the first step in porting ImGui-specific overlays to the Qt frontend
// (Phase 5 feature parity).  The interface mirrors the subset of ImDrawList
// calls used by src/ui/overlay/*.cpp.

#include <cstdint>
#include <string_view>

namespace spectra
{

// 2D point in physical pixels.
struct OverlayPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// Packed RGBA color (0xAABBGGRR to match ImU32 for zero-cost ImGui mapping).
using OverlayColor = uint32_t;

// Abstract font handle — backends provide concrete implementations.
struct OverlayFont
{
    OverlayFont()                              = default;
    virtual ~OverlayFont()                     = default;
    OverlayFont(const OverlayFont&)            = delete;
    OverlayFont& operator=(const OverlayFont&) = delete;
    float        size                          = 14.0f;
};

// Rounding corner flags (bitmask, matches ImDrawFlags_RoundCorners_*).
enum class OverlayRoundCorners : uint32_t
{
    None        = 0,
    TopLeft     = 1,
    TopRight    = 2,
    BottomLeft  = 4,
    BottomRight = 8,
    All         = 15,
};

// Abstract 2D draw list for overlay rendering.
class OverlayDrawList
{
   public:
    OverlayDrawList()                                  = default;
    virtual ~OverlayDrawList()                         = default;
    OverlayDrawList(const OverlayDrawList&)            = delete;
    OverlayDrawList& operator=(const OverlayDrawList&) = delete;

    // ── Clip rect ──────────────────────────────────────────────────────────
    virtual void push_clip_rect(OverlayPoint min, OverlayPoint max) = 0;
    virtual void pop_clip_rect()                                    = 0;

    // ── Primitives ─────────────────────────────────────────────────────────

    virtual void add_line(OverlayPoint p0,
                          OverlayPoint p1,
                          OverlayColor color,
                          float        thickness = 1.0f) = 0;

    virtual void add_rect_filled(OverlayPoint        p0,
                                 OverlayPoint        p1,
                                 OverlayColor        color,
                                 float               rounding = 0.0f,
                                 OverlayRoundCorners corners  = OverlayRoundCorners::All) = 0;

    virtual void add_rect(OverlayPoint        p0,
                          OverlayPoint        p1,
                          OverlayColor        color,
                          float               rounding  = 0.0f,
                          OverlayRoundCorners corners   = OverlayRoundCorners::All,
                          float               thickness = 1.0f) = 0;

    virtual void add_circle_filled(OverlayPoint center,
                                   float        radius,
                                   OverlayColor color,
                                   int          num_segments = 0) = 0;

    virtual void add_circle(OverlayPoint center,
                            float        radius,
                            OverlayColor color,
                            int          num_segments = 0,
                            float        thickness    = 1.0f) = 0;

    virtual void add_triangle_filled(OverlayPoint p0,
                                     OverlayPoint p1,
                                     OverlayPoint p2,
                                     OverlayColor color) = 0;

    // ── Text ───────────────────────────────────────────────────────────────
    // If font is nullptr, the backend's default font is used.
    virtual void add_text(OverlayFont*     font,
                          float            font_size,
                          OverlayPoint     pos,
                          OverlayColor     color,
                          std::string_view text) = 0;

    // ── Metrics (for layout) ───────────────────────────────────────────────
    virtual OverlayPoint text_size(OverlayFont*     font,
                                   float            font_size,
                                   std::string_view text) const = 0;
};

// ── Color helpers ─────────────────────────────────────────────────────────────

inline OverlayColor overlay_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<OverlayColor>(a) << 24 | static_cast<OverlayColor>(b) << 16
           | static_cast<OverlayColor>(g) << 8 | static_cast<OverlayColor>(r);
}

inline OverlayColor overlay_color_rgba(float r, float g, float b, float a)
{
    return overlay_color_rgba(static_cast<uint8_t>(r * 255.0f + 0.5f),
                              static_cast<uint8_t>(g * 255.0f + 0.5f),
                              static_cast<uint8_t>(b * 255.0f + 0.5f),
                              static_cast<uint8_t>(a * 255.0f + 0.5f));
}

}   // namespace spectra
