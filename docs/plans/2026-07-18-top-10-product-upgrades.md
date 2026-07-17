# Spectra: Top 10 Product Upgrades

This ranking combines the current architecture review, QA findings, product surface, and concrete
gaps in the implementation. Scores are relative (5 is best); `Delivery` rewards improvements that
can ship with strong automated coverage without destabilizing unrelated subsystems.

| Rank | Upgrade | User impact | Differentiation | Delivery | Why it matters |
|---:|---|:---:|:---:|:---:|---|
| 1 | Drag-and-drop, multi-series CSV/TSV import | 5 | 4 | 5 | Turns the empty desktop app into a useful plot in one gesture and removes repetitive one-column-at-a-time setup. |
| 2 | End-to-end epoch-scale X precision | 5 | 5 | 4 | Scientific, financial, ROS, and telemetry timestamps lose sub-second detail when coerced directly to `float`; correctness is non-negotiable. |
| 3 | Vulkan surface-loss recovery | 5 | 4 | 4 | A compositor restart, display hot-plug, or surface migration should recover transparently instead of leaving a permanently broken window. |
| 4 | First-class heatmaps and per-point colormaps | 5 | 4 | 3 | Heatmaps are a baseline scientific primitive; the current plugin example should graduate into the supported API and renderer. |
| 5 | Progressive million-point rendering | 5 | 5 | 2 | Background ingestion, multiresolution LOD, and bounded GPU uploads would remove the remaining large-data frame spikes. |
| 6 | Linked brushing and selection across subplots | 4 | 5 | 3 | Shared cursors already exist; coordinated point/range selection would turn dashboards into real exploratory analysis tools. |
| 7 | WebGPU 3D parity and browser packaging | 5 | 5 | 1 | A zero-install browser target is a major distribution advantage, but the missing 3D pipelines make this a larger, riskier program. |
| 8 | Remote streaming transport with backpressure | 4 | 5 | 2 | The local Unix-socket topic path is strong; secure network transport would unlock remote labs, robots, and production observability. |
| 9 | Out-of-process plugin isolation | 4 | 4 | 2 | Guarded callbacks catch exceptions, but a native plugin crash can still terminate the host; isolation is essential for an ecosystem. |
| 10 | Native screen-reader accessibility | 4 | 5 | 2 | Keyboard and contrast support are good foundations, but platform accessibility trees are needed for non-visual use. |

## Why the first three ship together

They improve one complete workflow: get real telemetry into Spectra, preserve its timestamps exactly,
and keep the plotting session alive across desktop/display disruptions. The combination delivers more
value than three isolated chart types while exercising clean seams already present in the repository:
the CSV loader, `Series`/`Axes` data model, camera-relative renderer, per-window Vulkan state, and UI
file-drop routing.

The implementation deliberately keeps GPU coordinates as small floats for throughput. A per-series
double X origin carries the absolute coordinate through autoscale, interaction, tooltips, markers,
SVG export, and the camera-relative upload step. This avoids doubling GPU bandwidth while preserving
sub-second resolution at Unix-epoch scale.

The Vulkan change treats `VK_ERROR_SURFACE_LOST_KHR` differently from an out-of-date swapchain: it
invalidates the window, destroys resources tied to the dead surface, recreates the native surface,
then builds a fresh swapchain without reusing the invalid old handle.

## Follow-up sequence

The recommended next product tranche is ranks 4–6: first-class heatmaps/colormaps, progressive
million-point rendering, then linked brushing. Together they would make Spectra substantially stronger
for exploratory analysis while building on the ingestion and precision foundation delivered here.
