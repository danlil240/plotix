#include "figure_serializer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>

#include "overlay_snapshot.hpp"

#include "../dialog_env_guard.hpp"
#include "../native_dialog_policy.hpp"
#include "../../../third_party/tinyfiledialogs.h"

namespace spectra
{

// ─── Binary format constants ────────────────────────────────────────────────
static constexpr uint32_t MAGIC   = 0x53504346;   // "SPCF" — Spectra Figure
static constexpr uint32_t VERSION = 5;

// Chunk tags
enum ChunkTag : uint16_t
{
    TAG_FIGURE_CONFIG = 0x0001,
    TAG_FIGURE_STYLE  = 0x0002,
    TAG_LEGEND_CONFIG = 0x0003,
    TAG_SUBPLOT_GRID  = 0x0004,

    TAG_AXES_2D = 0x0010,
    TAG_AXES_3D = 0x0011,

    TAG_SERIES_LINE    = 0x0020,
    TAG_SERIES_SCATTER = 0x0021,
    TAG_SERIES_LINE3D  = 0x0022,
    TAG_SERIES_SCAT3D  = 0x0023,
    TAG_SERIES_SURFACE = 0x0024,
    TAG_SERIES_MESH    = 0x0025,
    TAG_SERIES_BOXPLOT = 0x0026,
    TAG_SERIES_VIOLIN  = 0x0027,
    TAG_SERIES_HIST    = 0x0028,
    TAG_SERIES_BAR     = 0x0029,

    TAG_ANNOTATION     = 0x0040,
    TAG_MARKER         = 0x0041,
    TAG_OVERLAY_CONFIG = 0x0042,

    TAG_END = 0xFFFF,
};

// ─── Writer helper ──────────────────────────────────────────────────────────
class BinaryWriter
{
   public:
    explicit BinaryWriter(std::ofstream& f) : f_(f) {}

    void write_u16(uint16_t v) { f_.write(reinterpret_cast<const char*>(&v), 2); }
    void write_u32(uint32_t v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_i32(int32_t v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_f32(float v) { f_.write(reinterpret_cast<const char*>(&v), 4); }
    void write_f64(double v) { f_.write(reinterpret_cast<const char*>(&v), 8); }
    void write_u8(uint8_t v) { f_.write(reinterpret_cast<const char*>(&v), 1); }

    void write_color(const Color& c)
    {
        write_f32(c.r);
        write_f32(c.g);
        write_f32(c.b);
        write_f32(c.a);
    }

    void write_string(const std::string& s)
    {
        auto len = static_cast<uint32_t>(s.size());
        write_u32(len);
        if (len > 0)
            f_.write(s.data(), len);
    }

    void write_floats(std::span<const float> data)
    {
        auto count = static_cast<uint32_t>(data.size());
        write_u32(count);
        if (count > 0)
            f_.write(reinterpret_cast<const char*>(data.data()), count * sizeof(float));
    }

    void write_u32s(std::span<const uint32_t> data)
    {
        auto count = static_cast<uint32_t>(data.size());
        write_u32(count);
        if (count > 0)
            f_.write(reinterpret_cast<const char*>(data.data()), count * sizeof(uint32_t));
    }

    // Write chunk header. Returns position of length field for patching.
    std::streampos begin_chunk(ChunkTag tag)
    {
        write_u16(static_cast<uint16_t>(tag));
        auto pos = f_.tellp();
        write_u32(0);   // placeholder length
        return pos;
    }

    void end_chunk(std::streampos length_pos)
    {
        auto end = f_.tellp();
        auto len = static_cast<uint32_t>(end - length_pos - 4);
        f_.seekp(length_pos);
        write_u32(len);
        f_.seekp(end);
    }

    void write_series_common(const Series& s)
    {
        write_string(s.label());
        write_color(s.color());
        write_u8(static_cast<uint8_t>(s.visible()));
        write_u8(static_cast<uint8_t>(s.line_style()));
        write_u8(static_cast<uint8_t>(s.marker_style()));
        write_f32(s.marker_size());
        write_f32(s.opacity());
        write_f32(s.plot_style().line_width);
        write_u8(static_cast<uint8_t>(s.excluded_from_autoscale()));
        write_u8(static_cast<uint8_t>(s.show_in_legend()));
        write_u8(static_cast<uint8_t>(s.is_reference_line()));
        write_f64(s.x_offset());
    }

    [[nodiscard]] bool good() const { return f_.good(); }

   private:
    std::ofstream& f_;
};

// ─── Reader helper ──────────────────────────────────────────────────────────
class BinaryReader
{
   public:
    explicit BinaryReader(std::ifstream& f) : f_(f) {}

    uint16_t read_u16()
    {
        uint16_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 2);
        return v;
    }
    uint32_t read_u32()
    {
        uint32_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    int32_t read_i32()
    {
        int32_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    float read_f32()
    {
        float v = 0.0f;
        f_.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }
    double read_f64()
    {
        double v = 0.0;
        f_.read(reinterpret_cast<char*>(&v), 8);
        return v;
    }
    uint8_t read_u8()
    {
        uint8_t v = 0;
        f_.read(reinterpret_cast<char*>(&v), 1);
        return v;
    }

    Color read_color() { return {read_f32(), read_f32(), read_f32(), read_f32()}; }

    std::string read_string()
    {
        uint32_t len = read_u32();
        if (len == 0)
            return {};
        std::string s(len, '\0');
        f_.read(s.data(), len);
        return s;
    }

    std::vector<float> read_floats()
    {
        uint32_t           count = read_u32();
        std::vector<float> v(count);
        if (count > 0)
            f_.read(reinterpret_cast<char*>(v.data()), count * sizeof(float));
        return v;
    }

    std::vector<uint32_t> read_u32s()
    {
        uint32_t              count = read_u32();
        std::vector<uint32_t> v(count);
        if (count > 0)
            f_.read(reinterpret_cast<char*>(v.data()), count * sizeof(uint32_t));
        return v;
    }

    void skip(uint32_t bytes) { f_.seekg(bytes, std::ios::cur); }

    void read_series_common(Series& s)
    {
        s.label(read_string());
        s.color(read_color());
        s.visible(read_u8() != 0);
        s.line_style(static_cast<LineStyle>(read_u8()));
        s.marker_style(static_cast<MarkerStyle>(read_u8()));
        s.marker_size(read_f32());
        s.opacity(read_f32());
        float lw      = read_f32();
        auto  ps      = s.plot_style();
        ps.line_width = lw;
        s.plot_style(ps);
    }

    [[nodiscard]] bool good() const { return f_.good(); }

   private:
    std::ifstream& f_;
};

// ─── Save implementation ────────────────────────────────────────────────────

static void write_axes_2d(BinaryWriter& w, const Axes& axes, int axes_index)
{
    auto pos = w.begin_chunk(TAG_AXES_2D);

    w.write_i32(axes_index);
    w.write_string(axes.title());
    w.write_string(axes.xlabel());
    w.write_string(axes.ylabel());
    w.write_u8(static_cast<uint8_t>(axes.grid_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.border_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.autoscale_mode()));

    auto xl = axes.x_limits();
    auto yl = axes.y_limits();
    w.write_f32(xl.min);
    w.write_f32(xl.max);
    w.write_f32(yl.min);
    w.write_f32(yl.max);

    // Axis style
    const auto& as = axes.axis_style();
    w.write_color(as.tick_color);
    w.write_color(as.label_color);
    w.write_color(as.grid_color);
    w.write_f32(as.tick_length);
    w.write_f32(as.label_size);
    w.write_f32(as.title_size);
    w.write_f32(as.grid_width);

    w.end_chunk(pos);

    // Write series for this axes
    for (const auto& sp : axes.series())
    {
        if (auto* ls = dynamic_cast<LineSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_LINE);
            w.write_i32(axes_index);
            w.write_series_common(*ls);
            w.write_f32(ls->width());
            w.write_floats(ls->x_data());
            w.write_floats(ls->y_data());
            w.end_chunk(spos);
        }
        else if (auto* ss = dynamic_cast<ScatterSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SCATTER);
            w.write_i32(axes_index);
            w.write_series_common(*ss);
            w.write_f32(ss->size());
            w.write_floats(ss->x_data());
            w.write_floats(ss->y_data());
            w.end_chunk(spos);
        }
        else if (auto* bp = dynamic_cast<BoxPlotSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_BOXPLOT);
            w.write_i32(axes_index);
            w.write_series_common(*bp);
            w.write_f32(bp->box_width());
            w.write_u8(static_cast<uint8_t>(bp->show_outliers()));
            w.write_u8(static_cast<uint8_t>(bp->notched()));
            w.write_u8(static_cast<uint8_t>(bp->gradient()));
            // Write box data
            auto box_count = static_cast<uint32_t>(bp->positions().size());
            w.write_u32(box_count);
            for (uint32_t i = 0; i < box_count; ++i)
            {
                w.write_f32(bp->positions()[i]);
                const auto& st = bp->stats()[i];
                w.write_f32(st.median);
                w.write_f32(st.q1);
                w.write_f32(st.q3);
                w.write_f32(st.whisker_low);
                w.write_f32(st.whisker_high);
                w.write_floats(st.outliers);
            }
            w.end_chunk(spos);
        }
        else if (auto* vs = dynamic_cast<ViolinSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_VIOLIN);
            w.write_i32(axes_index);
            w.write_series_common(*vs);
            w.write_f32(vs->violin_width());
            w.write_i32(vs->resolution());
            w.write_u8(static_cast<uint8_t>(vs->show_box()));
            w.write_u8(static_cast<uint8_t>(vs->gradient()));
            auto vcount = static_cast<uint32_t>(vs->violins().size());
            w.write_u32(vcount);
            for (const auto& vd : vs->violins())
            {
                w.write_f32(vd.x_position);
                w.write_floats(vd.values);
            }
            w.end_chunk(spos);
        }
        else if (auto* hs = dynamic_cast<HistogramSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_HIST);
            w.write_i32(axes_index);
            w.write_series_common(*hs);
            w.write_i32(hs->bins());
            w.write_u8(static_cast<uint8_t>(hs->cumulative()));
            w.write_u8(static_cast<uint8_t>(hs->density()));
            w.write_u8(static_cast<uint8_t>(hs->gradient()));
            w.write_floats(hs->raw_values());
            w.end_chunk(spos);
        }
        else if (auto* bs = dynamic_cast<BarSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_BAR);
            w.write_i32(axes_index);
            w.write_series_common(*bs);
            w.write_f32(bs->bar_width());
            w.write_f32(bs->baseline());
            w.write_u8(static_cast<uint8_t>(bs->orientation()));
            w.write_u8(static_cast<uint8_t>(bs->gradient()));
            w.write_floats(bs->bar_positions());
            w.write_floats(bs->bar_heights());
            w.end_chunk(spos);
        }
    }
}

static void write_axes_3d(BinaryWriter& w, const Axes3D& axes, int axes_index)
{
    auto pos = w.begin_chunk(TAG_AXES_3D);

    w.write_i32(axes_index);
    w.write_string(axes.title());
    w.write_string(axes.xlabel());
    w.write_string(axes.ylabel());
    w.write_string(axes.zlabel());
    w.write_u8(static_cast<uint8_t>(axes.grid_enabled()));
    w.write_u8(static_cast<uint8_t>(axes.border_enabled()));

    auto xl = axes.x_limits();
    auto yl = axes.y_limits();
    auto zl = axes.z_limits();
    w.write_f32(xl.min);
    w.write_f32(xl.max);
    w.write_f32(yl.min);
    w.write_f32(yl.max);
    w.write_f32(zl.min);
    w.write_f32(zl.max);

    // Grid planes
    w.write_i32(static_cast<int32_t>(axes.grid_planes()));
    w.write_u8(static_cast<uint8_t>(axes.show_bounding_box()));

    // Lighting
    auto ld = axes.light_dir();
    w.write_f32(ld.x);
    w.write_f32(ld.y);
    w.write_f32(ld.z);
    w.write_u8(static_cast<uint8_t>(axes.lighting_enabled()));

    // Axis style
    const auto& as = axes.axis_style();
    w.write_color(as.tick_color);
    w.write_color(as.label_color);
    w.write_color(as.grid_color);
    w.write_f32(as.tick_length);
    w.write_f32(as.label_size);
    w.write_f32(as.title_size);
    w.write_f32(as.grid_width);

    // Camera
    const auto& cam = axes.camera();
    w.write_f32(cam.azimuth);
    w.write_f32(cam.elevation);
    w.write_f32(cam.distance);
    w.write_f32(cam.fov);
    w.write_f32(cam.near_clip);
    w.write_f32(cam.far_clip);
    w.write_f32(cam.ortho_size);
    w.write_u8(static_cast<uint8_t>(cam.projection_mode));
    w.write_f32(cam.target.x);
    w.write_f32(cam.target.y);
    w.write_f32(cam.target.z);
    w.write_f32(cam.up.x);
    w.write_f32(cam.up.y);
    w.write_f32(cam.up.z);

    w.end_chunk(pos);

    // Write 3D series
    for (const auto& sp : axes.series())
    {
        if (auto* ls = dynamic_cast<LineSeries3D*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_LINE3D);
            w.write_i32(axes_index);
            w.write_series_common(*ls);
            w.write_f32(ls->width());
            w.write_u8(static_cast<uint8_t>(ls->blend_mode()));
            w.write_floats(ls->x_data());
            w.write_floats(ls->y_data());
            w.write_floats(ls->z_data());
            w.end_chunk(spos);
        }
        else if (auto* ss = dynamic_cast<ScatterSeries3D*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SCAT3D);
            w.write_i32(axes_index);
            w.write_series_common(*ss);
            w.write_f32(ss->size());
            w.write_u8(static_cast<uint8_t>(ss->blend_mode()));
            w.write_floats(ss->x_data());
            w.write_floats(ss->y_data());
            w.write_floats(ss->z_data());
            w.end_chunk(spos);
        }
        else if (auto* surf = dynamic_cast<SurfaceSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_SURFACE);
            w.write_i32(axes_index);
            w.write_series_common(*surf);
            w.write_u8(static_cast<uint8_t>(surf->colormap_type()));
            w.write_f32(surf->colormap_min());
            w.write_f32(surf->colormap_max());
            w.write_f32(surf->ambient());
            w.write_f32(surf->specular());
            w.write_f32(surf->shininess());
            w.write_u8(static_cast<uint8_t>(surf->blend_mode()));
            w.write_u8(static_cast<uint8_t>(surf->double_sided()));
            w.write_u8(static_cast<uint8_t>(surf->wireframe()));
            w.write_u8(static_cast<uint8_t>(surf->colormap_alpha()));
            w.write_f32(surf->colormap_alpha_min());
            w.write_f32(surf->colormap_alpha_max());
            w.write_floats(surf->x_grid());
            w.write_floats(surf->y_grid());
            w.write_floats(surf->z_values());
            w.end_chunk(spos);
        }
        else if (auto* ms = dynamic_cast<MeshSeries*>(sp.get()))
        {
            auto spos = w.begin_chunk(TAG_SERIES_MESH);
            w.write_i32(axes_index);
            w.write_series_common(*ms);
            w.write_f32(ms->ambient());
            w.write_f32(ms->specular());
            w.write_f32(ms->shininess());
            w.write_u8(static_cast<uint8_t>(ms->blend_mode()));
            w.write_u8(static_cast<uint8_t>(ms->double_sided()));
            w.write_u8(static_cast<uint8_t>(ms->wireframe()));
            w.write_floats(ms->vertices());
            w.write_u32s(ms->indices());
            w.end_chunk(spos);
        }
    }
}

bool FigureSerializer::save(const std::string&     path,
                            const Figure&          figure,
                            const OverlaySnapshot* overlay)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    BinaryWriter w(f);

    // Header
    w.write_u32(MAGIC);
    w.write_u32(VERSION);

    // Figure config
    {
        auto pos = w.begin_chunk(TAG_FIGURE_CONFIG);
        w.write_u32(figure.width());
        w.write_u32(figure.height());
        w.end_chunk(pos);
    }

    // Figure style
    {
        auto        pos = w.begin_chunk(TAG_FIGURE_STYLE);
        const auto& s   = figure.style();
        w.write_color(s.background);
        w.write_f32(s.margin_top);
        w.write_f32(s.margin_bottom);
        w.write_f32(s.margin_left);
        w.write_f32(s.margin_right);
        w.write_f32(s.subplot_hgap);
        w.write_f32(s.subplot_vgap);
        w.write_f32(s.min_subplot_height);
        w.end_chunk(pos);
    }

    // Legend config
    {
        auto        pos = w.begin_chunk(TAG_LEGEND_CONFIG);
        const auto& lc  = figure.legend();
        w.write_u8(static_cast<uint8_t>(lc.position));
        w.write_u8(static_cast<uint8_t>(lc.visible));
        w.write_f32(lc.font_size);
        w.write_color(lc.bg_color);
        w.write_color(lc.border_color);
        w.write_f32(lc.padding);
        w.end_chunk(pos);
    }

    // Subplot grid — total axes = max(axes_.size, all_axes_.size) since
    // 2D axes live in axes_ and 3D axes live in all_axes_
    uint32_t total_axes =
        static_cast<uint32_t>(std::max(figure.axes().size(), figure.all_axes().size()));
    {
        auto pos = w.begin_chunk(TAG_SUBPLOT_GRID);
        w.write_i32(figure.grid_rows());
        w.write_i32(figure.grid_cols());
        w.write_u32(total_axes);
        w.end_chunk(pos);
    }

    // Write all axes and their series.
    // 2D axes are in axes_, 3D axes are in all_axes_.
    // We iterate up to total_axes and check both containers at each index.
    int axes_idx = 0;
    for (uint32_t i = 0; i < total_axes; ++i)
    {
        // Check 3D first (all_axes_)
        if (i < figure.all_axes().size() && figure.all_axes()[i])
        {
            auto* ptr = figure.all_axes()[i].get();
            if (auto* a3d = dynamic_cast<Axes3D*>(ptr))
            {
                write_axes_3d(w, *a3d, axes_idx);
                ++axes_idx;
                continue;
            }
        }
        // Check 2D (axes_)
        if (i < figure.axes().size() && figure.axes()[i])
        {
            write_axes_2d(w, *figure.axes()[i], axes_idx);
            ++axes_idx;
            continue;
        }
    }

    // Write overlay state (if provided)
    if (overlay)
    {
        // Overlay config (crosshair, tooltip)
        {
            auto opos = w.begin_chunk(TAG_OVERLAY_CONFIG);
            w.write_u8(overlay->crosshair_enabled ? 1 : 0);
            w.write_u8(overlay->tooltip_enabled ? 1 : 0);
            w.end_chunk(opos);
        }

        // Markers
        for (const auto& m : overlay->markers)
        {
            auto mpos = w.begin_chunk(TAG_MARKER);
            w.write_f32(m.data_x);
            w.write_f32(m.data_y);
            w.write_string(m.series_label);
            w.write_u32(static_cast<uint32_t>(m.point_index));
            w.write_i32(static_cast<int32_t>(m.axes_index));
            w.end_chunk(mpos);
        }

        // Annotations
        for (const auto& ann : overlay->annotations)
        {
            if (ann.text.empty())
                continue;
            auto apos = w.begin_chunk(TAG_ANNOTATION);
            w.write_f32(ann.data_x);
            w.write_f32(ann.data_y);
            w.write_string(ann.text);
            w.write_color(ann.color);
            w.write_f32(ann.offset_x);
            w.write_f32(ann.offset_y);
            w.write_i32(static_cast<int32_t>(ann.axes_index));
            w.end_chunk(apos);
        }
    }

    // End marker
    w.write_u16(static_cast<uint16_t>(TAG_END));
    w.write_u32(0);

    return w.good();
}

// ─── Load implementation ────────────────────────────────────────────────────

bool FigureSerializer::load(const std::string& path, Figure& figure, OverlaySnapshot* overlay)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    BinaryReader r(f);

    // Verify header
    uint32_t magic = r.read_u32();
    if (magic != MAGIC)
        return false;

    uint32_t version = r.read_u32();
    if (version > VERSION)
        return false;

    // Clear existing figure data — use clear_series() to trigger GPU cleanup
    for (auto& ax : figure.axes_mut())
    {
        if (ax)
            ax->clear_series();
    }
    for (auto& ax : figure.all_axes_mut())
    {
        if (ax)
            ax->clear_series();
    }
    figure.axes_mut().clear();
    figure.all_axes_mut().clear();
    figure.grid_rows_ = 1;
    figure.grid_cols_ = 1;

    int grid_rows = 1;
    int grid_cols = 1;

    // Track axes as we create them: index -> AxesBase*
    std::vector<AxesBase*> axes_ptrs;

    // Read chunks
    while (r.good())
    {
        uint16_t tag = r.read_u16();
        uint32_t len = r.read_u32();

        if (!r.good())
            break;

        if (tag == TAG_END)
            break;

        switch (static_cast<ChunkTag>(tag))
        {
            case TAG_FIGURE_CONFIG:
            {
                figure.config_.width  = r.read_u32();
                figure.config_.height = r.read_u32();
                break;
            }

            case TAG_FIGURE_STYLE:
            {
                auto& s         = figure.style();
                s.background    = r.read_color();
                s.margin_top    = r.read_f32();
                s.margin_bottom = r.read_f32();
                s.margin_left   = r.read_f32();
                s.margin_right  = r.read_f32();
                s.subplot_hgap  = r.read_f32();
                s.subplot_vgap  = r.read_f32();
                if (r.good() && len >= 32)   // backward compat: field added after v3
                    s.min_subplot_height = r.read_f32();
                break;
            }

            case TAG_LEGEND_CONFIG:
            {
                auto& lc        = figure.legend();
                lc.position     = static_cast<LegendPosition>(r.read_u8());
                lc.visible      = r.read_u8() != 0;
                lc.font_size    = r.read_f32();
                lc.bg_color     = r.read_color();
                lc.border_color = r.read_color();
                lc.padding      = r.read_f32();
                break;
            }

            case TAG_SUBPLOT_GRID:
            {
                grid_rows = r.read_i32();
                grid_cols = r.read_i32();
                (void)
                    r.read_u32();   // axes_count (reserved for validation; consumed for alignment)
                figure.grid_rows_ = grid_rows;
                figure.grid_cols_ = grid_cols;
                break;
            }

            case TAG_AXES_2D:
            {
                int idx = r.read_i32();
                (void)idx;

                // Create axes via subplot to maintain proper grid
                int   current = static_cast<int>(figure.axes().size());
                auto& axes    = figure.subplot(grid_rows, grid_cols, current + 1);

                axes.title(r.read_string());
                axes.xlabel(r.read_string());
                axes.ylabel(r.read_string());
                axes.grid(r.read_u8() != 0);
                axes.show_border(r.read_u8() != 0);
                auto saved_autoscale = static_cast<AutoscaleMode>(r.read_u8());

                float xmin = r.read_f32();
                float xmax = r.read_f32();
                float ymin = r.read_f32();
                float ymax = r.read_f32();

                // Set Manual first so xlim/ylim stick, then set limits,
                // then restore the saved autoscale mode without clearing them.
                axes.autoscale_mode(AutoscaleMode::Manual);
                axes.xlim(xmin, xmax);
                axes.ylim(ymin, ymax);
                axes.autoscale_mode(saved_autoscale);

                // Axis style
                auto& as       = axes.axis_style();
                as.tick_color  = r.read_color();
                as.label_color = r.read_color();
                as.grid_color  = r.read_color();
                as.tick_length = r.read_f32();
                as.label_size  = r.read_f32();
                as.title_size  = r.read_f32();
                as.grid_width  = r.read_f32();

                axes_ptrs.push_back(&axes);
                break;
            }

            case TAG_AXES_3D:
            {
                int idx = r.read_i32();
                (void)idx;

                int   current = static_cast<int>(figure.all_axes().size());
                auto& axes    = figure.subplot3d(grid_rows, grid_cols, current + 1);

                axes.title(r.read_string());
                axes.xlabel(r.read_string());
                axes.ylabel(r.read_string());
                axes.zlabel(r.read_string());
                axes.grid(r.read_u8() != 0);
                axes.show_border(r.read_u8() != 0);

                float xmin = r.read_f32();
                float xmax = r.read_f32();
                float ymin = r.read_f32();
                float ymax = r.read_f32();
                float zmin = r.read_f32();
                float zmax = r.read_f32();
                axes.xlim(xmin, xmax);
                axes.ylim(ymin, ymax);
                axes.zlim(zmin, zmax);

                axes.set_grid_planes(r.read_i32());
                axes.show_bounding_box(r.read_u8() != 0);

                // Lighting
                vec3 ld;
                ld.x = r.read_f32();
                ld.y = r.read_f32();
                ld.z = r.read_f32();
                axes.light_dir(ld);
                axes.lighting_enabled(r.read_u8() != 0);

                // Axis style
                auto& as       = axes.axis_style();
                as.tick_color  = r.read_color();
                as.label_color = r.read_color();
                as.grid_color  = r.read_color();
                as.tick_length = r.read_f32();
                as.label_size  = r.read_f32();
                as.title_size  = r.read_f32();
                as.grid_width  = r.read_f32();

                // Camera
                auto& cam           = axes.camera();
                cam.azimuth         = r.read_f32();
                cam.elevation       = r.read_f32();
                cam.distance        = r.read_f32();
                cam.fov             = r.read_f32();
                cam.near_clip       = r.read_f32();
                cam.far_clip        = r.read_f32();
                cam.ortho_size      = r.read_f32();
                cam.projection_mode = static_cast<Camera::ProjectionMode>(r.read_u8());
                cam.target.x        = r.read_f32();
                cam.target.y        = r.read_f32();
                cam.target.z        = r.read_f32();
                cam.up.x            = r.read_f32();
                cam.up.y            = r.read_f32();
                cam.up.z            = r.read_f32();
                cam.update_position_from_orbit();

                axes_ptrs.push_back(&axes);
                break;
            }

            case TAG_SERIES_LINE:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                auto x_data = std::vector<float>();   // read after common
                auto y_data = std::vector<float>();

                // Use temporary to read common + specific, then create series
                std::string lbl   = r.read_string();
                Color       col   = r.read_color();
                bool        vis   = r.read_u8() != 0;
                auto        ls_e  = static_cast<LineStyle>(r.read_u8());
                auto        ms_e  = static_cast<MarkerStyle>(r.read_u8());
                float       msz   = r.read_f32();
                float       opac  = r.read_f32();
                float       lw    = r.read_f32();
                bool        excl  = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;
                float       width = r.read_f32();

                x_data = r.read_floats();
                y_data = r.read_floats();

                auto& s = axes->line(x_data, y_data);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.width(width);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);
                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_SERIES_SCATTER:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff = (version >= 5) ? r.read_f64() : 0.0;
                float       sz   = r.read_f32();

                auto x_data = r.read_floats();
                auto y_data = r.read_floats();

                auto& s = axes->scatter(x_data, y_data);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.size(sz);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);
                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_SERIES_BOXPLOT:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                float bw        = r.read_f32();
                bool  show_outl = r.read_u8() != 0;
                bool  notch     = r.read_u8() != 0;
                bool  grad      = r.read_u8() != 0;

                auto& bp = axes->box_plot();
                bp.label(lbl).color(col).visible(vis);
                bp.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                bp.box_width(bw).show_outliers(show_outl).notched(notch).gradient(grad);
                bp.set_excluded_from_autoscale(excl);
                bp.set_show_in_legend(inleg);
                bp.set_reference_line(isref);
                bp.x_offset(xoff);

                uint32_t box_count = r.read_u32();
                for (uint32_t i = 0; i < box_count; ++i)
                {
                    float xpos     = r.read_f32();
                    float median   = r.read_f32();
                    float q1       = r.read_f32();
                    float q3       = r.read_f32();
                    float wlo      = r.read_f32();
                    float whi      = r.read_f32();
                    auto  outliers = r.read_floats();
                    bp.add_box(xpos, median, q1, q3, wlo, whi, outliers);
                }

                auto ps       = bp.plot_style();
                ps.line_width = lw;
                bp.plot_style(ps);
                break;
            }

            case TAG_SERIES_VIOLIN:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                float vw      = r.read_f32();
                int   res     = r.read_i32();
                bool  show_bx = r.read_u8() != 0;
                bool  grad    = r.read_u8() != 0;

                auto& vs = axes->violin();
                vs.label(lbl).color(col).visible(vis);
                vs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                vs.violin_width(vw).resolution(res).show_box(show_bx).gradient(grad);
                vs.set_excluded_from_autoscale(excl);
                vs.set_show_in_legend(inleg);
                vs.set_reference_line(isref);
                vs.x_offset(xoff);

                uint32_t vcount = r.read_u32();
                for (uint32_t i = 0; i < vcount; ++i)
                {
                    float xpos = r.read_f32();
                    auto  vals = r.read_floats();
                    vs.add_violin(xpos, vals);
                }

                auto ps       = vs.plot_style();
                ps.line_width = lw;
                vs.plot_style(ps);
                break;
            }

            case TAG_SERIES_HIST:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                int  bins = r.read_i32();
                bool cum  = r.read_u8() != 0;
                bool dens = r.read_u8() != 0;
                bool grad = r.read_u8() != 0;
                auto vals = r.read_floats();

                auto& hs = axes->histogram(vals, bins);
                hs.label(lbl).color(col).visible(vis);
                hs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                hs.cumulative(cum).density(dens).gradient(grad);
                hs.set_excluded_from_autoscale(excl);
                hs.set_show_in_legend(inleg);
                hs.set_reference_line(isref);
                hs.x_offset(xoff);

                auto ps       = hs.plot_style();
                ps.line_width = lw;
                hs.plot_style(ps);
                break;
            }

            case TAG_SERIES_BAR:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                float bw   = r.read_f32();
                float base = r.read_f32();
                auto  ori  = static_cast<BarOrientation>(r.read_u8());
                bool  grad = r.read_u8() != 0;
                auto  pos  = r.read_floats();
                auto  hts  = r.read_floats();

                auto& bs = axes->bar(pos, hts);
                bs.label(lbl).color(col).visible(vis);
                bs.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                bs.bar_width(bw).baseline(base).orientation(ori).gradient(grad);
                bs.set_excluded_from_autoscale(excl);
                bs.set_show_in_legend(inleg);
                bs.set_reference_line(isref);
                bs.x_offset(xoff);

                auto ps2       = bs.plot_style();
                ps2.line_width = lw;
                bs.plot_style(ps2);
                break;
            }

            case TAG_SERIES_LINE3D:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;
                float       wid  = r.read_f32();
                auto        bm   = static_cast<BlendMode>(r.read_u8());

                auto x = r.read_floats();
                auto y = r.read_floats();
                auto z = r.read_floats();

                auto& s = axes->line3d(x, y, z);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.width(wid).blend_mode(bm);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_SERIES_SCAT3D:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;
                float       sz   = r.read_f32();
                auto        bm   = static_cast<BlendMode>(r.read_u8());

                auto x = r.read_floats();
                auto y = r.read_floats();
                auto z = r.read_floats();

                auto& s = axes->scatter3d(x, y, z);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.size(sz).blend_mode(bm);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_SERIES_SURFACE:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                auto  cmap     = static_cast<ColormapType>(r.read_u8());
                float cmap_min = r.read_f32();
                float cmap_max = r.read_f32();
                float amb      = r.read_f32();
                float spec     = r.read_f32();
                float shin     = r.read_f32();
                auto  bm       = static_cast<BlendMode>(r.read_u8());
                bool  ds       = r.read_u8() != 0;
                bool  wf       = r.read_u8() != 0;
                bool  ca       = r.read_u8() != 0;
                float ca_min   = r.read_f32();
                float ca_max   = r.read_f32();

                auto xg = r.read_floats();
                auto yg = r.read_floats();
                auto zv = r.read_floats();

                auto& s = axes->surface(xg, yg, zv);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.colormap(cmap).colormap_range(cmap_min, cmap_max);
                s.ambient(amb).specular(spec).shininess(shin);
                s.blend_mode(bm).double_sided(ds).wireframe(wf);
                s.colormap_alpha(ca).colormap_alpha_range(ca_min, ca_max);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_SERIES_MESH:
            {
                int idx = r.read_i32();
                if (idx < 0 || idx >= static_cast<int>(axes_ptrs.size()))
                {
                    r.skip(len - 4);
                    break;
                }
                auto* axes = dynamic_cast<Axes3D*>(axes_ptrs[idx]);
                if (!axes)
                {
                    r.skip(len - 4);
                    break;
                }

                std::string lbl  = r.read_string();
                Color       col  = r.read_color();
                bool        vis  = r.read_u8() != 0;
                auto        ls_e = static_cast<LineStyle>(r.read_u8());
                auto        ms_e = static_cast<MarkerStyle>(r.read_u8());
                float       msz  = r.read_f32();
                float       opac = r.read_f32();
                float       lw   = r.read_f32();
                bool        excl = (version >= 2) && (r.read_u8() != 0);
                bool        inleg = (version >= 3) ? (r.read_u8() != 0) : true;
                bool        isref = (version >= 4) ? (r.read_u8() != 0) : (excl && !inleg);
                double      xoff  = (version >= 5) ? r.read_f64() : 0.0;

                float amb  = r.read_f32();
                float spec = r.read_f32();
                float shin = r.read_f32();
                auto  bm   = static_cast<BlendMode>(r.read_u8());
                bool  ds   = r.read_u8() != 0;
                bool  wf   = r.read_u8() != 0;

                auto verts = r.read_floats();
                auto inds  = r.read_u32s();

                auto& s = axes->mesh(verts, inds);
                s.label(lbl).color(col).visible(vis);
                s.line_style(ls_e).marker_style(ms_e).marker_size(msz).opacity(opac);
                s.ambient(amb).specular(spec).shininess(shin);
                s.blend_mode(bm).double_sided(ds).wireframe(wf);
                s.set_excluded_from_autoscale(excl);
                s.set_show_in_legend(inleg);
                s.set_reference_line(isref);
                s.x_offset(xoff);

                auto ps       = s.plot_style();
                ps.line_width = lw;
                s.plot_style(ps);
                break;
            }

            case TAG_ANNOTATION:
            {
                if (overlay)
                {
                    OverlaySnapshot::AnnotationEntry ae;
                    ae.data_x     = r.read_f32();
                    ae.data_y     = r.read_f32();
                    ae.text       = r.read_string();
                    ae.color      = r.read_color();
                    ae.offset_x   = r.read_f32();
                    ae.offset_y   = r.read_f32();
                    int32_t ai    = r.read_i32();
                    ae.axes_index = (ai >= 0) ? static_cast<size_t>(ai) : 0;
                    overlay->annotations.push_back(std::move(ae));
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            case TAG_MARKER:
            {
                if (overlay)
                {
                    OverlaySnapshot::MarkerEntry me;
                    me.data_x       = r.read_f32();
                    me.data_y       = r.read_f32();
                    me.series_label = r.read_string();
                    me.point_index  = r.read_u32();
                    int32_t ai      = r.read_i32();
                    me.axes_index   = (ai >= 0) ? static_cast<size_t>(ai) : 0;
                    overlay->markers.push_back(std::move(me));
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            case TAG_OVERLAY_CONFIG:
            {
                if (overlay)
                {
                    overlay->crosshair_enabled = r.read_u8() != 0;
                    overlay->tooltip_enabled   = r.read_u8() != 0;
                }
                else
                {
                    r.skip(len);
                }
                break;
            }

            default:
                // Unknown chunk — skip
                r.skip(len);
                break;
        }
    }

    return r.good() || f.eof();
}

// ─── Dialog wrappers ────────────────────────────────────────────────────────

bool FigureSerializer::save_with_dialog(const Figure& figure, const OverlaySnapshot* overlay)
{
    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("figure_serializer", "Save figure dialog suppressed (automation mode)");
        return false;
    }

    DialogEnvGuard env_guard;

    char const* filter_patterns[] = {"*.spectra"};
    const char* home_env          = std::getenv("HOME");
    std::string home_dir = (home_env ? std::string(home_env) + "/" : "/") + "figure.spectra";
    const char* home     = home_dir.c_str();
    char*       result   = tinyfd_saveFileDialog("Save Figure",
                                                 home,
                                                 1,
                                                 filter_patterns,
                                                 "Spectra Figure (*.spectra)");

    if (!result)
        return false;

    return save(result, figure, overlay);
}

bool FigureSerializer::load_with_dialog(Figure& figure, OverlaySnapshot* overlay)
{
    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("figure_serializer", "Load figure dialog suppressed (automation mode)");
        return false;
    }

    DialogEnvGuard env_guard;

    char const* filter_patterns[] = {"*.spectra"};
    const char* home_env          = std::getenv("HOME");
    std::string home_dir          = home_env ? std::string(home_env) + "/" : "/";
    const char* home              = home_dir.c_str();
    char*       result            = tinyfd_openFileDialog("Open Figure",
                                                          home,
                                                          1,
                                                          filter_patterns,
                                                          "Spectra Figure (*.spectra)",
                                                          0);

    if (!result)
        return false;

    return load(result, figure, overlay);
}

}   // namespace spectra
