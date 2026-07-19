#pragma once

// ─── Spectra Easy API ───────────────────────────────────────────────────────
//
// The simplest possible interface for scientific plotting.
// One header, zero boilerplate. Works identically in inproc and multiproc modes.
//
//   #include <spectra/easy.hpp>
//
//   int main() {
//       std::vector<float> x = {0, 1, 2, 3, 4};
//       std::vector<float> y = {0, 1, 4, 9, 16};
//
//       spectra::plot(x, y, "r--o");          // MATLAB-style format string
//       spectra::title("My Plot");
//       spectra::xlabel("X"); spectra::ylabel("Y");
//       spectra::show();
//   }
//
// ─── Progressive Complexity ─────────────────────────────────────────────────
//
// Level 1: One-liner plots
//   spectra::plot(x, y);
//   spectra::scatter(x, y);
//   spectra::show();
//
// Level 2: Styling
//   spectra::plot(x, y, "r--o").label("sin(x)");
//   spectra::plot(x, y2, "b:s").label("cos(x)");
//   spectra::title("Trig");
//   spectra::legend();
//   spectra::show();
//
// Level 3: Subplots
//   spectra::subplot(2, 1, 1);
//   spectra::plot(x, y1);
//   spectra::title("Top");
//
//   spectra::subplot(2, 1, 2);
//   spectra::plot(x, y2);
//   spectra::title("Bottom");
//   spectra::show();
//
// Level 4: Multiple windows & tabs
//   spectra::figure();              // Window 1
//   spectra::plot(x, y1);
//
//   spectra::tab();                 // Tab in same window
//   spectra::plot(x, y2);
//
//   spectra::figure();              // Window 2 (new OS window)
//   spectra::plot(x, y3);
//   spectra::show();
//
// Level 5: Real-time / animation
//   auto& line = spectra::plot(x, y);
//   spectra::on_update([&](float dt, float t) {
//       for (int i = 0; i < N; ++i)
//           y[i] = sin(x[i] + t);
//       line.set_y(y);
//   });
//   spectra::show();
//
// Level 6: 3D
//   spectra::figure();
//   spectra::plot3(x, y, z);
//   spectra::scatter3(x, y, z, "ro");
//   spectra::surf(xg, yg, zv);
//   spectra::show();
//
// Level 7: Full control (drop down to object API)
//   spectra::App app;
//   auto& fig = app.figure();
//   auto& ax  = fig.subplot(1, 1, 1);
//   ax.line(x, y);
//   app.run();
//
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <span>
#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <string_view>

#include <spectra/knob_manager.hpp>

namespace spectra
{

// Tunable coefficient for interactive function plots (y = f(x, params...)).
struct FplotParam
{
    std::string name;
    double      value   = 1.0;
    double      min_val = -10.0;
    double      max_val = 10.0;
};

// ─── Internal: Global State ─────────────────────────────────────────────────

namespace detail
{

struct InteractiveHLine
{
    LineSeries* series = nullptr;
    Knob*       knob   = nullptr;
};

struct InteractiveVLine
{
    LineSeries* series = nullptr;
    Knob*       knob   = nullptr;
};

struct InteractiveFplot
{
    LineSeries*                                            series = nullptr;
    std::vector<Knob*>                                     knobs;
    std::function<double(double, std::span<const double>)> func;
    int                                                    n = 200;
    std::vector<float>                                     xs;
    std::vector<float>                                     ys;
    std::vector<double>                                    param_scratch;
};

inline App& global_app()
{
    static App instance;
    return instance;
}

struct EasyState
{
    App*    app          = nullptr;
    Figure* current_fig  = nullptr;
    Axes*   current_ax   = nullptr;
    Axes3D* current_ax3d = nullptr;
    bool    owns_app     = false;

    // Animation callback
    std::function<void(float dt, float elapsed)> on_update_cb;
    uint64_t                                     on_update_frame_ =
        UINT64_MAX;   // frame guard: prevents double-fire when wired to multiple figures

    // Knob manager (shared across all figures in easy API)
    KnobManager knob_mgr;

    // All figures created in this easy-API session (for on_update wiring)
    std::vector<Figure*> all_figures;

    // Auto-created state tracking
    bool has_explicit_figure  = false;
    bool has_explicit_subplot = false;

    // Interactive reference lines / function plots (see easy_interactive.hpp).
    std::vector<InteractiveHLine> interactive_hlines_;
    std::vector<InteractiveVLine> interactive_vlines_;
    std::vector<InteractiveFplot> interactive_fplots_;
    bool                          interactive_anim_wired_ = false;

    bool has_interactive_bindings() const
    {
        return !interactive_hlines_.empty() || !interactive_vlines_.empty()
               || !interactive_fplots_.empty();
    }

    void tick_interactive();

    App& ensure_app()
    {
        if (!app)
        {
            app      = &global_app();
            owns_app = false;
        }
        return *app;
    }

    Figure& ensure_figure()
    {
        ensure_app();
        if (!current_fig)
        {
            current_fig = &app->figure();
            all_figures.push_back(current_fig);
            has_explicit_figure = false;
        }
        return *current_fig;
    }

    Axes& ensure_axes()
    {
        ensure_figure();
        if (!current_ax)
        {
            current_ax           = &current_fig->subplot(1, 1, 1);
            has_explicit_subplot = false;
        }
        return *current_ax;
    }

    Axes3D& ensure_axes3d()
    {
        ensure_figure();
        if (!current_ax3d)
        {
            current_ax3d         = &current_fig->subplot3d(1, 1, 1);
            has_explicit_subplot = false;
        }
        return *current_ax3d;
    }

    void reset()
    {
        current_fig      = nullptr;
        current_ax       = nullptr;
        current_ax3d     = nullptr;
        on_update_cb     = nullptr;
        on_update_frame_ = UINT64_MAX;
        all_figures.clear();
        has_explicit_figure  = false;
        has_explicit_subplot = false;
        interactive_hlines_.clear();
        interactive_vlines_.clear();
        interactive_fplots_.clear();
        interactive_anim_wired_ = false;
        // Don't reset app — it persists
    }
};

inline EasyState& easy_state()
{
    static EasyState s;
    return s;
}

}   // namespace detail

// ─── Figure Management ──────────────────────────────────────────────────────

// Create a new figure (new OS window). Returns the Figure for advanced use.
inline Figure& figure(uint32_t width = 1280, uint32_t height = 720)
{
    auto& s = detail::easy_state();
    s.ensure_app();
    s.current_fig = &s.app->figure({.width = width, .height = height});
    s.all_figures.push_back(s.current_fig);
    s.current_ax           = nullptr;
    s.current_ax3d         = nullptr;
    s.has_explicit_figure  = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// Create a new figure (new OS window) with a custom tab/window title.
//
//   spectra::figure("Trajectory");
//
inline Figure& figure(const std::string& name, uint32_t width = 1280, uint32_t height = 720)
{
    Figure& fig = figure(width, height);
    fig.set_tab_title(name);
    return fig;
}

// Create a new figure that opens as a tab next to an existing figure.
inline Figure& figure(Figure& tab_next_to, uint32_t /*width*/ = 1280, uint32_t /*height*/ = 720)
{
    auto& s = detail::easy_state();
    s.ensure_app();
    s.current_fig = &s.app->figure(tab_next_to);
    s.all_figures.push_back(s.current_fig);
    s.current_ax           = nullptr;
    s.current_ax3d         = nullptr;
    s.has_explicit_figure  = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// Create a new figure as a tab in the current window.
// If no figure exists yet, behaves like figure() (creates a new window).
//
//   spectra::figure();          // Window 1
//   spectra::plot(x, y_sin);    //   tab 1: sine
//
//   spectra::tab();             //   tab 2 (same window)
//   spectra::plot(x, y_cos);    //   tab 2: cosine
//
//   spectra::figure();          // Window 2 (new OS window)
//   spectra::plot(x, y_exp);
//
inline Figure& tab(uint32_t width = 0, uint32_t height = 0)
{
    auto& s = detail::easy_state();
    s.ensure_app();

    if (!s.current_fig)
    {
        // No current figure — just create a new window
        return figure(width ? width : 1280, height ? height : 720);
    }

    // Create a new figure as a tab next to the current figure
    s.current_fig = &s.app->figure(*s.current_fig);
    s.all_figures.push_back(s.current_fig);
    s.current_ax           = nullptr;
    s.current_ax3d         = nullptr;
    s.has_explicit_figure  = true;
    s.has_explicit_subplot = false;
    return *s.current_fig;
}

// Create a new tab in the current window with a custom title.
//
//   spectra::figure("Trajectories");
//   spectra::plot(x, y);
//   spectra::tab("XY View");
//   spectra::plot(x, z);
//
// If there is no current figure, opens a new window with the given title.
inline Figure& tab(const std::string& name, uint32_t width = 0, uint32_t height = 0)
{
    Figure& fig = tab(width, height);
    fig.set_tab_title(name);
    return fig;
}

// Set (or change) the title of the current tab/figure.
//
//   spectra::figure();
//   spectra::tab_name("Interceptor");
//
inline void tab_name(const std::string& name)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_fig->set_tab_title(name);
}

// ─── Subplot Selection ──────────────────────────────────────────────────────

// Select a 2D subplot (creates the figure if needed). 1-based index.
inline Axes& subplot(int rows, int cols, int index)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_ax           = &s.current_fig->subplot(rows, cols, index);
    s.current_ax3d         = nullptr;
    s.has_explicit_subplot = true;
    return *s.current_ax;
}

// Select a 3D subplot (creates the figure if needed). 1-based index.
inline Axes3D& subplot3d(int rows, int cols, int index)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_ax3d         = &s.current_fig->subplot3d(rows, cols, index);
    s.current_ax           = nullptr;
    s.has_explicit_subplot = true;
    return *s.current_ax3d;
}

// ─── 2D Plotting ────────────────────────────────────────────────────────────

// Plot a line. Auto-creates figure and axes if needed.
inline LineSeries& plot(std::span<const float> x,
                        std::span<const float> y,
                        std::string_view       fmt = "-")
{
    return detail::easy_state().ensure_axes().plot(x, y, fmt);
}

// Plot with explicit PlotStyle.
inline LineSeries& plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style)
{
    return detail::easy_state().ensure_axes().plot(x, y, style);
}

// Create an empty line series (for real-time append).
inline LineSeries& plot()
{
    return detail::easy_state().ensure_axes().line();
}

// Horizontal reference line (e.g. y=0). Default style is dashed gray.
inline LineSeries& hline(double y, std::string_view fmt = "k--")
{
    return detail::easy_state().ensure_axes().hline(y, fmt);
}

// Vertical reference line (e.g. x=0). Default style is dashed gray.
inline LineSeries& vline(double x, std::string_view fmt = "k--")
{
    return detail::easy_state().ensure_axes().vline(x, fmt);
}

// Plot a function y = f(x) over [xmin, xmax].
//
//   spectra::fplot([](double x) { return x * x; }, -2.0, 2.0);
//   spectra::fplot([](double x) { return std::sin(x); }, 0.0, 6.28, 300, "r-");
//
inline LineSeries& fplot(std::function<double(double)> func,
                         double                        xmin,
                         double                        xmax,
                         int                           n   = 200,
                         std::string_view              fmt = "-")
{
    return detail::easy_state().ensure_axes().fplot(std::move(func), xmin, xmax, n, fmt);
}

// Scatter plot.
inline ScatterSeries& scatter(std::span<const float> x, std::span<const float> y)
{
    return detail::easy_state().ensure_axes().scatter(x, y);
}

// Create an empty scatter series (for real-time append).
inline ScatterSeries& scatter()
{
    return detail::easy_state().ensure_axes().scatter();
}

// ─── Statistical Plots ──────────────────────────────────────────────────────

// Create an empty box plot series. Add boxes with .add_box(x, data).
inline BoxPlotSeries& box_plot()
{
    return detail::easy_state().ensure_axes().box_plot();
}

// Create an empty violin series. Add violins with .add_violin(x, data).
inline ViolinSeries& violin()
{
    return detail::easy_state().ensure_axes().violin();
}

// Histogram from raw data values.
inline HistogramSeries& histogram(std::span<const float> values, int bins = 30)
{
    return detail::easy_state().ensure_axes().histogram(values, bins);
}

// Bar chart from positions and heights.
inline BarSeries& bar(std::span<const float> positions, std::span<const float> heights)
{
    return detail::easy_state().ensure_axes().bar(positions, heights);
}

// Filled uncertainty/confidence envelope between lower and upper samples.
inline BandSeries& band(std::span<const float> x,
                        std::span<const float> lower,
                        std::span<const float> upper)
{
    return detail::easy_state().ensure_axes().band(x, lower, upper);
}

// ─── Shape Annotations ──────────────────────────────────────────────────────

// Create an empty shape series. Add shapes with .rect(), .circle(), .arrow(), etc.
inline ShapeSeries& shapes()
{
    return detail::easy_state().ensure_axes().shapes();
}

// Per-shape convenience functions — each creates its own ShapeSeries so
// styles (fill_color, line_color, fill_opacity, etc.) are independent.
//
//   spectra::rect(1,1,2,2).fill_color({1,0,0,1}).fill_opacity(0.5f);
//   spectra::circle(5,5,1).fill_color({0,0,1,1});
//

inline ShapeSeries& rect(float x, float y, float w, float h)
{
    return detail::easy_state().ensure_axes().shapes().rect(x, y, w, h);
}

inline ShapeSeries& circle(float cx, float cy, float r)
{
    return detail::easy_state().ensure_axes().shapes().circle(cx, cy, r);
}

inline ShapeSeries& ellipse(float cx, float cy, float rx, float ry)
{
    return detail::easy_state().ensure_axes().shapes().ellipse(cx, cy, rx, ry);
}

inline ShapeSeries& arrow(float x1, float y1, float x2, float y2)
{
    return detail::easy_state().ensure_axes().shapes().arrow(x1, y1, x2, y2);
}

inline ShapeSeries& ring(float cx, float cy, float outer_r, float inner_r)
{
    return detail::easy_state().ensure_axes().shapes().ring(cx, cy, outer_r, inner_r);
}

inline ShapeSeries& polygon(std::span<const float> x, std::span<const float> y)
{
    return detail::easy_state().ensure_axes().shapes().polygon(x, y);
}

inline ShapeSeries& shape_line(float x1, float y1, float x2, float y2)
{
    return detail::easy_state().ensure_axes().shapes().line(x1, y1, x2, y2);
}

inline ShapeSeries& text_annotation(float x, float y, const std::string& content)
{
    return detail::easy_state().ensure_axes().shapes().text(x, y, content);
}

// ─── 3D Plotting ────────────────────────────────────────────────────────────

// 3D line plot.
inline LineSeries3D& plot3(std::span<const float> x,
                           std::span<const float> y,
                           std::span<const float> z)
{
    auto& ax  = detail::easy_state().ensure_axes3d();
    auto& ref = ax.line3d(x, y, z);
    ax.auto_fit();
    return ref;
}

// 3D scatter plot.
inline ScatterSeries3D& scatter3(std::span<const float> x,
                                 std::span<const float> y,
                                 std::span<const float> z)
{
    auto& ax  = detail::easy_state().ensure_axes3d();
    auto& ref = ax.scatter3d(x, y, z);
    ax.auto_fit();
    return ref;
}

// Surface plot.
inline SurfaceSeries& surf(std::span<const float> x_grid,
                           std::span<const float> y_grid,
                           std::span<const float> z_values)
{
    auto& ax  = detail::easy_state().ensure_axes3d();
    auto& ref = ax.surface(x_grid, y_grid, z_values);
    ax.auto_fit();
    return ref;
}

// Mesh plot.
inline MeshSeries& mesh(std::span<const float> vertices, std::span<const uint32_t> indices)
{
    auto& ax  = detail::easy_state().ensure_axes3d();
    auto& ref = ax.mesh(vertices, indices);
    ax.auto_fit();
    return ref;
}

// Create an empty 3D shape series. Add shapes with .box(), .sphere(), .cylinder(), etc.
inline ShapeSeries3D& shapes3d()
{
    return detail::easy_state().ensure_axes3d().shapes3d();
}

// ─── Axes Configuration (applies to current axes) ───────────────────────────

inline void xlim(float min, float max)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->xlim(min, max);
    else
        s.ensure_axes().xlim(min, max);
}

inline void ylim(float min, float max)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->ylim(min, max);
    else
        s.ensure_axes().ylim(min, max);
}

inline void zlim(float min, float max)
{
    detail::easy_state().ensure_axes3d().zlim(min, max);
}

inline void title(const std::string& t)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->title(t);
    else
        s.ensure_axes().title(t);
}

inline void xlabel(const std::string& lbl)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->xlabel(lbl);
    else
        s.ensure_axes().xlabel(lbl);
}

inline void ylabel(const std::string& lbl)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->ylabel(lbl);
    else
        s.ensure_axes().ylabel(lbl);
}

inline void zlabel(const std::string& lbl)
{
    detail::easy_state().ensure_axes3d().zlabel(lbl);
}

inline void grid(bool enabled = true)
{
    auto& s = detail::easy_state();
    if (s.current_ax3d)
        s.current_ax3d->grid(enabled);
    else
        s.ensure_axes().grid(enabled);
}

// Set the visible streaming buffer length (in seconds) for current 2D axes.
// While enabled, x/y limits follow the latest data window automatically.
inline void presented_buffer(float seconds)
{
    detail::easy_state().ensure_axes().presented_buffer(seconds);
}

inline void legend(LegendPosition pos = LegendPosition::TopRight)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.current_fig->legend().position = pos;
    s.current_fig->legend().visible  = true;
}

// ─── Real-Time / Animation ──────────────────────────────────────────────────

// Register a per-frame update callback. Called every frame with (dt, elapsed_seconds).
// Use this to update data in real-time (e.g. live sensor streams, simulations).
//
//   auto& line = spectra::plot(x, y);
//   spectra::on_update([&](float dt, float t) {
//       for (int i = 0; i < N; ++i) y[i] = sin(x[i] + t);
//       line.set_y(y);
//       spectra::xlim(t - 10, t);  // sliding window
//   });
//   spectra::show();
//
inline void on_update(std::function<void(float dt, float elapsed)> callback)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.on_update_cb = std::move(callback);

    // Wire the animation callback on ALL figures so it fires regardless of
    // which tab is active.  The frame guard (on_update_frame_) ensures the
    // user callback executes at most once per frame tick — critical because
    // the multiproc loop fires anim_on_frame_ for every animated figure.
    auto on_frame_cb = [&s](Frame& f)
    {
        uint64_t fc = f.frame_number();
        if (fc == s.on_update_frame_)
            return;   // already fired this frame
        s.on_update_frame_ = fc;
        if (s.on_update_cb)
            s.on_update_cb(f.delta_time(), f.elapsed_seconds());
    };
    for (auto* fig : s.all_figures)
    {
        fig->animate().fps(60).on_frame(on_frame_cb).play();
    }
}

// Register per-frame update with explicit FPS target.
inline void on_update(float fps, std::function<void(float dt, float elapsed)> callback)
{
    auto& s = detail::easy_state();
    s.ensure_figure();
    s.on_update_cb = std::move(callback);

    auto on_frame_cb = [&s](Frame& f)
    {
        uint64_t fc = f.frame_number();
        if (fc == s.on_update_frame_)
            return;
        s.on_update_frame_ = fc;
        if (s.on_update_cb)
            s.on_update_cb(f.delta_time(), f.elapsed_seconds());
    };
    for (auto* fig : s.all_figures)
    {
        fig->animate().fps(fps).on_frame(on_frame_cb).play();
    }
}

// ─── Knobs (Interactive Parameters) ──────────────────────────────────────────
//
// Knobs are interactive controls that appear as a floating panel on the plot.
// When the user adjusts a knob, it changes a variable's value in real-time,
// triggering data recomputation via the on_update callback.
//
// Level 1: Define a knob, read its value in on_update:
//
//   auto& freq = spectra::knob("Frequency", 1.0f, 0.1f, 10.0f);
//   auto& line = spectra::plot(x, y);
//   spectra::on_update([&](float dt, float t) {
//       for (int i = 0; i < N; i++) y[i] = sin(freq.value * x[i]);
//       line.set_y(y);
//   });
//   spectra::show();
//
// Level 2: Per-knob callback (fires only when that knob changes):
//
//   spectra::knob("Amplitude", 1.0f, 0.0f, 5.0f, [&](float val) {
//       for (int i = 0; i < N; i++) y[i] = val * sin(x[i]);
//       line.set_y(y);
//   });
//
// Level 3: Other knob types:
//
//   spectra::knob_int("Harmonics", 3, 1, 10);
//   spectra::knob_bool("Show Grid", true);
//   spectra::knob_choice("Waveform", {"Sine", "Square", "Triangle"});
//

// Add a float slider knob.  Returns reference to the Knob for reading .value.
inline Knob& knob(const std::string&         name,
                  float                      default_val,
                  float                      min_val,
                  float                      max_val,
                  std::function<void(float)> on_change = nullptr)
{
    return detail::easy_state()
        .knob_mgr.add_float(name, default_val, min_val, max_val, 0.0f, std::move(on_change));
}

// Float knob with explicit step size.
inline Knob& knob(const std::string&         name,
                  float                      default_val,
                  float                      min_val,
                  float                      max_val,
                  float                      step,
                  std::function<void(float)> on_change = nullptr)
{
    return detail::easy_state()
        .knob_mgr.add_float(name, default_val, min_val, max_val, step, std::move(on_change));
}

// Integer slider knob.
inline Knob& knob_int(const std::string&         name,
                      int                        default_val,
                      int                        min_val,
                      int                        max_val,
                      std::function<void(float)> on_change = nullptr)
{
    return detail::easy_state().knob_mgr.add_int(name,
                                                 default_val,
                                                 min_val,
                                                 max_val,
                                                 std::move(on_change));
}

// Boolean checkbox knob.
inline Knob& knob_bool(const std::string&         name,
                       bool                       default_val = false,
                       std::function<void(float)> on_change   = nullptr)
{
    return detail::easy_state().knob_mgr.add_bool(name, default_val, std::move(on_change));
}

// Choice dropdown knob.
inline Knob& knob_choice(const std::string&              name,
                         const std::vector<std::string>& choices,
                         int                             default_index = 0,
                         std::function<void(float)>      on_change     = nullptr)
{
    return detail::easy_state().knob_mgr.add_choice(name,
                                                    choices,
                                                    default_index,
                                                    std::move(on_change));
}

// Set a global callback that fires whenever ANY knob value changes.
// Useful for batch recomputation of plot data.
inline void on_knob_change(std::function<void()> callback)
{
    detail::easy_state().knob_mgr.set_on_any_change(std::move(callback));
}

// Access the knob manager directly (for advanced use).
inline KnobManager& knobs()
{
    return detail::easy_state().knob_mgr;
}

// ─── Export ─────────────────────────────────────────────────────────────────

// Save current figure as PNG.
inline void save_png(const std::string& path)
{
    detail::easy_state().ensure_figure().save_png(path);
}

// Save current figure as PNG with explicit resolution.
inline void save_png(const std::string& path, uint32_t width, uint32_t height)
{
    detail::easy_state().ensure_figure().save_png(path, width, height);
}

// Save current figure as SVG.
inline void save_svg(const std::string& path)
{
    detail::easy_state().ensure_figure().save_svg(path);
}

// Copy current figure to the system clipboard as a PNG image.
inline void copy_to_clipboard()
{
    detail::easy_state().ensure_figure().copy_to_clipboard();
}

// ─── Show & Run ─────────────────────────────────────────────────────────────

// Show all figures and enter the interactive event loop (blocking).
// This is the last call in your program.
inline void show()
{
    auto& s = detail::easy_state();
    s.ensure_app();

    // Transfer easy-API knobs into the App so the window UI context can pick them up.
    // The App stores a pointer to the easy-API KnobManager; the window init code
    // copies knobs into the per-window KnobManager at create_first_window_with_ui().
    s.app->set_knob_manager(&s.knob_mgr);

    s.app->run();
    s.reset();
}

// ─── Utility ────────────────────────────────────────────────────────────────

// Get the current axes (2D). Returns nullptr if no axes created yet.
inline Axes* gca()
{
    return detail::easy_state().current_ax;
}

// Get the current 3D axes. Returns nullptr if no 3D axes created yet.
inline Axes3D* gca3d()
{
    return detail::easy_state().current_ax3d;
}

// Get the current figure. Returns nullptr if no figure created yet.
inline Figure* gcf()
{
    return detail::easy_state().current_fig;
}

// Clear the current axes (remove all series).
inline void cla()
{
    auto& s = detail::easy_state();
    if (s.current_ax)
        s.current_ax->clear_series();
    if (s.current_ax3d)
        s.current_ax3d->clear_series();
}

}   // namespace spectra

#include <spectra/easy_interactive.hpp>
