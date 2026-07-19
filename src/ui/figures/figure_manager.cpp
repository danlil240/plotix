#include "figure_manager.hpp"

#include <cassert>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/logger.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>

#include "tab_bar.hpp"

namespace spectra
{

FigureManager::FigureManager(FigureRegistry& registry) : registry_(registry)
{
    // Import any existing figures from the registry.
    // Use FigureId-based titles (stable across windows) rather than
    // position-based titles (which change when figures move between windows).
    for (auto id : registry_.all_ids())
    {
        ordered_ids_.push_back(id);
        FigureState st(id, registry_.get(id));
        // Leave custom_title empty so get_title() falls back to either the
        // figure's own tab_title (set via easy API) or default_title(id).
        states_[id] = std::move(st);
    }
    if (!ordered_ids_.empty())
    {
        active_index_ = ordered_ids_[0];
    }
}

size_t FigureManager::id_to_pos(FigureId id) const
{
    for (size_t i = 0; i < ordered_ids_.size(); ++i)
    {
        if (ordered_ids_[i] == id)
            return i;
    }
    return SIZE_MAX;
}

FigureId FigureManager::pos_to_id(size_t pos) const
{
    if (pos < ordered_ids_.size())
        return ordered_ids_[pos];
    return INVALID_FIGURE_ID;
}

void FigureManager::set_tab_bar(TabBar* tab_bar)
{
    tab_bar_ = tab_bar;
    if (tab_bar_)
    {
        sync_tab_bar();
    }
}

FigureId FigureManager::create_figure(const FigureConfig& config)
{
    auto new_figure = std::make_unique<Figure>(config);
    // UI-created figures should open directly on a plot canvas, not the
    // startup welcome state shown for a truly blank figure.
    new_figure->subplot(1, 1, 1);

    auto id = registry_.register_figure(std::move(new_figure));
    ordered_ids_.push_back(id);

    SPECTRA_LOG_TRACE("figure", "Created figure id={}", id);

    // Add state for the new figure (use next available figure number)
    FigureState new_state(id, registry_.get(id));
    new_state.set_custom_title(default_title(next_figure_number()));
    states_[id] = std::move(new_state);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->add_tab(get_title(id));
    }

    // Switch to the new figure
    switch_to(id);

    return id;
}

bool FigureManager::close_figure(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX)
    {
        return false;
    }

    SPECTRA_LOG_TRACE("figure", "Closing figure id={}", index);

    // Last figure: request window close instead of closing the figure
    if (ordered_ids_.size() <= 1)
    {
        if (on_window_close_request_)
            on_window_close_request_();
        return false;
    }

    // Notify before removal
    if (on_figure_closed_)
    {
        on_figure_closed_(index);
    }

    // Remove from tab bar first (before modifying vectors)
    if (tab_bar_)
    {
        tab_bar_->remove_tab(pos);
    }

    // Remove from ordered list and registry
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos));
    states_.erase(index);
    registry_.unregister_figure(index);

    // Adjust active index
    if (active_index_ == index)
    {
        // Switch to nearest remaining figure
        size_t new_pos = (pos < ordered_ids_.size()) ? pos : ordered_ids_.size() - 1;
        active_index_  = ordered_ids_[new_pos];
    }

    // Sync tab bar active state
    if (tab_bar_)
    {
        size_t active_pos = id_to_pos(active_index_);
        if (active_pos != SIZE_MAX)
            tab_bar_->set_active_tab(active_pos);
    }

    // Notify figure changed
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_all_except(FigureId index)
{
    if (id_to_pos(index) == SIZE_MAX)
    {
        return false;
    }

    // Save the figure we want to keep
    save_active_state();

    // Collect IDs to remove
    std::vector<FigureId> to_remove;
    for (auto id : ordered_ids_)
    {
        if (id != index)
            to_remove.push_back(id);
    }

    for (auto id : to_remove)
    {
        if (on_figure_closed_)
        {
            on_figure_closed_(id);
        }
        states_.erase(id);
        registry_.unregister_figure(id);
    }

    ordered_ids_  = {index};
    active_index_ = index;

    // Rebuild tab bar
    sync_tab_bar();

    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

bool FigureManager::close_to_right(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX)
    {
        return false;
    }

    // Nothing to close if this is the last tab positionally
    if (pos + 1 >= ordered_ids_.size())
    {
        return false;
    }

    save_active_state();

    // Collect IDs to the right
    std::vector<FigureId> to_remove(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos + 1),
                                    ordered_ids_.end());

    for (auto id : to_remove)
    {
        if (on_figure_closed_)
        {
            on_figure_closed_(id);
        }
        states_.erase(id);
        registry_.unregister_figure(id);
    }

    ordered_ids_.resize(pos + 1);

    // Adjust active index if it was beyond the closed range
    if (id_to_pos(active_index_) == SIZE_MAX)
    {
        active_index_ = index;
    }

    sync_tab_bar();

    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }

    return true;
}

FigureState FigureManager::remove_figure(FigureId id)
{
    size_t pos = id_to_pos(id);
    if (pos == SIZE_MAX)
        return {};

    SPECTRA_LOG_TRACE("figure", "Removing figure id={}", id);

    // Save current state before removal
    if (id == active_index_)
        save_active_state();

    // Extract state
    FigureState extracted;
    auto        st_it = states_.find(id);
    if (st_it != states_.end())
    {
        extracted = std::move(st_it->second);
        states_.erase(st_it);
    }

    // Remove from tab bar before modifying ordered_ids_
    if (tab_bar_)
        tab_bar_->remove_tab(pos);

    // Remove from ordered list (do NOT unregister from registry)
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(pos));

    // Adjust active index
    if (active_index_ == id)
    {
        if (!ordered_ids_.empty())
        {
            size_t new_pos = (pos < ordered_ids_.size()) ? pos : ordered_ids_.size() - 1;
            active_index_  = ordered_ids_[new_pos];
        }
        else
        {
            active_index_ = INVALID_FIGURE_ID;
        }
    }

    // Sync tab bar active state
    if (tab_bar_ && !ordered_ids_.empty())
    {
        size_t active_pos = id_to_pos(active_index_);
        if (active_pos != SIZE_MAX)
            tab_bar_->set_active_tab(active_pos);
    }

    // Notify figure changed
    if (on_figure_changed_)
        on_figure_changed_(active_index_, active_figure());

    return extracted;
}

void FigureManager::add_figure(FigureId id, FigureState fig_state)
{
    // Don't add duplicates
    if (id_to_pos(id) != SIZE_MAX)
        return;

    // Verify figure exists in registry
    if (!registry_.get(id))
        return;

    SPECTRA_LOG_TRACE("figure", "Adding figure id={}", id);
    fig_state.set_figure_id(id);
    fig_state.set_model(registry_.get(id));
    ordered_ids_.push_back(id);
    states_[id] = std::move(fig_state);

    // Sync tab bar
    if (tab_bar_)
        tab_bar_->add_tab(get_title(id));

    // Switch to the new figure
    switch_to(id);
}

FigureId FigureManager::duplicate_figure(FigureId index)
{
    Figure* src = registry_.get(index);
    if (!src)
    {
        return INVALID_FIGURE_ID;
    }

    // Create a new figure with the same dimensions
    FigureConfig cfg;
    cfg.width         = src->width();
    cfg.height        = src->height();
    auto  new_fig_ptr = std::make_unique<Figure>(cfg);
    auto& new_fig     = *new_fig_ptr;

    // ── Deep-copy 2D axes with all series data ──────────────────────
    for (size_t i = 0; i < src->axes().size(); ++i)
    {
        if (!src->axes()[i])
            continue;

        auto& src_ax = *src->axes()[i];
        auto& dst_ax = new_fig.subplot(src->grid_rows(), src->grid_cols(), static_cast<int>(i + 1));

        // Copy axis properties
        dst_ax.xlim(src_ax.x_limits().min, src_ax.x_limits().max);
        dst_ax.ylim(src_ax.y_limits().min, src_ax.y_limits().max);
        if (!src_ax.get_title().empty())
            dst_ax.title(src_ax.get_title());
        if (!src_ax.get_xlabel().empty())
            dst_ax.xlabel(src_ax.get_xlabel());
        if (!src_ax.get_ylabel().empty())
            dst_ax.ylabel(src_ax.get_ylabel());
        dst_ax.grid(src_ax.grid_enabled());
        dst_ax.show_border(src_ax.border_enabled());
        dst_ax.autoscale_mode(src_ax.get_autoscale_mode());
        dst_ax.axis_style() = src_ax.axis_style();

        // Deep-copy 2D series
        for (const auto& s : src_ax.series())
        {
            if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            {
                auto& dup = dst_ax.line(ls->x_data(), ls->y_data());
                dup.color(ls->color());
                dup.width(ls->width());
                if (!ls->label().empty())
                    dup.label(ls->label());
                dup.visible(ls->visible());
                dup.plot_style(ls->plot_style());
            }
            else if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            {
                auto& dup = dst_ax.scatter(ss->x_data(), ss->y_data());
                dup.color(ss->color());
                dup.size(ss->size());
                if (!ss->label().empty())
                    dup.label(ss->label());
                dup.visible(ss->visible());
                dup.plot_style(ss->plot_style());
                if (!ss->color_values_data().empty())
                    dup.color_values(ss->color_values_data()).colormap(ss->colormap());
                if (ss->colormap_range_set())
                    dup.colormap_range(ss->colormap_min(), ss->colormap_max());
            }
            else if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
            {
                auto& dup =
                    dst_ax.band(band->x_values(), band->lower_values(), band->upper_values());
                dup.color(band->color());
                dup.fill_opacity(band->fill_opacity());
                dup.edge_width(band->edge_width());
                dup.show_edges(band->show_edges());
                if (!band->label().empty())
                    dup.label(band->label());
                dup.visible(band->visible());
                dup.opacity(band->opacity());
                dup.plot_style(band->plot_style());
            }
            else if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
            {
                auto& dup = dst_ax.box_plot();
                for (size_t bi = 0; bi < bp->positions().size(); ++bi)
                {
                    const auto& st = bp->stats()[bi];
                    dup.add_box(bp->positions()[bi],
                                st.median,
                                st.q1,
                                st.q3,
                                st.whisker_low,
                                st.whisker_high,
                                st.outliers);
                }
                dup.color(bp->color());
                dup.box_width(bp->box_width());
                dup.show_outliers(bp->show_outliers());
                dup.notched(bp->notched());
                dup.gradient(bp->gradient());
                if (!bp->label().empty())
                    dup.label(bp->label());
                dup.visible(bp->visible());
                dup.opacity(bp->opacity());
            }
            else if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            {
                auto& dup = dst_ax.violin();
                for (const auto& vd : vn->violins())
                    dup.add_violin(vd.x_position, vd.values);
                dup.color(vn->color());
                dup.violin_width(vn->violin_width());
                dup.resolution(vn->resolution());
                dup.show_box(vn->show_box());
                dup.gradient(vn->gradient());
                if (!vn->label().empty())
                    dup.label(vn->label());
                dup.visible(vn->visible());
                dup.opacity(vn->opacity());
            }
            else if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            {
                auto& dup = dst_ax.histogram(hs->raw_values(), hs->bins());
                dup.color(hs->color());
                dup.cumulative(hs->cumulative());
                dup.density(hs->density());
                dup.gradient(hs->gradient());
                if (!hs->label().empty())
                    dup.label(hs->label());
                dup.visible(hs->visible());
                dup.opacity(hs->opacity());
            }
            else if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            {
                auto& dup = dst_ax.bar(bs->bar_positions(), bs->bar_heights());
                dup.color(bs->color());
                dup.bar_width(bs->bar_width());
                dup.baseline(bs->baseline());
                dup.orientation(bs->orientation());
                dup.gradient(bs->gradient());
                if (!bs->label().empty())
                    dup.label(bs->label());
                dup.visible(bs->visible());
                dup.opacity(bs->opacity());
            }
        }
    }

    // ── Deep-copy 3D axes (all_axes_) with all series data ──────────
    for (size_t i = 0; i < src->all_axes().size(); ++i)
    {
        if (!src->all_axes()[i])
            continue;

        auto* src_ax3d = dynamic_cast<const Axes3D*>(src->all_axes()[i].get());
        if (!src_ax3d)
            continue;

        auto& dst_ax3d =
            new_fig.subplot3d(src->grid_rows(), src->grid_cols(), static_cast<int>(i + 1));

        // Copy 3D axis properties
        dst_ax3d.xlim(src_ax3d->x_limits().min, src_ax3d->x_limits().max);
        dst_ax3d.ylim(src_ax3d->y_limits().min, src_ax3d->y_limits().max);
        dst_ax3d.zlim(src_ax3d->z_limits().min, src_ax3d->z_limits().max);
        if (!src_ax3d->get_xlabel().empty())
            dst_ax3d.xlabel(src_ax3d->get_xlabel());
        if (!src_ax3d->get_ylabel().empty())
            dst_ax3d.ylabel(src_ax3d->get_ylabel());
        if (!src_ax3d->get_zlabel().empty())
            dst_ax3d.zlabel(src_ax3d->get_zlabel());
        if (!src_ax3d->get_title().empty())
            dst_ax3d.title(src_ax3d->get_title());
        dst_ax3d.grid(src_ax3d->grid_enabled());
        dst_ax3d.grid_planes(src_ax3d->grid_planes());
        dst_ax3d.show_bounding_box(src_ax3d->show_bounding_box());
        dst_ax3d.light_dir(src_ax3d->light_dir());
        dst_ax3d.lighting_enabled(src_ax3d->lighting_enabled());

        // Copy camera state
        dst_ax3d.camera().target          = src_ax3d->camera().target;
        dst_ax3d.camera().up              = src_ax3d->camera().up;
        dst_ax3d.camera().azimuth         = src_ax3d->camera().azimuth;
        dst_ax3d.camera().elevation       = src_ax3d->camera().elevation;
        dst_ax3d.camera().distance        = src_ax3d->camera().distance;
        dst_ax3d.camera().fov             = src_ax3d->camera().fov;
        dst_ax3d.camera().near_clip       = src_ax3d->camera().near_clip;
        dst_ax3d.camera().far_clip        = src_ax3d->camera().far_clip;
        dst_ax3d.camera().projection_mode = src_ax3d->camera().projection_mode;
        dst_ax3d.camera().ortho_size      = src_ax3d->camera().ortho_size;
        dst_ax3d.camera().update_position_from_orbit();

        // Deep-copy 3D series
        for (const auto& s : src_ax3d->series())
        {
            if (auto* ls3 = dynamic_cast<const LineSeries3D*>(s.get()))
            {
                auto& dup = dst_ax3d.line3d(ls3->x_data(), ls3->y_data(), ls3->z_data());
                dup.color(ls3->color());
                dup.width(ls3->width());
                if (!ls3->label().empty())
                    dup.label(ls3->label());
                dup.visible(ls3->visible());
                dup.plot_style(ls3->plot_style());
                dup.blend_mode(ls3->blend_mode());
                dup.opacity(ls3->opacity());
            }
            else if (auto* ss3 = dynamic_cast<const ScatterSeries3D*>(s.get()))
            {
                auto& dup = dst_ax3d.scatter3d(ss3->x_data(), ss3->y_data(), ss3->z_data());
                dup.color(ss3->color());
                dup.size(ss3->size());
                if (!ss3->label().empty())
                    dup.label(ss3->label());
                dup.visible(ss3->visible());
                dup.plot_style(ss3->plot_style());
                dup.blend_mode(ss3->blend_mode());
                dup.opacity(ss3->opacity());
            }
            else if (auto* sf = dynamic_cast<const SurfaceSeries*>(s.get()))
            {
                auto& dup = dst_ax3d.surface(sf->x_grid(), sf->y_grid(), sf->z_values());
                dup.color(sf->color());
                if (!sf->label().empty())
                    dup.label(sf->label());
                dup.visible(sf->visible());
                dup.colormap(sf->colormap_type());
                dup.colormap_range(sf->colormap_min(), sf->colormap_max());
                dup.ambient(sf->ambient());
                dup.specular(sf->specular());
                dup.shininess(sf->shininess());
                dup.blend_mode(sf->blend_mode());
                dup.double_sided(sf->double_sided());
                dup.wireframe(sf->wireframe());
                dup.colormap_alpha(sf->colormap_alpha());
                dup.colormap_alpha_range(sf->colormap_alpha_min(), sf->colormap_alpha_max());
                dup.opacity(sf->opacity());
            }
            else if (auto* ms = dynamic_cast<const MeshSeries*>(s.get()))
            {
                auto& dup = dst_ax3d.mesh(ms->vertices(), ms->indices());
                dup.color(ms->color());
                if (!ms->label().empty())
                    dup.label(ms->label());
                dup.visible(ms->visible());
                dup.ambient(ms->ambient());
                dup.specular(ms->specular());
                dup.shininess(ms->shininess());
                dup.blend_mode(ms->blend_mode());
                dup.double_sided(ms->double_sided());
                dup.wireframe(ms->wireframe());
                dup.opacity(ms->opacity());
            }
        }
    }

    // Copy figure style and legend
    new_fig.style()  = src->style();
    new_fig.legend() = src->legend();

    // NOTE: Animation callbacks are NOT copied to duplicates.
    // The on_frame callback captures references to the original figure's
    // series objects (e.g. scatter.set_data()), so copying it would cause
    // the duplicate's animation to mutate the original figure's data.
    // Duplicated figures are static snapshots of the current frame.

    auto new_id = registry_.register_figure(std::move(new_fig_ptr));
    ordered_ids_.push_back(new_id);

    // Create state with next available figure number
    FigureState new_state(new_id, registry_.get(new_id));
    new_state.set_custom_title(default_title(next_figure_number()));
    states_[new_id] = std::move(new_state);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->add_tab(get_title(new_id));
    }

    switch_to(new_id);
    return new_id;
}

void FigureManager::switch_to(FigureId index)
{
    size_t pos = id_to_pos(index);
    if (pos == SIZE_MAX || index == active_index_)
    {
        return;
    }

    SPECTRA_LOG_TRACE("figure", "Switching to figure id={} (from id={})", index, active_index_);

    // Save current figure state before switching
    save_active_state();

    active_index_ = index;

    // Restore state for the new active figure
    restore_state(index);

    // Sync tab bar
    if (tab_bar_)
    {
        tab_bar_->set_active_tab(pos);
    }

    // Notify
    if (on_figure_changed_)
    {
        on_figure_changed_(active_index_, active_figure());
    }
}

void FigureManager::switch_to_next()
{
    if (ordered_ids_.size() <= 1)
        return;
    size_t pos = id_to_pos(active_index_);
    if (pos == SIZE_MAX)
        return;
    size_t next_pos = (pos + 1) % ordered_ids_.size();
    switch_to(ordered_ids_[next_pos]);
}

void FigureManager::switch_to_previous()
{
    if (ordered_ids_.size() <= 1)
        return;
    size_t pos = id_to_pos(active_index_);
    if (pos == SIZE_MAX)
        return;
    size_t prev_pos = (pos == 0) ? ordered_ids_.size() - 1 : pos - 1;
    switch_to(ordered_ids_[prev_pos]);
}

void FigureManager::move_tab(FigureId from_index, FigureId to_index)
{
    size_t from_pos = id_to_pos(from_index);
    size_t to_pos   = id_to_pos(to_index);
    if (from_pos == SIZE_MAX || to_pos == SIZE_MAX || from_pos == to_pos)
    {
        return;
    }

    // Reorder in ordered_ids_
    auto id = ordered_ids_[from_pos];
    ordered_ids_.erase(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(from_pos));
    ordered_ids_.insert(ordered_ids_.begin() + static_cast<std::ptrdiff_t>(to_pos), id);

    sync_tab_bar();
}

Figure* FigureManager::active_figure() const
{
    return registry_.get(active_index_);
}

Figure* FigureManager::get_figure(FigureId id) const
{
    return registry_.get(id);
}

bool FigureManager::can_close(FigureId index) const
{
    (void)index;
    return ordered_ids_.size() > 1;
}

FigureState& FigureManager::state(FigureId index)
{
    ensure_states();
    auto it = states_.find(index);
    if (it == states_.end())
    {
        static FigureState dummy;
        return dummy;
    }
    // Lazily keep model pointer up-to-date (figure may have been
    // re-registered or the pointer invalidated by registry changes).
    if (!it->second.model())
        it->second.set_model(registry_.get(index));
    return it->second;
}

const FigureState& FigureManager::state(FigureId index) const
{
    auto it = states_.find(index);
    if (it == states_.end())
    {
        static const FigureState dummy;
        return dummy;
    }
    return it->second;
}

FigureState& FigureManager::active_state()
{
    return state(active_index_);
}

std::string FigureManager::get_title(FigureId index) const
{
    auto it = states_.find(index);
    if (it != states_.end() && !it->second.custom_title().empty())
    {
        return it->second.custom_title();
    }
    // Fall back to a tab_title set on the Figure itself (easy API).
    Figure* fig = registry_.get(index);
    if (fig && !fig->tab_title().empty())
        return fig->tab_title();
    // Final fallback: FigureId-based title (stable across windows).
    return default_title(index);
}

void FigureManager::set_title(FigureId index, const std::string& title)
{
    ensure_states();
    auto it = states_.find(index);
    if (it != states_.end())
    {
        it->second.set_custom_title(title);
        if (tab_bar_)
        {
            size_t pos = id_to_pos(index);
            if (pos != SIZE_MAX)
                tab_bar_->set_tab_title(pos, title);
        }
    }
}

void FigureManager::mark_modified(FigureId index, bool modified)
{
    ensure_states();
    auto it = states_.find(index);
    if (it != states_.end())
    {
        it->second.set_is_modified(modified);
    }
}

bool FigureManager::is_modified(FigureId index) const
{
    auto it = states_.find(index);
    if (it == states_.end())
        return false;
    return it->second.is_modified();
}

bool FigureManager::process_pending()
{
    bool changed = false;

    if (pending_create_)
    {
        Figure*      current = active_figure();
        FigureConfig cfg;
        if (current)
        {
            cfg.width  = current->width();
            cfg.height = current->height();
        }
        create_figure(cfg);
        pending_create_ = false;
        changed         = true;
    }

    if (pending_close_ != INVALID_FIGURE_ID)
    {
        FigureId idx   = pending_close_;
        pending_close_ = INVALID_FIGURE_ID;
        close_figure(idx);
        changed = true;
    }

    if (pending_switch_ != INVALID_FIGURE_ID)
    {
        FigureId idx    = pending_switch_;
        pending_switch_ = INVALID_FIGURE_ID;
        if (id_to_pos(idx) != SIZE_MAX && idx != active_index_)
        {
            switch_to(idx);
            changed = true;
        }
    }

    return changed;
}

void FigureManager::queue_create()
{
    pending_create_ = true;
}

void FigureManager::queue_close(FigureId index)
{
    pending_close_ = index;
}

void FigureManager::queue_switch(FigureId index)
{
    pending_switch_ = index;
}

void FigureManager::save_active_state()
{
    ensure_states();
    Figure* fig = registry_.get(active_index_);
    if (!fig)
    {
        return;
    }

    auto it = states_.find(active_index_);
    if (it == states_.end())
        return;

    // Ensure the ViewModel has the model pointer, then delegate.
    it->second.set_model(fig);
    it->second.save_axes_state();
}

void FigureManager::restore_state(FigureId index)
{
    Figure* fig = registry_.get(index);
    if (!fig)
        return;

    auto it = states_.find(index);
    if (it == states_.end())
        return;

    // Ensure the ViewModel has the model pointer, then delegate.
    it->second.set_model(fig);
    it->second.restore_axes_state();
}

std::string FigureManager::default_title(FigureId index)
{
    return "Figure " + std::to_string(index);
}

void FigureManager::sync_tab_bar()
{
    if (!tab_bar_)
        return;

    // Rebuild tab bar from scratch (clear without firing callbacks)
    tab_bar_->clear_tabs();

    for (unsigned long ordered_id : ordered_ids_)
    {
        tab_bar_->add_tab(get_title(ordered_id));
    }

    // Set active
    size_t active_pos = id_to_pos(active_index_);
    if (active_pos != SIZE_MAX)
    {
        tab_bar_->set_active_tab(active_pos);
    }
}

void FigureManager::ensure_states()
{
    for (auto id : ordered_ids_)
    {
        if (states_.find(id) == states_.end())
        {
            FigureState st(id, registry_.get(id));
            // Leave custom_title empty; get_title() resolves from Figure::tab_title
            // or default_title(id).
            states_[id] = std::move(st);
        }
    }
}

size_t FigureManager::next_figure_number() const
{
    // Find the highest figure number used
    size_t max_num = ordered_ids_.size();
    for (const auto& [id, st] : states_)
    {
        if (!st.custom_title().empty())
        {
            // Try to parse "Figure N" pattern
            const std::string prefix = "Figure ";
            if (st.custom_title().substr(0, prefix.size()) == prefix)
            {
                try
                {
                    size_t num = std::stoul(st.custom_title().substr(prefix.size()));
                    if (num >= max_num)
                        max_num = num + 1;
                }
                catch (...)
                {
                    // Not a number, ignore
                }
            }
        }
    }
    return max_num;
}

}   // namespace spectra
