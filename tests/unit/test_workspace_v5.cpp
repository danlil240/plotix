// test_workspace_v5.cpp — Workspace v5 desktop-layout round-trip and v4→v5 migration tests.
//
// Exercises Phase 6 acceptance criteria:
//   - v4/v5 fixtures load
//   - multi-window round-trip (desktop_layout with detached windows)

#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/figure.hpp>

#include "ui/workspace/workspace.hpp"

using namespace spectra;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static WorkspaceData make_v5_workspace()
{
    WorkspaceData data;
    data.version             = WorkspaceData::FORMAT_VERSION;   // 5
    data.theme_name          = "night";
    data.active_figure_index = 0;

    WorkspaceData::FigureState fig;
    fig.title     = "Main Plot";
    fig.width     = 1920;
    fig.height    = 1080;
    fig.grid_rows = 1;
    fig.grid_cols = 2;

    WorkspaceData::AxisState ax;
    ax.x_min        = 0.0f;
    ax.x_max        = 10.0f;
    ax.y_min        = -1.0f;
    ax.y_max        = 1.0f;
    ax.auto_fit     = false;
    ax.grid_visible = true;
    ax.x_label      = "Time";
    ax.y_label      = "Value";
    fig.axes.push_back(ax);

    WorkspaceData::SeriesState ser;
    ser.name       = "sensor";
    ser.type       = "line";
    ser.line_width = 2.0f;
    ser.visible    = true;
    fig.series.push_back(ser);

    data.figures.push_back(fig);

    // v5 desktop layout
    data.desktop_layout.provider                    = "native";
    data.desktop_layout.provider_version            = "1.0";
    data.desktop_layout.main_window_state_base64    = "AAAAAQAAAA==";
    data.desktop_layout.main_window_geometry_base64 = "AAAAAQAAAABkAAABAA==";
    data.desktop_layout.main_window_split_layout =
        R"({"active":7,"root":{"leaf":true,"figure":7}})";
    data.desktop_layout.main_window_figure_ids = {7, 8};
    data.desktop_layout.provider_layout        = "{}";

    // Two detached windows
    {
        WorkspaceData::DesktopLayoutState::WindowState w;
        w.state_base64    = "AAAAAQAAABg=";
        w.geometry_base64 = "AAAAAQAAABgAAAABAA==";
        w.title           = "Detached Figure 1";
        w.split_layout    = R"({"active":9,"root":{"leaf":true,"figure":9}})";
        w.figure_ids      = {9};
        data.desktop_layout.windows.push_back(w);
    }
    {
        WorkspaceData::DesktopLayoutState::WindowState w;
        w.state_base64    = "AAAAAQAAABw=";
        w.geometry_base64 = "AAAAAQAAABwAAAABAA==";
        w.title           = "Detached Figure 2";
        data.desktop_layout.windows.push_back(w);
    }

    return data;
}

// ─── V5 Round-Trip ────────────────────────────────────────────────────────────

TEST(WorkspaceV5, RoundTrip)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v5.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    auto original = make_v5_workspace();
    ASSERT_TRUE(Workspace::save(path_str, original));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    // Core fields
    EXPECT_EQ(loaded.version, 5u);
    EXPECT_EQ(loaded.theme_name, "night");

    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_EQ(loaded.figures[0].title, "Main Plot");
    EXPECT_EQ(loaded.figures[0].grid_cols, 2);

    // Desktop layout
    EXPECT_EQ(loaded.desktop_layout.provider, "native");
    EXPECT_EQ(loaded.desktop_layout.provider_version, "1.0");
    EXPECT_EQ(loaded.desktop_layout.main_window_state_base64, "AAAAAQAAAA==");
    EXPECT_EQ(loaded.desktop_layout.main_window_geometry_base64, "AAAAAQAAAABkAAABAA==");
    EXPECT_EQ(loaded.desktop_layout.main_window_split_layout,
              R"({"active":7,"root":{"leaf":true,"figure":7}})");
    EXPECT_EQ(loaded.desktop_layout.main_window_figure_ids, (std::vector<FigureId>{7, 8}));
    EXPECT_EQ(loaded.desktop_layout.provider_layout, "{}");

    // Detached windows
    ASSERT_EQ(loaded.desktop_layout.windows.size(), 2u);
    EXPECT_EQ(loaded.desktop_layout.windows[0].title, "Detached Figure 1");
    EXPECT_EQ(loaded.desktop_layout.windows[0].state_base64, "AAAAAQAAABg=");
    EXPECT_EQ(loaded.desktop_layout.windows[0].figure_ids, (std::vector<FigureId>{9}));
    EXPECT_EQ(loaded.desktop_layout.windows[0].split_layout,
              R"({"active":9,"root":{"leaf":true,"figure":9}})");
    EXPECT_EQ(loaded.desktop_layout.windows[1].title, "Detached Figure 2");
    EXPECT_EQ(loaded.desktop_layout.windows[1].geometry_base64, "AAAAAQAAABwAAAABAA==");

    std::filesystem::remove(path);
}

// ─── V5 Empty Desktop Layout ──────────────────────────────────────────────────

TEST(WorkspaceV5, EmptyDesktopLayout)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v5_empty.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    WorkspaceData data;
    data.version    = 5;
    data.theme_name = "light";

    WorkspaceData::FigureState fig;
    fig.title = "Simple";
    data.figures.push_back(fig);

    // Leave desktop_layout as defaults (empty provider, no windows)
    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    EXPECT_EQ(loaded.version, 5u);
    EXPECT_TRUE(loaded.desktop_layout.provider.empty());
    EXPECT_TRUE(loaded.desktop_layout.windows.empty());

    std::filesystem::remove(path);
}

// ─── V4 Backward Compatibility (v4 loads with v5 defaults) ────────────────────

TEST(WorkspaceV5, V4BackwardCompat)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v4compat.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    // Create a v4 workspace (no desktop_layout)
    WorkspaceData v4data;
    v4data.version               = 4;
    v4data.theme_name            = "dark";
    v4data.mode_transition_state = "idle";

    WorkspaceData::FigureState fig;
    fig.title = "V4 Figure";

    WorkspaceData::AxisState ax;
    ax.is_3d = true;
    fig.axes.push_back(ax);

    WorkspaceData::Axes3DState a3;
    a3.axes_index = 0;
    a3.z_label    = "Depth";
    fig.axes_3d.push_back(a3);

    v4data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(path_str, v4data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    // v4 file keeps version=4
    EXPECT_EQ(loaded.version, 4u);
    EXPECT_EQ(loaded.theme_name, "dark");
    EXPECT_EQ(loaded.mode_transition_state, "idle");

    // v5 desktop_layout should be defaults (empty)
    EXPECT_TRUE(loaded.desktop_layout.provider.empty());
    EXPECT_TRUE(loaded.desktop_layout.windows.empty());

    // v4 3D axes state should load
    ASSERT_EQ(loaded.figures.size(), 1u);
    ASSERT_EQ(loaded.figures[0].axes.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].axes[0].is_3d);
    ASSERT_EQ(loaded.figures[0].axes_3d.size(), 1u);
    EXPECT_EQ(loaded.figures[0].axes_3d[0].z_label, "Depth");

    std::filesystem::remove(path);
}

// ─── V5 Multi-Window Round-Trip (many detached windows) ───────────────────────

TEST(WorkspaceV5, MultiWindowRoundTrip)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v5_multi.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    WorkspaceData data;
    data.version    = 5;
    data.theme_name = "night";

    WorkspaceData::FigureState fig;
    fig.title = "Main";
    data.figures.push_back(fig);

    data.desktop_layout.provider                    = "native";
    data.desktop_layout.main_window_state_base64    = "state_main";
    data.desktop_layout.main_window_geometry_base64 = "geom_main";

    // Simulate 5 detached windows
    for (int i = 0; i < 5; ++i)
    {
        WorkspaceData::DesktopLayoutState::WindowState w;
        w.state_base64    = "state_" + std::to_string(i);
        w.geometry_base64 = "geom_" + std::to_string(i);
        w.title           = "Window " + std::to_string(i);
        data.desktop_layout.windows.push_back(w);
    }

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    EXPECT_EQ(loaded.desktop_layout.provider, "native");
    EXPECT_EQ(loaded.desktop_layout.main_window_state_base64, "state_main");
    ASSERT_EQ(loaded.desktop_layout.windows.size(), 5u);

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(loaded.desktop_layout.windows[i].title, "Window " + std::to_string(i));
        EXPECT_EQ(loaded.desktop_layout.windows[i].state_base64, "state_" + std::to_string(i));
        EXPECT_EQ(loaded.desktop_layout.windows[i].geometry_base64, "geom_" + std::to_string(i));
    }

    std::filesystem::remove(path);
}

// ─── V5 Provider Mismatch Detection ───────────────────────────────────────────
// Verify that a workspace saved with one provider can be loaded and the
// provider field is preserved for fallback logic.

TEST(WorkspaceV5, ProviderPreserved)
{
    auto path     = std::filesystem::temp_directory_path() / "spectra_test_v5_provider.spectra";
    auto path_str = path.string();
    std::filesystem::remove(path);

    WorkspaceData data;
    data.version                         = 5;
    data.desktop_layout.provider         = "kddockwidgets";
    data.desktop_layout.provider_version = "2.1.0";
    data.desktop_layout.provider_layout  = "{\"layout\":\"complex\"}";

    WorkspaceData::FigureState fig;
    fig.title = "Test";
    data.figures.push_back(fig);

    ASSERT_TRUE(Workspace::save(path_str, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path_str, loaded));

    EXPECT_EQ(loaded.desktop_layout.provider, "kddockwidgets");
    EXPECT_EQ(loaded.desktop_layout.provider_version, "2.1.0");
    EXPECT_EQ(loaded.desktop_layout.provider_layout, "{\"layout\":\"complex\"}");

    std::filesystem::remove(path);
}

TEST(WorkspaceV5, PerFigureTimelinePayloadRoundTrips)
{
    WorkspaceData data;
    data.figures.resize(2);
    data.figures[0].figure_id = 11;
    data.figures[0].timeline_json =
        R"({"duration":4,"tracks":[{"id":1,"name":"X","keyframes":[{"t":1}]}]})";
    data.figures[1].figure_id = 22;
    data.figures[1].timeline_json =
        R"({"duration":8,"tracks":[{"id":2,"name":"Y","keyframes":[{"t":2}]}]})";

    const auto path = std::filesystem::temp_directory_path() / "spectra_timeline_workspace.spectra";
    ASSERT_TRUE(Workspace::save(path.string(), data));
    WorkspaceData restored;
    ASSERT_TRUE(Workspace::load(path.string(), restored));
    std::filesystem::remove(path);
    ASSERT_EQ(restored.figures.size(), 2u);
    EXPECT_EQ(restored.figures[0].timeline_json, data.figures[0].timeline_json);
    EXPECT_EQ(restored.figures[1].timeline_json, data.figures[1].timeline_json);
}

TEST(WorkspaceV5, PopulatedFigureSnapshotSurvivesSaveLoadAndRecreation)
{
    Figure source({777, 555});
    source.set_tab_title("Recovered data");
    auto&                    axes = source.subplot(1, 1, 1);
    const std::vector<float> x    = {1.0f, 2.0f, 3.0f};
    const std::vector<float> y    = {9.0f, 7.0f, 5.0f};
    axes.scatter(x, y).label("persisted samples").size(11.0f);

    auto data = Workspace::capture({&source}, 0, "night", true, 320.0f, false);
    ASSERT_EQ(data.figures.size(), 1u);
    data.figures[0].figure_id = 42;
    ASSERT_FALSE(data.figures[0].snapshot_base64.empty());

    auto path = std::filesystem::temp_directory_path() / "spectra_populated_workspace.spectra";
    ASSERT_TRUE(Workspace::save(path.string(), data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path.string(), loaded));
    std::filesystem::remove(path);

    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_EQ(loaded.figures[0].figure_id, 42u);
    std::vector<std::unique_ptr<Figure>> restored;
    ASSERT_TRUE(Workspace::restore_figures(loaded, restored));
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored[0]->tab_title(), "Recovered data");
    EXPECT_EQ(restored[0]->width(), 777u);
    ASSERT_EQ(restored[0]->axes().size(), 1u);
    ASSERT_EQ(restored[0]->axes()[0]->series().size(), 1u);
    auto* scatter = dynamic_cast<ScatterSeries*>(restored[0]->axes()[0]->series()[0].get());
    ASSERT_NE(scatter, nullptr);
    EXPECT_EQ(std::vector<float>(scatter->x_data().begin(), scatter->x_data().end()), x);
    EXPECT_EQ(std::vector<float>(scatter->y_data().begin(), scatter->y_data().end()), y);
    EXPECT_FLOAT_EQ(scatter->size(), 11.0f);
}

TEST(WorkspaceV5, CorruptSnapshotDoesNotPartiallyReplaceFigures)
{
    WorkspaceData              data;
    WorkspaceData::FigureState state;
    state.snapshot_base64 = "not-base64";
    data.figures.push_back(state);

    std::vector<std::unique_ptr<Figure>> figures;
    figures.push_back(std::make_unique<Figure>());
    std::vector<OverlaySnapshot> overlays(1);
    overlays[0].crosshair_enabled = true;
    auto* original                = figures[0].get();
    EXPECT_FALSE(Workspace::restore_figures(data, figures, &overlays));
    ASSERT_EQ(figures.size(), 1u);
    EXPECT_EQ(figures[0].get(), original);
    ASSERT_EQ(overlays.size(), 1u);
    EXPECT_TRUE(overlays[0].crosshair_enabled);
}

TEST(WorkspaceV5, PerFigureOverlaysSurviveSnapshotRecreation)
{
    Figure                   first;
    const std::vector<float> x        = {0.0f, 1.0f};
    const std::vector<float> first_y  = {2.0f, 3.0f};
    const std::vector<float> second_y = {4.0f, 5.0f};
    first.subplot(1, 1, 1).plot(x, first_y).label("first line");
    Figure second;
    second.subplot(1, 1, 1).plot(x, second_y).label("second line");

    std::vector<OverlaySnapshot> overlays(2);
    overlays[0].crosshair_enabled = true;
    overlays[0].tooltip_enabled   = false;
    overlays[0].tool_mode         = 3;
    overlays[0].markers.push_back({0.0f, 2.0f, "first line", 0, 0});
    overlays[0].annotations.push_back(
        {1.0f, 3.0f, "remember me", {0.2f, 0.4f, 0.6f, 1.0f}, 7.0f, -12.0f, 0});
    overlays[0].region      = {true, 0.1f, 0.9f, 2.1f, 2.9f, 0};
    overlays[0].measurement = {true, 0.0, 2.0, 1.0, 3.0, 0};

    const auto data =
        Workspace::capture({&first, &second}, 0, "night", true, 320.0f, false, &overlays);
    std::vector<std::unique_ptr<Figure>> restored;
    std::vector<OverlaySnapshot>         restored_overlays;
    ASSERT_TRUE(Workspace::restore_figures(data, restored, &restored_overlays));
    ASSERT_EQ(restored.size(), 2u);
    ASSERT_EQ(restored_overlays.size(), 2u);
    EXPECT_TRUE(restored_overlays[0].crosshair_enabled);
    EXPECT_FALSE(restored_overlays[0].tooltip_enabled);
    EXPECT_EQ(restored_overlays[0].tool_mode, 3);
    ASSERT_EQ(restored_overlays[0].markers.size(), 1u);
    EXPECT_EQ(restored_overlays[0].markers[0].series_label, "first line");
    ASSERT_EQ(restored_overlays[0].annotations.size(), 1u);
    EXPECT_EQ(restored_overlays[0].annotations[0].text, "remember me");
    EXPECT_FLOAT_EQ(restored_overlays[0].annotations[0].offset_x, 7.0f);
    EXPECT_TRUE(restored_overlays[0].region.valid);
    EXPECT_FLOAT_EQ(restored_overlays[0].region.x_min, 0.1f);
    EXPECT_FLOAT_EQ(restored_overlays[0].region.y_max, 2.9f);
    EXPECT_TRUE(restored_overlays[0].measurement.valid);
    EXPECT_DOUBLE_EQ(restored_overlays[0].measurement.start_data_x, 0.0);
    EXPECT_DOUBLE_EQ(restored_overlays[0].measurement.end_data_y, 3.0);
    EXPECT_TRUE(restored_overlays[1].markers.empty());
    EXPECT_TRUE(restored_overlays[1].annotations.empty());
}
