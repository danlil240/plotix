// figure_snapshot.cpp — Implementation of shared IPC figure snapshot utilities.

#include "figure_snapshot.hpp"

#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <cmath>

namespace spectra::ipc
{

std::unique_ptr<spectra::Figure> build_figure_from_snapshot(
    const SnapshotFigureState& snap,
    uint32_t                   override_width,
    uint32_t                   override_height)
{
    spectra::FigureConfig cfg;
    cfg.width  = (override_width > 0) ? override_width : snap.width;
    cfg.height = (override_height > 0) ? override_height : snap.height;
    auto fig   = std::make_unique<spectra::Figure>(cfg);

    int rows = std::max(snap.grid_rows, int32_t(1));
    int cols = std::max(snap.grid_cols, int32_t(1));

    size_t num_axes = std::max(snap.axes.size(), size_t(1));
    for (size_t i = 0; i < num_axes; ++i)
    {
        bool axes_is_3d = (i < snap.axes.size()) && snap.axes[i].is_3d;

        if (axes_is_3d)
        {
            auto&       ax3d = fig->subplot3d(rows, cols, static_cast<int>(i + 1));
            const auto& sa   = snap.axes[i];
            ax3d.xlim(sa.x_min, sa.x_max);
            ax3d.ylim(sa.y_min, sa.y_max);
            ax3d.zlim(sa.z_min, sa.z_max);
            ax3d.grid(sa.grid_visible);
            if (!sa.x_label.empty())
                ax3d.xlabel(sa.x_label);
            if (!sa.y_label.empty())
                ax3d.ylabel(sa.y_label);
            if (!sa.title.empty())
                ax3d.title(sa.title);

            for (const auto& ss : snap.series)
            {
                if (!is_3d_series_type(ss.type))
                    continue;
                if (ss.axes_index != static_cast<uint32_t>(i))
                    continue;

                std::vector<float> xs, ys, zs;
                for (size_t j = 0; j + 2 < ss.data.size(); j += 3)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                    zs.push_back(ss.data[j + 2]);
                }

                if (ss.type == "scatter3d")
                {
                    auto& s = ax3d.scatter3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else if (ss.type == "surface")
                {
                    std::vector<float> ux(xs.begin(), xs.end());
                    std::vector<float> uy(ys.begin(), ys.end());
                    std::sort(ux.begin(), ux.end());
                    ux.erase(std::unique(ux.begin(), ux.end(),
                                         [](float a, float b) { return std::abs(a - b) < 1e-6f; }),
                             ux.end());
                    std::sort(uy.begin(), uy.end());
                    uy.erase(std::unique(uy.begin(), uy.end(),
                                         [](float a, float b) { return std::abs(a - b) < 1e-6f; }),
                             uy.end());

                    size_t ncols = ux.size();
                    size_t nrows = uy.size();
                    std::vector<float> z_grid(nrows * ncols, 0.0f);
                    for (size_t k = 0; k < xs.size(); ++k)
                    {
                        auto cit = std::lower_bound(ux.begin(), ux.end(), xs[k] - 1e-6f);
                        auto ci  = static_cast<size_t>(std::distance(ux.begin(), cit));
                        if (ci >= ncols) ci = ncols - 1;
                        auto rit = std::lower_bound(uy.begin(), uy.end(), ys[k] - 1e-6f);
                        auto ri  = static_cast<size_t>(std::distance(uy.begin(), rit));
                        if (ri >= nrows) ri = nrows - 1;
                        z_grid[ri * ncols + ci] = zs[k];
                    }

                    auto& s = ax3d.surface(ux, uy, z_grid);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else   // "line3d" or "mesh" (mesh treated as line3d)
                {
                    auto& s = ax3d.line3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
        else
        {
            auto& ax = fig->subplot(rows, cols, static_cast<int>(i + 1));
            if (i < snap.axes.size())
            {
                const auto& sa = snap.axes[i];
                ax.xlim(sa.x_min, sa.x_max);
                ax.ylim(sa.y_min, sa.y_max);
                ax.grid(sa.grid_visible);
                if (!sa.x_label.empty())
                    ax.xlabel(sa.x_label);
                if (!sa.y_label.empty())
                    ax.ylabel(sa.y_label);
                if (!sa.title.empty())
                    ax.title(sa.title);
            }

            for (const auto& ss : snap.series)
            {
                if (is_3d_series_type(ss.type))
                    continue;
                if (ss.axes_index != static_cast<uint32_t>(i))
                    continue;

                std::vector<float> xs, ys;
                for (size_t j = 0; j + 1 < ss.data.size(); j += 2)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                }
                if (ss.type == "scatter")
                {
                    auto& s = ax.scatter(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else
                {
                    auto& s = ax.line(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
    }

    if (snap.live_fps > 0.0f)
    {
        fig->anim_.live_streaming = true;
        fig->anim_.fps            = snap.live_fps;
    }

    return fig;
}

void apply_diff_op_to_cache(SnapshotFigureState& fig, const DiffOp& op)
{
    switch (op.type)
    {
        case DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].x_min = op.f1;
                fig.axes[op.axes_index].x_max = op.f2;
                fig.axes[op.axes_index].y_min = op.f3;
                fig.axes[op.axes_index].y_max = op.f4;
            }
            break;
        case DiffOp::Type::SET_SERIES_COLOR:
            if (op.series_index < fig.series.size())
            {
                fig.series[op.series_index].color_r = op.f1;
                fig.series[op.series_index].color_g = op.f2;
                fig.series[op.series_index].color_b = op.f3;
                fig.series[op.series_index].color_a = op.f4;
            }
            break;
        case DiffOp::Type::SET_SERIES_VISIBLE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].visible = op.bool_val;
            break;
        case DiffOp::Type::SET_FIGURE_TITLE:
            fig.title = op.str_val;
            break;
        case DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].grid_visible = op.bool_val;
            break;
        case DiffOp::Type::SET_LINE_WIDTH:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].line_width = op.f1;
            break;
        case DiffOp::Type::SET_MARKER_SIZE:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].marker_size = op.f1;
            break;
        case DiffOp::Type::SET_OPACITY:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].opacity = op.f1;
            break;
        case DiffOp::Type::SET_SERIES_DATA:
            if (op.series_index < fig.series.size())
            {
                fig.series[op.series_index].data        = op.data;
                fig.series[op.series_index].point_count = static_cast<uint32_t>(op.data.size() / 2);
            }
            break;
        case DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].z_min = op.f1;
                fig.axes[op.axes_index].z_max = op.f2;
            }
            break;
        case DiffOp::Type::ADD_SERIES:
        {
            SnapshotSeriesState s;
            s.type       = op.str_val;
            s.axes_index = op.axes_index;
            while (fig.series.size() <= op.series_index)
                fig.series.push_back({});
            fig.series[op.series_index] = std::move(s);
            break;
        }
        case DiffOp::Type::ADD_AXES:
        {
            SnapshotAxisState ax;
            ax.is_3d = op.bool_val;
            while (fig.axes.size() <= op.axes_index)
                fig.axes.push_back({});
            fig.axes[op.axes_index] = std::move(ax);
            break;
        }
        case DiffOp::Type::SET_SERIES_LABEL:
            if (op.series_index < fig.series.size())
                fig.series[op.series_index].name = op.str_val;
            break;
        case DiffOp::Type::SET_AXIS_XLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].x_label = op.str_val;
            break;
        case DiffOp::Type::SET_AXIS_YLABEL:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].y_label = op.str_val;
            break;
        case DiffOp::Type::SET_AXIS_TITLE:
            if (op.axes_index < fig.axes.size())
                fig.axes[op.axes_index].title = op.str_val;
            break;
        default:
            break;
    }
}

void apply_diff_op_to_figure(spectra::Figure& fig, const DiffOp& op)
{
    switch (op.type)
    {
        case DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                fig.axes_mut()[op.axes_index]->xlim(op.f1, op.f2);
                fig.axes_mut()[op.axes_index]->ylim(op.f3, op.f4);
            }
            break;
        case DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->grid(op.bool_val);
            break;
        case DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(fig.axes_mut()[op.axes_index].get());
                if (ax3d) ax3d->zlim(op.f1, op.f2);
            }
            break;
        case DiffOp::Type::ADD_SERIES:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax   = fig.axes_mut()[op.axes_index].get();
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(ax);
                if (ax3d)
                {
                    if (op.str_val == "scatter3d")
                        ax3d->scatter3d({}, {}, {});
                    else if (op.str_val == "surface")
                        ax3d->surface({}, {}, {});
                    else
                        ax3d->line3d({}, {}, {});
                }
                else
                {
                    if (op.str_val == "scatter")
                        ax->scatter({}, {});
                    else
                        ax->line({}, {});
                }
            }
            break;
        case DiffOp::Type::SET_SERIES_DATA:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto& series_vec = fig.axes_mut()[op.axes_index]->series_mut();
                if (op.series_index < series_vec.size() && series_vec[op.series_index])
                {
                    auto* s = series_vec[op.series_index].get();
                    if (auto* line3d = dynamic_cast<spectra::LineSeries3D*>(s))
                    {
                        size_t n = op.data.size() / 3;
                        std::vector<float> xv(n), yv(n), zv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 3];
                            yv[i] = op.data[i * 3 + 1];
                            zv[i] = op.data[i * 3 + 2];
                        }
                        line3d->set_x(xv);
                        line3d->set_y(yv);
                        line3d->set_z(zv);
                    }
                    else if (auto* scatter3d = dynamic_cast<spectra::ScatterSeries3D*>(s))
                    {
                        size_t n = op.data.size() / 3;
                        std::vector<float> xv(n), yv(n), zv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 3];
                            yv[i] = op.data[i * 3 + 1];
                            zv[i] = op.data[i * 3 + 2];
                        }
                        scatter3d->set_x(xv);
                        scatter3d->set_y(yv);
                        scatter3d->set_z(zv);
                    }
                    else if (auto* line = dynamic_cast<spectra::LineSeries*>(s))
                    {
                        size_t n = op.data.size() / 2;
                        std::vector<float> xv(n), yv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 2];
                            yv[i] = op.data[i * 2 + 1];
                        }
                        line->set_x(xv);
                        line->set_y(yv);
                    }
                    else if (auto* scatter = dynamic_cast<spectra::ScatterSeries*>(s))
                    {
                        size_t n = op.data.size() / 2;
                        std::vector<float> xv(n), yv(n);
                        for (size_t i = 0; i < n; ++i)
                        {
                            xv[i] = op.data[i * 2];
                            yv[i] = op.data[i * 2 + 1];
                        }
                        scatter->set_x(xv);
                        scatter->set_y(yv);
                    }
                }
            }
            break;
        case DiffOp::Type::SET_SERIES_LABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto& series_vec = fig.axes_mut()[op.axes_index]->series_mut();
                if (op.series_index < series_vec.size() && series_vec[op.series_index])
                    series_vec[op.series_index]->label(op.str_val);
            }
            break;
        case DiffOp::Type::SET_AXIS_XLABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->xlabel(op.str_val);
            break;
        case DiffOp::Type::SET_AXIS_YLABEL:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->ylabel(op.str_val);
            break;
        case DiffOp::Type::SET_AXIS_TITLE:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
                fig.axes_mut()[op.axes_index]->title(op.str_val);
            break;
        case DiffOp::Type::SET_LIVE_FPS:
            fig.anim_.live_streaming = op.bool_val;
            if (op.f1 > 0.0)
                fig.anim_.fps = static_cast<float>(op.f1);
            break;
        default:
            break;
    }
}

std::vector<spectra::FigureId> rebuild_registry_from_cache(
    spectra::FigureRegistry&                registry,
    const std::vector<SnapshotFigureState>& cache,
    uint32_t                                width,
    uint32_t                                height)
{
    for (auto id : registry.all_ids())
        registry.unregister_figure(id);

    std::vector<spectra::FigureId> ids;
    for (const auto& snap : cache)
    {
        auto fig = build_figure_from_snapshot(snap, width, height);
        auto id  = registry.register_figure(std::move(fig));
        ids.push_back(id);
    }
    return ids;
}

}   // namespace spectra::ipc
