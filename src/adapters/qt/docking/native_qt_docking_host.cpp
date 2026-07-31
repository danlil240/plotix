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
    auto* dock = window_->findChild<QDockWidget*>(QString::fromStdString(desc.id + "_dock"));
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

    return add_figure_tab(desc.figure_id) ? desc.figure_id : INVALID_DOCUMENT_ID;
}

void NativeQtDockingHost::remove_document(DocumentId id)
{
    const FigureId fid = static_cast<FigureId>(id);
    if (registry_)
        registry_->close_document(fid);
    else if (window_)
        window_->close_figure_tab(fid);
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

    for (FigureId fid : window_->open_figure_ids())
        result.push_back(static_cast<DocumentId>(fid));
    return result;
}

// ── Cross-window operations ───────────────────────────────────────────────

HostId NativeQtDockingHost::detach_document(DocumentId id)
{
    FigureId fid = static_cast<FigureId>(id);
    if (!window_ || !registry_ || fid == INVALID_FIGURE_ID)
        return INVALID_HOST_ID;

    return registry_->detach_document(fid, id_);
}

void NativeQtDockingHost::move_document(DocumentId id, HostId target_host)
{
    FigureId fid = static_cast<FigureId>(id);
    if (!window_ || !registry_ || fid == INVALID_FIGURE_ID)
        return;

    registry_->move_document(fid, id_, target_host);
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
    state.provider         = "native";
    state.provider_version = "1.0";

    if (!window_)
        return state;

    DockLayoutState::DockWindowState ws;
    ws.host_id         = id_;
    ws.state_base64    = to_base64(window_->saveState());
    ws.geometry_base64 = to_base64(window_->saveGeometry());
    ws.title           = title();
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

bool NativeQtDockingHost::add_figure_tab(FigureId fid)
{
    return window_ && !window_->canvas_for(fid) && window_->add_figure_tab(fid) >= 0;
}

bool NativeQtDockingHost::release_figure_tab(FigureId fid)
{
    return window_ && window_->release_figure_tab(fid);
}

}   // namespace spectra::adapters::qt
