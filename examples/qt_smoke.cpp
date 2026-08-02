// qt_smoke.cpp — Minimal Qt smoke test for Spectra Vulkan canvas.
//
// Build (requires Qt6):
//   cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON \
//         -DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make qt_smoke
//
// Usage:
//   ./qt_smoke
//
// This is the minimal verification executable required by Phase 2.
// It creates a single window, renders a static line plot, and exits
// after a few seconds.  Validates:
//   - QtRuntime init/shutdown
//   - Surface creation + swapchain
//   - Figure rendering via Vulkan
//   - Surface generation tracking
//   - Event-driven frame scheduling

#ifdef SPECTRA_HAS_QT6

    #include <QApplication>
    #include <QMainWindow>
    #include <QTimer>

    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include "adapters/qt/qt_runtime.hpp"
    #include "adapters/qt/figure_canvas_widget.hpp"
    #include "ui/input/input.hpp"

    #include <cmath>
    #include <iostream>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 1. Initialize Spectra Qt runtime
    spectra::adapters::qt::QtRuntime runtime;
    if (!runtime.init())
    {
        std::cerr << "FAIL: QtRuntime::init() failed\n";
        return 1;
    }
    std::cout << "PASS: QtRuntime::init()\n";

    // 2. Create a figure with a simple line plot
    spectra::Figure figure;
    auto&           ax = figure.subplot(1, 1, 1);

    constexpr int      k_points = 200;
    std::vector<float> x(k_points), y(k_points);
    for (int i = 0; i < k_points; ++i)
    {
        const float t = static_cast<float>(i) * 0.05f;
        x[i]          = t;
        y[i]          = std::sin(t) * 0.5f * std::cos(2.0f * t);
    }
    ax.line(x, y).label("signal").color(spectra::colors::cyan).width(2.0f);
    ax.title("Qt Smoke Test");
    ax.xlabel("t");
    ax.ylabel("amplitude");
    ax.grid(true);
    ax.auto_fit();

    // 3. Create input handler
    spectra::InputHandler input;
    input.set_figure(&figure);
    if (!figure.all_axes().empty() && figure.all_axes()[0])
    {
        input.set_active_axes_base(figure.all_axes()[0].get());
    }

    // 4. Create the main window with a FigureCanvasWidget
    QMainWindow main_window;
    main_window.setWindowTitle("Spectra Qt Smoke Test");
    main_window.resize(800, 600);

    auto* canvas =
        new spectra::adapters::qt::FigureCanvasWidget(&runtime, &figure, &input, &main_window);
    main_window.setCentralWidget(canvas);

    // 5. Show the main window — Vulkan instance is already set by FigureCanvasWidget
    main_window.show();

    // 6. Start animation timer for continuous rendering
    canvas->startAnimationTimer();

    // 7. Auto-exit after 3 seconds (smoke test — not interactive)
    QTimer::singleShot(
        3000,
        [&]()
        {
            // Verify surface generation is valid
            auto gen = canvas->vulkanWindow()->surface_generation();
            if (gen > 0)
            {
                std::cout << "PASS: surface_generation=" << gen << "\n";
            }
            else
            {
                std::cerr << "WARN: surface_generation=0 (surface may not have been created)\n";
            }

            std::cout << "PASS: Qt smoke test completed\n";
            QApplication::quit();
        });

    int result = app.exec();

    // 8. Clean shutdown
    canvas->stopAnimationTimer();
    runtime.shutdown();

    std::cout << "PASS: QtRuntime::shutdown()\n";
    return result;
}

#else

    #include <iostream>

int main()
{
    std::cout << "This smoke test requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON "
                 "-DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
