#pragma once

#include "spectra_vulkan_window.hpp"

#include "ui/commands/undo_manager.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace spectra
{
class AxesBase;
class Series;
class SeriesClipboard;
}   // namespace spectra

namespace spectra::adapters::qt
{

using SeriesRemovedCallback    = std::function<void(const Series*)>;
using SeriesChangedCallback    = std::function<void()>;
using SeriesOwnerAliveCallback = std::function<bool(const AxesBase*)>;

bool copy_selected_series(const std::vector<SpectraVulkanWindow::SeriesSelection>& selection,
                          SeriesClipboard&                                         clipboard,
                          bool                                                     cut);

// Performs the initial cut/delete and returns the matching undo/redo action.
std::optional<UndoAction> remove_selected_series(
    const std::vector<SpectraVulkanWindow::SeriesSelection>& selection,
    SeriesClipboard*                                         clipboard,
    bool                                                     cut,
    SeriesRemovedCallback                                    on_removed,
    SeriesChangedCallback                                    on_changed     = {},
    SeriesOwnerAliveCallback                                 owner_is_alive = {});

// Performs the initial paste and returns the matching undo/redo action.
std::optional<UndoAction> paste_series(AxesBase&                target,
                                       SeriesClipboard&         clipboard,
                                       SeriesRemovedCallback    on_removed,
                                       SeriesChangedCallback    on_changed     = {},
                                       SeriesOwnerAliveCallback owner_is_alive = {});

}   // namespace spectra::adapters::qt
