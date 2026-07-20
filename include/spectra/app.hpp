#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include <spectra/figure_registry.hpp>

namespace spectra
{

enum class RenderBackend
{
    Vulkan,
    WebGPU,
};

struct AppConfig
{
    bool          headless = false;
    std::string   socket_path;   // non-empty → multiproc mode; empty → check SPECTRA_SOCKET env
    RenderBackend backend = RenderBackend::Vulkan;
};

class SessionRuntime;
class WindowRuntime;
class ApplicationServices;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
class WindowManager;
#endif

class App
{
   public:
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    Figure& figure(const FigureConfig& config = {});
    Figure& figure(Figure& sibling);

    // Run the application (blocking — processes all figures)
    void run();

    // Frame-by-frame control (alternative to run()).
    // init_runtime() performs all setup, step() runs one frame,
    // shutdown_runtime() cleans up.  run_inproc() calls these internally.
    struct StepResult
    {
        bool     should_exit   = false;
        float    frame_time_ms = 0.0f;
        uint64_t frame_number  = 0;
    };

    void       init_runtime();
    StepResult step();
    void       shutdown_runtime();

    // Access internals exposed for QA / testing after init_runtime().
    WindowUIContext* ui_context();
    SessionRuntime*  session();
    FigureRegistry&  figure_registry() { return registry_; }
#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
    WindowManager* window_manager();
#endif

    bool is_headless() const { return config_.headless; }

    // Signal-safe: call from SIGINT/SIGTERM handlers to request graceful exit.
    // The next iteration of the run loop will detect this and break out,
    // allowing shutdown_runtime() to run.
    static void request_exit();

    // Access internals (for renderer integration)
    Backend*  backend() { return backend_.get(); }
    Renderer* renderer() { return renderer_.get(); }

    // Access framework-neutral application services (available after init_runtime()).
    ApplicationServices* app_services();

    // Knob manager (set by easy API before run(), or by user directly)
    void         set_knob_manager(KnobManager* km) { knob_manager_ = km; }
    KnobManager* knob_manager() const { return knob_manager_; }

   private:
    void run_inproc();
    void run_multiproc();
    void render_secondary_window(struct WindowContext* wctx);

    // Opaque runtime state created by init_runtime(), destroyed by shutdown_runtime().
    struct AppRuntime;
    std::unique_ptr<AppRuntime> runtime_;

    // Group figures into windows based on sibling relationships.
    // Returns a vector of groups; each group is a vector of FigureIds
    // that should share one OS window.
    std::vector<std::vector<FigureId>> compute_window_groups() const;

    AppConfig                         config_;
    FigureRegistry                    registry_;
    std::unique_ptr<ui::ThemeManager> theme_mgr_;
    std::unique_ptr<Backend>          backend_;
    std::unique_ptr<Renderer>         renderer_;

    // Maps a FigureId to the FigureId it should be tabbed next to.
    // Figures not in this map get their own window.
    std::unordered_map<FigureId, FigureId> sibling_map_;

    // External knob manager (not owned — set by easy API or user)
    KnobManager* knob_manager_ = nullptr;

    static std::atomic<bool> s_exit_requested_;
};

}   // namespace spectra
