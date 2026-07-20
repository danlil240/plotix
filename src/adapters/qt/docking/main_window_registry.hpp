#pragma once

// MainWindowRegistry — tracks all SpectraMainWindow instances and coordinates
// cross-window operations (detach, move, close).
//
// Phase 4 component: enables multi-window support.  Each main window is
// wrapped in a NativeQtDockingHost.  The registry owns detached windows
// and routes cross-window document operations.

#include "docking_host.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace spectra
{
class FigureRegistry;
class ApplicationServices;
}

namespace spectra::adapters::qt
{

class SpectraMainWindow;
class QtRuntime;
class QtActionBridge;
class NativeQtDockingHost;

class MainWindowRegistry
{
   public:
    MainWindowRegistry(QtRuntime*         runtime,
                       FigureRegistry*    registry,
                       QtActionBridge*    action_bridge,
                       ApplicationServices* services);
    ~MainWindowRegistry();

    MainWindowRegistry(const MainWindowRegistry&)            = delete;
    MainWindowRegistry& operator=(const MainWindowRegistry&) = delete;

    // Register an existing SpectraMainWindow as a new host.
    // Returns the HostId.  The registry does NOT take ownership of the window.
    HostId register_window(SpectraMainWindow* window);

    // Create a new detached main window.  The registry owns it.
    // Returns the HostId, or INVALID_HOST_ID on failure.
    HostId create_detached_window();

    // Close and destroy a detached window (owned by the registry).
    // Returns true if the window was found and closed.
    bool close_detached_window(HostId id);

    // Get the DockingHost for a given HostId.
    DockingHost* host(HostId id) const;

    // Get the NativeQtDockingHost for a given HostId.
    NativeQtDockingHost* native_host(HostId id) const;

    // Find the host that contains a given figure document.
    HostId find_host_for_figure(FigureId fid) const;

    // Get all registered hosts.
    std::vector<HostId> all_hosts() const;

    // Number of registered windows.
    size_t window_count() const { return hosts_.size(); }

    // Close all detached windows (called during shutdown).
    void close_all_detached();

   private:
    QtRuntime*           runtime_       = nullptr;
    FigureRegistry*      registry_      = nullptr;
    QtActionBridge*      action_bridge_ = nullptr;
    ApplicationServices* services_      = nullptr;

    HostId next_host_id_ = 1;

    struct HostEntry
    {
        HostId                                       id = INVALID_HOST_ID;
        std::unique_ptr<NativeQtDockingHost>         host;
        std::unique_ptr<SpectraMainWindow>           window;  // non-null for detached windows
        bool                                         owned = false;  // true for detached windows
    };
    std::unordered_map<HostId, HostEntry> hosts_;
};

}   // namespace spectra::adapters::qt
