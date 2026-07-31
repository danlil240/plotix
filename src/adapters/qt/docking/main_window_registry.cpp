// main_window_registry.cpp — Multi-window registry implementation.

#include "main_window_registry.hpp"

#include "native_qt_docking_host.hpp"

#include "../qt_main_window.hpp"
#include "../qt_runtime.hpp"
#include "../qt_action_bridge.hpp"

#include "app/application_services.hpp"

#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QObject>

namespace spectra::adapters::qt
{

MainWindowRegistry::MainWindowRegistry(QtRuntime*           runtime,
                                       FigureRegistry*      registry,
                                       QtActionBridge*      action_bridge,
                                       ApplicationServices* services)
    : runtime_(runtime), registry_(registry), action_bridge_(action_bridge), services_(services)
{
}

MainWindowRegistry::~MainWindowRegistry()
{
    for (const auto& connection : window_connections_)
        QObject::disconnect(connection);
    window_connections_.clear();
    close_all_detached();
}

HostId MainWindowRegistry::register_window(SpectraMainWindow* window)
{
    if (!window)
        return INVALID_HOST_ID;

    HostId id   = next_host_id_++;
    auto   host = std::make_unique<NativeQtDockingHost>(id, window, this);

    HostEntry entry;
    entry.id    = id;
    entry.host  = std::move(host);
    entry.owned = false;
    hosts_[id]  = std::move(entry);
    wire_window(id, window);

    SPECTRA_LOG_INFO("main_window_registry", "Registered window host_id=" + std::to_string(id));
    return id;
}

HostId MainWindowRegistry::create_detached_window()
{
    if (!runtime_ || !registry_)
        return INVALID_HOST_ID;

    HostId id = next_host_id_++;

    // Create a new SpectraMainWindow
    auto window =
        std::make_unique<SpectraMainWindow>(runtime_, registry_, action_bridge_, services_);

    auto host = std::make_unique<NativeQtDockingHost>(id, window.get(), this);

    // Show the new window
    window->show();
    window->setWindowTitle(QString("Spectra — Figure"));
    window->resize(800, 600);

    HostEntry entry;
    entry.id     = id;
    entry.host   = std::move(host);
    entry.window = std::move(window);
    entry.owned  = true;
    hosts_[id]   = std::move(entry);
    wire_window(id, hosts_[id].window.get());

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

bool MainWindowRegistry::close_document(FigureId fid)
{
    if (fid == INVALID_FIGURE_ID || !registry_)
        return false;

    bool removed_from_window = false;
    for (auto& [id, entry] : hosts_)
    {
        (void)id;
        if (entry.host)
            removed_from_window = entry.host->release_figure_tab(fid) || removed_from_window;
    }

    if (!registry_->get(fid))
    {
        if (removed_from_window && document_changed_callback_)
            document_changed_callback_();
        return removed_from_window;
    }

    registry_->unregister_figure(fid);
    if (document_changed_callback_)
        document_changed_callback_();
    SPECTRA_LOG_INFO("main_window_registry", "Closed document figure_id=" + std::to_string(fid));
    return true;
}

bool MainWindowRegistry::move_document(FigureId fid, HostId source_host, HostId target_host)
{
    if (fid == INVALID_FIGURE_ID || source_host == INVALID_HOST_ID || target_host == INVALID_HOST_ID
        || !registry_ || !registry_->get(fid))
    {
        return false;
    }

    auto* source = native_host(source_host);
    auto* target = native_host(target_host);
    if (!source || !target || !source->main_window() || !source->main_window()->canvas_for(fid))
    {
        return false;
    }

    size_t owner_count = 0;
    for (const auto& [id, entry] : hosts_)
    {
        (void)id;
        if (entry.host && entry.host->main_window() && entry.host->main_window()->canvas_for(fid))
            ++owner_count;
    }
    if (owner_count != 1)
        return false;

    if (source_host == target_host)
        return true;
    if (!target->main_window() || target->main_window()->canvas_for(fid))
        return false;

    if (!target->add_figure_tab(fid))
        return false;

    if (!source->release_figure_tab(fid))
    {
        target->release_figure_tab(fid);
        return false;
    }

    if (document_changed_callback_)
        document_changed_callback_();
    SPECTRA_LOG_INFO("main_window_registry",
                     "Moved document figure_id=" + std::to_string(fid)
                         + " source_host=" + std::to_string(source_host)
                         + " target_host=" + std::to_string(target_host));
    return true;
}

HostId MainWindowRegistry::detach_document(FigureId fid, HostId source_host)
{
    if (find_host_for_figure(fid) != source_host)
        return INVALID_HOST_ID;

    const HostId target_host = create_detached_window();
    if (target_host == INVALID_HOST_ID)
        return INVALID_HOST_ID;

    if (move_document(fid, source_host, target_host))
        return target_host;

    close_detached_window(target_host);
    return INVALID_HOST_ID;
}

void MainWindowRegistry::wire_window(HostId id, SpectraMainWindow* window)
{
    if (!window)
        return;

    window_connections_.push_back(QObject::connect(window,
                                                   &SpectraMainWindow::figure_closed,
                                                   window,
                                                   [this](FigureId fid) { close_document(fid); }));
    window_connections_.push_back(QObject::connect(window,
                                                   &SpectraMainWindow::figure_detach_requested,
                                                   window,
                                                   [this, id](FigureId fid)
                                                   { detach_document(fid, id); }));
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
