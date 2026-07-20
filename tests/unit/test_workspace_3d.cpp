#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/series3d.hpp>

#include "ui/workspace/workspace.hpp"

using namespace spectra;

// ─── Axes3DState Struct ─────────────────────────────────────────────────────

TEST(WorkspaceAxes3DState, DefaultValues)
{
    WorkspaceData::Axes3DState a3;
    EXPECT_EQ(a3.axes_index, 0u);
    EXPECT_FLOAT_EQ(a3.z_min, 0.0f);
    EXPECT_FLOAT_EQ(a3.z_max, 1.0f);
    EXPECT_TRUE(a3.z_label.empty());
    EXPECT_TRUE(a3.camera_state.empty());
    EXPECT_EQ(a3.grid_planes, 1);
    EXPECT_TRUE(a3.show_bounding_box);
    EXPECT_TRUE(a3.lighting_enabled);
    EXPECT_FLOAT_EQ(a3.light_dir_x, 1.0f);
    EXPECT_FLOAT_EQ(a3.light_dir_y, 1.0f);
    EXPECT_FLOAT_EQ(a3.light_dir_z, 1.0f);
}

TEST(WorkspaceAxes3DState, Is3DFlag)
{
    WorkspaceData::AxisState as;
    EXPECT_FALSE(as.is_3d);
    as.is_3d = true;
    EXPECT_TRUE(as.is_3d);
}

// ─── SeriesState 3D Fields ──────────────────────────────────────────────────

TEST(WorkspaceSeries3D, DefaultValues)
{
    WorkspaceData::SeriesState ss;
    EXPECT_EQ(ss.colormap_type, 0);
    EXPECT_FLOAT_EQ(ss.ambient, 0.0f);
    EXPECT_FLOAT_EQ(ss.specular, 0.0f);
    EXPECT_FLOAT_EQ(ss.shininess, 0.0f);
}

TEST(WorkspaceSeries3D, TypeStrings)
{
    WorkspaceData::SeriesState ss;
    ss.type = "line3d";
    EXPECT_EQ(ss.type, "line3d");
    ss.type = "scatter3d";
    EXPECT_EQ(ss.type, "scatter3d");
    ss.type = "surface";
    EXPECT_EQ(ss.type, "surface");
    ss.type = "mesh";
    EXPECT_EQ(ss.type, "mesh");
}

// ─── Format Version ─────────────────────────────────────────────────────────

TEST(WorkspaceVersion, FormatVersionIs5)
{
    EXPECT_EQ(WorkspaceData::FORMAT_VERSION, 5u);
}

// ─── Serialization Round-Trip ───────────────────────────────────────────────

TEST(Workspace3DRoundTrip, EmptyWorkspace)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    std::string path = "/tmp/spectra_test_ws3d_empty.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    std::filesystem::remove(path);
}

TEST(Workspace3DRoundTrip, SingleAxes3D)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    fig.title = "3D Test";

    WorkspaceData::AxisState ax;
    ax.is_3d   = true;
    ax.x_min   = -10.0f;
    ax.x_max   = 10.0f;
    ax.y_min   = -5.0f;
    ax.y_max   = 5.0f;
    ax.x_label = "X Axis";
    ax.y_label = "Y Axis";
    ax.title   = "3D Plot";
    fig.axes.push_back(ax);

    WorkspaceData::Axes3DState a3;
    a3.axes_index        = 0;
    a3.z_min             = -3.0f;
    a3.z_max             = 3.0f;
    a3.z_label           = "Z Axis";
    a3.camera_state      = "{\"azimuth\":45,\"elevation\":30}";
    a3.grid_planes       = 7;
    a3.show_bounding_box = true;
    a3.lighting_enabled  = false;
    a3.light_dir_x       = 0.5f;
    a3.light_dir_y       = 0.7f;
    a3.light_dir_z       = 1.0f;
    fig.axes_3d.push_back(a3);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_single.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].axes.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].axes[0].is_3d);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].x_min, -10.0f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes[0].x_max, 10.0f);

    ASSERT_EQ(loaded.figures[0].axes_3d.size(), 1u);
    const auto& la3 = loaded.figures[0].axes_3d[0];
    EXPECT_EQ(la3.axes_index, 0u);
    EXPECT_FLOAT_EQ(la3.z_min, -3.0f);
    EXPECT_FLOAT_EQ(la3.z_max, 3.0f);
    EXPECT_EQ(la3.z_label, "Z Axis");
    EXPECT_FALSE(la3.camera_state.empty());
    EXPECT_EQ(la3.grid_planes, 7);
    EXPECT_TRUE(la3.show_bounding_box);
    EXPECT_FALSE(la3.lighting_enabled);
    EXPECT_FLOAT_EQ(la3.light_dir_x, 0.5f);
    EXPECT_FLOAT_EQ(la3.light_dir_y, 0.7f);
    EXPECT_FLOAT_EQ(la3.light_dir_z, 1.0f);

    std::filesystem::remove(path);
}

TEST(Workspace3DRoundTrip, Mixed2DAnd3DAxes)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;

    // 2D axes
    WorkspaceData::AxisState ax2d;
    ax2d.is_3d = false;
    ax2d.x_min = 0.0f;
    ax2d.x_max = 100.0f;
    fig.axes.push_back(ax2d);

    // 3D axes
    WorkspaceData::AxisState ax3d;
    ax3d.is_3d = true;
    ax3d.x_min = -1.0f;
    ax3d.x_max = 1.0f;
    fig.axes.push_back(ax3d);

    WorkspaceData::Axes3DState a3;
    a3.axes_index = 1;
    a3.z_min      = -2.0f;
    a3.z_max      = 2.0f;
    fig.axes_3d.push_back(a3);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_mixed.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    ASSERT_EQ(loaded.figures[0].axes.size(), 2u);
    EXPECT_FALSE(loaded.figures[0].axes[0].is_3d);
    EXPECT_TRUE(loaded.figures[0].axes[1].is_3d);

    ASSERT_EQ(loaded.figures[0].axes_3d.size(), 1u);
    EXPECT_EQ(loaded.figures[0].axes_3d[0].axes_index, 1u);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes_3d[0].z_min, -2.0f);

    std::filesystem::remove(path);
}

TEST(Workspace3DRoundTrip, Series3DMetadata)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    WorkspaceData::AxisState   ax;
    ax.is_3d = true;
    fig.axes.push_back(ax);

    WorkspaceData::SeriesState ss;
    ss.type          = "surface";
    ss.name          = "sin_cos";
    ss.colormap_type = 1;   // Viridis
    ss.ambient       = 0.2f;
    ss.specular      = 0.5f;
    ss.shininess     = 64.0f;
    ss.point_count   = 10000;
    fig.series.push_back(ss);

    WorkspaceData::SeriesState ms;
    ms.type      = "mesh";
    ms.name      = "custom_mesh";
    ms.ambient   = 0.1f;
    ms.specular  = 0.3f;
    ms.shininess = 32.0f;
    fig.series.push_back(ms);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_series.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    ASSERT_EQ(loaded.figures[0].series.size(), 2u);

    const auto& ls = loaded.figures[0].series[0];
    EXPECT_EQ(ls.type, "surface");
    EXPECT_EQ(ls.name, "sin_cos");
    EXPECT_EQ(ls.colormap_type, 1);
    EXPECT_FLOAT_EQ(ls.ambient, 0.2f);
    EXPECT_FLOAT_EQ(ls.specular, 0.5f);
    EXPECT_FLOAT_EQ(ls.shininess, 64.0f);

    const auto& lm = loaded.figures[0].series[1];
    EXPECT_EQ(lm.type, "mesh");
    EXPECT_FLOAT_EQ(lm.ambient, 0.1f);

    std::filesystem::remove(path);
}

TEST(Workspace3DRoundTrip, ModeTransitionState)
{
    WorkspaceData data;
    data.version               = WorkspaceData::FORMAT_VERSION;
    data.mode_transition_state = "{\"duration\":0.8,\"direction\":1}";

    std::string path = "/tmp/spectra_test_ws3d_mt.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    EXPECT_FALSE(loaded.mode_transition_state.empty());
    EXPECT_NE(loaded.mode_transition_state.find("0.8"), std::string::npos);

    std::filesystem::remove(path);
}

// ─── Backward Compatibility ─────────────────────────────────────────────────

TEST(Workspace3DBackwardCompat, V3FileLoadsWithDefaults)
{
    // Simulate a v3 file (no is_3d, no axes_3d, no 3D series fields)
    std::string v3_json = R"({
        "version": 3,
        "theme_name": "dark",
        "active_figure_index": 0,
        "panels": {"inspector_visible": true, "inspector_width": 320, "nav_rail_expanded": false},
        "figures": [
            {
                "title": "Old Figure",
                "width": 1280, "height": 720,
                "grid_rows": 1, "grid_cols": 1,
                "axes": [{"x_min": 0, "x_max": 10, "y_min": 0, "y_max": 5, "auto_fit": false, "grid_visible": true, "x_label": "", "y_label": "", "title": ""}],
                "series": [{"name": "s1", "type": "line", "color_r": 1, "color_g": 0, "color_b": 0, "color_a": 1, "line_width": 2, "marker_size": 6, "visible": true, "point_count": 100, "opacity": 1, "line_style": 1, "marker_style": 0, "dash_pattern": []}]
            }
        ],
        "interaction": {"crosshair_enabled": false, "tooltip_enabled": true, "markers": []},
        "undo_count": 0, "redo_count": 0,
        "axis_link_state": "",
        "transforms": [],
        "shortcut_overrides": [],
        "timeline": {"playhead": 0, "duration": 10, "fps": 30, "loop_mode": 0, "loop_start": 0, "loop_end": 0, "playing": false},
        "plugin_state": "",
        "data_palette_name": ""
    })";

    std::string path = "/tmp/spectra_test_ws3d_v3compat.spectra";
    {
        std::ofstream f(path);
        f << v3_json;
    }

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    EXPECT_EQ(loaded.version, 3u);

    // v3 files should have is_3d = false (default)
    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].axes.size(), 1u);
    EXPECT_FALSE(loaded.figures[0].axes[0].is_3d);

    // No 3D axes state
    EXPECT_TRUE(loaded.figures[0].axes_3d.empty());

    // Series should have default 3D fields
    ASSERT_EQ(loaded.figures[0].series.size(), 1u);
    EXPECT_EQ(loaded.figures[0].series[0].colormap_type, 0);
    EXPECT_FLOAT_EQ(loaded.figures[0].series[0].ambient, 0.0f);

    // No mode transition state
    EXPECT_TRUE(loaded.mode_transition_state.empty());

    std::filesystem::remove(path);
}

TEST(Workspace3DBackwardCompat, FutureVersionRejected)
{
    std::string future_json = R"({"version": 99})";
    std::string path        = "/tmp/spectra_test_ws3d_future.spectra";
    {
        std::ofstream f(path);
        f << future_json;
    }

    WorkspaceData loaded;
    EXPECT_FALSE(Workspace::load(path, loaded));
    std::filesystem::remove(path);
}

// ─── Multiple 3D Axes ───────────────────────────────────────────────────────

TEST(Workspace3DMultiple, TwoAxes3DInOneFigure)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    fig.grid_rows = 1;
    fig.grid_cols = 2;

    for (int i = 0; i < 2; ++i)
    {
        WorkspaceData::AxisState ax;
        ax.is_3d = true;
        ax.x_min = static_cast<float>(-i - 1);
        ax.x_max = static_cast<float>(i + 1);
        fig.axes.push_back(ax);

        WorkspaceData::Axes3DState a3;
        a3.axes_index  = static_cast<size_t>(i);
        a3.z_min       = static_cast<float>(-i - 2);
        a3.z_max       = static_cast<float>(i + 2);
        a3.grid_planes = (i == 0) ? 1 : 7;
        fig.axes_3d.push_back(a3);
    }

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_multi.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    ASSERT_EQ(loaded.figures[0].axes.size(), 2u);
    ASSERT_EQ(loaded.figures[0].axes_3d.size(), 2u);

    EXPECT_EQ(loaded.figures[0].axes_3d[0].grid_planes, 1);
    EXPECT_EQ(loaded.figures[0].axes_3d[1].grid_planes, 7);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes_3d[1].z_min, -3.0f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes_3d[1].z_max, 3.0f);

    std::filesystem::remove(path);
}

// ─── Camera State Serialization ─────────────────────────────────────────────

TEST(Workspace3DCamera, CameraStateRoundTrip)
{
    Camera cam;
    cam.azimuth         = 60.0f;
    cam.elevation       = 45.0f;
    cam.distance        = 12.0f;
    cam.fov             = 50.0f;
    cam.projection_mode = Camera::ProjectionMode::Perspective;
    cam.update_position_from_orbit();

    std::string serialized = cam.serialize();
    EXPECT_FALSE(serialized.empty());

    Camera cam2;
    cam2.deserialize(serialized);
    EXPECT_NEAR(cam2.azimuth, 60.0f, 0.1f);
    EXPECT_NEAR(cam2.elevation, 45.0f, 0.1f);
    EXPECT_NEAR(cam2.distance, 12.0f, 0.1f);
}

TEST(Workspace3DCamera, CameraInWorkspaceRoundTrip)
{
    Camera cam;
    cam.azimuth   = 120.0f;
    cam.elevation = 15.0f;
    cam.distance  = 20.0f;
    cam.update_position_from_orbit();

    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    WorkspaceData::AxisState   ax;
    ax.is_3d = true;
    fig.axes.push_back(ax);

    WorkspaceData::Axes3DState a3;
    a3.axes_index   = 0;
    a3.camera_state = cam.serialize();
    fig.axes_3d.push_back(a3);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_cam.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    ASSERT_EQ(loaded.figures[0].axes_3d.size(), 1u);
    Camera restored;
    restored.deserialize(loaded.figures[0].axes_3d[0].camera_state);
    EXPECT_NEAR(restored.azimuth, 120.0f, 0.5f);
    EXPECT_NEAR(restored.elevation, 15.0f, 0.5f);
    EXPECT_NEAR(restored.distance, 20.0f, 0.5f);

    std::filesystem::remove(path);
}

// ─── Special Characters ─────────────────────────────────────────────────────

TEST(Workspace3DSpecialChars, LabelsWithSpecialChars)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    WorkspaceData::AxisState   ax;
    ax.is_3d   = true;
    ax.x_label = "X \"axis\"";
    ax.y_label = "Y\\axis";
    fig.axes.push_back(ax);

    WorkspaceData::Axes3DState a3;
    a3.axes_index = 0;
    a3.z_label    = "Z\nlabel";
    fig.axes_3d.push_back(a3);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_special.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    // JSON escaping should preserve the strings
    EXPECT_EQ(loaded.figures[0].axes[0].x_label, "X \"axis\"");
    EXPECT_EQ(loaded.figures[0].axes_3d[0].z_label, "Z\nlabel");

    std::filesystem::remove(path);
}

// ─── Lighting State ─────────────────────────────────────────────────────────

TEST(Workspace3DLighting, LightingDisabled)
{
    WorkspaceData data;
    data.version = WorkspaceData::FORMAT_VERSION;

    WorkspaceData::FigureState fig;
    WorkspaceData::AxisState   ax;
    ax.is_3d = true;
    fig.axes.push_back(ax);

    WorkspaceData::Axes3DState a3;
    a3.axes_index       = 0;
    a3.lighting_enabled = false;
    a3.light_dir_x      = 0.0f;
    a3.light_dir_y      = 1.0f;
    a3.light_dir_z      = 0.0f;
    fig.axes_3d.push_back(a3);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_light.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    EXPECT_FALSE(loaded.figures[0].axes_3d[0].lighting_enabled);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes_3d[0].light_dir_x, 0.0f);
    EXPECT_FLOAT_EQ(loaded.figures[0].axes_3d[0].light_dir_y, 1.0f);

    std::filesystem::remove(path);
}

// ─── Full State Round-Trip ──────────────────────────────────────────────────

TEST(Workspace3DFull, CompleteStateRoundTrip)
{
    WorkspaceData data;
    data.version               = WorkspaceData::FORMAT_VERSION;
    data.theme_name            = "light";
    data.active_figure_index   = 0;
    data.mode_transition_state = "{\"duration\":1.0}";

    WorkspaceData::FigureState fig;
    fig.title     = "Full 3D Test";
    fig.width     = 1920;
    fig.height    = 1080;
    fig.grid_rows = 2;
    fig.grid_cols = 2;

    // 2D axes at index 0
    WorkspaceData::AxisState ax2d;
    ax2d.is_3d = false;
    ax2d.x_min = 0;
    ax2d.x_max = 100;
    fig.axes.push_back(ax2d);

    // 3D axes at index 1
    WorkspaceData::AxisState ax3d;
    ax3d.is_3d = true;
    ax3d.x_min = -5;
    ax3d.x_max = 5;
    ax3d.y_min = -5;
    ax3d.y_max = 5;
    fig.axes.push_back(ax3d);

    WorkspaceData::Axes3DState a3;
    a3.axes_index        = 1;
    a3.z_min             = -3;
    a3.z_max             = 3;
    a3.z_label           = "Depth";
    a3.camera_state      = "{\"az\":45}";
    a3.grid_planes       = 3;   // XY | XZ
    a3.show_bounding_box = false;
    a3.lighting_enabled  = true;
    a3.light_dir_x       = 0.5f;
    fig.axes_3d.push_back(a3);

    // 2D series
    WorkspaceData::SeriesState s2d;
    s2d.type = "line";
    s2d.name = "2d_line";
    fig.series.push_back(s2d);

    // 3D series
    WorkspaceData::SeriesState s3d;
    s3d.type          = "surface";
    s3d.name          = "3d_surface";
    s3d.colormap_type = 2;   // Plasma
    s3d.ambient       = 0.15f;
    s3d.specular      = 0.4f;
    s3d.shininess     = 48.0f;
    fig.series.push_back(s3d);

    data.figures.push_back(fig);

    std::string path = "/tmp/spectra_test_ws3d_full.spectra";
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));

    EXPECT_EQ(loaded.version, WorkspaceData::FORMAT_VERSION);
    EXPECT_EQ(loaded.theme_name, "light");
    EXPECT_FALSE(loaded.mode_transition_state.empty());

    ASSERT_EQ(loaded.figures.size(), 1u);
    const auto& lf = loaded.figures[0];
    EXPECT_EQ(lf.width, 1920u);
    EXPECT_EQ(lf.grid_rows, 2);

    ASSERT_EQ(lf.axes.size(), 2u);
    EXPECT_FALSE(lf.axes[0].is_3d);
    EXPECT_TRUE(lf.axes[1].is_3d);

    ASSERT_EQ(lf.axes_3d.size(), 1u);
    EXPECT_EQ(lf.axes_3d[0].axes_index, 1u);
    EXPECT_FLOAT_EQ(lf.axes_3d[0].z_min, -3.0f);
    EXPECT_EQ(lf.axes_3d[0].z_label, "Depth");
    EXPECT_EQ(lf.axes_3d[0].grid_planes, 3);
    EXPECT_FALSE(lf.axes_3d[0].show_bounding_box);
    EXPECT_FLOAT_EQ(lf.axes_3d[0].light_dir_x, 0.5f);

    ASSERT_EQ(lf.series.size(), 2u);
    EXPECT_EQ(lf.series[0].type, "line");
    EXPECT_EQ(lf.series[1].type, "surface");
    EXPECT_EQ(lf.series[1].colormap_type, 2);
    EXPECT_FLOAT_EQ(lf.series[1].ambient, 0.15f);
    EXPECT_FLOAT_EQ(lf.series[1].shininess, 48.0f);

    std::filesystem::remove(path);
}
