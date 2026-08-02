// SubplotManager — implementation.
//
// See subplot_manager.hpp for design notes.

#include "subplot_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <spectra/color.hpp>
#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "ui/theme/theme.hpp"
#endif

#include "plot_series_pruning.hpp"

namespace
{
constexpr size_t CUSTOM_AXES_MAX_POINTS = 100'000;
}   // namespace

// AxisLinkManager lives under src/ui/data/ — included here (not in the public
// header) so that adapter users don't need src/ui on their include path.
#include "ui/data/axis_link.hpp"
#include "topic_discovery.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SubplotManager::SubplotManager(Ros2Bridge&          bridge,
                               MessageIntrospector& intr,
                               int                  rows,
                               int                  cols,
                               spectra::Figure*     external_figure)
    : bridge_(bridge), intr_(intr), rows_(rows < 1 ? 1 : rows), cols_(cols < 1 ? 1 : cols)
{
    if (external_figure)
    {
        figure_ = external_figure;
    }
    else
    {
        // Create the shared Figure with default size.
        spectra::FigureConfig cfg;
        cfg.width  = 1280;
        cfg.height = static_cast<uint32_t>(720 * rows_ / std::max(1, cols_));
        if (cfg.height < 400)
            cfg.height = 400;

        owned_figure_ = std::make_unique<spectra::Figure>(cfg);
        figure_       = owned_figure_.get();
    }

    if (figure_)
    {
        auto& style              = figure_->style();
        style.margin_left        = 52.0f;
        style.margin_right       = 14.0f;
        style.margin_top         = 24.0f;
        style.margin_bottom      = 40.0f;
        style.subplot_hgap       = 20.0f;
        style.subplot_vgap       = 24.0f;
        style.min_subplot_height = 100.0f;
        // Night-theme bg_canvas fallback until apply_plot_theme() runs on first draw.
        style.background = spectra::Color(0.075f, 0.125f, 0.196f, 1.0f);
    }

    // Pre-create all subplot Axes so they exist in the figure's axes_ list.
    // This also sets the figure's grid_rows_ / grid_cols_ to the final values.
    const int n = rows_ * cols_;
    for (int i = 1; i <= n; ++i)
        figure_->subplot(rows_, cols_, i);

    // Create the AxisLinkManager.
    link_manager_ = std::make_unique<spectra::AxisLinkManager>();

    // Initialise the slots vector — one SlotEntry per grid cell.
    // SlotEntry is non-copyable (unique_ptr member), so emplace_back each entry.
    slots_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        slots_.emplace_back();
        slots_.back().slot = i + 1;
        // Axes pointer — pre-created above, index i corresponds to subplot(rows,cols,i+1).
        slots_.back().axes = figure_->axes()[static_cast<size_t>(i)].get();
        slots_.back().axes->presented_buffer(static_cast<float>(scroll_window_s_));
    }
}

void SubplotManager::apply_plot_theme()
{
    if (!figure_)
        return;

    auto& style              = figure_->style();
    style.margin_left        = 52.0f;
    style.margin_right       = 14.0f;
    style.margin_top         = 24.0f;
    style.margin_bottom      = 40.0f;
    style.subplot_hgap       = 20.0f;
    style.subplot_vgap       = 24.0f;
    style.min_subplot_height = 100.0f;

#ifdef SPECTRA_USE_IMGUI
    const auto& th   = ui::theme();
    style.background = spectra::Color(th.bg_canvas.r, th.bg_canvas.g, th.bg_canvas.b, 1.0f);

    const spectra::Color tick_color(th.tick_label.r, th.tick_label.g, th.tick_label.b, 1.0f);
    for (const auto& ax_ptr : figure_->axes())
    {
        if (!ax_ptr)
            continue;
        ax_ptr->grid(true);
        ax_ptr->show_border(true);
        auto& axis_style       = ax_ptr->axis_style();
        axis_style.tick_color  = tick_color;
        axis_style.label_color = tick_color;
        axis_style.grid_color  = {0.0f, 0.0f, 0.0f, 0.0f};
    }
#else
    style.background = spectra::Color(0.075f, 0.125f, 0.196f, 1.0f);
#endif
}

void SubplotManager::detach_external_figure()
{
    if (owned_figure_)
        return;

    for (auto& se : slots_)
    {
        if (se.direct_ctx)
            se.direct_ctx->active.store(false, std::memory_order_release);

        if (se.subscriber)
        {
            se.subscriber->stop();
            se.subscriber.reset();
        }

        for (auto& es : se.extra_series)
        {
            if (es->direct_ctx)
                es->direct_ctx->active.store(false, std::memory_order_release);
            if (es->subscriber)
            {
                es->subscriber->stop();
                es->subscriber.reset();
            }
            es->series = nullptr;
        }

        se.extra_series.clear();
        se.series = nullptr;
        se.axes   = nullptr;
        se.topic.clear();
        se.field_path.clear();
        se.type_name.clear();
        se.extractor_id     = -1;
        se.samples_received = 0;
        se.auto_fitted      = false;
        se.drain_buf.clear();
        se.direct_ctx.reset();
    }
}

SubplotManager::~SubplotManager()
{
    // When bound to an external Figure (spectra-ros main canvas), that figure
    // can be destroyed by WindowManager before RosAppShell shutdown runs.
    if (!owned_figure_)
    {
        detach_external_figure();
        return;
    }

    clear();
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

int SubplotManager::index_of(int row, int col) const
{
    if (row < 1 || row > rows_ || col < 1 || col > cols_)
        return -1;
    return (row - 1) * cols_ + col;
}

// ---------------------------------------------------------------------------
// Plot management
// ---------------------------------------------------------------------------

SubplotHandle SubplotManager::add_plot(int                slot,
                                       const std::string& topic,
                                       const std::string& field_path,
                                       const std::string& type_name,
                                       size_t             buffer_depth)
{
    SubplotHandle bad;
    bad.slot = -1;

    if (slot < 1 || slot > capacity())
        return bad;
    if (topic.empty() || field_path.empty())
        return bad;

    // Resolve message type.
    std::string resolved_type = type_name;
    if (resolved_type.empty())
    {
        resolved_type = detect_type(topic);
        if (resolved_type.empty())
            return bad;
    }

    SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];

    // Check if this exact topic:field is already in the slot.
    if (se.topic == topic && se.field_path == field_path)
        return bad;   // already plotting this
    for (const auto& es : se.extra_series)
    {
        if (es->topic == topic && es->field_path == field_path)
            return bad;
    }

    // If the slot already has a primary series, add as extra.
    if (!se.topic.empty() && se.series != nullptr)
    {
        if (se.axis_mode == AxisMode::CustomAxes)
            return bad;

        auto entry        = std::make_unique<SeriesEntry>();
        entry->topic      = topic;
        entry->field_path = field_path;
        entry->type_name  = resolved_type;

        std::string    lbl   = topic + "/" + field_path;
        spectra::Color color = next_color();
        entry->color_index   = color_cursor_ - 1;

        spectra::LineSeries& ls = se.axes->line();
        ls.label(lbl);
        ls.color(color);
        ls.width(3.0f);
        entry->series = &ls;

        // Configure subscriber — reuse the primary subscription when the topic
        // and type match so we deserialize each message only once.
        if (bridge_.is_ok())
        {
            GenericSubscriber* shared = nullptr;
            if (se.subscriber && se.subscriber->is_running() && se.topic == topic
                && se.type_name == resolved_type)
            {
                shared = se.subscriber.get();
            }

            int eid = -1;
            if (shared)
            {
                eid = shared->add_field(field_path);
                if (eid < 0)
                    return bad;
            }
            else
            {
                auto sub = std::make_unique<GenericSubscriber>(bridge_.node(),
                                                               topic,
                                                               resolved_type,
                                                               intr_,
                                                               buffer_depth);
                eid      = sub->add_field(field_path);
                if (eid < 0)
                    return bad;
                if (!sub->start())
                    return bad;
                entry->subscriber      = std::move(sub);
                entry->owns_subscriber = true;
                entry->drain_buf.reserve(std::min(buffer_depth, MAX_DRAIN_PER_POLL));
            }

            // Enable thread-safe series + direct-write callback.
            entry->series->set_thread_safe(true);
            entry->direct_ctx      = std::make_unique<DirectWriteContext>();
            entry->extractor_id    = eid;
            entry->owns_subscriber = (shared == nullptr);

            auto*      ctx     = entry->direct_ctx.get();
            auto*      origin  = &shared_time_origin_;
            auto*      has_o   = &has_shared_origin_;
            auto*      series  = entry->series;
            auto*      data_cb = &on_data_cb_;
            int        slot_id = slot;
            const auto cb = [ctx, origin, has_o, series, data_cb, slot_id](double t_sec, double val)
            {
                if (!ctx->active.load(std::memory_order_acquire))
                    return;
                if (!*has_o)
                {
                    *origin = t_sec;
                    *has_o  = true;
                }
                const double t_rel = t_sec - *origin;
                series->append(static_cast<float>(t_rel), static_cast<float>(val));
                ctx->samples_written.fetch_add(1, std::memory_order_relaxed);

                if (*data_cb)
                    (*data_cb)(slot_id, t_sec, val);
            };

            if (shared)
                shared->set_field_callback(eid, cb);
            else
                entry->subscriber->set_field_callback(eid, cb);
        }

        SubplotHandle h;
        h.slot       = slot;
        h.topic      = topic;
        h.field_path = field_path;
        h.axes       = se.axes;
        h.series     = entry->series;

        se.extra_series.push_back(std::move(entry));

        // Update labels to reflect the slot state.
        update_slot_labels(se);

        rebuild_x_links();
        return h;
    }

    // Primary series (slot was empty).
    // If there was a previous subscription, stop it.
    if (se.subscriber)
    {
        se.subscriber->stop();
        se.subscriber.reset();
    }

    se.topic        = topic;
    se.field_path   = field_path;
    se.type_name    = resolved_type;
    se.buffer_depth = buffer_depth;

    // Clear any stale series data.
    if (se.series)
    {
        se.axes->clear_series();
        se.series = nullptr;
    }

    // Build the label.
    std::string lbl = topic + "/" + field_path;

    // Add a LineSeries to the subplot axes.
    spectra::Color color    = next_color();
    se.color_index          = color_cursor_ - 1;
    spectra::LineSeries& ls = se.axes->line();
    ls.label(lbl);
    ls.color(color);
    ls.width(3.0f);
    se.series = &ls;

    // Reset auto-fit state.
    se.samples_received = 0;
    se.auto_fitted      = false;

    std::string error;
    if (!configure_slot_axes(slot, AxisMode::TimeSeries, AXIS_SOURCE_TIME, field_path, &error))
        return bad;

    // Re-link X axes across all active slots.
    rebuild_x_links();

    SubplotHandle h;
    h.slot       = slot;
    h.topic      = topic;
    h.field_path = field_path;
    h.axes       = se.axes;
    h.series     = se.series;
    return h;
}

SubplotHandle SubplotManager::add_plot(int                row,
                                       int                col,
                                       const std::string& topic,
                                       const std::string& field_path,
                                       const std::string& type_name,
                                       size_t             buffer_depth)
{
    return add_plot(index_of(row, col), topic, field_path, type_name, buffer_depth);
}

bool SubplotManager::remove_plot(int slot)
{
    if (slot < 1 || slot > capacity())
        return false;

    SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    if (!se.active() || (se.topic.empty() && se.extra_series.empty()))
        return false;

    // Stop extra series subscribers.
    for (auto& es : se.extra_series)
    {
        if (es->subscriber)
        {
            es->subscriber->stop();
            es->subscriber.reset();
        }
    }
    se.extra_series.clear();

    if (se.subscriber)
    {
        se.subscriber->stop();
        se.subscriber.reset();
    }

    if (se.series)
    {
        se.axes->clear_series();
        se.series = nullptr;
    }

    se.topic.clear();
    se.field_path.clear();
    se.type_name.clear();
    se.extractor_id     = -1;
    se.x_extractor_id   = -1;
    se.y_extractor_id   = -1;
    se.samples_received = 0;
    se.auto_fitted      = false;
    se.drain_buf.clear();
    se.x_drain_buf.clear();
    se.y_drain_buf.clear();
    se.manual_ylim.reset();
    se.direct_ctx.reset();
    reset_slot_axis_config(se);
    apply_slot_presented_buffer(se);
    update_slot_labels(se);

    // Re-link (may have fewer active slots now).
    rebuild_x_links();

    return true;
}

bool SubplotManager::remove_series_from_slot(int                slot,
                                             const std::string& topic,
                                             const std::string& field_path)
{
    if (slot < 1 || slot > capacity())
        return false;

    SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];

    // Check extra series first.
    for (auto it = se.extra_series.begin(); it != se.extra_series.end(); ++it)
    {
        if ((*it)->topic == topic && (*it)->field_path == field_path)
        {
            if ((*it)->direct_ctx)
                (*it)->direct_ctx->active.store(false, std::memory_order_release);
            if ((*it)->owns_subscriber && (*it)->subscriber)
            {
                (*it)->subscriber->stop();
                (*it)->subscriber.reset();
            }
            else if (!(*it)->owns_subscriber && se.subscriber && (*it)->extractor_id >= 0)
            {
                se.subscriber->remove_field((*it)->extractor_id);
            }
            // Remove the LineSeries from axes.
            const auto& all_series = se.axes->series();
            for (size_t i = 0; i < all_series.size(); ++i)
            {
                if (all_series[i].get() == (*it)->series)
                {
                    se.axes->remove_series(i);
                    break;
                }
            }
            se.extra_series.erase(it);
            update_slot_labels(se);
            return true;
        }
    }

    // Check primary series.
    if (se.topic == topic && se.field_path == field_path)
    {
        if (se.extra_series.empty())
        {
            // Only primary; full clear.
            return remove_plot(slot);
        }

        // Promote first extra to primary.
        if (se.subscriber && se.extractor_id >= 0)
            se.subscriber->remove_field(se.extractor_id);

        // Remove the primary LineSeries from axes.
        const auto& all_series = se.axes->series();
        for (size_t i = 0; i < all_series.size(); ++i)
        {
            if (all_series[i].get() == se.series)
            {
                se.axes->remove_series(i);
                break;
            }
        }

        auto& first   = se.extra_series.front();
        se.topic      = first->topic;
        se.field_path = first->field_path;
        se.type_name  = first->type_name;
        se.series     = first->series;
        if (first->owns_subscriber)
            se.subscriber = std::move(first->subscriber);
        se.extractor_id     = first->extractor_id;
        se.x_extractor_id   = -1;
        se.y_extractor_id   = first->extractor_id;
        se.samples_received = first->samples_received;
        se.auto_fitted      = first->auto_fitted;
        se.color_index      = first->color_index;
        se.drain_buf        = std::move(first->drain_buf);
        se.x_drain_buf.clear();
        se.y_drain_buf.clear();
        se.direct_ctx   = std::move(first->direct_ctx);
        se.axis_mode    = AxisMode::TimeSeries;
        se.x_field_path = AXIS_SOURCE_TIME;
        se.y_field_path = se.field_path;
        se.extra_series.erase(se.extra_series.begin());
        update_slot_labels(se);
        return true;
    }

    return false;
}

void SubplotManager::clear()
{
    for (int i = 1; i <= capacity(); ++i)
        remove_plot(i);
}

bool SubplotManager::has_plot(int slot) const
{
    if (slot < 1 || slot > capacity())
        return false;
    const SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    return (!se.topic.empty() && se.series != nullptr) || !se.extra_series.empty();
}

int SubplotManager::active_count() const
{
    int count = 0;
    for (const auto& se : slots_)
    {
        if ((!se.topic.empty() && se.series != nullptr) || !se.extra_series.empty())
            ++count;
    }
    return count;
}

int SubplotManager::slot_series_count(int slot) const
{
    if (slot < 1 || slot > capacity())
        return 0;
    return slots_[static_cast<size_t>(slot - 1)].series_count();
}

const SeriesEntry* SubplotManager::slot_series(int slot, int series_idx) const
{
    if (slot < 1 || slot > capacity())
        return nullptr;
    const SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    if (series_idx < 0 || series_idx >= se.series_count())
        return nullptr;

    // Index 0 = the primary series (wrapped as a static local).
    if (series_idx == 0 && !se.topic.empty())
    {
        static thread_local SeriesEntry primary;
        primary.topic       = se.topic;
        primary.field_path  = se.field_path;
        primary.type_name   = se.type_name;
        primary.series      = se.series;
        primary.color_index = se.color_index;
        return &primary;
    }

    // Adjust index: if primary exists, extra starts at index 1.
    int extra_idx = se.topic.empty() ? series_idx : (series_idx - 1);
    if (extra_idx < 0 || extra_idx >= static_cast<int>(se.extra_series.size()))
        return nullptr;
    return se.extra_series[static_cast<size_t>(extra_idx)].get();
}

SubplotHandle SubplotManager::handle(int slot) const
{
    SubplotHandle bad;
    bad.slot = -1;

    if (slot < 1 || slot > capacity())
        return bad;

    const SlotEntry& se = slots_[static_cast<size_t>(slot - 1)];
    if (!se.active() || se.topic.empty())
        return bad;

    SubplotHandle h;
    h.slot       = slot;
    h.topic      = se.topic;
    h.field_path = se.field_path;
    h.axes       = se.axes;
    h.series     = se.series;
    return h;
}

std::vector<SubplotHandle> SubplotManager::handles() const
{
    std::vector<SubplotHandle> result;
    for (const auto& se : slots_)
    {
        if (!se.topic.empty() && se.series != nullptr)
        {
            SubplotHandle h;
            h.slot       = se.slot;
            h.topic      = se.topic;
            h.field_path = se.field_path;
            h.axes       = se.axes;
            h.series     = se.series;
            result.push_back(h);
        }
        for (const auto& es : se.extra_series)
        {
            if (es->active())
            {
                SubplotHandle h;
                h.slot       = se.slot;
                h.topic      = es->topic;
                h.field_path = es->field_path;
                h.axes       = se.axes;
                h.series     = es->series;
                result.push_back(h);
            }
        }
    }
    return result;
}

bool SubplotManager::slot_supports_custom_axes(int slot, std::string* reason_out) const
{
    const SlotEntry* se = slot_entry(slot);
    if (!se || !se->series || se->topic.empty())
    {
        if (reason_out)
            *reason_out = "Select a live plot first.";
        return false;
    }
    if (se->series_count() != 1)
    {
        if (reason_out)
            *reason_out = "Custom axes are available only for slots with a single live series from "
                          "one topic.";
        return false;
    }

    std::string resolved_type = se->type_name.empty() ? detect_type(se->topic) : se->type_name;
    const auto  numeric       = numeric_fields_for_type(resolved_type);
    if (numeric.empty())
    {
        if (reason_out)
            *reason_out = "No numeric fields are available for this topic type.";
        return false;
    }

    if (reason_out)
        reason_out->clear();
    return true;
}

std::vector<std::string> SubplotManager::slot_numeric_fields(int slot) const
{
    const SlotEntry* se = slot_entry(slot);
    if (!se)
        return {};
    const std::string resolved_type =
        se->type_name.empty() ? detect_type(se->topic) : se->type_name;
    return numeric_fields_for_type(resolved_type);
}

bool SubplotManager::configure_slot_axes(int                slot,
                                         AxisMode           mode,
                                         const std::string& x_field_path,
                                         const std::string& y_field_path,
                                         std::string*       error_out)
{
    SlotEntry* se = slot_entry(slot);
    if (!se || !se->series || se->topic.empty())
    {
        if (error_out)
            *error_out = "Select a live plot first.";
        return false;
    }

    const std::string resolved_type =
        se->type_name.empty() ? detect_type(se->topic) : se->type_name;
    if (resolved_type.empty())
    {
        if (error_out)
            *error_out = "Topic type could not be resolved.";
        return false;
    }

    const std::string desired_x = (mode == AxisMode::TimeSeries || x_field_path.empty())
                                      ? std::string(AXIS_SOURCE_TIME)
                                      : x_field_path;
    const std::string desired_y = y_field_path.empty() ? se->field_path : y_field_path;

    const bool live_path_ready =
        !bridge_.is_ok() || (se->subscriber != nullptr)
        || (se->axis_mode == AxisMode::TimeSeries && se->direct_ctx != nullptr)
        || (se->axis_mode == AxisMode::TimeSeries && se->extractor_id >= 0)
        || (se->axis_mode == AxisMode::CustomAxes && se->y_extractor_id >= 0);

    if (live_path_ready && se->axis_mode == mode && se->x_field_path == desired_x
        && se->y_field_path == desired_y && se->type_name == resolved_type)
    {
        if (error_out)
            error_out->clear();
        return true;
    }

    if (!validate_axis_config(*se, mode, resolved_type, desired_x, desired_y, error_out))
        return false;

    const auto old_type_name    = se->type_name;
    const auto old_field_path   = se->field_path;
    const auto old_axis_mode    = se->axis_mode;
    const auto old_x_field_path = se->x_field_path;
    const auto old_y_field_path = se->y_field_path;
    const bool old_thread_safe  = se->series->is_thread_safe();

    se->type_name    = resolved_type;
    se->field_path   = desired_y;
    se->axis_mode    = mode;
    se->x_field_path = desired_x;
    se->y_field_path = desired_y;

    if (!rebuild_primary_slot_subscription(*se, se->buffer_depth, error_out))
    {
        se->type_name    = old_type_name;
        se->field_path   = old_field_path;
        se->axis_mode    = old_axis_mode;
        se->x_field_path = old_x_field_path;
        se->y_field_path = old_y_field_path;
        se->series->set_thread_safe(old_thread_safe);
        return false;
    }

    se->samples_received = 0;
    se->auto_fitted      = false;
    se->manual_ylim.reset();
    se->drain_buf.clear();
    se->x_drain_buf.clear();
    se->y_drain_buf.clear();

    if (se->series)
    {
        se->series->set_x(std::span<const float>{});
        se->series->set_y(std::span<const float>{});
    }

    apply_slot_presented_buffer(*se);
    update_slot_labels(*se);
    rebuild_x_links();

    if (error_out)
        error_out->clear();
    return true;
}

// ---------------------------------------------------------------------------
// poll() — hot path (render thread, called every frame)
// ---------------------------------------------------------------------------

void SubplotManager::poll()
{
    const double wall_now = explicit_now_s_.value_or(
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());

    for (auto& se : slots_)
    {
        if (!se.active())
            continue;

        bool slot_received_new_data = false;

        // Establish shared time origin on first frame so all subplots are
        // synchronized and x-values use small relative seconds.
        if (!has_shared_origin_)
            set_shared_time_origin(wall_now);

        // -- Drain primary series --
        // TimeSeries keeps the existing direct-write path. CustomAxes uses
        // ring-buffer extraction so x/y can be paired from the same message.
        if (se.axis_mode == AxisMode::CustomAxes)
        {
            if (se.subscriber && se.subscriber->is_running() && se.y_extractor_id >= 0)
            {
                if (se.y_field_path.empty())
                    continue;

                if (se.x_field_path == AXIS_SOURCE_TIME)
                {
                    if (se.y_drain_buf.capacity() < MAX_DRAIN_PER_POLL)
                        se.y_drain_buf.resize(MAX_DRAIN_PER_POLL);

                    const size_t pending_y =
                        std::min(se.subscriber->pending(se.y_extractor_id), MAX_DRAIN_PER_POLL);
                    const size_t n = se.subscriber->pop_bulk(se.y_extractor_id,
                                                             se.y_drain_buf.data(),
                                                             pending_y);

                    if (n > 0)
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            const FieldSample& y_s   = se.y_drain_buf[i];
                            const double       t_sec = static_cast<double>(y_s.timestamp_ns) * 1e-9;
                            se.series->append(static_cast<float>(t_sec),
                                              static_cast<float>(y_s.value));

                            if (on_data_cb_)
                                on_data_cb_(se.slot, t_sec, y_s.value);
                        }

                        se.samples_received += n;
                        se.auto_fitted         = (se.samples_received >= auto_fit_samples_);
                        slot_received_new_data = true;
                    }
                }
                else if (se.x_extractor_id >= 0)
                {
                    if (se.x_drain_buf.capacity() < MAX_DRAIN_PER_POLL)
                        se.x_drain_buf.resize(MAX_DRAIN_PER_POLL);
                    if (se.y_drain_buf.capacity() < MAX_DRAIN_PER_POLL)
                        se.y_drain_buf.resize(MAX_DRAIN_PER_POLL);

                    const size_t pending_x =
                        std::min(se.subscriber->pending(se.x_extractor_id), MAX_DRAIN_PER_POLL);
                    const size_t pending_y =
                        std::min(se.subscriber->pending(se.y_extractor_id), MAX_DRAIN_PER_POLL);
                    const size_t pair_count = std::min(pending_x, pending_y);
                    const size_t nx         = se.subscriber->pop_bulk(se.x_extractor_id,
                                                              se.x_drain_buf.data(),
                                                              pair_count);
                    const size_t ny         = se.subscriber->pop_bulk(se.y_extractor_id,
                                                              se.y_drain_buf.data(),
                                                              pair_count);
                    const size_t n          = std::min(nx, ny);

                    if (n > 0)
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            const FieldSample& x_s   = se.x_drain_buf[i];
                            const FieldSample& y_s   = se.y_drain_buf[i];
                            const double       t_sec = static_cast<double>(y_s.timestamp_ns) * 1e-9;
                            se.series->append(static_cast<float>(x_s.value),
                                              static_cast<float>(y_s.value));

                            if (on_data_cb_)
                                on_data_cb_(se.slot, t_sec, y_s.value);
                        }

                        se.samples_received += n;
                        se.auto_fitted         = (se.samples_received >= auto_fit_samples_);
                        slot_received_new_data = true;
                    }
                }
            }
        }
        else if (se.direct_ctx)
        {
            // Commit pending data from the executor thread to the series.
            if (se.series)
                se.series->commit_pending();

            se.samples_received = se.direct_ctx->samples_written.load(std::memory_order_relaxed);
            se.auto_fitted      = (se.samples_received >= auto_fit_samples_);
            if (se.samples_received > 0)
                slot_received_new_data = true;
        }
        else if (se.subscriber && se.subscriber->is_running() && se.extractor_id >= 0)
        {
            if (se.drain_buf.capacity() < MAX_DRAIN_PER_POLL)
                se.drain_buf.resize(MAX_DRAIN_PER_POLL);

            const size_t pending =
                std::min(se.subscriber->pending(se.extractor_id), MAX_DRAIN_PER_POLL);
            const size_t n = se.subscriber->pop_bulk(se.extractor_id, se.drain_buf.data(), pending);

            if (n > 0)
            {
                const double origin = shared_time_origin_;
                for (size_t i = 0; i < n; ++i)
                {
                    const FieldSample& s     = se.drain_buf[i];
                    const double       t_sec = static_cast<double>(s.timestamp_ns) * 1e-9;
                    const double       t_rel = t_sec - origin;
                    se.series->append(static_cast<float>(t_rel), static_cast<float>(s.value));

                    if (on_data_cb_)
                        on_data_cb_(se.slot, t_sec, s.value);
                }

                se.samples_received += n;
                se.auto_fitted         = (se.samples_received >= auto_fit_samples_);
                slot_received_new_data = true;
            }
        }

        // -- Drain extra series --
        for (auto& es : se.extra_series)
        {
            // Direct-write mode for extra series.
            if (es->direct_ctx)
            {
                if (es->series)
                    es->series->commit_pending();

                es->samples_received =
                    es->direct_ctx->samples_written.load(std::memory_order_relaxed);
                es->auto_fitted = (es->samples_received >= auto_fit_samples_);
                if (es->samples_received > 0)
                    slot_received_new_data = true;
                continue;
            }

            if (!es->subscriber || !es->subscriber->is_running() || es->extractor_id < 0)
                continue;

            if (es->drain_buf.capacity() < MAX_DRAIN_PER_POLL)
                es->drain_buf.reserve(MAX_DRAIN_PER_POLL);

            const size_t n = es->subscriber->pop_bulk(es->extractor_id,
                                                      es->drain_buf.data(),
                                                      MAX_DRAIN_PER_POLL);

            if (n > 0)
            {
                const double origin = shared_time_origin_;
                for (size_t i = 0; i < n; ++i)
                {
                    const FieldSample& s     = es->drain_buf[i];
                    const double       t_sec = static_cast<double>(s.timestamp_ns) * 1e-9;
                    const double       t_rel = t_sec - origin;
                    es->series->append(static_cast<float>(t_rel), static_cast<float>(s.value));

                    if (on_data_cb_)
                        on_data_cb_(se.slot, t_sec, s.value);
                }

                es->samples_received += n;
                es->auto_fitted        = (es->samples_received >= auto_fit_samples_);
                slot_received_new_data = true;
            }
        }

        bool ready_for_live_autofit = (se.samples_received >= auto_fit_samples_);
        if (!ready_for_live_autofit)
        {
            for (const auto& es : se.extra_series)
            {
                if (es->samples_received >= auto_fit_samples_)
                {
                    ready_for_live_autofit = true;
                    break;
                }
            }
        }

        if (slot_received_new_data && ready_for_live_autofit && !se.manual_ylim.has_value())
            se.auto_fitted = true;

        // Apply manual Y limits if set.
        if (se.manual_ylim.has_value())
            se.axes->ylim(se.manual_ylim->min, se.manual_ylim->max);

        // Custom-axis slots retain conservative FIFO caps because arbitrary
        // x-values are not guaranteed to be safe for time-window pruning.
        if (se.axis_mode == AxisMode::CustomAxes)
        {
            if (se.series)
                se.series->trim_to_max_points(CUSTOM_AXES_MAX_POINTS);
            for (auto& es : se.extra_series)
            {
                if (es->series)
                    es->series->trim_to_max_points(CUSTOM_AXES_MAX_POINTS);
            }
        }
        else if (pruning_enabled_ && se.axis_mode == AxisMode::TimeSeries && se.axes)
        {
            if (se.series)
                prune_time_series(*se.series, *se.axes, prune_buffer_s_, true);
            for (auto& es : se.extra_series)
            {
                if (es->series)
                    prune_time_series(*es->series, *se.axes, prune_buffer_s_, true);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Shared cursor
// ---------------------------------------------------------------------------

void SubplotManager::notify_cursor(spectra::Axes* source_axes,
                                   float          data_x,
                                   float          data_y,
                                   double         screen_x,
                                   double         screen_y)
{
    if (!source_axes)
    {
        clear_cursor();
        return;
    }

    spectra::SharedCursor cur;
    cur.valid       = true;
    cur.data_x      = data_x;
    cur.data_y      = data_y;
    cur.screen_x    = screen_x;
    cur.screen_y    = screen_y;
    cur.source_axes = source_axes;
    link_manager_->update_shared_cursor(cur);
}

void SubplotManager::clear_cursor()
{
    link_manager_->clear_shared_cursor();
}

// ---------------------------------------------------------------------------
// Auto-scroll
// ---------------------------------------------------------------------------

void SubplotManager::set_time_window(double seconds)
{
    scroll_window_s_ = seconds;
    for (auto& se : slots_)
        apply_slot_presented_buffer(se);
}

void SubplotManager::set_prune_buffer(double seconds)
{
    prune_buffer_s_ = std::max(0.0, seconds);
}

void SubplotManager::set_now(double wall_time_s)
{
    explicit_now_s_ = wall_time_s;

    if (!has_shared_origin_)
        set_shared_time_origin(wall_time_s);

    const double now_rel = wall_time_s - shared_time_origin_;
    for (auto& se : slots_)
    {
        if (se.axes && se.axis_mode == AxisMode::TimeSeries)
            se.axes->set_presented_buffer_right_edge(now_rel);
    }
}

void SubplotManager::pause_scroll(int slot)
{
    if (slot < 1 || slot > capacity())
        return;
    auto& se = slots_[static_cast<size_t>(slot - 1)];
    if (se.axes && se.axis_mode == AxisMode::TimeSeries)
    {
        auto lim = se.axes->x_limits();
        se.axes->xlim(lim.min, lim.max);
    }
}

void SubplotManager::resume_scroll(int slot)
{
    if (slot < 1 || slot > capacity())
        return;
    auto& se = slots_[static_cast<size_t>(slot - 1)];
    if (se.axes && se.axis_mode == AxisMode::TimeSeries)
        se.axes->resume_follow();
}

bool SubplotManager::is_scroll_paused(int slot) const
{
    if (slot < 1 || slot > capacity())
        return false;
    const auto& se = slots_[static_cast<size_t>(slot - 1)];
    return se.axes ? !se.axes->is_presented_buffer_following() : false;
}

void SubplotManager::pause_all_scroll()
{
    for (auto& se : slots_)
    {
        if (se.axes && se.axis_mode == AxisMode::TimeSeries)
        {
            auto lim = se.axes->x_limits();
            se.axes->xlim(lim.min, lim.max);
        }
    }
}

void SubplotManager::resume_all_scroll()
{
    for (auto& se : slots_)
    {
        if (se.axes && se.axis_mode == AxisMode::TimeSeries)
            se.axes->resume_follow();
    }
}

size_t SubplotManager::total_memory_bytes() const
{
    size_t total = 0;
    for (const auto& se : slots_)
    {
        if (se.series)
            total += se.series->memory_bytes();
        for (const auto& es : se.extra_series)
        {
            if (es->series)
                total += es->series->memory_bytes();
        }
    }
    return total;
}

size_t SubplotManager::total_point_count() const
{
    size_t total = 0;
    for (const auto& se : slots_)
    {
        if (se.series)
            total += se.series->point_count();
        for (const auto& es : se.extra_series)
        {
            if (es->series)
                total += es->series->point_count();
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Per-slot time-window override
// ---------------------------------------------------------------------------

void SubplotManager::set_slot_time_window(int slot, double seconds)
{
    SlotEntry* se = slot_entry(slot);
    if (!se)
        return;
    if (seconds > 0.0)
    {
        se->time_window_override_s = seconds;
    }
    else
    {
        se->time_window_override_s = -1.0;
    }
    apply_slot_presented_buffer(*se);
}

double SubplotManager::slot_time_window(int slot) const
{
    const SlotEntry* se = slot_entry(slot);
    if (!se)
        return scroll_window_s_;
    return (se->time_window_override_s > 0.0) ? se->time_window_override_s : scroll_window_s_;
}

void SubplotManager::clear_slot_time_window(int slot)
{
    set_slot_time_window(slot, -1.0);
}

// ---------------------------------------------------------------------------
// Subplot context menu
// ---------------------------------------------------------------------------

SubplotManager::SubplotAction SubplotManager::draw_slot_context_menu(int slot, const char* popup_id)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::BeginPopupContextItem(popup_id))
        return SubplotAction::None;

    SubplotAction action = SubplotAction::None;

    const bool        has         = has_plot(slot);
    const std::string topic_label = has ? slots_[static_cast<size_t>(slot - 1)].topic : "";

    // Header.
    if (has)
        ImGui::TextDisabled("Slot %d: %s", slot, topic_label.c_str());
    else
        ImGui::TextDisabled("Slot %d (empty)", slot);
    ImGui::Separator();

    if (ImGui::MenuItem("Clear", nullptr, false, has))
        action = SubplotAction::Clear;

    if (ImGui::MenuItem("Duplicate to next slot", nullptr, false, has))
        action = SubplotAction::Duplicate;

    if (ImGui::MenuItem("Detach to new window", nullptr, false, has))
        action = SubplotAction::Detach;

    ImGui::Separator();

    // Per-slot time-window submenu.
    if (ImGui::BeginMenu("Time window", has))
    {
        const double current_win = slot_time_window(slot);
        const bool   has_override =
            (slot < 1 || slot > capacity())
                  ? false
                  : (slots_[static_cast<size_t>(slot - 1)].time_window_override_s > 0.0);

        static const double kPresets[] = {5.0, 10.0, 30.0, 60.0, 300.0, 600.0};
        static const char*  kLabels[]  = {"5 s", "10 s", "30 s", "1 min", "5 min", "10 min"};
        for (int i = 0; i < 6; ++i)
        {
            const bool sel = std::abs(current_win - kPresets[i]) < 0.1;
            if (ImGui::MenuItem(kLabels[i], nullptr, sel))
                set_slot_time_window(slot, kPresets[i]);
        }
        ImGui::Separator();
        if (has_override && ImGui::MenuItem("Reset to global"))
            clear_slot_time_window(slot);

        ImGui::EndMenu();
    }

    ImGui::EndPopup();
    return action;
#else
    (void)slot;
    (void)popup_id;
    return SubplotAction::None;
#endif
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void SubplotManager::set_figure_size(uint32_t w, uint32_t h)
{
    if (figure_)
        figure_->set_size(w, h);
}

// ---------------------------------------------------------------------------
// Dynamic grid management
// ---------------------------------------------------------------------------

int SubplotManager::add_row()
{
    ++rows_;
    const int new_n = rows_ * cols_;

    // Create new subplot axes in the figure for each cell in the new row.
    for (int c = 1; c <= cols_; ++c)
    {
        const int idx = (rows_ - 1) * cols_ + c;
        figure_->subplot(rows_, cols_, idx);
    }

    // Re-register existing axes with the new grid dimensions.
    for (int i = 0; i < new_n - cols_; ++i)
    {
        figure_->subplot(rows_, cols_, i + 1);
    }

    // Add SlotEntries for the new row.
    const int old_n = static_cast<int>(slots_.size());
    for (int i = old_n; i < new_n; ++i)
    {
        slots_.emplace_back();
        slots_.back().slot = i + 1;
        slots_.back().axes = figure_->axes()[static_cast<size_t>(i)].get();
        slots_.back().axes->presented_buffer(static_cast<float>(scroll_window_s_));
    }

    return (rows_ - 1) * cols_ + 1;   // first slot in new row
}

bool SubplotManager::remove_last_row()
{
    if (rows_ <= 1)
        return false;

    // Remove plots from the last row's slots.
    const int first_slot = (rows_ - 1) * cols_ + 1;
    for (int s = first_slot; s <= rows_ * cols_; ++s)
        remove_plot(s);

    // Remove the slot entries.
    const int new_n = (rows_ - 1) * cols_;
    slots_.resize(static_cast<size_t>(new_n));

    // Remove the axes from the figure.
    auto& axes_vec = figure_->axes_mut();
    while (static_cast<int>(axes_vec.size()) > new_n)
        axes_vec.pop_back();
    auto& all_axes = figure_->all_axes_mut();
    while (static_cast<int>(all_axes.size()) > new_n)
        all_axes.pop_back();

    --rows_;

    // Re-register all axes with updated grid and shrink the grid dimensions
    // so compute_layout() redistributes axes to fill available space.
    figure_->set_grid(rows_, cols_);
    for (int i = 0; i < new_n; ++i)
        figure_->subplot(rows_, cols_, i + 1);

    rebuild_x_links();
    return true;
}

void SubplotManager::compact()
{
    // Collect active slot data in order.
    struct ActiveSlot
    {
        std::string                               topic;
        std::string                               field_path;
        std::string                               type_name;
        std::vector<float>                        x_data;
        std::vector<float>                        y_data;
        std::unique_ptr<GenericSubscriber>        subscriber;
        int                                       extractor_id{};
        int                                       x_extractor_id{};
        int                                       y_extractor_id{};
        size_t                                    samples_received{};
        bool                                      auto_fitted{};
        size_t                                    color_index{};
        std::vector<FieldSample>                  drain_buf;
        std::vector<FieldSample>                  x_drain_buf;
        std::vector<FieldSample>                  y_drain_buf;
        double                                    time_window_override_s{};
        std::optional<spectra::AxisLimits>        manual_ylim;
        std::vector<std::unique_ptr<SeriesEntry>> extra_series;
        std::unique_ptr<DirectWriteContext>       direct_ctx;
        AxisMode                                  axis_mode{};
        std::string                               x_field_path;
        std::string                               y_field_path;
        size_t                                    buffer_depth{};
        // Saved x/y data for each extra series (parallel to extra_series).
        std::vector<std::pair<std::vector<float>, std::vector<float>>> extra_xy;
    };

    std::vector<ActiveSlot> actives;
    for (auto& se : slots_)
    {
        if (se.topic.empty() && se.extra_series.empty())
            continue;
        ActiveSlot a;
        a.topic      = std::move(se.topic);
        a.field_path = std::move(se.field_path);
        a.type_name  = std::move(se.type_name);
        if (se.series)
        {
            a.x_data = std::vector<float>(se.series->x_data().begin(), se.series->x_data().end());
            a.y_data = std::vector<float>(se.series->y_data().begin(), se.series->y_data().end());
        }
        a.subscriber             = std::move(se.subscriber);
        a.extractor_id           = se.extractor_id;
        a.x_extractor_id         = se.x_extractor_id;
        a.y_extractor_id         = se.y_extractor_id;
        a.samples_received       = se.samples_received;
        a.auto_fitted            = se.auto_fitted;
        a.color_index            = se.color_index;
        a.drain_buf              = std::move(se.drain_buf);
        a.x_drain_buf            = std::move(se.x_drain_buf);
        a.y_drain_buf            = std::move(se.y_drain_buf);
        a.time_window_override_s = se.time_window_override_s;
        a.manual_ylim            = se.manual_ylim;
        a.direct_ctx             = std::move(se.direct_ctx);
        a.axis_mode              = se.axis_mode;
        a.x_field_path           = se.x_field_path;
        a.y_field_path           = se.y_field_path;
        a.buffer_depth           = se.buffer_depth;
        a.extra_series           = std::move(se.extra_series);
        for (auto& es : a.extra_series)
        {
            if (es->series)
            {
                a.extra_xy.emplace_back(
                    std::vector<float>(es->series->x_data().begin(), es->series->x_data().end()),
                    std::vector<float>(es->series->y_data().begin(), es->series->y_data().end()));
            }
            else
            {
                a.extra_xy.emplace_back();
            }
        }

        // Clear source slot.
        se.topic.clear();
        se.field_path.clear();
        se.type_name.clear();
        se.series           = nullptr;
        se.extractor_id     = -1;
        se.x_extractor_id   = -1;
        se.y_extractor_id   = -1;
        se.samples_received = 0;
        se.auto_fitted      = false;
        se.drain_buf.clear();
        se.x_drain_buf.clear();
        se.y_drain_buf.clear();
        se.manual_ylim.reset();
        se.direct_ctx.reset();
        se.extra_series.clear();
        reset_slot_axis_config(se);
        if (se.axes)
            se.axes->clear_series();
        actives.push_back(std::move(a));
    }

    if (actives.empty())
    {
        // All empty — shrink to 1 row.
        while (rows_ > 1)
            remove_last_row();
        return;
    }

    // Determine how many rows we need.
    int needed_rows = (static_cast<int>(actives.size()) + cols_ - 1) / cols_;
    if (needed_rows < 1)
        needed_rows = 1;

    // Shrink to needed rows (remove from bottom).
    while (rows_ > needed_rows && rows_ > 1)
        remove_last_row();

    // Re-place active data into consecutive slots.
    for (size_t i = 0; i < actives.size(); ++i)
    {
        const int slot_idx = static_cast<int>(i);
        if (slot_idx >= static_cast<int>(slots_.size()))
            break;
        auto& se = slots_[static_cast<size_t>(slot_idx)];
        auto& a  = actives[i];

        // Move series data from old axes to new axes (re-add to the axes).
        se.topic                  = std::move(a.topic);
        se.field_path             = std::move(a.field_path);
        se.type_name              = std::move(a.type_name);
        se.subscriber             = std::move(a.subscriber);
        se.extractor_id           = a.extractor_id;
        se.x_extractor_id         = a.x_extractor_id;
        se.y_extractor_id         = a.y_extractor_id;
        se.samples_received       = a.samples_received;
        se.auto_fitted            = a.auto_fitted;
        se.color_index            = a.color_index;
        se.drain_buf              = std::move(a.drain_buf);
        se.x_drain_buf            = std::move(a.x_drain_buf);
        se.y_drain_buf            = std::move(a.y_drain_buf);
        se.time_window_override_s = a.time_window_override_s;
        se.manual_ylim            = a.manual_ylim;
        se.direct_ctx             = std::move(a.direct_ctx);
        se.axis_mode              = a.axis_mode;
        se.x_field_path           = std::move(a.x_field_path);
        se.y_field_path           = std::move(a.y_field_path);
        se.buffer_depth           = a.buffer_depth;

        // Create new primary LineSeries in the target axes.
        std::string    lbl = se.topic + "/" + se.field_path;
        spectra::Color color =
            spectra::palette::default_cycle[se.color_index % spectra::palette::default_cycle_size];
        spectra::LineSeries& ls = se.axes->line();
        ls.label(lbl);
        ls.color(color);
        ls.width(3.0f);

        // Copy saved data into the new series.
        if (!a.x_data.empty())
        {
            ls.set_x(std::move(a.x_data));
            ls.set_y(std::move(a.y_data));
        }
        se.series = &ls;

        // Move extra series.
        se.extra_series = std::move(a.extra_series);
        for (size_t ei = 0; ei < se.extra_series.size(); ++ei)
        {
            auto&          es = se.extra_series[ei];
            spectra::Color ecol =
                spectra::palette::default_cycle[es->color_index
                                                % spectra::palette::default_cycle_size];
            spectra::LineSeries& els = se.axes->line();
            els.label(es->topic + "/" + es->field_path);
            els.color(ecol);
            els.width(3.0f);
            if (ei < a.extra_xy.size() && !a.extra_xy[ei].first.empty())
            {
                els.set_x(std::move(a.extra_xy[ei].first));
                els.set_y(std::move(a.extra_xy[ei].second));
            }
            es->series = &els;
        }

        apply_slot_presented_buffer(se);
        update_slot_labels(se);
    }

    rebuild_x_links();
}

// ---------------------------------------------------------------------------
// Shared time origin
// ---------------------------------------------------------------------------

void SubplotManager::set_shared_time_origin(double epoch_seconds)
{
    shared_time_origin_ = epoch_seconds;
    has_shared_origin_  = true;
}

// ---------------------------------------------------------------------------
// Y-axis limit controls
// ---------------------------------------------------------------------------

void SubplotManager::set_slot_ylim(int slot, double ymin, double ymax)
{
    SlotEntry* se = slot_entry(slot);
    if (!se)
        return;
    se->manual_ylim = spectra::AxisLimits{ymin, ymax};
    se->axes->ylim(ymin, ymax);
}

void SubplotManager::clear_slot_ylim(int slot)
{
    SlotEntry* se = slot_entry(slot);
    if (!se)
        return;
    se->manual_ylim.reset();
    if (se->axes)
        se->axes->clear_ylim();
}

void SubplotManager::auto_fit_slot_y(int slot)
{
    SlotEntry* se = slot_entry(slot);
    if (!se)
        return;
    se->manual_ylim.reset();
    if (se->axes)
        se->axes->clear_ylim();
}

void SubplotManager::clear_slot_data(int slot)
{
    SlotEntry* se = slot_entry(slot);
    if (!se)
        return;

    // Clear data from primary series.
    if (se->series)
    {
        se->series->set_x(std::span<const float>{});
        se->series->set_y(std::span<const float>{});
    }

    // Clear data from extra series.
    for (auto& es : se->extra_series)
    {
        if (es->series)
        {
            es->series->set_x(std::span<const float>{});
            es->series->set_y(std::span<const float>{});
        }
    }

    se->samples_received = 0;
    se->auto_fitted      = false;
}

// ---------------------------------------------------------------------------
// Slot axis configuration helpers
// ---------------------------------------------------------------------------

std::vector<std::string> SubplotManager::numeric_fields_for_type(const std::string& type_name) const
{
    if (type_name.empty())
        return {};
    auto schema = intr_.introspect(type_name);
    if (!schema)
        return {};
    return schema->numeric_paths();
}

bool SubplotManager::validate_axis_config(const SlotEntry&   se,
                                          AxisMode           mode,
                                          const std::string& resolved_type,
                                          const std::string& x_field_path,
                                          const std::string& y_field_path,
                                          std::string*       error_out) const
{
    if (!se.series || se.topic.empty())
    {
        if (error_out)
            *error_out = "Select a live plot first.";
        return false;
    }
    if (mode == AxisMode::CustomAxes && se.series_count() != 1)
    {
        if (error_out)
            *error_out = "Custom axes are available only for slots with a single live series from "
                         "one topic.";
        return false;
    }

    const auto numeric = numeric_fields_for_type(resolved_type);
    if (numeric.empty())
    {
        if (error_out)
            *error_out = "No numeric fields are available for this topic type.";
        return false;
    }

    const auto is_numeric_path = [&](const std::string& path)
    { return std::find(numeric.begin(), numeric.end(), path) != numeric.end(); };

    if (y_field_path.empty() || !is_numeric_path(y_field_path))
    {
        if (error_out)
            *error_out = "Y source must be a numeric field.";
        return false;
    }

    if (mode == AxisMode::TimeSeries)
    {
        if (error_out)
            error_out->clear();
        return true;
    }

    if (x_field_path != AXIS_SOURCE_TIME && !is_numeric_path(x_field_path))
    {
        if (error_out)
            *error_out = "X source must be time or a numeric field.";
        return false;
    }

    if (error_out)
        error_out->clear();
    return true;
}

bool SubplotManager::rebuild_primary_slot_subscription(SlotEntry&   se,
                                                       size_t       buffer_depth,
                                                       std::string* error_out)
{
    std::unique_ptr<GenericSubscriber>  new_subscriber;
    std::unique_ptr<DirectWriteContext> new_direct_ctx;
    int                                 new_x_extractor_id = -1;
    int                                 new_y_extractor_id = -1;

    if (bridge_.is_ok())
    {
        auto sub = std::make_unique<GenericSubscriber>(bridge_.node(),
                                                       se.topic,
                                                       se.type_name,
                                                       intr_,
                                                       buffer_depth);

        if (se.axis_mode == AxisMode::TimeSeries)
        {
            new_y_extractor_id = sub->add_field(se.y_field_path);
            if (new_y_extractor_id < 0)
            {
                if (error_out)
                    *error_out = "The selected Y field could not be subscribed.";
                return false;
            }

            se.series->set_thread_safe(true);
            new_direct_ctx = std::make_unique<DirectWriteContext>();

            auto* ctx     = new_direct_ctx.get();
            auto* origin  = &shared_time_origin_;
            auto* has_o   = &has_shared_origin_;
            auto* series  = se.series;
            auto* data_cb = &on_data_cb_;
            int   slot_id = se.slot;
            sub->set_field_callback(
                new_y_extractor_id,
                [ctx, origin, has_o, series, data_cb, slot_id](double t_sec, double val)
                {
                    if (!ctx->active.load(std::memory_order_acquire))
                        return;
                    if (!*has_o)
                    {
                        *origin = t_sec;
                        *has_o  = true;
                    }
                    const double t_rel = t_sec - *origin;
                    series->append(static_cast<float>(t_rel), static_cast<float>(val));
                    ctx->samples_written.fetch_add(1, std::memory_order_relaxed);

                    if (*data_cb)
                        (*data_cb)(slot_id, t_sec, val);
                });
        }
        else
        {
            new_y_extractor_id = sub->add_field(se.y_field_path);
            if (new_y_extractor_id < 0)
            {
                if (error_out)
                    *error_out = "The selected Y field could not be subscribed.";
                return false;
            }
            if (se.x_field_path != AXIS_SOURCE_TIME)
            {
                new_x_extractor_id = sub->add_field(se.x_field_path);
                if (new_x_extractor_id < 0)
                {
                    if (error_out)
                        *error_out = "The selected X field could not be subscribed.";
                    return false;
                }
            }

            se.series->set_thread_safe(false);
        }

        if (!sub->start())
        {
            if (error_out)
                *error_out = "Failed to rebuild the live subscription for this slot.";
            return false;
        }

        new_subscriber = std::move(sub);
    }
    else
    {
        se.series->set_thread_safe(false);
    }

    if (se.subscriber)
        se.subscriber->stop();

    se.subscriber     = std::move(new_subscriber);
    se.direct_ctx     = std::move(new_direct_ctx);
    se.extractor_id   = new_y_extractor_id;
    se.x_extractor_id = new_x_extractor_id;
    se.y_extractor_id = new_y_extractor_id;

    if (se.drain_buf.capacity() < MAX_DRAIN_PER_POLL)
        se.drain_buf.resize(MAX_DRAIN_PER_POLL);
    if (se.x_drain_buf.capacity() < MAX_DRAIN_PER_POLL)
        se.x_drain_buf.resize(MAX_DRAIN_PER_POLL);
    if (se.y_drain_buf.capacity() < MAX_DRAIN_PER_POLL)
        se.y_drain_buf.resize(MAX_DRAIN_PER_POLL);

    return true;
}

void SubplotManager::apply_slot_presented_buffer(SlotEntry& se)
{
    if (!se.axes)
        return;

    if (se.axis_mode == AxisMode::CustomAxes)
    {
        se.axes->presented_buffer(0.0f);
        return;
    }

    const double seconds =
        (se.time_window_override_s > 0.0) ? se.time_window_override_s : scroll_window_s_;
    se.axes->presented_buffer(static_cast<float>(seconds));
}

void SubplotManager::reset_slot_axis_config(SlotEntry& se)
{
    se.axis_mode    = AxisMode::TimeSeries;
    se.x_field_path = AXIS_SOURCE_TIME;
    se.y_field_path.clear();
    se.buffer_depth = 10000;
}

void SubplotManager::update_slot_labels(SlotEntry& se)
{
    if (!se.axes)
        return;

    if (se.series_count() == 0)
    {
        se.axes->xlabel("time (s)");
        se.axes->ylabel("");
        return;
    }

    if (se.axis_mode == AxisMode::CustomAxes && se.series_count() == 1 && se.series)
    {
        const std::string x_label =
            (se.x_field_path == AXIS_SOURCE_TIME) ? "time (s)" : se.x_field_path;
        se.axes->xlabel(x_label);
        se.axes->ylabel(se.y_field_path);
        se.series->label(se.topic + ": " + se.y_field_path + " vs " + x_label);
        return;
    }

    se.axes->xlabel("time (s)");
    if (se.series_count() <= 1)
    {
        std::string lbl = se.topic + "/" + se.field_path;
        se.axes->ylabel(lbl);
        if (se.series)
            se.series->label(lbl);
        return;
    }

    // Multiple series: use short field names.
    std::string lbl;
    if (!se.topic.empty())
    {
        lbl = se.field_path;
    }
    for (const auto& es : se.extra_series)
    {
        if (!lbl.empty())
            lbl += ", ";
        lbl += es->field_path;
    }
    se.axes->ylabel(lbl);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

SubplotManager::SlotEntry* SubplotManager::slot_entry(int slot)
{
    if (slot < 1 || slot > capacity())
        return nullptr;
    return &slots_[static_cast<size_t>(slot - 1)];
}

const SubplotManager::SlotEntry* SubplotManager::slot_entry(int slot) const
{
    if (slot < 1 || slot > capacity())
        return nullptr;
    return &slots_[static_cast<size_t>(slot - 1)];
}

std::string SubplotManager::detect_type(const std::string& topic) const
{
    // Use the TopicDiscovery cache — it queries DDS on a dedicated background
    // thread, avoiding the ABBA deadlock between the executor's wait-set and
    // FastDDS's participant lock that occurs with rmw_fastrtps.
    if (discovery_)
    {
        if (discovery_->has_topic(topic))
        {
            TopicInfo ti = discovery_->topic(topic);
            if (!ti.types.empty())
                return ti.types.front();
        }
        return {};   // topic not yet discovered; will succeed on retry
    }

    // Do NOT fall back to node_->get_topic_names_and_types() — that DDS
    // graph call can deadlock with rmw_fastrtps's discovery thread when
    // namespaced participants are present.  Return empty and let the caller
    // retry after TopicDiscovery has refreshed.
    return {};
}

spectra::Color SubplotManager::next_color()
{
    const spectra::Color c =
        spectra::palette::default_cycle[color_cursor_ % spectra::palette::default_cycle_size];
    ++color_cursor_;
    return c;
}

void SubplotManager::rebuild_x_links()
{
    // Collect all active axes pointers.
    std::vector<spectra::Axes*> active_axes;
    // Also collect inactive (but non-null) axes to ensure stale groups are cleaned.
    std::vector<spectra::Axes*> all_axes;
    for (const auto& se : slots_)
    {
        if (se.axes)
        {
            all_axes.push_back(se.axes);
            if (se.series_count() > 0 && se.axis_mode == AxisMode::TimeSeries)
                active_axes.push_back(se.axes);
        }
    }

    // Always unlink ALL axes first to remove stale groups.
    for (auto* ax : all_axes)
        link_manager_->unlink(ax);

    if (!x_links_enabled_ || active_axes.size() < 2)
        return;

    // Link all active axes together on X.
    spectra::Axes* leader = active_axes[0];
    for (size_t i = 1; i < active_axes.size(); ++i)
        link_manager_->link(leader, active_axes[i], spectra::LinkAxis::X);
}

void SubplotManager::set_x_links_enabled(bool enabled)
{
    if (x_links_enabled_ == enabled)
        return;
    x_links_enabled_ = enabled;
    rebuild_x_links();
}

}   // namespace spectra::adapters::ros2
