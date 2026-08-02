# Spectra Legacy vs Qt 6 Application Parity Gap Report

**Audit date:** 2026-07-26

**Audited revision:** `09a6c6ae02b2ffe3f59e9e092d0b151445282590` on
`plan/qt6-application-migration`, plus the pre-existing uncommitted working-tree changes listed by
`git status` at audit time

**Decision:** **Qt is not eligible to become the default frontend.** Keep
`SPECTRA_DEFAULT_FRONTEND=legacy`.

**Implementation update (2026-08-02):** The remaining P0 document lifecycle and
OS-level drag gaps are closed. `MainWindowRegistry` now retires empty detached
windows consistently after `close_document`, `move_document`, and `redock_document`,
using a deferred `QTimer` teardown that avoids destroying the command sender during
dispatch. `SpectraDocumentTabBar` has been converted from a signal-based detach
gesture into a real `QDrag` using the shared `application/x-spectra-figure-id` MIME
type; it now supports drag-enter, drag-move, and drop, accepts cross-window drops
on the header, and falls back to detach only when no Spectra target accepts the
drop. Welcome-state chrome transitions (`on_welcome_page_visible`) are now
non-dirty, so opening or closing a figure does not spuriously mark the workspace
as modified. The complete `qt_test_qt_window_ops` (35/35), `qt_test_qt_panels`
(69/69), `qt_test_qt_docking` (6/6), and `qt_test_qt_workspace` (20/20) suites
pass under `QT_QPA_PLATFORM=offscreen`.

**Companion plan:** [QT6_APPLICATION_MIGRATION_PLAN.md](QT6_APPLICATION_MIGRATION_PLAN.md)

**Implementation update (2026-07-26):** `QT-GAP-001`, `QT-GAP-002`, `QT-GAP-003`, and
`QT-GAP-016` are partially reduced. Qt now implements `list_menus`, `switch_figure`, `add_series`,
`get_figure_info`, `get_screenshot_base64`, and `list_methods`; the figure mutation and
serialization paths are shared with legacy. `capture_screenshot` now has explicit canvas scope,
while `capture_window` and base64 capture use the composed screen surface and include owned
top-level popups/dialogs. A launched X11/lavapipe check produced distinct 1208×612 canvas and
1280×720 window PNGs, included the native Vulkan axes and the separate command-palette dialog, and
matched a simultaneous X11 root capture pixel-for-pixel. The custom menu strip now opens
non-blocking popups, allowing MCP menu clicks to return while capture and dismissal operate on the
open menu. Qt also dispatches the seven pointer,
wheel, keyboard, text, and double-click automation methods to real Qt widgets or the embedded
native canvas. Three advertised fuzz tools remain explicit not-implemented responses. Document
close, move, and detach now route through
`MainWindowRegistry`: tab close unregisters the figure exactly once, cross-window transfer validates
both hosts before releasing the source, failed destination/release operations preserve or roll back
the document, and docking hosts enumerate their documents. The Qt tests cover semantic automation,
close notification/model removal, successful move, and failure preservation. Persistence dirty
tracking now covers successful document close/move, a production timer ticks the autosave manager,
shutdown saves before destroying window topology, and interactive startup invokes crash recovery.
Dirty coverage for other workspace mutations, complete restore, nested redock topology, and
process-restart coverage remain open.

**Implementation update (2026-07-31):** `QT-GAP-007` is further reduced. Navigation-rail
interaction tools now invoke the same registered commands as menus, shortcuts, the command palette,
and automation. The rail highlight and status tool chip are synchronized from the active canvas's
`InputHandler`, including per-document tool restoration on tab switches. Panel navigation no longer
impersonates an interaction-tool selection. A focused Qt regression covers command dispatch,
panel-selection isolation, direct command execution, and per-document state. `QT-GAP-005` and
`QT-GAP-010` are also reduced: `panel.toggle_nav_rail` now has a semantic result, and the persisted
Inspector, Navigation Rail, and Timeline settings are applied to the live shell. Shell/menu changes
are reflected back into the settings controls and persistence path. `QT-GAP-001` is further reduced:
successful native-canvas renders now advance the shared frame counter, `pump_frames` synchronously
renders the requested count and errors on partial/zero progress, and deferred `wait_frames` requests
consume actual rendered-frame deltas instead of Qt timer polls. The live MCP regression covers exact
pump and wait progress; a launched X11/lavapipe application returned `pumped: 3` and then completed a
two-frame wait against its visible Vulkan canvas.

**Implementation update (2026-08-01):** `QT-GAP-010` is further reduced. The Qt application
stylesheet and every custom-painted shell surface now derive from the live process
`ThemeManager` palette instead of a second static night palette. Theme changes from Settings,
registered theme commands, and workspace load refresh all top-level windows and native popups;
command selections also update the persisted default and the Settings control. Reusable Qt
controls no longer retain local dark-color overrides. The focused regression renders the custom
header and navigation rail under Night and Light, verifies a material luminance change, and checks
that the application stylesheet contains the Light theme background with no original hard-coded
night background. Full approved panel/dialog goldens and the platform/DPR matrix remain open.

**Implementation update (2026-08-01):** `QT-GAP-011` is closed for the audited stale-state
failure. The Qt Inspector now derives its axes summary and tabs from one non-null unified traversal
of the active `Figure`, so 2D and 3D axes cannot disagree with the model. Successful active-canvas
frames synchronize the displayed framebuffer dimensions and externally changed controls without
rebuilding the editor; axes or series topology changes trigger a safe rebuild. `Axes3D` receives
native title, XYZ label/limit, grid-plane, bounding-box, and series controls rather than being
silently omitted. Focused regressions reproduce the old 640×480 to 1208×612 stale-size path, mix
2D and 3D axes, add axes after activation, synchronize external 3D mutations, and mutate 3D controls
back to the authoritative model. The complete 9-test Qt-labelled suite passes. Inspector feature
parity beyond the stale-state finding, including the full legacy property/data surface, remains
tracked by `QT-GAP-007` and `QT-GAP-012`.

**Implementation update (2026-08-01):** `QT-GAP-008` is partially reduced. Each Qt figure now
resolves a stable `TimelineEditor` initialized from its animation duration, FPS, loop mode, and
playhead. The visible Timeline switches to the active figure's editor, and the six registered
animation commands resolve that same active editor instead of a disconnected controller-only
instance. Canvas animation ticks—not the Timeline's presentation timer—advance playback, evaluate
the editor, synchronize state back to `FigureAnimState`, and invoke the production `on_frame`
callback before rendering. Detached hosts receive the same per-figure resolver. Focused tests prove
that widget polling cannot advance time independently, external semantic changes are reflected,
document model switching works, play controls mutate the shared editor, and canvas ticks drive both
the editor and the figure callback through playing and paused/scrubbed states. Curve Editor, native
track/keyframe authoring, undo transactions, and timeline/keyframe persistence remain open.

**Implementation update (2026-08-01):** `QT-GAP-008` is further reduced. The native Timeline now
authors, renames, removes, hides, and locks tracks and adds, moves, selects, and removes keyframes
through addressable Qt controls. Each mutation snapshots the authoritative `TimelineEditor` into the
shared undo manager; undo and redo refresh the panel and mark workspace state dirty. Timeline JSON
now round-trips track names, colors, flags, keyframes, transport/playhead, loop/snap, and view state,
and every figure stores its own payload in manual workspaces, autosaves, crash recovery, and detached
window reconstruction. The legacy single-timeline workspace fields remain a backward-compatible
fallback. Focused model, workspace-file, and real-widget regressions pass, as does the complete
9-test Qt-labelled suite. Curve/value-channel authoring, interpolation controls, and launched visual
artifact comparison remain open.

**Implementation update (2026-08-01):** `QT-GAP-008` is further reduced. Production Qt timelines
now own a per-figure `KeyframeInterpolator`; animated tracks use stable shared track/channel IDs,
and rename/remove plus keyframe move/remove remain synchronized across marker and typed-channel
state. The native Timeline authors and edits numeric keyframe values, all seven interpolation modes
(Step, Linear, Cubic Bezier, Spring, Ease In/Out variants), all four tangent modes, and explicit
in/out tangent handles; it shows typed values and interpolation in the keyframe list and routes
changes through the existing serialized undo/autosave/workspace transaction. Focused model and
widget tests cover 150 animation assertions plus value/mode/tangent undo and exact serialization
round trip. The graphical Curve Editor, property-binding UI, and launched visual/restart comparison
remain open.

**Implementation update (2026-08-01):** `QT-GAP-008` property authoring is further reduced. Every
numeric Timeline track can now bind through a native property selector to stable 2D/3D axes-limit,
series-opacity, line-width, or point-size paths. Canvas playback and scrubbing apply interpolated
values to the authoritative figure rather than a presentation copy. The path—not a raw pointer or
callback—is stored with the track, so undo/redo and workspace/autosave/recovery deserialize the
track first and rebuild topology-resolving callbacks afterward. Missing axes/series targets stay
visibly unavailable without losing their intended path and recover when the topology returns.
Focused model and real-widget tests prove 2D axes and 2D/3D series application, binding undo/redo,
and exact property rebinding after serialization. The graphical Curve Editor and launched
cross-frontend visual/restart artifact comparison remain open.

**Implementation update (2026-08-01):** `QT-GAP-008` native curve authoring is now implemented.
The restored `panel.toggle_curve_editor` command, View action, check state, and navigation-rail
control open a real dock bound to the active figure's same interpolator. Its themed painted surface
overlays every channel, sampled interpolation curves, keyframe diamonds, selected tangent handles,
grid/value/time bounds, and the live playhead. Fit/reset, wheel zoom, middle-button pan, click and
box selection, keyframe/tangent dragging, Escape cancellation, and Delete are functional. Curve
edits synchronize typed channels back to Timeline markers, evaluate bound properties immediately,
and commit one shared undo/autosave transaction; active-document switches rebind the dock. A real
widget regression renders the graph, drags a keyframe, proves marker/channel agreement, and undoes
the exact time/value edit. `QT-GAP-008` remains open only for launched cross-frontend restart and
approved visual/artifact comparison.

**Implementation update (2026-08-01):** the missing Axes linking workflow is now implemented across
`QT-GAP-004`, `QT-GAP-006`, `QT-GAP-007`, and `QT-GAP-016`. The visible Axes menu exposes Link X,
Link Y, Link Z, Link All, and Unlink All with live enable state derived from the active figure's
actual 2D/3D topology. One controller-owned `AxisLinkManager` is injected into every existing and
new `InputHandler`, including rebuilt split panes and detached hosts, so production pan/zoom/limits
propagate through the shared path. X/Y/XY groups cover all 2D subplots; X/Y/Z/XYZ groups cover all
3D subplots without the legacy X/Y-to-All mismatch. Workspace/autosave state now serializes both
2D and 3D groups against deterministic global axes indices, restores them after fresh figure IDs,
and removes memberships before model destruction. Focused tests cover 52 manager assertions,
2D/3D serialization and propagation, real Axes actions, per-canvas input injection, Z isolation,
and complete unlink. Launched pointer/wheel and restart evidence remains open.

**Implementation update (2026-08-01):** `QT-GAP-006` and `QT-GAP-007` are further reduced. Qt no
longer appends an ad-hoc second set of panel and split actions after routing registered commands:
menus, shortcuts, the palette, and automation now reference the same deterministic `QAction`
instances exactly once. Registered panel actions are checkable and synchronize from the live panel
surface, including command/shortcut-driven changes, and target the owning active window. Empty Axes
and Transforms headers are hidden instead of advertising inert menus. Focused regressions assert
deterministic action/category ordering, single-instance Panels/Splits entries, semantic execution,
and the truthful seven-menu header. A shared cross-frontend ordered menu model and real Axes/
Transforms commands remain open.

**Implementation update (2026-08-01):** the Qt Transforms menu is now populated from the same live
`TransformRegistry` as legacy, including transforms added or removed by plugins before the menu is
opened. Selecting a named transform applies it immediately to every visible editable 2D series,
preserving the legacy scope while routing through the Qt Transform panel's authoritative shared
undo, redraw, and autosave path. Custom Formula opens and focuses the native formula editor instead
of presenting an inert action. Stable action IDs and menu automation expose both paths. Focused
regressions prove the menu is visible, built-in actions are present, a prior single-series target is
replaced by all-visible scope, two series receive the exact transform result, and one undo restores
both. `QT-GAP-006` remains open for a shared ordered menu model and launched cross-frontend
label/order/enable/result comparison, not for absent Axes or Transform commands.

**Implementation update (2026-08-01):** the audited native dock-topology persistence hole in
`QT-GAP-004` and `QT-GAP-014` is now covered. `QMainWindow` state and geometry round-trip dock areas,
tab groups, visibility, floating state, and window geometry through manual workspace, autosave, and
recovery payloads for primary and detached hosts. Direct dock visibility, floating, and location
changes now mark workspace state dirty even when they originate from pointer interaction rather
than a registered command. Restore no longer reports success for empty or corrupt Qt state bytes. A
focused real-widget regression captures a right-side tab group with a hidden member and a visible
floating dock, destructively rearranges all three panels, restores the payload, and proves the exact
topology/state; a corrupt-state regression proves truthful failure. Broader multi-window placement
and cross-platform restart evidence remain open.

**Implementation update (2026-08-01):** `QT-GAP-007` and `QT-GAP-015` are further reduced by
restoring the Markers navigation-rail control. Like legacy, it is active when the current canvas has
persistent data-tip markers and clears them when clicked. Marker state is queried and mutated
through the canvas/runtime-owned `DataInteraction`, including the pre-attachment snapshot path; the
operation redraws and emits the normal workspace/autosave mutation signal. Its active indicator is
independent of the Select/Pan/Zoom tool selection, and switching documents recomputes it from the
new active canvas. Focused regressions prove one document's clear leaves another document's marker
intact, preserves non-marker overlay state, updates the rail without changing Pan, and reports only
real mutations. Launched marker placement/removal pixels, marker editing/undo, and export remain
open.

**Implementation update (2026-08-01):** `QT-GAP-005` and `QT-GAP-007` panel navigation is further
consolidated. Inspector, Timeline, Curve Editor, Plugins, Topics, and Settings rail buttons now
trigger their registered `QAction` instances, so rail clicks execute the same command callbacks and
live check-state synchronization as menus, shortcuts, the palette, and automation. Lightweight
hosts retain direct-surface fallbacks only when a command is genuinely absent; panel navigation
still does not overwrite the active interaction-tool selection. A focused regression activates all
six buttons, proves each exact command runs once, and proves the active Select state is unchanged.
The Transform rail item remains a direct native-panel path because there is no shared legacy
command descriptor for that dialog.

**Implementation update (2026-08-01):** `QT-GAP-016` keyboard/accessibility coverage is further
reduced for the custom-painted shell. Navigation buttons and the Home control now expose meaningful
accessible names and shortcut descriptions. Frameless minimize/maximize/restore/close controls and
custom dock collapse/close controls are keyboard-focusable and named by result instead of exposing
only decorative glyphs. The custom document tab bar now has a live count/active-document accessible
description, a visible focus outline, and keyboard selection with Left/Right/Home/End plus Delete,
Insert, and Ctrl+Shift+D close/add/detach results. A real-widget regression proves the names, focus
policies, emitted document IDs, wraparound selection, and every new key result. Child-level tab
roles, screen-reader smoke, focus traversal across all panels, IME, and platform key layouts remain
open.

**Implementation update (2026-08-01):** `QT-GAP-007`, `QT-GAP-014`, and `QT-GAP-016` no longer
fall back to the wrong split pane when focus belongs to an embedded native canvas. Each
`FigureCanvasWidget` now promotes native-window/container FocusIn and mouse activation to its
owning figure; the split controller selects that pane/document before subsequent tool, menu,
shortcut, palette, or automation commands resolve active state. Focused QWidget descendants are
also recognized by `active_pane()` instead of requiring the `QTabWidget` itself to own focus. A
focused two-pane regression moves native focus to the second Vulkan window and proves the active
figure changes and the next Measure command mutates only that pane's `InputHandler`.

**Implementation update (2026-08-01):** native cross-pane tab drag-and-drop is now implemented for
`QT-GAP-014` and `QT-GAP-016`. Pane tab bars carry a stable figure-ID MIME payload; a destination
drop moves the figure between authoritative `SplitPane` lists, reparents the existing canvas and
`InputHandler` without renderer/input-state recreation, activates the destination, and emits one
workspace/autosave topology mutation. Same-pane native tab moves also reorder the model so saved
tab order cannot disagree with the UI. The drop is deferred until after Qt returns from the source
widget event, preventing deletion of a live event receiver during splitter reconstruction. A
focused real drag-enter/drop regression proves acceptance, exact destination/source membership,
canvas/input pointer preservation, active-document result, and one dirty notification. Cross-window
drag-and-drop and platform drag behavior remain open.

**Implementation update (2026-08-01):** the same stable tab MIME path now supports cross-window
drops through `MainWindowRegistry`, further reducing `QT-GAP-003`, `QT-GAP-014`, and `QT-GAP-016`.
The target remembers the exact destination pane for the next destination-first add; the registry
validates single ownership, creates the destination canvas, transfers overlay markers, active tool,
and selected-series state, then releases the source. A failed source release removes the destination
copy and preserves the source. Focused regressions prove a real cross-window drag-enter/drop moves
the document without unregistering its model and emits one persistence mutation, while direct move
proves marker/tool/selection preservation and the existing invalid-destination rollback remains
green. Launched OS drag behavior, empty detached-window retirement policy, and multi-platform
placement remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` is further reduced. The Qt Data Editor now
appends rows, deletes a multi-row selection, moves rows up/down, and pastes bounded rectangular TSV
or CSV cells into X/Y data, extending the selected line/scatter series when needed. Each operation
validates the complete input before mutation, commits as one shared undo/redo transaction, refreshes
the live table after apply/undo/redo, requests the owning figure redraw, and marks workspace/autosave
state dirty. Stable buttons plus native Paste/Delete shortcuts expose the same operations. Focused
tests cover exact model results, history/redraw/dirty notifications, undo restoration, and atomic
rejection of non-numeric paste. Injected Import/Export buttons now use the shared CSV/TSV parser to
replace the selected series through one undoable mutation and write a verified two-column CSV
artifact without bypassing automation dialog policy. The legacy multi-column picker/multi-series
creation, explicit target management, large-data virtualization, and the remaining
transform/topic/plugin workflows remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` Data Editor import is further reduced. CSV/TSV
files with more than two numeric columns now open an automation-safe in-panel mapping surface with
a shared-X selector, checkable multi-Y list, and Select All/Clear helpers, defaulting every non-X
column to a named new line series on the selected axes. A two-column file also enters this creation
path when the selected axes has no series, so an empty plot is recoverable without a pre-existing
target. The complete mapping and row counts are validated before mutation, the
200-series bound is enforced, and all created topology/data/style is captured in one undo/redo
action with redraw, live selector refresh, and autosave notification. A real three-column artifact
test proves labels and values for both created series plus exact topology removal/restoration across
undo/redo. Optional Z/3D mapping, absolute datetime-offset presentation, and large-data
virtualization remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` Data Editor 3D handling is further reduced.
Line/scatter 3D series now use a native X/Y/Z table and the same validated cell, row, rectangular
paste, shared undo/redo, redraw, and autosave path as 2D data. On 3D axes, CSV mapping exposes
distinct shared X and Z selectors plus one or more Y series; each selection creates a named 3D line
with exact topology-aware undo/redo. Export writes an explicit `x,y,z` artifact. Focused tests prove
mapped multi-series values, Z editing, exact export, undo, topology removal, and redo restoration.
Absolute datetime-offset presentation and large-data virtualization remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` Data Editor timestamp handling is further
reduced. The Qt path now consumes the shared CSV parser's double-precision per-column base instead
of discarding it: imported epoch/datetime X values remain precise relative floats in the renderer
while the table and exported CSV present absolute values. Absolute X edits and paste convert back to
the relative representation. The offset is part of shared data undo/redo, series clipboard/topology
snapshots, and version-6 figure serialization for line/scatter 2D and 3D series. Focused regressions
cover sub-second epoch import, presentation, editing, export, undo/redo, clipboard preservation, and
2D/3D restart serialization. Large-data virtualization remains open.

**Implementation update (2026-08-01):** `QT-GAP-012` Data Editor scalability is further reduced.
The native table now materializes at most 1,000 rows at once, exposes addressable Previous/Next
controls and absolute row ranges, and maps selection, cell edits, row operations, paste extension,
and undo refresh back to the correct underlying indices. A 2,505-point focused fixture proves the
bounded row count, page boundaries, absolute-index mutation, undo restoration, and retained page.

**Implementation update (2026-08-01):** `QT-GAP-012` Data Editor empty-state behavior is now
reduced. An axes with no series shows a compact, addressable explanation and direct Import CSV
recovery action instead of reserving the full blank table; absent figures/axes and unsupported
series get distinct actionable states. Starting column mapping temporarily reveals its controls,
cancel returns to the compact state, and a newly created series restores the normal selector/table
surface. A focused regression proves the empty state, enabled recovery action, hidden grid, live
transition after the first series appears, and populated row count.

**Implementation update (2026-08-01):** `QT-GAP-012` is further reduced for transforms. The Qt
Transform panel now requires an explicit target choice—one named editable series or all visible
series—and applies both individual transforms and pipelines only within that scope. A live
non-destructive preview reports affected series/point counts and output Y bounds before Apply. The
same shared undo transaction restores the exact target data, refreshes the preview, redraws the
owning figure, and marks workspace/autosave state dirty on apply, undo, and redo. Focused tests prove
that a selected series changes while its neighbor remains byte-for-byte unchanged. Custom-formula
authoring now uses the shared expression parser (`x`, `y`, `t`, `i`, `n`, and cross-series
references), validates before mutation, reports scoped output bounds, rejects invalid input without
history, and commits through the same undo/redraw/autosave path. Transformation provenance,
pipeline workspace persistence, and broader artifact/error coverage remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` transform persistence is further reduced. The
Qt panel now retains a distinct pipeline per figure, including its all-visible, axes-wide, or
exact-series target, ordered/enabled steps, pipeline name, transform names/types, and every built-in parameter
(scale, offset, clamp bounds, NaN policy, log base, FFT dB/sample rate). Workspace capture follows
the figure's owning native window, records figure indices rather than transient IDs, and restore
rebinds the state after fresh figure-ID and window-layout reconstruction. Legacy `param` workspaces
remain readable for Scale/Offset. Focused schema and real-widget round trips prove target and full
parameter restoration. Transformation provenance, unavailable plugin-defined custom functions,
and broader launched restart/error coverage remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` transform targeting/editing is further
reduced. Each axes with editable data now appears as a native target between all-visible and its
individual series, and the shared transform/pipeline/formula paths consistently honor that scope.
Pipeline rows are checkable and expose stable Up/Down/Remove/Clear controls; reordering and enabling
steps immediately updates per-figure persisted state. Focused tests prove an axes-wide transform
changes both series on the selected axes while leaving another axes untouched and undo restores the
exact inputs; a reordered disabled step survives the panel/workspace round trip. Arbitrary
multi-target selection and transformation provenance remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` arbitrary transform targeting is now reduced.
The Qt panel exposes a checkable exact-series list across every 2D axes; one or more checked entries
override the quick all-visible/axes/single-series scope for built-in transforms, shared-parser
formulas, previews, and pipelines. The exact `(axes, series)` set is serialized in each per-figure
pipeline and restored through workspace restart, while shared undo mutates and restores only that
set. Transform snapshots also retain absolute datetime X offsets through built-in, formula, and
pipeline operations (FFT deliberately establishes a new zero-based frequency domain). Focused
tests prove a non-contiguous two-of-three selection across axes, unchanged excluded data, exact
undo, target round-trip, and timestamp-offset preservation. Transformation provenance and
unavailable plugin-defined custom-function restoration remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` custom-transform persistence and provider
provenance are now reduced. Plugin initialization records every transform name against its provider,
workspace steps preserve that source, and plugin unload removes owned callbacks before closing the
library. If a workspace opens without the provider, the Qt pipeline retains the step in its exact
position and intended check state, marks it visibly unavailable, and safely excludes it from apply;
refresh resolves the same step in place when the provider returns. Focused registry, plugin-load,
workspace, pipeline, and Qt tests prove provider attribution, safe unload, lossless unavailable-step
round-trip, non-mutation by the missing step, and later recovery/application. Broader launched
plugin lifecycle and artifact/error comparison remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` portable plugin-panel interaction is further
reduced. The shared registry now exposes each schema together with its stable interaction ID, and
the Qt renderer sends that exact ID plus the property/action ID and serialized value to callbacks.
Plugin-returned clamped values immediately reconcile the native control. Group child indices now
build recursively with nested `QGroupBox` ownership, root de-duplication, cycle rejection, real
separators, tooltips, and addressable property/action widgets. A focused real-widget test proves a
nested group renders each child exactly once, integer callback payloads carry the registered schema
ID, the returned value replaces the attempted edit, and a nested action receives its exact IDs.
Plugin custom directories, management diagnostics/capabilities, and launched artifact/error
coverage remain open.

**Implementation update (2026-08-01):** `QT-GAP-012` plugin management is further reduced. The Qt
panel now matches the legacy default-plus-custom-directory scan workflow with de-duplicated,
removable paths, separate all-directory/default rescans, persistent in-panel results, and stable
automation IDs. Loaded-plugin cards expose library path, negotiated API version, manifest
capabilities, call/fault/init diagnostics, quarantine/last-fault health, and enabled/unload controls.
Both plugin panels replace their scroll content atomically on refresh, preventing stale duplicate
controls without destroying the sender during an active signal. Focused tests prove custom path
add/de-duplicate/remove, real mock-plugin load, path/API/capability/diagnostic presentation, and
single live control trees. Launched plugin artifact/error coverage remains open.

**Implementation update (2026-08-01):** `QT-GAP-007` is further reduced. Figure renames now
propagate from the Inspector's authoritative model mutation to both the custom document strip and
the native per-pane tab, while successful rendered frames resynchronize externally changed and
undo/redo-restored titles. The status zoom chip now uses the same first-2D-axis data-range/view-range
calculation as the legacy shell, refreshes immediately on active-document changes, and follows live
limit changes after successful frames. Cursor, FPS, GPU time, zoom, and title synchronization reject
background-tab frame/input signals instead of allowing a non-active canvas to overwrite visible
state. Cursor readout now publishes the `InputHandler`'s double-precision 2D data coordinates after
axes hit-testing and clears outside axes instead of labeling raw canvas pixels as X/Y. Focused
regressions cover rename propagation, external model synchronization, per-document zoom restoration,
background-render isolation, DPR-aware data-coordinate mapping, and invalid/outside clearing. The
remaining visible-control result matrix is open.

**Implementation update (2026-08-01):** `QT-GAP-005` and `QT-GAP-007` are further reduced by
replacing the `plot.function` no-op with a native modeless editor. It resolves the active figure's
2D axes, initializes its range from the live limits, validates formulas through the shared
expression parser, and delegates sampling/styling to `ui::add_function_plot`. The result requests a
targeted redraw and the editor remains automation-addressable through stable object names. A focused
regression covers invalid-expression rejection and the exact sampled series/model result. Nine
registered Qt commands remain explicit no-ops/stubs.

**Implementation update (2026-08-01):** `QT-GAP-005` is further reduced. `app.cancel` now closes
the owning window's active popup, command palette, or modeless dialog and reports whether it
dismissed a surface; it is no longer a fallback no-op. A focused regression covers owned-dialog
dismissal and the empty-state result. Eight registered Qt commands remain explicit no-ops/stubs.

**Implementation update (2026-08-01):** `QT-GAP-005` and `QT-GAP-007` are further reduced.
`view.toggle_crosshair` now toggles state on the active figure's own native canvas, forwards that
state to the canvas's production `DataInteraction` overlay when the Vulkan surface is attached, and
requests a frame. Switching documents preserves each canvas's independent crosshair state. A focused
adapter regression covers active-document routing and isolation; a launched pixel-result fixture is
still required. Seven registered Qt commands remain explicit no-ops/stubs.

**Implementation update (2026-08-01):** `QT-GAP-005` is further reduced without claiming feature
parity: the unimplemented `panel.toggle_curve_editor` descriptor has been removed from the Qt
catalog, matching its already-hidden rail control. Menus, shortcuts, the command palette, and
automation no longer advertise a no-op. Curve/keyframe authoring remains an explicit `QT-GAP-008`
blocker. The only remaining registered no-ops are the six series commands.

**Implementation update (2026-08-01):** `QT-GAP-005` is further reduced. All six registered series
commands now have semantic Qt results. Selection is owned per canvas/document and synchronized
between pointer interaction, commands, the retained inspector context, and renderer highlighting.
Cycle, deselect, deep-copy copy/cut, bounded paste, and multi-selection delete operate on the active
document. Cut, delete, and paste produce undo/redo transactions and notify interaction/render state
before destroying live series pointers. Focused adapter regressions cover document isolation,
selection cleanup, multi-series delete undo/redo, clipboard type/data/style preservation, and paste
undo/redo. No registered Qt command is now an explicit empty stub. `QT-GAP-005` remains open for
descriptor/menu/shortcut state parity, deterministic dialog routing, integrated Help, launched
pointer-selection/highlight evidence, and the complete visible-control result matrix.

**Implementation update (2026-08-01):** `QT-GAP-001` is further reduced. Qt now implements
`fuzz_step`, `fuzz_reset`, and `list_fuzz_actions`; no advertised MCP method returns the old
frontend-specific not-implemented error. Legacy and Qt consume one authoritative 16-action weight
catalog. The Qt runner maps forced or seeded actions to its semantic command, figure lifecycle,
model mutation, input, split/detach, resize/move, and rendered-frame contracts. Figure switching,
closing, and detaching resolve the owning host instead of duplicating a detached document into the
main window. The live MCP regression enumerates the full catalog, proves seeded repeatability,
forces all 16 actions, asserts their model/window/command results, and checks unknown-action errors.
A production X11/lavapipe launch returned the 16-action catalog, reset seed 77, created figure 1
with an 83-point series, added a 132-point series, and reported the visible active figure with two
series before clean `app.quit` shutdown. The P0 gate remains open until the same complete
launched-process fixture is run unchanged against both frontends and its state, artifacts, pixels,
and failures are compared.

**Implementation update (2026-08-01):** `QT-GAP-004` and `QT-GAP-014` are materially reduced.
Workspace/autosave capture now embeds a lossless `FigureSerializer` payload for every figure,
including populated 2D/3D series data and styles, while retaining backward-compatible v5 metadata.
Restore validates and deserializes every payload before replacing any live state, releases canvases
before model destruction, clears pointer-bearing undo/timeline state, registers fresh figures, and
remaps saved IDs through main/detached window document assignments. Split serialization now
preserves every per-pane tab and active local tab rather than only one figure; primary-host
selection and document enumeration are deterministic. Focused unit/Qt tests cover in-memory and
disk data round-trip, corrupt-snapshot atomicity, multi-tab split round-trip, ID remapping, and Qt
bridge capture/restore. A production two-process X11/lavapipe run created a four-point line named
`restart-proof`, saved and exited, then started with an empty registry and restored the same label,
type, point count, and axis ranges through `file.load_workspace`. Qt now recursively rebuilds mixed
horizontal/vertical split trees as nested native `QSplitter`s and synchronizes dragged ratios back
to the serialized model. Direct out-of-service mutation tracking, detached-window restart, native
dock topology, and forced-crash interactive recovery remain open.

**Implementation update (2026-08-01):** `QT-GAP-019` is materially reduced. Qt no longer treats
the shortcut strings copied into command descriptors as an independent binding system. The shared
`ShortcutManager` now deterministically drives every live `QAction`, including multiple bindings,
and synchronizes the command metadata returned to menus, the palette, and automation. The native
Shortcuts panel has a real `QKeySequenceEdit` capture dialog; rebinding resolves conflicts,
immediately updates both the displaced and destination actions, and Reset restores the shared
defaults live. Workspace/autosave snapshots persist the complete binding/unbound-command set and
restore it after restart. Focused regressions cover multi-binding action synchronization and
conflict replacement. Cross-platform key-layout/modifier coverage and a launched rebind/restart
fixture remain open.

**Implementation update (2026-08-01):** `QT-GAP-004` and `QT-GAP-015` are further reduced. The
retained `ImGuiIntegration::build_ui` path already draws tooltip, legend, data-tip marker,
annotation, ROI, selection, crosshair, and measurement primitives into the native canvas's Vulkan
render pass; the unused `QtOverlayDrawList` is not the production overlay path. Workspace/autosave
capture now supplies each figure's live canvas-owned overlay snapshot to `FigureSerializer`, and
restore reapplies the active tool, crosshair, tooltip, markers, annotations, ROI bounds, completed
double-precision measurements, offsets/colors, and axes ownership to the new canvas after figure-ID
remapping. Native surface destruction captures the same snapshot before
tearing down `DataInteraction` and reapplies it after reattachment. Restoration remains atomic
across both figure and overlay vectors, and a multi-subplot regression fixes restored markers losing
their owning axes. Focused unit/Qt regressions cover two-figure overlay isolation, serialization,
corrupt-snapshot atomicity, axes ownership, ROI statistics reconstruction, completed-measurement
restoration, and pre-attachment queuing. Launched interaction/pixel evidence, annotation/ROI/
measurement editing and undo, and export remain open.

**Implementation update (2026-08-01):** `QT-GAP-004` dirty propagation is materially reduced. The
Qt autosave debounce now receives successful model/undo redraw transactions, figure lifecycle,
canvas release/wheel/key mutations, tool/crosshair changes, panel visibility, split topology,
settings, shortcut rebind/reset, and document moves from primary and detached hosts. Hover-only
movement and rendered frames do not dirty the workspace. Focused Qt regressions prove that active
tool, canvas overlay, and navigation-rail mutations reach the same registry-level persistence
callback. Timeline/keyframe authoring and mutations performed directly outside frontend services
still need explicit coverage; populated detached restart is proven while native dock-widget restart
remains unproved.

**Implementation update (2026-08-01):** `QT-GAP-004` forced-crash recovery is now proven on the
production Qt application under X11/lavapipe. Test-only environment policy can accept/decline the
otherwise unchanged native recovery prompt and shorten the autosave timers. A populated four-point
scatter labelled `crash-recovered` reached the configured autosave after its mutation debounce; the
process was terminated with `SIGKILL`; a second process accepted recovery and MCP reported one
active figure with the same scatter type, label, four points, and restored axis ranges. The run
exposed and fixed a cleanup bug: the controller had cleared the legacy static `/tmp` path instead
of `WorkspaceAutosave`'s configured user path, so clean exits could leave false crash prompts.
Cleanup is now path-owning/idempotent, recovery clears the correct file, and a successfully
completed shutdown removes any subsequently regenerated recovery snapshot. Dock-widget topology
and broader multi-window/platform placement remain persistence blockers.

**Implementation update (2026-08-01):** `QT-GAP-003` and `QT-GAP-004` detached restart are further
reduced by a production two-process X11/lavapipe fixture. The first process created two figures,
placed a three-point line labelled `detached-proof` in figure 2, transactionally detached that
document into the visible `Spectra — Figure` top-level window, and saved. The second process loaded
the workspace with fresh figure IDs; MCP reported both figures and the exact detached line
type/label/point count/ranges, while the X11 window tree again contained the primary `Spectra` and
detached `Spectra — Figure` top-level windows with their own Vulkan children. Cross-window drag and
native dock-widget topology, and multi-detached/multi-screen placement remain open.

**Implementation update (2026-08-01):** `QT-GAP-003` and `QT-GAP-014` are further reduced. A
detached pane now exposes **Move to Main Window**, and the existing `figure.move_to_window` command
uses the command target's actual owning host to toggle between detach and redock. Redock reuses the
same destination-first, rollback-safe registry transaction as cross-host move. An initial launched
run found that immediately deleting the emptied detached host could destroy the command sender
before dispatch returned; empty-host teardown is now deferred to the next event-loop turn. A focused
regression and a production X11/lavapipe run both move the populated `detached-proof` figure back to
the primary host, retain both figures and all line data, and remove the empty detached top-level
window. Cross-pane/cross-window drag-and-drop, native dock-widget topology, and broader
multi-window/platform placement remain open. The complete 9-test Qt-labelled suite passes in 3.94
seconds.

**Implementation update (2026-08-01):** `QT-GAP-013` is materially reduced. The Qt entry point now
initializes the shared native-dialog policy before `QApplication`, so automation cannot hang in a
modal file/color/number dialog. File commands, Export, Accessibility, Plugins, and Inspector route
through the injected `DialogService`; automation cancels safely unless an explicit title-scoped
path/value is supplied. PNG export and Copy Figure now consume real RGBA pixels read back from the
owning native Vulkan canvas, SVG writes directly through `SvgExporter`, and figure save/load uses
the selected path while preserving the canvas overlay snapshot. The Export panel uses the same
canvas callback for requested-size PNGs and supplies plugin callbacks with real requested-size RGBA
pixels plus non-empty lossless figure/workspace JSON instead of null pixels and an empty string.
Focused tests cover dialog cancellation/scripted values, panel delegation, and injected-path HTML
and WAV artifacts. A production X11/lavapipe fixture created a populated four-point line, wrote a
1208×612 RGBA PNG, SVG, and binary figure, advertised `image/png` on the X clipboard, then added a
temporary series and proved figure load restored only the original labelled line. Launched plugin
callback artifacts, cross-frontend byte/pixel comparisons, error/overwrite behavior, and the
remaining platform/DPR matrix remain open. The complete 9-test Qt-labelled suite passes in 4.10
seconds.

**Implementation update (2026-08-02):** `QT-GAP-009` and `QT-GAP-010` are further reduced for the command palette. The palette no longer paints category separators and disabled commands with hardcoded near-white and mid-gray brushes; it now derives separator and disabled-foreground colors from the shared `SpectraColors`/ThemeManager palette. The global application stylesheet also applies a `QDialog` background from the live theme, so the palette's client area follows the active shell. The 10 Qt-labelled tests and the full 168 non-GPU test suite pass; the launched X11/Wayland/DPR pixel verification of the command palette remains open.

**Implementation update (2026-08-02):** `QT-GAP-009` and `QT-GAP-010` are further reduced for native control indicators. The `QCheckBox::indicator:checked` and `QComboBox::down-arrow` stylesheet rules no longer use `image: none`; they now render checkmark and chevron icons from the embedded icon font, persisted to theme-colored PNGs and embedded as `url(file:///...)` references. The icons follow the active Night/Light palette and disabled state. The Qt test suite and visual regression tests continue to pass; `QSpinBox`/`QDoubleSpinBox` arrow indicators remain CSS triangles and are not yet converted.

**Implementation update (2026-08-02):** `QT-GAP-009` and `QT-GAP-010` are further reduced for `QSpinBox`/`QDoubleSpinBox` arrow indicators. The up/down arrow subcontrols now render chevron icons from the embedded icon font using the same `icon_image_url` path as the checkbox and combobox indicators. Disabled spin boxes use a dimmed icon, and the generated PNGs follow the active Night/Light palette. The Qt test suite continues to pass; launched X11/Wayland/DPR pixel verification of all native control indicators remains open.

## 1. Executive summary

This audit compared fresh builds of the legacy GLFW/ImGui application and `spectra-qt-app` through
the repository's [spectra-mcp](../.cursor/skills/spectra-mcp/) workflow, live X11/lavapipe sessions,
command and state transcripts, direct control activation, compositor screenshots, the Qt test suite,
and source inspection. It covers visible buttons and menus, their results, application state,
panels, graphics, automation, persistence, windows, input, plugins, and adapters.

The Qt frontend has made real infrastructure progress, but **command registration is far ahead of
feature implementation**:

- Legacy registers **83 command descriptors**. Qt currently exposes **86 descriptors**: all 83
  shared IDs plus three Qt-only IDs. This is still not behavior
  parity: **0 Qt commands are explicit empty stubs/no-ops**, but several handlers still bypass
  deterministic services or produce materially different results.
- The Qt MCP endpoint now implements **all 28 advertised MCP tools**. `pump_frames` renders the
  active native canvas and verifies exact progress, `wait_frames` advances only from successful
  render completions, and `dismiss_ui_capture` closes active Qt popups and releases widget/native
  grabs. Identical fuzz fixtures
  therefore cannot be executed against both frontends.
- Qt's automation capture path now defines canvas-only versus whole-window scope and captures
  composed screen pixels, including the embedded native Vulkan `QWindow` and owned top-level
  popups/dialogs. The adapter returns dimensions and real PNG base64 data. In a launched
  X11/lavapipe session, the 1280×720 whole-window output matched a simultaneous X11 root dump with
  zero differing pixels, while the 1208×612 canvas-only output contained the rendered axes.
  Wayland validation and multi-screen/DPR coverage are still required.
- The blank renderer-owned 2D plot is the strongest parity result: after normalizing theme, grid,
  and border, its crop differed in **0.092%** of pixels by more than two channel values, just inside
  the migration plan's 0.1% threshold. The complete blank shell differed by **11.750%**, the welcome
  windows by **91.553%**, and no populated series, overlays, 3D scene, or high-DPI/platform matrix
  could be compared through the common automation path.
- The visible Qt panels are not graphics-parity implementations. Native-white controls occupy
  **16.336%** of the Topics window, **6.452%** of Timeline, **5.293%** of Data Editor, and **3.626%**
  of Transforms. The command-palette dialog is **77.727%** near-white inside a dark application.
  Several combo arrows, checkbox marks, disabled states, and dock controls are missing or illegible.
- Timeline transport, scrub, duration, FPS, loop, active-document switching, production `on_frame`
  progression, native typed value/interpolation/tangent track/keyframe authoring, undo/redo, and
  workspace persistence now share per-figure state. Native tracks bind to persistent 2D/3D
  axes-limit and series-style paths. The native Curve Editor edits those same typed channels and
  tangent handles. Launched artifact comparison remains absent; Markers
  are hidden; Topics is a
  generic data-source list rather than the legacy ROS/PX4 workflow; and plugins, transforms,
  splits, advanced input, and full cross-platform artifact parity remain partial or unsafe.
- Document close/move/detach/redock now has a central rollback-safe path and direct Qt test coverage.
  Workspace load recreates lossless populated figures and saved document assignments across a real
  process restart; model redraw transactions, figure/document lifecycle, canvas interaction, tools,
  overlays, panels, splits, settings, and shortcuts mark autosave dirty; periodic autosave runs in
  the Qt event loop; forced-crash recovery and populated detached restart are production-proven.
  Direct out-of-service mutations, native dock-widget topology, and broader platform placement are
  not yet proven.
- The Inspector's live size and axes summary now follows the active rendered model, includes both
  2D and 3D axes, and rebuilds when axes/series topology changes. Figure renames synchronize both
  Qt tab systems immediately and after rendered external/undo changes. The complete legacy
  property/data surface remains open.
- All **9/9 Qt-labelled tests pass**. The panel suite now renders and compares Night/Light custom
  shell surfaces, but the dedicated visual test still does not include a Vulkan canvas or opened
  panels. The automation test now verifies the capture scopes/base64 payload, the five earlier
  semantic methods, seven input methods, and deterministic semantic results for all 16 fuzz
  actions, but it still is not an unchanged cross-frontend launched-process parity fixture.

The audit also found current defects in the legacy reference: its MCP model-only figure creation
does not attach a visible document, requested scatter data is reported as line data, screenshot
capture reports success without writing a file, and `panel.toggle_inspector` reproducibly crashes
after creating a figure. These defects must be fixed or normalized, but they do not reduce the Qt
acceptance scope.

## 2. Scope, environment, and method

### 2.1 Environment

- GCC/Ninja release build, `SPECTRA_USE_ROS2=OFF`, tests/examples/golden tests enabled.
- Fresh successful build of `spectra` and `spectra-qt-app`.
- Xvfb display `:99`, 1280×720 requested client size, lavapipe software Vulkan,
  `SPECTRA_RUNTIME_MODE=inproc`, and `SPECTRA_AUTOMATION=1`.
- Qt additionally required X11 selection (`GDK_BACKEND=x11`, `XDG_SESSION_TYPE=x11`, and no inherited
  `WAYLAND_DISPLAY`) in this environment.
- Verification command: `ctest --test-dir build -L qt --output-on-failure`; all nine tests passed.
- The working tree was already dirty and contained Qt/ImGui changes before the audit. It was not
  cleaned or reset. Results describe the exact working tree above, not committed `HEAD` alone.

### 2.2 Procedure

1. Launch each frontend with the documented `spectra-mcp` runtime and query its live endpoint.
2. Record all command descriptors, available MCP methods, menus, state, figures, and command results.
3. Exercise welcome, figure creation, line/scatter model creation, panels, settings, themes, tools,
   tabs, splits/windows, menus, palette, and capture methods wherever each endpoint allowed.
4. Capture both application-returned images and compositor/root images. The latter are authoritative
   for native child windows, popups, and dialogs omitted by application grabs.
5. Compare normalized blank plot, complete shell, welcome, panel, and capture-contract images.
6. Trace every visible Qt rail/header/status control and the principal panel actions to their
   handlers, models, persistence paths, and tests.
7. Separate failures in the legacy reference from Qt gaps rather than treating a broken baseline as
   achieved parity.

The temporary transcripts and captures are under
`/tmp/spectra-parity-audit-20260726/`. They are diagnostic artifacts, not checked-in golden files.

### 2.3 Limitations

- At audit time Qt could not add a series or query figure details through MCP. Those two model-level
  operations are now implemented and shared with legacy, but populated renderer comparisons,
  multi-axis, 3D, overlay, and animation fixtures have not yet been rerun and remain **unverified**.
- Qt MCP can now synthesize mouse movement/click/drag/scroll, shared key codes, text commits, and
  double-clicks against widgets and the embedded native canvas. Complete input result fixtures,
  pixel-delta/phase scrolling, IME composition, touch/tablet/gesture, drag/drop, and focus-transition
  coverage remain open.
- ROS2 was disabled. The report records the explicit Qt placeholders and generic-source mismatch,
  but no live ROS2/PX4 topic session was run.
- This pass covers Linux/X11 only. Wayland, Windows, macOS, touch/tablet, IME, screen reader, and 200%
  DPR remain open acceptance work.
- Xvfb popup painting was inconsistent for some small `QMenu` windows. Menu ownership and action
  population findings below are source-confirmed; exact pixel appearance of those small popups is
  not used as the sole evidence.

## 3. Severity

| Severity | Meaning |
|---|---|
| P0 | Default-switch blocker: automation cannot establish parity, or a core workflow risks crash, loss, duplication, or unrecoverable state. |
| P1 | Major missing, disconnected, misleading, or materially divergent user workflow or visual surface. |
| P2 | Partial fidelity, accessibility, consistency, test, or maintainability gap required for sign-off. |
| P3 | Secondary polish or diagnostics issue. |

## 4. Blocking findings

| ID | Sev. | Finding | Evidence/result | Required closure |
|---|---:|---|---|---|
| QT-GAP-001 | P0 | Common automation contract is incomplete | **Partial:** all 28 methods now have Qt implementations, including a shared-catalog seeded/forced fuzz runner, scoped capture, popup/grab dismissal, truthful native-canvas `pump_frames`, and rendered-progress-based `wait_frames`. The unchanged cross-frontend launched fixture and equality assertions remain absent. | Run the same complete MCP fixture against both frontends and assert equivalent state, UI, artifacts, and errors. |
| QT-GAP-002 | P0 | Qt screenshots are not truthful | **Partial:** canvas and window capture now have distinct scopes; composed screen capture includes native canvas content and owned popups/dialogs, returns dimensions, and backs base64 PNG output. A launched X11/lavapipe 1280×720 capture matched an external root dump exactly; Wayland, multi-screen, and DPR validation remain open. | Prove the new capture path across the remaining required platform/DPR matrix. |
| QT-GAP-003 | P0 | Document close/move state is unsafe | **Partial:** close/move/detach/redock and cross-window drop share a destination-first rollback-safe controller. Production fixtures preserve a populated document through detached restart and command/context-menu redock into the primary host, including safe deferred teardown of the empty source host. Cross-pane drag preserves the live canvas/input; cross-window drag transfers overlay/tool/selection state without duplicating the model. Launched OS drag and broader placement coverage remain open. | Prove drag rollback and multi-window placement across the required platform matrix, and retain restart/redock coverage. |
| QT-GAP-004 | P0 | Workspace/autosave/recovery cannot restore a session | **Partial:** lossless figure plus per-canvas tool/crosshair/tooltip/marker/annotation/ROI/measurement snapshots, deterministic main/detached document assignments, nested pane trees, active pane tabs, native dock areas/tab groups/visibility/floating state/window geometry, and fresh-ID remapping restore missing models. Frontend model, lifecycle, canvas, panel, dock, split, settings, and shortcut mutations feed autosave. Production launched fixtures preserve populated data through manual restart, forced `SIGKILL` recovery, and detached-window recreation; clean shutdown clears the configured recovery file. Direct out-of-service mutations and broader multi-window/platform placement remain unproven. | Round-trip broader multi-window/platform placement and retain explicit dirty coverage for each new mutation surface. |
| QT-GAP-005 | P1 | Registered commands overstate implemented behavior | **Partial:** no registered descriptor is an explicit empty stub. Series selection/clipboard/removal, animation transport, function plotting, transient cancellation, and per-canvas crosshair state have semantic results; unavailable Curve Editor is hidden and excluded from Qt descriptors. Help and several file/dialog actions still bypass deterministic application UI, and the complete result/state matrix is not proven. | Every visible/registered command must have a tested semantic result or be disabled/hidden and excluded from parity counts. |
| QT-GAP-006 | P1 | Menu hierarchy is incomplete and contradictory | **Partial:** command-ID routing places lifecycle, Help, animation, theme, series, and accessibility actions under the intended menus. Panels and Splits contain each registered semantic `QAction` exactly once with deterministic ordering; check state follows live panel visibility. Axes exposes semantic X/Y/Z/All linking and unlink. Transforms dynamically mirrors the shared registry, applies named transforms to all visible editable series through undo/redraw/autosave, and opens the formula editor. The legacy and Qt top-level/order/descriptor models still differ. | Bind a shared ordered menu model with identical categories, labels, shortcuts, enable/check state, and results, then prove them in launched cross-frontend fixtures. |
| QT-GAP-007 | P1 | Visible controls are inert or misleading | **Partial:** Home triggers `view.home`; reset layout resets panel toggle state; rail tools execute shared commands and restore per-document tool state; Markers reflects and clears only active-canvas data tips. Figure titles synchronize across both tab systems. Zoom uses the legacy range calculation; cursor uses hit-tested 2D data coordinates; cursor/FPS/GPU/zoom updates are scoped to the active canvas. The remaining visible-control result matrix is incomplete. | Every visible control must execute one semantic command and reflect authoritative state. |
| QT-GAP-008 | P1 | Timeline and curve editing are incomplete | **Partial:** transport, all six animation commands, per-canvas playback, `FigureAnimState::on_frame`, native typed value/interpolation/tangent track/keyframe CRUD/selection/visibility/lock, stable 2D/3D axes/series property binding, graphical multi-channel curve/keyframe/tangent editing, shared undo/redo, and per-figure workspace/autosave/recovery serialization use one editor/interpolator across active-document switches and detached hosts. Launched cross-frontend restart and approved visual artifacts remain absent. | Prove authored curves, bound model results, restart state, and pixels in launched cross-frontend fixtures. |
| QT-GAP-009 | P1 | Panel graphics do not follow the dark shell/design system | **Partial:** Stylesheet now covers QListWidget, QTableWidget, QPlainTextEdit/QTextEdit, QRadioButton, QProgressBar, QSlider, QToolTip, disabled states, QComboBox down-arrows, QSpinBox up/down arrows, QMenu checkable indicators, QDockWidget close/float buttons, and central container/canvas frame backgrounds. QCheckBox, QComboBox, and QSpinBox/QDoubleSpinBox indicators now render theme-colored icon-font PNGs instead of CSS shapes. Native and custom surfaces now consume the live theme palette; approved golden image validation remains open. | Theme every state/control and pass approved panel goldens at all reference sizes/DPRs. |
| QT-GAP-010 | P1 | Settings/theme behavior is disconnected | **Partial:** panel visibility round-trips through the live shell and persistence. Renderer, application stylesheet, custom-painted shell, panels, detached windows, and popups now consume the shared live theme; Settings/commands synchronize and persist the selected default. Imported/custom-theme refresh coverage and the full visual platform/DPR matrix remain open. | Prove every supported theme and settings path against approved renderer/chrome/panel goldens and persistence/restart fixtures. |
| QT-GAP-011 | P1 | Inspector reports contradictory/stale state | **Fixed:** successful active-canvas frames synchronize the live size and control values; one unified non-null traversal drives counts and tabs for 2D/3D axes; topology changes rebuild safely; native 3D controls read and mutate the authoritative model. Focused stale-size/mixed-axis/topology/3D-control regressions pass. | Retain the synchronization regressions. Broader Inspector feature parity remains under QT-GAP-007/QT-GAP-012. |
| QT-GAP-012 | P1 | Data, transforms, topics, and plugins are prototypes | **Partial:** Data Editor 2D/3D cell/row/clipboard/selected-series file workflows, bounded large-series pagination, precise absolute datetime/epoch X presentation, and mapped shared-X/multi-Y or shared-XZ/multi-Y series creation use injected paths, atomic validation, topology-aware shared undo/redo, redraw, live refresh, autosave dirty coverage, and verified CSV artifacts. Transforms now provide explicit all-visible, axes-wide, single-series, or arbitrary exact multi-series targets, editable ordered/enabled pipelines, non-destructive built-in/custom-formula previews, shared-parser validation and cross-series expressions, scoped undo, datetime-offset preservation, per-figure lossless pipeline state, provider provenance, and unavailable custom-step recovery. Portable plugin schemas render nested groups and deliver exact stable callback IDs/values with returned-value reconciliation; management covers default/custom scans, lifecycle controls, manifest capabilities, and diagnostics. ROS workflows and launched plugin artifacts/errors remain incomplete. | Match every legacy result, validation, undo, persistence, error, and artifact path. |
| QT-GAP-013 | P1 | Export and dialogs are not automation-safe | **Partial:** Qt file/color/number surfaces use the injected dialog policy and cancel deterministically under automation unless explicitly scripted. Production Vulkan readback now drives PNG/clipboard output; SVG and lossless figure paths write directly; HTML/WAV injected-path artifacts are tested; plugin exports receive real requested-size RGBA and non-empty figure JSON. A launched fixture verifies PNG, SVG, clipboard MIME, figure save/load, and data restoration. Launched plugin artifacts, error/overwrite behavior, cross-frontend comparison, and platform/DPR coverage remain open. | Verify real plugin callbacks and the complete artifact/cancel/error matrix unchanged across both frontends and required platforms. |
| QT-GAP-014 | P1 | Splits/docks cannot preserve arbitrary topology | **Partial:** mixed nested split trees, ratios, every pane tab, active local tabs/order, ID remapping, focused native-canvas pane activation, cross-pane/window drag, and native dock area/tab/visibility/floating topology now round-trip. Transactional command/context-menu/drop movement preserves populated documents and canvas-owned overlay/tool/selection state, but custom/internal tabs still coexist and launched platform drag placement is absent. | Prove pane/window drag placement through complete launched save/load fixtures on the platform matrix. |
| QT-GAP-015 | P1 | Overlay/tool parity is not demonstrated | **Partial:** native Vulkan frames execute the retained production overlay draw path; active tool, crosshair, tooltip setting, data-tip markers, annotations, ROI bounds/statistics, and completed measurements now preserve per-canvas identity, axes ownership, native-surface lifetime, and workspace recreation. Markers is visible and clears active-canvas data tips with truthful rail/persistence state. Launched pixels/results, editing/undo, and export are absent. | Exercise select, crosshair, tooltip, legend, markers, measure, annotate, ROI, and data tips end to end, including editing, undo, and artifacts. |
| QT-GAP-016 | P1 | Input coverage is incomplete | **Partial:** MCP pointer/button/drag/wheel/key/text/double-click events now reach widgets and the native canvas. Custom shell navigation/window/dock controls are named and focusable; document tabs support visible keyboard selection plus close/add/detach and model-safe pane/window DnD. Pixel scrolling/phases, IME composition, touch/tablet/gesture, launched platform DnD, full focus transitions, child-level tab roles, and complete platform key coverage remain open. | Implement and test the platform input matrix, including QAction/canvas/text focus arbitration. |
| QT-GAP-017 | P1 | ROS2/PX4 Qt UI remains placeholder/generic | Display render area is explicitly marked placeholder; inspector is label-only; Topics is a generic registry browser. | Port all supported discovery, QoS, plot, display, inspector, bag/ULog, reconnect, and error workflows. |
| QT-GAP-018 | P1 | Full graphics parity is unestablished | Only a blank 2D crop passes narrowly; shell, welcome, panels, series, overlays, 3D, DPR, and platform baselines remain. | Pass the complete approved visual matrix and store numerical failure artifacts. |
| QT-GAP-019 | P2 | Shortcut/action state can diverge | **Partial:** one shared manager now drives live QAction bindings and exposed command metadata; native capture, conflict replacement, reset, and workspace persistence are implemented. Label/category/check-state parity plus platform key-layout/modifier and launched restart coverage remain open. | Compare and test the remaining label/category/enabled/check state and platform key behavior in launched cross-frontend rebind/reset/restart fixtures. |
| QT-GAP-020 | P2 | Current tests cannot detect the observed failures | Visual grab excludes runtime canvas/panels; automation treats structured error as success; action tests mostly prove descriptor creation. | Replace smoke assertions with launched-process semantic, artifact, state, and image comparisons. |

## 5. Automation parity

### 5.1 Endpoint and tool results

Both live applications answered on the configured endpoint. At audit time Qt logged 86 registered commands and its
event-loop dispatch no longer timed out. The remaining difference is semantic:

| MCP result | Legacy | Qt |
|---|---|---|
| `ping`, `get_state`, `list_commands`, `execute_command` | Implemented | Implemented |
| `create_figure` | Mutates registry, but does not attach a visible document | Creates a visible Qt figure when using the semantic command; direct tool is implemented |
| `set_window_size`, `resize_window` | Implemented | Implemented |
| `capture_window`, `capture_screenshot` | Reports success, but the observed screenshot file was not written | Distinct whole-window/canvas scopes capture composed pixels and verify PNG output; launched X11 equivalence passes |
| `list_menus` | Implemented, but only reports File/Edit/View/Tools/Plot | Implemented from the nine visible Qt menus with action enabled/checkable state |
| `mouse_move`, `mouse_click`, `mouse_drag`, `scroll` | Implemented | Implemented through synchronous Qt events, including native-canvas targeting |
| `key_press`, `text_input`, `double_click` | Implemented | Implemented for focused Qt widgets/native canvas with shared key/modifier translation |
| `switch_figure`, `add_series`, `get_figure_info` | Implemented with legacy defects noted in section 13 | Implemented; series mutation and figure serialization share the legacy operations |
| `get_screenshot_base64` | Implemented | Implemented from the same whole-window composed PNG capture |
| `fuzz_step`, `fuzz_reset`, `list_fuzz_actions` | Implemented | Implemented from the shared 16-action catalog with seeded/forced Qt semantic dispatch |
| `list_methods` | Implemented | Implemented from the shared automation handler catalog |
| `pump_frames` | Pumps legacy frames | Synchronously renders the active native canvas, returns the observed count, and errors if exact progress is impossible |
| `wait_frames` | Waits for frame progress | Deferred by the shared queue and completed only after the requested number of successful Qt renders |
| `dismiss_ui_capture` | Dismisses capture UI | Closes active Qt popups and releases widget keyboard/mouse plus native-canvas mouse grabs |

No advertised Qt MCP method now returns a frontend-specific not-implemented response. This closes
the catalog/dispatch omission, not the required unchanged launched-process equality fixture.

### 5.2 Capture contract

The audit's old QWidget-only path produced byte-identical `capture_window` and
`capture_screenshot` files, omitted the Vulkan child and top-level UI, and differed from the
simultaneous compositor capture in **80.432%** of pixels by more than two channel values.

The replacement path now:

- maps `capture_screenshot` to the active native canvas bounds;
- maps `capture_window` and `get_screenshot_base64` to the main window plus visible owned
  top-level popup/dialog bounds;
- captures composed screen pixels so the embedded native canvas is present;
- verifies PNG encoding/writes before success and returns the actual pixel dimensions and scope.

Adapter-level tests prove scope dispatch, dimensions, non-empty base64 data, non-blocking menu
activation, and popup dismissal. A launched Qt X11/lavapipe session then produced a 1208×612 canvas
PNG, a 1280×720 whole-window PNG, and a 1280×720 base64 PNG payload. The canvas contained a
populated Vulkan line plot with axes and legend, the whole-window capture included separate
command-palette and File-menu surfaces, and the static whole-window image matched a simultaneous
`xwd` root capture with **0 pixels differing** and zero mean absolute channel error. The File-menu
MCP click returned without blocking, and dismissal reported `"popup":true`. Wayland, mixed-screen,
and 200% DPR validation are still required before this P0 gap can be closed.

## 6. Command, menu, shortcut, and button parity

### 6.1 Descriptor inventory

| Metric | Legacy | Qt |
|---|---:|---:|
| Registered command IDs | 83 | 86 |
| Shared IDs | 83 | 83 |
| Legacy-only IDs | 0 | 0 |
| Qt-only IDs | 0 | 3 |

Qt-only IDs are `app.quit`, `file.export`, and `view.reset_layout`.
All legacy IDs, including `panel.toggle_curve_editor`, now have registered Qt counterparts.

Qt category counts are:

| Category | Count | Category | Count |
|---|---:|---|---:|
| Accessibility | 1 | Animation | 6 |
| App | 5 | Data | 2 |
| Edit | 2 | Figure | 13 |
| File | 9 | Panel | 2 |
| Plot | 5 | Series | 6 |
| Theme | 4 | Tools | 6 |
| View | 25 |  |  |

Source inspection finds no registered Qt command whose handler is an explicit empty stub. The six
series descriptors now operate the active canvas/model with shared selection, clipboard, and
undo/redo transactions. A launched pointer-selection and renderer-highlight fixture remains open.

Additional descriptor-to-result divergences:

- All six animation commands and the visible `QtTimelineWidget` resolve the same active per-figure
  editor. The canvas tick advances it and invokes the figure's production animation callback.
- Theme commands update the renderer and all Qt shell/panel surfaces, synchronize the Settings
  control, and persist the selected default.
- `help.show` launches an external URL through `xdg-open`; there is no integrated Help surface and
  the visible Help menu is empty.
- File, color, and numeric actions use the injected dialog service. Automation cancels them without
  a modal surface unless a title-scoped deterministic path/value is supplied.
- HTML/WAV outputs use injected user-selected paths and focused tests verify both artifacts.
- `figure.move_to_window` now uses the same validated, rollback-safe detach transaction as the tab
  action.

### 6.2 Shared descriptor mismatches

| Command | Legacy descriptor | Qt descriptor | Gap |
|---|---|---|---|
| `figure.tab_1` … `figure.tab_9` | `1` … `9` | `1` … `9` from the shared manager | Shortcut aligned; label/category result comparison remains |
| `file.export_png` | `Ctrl+S` | `Ctrl+S` from the shared manager | Shortcut and real artifact path aligned; cross-frontend pixels/platforms remain |
| `file.export_svg` | `Ctrl+Shift+S` | `Ctrl+Shift+S` from the shared manager | Shortcut and real artifact path aligned; cross-frontend bytes remain |
| `panel.open_settings` | Panel, no shortcut | View, no live shortcut | Category differs |
| `panel.toggle_data_editor` | Panel, no shortcut | View, no live shortcut | Category differs |
| `panel.toggle_inspector` | Legacy label/category, no shortcut | Different label/category, no live shortcut | Label/category differ |
| `panel.toggle_timeline` | Legacy label/category | Different label/category, shared `T` binding | Label/category differ |
| `panel.toggle_topics` | Legacy label/category | Different label/category | Descriptor differs |
| Tool modes | Shared defaults | Qt live actions consume the same shared defaults | Shortcut binding aligned; exact tool results remain |
| `view.autofit` | “Auto-Fit Active Axes”, `A` | “Auto-Fit Active Figure”, live `A` | Label/scope differ |
| `view.fullscreen` | Canvas label, `F` | Generic label, live `F` | Label differs |

### 6.3 Menu hierarchy and live result

The Qt custom header now uses each registered action once, but it does not yet build menus from the
same ordered hierarchy as legacy:

| Menu | Qt population/result | Parity gap |
|---|---|---|
| File | Figure lifecycle, file operations, new-window/palette, and quit actions route here | Exact legacy grouping, ordering, enable state, and labels remain different |
| Edit | Undo/redo and series selection/clipboard/removal actions route here | Exact legacy ordering/check state remains unproved |
| View | View actions plus Panels and Splits submenus; each registered action appears exactly once | Shared cross-frontend order and complete panel command coverage remain open |
| Tools | Interaction tools plus Theme and Animation submenus and accessibility actions | Exact legacy hierarchy/order remains different |
| Plot | Only plot/reference/function commands route here | Function workflow works; exact legacy ordering remains unproved |
| Data | Copy/export data actions | Still not the legacy import/data workflow |
| Axes | Link X/Y/Z/All and Unlink All act on active 2D/3D topology through the shared manager; unavailable dimensions disable before activation. | Launched pan/zoom propagation and restart comparison remain open. |
| Transforms | Dynamically lists the shared transform registry; named actions apply immediately to all visible editable 2D series through shared undo/redraw/autosave, while Custom Formula opens and focuses the native editor | Shared ordering/label/enable state and launched plugin/result comparison remain open |
| Help | `help.show` routes here | Integrated in-application Help remains incomplete |

The routed hierarchy is deterministic by category and command ID, and local duplicate panel/split
actions have been removed. A shared explicit order/check-state model consumed unchanged by both
frontends is still required. Several smaller popup surfaces appeared black/blank under Xvfb; because
of the Xvfb caveat, approved capture—not that paint artifact—is still required.

The custom menu buttons now use non-blocking `QMenu::popup()`. A production MCP click returned
immediately with the File menu still visible, enabling truthful capture and
`dismiss_ui_capture`; the hierarchy/content mismatches above remain open.

Qt MCP now reports this hierarchy and action state. An identical cross-frontend ordering/result
comparison is still required.

### 6.4 Header, rail, tabs, and status

| Visible control | Observed Qt result | Status |
|---|---|---|
| Header `+` | Creates a figure through `figure.new` | Basic pass |
| Custom document tab select | Activates visible tab | Basic pass |
| Custom tab close | Routes through the central document lifecycle and unregisters the figure once | Adapter-level pass |
| Rename in Inspector | Mutates the model and synchronizes the custom document strip and native pane tab; rendered external/undo changes also resynchronize | Adapter-level pass |
| Home | Executes the shared `view.home` action | Basic pass |
| Select/Pan/Zoom/Measure/Annotate/ROI | Execute shared commands; rail/status reflect the active canvas's tool per document; retained production overlays render in the native Vulkan pass; completed ROI/measurement/annotation state persists | Adapter-level pass; launched results/pixels and edit/undo remain unverified |
| Markers | Visible; active state follows the current canvas's persistent data tips, and click clears only that canvas through the retained overlay with redraw/autosave notification | Launched placement/removal pixels, editing/undo, and export remain open |
| Curve Editor | Opens the native multi-channel graphical curve/tangent editor through the shared command | Launched restart and visual artifact comparison remain open |
| Transform/Inspector/Timeline/Plugins/Topics/Settings | Inspector, Timeline, Curve Editor, Plugins, Topics, and Settings dispatch their shared registered actions without overwriting the active tool; Transform opens the native transform dock because no shared legacy command exists | Shared Transform descriptor and launched all-entry result matrix remain open |
| Help rail item | Hidden | Feature absent |
| Reset Layout | Hides docks and resets panel toggle check states | Basic pass |
| Status cursor/zoom/fps/GPU fields | Active-canvas-only updates; zoom matches the legacy range calculation and restores per document; cursor uses DPR-aware, axes-hit-tested 2D data coordinates and clears outside axes | Adapter-level pass; launched visual/input fixture pending |
| Status tool field | Reflects the active canvas's `InputHandler` and restores per-document state | Adapter-level pass |

The Home tooltip advertises `Ctrl+H` while the registered `view.home` shortcut is `Home`.

## 7. Shell and graphics comparison

### 7.1 Measured results

Measurements use the live 1280×720 reference session unless stated otherwise.

| Comparison | Pixels differing >2 | Pixels differing >10 | Mean absolute channel error | Result |
|---|---:|---:|---:|---|
| Normalized blank renderer-owned plot crop | 0.092% | 0.012% | 0.008 | Narrow blank-2D pass |
| Normalized blank complete shell | 11.750% | 4.969% | 2.433 | Fail |
| Welcome complete window | 91.553% | 14.394% | 6.660 | Fail |
| Qt MCP grab versus actual Qt compositor image | 80.432% | 73.613% | 4.759 | Capture-contract fail |

The normalized plot crop was `(141,113)-(1260,627)`, 1119×514. It is valid evidence only for this
run. Production goldens must derive the physical renderer rectangle from runtime state, not preserve
hard-coded coordinates.

### 7.2 Welcome and chrome

| Legacy | Qt |
|---|---|
| Large Spectra logo, product name/subtitle, `Ctrl+T` hint, and version `v0.3.0` | Text-only name/subtitle, generic hint, no logo, no version, no `Ctrl+T` cue |
| Welcome content owns the empty state without persistent navigation/document/status chrome | Navigation rail, empty document strip, and status chrome remain visible |
| Compact renderer-centered composition | Different spacing, alignment, and hierarchy |

### 7.3 Panel graphics measurements

Near-white pixel share in the complete Qt window after opening each panel:

| Qt surface | Near-white share | Main visible cause |
|---|---:|---|
| Topics | 16.336% | Large native-white source list |
| Timeline | 6.452% | Large native-white Tracks list |
| Data Editor | 5.293% | Native-white table |
| Transforms | 3.626% | Native-white pipeline list |
| Command Palette dialog | 77.727% | White results/category surface |
| Clean shell | ~0.002% | Baseline |

These are not subtle antialiasing differences; they are unthemed widget regions inside the dark
shell.

Other observed graphics gaps:

- `QComboBox` arrows are missing or indistinguishable in Settings, Transforms, and Export.
- Checked settings boxes render as a solid purple square without a recognizable checkmark.
- Disabled states are not visually clear. Timeline looks interactive despite the entire widget being
  disabled, and transform actions look enabled without a selected target or pipeline.
- Dock title-bar close/undock icons are explicitly styled away, leaving no visible window-management
  controls.
- Left docks move the Spectra header and navigation rail to the right; right/bottom docks compress
  the canvas. This does not match the legacy overlay/drawer composition.
- Night/Light shell switching is covered by a rendered header/rail luminance regression, but full
  panel/dialog goldens at every required size, DPR, and platform remain open.
- The command palette combines a dark input with pale category bands and a nearly all-white results
  area. It is also a separate top-level dialog omitted from MCP captures.
- The shell stylesheet now covers the audited standard-widget fallbacks and is generated from the
  shared theme palette; pixel approval of every widget state remains open.
- Compact mode below 1100px only narrows the rail; the inspector-overlay behavior remains a TODO and
  docks can crush the canvas.

## 8. Panel-by-panel behavior and result comparison

| Panel/workflow | Observed Qt result | Gap |
|---|---|---|
| Inspector | Opens and edits 2D plus native 3D title/XYZ/grid/bounds/series controls. Live size and controls synchronize after successful active-canvas frames, axes/series topology changes rebuild from a unified model traversal, and rename updates both tab systems. | Audited stale size/count, missing-3D-tab, and title-propagation defects are fixed; complete legacy property/data parity remains open. |
| Timeline | Enabled transport, scrubber, duration, FPS, loop, frame labels, track CRUD/visibility/lock, typed numeric keyframe value, seven-mode interpolation, four-mode tangent, explicit in/out-handle authoring, and stable 2D/3D axes/series property targets follow the active figure's shared editor/interpolator. Canvas ticks apply bound values and drive production `on_frame`; mutations use shared undo/redo and per-figure workspace/autosave/recovery serialization. | Launched restart and visual artifact comparison remain open. |
| Curve Editor | Native dock paints multi-channel sampled curves, keyframes, tangent handles, grid/bounds, and playhead; fit/reset, zoom/pan, click/box selection, drag, delete, cancel, shared undo, and active-document switching operate the Timeline's authoritative channels. | Launched restart, interaction pixels, and cross-frontend visual artifact comparison remain open. |
| Data Editor | Edits line/scatter X/Y or X/Y/Z cells with absolute datetime/epoch X presentation and bounded 1,000-row pages; appends, deletes, and reorders rows; pastes rectangular TSV/CSV; replaces the selected 2D series from a two-column file; maps shared-X/multiple-Y columns into named 2D series or shared-XZ/multiple-Y columns into named 3D series; exports matching two- or three-column CSV through injected paths and topology/data-aware undo/redraw/autosave notification; and presents compact empty/recovery states instead of a blank grid. | Core data workflows are implemented; approved visual parity remains open. |
| Topics | Dock title is “Data Sources”; lists generic `DataSourceRegistry` entries. Start/Stop are disabled without selection. | Not the legacy ROS/PX4 browse/filter/QoS/topic-to-series workflow; large white list and weak disabled styling. |
| Transforms | Applies operations, editable ordered/enabled pipelines, and shared-parser custom formulas to all visible editable series, one axes, one named series, or an arbitrary exact checked set across axes; previews affected series/points and output Y bounds without mutation; uses scoped undo/redraw/autosave notifications; preserves absolute datetime X offsets; and round-trips per-figure pipelines with targets/full parameters, provider provenance, visible unavailable custom steps, and in-place recovery when providers return. | Broader launched plugin lifecycle and cross-frontend artifact/error comparison remain open. |
| Settings | Theme, palette, and three visibility checkboxes. | Visibility and theme now round-trip through the live shell and persistence; theme reaches renderer, native widgets, custom-painted chrome, detached windows, and popups. Full control-state goldens remain open. |
| Shortcuts | Lists the shared bindings, captures native key sequences, replaces conflicts, resets defaults, and updates live actions/metadata. | Cross-platform layout/modifier behavior and launched rebind/restart parity remain open. |
| Plugins | Load Plugin, default/custom directory scans, loaded-plugin enable/unload controls, manifest/API/capability/health diagnostics, and portable typed schema panels. Plugin loading uses the injected dialog service; nested schema groups render recursively; property/action callbacks receive exact stable schema and element IDs/values with returned-value reconciliation; and export callbacks receive real pixels plus serialized figure state. | Launched plugin artifact/error coverage remains absent. |
| Export | Right dock with built-in PNG plus plugin formats, width/height/path/Browse/Export. Production callbacks use owning-canvas Vulkan readback, requested dimensions, figure JSON, and injected paths. | Combo affordance remains unclear; launched plugin and complete cancel/error/overwrite/cross-frontend matrix remain open. |
| Command Palette | Search dialog lists command descriptors; direct Escape and shared `app.cancel` both close it. | Top-level-surface capture and approved themed visual evidence remain open. |
| ROS2 display/inspector | Source contains “Display render area — plugin auxiliary UI” placeholder and label-only inspector information. | Placeholder, not parity. |

## 9. Functional parity matrix

| Area | Qt result | State |
|---|---|---|
| Figure create/activate | Basic semantic path works | Partial |
| Figure close/rename/reopen | Close uses the transactional registry path and rename updates both Qt tab systems; reopen restoration is not established | Partial; reopen/restart remains blocking |
| CSV/data import | No equivalent verified Qt workflow | Missing |
| Series add/remove/reorder/copy/cut/paste | MCP add-series works; active-document cycle/copy/cut/paste/delete/deselect use per-canvas selection and undoable clipboard/removal transactions. Reorder and launched pointer/highlight equivalence remain unproved. | Partial improvement |
| 2D render | Blank axes render and narrow crop matches | Blank-only pass |
| 2D pan/wheel/box/reset/fit | Tool routing and some commands exist; exact state/result matrix unavailable | Partial/unverified |
| Axis linking X/Y/Z/all | Native Axes actions create exact 2D/3D link groups; every canvas input handler propagates limits through one shared manager, and 2D/3D groups persist across workspace recreation. | Focused implementation pass; launched pointer/wheel and restart evidence remains open. |
| Plot helpers | H/V/zero lines and a shared-parser native function editor mutate the model; Plot grouping remains wrong | Partial |
| Legend/grid/border | Some handlers/inspector controls exist | Partial; interaction/fidelity unverified |
| Crosshair/markers/data tips | The retained production overlay renders in the native Vulkan pass; crosshair/tooltip/marker state survives per-document workspace recreation and native-surface reattachment with marker axes ownership intact. The Markers rail state follows and clears only the active canvas. Launched placement/removal pixels/interactions remain unproved. | Partial improvement |
| Measure/annotate/ROI/select | Retained overlays and tool input are connected; annotation objects, completed ROI bounds/statistics, measurement endpoints, and active tool persist per canvas through workspace recreation. Launched results/pixels, editing/undo, and export remain unverified. | Partial improvement |
| 3D scene/camera/input/inspector | Shared renderer may support drawing; Qt shell/input workflow not compared | Unverified |
| Undo/redo | Inspector/data/transform, series cut/delete/paste, and Timeline track/keyframe mutations have transaction coverage | Partial improvement |
| Timeline/keyframes/curves | Timeline transport, canvas progression, native typed value/interpolation/tangent track/keyframe CRUD/selection, persistent 2D/3D property binding, graphical curve/tangent editing, undo/redo, and per-figure serialization share authoritative state | Partial; launched restart and approved artifact comparison missing |
| Theme/palette | Renderer, native widgets, custom shell, Settings, and commands share the live palette; full theme-import/restart/platform visual evidence remains open | Partial improvement |
| Shortcuts | Shared bindings drive live actions and descriptors; native conflict replacement, reset, and workspace persistence are covered. Platform layouts/modifiers and launched restart remain. | Partial improvement |
| Split panes | Mixed nested native splitters, ratios, pane tabs/order, active local tabs, focused-canvas targeting, and cross-pane drag/drop round-trip without recreating canvases/input; cross-window drop uses the transactional registry and exact target pane | Partial improvement; launched platform placement remains |
| Detach/redock/move | Transactional close/move/detach/redock and pane/window drop are implemented; populated detached restart and redock to primary are launched-process proven. Cross-window drop preserves overlay/tool/selection state. | Partial improvement; launched OS drag and broader placement remain open |
| Multi-window isolation | Per-canvas ImGui/input/interaction ownership improved | Improvement; concurrent gesture matrix unverified |
| Workspace save/load | Recreates populated figures and nested document/pane-tab assignments across a real process restart; native dock area/tab/visibility/floating state and window geometry have focused round-trip coverage. Broader detached-window/platform placement remains incomplete. | Partial improvement |
| Autosave/crash recovery | Document mutations, timer, pre-teardown save, and interactive startup wired; restore/dirty coverage incomplete | Partial improvement |
| PNG/SVG/copy image | Launched PNG/SVG artifacts and `image/png` clipboard MIME are proven from a populated native Vulkan canvas; panel requested-size/plugin routing has focused coverage. Cross-frontend/platform results remain. | Partial improvement |
| Copy data/HTML/WAV | HTML and WAV use injected destinations and focused tests verify their files; cross-frontend content comparison remains. | Partial improvement |
| Plugins | Reduced management and broken ownership/payload paths | P1 broken |
| Backend/IPC reconnect | No complete reconnect/backoff/restart parity evidence | Unverified |
| Python publisher/show | Not exercised in this pass | Unverified |
| ROS2/PX4 | Generic/placeholder Qt panels; ROS2-off build | P1 placeholder |
| Accessibility/keyboard/focus | Sonification plus HTML output exist; custom rail/Home/window/dock controls are named and keyboard-focusable, while document tabs expose live summary state and keyboard select/close/add/detach results | Partial; child tab roles, full focus order, screen-reader/IME/platform matrix remain open |

## 10. Windows, docking, persistence, overlay, and input details

### 10.1 Documents, splits, and windows

- Custom tab close now reaches `MainWindowRegistry::close_document()`, removes every visual
  occurrence, and unregisters the figure model. Main-window forwarding emits one close notification.
- `open_figure_ids()` follows the authoritative pane/tree tab order; main-host enumeration is sorted
  and the primary host is recorded explicitly.
- Split reconstruction recursively creates native `QSplitter` nodes for mixed horizontal/vertical
  topology and synchronizes moved handle ratios back to the logical tree.
- Split mode introduces internal `QTabWidget` bars in addition to the custom document strip.
- Native `QWindow`/container FocusIn and mouse activation now promote the owning figure/pane before
  commands resolve active state; focused QWidget descendants are also recognized by `active_pane()`.
- Pane/window document drag uses a stable figure MIME payload; local drops use a model-first
  live-canvas transfer and cross-window drops use the rollback-safe registry transaction.
- Custom-tab detach, `figure.move_to_window`, and native host movement now share one registry
  transaction. It validates source/destination and unique ownership, adds the destination before
  releasing the source, rolls the destination back if release fails, and preserves the source when
  a destination cannot be created.
- `NativeQtDockingHost::documents()` now returns the window's open figure IDs.

### 10.2 Workspace, autosave, and recovery

- Workspace capture embeds complete binary figure snapshots and records main/detached document
  assignments plus each logical split tree. Restore validates all snapshots before mutation,
  recreates models, remaps old IDs, and rebuilds saved pane tabs/windows.
- Production manual save/load, forced-`SIGKILL` autosave recovery, and populated detached-window
  reconstruction have crossed real process boundaries with exact model and top-level-window checks.
- Nested split trees now rebuild as matching native splitter hierarchies; detached window/dock
  topology still lacks a complete launched-process fixture.
- Interactive in-process startup invokes `check_crash_recovery()` and routes accepted recovery
  through the full figure/document restore path; deterministic launched tests use an explicit
  accept/decline policy without changing the production prompt default.
- Successful frontend model redraw transactions, figure/document lifecycle, canvas interaction,
  tools/overlays, panel visibility, splits, settings, and shortcuts mark autosave dirty, and a Qt
  timer calls `tick()`. Direct model changes outside the frontend service paths remain unverified.
- Dirty state is saved before live windows and the docking registry are destroyed; the configured
  recovery file is then cleared only after shutdown completes successfully, so clean exits do not
  masquerade as crashes.

### 10.3 Overlays and interaction ownership

Per-canvas state is a verified improvement in the current working tree:

- each canvas owns its `ImGuiIntegration`/context, `DataInteraction`, frame timing, input binding, and
  inspector callbacks;
- `ImGuiIntegration::build_ui` submits retained overlay primitives into the same native Vulkan render
  pass after plot geometry rather than relying on the unused QPainter adapter;
- each workspace figure payload now includes its canvas's tool, crosshair, tooltip, marker,
  annotation, ROI, and measurement state, and native surface teardown/recreation preserves it;
- icon-font lookup is context-local;
- tests cover canvas-local inspector routing, overlay snapshot isolation/axes ownership, queued
  reattachment, and teardown.

That fixes a serious shared-state design defect, but it does not complete overlay parity:

- `QtOverlayDrawList` remains unused because the retained ImGui draw list is the production Vulkan
  composition path; removing or repurposing the dead adapter remains cleanup work;
- launched tooltip, legend, marker, selection, measurement, annotation, ROI, crosshair, and data-tip
  pixel/result equivalence plus overlay editing/undo/export is still incomplete;
- simultaneous hover/selection/gesture behavior across canvases is not tested.

### 10.4 Input

The router covers mouse press/release/move/double-click, angle wheel, printable/shared navigation and
function keys, modifier translation, and committed text through the MCP path. Automation hit-tests
widgets in main-window coordinates and explicitly targets the embedded native canvas when the point
falls inside it. It does not yet cover:

- high-resolution `pixelDelta`, scroll phases, and momentum;
- IME preedit/composition;
- touch, tablet/stylus, or gestures;
- drag/drop;
- enter/leave/focus transitions;
- keypad identity and the complete platform-specific key map;
- exhaustive arbitration between QAction shortcuts, text widgets, and the focused native canvas.

## 11. Test-suite gap analysis

All nine Qt-labelled tests passed in 4.10 seconds. This proves the current tree builds and its tested
helpers remain stable; it does not prove application parity.

| Test area | What it establishes | What it misses |
|---|---|---|
| Visual regression | Main widget can be grabbed, dimensions are plausible, closed shell is broadly dark | No services/runtime/native Vulkan `QWindow`, opened panels, legacy baseline, numerical diff, plots, overlays, 3D, DPRs, or platforms |
| Automation | Live MCP verifies menu/method discovery, figure activation, exact scatter data/type, validation errors, widget/native-canvas input dispatch, text/key results, capture scopes/base64, exact frame pumping, rendered-progress waiting, seeded repeatability, all 16 forced fuzz actions, and unknown-action errors. A production X11/lavapipe Qt launch also proved catalog/reset/create/add/state/clean-shutdown results. | No automated unchanged cross-frontend launched-process semantic/state/artifact equality; remaining advanced-input and non-X11 capture semantics |
| Action bridge | Descriptors produce actions and dispatch helpers | Handler result parity, complete menu attachment/order, shortcut/check-state/rebind conflicts |
| Panels | Construction and selected mutation helpers; inspector/data/transform undo improvements; Export callback routing; injected-path HTML/WAV artifacts | Full visible control-to-result workflows, styling, persistence, and launched plugin artifacts |
| Docking/window ops | Selected helper state changes plus nested native splitter topology | User drag/drop, destination rollback, native focus, detached/dock complete restoration |
| Workspace | Lossless populated-figure disk round-trip, corrupt atomicity, pane-tab/ID remap, Qt bridge capture/restore, and a production two-process manual save/load | Detached/nested window topology and forced-crash interactive autosave recovery |
| Dialogs | Injected file/color/number behavior, deterministic automation cancellation/scripted values, and file-producing panel routes | Native interaction and cross-platform cancel/overwrite/error behavior |
| Plugin UI | Registry/schema helper behavior and production export routing with real RGBA/figure JSON | Real plugin identity, nested groups, action/property ownership, launched artifact/error paths, visual parity |

Minimum replacements:

1. Launch both production executables and run identical MCP workflows.
2. Treat structured errors and false-success responses as failures.
3. Assert command/menu/button results, state, undo, saved data, and artifacts—not only existence.
4. Capture the compositor-equivalent complete Qt surface including native canvas and top-level UI.
5. Store approved reference images and numerical diffs for every migration-plan fixture.
6. Add ASan/UBSan interaction coverage for close/move/detach, panels, dialogs, and endpoint shutdown.

## 12. Direct source evidence

- [qt_automation_adapter.cpp](../src/adapters/qt/qt_automation_adapter.cpp): implemented Qt MCP
  command/figure/menu/catalog, widget/native-canvas input, scoped capture/base64, and popup/grab
  dismissal branches, truthful active-canvas frame pumping, rendered-frame progress reporting, and
  semantic seeded/forced dispatch for all shared fuzz actions.
- [automation_fuzz_catalog.hpp](../src/ui/automation/automation_fuzz_catalog.hpp): authoritative
  fuzz action names and weights shared by the legacy and Qt runners.
- [automation_server.cpp](../src/ui/automation/automation_server.cpp): deferred frame waits consume
  a frontend-supplied rendered-frame delta; the legacy per-frame poll retains its one-frame default.
- [spectra_vulkan_window.cpp](../src/adapters/qt/spectra_vulkan_window.cpp): successful Qt render
  completions advance the shared monotonic frame counter used by automation; pointer movement emits
  the `InputHandler`'s hit-tested data-space readout rather than raw canvas pixels.
- [qt_application.cpp](../src/adapters/qt/qt_application.cpp): 86 command registrations with no
  explicit empty handler, active-document series routing, per-figure timeline/command resolution,
  shared/persisted theme behavior, and transactional lossless workspace reconstruction.
- [qt_series_commands.cpp](../src/adapters/qt/qt_series_commands.cpp): deep-copy series clipboard,
  bounded paste, removal cleanup, and cut/delete/paste undo/redo transactions shared by Qt command
  handlers and focused tests.
- [qt_main_window.cpp](../src/adapters/qt/qt_main_window.cpp): shared-theme stylesheet application,
  active-figure panel/timeline/title/status synchronization, production animation tick routing,
  deterministic single-instance command menu routing, live panel check state, and compact-mode
  limitations.
- [spectra_app_header.cpp](../src/adapters/qt/components/spectra_app_header.cpp): custom menu/header,
  document strip, Home signal, and welcome controls.
- [spectra_status_bar.cpp](../src/adapters/qt/components/spectra_status_bar.cpp): live active-canvas
  data-coordinate cursor/FPS/GPU/zoom presentation and invalid-readout clearing.
- [inspector_widget.cpp](../src/adapters/qt/panels/inspector_widget.cpp): live-model synchronized 2D/3D
  summary, topology, and editing; complete legacy property/data parity remains open.
- [timeline_widget.cpp](../src/adapters/qt/panels/timeline_widget.cpp): active per-figure transport,
  typed value/interpolation/tangent track/keyframe authoring and selection, shared undo/redo,
  persistent 2D/3D property-target binding, workspace-dirty signaling, and presentation-only polling
  behavior.
- [curve_editor_widget.cpp](../src/adapters/qt/panels/curve_editor_widget.cpp): native painted
  multi-channel curve/keyframe/tangent editing with fit/zoom/pan/selection, shared marker
  synchronization, undo, and active-document switching.
- [timeline_editor.cpp](../src/ui/animation/timeline_editor.cpp): complete Timeline track/keyframe,
  transport, snap, view, and interpolator JSON round-trip used by undo and persistence.
- [data_editor_widget.cpp](../src/adapters/qt/panels/data_editor_widget.cpp): validated cell and row
  editing, rectangular clipboard paste, selected-series replacement, mapped shared-X/multi-Y 2D or
  shared-XZ/multi-Y 3D series creation, and injected-path CSV/TSV import/export through shared undo,
  redraw, live refresh, autosave notifications, bounded large-series pagination, precise absolute
  timestamp offsets, and focused artifacts.
- [transform_widget.cpp](../src/adapters/qt/panels/transform_widget.cpp): explicit all-visible,
  axes-wide, single-series, or arbitrary exact multi-series targeting, editable ordered/enabled
  pipeline rows, non-destructive
  built-in and shared-parser formula previews, scoped
  pipeline/application/formula undo, redraw, autosave signaling, and per-figure lossless built-in
  pipeline workspace state, provider provenance, safe plugin-owned callback teardown, and
  unavailable custom-step preservation/recovery.
- [plugin_panel_widget.cpp](../src/adapters/qt/panels/plugin_panel_widget.cpp): typed portable
  schema rendering with stable callback IDs, returned-value reconciliation, nested group traversal,
  cycle rejection, root de-duplication, tooltips, and addressable controls.
- [plugins_widget.cpp](../src/adapters/qt/panels/plugins_widget.cpp): injected-path loading,
  default/custom directory scanning, lifecycle controls, manifest capabilities, runtime diagnostics,
  health/status reporting, and signal-safe atomic refresh.
- [function_plot_dialog.cpp](../src/adapters/qt/panels/function_plot_dialog.cpp): modeless shared-parser
  formula validation and shared function-series sampling against the active figure.
- [settings_widget.cpp](../src/adapters/qt/panels/settings_widget.cpp): persisted panel visibility,
  theme selection, and authoritative external-theme synchronization.
- [command_palette_dialog.cpp](../src/adapters/qt/panels/command_palette_dialog.cpp): palette
  construction, light fallback surfaces, and direct dialog behavior.
- [shortcut_widget.cpp](../src/adapters/qt/panels/shortcut_widget.cpp): native shortcut capture,
  conflict replacement, live action synchronization, reset, and persistence.
- [export_widget.cpp](../src/adapters/qt/panels/export_widget.cpp): built-in/plugin export UI and
  owning-canvas RGBA/figure-JSON callback routing; launched plugin verification remains incomplete.
- [native_qt_docking_host.cpp](../src/adapters/qt/docking/native_qt_docking_host.cpp): document
  enumeration and transactional move/detach/redock ordering.
- [qt_workspace_bridge.cpp](../src/adapters/qt/qt_workspace_bridge.cpp): deterministic primary-host,
  main/detached document, split-tree, and old-to-new figure-ID persistence mapping; complete
  detached restart evidence is launched-process proven; native dock-widget restart remains
  incomplete.
- [split_view_container.cpp](../src/adapters/qt/split_view_container.cpp): recursive native split-tree
  reconstruction, pane-tab persistence, focus selection, document movement, and native-pane title
  synchronization.
- [qt_runtime.cpp](../src/adapters/qt/qt_runtime.cpp): improved per-canvas
  integration/input/overlay/crosshair ownership.
- [spectra_vulkan_window.cpp](../src/adapters/qt/spectra_vulkan_window.cpp) and
  [qt_input_router.hpp](../src/adapters/qt/qt_input_router.hpp): native canvas embedding and restricted
  event/key coverage.
- [ros_panel_manager.cpp](../src/adapters/qt/ros2/ros_panel_manager.cpp): explicit placeholder
  display and label-only inspector surfaces.
- [test_qt_visual_regression.cpp](../tests/qt/test_qt_visual_regression.cpp): QWidget grab and broad
  darkness assertions without the production Vulkan/panel matrix.
- [test_qt_automation.cpp](../tests/qt/test_qt_automation.cpp): live semantic checks for figure,
  menu, input, scoped capture/base64 methods, deterministic fuzz reset/step behavior, every forced
  fuzz action result, and error handling.
- [test_qt_panels.cpp](../tests/qt/test_qt_panels.cpp): command-backed navigation state, live
  Settings visibility, and rendered Night/Light custom-shell coverage.
- [imgui_command_bar.cpp](../src/ui/imgui/imgui_command_bar.cpp): legacy command/menu behavior used as
  the reference alongside the live endpoint.

## 13. Legacy/reference defects found

These are legacy defects, not Qt parity credits:

| ID | Current reproduction | Required follow-up |
|---|---|---|
| LEGACY-AUDIT-001 | MCP `create_figure` and `add_series` mutate the registry but leave the visible application on Welcome; a later semantic `figure.new` creates a separate visible document. | Define model-only versus visible-document automation explicitly and make the fixture deterministic. |
| LEGACY-AUDIT-002 | After a visible figure exists, MCP-added series changes registry state but the visible canvas remains empty axes. | Attach automation series to the same authoritative visible document model. |
| LEGACY-AUDIT-003 | A requested scatter series was returned by `get_figure_info` as type `line`. | Fix type serialization and add exact type/data assertions. |
| LEGACY-AUDIT-004 | `capture_screenshot` returned a success path, but no file was created. | Verify filesystem output before returning success and cover failure/cancel. |
| LEGACY-AUDIT-005 | On clean repeated launches, `figure.new` followed by `panel.toggle_inspector` terminated the legacy process and the endpoint returned an empty reply. | Reproduce under ASan, fix the crash, and add a process-survival test. |
| LEGACY-AUDIT-006 | Live `list_menus` reports only File, Edit, View, Tools, and Plot even though legacy source exposes additional Data/Axes/Transforms behavior. | Make menu introspection enumerate the authoritative complete menu model. |

Legacy goldens involving these paths must not be approved until the reference behavior is stable.

## 14. Verified improvements that must be retained

The following work is valuable but does not close the application parity gate:

- Qt and the services layer expose one reachable MCP endpoint with Qt event-loop dispatch.
- Qt registers all 83 legacy command IDs, making descriptor differences measurable.
- Basic semantic `figure.new`, tab activation, tool-mode routing, and blank Vulkan rendering work.
- Inspector, data-cell, and transform mutations have shared undo/redraw transaction coverage.
- Inspector callbacks resolve stable figure/axes identifiers rather than retaining dead local
  references.
- Inspector size/control synchronization follows successful active-canvas frames; unified non-null
  axes traversal covers 2D/3D tabs and topology changes.
- Visible Timeline controls, animation commands, and canvas-driven figure callbacks share stable
  per-figure editor state rather than independently advancing presentation state.
- Native Timeline track/keyframe mutations are selectable and undoable, mark autosave dirty, and
  round-trip per figure through the same workspace/recovery payload used by detached hosts.
- Document titles and zoom status follow the active figure model; background canvases cannot
  overwrite active cursor/performance/zoom state.
- Plot Function uses a native validated editor and the shared expression/sampling implementation.
- Crosshair state is routed to the active canvas's production data-interaction overlay and remains
  isolated per document.
- Tool, crosshair, tooltip, data-tip marker, annotation, ROI, and completed-measurement snapshots
  survive workspace recreation and native surface reattachment with per-figure and per-axes identity
  preserved.
- Series selection is per canvas and shared by pointer interaction, commands, inspector context, and
  renderer highlighting; clipboard/removal mutations are covered by undo/redo adapter tests.
- Per-canvas ImGui context, interaction, input, timing, font, and inspector routing are isolated.
- Detached documents can be moved back to the primary host through the shared transactional command
  or context menu; emptied detached hosts close after command dispatch without destroying the sender.
- Renderer, native Qt widgets, and custom-painted shell surfaces share the live theme palette;
  Settings and theme commands keep the selected default synchronized and persisted.
- All nine Qt-labelled tests build and pass.

These improvements should remain covered while semantic and visual tests are expanded.

## 15. Required closure gates

### P0

1. Implement the complete MCP contract in Qt, fix truthful canvas/window/dialog capture, and run one
   fixture suite unchanged against both production executables.
2. Consolidate document lifecycle into one transactional path; prove close, reorder, detach, move,
   destination failure, redock, and registry/pane synchronization without loss or duplication.
3. Round-trip a populated multi-window workspace with mixed nested splits through save, load,
   autosave, forced crash, and restart recovery.
4. Fix the legacy reference crashes and false-success automation results required to generate stable
   comparison fixtures.

### P1

5. Give every registered and visible command a real semantic result; remove or disable stubs until
   implemented. Share menu hierarchy, labels, shortcuts, enable/check state, and conflict policy.
6. Complete timeline/curve editor, inspector, data editor, settings, transforms, topics, plugins,
   export, accessibility, and ROS2/PX4 workflows against their production models.
7. Route every header/rail/menu/palette/shortcut entry through the same command and state controller;
   eliminate inert Home, placeholder status values, stale rail selection, and duplicated menu items.
8. Complete nested split/dock/window behavior, deterministic active-pane focus, and visible
   close/undock controls.
9. Port and exercise all overlays and complete mouse, keyboard, IME, scrolling, touch/tablet,
   drag/drop, focus, and high-DPI input.
10. Bind all Qt chrome and panels to shared theme/design tokens. No native-white fallbacks, missing
    indicators, illegible disabled states, clipping, or unexpected canvas compression are allowed.

### Required evidence

- Exact command ID/label/category/default shortcut/enabled/check-state and menu-order comparison.
- A visible-control matrix that activates every button, menu item, palette item, and shortcut and
  asserts its model, panel, tool, undo, persistence, artifact, or error result.
- Approved welcome, line, scatter, multi-axis, 3D, every overlay, every panel/dialog, split, detached
  window, plugin, topic, and recovery images at 1280×720, 1600×900, and 200% DPR on X11 and Wayland,
  followed by Windows and macOS release baselines.
- Renderer-owned regions at no more than 0.1% pixels differing by more than two 8-bit channel values,
  plus approved shell/panel thresholds and stored diff images.
- Verified PNG, SVG, clipboard image/data, HTML table, figure, workspace, plugin, and failure/cancel
  artifacts.
- Keyboard-only focus/order/roles/names, screen-reader smoke, and sanitizer-backed interaction tests.

## 16. Sign-off rule

A command descriptor, constructed widget, HTTP 200 response, non-null QWidget grab, matching blank
plot, or passing Qt-labelled smoke test is not application parity.

Qt may become the default only when every P0/P1 item in this report is closed, the migration plan's
complete functional matrix is green, every visible control has an equivalent tested result, and the
approved visual/state/artifact gates pass on the required platforms. Until then, Qt remains opt-in
and the legacy frontend remains the production default.
