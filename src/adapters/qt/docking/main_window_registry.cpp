// main_window_registry.cpp — Multi-window registry implementation.

#include "main_window_registry.hpp"

#include "native_qt_docking_host.hpp"

#include "../qt_main_window.hpp"
#include "../qt_runtime.hpp"
#include "../qt_action_bridge.hpp"

#include "app/application_services.hpp"

#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

namespace spectra::adapters::qt
{

MainWindowRegistry::MainWindowRegistry(QtRuntime*           runtime,
                                       FigureRegistry*      registry,
                                       QtActionBridge*      action_bridge,
                                       ApplicationServices* services)
    : runtime_(runtime),
      registry_(registry),
      action_bridge_(action_bridge),
      services_(services)
{
}

MainWindowRegistry::~MainWindowRegistry()
{
    close_all_detached();
}

HostId MainWindowRegistry::register_window(SpectraMainWindow* window)
{
    if (!window)
        return INVALID_HOST_ID;

    HostId id = next_host_id_++;
    auto host = std::make_unique<NativeQtDockingHost>(id, window, this);

    // Set detach callback so this host can create new windows
    host->set_detach_callback(
        [this](NativeQtDockingHost* /*source*/, DocumentId doc_id) -> HostId {
            HostId new_id = create_detached_window();
            if (new_id != INVALID_HOST_ID)
            {
                auto* new_host = native_host(new_id);
                if (new_host)
                    new_host->add_figure_tab(static_cast<FigureId>(doc_id));
            }
            return new_id;
        });

    HostEntry entry;
    entry.id    = id;
    entry.host  = std::move(host);
    entry.owned = false;
    hosts_[id]  = std::move(entry);

    SPECTRA_LOG_INFO("main_window_registry",
                     "Registered window host_id=" + std::to_string(id));
    return id;
}

HostId MainWindowRegistry::create_detached_window()
{
    if (!runtime_ || !registry_)
        return INVALID_HOST_ID;

    HostId id = next_host_id_++;

    // Create a new SpectraMainWindow
    auto window = std::make_unique<SpectraMainWindow>(
        runtime_, registry_, action_bridge_, services_);

    auto host = std::make_unique<NativeQtDockingHost>(id, window.get(), this);

    // Set detach callback for the new host too
    host->set_detach_callback(
        [this](NativeQtDockingHost* /*source*/, DocumentId doc_id) -> HostId {
            HostId new_id = create_detached_window();
            if (new_id != INVALID_HOST_ID)
            {
                auto* new_host = native_host(new_id);
                if (new_host)
                    new_host->add_figure_tab(static_cast<FigureId>(doc_id));
            }
            return new_id;
        });

    // Show the new window
    window->show();
    window->setWindowTitle(QString("Spectra — Figure"));
    window->resize(800, 600);

    HostEntry entry;
    entry.id    = id;
    entry.host  = std::move(host);
    entry.window = std::move(window);
    entry.owned = true;
    hosts_[id]  = std::move(entry);

    SPECTRA_LOG_INFO("main_window_registry",
                     "Created detached window host_id=" + std::to_string(id));
    return id;
}

bool MainWindowRegistry::close_detached_window(HostId id)
{
    auto it = hosts_.find(id);
    if (it == hosts_.end() || !it->second.owned)
        return false;

    // Destroy the host first (releases references to the window)
    it->second.host.reset();
    it->second.window.reset();
    hosts_.erase(it);

    SPECTRA_LOG_INFO("main_window_registry",
                     "Closed detached window host_id=" + std::to_string(id));
    return true;
}

DockingHost* MainWindowRegistry::host(HostId id) const
{
    auto it = hosts_.find(id);
    return it != hosts_.end() ? it->second.host.get() : nullptr;
}

NativeQtDockingHost* MainWindowRegistry::native_host(HostId id) const
{
    auto it = hosts_.find(id);
    return it != hosts_.end() ? it->second.host.get() : nullptr;
}

HostId MainWindowRegistry::find_host_for_figure(FigureId fid) const
{
    for (const auto& [id, entry] : hosts_)
    {
        if (!entry.host || !entry.host->main_window())
            continue;
        if (entry.host->main_window()->canvas_for(fid))
            return id;
    }
    return INVALID_HOST_ID;
}

std::vector<HostId> MainWindowRegistry::all_hosts() const
{
    std::vector<HostId> result;
    result.reserve(hosts_.size());
    for (const auto& [id, entry] : hosts_)
        result.push_back(id);
    return result;
}

void MainWindowRegistry::close_all_detached()
{
    // Close owned windows in reverse order
    std::vector<HostId> to_close;
    for (const auto& [id, entry] : hosts_)
    {
        if (entry.owned)
            to_close.push_back(id);
    }
    for (auto it = to_close.rbegin(); it != to_close.rend(); ++it)
        close_detached_window(*it);
}

}   // namespace spectra::adapters::qt
