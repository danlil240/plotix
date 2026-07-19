// render_upload.cpp — Series GPU data upload and dirty tracking.
// Split from renderer.cpp (MR-1) for focused module ownership.

#include "renderer.hpp"

#include <cstring>
#include <limits>
#include <spectra/chunked_series.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <spectra/series_stats.hpp>

#include "ui/workspace/plugin_guard.hpp"

namespace spectra
{

void Renderer::upload_series_data(Series& series)
{
    upload_series_data(series, 0.0, 0.0);
}

void Renderer::upload_series_data(Series& series, double origin_x, double origin_y)
{
    // Try 2D series first
    auto* line    = dynamic_cast<LineSeries*>(&series);
    auto* scatter = dynamic_cast<ScatterSeries*>(&series);

    // Try chunked series
    auto* chunked_line = dynamic_cast<ChunkedLineSeries*>(&series);

    // Try 3D series
    auto* line3d    = dynamic_cast<LineSeries3D*>(&series);
    auto* scatter3d = dynamic_cast<ScatterSeries3D*>(&series);
    auto* surface   = dynamic_cast<SurfaceSeries*>(&series);
    auto* mesh      = dynamic_cast<MeshSeries*>(&series);

    // Try statistical series
    auto* boxplot   = dynamic_cast<BoxPlotSeries*>(&series);
    auto* violin    = dynamic_cast<ViolinSeries*>(&series);
    auto* histogram = dynamic_cast<HistogramSeries*>(&series);
    auto* bar       = dynamic_cast<BarSeries*>(&series);
    auto* band      = dynamic_cast<BandSeries*>(&series);
    auto* stem      = dynamic_cast<StemSeries*>(&series);

    // Try shape series
    auto* shape   = dynamic_cast<ShapeSeries*>(&series);
    auto* shape3d = dynamic_cast<ShapeSeries3D*>(&series);

    // Try custom plugin series
    auto* custom = dynamic_cast<CustomSeries*>(&series);

    auto& gpu = series_gpu_data_[&series];

    // Tag series type on first encounter (avoids dynamic_cast in render_series)
    if (gpu.type == SeriesType::Unknown)
    {
        if (line)
            gpu.type = SeriesType::Line2D;
        else if (scatter)
            gpu.type = SeriesType::Scatter2D;
        else if (chunked_line)
            gpu.type = SeriesType::ChunkedLine2D;
        else if (line3d)
            gpu.type = SeriesType::Line3D;
        else if (scatter3d)
            gpu.type = SeriesType::Scatter3D;
        else if (surface)
            gpu.type = SeriesType::Surface3D;
        else if (mesh)
            gpu.type = SeriesType::Mesh3D;
        else if (boxplot)
            gpu.type = SeriesType::BoxPlot2D;
        else if (violin)
            gpu.type = SeriesType::Violin2D;
        else if (histogram)
            gpu.type = SeriesType::Histogram2D;
        else if (bar)
            gpu.type = SeriesType::Bar2D;
        else if (band)
            gpu.type = SeriesType::Band2D;
        else if (stem)
            gpu.type = SeriesType::Stem2D;
        else if (shape)
            gpu.type = SeriesType::Shape2D;
        else if (shape3d)
            gpu.type = SeriesType::Shape3D;
        else if (custom)
        {
            gpu.type             = SeriesType::Custom;
            gpu.custom_type_name = custom->type_name();
        }
    }

    // Handle 2D line/scatter and statistical/shape series (vec2 interleaved)
    if (line || scatter || boxplot || violin || histogram || bar || band || stem || shape)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        size_t       count  = 0;

        if (line)
        {
            x_data = line->x_data().data();
            y_data = line->y_data().data();
            count  = line->point_count();
        }
        else if (scatter)
        {
            x_data = scatter->x_data().data();
            y_data = scatter->y_data().data();
            count  = scatter->point_count();
        }
        else if (boxplot)
        {
            boxplot->rebuild_geometry();
            x_data = boxplot->x_data().data();
            y_data = boxplot->y_data().data();
            count  = boxplot->point_count();
        }
        else if (violin)
        {
            violin->rebuild_geometry();
            x_data = violin->x_data().data();
            y_data = violin->y_data().data();
            count  = violin->point_count();
        }
        else if (histogram)
        {
            histogram->rebuild_geometry();
            x_data = histogram->x_data().data();
            y_data = histogram->y_data().data();
            count  = histogram->point_count();
        }
        else if (bar)
        {
            bar->rebuild_geometry();
            x_data = bar->x_data().data();
            y_data = bar->y_data().data();
            count  = bar->point_count();
        }
        else if (band)
        {
            band->rebuild_geometry();
            x_data = band->x_data().data();
            y_data = band->y_data().data();
            count  = band->point_count();
        }
        else if (stem)
        {
            stem->rebuild_geometry();
            x_data = stem->x_data().data();
            y_data = stem->y_data().data();
            count  = stem->point_count();
        }
        else if (shape)
        {
            shape->rebuild_geometry();
            x_data = shape->x_data().data();
            y_data = shape->y_data().data();
            count  = shape->point_count();
        }

        if (count == 0)
            return;

        const bool use_scatter_colormap =
            scatter && scatter->has_colormap() && scatter_colormap_pipeline_;
        const size_t stride_floats = use_scatter_colormap ? 4 : 2;
        size_t       byte_size     = count * stride_floats * sizeof(float);
        if (!gpu.ssbo || gpu.ssbo_capacity < count || gpu.ssbo_stride_floats != stride_floats)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            // Over-allocate (2x) so streaming series with steadily growing point
            // counts don't reallocate the GPU buffer every frame. The guard
            // compares against ssbo_capacity (element capacity), not uploaded_count,
            // so the headroom is actually used.
            size_t alloc_count     = count * 2;
            gpu.ssbo               = backend_.create_buffer(BufferUsage::Storage,
                                              alloc_count * stride_floats * sizeof(float));
            gpu.ssbo_capacity      = alloc_count;
            gpu.ssbo_stride_floats = stride_floats;
        }

        size_t floats_needed = count * stride_floats;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        // Camera-relative upload: subtract origin in double precision
        // before converting to float.  Keeps GPU floats small, eliminating
        // catastrophic cancellation at deep zoom.
        // The series x_offset (logical x = x_offset + stored float) folds
        // into the effective origin: uploaded = (x + xoff) - origin_x.
        const double x_org = origin_x - series.x_offset();
        for (size_t i = 0; i < count; ++i)
        {
            const size_t base     = i * stride_floats;
            upload_scratch_[base] = static_cast<float>(static_cast<double>(x_data[i]) - x_org);
            upload_scratch_[base + 1] =
                static_cast<float>(static_cast<double>(y_data[i]) - origin_y);
            if (use_scatter_colormap)
            {
                upload_scratch_[base + 2] = scatter->color_values_data()[i];
                upload_scratch_[base + 3] = 0.0f;
            }
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;
        gpu.origin_x       = origin_x;
        gpu.origin_y       = origin_y;

        // Upload fill geometry for statistical series (interleaved {x,y,alpha} vertex buffer)
        std::span<const float> fill_verts;
        size_t                 fill_count = 0;
        if (boxplot && boxplot->fill_vertex_count() > 0)
        {
            fill_verts = boxplot->fill_verts();
            fill_count = boxplot->fill_vertex_count();
        }
        else if (violin && violin->fill_vertex_count() > 0)
        {
            fill_verts = violin->fill_verts();
            fill_count = violin->fill_vertex_count();
        }
        else if (histogram && histogram->fill_vertex_count() > 0)
        {
            fill_verts = histogram->fill_verts();
            fill_count = histogram->fill_vertex_count();
        }
        else if (bar && bar->fill_vertex_count() > 0)
        {
            fill_verts = bar->fill_verts();
            fill_count = bar->fill_vertex_count();
        }
        else if (band && band->fill_vertex_count() > 0)
        {
            fill_verts = band->fill_verts();
            fill_count = band->fill_vertex_count();
        }
        else if (shape && shape->fill_vertex_count() > 0)
        {
            fill_verts = shape->fill_verts();
            fill_count = shape->fill_vertex_count();
        }

        if (fill_count > 0)
        {
            // 3 floats per vertex: x, y, alpha — apply origin offset
            size_t fill_bytes  = fill_count * 3 * sizeof(float);
            size_t fill_floats = fill_count * 3;
            if (!gpu.fill_buffer || gpu.fill_vertex_capacity < fill_count)
            {
                if (gpu.fill_buffer)
                    backend_.destroy_buffer(gpu.fill_buffer);
                size_t fill_alloc = fill_count * 2;
                gpu.fill_buffer =
                    backend_.create_buffer(BufferUsage::Vertex, fill_alloc * 3 * sizeof(float));
                gpu.fill_vertex_capacity = fill_alloc;
            }

            if (x_org != 0.0 || origin_y != 0.0)
            {
                // Re-center fill vertices (stride=3: x, y, alpha)
                if (upload_scratch_.size() < fill_floats)
                    upload_scratch_.resize(fill_floats);
                const float* fv = fill_verts.data();
                for (size_t i = 0; i < fill_count; ++i)
                {
                    upload_scratch_[i * 3] =
                        static_cast<float>(static_cast<double>(fv[i * 3]) - x_org);
                    upload_scratch_[i * 3 + 1] =
                        static_cast<float>(static_cast<double>(fv[i * 3 + 1]) - origin_y);
                    upload_scratch_[i * 3 + 2] = fv[i * 3 + 2];   // alpha unchanged
                }
                backend_.upload_buffer(gpu.fill_buffer, upload_scratch_.data(), fill_bytes);
            }
            else
            {
                backend_.upload_buffer(gpu.fill_buffer, fill_verts.data(), fill_bytes);
            }
            gpu.fill_vertex_count = fill_count;
        }

        // Upload outlier data for box plots (persistent buffer, avoids in-flight destruction)
        if (boxplot && boxplot->outlier_count() > 0)
        {
            size_t out_count     = boxplot->outlier_count();
            size_t out_byte_size = out_count * 2 * sizeof(float);
            if (!gpu.outlier_buffer || gpu.outlier_capacity < out_count)
            {
                if (gpu.outlier_buffer)
                    backend_.destroy_buffer(gpu.outlier_buffer);
                size_t out_alloc = out_count * 2;
                gpu.outlier_buffer =
                    backend_.create_buffer(BufferUsage::Storage, out_alloc * 2 * sizeof(float));
                gpu.outlier_capacity = out_alloc;
            }
            size_t out_floats = out_count * 2;
            if (upload_scratch_.size() < out_floats)
                upload_scratch_.resize(out_floats);
            const float* ox = boxplot->outlier_x().data();
            const float* oy = boxplot->outlier_y().data();
            for (size_t i = 0; i < out_count; ++i)
            {
                upload_scratch_[i * 2] = static_cast<float>(static_cast<double>(ox[i]) - x_org);
                upload_scratch_[i * 2 + 1] =
                    static_cast<float>(static_cast<double>(oy[i]) - origin_y);
            }
            backend_.upload_buffer(gpu.outlier_buffer, upload_scratch_.data(), out_byte_size);
            gpu.outlier_count = out_count;
        }
        else if (boxplot)
        {
            gpu.outlier_count = 0;
        }

        series.clear_dirty();
    }
    // Handle chunked 2D line series (demand-loaded, LoD-aware)
    else if (chunked_line)
    {
        // Query visible data: use full range when no specific viewport is available.
        // The renderer's render_series() will clip to the actual viewport.
        // We use a generous max_points budget to maintain visual fidelity.
        auto vis = chunked_line->visible_data(-std::numeric_limits<float>::max(),
                                              std::numeric_limits<float>::max(),
                                              65536);

        size_t count = vis.x.size();
        if (count == 0)
        {
            series.clear_dirty();
            return;
        }

        // WS-5.2: Skip re-upload if data and origin haven't changed significantly.
        // Small pans are handled by the data_offset push constant in render_series()
        // without requiring GPU buffer re-upload.
        const auto current_generation = static_cast<uint64_t>(chunked_line->point_count());
        const bool count_changed      = (count != gpu.uploaded_count);
        const bool origin_moved = (std::abs(static_cast<double>(gpu.origin_x) - origin_x) > 1.0
                                   || std::abs(static_cast<double>(gpu.origin_y) - origin_y) > 1.0);
        const bool needs_upload = count_changed || origin_moved || !gpu.ssbo
                                  || (current_generation != gpu.data_generation);

        if (!needs_upload)
        {
            series.clear_dirty();
            return;
        }

        size_t byte_size = count * 2 * sizeof(float);
        if (!gpu.ssbo || gpu.ssbo_capacity < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_count = count * 2;
            gpu.ssbo =
                backend_.create_buffer(BufferUsage::Storage, alloc_count * 2 * sizeof(float));
            gpu.ssbo_capacity = alloc_count;
        }

        size_t floats_needed = count * 2;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);

        const double x_org = origin_x - series.x_offset();
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 2] = static_cast<float>(static_cast<double>(vis.x[i]) - x_org);
            upload_scratch_[i * 2 + 1] =
                static_cast<float>(static_cast<double>(vis.y[i]) - origin_y);
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count  = count;
        gpu.origin_x        = origin_x;
        gpu.origin_y        = origin_y;
        gpu.data_generation = current_generation;

        series.clear_dirty();
    }
    // Handle 3D line/scatter (vec4 interleaved: x,y,z,pad)
    else if (line3d || scatter3d)
    {
        const float* x_data = nullptr;
        const float* y_data = nullptr;
        const float* z_data = nullptr;
        size_t       count  = 0;

        if (line3d)
        {
            x_data = line3d->x_data().data();
            y_data = line3d->y_data().data();
            z_data = line3d->z_data().data();
            count  = line3d->point_count();
        }
        else if (scatter3d)
        {
            x_data = scatter3d->x_data().data();
            y_data = scatter3d->y_data().data();
            z_data = scatter3d->z_data().data();
            count  = scatter3d->point_count();
        }

        if (count == 0)
            return;

        size_t byte_size = count * 4 * sizeof(float);
        if (!gpu.ssbo || gpu.ssbo_capacity < count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            size_t alloc_count = count * 2;
            gpu.ssbo =
                backend_.create_buffer(BufferUsage::Storage, alloc_count * 4 * sizeof(float));
            gpu.ssbo_capacity = alloc_count;
        }

        size_t floats_needed = count * 4;
        if (upload_scratch_.size() < floats_needed)
            upload_scratch_.resize(floats_needed);
        for (size_t i = 0; i < count; ++i)
        {
            upload_scratch_[i * 4]     = x_data[i];
            upload_scratch_[i * 4 + 1] = y_data[i];
            upload_scratch_[i * 4 + 2] = z_data[i];
            upload_scratch_[i * 4 + 3] = 0.0f;   // padding
        }

        backend_.upload_buffer(gpu.ssbo, upload_scratch_.data(), byte_size);
        gpu.uploaded_count = count;
        series.clear_dirty();
    }
    // Handle surface (vertex buffer + index buffer)
    else if (surface)
    {
        // Choose between wireframe and solid mesh
        const SurfaceMesh* active_mesh = nullptr;
        if (surface->wireframe())
        {
            if (!surface->is_wireframe_mesh_generated())
            {
                surface->generate_wireframe_mesh();
            }
            if (!surface->is_wireframe_mesh_generated())
                return;
            active_mesh = &surface->wireframe_mesh();
        }
        else
        {
            if (!surface->is_mesh_generated())
            {
                surface->generate_mesh();
            }
            if (!surface->is_mesh_generated())
                return;
            active_mesh = &surface->mesh();
        }

        if (active_mesh->vertices.empty() || active_mesh->indices.empty())
            return;

        size_t vert_byte_size = active_mesh->vertices.size() * sizeof(float);
        size_t idx_byte_size  = active_mesh->indices.size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < active_mesh->vertex_count)
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, active_mesh->vertices.data(), vert_byte_size);
        gpu.uploaded_count = active_mesh->vertex_count;

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < active_mesh->indices.size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, active_mesh->indices.data(), idx_byte_size);
        gpu.index_count = active_mesh->indices.size();

        series.clear_dirty();
    }
    // Handle mesh (vertex buffer + index buffer)
    else if (mesh)
    {
        if (mesh->vertices().empty() || mesh->indices().empty())
            return;

        size_t vert_byte_size = mesh->vertices().size() * sizeof(float);
        size_t idx_byte_size  = mesh->indices().size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < mesh->vertex_count())
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, mesh->vertices().data(), vert_byte_size);
        gpu.uploaded_count = mesh->vertex_count();

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < mesh->indices().size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, mesh->indices().data(), idx_byte_size);
        gpu.index_count = mesh->indices().size();

        series.clear_dirty();
    }
    // Handle 3D shapes (vertex buffer + index buffer, same as MeshSeries)
    else if (shape3d)
    {
        shape3d->rebuild_geometry();

        if (shape3d->vertices().empty() || shape3d->indices().empty())
            return;

        size_t vert_byte_size = shape3d->vertices().size() * sizeof(float);
        size_t idx_byte_size  = shape3d->indices().size() * sizeof(uint32_t);

        // Vertex buffer
        if (!gpu.ssbo || gpu.uploaded_count < shape3d->vertex_count())
        {
            if (gpu.ssbo)
                backend_.destroy_buffer(gpu.ssbo);
            gpu.ssbo = backend_.create_buffer(BufferUsage::Vertex, vert_byte_size);
        }
        backend_.upload_buffer(gpu.ssbo, shape3d->vertices().data(), vert_byte_size);
        gpu.uploaded_count = shape3d->vertex_count();

        // Index buffer
        if (!gpu.index_buffer || gpu.index_count < shape3d->indices().size())
        {
            if (gpu.index_buffer)
                backend_.destroy_buffer(gpu.index_buffer);
            gpu.index_buffer = backend_.create_buffer(BufferUsage::Index, idx_byte_size);
        }
        backend_.upload_buffer(gpu.index_buffer, shape3d->indices().data(), idx_byte_size);
        gpu.index_count = shape3d->indices().size();

        series.clear_dirty();
    }

    // Custom plugin series upload
    if (custom && series_type_registry_)
    {
        auto* entry = series_type_registry_->find_mut(custom->type_name());
        if (entry && entry->upload_fn && !entry->faulted)
        {
            auto result = plugin_guard_invoke(entry->type_name.c_str(),
                                              [&]() {
                                                  entry->upload_fn(backend_,
                                                                   custom->data(),
                                                                   gpu.plugin_gpu_state,
                                                                   custom->element_count());
                                              });
            if (result != PluginCallResult::Success)
            {
                entry->faulted = true;
            }
        }
        series.clear_dirty();
    }
}

}   // namespace spectra
