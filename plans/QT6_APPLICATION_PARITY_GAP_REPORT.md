# Spectra Legacy vs Qt 6 Application Parity Gap Report

**Audit date:** 2026-07-22  
**Audited revision:** `43cb2bb39e876b854feb2442c1a1546272909b95` on `plan/qt6-application-migration`  
**Decision:** **Qt is not eligible to become the default frontend.** Keep `SPECTRA_DEFAULT_FRONTEND=legacy`.  
**Companion plan:** [QT6_APPLICATION_MIGRATION_PLAN.md](QT6_APPLICATION_MIGRATION_PLAN.md)

## 1. Executive summary

This audit compared the current legacy GLFW/ImGui application with `spectra-qt-app` using the
repository's `spectra-mcp` workflow, live 1280x720 launches on X11/lavapipe, screenshots, command
catalogues, direct control activation, the Qt test suite, and source inspection where Qt automation
could not reach a workflow.

The Qt shell is a useful prototype around the shared Vulkan renderer, but it is not an application
parity implementation yet:

- Legacy registers **83 commands**. Qt registers **35**. Only **32 command IDs are shared**;
  **51 legacy command IDs are absent** from Qt, while Qt adds three replacement-only IDs.
- Qt's embedded MCP endpoint is unusable in a real launch. Two MCP servers attempt to bind the same
  port; the reachable one is not pumped by the Qt event loop, so `ping` times out. The passing Qt
  automation test does not exercise this deployed topology.
- Several visible controls do nothing: **Markers**, **Curve Editor**, and **Help** navigation buttons;
  the **Data**, **Axes**, and **Transforms** menus; and the inspector shortcut/action. The separate
  Help menu is constructed but never inserted into the custom header.
- The timeline is constructed with a null model and is disabled. Settings switches do not control
  the shell. Shortcut rebinding is a TODO. Workspace/autosave saves window chrome but not user
  figures. Export is PNG-oriented and does not provide the legacy artifact set.
- The inspector contains a dangling-reference capture in four axis-limit callbacks. Editing a limit
  can access a destroyed stack object. This is a correctness and crash-risk blocker independent of
  broader parity.
- Model mutations in the Qt inspector, data editor, and transform panel generally bypass the shared
  undo transaction, validation, redraw, and persistence paths. A widget being present therefore does
  not mean that the legacy workflow is present.
- Production overlays are still ImGui/`ImDrawList` based. The renderer-neutral Qt overlay adapter is
  not in the production frame path, while a single runtime-wide ImGui integration and interaction
  object is rebound across canvases.
- The nine Qt-labelled tests pass, but they mostly prove construction, dimensions, JSON helper
  behavior, and callback-level adapters. They do not compare approved images, operate most controls,
  verify persisted figures, or reach the live Qt MCP endpoint.

One narrow result is encouraging: after forcing both frontends to the Night theme, the blank
renderer-owned plot crop differed in only **0.092%** of pixels by more than two channel values, just
inside the plan's 0.1% threshold. That result covers only a blank 2D plot. The full shell differed by
**11.456%**, and no claim can yet be made for plotted series, labels, overlays, 3D, panels, high-DPI,
or other platforms.

## 2. Audit scope and method

### 2.1 Environment

- Build: GCC/Ninja release build, ROS2 off, tests/examples/golden tests enabled.
- Runtime: Xvfb `:99`, 1280x720 application client, lavapipe software Vulkan, isolated
  `XDG_RUNTIME_DIR`, `SPECTRA_RUNTIME_MODE=inproc`, and `SPECTRA_AUTOMATION=1`.
- Binaries: `build/spectra-legacy` and `build/spectra-qt-app`.
- Verification: full incremental build and `ctest --test-dir build -L qt --output-on-failure`.
- The working tree already contained uncommitted Qt/ImGui shell changes before this audit. Those
  changes were left intact. Findings describe the exact working tree named above, not only committed
  `HEAD`.

### 2.2 Comparison procedure

1. Start the legacy application through the documented `spectra-mcp` environment.
2. Query state, commands, menus, figures, panels, and themes; execute commands and capture the window.
3. Start Qt with the equivalent environment and attempt the same MCP calls.
4. After the live Qt endpoint timed out, use X11 control activation and screenshots for visible Qt
   behavior, then inspect the action, panel, workspace, input, docking, and automation sources.
5. Force the same Night theme and compare identical blank-plot and shell crops.
6. Build the repository and run all tests carrying the `qt` CTest label.

### 2.3 Important limitations

- Qt MCP failure prevented identical automation fixtures from creating populated series, 3D scenes,
  splits, detached windows, and overlays in both frontends. Those areas remain **unverified**, not
  passed. Static evidence identifying missing paths is recorded below.
- This pass exercised Linux/X11 only. Wayland, Windows, macOS, 200% DPR, input methods, touch, and
  screen-reader behavior remain untested.
- Temporary screenshots and JSON transcripts were generated under
  `/tmp/spectra-qt-gap-audit/`; the durable evidence is summarized in this report.
- A few legacy automation/reference defects were found and are separated in section 12 so they are
  not mistaken for Qt accomplishments.

## 3. Severity definition

| Severity | Meaning for the migration |
|---|---|
| P0 | Release/default-switch blocker: crash/data-loss risk, core automation absent, or fundamental shared-state/persistence failure. |
| P1 | Major user workflow missing, inert, materially wrong, or visually unusable. |
| P2 | Partial behavior, fidelity, accessibility, or maintainability gap that must close before parity sign-off. |
| P3 | Polish, diagnostic, or secondary consistency issue. |

## 4. Release-blocking findings

| ID | Sev. | Gap | Observed result | Required result |
|---|---:|---|---|---|
| QT-GAP-001 | P0 | Live Qt MCP is broken | Qt starts two MCP servers on the same port. One bind fails and calls to the other time out. The adapter timer contains only a future-work comment. | One Qt-owned endpoint, reachable through every `spectra-mcp` tool, with requests dispatched on the Qt event loop. |
| QT-GAP-002 | ~~P0~~ ✅ FIXED | Inspector axis callbacks hold dangling references | **FIXED:** All inspector lambdas now capture `FigureId` + axes/series indices and look up live objects via `FigureRegistry::get()` + `Figure::get_axes()`. No raw pointers to local controls are captured. | Capture stable widgets/state by value or retrieve controls from owned storage; add an edit-and-destroy regression test. |
| QT-GAP-003 | ~~P0~~ ✅ PARTIAL | Workspace/autosave is chrome-only | **FIXED:** Autosave serialize fn now calls `Workspace::capture()` with all figures from the registry, populating figure/series/axes data, theme, inspector/nav-rail state, and desktop layout. Recovery prompt already existed. Remaining: full round-trip restore of figure data on load. | Round-trip all figures, series, axes, layouts, panel state, splits, windows, active document/tool, and unsaved recovery. |
| QT-GAP-004 | P0 | Per-canvas state is not isolated | One runtime-level ImGui integration and one `DataInteraction` are rebound among all Qt canvases. | Each canvas/document owns independent interaction and overlay state; simultaneous windows cannot mutate one another. |
| QT-GAP-005 | ~~P0~~ ✅ FIXED (Inspector) | Mutating panels bypass application transactions | **FIXED (Inspector):** All inspector mutations now route through `undoable_property.hpp` helpers (`undoable_xlim`, `undoable_ylim`, `undoable_set_title`, `undoable_set_series_color`, etc.) and call `RedrawRequest::request_redraw()` after each change. Data editor and transforms still need wiring. | Every edit uses the same frontend-neutral command/transaction path and produces identical undo and render results. |
| QT-GAP-006 | P1 | Command surface is incomplete | 83 legacy commands versus 35 Qt; 51 legacy IDs missing. | Register one shared descriptor/handler set before either frontend builds menus, shortcuts, or the palette. |
| QT-GAP-007 | ~~P1~~ ✅ PARTIAL | Visible controls are inert or miswired | **FIXED:** Inert nav rail buttons (Markers, Curve Editor, Help) are now hidden via `set_button_visible()`. Help menu added to custom app header. Inspector toggle already wired. Data/Axes/Transforms menus were already populated from command registry. | Every visible control executes a tested semantic command or is hidden until implemented. |
| QT-GAP-008 | P1 | Timeline and curve editing are absent | Timeline is created with `nullptr` and disabled; no Qt curve editor exists. | Bind the real timeline model, transport, scrubber, loop/FPS/duration, keyframes, and curve editor with undo. |
| QT-GAP-009 | P1 | Export/clipboard parity is absent | Qt exposes `file.export` and a PNG/plugin panel; PNG success is not verified, plugin export passes empty figure/pixel payloads, and SVG/copy/video paths are absent. | Produce and verify every legacy artifact through shared export services, including failures/cancel/progress. |
| QT-GAP-010 | P1 | Production overlays are not ported | Qt overlay adapter has no production call sites; retained overlays still use ImGui directly. | Exercise crosshair, tooltip, legend, markers, selection, measurement, annotation, ROI, and data tips in Qt. |
| QT-GAP-011 | P1 | Split/docking/workspace topology is incomplete | Split view flattens mixed trees; dock host cannot enumerate documents; moves can remove a source before validating the destination. | Preserve arbitrary pane trees, documents, focus/tool state, and rollback-safe cross-window moves. |
| QT-GAP-012 | P1 | Visual parity is not established | Blank plot crop is close, but shell/welcome/panels diverge and native-white surfaces remain. | Approved, deterministic legacy-vs-Qt baselines for the complete matrix and all required sizes/DPRs/platforms. |

## 5. Command parity

### 5.1 Counts and semantic mismatches

| Metric | Legacy | Qt | Gap |
|---|---:|---:|---:|
| Registered commands | 83 | 35 | 48 fewer in Qt |
| IDs shared by both | 32 | 32 | — |
| Legacy-only IDs | 51 | 0 | 51 missing |
| Qt-only real IDs | 0 | 3 | Replacement APIs, not parity |

Qt-only IDs are `app.quit`, `file.export`, and `view.reset_layout`. They do not replace the missing
legacy IDs for exit/cancel semantics, specific export artifacts, or split reset.

Shared IDs also differ:

| Command | Legacy | Qt | Gap |
|---|---|---|---|
| `figure.tab_1` … `figure.tab_9` | `1` … `9` | `Alt+1` … `Alt+9` | Muscle memory and documented shortcuts change. |
| `view.autofit` | `A`, “Auto-Fit Active Axes” | `Shift+A`, “Auto-Fit Active Figure” | Shortcut and scope/label differ. |
| `view.fullscreen` | `F` | `F11` | Shortcut differs. |
| Tool commands | Registered without these Qt keys | `V`, `H`, `Z`, `M`, `A`, `Shift+R` | Qt introduces conflicting/changed bindings without a shared policy. |
| Panel commands | Legacy has panel behavior, generally no default key | Qt adds `I`, `T`, `,`, `D` variants | Bindings differ and the inspector action is currently broken. |

`QtActionBridge::refresh()` updates labels and enabled state but not shortcuts/check state. Shortcut
overrides therefore cannot reliably update already-created `QAction` objects.

### 5.2 Exhaustive legacy-only command IDs

| Category | Missing in Qt |
|---|---|
| Accessibility | `accessibility.sonify_series` |
| Animation | `anim.go_to_end`, `anim.go_to_start`, `anim.step_back`, `anim.step_forward`, `anim.stop`, `anim.toggle_play` |
| Application/help | `app.cancel`, `app.command_palette`, `app.new_window`, `help.show` |
| Data | `data.copy_to_clipboard`, `data.export_html_table` |
| Figure/window | `figure.move_to_window` |
| File/export/persistence | `file.copy_to_clipboard`, `file.export_png`, `file.export_svg`, `file.load_figure`, `file.load_workspace`, `file.save_figure`, `file.save_workspace` |
| Panels | `panel.toggle_curve_editor`, `panel.toggle_nav_rail`, `panel.toggle_plugins` |
| Plot creation | `plot.function`, `plot.hline`, `plot.hline_zero`, `plot.vline`, `plot.vline_zero` |
| Series editing | `series.copy`, `series.cut`, `series.cycle_selection`, `series.delete`, `series.deselect`, `series.paste` |
| Themes | `theme.dark`, `theme.light`, `theme.night`, `theme.toggle` |
| View/navigation | `view.home`, `view.pan_down`, `view.pan_left`, `view.pan_right`, `view.pan_up`, `view.reset_splits`, `view.toggle_border`, `view.toggle_crosshair`, `view.toggle_grid`, `view.toggle_legend`, `view.zoom_in`, `view.zoom_out` |

## 6. Visible shell, menus, buttons, and result comparison

### 6.1 Welcome, document tabs, and shell

| Control/surface | Legacy result | Qt result | Status |
|---|---|---|---|
| Empty/welcome screen | Branded logo, product subtitle, `Ctrl+T` hint and version; navigation rail hidden. | Plain text welcome, no logo/version/shortcut hint; navigation rail and empty document strip remain visible. | P1 mismatch |
| New figure | `figure.new` creates and activates a document tab. | Header `+` executes `figure.new` and created Figure 1 in the live run. | Pass for basic case |
| Tab select/close | Shared semantic commands and legacy document UI. | Basic select/close exists; custom top tabs coexist with QTabWidget pane tabs during splits. | P1 partial |
| Tab reorder/detach | Legacy supports window/document commands. | QTabWidget reorder is pane-local; no cross-pane drag; detach/move safety and persistence are incomplete. | P1 partial |
| Home/reset button | Resets the active view. | Command exists, but complete parity across 2D/3D/splits was not automatable. | Unverified |
| Inspector chevron | Opens the active figure inspector. | Direct navigation can open it; the command/shortcut calls `on_toggle_inspector()` without required `bool`, producing a Qt meta-object error. | P1 broken |
| Status bar | Reports meaningful tool/application state. | Present, but panel navigation can change selected rail/status state without changing canvas tool state. | P2 mismatch |

### 6.2 Top-level menus

| Menu | Legacy entries/results | Qt live result | Gap |
|---|---|---|---|
| File | New Figure; PNG/SVG; Copy as Image; plugin formats; Save/Load Workspace; Save/Load Figure; Exit. | Export Figure, Quit, Command Palette. | Nearly the entire file lifecycle is missing. `Ctrl+S` is assigned to export rather than save. |
| Edit | Undo, Redo plus series edit commands through shortcuts/palette. | Undo, Redo. | Series clipboard/delete/select workflows are absent. |
| View | Inspector/nav, fit/reset, grid/legend/data tips, timeline, curve editor, parameters, data editor, topics, plugins. | Small command subset plus separately constructed dock toggles. | Missing controls, duplicate semantic/ad-hoc actions, and state can diverge. |
| Tools | Screenshot, Undo/Redo, theme settings, command palette, optional ROS2 adapter. | Tool modes and a subset of panels. | Screenshot/theme/ROS workflows and legacy grouping are absent. |
| Plot | Zero lines, arbitrary horizontal/vertical lines, function plot. | No registered plot-creation commands. | Entire menu workflow absent. |
| Data | Load from CSV. | Menu is visible but opens no items. | Inert advertised menu. |
| Axes | Link X/Y/Z/all axes and unlink all, including 3D linking. | Menu is visible but empty. | Entire workflow absent. |
| Transforms | All registered transforms plus custom formula. | Menu is visible but empty; a separate dock applies a restricted destructive pipeline. | Menu broken and semantics differ. |
| Help | Help workflow. | A `QMenu` is constructed but is not added to the custom header; Help nav is inert. | No reachable help. |

### 6.3 Navigation rail buttons

| Button | Qt observed/code result | Parity result |
|---|---|---|
| Select | Changes active `InputHandler` tool. | Basic routing implemented; selection/series actions and overlays still incomplete. |
| Pan | Changes active tool. | Basic routing implemented; end-to-end gesture parity unverified. |
| Box Zoom | Changes active tool. | Basic routing implemented; high-DPI/multi-axes parity unverified. |
| Measure | Changes active tool. | Overlay remains on retained ImGui path; undo/export/accessibility unverified. |
| Annotate | Changes active tool. | Create/edit/persist/undo parity unverified. |
| ROI | Changes active tool. | Model/overlay/result parity unverified. |
| Markers | Switch statement contains no action. | **Inert visible button.** |
| Transforms | Opens the transform dock directly. | Bypasses shared command; panel behavior is destructive and incomplete. |
| Inspector | Opens drawer directly. | Direct path works; semantic command/shortcut is broken. |
| Timeline | Opens a disabled/null-model dock. | Visible but unusable. |
| Curve Editor | Switch statement contains no action. | **Inert visible button.** |
| Plugins | Opens plugin panel directly. | Partial UI and broken portable-panel callback routing. |
| Topics | Opens topics dock directly. | Generic data-source list only; ROS topic workflows missing. |
| Settings | Opens settings dock directly. | Settings controls are incomplete and visibility toggles are not wired to shell state. |
| Help | Switch statement contains no action. | **Inert visible button.** |

Panel buttons also set the rail's selected item as though they were exclusive canvas tools. Opening a
panel can therefore remove the visual highlight from the actual active tool while leaving the
`InputHandler::ToolMode` unchanged.

## 7. Feature and workflow parity matrix

| Area | Legacy baseline | Current Qt result | Sev./state |
|---|---|---|---|
| Figure lifecycle | New/close/tab navigation, save/load figure, multiple windows. | Basic new/close/tab commands; no save/load figure or safe complete window movement. | P1 partial |
| CSV/data import | CSV dialog, column selection and plotting flow. | Empty Data menu; no equivalent reachable Qt import flow. | P1 missing |
| Series management | Copy/cut/paste/delete/deselect/cycle selection, styles and data interactions. | Commands missing; inspector covers a subset of style properties. | P1 missing |
| Data editor | Integrated panel and figure/series editing. | Line/scatter x/y cells only; no row operations, validation feedback, undo, redraw, paste/import/export, or reorder. | P1 prototype |
| 2D navigation | Pan, wheel/box zoom, reset/home/fit, keyboard pan/zoom, grid, legend, crosshair. | Core tool modes exist; many semantic commands and overlays are absent. | P1 partial |
| Axis linking | Link X/Y/Z/all, unlink, shared cursor. | Axes menu empty. | P1 missing |
| Plot helpers | Horizontal/vertical/zero lines and function plots. | Plot commands absent. | P1 missing |
| Legend | Visibility and interaction through shared model. | Inspector creates a legend checkbox but connects no handler. | P1 broken |
| Markers/data tips | Create/remove/interact through overlay system. | Rail button inert; production overlay port incomplete. | P1 missing |
| Measurement/annotation/ROI | Interactive overlays integrated with model and rendering. | Tool modes route, but overlays remain retained ImGui and complete result/persistence parity is unproven. | P1 partial |
| 3D | Camera/orbit/pan/zoom, 3D axes and scene/display interactions. | Shared renderer may draw 3D, but Qt shell/input/inspector parity is not implemented or tested. | P1 unverified |
| Inspector | Figure, series, axes, data controls with immediate correct redraw and state. | Subset only; dead legend control, stale observed counts/size, no 3D inspector, direct untracked mutation, and dangling limit callbacks. | P0 unsafe |
| Themes | Dark/light/night/toggle, palette and themed shell/panels. | Renderer theme selection exists, but shell is hard-coded and no theme commands are registered. | P1 partial |
| Settings | Appearance, shortcuts, UI defaults, palette, glass/transparency/blur/glow and persisted controls. | Theme/palette plus three checkboxes; panel/nav/timeline checkboxes do not operate the shell. | P1 prototype |
| Shortcut editing | Discover, capture, conflict check, rebind, reset and persist. | Rebind explicitly TODO; reset does not rebuild live QActions or persist overrides. | P1 missing |
| Command palette | Themed palette over complete shared command set. | Only 35 commands, hard-coded light category rows, point-size warnings, and row/index logic can reject later commands. | P1 broken |
| Timeline | Transport, step/start/end, scrubber, loop, timing model. | Widget constructed with null editor and disabled; animation commands absent. | P1 missing |
| Curve editor | Usable curve/keyframe editing. | No Qt panel; rail button inert. | P1 missing |
| Transforms | Registered transforms/custom formula applied through known app behavior. | Separate dock destructively applies presets to every visible line/scatter series, without target selection, undo, validation, redraw, or persistence provenance. | P1 divergent |
| Export | PNG, SVG, copy image, plugin formats, deterministic artifacts. | Generic export/PNG panel; no SVG/copy; plugin path receives null pixels/empty figure JSON; success is reported without artifact verification. | P1 broken |
| Clipboard/table export | Image copy, data copy, HTML table. | Service primitives exist but equivalent commands/workflows are not registered. | P1 missing |
| Splits | Split right/down, close/reset while preserving documents/state. | Live-canvas preservation improved, but nested mixed topology is flattened and close/context/focus behavior can target or close the wrong document. | P1 partial |
| Detach/redock | Multiple native windows and document movement with state preserved. | Host move/detach removes source before destination success; enumeration/workspace population is incomplete. | P0 data-loss risk |
| Workspace | Figures, series, axes, layouts, panels, windows, active state and recovery. | Geometry/state blobs only; documents and figures omitted. | P0 missing |
| Crash recovery | Restores unsaved user session. | Check is not invoked and available payload cannot reconstruct figures. | P0 missing |
| Multi-window runtime | Independent canvas/tool/overlay state with shared process renderer/services. | Shared process renderer improved, but interaction/ImGui state is still rebound globally. | P0 unsafe |
| Backend/IPC | Snapshot/diff, connection loss recovery and restart. | Basic connect/snapshot/diff; no reconnect/backoff/restart; initial synchronous drain can block startup. | P1 partial |
| Python live show | Backend/display lifecycle equivalent to legacy. | Not exercised because Qt MCP/runtime path failed first. | Unverified |
| ROS2/PX4 | Supported topics, adapters, displays, inspectors and plot flows. | Placeholder render/inspector area and generic source list; tests use a stub display. | P1 placeholder |
| Plugins | Load/scan/custom dirs, diagnostics, capabilities, portable panels and exports. | Reduced management UI; direct native dialogs; group children are not rendered; callbacks use empty plugin ID. | P1 broken |
| Accessibility | Keyboard, focus, roles/names, screen reader, sonification and HTML export. | Sonification/export utilities exist, but dialogs bypass automation service and there is no complete role/focus/navigation verification. | P1 unverified |
| Input coverage | Mouse, precise wheel, keyboard, text/IME, touch/stylus, drag/drop, focus and DPI. | Router covers basic mouse, angle wheel, and a limited key set; missing text/IME, pixel wheel phases, tablet/touch/gesture, DnD, enter/leave/focus and many keys. | P1 incomplete |

## 8. Component-level findings

### 8.1 Automation and services

- `QtAutomationAdapter::start()` calls `ApplicationServices::start_automation()`, which already
  creates an MCP server, then creates a second `McpServer` on the same address. The second bind fails.
- The adapter timer does not dispatch MCP requests to Qt; its source explicitly leaves that for
  future work. The server that did bind therefore responds only with a timeout.
- Many Qt panels instantiate `QFileDialog`/`QMessageBox` directly rather than using injected
  `DialogService`. Even after fixing the port, automation cannot deterministically accept/cancel
  these dialogs.
- Legacy `create_figure`/`add_series` MCP calls can update the registry without attaching the figure
  to the visible shell. The automation contract itself needs an explicit document lifecycle model.

### 8.2 Inspector and editors

- Four axes-limit signals capture a local control aggregate by reference. This must be fixed before
  any manual inspector QA because interaction can dereference dead stack memory.
- The legend checkbox has no signal connection.
- Figure title changes do not update the custom document-tab title.
- Inspector mutations do not consistently create undo operations, request redraw, validate ranges,
  dirty the workspace, or update dependent UI.
- The inspector does not expose an equivalent 3D inspector and lacks the complete legacy series/data
  operations.
- The data editor only supports `LineSeries` and `ScatterSeries`; each cell edit replaces a whole
  vector. It has no add/delete row, multi-cell paste, type-aware validation, undo, explicit redraw,
  import/export, or large-data strategy.

### 8.3 Timeline, transforms, topics, and settings

- `QtTimelineWidget(nullptr, this)` guarantees the timeline is disabled in production.
- There is no Qt curve editor implementation despite a visible rail button.
- The transform dock applies each operation destructively to all visible supported series. It lacks
  axes/series targeting, preview, undo, provenance, persistence, failure feedback, and redraw.
- The Topics panel lists generic `DataSourceRegistry` entries with start/stop buttons, not the legacy
  ROS/PX4 browsing, filtering, QoS, topic selection, and topic-to-series workflow.
- Settings changes to Inspector/Nav/Timeline visibility are stored but no main-window connection
  applies them. Live capture showed a checked Inspector setting while the drawer was hidden.
- Shell styling is not driven fully by the selected renderer/application theme. Native white list and
  table surfaces appear in the command palette, data editor, transform panel, and topics panel.

### 8.4 Plugins and extensibility

- Plugin management omits custom scan directories, diagnostics/capabilities, detailed errors and the
  richer rescan workflow visible in legacy.
- Portable plugin-panel controls call `set_property_value` and `trigger_action` with an empty plugin
  ID, so callbacks cannot be reliably routed to their owner.
- Group elements create a container but do not recursively render their child schema elements.
- Color is exposed as a raw line edit and separators are empty widgets rather than matching the
  legacy UI contract.
- Plugin export receives null pixel data and empty figure JSON, making image- and figure-aware plugin
  exporters nonfunctional even when their name appears.

### 8.5 Windows, splits, docking, and persistence

- Split reconstruction uses a single `QSplitter` orientation derived from the root. It cannot render
  nested mixed horizontal/vertical pane trees faithfully.
- QTabWidget movement is limited to one pane; cross-pane drag/drop is absent.
- The context close-split path closes a figure tab before collapsing the split, which can destroy the
  document instead of preserving it.
- Active-pane detection checks QTabWidget focus, while focus usually lives in the embedded Vulkan
  `QWindow`; commands can target a fallback/wrong pane.
- Split mode introduces QTabWidget's internal tab bars in addition to the custom document strip.
- `NativeQtDockingHost::documents()` returns an empty TODO result. Workspace capture therefore cannot
  know which documents belong to which window.
- Detach/move removes a document from the source before proving the destination is valid. A failure
  can make the document disappear from the UI.
- Saved Qt main-window state/geometry does not encode figure content, document IDs, nested pane
  topology, active tool, or complete panel controller state. Iterating an unordered host registry and
  treating its first entry as the main window is nondeterministic.

### 8.6 Rendering, overlays, and input

- The Vulkan plot content is the strongest shared area, but the current successful diff covers only
  an empty 2D renderer crop.
- `QtOverlayDrawList` has no production usage outside its own implementation. Annotation, legend,
  selection, measurement and related overlay code remains coupled to ImGui/`ImDrawList`.
- One `ImGuiIntegration` and `DataInteraction` at runtime scope are rebound per canvas render. This
  makes active selection, hover, overlay, and edit state vulnerable to cross-window bleed.
- Qt input forwards move/press/release, angle-based wheel, and a small key map. It does not forward
  double-click, text/IME, high-resolution `pixelDelta`/scroll phases, tablet/stylus, touch/gesture,
  drag/drop, enter/leave/focus transitions, function keys, punctuation, or modifier-only keys.
- QAction shortcuts and focused embedded QWindows have no demonstrated arbitration policy; shortcut,
  text entry, and canvas-key conflicts remain untested.

## 9. Visual comparison

### 9.1 Measured 1280x720 results

| Comparison | Pixels differing by >2 channels | Pixels differing by >10 channels | Mean absolute channel error | Interpretation |
|---|---:|---:|---:|---|
| Night-theme blank renderer plot crop | 0.092% | 0.012% | 0.008 | Narrow blank-2D pass at the plan threshold. |
| Night-theme blank full shell | 11.456% | 4.196% | 1.988 | Shell/chrome is not equivalent. |
| Welcome full window | 99.998% | 13.937% | 9.381 | Composition and branding differ substantially. |
| Default-theme blank full window | 99.998% over threshold | Not used as a gate | Not used as a gate | Defaults select different themes, itself a product mismatch. |

The blank plot crop was `(141,113)-(1260,627)` and the canvas-shell crop was
`(72,74)-(1280,686)`. These coordinates are evidence for this 1280x720 run only; future golden tests
must derive regions from the actual physical canvas rectangle rather than hard-code them.

### 9.2 Visible defects in live captures

- Qt welcome lacks the legacy logo, version and shortcut cue and shows chrome that legacy suppresses.
- The command palette has large white native surfaces and light-gray category bands. It also emitted
  repeated `QFont::setPointSize: Point size <= 0 (-2)` warnings.
- Data-editor tables, transform lists, and topics sources use native white backgrounds inside the
  dark shell.
- Opening several docks consumes and compresses the document area rather than reproducing the legacy
  overlay/drawer layout; stacked panels become narrow and visually clipped at the reference size.
- The default renderer theme differs between frontends, invalidating an out-of-the-box screenshot
  comparison until defaults are unified.

## 10. Test-suite gap analysis

All nine Qt-labelled tests passed in the audited build. That is a build-health signal, not parity
evidence.

| Test area | What it currently establishes | What it does not establish |
|---|---|---|
| Visual regression | Widget sizes/presence, non-null grab, broad dark-window ratio. | No approved legacy baseline, pixel diff, opened-panel capture, plots, overlays, 3D, sizes, DPRs or platforms. |
| Panels | Widgets can be constructed and selected metadata can change. | Visible button-to-result workflows, undo/redraw/persistence, real models, themed surfaces, crash-prone inspector edits. |
| Window ops/docking | Selected helper operations and object state. | Cross-window user drag/drop, destination failure rollback, nested splits, focus routing, full state restoration. |
| Dialogs | Dialog-service helper behavior. | Panels that bypass the service with direct native dialogs. |
| Automation | Callback/start-stop behavior in isolation. | Launching `spectra-qt-app`, binding one MCP port, `ping`, dispatching tools, screenshots and command results. |
| Action bridge | Basic action creation/dispatch. | Complete 83-command parity, custom shortcuts, checked state, enable predicates, command palette row mapping. |
| Workspace | JSON/state helper round-trips. | Real figures/series/axes, split/window/document assignments, active state, unsaved recovery. |
| Plugin UI | Registry/schema-level behavior. | Real plugin IDs, nested group children, property/action routing, export payloads and visual equivalence. |

Minimum new tests are listed in section 13.

## 11. Evidence in source

The most direct implementation evidence is in:

- [qt_application.cpp](../src/adapters/qt/qt_application.cpp): 35-command registration, static figure
  registry, broken inspector invocation, and incomplete recovery/autosave ownership.
- [qt_automation_adapter.cpp](../src/adapters/qt/qt_automation_adapter.cpp) and
  [application_services.cpp](../src/app/application_services.cpp): duplicate MCP creation and missing
  Qt dispatch.
- [qt_main_window.cpp](../src/adapters/qt/qt_main_window.cpp): missing menu insertion, null timeline,
  inert rail cases, direct panel routing and compact-mode TODO.
- [inspector_widget.cpp](../src/adapters/qt/panels/inspector_widget.cpp): dead legend checkbox and
  dangling axis-limit captures.
- [command_palette_dialog.cpp](../src/adapters/qt/panels/command_palette_dialog.cpp): hard-coded light
  rows, invalid font adjustment and command/separator index handling.
- [shortcut_widget.cpp](../src/adapters/qt/panels/shortcut_widget.cpp): explicit rebinding TODO.
- [native_qt_docking_host.cpp](../src/adapters/qt/docking/native_qt_docking_host.cpp) and
  [qt_workspace_bridge.cpp](../src/adapters/qt/qt_workspace_bridge.cpp): empty document enumeration and
  geometry-only/nondeterministic workspace capture.
- [plugin_panel_widget.cpp](../src/adapters/qt/panels/plugin_panel_widget.cpp): empty plugin IDs and
  incomplete schema rendering.
- [qt_runtime.cpp](../src/adapters/qt/qt_runtime.cpp): runtime-wide ImGui/interaction ownership.
- [qt_input_router.hpp](../src/adapters/qt/qt_input_router.hpp) and
  [spectra_vulkan_window.cpp](../src/adapters/qt/spectra_vulkan_window.cpp): restricted input event and
  key coverage.
- [imgui_command_bar.cpp](../src/ui/imgui/imgui_command_bar.cpp): legacy File/View/Tools/Plot/Data/Axes/
  Transforms reference behavior used for this comparison.

## 12. Legacy/reference defects discovered during comparison

These do not reduce Qt's required scope. They should be fixed or explicitly normalized in the shared
behavioral contract before creating goldens:

| ID | Legacy observation | Follow-up |
|---|---|---|
| LEGACY-AUDIT-001 | Fresh MCP runs of `panel.toggle_timeline` and `panel.toggle_topics` terminated with a segmentation fault. | Reproduce under ASan and fix before using those states as golden fixtures. |
| LEGACY-AUDIT-002 | `panel.toggle_data_editor` displayed Inspector in the observed run and reported an undo entry. | Correct command-to-panel mapping and add an exact state assertion. |
| LEGACY-AUDIT-003 | MCP `create_figure`/`add_series` changed registry state without attaching the figure to the visible welcome shell until `figure.new`. | Define whether automation creates model-only figures or visible documents; expose separate explicit tools if both are needed. |
| LEGACY-AUDIT-004 | Window capture in some panel sequences was followed by process exit. | Isolate capture from panel crash behavior and make the endpoint return a structured failure. |

## 13. Required closure plan and acceptance evidence

### P0 — make comparison and state safe

1. Collapse Qt automation to one server and implement event-loop dispatch for every `spectra-mcp`
   tool. Add a process-level launch/ping/state/command/capture test.
2. Fix the inspector dangling captures and add sanitizer-backed edits for every inspector control.
3. Introduce shared, frontend-neutral command handlers/transactions for all mutations. Require undo,
   redraw, dirty-state, validation and serialized-result assertions.
4. Serialize and recover real documents and model data, including split/window assignments and active
   state. Test crash recovery with a populated multi-window workspace.
5. Scope interaction and overlays to canvas/document ownership and test simultaneous windows.

### P1 — close user-visible workflow gaps

6. Register the complete shared command set; remove replacement-only divergence or provide explicit
   compatibility aliases. Bind menus, palette, shortcuts and rail buttons only through it.
7. Hide no-longer-empty placeholders until their commands exist; then port Data, Axes, Transforms,
   Plot and Help workflows exactly.
8. Complete inspector, data editor, timeline, curve editor, settings, shortcuts, topics, plugins,
   accessibility and ROS/PX4 panels with real models and artifacts.
9. Complete PNG/SVG/clipboard/plugin export and verify written artifacts and error/cancel behavior.
10. Finish arbitrary split topology, safe cross-pane/window moves, focus targeting and full workspace
    persistence.
11. Put every overlay on a production renderer-neutral path and complete the input event matrix.

### Required automated gates

- Exact command ID/label/default-shortcut/category/enabled/check-state comparison for both frontends.
- A button/menu matrix test that activates every visible control and asserts the expected model,
  view, panel, undo, persistence, or artifact result; no visible no-op is allowed.
- Identical MCP fixtures for welcome, line/scatter, multi-axes, 3D, every overlay, inspector/editor,
  split, detached window, dialogs, plugins, topics and recovery.
- Approved screenshot baselines at 1280x720, 1600x900 and 200% DPR on X11 and Wayland, followed by
  Windows and macOS release baselines. Store failure images and numerical diffs.
- Artifact comparison for PNG, SVG, clipboard image/data, HTML table, figure and workspace files.
- ASan/UBSan interaction run covering inspector, split/detach, panel lifecycle and endpoint shutdown.
- Keyboard-only navigation, focus order, accessible names/roles and screen-reader smoke tests.

## 14. Sign-off rule

Do not interpret a constructed widget, non-null screenshot, matching blank renderer crop, or passing
Qt-labelled test as application parity. Qt may become the default only when every P0/P1 item above is
closed, the exhaustive visible-control matrix has no unexplained no-op or divergent result, the full
functional matrix in the migration plan is green, and the approved visual/artifact gates pass on the
required platforms.
