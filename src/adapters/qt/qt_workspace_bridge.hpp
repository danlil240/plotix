#pragma once

// QtWorkspaceBridge — bridges between the Qt docking layout system and the
// framework-neutral WorkspaceData::DesktopLayoutState for workspace save/restore.
//
// Phase 6 component: enables workspace v5 desktop layout persistence.
// Converts DockLayoutState from MainWindowRegistry into DesktopLayoutState
// for serialization, and applies DesktopLayoutState back through the registry.

#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>

namespace spectra
{
struct WorkspaceData;
}

namespace spectra::adapters::qt
{

class MainWindowRegistry;

class QtWorkspaceBridge
{
   public:
    explicit QtWorkspaceBridge(MainWindowRegistry* registry);
    ~QtWorkspaceBridge() = default;

    QtWorkspaceBridge(const QtWorkspaceBridge&)            = delete;
    QtWorkspaceBridge& operator=(const QtWorkspaceBridge&) = delete;

    // Capture the current desktop layout from the MainWindowRegistry into
    // the workspace data's desktop_layout field.
    void capture_layout(WorkspaceData& data) const;

    // Apply a desktop layout from workspace data to the MainWindowRegistry.
    // Returns true if layout was applied (even partially).
    bool apply_layout(const WorkspaceData&                          data,
                      const std::unordered_map<FigureId, FigureId>& id_map = {}) const;

    // Set the provider name (e.g. "native", "kddockwidgets").
    void set_provider(const std::string& name) { provider_ = name; }

   private:
    MainWindowRegistry* registry_ = nullptr;
    std::string         provider_ = "native";
};

}   // namespace spectra::adapters::qt
