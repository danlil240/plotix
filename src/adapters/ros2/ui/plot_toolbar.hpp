#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <functional>

namespace spectra::adapters::ros2
{

// Mutable toolbar state (time window synced from subplot manager each frame).
struct PlotToolbarState
{
    float time_window_s{30.0f};
    bool  live{true};
    bool  x_links_enabled{true};
    bool  pruning_enabled{true};
    float prune_buffer_s{60.0f};
    int   active_slot{1};
};

struct PlotToolbarActions
{
    std::function<void(float)> set_time_window;
    std::function<void(bool)>  set_live;
    std::function<void()>      autofit;
    std::function<void()>      clear_plot;
    std::function<void()>      add_subplot;
    std::function<void()>      remove_subplot;
    std::function<void(bool)>  set_x_links;
    std::function<void()>      export_screenshot;
    std::function<void()>      export_video;
    std::function<void(bool)>  set_pruning;
    std::function<void(float)> set_prune_buffer;
};

// Draw the plot-area toolbar strip. Returns the height consumed (for layout).
float draw_plot_toolbar(PlotToolbarState& state, const PlotToolbarActions& actions);

}   // namespace spectra::adapters::ros2

#endif   // SPECTRA_USE_IMGUI
