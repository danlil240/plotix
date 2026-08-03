# Qt Inspector — Full Legacy Parity Migration (Option A)

**Date:** 2026-08-02
**Approach:** Full custom-widget parity
**Scope:** `src/adapters/qt/panels/inspector_widget.cpp/.hpp` and new reusable components in `src/adapters/qt/components/`

## 1. Header

- **Intent:** Make the Qt inspector visually and behaviorally identical to the legacy ImGui inspector across all four tabs (Figure, Series, Axes, Data) by replacing stock `QTabWidget`/`QFormLayout`/`QListWidget` with custom-painted, theme-aware Qt components.
- **Scope:** Qt inspector panel only. Model, undo, redraw, dialog services, and public API are preserved.
- **Non-goals:** No changes to Vulkan renderer, IPC protocol, data model, or public C++/Python API. No global state.
- **Acceptance criteria:**
  - Top tab bar matches legacy segmented control (rounded track, accent pill, hover states).
  - Figure page matches legacy title/subtitle, collapsible sections, color fields, and margin rows.
  - Series page matches legacy series browser (color dot, visibility eye, bulk actions) and series properties.
  - Axes page matches legacy X/Y/Z sections, ranges, grid/border, autoscale, statistics, reference lines.
  - Data page keeps existing `QtDataEditorWidget` but is wrapped/styled consistently.
  - Existing `qt_test_qt_panels` and `qt_test_qt_visual_regression` pass after object-name updates.
  - No validation errors, no frame hitches, works in multi-window and offscreen modes.
- **Risk assessment:** Largest surface area of the three options. Custom widget code is ~2–3 kLOC, many object names/types in tests change, focus/screen-reader behavior must be preserved. Mitigated by reusing existing model wiring and implementing in phases.

## 2. New custom components

All new components live in `spectra::adapters::qt` and consume `SpectraColors` / `SpectraGeometry` / `SpectraTypography` from `spectra_design_tokens.hpp`.

| Component | Legacy equivalent | Description |
|---|---|---|
| `SegmentedControl` | `ImGuiIntegration::draw_inspector` tab bar | 4-segment pill control with text, accent active fill, hover, and rounded track. |
| `PanelTitle` | `widgets::panel_title` | Bold primary title + optional muted subtitle. |
| `SectionHeader` | `widgets::section_header` | Chevron + uppercase label on a rounded surface band with hover and collapse/expand animation. |
| `PropertyRow` | `widgets::drag_field`, `widgets::text_field`, `widgets::combo_field` | Label-left / value-right on a tertiary input surface with `RADIUS_MD` and focus ring. |
| `RangeRow` | `widgets::drag_field2` | Two compact numeric inputs sharing one row. |
| `ColorField` | `widgets::color_field` | 28×28 rounded color swatch + label, opens a popover color picker. |
| `ToggleField` | `widgets::toggle_field` | Visible/Boolean switch with label. |
| `SeriesListView` | `Inspector::draw_series_browser` | Custom list with color dot, eye icon, label, action buttons, and bulk bar. |
| `ReferenceLineRow` | `Inspector::draw_reference_lines` | Color dot + label + delete button for reference lines. |

## 3. Page-by-page design

### Figure tab
- `SegmentedControl` at the top.
- `PanelTitle("Figure", "{n} axes, {m} series")`.
- Remove the existing `Summary` `QGroupBox` and title row; the legacy inspector does not edit the figure title inside the Figure tab, so the page only contains Background, Margins, Legend, and Quick Actions.
- `BACKGROUND` section: `ColorField("Background Color")`.
- `MARGINS` section: seven `PropertyRow`s using `QDoubleSpinBox` with suffix `" px"`.
- `LEGEND` section: `PropertyRow` with `QCheckBox` labeled `Show Legend`, `PropertyRow` for Position, Font Size, Padding, plus `ColorField`s for Background and Border.
- `QUICK ACTIONS` section: full-width `QPushButton("Reset to Defaults")`.

### Series tab
- `PanelTitle("Series", "{n} series")`.
- Bulk action bar (Copy, Cut, Delete) for multi-selection.
- `SeriesListView` replacing `QListWidget`.
- Selected series `PanelTitle("{Type}: {label}")` with color swatch and type badge.
- `APPEARANCE` section: `ColorField`, `ToggleField("Visible")`, Line Style, Marker, Marker Size (conditional), Opacity, Line/Point Width, Label `PropertyRow`.
- `PREVIEW` section: restyled `SparklineWidget`.
- `DATA` section: restyled statistics grid matching legacy info rows.
- `Back to Series List` button.

### Axes tab
- `PanelTitle("Axes", "{n} axes")`.
- Show the currently selected (or first) axes only, mirroring the legacy `SelectionType::Axes` path. A compact axes selector is provided only if the user needs to switch axes without canvas selection.
- `X AXIS`, `Y AXIS`, `Z AXIS` sections: label `PropertyRow` + `RangeRow` for limits.
- `GRID & BORDER` / `GRID & BOUNDING BOX` sections: `ToggleField`s, `ColorField`, `PropertyRow`s for width/tick length, `QComboBox` for 3D grid planes.
- `AUTOSCALE` section: mode combo + `Auto-fit Now` button.
- `STATISTICS` section: legacy info rows.
- `REFERENCE LINES` section: `ReferenceLineRow`s and add-row controls.

### Data tab
- `PanelTitle("Data")`.
- `PanelTitle("Data")` then the existing `QtDataEditorWidget`, with its table, buttons, and empty state styled to use token colors and spacing.

## 4. Theming

- All new widgets derive colors from `spectra_colors()` and react to `ThemeManager` palette changes (already wired for `QT-GAP-010`).
- Use `design_tokens.hpp` for radii, spacing, and typography; use `spectra_design_tokens.hpp` for the Qt color/geometry/typography wrappers.
- No hard-coded colors or sizes.

## 5. Implementation phases

1. ✅ **Primitives:** `SegmentedControl`, `PanelTitle`, `SectionHeader`, `PropertyRow`, `RangeRow`, `ColorField`, `ToggleField`, `SliderField`, `InfoRow`, `Separator`, `SeparatorLabel`, `SwatchLabel`, `SeriesListView`, `ReferenceLineRow`, `DragSpinBox`.
2. ✅ **Figure page parity** — `Show Legend` is a `ToggleField`; margins/legend rows use `PropertyRow` + `DragSpinBox`; colors use `ColorField`.
3. ✅ **Series page parity** — content-sized `SeriesListView`, `"{Type}: {label}"` title, swatch + type badge, legacy appearance order (Color, Visible, Line Style, Marker, Marker Size, Opacity, Line/Point Width, Label), `PREVIEW`, `DATA` stat rows grouped by `SeparatorLabel`, `Back to Series List`.
4. ✅ **Axes page parity** — `QTabWidget` replaced by `inspector_axes_selector` + `inspector_axes_stack`; `RangeRow` for limits (Range before Label, as in legacy); `ToggleField` for grid/border/bounding box; `ColorField` for grid color; legacy stat rows; reference lines.
5. ✅ **Data page styling** — `PanelTitle`, `SectionHeader` bands replacing `QGroupBox`, `PropertyRow` selectors, two-column action grid so nothing clips in the fixed-width drawer.
6. ✅ **Polish:** eased hover/active states on the nav rail, focus rings, token-only colors.

### Documented deviations from legacy

- **Axes title editor.** Legacy has no title field on the axes page. Qt keeps one as a bare `PropertyRow` above the axis sections to avoid losing the editing affordance.
- **Nav rail outer shadow.** Legacy paints a 4-layer shadow *outside* the rail, over the canvas. A Qt widget cannot paint outside its own geometry, so only the right-edge hairline is reproduced.
- **`Help` nav item** stays hidden in Qt (`set_button_visible(14, false)`); it is reachable from the menu.

## 5b. Nav rail parity

- `SpectraNavButton` is a direct port of `icon_label_button` (`src/ui/imgui/imgui_command_bar.cpp`): `SPACE_2 * scale` horizontal pill inset, `3 * scale` vertical inset, hover/active lift, two-ring outer glow, `control_surface_color`/`control_border_color`/`control_text_color` equivalents, top inner highlight, 3% icon growth when active, and the `NAV_RAIL_*_ALPHA_*` tokens.
- `SpectraNavRail` no longer paints a rounded glass card with a purple gradient and cyan accent edge; legacy renders the rail with `NoBackground` plus a single `border_subtle @ 0.52` hairline.

## 6. Verification

- Run `cmake --build build -j$(nproc)`.
- Run `ctest --test-dir build -R qt_test_qt_panels --output-on-failure`.
- Run `ctest --test-dir build -R qt_test_qt_visual_regression --output-on-failure`.
- Manual: launch `build/spectra` (or `xvfb-run -a ./build/spectra`), create figure, open inspector, switch tabs, edit margins/color/legend, add series, add axes, test resize and theme switch.
- Update object names/types in `tests/qt/test_qt_panels.cpp` where widget classes change.
