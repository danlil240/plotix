#pragma once

// DockingHost — abstract docking/window-layout interface.
//
// Phase 4 component: defines the provider-neutral interface for managing
// dockable panels, document tabs, and multi-window layout.
//
// The application must not directly depend on provider APIs (KDDockWidgets,
// Qt ADS, etc.) outside provider implementations.  All docking operations
// flow through this interface.
//
// Concrete implementations:
//   - NativeQtDockingHost (QMainWindow + QDockWidget, mandatory fallback)
//   - KDDockWidgetsHost  (optional, after licensing gate)

#include <cstdint>
#include <string>
#include <vector>

#include <spectra/fwd.hpp>

namespace spectra::adapters::qt
{

// Opaque handle types for docking entities.
using PanelId    = uint64_t;
using DocumentId = uint64_t;   // Same as FigureId for figure documents
using HostId     = uint64_t;

inline constexpr PanelId    INVALID_PANEL_ID    = ~PanelId{0};
inline constexpr DocumentId INVALID_DOCUMENT_ID = ~DocumentId{0};
inline constexpr HostId     INVALID_HOST_ID     = ~HostId{0};

// Descriptor for a dockable panel.
struct PanelDescriptor
{
    std::string id;      // unique panel id (e.g. "inspector", "topics")
    std::string title;   // display title
    std::string area;    // "left", "right", "bottom", "top"
    bool        default_visible = true;
};

// Descriptor for a document (figure) tab.
struct DocumentDescriptor
{
    FigureId    figure_id = INVALID_FIGURE_ID;
    std::string title;
};

// Serialized layout state for save/restore.
struct DockLayoutState
{
    std::string provider;   // "native", "kddockwidgets", etc.
    std::string provider_version;
    // Per-window state (base64-encoded Qt saveState/saveGeometry).
    struct DockWindowState
    {
        HostId      host_id = INVALID_HOST_ID;
        std::string state_base64;
        std::string geometry_base64;
        std::string title;
    };
    std::vector<DockWindowState> windows;
};

// Abstract docking host interface.
//
// A DockingHost manages one main window and its dockable panels + document
// tabs.  Multiple DockingHost instances may exist simultaneously (one per
// main window).  The MainWindowRegistry coordinates cross-window operations.
class DockingHost
{
   public:
    DockingHost()                              = default;
    DockingHost(const DockingHost&)            = delete;
    DockingHost& operator=(const DockingHost&) = delete;
    virtual ~DockingHost()                     = default;

    // ── Panel management ───────────────────────────────────────────────────

    // Add a dockable panel to this host.  Returns a PanelId or INVALID_PANEL_ID.
    virtual PanelId add_panel(const PanelDescriptor& desc) = 0;

    // Remove a panel from this host.
    virtual void remove_panel(PanelId id) = 0;

    // Show or hide a panel.
    virtual void set_panel_visible(PanelId id, bool visible) = 0;
    virtual bool is_panel_visible(PanelId id) const          = 0;

    // ── Document (figure) management ───────────────────────────────────────

    // Add a figure document as a tab in this host.  Returns a DocumentId.
    virtual DocumentId add_document(const DocumentDescriptor& desc) = 0;

    // Remove a document from this host (does not destroy the figure).
    virtual void remove_document(DocumentId id) = 0;

    // Get the currently active document, or INVALID_DOCUMENT_ID.
    virtual DocumentId active_document() const = 0;

    // Get all documents in this host.
    virtual std::vector<DocumentId> documents() const = 0;

    // ── Cross-window operations ────────────────────────────────────────────

    // Detach a document into a new main window (new DockingHost).
    // Returns the HostId of the new window, or INVALID_HOST_ID on failure.
    virtual HostId detach_document(DocumentId id) = 0;

    // Move a document from this host to another host.
    virtual void move_document(DocumentId id, HostId target_host) = 0;

    // ── Layout persistence ─────────────────────────────────────────────────

    virtual DockLayoutState save_layout() const                          = 0;
    virtual bool            restore_layout(const DockLayoutState& state) = 0;

    // ── Host identity ──────────────────────────────────────────────────────

    virtual HostId      host_id() const                     = 0;
    virtual std::string title() const                       = 0;
    virtual void        set_title(const std::string& title) = 0;
};

}   // namespace spectra::adapters::qt
