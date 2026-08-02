#include "qt_series_commands.hpp"

#include "ui/commands/series_clipboard.hpp"

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <memory>
#include <utility>

namespace spectra::adapters::qt
{
namespace
{

bool remove_series_pointer(AxesBase& owner, const Series* series)
{
    const auto& entries = owner.series();
    for (size_t index = 0; index < entries.size(); ++index)
    {
        if (entries[index].get() == series)
            return owner.remove_series(index);
    }
    return false;
}

void notify_changed(const SeriesChangedCallback& callback)
{
    if (callback)
        callback();
}

bool can_access_owner(const SeriesOwnerAliveCallback& callback, const AxesBase* owner)
{
    return owner && (!callback || callback(owner));
}

}   // namespace

bool copy_selected_series(const std::vector<SpectraVulkanWindow::SeriesSelection>& selection,
                          SeriesClipboard&                                         clipboard,
                          bool                                                     cut)
{
    std::vector<const Series*> selected;
    selected.reserve(selection.size());
    for (const auto& entry : selection)
    {
        if (entry.series)
            selected.push_back(entry.series);
    }
    if (selected.empty())
        return false;

    if (cut)
        clipboard.cut_multi(selected);
    else
        clipboard.copy_multi(selected);
    return true;
}

std::optional<UndoAction> remove_selected_series(
    const std::vector<SpectraVulkanWindow::SeriesSelection>& selection,
    SeriesClipboard*                                         clipboard,
    bool                                                     cut,
    SeriesRemovedCallback                                    on_removed,
    SeriesChangedCallback                                    on_changed,
    SeriesOwnerAliveCallback                                 owner_is_alive)
{
    if (selection.empty())
        return std::nullopt;
    if (cut && (!clipboard || !copy_selected_series(selection, *clipboard, true)))
        return std::nullopt;

    struct RemovedSeries
    {
        AxesBase*      owner = nullptr;
        SeriesSnapshot snapshot;
        Series*        restored = nullptr;
    };
    auto removed = std::make_shared<std::vector<RemovedSeries>>();
    removed->reserve(selection.size());
    for (const auto& entry : selection)
    {
        if (!entry.series || !can_access_owner(owner_is_alive, entry.owner))
            continue;
        RemovedSeries state{entry.owner, SeriesClipboard::snapshot(*entry.series), nullptr};
        if (on_removed)
            on_removed(entry.series);
        if (remove_series_pointer(*entry.owner, entry.series))
            removed->push_back(std::move(state));
    }
    if (removed->empty())
        return std::nullopt;

    notify_changed(on_changed);
    return UndoAction{cut ? "Cut series" : "Delete series",
                      [removed, on_changed, owner_is_alive]()
                      {
                          for (auto& entry : *removed)
                          {
                              if (can_access_owner(owner_is_alive, entry.owner))
                              {
                                  entry.restored =
                                      SeriesClipboard::paste_to(*entry.owner, entry.snapshot);
                              }
                          }
                          notify_changed(on_changed);
                      },
                      [removed, on_removed, on_changed, owner_is_alive]()
                      {
                          for (auto& entry : *removed)
                          {
                              if (!entry.restored || !can_access_owner(owner_is_alive, entry.owner))
                                  continue;
                              if (on_removed)
                                  on_removed(entry.restored);
                              remove_series_pointer(*entry.owner, entry.restored);
                              entry.restored = nullptr;
                          }
                          notify_changed(on_changed);
                      }};
}

std::optional<UndoAction> paste_series(AxesBase&                target,
                                       SeriesClipboard&         clipboard,
                                       SeriesRemovedCallback    on_removed,
                                       SeriesChangedCallback    on_changed,
                                       SeriesOwnerAliveCallback owner_is_alive)
{
    constexpr size_t kMaxSeriesPerAxes = 200;
    if (!clipboard.has_data() || target.series().size() + clipboard.count() > kMaxSeriesPerAxes)
        return std::nullopt;

    struct PasteState
    {
        AxesBase*                   owner = nullptr;
        std::vector<SeriesSnapshot> snapshots;
        std::vector<Series*>        live;
    };
    auto state       = std::make_shared<PasteState>();
    state->owner     = &target;
    state->snapshots = clipboard.peek_all();
    state->live      = clipboard.paste_all(target);
    if (state->live.empty())
        return std::nullopt;

    notify_changed(on_changed);
    return UndoAction{
        "Paste series",
        [state, on_removed, on_changed, owner_is_alive]()
        {
            if (!can_access_owner(owner_is_alive, state->owner))
                return;
            for (Series* series : state->live)
            {
                if (on_removed)
                    on_removed(series);
                remove_series_pointer(*state->owner, series);
            }
            state->live.clear();
            notify_changed(on_changed);
        },
        [state, on_changed, owner_is_alive]()
        {
            if (!can_access_owner(owner_is_alive, state->owner))
                return;
            state->live.clear();
            for (const auto& snapshot : state->snapshots)
            {
                if (Series* series = SeriesClipboard::paste_to(*state->owner, snapshot))
                    state->live.push_back(series);
            }
            notify_changed(on_changed);
        }};
}

}   // namespace spectra::adapters::qt
