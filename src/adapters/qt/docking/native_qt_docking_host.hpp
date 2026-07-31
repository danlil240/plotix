#pragma once

// NativeQtDockingHost — native Qt docking provider implementation.
//
// Phase 4 component: implements DockingHost using QMainWindow + QDockWidget
// + QTabWidget.  This is the mandatory dependency-free fallback provider.
//
// Features:
//   - dockable panels via QDockWidget
//   - figure documents as tabs in a central QTabWidget
//   - document detach into a new SpectraMainWindow (via MainWindowRegistry)
//   - cross-window document movement
//   - layout save/restore via QMainWindow::saveState()/saveGeometry()

#include "docking_host.hpp"

#include <QMainWindow>

#include <functional>
#include <memory>
#include <unordered_map>

class QDockWidget;
class QTabWidget;
class QWidget;

namespace spectra
{
class FigureRegistry;
}

namespace spectra::adapters::qt
{

class SpectraMainWindow;
class QtRuntime;
class QtActionBridge;
class FigureCanvasWidget;
class MainWindowRegistry;

class NativeQtDockingHost : public DockingHost
{
   public:
    NativeQtDockingHost(HostId id, SpectraMainWindow* window, MainWindowRegistry* registry);
    ~NativeQtDockingHost() override;

    NativeQtDockingHost(const NativeQtDockingHost&)            = delete;
    NativeQtDockingHost& operator=(const NativeQtDockingHost&) = delete;

    // ── DockingHost interface ──────────────────────────────────────────────

    PanelId add_panel(const PanelDescriptor& desc) override;
    void    remove_panel(PanelId id) override;
    void    set_panel_visible(PanelId id, bool visible) override;
    bool    is_panel_visible(PanelId id) const override;

    DocumentId              add_document(const DocumentDescriptor& desc) override;
    void                    remove_document(DocumentId id) override;
    DocumentId              active_document() const override;
    std::vector<DocumentId> documents() const override;

    HostId detach_document(DocumentId id) override;
    void   move_document(DocumentId id, HostId target_host) override;

    DockLayoutState save_layout() const override;
    bool            restore_layout(const DockLayoutState& state) override;

    HostId      host_id() const override { return id_; }
    std::string title() const override;
    void        set_title(const std::string& title) override;

    // ── Native-specific ────────────────────────────────────────────────────

    // Get the underlying SpectraMainWindow.
    SpectraMainWindow* main_window() const { return window_; }

    // Add a figure tab directly (used during move operations).
    // Returns false when the figure cannot be opened or is already present.
    bool add_figure_tab(FigureId fid);

    // Remove a figure tab without closing the underlying figure model.
    bool release_figure_tab(FigureId fid);

   private:
    HostId              id_       = INVALID_HOST_ID;
    SpectraMainWindow*  window_   = nullptr;
    MainWindowRegistry* registry_ = nullptr;

    // Panel tracking: panel_id -> QDockWidget
    struct PanelEntry
    {
        PanelId      id   = INVALID_PANEL_ID;
        QDockWidget* dock = nullptr;
        std::string  area;
    };
    std::unordered_map<PanelId, PanelEntry> panels_;
    PanelId                                 next_panel_id_ = 1;
};

}   // namespace spectra::adapters::qt
