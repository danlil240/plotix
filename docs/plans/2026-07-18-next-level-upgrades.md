# Spectra next-level upgrade audit

This audit is measured from commit `959e727f`, which contains the complete user-authored
`main` stash plus its status-bar lifetime fix. None of that baseline work is counted below.

## Ranked opportunities

| Rank | Feature / optimization | Impact | Cost / risk | Status |
|---:|---|---|---|---|
| 1 | True GPU per-point scatter colormaps | Turns the existing C++, C, and Python API into a working visual encoding for a third variable | Medium Vulkan/WebGPU shader and upload risk | Selected |
| 2 | Native uncertainty and confidence bands | Adds a core scientific primitive for simulations, measurements, forecasting, and statistics | Medium model/renderer/serialization surface | Selected |
| 3 | Indexed nearest-point interaction | Removes the O(N) hover scan from every frame for sorted time series and scatter data | Low semantic risk with an exact unsorted fallback | Selected |
| 4 | Linked brushing across subplots | Makes coordinated multi-view exploration substantially more powerful | High: needs stable row identity across heterogeneous series | Candidate |
| 5 | Screen-space density aggregation | Preserves distributions when millions of points collapse into the same pixels | High: compute/raster aggregation pipeline | Candidate |
| 6 | Native heatmap, contour, and colorbar system | Fills a major scientific plotting gap and generalizes scalar color encodings | High: new 2D scalar-field primitive and UI | Candidate |
| 7 | Browser-style viewport history | Makes exploratory pan/zoom reversible independently of document undo | Medium: per-axes history lifecycle and gesture coalescing | Candidate |
| 8 | Async, cancellable, out-of-core imports | Keeps the UI responsive on multi-gigabyte CSV/Parquet data | High: background ownership, progress, and format strategy | Candidate |
| 9 | Polar axes and wind-rose plots | Expands engineering, RF, navigation, and directional analysis | High: transforms, ticks, interaction, and shaders | Candidate |
| 10 | Derived-series expression pipeline | Lets users create calculated columns and live transforms without preprocessing | Medium-high: expression UI, dependency graph, streaming updates | Candidate |

## Why these top three

### 1. GPU scatter colormaps

`ScatterSeries::color_values()` and the C/Python bindings already promise per-point scalar
coloring, but the 2D renderer contains an explicit TODO and draws every point with the series'
single color. Completing the GPU path closes a product correctness gap and enables dense
three-variable analysis without splitting data into many series.

### 2. Native uncertainty bands

Confidence intervals are a first-class scientific chart primitive, not merely decoration.
A dedicated series can validate aligned inputs, autoscale correctly, survive serialization,
and generate robust triangle geometry without asking callers to construct self-intersecting
polygons manually.

### 3. Indexed nearest-point interaction

The current tooltip query scans every point in every visible 2D series on every mouse-move
frame. Most engineering and time-series data is monotonic in X. Recording that invariant when
data is set allows binary-search narrowing while preserving the current full-scan fallback for
unsorted data. This targets interaction latency directly without changing rendered fidelity.

## Acceptance criteria

- A scatter series with scalar values visibly produces different GPU colors and honors its
  colormap and explicit range.
- An uncertainty band renders a filled lower/upper envelope, participates in autoscale, and
  round-trips through workspace serialization.
- Sorted-X nearest-point queries inspect only the screen-relevant range; unsorted data returns
  the same result as a full scan.
- New behavior has unit coverage, a runnable example, formatter checks, sanitizer coverage,
  and the complete GitHub CI matrix passes.

## Local verification

- Strict GCC and Clang builds with `-Wall -Wextra -Werror` complete successfully.
- 108/108 non-GPU C++ tests pass in release, ASan, and UBSan configurations.
- 16/16 Vulkan GPU tests and 5/5 golden-image tests pass on lavapipe.
- The scientific example renders both scalar-colored scatter points and an uncertainty band
  with Vulkan validation enabled.
- 401 Python tests pass; 55 optional/environment-specific tests skip.
