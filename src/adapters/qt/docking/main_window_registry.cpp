// main_window_registry.cpp — Multi-window registry implementation.

#include "main_window_registry.hpp"

#include "native_qt_docking_host.hpp"

#include "../qt_main_window.hpp"
#include "../figure_canvas_widget.hpp"
#include "../split_view_container.hpp"
#include "../qt_runtime.hpp"
#include "../qt_action_bridge.hpp"

#include "app/application_services.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/input/input.hpp"

#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QObject>
#include <QCoreApplication>
#include <QTimer>

#include <algorithm>

namespace spectra::adapters::qt
{

MainWindowRegistry::MainWindowRegistry(QtRuntime*                               runtime,
                                       FigureRegistry*                          registry,
                                       QtActionBridge*                          action_bridge,
                                       ApplicationServices*                     services,
                                       AxisLinkManager*                         axis_link_manager,
                                       std::function<TimelineEditor*(FigureId)> timeline_resolver)
    : runtime_(runtime), registry_(registry), action_bridge_(action_bridge), services_(services),
      axis_link_manager_(axis_link_manager), timeline_resolver_(std::move(timeline_resolver))
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
    if (primary_host_id_ == INVALID_HOST_ID)
        primary_host_id_ = id;
    window->set_detached_host(id != primary_host_id_);
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
    auto window = std::make_unique<SpectraMainWindow>(runtime_,
                                                      registry_,
                                                      action_bridge_,
                                                      services_,
                                                      timeline_resolver_);
    window->set_axis_link_manager(axis_link_manager_);

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
    hosts_[id].window->set_detached_host(true);
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

    // Identify which hosts actually contain the figure before removing tabs,
    // then release and retire any owned (detached) host that becomes empty.
    std::vector<HostId> affected_hosts;
    affected_hosts.reserve(hosts_.size());
    for (const auto& [id, entry] : hosts_)
    {
        if (entry.host && entry.host->main_window() && entry.host->main_window()->canvas_for(fid))
            affected_hosts.push_back(id);
    }

    bool removed_from_window = false;
    for (HostId hid : affected_hosts)
    {
        auto it = hosts_.find(hid);
        if (it != hosts_.end() && it->second.host)
            removed_from_window = it->second.host->release_figure_tab(fid) || removed_from_window;
    }

    for (HostId hid : affected_hosts)
        maybe_retire_empty_host(hid);

    if (!registry_->get(fid))
    {
        if (removed_from_window && document_changed_callback_)
            document_changed_callback_();
        return removed_from_window;
    }

    if (Figure* figure = registry_->get(fid); axis_link_manager_ && figure)
    {
        for (auto& axes : figure->axes_mut())
            if (axes)
                axis_link_manager_->remove_from_all(axes.get());
        for (auto& axes : figure->all_axes_mut())
            if (auto* axes3d = dynamic_cast<Axes3D*>(axes.get()))
                axis_link_manager_->remove_from_all_3d(axes3d);
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

    auto*           source_canvas = source->main_window()->canvas_for(fid);
    auto*           source_window = source_canvas ? source_canvas->vulkanWindow() : nullptr;
    OverlaySnapshot overlay;
    const bool      has_overlay = source_window && source_window->captureOverlaySnapshot(overlay);
    if (InputHandler* input = source->main_window()->central_view()->input_handler_for(fid))
        overlay.tool_mode = static_cast<uint8_t>(input->tool_mode());
    const auto selection = source_window ? source_window->selectedSeries()
                                         : std::vector<SpectraVulkanWindow::SeriesSelection>{};

    if (!target->add_figure_tab(fid))
        return false;

    if (auto* target_canvas = target->main_window()->canvas_for(fid))
    {
        if (has_overlay)
            target_canvas->vulkanWindow()->restoreOverlaySnapshot(overlay);
        if (!selection.empty())
            target_canvas->vulkanWindow()->setSeriesSelection(selection);
        if (InputHandler* input = target->main_window()->central_view()->input_handler_for(fid))
        {
            const auto max_tool = static_cast<uint8_t>(ToolMode::ROI);
            input->set_tool_mode(overlay.tool_mode <= max_tool
                                     ? static_cast<ToolMode>(overlay.tool_mode)
                                     : ToolMode::Pan);
        }
    }

    if (!source->release_figure_tab(fid))
    {
        target->release_figure_tab(fid);
        return false;
    }

    // An owned (detached) source host that is now empty should be retired
    // consistently, regardless of whether this move was a redock or a
    // cross-detached transfer.
    maybe_retire_empty_host(source_host);

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

bool MainWindowRegistry::redock_document(FigureId fid, HostId source_host)
{
    if (primary_host_id_ == INVALID_HOST_ID || source_host == primary_host_id_)
        return false;
    if (!move_document(fid, source_host, primary_host_id_))
        return false;

    // Empty-detached-host teardown is handled inside move_document; this
    // path is intentionally kept symmetric with any cross-host move.
    return true;
}

void MainWindowRegistry::maybe_retire_empty_host(HostId id)
{
    auto it = hosts_.find(id);
    if (it == hosts_.end() || !it->second.owned || !it->second.host)
        return;

    auto* window = it->second.host->main_window();
    if (!window || !window->open_figure_ids().empty())
        return;

    // Destroying an owned (detached) window synchronously inside a command or
    // signal handler can invalidate the sender, so retire on the next event-loop
    // turn. Primary and other non-owned hosts are never closed.
    QTimer::singleShot(0,
                       QCoreApplication::instance(),
                       [this, id]() { close_detached_window(id); });
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
    window_connections_.push_back(QObject::connect(window,
                                                   &SpectraMainWindow::figure_redock_requested,
                                                   window,
                                                   [this, id](FigureId fid)
                                                   { redock_document(fid, id); }));
    window_connections_.push_back(QObject::connect(
        window,
        &SpectraMainWindow::figure_move_to_pane_requested,
        window,
        [this, id, window](FigureId fid, size_t target_pane_index)
        {
            const HostId source_id = find_host_for_figure(fid);
            if (source_id == INVALID_HOST_ID || source_id == id
                || !window->central_view()->set_next_figure_target_pane(target_pane_index))
                return;
            if (!move_document(fid, source_id, id))
                window->central_view()->clear_next_figure_target_pane();
        }));
    window_connections_.push_back(
        QObject::connect(window,
                         &SpectraMainWindow::figure_drop_requested,
                         window,
                         [this, id](FigureId fid)
                         {
                             const HostId source = find_host_for_figure(fid);
                             if (source != INVALID_HOST_ID && source != id)
                                 move_document(fid, source, id);
                         }));
    window_connections_.push_back(QObject::connect(window,
                                                   &SpectraMainWindow::workspace_state_changed,
                                                   window,
                                                   [this]()
                                                   {
                                                       if (document_changed_callback_)
                                                           document_changed_callback_();
                                                   }));
}

std::vector<HostId> MainWindowRegistry::all_hosts() const
{
    std::vector<HostId> result;
    result.reserve(hosts_.size());
    for (const auto& [id, entry] : hosts_)
        result.push_back(id);
    std::sort(result.begin(), result.end());
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
