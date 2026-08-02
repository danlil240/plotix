#pragma once

// SubplotManager — multi-subplot layout for ROS2 live plotting (C4).
//
// Manages a configurable NxM grid of ROS2 field subscriptions inside a single
// Spectra Figure.  All subplot axes share a common X-axis range via
// AxisLinkManager (LinkAxis::X), and a shared cursor is broadcast across
// subplots whenever the user hovers over any one of them.
//
// Relationship to RosPlotManager:
//   SubplotManager owns its own Figure, its own set of GenericSubscribers (one
//   per slot), and uses presented_buffer auto-scroll.  It is entirely independent of
//   RosPlotManager — both can coexist in the same application.
//
// Typical usage:
//   Ros2Bridge bridge;  bridge.init(...);  bridge.start_spin();
//   MessageIntrospector intr;
//   SubplotManager mgr(bridge, intr, 3, 1);    // 3 rows, 1 column
//
//   auto h = mgr.add_plot(0, "/imu", "linear_acceleration.x");
//   auto h = mgr.add_plot(1, "/imu", "linear_acceleration.y");
//   auto h = mgr.add_plot(2, "/cmd_vel", "linear.x");
//
//   // In render loop:
//   mgr.poll();     // drain ring buffers, append to series, advance scrollers
//   mgr.figure();   // pass to renderer
//
// Grid layout:
//   Subplots are 1-indexed (slot 1 … rows*cols) following the Spectra
//   Figure::subplot() convention.  Helper index_of(row, col) converts
//   (1-based row, 1-based col) → 1-based slot index.
//
// Shared cursor:
//   The caller forwards hovered-axes cursor data via
//   notify_cursor(source_axes*, data_x, data_y).  SubplotManager broadcasts
//   to AxisLinkManager so the shared cursor is visible on all linked subplots.
//
// Thread-safety:
//   add_plot() / remove_plot() / clear() must be called from the render thread.
//   poll() is render-thread-only (SPSC ring buffer contract).
//   notify_cursor() is render-thread-only.
//   All configuration setters are render-thread-only.
//   AxisLinkManager is internally mutex-protected; all other state is not.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "generic_subscriber.hpp"
#include "axis_mode.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "ros_plot_manager.hpp"   // DirectWriteContext

// AxisLinkManager lives under src/ui/data/ — not a public include header.
// We forward-declare here and include the .hpp from subplot_manager.cpp.
namespace spectra
{
class AxisLinkManager;
}

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// SeriesEntry — one (topic, field) subscription within a subplot slot.
// ---------------------------------------------------------------------------

struct SeriesEntry
{
    std::string topic;
    std::string field_path;
    std::string type_name;

    spectra::LineSeries*               series{nullptr};
    std::unique_ptr<GenericSubscriber> subscriber;
    bool                               owns_subscriber{true};
    int                                extractor_id{-1};
    size_t                             samples_received{0};
    bool                               auto_fitted{false};
    size_t                             color_index{0};

    std::vector<FieldSample> drain_buf;

    // Direct-write context (thread-safe series mode).
    std::unique_ptr<DirectWriteContext> direct_ctx;

    bool active() const { return series != nullptr && !topic.empty(); }
};

// ---------------------------------------------------------------------------
// SubplotHandle — returned by add_plot(); allows direct access to axes/series.
// ---------------------------------------------------------------------------

struct SubplotHandle
{
    // 1-based slot index within the grid (same as Figure::subplot index).
    int slot{-1};

    // Topic and field that this slot tracks.
    std::string topic;
    std::string field_path;

    // Non-owning pointers into SubplotManager-owned storage.
    spectra::Axes*       axes{nullptr};
    spectra::LineSeries* series{nullptr};

    bool valid() const { return slot >= 1 && axes != nullptr && series != nullptr; }
};

class TopicDiscovery;

// ---------------------------------------------------------------------------
// SubplotManager — main class.
// ---------------------------------------------------------------------------

class SubplotManager
{
   public:
    // Minimum samples before live Y auto-fit starts.
    static constexpr size_t AUTO_FIT_SAMPLES = 1;

    // Maximum samples drained per slot per poll() call.
    static constexpr size_t MAX_DRAIN_PER_POLL = 4096;

    // -----------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------

    // Create a SubplotManager with a rows×cols grid.
    // rows and cols must each be >= 1.
    SubplotManager(Ros2Bridge&          bridge,
                   MessageIntrospector& intr,
                   int                  rows            = 1,
                   int                  cols            = 1,
                   spectra::Figure*     external_figure = nullptr);

    ~SubplotManager();

    // Non-copyable, non-movable.
    SubplotManager(const SubplotManager&)            = delete;
    SubplotManager& operator=(const SubplotManager&) = delete;
    SubplotManager(SubplotManager&&)                 = delete;
    SubplotManager& operator=(SubplotManager&&)      = delete;

    // -----------------------------------------------------------------
    // Grid size
    // -----------------------------------------------------------------

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int capacity() const { return rows_ * cols_; }

    // Convert (1-based row, 1-based col) → 1-based slot index.
    // Returns -1 if out of range.
    int index_of(int row, int col) const;

    // -----------------------------------------------------------------
    // Dynamic grid management
    // -----------------------------------------------------------------

    // Add a new row to the bottom of the grid.  Returns the 1-based slot
    // index of the first cell in the new row.
    int add_row();

    // Remove the last row from the grid.  Stops any subscriptions in the
    // removed slots.  Returns false if only 1 row remains.
    bool remove_last_row();

    // Compact the grid: shift active plots up to fill gaps, then remove
    // empty trailing rows.  Call after remove_plot() to reclaim space.
    void compact();

    // -----------------------------------------------------------------
    // Plot management  (multi-series per slot)
    // -----------------------------------------------------------------

    // Subscribe to (topic, field_path) and add it to slot (1-based).
    // A slot can hold multiple series — each add_plot() appends.
    SubplotHandle add_plot(int                slot,
                           const std::string& topic,
                           const std::string& field_path,
                           const std::string& type_name    = "",
                           size_t             buffer_depth = 10000);

    // Convenience: add_plot(row, col, topic, field_path, ...).
    SubplotHandle add_plot(int                row,
                           int                col,
                           const std::string& topic,
                           const std::string& field_path,
                           const std::string& type_name    = "",
                           size_t             buffer_depth = 10000);

    // Remove the plot in a slot (removes all series).  Returns false if slot is empty.
    bool remove_plot(int slot);

    // Remove a specific series from a slot by topic:field_path. Returns false
    // if not found.
    bool remove_series_from_slot(int slot, const std::string& topic, const std::string& field_path);

    // Clear all slots (removes all subscriptions, clears series data).
    void clear();

    // Stop live subscriptions and drop series/axes pointers without touching
    // Figure-owned storage.  Required when an external canvas Figure is
    // destroyed (e.g. spectra-ros window close) before ~SubplotManager().
    void detach_external_figure();

    // Is slot active (has at least one subscription)?
    bool has_plot(int slot) const;

    // Number of active (non-empty) slots.
    int active_count() const;

    // Number of active series in a specific slot.
    int slot_series_count(int slot) const;

    // Access individual series entries in a slot (0-based series index).
    const SeriesEntry* slot_series(int slot, int series_idx) const;

    // Returns whether a slot can be switched into custom-axis mode.
    bool slot_supports_custom_axes(int slot, std::string* reason_out = nullptr) const;

    // Returns numeric leaf field paths available for the slot's current topic type.
    std::vector<std::string> slot_numeric_fields(int slot) const;

    // Reconfigure the slot's primary series between time-series and custom-axis modes.
    bool configure_slot_axes(int                slot,
                             AxisMode           mode,
                             const std::string& x_field_path,
                             const std::string& y_field_path,
                             std::string*       error_out = nullptr);

    // Retrieve handle for a slot.  Returns invalid handle if slot is empty.
    SubplotHandle handle(int slot) const;

    // All active handles (snapshot).
    std::vector<SubplotHandle> handles() const;

    // -----------------------------------------------------------------
    // Frame update (render thread)
    // -----------------------------------------------------------------

    // Drain ring buffers and append new points to all active series.
    // Also prunes old data and manages auto-scroll via presented_buffer.
    // Call once per frame from the render thread.
    void poll();

    // -----------------------------------------------------------------
    // Figure access
    // -----------------------------------------------------------------

    spectra::Figure&       figure() { return *figure_; }
    const spectra::Figure& figure() const { return *figure_; }

    // Align figure canvas margins, background, and axis chrome with the active UI theme.
    void apply_plot_theme();

    // -----------------------------------------------------------------
    // Axis linking
    // -----------------------------------------------------------------

    // The AxisLinkManager shared across all subplot axes (X-axis linked).
    spectra::AxisLinkManager&       link_manager() { return *link_manager_; }
    const spectra::AxisLinkManager& link_manager() const { return *link_manager_; }

    // -----------------------------------------------------------------
    // Shared cursor
    // -----------------------------------------------------------------

    // Notify the manager that the cursor is at (data_x, data_y) on
    // source_axes.  Broadcasts via AxisLinkManager::update_shared_cursor().
    // Pass nullptr to clear the cursor.
    void notify_cursor(spectra::Axes* source_axes,
                       float          data_x,
                       float          data_y,
                       double         screen_x = 0.0,
                       double         screen_y = 0.0);

    // Clear the shared cursor (e.g. mouse left the window).
    void clear_cursor();

    // -----------------------------------------------------------------
    // Shared time origin
    // -----------------------------------------------------------------

    // Set a shared time origin for all slots.  New subplots inherit this
    // origin so their X-axis is synchronized with existing ones.
    void   set_shared_time_origin(double epoch_seconds);
    double shared_time_origin() const { return shared_time_origin_; }
    bool   has_shared_time_origin() const { return has_shared_origin_; }

    // -----------------------------------------------------------------
    // Auto-scroll (C2)
    // -----------------------------------------------------------------

    // Set the sliding time window (seconds) for all axes.
    void   set_time_window(double seconds);
    double time_window() const { return scroll_window_s_; }

    // Configure pruning of samples older than the current visible left edge.
    void   set_prune_buffer(double seconds);
    double prune_buffer() const { return prune_buffer_s_; }
    void   set_pruning_enabled(bool enabled) { pruning_enabled_ = enabled; }
    bool   pruning_enabled() const { return pruning_enabled_; }

    // Advance "now" for auto-scroll (call once per frame
    // before poll(), or let poll() call it automatically with wall clock).
    void set_now(double wall_time_s);

    // Pause / resume auto-scroll for a specific slot.
    void pause_scroll(int slot);
    void resume_scroll(int slot);
    bool is_scroll_paused(int slot) const;

    // Pause / resume all slots.
    void pause_all_scroll();
    void resume_all_scroll();

    // When enabled (default), time-series subplots share X-axis limits.
    void set_x_links_enabled(bool enabled);
    bool x_links_enabled() const { return x_links_enabled_; }
    void rebuild_x_links();

    // Total estimated memory across all active series.
    size_t total_memory_bytes() const;

    // Total committed points across all live series (for diagnostics).
    size_t total_point_count() const;

    // -----------------------------------------------------------------
    // Y-axis limit controls
    // -----------------------------------------------------------------

    // Set explicit Y-axis limits for a slot.
    void set_slot_ylim(int slot, double ymin, double ymax);

    // Clear manual Y limits so Y is derived automatically from the current X
    // view (live window while following, paused window otherwise).
    void clear_slot_ylim(int slot);

    // Clear all series data in a slot without removing the subplot or subscription.
    void clear_slot_data(int slot);

    // Restore automatic Y fitting for a slot.
    void auto_fit_slot_y(int slot);

    // -----------------------------------------------------------------
    // Per-slot time-window override
    // -----------------------------------------------------------------

    // Override the scroll time-window for a specific slot.
    // Pass seconds <= 0 to revert the slot to the global window.
    void set_slot_time_window(int slot, double seconds);

    // Returns the effective window for a slot (per-slot override or global).
    double slot_time_window(int slot) const;

    // Clear any per-slot override, reverting to the global window.
    void clear_slot_time_window(int slot);

    // -----------------------------------------------------------------
    // Subplot context menu helpers (ImGui, render-thread only)
    // -----------------------------------------------------------------

    // Possible actions returned from draw_slot_context_menu().
    enum class SubplotAction : uint8_t
    {
        None      = 0,
        Rename    = 1,
        Clear     = 2,
        Duplicate = 3,
        Detach    = 4,
    };

    // Draw an ImGui right-click popup for a subplot slot.
    // Call this after drawing the subplot's axes/canvas widget.
    // Returns the action the user selected (None if popup not open or no action).
    // `popup_id` must be unique per slot — callers typically pass e.g. "##sctx_1".
    SubplotAction draw_slot_context_menu(int slot, const char* popup_id);

    // -----------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------

    void   set_figure_size(uint32_t w, uint32_t h);
    void   set_auto_fit_samples(size_t n) { auto_fit_samples_ = n; }
    size_t auto_fit_samples() const { return auto_fit_samples_; }
    void   set_topic_discovery(TopicDiscovery* disc) { discovery_ = disc; }

    // ------------------------------------------------------------------
    // SlotEntry — per-slot state.  Now supports multiple series per slot.
    // ------------------------------------------------------------------
    struct SlotEntry
    {
        int slot{-1};   // 1-based

        // Legacy single-series fields (kept for backwards compat in handle()).
        std::string topic;
        std::string field_path;
        std::string type_name;

        // Non-owning Spectra pointers — owned by figure_.
        spectra::Axes*       axes{nullptr};
        spectra::LineSeries* series{nullptr};

        // ROS2 subscription (for legacy single-series mode).
        std::unique_ptr<GenericSubscriber> subscriber;
        int                                extractor_id{-1};
        int                                x_extractor_id{-1};
        int                                y_extractor_id{-1};

        // Auto-fit state.
        size_t samples_received{0};
        bool   auto_fitted{false};

        // Color index in the palette.
        size_t color_index{0};

        // Per-slot scratch drain buffer.
        std::vector<FieldSample> drain_buf;
        std::vector<FieldSample> x_drain_buf;
        std::vector<FieldSample> y_drain_buf;

        // Per-slot time-window override (seconds). <= 0 means use global.
        double time_window_override_s{-1.0};

        // The slot is the source of truth for axis mode and configured axes.
        AxisMode    axis_mode{AxisMode::TimeSeries};
        std::string x_field_path{AXIS_SOURCE_TIME};
        std::string y_field_path;
        size_t      buffer_depth{10000};

        // Multi-series support: additional series in this slot.
        std::vector<std::unique_ptr<SeriesEntry>> extra_series;

        // Manual Y-axis limits (nullopt = auto).
        std::optional<spectra::AxisLimits> manual_ylim;

        // Direct-write context for primary series (thread-safe mode).
        std::unique_ptr<DirectWriteContext> direct_ctx;

        bool active() const { return slot >= 1 && axes != nullptr; }

        // Total number of series (1 if legacy, 1+extra otherwise)
        int series_count() const
        {
            if (topic.empty() && extra_series.empty())
                return 0;
            int n = topic.empty() ? 0 : 1;
            return n + static_cast<int>(extra_series.size());
        }
    };

    // Public access to a SlotEntry (for UI controls).
    const SlotEntry* slot_entry_pub(int slot) const { return slot_entry(slot); }
    SlotEntry*       slot_entry_pub(int slot) { return slot_entry(slot); }

    // -----------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------

    // Called (render thread, inside poll()) when a new point is appended.
    using OnDataCallback = std::function<void(int slot, double t_sec, double value)>;
    void set_on_data(OnDataCallback cb) { on_data_cb_ = std::move(cb); }

   private:
    // Retrieve entry by 1-based slot (creates if needed up to capacity).
    SlotEntry*       slot_entry(int slot);
    const SlotEntry* slot_entry(int slot) const;

    // Auto-detect type name for topic.
    std::string detect_type(const std::string& topic) const;

    // Resolve numeric field paths for a topic type.
    std::vector<std::string> numeric_fields_for_type(const std::string& type_name) const;

    bool validate_axis_config(const SlotEntry&   se,
                              AxisMode           mode,
                              const std::string& resolved_type,
                              const std::string& x_field_path,
                              const std::string& y_field_path,
                              std::string*       error_out) const;

    bool rebuild_primary_slot_subscription(SlotEntry&   se,
                                           size_t       buffer_depth,
                                           std::string* error_out);

    void apply_slot_presented_buffer(SlotEntry& se);

    void reset_slot_axis_config(SlotEntry& se);

    // Assign next palette color.
    spectra::Color next_color();

    // Update slot labels based on its configured axis mode and series layout.
    void update_slot_labels(SlotEntry& se);

    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------

    Ros2Bridge&          bridge_;
    MessageIntrospector& intr_;

    int rows_{1};
    int cols_{1};

    std::unique_ptr<spectra::Figure>          owned_figure_;
    spectra::Figure*                          figure_{nullptr};
    std::unique_ptr<spectra::AxisLinkManager> link_manager_;

    // Slots vector indexed [slot-1] (0-based internally).
    // Size is always rows_ * cols_ after construction.
    std::vector<SlotEntry> slots_;

    // Color assignment cursor.
    size_t color_cursor_{0};

    // Configuration.
    static constexpr double DEFAULT_SCROLL_WINDOW_S = 30.0;
    static constexpr double DEFAULT_PRUNE_BUFFER_S  = 20.0;
    double                  scroll_window_s_        = DEFAULT_SCROLL_WINDOW_S;
    double                  prune_buffer_s_         = DEFAULT_PRUNE_BUFFER_S;
    bool                    pruning_enabled_        = true;
    bool                    x_links_enabled_        = true;
    size_t                  auto_fit_samples_       = AUTO_FIT_SAMPLES;

    // Shared time origin across all slots.
    double                shared_time_origin_{0.0};
    bool                  has_shared_origin_{false};
    std::optional<double> explicit_now_s_;

    OnDataCallback on_data_cb_;

    TopicDiscovery* discovery_{nullptr};
};

}   // namespace spectra::adapters::ros2
