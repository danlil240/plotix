#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>   // For Rect
#include <string>
#include <vector>

namespace spectra
{

// ─── Split direction ─────────────────────────────────────────────────────────

enum class SplitDirection
{
    Horizontal,   // Left | Right  (vertical divider)
    Vertical      // Top / Bottom  (horizontal divider)
};

// ─── SplitPane ───────────────────────────────────────────────────────────────
// A leaf or internal node in the split tree.
// Leaf nodes hold a figure_index; internal nodes hold two children + a ratio.

class SplitPane
{
   public:
    using PaneId = uint32_t;

    // Create a leaf pane bound to a figure id
    explicit SplitPane(FigureId figure_id = 0);
    ~SplitPane() = default;

    // Non-copyable, movable
    SplitPane(const SplitPane&)                = delete;
    SplitPane& operator=(const SplitPane&)     = delete;
    SplitPane(SplitPane&&) noexcept            = default;
    SplitPane& operator=(SplitPane&&) noexcept = default;

    // ── Tree structure ──────────────────────────────────────────────────

    // Split this leaf pane into two children. Returns pointer to the new
    // (second) child pane. The original figure_index moves to the first child.
    // new_figure_id is assigned to the second child.
    // Returns nullptr if this pane is already split.
    SplitPane* split(SplitDirection direction, FigureId new_figure_index, float ratio = 0.5f);

    // Unsplit: collapse this internal node back to a leaf, keeping the
    // child identified by keep_first (true = first child, false = second).
    // Returns true on success, false if already a leaf.
    bool unsplit(bool keep_first = true);

    // ── Queries ─────────────────────────────────────────────────────────

    bool is_leaf() const { return !first_ && !second_; }
    bool is_split() const { return first_ && second_; }

    PaneId   id() const { return id_; }
    FigureId figure_index() const { return figure_index_; }
    void     set_figure_index(FigureId idx);

    // ── Multi-figure per pane (per-pane tab bar) ────────────────────────

    const std::vector<FigureId>& figure_indices() const { return figure_indices_; }
    size_t                       active_local_index() const { return active_local_; }
    void                         set_active_local_index(size_t local_idx);
    void                         add_figure(FigureId fig_id);
    void                         remove_figure(FigureId fig_id);
    bool                         move_figure(FigureId fig_id, size_t target_index);
    bool                         has_figure(FigureId fig_id) const;
    size_t                       figure_count() const { return figure_indices_.size(); }
    void                         swap_contents(SplitPane& other);

    // Tab header height (only drawn when pane has >1 figure or view is split)
    static constexpr float PANE_TAB_HEIGHT = 26.0f;

    // Content bounds (bounds minus tab header)
    Rect content_bounds() const;

    SplitDirection split_direction() const { return split_direction_; }
    float          split_ratio() const { return split_ratio_; }
    void           set_split_ratio(float ratio);

    SplitPane* first() const { return first_.get(); }
    SplitPane* second() const { return second_.get(); }
    SplitPane* parent() const { return parent_; }

    // ── Layout ──────────────────────────────────────────────────────────

    // Compute bounds for this pane and all descendants
    void compute_layout(const Rect& bounds);
    Rect bounds() const { return bounds_; }

    // Get the splitter handle rect (only valid for internal nodes)
    Rect splitter_rect() const;

    // ── Traversal ───────────────────────────────────────────────────────

    // Collect all leaf panes (in depth-first order)
    void collect_leaves(std::vector<SplitPane*>& out);
    void collect_leaves(std::vector<const SplitPane*>& out) const;

    // Find the leaf pane containing the given figure id (nullptr if not found)
    SplitPane*       find_by_figure(FigureId figure_index);
    const SplitPane* find_by_figure(FigureId figure_index) const;

    // Find the leaf pane whose bounds contain the given point
    SplitPane* find_at_point(float x, float y);

    // Find the pane by id
    SplitPane* find_by_id(PaneId id);

    // Count total nodes (leaves + internal)
    size_t count_nodes() const;
    size_t count_leaves() const;

    // ── Serialization ───────────────────────────────────────────────────

    std::string                       serialize() const;
    static std::unique_ptr<SplitPane> deserialize(const std::string& data);
    void remap_figure_ids(const std::function<FigureId(FigureId)>& remap);

    // ── Constants ────────────────────────────────────────────────────────

    static constexpr float SPLITTER_WIDTH = 6.0f;
    static constexpr float MIN_PANE_SIZE  = 100.0f;
    static constexpr float MIN_RATIO      = 0.1f;
    static constexpr float MAX_RATIO      = 0.9f;

   private:
    PaneId                id_;
    FigureId              figure_index_ = 0;
    std::vector<FigureId> figure_indices_;     // All figures in this pane
    size_t                active_local_ = 0;   // Index into figure_indices_

    SplitDirection split_direction_ = SplitDirection::Horizontal;
    float          split_ratio_     = 0.5f;

    SplitPane*                 parent_ = nullptr;
    std::unique_ptr<SplitPane> first_;
    std::unique_ptr<SplitPane> second_;

    Rect bounds_{};

    static PaneId next_id();
};

// ─── SplitViewManager ────────────────────────────────────────────────────────
// High-level manager for the split view system.
// Owns the root SplitPane tree and provides convenience operations.
// Thread-safe via internal mutex.

class SplitViewManager
{
   public:
    using SplitCallback      = std::function<void(SplitPane* pane)>;
    using PaneChangeCallback = std::function<void(FigureId figure_id)>;

    SplitViewManager();
    ~SplitViewManager() = default;

    // Non-copyable
    SplitViewManager(const SplitViewManager&)            = delete;
    SplitViewManager& operator=(const SplitViewManager&) = delete;

    // ── Split operations ────────────────────────────────────────────────

    // Split the pane containing figure_index. Returns the new pane, or
    // nullptr if the figure is not found or max splits reached.
    SplitPane* split_pane(FigureId       figure_index,
                          SplitDirection direction,
                          FigureId       new_figure_index,
                          float          ratio = 0.5f);

    // Split the active pane (convenience)
    SplitPane* split_active(SplitDirection direction,
                            FigureId       new_figure_index,
                            float          ratio = 0.5f);

    // Close a split pane (unsplit its parent, keeping the sibling)
    bool close_pane(FigureId figure_index);

    // Unsplit all — collapse back to single pane
    void unsplit_all();

    // ── Active pane ─────────────────────────────────────────────────────

    FigureId         active_figure_index() const;
    void             set_active_figure_index(FigureId idx);
    SplitPane*       active_pane();
    const SplitPane* active_pane() const;

    // ── Layout ──────────────────────────────────────────────────────────

    // Recompute layout for all panes within the given canvas bounds
    void update_layout(const Rect& canvas_bounds);
    Rect canvas_bounds() const { return canvas_bounds_; }

    // ── Queries ─────────────────────────────────────────────────────────

    bool                          is_split() const;
    size_t                        pane_count() const;
    std::vector<SplitPane*>       all_panes();
    std::vector<const SplitPane*> all_panes() const;

    // Get the pane at a screen position
    SplitPane* pane_at_point(float x, float y);

    // Get the pane for a figure
    SplitPane* pane_for_figure(FigureId figure_index);

    // Check if a figure is visible in any pane
    bool is_figure_visible(FigureId figure_index) const;

    // ── Splitter interaction ────────────────────────────────────────────

    // Hit-test splitters. Returns the internal node whose splitter is hit,
    // or nullptr if no splitter is at that position.
    SplitPane* splitter_at_point(float x, float y);

    // Begin/update/end splitter drag
    void       begin_splitter_drag(SplitPane* splitter_pane, float mouse_pos);
    void       update_splitter_drag(float mouse_pos);
    void       end_splitter_drag();
    bool       is_dragging_splitter() const { return dragging_splitter_ != nullptr; }
    SplitPane* dragging_splitter() const { return dragging_splitter_; }

    // ── Root access ─────────────────────────────────────────────────────

    SplitPane*       root() { return root_.get(); }
    const SplitPane* root() const { return root_.get(); }

    // ── Serialization ───────────────────────────────────────────────────

    std::string serialize() const;
    bool        deserialize(const std::string& data);
    void        remap_figure_ids(const std::function<FigureId(FigureId)>& remap);

    // ── Callbacks ───────────────────────────────────────────────────────

    void set_on_split(SplitCallback cb) { on_split_ = std::move(cb); }
    void set_on_unsplit(SplitCallback cb) { on_unsplit_ = std::move(cb); }
    void set_on_active_changed(PaneChangeCallback cb) { on_active_changed_ = std::move(cb); }

    // ── Constants ────────────────────────────────────────────────────────

    static constexpr size_t MAX_PANES = 8;

   private:
    std::unique_ptr<SplitPane> root_;
    FigureId                   active_figure_index_ = 0;
    Rect                       canvas_bounds_{};
    mutable std::mutex         mutex_;

    // Splitter drag state
    SplitPane* dragging_splitter_ = nullptr;
    float      drag_start_pos_    = 0.0f;
    float      drag_start_ratio_  = 0.5f;

    // Callbacks
    SplitCallback      on_split_;
    SplitCallback      on_unsplit_;
    PaneChangeCallback on_active_changed_;

    // Internal helpers
    SplitPane* find_splitter_recursive(SplitPane* node, float x, float y);
    void       recompute_layout();
};

}   // namespace spectra
