// qt_embed_demo.cpp — Qt6 example embedding a Spectra plot via Vulkan canvas.
//
// Build (requires Qt6):
//   cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON \
//         -DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make qt_embed_demo
//
// Usage:
//   ./qt_embed_demo [--multi|--single]
//
// This demonstrates first-class Vulkan integration: Spectra creates a real
// VkSurface + swapchain on a QWindow and renders directly — no CPU readback,
// no QImage blitting.  Input events are forwarded through Spectra's
// InputHandler for pan/zoom/select.

#ifdef SPECTRA_HAS_QT6

    #include <QApplication>
    #include <QMainWindow>
    #include <QMouseEvent>
    #include <QPlatformSurfaceEvent>
    #include <QShortcut>
    #include <QSplitter>
    #include <QStatusBar>
    #include <QTimer>
    #include <QWidget>
    #include <QWindow>

    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/embed.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <spectra/series_stats.hpp>

    #include "adapters/qt/qt_runtime.hpp"
    #include "ui/input/input.hpp"

    #include <algorithm>
    #include <chrono>
    #include <cmath>
    #include <functional>
    #include <memory>
    #include <string>
    #include <vector>

// ─── SpectraVulkanWindow ─────────────────────────────────────────────────────
// A QWindow with VulkanSurface type that drives the Spectra render loop.

class SpectraVulkanWindow : public QWindow
{
   public:
    using AnimationTickCallback = std::function<void(float)>;

    explicit SpectraVulkanWindow(QWindow* parent = nullptr) : QWindow(parent)
    {
        setSurfaceType(QSurface::VulkanSurface);
        setMinimumSize(QSize(400, 300));
        resize(800, 600);
    }

    void setRuntime(spectra::adapters::qt::QtRuntime* rt)
    {
        runtime_ = rt;
        if (runtime_ && input_)
        {
            runtime_->set_input_handler(this, input_);
        }
    }
    void setFigure(spectra::Figure* fig) { figure_ = fig; }
    void setInputHandler(spectra::InputHandler* ih)
    {
        input_ = ih;
        if (runtime_)
        {
            runtime_->set_input_handler(this, input_);
        }
    }
    void setAnimationTick(AnimationTickCallback cb) { animation_tick_ = std::move(cb); }
    bool isAttached() const { return attached_; }

    bool ensureAttached()
    {
        if (attached_)
            return true;
        if (!runtime_ || !isExposed())
            return false;

        auto dpr = devicePixelRatio();
        auto w   = static_cast<uint32_t>(width() * dpr);
        auto h   = static_cast<uint32_t>(height() * dpr);
        if (w == 0 || h == 0)
            return false;

        if (!runtime_->attach_window(this, w, h))
            return false;

        attached_ = true;
        last_dpr_ = dpr;
        return true;
    }

    void forceDetach()
    {
        if (runtime_ && attached_)
        {
            runtime_->detach_window(this);
        }
        attached_ = false;
    }

    void startFrameTimer()
    {
        has_last_frame_time_ = false;
        timer_               = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &SpectraVulkanWindow::renderFrame);
        timer_->start(16);   // ~60 FPS
    }

   protected:
    bool event(QEvent* event) override
    {
        if (event && event->type() == QEvent::PlatformSurface && runtime_)
        {
            auto* platform_event = static_cast<QPlatformSurfaceEvent*>(event);
            if (platform_event->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            {
                forceDetach();
            }
            else if (platform_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
            {
                (void)ensureAttached();
            }
        }
        return QWindow::event(event);
    }

    void exposeEvent(QExposeEvent* /*event*/) override
    {
        if (!isExposed())
            return;

        if (!attached_ && runtime_)
        {
            // First expose: create surface + swapchain.
            if (ensureAttached())
            {
                renderFrame();
            }
            return;
        }

        if (attached_ && runtime_)
        {
            // Re-expose after hide (minimize→restore, monitor move, etc.)
            // Check for DPR change (moved to a different-scale monitor).
            auto dpr = devicePixelRatio();
            if (dpr != last_dpr_)
            {
                last_dpr_ = dpr;
                runtime_->mark_swapchain_dirty(this);
            }

            // Ensure a frame is rendered promptly after becoming visible again.
            renderFrame();
        }
    }

    void resizeEvent(QResizeEvent* /*event*/) override
    {
        if (!attached_ || !runtime_)
            return;

        // Set dirty flag — actual swapchain recreation is deferred to
        // begin_frame() at the next frame boundary.
        runtime_->mark_swapchain_dirty(this);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        input_->on_mouse_move(pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        input_->on_mouse_button(btn,
                                spectra::embed::ACTION_PRESS,
                                mod,
                                pos.x() * dpr,
                                pos.y() * dpr);
        requestUpdate();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        input_->on_mouse_button(btn,
                                spectra::embed::ACTION_RELEASE,
                                mod,
                                pos.x() * dpr,
                                pos.y() * dpr);
        requestUpdate();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!input_)
            return;
        auto  pos = event->position();
        auto  dpr = devicePixelRatio();
        float dy  = static_cast<float>(event->angleDelta().y()) / 120.0f;
        float dx  = static_cast<float>(event->angleDelta().x()) / 120.0f;
        input_->on_scroll(dx, dy, pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!input_)
            return;
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        input_->on_key(key, spectra::embed::ACTION_PRESS, mod);
        requestUpdate();
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        if (!input_)
            return;
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        input_->on_key(key, spectra::embed::ACTION_RELEASE, mod);
    }

   private:
    void renderFrame()
    {
        if (!attached_ || !runtime_ || !figure_)
            return;

        // Visibility guard: skip rendering when window is not exposed
        // (hidden, minimized, or zero-size).  Prevents swapchain storms
        // and wasted GPU work.
        if (!isExposed())
            return;

        auto dpr = devicePixelRatio();
        auto w   = static_cast<uint32_t>(width() * dpr);
        auto h   = static_cast<uint32_t>(height() * dpr);
        if (w == 0 || h == 0)
            return;

        float      dt  = 1.0f / 60.0f;
        const auto now = Clock::now();
        if (has_last_frame_time_)
        {
            dt = std::chrono::duration<float>(now - last_frame_time_).count();
            dt = std::clamp(dt, 1.0f / 240.0f, 0.1f);
        }
        last_frame_time_     = now;
        has_last_frame_time_ = true;

        if (input_)
        {
            input_->update(dt);
        }
        if (animation_tick_)
        {
            animation_tick_(dt);
        }

        (void)runtime_->render_window(this, *figure_);
    }

    static int qtButtonToSpectra(Qt::MouseButton btn)
    {
        switch (btn)
        {
            case Qt::LeftButton:
                return spectra::embed::MOUSE_BUTTON_LEFT;
            case Qt::RightButton:
                return spectra::embed::MOUSE_BUTTON_RIGHT;
            case Qt::MiddleButton:
                return spectra::embed::MOUSE_BUTTON_MIDDLE;
            default:
                return 0;
        }
    }

    static int qtModsToSpectra(Qt::KeyboardModifiers mods)
    {
        int result = 0;
        if (mods & Qt::ShiftModifier)
            result |= spectra::embed::MOD_SHIFT;
        if (mods & Qt::ControlModifier)
            result |= spectra::embed::MOD_CONTROL;
        if (mods & Qt::AltModifier)
            result |= spectra::embed::MOD_ALT;
        if (mods & Qt::MetaModifier)
            result |= spectra::embed::MOD_SUPER;
        return result;
    }

    static int qtKeyToSpectra(int qt_key)
    {
        if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z)
            return qt_key;
        if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9)
            return qt_key;

        switch (qt_key)
        {
            case Qt::Key_Escape:
                return spectra::embed::KEY_ESCAPE;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return spectra::embed::KEY_ENTER;
            case Qt::Key_Tab:
                return spectra::embed::KEY_TAB;
            case Qt::Key_Backspace:
                return spectra::embed::KEY_BACKSPACE;
            case Qt::Key_Delete:
                return spectra::embed::KEY_DELETE;
            case Qt::Key_Right:
                return spectra::embed::KEY_RIGHT;
            case Qt::Key_Left:
                return spectra::embed::KEY_LEFT;
            case Qt::Key_Down:
                return spectra::embed::KEY_DOWN;
            case Qt::Key_Up:
                return spectra::embed::KEY_UP;
            case Qt::Key_Home:
                return spectra::embed::KEY_HOME;
            case Qt::Key_End:
                return spectra::embed::KEY_END;
            case Qt::Key_Space:
                return spectra::embed::KEY_SPACE;
            default:
                return 0;
        }
    }

    spectra::adapters::qt::QtRuntime* runtime_ = nullptr;
    spectra::Figure*                  figure_  = nullptr;
    spectra::InputHandler*            input_   = nullptr;
    AnimationTickCallback             animation_tick_;
    QTimer*                           timer_    = nullptr;
    bool                              attached_ = false;
    qreal                             last_dpr_ = 1.0;

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_frame_time_{};
    bool              has_last_frame_time_ = false;
};

// ─── main ────────────────────────────────────────────────────────────────────

    #ifdef SPECTRA_QT_FORCE_MULTI_CANVAS
static constexpr bool k_default_multi_canvas = true;
    #else
static constexpr bool k_default_multi_canvas = false;
    #endif

struct AllFeaturesScene
{
    std::vector<float>      x_wave;
    std::vector<float>      y_wave;
    std::vector<float>      y_envelope;
    std::vector<float>      scatter_x;
    std::vector<float>      scatter_y;
    spectra::LineSeries*    wave_series     = nullptr;
    spectra::LineSeries*    envelope_series = nullptr;
    spectra::ScatterSeries* scatter_series  = nullptr;
    float                   phase           = 0.0f;
    bool                    paused          = false;
};

static void reseed_scatter(AllFeaturesScene& scene, float seed)
{
    constexpr int k_scatter_count = 240;
    scene.scatter_x.resize(k_scatter_count);
    scene.scatter_y.resize(k_scatter_count);
    for (int i = 0; i < k_scatter_count; ++i)
    {
        const float t       = static_cast<float>(i) * 0.13f;
        const float orbital = 0.8f + 0.35f * std::sin(0.21f * static_cast<float>(i) + seed);
        scene.scatter_x[i]  = 2.8f * std::cos(t + seed * 0.7f) * orbital;
        scene.scatter_y[i]  = 2.0f * std::sin(1.4f * t + seed) + 0.35f * std::cos(2.7f * t - seed);
    }

    if (scene.scatter_series)
    {
        scene.scatter_series->set_x(scene.scatter_x);
        scene.scatter_series->set_y(scene.scatter_y);
    }
}

static void populate_all_features_figure(spectra::Figure&  fig,
                                         AllFeaturesScene& scene,
                                         float             phase_seed)
{
    auto& ax_signals = fig.subplot(2, 2, 1);
    auto& ax_phase   = fig.subplot(2, 2, 2);
    auto& ax_stats   = fig.subplot(2, 2, 3);
    auto& ax_3d      = fig.subplot3d(2, 2, 4);

    ax_signals.clear_series();
    ax_phase.clear_series();
    ax_stats.clear_series();
    ax_3d.clear_series();

    scene.phase = phase_seed;

    // 1) Animated 2D waveform panel
    constexpr int k_wave_points = 420;
    scene.x_wave.resize(k_wave_points);
    scene.y_wave.resize(k_wave_points);
    scene.y_envelope.resize(k_wave_points);
    for (int i = 0; i < k_wave_points; ++i)
    {
        const float x       = static_cast<float>(i) * 0.03f;
        scene.x_wave[i]     = x;
        scene.y_wave[i]     = std::sin(x + scene.phase) + 0.22f * std::sin(3.2f * x - scene.phase);
        scene.y_envelope[i] = 0.35f * std::cos(0.8f * x - 0.5f * scene.phase);
    }

    scene.wave_series = &ax_signals.line(scene.x_wave, scene.y_wave)
                             .label("composite wave")
                             .color(spectra::colors::cyan)
                             .line_style(spectra::LineStyle::Solid)
                             .width(2.5f);
    scene.envelope_series = &ax_signals.line(scene.x_wave, scene.y_envelope)
                                 .label("envelope")
                                 .color(spectra::colors::yellow)
                                 .line_style(spectra::LineStyle::DashDot)
                                 .opacity(0.8f);
    ax_signals.title("Live Signal (2D)");
    ax_signals.xlabel("t");
    ax_signals.ylabel("amplitude");
    ax_signals.grid(true);
    ax_signals.auto_fit();

    // 2) Scatter + trend panel
    reseed_scatter(scene, phase_seed + 0.75f);
    scene.scatter_series = &ax_phase.scatter(scene.scatter_x, scene.scatter_y)
                                .label("phase cloud")
                                .color(spectra::colors::magenta)
                                .marker_style(spectra::MarkerStyle::Circle)
                                .marker_size(3.5f)
                                .opacity(0.8f);

    std::vector<float> trend_x(120), trend_y(120);
    for (int i = 0; i < static_cast<int>(trend_x.size()); ++i)
    {
        const float x =
            -3.2f + 6.4f * static_cast<float>(i) / static_cast<float>(trend_x.size() - 1);
        trend_x[i] = x;
        trend_y[i] = 0.65f * x;
    }
    ax_phase.line(trend_x, trend_y)
        .label("trend")
        .color(spectra::colors::orange)
        .line_style(spectra::LineStyle::Dashed)
        .opacity(0.7f);
    ax_phase.title("Live Scatter + Trend");
    ax_phase.xlabel("x");
    ax_phase.ylabel("y");
    ax_phase.grid(true);
    ax_phase.auto_fit();

    // 3) Statistical series panel (histogram + bars)
    std::vector<float> hist_values;
    hist_values.reserve(1200);
    for (int i = 0; i < 1200; ++i)
    {
        const float t = static_cast<float>(i) * 0.029f;
        hist_values.push_back(std::sin(t) + 0.45f * std::sin(2.7f * t + 0.35f * phase_seed));
    }
    ax_stats.histogram(hist_values, 28)
        .label("distribution")
        .color(spectra::colors::green)
        .opacity(0.55f);

    std::vector<float> bands_x = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    std::vector<float> bands_y;
    bands_y.reserve(bands_x.size());
    for (float x : bands_x)
    {
        bands_y.push_back(0.25f + 0.35f * (1.0f + std::sin(x * 1.6f + phase_seed)));
    }
    ax_stats.bar(bands_x, bands_y)
        .label("bands")
        .color(spectra::Color{0.19f, 0.63f, 0.95f, 1.0f})
        .bar_width(0.6f)
        .opacity(0.7f);

    ax_stats.title("Histogram + Bar");
    ax_stats.xlabel("bin / category");
    ax_stats.ylabel("value");
    ax_stats.grid(true);
    ax_stats.auto_fit();

    // 4) 3D panel (line + scatter + lit surface)
    std::vector<float> x3d(220), y3d(220), z3d(220);
    for (int i = 0; i < static_cast<int>(x3d.size()); ++i)
    {
        const float t = static_cast<float>(i) * 0.1f;
        x3d[i]        = 1.3f * std::cos(t + phase_seed);
        y3d[i]        = 1.3f * std::sin(t + phase_seed);
        z3d[i]        = -1.3f + 2.6f * static_cast<float>(i) / static_cast<float>(x3d.size() - 1);
    }
    ax_3d.line3d(x3d, y3d, z3d)
        .label("helix")
        .color(spectra::colors::cyan)
        .width(2.0f)
        .opacity(0.9f);

    std::vector<float> sx, sy, sz;
    sx.reserve(180);
    sy.reserve(180);
    sz.reserve(180);
    for (int i = 0; i < 180; ++i)
    {
        const float t = static_cast<float>(i) * 0.21f;
        sx.push_back(1.8f * std::cos(0.7f * t + 1.2f));
        sy.push_back(1.2f * std::sin(1.1f * t + phase_seed));
        sz.push_back(0.8f * std::cos(1.5f * t - phase_seed));
    }
    ax_3d.scatter3d(sx, sy, sz)
        .label("samples")
        .color(spectra::colors::yellow)
        .size(4.0f)
        .opacity(0.85f);

    std::vector<float> gx(28), gy(28), gz(28 * 28);
    for (int i = 0; i < static_cast<int>(gx.size()); ++i)
    {
        gx[i] = -2.0f + 4.0f * static_cast<float>(i) / static_cast<float>(gx.size() - 1);
        gy[i] = -2.0f + 4.0f * static_cast<float>(i) / static_cast<float>(gy.size() - 1);
    }
    for (int r = 0; r < static_cast<int>(gy.size()); ++r)
    {
        for (int c = 0; c < static_cast<int>(gx.size()); ++c)
        {
            const float x                           = gx[c];
            const float y                           = gy[r];
            const float d                           = std::sqrt(x * x + y * y);
            gz[r * static_cast<int>(gx.size()) + c] = 0.35f * std::sin(3.2f * d + phase_seed);
        }
    }
    ax_3d.surface(gx, gy, gz)
        .label("ripple surface")
        .colormap(spectra::ColormapType::Viridis)
        .opacity(0.55f)
        .ambient(0.25f)
        .specular(0.35f);

    ax_3d.title("3D Line / Scatter / Surface");
    ax_3d.xlabel("X");
    ax_3d.ylabel("Y");
    ax_3d.zlabel("Z");
    ax_3d.grid(true);
    ax_3d.grid_planes(spectra::Axes3D::GridPlane::All);
    ax_3d.show_bounding_box(true);
    ax_3d.lighting_enabled(true);
    ax_3d.auto_fit();

    fig.legend().visible  = true;
    fig.legend().position = spectra::LegendPosition::TopRight;
}

static void tick_all_features_scene(AllFeaturesScene& scene, float dt)
{
    if (scene.paused || !scene.wave_series || !scene.envelope_series || !scene.scatter_series)
    {
        return;
    }

    scene.phase += dt * 1.4f;

    for (size_t i = 0; i < scene.x_wave.size(); ++i)
    {
        const float x       = scene.x_wave[i];
        scene.y_wave[i]     = std::sin(x + scene.phase) + 0.22f * std::sin(3.2f * x - scene.phase);
        scene.y_envelope[i] = 0.35f * std::cos(0.8f * x - 0.5f * scene.phase);
    }

    for (size_t i = 0; i < scene.scatter_y.size(); ++i)
    {
        const float x      = scene.scatter_x[i];
        const float k      = static_cast<float>(i) * 0.031f;
        scene.scatter_y[i] = 2.0f * std::sin(0.95f * x + scene.phase + k)
                             + 0.3f * std::cos(2.1f * x - 0.65f * scene.phase);
    }

    scene.wave_series->set_y(scene.y_wave);
    scene.envelope_series->set_y(scene.y_envelope);
    scene.scatter_series->set_y(scene.scatter_y);
}

static void setup_input_handler(spectra::InputHandler& input, spectra::Figure& figure)
{
    input.set_figure(&figure);
    if (!figure.all_axes().empty() && figure.all_axes()[0])
    {
        input.set_active_axes_base(figure.all_axes()[0].get());
    }
}

static void set_grid_all_axes(spectra::Figure& figure, bool enabled)
{
    for (auto& axes_ptr : figure.all_axes_mut())
    {
        if (axes_ptr)
        {
            axes_ptr->grid(enabled);
        }
    }
}

static bool grid_enabled_any(const spectra::Figure& figure)
{
    for (const auto& axes_ptr : figure.all_axes())
    {
        if (axes_ptr && axes_ptr->grid_enabled())
        {
            return true;
        }
    }
    return false;
}

static void auto_fit_all_axes(spectra::Figure& figure)
{
    for (auto& axes_ptr : figure.all_axes_mut())
    {
        if (axes_ptr)
        {
            axes_ptr->auto_fit();
        }
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    bool multi_canvas = k_default_multi_canvas;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--multi" || arg == "-m")
        {
            multi_canvas = true;
        }
        else if (arg == "--single" || arg == "-s")
        {
            multi_canvas = false;
        }
    }

    // 1. Initialize Spectra Qt runtime (Vulkan backend + renderer)
    spectra::adapters::qt::QtRuntime runtime;
    if (!runtime.init())
    {
        qFatal("Failed to initialize Spectra Vulkan runtime");
        return 1;
    }

    QMainWindow main_window;
    main_window.setWindowTitle(multi_canvas ? "Spectra — Qt Embed All Features (Multi Canvas)"
                                            : "Spectra — Qt Embed All Features");
    main_window.resize(multi_canvas ? 1500 : 1280, multi_canvas ? 860 : 840);

    // Keep demo objects alive for the entire event loop.
    spectra::Figure       figure_a;
    spectra::InputHandler input_a;
    SpectraVulkanWindow   window_a;

    spectra::Figure       figure_b;
    spectra::InputHandler input_b;
    SpectraVulkanWindow   window_b;
    AllFeaturesScene      scene_a;
    AllFeaturesScene      scene_b;

    // 2. Create demo figures
    populate_all_features_figure(figure_a, scene_a, 0.0f);
    setup_input_handler(input_a, figure_a);

    window_a.setRuntime(&runtime);
    window_a.setFigure(&figure_a);
    window_a.setInputHandler(&input_a);
    window_a.setAnimationTick([&scene_a](float dt) { tick_all_features_scene(scene_a, dt); });
    window_a.setVulkanInstance(runtime.vulkan_instance());

    if (!multi_canvas)
    {
        QWidget* container = QWidget::createWindowContainer(&window_a, &main_window);
        container->setMinimumSize(400, 300);
        container->setFocusPolicy(Qt::StrongFocus);
        main_window.setCentralWidget(container);
    }
    else
    {
        populate_all_features_figure(figure_b, scene_b, 0.75f);
        setup_input_handler(input_b, figure_b);

        window_b.setRuntime(&runtime);
        window_b.setFigure(&figure_b);
        window_b.setInputHandler(&input_b);
        window_b.setAnimationTick([&scene_b](float dt) { tick_all_features_scene(scene_b, dt); });
        window_b.setVulkanInstance(runtime.vulkan_instance());

        auto* splitter = new QSplitter(Qt::Horizontal, &main_window);

        QWidget* left_container = QWidget::createWindowContainer(&window_a, splitter);
        left_container->setMinimumSize(300, 250);
        left_container->setFocusPolicy(Qt::StrongFocus);

        QWidget* right_container = QWidget::createWindowContainer(&window_b, splitter);
        right_container->setMinimumSize(300, 250);
        right_container->setFocusPolicy(Qt::StrongFocus);

        splitter->addWidget(left_container);
        splitter->addWidget(right_container);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        main_window.setCentralWidget(splitter);

        // Multi-canvas lifecycle test hook:
        // Ctrl+D toggles right canvas detach/reattach without restarting app.
        auto* toggle_right_attach =
            new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), &main_window);
        QObject::connect(toggle_right_attach,
                         &QShortcut::activated,
                         &main_window,
                         [&window_b]()
                         {
                             if (window_b.isAttached())
                             {
                                 window_b.forceDetach();
                             }
                             else
                             {
                                 (void)window_b.ensureAttached();
                             }
                         });
    }

    auto* pause_shortcut = new QShortcut(QKeySequence(QStringLiteral("Space")), &main_window);
    QObject::connect(pause_shortcut,
                     &QShortcut::activated,
                     &main_window,
                     [&scene_a, &scene_b, multi_canvas]()
                     {
                         scene_a.paused = !scene_a.paused;
                         if (multi_canvas)
                         {
                             scene_b.paused = scene_a.paused;
                         }
                     });

    auto* randomize_shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), &main_window);
    QObject::connect(randomize_shortcut,
                     &QShortcut::activated,
                     &main_window,
                     [&scene_a, &scene_b, multi_canvas]()
                     {
                         reseed_scatter(scene_a, scene_a.phase + 1.7f);
                         if (multi_canvas)
                         {
                             reseed_scatter(scene_b, scene_b.phase + 1.9f);
                         }
                     });

    auto* grid_shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), &main_window);
    QObject::connect(grid_shortcut,
                     &QShortcut::activated,
                     &main_window,
                     [&figure_a, &figure_b, multi_canvas]()
                     {
                         const bool next_grid = !grid_enabled_any(figure_a);
                         set_grid_all_axes(figure_a, next_grid);
                         if (multi_canvas)
                         {
                             set_grid_all_axes(figure_b, next_grid);
                         }
                     });

    auto* autofit_shortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")), &main_window);
    QObject::connect(autofit_shortcut,
                     &QShortcut::activated,
                     &main_window,
                     [&figure_a, &figure_b, multi_canvas]()
                     {
                         auto_fit_all_axes(figure_a);
                         if (multi_canvas)
                         {
                             auto_fit_all_axes(figure_b);
                         }
                     });

    QString status_text = QStringLiteral(
        "Space pause/resume  |  Ctrl+R reseed scatter  |  Ctrl+G grid  |  Ctrl+Shift+A auto-fit");
    if (multi_canvas)
    {
        status_text += QStringLiteral("  |  Ctrl+D detach/reattach right canvas");
    }
    main_window.statusBar()->showMessage(status_text);

    main_window.show();

    // 3. Start frame timers after windows are visible
    window_a.startFrameTimer();
    if (multi_canvas)
    {
        window_b.startFrameTimer();
    }

    int result = app.exec();

    // 4. Clean shutdown — runtime dtor also handles Vulkan cleanup
    runtime.shutdown();

    return result;
}

#else

    #include <iostream>

int main()
{
    std::cout << "This example requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON "
                 "-DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n"
              << "Run with --multi for two canvases, or --single for one.\n"
              << "All-features shortcuts: Space, Ctrl+R, Ctrl+G, Ctrl+Shift+A\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
