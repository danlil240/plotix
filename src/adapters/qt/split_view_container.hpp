#pragma once

// QtSplitViewContainer — central widget for split-pane figure display.
//
// Replaces the plain QTabWidget in SpectraMainWindow's central area when
// split mode is active.  Uses QSplitter for Qt-native split layout while
// wrapping the framework-neutral SplitViewManager for the logical model.
//
// Each pane is a QTabWidget holding FigureCanvasWidget instances.
// Supports:
//   - Split active pane right (horizontal) or down (vertical)
//   - Close a split pane (unsplit, keeping sibling)
//   - Reset to single pane
//   - Per-pane tab bars with figure tabs
//
// The SplitViewManager tracks which figure is in which pane; QSplitter
// handles the native layout and interactive splitter dragging.

#include <QWidget>

#include <spectra/fwd.hpp>

#include <memory>
#include <vector>

class QSplitter;
class QTabWidget;

namespace spectra
{
class SplitViewManager;
class FigureRegistry;
enum class ToolMode;
}   // namespace spectra

namespace spectra::adapters::qt
{

class QtRuntime;
class FigureCanvasWidget;

class QtSplitViewContainer : public QWidget
{
    Q_OBJECT

   public:
    QtSplitViewContainer(QtRuntime* runtime, FigureRegistry* registry, QWidget* parent = nullptr);
    ~QtSplitViewContainer() override;

    QtSplitViewContainer(const QtSplitViewContainer&)            = delete;
    QtSplitViewContainer& operator=(const QtSplitViewContainer&) = delete;

    // ── Figure tab management ──────────────────────────────────────────────

    // Add a figure as a new tab in the active pane.  Returns the tab index.
    int add_figure_tab(FigureId id);

    // Close the tab containing the given figure (in any pane) and notify the
    // document lifecycle owner. Returns false when the figure is not open.
    bool close_figure_tab(FigureId id);

    // Remove an open tab without emitting figure_closed. Cross-window moves
    // use this only after the destination has accepted the document.
    bool release_figure_tab(FigureId id);

    // Get the FigureId of the currently active tab in the active pane.
    FigureId active_figure_id() const;

    // Get the canvas widget for a figure, or nullptr.
    FigureCanvasWidget* canvas_for(FigureId id) const;

    // Number of open figure tabs across all panes.
    int figure_tab_count() const;

    // Get all FigureIds currently open as tabs across all panes.
    std::vector<FigureId> open_figure_ids() const;

    // Activate an already-open figure and focus its pane.
    bool activate_figure(FigureId id);

    // Select the interaction tool for the active document.
    void     set_active_tool(ToolMode tool);
    ToolMode active_tool() const;

    // ── Welcome page ───────────────────────────────────────────────────────

    void show_welcome_page();
    void hide_welcome_page();

    // ── Split operations ───────────────────────────────────────────────────

    // Split the active pane horizontally (left | right).
    // The active figure stays in the first pane; the next figure in the
    // registry goes to the new pane.  Returns true on success.
    bool split_right();

    // Split the active pane vertically (top / bottom).
    bool split_down();

    // Close the active split pane (unsplit its parent, keeping sibling).
    bool close_split();

    // Reset to single pane (all figures in one tab widget).
    void reset_splits();

    // Is the view currently split?
    bool is_split() const;

    // Number of panes.
    size_t pane_count() const;

    // ── Accessors ──────────────────────────────────────────────────────────

    SplitViewManager& split_view() { return *split_view_; }

   signals:
    void figure_closed(FigureId id);
    void figure_activated(FigureId id);
    void figure_detach_requested(FigureId id);
    void canvas_created(FigureId id, FigureCanvasWidget* canvas);

   private slots:
    void on_tab_changed(int index);
    void on_tab_close_requested(int index);
    void on_tab_context_menu(const QPoint& pos);

   private:
    struct PaneWidget;

    void rebuild_splitter();
    void sync_from_split_view();
    void update_active_from_focus();
    bool remove_figure_tab(FigureId id, bool notify_closed);

    PaneWidget* active_pane() const;
    PaneWidget* pane_for_figure(FigureId id) const;
    int         find_pane_index(PaneWidget* pane) const;

    QtRuntime*                        runtime_  = nullptr;
    FigureRegistry*                   registry_ = nullptr;
    std::unique_ptr<SplitViewManager> split_view_;

    QSplitter* splitter_     = nullptr;
    QWidget*   welcome_page_ = nullptr;

    std::vector<PaneWidget*> panes_;
};

}   // namespace spectra::adapters::qt
