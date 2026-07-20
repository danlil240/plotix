// native_qt_docking_host.cpp — Native Qt docking provider implementation.

#include "native_qt_docking_host.hpp"

#include "main_window_registry.hpp"
#include "../qt_main_window.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

#include <QByteArray>
#include <QDockWidget>
#include <QMainWindow>
#include <QTabWidget>

namespace spectra::adapters::qt
{

NativeQtDockingHost::NativeQtDockingHost(HostId              id,
                                         SpectraMainWindow*  window,
                                         MainWindowRegistry* registry)
    : id_(id), window_(window), registry_(registry)
{
}

NativeQtDockingHost::~NativeQtDockingHost() = default;

// ── Panel management ──────────────────────────────────────────────────────

PanelId NativeQtDockingHost::add_panel(const PanelDescriptor& desc)
{
    if (!window_)
        return INVALID_PANEL_ID;

    // For the native provider, panels are already created by SpectraMainWindow.
    // We track them here for the DockingHost interface.
    PanelId pid = next_panel_id_++;

    PanelEntry entry;
    entry.id   = pid;
    entry.area = desc.area;

    // Find existing QDockWidget by object name
    auto* dock = window_->findChild<QDockWidget*>(
        QString::fromStdString(desc.id + "_dock"));
    if (dock != nullptr)
    {
        entry.dock = dock;
        if (!desc.default_visible)
            dock->setVisible(false);
    }

    panels_[pid] = entry;
    return pid;
}

void NativeQtDockingHost::remove_panel(PanelId id)
{
    panels_.erase(id);
}

void NativeQtDockingHost::set_panel_visible(PanelId id, bool visible)
{
    auto it = panels_.find(id);
    if (it != panels_.end() && it->second.dock)
        it->second.dock->setVisible(visible);
}

bool NativeQtDockingHost::is_panel_visible(PanelId id) const
{
    auto it = panels_.find(id);
    return it != panels_.end() && it->second.dock != nullptr && it->second.dock->isVisible();
}

// ── Document (figure) management ──────────────────────────────────────────

DocumentId NativeQtDockingHost::add_document(const DocumentDescriptor& desc)
{
    if (!window_ || desc.figure_id == INVALID_FIGURE_ID)
        return INVALID_DOCUMENT_ID;

    window_->add_figure_tab(desc.figure_id);
    return desc.figure_id;  // DocumentId == FigureId
}

void NativeQtDockingHost::remove_document(DocumentId id)
{
    if (window_)
        window_->close_figure_tab(static_cast<FigureId>(id));
}

DocumentId NativeQtDockingHost::active_document() const
{
    if (!window_)
        return INVALID_DOCUMENT_ID;
    return static_cast<DocumentId>(window_->active_figure_id());
}

std::vector<DocumentId> NativeQtDockingHost::documents() const
{
    std::vector<DocumentId> result;
    if (!window_)
        return result;

    // Get all figure IDs from the registry that are open in this window.
    // We check each canvas in the main window.
    // For now, we rely on the main window's figure_tabs_ map.
    // Since we don't have direct access, we query through the public API.
    // The SpectraMainWindow tracks figure_tabs_ internally.
    // We can get the active figure and count, but not the full list.
    // TODO: Add a figures() accessor to SpectraMainWindow.
    // For now, return empty — the detach/move logic uses find_host_for_figure.
    return result;
}

// ── Cross-window operations ───────────────────────────────────────────────

HostId NativeQtDockingHost::detach_document(DocumentId id)
{
    FigureId fid = static_cast<FigureId>(id);
    if (!window_ || fid == INVALID_FIGURE_ID)
        return INVALID_HOST_ID;

    // Remove the figure tab from this window (stops animation, detaches canvas)
    window_->close_figure_tab(fid);

    // Create a new detached window via the registry
    if (detach_cb_)
    {
        return detach_cb_(this, id);
    }

    // Fallback: create via registry directly
    if (registry_)
    {
        HostId new_host_id = registry_->create_detached_window();
        if (new_host_id != INVALID_HOST_ID)
        {
            auto* new_host = registry_->native_host(new_host_id);
            if (new_host)
                new_host->add_figure_tab(fid);
        }
        return new_host_id;
    }

    return INVALID_HOST_ID;
}

void NativeQtDockingHost::move_document(DocumentId id, HostId target_host)
{
    FigureId fid = static_cast<FigureId>(id);
    if (!window_ || fid == INVALID_FIGURE_ID)
        return;

    // Remove from this window
    window_->close_figure_tab(fid);

    // Add to target host
    if (registry_)
    {
        auto* target = registry_->native_host(target_host);
        if (target)
            target->add_figure_tab(fid);
    }
}

// ── Layout persistence ────────────────────────────────────────────────────

static std::string to_base64(const QByteArray& ba)
{
    return ba.toBase64().toStdString();
}

static QByteArray from_base64(const std::string& s)
{
    return QByteArray::fromBase64(QByteArray::fromStdString(s));
}

DockLayoutState NativeQtDockingHost::save_layout() const
{
    DockLayoutState state;
    state.provider        = "native";
    state.provider_version = "1.0";

    if (!window_)
        return state;

    DockLayoutState::DockWindowState ws;
    ws.host_id          = id_;
    ws.state_base64     = to_base64(window_->saveState());
    ws.geometry_base64  = to_base64(window_->saveGeometry());
    ws.title            = title();
    state.windows.push_back(ws);

    return state;
}

bool NativeQtDockingHost::restore_layout(const DockLayoutState& state)
{
    if (!window_)
        return false;

    for (const auto& ws : state.windows)
    {
        if (ws.host_id == id_)
        {
            window_->restoreState(from_base64(ws.state_base64));
            window_->restoreGeometry(from_base64(ws.geometry_base64));
            if (!ws.title.empty())
                set_title(ws.title);
            return true;
        }
    }
    return false;
}

// ── Host identity ─────────────────────────────────────────────────────────

std::string NativeQtDockingHost::title() const
{
    if (!window_)
        return {};
    return window_->windowTitle().toStdString();
}

void NativeQtDockingHost::set_title(const std::string& title)
{
    if (window_)
        window_->setWindowTitle(QString::fromStdString(title));
}

// ── Native-specific ───────────────────────────────────────────────────────

void NativeQtDockingHost::add_figure_tab(FigureId fid)
{
    if (window_)
        window_->add_figure_tab(fid);
}

FigureId NativeQtDockingHost::remove_figure_tab(FigureId fid)
{
    if (window_)
    {
        window_->close_figure_tab(fid);
        return fid;
    }
    return INVALID_FIGURE_ID;
}

}   // namespace spectra::adapters::qt
