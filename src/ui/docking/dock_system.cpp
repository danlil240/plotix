#include "dock_system.hpp"

#include <algorithm>
#include <cmath>
#include <spectra/logger.hpp>

namespace spectra
{

// ─── DockSystem ──────────────────────────────────────────────────────────────

DockSystem::DockSystem() = default;

SplitPane* DockSystem::split_right(FigureId new_figure_index, float ratio)
{
    SPECTRA_LOG_TRACE("dock", "Split right: figure={}", new_figure_index);
    auto* result = split_view_.split_active(SplitDirection::Horizontal, new_figure_index, ratio);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

SplitPane* DockSystem::split_down(FigureId new_figure_index, float ratio)
{
    SPECTRA_LOG_TRACE("dock", "Split down: figure={}", new_figure_index);
    auto* result = split_view_.split_active(SplitDirection::Vertical, new_figure_index, ratio);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

SplitPane* DockSystem::split_figure_right(FigureId figure_index,
                                          FigureId new_figure_index,
                                          float    ratio)
{
    auto* result =
        split_view_.split_pane(figure_index, SplitDirection::Horizontal, new_figure_index, ratio);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

SplitPane* DockSystem::split_figure_down(FigureId figure_index,
                                         FigureId new_figure_index,
                                         float    ratio)
{
    auto* result =
        split_view_.split_pane(figure_index, SplitDirection::Vertical, new_figure_index, ratio);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

bool DockSystem::close_split(FigureId figure_index)
{
    SPECTRA_LOG_TRACE("dock", "Close split: figure={}", figure_index);
    bool result = split_view_.close_pane(figure_index);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

void DockSystem::reset_splits()
{
    split_view_.unsplit_all();
    if (on_layout_changed_)
        on_layout_changed_();
}

// ─── Drag-to-dock ────────────────────────────────────────────────────────────

void DockSystem::begin_drag(FigureId figure_index, float mouse_x, float mouse_y)
{
    SPECTRA_LOG_TRACE("dock", "Dock drag begin: figure={}", figure_index);
    is_dragging_           = true;
    dragging_figure_index_ = figure_index;
    drag_mouse_x_          = mouse_x;
    drag_mouse_y_          = mouse_y;
    current_drop_target_   = DropTarget{};
}

DropTarget DockSystem::update_drag(float mouse_x, float mouse_y)
{
    if (!is_dragging_)
        return DropTarget{};

    drag_mouse_x_        = mouse_x;
    drag_mouse_y_        = mouse_y;
    current_drop_target_ = compute_drop_target(mouse_x, mouse_y);
    return current_drop_target_;
}

bool DockSystem::end_drag(float mouse_x, float mouse_y)
{
    if (!is_dragging_)
        return false;

    auto target          = compute_drop_target(mouse_x, mouse_y);
    is_dragging_         = false;
    current_drop_target_ = DropTarget{};

    if (target.zone == DropZone::None || !target.target_pane)
    {
        SPECTRA_LOG_TRACE("dock", "Dock drag ended: no valid drop target");
        return false;
    }

    // In non-split mode, only allow edge zones (not Center)
    bool was_split = split_view_.is_split();
    if (!was_split && target.zone == DropZone::Center)
    {
        return false;
    }

    SPECTRA_LOG_TRACE("dock",
                      "Dock drag dropped: figure={} zone={}",
                      dragging_figure_index_,
                      static_cast<int>(target.zone));
    FigureId target_figure = target.target_pane->figure_index();

    // Don't dock onto self if the target pane only has one figure
    // (splitting a single-figure pane onto itself makes no sense).
    // But if the pane has multiple figures, splitting one out is valid.
    if (target_figure == dragging_figure_index_ && target.target_pane->figure_count() <= 1)
    {
        return false;
    }

    SplitPane* result     = nullptr;
    bool       needs_swap = false;   // True for Left/Top where dragged figure should be first

    switch (target.zone)
    {
        case DropZone::Left:
            result     = split_view_.split_pane(target_figure,
                                            SplitDirection::Horizontal,
                                            dragging_figure_index_,
                                            0.5f);
            needs_swap = true;
            break;
        case DropZone::Right:
            result = split_view_.split_pane(target_figure,
                                            SplitDirection::Horizontal,
                                            dragging_figure_index_,
                                            0.5f);
            break;
        case DropZone::Top:
            result     = split_view_.split_pane(target_figure,
                                            SplitDirection::Vertical,
                                            dragging_figure_index_,
                                            0.5f);
            needs_swap = true;
            break;
        case DropZone::Bottom:
            result = split_view_.split_pane(target_figure,
                                            SplitDirection::Vertical,
                                            dragging_figure_index_,
                                            0.5f);
            break;
        case DropZone::Center:
            // Add figure to this pane as a new tab
            if (target.target_pane)
            {
                target.target_pane->add_figure(dragging_figure_index_);
                result = target.target_pane;
            }
            break;
        case DropZone::None:
            break;
    }

    if (result && target.zone != DropZone::Center)
    {
        // split() copies ALL figure_indices_ to the first child, so the
        // dragged figure may be duplicated. Clean up: remove it from the
        // pane that should NOT have it.
        auto* parent = result->parent();
        if (parent)
        {
            auto* first  = parent->first();
            auto* second = parent->second();

            if (needs_swap && first && second)
            {
                // Dragged figure should be in first (left/top), originals in second
                first->swap_contents(*second);
            }

            // The pane that inherited all figures via split() may still
            // contain the dragged figure — remove it from there
            auto* originals_pane = needs_swap ? second : first;
            if (originals_pane && originals_pane->has_figure(dragging_figure_index_))
            {
                originals_pane->remove_figure(dragging_figure_index_);
            }
        }

        // The dragged figure's destination pane (after swap):
        auto* dest_pane = needs_swap ? (parent ? parent->first() : result) : result;

        // Remove the dragged figure from ALL other leaf panes (the source
        // pane still has it when dragging across panes)
        auto all_leaves = split_view_.all_panes();
        for (auto* leaf : all_leaves)
        {
            if (leaf != dest_pane && leaf->has_figure(dragging_figure_index_))
            {
                leaf->remove_figure(dragging_figure_index_);
            }
        }

        // Collapse any empty panes that resulted from the removal
        all_leaves = split_view_.all_panes();
        for (auto* leaf : all_leaves)
        {
            if (leaf->figure_count() == 0 && leaf->figure_index() == INVALID_FIGURE_ID)
            {
                auto* p = leaf->parent();
                if (p)
                {
                    bool keep_first = (p->second() == leaf);
                    p->unsplit(keep_first);
                    break;   // Tree changed, stop iterating
                }
            }
        }
    }
    else if (result && target.zone == DropZone::Center)
    {
        // Center drop: figure was added to the target pane as a tab.
        // Remove it from all OTHER leaf panes (the source pane) and
        // collapse any panes that become empty.
        auto all_leaves = split_view_.all_panes();
        for (auto* leaf : all_leaves)
        {
            if (leaf != result && leaf->has_figure(dragging_figure_index_))
            {
                leaf->remove_figure(dragging_figure_index_);
            }
        }

        // Collapse any empty panes that resulted from the removal
        all_leaves = split_view_.all_panes();
        for (auto* leaf : all_leaves)
        {
            if (leaf->figure_count() == 0 && leaf->figure_index() == INVALID_FIGURE_ID)
            {
                auto* p = leaf->parent();
                if (p)
                {
                    bool keep_first = (p->second() == leaf);
                    p->unsplit(keep_first);
                    break;   // Tree changed, stop iterating
                }
            }
        }
    }

    if (result)
    {
        split_view_.set_active_figure_index(dragging_figure_index_);
        if (on_layout_changed_)
            on_layout_changed_();
        return true;
    }

    return false;
}

void DockSystem::cancel_drag()
{
    is_dragging_         = false;
    current_drop_target_ = DropTarget{};
}

// ─── Layout ──────────────────────────────────────────────────────────────────

void DockSystem::update_layout(const Rect& canvas_bounds)
{
    split_view_.update_layout(canvas_bounds);
}

std::vector<DockSystem::PaneInfo> DockSystem::get_pane_infos() const
{
    std::vector<PaneInfo> result;
    auto                  panes  = split_view_.all_panes();
    FigureId              active = split_view_.active_figure_index();

    for (const auto* pane : panes)
    {
        PaneInfo info;
        info.figure_index = pane->figure_index();
        info.bounds       = pane->content_bounds();   // Excludes per-pane tab header
        info.is_active    = (pane->figure_index() == active);
        info.pane_id      = pane->id();
        result.push_back(info);
    }
    return result;
}

// ─── Splitter interaction ────────────────────────────────────────────────────

bool DockSystem::is_over_splitter(float x, float y) const
{
    // const_cast is safe here — splitter_at_point only reads
    auto* self = const_cast<DockSystem*>(this);
    return self->split_view_.splitter_at_point(x, y) != nullptr;
}

SplitDirection DockSystem::splitter_direction_at(float x, float y) const
{
    auto* self     = const_cast<DockSystem*>(this);
    auto* splitter = self->split_view_.splitter_at_point(x, y);
    if (splitter)
    {
        return splitter->split_direction();
    }
    return SplitDirection::Horizontal;
}

void DockSystem::begin_splitter_drag(float x, float y)
{
    auto* splitter = split_view_.splitter_at_point(x, y);
    if (!splitter)
        return;

    float pos = (splitter->split_direction() == SplitDirection::Horizontal) ? x : y;
    split_view_.begin_splitter_drag(splitter, pos);
}

void DockSystem::update_splitter_drag(float mouse_pos)
{
    split_view_.update_splitter_drag(mouse_pos);
}

void DockSystem::end_splitter_drag()
{
    split_view_.end_splitter_drag();
    if (on_layout_changed_)
        on_layout_changed_();
}

void DockSystem::activate_pane_at(float x, float y)
{
    auto* pane = split_view_.pane_at_point(x, y);
    if (pane && pane->is_leaf())
    {
        split_view_.set_active_figure_index(pane->figure_index());
    }
}

bool DockSystem::move_figure_to_pane(FigureId figure_index, SplitPane::PaneId target_pane_id)
{
    auto* root = split_view_.root();
    if (!root)
        return false;

    auto* target = root->find_by_id(target_pane_id);
    if (!target || !target->is_leaf())
        return false;

    // Find the source pane that currently holds this figure
    auto       panes  = split_view_.all_panes();
    SplitPane* source = nullptr;
    for (auto* p : panes)
    {
        if (p->has_figure(figure_index))
        {
            source = const_cast<SplitPane*>(p);
            break;
        }
    }
    if (!source || source == target)
        return false;

    // Remove from source
    source->remove_figure(figure_index);

    // Add to target
    target->add_figure(figure_index);

    // If source is now empty, collapse it (unsplit its parent, keeping sibling)
    if (source->figure_count() == 0 && source->parent())
    {
        auto* parent          = source->parent();
        bool  source_is_first = (parent->first() == source);
        parent->unsplit(!source_is_first);   // Keep the sibling
    }

    split_view_.set_active_figure_index(figure_index);
    if (on_layout_changed_)
        on_layout_changed_();
    return true;
}

void DockSystem::activate_local_tab(SplitPane::PaneId pane_id, size_t local_index)
{
    auto* root = split_view_.root();
    if (!root)
        return;
    auto* pane = root->find_by_id(pane_id);
    if (!pane || !pane->is_leaf())
        return;
    pane->set_active_local_index(local_index);
    split_view_.set_active_figure_index(pane->figure_index());
}

// ─── Serialization ───────────────────────────────────────────────────────────

std::string DockSystem::serialize() const
{
    return split_view_.serialize();
}

bool DockSystem::deserialize(const std::string& data)
{
    bool result = split_view_.deserialize(data);
    if (result && on_layout_changed_)
        on_layout_changed_();
    return result;
}

// ─── Internal helpers ────────────────────────────────────────────────────────

DropTarget DockSystem::compute_drop_target(float x, float y) const
{
    auto* self = const_cast<DockSystem*>(this);
    auto* pane = self->split_view_.pane_at_point(x, y);
    if (!pane || !pane->is_leaf())
    {
        return DropTarget{};
    }

    Rect b = pane->bounds();
    if (b.w < 1.0f || b.h < 1.0f)
    {
        return DropTarget{};
    }

    // Compute edge zones
    float edge_w = std::max(b.w * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);
    float edge_h = std::max(b.h * DROP_ZONE_FRACTION, DROP_ZONE_MIN_SIZE);

    // Clamp to half the pane size
    edge_w = std::min(edge_w, b.w * 0.4f);
    edge_h = std::min(edge_h, b.h * 0.4f);

    float rel_x = x - b.x;
    float rel_y = y - b.y;

    DropZone zone = DropZone::Center;

    if (rel_x < edge_w)
    {
        zone = DropZone::Left;
    }
    else if (rel_x > b.w - edge_w)
    {
        zone = DropZone::Right;
    }
    else if (rel_y < edge_h)
    {
        zone = DropZone::Top;
    }
    else if (rel_y > b.h - edge_h)
    {
        zone = DropZone::Bottom;
    }

    DropTarget target;
    target.zone           = zone;
    target.target_pane    = pane;
    target.highlight_rect = compute_drop_highlight(pane, zone);
    return target;
}

Rect DockSystem::compute_drop_highlight(const SplitPane* pane, DropZone zone) const
{
    if (!pane)
        return Rect{};

    Rect b = pane->bounds();

    switch (zone)
    {
        case DropZone::Left:
            return Rect{b.x, b.y, b.w * 0.5f, b.h};
        case DropZone::Right:
            return Rect{b.x + b.w * 0.5f, b.y, b.w * 0.5f, b.h};
        case DropZone::Top:
            return Rect{b.x, b.y, b.w, b.h * 0.5f};
        case DropZone::Bottom:
            return Rect{b.x, b.y + b.h * 0.5f, b.w, b.h * 0.5f};
        case DropZone::Center:
            return b;
        case DropZone::None:
            return Rect{};
    }
    return Rect{};
}

}   // namespace spectra
