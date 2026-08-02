#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "ui/workspace/workspace.hpp"

using namespace spectra;

// Helper to create a minimal v3 workspace
static WorkspaceData make_v3_workspace()
{
    WorkspaceData data;
    data.version             = WorkspaceData::FORMAT_VERSION;
    data.theme_name          = "dark";
    data.active_figure_index = 0;
    data.data_palette_name   = "okabe_ito";

    WorkspaceData::FigureState fig;
    fig.title            = "Test Figure";
    fig.width            = 1920;
    fig.height           = 1080;
    fig.grid_rows        = 2;
    fig.grid_cols        = 2;
    fig.is_modified      = true;
    fig.custom_tab_title = "My Tab";

    WorkspaceData::AxisState ax;
    ax.x_min        = -10.0f;
    ax.x_max        = 10.0f;
    ax.y_min        = -5.0f;
    ax.y_max        = 5.0f;
    ax.auto_fit     = false;
    ax.grid_visible = true;
    ax.x_label      = "Time (s)";
    ax.y_label      = "Amplitude";
    ax.title        = "Signal";
    fig.axes.push_back(ax);

    WorkspaceData::SeriesState ser;
    ser.name         = "sin(x)";
    ser.type         = "line";
    ser.color_r      = 0.2f;
    ser.color_g      = 0.4f;
    ser.color_b      = 0.8f;
    ser.color_a      = 1.0f;
    ser.line_width   = 2.5f;
    ser.marker_size  = 8.0f;
    ser.visible      = true;
    ser.point_count  = 1000;
    ser.opacity      = 0.9f;
    ser.line_style   = 2;   // Dashed
    ser.marker_style = 1;   // Circle
    ser.dash_pattern = {10.0f, 5.0f, 3.0f, 5.0f};
    fig.series.push_back(ser);

    data.figures.push_back(fig);
    return data;
}

// ─── V3 Round-Trip ───────────────────────────────────────────────────────────

TEST(WorkspaceV3, RoundTrip)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v3.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();
    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_EQ(loaded.theme_name, "dark");
    EXPECT_EQ(loaded.data_palette_name, "okabe_ito");
    ASSERT_EQ(loaded.figures.size(), 1u);

    const auto& fig = loaded.figures[0];
    EXPECT_EQ(fig.title, "Test Figure");
    EXPECT_EQ(fig.width, 1920u);
    EXPECT_EQ(fig.height, 1080u);
    EXPECT_TRUE(fig.is_modified);
    EXPECT_EQ(fig.custom_tab_title, "My Tab");

    ASSERT_EQ(fig.axes.size(), 1u);
    EXPECT_FLOAT_EQ(fig.axes[0].x_min, -10.0f);
    EXPECT_FLOAT_EQ(fig.axes[0].x_max, 10.0f);

    ASSERT_EQ(fig.series.size(), 1u);
    const auto& s = fig.series[0];
    EXPECT_EQ(s.name, "sin(x)");
    EXPECT_EQ(s.line_style, 2);
    EXPECT_EQ(s.marker_style, 1);
    EXPECT_FLOAT_EQ(s.opacity, 0.9f);
    ASSERT_EQ(s.dash_pattern.size(), 4u);
    EXPECT_FLOAT_EQ(s.dash_pattern[0], 10.0f);
    EXPECT_FLOAT_EQ(s.dash_pattern[1], 5.0f);
    EXPECT_FLOAT_EQ(s.dash_pattern[2], 3.0f);
    EXPECT_FLOAT_EQ(s.dash_pattern[3], 5.0f);

    std::filesystem::remove(path);
}

// ─── V2 Backward Compatibility ───────────────────────────────────────────────

TEST(WorkspaceV3, V2BackwardCompat)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v2compat.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    // Save a v2-style workspace (manually set version to 2)
    WorkspaceData v2data;
    v2data.version    = 2;
    v2data.theme_name = "light";

    WorkspaceData::FigureState fig;
    fig.title = "V2 Figure";
    WorkspaceData::SeriesState ser;
    ser.name    = "data";
    ser.type    = "line";
    ser.opacity = 0.5f;
    fig.series.push_back(ser);
    v2data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(path_str, v2data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    // v2 files should load with v3 defaults
    EXPECT_EQ(loaded.version, 2u);
    EXPECT_TRUE(loaded.axis_link_state.empty());
    EXPECT_TRUE(loaded.transforms.empty());
    EXPECT_TRUE(loaded.shortcut_overrides.empty());
    EXPECT_TRUE(loaded.data_palette_name.empty());
    EXPECT_FLOAT_EQ(loaded.timeline.playhead, 0.0f);
    EXPECT_FLOAT_EQ(loaded.timeline.duration, 10.0f);

    // Series should have default v3 fields
    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].series.size(), 1u);
    EXPECT_EQ(loaded.figures[0].series[0].line_style, 1);     // Default Solid
    EXPECT_EQ(loaded.figures[0].series[0].marker_style, 0);   // Default None
    EXPECT_TRUE(loaded.figures[0].series[0].dash_pattern.empty());

    std::filesystem::remove(path);
}

// ─── Future Version Rejection ────────────────────────────────────────────────

TEST(WorkspaceV3, FutureVersionRejected)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_future.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    // Write a file with version 99
    {
        std::ofstream f(path_str);
        f << "{\"version\": 99, \"theme_name\": \"dark\"}";
    }

    WorkspaceData loaded;
    EXPECT_FALSE(Workspace::load(path_str, loaded));

    std::filesystem::remove(path);
}

// ─── Axis Link State ─────────────────────────────────────────────────────────

TEST(WorkspaceV3, AxisLinkState)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_axislink.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data            = make_v3_workspace();
    data.axis_link_state = "groups:1,axis:3,members:0,1";

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.axis_link_state, "groups:1,axis:3,members:0,1");

    std::filesystem::remove(path);
}

// ─── Data Transform Pipelines ────────────────────────────────────────────────

TEST(WorkspaceV3, TransformPipeline)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_transforms.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();

    WorkspaceData::TransformState ts;
    ts.figure_index = 0;
    ts.axes_index   = 0;
    ts.steps.push_back({1, 0.0f, true});      // Log10
    ts.steps.push_back({10, 2.5f, true});     // Scale(2.5)
    ts.steps.push_back({11, -1.0f, false});   // Offset(-1.0), disabled
    data.transforms.push_back(ts);

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    ASSERT_EQ(loaded.transforms.size(), 1u);

    const auto& lt = loaded.transforms[0];
    EXPECT_EQ(lt.figure_index, 0u);
    EXPECT_EQ(lt.axes_index, 0u);
    ASSERT_EQ(lt.steps.size(), 3u);
    EXPECT_EQ(lt.steps[0].type, 1);
    EXPECT_TRUE(lt.steps[0].enabled);
    EXPECT_EQ(lt.steps[1].type, 10);
    EXPECT_FLOAT_EQ(lt.steps[1].param, 2.5f);
    EXPECT_EQ(lt.steps[2].type, 11);
    EXPECT_FALSE(lt.steps[2].enabled);

    std::filesystem::remove(path);
}

TEST(WorkspaceV3, TransformPipelineRoundTripsTargetAndFullParameters)
{
    auto path = std::filesystem::temp_directory_path() / "spectra_test_transform_params.spectra";
    std::filesystem::remove(path);

    auto                          data = make_v3_workspace();
    WorkspaceData::TransformState state;
    state.figure_index = 0;
    state.axes_index   = 2;
    state.series_index = 3;
    state.all_visible  = false;
    state.target       = "multi:0:1,2:3";
    state.name         = "analysis";
    WorkspaceData::TransformState::Step step;
    step.type            = 13;
    step.name            = "FFT";
    step.source          = "core";
    step.enabled         = false;
    step.params_version  = 1;
    step.scale_factor    = 2.5f;
    step.offset_value    = -3.0f;
    step.clamp_min       = -8.0f;
    step.clamp_max       = 9.0f;
    step.log_base        = 2.0f;
    step.skip_nan        = false;
    step.fft_db          = true;
    step.fft_sample_rate = 48000.0f;
    state.steps.push_back(step);
    data.transforms.push_back(state);

    ASSERT_TRUE(Workspace::save(path.string(), data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path.string(), loaded));
    ASSERT_EQ(loaded.transforms.size(), 1u);
    const auto& restored = loaded.transforms.front();
    EXPECT_EQ(restored.axes_index, 2u);
    EXPECT_EQ(restored.series_index, 3u);
    EXPECT_FALSE(restored.all_visible);
    EXPECT_EQ(restored.target, "multi:0:1,2:3");
    EXPECT_EQ(restored.name, "analysis");
    ASSERT_EQ(restored.steps.size(), 1u);
    const auto& restored_step = restored.steps.front();
    EXPECT_EQ(restored_step.name, "FFT");
    EXPECT_EQ(restored_step.source, "core");
    EXPECT_FALSE(restored_step.enabled);
    EXPECT_EQ(restored_step.params_version, 1);
    EXPECT_FLOAT_EQ(restored_step.scale_factor, 2.5f);
    EXPECT_FLOAT_EQ(restored_step.offset_value, -3.0f);
    EXPECT_FLOAT_EQ(restored_step.clamp_min, -8.0f);
    EXPECT_FLOAT_EQ(restored_step.clamp_max, 9.0f);
    EXPECT_FLOAT_EQ(restored_step.log_base, 2.0f);
    EXPECT_FALSE(restored_step.skip_nan);
    EXPECT_TRUE(restored_step.fft_db);
    EXPECT_FLOAT_EQ(restored_step.fft_sample_rate, 48000.0f);

    std::filesystem::remove(path);
}

TEST(WorkspaceV3, MultipleTransformPipelines)
{
    auto path = std::filesystem::temp_directory_path() / "spectra_test_multi_transforms.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();

    WorkspaceData::TransformState ts1;
    ts1.figure_index = 0;
    ts1.axes_index   = 0;
    ts1.steps.push_back({1, 0.0f, true});
    data.transforms.push_back(ts1);

    WorkspaceData::TransformState ts2;
    ts2.figure_index = 0;
    ts2.axes_index   = 1;
    ts2.steps.push_back({4, 0.0f, true});   // Negate
    ts2.steps.push_back({5, 0.0f, true});   // Normalize
    data.transforms.push_back(ts2);

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.transforms.size(), 2u);
    EXPECT_EQ(loaded.transforms[0].steps.size(), 1u);
    EXPECT_EQ(loaded.transforms[1].steps.size(), 2u);

    std::filesystem::remove(path);
}

// ─── Shortcut Overrides ──────────────────────────────────────────────────────

TEST(WorkspaceV3, ShortcutOverrides)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_shortcuts.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();
    data.shortcut_overrides.push_back({"view.reset", "Ctrl+R", false});
    data.shortcut_overrides.push_back({"view.zoom", "Ctrl+Plus", false});
    data.shortcut_overrides.push_back({"edit.undo", "", true});

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    ASSERT_EQ(loaded.shortcut_overrides.size(), 3u);

    EXPECT_EQ(loaded.shortcut_overrides[0].command_id, "view.reset");
    EXPECT_EQ(loaded.shortcut_overrides[0].shortcut_str, "Ctrl+R");
    EXPECT_FALSE(loaded.shortcut_overrides[0].removed);

    EXPECT_EQ(loaded.shortcut_overrides[1].command_id, "view.zoom");
    EXPECT_EQ(loaded.shortcut_overrides[1].shortcut_str, "Ctrl+Plus");

    EXPECT_EQ(loaded.shortcut_overrides[2].command_id, "edit.undo");
    EXPECT_TRUE(loaded.shortcut_overrides[2].removed);

    std::filesystem::remove(path);
}

// ─── Timeline State ──────────────────────────────────────────────────────────

TEST(WorkspaceV3, TimelineState)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_timeline.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data                = make_v3_workspace();
    data.timeline.playhead   = 3.5f;
    data.timeline.duration   = 20.0f;
    data.timeline.fps        = 60.0f;
    data.timeline.loop_mode  = 2;   // PingPong
    data.timeline.loop_start = 1.0f;
    data.timeline.loop_end   = 15.0f;
    data.timeline.playing    = true;

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    EXPECT_FLOAT_EQ(loaded.timeline.playhead, 3.5f);
    EXPECT_FLOAT_EQ(loaded.timeline.duration, 20.0f);
    EXPECT_FLOAT_EQ(loaded.timeline.fps, 60.0f);
    EXPECT_EQ(loaded.timeline.loop_mode, 2);
    EXPECT_FLOAT_EQ(loaded.timeline.loop_start, 1.0f);
    EXPECT_FLOAT_EQ(loaded.timeline.loop_end, 15.0f);
    EXPECT_TRUE(loaded.timeline.playing);

    std::filesystem::remove(path);
}

// ─── Plugin State ────────────────────────────────────────────────────────────

TEST(WorkspaceV3, PluginState)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_plugins.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data         = make_v3_workspace();
    data.plugin_state = "plugin:MyPlugin,enabled:true";

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.plugin_state, "plugin:MyPlugin,enabled:true");

    std::filesystem::remove(path);
}

// ─── Data Palette Name ───────────────────────────────────────────────────────

TEST(WorkspaceV3, DataPaletteName)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_palette.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data              = make_v3_workspace();
    data.data_palette_name = "tol_bright";

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.data_palette_name, "tol_bright");

    std::filesystem::remove(path);
}

// ─── Dash Pattern Edge Cases ─────────────────────────────────────────────────

TEST(WorkspaceV3, EmptyDashPattern)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_empty_dash.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();
    data.figures[0].series[0].dash_pattern.clear();

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_TRUE(loaded.figures[0].series[0].dash_pattern.empty());

    std::filesystem::remove(path);
}

TEST(WorkspaceV3, SingleDashValue)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_single_dash.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data                              = make_v3_workspace();
    data.figures[0].series[0].dash_pattern = {5.0f};

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    ASSERT_EQ(loaded.figures[0].series[0].dash_pattern.size(), 1u);
    EXPECT_FLOAT_EQ(loaded.figures[0].series[0].dash_pattern[0], 5.0f);

    std::filesystem::remove(path);
}

// ─── Full State Round-Trip ───────────────────────────────────────────────────

TEST(WorkspaceV3, FullStateRoundTrip)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_full_v3.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();

    // Fill all v3 fields
    data.axis_link_state   = "groups:empty";
    data.data_palette_name = "wong";
    data.plugin_state      = "plugins:empty";

    data.timeline.playhead   = 5.0f;
    data.timeline.duration   = 30.0f;
    data.timeline.fps        = 24.0f;
    data.timeline.loop_mode  = 1;
    data.timeline.loop_start = 2.0f;
    data.timeline.loop_end   = 28.0f;
    data.timeline.playing    = false;

    data.shortcut_overrides.push_back({"cmd.a", "Ctrl+A", false});
    data.shortcut_overrides.push_back({"cmd.b", "", true});

    WorkspaceData::TransformState ts;
    ts.figure_index = 0;
    ts.axes_index   = 0;
    ts.steps.push_back({3, 0.0f, true});
    data.transforms.push_back(ts);

    data.interaction.crosshair_enabled = true;
    data.interaction.tooltip_enabled   = false;
    WorkspaceData::InteractionState::MarkerEntry marker;
    marker.data_x       = 3.14f;
    marker.data_y       = 0.0f;
    marker.series_label = "sin(x)";
    data.interaction.markers.push_back(marker);

    data.undo_count = 5;
    data.redo_count = 2;

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    // Verify everything
    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_EQ(loaded.theme_name, "dark");
    EXPECT_EQ(loaded.data_palette_name, "wong");
    EXPECT_EQ(loaded.axis_link_state, "groups:empty");
    EXPECT_EQ(loaded.plugin_state, "plugins:empty");

    EXPECT_FLOAT_EQ(loaded.timeline.playhead, 5.0f);
    EXPECT_FLOAT_EQ(loaded.timeline.duration, 30.0f);
    EXPECT_FLOAT_EQ(loaded.timeline.fps, 24.0f);
    EXPECT_EQ(loaded.timeline.loop_mode, 1);
    EXPECT_FALSE(loaded.timeline.playing);

    EXPECT_EQ(loaded.shortcut_overrides.size(), 2u);
    EXPECT_EQ(loaded.transforms.size(), 1u);

    EXPECT_TRUE(loaded.interaction.crosshair_enabled);
    EXPECT_FALSE(loaded.interaction.tooltip_enabled);
    EXPECT_EQ(loaded.interaction.markers.size(), 1u);

    EXPECT_EQ(loaded.undo_count, 5u);
    EXPECT_EQ(loaded.redo_count, 2u);

    std::filesystem::remove(path);
}

// ─── Empty Workspace ─────────────────────────────────────────────────────────

TEST(WorkspaceV3, EmptyWorkspace)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_empty_v3.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_TRUE(loaded.figures.empty());
    EXPECT_TRUE(loaded.transforms.empty());
    EXPECT_TRUE(loaded.shortcut_overrides.empty());

    std::filesystem::remove(path);
}

// ─── Special Characters ──────────────────────────────────────────────────────

TEST(WorkspaceV3, SpecialCharsInStrings)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_special_v3.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data                      = make_v3_workspace();
    data.figures[0].title          = "Test \"quoted\" title";
    data.figures[0].series[0].name = "sin(x) \\ cos(x)";
    data.data_palette_name         = "palette\"with\"quotes";

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.figures[0].title, "Test \"quoted\" title");
    EXPECT_EQ(loaded.data_palette_name, "palette\"with\"quotes");

    std::filesystem::remove(path);
}

// ─── Multiple Figures ────────────────────────────────────────────────────────

TEST(WorkspaceV3, MultipleFigures)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_multi_fig_v3.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto data = make_v3_workspace();

    // Add a second figure
    WorkspaceData::FigureState fig2;
    fig2.title  = "Figure 2";
    fig2.width  = 800;
    fig2.height = 600;
    WorkspaceData::SeriesState ser2;
    ser2.name         = "cos(x)";
    ser2.type         = "scatter";
    ser2.line_style   = 0;
    ser2.marker_style = 3;   // Diamond
    fig2.series.push_back(ser2);
    data.figures.push_back(fig2);

    data.active_figure_index = 1;

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));
    EXPECT_EQ(loaded.figures.size(), 2u);
    EXPECT_EQ(loaded.active_figure_index, 1u);
    EXPECT_EQ(loaded.figures[1].title, "Figure 2");
    EXPECT_EQ(loaded.figures[1].series[0].marker_style, 3);

    std::filesystem::remove(path);
}
