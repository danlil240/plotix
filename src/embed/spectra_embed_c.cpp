#include <spectra/spectra_embed_c.h>
#include <spectra/embed.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>
#include <spectra/series3d.hpp>
#include <spectra/plot_style.hpp>

#include "ui/theme/theme.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <span>
#include <string>
#include <vector>

// Opaque handle wrappers — the C API uses pointers to these.
// EmbedSurface owns all figures/axes/series, so we just wrap pointers.

struct SpectraEmbed
{
    spectra::EmbedSurface surface;
    SpectraFrameCb        frame_cb  = nullptr;
    void*                 frame_ud  = nullptr;
    SpectraRedrawCb       redraw_cb = nullptr;
    void*                 redraw_ud = nullptr;
    explicit SpectraEmbed(uint32_t w, uint32_t h) : surface(spectra::EmbedConfig{w, h}) {}
    explicit SpectraEmbed(const spectra::EmbedConfig& cfg) : surface(cfg) {}
};

struct SpectraFigure
{
    spectra::Figure* ptr;
};

struct SpectraAxes
{
    spectra::Axes*     axes_2d = nullptr;
    spectra::Axes3D*   axes_3d = nullptr;
    spectra::AxesBase* base    = nullptr;
    spectra::Figure*   fig     = nullptr;   // owning figure (for legend control)
};

struct SpectraSeries
{
    spectra::Series* ptr      = nullptr;
    uint32_t         capacity = 0;   // ring-buffer cap (0 = unbounded)
};

// Small pool of wrapper objects so the C API can return stable pointers.
// These are leaked intentionally — the embed surface owns the real objects.
// A real production API would use a handle table; this is sufficient for FFI demos.
static thread_local std::deque<SpectraFigure> g_fig_pool;
static thread_local std::deque<SpectraAxes>   g_ax_pool;
static thread_local std::deque<SpectraSeries> g_series_pool;

// ── Enum mapping helpers ─────────────────────────────────────────────────────

static spectra::LineStyle to_line_style(int v)
{
    switch (v)
    {
        case SPECTRA_LINE_NONE:
            return spectra::LineStyle::None;
        case SPECTRA_LINE_DASHED:
            return spectra::LineStyle::Dashed;
        case SPECTRA_LINE_DOTTED:
            return spectra::LineStyle::Dotted;
        default:
            return spectra::LineStyle::Solid;
    }
}

static spectra::MarkerStyle to_marker_style(int v)
{
    using M = spectra::MarkerStyle;
    switch (v)
    {
        case SPECTRA_MARKER_NONE:
            return M::None;
        case SPECTRA_MARKER_CIRCLE:
            return M::Circle;
        case SPECTRA_MARKER_PLUS:
            return M::Plus;
        case SPECTRA_MARKER_CROSS:
            return M::Cross;
        case SPECTRA_MARKER_STAR:
            return M::Star;
        case SPECTRA_MARKER_SQUARE:
            return M::Square;
        case SPECTRA_MARKER_DIAMOND:
            return M::Diamond;
        case SPECTRA_MARKER_TRIANGLE_UP:
            return M::TriangleUp;
        case SPECTRA_MARKER_TRIANGLE_DOWN:
            return M::TriangleDown;
        case SPECTRA_MARKER_TRIANGLE_LEFT:
            return M::TriangleLeft;
        case SPECTRA_MARKER_TRIANGLE_RIGHT:
            return M::TriangleRight;
        default:
            return M::Circle;
    }
}

static spectra::ColormapType to_colormap(int v)
{
    using C = spectra::ColormapType;
    switch (v)
    {
        case SPECTRA_CMAP_VIRIDIS:
            return C::Viridis;
        case SPECTRA_CMAP_PLASMA:
            return C::Plasma;
        case SPECTRA_CMAP_INFERNO:
            return C::Inferno;
        case SPECTRA_CMAP_MAGMA:
            return C::Magma;
        case SPECTRA_CMAP_JET:
            return C::Jet;
        case SPECTRA_CMAP_COOLWARM:
            return C::Coolwarm;
        case SPECTRA_CMAP_GRAYSCALE:
            return C::Grayscale;
        default:
            return C::None;
    }
}

static spectra::LegendPosition to_legend_position(int v)
{
    using L = spectra::LegendPosition;
    switch (v)
    {
        case SPECTRA_LEGEND_TOP_LEFT:
            return L::TopLeft;
        case SPECTRA_LEGEND_BOTTOM_RIGHT:
            return L::BottomRight;
        case SPECTRA_LEGEND_BOTTOM_LEFT:
            return L::BottomLeft;
        case SPECTRA_LEGEND_NONE:
            return L::None;
        default:
            return L::TopRight;
    }
}

// Trim a line/scatter series to its configured ring-buffer capacity.
static void trim_to_capacity(SpectraSeries* s)
{
    if (!s || s->capacity == 0)
        return;
    const auto cap  = static_cast<size_t>(s->capacity);
    auto       trim = [cap](auto* series)
    {
        auto         x     = series->x_data();
        auto         y     = series->y_data();
        const size_t count = series->point_count();
        if (count > cap)
        {
            const size_t       offset = count - cap;
            std::vector<float> nx(x.begin() + static_cast<ptrdiff_t>(offset),
                                  x.begin() + static_cast<ptrdiff_t>(count));
            std::vector<float> ny(y.begin() + static_cast<ptrdiff_t>(offset),
                                  y.begin() + static_cast<ptrdiff_t>(count));
            series->set_x(nx);
            series->set_y(ny);
        }
    };

    if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
        trim(line);
    else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
        trim(sc);
}

extern "C"
{
    // ── Lifecycle ───────────────────────────────────────────────────────────────

    SpectraEmbed* spectra_embed_create(uint32_t width, uint32_t height)
    {
        auto* s = new (std::nothrow) SpectraEmbed(width, height);
        if (!s || !s->surface.is_valid())
        {
            delete s;
            return nullptr;
        }
        return s;
    }

    SpectraEmbed* spectra_embed_create_ex(uint32_t    width,
                                          uint32_t    height,
                                          const char* theme,
                                          float       dpi_scale,
                                          uint32_t    msaa,
                                          float       bg_alpha)
    {
        spectra::EmbedConfig cfg;
        cfg.width            = width;
        cfg.height           = height;
        cfg.dpi_scale        = dpi_scale > 0.0f ? dpi_scale : 1.0f;
        cfg.msaa             = msaa > 0 ? msaa : 1;
        cfg.background_alpha = bg_alpha;
        if (theme)
            cfg.theme = theme;

        auto* s = new (std::nothrow) SpectraEmbed(cfg);
        if (!s || !s->surface.is_valid())
        {
            delete s;
            return nullptr;
        }
        return s;
    }

    void spectra_embed_config_default(SpectraEmbedConfig* cfg)
    {
        if (!cfg)
            return;
        spectra::EmbedConfig def;
        cfg->width             = def.width;
        cfg->height            = def.height;
        cfg->msaa              = def.msaa;
        cfg->dpi_scale         = def.dpi_scale;
        cfg->background_alpha  = def.background_alpha;
        cfg->theme             = nullptr;
        cfg->show_imgui_chrome = def.show_imgui_chrome ? 1 : 0;
        cfg->show_command_bar  = def.show_command_bar ? 1 : 0;
        cfg->show_status_bar   = def.show_status_bar ? 1 : 0;
        cfg->show_nav_rail     = def.show_nav_rail ? 1 : 0;
        cfg->show_inspector    = def.show_inspector ? 1 : 0;
        cfg->show_legend       = def.show_legend ? 1 : 0;
        cfg->show_crosshair    = def.show_crosshair ? 1 : 0;
    }

    SpectraEmbed* spectra_embed_create_config(const SpectraEmbedConfig* in)
    {
        if (!in)
            return nullptr;

        spectra::EmbedConfig cfg;
        cfg.width            = in->width > 0 ? in->width : cfg.width;
        cfg.height           = in->height > 0 ? in->height : cfg.height;
        cfg.msaa             = in->msaa > 0 ? in->msaa : 1;
        cfg.dpi_scale        = in->dpi_scale > 0.0f ? in->dpi_scale : 1.0f;
        cfg.background_alpha = in->background_alpha;
        if (in->theme)
            cfg.theme = in->theme;
        // Any explicit chrome request implies the ImGui pipeline must run.
        cfg.show_imgui_chrome = (in->show_imgui_chrome != 0) || (in->show_command_bar != 0)
                                || (in->show_status_bar != 0) || (in->show_nav_rail != 0)
                                || (in->show_inspector != 0);
        cfg.show_command_bar = in->show_command_bar != 0;
        cfg.show_status_bar  = in->show_status_bar != 0;
        cfg.show_nav_rail    = in->show_nav_rail != 0;
        cfg.show_inspector   = in->show_inspector != 0;
        cfg.show_legend      = in->show_legend != 0;
        cfg.show_crosshair   = in->show_crosshair != 0;

        auto* s = new (std::nothrow) SpectraEmbed(cfg);
        if (!s || !s->surface.is_valid())
        {
            delete s;
            return nullptr;
        }
        return s;
    }

    void spectra_embed_destroy(SpectraEmbed* s)
    {
        delete s;
    }

    int spectra_embed_is_valid(const SpectraEmbed* s)
    {
        return (s && s->surface.is_valid()) ? 1 : 0;
    }

    // ── Figure management ───────────────────────────────────────────────────────

    SpectraFigure* spectra_embed_figure(SpectraEmbed* s)
    {
        if (!s)
            return nullptr;
        auto& fig = s->surface.figure();
        g_fig_pool.push_back(SpectraFigure{&fig});
        return &g_fig_pool.back();
    }

    SpectraFigure* spectra_embed_active_figure(SpectraEmbed* s)
    {
        if (!s)
            return nullptr;
        auto* fig = s->surface.active_figure();
        if (!fig)
            return nullptr;
        g_fig_pool.push_back(SpectraFigure{fig});
        return &g_fig_pool.back();
    }

    // ── Axes management ─────────────────────────────────────────────────────────

    SpectraAxes* spectra_figure_subplot(SpectraFigure* fig, int rows, int cols, int index)
    {
        if (!fig || !fig->ptr)
            return nullptr;
        auto& ax = fig->ptr->subplot(rows, cols, index);
        g_ax_pool.push_back(SpectraAxes{&ax, nullptr, &ax, fig->ptr});
        return &g_ax_pool.back();
    }

    SpectraAxes* spectra_figure_subplot3d(SpectraFigure* fig, int rows, int cols, int index)
    {
        if (!fig || !fig->ptr)
            return nullptr;
        auto& ax = fig->ptr->subplot3d(rows, cols, index);
        g_ax_pool.push_back(SpectraAxes{nullptr, &ax, &ax, fig->ptr});
        return &g_ax_pool.back();
    }

    // ── Series creation ─────────────────────────────────────────────────────────

    SpectraSeries* spectra_axes_line(SpectraAxes* ax,
                                     const float* x,
                                     const float* y,
                                     uint32_t     count,
                                     const char*  label)
    {
        if (!ax || !ax->axes_2d || !x || !y || count == 0)
            return nullptr;

        std::span<const float> xs(x, count);
        std::span<const float> ys(y, count);
        auto&                  series = ax->axes_2d->line(xs, ys);
        if (label && label[0] != '\0')
            series.label(label);

        g_series_pool.push_back(SpectraSeries{&series});
        return &g_series_pool.back();
    }

    SpectraSeries* spectra_axes_scatter(SpectraAxes* ax,
                                        const float* x,
                                        const float* y,
                                        uint32_t     count,
                                        const char*  label)
    {
        if (!ax || !ax->axes_2d || !x || !y || count == 0)
            return nullptr;

        std::span<const float> xs(x, count);
        std::span<const float> ys(y, count);
        auto&                  series = ax->axes_2d->scatter(xs, ys);
        if (label && label[0] != '\0')
            series.label(label);

        g_series_pool.push_back(SpectraSeries{&series});
        return &g_series_pool.back();
    }

    // ── Series data update ──────────────────────────────────────────────────────

    void spectra_series_set_x(SpectraSeries* s, const float* x, uint32_t count)
    {
        if (!s || !s->ptr || !x || count == 0)
            return;
        if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
            line->set_x({x, count});
        else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
            sc->set_x({x, count});
    }

    void spectra_series_set_y(SpectraSeries* s, const float* y, uint32_t count)
    {
        if (!s || !s->ptr || !y || count == 0)
            return;
        if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
            line->set_y({y, count});
        else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
            sc->set_y({y, count});
    }

    void spectra_series_set_data(SpectraSeries* s, const float* x, const float* y, uint32_t count)
    {
        if (!s || !s->ptr || !x || !y || count == 0)
            return;
        if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
        {
            line->set_x({x, count});
            line->set_y({y, count});
        }
        else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
        {
            sc->set_x({x, count});
            sc->set_y({y, count});
        }
    }

    // ── Series styling (Phase 1A) ────────────────────────────────────────────

    void spectra_series_set_color(SpectraSeries* s, float r, float g, float b, float a)
    {
        if (!s || !s->ptr)
            return;
        s->ptr->set_color(spectra::Color{r, g, b, a});
    }

    void spectra_series_set_opacity(SpectraSeries* s, float v)
    {
        if (s && s->ptr)
            s->ptr->opacity(v);
    }

    void spectra_series_set_line_width(SpectraSeries* s, float v)
    {
        if (!s || !s->ptr)
            return;
        if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
            line->width(v);
        else if (auto* l3 = dynamic_cast<spectra::LineSeries3D*>(s->ptr))
            l3->width(v);
        else
            s->ptr->plot_style_mut().line_width = v;
    }

    void spectra_series_set_marker_size(SpectraSeries* s, float v)
    {
        if (s && s->ptr)
            s->ptr->marker_size(v);
    }

    void spectra_series_set_marker_style(SpectraSeries* s, int style)
    {
        if (s && s->ptr)
            s->ptr->marker_style(to_marker_style(style));
    }

    void spectra_series_set_line_style(SpectraSeries* s, int style)
    {
        if (s && s->ptr)
            s->ptr->line_style(to_line_style(style));
    }

    void spectra_series_set_label(SpectraSeries* s, const char* label)
    {
        if (s && s->ptr && label)
            s->ptr->label(label);
    }

    // ── Series streaming helpers (Phase 1G) ──────────────────────────────────

    void spectra_series_append_xy(SpectraSeries* s, float x, float y)
    {
        if (!s || !s->ptr)
            return;
        s->ptr->append(x, y);
        trim_to_capacity(s);
    }

    void spectra_series_append_data(SpectraSeries* s,
                                    const float*   x,
                                    const float*   y,
                                    uint32_t       count)
    {
        if (!s || !s->ptr || !x || !y || count == 0)
            return;
        for (uint32_t i = 0; i < count; ++i)
            s->ptr->append(x[i], y[i]);
        trim_to_capacity(s);
    }

    void spectra_series_set_capacity(SpectraSeries* s, uint32_t max_points)
    {
        if (!s)
            return;
        s->capacity = max_points;
        trim_to_capacity(s);
    }

    void spectra_series_clear(SpectraSeries* s)
    {
        if (!s || !s->ptr)
            return;
        std::span<const float> empty{};
        if (auto* line = dynamic_cast<spectra::LineSeries*>(s->ptr))
        {
            line->set_x(empty);
            line->set_y(empty);
        }
        else if (auto* sc = dynamic_cast<spectra::ScatterSeries*>(s->ptr))
        {
            sc->set_x(empty);
            sc->set_y(empty);
        }
    }

    // ── Bar series options (Phase 1B) ────────────────────────────────────────

    void spectra_series_set_bar_width(SpectraSeries* s, float width)
    {
        if (auto* bar = (s && s->ptr) ? dynamic_cast<spectra::BarSeries*>(s->ptr) : nullptr)
            bar->bar_width(width);
    }

    void spectra_series_set_bar_baseline(SpectraSeries* s, float baseline)
    {
        if (auto* bar = (s && s->ptr) ? dynamic_cast<spectra::BarSeries*>(s->ptr) : nullptr)
            bar->baseline(baseline);
    }

    void spectra_series_set_bar_orientation(SpectraSeries* s, int orientation)
    {
        if (auto* bar = (s && s->ptr) ? dynamic_cast<spectra::BarSeries*>(s->ptr) : nullptr)
            bar->orientation(orientation == SPECTRA_BAR_HORIZONTAL
                                 ? spectra::BarOrientation::Horizontal
                                 : spectra::BarOrientation::Vertical);
    }

    void spectra_series_set_bar_gradient(SpectraSeries* s, int enabled)
    {
        if (auto* bar = (s && s->ptr) ? dynamic_cast<spectra::BarSeries*>(s->ptr) : nullptr)
            bar->gradient(enabled != 0);
    }

    // ── Histogram series options (Phase 1C) ──────────────────────────────────

    void spectra_series_set_histogram_bins(SpectraSeries* s, int bins)
    {
        if (auto* h = (s && s->ptr) ? dynamic_cast<spectra::HistogramSeries*>(s->ptr) : nullptr)
        {
            h->bins(bins > 0 ? bins : 30);
            h->rebuild_geometry();
        }
    }

    void spectra_series_set_histogram_cumulative(SpectraSeries* s, int enabled)
    {
        if (auto* h = (s && s->ptr) ? dynamic_cast<spectra::HistogramSeries*>(s->ptr) : nullptr)
        {
            h->cumulative(enabled != 0);
            h->rebuild_geometry();
        }
    }

    void spectra_series_set_histogram_density(SpectraSeries* s, int enabled)
    {
        if (auto* h = (s && s->ptr) ? dynamic_cast<spectra::HistogramSeries*>(s->ptr) : nullptr)
        {
            h->density(enabled != 0);
            h->rebuild_geometry();
        }
    }

    void spectra_series_set_histogram_gradient(SpectraSeries* s, int enabled)
    {
        if (auto* h = (s && s->ptr) ? dynamic_cast<spectra::HistogramSeries*>(s->ptr) : nullptr)
        {
            h->gradient(enabled != 0);
            h->rebuild_geometry();
        }
    }

    // ── 3D series (Phase 1D) ─────────────────────────────────────────────────

    SpectraSeries* spectra_axes3d_line(SpectraAxes* ax,
                                       const float* x,
                                       const float* y,
                                       const float* z,
                                       uint32_t     count,
                                       const char*  label)
    {
        if (!ax || !ax->axes_3d || !x || !y || !z || count == 0)
            return nullptr;
        auto& series = ax->axes_3d->line3d({x, count}, {y, count}, {z, count});
        if (label && label[0] != '\0')
            series.label(label);
        g_series_pool.push_back(SpectraSeries{&series, 0});
        return &g_series_pool.back();
    }

    SpectraSeries* spectra_axes3d_scatter(SpectraAxes* ax,
                                          const float* x,
                                          const float* y,
                                          const float* z,
                                          uint32_t     count,
                                          const char*  label)
    {
        if (!ax || !ax->axes_3d || !x || !y || !z || count == 0)
            return nullptr;
        auto& series = ax->axes_3d->scatter3d({x, count}, {y, count}, {z, count});
        if (label && label[0] != '\0')
            series.label(label);
        g_series_pool.push_back(SpectraSeries{&series, 0});
        return &g_series_pool.back();
    }

    SpectraSeries* spectra_axes3d_surf(SpectraAxes* ax,
                                       const float* x_grid,
                                       uint32_t     nx,
                                       const float* y_grid,
                                       uint32_t     ny,
                                       const float* z_values,
                                       const char*  label)
    {
        if (!ax || !ax->axes_3d || !x_grid || !y_grid || !z_values || nx == 0 || ny == 0)
            return nullptr;
        auto& series = ax->axes_3d->surface({x_grid, nx},
                                            {y_grid, ny},
                                            {z_values, static_cast<size_t>(nx) * ny});
        if (label && label[0] != '\0')
            series.label(label);
        g_series_pool.push_back(SpectraSeries{&series, 0});
        return &g_series_pool.back();
    }

    void spectra_series_set_z(SpectraSeries* s, const float* z, uint32_t count)
    {
        if (!s || !s->ptr || !z || count == 0)
            return;
        if (auto* l3 = dynamic_cast<spectra::LineSeries3D*>(s->ptr))
            l3->set_z({z, count});
        else if (auto* s3 = dynamic_cast<spectra::ScatterSeries3D*>(s->ptr))
            s3->set_z({z, count});
    }

    void spectra_series_set_colormap(SpectraSeries* s, int colormap)
    {
        if (auto* surf = (s && s->ptr) ? dynamic_cast<spectra::SurfaceSeries*>(s->ptr) : nullptr)
            surf->colormap(to_colormap(colormap));
    }

    void spectra_series_set_colormap_range(SpectraSeries* s, float min_val, float max_val)
    {
        if (auto* surf = (s && s->ptr) ? dynamic_cast<spectra::SurfaceSeries*>(s->ptr) : nullptr)
            surf->colormap_range(min_val, max_val);
    }

    // ── Rendering ───────────────────────────────────────────────────────────────

    int spectra_embed_render(SpectraEmbed* s, uint8_t* out_rgba)
    {
        if (!s || !out_rgba)
            return 0;
        return s->surface.render_to_buffer(out_rgba) ? 1 : 0;
    }

    int spectra_embed_resize(SpectraEmbed* s, uint32_t width, uint32_t height)
    {
        if (!s)
            return 0;
        return s->surface.resize(width, height) ? 1 : 0;
    }

    int spectra_embed_render_png(SpectraEmbed* s, const char* path)
    {
        if (!s || !path)
            return 0;
        uint32_t             w = s->surface.width();
        uint32_t             h = s->surface.height();
        std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
        if (!s->surface.render_to_buffer(buf.data()))
            return 0;
        return spectra::ImageExporter::write_png(path, buf.data(), w, h) ? 1 : 0;
    }

    uint32_t spectra_embed_width(const SpectraEmbed* s)
    {
        return s ? s->surface.width() : 0;
    }

    uint32_t spectra_embed_height(const SpectraEmbed* s)
    {
        return s ? s->surface.height() : 0;
    }

    // ── Input forwarding ────────────────────────────────────────────────────────

    void spectra_embed_mouse_move(SpectraEmbed* s, float x, float y)
    {
        if (s)
            s->surface.inject_mouse_move(x, y);
    }

    void spectra_embed_mouse_button(SpectraEmbed* s,
                                    int           button,
                                    int           action,
                                    int           mods,
                                    float         x,
                                    float         y)
    {
        if (s)
            s->surface.inject_mouse_button(button, action, mods, x, y);
    }

    void spectra_embed_scroll(SpectraEmbed* s, float dx, float dy, float cx, float cy)
    {
        if (s)
            s->surface.inject_scroll(dx, dy, cx, cy);
    }

    void spectra_embed_key(SpectraEmbed* s, int key, int action, int mods)
    {
        if (s)
            s->surface.inject_key(key, action, mods);
    }

    void spectra_embed_update(SpectraEmbed* s, float dt)
    {
        if (s)
            s->surface.update(dt);
    }

    // ── Display configuration ────────────────────────────────────────────────────

    void spectra_embed_set_dpi_scale(SpectraEmbed* s, float scale)
    {
        if (s)
            s->surface.set_dpi_scale(scale);
    }

    float spectra_embed_get_dpi_scale(const SpectraEmbed* s)
    {
        return s ? s->surface.dpi_scale() : 1.0f;
    }

    void spectra_embed_set_background_alpha(SpectraEmbed* s, float alpha)
    {
        if (s)
            s->surface.set_background_alpha(alpha);
    }

    float spectra_embed_get_background_alpha(const SpectraEmbed* s)
    {
        return s ? s->surface.background_alpha() : 1.0f;
    }

    // ── Theme & UI chrome ────────────────────────────────────────────────────────

    void spectra_embed_set_theme(SpectraEmbed* s, const char* theme)
    {
        if (!s || !theme)
            return;
        // Theme is applied via the embed-owned ThemeManager registered through
        // set_current(); instance() returns that object.
        spectra::ui::ThemeManager::instance().set_theme(theme);
    }

    void spectra_embed_set_show_command_bar(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_command_bar(visible != 0);
    }

    void spectra_embed_set_show_status_bar(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_status_bar(visible != 0);
    }

    void spectra_embed_set_show_nav_rail(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_nav_rail(visible != 0);
    }

    void spectra_embed_set_show_inspector(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_inspector(visible != 0);
    }

    void spectra_embed_set_show_legend(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_legend(visible != 0);
    }

    void spectra_embed_set_show_crosshair(SpectraEmbed* s, int visible)
    {
        if (s)
            s->surface.set_show_crosshair(visible != 0);
    }

    int spectra_embed_is_command_bar_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_command_bar()) ? 1 : 0;
    }

    int spectra_embed_is_status_bar_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_status_bar()) ? 1 : 0;
    }

    int spectra_embed_is_nav_rail_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_nav_rail()) ? 1 : 0;
    }

    int spectra_embed_is_inspector_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_inspector()) ? 1 : 0;
    }

    int spectra_embed_is_legend_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_legend()) ? 1 : 0;
    }

    int spectra_embed_is_crosshair_visible(const SpectraEmbed* s)
    {
        return (s && s->surface.show_crosshair()) ? 1 : 0;
    }

    // ── Animation & live data (Phase 4) ──────────────────────────────────────

    void spectra_embed_set_on_frame(SpectraEmbed* s, SpectraFrameCb cb, void* user_data)
    {
        if (!s)
            return;
        s->frame_cb = cb;
        s->frame_ud = user_data;
        if (cb)
        {
            SpectraEmbed* self = s;
            s->surface.set_frame_callback([self](float t, float dt)
                                          { self->frame_cb(self, t, dt, self->frame_ud); });
        }
        else
        {
            s->surface.clear_frame_callback();
        }
    }

    void spectra_embed_clear_on_frame(SpectraEmbed* s)
    {
        if (!s)
            return;
        s->frame_cb = nullptr;
        s->frame_ud = nullptr;
        s->surface.clear_frame_callback();
    }

    void spectra_embed_set_redraw_callback(SpectraEmbed* s, SpectraRedrawCb cb, void* user_data)
    {
        if (!s)
            return;
        s->redraw_cb = cb;
        s->redraw_ud = user_data;
        if (cb)
        {
            SpectraEmbed* self = s;
            s->surface.set_redraw_callback([self]() { self->redraw_cb(self->redraw_ud); });
        }
        else
        {
            s->surface.set_redraw_callback(nullptr);
        }
    }

    int spectra_embed_animation_play(SpectraEmbed* s, float fps, float duration_sec)
    {
        if (!s)
            return 0;
        float step = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 60.0f);
        if (duration_sec <= 0.0f)
        {
            s->surface.update(step);
            return 1;
        }
        int frames = 0;
        for (float t = 0.0f; t < duration_sec; t += step)
        {
            s->surface.update(step);
            ++frames;
        }
        return frames;
    }

    void spectra_embed_animation_stop(SpectraEmbed* s)
    {
        if (s)
            s->surface.reset_frame_clock();
    }

    // ── Interactive event callbacks (Phase 3) ────────────────────────────────────

    void spectra_embed_set_on_point_selected(SpectraEmbed*          s,
                                             SpectraPointSelectedCb cb,
                                             void*                  user_data)
    {
        if (!s)
            return;
        if (!cb)
        {
            s->surface.set_on_point_selected(nullptr);
            return;
        }
        s->surface.set_on_point_selected(
            [cb, user_data](int ai, int si, std::size_t pi, double x, double y)
            { cb(ai, si, pi, x, y, user_data); });
    }

    void spectra_embed_set_on_series_selected(SpectraEmbed*           s,
                                              SpectraSeriesSelectedCb cb,
                                              void*                   user_data)
    {
        if (!s)
            return;
        if (!cb)
        {
            s->surface.set_on_series_selected(nullptr);
            return;
        }
        s->surface.set_on_series_selected([cb, user_data](int ai, int si)
                                          { cb(ai, si, user_data); });
    }

    void spectra_embed_set_on_hover(SpectraEmbed* s, SpectraHoverCb cb, void* user_data)
    {
        if (!s)
            return;
        if (!cb)
        {
            s->surface.set_on_hover(nullptr);
            return;
        }
        s->surface.set_on_hover([cb, user_data](int ai, int si, std::size_t pi, double x, double y)
                                { cb(ai, si, pi, x, y, user_data); });
    }

    void spectra_embed_set_on_view_changed(SpectraEmbed*        s,
                                           SpectraViewChangedCb cb,
                                           void*                user_data)
    {
        if (!s)
            return;
        if (!cb)
        {
            s->surface.set_on_view_changed(nullptr);
            return;
        }
        s->surface.set_on_view_changed(
            [cb, user_data](double xmin, double xmax, double ymin, double ymax)
            { cb(xmin, xmax, ymin, ymax, user_data); });
    }

    // ── Axes configuration ──────────────────────────────────────────────────────

    void spectra_axes_set_xlabel(SpectraAxes* ax, const char* label)
    {
        if (!ax || !ax->axes_2d || !label)
            return;
        ax->axes_2d->xlabel(label);
    }

    void spectra_axes_set_ylabel(SpectraAxes* ax, const char* label)
    {
        if (!ax || !ax->axes_2d || !label)
            return;
        ax->axes_2d->ylabel(label);
    }

    void spectra_axes_set_title(SpectraAxes* ax, const char* title)
    {
        if (!ax || !ax->axes_2d || !title)
            return;
        ax->axes_2d->title(title);
    }

    void spectra_axes_set_xlim(SpectraAxes* ax, float min_val, float max_val)
    {
        if (!ax || !ax->axes_2d)
            return;
        ax->axes_2d->xlim(static_cast<double>(min_val), static_cast<double>(max_val));
    }

    void spectra_axes_set_ylim(SpectraAxes* ax, float min_val, float max_val)
    {
        if (!ax || !ax->axes_2d)
            return;
        ax->axes_2d->ylim(static_cast<double>(min_val), static_cast<double>(max_val));
    }

    void spectra_axes_set_grid(SpectraAxes* ax, int enabled)
    {
        if (!ax || !ax->axes_2d)
            return;
        ax->axes_2d->grid(enabled != 0);
    }

    void spectra_axes_show_border(SpectraAxes* ax, int enabled)
    {
        if (!ax || !ax->axes_2d)
            return;
        ax->axes_2d->show_border(enabled != 0);
    }

    void spectra_axes_auto_fit(SpectraAxes* ax)
    {
        if (!ax || !ax->axes_2d)
            return;
        ax->axes_2d->auto_fit();
    }

    void spectra_axes_set_xscale(SpectraAxes* ax, int scale_type)
    {
        if (!ax || !ax->axes_2d)
            return;
        switch (scale_type)
        {
            case SPECTRA_SCALE_LOG10:
                ax->axes_2d->xscale(spectra::ScaleType::Log10);
                break;
            case SPECTRA_SCALE_LOG2:
                ax->axes_2d->xscale(spectra::ScaleType::Log2);
                break;
            case SPECTRA_SCALE_SQRT:
                ax->axes_2d->xscale(spectra::ScaleType::Sqrt);
                break;
            default:
                ax->axes_2d->xscale(spectra::ScaleType::Linear);
                break;
        }
    }

    void spectra_axes_set_yscale(SpectraAxes* ax, int scale_type)
    {
        if (!ax || !ax->axes_2d)
            return;
        switch (scale_type)
        {
            case SPECTRA_SCALE_LOG10:
                ax->axes_2d->yscale(spectra::ScaleType::Log10);
                break;
            case SPECTRA_SCALE_LOG2:
                ax->axes_2d->yscale(spectra::ScaleType::Log2);
                break;
            case SPECTRA_SCALE_SQRT:
                ax->axes_2d->yscale(spectra::ScaleType::Sqrt);
                break;
            default:
                ax->axes_2d->yscale(spectra::ScaleType::Linear);
                break;
        }
    }

    void spectra_axes_show_legend(SpectraAxes* ax, int visible)
    {
        if (!ax || !ax->fig)
            return;
        ax->fig->legend().visible = (visible != 0);
    }

    void spectra_axes_set_legend_position(SpectraAxes* ax, int position)
    {
        if (!ax || !ax->fig)
            return;
        ax->fig->legend().position = to_legend_position(position);
        if (position == SPECTRA_LEGEND_NONE)
            ax->fig->legend().visible = false;
    }

    SpectraSeries* spectra_axes_histogram(SpectraAxes* ax,
                                          const float* values,
                                          uint32_t     count,
                                          int          bins,
                                          const char*  label)
    {
        if (!ax || !ax->axes_2d || !values || count == 0)
            return nullptr;

        std::span<const float> vs(values, count);
        auto&                  series = ax->axes_2d->histogram(vs, bins > 0 ? bins : 30);
        if (label && label[0] != '\0')
            series.label(label);

        g_series_pool.push_back(SpectraSeries{&series});
        return &g_series_pool.back();
    }

    SpectraSeries* spectra_axes_bar(SpectraAxes* ax,
                                    const float* positions,
                                    const float* heights,
                                    uint32_t     count,
                                    const char*  label)
    {
        if (!ax || !ax->axes_2d || !positions || !heights || count == 0)
            return nullptr;

        std::span<const float> ps(positions, count);
        std::span<const float> hs(heights, count);
        auto&                  series = ax->axes_2d->bar(ps, hs);
        if (label && label[0] != '\0')
            series.label(label);

        g_series_pool.push_back(SpectraSeries{&series});
        return &g_series_pool.back();
    }

    SpectraSeries* spectra_axes_stem(SpectraAxes* ax,
                                     const float* x,
                                     const float* y,
                                     uint32_t     count,
                                     const char*  label)
    {
        if (!ax || !ax->axes_2d || !x || !y || count == 0)
            return nullptr;

        std::span<const float> xs(x, count);
        std::span<const float> ys(y, count);
        auto&                  series = ax->axes_2d->stem(xs, ys);
        if (label && label[0] != '\0')
            series.label(label);
        // Default stem appearance: filled circle heads, thin stems
        series.stem_width(1.5f);
        series.marker_style(spectra::MarkerStyle::FilledCircle);
        series.marker_size(6.0f);

        g_series_pool.push_back(SpectraSeries{&series});
        return &g_series_pool.back();
    }

    // ── Scatter colormap (Phase 7C) ────────────────────────────────────────

    void spectra_series_set_scatter_colors(SpectraSeries* s, const float* values, uint32_t count)
    {
        if (auto* sc = (s && s->ptr) ? dynamic_cast<spectra::ScatterSeries*>(s->ptr) : nullptr)
            sc->color_values({values, count});
    }

    void spectra_series_set_scatter_colormap(SpectraSeries* s, int colormap)
    {
        if (auto* sc = (s && s->ptr) ? dynamic_cast<spectra::ScatterSeries*>(s->ptr) : nullptr)
            sc->colormap(to_colormap(colormap));
    }

    void spectra_series_set_scatter_colormap_range(SpectraSeries* s, float min_val, float max_val)
    {
        if (auto* sc = (s && s->ptr) ? dynamic_cast<spectra::ScatterSeries*>(s->ptr) : nullptr)
            sc->colormap_range(min_val, max_val);
    }

    // ── Figure configuration ────────────────────────────────────────────────────

    void spectra_figure_set_title(SpectraFigure* fig, const char* title)
    {
        if (!fig || !fig->ptr || !title)
            return;
        // Figure doesn't have a public set_title; set the first axes title instead.
        // This is a convenience for single-subplot figures.
        if (!fig->ptr->axes().empty() && fig->ptr->axes()[0])
            fig->ptr->axes()[0]->title(title);
    }

    // ── Easy Render API ──────────────────────────────────────────────────────────

    // Internal helper: create surface, add series, render to heap-allocated buffer.
    static uint8_t* render_easy(const float* x,
                                const float* y,
                                uint32_t     count,
                                uint32_t     width,
                                uint32_t     height,
                                uint32_t*    out_width,
                                uint32_t*    out_height,
                                bool         scatter)
    {
        if (!x || !y || count == 0 || width == 0 || height == 0)
            return nullptr;

        spectra::EmbedSurface surface(spectra::EmbedConfig{width, height});
        if (!surface.is_valid())
            return nullptr;

        auto& fig = surface.figure();
        auto& ax  = fig.subplot(1, 1, 1);

        std::span<const float> xs(x, count);
        std::span<const float> ys(y, count);

        if (scatter)
            ax.scatter(xs, ys);
        else
            ax.line(xs, ys);

        ax.auto_fit();

        auto* buf = new (std::nothrow) uint8_t[width * height * 4];
        if (!buf)
            return nullptr;

        if (!surface.render_to_buffer(buf))
        {
            delete[] buf;
            return nullptr;
        }

        if (out_width)
            *out_width = width;
        if (out_height)
            *out_height = height;
        return buf;
    }

    uint8_t* spectra_render_line(const float* x,
                                 const float* y,
                                 uint32_t     count,
                                 uint32_t     width,
                                 uint32_t     height,
                                 uint32_t*    out_width,
                                 uint32_t*    out_height)
    {
        return render_easy(x, y, count, width, height, out_width, out_height, false);
    }

    uint8_t* spectra_render_scatter(const float* x,
                                    const float* y,
                                    uint32_t     count,
                                    uint32_t     width,
                                    uint32_t     height,
                                    uint32_t*    out_width,
                                    uint32_t*    out_height)
    {
        return render_easy(x, y, count, width, height, out_width, out_height, true);
    }

    int spectra_render_line_png(const float* x,
                                const float* y,
                                uint32_t     count,
                                uint32_t     width,
                                uint32_t     height,
                                const char*  path)
    {
        if (!path)
            return 0;
        uint32_t w   = 0;
        uint32_t h   = 0;
        uint8_t* buf = render_easy(x, y, count, width, height, &w, &h, false);
        if (!buf)
            return 0;
        bool ok = spectra::ImageExporter::write_png(path, buf, w, h);
        delete[] buf;
        return ok ? 1 : 0;
    }

    int spectra_render_scatter_png(const float* x,
                                   const float* y,
                                   uint32_t     count,
                                   uint32_t     width,
                                   uint32_t     height,
                                   const char*  path)
    {
        if (!path)
            return 0;
        uint32_t w   = 0;
        uint32_t h   = 0;
        uint8_t* buf = render_easy(x, y, count, width, height, &w, &h, true);
        if (!buf)
            return 0;
        bool ok = spectra::ImageExporter::write_png(path, buf, w, h);
        delete[] buf;
        return ok ? 1 : 0;
    }

    void spectra_free_pixels(uint8_t* pixels)
    {
        delete[] pixels;
    }

}   // extern "C"
