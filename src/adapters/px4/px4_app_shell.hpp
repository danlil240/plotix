#pragma once

// Px4AppShell — application shell for spectra-px4 standalone executable.
//
// Owns and wires PX4 adapter backends + panels into a dockspace-driven UI.
// Supports both offline ULog analysis and real-time MAVLink inspection.

#include "px4_bridge.hpp"
#include "px4_plot_manager.hpp"
#include "ulog_reader.hpp"

#include "ui/live_connection_panel.hpp"
#include "ui/ulog_file_panel.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/app_shell.hpp"
#endif

namespace spectra
{
class Figure;
class LayoutManager;
class WindowManager;
}   // namespace spectra

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Px4AppConfig
// ---------------------------------------------------------------------------

struct Px4AppConfig
{
    std::string ulog_file;   // optional: open this ULog on launch
    std::string host{"127.0.0.1"};
    uint16_t    port{14540};
    bool        auto_connect{false};
    double      time_window_s{30.0};
    uint32_t    window_width{1400};
    uint32_t    window_height{800};

    // When true, use ChunkedLineSeries for plotting (efficient for large
    // ULog files and long-running live sessions).  Default: false.
    bool use_chunked{false};

    // Memory budget (bytes) for chunked series.  0 = unlimited.
    size_t chunked_memory_budget{0};
};

Px4AppConfig parse_px4_args(int argc, char** argv, std::string& error_out);

// ---------------------------------------------------------------------------
// AutoPlotField — one field within an auto-plot group.
// ---------------------------------------------------------------------------

struct AutoPlotField
{
    std::string topic;
    std::string field;
    int         array_idx{-1};
    uint8_t     multi_id{0};
    std::string label;

    std::vector<float> times;
    std::vector<float> values;
};

// ---------------------------------------------------------------------------
// AutoPlotGroup — a logical group of related fields shown in one subplot.
// ---------------------------------------------------------------------------

struct AutoPlotGroup
{
    std::string                title;
    std::string                ylabel;
    std::vector<AutoPlotField> fields;
};

// ---------------------------------------------------------------------------
// Px4AppShell
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI
class Px4AppShell : public spectra::ui::shell::AppShell
#else
class Px4AppShell
#endif
{
   public:
    explicit Px4AppShell(const Px4AppConfig& cfg);
    ~Px4AppShell();

    Px4AppShell(const Px4AppShell&)            = delete;
    Px4AppShell& operator=(const Px4AppShell&) = delete;
    Px4AppShell(Px4AppShell&&)                 = delete;
    Px4AppShell& operator=(Px4AppShell&&)      = delete;

    // Bind to the Spectra render figure.
    void set_canvas_figure(spectra::Figure* fig) { canvas_figure_ = fig; }

    // Wire WindowManager for OS-level panel tearoff.
    void set_window_manager(spectra::WindowManager* wm);

    void set_layout_manager(spectra::LayoutManager* lm);

    bool panel_visible(const char* id) const;
    void set_panel_visible(const char* id, bool v);

    bool nav_rail_visible() const;
    bool nav_rail_expanded() const;
    void set_nav_rail_visible(bool v);
    void set_nav_rail_expanded(bool v);

    // Initialise components.
    bool init();

    // Shut down all components.
    void shutdown();

    // Per-frame update (call from render loop).
    void poll();

    // Draw ImGui UI (call from ImGui frame).
    void draw();

    // Process deferred panel create/destroy — call AFTER app.step().
    void process_pending_panels();

    // Request graceful shutdown.
    void request_shutdown() { shutdown_requested_.store(true, std::memory_order_relaxed); }
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_relaxed); }

    // Open a ULog file (offline analysis).
    bool open_ulog(const std::string& path);

    // Auto-plot interesting fields from the loaded ULog (flight-review style).
    void auto_plot_ulog();

    // Close all auto-generated and manually added plots.
    void close_all_plots();

    // Accessors.
    ULogReader&         reader() { return reader_; }
    Px4Bridge&          bridge() { return bridge_; }
    Px4PlotManager&     plot_manager() { return plot_mgr_; }
    const Px4AppConfig& config() const { return cfg_; }

#ifdef SPECTRA_USE_IMGUI
   protected:
    void on_register_panels() override;
    void on_populate_menus(spectra::ui::shell::MenuBar& bar) override;
    void on_populate_nav_rail(spectra::ui::shell::NavRail& rail) override;
    void on_default_layout(unsigned int dockspace_id) override;
    void on_build_status_bar(spectra::ui::shell::StatusBar& bar) override;
#endif

   private:
    bool open_ulog_with_dialog();
    void sync_canvas_figure(bool force = false);
    void sync_auto_plot_figure();
    void sync_manual_plot_figure(bool force);
    void rebuild_figure_axes(int num_groups);
    void sync_layout_chrome();

    void draw_ulog_file(bool* p_open = nullptr);
    void draw_live_connection(bool* p_open = nullptr);

    Px4AppConfig cfg_;

    ULogReader     reader_;
    Px4Bridge      bridge_;
    Px4PlotManager plot_mgr_;

    std::unique_ptr<ULogFilePanel>       file_panel_;
    std::unique_ptr<LiveConnectionPanel> live_panel_;

    spectra::Figure* canvas_figure_{nullptr};
    uint64_t         last_canvas_revision_{static_cast<uint64_t>(-1)};

    // Auto-plot state.
    std::vector<AutoPlotGroup> auto_plot_groups_;
    bool                       auto_plot_active_{false};

    std::atomic<bool> shutdown_requested_{false};

    std::string last_open_error_;

#ifdef SPECTRA_USE_IMGUI
    std::map<std::string, bool> pending_panel_visibility_;
#endif
};

}   // namespace spectra::adapters::px4
