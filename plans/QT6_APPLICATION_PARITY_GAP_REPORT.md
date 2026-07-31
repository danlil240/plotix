# Spectra Legacy vs Qt 6 Application Parity Gap Report

**Audit date:** 2026-07-26

**Audited revision:** `09a6c6ae02b2ffe3f59e9e092d0b151445282590` on
`plan/qt6-application-migration`, plus the pre-existing uncommitted working-tree changes listed by
`git status` at audit time

**Decision:** **Qt is not eligible to become the default frontend.** Keep
`SPECTRA_DEFAULT_FRONTEND=legacy`.

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

## 1. Executive summary

This audit compared fresh builds of the legacy GLFW/ImGui application and `spectra-qt-app` through
the repository's [spectra-mcp](../.cursor/skills/spectra-mcp/) workflow, live X11/lavapipe sessions,
command and state transcripts, direct control activation, compositor screenshots, the Qt test suite,
and source inspection. It covers visible buttons and menus, their results, application state,
panels, graphics, automation, persistence, windows, input, plugins, and adapters.

The Qt frontend has made real infrastructure progress, but **command registration is far ahead of
feature implementation**:

- Legacy registers **83 command descriptors**. Qt registers all 83 legacy IDs plus three Qt-only
  IDs, for **86 descriptors**. This is descriptor parity, not behavior parity: at least **10 Qt
  commands are explicit stubs/no-ops**, and several other handlers operate disconnected models,
  bypass deterministic services, or produce materially different results.
- The Qt MCP endpoint now answers without timing out, but **3 of the 28 advertised MCP tools
  explicitly return “Qt automation method is not implemented.”** `pump_frames` now renders the
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
- Timeline is visibly present but connected to `nullptr`; Curve Editor and Markers are hidden or
  absent; Light theme still does not apply to the shell; zoom status remains static; Topics is a
  generic data-source list rather than the legacy ROS/PX4 workflow; and export, plugins, transforms,
  splits, workspace recovery, and input remain partial or unsafe.
- Document close/move/detach now has a central rollback-safe path and direct Qt test coverage.
  Successful document close/move now marks autosave dirty, periodic autosave runs in the Qt event
  loop, and interactive startup invokes recovery. Workspace load still does not recreate documents,
  most non-document mutations do not mark autosave dirty, and nested topology recovery remains
  incomplete.
- All **9/9 Qt-labelled tests pass**, but the visual test does not include a Vulkan canvas or opened
  panels. The automation test now verifies the capture scopes/base64 payload, the five earlier
  semantic methods, and seven input methods, and distinguishes the three explicit unsupported
  responses, but it still is not a launched-process parity fixture.

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
| QT-GAP-001 | P0 | Common automation contract is incomplete | **Partial:** `get_screenshot_base64`, scoped capture, real Qt popup/grab dismissal, truthful native-canvas `pump_frames`, and rendered-progress-based `wait_frames` were added. 3/28 Qt tools still return explicit not-implemented fuzz errors. | Run the same complete MCP fixture against both frontends and assert equivalent state, UI, artifacts, and errors. |
| QT-GAP-002 | P0 | Qt screenshots are not truthful | **Partial:** canvas and window capture now have distinct scopes; composed screen capture includes native canvas content and owned popups/dialogs, returns dimensions, and backs base64 PNG output. A launched X11/lavapipe 1280×720 capture matched an external root dump exactly; Wayland, multi-screen, and DPR validation remain open. | Prove the new capture path across the remaining required platform/DPR matrix. |
| QT-GAP-003 | P0 | Document close/move state is unsafe | **Partial:** close/move/detach now share a controller, close removes UI and registry state once, destination validation precedes source release, failed transfer preserves/rolls back, and successful mutations mark persistence dirty. Nested redock and launched-process coverage remain open. | Extend the transaction through complete redock/persistence state and prove close, move, detach, rollback, and restart behavior in launched-process tests. |
| QT-GAP-004 | P0 | Workspace/autosave/recovery cannot restore a session | **Partial:** host documents are enumerated; successful close/move marks dirty; autosave is ticked and saved before teardown; interactive startup invokes recovery. Load still cannot recreate missing figures/windows/topology, and other mutations lack dirty coverage. | Round-trip populated figures, windows, nested panes, docks, active state, and unsaved recovery in a process-restart test. |
| QT-GAP-005 | P1 | Registered commands overstate implemented behavior | At least 10 descriptors are explicit no-ops; animation uses a model disconnected from the visible timeline; help bypasses the application UI. | Every visible/registered command must have a tested semantic result or be disabled/hidden and excluded from parity counts. |
| QT-GAP-006 | P1 | Menu hierarchy is incomplete and contradictory | **Partial:** Menu routing now uses command-ID prefixes. Figure lifecycle (new/close/next/prev/tab/move) routed to File. Help/animation/theme/series/accessibility routed to correct menus. View menu reduced via Panels and Splits submenus. Axes/Transforms still empty (no commands registered). | Bind a shared ordered menu model with identical categories, labels, shortcuts, enable/check state, and results. |
| QT-GAP-007 | P1 | Visible controls are inert or misleading | **Partial:** Home triggers `view.home`; cursor/FPS/GPU status fields are wired; reset layout resets panel toggle state. Rail tools now execute shared commands, while rail/status tool state follows the active canvas across document switches. Panel rail buttons no longer overwrite tool state. Remaining visible-control semantics and zoom-status updates are not fully verified. | Every visible control must execute one semantic command and reflect authoritative state. |
| QT-GAP-008 | P1 | Timeline and curve editing are absent | Timeline receives `nullptr` and is disabled; Curve Editor has no Qt panel/handler. | Bind the production timeline/keyframe model and complete transport, scrub, loop, FPS/duration, and curve editing with undo. |
| QT-GAP-009 | P1 | Panel graphics do not follow the dark shell/design system | **Partial:** Stylesheet now covers QListWidget, QTableWidget, QPlainTextEdit/QTextEdit, QRadioButton, QProgressBar, QSlider, QToolTip, disabled states, QComboBox down-arrows, QSpinBox up/down arrows, QMenu checkable indicators, QDockWidget close/float buttons, and central container/canvas frame backgrounds. Light theme chrome mismatch (QT-GAP-010) and golden image validation remain open. | Theme every state/control and pass approved panel goldens at all reference sizes/DPRs. |
| QT-GAP-010 | P1 | Settings/theme behavior is disconnected | **Partial:** persisted Inspector, Navigation Rail, and Timeline visibility is applied to the live shell, while live changes synchronize back to controls/persistence. Light theme still changes the renderer while shell/panels remain hard-coded dark. | Apply one shared theme/token and settings state to renderer, chrome, panels, and persistence. |
| QT-GAP-011 | P1 | Inspector reports contradictory/stale state | Live panel showed 1500×900 while state was 1208×612 and “Axes 0” versus tab/state axes count 1. | Derive all values from the active live model after resize and cover 2D/3D axes consistently. |
| QT-GAP-012 | P1 | Data, transforms, topics, and plugins are prototypes | Missing row operations/import/export/targets/preview/provenance/ROS workflows; plugin IDs/payloads/schema rendering are incomplete. | Match every legacy result, validation, undo, persistence, error, and artifact path. |
| QT-GAP-013 | P1 | Export and dialogs are not automation-safe | Direct native dialogs bypass injected services; plugin export gets null pixels/empty JSON; artifact outcomes are unverified. | Use shared dialog/export services and verify PNG, SVG, clipboard, table, figure, workspace, and plugin artifacts. |
| QT-GAP-014 | P1 | Splits/docks cannot preserve arbitrary topology | Mixed nested splits are flattened; focus targeting is unreliable; custom and internal tabs coexist; dock controls are hidden. | Preserve nested trees, active pane/document/tool, and safe cross-pane/window movement through save/load. |
| QT-GAP-015 | P1 | Overlay/tool parity is not demonstrated | Crosshair is a stub, Markers is hidden, retained ImGui overlays remain, and interaction cannot be automated. | Exercise select, crosshair, tooltip, legend, markers, measure, annotate, ROI, and data tips end to end. |
| QT-GAP-016 | P1 | Input coverage is incomplete | **Partial:** MCP pointer/button/drag/wheel/key/text/double-click events now reach widgets and the native canvas. Pixel scrolling/phases, IME composition, touch/tablet/gesture, DnD, focus transitions, and complete platform key coverage remain open. | Implement and test the platform input matrix, including QAction/canvas/text focus arbitration. |
| QT-GAP-017 | P1 | ROS2/PX4 Qt UI remains placeholder/generic | Display render area is explicitly marked placeholder; inspector is label-only; Topics is a generic registry browser. | Port all supported discovery, QoS, plot, display, inspector, bag/ULog, reconnect, and error workflows. |
| QT-GAP-018 | P1 | Full graphics parity is unestablished | Only a blank 2D crop passes narrowly; shell, welcome, panels, series, overlays, 3D, DPR, and platform baselines remain. | Pass the complete approved visual matrix and store numerical failure artifacts. |
| QT-GAP-019 | P2 | Shortcut/action state can diverge | Shared IDs have different defaults; action refresh does not fully establish live shortcut/check-state parity; rebinding remains TODO. | Compare and test ID, label, category, shortcut, enabled/check state, conflicts, rebind, reset, and persistence. |
| QT-GAP-020 | P2 | Current tests cannot detect the observed failures | Visual grab excludes runtime canvas/panels; automation treats structured error as success; action tests mostly prove descriptor creation. | Replace smoke assertions with launched-process semantic, artifact, state, and image comparisons. |

## 5. Automation parity

### 5.1 Endpoint and tool results

Both live applications answered on the configured endpoint. Qt logged 86 registered commands and its
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
| `fuzz_step`, `fuzz_reset`, `list_fuzz_actions` | Implemented | **Not implemented** |
| `list_methods` | Implemented | Implemented from the shared automation handler catalog |
| `pump_frames` | Pumps legacy frames | Synchronously renders the active native canvas, returns the observed count, and errors if exact progress is impossible |
| `wait_frames` | Waits for frame progress | Deferred by the shared queue and completed only after the requested number of successful Qt renders |
| `dismiss_ui_capture` | Dismisses capture UI | Closes active Qt popups and releases widget keyboard/mouse plus native-canvas mouse grabs |

The three explicit Qt not-implemented methods are:

`fuzz_step`, `fuzz_reset`, and `list_fuzz_actions`.

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

Qt category counts are:

| Category | Count | Category | Count |
|---|---:|---|---:|
| Accessibility | 1 | Animation | 6 |
| App | 5 | Data | 2 |
| Edit | 2 | Figure | 13 |
| File | 9 | Panel | 3 |
| Plot | 5 | Series | 6 |
| Theme | 4 | Tools | 6 |
| View | 24 |  |  |

The following registered Qt commands are explicit no-ops/stubs:

- `app.cancel`
- `panel.toggle_curve_editor`
- `plot.function`
- `view.toggle_crosshair`
- `series.cycle_selection`, `series.copy`, `series.cut`, `series.paste`, `series.delete`, and
  `series.deselect`

Additional descriptor-to-result divergences:

- All six animation commands operate a controller-owned `TimelineEditor`, while the visible
  `QtTimelineWidget` was created with `nullptr`.
- Theme commands update the renderer theme, not the hard-coded Qt shell/panel stylesheet.
- `help.show` launches an external URL through `xdg-open`; there is no integrated Help surface and
  the visible Help menu is empty.
- Figure/workspace/export actions use direct native dialogs, bypassing deterministic dialog
  automation.
- HTML/WAV outputs use hard-coded relative filenames rather than a user-selected destination.
- `figure.move_to_window` now uses the same validated, rollback-safe detach transaction as the tab
  action.

### 6.2 Shared descriptor mismatches

| Command | Legacy descriptor | Qt descriptor | Gap |
|---|---|---|---|
| `figure.tab_1` … `figure.tab_9` | `1` … `9` | `Alt+1` … `Alt+9` | Changed navigation muscle memory |
| `file.export_png` | `Ctrl+S` | `Ctrl+Shift+S` | Changed shortcut |
| `file.export_svg` | `Ctrl+Shift+S` | `Ctrl+Shift+Alt+S` | Changed shortcut |
| `panel.open_settings` | Panel, no shortcut | View, `Ctrl+,` | Category and shortcut differ |
| `panel.toggle_data_editor` | Panel, no shortcut | View, `D` | Category and shortcut differ |
| `panel.toggle_inspector` | Legacy label/category, no shortcut | Different label/category, `I` | Descriptor differs |
| `panel.toggle_timeline` | Legacy label/category | Different label/category | Descriptor differs |
| `panel.toggle_topics` | Legacy label/category | Different label/category | Descriptor differs |
| Tool modes | No equivalent defaults | `A`, `Z`, `M`, `H`, `Shift+R`, `V` | Qt-only default bindings/conflict risk |
| `view.autofit` | “Auto-Fit Active Axes”, `A` | “Auto-Fit Active Figure”, `Shift+A` | Scope, label, and shortcut differ |
| `view.fullscreen` | Canvas label, `F` | Generic label, `F11` | Label and shortcut differ |

### 6.3 Menu hierarchy and live result

The Qt custom header does not build menus from the same hierarchy as legacy:

| Menu | Qt population/result | Parity gap |
|---|---|---|
| File | Nine File-category actions plus an ad-hoc command-palette item | New Figure is missing from File; direct-dialog paths remain |
| Edit | Two Edit actions | Series editing commands are not attached here |
| View | 24 View actions, 11 ad-hoc panel toggles, and five duplicated split/reset actions | Roughly 40 entries, duplicate reset/close-split meanings, popup extends below 720px |
| Tools | Six Tools actions | Theme/accessibility/application actions are elsewhere or unattached |
| Plot | 13 Figure actions plus five Plot actions | Figure New/Close/Tabs are incorrectly grouped under Plot; function plot is a stub |
| Data | Two Data actions | Hard-coded output behavior; not the legacy import/data workflow |
| Axes | No category actions | Visible but empty |
| Transforms | No category actions | Visible but empty; transform dock is separate |
| Help | No category actions | Visible but empty because `help.show` is categorized as App |

Accessibility, Animation, App, Panel, Series, and Theme category menus are constructed transiently
but are not attached to the custom header. The View menu rendered live but overflowed the reference
height. Several smaller popup surfaces appeared black/blank under Xvfb; because of the Xvfb caveat,
the source-confirmed action population—not that paint artifact—is the blocking evidence.

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
| Rename in Inspector | Figure title changes; custom document-tab title is not synchronized | P1 broken |
| Home | Executes the shared `view.home` action | Basic pass |
| Select/Pan/Zoom/Measure/Annotate/ROI | Execute shared commands; rail/status reflect the active canvas's tool per document | Adapter-level pass; results/overlays unverified |
| Markers | Hidden | Feature absent |
| Curve Editor | Hidden and command stub | Feature absent |
| Transform/Inspector/Timeline/Plugins/Topics/Settings | Opens a dock directly without overwriting the active tool indicator | Panel command-state unification remains open |
| Help rail item | Hidden | Feature absent |
| Reset Layout | Hides docks and resets panel toggle check states | Basic pass |
| Status cursor/zoom/fps/GPU fields | Cursor/FPS/GPU receive production canvas signals; zoom remains static | Partial |
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
- Light theme produces a light plot inside unchanged dark chrome and dark panels.
- The command palette combines a dark input with pale category bands and a nearly all-white results
  area. It is also a separate top-level dialog omitted from MCP captures.
- The shell stylesheet does not comprehensively theme `QListWidget`, `QTableWidget`, combo arrows,
  checkbox indicators, or disabled states.
- Compact mode below 1100px only narrows the rail; the inspector-overlay behavior remains a TODO and
  docks can crush the canvas.

## 8. Panel-by-panel behavior and result comparison

| Panel/workflow | Observed Qt result | Gap |
|---|---|---|
| Inspector | Opens and can edit a useful subset through undo-aware transactions. Live summary showed stale 1500×900 size while state was 1208×612 and “Axes 0” while tab/state showed one axis. Rename does not update document tab. | Uses inconsistent axes collections and stale refresh timing; lacks complete 2D/3D/data parity. |
| Timeline | Opens a large bottom dock showing 0.10s, FPS 1, None, Frame 0/0. Widget was constructed with `nullptr` and disabled. | Looks usable but cannot drive the production animation model. |
| Curve Editor | No visible Qt panel; command is a stub and rail item is hidden. | Missing. |
| Data Editor | Edits line/scatter x/y cells with validation, undo, and redraw. Empty figure shows a large white table. | No add/delete rows, multi-cell paste, reorder, import/export, target management, or large-data strategy. |
| Topics | Dock title is “Data Sources”; lists generic `DataSourceRegistry` entries. Start/Stop are disabled without selection. | Not the legacy ROS/PX4 browse/filter/QoS/topic-to-series workflow; large white list and weak disabled styling. |
| Transforms | Destructively applies operations/pipeline to all visible line/scatter series; undo snapshots exist. Buttons appear available without a target/pipeline. | No series/axes target, preview, provenance, safe applicability model, persistence, or equivalent custom-formula workflow. |
| Settings | Theme, palette, and three visibility checkboxes. | Inspector, Navigation Rail, and Timeline visibility now round-trip through the live shell and persistence; theme still affects the renderer only and controls need visual validation. |
| Plugins | Load Plugin, Scan Default, and empty plugin list. | No custom dirs/diagnostics/capabilities; direct dialogs; callback plugin ID is empty; group children are not recursive; raw color editor; export receives null pixels/empty JSON. |
| Export | Right dock with the observed built-in PNG entry, width/height/path/Browse/Export. | Combo affordance is unclear; no verified artifact/cancel/error matrix; plugin payload invalid; direct PNG/SVG commands use separate paths. |
| Command Palette | Search dialog lists command descriptors. `app.cancel` did not close it; direct Escape did. | Top-level surface is omitted from captures, mostly native white, and cancel semantics are disconnected. |
| ROS2 display/inspector | Source contains “Display render area — plugin auxiliary UI” placeholder and label-only inspector information. | Placeholder, not parity. |

## 9. Functional parity matrix

| Area | Qt result | State |
|---|---|---|
| Figure create/activate | Basic semantic path works | Partial |
| Figure close/rename/reopen | Close leaves registry state; rename leaves tab stale; reopen not established | P0/P1 broken |
| CSV/data import | No equivalent verified Qt workflow | Missing |
| Series add/remove/reorder/copy/cut/paste | Six series commands are stubs; MCP cannot add series | Missing |
| 2D render | Blank axes render and narrow crop matches | Blank-only pass |
| 2D pan/wheel/box/reset/fit | Tool routing and some commands exist; exact state/result matrix unavailable | Partial/unverified |
| Axis linking X/Y/Z/all | Axes menu is empty; no complete Qt workflow | Missing |
| Plot helpers | H/V/zero lines have handlers; function plot is stub; Plot grouping is wrong | Partial |
| Legend/grid/border | Some handlers/inspector controls exist | Partial; interaction/fidelity unverified |
| Crosshair/markers/data tips | Crosshair stub; Markers hidden; overlays not exercised | Missing |
| Measure/annotate/ROI/select | Tool mode changes; produced objects, editing, undo, persistence, and export unverified | Partial |
| 3D scene/camera/input/inspector | Shared renderer may support drawing; Qt shell/input workflow not compared | Unverified |
| Undo/redo | Inspector/data/transform paths have transaction coverage | Partial improvement |
| Timeline/keyframes/curves | Visible model disconnected; curve editor absent | Missing |
| Theme/palette | Renderer changes; shell does not | Divergent |
| Shortcuts | Descriptors exist with differences; rebinding/persistence incomplete | Partial |
| Split panes | Basic split exists; nested mixed topology is flattened | Partial/broken |
| Detach/redock/move | Transactional close/move/detach and enumeration implemented; nested redock/persistence coverage open | Partial improvement |
| Multi-window isolation | Per-canvas ImGui/input/interaction ownership improved | Improvement; concurrent gesture matrix unverified |
| Workspace save/load | Captures some model/chrome data; cannot recreate complete session | P0 incomplete |
| Autosave/crash recovery | Document mutations, timer, pre-teardown save, and interactive startup wired; restore/dirty coverage incomplete | Partial improvement |
| PNG/SVG/copy image | Handlers/panel paths exist; artifacts and equivalent UI not verified | Partial |
| Copy data/HTML/WAV | Service calls exist; filenames/dialog behavior diverge | Partial/divergent |
| Plugins | Reduced management and broken ownership/payload paths | P1 broken |
| Backend/IPC reconnect | No complete reconnect/backoff/restart parity evidence | Unverified |
| Python publisher/show | Not exercised in this pass | Unverified |
| ROS2/PX4 | Generic/placeholder Qt panels; ROS2-off build | P1 placeholder |
| Accessibility/keyboard/focus | Sonification descriptor exists; full roles/focus/screen-reader matrix absent | Unverified |

## 10. Windows, docking, persistence, overlay, and input details

### 10.1 Documents, splits, and windows

- Custom tab close now reaches `MainWindowRegistry::close_document()`, removes every visual
  occurrence, and unregisters the figure model. Main-window forwarding emits one close notification.
- `open_figure_ids()` iterates unordered maps, making numeric tab navigation order nondeterministic.
- Split reconstruction flattens panes under one root `QSplitter` orientation and cannot restore mixed
  nested horizontal/vertical topology.
- Split mode introduces internal `QTabWidget` bars in addition to the custom document strip.
- Active-pane detection checks the `QTabWidget` focus while focus normally belongs to the embedded
  Vulkan `QWindow`, so commands can fall back to the wrong pane.
- Cross-pane document drag is absent.
- Custom-tab detach, `figure.move_to_window`, and native host movement now share one registry
  transaction. It validates source/destination and unique ownership, adds the destination before
  releasing the source, rolls the destination back if release fails, and preserves the source when
  a destination cannot be created.
- `NativeQtDockingHost::documents()` now returns the window's open figure IDs.

### 10.2 Workspace, autosave, and recovery

- Workspace capture can now enumerate window/document associations, but restore still cannot
  reconstruct the complete document/window topology.
- Workspace load applies serialized data to the vector of figures that already exists; it does not
  recreate missing figures/documents/windows and topology.
- Host selection iterates unordered IDs and treats the first result as the main window.
- Interactive in-process startup now invokes `check_crash_recovery()` (suppressed under automation),
  but it only restores the limited available desktop state.
- Successful document close/move marks autosave dirty, and a Qt timer calls `tick()`. Other figure,
  series, panel, split, theme, and layout mutations still lack complete dirty propagation.
- Dirty state is saved before live windows and the docking registry are destroyed, preserving the
  available document/window topology in the shutdown snapshot.

### 10.3 Overlays and interaction ownership

Per-canvas state is a verified improvement in the current working tree:

- each canvas owns its `ImGuiIntegration`/context, `DataInteraction`, frame timing, input binding, and
  inspector callbacks;
- icon-font lookup is context-local;
- tests cover canvas-local inspector routing and teardown.

That fixes a serious shared-state design defect, but it does not complete overlay parity:

- `QtOverlayDrawList` has no production call site;
- production crosshair, tooltip, legend, marker, selection, measurement, annotation, ROI, and data-tip
  paths remain retained ImGui or absent;
- `view.toggle_crosshair` is a stub;
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

All nine Qt-labelled tests passed in 0.92 seconds. This proves the current tree builds and its tested
helpers remain stable; it does not prove application parity.

| Test area | What it establishes | What it misses |
|---|---|---|
| Visual regression | Main widget can be grabbed, dimensions are plausible, closed shell is broadly dark | No services/runtime/native Vulkan `QWindow`, opened panels, legacy baseline, numerical diff, plots, overlays, 3D, DPRs, or platforms |
| Automation | Live MCP verifies menu/method discovery, figure activation, exact scatter data/type, validation errors, widget/native-canvas input dispatch, text/key results, capture scopes/base64, exact frame pumping, rendered-progress waiting, and the three known unsupported methods; manual launched X11 capture matches the compositor and launched frame pumping/waiting succeeds | No automated cross-frontend launched-process semantic/state/artifact equality; remaining advanced-input/fuzz and non-X11 capture semantics |
| Action bridge | Descriptors produce actions and dispatch helpers | Handler result parity, complete menu attachment/order, shortcut/check-state/rebind conflicts |
| Panels | Construction and selected mutation helpers; inspector/data/transform undo improvements | Full visible control-to-result workflows, real models, styling, persistence, dialogs |
| Docking/window ops | Selected helper state changes | User drag/drop, destination rollback, nested topology, native focus, complete restoration |
| Workspace | Serialization helper round-trips | Process restart with real populated figures, documents, windows, recovery |
| Dialogs | Injected dialog helper behavior | Panels/actions that bypass it with direct native dialogs |
| Plugin UI | Registry/schema helper behavior | Real plugin identity, nested groups, action/property ownership, valid export payloads, visual parity |

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
  three explicit not-implemented fuzz returns.
- [automation_server.cpp](../src/ui/automation/automation_server.cpp): deferred frame waits consume
  a frontend-supplied rendered-frame delta; the legacy per-frame poll retains its one-frame default.
- [spectra_vulkan_window.cpp](../src/adapters/qt/spectra_vulkan_window.cpp): successful Qt render
  completions advance the shared monotonic frame counter used by automation.
- [qt_application.cpp](../src/adapters/qt/qt_application.cpp): 86 command registrations, explicit
  stubs, theme/help/export behavior, timeline model, and incomplete autosave/recovery lifecycle.
- [qt_main_window.cpp](../src/adapters/qt/qt_main_window.cpp): menu category construction, duplicated
  View actions, null visible timeline model, direct panel routing, and compact-mode limitations.
- [spectra_app_header.cpp](../src/adapters/qt/components/spectra_app_header.cpp): custom menu/header,
  document strip, Home signal, and welcome controls.
- [spectra_status_bar.cpp](../src/adapters/qt/components/spectra_status_bar.cpp): placeholder status
  fields and public setters with no production update path.
- [inspector_widget.cpp](../src/adapters/qt/panels/inspector_widget.cpp): undo-aware editing plus stale
  summary/axes-source and tab-synchronization gaps.
- [timeline_widget.cpp](../src/adapters/qt/panels/timeline_widget.cpp): disabled/null-model behavior.
- [settings_widget.cpp](../src/adapters/qt/panels/settings_widget.cpp): local settings store and
  incomplete shell/theme connection.
- [command_palette_dialog.cpp](../src/adapters/qt/panels/command_palette_dialog.cpp): palette
  construction, light fallback surfaces, and direct dialog behavior.
- [shortcut_widget.cpp](../src/adapters/qt/panels/shortcut_widget.cpp): shortcut rebinding TODO.
- [export_widget.cpp](../src/adapters/qt/panels/export_widget.cpp): built-in/plugin export UI and
  incomplete export payload/verification.
- [native_qt_docking_host.cpp](../src/adapters/qt/docking/native_qt_docking_host.cpp): empty document
  enumeration and move/detach ordering.
- [qt_workspace_bridge.cpp](../src/adapters/qt/qt_workspace_bridge.cpp): incomplete/nondeterministic
  host, document, and restore mapping.
- [split_view_container.cpp](../src/adapters/qt/split_view_container.cpp): flattened split
  reconstruction, focus selection, and document movement.
- [qt_runtime.cpp](../src/adapters/qt/qt_runtime.cpp): improved per-canvas integration/input/overlay
  ownership.
- [spectra_vulkan_window.cpp](../src/adapters/qt/spectra_vulkan_window.cpp) and
  [qt_input_router.hpp](../src/adapters/qt/qt_input_router.hpp): native canvas embedding and restricted
  event/key coverage.
- [ros_panel_manager.cpp](../src/adapters/qt/ros2/ros_panel_manager.cpp): explicit placeholder
  display and label-only inspector surfaces.
- [test_qt_visual_regression.cpp](../tests/qt/test_qt_visual_regression.cpp): QWidget grab and broad
  darkness assertions without the production Vulkan/panel matrix.
- [test_qt_automation.cpp](../tests/qt/test_qt_automation.cpp): live semantic checks for figure,
  menu, input, scoped capture/base64 methods and explicit assertions for the three unsupported fuzz
  methods.
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
- Per-canvas ImGui context, interaction, input, timing, font, and inspector routing are isolated.
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
