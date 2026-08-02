// Performance regression benchmark: Qt vs legacy frontend overhead.
//
// Measures operations that are shared between frontends to establish
// baselines and detect regressions:
//   - CommandRegistry operations (framework-neutral)
//   - UndoManager operations (framework-neutral)
//   - Headless render frame time (same renderer for both frontends)
//   - Figure creation and series setup
//
// When SPECTRA_USE_QT is enabled, additional Qt-specific benchmarks
// measure QtActionBridge construction and action rebuild overhead.

#include <benchmark/benchmark.h>
#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

#include "ui/commands/command_registry.hpp"
#include "ui/commands/undo_manager.hpp"

#ifdef SPECTRA_USE_QT
    #include <QApplication>
    #include "adapters/qt/qt_action_bridge.hpp"
#endif

using namespace spectra;

// ─── Data helpers ────────────────────────────────────────────────────────────

static std::vector<float> gen_x(size_t n)
{
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(i) / static_cast<float>(n) * 100.0f;
    return x;
}

static std::vector<float> gen_y_sin(const std::vector<float>& x)
{
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = std::sin(x[i] * 0.1f);
    return y;
}

// ─── Command registry operations (framework-neutral) ─────────────────────────

static void BM_CommandRegistry_Register100(benchmark::State& state)
{
    for (auto _ : state)
    {
        state.PauseTiming();
        CommandRegistry registry;
        state.ResumeTiming();

        for (int i = 0; i < 100; ++i)
        {
            registry.register_command("bench.cmd_" + std::to_string(i),
                                      "Bench Command " + std::to_string(i),
                                      []() {});
        }
        benchmark::DoNotOptimize(registry);
    }
}
BENCHMARK(BM_CommandRegistry_Register100)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_Invoke100(benchmark::State& state)
{
    CommandRegistry registry;
    int             counter = 0;
    for (int i = 0; i < 100; ++i)
    {
        registry.register_command("bench.cmd_" + std::to_string(i),
                                  "Bench Command " + std::to_string(i),
                                  [&counter]() { ++counter; });
    }

    for (auto _ : state)
    {
        for (int i = 0; i < 100; ++i)
            registry.execute("bench.cmd_" + std::to_string(i));
    }
    benchmark::DoNotOptimize(counter);
}
BENCHMARK(BM_CommandRegistry_Invoke100)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_Search(benchmark::State& state)
{
    CommandRegistry registry;
    for (int i = 0; i < 100; ++i)
    {
        registry.register_command("bench.cmd_" + std::to_string(i),
                                  "Bench Command " + std::to_string(i),
                                  []() {});
    }

    for (auto _ : state)
    {
        auto results = registry.search("bench", 20);
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(BM_CommandRegistry_Search)->Unit(benchmark::kMicrosecond);

// ─── Undo/Redo operations (framework-neutral) ────────────────────────────────

static void BM_UndoManager_Push100(benchmark::State& state)
{
    for (auto _ : state)
    {
        UndoManager undo;
        for (int i = 0; i < 100; ++i)
        {
            UndoAction action;
            action.description = "action_" + std::to_string(i);
            action.undo_fn     = []() {};
            action.redo_fn     = []() {};
            undo.push(std::move(action));
        }
        benchmark::DoNotOptimize(undo);
    }
}
BENCHMARK(BM_UndoManager_Push100)->Unit(benchmark::kMicrosecond);

static void BM_UndoManager_UndoRedoCycle(benchmark::State& state)
{
    UndoManager undo;
    for (int i = 0; i < 50; ++i)
    {
        UndoAction action;
        action.description = "action_" + std::to_string(i);
        action.undo_fn     = []() {};
        action.redo_fn     = []() {};
        undo.push(std::move(action));
    }

    for (auto _ : state)
    {
        for (int i = 0; i < 50; ++i)
            undo.undo();
        for (int i = 0; i < 50; ++i)
            undo.redo();
    }
}
BENCHMARK(BM_UndoManager_UndoRedoCycle)->Unit(benchmark::kMicrosecond);

// ─── Headless render (same renderer, measures full pipeline) ─────────────────

static void BM_HeadlessRender_Line_10K(benchmark::State& state)
{
    auto x = gen_x(10000);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = 1280, .height = 720});
        auto&        ax  = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Line_10K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_MultiSeries_10x1K(benchmark::State& state)
{
    auto x = gen_x(1000);

    for (auto _ : state)
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = 1280, .height = 720});
        auto&        ax  = fig.subplot(1, 1, 1);
        for (int s = 0; s < 10; ++s)
        {
            std::vector<float> y(x.size());
            for (size_t i = 0; i < x.size(); ++i)
                y[i] = std::sin(x[i] * 0.1f + s * 0.5f);
            ax.line(x, y).label("series_" + std::to_string(s));
        }
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-5.0f, 5.0f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_MultiSeries_10x1K)->Unit(benchmark::kMillisecond);

static void BM_FigureCreation_MultiFigure_4(benchmark::State& state)
{
    auto x = gen_x(1000);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true, .socket_path = ""});
        for (int f = 0; f < 4; ++f)
        {
            auto& fig = app.figure({.width = 800, .height = 600});
            auto& ax  = fig.subplot(1, 1, 1);
            ax.line(x, y).label("bench_" + std::to_string(f));
            ax.xlim(0.0f, 100.0f);
            ax.ylim(-1.5f, 1.5f);
        }
        benchmark::DoNotOptimize(app);
    }
}
BENCHMARK(BM_FigureCreation_MultiFigure_4)->Unit(benchmark::kMillisecond);

// ─── Qt-specific benchmarks (only when Qt is available) ──────────────────────

#ifdef SPECTRA_USE_QT

static void BM_QtActionBridge_Rebuild100(benchmark::State& state)
{
    int          argc_local   = 1;
    char*        argv_local[] = {(char*)"bench", nullptr};
    QApplication app(argc_local, argv_local);

    CommandRegistry registry;
    for (int i = 0; i < 100; ++i)
    {
        registry.register_command(
            "qt.bench.cmd_" + std::to_string(i),
            "Qt Bench Command " + std::to_string(i),
            []() {},
            "",
            "Bench");
    }

    for (auto _ : state)
    {
        spectra::adapters::qt::QtActionBridge bridge(registry);
        bridge.rebuild();
        benchmark::DoNotOptimize(bridge);
    }
}
BENCHMARK(BM_QtActionBridge_Rebuild100)->Unit(benchmark::kMicrosecond);

static void BM_QtActionBridge_ActionLookup(benchmark::State& state)
{
    int          argc_local   = 1;
    char*        argv_local[] = {(char*)"bench", nullptr};
    QApplication app(argc_local, argv_local);

    CommandRegistry registry;
    for (int i = 0; i < 100; ++i)
    {
        registry.register_command(
            "qt.bench.cmd_" + std::to_string(i),
            "Qt Bench Command " + std::to_string(i),
            []() {},
            "",
            "Bench");
    }

    spectra::adapters::qt::QtActionBridge bridge(registry);
    bridge.rebuild();

    for (auto _ : state)
    {
        for (int i = 0; i < 100; ++i)
        {
            auto* action = bridge.action_for("qt.bench.cmd_" + std::to_string(i));
            benchmark::DoNotOptimize(action);
        }
    }
}
BENCHMARK(BM_QtActionBridge_ActionLookup)->Unit(benchmark::kMicrosecond);

#endif   // SPECTRA_USE_QT

BENCHMARK_MAIN();
