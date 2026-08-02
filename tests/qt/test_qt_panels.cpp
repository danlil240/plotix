// test_qt_panels.cpp — Qt integration tests for panel visibility and dock widget behavior.
//
// Verifies:
//   - Panel visibility toggle (show/hide QDockWidget)
//   - Dock widget areas are correct
//   - View menu toggle actions create correct checkable state
//   - Welcome page show/hide
//   - Status bar message
//   - Panel object names for automation/stable IDs

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDockWidget>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGroupBox>
#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QLineEdit>
#include <QImage>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>

#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"
#include "adapters/qt/qt_series_commands.hpp"
#include "adapters/qt/figure_canvas_widget.hpp"
#include "adapters/qt/split_view_container.hpp"
#include "adapters/qt/components/spectra_app_header.hpp"
#include "adapters/qt/components/spectra_design_tokens.hpp"
#include "adapters/qt/components/spectra_document_tab_bar.hpp"
#include "adapters/qt/components/spectra_inspector_drawer.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"
#include "adapters/qt/components/spectra_nav_button.hpp"
#include "adapters/qt/components/spectra_nav_rail.hpp"
#include "adapters/qt/components/spectra_status_bar.hpp"
#include "adapters/qt/panels/inspector_widget.hpp"
#include "adapters/qt/panels/data_editor_widget.hpp"
#include "adapters/qt/panels/function_plot_dialog.hpp"
#include "adapters/qt/panels/export_widget.hpp"
#include "adapters/qt/panels/accessibility_widget.hpp"
#include "adapters/qt/panels/settings_widget.hpp"
#include "adapters/qt/panels/shortcut_widget.hpp"
#include "adapters/qt/panels/timeline_widget.hpp"
#include "adapters/qt/panels/timeline_property_binding.hpp"
#include "adapters/qt/panels/curve_editor_widget.hpp"
#include "adapters/qt/panels/transform_widget.hpp"

#include "app/frontend_services.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/series_clipboard.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/animation/timeline_editor.hpp"
#include "ui/animation/keyframe_interpolator.hpp"
#include "ui/input/input.hpp"
#include "ui/data/axis_link.hpp"
#include "io/export_registry.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/frame.hpp>

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace
{

class CountingRedrawRequest final : public spectra::RedrawRequest
{
   public:
    void request_redraw() override { ++count; }
    void request_redraw(spectra::FigureId) override { ++count; }

    int count = 0;
};

class TextClipboardService final : public spectra::ClipboardService
{
   public:
    void        copy_text(const std::string& value) override { text = value; }
    void        copy_image(const std::vector<uint8_t>&) override {}
    std::string paste_text() override { return text; }

    std::string text;
};

class PathDialogService final : public spectra::DialogService
{
   public:
    std::optional<std::string> file_dialog(FileType,
                                           const std::string& title,
                                           const std::string&,
                                           const std::vector<FileFilter>&) override
    {
        const auto it = paths.find(title);
        return it == paths.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool message_box(const std::string&, const std::string&, bool) override { return true; }
    std::optional<spectra::Color> color_picker(const std::string&, const spectra::Color&) override
    {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> paths;
};

struct QtPanelEnv
{
    std::unique_ptr<spectra::FigureRegistry>               registry;
    std::unique_ptr<spectra::CommandRegistry>              cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtPanelEnv()
    {
        registry      = std::make_unique<spectra::FigureRegistry>();
        cmd_registry  = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }
};

QtPanelEnv& env()
{
    static QtPanelEnv e;
    return e;
}

double average_luminance(const QImage& image)
{
    double samples = 0.0;
    double sum     = 0.0;
    for (int y = 0; y < image.height(); y += 4)
    {
        for (int x = 0; x < image.width(); x += 4)
        {
            const QColor color = image.pixelColor(x, y);
            sum += 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
            samples += 1.0;
        }
    }
    return samples > 0.0 ? sum / samples : 0.0;
}

}   // namespace

// ── Panel visibility: dock widget show/hide ──────────────────────────────────

TEST(QtPanels, DockWidgetShowHide)
{
    auto&       e = env();
    QMainWindow mw;

    auto* dock = new QDockWidget("Test Panel", &mw);
    dock->setObjectName("test_dock");
    mw.addDockWidget(Qt::RightDockWidgetArea, dock);

    // In offscreen mode, isVisible() checks the full widget hierarchy
    // (parent must be shown too). Use isHidden() to test the explicit
    // visibility flag instead.
    dock->setHidden(true);
    EXPECT_TRUE(dock->isHidden());
    dock->setHidden(false);
    EXPECT_FALSE(dock->isHidden());
}

TEST(QtPanels, DockWidgetTogglePattern)
{
    auto&       e = env();
    QMainWindow mw;

    auto* dock = new QDockWidget("Toggle Panel", &mw);
    dock->setObjectName("toggle_dock");
    mw.addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Simulate the toggle pattern used by SpectraMainWindow::on_toggle_*()
    // Use isHidden() to track explicit visibility state in offscreen mode
    dock->setHidden(false);
    bool hidden = dock->isHidden();
    dock->setHidden(!hidden);
    EXPECT_NE(dock->isHidden(), hidden);

    dock->setHidden(!dock->isHidden());
    EXPECT_EQ(dock->isHidden(), hidden);
}

TEST(QtPanels, MultipleDockWidgetAreas)
{
    auto&       e = env();
    QMainWindow mw;

    auto* left_dock = new QDockWidget("Left", &mw);
    left_dock->setObjectName("left_dock");
    mw.addDockWidget(Qt::LeftDockWidgetArea, left_dock);

    auto* right_dock = new QDockWidget("Right", &mw);
    right_dock->setObjectName("right_dock");
    mw.addDockWidget(Qt::RightDockWidgetArea, right_dock);

    auto* bottom_dock = new QDockWidget("Bottom", &mw);
    bottom_dock->setObjectName("bottom_dock");
    mw.addDockWidget(Qt::BottomDockWidgetArea, bottom_dock);

    // All docks should be present
    EXPECT_NE(mw.findChild<QDockWidget*>("left_dock"), nullptr);
    EXPECT_NE(mw.findChild<QDockWidget*>("right_dock"), nullptr);
    EXPECT_NE(mw.findChild<QDockWidget*>("bottom_dock"), nullptr);
}

// ── SpectraMainWindow without services (no panels built) ─────────────────────

TEST(QtPanels, MainWindowWithoutServicesHasNoPanels)
{
    auto& e = env();

    // Create main window with nullptr services — build_panels() returns early
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Panels should not be created when services is null
    EXPECT_EQ(mw.inspector_panel(), nullptr);
    EXPECT_EQ(mw.topics_panel(), nullptr);
    EXPECT_EQ(mw.settings_panel(), nullptr);
    EXPECT_EQ(mw.timeline_panel(), nullptr);
    EXPECT_EQ(mw.export_panel(), nullptr);
}

TEST(QtPanels, AxesMenuLinks2DAnd3DAxesThroughCanvasInputManager)
{
    spectra::FigureRegistry registry;
    auto                    figure   = std::make_unique<spectra::Figure>();
    auto&                   first2d  = figure->subplot(1, 2, 1);
    auto&                   second2d = figure->subplot(1, 2, 2);
    first2d.xlim(0.0, 10.0);
    second2d.xlim(-5.0, 5.0);
    auto& first3d  = figure->subplot3d(1, 2, 1);
    auto& second3d = figure->subplot3d(1, 2, 2);
    first3d.xlim(0.0, 2.0);
    second3d.xlim(-3.0, 3.0);
    first3d.zlim(0.0, 4.0);
    second3d.zlim(-8.0, 8.0);
    const spectra::FigureId id = registry.register_figure(std::move(figure));

    spectra::CommandRegistry              commands;
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::AxisLinkManager                 manager;
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &action_bridge, nullptr);
    window.set_axis_link_manager(&manager);
    ASSERT_GE(window.add_figure_tab(id), 0);
    auto* input = window.central_view()->input_handler_for(id);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->axis_link_manager(), &manager);

    auto* link_x = window.findChild<QAction*>("axes_link_x");
    auto* link_z = window.findChild<QAction*>("axes_link_z");
    auto* unlink = window.findChild<QAction*>("axes_unlink_all");
    ASSERT_NE(link_x, nullptr);
    ASSERT_NE(link_z, nullptr);
    ASSERT_NE(unlink, nullptr);

    link_x->trigger();
    EXPECT_EQ(manager.group_count(), 1u);
    EXPECT_EQ(manager.group_3d_count(), 1u);
    first2d.xlim(20.0, 30.0);
    manager.propagate_from(&first2d, {0.0, 10.0}, {-1.0, 1.0});
    EXPECT_DOUBLE_EQ(second2d.x_limits().min, 20.0);
    EXPECT_DOUBLE_EQ(second2d.x_limits().max, 30.0);
    first3d.xlim(7.0, 9.0);
    manager.propagate_from_3d(&first3d);
    EXPECT_DOUBLE_EQ(second3d.x_limits().min, 7.0);
    EXPECT_DOUBLE_EQ(second3d.x_limits().max, 9.0);
    EXPECT_DOUBLE_EQ(second3d.z_limits().min, -8.0);

    link_z->trigger();
    EXPECT_EQ(manager.group_3d_count(), 2u);
    first3d.zlim(11.0, 13.0);
    manager.propagate_from_3d(&first3d);
    EXPECT_DOUBLE_EQ(second3d.z_limits().min, 11.0);
    EXPECT_DOUBLE_EQ(second3d.z_limits().max, 13.0);

    unlink->trigger();
    EXPECT_EQ(manager.group_count(), 0u);
    EXPECT_EQ(manager.group_3d_count(), 0u);
}

TEST(QtPanels, ExportPanelDelegatesBuiltInAndPluginArtifactsToNativeCanvasCallback)
{
    spectra::FigureRegistry registry;
    const auto              id = registry.register_figure(std::make_unique<spectra::Figure>());
    spectra::ExportFormatRegistry formats;
    formats.register_format("Probe Format",
                            "probe",
                            [](const spectra::ExportContext&) { return true; });
    spectra::NullDialogService            dialogs;
    spectra::adapters::qt::QtExportWidget panel(&formats, &registry, &dialogs);
    panel.set_active_figure(id);

    spectra::FigureId seen_id = spectra::INVALID_FIGURE_ID;
    std::string       seen_format;
    std::string       seen_path;
    uint32_t          seen_width  = 0;
    uint32_t          seen_height = 0;
    panel.set_export_callback(
        [&](spectra::FigureId  figure_id,
            const std::string& format,
            const std::string& path,
            uint32_t           width,
            uint32_t           height)
        {
            seen_id     = figure_id;
            seen_format = format;
            seen_path   = path;
            seen_width  = width;
            seen_height = height;
            return true;
        });

    auto* path   = panel.findChild<QLineEdit*>("export_path");
    auto* width  = panel.findChild<QSpinBox*>("export_width");
    auto* height = panel.findChild<QSpinBox*>("export_height");
    auto* format = panel.findChild<QComboBox*>("export_format");
    auto* button = panel.findChild<QPushButton*>("export_button");
    ASSERT_NE(path, nullptr);
    ASSERT_NE(width, nullptr);
    ASSERT_NE(height, nullptr);
    ASSERT_NE(format, nullptr);
    ASSERT_NE(button, nullptr);

    path->setText("/tmp/panel-export.png");
    width->setValue(640);
    height->setValue(360);
    button->click();
    EXPECT_EQ(seen_id, id);
    EXPECT_EQ(seen_format, "png_builtin");
    EXPECT_EQ(seen_path, "/tmp/panel-export.png");
    EXPECT_EQ(seen_width, 640u);
    EXPECT_EQ(seen_height, 360u);

    const int plugin_index = format->findData("Probe Format");
    ASSERT_GE(plugin_index, 0);
    format->setCurrentIndex(plugin_index);
    path->setText("/tmp/panel-export.probe");
    button->click();
    EXPECT_EQ(seen_format, "Probe Format");
    EXPECT_EQ(seen_path, "/tmp/panel-export.probe");
}

TEST(QtPanels, AccessibilityExportsUseInjectedDialogPathsAndWriteArtifacts)
{
    namespace fs             = std::filesystem;
    const fs::path html_path = fs::temp_directory_path() / "spectra_qt_accessibility.html";
    const fs::path wav_path  = fs::temp_directory_path() / "spectra_qt_accessibility.wav";
    fs::remove(html_path);
    fs::remove(wav_path);

    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);
    const float             x[]    = {0.0f, 1.0f, 2.0f, 3.0f};
    const float             y[]    = {0.0f, 1.0f, 0.5f, -0.5f};
    axes.line(x, y).label("accessible-proof");
    const auto id = registry.register_figure(std::move(figure));

    PathDialogService dialogs;
    dialogs.paths["Export HTML Table"]       = html_path.string();
    dialogs.paths["Export Sonification WAV"] = wav_path.string();
    spectra::adapters::qt::QtAccessibilityWidget panel(&registry, &dialogs);
    panel.set_active_figure(id);

    auto* html_button = panel.findChild<QPushButton*>("accessibility_html_btn");
    auto* wav_button  = panel.findChild<QPushButton*>("accessibility_sonify_btn");
    ASSERT_NE(html_button, nullptr);
    ASSERT_NE(wav_button, nullptr);
    html_button->click();
    wav_button->click();

    ASSERT_TRUE(fs::exists(html_path));
    ASSERT_TRUE(fs::exists(wav_path));
    EXPECT_GT(fs::file_size(wav_path), 44u);
    std::ifstream     html_file(html_path);
    const std::string html((std::istreambuf_iterator<char>(html_file)),
                           std::istreambuf_iterator<char>());
    EXPECT_NE(html.find("accessible-proof"), std::string::npos);

    fs::remove(html_path);
    fs::remove(wav_path);
}

TEST(QtPanels, ShortcutRebindUpdatesManagerActionAndConflictOwner)
{
    spectra::CommandRegistry registry;
    registry.register_command("first", "First", [] {}, "Ctrl+A", "Test");
    registry.register_command("second", "Second", [] {}, "Ctrl+B", "Test");
    spectra::ShortcutManager shortcuts;
    shortcuts.set_command_registry(&registry);
    shortcuts.bind(spectra::Shortcut::from_string("Ctrl+A"), "first");
    shortcuts.bind(spectra::Shortcut::from_string("Ctrl+B"), "second");

    spectra::adapters::qt::QtActionBridge actions(registry);
    actions.rebuild();
    actions.sync_shortcuts(shortcuts);
    spectra::adapters::qt::QtShortcutWidget widget(&shortcuts, &actions);

    std::string error;
    ASSERT_TRUE(widget.rebind_command("first", QKeySequence("Ctrl+B"), &error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(shortcuts.command_for_shortcut(spectra::Shortcut::from_string("Ctrl+B")), "first");
    EXPECT_FALSE(shortcuts.shortcut_for_command("second").valid());
    EXPECT_EQ(actions.action_for("first")->shortcut(), QKeySequence("Ctrl+B"));
    EXPECT_TRUE(actions.action_for("second")->shortcuts().empty());
    EXPECT_EQ(registry.find("first")->shortcut, "Ctrl+B");
    EXPECT_TRUE(registry.find("second")->shortcut.empty());

    EXPECT_FALSE(widget.rebind_command("first", QKeySequence{}, &error));
    EXPECT_FALSE(error.empty());
}

TEST(QtPanels, MainWindowWithoutServicesHasMenus)
{
    spectra::FigureRegistry  registry;
    spectra::CommandRegistry commands;
    commands.register_command("file.test", "Test File Action", []() {}, "", "File");
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, &action_bridge, nullptr);

    // The native menu bar is intentionally hidden; the Spectra header owns
    // the legacy menu buttons and their QMenu popups.
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->findChildren<spectra::adapters::qt::SpectraMenuButton*>().size(), 9);

    const std::string menu_state = mw.automation_menu_state();
    EXPECT_NE(menu_state.find(R"("name":"File")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("name":"Help")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("name":"Axes")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("name":"Transforms")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("label":"square")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("label":"Custom Formula...")"), std::string::npos);
    EXPECT_NE(mw.findChild<QAction*>("transform_square"), nullptr);
    EXPECT_NE(mw.findChild<QAction*>("transform_custom_formula"), nullptr);
    EXPECT_NE(menu_state.find(R"("label":"Test File Action")"), std::string::npos);
    EXPECT_NE(menu_state.find(R"("enabled":true)"), std::string::npos);
}

TEST(QtPanels, MenuStripHoverSwitchesOpenMenu)
{
    // QMenu popups are not visible on the offscreen platform, so the hover
    // switch cannot be exercised there. It is verified under a real X11/Wayland
    // compositor with the Qt xcb/wayland QPA plugins.
    if (QGuiApplication::platformName() == "offscreen")
        GTEST_SKIP();

    spectra::FigureRegistry  registry;
    spectra::CommandRegistry commands;
    commands.register_command("file.test", "Test File Action", []() {}, "", "File");
    commands.register_command("edit.test", "Test Edit Action", []() {}, "", "Edit");
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, &action_bridge, nullptr);
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    ASSERT_NE(header, nullptr);
    auto* strip = header->findChild<spectra::adapters::qt::SpectraMenuStrip*>();
    ASSERT_NE(strip, nullptr);

    auto* file_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_file");
    auto* edit_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_edit");
    ASSERT_NE(file_btn, nullptr);
    ASSERT_NE(edit_btn, nullptr);

    mw.show();
    mw.raise();
    mw.activateWindow();
    QApplication::setActiveWindow(&mw);
    QTest::qWait(300);

    // Place the cursor over the File button before opening, so the menu
    // strip sees the cursor in the menu zone and does not auto-close.
    QCursor::setPos(file_btn->mapToGlobal(file_btn->rect().center()));
    QTest::qWait(50);

    // Open the File menu directly.
    file_btn->popup_menu();
    QTest::qWait(300);

    QMenu* file_menu = file_btn->menu();
    QMenu* edit_menu = edit_btn->menu();
    ASSERT_NE(file_menu, nullptr);
    ASSERT_NE(edit_menu, nullptr);
    EXPECT_TRUE(file_menu->isVisible());
    EXPECT_FALSE(edit_menu->isVisible());

    // Hover over the Edit button while the File menu is open.
    const QPoint edit_center = edit_btn->mapToGlobal(edit_btn->rect().center());
    QCursor::setPos(edit_center);
    QTest::qWait(100);

    EXPECT_FALSE(file_menu->isVisible());
    EXPECT_TRUE(edit_menu->isVisible());
}

TEST(QtPanels, MenuStripHoverRapidSwitching)
{
    if (QGuiApplication::platformName() == "offscreen")
        GTEST_SKIP();

    spectra::FigureRegistry  registry;
    spectra::CommandRegistry commands;
    commands.register_command("file.test", "Test File Action", []() {}, "", "File");
    commands.register_command("edit.test", "Test Edit Action", []() {}, "", "Edit");
    commands.register_command("view.test", "Test View Action", []() {}, "", "View");
    commands.register_command("tools.test", "Test Tools Action", []() {}, "", "Tools");
    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, &action_bridge, nullptr);
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    ASSERT_NE(header, nullptr);
    auto* strip = header->findChild<spectra::adapters::qt::SpectraMenuStrip*>();
    ASSERT_NE(strip, nullptr);

    auto* file_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_file");
    auto* edit_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_edit");
    auto* view_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_view");
    auto* tools_btn =
        strip->findChild<spectra::adapters::qt::SpectraMenuButton*>("spectra_menu_tools");
    ASSERT_NE(file_btn, nullptr);
    ASSERT_NE(edit_btn, nullptr);
    ASSERT_NE(view_btn, nullptr);
    ASSERT_NE(tools_btn, nullptr);

    mw.show();
    mw.raise();
    mw.activateWindow();
    QApplication::setActiveWindow(&mw);
    QTest::qWait(300);

    QCursor::setPos(file_btn->mapToGlobal(file_btn->rect().center()));
    QTest::qWait(50);

    file_btn->popup_menu();
    QTest::qWait(300);

    QMenu* file_menu  = file_btn->menu();
    QMenu* edit_menu  = edit_btn->menu();
    QMenu* view_menu  = view_btn->menu();
    QMenu* tools_menu = tools_btn->menu();
    ASSERT_NE(file_menu, nullptr);
    ASSERT_NE(edit_menu, nullptr);
    ASSERT_NE(view_menu, nullptr);
    ASSERT_NE(tools_menu, nullptr);
    EXPECT_TRUE(file_menu->isVisible());
    EXPECT_FALSE(edit_menu->isVisible());
    EXPECT_FALSE(view_menu->isVisible());
    EXPECT_FALSE(tools_menu->isVisible());

    // Sweep across the menu bar quickly. The strip must keep the last
    // hovered menu open and not dismiss everything during the motion.
    QCursor::setPos(edit_btn->mapToGlobal(edit_btn->rect().center()));
    QTest::qWait(40);
    QCursor::setPos(view_btn->mapToGlobal(view_btn->rect().center()));
    QTest::qWait(40);
    QCursor::setPos(tools_btn->mapToGlobal(tools_btn->rect().center()));
    QTest::qWait(200);

    EXPECT_FALSE(file_menu->isVisible());
    EXPECT_FALSE(edit_menu->isVisible());
    EXPECT_FALSE(view_menu->isVisible());
    EXPECT_TRUE(tools_menu->isVisible());

    // Only the button that is actually under the cursor should be highlighted;
    // previously-opened buttons must not retain a stale hover state.
    EXPECT_FALSE(file_btn->underMouse());
    EXPECT_FALSE(edit_btn->underMouse());
    EXPECT_FALSE(view_btn->underMouse());
    EXPECT_TRUE(tools_btn->underMouse());
}

TEST(QtPanels, MainWindowHasStatusBar)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* status = mw.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    ASSERT_NE(status, nullptr);
    mw.set_status("Test message");
    EXPECT_EQ(status->message().toStdString(), "Test message");
}

TEST(QtPanels, CustomShellControlsExposeNamesAndDocumentTabsAreKeyboardOperable)
{
    spectra::adapters::qt::SpectraDocumentTabBar tabs;
    tabs.add_tab("First", 1);
    tabs.add_tab("Second", 2);
    tabs.add_tab("Third", 3);
    tabs.set_active_tab(2);
    EXPECT_EQ(tabs.accessibleName(), "Document tabs");
    EXPECT_TRUE(tabs.accessibleDescription().contains("3 document tabs"));
    EXPECT_TRUE(tabs.accessibleDescription().contains("Active: Second"));
    EXPECT_EQ(tabs.focusPolicy(), Qt::StrongFocus);

    int selected = -1;
    int closed   = -1;
    int detached = -1;
    int added    = 0;
    QObject::connect(&tabs,
                     &spectra::adapters::qt::SpectraDocumentTabBar::tab_selected,
                     [&selected](int id) { selected = id; });
    QObject::connect(&tabs,
                     &spectra::adapters::qt::SpectraDocumentTabBar::tab_closed,
                     [&closed](int id) { closed = id; });
    QObject::connect(&tabs,
                     &spectra::adapters::qt::SpectraDocumentTabBar::tab_detach_requested,
                     [&detached](int id) { detached = id; });
    QObject::connect(&tabs,
                     &spectra::adapters::qt::SpectraDocumentTabBar::tab_add_requested,
                     [&added]() { ++added; });

    QTest::keyClick(&tabs, Qt::Key_Right);
    EXPECT_EQ(tabs.active_tab_id(), 3);
    EXPECT_EQ(selected, 3);
    QTest::keyClick(&tabs, Qt::Key_Home);
    EXPECT_EQ(tabs.active_tab_id(), 1);
    QTest::keyClick(&tabs, Qt::Key_Left);
    EXPECT_EQ(tabs.active_tab_id(), 3);
    QTest::keyClick(&tabs, Qt::Key_Delete);
    EXPECT_EQ(closed, 3);
    QTest::keyClick(&tabs, Qt::Key_Insert);
    EXPECT_EQ(added, 1);
    QTest::keyClick(&tabs, Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyRelease(&tabs, Qt::Key_Shift);
    QTest::keyRelease(&tabs, Qt::Key_Control);
    EXPECT_EQ(detached, 3);
    EXPECT_EQ(QApplication::keyboardModifiers(), Qt::NoModifier);

    spectra::FigureRegistry  registry;
    spectra::CommandRegistry commands;
    commands.register_command("file.test", "Test", [] {}, "", "File");
    spectra::adapters::qt::QtActionBridge actions(commands);
    actions.rebuild();
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);

    auto* rail = window.findChild<spectra::adapters::qt::SpectraNavRail*>("spectra_nav_rail");
    ASSERT_NE(rail, nullptr);
    for (auto* button : rail->findChildren<spectra::adapters::qt::SpectraNavButton*>())
    {
        EXPECT_FALSE(button->accessibleName().isEmpty());
        EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    }
    QPushButton* home_button = nullptr;
    for (auto* button : window.findChildren<QPushButton*>())
        if (button->toolTip() == "Home (Home)")
            home_button = button;
    ASSERT_NE(home_button, nullptr);
    EXPECT_EQ(home_button->accessibleName(), "Home");
    EXPECT_TRUE(home_button->accessibleDescription().contains("Shortcut Home"));
    EXPECT_EQ(home_button->focusPolicy(), Qt::StrongFocus);

    for (const char* id :
         {"window_minimize_button", "window_maximize_button", "window_close_button"})
    {
        auto* button = window.findChild<QPushButton*>(id);
        ASSERT_NE(button, nullptr);
        EXPECT_FALSE(button->accessibleName().isEmpty());
        EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    }
}

TEST(QtPanels, SharedThemeRecolorsNativeAndCustomShellSurfaces)
{
    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);
    auto* header = mw.findChild<spectra::adapters::qt::SpectraAppHeader*>("spectra_app_header");
    auto* rail   = mw.findChild<spectra::adapters::qt::SpectraNavRail*>("spectra_nav_rail");
    ASSERT_NE(header, nullptr);
    ASSERT_NE(rail, nullptr);

    spectra::ui::ThemeManager themes;
    themes.ensure_initialized();
    themes.set_theme("night");
    mw.apply_theme(themes.colors());
    mw.show();
    QApplication::processEvents();
    const double dark_header = average_luminance(header->grab().toImage());
    const double dark_rail   = average_luminance(rail->grab().toImage());

    themes.set_theme("light");
    mw.apply_theme(themes.colors());
    QApplication::processEvents();
    const double light_header = average_luminance(header->grab().toImage());
    const double light_rail   = average_luminance(rail->grab().toImage());

    EXPECT_GT(light_header, dark_header + 0.45);
    EXPECT_GT(light_rail, dark_rail + 0.40);
    EXPECT_EQ(spectra::adapters::qt::spectra_colors().window_base, QColor("#d9e4ee"));
    EXPECT_TRUE(qApp->styleSheet().contains("#d9e4ee", Qt::CaseInsensitive));
    EXPECT_FALSE(qApp->styleSheet().contains("#0a0f18", Qt::CaseInsensitive));

    // Avoid leaking the light global application stylesheet into later tests.
    mw.refresh_theme();
}

TEST(QtPanels, MainWindowHasWelcomePage)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // No figures should be open — active figure should be invalid
    // Note: figure_tab_count may count the welcome page tab, so just check active_figure_id
    EXPECT_EQ(mw.active_figure_id(), spectra::INVALID_FIGURE_ID);
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, MainWindowHasCentralView)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* central = mw.central_view();
    ASSERT_NE(central, nullptr);
    EXPECT_EQ(central->objectName().toStdString(), "central_view");
}

TEST(QtPanels, MainWindowNavRailVisibilityUsesLiveShellState)
{
    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    auto* rail = mw.findChild<spectra::adapters::qt::SpectraNavRail*>("spectra_nav_rail");
    ASSERT_NE(rail, nullptr);
    EXPECT_TRUE(mw.is_nav_rail_visible());

    mw.set_nav_rail_visible(false);
    EXPECT_TRUE(rail->isHidden());
    EXPECT_FALSE(mw.is_nav_rail_visible());

    mw.set_nav_rail_visible(true);
    EXPECT_FALSE(rail->isHidden());
    EXPECT_TRUE(mw.is_nav_rail_visible());
}

TEST(QtPanels, SettingsVisibilityControlsPersistAndEmitSemanticState)
{
    spectra::ui::settings::SettingsStore store;
    spectra::ui::ThemeManager            theme;
    theme.ensure_initialized();
    spectra::adapters::qt::QtSettingsWidget settings(&store, &theme);

    bool inspector_visible = true;
    bool nav_rail_visible  = true;
    bool timeline_visible  = false;
    QObject::connect(&settings,
                     &spectra::adapters::qt::QtSettingsWidget::inspector_visibility_changed,
                     [&](bool visible) { inspector_visible = visible; });
    QObject::connect(&settings,
                     &spectra::adapters::qt::QtSettingsWidget::nav_rail_visibility_changed,
                     [&](bool visible) { nav_rail_visible = visible; });
    QObject::connect(&settings,
                     &spectra::adapters::qt::QtSettingsWidget::timeline_visibility_changed,
                     [&](bool visible) { timeline_visible = visible; });

    auto* inspector = settings.findChild<QCheckBox*>("inspector_visible_check");
    auto* nav_rail  = settings.findChild<QCheckBox*>("nav_rail_visible_check");
    auto* timeline  = settings.findChild<QCheckBox*>("timeline_visible_check");
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(nav_rail, nullptr);
    ASSERT_NE(timeline, nullptr);

    inspector->setChecked(false);
    nav_rail->setChecked(false);
    timeline->setChecked(true);

    EXPECT_FALSE(store.data().inspector_visible);
    EXPECT_FALSE(store.data().nav_rail_visible);
    EXPECT_TRUE(store.data().timeline_visible);
    EXPECT_FALSE(inspector_visible);
    EXPECT_FALSE(nav_rail_visible);
    EXPECT_TRUE(timeline_visible);
}

TEST(QtPanels, TimelineWidgetReflectsSharedSemanticModelWithoutAdvancingIt)
{
    spectra::TimelineEditor timeline;
    timeline.set_duration(4.0f);
    timeline.set_fps(20.0f);
    const uint32_t track_id = timeline.add_track("Opacity");
    timeline.add_keyframe(track_id, 1.0f);

    spectra::adapters::qt::QtTimelineWidget widget(&timeline);
    auto* duration = widget.findChild<QDoubleSpinBox*>("timeline_duration");
    auto* fps      = widget.findChild<QSpinBox*>("timeline_fps");
    auto* scrubber = widget.findChild<QSlider*>("timeline_scrubber");
    auto* tracks   = widget.findChild<QListWidget*>("timeline_tracks");
    auto* play     = widget.findChild<QPushButton*>("timeline_play");
    ASSERT_NE(duration, nullptr);
    ASSERT_NE(fps, nullptr);
    ASSERT_NE(scrubber, nullptr);
    ASSERT_NE(tracks, nullptr);
    ASSERT_NE(play, nullptr);
    EXPECT_TRUE(widget.isEnabled());
    EXPECT_DOUBLE_EQ(duration->value(), 4.0);
    EXPECT_EQ(fps->value(), 20);
    ASSERT_EQ(tracks->count(), 1);
    EXPECT_TRUE(tracks->item(0)->text().contains("Opacity"));

    timeline.set_playhead(1.0f);
    timeline.play();
    timeline.set_playhead(1.0f);
    widget.on_tick();
    EXPECT_FLOAT_EQ(timeline.playhead(), 1.0f);
    EXPECT_EQ(scrubber->value(), 100);

    timeline.step_forward();
    widget.on_tick();
    EXPECT_FLOAT_EQ(timeline.playhead(), 1.05f);
    EXPECT_EQ(scrubber->value(), static_cast<int>(timeline.playhead() * 100));

    duration->setValue(6.0);
    fps->setValue(30);
    EXPECT_FLOAT_EQ(timeline.duration(), 6.0f);
    EXPECT_FLOAT_EQ(timeline.fps(), 30.0f);

    spectra::TimelineEditor second;
    second.set_duration(8.0f);
    second.set_fps(24.0f);
    widget.set_timeline(&second);
    EXPECT_DOUBLE_EQ(duration->value(), 8.0);
    EXPECT_EQ(fps->value(), 24);
    EXPECT_FALSE(second.is_playing());
    play->click();
    EXPECT_TRUE(second.is_playing());
    EXPECT_TRUE(timeline.is_playing());

    widget.set_timeline(nullptr);
    EXPECT_FALSE(widget.isEnabled());
}

TEST(QtPanels, TimelineWidgetAuthorsTracksAndKeyframesWithUndo)
{
    spectra::TimelineEditor                 timeline;
    spectra::UndoManager                    undo;
    spectra::adapters::qt::QtTimelineWidget widget(&timeline);
    widget.set_undo_manager(&undo);

    auto* track_name      = widget.findChild<QLineEdit*>("timeline_track_name");
    auto* add_track       = widget.findChild<QPushButton*>("timeline_add_track");
    auto* rename_track    = widget.findChild<QPushButton*>("timeline_rename_track");
    auto* tracks          = widget.findChild<QListWidget*>("timeline_tracks");
    auto* keyframes       = widget.findChild<QListWidget*>("timeline_keyframes");
    auto* keyframe_time   = widget.findChild<QDoubleSpinBox*>("timeline_keyframe_time");
    auto* add_keyframe    = widget.findChild<QPushButton*>("timeline_add_keyframe");
    auto* move_keyframe   = widget.findChild<QPushButton*>("timeline_move_keyframe");
    auto* remove_keyframe = widget.findChild<QPushButton*>("timeline_remove_keyframe");
    auto* visible         = widget.findChild<QCheckBox*>("timeline_track_visible");
    auto* locked          = widget.findChild<QCheckBox*>("timeline_track_locked");
    ASSERT_NE(track_name, nullptr);
    ASSERT_NE(add_track, nullptr);
    ASSERT_NE(rename_track, nullptr);
    ASSERT_NE(tracks, nullptr);
    ASSERT_NE(keyframes, nullptr);
    ASSERT_NE(keyframe_time, nullptr);
    ASSERT_NE(add_keyframe, nullptr);
    ASSERT_NE(move_keyframe, nullptr);
    ASSERT_NE(remove_keyframe, nullptr);
    ASSERT_NE(visible, nullptr);
    ASSERT_NE(locked, nullptr);

    int changed = 0;
    QObject::connect(&widget,
                     &spectra::adapters::qt::QtTimelineWidget::timeline_changed,
                     [&changed]() { ++changed; });

    track_name->setText("Opacity");
    add_track->click();
    ASSERT_EQ(timeline.track_count(), 1u);
    ASSERT_EQ(tracks->count(), 1);
    const uint32_t track_id = timeline.tracks()[0].id;
    EXPECT_EQ(tracks->currentRow(), 0);

    keyframe_time->setValue(1.25);
    add_keyframe->click();
    ASSERT_EQ(timeline.get_track(track_id)->keyframes.size(), 1u);
    EXPECT_FLOAT_EQ(timeline.get_track(track_id)->keyframes[0].time, 1.25f);

    keyframes->setCurrentRow(0);
    keyframe_time->setValue(2.0);
    move_keyframe->click();
    ASSERT_EQ(timeline.get_track(track_id)->keyframes.size(), 1u);
    EXPECT_FLOAT_EQ(timeline.get_track(track_id)->keyframes[0].time, 2.0f);

    track_name->setText("Alpha");
    rename_track->click();
    EXPECT_EQ(timeline.get_track(track_id)->name, "Alpha");

    visible->setChecked(false);
    EXPECT_FALSE(timeline.get_track(track_id)->visible);
    locked->setChecked(true);
    EXPECT_TRUE(timeline.get_track(track_id)->locked);

    ASSERT_TRUE(undo.undo());
    EXPECT_FALSE(timeline.get_track(track_id)->locked);
    ASSERT_TRUE(undo.redo());
    EXPECT_TRUE(timeline.get_track(track_id)->locked);
    ASSERT_TRUE(undo.undo());

    keyframes->setCurrentRow(0);
    remove_keyframe->click();
    EXPECT_TRUE(timeline.get_track(track_id)->keyframes.empty());
    ASSERT_TRUE(undo.undo());
    ASSERT_EQ(timeline.get_track(track_id)->keyframes.size(), 1u);
    EXPECT_FLOAT_EQ(timeline.get_track(track_id)->keyframes[0].time, 2.0f);
    EXPECT_GE(changed, 9);
}

TEST(QtPanels, TimelineWidgetAuthorsValueInterpolationWithUndoAndPersistence)
{
    spectra::TimelineEditor       timeline;
    spectra::KeyframeInterpolator interpolator;
    spectra::UndoManager          undo;
    timeline.set_interpolator(&interpolator);
    spectra::adapters::qt::QtTimelineWidget widget(&timeline);
    widget.set_undo_manager(&undo);

    auto* track_name   = widget.findChild<QLineEdit*>("timeline_track_name");
    auto* add_track    = widget.findChild<QPushButton*>("timeline_add_track");
    auto* time         = widget.findChild<QDoubleSpinBox*>("timeline_keyframe_time");
    auto* value        = widget.findChild<QDoubleSpinBox*>("timeline_keyframe_value");
    auto* mode         = widget.findChild<QComboBox*>("timeline_keyframe_interpolation");
    auto* add          = widget.findChild<QPushButton*>("timeline_add_keyframe");
    auto* set_value    = widget.findChild<QPushButton*>("timeline_set_keyframe_value");
    auto* keyframes    = widget.findChild<QListWidget*>("timeline_keyframes");
    auto* tangent_mode = widget.findChild<QComboBox*>("timeline_tangent_mode");
    auto* in_dt        = widget.findChild<QDoubleSpinBox*>("timeline_in_tangent_dt");
    auto* in_dv        = widget.findChild<QDoubleSpinBox*>("timeline_in_tangent_dv");
    auto* out_dt       = widget.findChild<QDoubleSpinBox*>("timeline_out_tangent_dt");
    auto* out_dv       = widget.findChild<QDoubleSpinBox*>("timeline_out_tangent_dv");
    auto* set_tangents = widget.findChild<QPushButton*>("timeline_set_tangents");
    ASSERT_NE(track_name, nullptr);
    ASSERT_NE(add_track, nullptr);
    ASSERT_NE(time, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(set_value, nullptr);
    ASSERT_NE(keyframes, nullptr);
    ASSERT_NE(tangent_mode, nullptr);
    ASSERT_NE(in_dt, nullptr);
    ASSERT_NE(in_dv, nullptr);
    ASSERT_NE(out_dt, nullptr);
    ASSERT_NE(out_dv, nullptr);
    ASSERT_NE(set_tangents, nullptr);

    track_name->setText("Opacity");
    add_track->click();
    const uint32_t track_id = timeline.tracks().front().id;
    time->setValue(1.5);
    value->setValue(0.25);
    mode->setCurrentIndex(static_cast<int>(spectra::InterpMode::EaseIn));
    add->click();
    const auto* channel = interpolator.channel(track_id);
    ASSERT_NE(channel, nullptr);
    const auto* authored = channel->find_keyframe(1.5f);
    ASSERT_NE(authored, nullptr);
    EXPECT_FLOAT_EQ(authored->value, 0.25f);
    EXPECT_EQ(authored->interp, spectra::InterpMode::EaseIn);
    ASSERT_EQ(keyframes->count(), 1);
    EXPECT_TRUE(keyframes->item(0)->text().contains("Ease In"));

    keyframes->setCurrentRow(0);
    value->setValue(0.75);
    mode->setCurrentIndex(static_cast<int>(spectra::InterpMode::Spring));
    set_value->click();
    authored = interpolator.channel(track_id)->find_keyframe(1.5f);
    ASSERT_NE(authored, nullptr);
    EXPECT_FLOAT_EQ(authored->value, 0.75f);
    EXPECT_EQ(authored->interp, spectra::InterpMode::Spring);
    ASSERT_TRUE(undo.undo());
    authored = interpolator.channel(track_id)->find_keyframe(1.5f);
    ASSERT_NE(authored, nullptr);
    EXPECT_FLOAT_EQ(authored->value, 0.25f);
    EXPECT_EQ(authored->interp, spectra::InterpMode::EaseIn);

    keyframes->setCurrentRow(0);
    tangent_mode->setCurrentIndex(static_cast<int>(spectra::TangentMode::Free));
    in_dt->setValue(-0.2);
    in_dv->setValue(-1.0);
    out_dt->setValue(0.3);
    out_dv->setValue(2.0);
    set_tangents->click();
    authored = interpolator.channel(track_id)->find_keyframe(1.5f);
    ASSERT_NE(authored, nullptr);
    EXPECT_EQ(authored->tangent_mode, spectra::TangentMode::Free);
    EXPECT_FLOAT_EQ(authored->in_tangent.dt, -0.2f);
    EXPECT_FLOAT_EQ(authored->in_tangent.dv, -1.0f);
    EXPECT_FLOAT_EQ(authored->out_tangent.dt, 0.3f);
    EXPECT_FLOAT_EQ(authored->out_tangent.dv, 2.0f);

    const std::string             serialized = timeline.serialize();
    spectra::TimelineEditor       restored;
    spectra::KeyframeInterpolator restored_interpolator;
    restored.set_interpolator(&restored_interpolator);
    ASSERT_TRUE(restored.deserialize(serialized));
    const auto* restored_keyframe = restored_interpolator.channel(track_id)->find_keyframe(1.5f);
    ASSERT_NE(restored_keyframe, nullptr);
    EXPECT_FLOAT_EQ(restored_keyframe->value, 0.25f);
    EXPECT_EQ(restored_keyframe->interp, spectra::InterpMode::EaseIn);
    EXPECT_EQ(restored_keyframe->tangent_mode, spectra::TangentMode::Free);
    EXPECT_FLOAT_EQ(restored_keyframe->in_tangent.dt, -0.2f);
    EXPECT_FLOAT_EQ(restored_keyframe->out_tangent.dv, 2.0f);

    ASSERT_TRUE(undo.undo());
    authored = interpolator.channel(track_id)->find_keyframe(1.5f);
    ASSERT_NE(authored, nullptr);
    EXPECT_EQ(authored->tangent_mode, spectra::TangentMode::Auto);
}

TEST(QtPanels, TimelinePropertyBindingAppliesAndRebindsAfterPersistence)
{
    spectra::Figure figure;
    auto&           line = figure.subplot(1, 1, 1).line();
    line.opacity(1.0f);

    spectra::TimelineEditor       timeline;
    spectra::KeyframeInterpolator interpolator;
    spectra::UndoManager          undo;
    timeline.set_interpolator(&interpolator);
    timeline.set_duration(2.0f);
    spectra::adapters::qt::QtTimelineWidget widget(&timeline);
    widget.set_undo_manager(&undo);
    widget.set_figure(&figure);

    auto* track_name = widget.findChild<QLineEdit*>("timeline_track_name");
    auto* add_track  = widget.findChild<QPushButton*>("timeline_add_track");
    auto* properties = widget.findChild<QComboBox*>("timeline_property_target");
    auto* bind       = widget.findChild<QPushButton*>("timeline_bind_property");
    auto* status     = widget.findChild<QLabel*>("timeline_property_status");
    ASSERT_NE(track_name, nullptr);
    ASSERT_NE(add_track, nullptr);
    ASSERT_NE(properties, nullptr);
    ASSERT_NE(bind, nullptr);
    ASSERT_NE(status, nullptr);

    track_name->setText("Animated Opacity");
    add_track->click();
    ASSERT_EQ(timeline.track_count(), 1u);
    const uint32_t track_id = timeline.tracks().front().id;

    int opacity_index = -1;
    for (int index = 0; index < properties->count(); ++index)
    {
        if (properties->itemData(index).toString() == "axes/0/series/0/opacity")
        {
            opacity_index = index;
            break;
        }
    }
    ASSERT_GT(opacity_index, 0);
    properties->setCurrentIndex(opacity_index);
    bind->click();
    ASSERT_NE(timeline.get_track(track_id), nullptr);
    EXPECT_EQ(timeline.get_track(track_id)->property_path, "axes/0/series/0/opacity");
    EXPECT_TRUE(status->text().startsWith("Bound"));

    timeline.add_animated_keyframe(track_id, 0.0f, 0.0f);
    timeline.add_animated_keyframe(track_id, 2.0f, 1.0f);
    timeline.set_playhead(1.0f);
    timeline.evaluate_at_playhead();
    EXPECT_FLOAT_EQ(line.opacity(), 0.5f);

    const std::string serialized = timeline.serialize();
    ASSERT_TRUE(undo.undo());
    ASSERT_NE(timeline.get_track(track_id), nullptr);
    EXPECT_TRUE(timeline.get_track(track_id)->property_path.empty());
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(timeline.get_track(track_id)->property_path, "axes/0/series/0/opacity");

    spectra::Figure restored_figure;
    auto&           restored_line = restored_figure.subplot(1, 1, 1).line();
    restored_line.opacity(1.0f);
    spectra::TimelineEditor       restored;
    spectra::KeyframeInterpolator restored_interpolator;
    restored.set_interpolator(&restored_interpolator);
    ASSERT_TRUE(restored.deserialize(serialized));
    spectra::adapters::qt::QtTimelineWidget restored_widget(&restored);
    restored_widget.set_figure(&restored_figure);
    restored.set_playhead(1.0f);
    restored.evaluate_at_playhead();
    EXPECT_FLOAT_EQ(restored_line.opacity(), 0.5f);
}

TEST(QtPanels, TimelinePropertyBindingCoversAxesAnd3DSeries)
{
    spectra::Figure figure;
    auto&           axes2d = figure.subplot(1, 2, 1);
    axes2d.xlim(-4.0, 4.0);
    auto& line3d = figure.subplot3d(1, 2, 2).line3d({}, {}, {});

    spectra::TimelineEditor       timeline;
    spectra::KeyframeInterpolator interpolator;
    timeline.set_interpolator(&interpolator);
    const uint32_t x_min = timeline.add_animated_track("X Min");
    const uint32_t width = timeline.add_animated_track("3D Width");
    ASSERT_TRUE(timeline.set_track_property_path(x_min, "axes/0/x_min"));
    ASSERT_TRUE(timeline.set_track_property_path(width, "axes/1/series/0/line_width"));
    timeline.add_animated_keyframe(x_min, 0.0f, -4.0f);
    timeline.add_animated_keyframe(x_min, 2.0f, -2.0f);
    timeline.add_animated_keyframe(width, 0.0f, 1.0f);
    timeline.add_animated_keyframe(width, 2.0f, 5.0f);

    const auto targets = spectra::adapters::qt::timeline_property_targets(figure);
    EXPECT_NE(std::find_if(targets.begin(),
                           targets.end(),
                           [](const auto& target) { return target.path == "axes/0/x_min"; }),
              targets.end());
    EXPECT_NE(std::find_if(targets.begin(),
                           targets.end(),
                           [](const auto& target)
                           { return target.path == "axes/1/series/0/line_width"; }),
              targets.end());

    spectra::adapters::qt::bind_timeline_properties(timeline, figure);
    timeline.set_playhead(1.0f);
    timeline.evaluate_at_playhead();
    EXPECT_DOUBLE_EQ(axes2d.x_limits().min, -3.0);
    EXPECT_FLOAT_EQ(line3d.width(), 3.0f);
}

TEST(QtPanels, CurveEditorPaintsAndDragsSharedKeyframesWithUndo)
{
    spectra::TimelineEditor       timeline;
    spectra::KeyframeInterpolator interpolator;
    spectra::UndoManager          undo;
    timeline.set_interpolator(&interpolator);
    timeline.set_duration(2.0f);
    const uint32_t track = timeline.add_animated_track("Opacity");
    timeline.add_animated_keyframe(track, 0.0f, 0.0f);
    timeline.add_animated_keyframe(track, 2.0f, 1.0f);

    spectra::adapters::qt::QtCurveEditorWidget widget(&timeline);
    widget.set_undo_manager(&undo);
    widget.resize(640, 320);
    widget.show();
    QApplication::processEvents();
    QWidget* canvas = widget.curve_canvas();
    ASSERT_NE(canvas, nullptr);
    ASSERT_GT(canvas->width(), 300);
    ASSERT_GT(canvas->height(), 150);

    QImage image(canvas->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    canvas->render(&image);
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 4)
        for (int x = 0; x < image.width(); x += 4)
            colors.insert(image.pixel(x, y));
    EXPECT_GT(colors.size(), 4);

    auto* channel = interpolator.channel(track);
    ASSERT_NE(channel, nullptr);
    ASSERT_EQ(channel->keyframes().size(), 2u);
    const auto&   view = widget.curve_editor()->view();
    const QPointF start(view.time_to_x(0.0f), view.value_to_y(0.0f));
    const QPointF moved(start.x() + 50.0, start.y() - 25.0);
    QMouseEvent   press(QEvent::MouseButtonPress,
                      start,
                      canvas->mapToGlobal(start.toPoint()),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove,
                     moved,
                     canvas->mapToGlobal(moved.toPoint()),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        moved,
                        canvas->mapToGlobal(moved.toPoint()),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);

    channel = interpolator.channel(track);
    ASSERT_NE(channel, nullptr);
    ASSERT_EQ(channel->keyframes().size(), 2u);
    EXPECT_GT(channel->keyframes().front().time, 0.0f);
    EXPECT_GT(channel->keyframes().front().value, 0.0f);
    ASSERT_NE(timeline.get_track(track), nullptr);
    ASSERT_EQ(timeline.get_track(track)->keyframes.size(), 2u);
    EXPECT_FLOAT_EQ(timeline.get_track(track)->keyframes.front().time,
                    channel->keyframes().front().time);

    ASSERT_TRUE(undo.undo());
    channel = interpolator.channel(track);
    ASSERT_NE(channel, nullptr);
    EXPECT_FLOAT_EQ(channel->keyframes().front().time, 0.0f);
    EXPECT_FLOAT_EQ(channel->keyframes().front().value, 0.0f);
    EXPECT_FLOAT_EQ(timeline.get_track(track)->keyframes.front().time, 0.0f);
}

TEST(QtPanels, CanvasAnimationTickDrivesSharedTimelineAndFigureCallback)
{
    spectra::FigureRegistry registry;
    spectra::TimelineEditor timeline;
    timeline.set_duration(2.0f);
    timeline.set_fps(10.0f);
    timeline.set_loop_mode(spectra::LoopMode::Loop);

    int            callback_count = 0;
    spectra::Frame last_frame;
    auto           figure = std::make_unique<spectra::Figure>();
    figure->subplot(1, 1, 1);
    figure->animate()
        .duration(2.0f)
        .fps(10.0f)
        .loop(true)
        .on_frame(
            [&](spectra::Frame& frame)
            {
                ++callback_count;
                last_frame = frame;
            })
        .play();
    auto*      figure_ptr = figure.get();
    const auto figure_id  = registry.register_figure(std::move(figure));

    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow window(
        nullptr,
        &registry,
        e.action_bridge.get(),
        nullptr,
        [&](spectra::FigureId id) { return id == figure_id ? &timeline : nullptr; });
    ASSERT_GE(window.add_figure_tab(figure_id), 0);
    auto* canvas = window.canvas_for(figure_id);
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(canvas->vulkanWindow(), nullptr);

    timeline.play();
    canvas->vulkanWindow()->tickAnimation(0.25f);
    EXPECT_NEAR(timeline.playhead(), 0.25f, 0.0001f);
    EXPECT_NEAR(figure_ptr->anim_.time, 0.25f, 0.0001f);
    EXPECT_EQ(callback_count, 1);
    EXPECT_NEAR(last_frame.elapsed_sec, 0.25f, 0.0001f);
    EXPECT_NEAR(last_frame.dt, 0.25f, 0.0001f);
    EXPECT_FALSE(last_frame.paused);

    timeline.pause();
    timeline.scrub_to(1.25f);
    canvas->vulkanWindow()->tickAnimation(0.5f);
    EXPECT_NEAR(figure_ptr->anim_.time, 1.25f, 0.0001f);
    EXPECT_EQ(callback_count, 2);
    EXPECT_FLOAT_EQ(last_frame.dt, 0.0f);
    EXPECT_TRUE(last_frame.paused);
    EXPECT_FLOAT_EQ(figure_ptr->anim_.fps, 10.0f);
    EXPECT_FLOAT_EQ(figure_ptr->anim_.duration, 2.0f);
    EXPECT_TRUE(figure_ptr->anim_.loop);
}

// ── Split view operations (no Vulkan needed for logic) ───────────────────────

TEST(QtPanels, SplitViewInitialState)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitViewReset)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    mw.reset_splits();
    EXPECT_FALSE(mw.is_split());
    EXPECT_EQ(mw.pane_count(), 1u);
}

TEST(QtPanels, SplitPreservesOpenCanvasesAndToolState)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;
    const auto              first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto              second = registry.register_figure(std::make_unique<spectra::Figure>());

    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, e.action_bridge.get(), nullptr);
    ASSERT_GE(mw.add_figure_tab(first), 0);
    ASSERT_GE(mw.add_figure_tab(second), 0);

    auto* first_canvas  = mw.canvas_for(first);
    auto* second_canvas = mw.canvas_for(second);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);

    mw.central_view()->set_active_tool(spectra::ToolMode::Select);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);

    ASSERT_TRUE(mw.split_right());
    EXPECT_EQ(mw.pane_count(), 2u);
    EXPECT_EQ(mw.canvas_for(first), first_canvas);
    EXPECT_EQ(mw.canvas_for(second), second_canvas);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);

    ASSERT_TRUE(mw.close_split());
    EXPECT_EQ(mw.pane_count(), 1u);
    EXPECT_EQ(mw.figure_tab_count(), 2);
    EXPECT_EQ(mw.canvas_for(first), first_canvas);
    EXPECT_EQ(mw.canvas_for(second), second_canvas);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
}

TEST(QtPanels, NativeCanvasFocusSelectsOwningSplitPaneForSubsequentCommands)
{
    spectra::FigureRegistry  registry;
    const auto               first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto               second = registry.register_figure(std::make_unique<spectra::Figure>());
    spectra::CommandRegistry commands;
    spectra::adapters::qt::QtActionBridge actions(commands);
    actions.rebuild();
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);
    ASSERT_GE(window.add_figure_tab(first), 0);
    ASSERT_GE(window.add_figure_tab(second), 0);
    ASSERT_TRUE(window.central_view()->activate_figure(first));
    ASSERT_TRUE(window.split_right());
    ASSERT_EQ(window.pane_count(), 2u);

    auto* first_canvas  = window.canvas_for(first);
    auto* second_canvas = window.canvas_for(second);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);
    ASSERT_EQ(window.active_figure_id(), first);

    QFocusEvent focus_in(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(second_canvas->vulkanWindow(), &focus_in);
    EXPECT_EQ(window.active_figure_id(), second);

    window.set_active_tool(spectra::ToolMode::Measure);
    EXPECT_EQ(window.central_view()->input_handler_for(second)->tool_mode(),
              spectra::ToolMode::Measure);
    EXPECT_NE(window.central_view()->input_handler_for(first)->tool_mode(),
              spectra::ToolMode::Measure);
}

TEST(QtPanels, CrossPaneTabDropMovesLiveCanvasAndMarksWorkspaceDirty)
{
    spectra::FigureRegistry  registry;
    const auto               first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto               second = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto               third  = registry.register_figure(std::make_unique<spectra::Figure>());
    spectra::CommandRegistry commands;
    spectra::adapters::qt::QtActionBridge actions(commands);
    actions.rebuild();
    spectra::adapters::qt::SpectraMainWindow window(nullptr, &registry, &actions, nullptr);
    ASSERT_GE(window.add_figure_tab(first), 0);
    ASSERT_GE(window.add_figure_tab(second), 0);
    ASSERT_GE(window.add_figure_tab(third), 0);
    ASSERT_TRUE(window.central_view()->activate_figure(first));
    ASSERT_TRUE(window.split_right());
    ASSERT_EQ(window.pane_count(), 2u);

    auto* third_canvas = window.canvas_for(third);
    auto* third_input  = window.central_view()->input_handler_for(third);
    ASSERT_NE(third_canvas, nullptr);
    ASSERT_NE(third_input, nullptr);

    QTabWidget* target_pane = nullptr;
    QTabWidget* source_pane = nullptr;
    for (auto* pane : window.central_view()->findChildren<QTabWidget*>())
    {
        if (pane->indexOf(window.canvas_for(second)) >= 0)
            target_pane = pane;
        if (pane->indexOf(third_canvas) >= 0)
            source_pane = pane;
    }
    ASSERT_NE(target_pane, nullptr);
    ASSERT_NE(source_pane, nullptr);
    ASSERT_NE(target_pane, source_pane);
    ASSERT_TRUE(target_pane->tabBar()->acceptDrops());

    int workspace_changes = 0;
    QObject::connect(&window,
                     &spectra::adapters::qt::SpectraMainWindow::workspace_state_changed,
                     [&workspace_changes]() { ++workspace_changes; });
    QMimeData mime;
    mime.setData("application/x-spectra-figure-id", QByteArray::number(third));
    QDragEnterEvent enter(QPoint(5, 5), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target_pane->tabBar(), &enter);
    ASSERT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(5, 5), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target_pane->tabBar(), &drop);
    ASSERT_TRUE(drop.isAccepted());
    QCoreApplication::processEvents();

    EXPECT_EQ(window.canvas_for(third), third_canvas);
    EXPECT_EQ(window.central_view()->input_handler_for(third), third_input);
    EXPECT_EQ(window.active_figure_id(), third);
    EXPECT_EQ(workspace_changes, 1);

    QTabWidget* restored_target = nullptr;
    QTabWidget* restored_source = nullptr;
    for (auto* pane : window.central_view()->findChildren<QTabWidget*>())
    {
        if (pane->indexOf(window.canvas_for(second)) >= 0)
            restored_target = pane;
        if (pane->indexOf(window.canvas_for(first)) >= 0)
            restored_source = pane;
    }
    ASSERT_NE(restored_target, nullptr);
    ASSERT_NE(restored_source, nullptr);
    EXPECT_GE(restored_target->indexOf(third_canvas), 0);
    EXPECT_LT(restored_source->indexOf(third_canvas), 0);
}

TEST(QtPanels, NavRailUsesCommandsAndReflectsPerDocumentToolState)
{
    spectra::FigureRegistry                   registry;
    spectra::CommandRegistry                  commands;
    spectra::adapters::qt::SpectraMainWindow* window        = nullptr;
    int                                       command_count = 0;
    std::vector<std::string>                  panel_commands;

    auto register_tool = [&](const char* id, const char* label, spectra::ToolMode tool)
    {
        commands.register_command(id,
                                  label,
                                  [&, tool]()
                                  {
                                      ++command_count;
                                      if (window)
                                          window->set_active_tool(tool);
                                  });
    };
    register_tool("tool.select", "Select Tool", spectra::ToolMode::Select);
    register_tool("tool.pan", "Pan Tool", spectra::ToolMode::Pan);
    register_tool("tool.box_zoom", "Box Zoom Tool", spectra::ToolMode::BoxZoom);
    register_tool("tool.measure", "Measure Tool", spectra::ToolMode::Measure);
    register_tool("tool.annotate", "Annotate Tool", spectra::ToolMode::Annotate);
    register_tool("tool.roi", "ROI Tool", spectra::ToolMode::ROI);

    const std::array<const char*, 6> panel_command_ids = {
        "panel.toggle_inspector",
        "panel.toggle_timeline",
        "panel.toggle_curve_editor",
        "panel.toggle_plugins",
        "panel.toggle_topics",
        "panel.open_settings",
    };
    for (const char* id : panel_command_ids)
        commands
            .register_command(id, id, [&, id]() { panel_commands.emplace_back(id); }, "", "Panel");

    spectra::adapters::qt::QtActionBridge action_bridge(commands);
    action_bridge.rebuild();
    spectra::adapters::qt::SpectraMainWindow mw(nullptr, &registry, &action_bridge, nullptr);
    window = &mw;

    const auto first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto second = registry.register_figure(std::make_unique<spectra::Figure>());
    ASSERT_GE(mw.add_figure_tab(first), 0);

    auto* rail   = mw.findChild<spectra::adapters::qt::SpectraNavRail*>("spectra_nav_rail");
    auto* status = mw.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    ASSERT_NE(rail, nullptr);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(rail->active_tool_index(), 1);
    EXPECT_EQ(status->active_tool(), "Pan");

    spectra::adapters::qt::SpectraNavButton*              select_button    = nullptr;
    spectra::adapters::qt::SpectraNavButton*              transform_button = nullptr;
    std::vector<spectra::adapters::qt::SpectraNavButton*> panel_buttons;
    for (auto* button : rail->findChildren<spectra::adapters::qt::SpectraNavButton*>())
    {
        if (button->toolTip() == "Select (V)")
            select_button = button;
        else if (button->toolTip() == "Transform (T)")
            transform_button = button;
        else if (button->toolTip() == "Inspector (I)" || button->toolTip() == "Timeline (L)"
                 || button->toolTip() == "Curve Editor (C)" || button->toolTip() == "Plugins (P)"
                 || button->toolTip() == "Topics (O)" || button->toolTip() == "Settings (,)")
            panel_buttons.push_back(button);
    }
    ASSERT_NE(select_button, nullptr);
    ASSERT_NE(transform_button, nullptr);
    ASSERT_EQ(panel_buttons.size(), panel_command_ids.size());

    select_button->click();
    EXPECT_EQ(command_count, 1);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
    EXPECT_EQ(rail->active_tool_index(), 0);
    EXPECT_EQ(status->active_tool(), "Select");

    for (auto* button : panel_buttons)
        button->click();
    EXPECT_EQ(panel_commands.size(), panel_command_ids.size());
    for (const char* id : panel_command_ids)
        EXPECT_EQ(std::count(panel_commands.begin(), panel_commands.end(), id), 1);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
    EXPECT_EQ(rail->active_tool_index(), 0);

    // Panel navigation must not impersonate an interaction-tool selection.
    transform_button->click();
    EXPECT_EQ(command_count, 1);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
    EXPECT_EQ(rail->active_tool_index(), 0);
    EXPECT_EQ(status->active_tool(), "Select");

    // Each canvas owns its InputHandler. Switching documents must restore
    // the active document's tool in the rail.
    ASSERT_GE(mw.add_figure_tab(second), 0);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Pan);
    EXPECT_EQ(rail->active_tool_index(), 1);
    EXPECT_EQ(status->active_tool(), "Pan");
    ASSERT_TRUE(mw.central_view()->activate_figure(first));
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Select);
    EXPECT_EQ(rail->active_tool_index(), 0);
    EXPECT_EQ(status->active_tool(), "Select");

    ASSERT_TRUE(commands.execute("tool.pan"));
    EXPECT_EQ(command_count, 2);
    EXPECT_EQ(mw.central_view()->active_tool(), spectra::ToolMode::Pan);
    EXPECT_EQ(rail->active_tool_index(), 1);
    EXPECT_EQ(status->active_tool(), "Pan");
}

TEST(QtPanels, CrosshairStateIsOwnedByActiveCanvas)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;
    const auto              first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto              second = registry.register_figure(std::make_unique<spectra::Figure>());

    spectra::adapters::qt::SpectraMainWindow window(nullptr,
                                                    &registry,
                                                    e.action_bridge.get(),
                                                    nullptr);
    ASSERT_GE(window.add_figure_tab(first), 0);
    ASSERT_GE(window.add_figure_tab(second), 0);
    auto* first_canvas  = window.canvas_for(first);
    auto* second_canvas = window.canvas_for(second);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);
    EXPECT_FALSE(first_canvas->vulkanWindow()->crosshairEnabled());
    EXPECT_FALSE(second_canvas->vulkanWindow()->crosshairEnabled());

    ASSERT_TRUE(window.toggle_active_crosshair());
    EXPECT_FALSE(first_canvas->vulkanWindow()->crosshairEnabled());
    EXPECT_TRUE(second_canvas->vulkanWindow()->crosshairEnabled());

    ASSERT_TRUE(window.central_view()->activate_figure(first));
    ASSERT_TRUE(window.toggle_active_crosshair());
    EXPECT_TRUE(first_canvas->vulkanWindow()->crosshairEnabled());
    EXPECT_TRUE(second_canvas->vulkanWindow()->crosshairEnabled());

    ASSERT_TRUE(window.central_view()->activate_figure(second));
    ASSERT_TRUE(window.toggle_active_crosshair());
    EXPECT_TRUE(first_canvas->vulkanWindow()->crosshairEnabled());
    EXPECT_FALSE(second_canvas->vulkanWindow()->crosshairEnabled());
}

TEST(QtPanels, MarkersRailReflectsAndClearsOnlyActiveCanvasMarkers)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;
    const auto              first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto              second = registry.register_figure(std::make_unique<spectra::Figure>());
    spectra::adapters::qt::SpectraMainWindow window(nullptr,
                                                    &registry,
                                                    e.action_bridge.get(),
                                                    nullptr);
    ASSERT_GE(window.add_figure_tab(first), 0);
    ASSERT_GE(window.add_figure_tab(second), 0);

    auto* rail = window.findChild<spectra::adapters::qt::SpectraNavRail*>("spectra_nav_rail");
    ASSERT_NE(rail, nullptr);
    spectra::adapters::qt::SpectraNavButton* markers_button = nullptr;
    for (auto* button : rail->findChildren<spectra::adapters::qt::SpectraNavButton*>())
        if (button->toolTip() == "Markers (K)")
            markers_button = button;
    ASSERT_NE(markers_button, nullptr);
    EXPECT_FALSE(markers_button->isHidden());

    auto* first_canvas  = window.canvas_for(first)->vulkanWindow();
    auto* second_canvas = window.canvas_for(second)->vulkanWindow();
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);
    spectra::OverlaySnapshot first_overlay;
    first_overlay.markers.push_back({1.0f, 2.0f, "first", 0, 0});
    first_canvas->restoreOverlaySnapshot(first_overlay);
    spectra::OverlaySnapshot second_overlay;
    second_overlay.markers.push_back({3.0f, 4.0f, "second", 0, 0});
    second_canvas->restoreOverlaySnapshot(second_overlay);

    ASSERT_TRUE(window.central_view()->activate_figure(second));
    ASSERT_TRUE(
        QMetaObject::invokeMethod(second_canvas, "persistentStateChanged", Qt::DirectConnection));
    EXPECT_TRUE(markers_button->is_active());
    EXPECT_EQ(rail->active_tool_index(), 1);

    int workspace_changes = 0;
    QObject::connect(&window,
                     &spectra::adapters::qt::SpectraMainWindow::workspace_state_changed,
                     [&workspace_changes]() { ++workspace_changes; });
    markers_button->click();
    EXPECT_EQ(second_canvas->markerCount(), 0u);
    EXPECT_EQ(first_canvas->markerCount(), 1u);
    EXPECT_FALSE(markers_button->is_active());
    EXPECT_EQ(rail->active_tool_index(), 1);
    EXPECT_EQ(workspace_changes, 1);

    ASSERT_TRUE(window.central_view()->activate_figure(first));
    EXPECT_TRUE(markers_button->is_active());
}

TEST(QtPanels, CanvasSeriesSelectionCyclesPerDocumentAndCleansUpRemoval)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;

    auto                       first_figure = std::make_unique<spectra::Figure>();
    auto&                      first_axes   = first_figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y1{1.0f, 2.0f};
    const std::array<float, 2> y2{3.0f, 4.0f};
    auto&                      first_line  = first_axes.plot(x, y1);
    auto&                      second_line = first_axes.plot(x, y2);
    first_line.label("First");
    second_line.label("Second");
    const auto first_id = registry.register_figure(std::move(first_figure));

    auto       second_figure = std::make_unique<spectra::Figure>();
    auto&      second_axes   = second_figure->subplot(1, 1, 1);
    auto&      third_line    = second_axes.plot(x, y1);
    const auto second_id     = registry.register_figure(std::move(second_figure));

    spectra::adapters::qt::SpectraMainWindow window(nullptr,
                                                    &registry,
                                                    e.action_bridge.get(),
                                                    nullptr);
    ASSERT_GE(window.add_figure_tab(first_id), 0);
    ASSERT_GE(window.add_figure_tab(second_id), 0);
    auto* first_canvas  = window.canvas_for(first_id)->vulkanWindow();
    auto* second_canvas = window.canvas_for(second_id)->vulkanWindow();
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);

    ASSERT_TRUE(window.central_view()->activate_figure(first_id));
    ASSERT_TRUE(first_canvas->cycleSeriesSelection());
    ASSERT_EQ(first_canvas->selectedSeries().size(), 1u);
    EXPECT_EQ(first_canvas->selectedSeries()[0].series, &first_line);
    ASSERT_TRUE(first_canvas->cycleSeriesSelection());
    ASSERT_EQ(first_canvas->selectedSeries().size(), 1u);
    EXPECT_EQ(first_canvas->selectedSeries()[0].series, &second_line);

    ASSERT_TRUE(window.central_view()->activate_figure(second_id));
    ASSERT_TRUE(second_canvas->cycleSeriesSelection());
    ASSERT_EQ(second_canvas->selectedSeries().size(), 1u);
    EXPECT_EQ(second_canvas->selectedSeries()[0].series, &third_line);
    ASSERT_EQ(first_canvas->selectedSeries().size(), 1u);
    EXPECT_EQ(first_canvas->selectedSeries()[0].series, &second_line);

    spectra::SeriesClipboard clipboard;
    clipboard.copy(*first_canvas->selectedSeries()[0].series);
    ASSERT_TRUE(clipboard.has_data());
    ASSERT_EQ(clipboard.count(), 1u);
    auto* pasted = clipboard.paste(second_axes);
    ASSERT_NE(pasted, nullptr);
    EXPECT_EQ(pasted->label(), second_line.label());
    EXPECT_EQ(second_axes.series().size(), 2u);

    first_canvas->notifySeriesRemoved(&second_line);
    ASSERT_TRUE(first_axes.remove_series(1));
    EXPECT_TRUE(first_canvas->selectedSeries().empty());
    EXPECT_EQ(first_axes.series().size(), 1u);

    second_canvas->deselectSeries();
    EXPECT_TRUE(second_canvas->selectedSeries().empty());
}

TEST(QtPanels, SeriesDeleteTransactionSupportsMultiSelectionUndoAndRedo)
{
    spectra::Figure            figure;
    auto&                      axes = figure.subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y1{1.0f, 2.0f};
    const std::array<float, 2> y2{3.0f, 4.0f};
    auto&                      first  = axes.plot(x, y1).label("First");
    auto&                      second = axes.plot(x, y2).label("Second");
    using Selection                   = spectra::adapters::qt::SpectraVulkanWindow::SeriesSelection;
    std::vector<Selection> selected{{&axes, &axes, &first, 0, 0}, {&axes, &axes, &second, 0, 1}};

    int  removed_count = 0;
    int  changed_count = 0;
    auto action        = spectra::adapters::qt::remove_selected_series(
        selected,
        nullptr,
        false,
        [&removed_count](const spectra::Series*) { ++removed_count; },
        [&changed_count]() { ++changed_count; });
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(axes.series().size(), 0u);
    EXPECT_EQ(removed_count, 2);
    EXPECT_EQ(changed_count, 1);

    spectra::UndoManager undo;
    undo.push(std::move(*action));
    ASSERT_TRUE(undo.undo());
    ASSERT_EQ(axes.series().size(), 2u);
    EXPECT_EQ(axes.series()[0]->label(), "First");
    EXPECT_EQ(axes.series()[1]->label(), "Second");
    EXPECT_EQ(changed_count, 2);

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(axes.series().size(), 0u);
    EXPECT_EQ(removed_count, 4);
    EXPECT_EQ(changed_count, 3);
}

TEST(QtPanels, SeriesClipboardPasteTransactionSupportsUndoAndRedo)
{
    spectra::Figure            source;
    auto&                      source_axes = source.subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y1{1.0f, 2.0f};
    const std::array<float, 2> y2{3.0f, 4.0f};
    auto&                      first  = source_axes.plot(x, y1).label("First");
    auto&                      second = source_axes.plot(x, y2).label("Second");
    using Selection                   = spectra::adapters::qt::SpectraVulkanWindow::SeriesSelection;
    const std::vector<Selection> selected{{&source_axes, &source_axes, &first, 0, 0},
                                          {&source_axes, &source_axes, &second, 0, 1}};

    spectra::SeriesClipboard clipboard;
    ASSERT_TRUE(spectra::adapters::qt::copy_selected_series(selected, clipboard, true));
    EXPECT_TRUE(clipboard.is_cut());
    ASSERT_TRUE(spectra::adapters::qt::copy_selected_series(selected, clipboard, false));
    EXPECT_FALSE(clipboard.is_cut());
    ASSERT_EQ(clipboard.count(), 2u);

    spectra::Figure target_figure;
    auto&           target        = target_figure.subplot(1, 1, 1);
    int             removed_count = 0;
    int             changed_count = 0;
    auto            action        = spectra::adapters::qt::paste_series(
        target,
        clipboard,
        [&removed_count](const spectra::Series*) { ++removed_count; },
        [&changed_count]() { ++changed_count; });
    ASSERT_TRUE(action.has_value());
    ASSERT_EQ(target.series().size(), 2u);
    EXPECT_EQ(target.series()[0]->label(), "First");
    EXPECT_EQ(target.series()[1]->label(), "Second");
    EXPECT_EQ(changed_count, 1);

    spectra::UndoManager undo;
    undo.push(std::move(*action));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(target.series().size(), 0u);
    EXPECT_EQ(removed_count, 2);
    EXPECT_EQ(changed_count, 2);

    ASSERT_TRUE(undo.redo());
    ASSERT_EQ(target.series().size(), 2u);
    EXPECT_EQ(target.series()[0]->label(), "First");
    EXPECT_EQ(target.series()[1]->label(), "Second");
    EXPECT_EQ(changed_count, 3);
}

TEST(QtPanels, SeriesTransactionDoesNotDereferenceClosedDocumentAxes)
{
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{1.0f, 2.0f};
    auto&                      line = axes.plot(x, y).label("Closing");
    using Selection                 = spectra::adapters::qt::SpectraVulkanWindow::SeriesSelection;
    const std::vector<Selection> selected{{&axes, &axes, &line, 0, 0}};

    bool owner_alive = true;
    auto action      = spectra::adapters::qt::remove_selected_series(
        selected,
        nullptr,
        false,
        {},
        {},
        [&owner_alive](const spectra::AxesBase*) { return owner_alive; });
    ASSERT_TRUE(action.has_value());
    owner_alive = false;
    figure.reset();

    spectra::UndoManager undo;
    undo.push(std::move(*action));
    EXPECT_TRUE(undo.undo());
    EXPECT_TRUE(undo.redo());
}

TEST(QtPanels, DocumentChromeAndZoomFollowActiveFigureModel)
{
    spectra::FigureRegistry registry;

    auto                       first_figure = std::make_unique<spectra::Figure>();
    auto&                      first_axes   = first_figure->subplot(1, 1, 1);
    const std::array<float, 2> first_x{0.0f, 10.0f};
    const std::array<float, 2> first_y{1.0f, 2.0f};
    first_axes.plot(first_x, first_y);
    first_axes.xlim(0.0, 10.0);
    first_figure->set_tab_title("Original");
    auto*      first_ptr = first_figure.get();
    const auto first_id  = registry.register_figure(std::move(first_figure));

    auto                       second_figure = std::make_unique<spectra::Figure>();
    auto&                      second_axes   = second_figure->subplot(1, 1, 1);
    const std::array<float, 2> second_x{0.0f, 10.0f};
    const std::array<float, 2> second_y{3.0f, 4.0f};
    second_axes.plot(second_x, second_y);
    second_axes.xlim(-5.0, 15.0);
    const auto second_id = registry.register_figure(std::move(second_figure));

    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow window(nullptr,
                                                    &registry,
                                                    e.action_bridge.get(),
                                                    nullptr);
    ASSERT_GE(window.add_figure_tab(first_id), 0);

    auto* tab_bar =
        window.findChild<spectra::adapters::qt::SpectraDocumentTabBar*>("spectra_doc_tab_bar");
    auto* status = window.findChild<spectra::adapters::qt::SpectraStatusBar*>("spectra_status_bar");
    ASSERT_NE(tab_bar, nullptr);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(tab_bar->tab_title(static_cast<int>(first_id)), "Original");
    EXPECT_EQ(window.central_view()->figure_title(first_id), "Original");
    EXPECT_DOUBLE_EQ(status->zoom(), 1.0);

    first_ptr->set_tab_title("Renamed");
    first_axes.xlim(2.5, 7.5);
    auto* first_canvas = window.canvas_for(first_id);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(first_canvas->vulkanWindow(),
                                          "frameStats",
                                          Qt::DirectConnection,
                                          Q_ARG(int, 60),
                                          Q_ARG(double, 1.5)));
    EXPECT_EQ(tab_bar->tab_title(static_cast<int>(first_id)), "Renamed");
    EXPECT_EQ(window.central_view()->figure_title(first_id), "Renamed");
    EXPECT_DOUBLE_EQ(status->zoom(), 2.0);

    ASSERT_GE(window.add_figure_tab(second_id), 0);
    EXPECT_DOUBLE_EQ(status->zoom(), 0.5);

    // A background canvas may render, but it must not replace active status.
    first_axes.xlim(4.0, 6.0);
    ASSERT_TRUE(QMetaObject::invokeMethod(first_canvas->vulkanWindow(),
                                          "frameStats",
                                          Qt::DirectConnection,
                                          Q_ARG(int, 12),
                                          Q_ARG(double, 8.0)));
    EXPECT_DOUBLE_EQ(status->zoom(), 0.5);
}

// ── View menu toggle actions (when services is null, no toggle actions) ──────

TEST(QtPanels, ViewMenuHasNoToggleActionsWithoutServices)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Without services, build_panels() returns early, so no toggle actions
    QMenu* found_view = mw.findChild<QMenu*>("menu_view");
    ASSERT_NE(found_view, nullptr);

    // Should NOT have toggle actions for panels (since no panels were built)
    EXPECT_EQ(found_view->findChild<QAction*>("view_toggle_inspector"), nullptr);
    EXPECT_EQ(found_view->findChild<QAction*>("view_toggle_topics"), nullptr);
}

// ── Tab create/close with no runtime (logic only) ────────────────────────────

TEST(QtPanels, TabCountZeroInitially)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // No figure tabs should be open
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

TEST(QtPanels, CloseFigureTabWithNoTabsDoesntCrash)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Closing a non-existent tab should be safe
    mw.close_figure_tab(spectra::FigureId{42});
    EXPECT_TRUE(mw.open_figure_ids().empty());
}

// ── Command palette (Ctrl+K) without services ────────────────────────────────

TEST(QtPanels, CommandPaletteNotCreatedWithoutServices)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // open_command_palette should be safe even if palette wasn't created
    mw.open_command_palette();   // should not crash
}

TEST(QtPanels, CancelTransientUiClosesOwnedDialogAndReportsResult)
{
    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow window(nullptr,
                                                    e.registry.get(),
                                                    e.action_bridge.get(),
                                                    nullptr);

    EXPECT_FALSE(window.cancel_transient_ui());

    QDialog dialog(&window);
    dialog.setModal(false);
    dialog.show();
    QApplication::processEvents();
    ASSERT_TRUE(dialog.isVisible());

    EXPECT_TRUE(window.cancel_transient_ui());
    EXPECT_FALSE(dialog.isVisible());
    EXPECT_FALSE(window.cancel_transient_ui());
}

// ── Stable object names for automation ───────────────────────────────────────

TEST(QtPanels, StableObjectNames)
{
    auto& e = env();

    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);

    // Central view should have stable object name
    EXPECT_EQ(mw.central_view()->objectName().toStdString(), "central_view");

    EXPECT_NE(mw.findChild<QWidget*>("spectra_status_bar"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_app_header"), nullptr);
    EXPECT_NE(mw.findChild<QWidget*>("spectra_nav_rail"), nullptr);
}

TEST(QtPanels, InspectorAxisLimitEditsUseLiveControls)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);
    axes.xlim(1.0, 5.0);
    axes.ylim(2.0, 6.0);
    const auto figure_id = registry.register_figure(std::move(figure));

    {
        spectra::adapters::qt::QtInspectorWidget inspector(&registry);
        inspector.set_active_figure(figure_id);

        auto* xmin = inspector.findChild<QDoubleSpinBox*>("axes_0_x_min");
        auto* xmax = inspector.findChild<QDoubleSpinBox*>("axes_0_x_max");
        auto* ymin = inspector.findChild<QDoubleSpinBox*>("axes_0_y_min");
        auto* ymax = inspector.findChild<QDoubleSpinBox*>("axes_0_y_max");
        ASSERT_NE(xmin, nullptr);
        ASSERT_NE(xmax, nullptr);
        ASSERT_NE(ymin, nullptr);
        ASSERT_NE(ymax, nullptr);

        xmin->setValue(-3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().max, 5.0);

        xmax->setValue(8.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
        EXPECT_DOUBLE_EQ(axes.x_limits().max, 8.0);

        ymin->setValue(-4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().min, -4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().max, 6.0);

        ymax->setValue(9.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().min, -4.0);
        EXPECT_DOUBLE_EQ(axes.y_limits().max, 9.0);
    }

    EXPECT_DOUBLE_EQ(axes.x_limits().min, -3.0);
    EXPECT_DOUBLE_EQ(axes.y_limits().max, 9.0);
}

TEST(QtPanels, InspectorSummarySynchronizesLiveSizeAndUnifiedAxes)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    figure->set_size(640, 480);
    figure->subplot(1, 2, 1).title("Two dimensional");
    auto& axes3d = figure->subplot3d(1, 2, 2);
    axes3d.title("Three dimensional");
    axes3d.zlabel("Depth");
    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* size  = inspector.findChild<QLabel*>("figure_size");
    auto* count = inspector.findChild<QLabel*>("figure_axes_count");
    auto* tabs  = inspector.findChild<QTabWidget*>("inspector_tabs");
    ASSERT_NE(size, nullptr);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(size->text(), QString::fromUtf8("640 × 480"));
    EXPECT_EQ(count->text(), "2");
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), "Axes 1");
    EXPECT_EQ(tabs->tabText(1), "Axes 2 (3D)");

    auto* live_figure = registry.get(figure_id);
    ASSERT_NE(live_figure, nullptr);
    live_figure->set_size(1208, 612);
    axes3d.zlim(-5.0, 9.0);
    axes3d.zlabel("Altitude");
    inspector.sync_from_model();

    EXPECT_EQ(size->text(), QString::fromUtf8("1208 × 612"));
    auto* zmin   = inspector.findChild<QDoubleSpinBox*>("axes_1_z_min");
    auto* zmax   = inspector.findChild<QDoubleSpinBox*>("axes_1_z_max");
    auto* zlabel = inspector.findChild<QLineEdit*>("axes_1_z_label");
    ASSERT_NE(zmin, nullptr);
    ASSERT_NE(zmax, nullptr);
    ASSERT_NE(zlabel, nullptr);
    EXPECT_DOUBLE_EQ(zmin->value(), -5.0);
    EXPECT_DOUBLE_EQ(zmax->value(), 9.0);
    EXPECT_EQ(zlabel->text(), "Altitude");

    live_figure->subplot(2, 2, 3).title("New axes");
    inspector.sync_from_model();
    size  = inspector.findChild<QLabel*>("figure_size");
    count = inspector.findChild<QLabel*>("figure_axes_count");
    tabs  = inspector.findChild<QTabWidget*>("inspector_tabs");
    ASSERT_NE(size, nullptr);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(size->text(), QString::fromUtf8("1208 × 612"));
    EXPECT_EQ(count->text(), "3");
    EXPECT_EQ(tabs->count(), 3);

    auto* sparse_xmin = inspector.findChild<QDoubleSpinBox*>("axes_1_x_min");
    ASSERT_NE(sparse_xmin, nullptr);
    sparse_xmin->setValue(-7.0);
    ASSERT_GT(live_figure->axes().size(), 2u);
    ASSERT_NE(live_figure->axes()[2], nullptr);
    EXPECT_DOUBLE_EQ(live_figure->axes()[2]->x_limits().min, -7.0);
}

TEST(QtPanels, InspectorThreeDimensionalControlsMutateAuthoritativeModel)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes3d = figure->subplot3d(1, 1, 1);
    axes3d.zlim(0.0, 10.0);
    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* zlabel = inspector.findChild<QLineEdit*>("axes_0_z_label");
    auto* zmin   = inspector.findChild<QDoubleSpinBox*>("axes_0_z_min");
    auto* zmax   = inspector.findChild<QDoubleSpinBox*>("axes_0_z_max");
    auto* grid   = inspector.findChild<QComboBox*>("axes_0_grid_planes");
    auto* bounds = inspector.findChild<QCheckBox*>("axes_0_bounding_box");
    ASSERT_NE(zlabel, nullptr);
    ASSERT_NE(zmin, nullptr);
    ASSERT_NE(zmax, nullptr);
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(bounds, nullptr);

    zlabel->setText("Height");
    zmin->setValue(-3.0);
    zmax->setValue(17.0);
    grid->setCurrentIndex(grid->findData(static_cast<int>(spectra::Axes3D::GridPlane::XZ)));
    bounds->setChecked(false);

    EXPECT_EQ(axes3d.zlabel(), "Height");
    EXPECT_DOUBLE_EQ(axes3d.z_limits().min, -3.0);
    EXPECT_DOUBLE_EQ(axes3d.z_limits().max, 17.0);
    EXPECT_EQ(axes3d.grid_planes(), spectra::Axes3D::GridPlane::XZ);
    EXPECT_FALSE(axes3d.show_bounding_box());
}

TEST(QtPanels, InspectorLegendControlUpdatesFigure)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    figure->subplot(1, 1, 1);
    auto*      figure_ptr = figure.get();
    const auto figure_id  = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* legend = inspector.findChild<QCheckBox*>("figure_legend_visible");
    ASSERT_NE(legend, nullptr);
    ASSERT_TRUE(figure_ptr->legend().visible);

    legend->setChecked(false);
    EXPECT_FALSE(figure_ptr->legend().visible);
    legend->setChecked(true);
    EXPECT_TRUE(figure_ptr->legend().visible);
}

TEST(QtPanels, FunctionPlotDialogValidatesAndAddsSampledSeries)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);
    axes.xlim(-2.0, 2.0);
    const auto figure_id = registry.register_figure(std::move(figure));

    CountingRedrawRequest                       redraw;
    spectra::adapters::qt::QtFunctionPlotDialog dialog(&registry, &redraw);
    dialog.open_for_figure(figure_id);

    auto* formula = dialog.findChild<QLineEdit*>("function_formula");
    auto* xmin    = dialog.findChild<QDoubleSpinBox*>("function_x_min");
    auto* xmax    = dialog.findChild<QDoubleSpinBox*>("function_x_max");
    auto* samples = dialog.findChild<QSpinBox*>("function_samples");
    auto* error   = dialog.findChild<QLabel*>("function_validation_error");
    auto* add     = dialog.findChild<QPushButton*>("function_add");
    ASSERT_NE(formula, nullptr);
    ASSERT_NE(xmin, nullptr);
    ASSERT_NE(xmax, nullptr);
    ASSERT_NE(samples, nullptr);
    ASSERT_NE(error, nullptr);
    ASSERT_NE(add, nullptr);
    EXPECT_DOUBLE_EQ(xmin->value(), -2.0);
    EXPECT_DOUBLE_EQ(xmax->value(), 2.0);

    formula->setText("sin(");
    EXPECT_FALSE(add->isEnabled());
    EXPECT_FALSE(error->text().isEmpty());

    formula->setText("x^2");
    samples->setValue(5);
    EXPECT_TRUE(add->isEnabled());
    add->click();

    ASSERT_EQ(axes.series().size(), 1u);
    auto* line = dynamic_cast<spectra::LineSeries*>(axes.series_mut()[0].get());
    ASSERT_NE(line, nullptr);
    ASSERT_EQ(line->x_data().size(), 5u);
    ASSERT_EQ(line->y_data().size(), 5u);
    EXPECT_FLOAT_EQ(line->x_data().front(), -2.0f);
    EXPECT_FLOAT_EQ(line->x_data().back(), 2.0f);
    EXPECT_FLOAT_EQ(line->y_data().front(), 4.0f);
    EXPECT_FLOAT_EQ(line->y_data()[2], 0.0f);
    EXPECT_EQ(line->label(), "x^2");
    EXPECT_EQ(redraw.count, 1);
}

TEST(QtPanels, InspectorSemanticCommandTogglesDrawer)
{
    auto&                                    e = env();
    spectra::adapters::qt::SpectraMainWindow mw(nullptr,
                                                e.registry.get(),
                                                e.action_bridge.get(),
                                                nullptr);
    auto*                                    drawer =
        mw.findChild<spectra::adapters::qt::SpectraInspectorDrawer*>("spectra_inspector");
    ASSERT_NE(drawer, nullptr);
    EXPECT_FALSE(drawer->is_open());

    mw.toggle_inspector();
    EXPECT_TRUE(drawer->is_open());
    mw.toggle_inspector();
    EXPECT_FALSE(drawer->is_open());
}

TEST(QtPanels, InspectorCallbacksAreScopedToOwningCanvas)
{
    auto&                   e = env();
    spectra::FigureRegistry registry;
    const auto              first  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto              second = registry.register_figure(std::make_unique<spectra::Figure>());

    spectra::adapters::qt::SpectraMainWindow first_window(nullptr,
                                                          &registry,
                                                          e.action_bridge.get(),
                                                          nullptr);
    spectra::adapters::qt::SpectraMainWindow second_window(nullptr,
                                                           &registry,
                                                           e.action_bridge.get(),
                                                           nullptr);
    ASSERT_GE(first_window.add_figure_tab(first), 0);
    ASSERT_GE(second_window.add_figure_tab(second), 0);

    auto* first_drawer =
        first_window.findChild<spectra::adapters::qt::SpectraInspectorDrawer*>("spectra_inspector");
    auto* second_drawer = second_window.findChild<spectra::adapters::qt::SpectraInspectorDrawer*>(
        "spectra_inspector");
    auto* first_canvas  = first_window.canvas_for(first);
    auto* second_canvas = second_window.canvas_for(second);
    ASSERT_NE(first_drawer, nullptr);
    ASSERT_NE(second_drawer, nullptr);
    ASSERT_NE(first_canvas, nullptr);
    ASSERT_NE(second_canvas, nullptr);

    first_canvas->vulkanWindow()->toggleInspector();
    EXPECT_TRUE(first_drawer->is_open());
    EXPECT_FALSE(second_drawer->is_open());

    second_canvas->vulkanWindow()->toggleInspector();
    EXPECT_TRUE(first_drawer->is_open());
    EXPECT_TRUE(second_drawer->is_open());

    first_canvas->vulkanWindow()->toggleInspector();
    EXPECT_FALSE(first_drawer->is_open());
    EXPECT_TRUE(second_drawer->is_open());
}

TEST(QtPanels, DataEditorEditsAreUndoableAndRequestRedraw)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                      undo;
    CountingRedrawRequest                     redraw;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw);
    editor.set_active_figure(figure_id);

    auto* table = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->item(0, 1), nullptr);
    table->item(0, 1)->setText("9");

    ASSERT_EQ(line.y_data().size(), 2u);
    EXPECT_FLOAT_EQ(line.y_data()[0], 9.0f);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_EQ(redraw.count, 2);

    ASSERT_TRUE(undo.redo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 9.0f);
    EXPECT_EQ(redraw.count, 3);
}

TEST(QtPanels, DataEditorEmptyAxesUsesCompactImportRecoveryState)
{
    spectra::FigureRegistry registry;
    auto                    figure    = std::make_unique<spectra::Figure>();
    auto&                   axes      = figure->subplot(1, 1, 1);
    const auto              figure_id = registry.register_figure(std::move(figure));
    spectra::adapters::qt::QtDataEditorWidget editor(&registry);
    editor.set_active_figure(figure_id);

    auto* empty       = editor.findChild<QWidget*>("de_empty_state");
    auto* message     = editor.findChild<QLabel*>("de_empty_state_label");
    auto* import      = editor.findChild<QPushButton*>("de_empty_import_csv");
    auto* table_group = editor.findChild<QGroupBox*>("de_table_group");
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(message, nullptr);
    ASSERT_NE(import, nullptr);
    ASSERT_NE(table_group, nullptr);
    EXPECT_FALSE(empty->isHidden());
    EXPECT_TRUE(message->text().contains("No series"));
    EXPECT_TRUE(import->isEnabled());
    EXPECT_TRUE(table_group->isHidden());

    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    axes.plot(x, y);
    editor.refresh();
    EXPECT_TRUE(empty->isHidden());
    EXPECT_FALSE(table_group->isHidden());
    auto* table = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 2);
}

TEST(QtPanels, DataEditorRejectsInvalidValuesWithoutHistory)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 1> x{0.0f};
    const std::array<float, 1> y{2.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                      undo;
    CountingRedrawRequest                     redraw;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw);
    editor.set_active_figure(figure_id);

    auto* table = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(table, nullptr);
    table->item(0, 1)->setText("not-a-number");

    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_EQ(table->item(0, 1)->text(), "2");
    EXPECT_EQ(undo.undo_count(), 0u);
    EXPECT_EQ(redraw.count, 0);
}

TEST(QtPanels, DataEditorRowOperationsAndRectangularPasteAreUndoable)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 3> x{0.0f, 1.0f, 2.0f};
    const std::array<float, 3> y{10.0f, 11.0f, 12.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                      undo;
    CountingRedrawRequest                     redraw;
    TextClipboardService                      clipboard;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw, &clipboard);
    editor.set_active_figure(figure_id);
    int data_changes = 0;
    QObject::connect(&editor,
                     &spectra::adapters::qt::QtDataEditorWidget::data_changed,
                     [&data_changes]() { ++data_changes; });

    auto* table     = editor.findChild<QTableWidget*>("de_data_table");
    auto* paste     = editor.findChild<QPushButton*>("de_paste");
    auto* add       = editor.findChild<QPushButton*>("de_add_row");
    auto* remove    = editor.findChild<QPushButton*>("de_delete_rows");
    auto* move_down = editor.findChild<QPushButton*>("de_move_down");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(paste, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_NE(move_down, nullptr);

    clipboard.text = "4\t14\n5\t15";
    table->setCurrentCell(2, 0);
    paste->click();
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f, 1.0f, 4.0f, 5.0f}));
    EXPECT_EQ(std::vector<float>(line.y_data().begin(), line.y_data().end()),
              (std::vector<float>{10.0f, 11.0f, 14.0f, 15.0f}));

    add->click();
    ASSERT_EQ(line.x_data().size(), 5u);
    EXPECT_FLOAT_EQ(line.x_data().back(), 6.0f);
    EXPECT_FLOAT_EQ(line.y_data().back(), 15.0f);

    table->selectRow(1);
    remove->click();
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f, 4.0f, 5.0f, 6.0f}));

    table->selectRow(0);
    move_down->click();
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{4.0f, 0.0f, 5.0f, 6.0f}));
    EXPECT_EQ(undo.undo_count(), 4u);
    EXPECT_EQ(redraw.count, 4);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f, 4.0f, 5.0f, 6.0f}));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f, 1.0f, 4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(table->rowCount(), 5);
    EXPECT_EQ(data_changes, 6);

    const size_t history_before = undo.undo_count();
    clipboard.text              = "7\tnot-a-number";
    table->setCurrentCell(0, 0);
    paste->click();
    EXPECT_EQ(undo.undo_count(), history_before);
    EXPECT_FLOAT_EQ(line.x_data()[0], 0.0f);
    EXPECT_EQ(data_changes, 6);
}

TEST(QtPanels, DataEditorImportsAndExportsCsvThroughInjectedDialogs)
{
    const auto import_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_import.csv";
    const auto export_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_export.csv";
    {
        std::ofstream input(import_path, std::ios::binary | std::ios::trunc);
        input << "time,value\n10,1\n20,4\n30,9\n";
    }

    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager  undo;
    CountingRedrawRequest redraw;
    PathDialogService     dialogs;
    dialogs.paths["Import Series Data"] = import_path.string();
    dialogs.paths["Export Series Data"] = export_path.string();
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw, nullptr, &dialogs);
    editor.set_active_figure(figure_id);

    auto* import_csv = editor.findChild<QPushButton*>("de_import_csv");
    auto* export_csv = editor.findChild<QPushButton*>("de_export_csv");
    auto* table      = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(import_csv, nullptr);
    ASSERT_NE(export_csv, nullptr);
    ASSERT_NE(table, nullptr);

    import_csv->click();
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{10.0f, 20.0f, 30.0f}));
    EXPECT_EQ(std::vector<float>(line.y_data().begin(), line.y_data().end()),
              (std::vector<float>{1.0f, 4.0f, 9.0f}));
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);
    EXPECT_EQ(table->rowCount(), 3);

    export_csv->click();
    std::ifstream     exported(export_path, std::ios::binary);
    const std::string csv((std::istreambuf_iterator<char>(exported)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(csv, "x,y\n10,1\n20,4\n30,9\n");

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f, 1.0f}));
    EXPECT_EQ(table->rowCount(), 2);
    EXPECT_EQ(redraw.count, 2);

    std::filesystem::remove(import_path);
    std::filesystem::remove(export_path);
}

TEST(QtPanels, DataEditorPreservesAndPresentsAbsoluteTimestampOffsets)
{
    const auto import_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_timestamp_import.csv";
    const auto export_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_timestamp_export.csv";
    {
        std::ofstream input(import_path, std::ios::binary | std::ios::trunc);
        input << "ros_time_s,value\n"
                 "1783350621.752999,1\n"
                 "1783350621.772817,2\n";
    }

    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 1> x{0.0f};
    const std::array<float, 1> y{0.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager       undo;
    PathDialogService          dialogs;
    dialogs.paths["Import Series Data"] = import_path.string();
    dialogs.paths["Export Series Data"] = export_path.string();
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, nullptr, nullptr, &dialogs);
    editor.set_active_figure(figure_id);

    auto* table      = editor.findChild<QTableWidget*>("de_data_table");
    auto* import_csv = editor.findChild<QPushButton*>("de_import_csv");
    auto* export_csv = editor.findChild<QPushButton*>("de_export_csv");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(import_csv, nullptr);
    ASSERT_NE(export_csv, nullptr);
    import_csv->click();

    EXPECT_NEAR(line.x_offset(), 1783350621.752999, 1e-6);
    ASSERT_EQ(line.x_data().size(), 2u);
    EXPECT_NEAR(line.x_data()[0], 0.0f, 1e-6f);
    EXPECT_NEAR(line.x_data()[1], 0.019818f, 1e-5f);
    ASSERT_NE(table->item(1, 0), nullptr);
    EXPECT_NEAR(table->item(1, 0)->text().toDouble(), 1783350621.772817, 1e-5);

    table->item(1, 0)->setText("1783350621.782817");
    EXPECT_NEAR(line.x_data()[1], 0.029818f, 1e-5f);
    export_csv->click();
    std::ifstream exported(export_path, std::ios::binary);
    std::string   header;
    std::string   first;
    std::string   second;
    ASSERT_TRUE(static_cast<bool>(std::getline(exported, header)));
    ASSERT_TRUE(static_cast<bool>(std::getline(exported, first)));
    ASSERT_TRUE(static_cast<bool>(std::getline(exported, second)));
    EXPECT_EQ(header, "x,y");
    EXPECT_NEAR(std::stod(first.substr(0, first.find(','))), 1783350621.752999, 1e-6);
    EXPECT_NEAR(std::stod(second.substr(0, second.find(','))), 1783350621.782817, 1e-5);

    ASSERT_TRUE(undo.undo());
    EXPECT_NEAR(line.x_data()[1], 0.019818f, 1e-5f);
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(line.x_offset(), 0.0);
    EXPECT_EQ(std::vector<float>(line.x_data().begin(), line.x_data().end()),
              (std::vector<float>{0.0f}));
    ASSERT_TRUE(undo.redo());
    EXPECT_NEAR(line.x_offset(), 1783350621.752999, 1e-6);

    std::filesystem::remove(import_path);
    std::filesystem::remove(export_path);
}

TEST(QtPanels, DataEditorPaginatesLargeSeriesAndEditsAbsoluteRows)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);
    std::vector<float>      x(2505);
    std::vector<float>      y(2505);
    for (size_t index = 0; index < x.size(); ++index)
    {
        x[index] = static_cast<float>(index);
        y[index] = static_cast<float>(index * 2);
    }
    auto&                line      = axes.plot(x, y);
    const auto           figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager undo;
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo);
    editor.set_active_figure(figure_id);

    auto* table    = editor.findChild<QTableWidget*>("de_data_table");
    auto* previous = editor.findChild<QPushButton*>("de_previous_page");
    auto* next     = editor.findChild<QPushButton*>("de_next_page");
    auto* page     = editor.findChild<QLabel*>("de_page_label");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(table->rowCount(), 1000);
    EXPECT_EQ(page->text(), "Rows 1-1000 of 2505");
    EXPECT_FALSE(previous->isEnabled());
    EXPECT_TRUE(next->isEnabled());

    next->click();
    EXPECT_EQ(table->rowCount(), 1000);
    EXPECT_EQ(page->text(), "Rows 1001-2000 of 2505");
    ASSERT_NE(table->item(0, 0), nullptr);
    EXPECT_EQ(table->item(0, 0)->text(), "1000");
    next->click();
    EXPECT_EQ(table->rowCount(), 505);
    EXPECT_EQ(page->text(), "Rows 2001-2505 of 2505");
    EXPECT_FALSE(next->isEnabled());

    ASSERT_NE(table->item(4, 1), nullptr);
    table->item(4, 1)->setText("9999");
    EXPECT_FLOAT_EQ(line.y_data()[2004], 9999.0f);
    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[2004], 4008.0f);
    EXPECT_EQ(page->text(), "Rows 2001-2505 of 2505");
    previous->click();
    EXPECT_EQ(page->text(), "Rows 1001-2000 of 2505");
}

TEST(QtPanels, DataEditorMapsMultipleCsvColumnsIntoUndoableSeries)
{
    const auto import_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_multi_import.csv";
    {
        std::ofstream input(import_path, std::ios::binary | std::ios::trunc);
        input << "time,alpha,beta\n10,1,2\n20,3,4\n30,5,6\n";
    }

    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    axes.plot(x, y).label("Existing");
    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager  undo;
    CountingRedrawRequest redraw;
    PathDialogService     dialogs;
    dialogs.paths["Import Series Data"] = import_path.string();
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw, nullptr, &dialogs);
    editor.set_active_figure(figure_id);
    int data_changes = 0;
    QObject::connect(&editor,
                     &spectra::adapters::qt::QtDataEditorWidget::data_changed,
                     [&data_changes]() { ++data_changes; });

    auto* import_csv = editor.findChild<QPushButton*>("de_import_csv");
    auto* mapping    = editor.findChild<QGroupBox*>("de_import_mapping");
    auto* x_column   = editor.findChild<QComboBox*>("de_import_x_column");
    auto* y_columns  = editor.findChild<QListWidget*>("de_import_y_columns");
    auto* apply      = editor.findChild<QPushButton*>("de_apply_import_columns");
    ASSERT_NE(import_csv, nullptr);
    ASSERT_NE(mapping, nullptr);
    ASSERT_NE(x_column, nullptr);
    ASSERT_NE(y_columns, nullptr);
    ASSERT_NE(apply, nullptr);

    import_csv->click();
    EXPECT_TRUE(mapping->isVisibleTo(&editor));
    EXPECT_EQ(x_column->currentText(), "time");
    ASSERT_EQ(y_columns->count(), 3);
    EXPECT_EQ(y_columns->item(0)->checkState(), Qt::Unchecked);
    EXPECT_EQ(y_columns->item(1)->checkState(), Qt::Checked);
    EXPECT_EQ(y_columns->item(2)->checkState(), Qt::Checked);
    EXPECT_EQ(axes.series().size(), 1u);
    EXPECT_EQ(undo.undo_count(), 0u);

    apply->click();
    ASSERT_EQ(axes.series().size(), 3u);
    EXPECT_EQ(axes.series()[1]->label(), "alpha");
    EXPECT_EQ(axes.series()[2]->label(), "beta");
    const auto* alpha = dynamic_cast<const spectra::LineSeries*>(axes.series()[1].get());
    const auto* beta  = dynamic_cast<const spectra::LineSeries*>(axes.series()[2].get());
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(std::vector<float>(alpha->x_data().begin(), alpha->x_data().end()),
              (std::vector<float>{10.0f, 20.0f, 30.0f}));
    EXPECT_EQ(std::vector<float>(alpha->y_data().begin(), alpha->y_data().end()),
              (std::vector<float>{1.0f, 3.0f, 5.0f}));
    EXPECT_EQ(std::vector<float>(beta->y_data().begin(), beta->y_data().end()),
              (std::vector<float>{2.0f, 4.0f, 6.0f}));
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);
    EXPECT_EQ(data_changes, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(axes.series().size(), 1u);
    ASSERT_TRUE(undo.redo());
    ASSERT_EQ(axes.series().size(), 3u);
    EXPECT_EQ(axes.series()[1]->label(), "alpha");
    EXPECT_EQ(axes.series()[2]->label(), "beta");
    EXPECT_EQ(redraw.count, 3);
    EXPECT_EQ(data_changes, 3);

    std::filesystem::remove(import_path);
}

TEST(QtPanels, DataEditorTwoColumnImportCanCreateFirstSeriesOnEmptyAxes)
{
    const auto import_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_empty_axes_import.csv";
    {
        std::ofstream input(import_path, std::ios::binary | std::ios::trunc);
        input << "time,value\n1,10\n2,20\n";
    }

    spectra::FigureRegistry registry;
    auto                    figure    = std::make_unique<spectra::Figure>();
    auto&                   axes      = figure->subplot(1, 1, 1);
    const auto              figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager    undo;
    CountingRedrawRequest   redraw;
    PathDialogService       dialogs;
    dialogs.paths["Import Series Data"] = import_path.string();
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw, nullptr, &dialogs);
    editor.set_active_figure(figure_id);

    editor.findChild<QPushButton*>("de_import_csv")->click();
    auto* mapping = editor.findChild<QGroupBox*>("de_import_mapping");
    auto* apply   = editor.findChild<QPushButton*>("de_apply_import_columns");
    ASSERT_NE(mapping, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(mapping->isHidden());
    apply->click();

    ASSERT_EQ(axes.series().size(), 1u);
    EXPECT_EQ(axes.series().front()->label(), "value");
    const auto* line = dynamic_cast<const spectra::LineSeries*>(axes.series().front().get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(std::vector<float>(line->x_data().begin(), line->x_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(line->y_data().begin(), line->y_data().end()),
              (std::vector<float>{10.0f, 20.0f}));
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(axes.series().empty());

    std::filesystem::remove(import_path);
}

TEST(QtPanels, DataEditorImportsEditsAndExportsThreeDimensionalSeries)
{
    const auto import_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_3d_import.csv";
    const auto export_path =
        std::filesystem::temp_directory_path() / "spectra_data_editor_3d_export.csv";
    {
        std::ofstream input(import_path, std::ios::binary | std::ios::trunc);
        input << "time,alpha,beta,depth\n1,10,20,100\n2,11,21,200\n";
    }

    spectra::FigureRegistry registry;
    auto                    figure    = std::make_unique<spectra::Figure>();
    auto&                   axes      = figure->subplot3d(1, 1, 1);
    const auto              figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager    undo;
    CountingRedrawRequest   redraw;
    PathDialogService       dialogs;
    dialogs.paths["Import Series Data"] = import_path.string();
    dialogs.paths["Export Series Data"] = export_path.string();
    spectra::adapters::qt::QtDataEditorWidget editor(&registry, &undo, &redraw, nullptr, &dialogs);
    editor.set_active_figure(figure_id);

    auto* import_csv = editor.findChild<QPushButton*>("de_import_csv");
    auto* export_csv = editor.findChild<QPushButton*>("de_export_csv");
    auto* mapping    = editor.findChild<QGroupBox*>("de_import_mapping");
    auto* z_column   = editor.findChild<QComboBox*>("de_import_z_column");
    auto* y_columns  = editor.findChild<QListWidget*>("de_import_y_columns");
    auto* apply      = editor.findChild<QPushButton*>("de_apply_import_columns");
    auto* table      = editor.findChild<QTableWidget*>("de_data_table");
    ASSERT_NE(import_csv, nullptr);
    ASSERT_NE(export_csv, nullptr);
    ASSERT_NE(mapping, nullptr);
    ASSERT_NE(z_column, nullptr);
    ASSERT_NE(y_columns, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(table, nullptr);

    import_csv->click();
    EXPECT_FALSE(mapping->isHidden());
    EXPECT_FALSE(z_column->isHidden());
    z_column->setCurrentIndex(z_column->findText("depth"));
    ASSERT_EQ(y_columns->count(), 4);
    y_columns->item(2)->setCheckState(Qt::Checked);
    EXPECT_EQ(y_columns->item(0)->checkState(), Qt::Unchecked);
    EXPECT_EQ(y_columns->item(1)->checkState(), Qt::Checked);
    EXPECT_EQ(y_columns->item(2)->checkState(), Qt::Checked);
    EXPECT_EQ(y_columns->item(3)->checkState(), Qt::Unchecked);
    apply->click();

    ASSERT_EQ(axes.series().size(), 2u);
    const auto* alpha = dynamic_cast<const spectra::LineSeries3D*>(axes.series()[0].get());
    const auto* beta  = dynamic_cast<const spectra::LineSeries3D*>(axes.series()[1].get());
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(alpha->label(), "alpha");
    EXPECT_EQ(beta->label(), "beta");
    EXPECT_EQ(std::vector<float>(alpha->x_data().begin(), alpha->x_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(alpha->y_data().begin(), alpha->y_data().end()),
              (std::vector<float>{10.0f, 11.0f}));
    EXPECT_EQ(std::vector<float>(alpha->z_data().begin(), alpha->z_data().end()),
              (std::vector<float>{100.0f, 200.0f}));
    EXPECT_EQ(std::vector<float>(beta->y_data().begin(), beta->y_data().end()),
              (std::vector<float>{20.0f, 21.0f}));
    ASSERT_EQ(table->columnCount(), 3);
    ASSERT_NE(table->item(0, 2), nullptr);

    table->item(0, 2)->setText("150");
    alpha = dynamic_cast<const spectra::LineSeries3D*>(axes.series()[0].get());
    ASSERT_NE(alpha, nullptr);
    EXPECT_FLOAT_EQ(alpha->z_data()[0], 150.0f);
    export_csv->click();
    std::ifstream     exported(export_path, std::ios::binary);
    const std::string csv((std::istreambuf_iterator<char>(exported)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(csv, "x,y,z\n1,10,150\n2,11,200\n");

    ASSERT_TRUE(undo.undo());
    alpha = dynamic_cast<const spectra::LineSeries3D*>(axes.series()[0].get());
    ASSERT_NE(alpha, nullptr);
    EXPECT_FLOAT_EQ(alpha->z_data()[0], 100.0f);
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(axes.series().empty());
    ASSERT_TRUE(undo.redo());
    ASSERT_EQ(axes.series().size(), 2u);
    EXPECT_NE(dynamic_cast<const spectra::LineSeries3D*>(axes.series()[0].get()), nullptr);
    EXPECT_EQ(redraw.count, 5);

    std::filesystem::remove(import_path);
    std::filesystem::remove(export_path);
}

TEST(QtPanels, TransformApplicationIsUndoableAndRequestsRedraw)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line = axes.plot(x, y);
    line.x_offset(1783350621.752999);
    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* combo = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* apply = transforms.findChild<QPushButton*>("apply_transform_btn");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(apply, nullptr);
    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    apply->click();

    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 6.0f);
    EXPECT_NEAR(line.x_offset(), 1783350621.752999, 1e-6);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 3.0f);
    EXPECT_NEAR(line.x_offset(), 1783350621.752999, 1e-6);
    EXPECT_EQ(redraw.count, 2);

    ASSERT_TRUE(undo.redo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 4.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 6.0f);
    EXPECT_NEAR(line.x_offset(), 1783350621.752999, 1e-6);
    EXPECT_EQ(redraw.count, 3);
}

TEST(QtPanels, NamedTransformCommandAppliesToAllVisibleSeriesAndIsUndoable)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> first_y{2.0f, -3.0f};
    const std::array<float, 2> second_y{-4.0f, 5.0f};
    auto&                      first     = axes.plot(x, first_y).label("First");
    auto&                      second    = axes.plot(x, second_y).label("Second");
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* target = transforms.findChild<QComboBox*>("transform_target");
    ASSERT_NE(target, nullptr);
    target->setCurrentIndex(target->findData("0:1"));

    ASSERT_TRUE(transforms.apply_named_transform("square"));
    EXPECT_EQ(target->currentData().toString(), "all");
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{4.0f, 9.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{16.0f, 25.0f}));
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{2.0f, -3.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{-4.0f, 5.0f}));
    EXPECT_EQ(redraw.count, 2);
}

TEST(QtPanels, TransformTargetAndPreviewLimitMutationToSelectedSeries)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> first_y{1.0f, 2.0f};
    const std::array<float, 2> second_y{3.0f, 4.0f};
    auto&                      first     = axes.plot(x, first_y).label("First");
    auto&                      second    = axes.plot(x, second_y).label("Second");
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    int                                      data_changes = 0;
    QObject::connect(&transforms,
                     &spectra::adapters::qt::QtTransformWidget::data_changed,
                     [&data_changes]() { ++data_changes; });
    transforms.set_active_figure(figure_id);

    auto* target  = transforms.findChild<QComboBox*>("transform_target");
    auto* combo   = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale   = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* preview = transforms.findChild<QLabel*>("transform_preview");
    auto* apply   = transforms.findChild<QPushButton*>("apply_transform_btn");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_EQ(target->count(), 4);

    target->setCurrentIndex(target->findData("0:1"));
    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    EXPECT_TRUE(preview->text().contains("1 series"));
    EXPECT_TRUE(preview->text().contains("[6, 8]"));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));

    apply->click();
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{6.0f, 8.0f}));
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(data_changes, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));
    EXPECT_EQ(data_changes, 2);
}

TEST(QtPanels, TransformAxesTargetMutatesOnlySeriesOnSelectedAxes)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      left   = figure->subplot(1, 2, 1);
    auto&                      right  = figure->subplot(1, 2, 2);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> first_y{1.0f, 2.0f};
    const std::array<float, 2> second_y{3.0f, 4.0f};
    const std::array<float, 2> other_y{5.0f, 6.0f};
    auto&                      first     = left.plot(x, first_y).label("First");
    auto&                      second    = left.plot(x, second_y).label("Second");
    auto&                      other     = right.plot(x, other_y).label("Other axes");
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo);
    transforms.set_active_figure(figure_id);
    auto* target = transforms.findChild<QComboBox*>("transform_target");
    auto* combo  = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale  = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* apply  = transforms.findChild<QPushButton*>("apply_transform_btn");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(apply, nullptr);

    target->setCurrentIndex(target->findData("axes:0"));
    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    apply->click();
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{2.0f, 4.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{6.0f, 8.0f}));
    EXPECT_EQ(std::vector<float>(other.y_data().begin(), other.y_data().end()),
              (std::vector<float>{5.0f, 6.0f}));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));
}

TEST(QtPanels, TransformArbitrarySeriesSelectionIsScopedUndoableAndPersistent)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      left   = figure->subplot(1, 2, 1);
    auto&                      right  = figure->subplot(1, 2, 2);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> first_y{1.0f, 2.0f};
    const std::array<float, 2> second_y{3.0f, 4.0f};
    const std::array<float, 2> third_y{5.0f, 6.0f};
    auto&                      first     = left.plot(x, first_y).label("First");
    auto&                      second    = left.plot(x, second_y).label("Second");
    auto&                      third     = right.plot(x, third_y).label("Third");
    const auto                 figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager       undo;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo);
    transforms.set_active_figure(figure_id);

    auto* targets = transforms.findChild<QListWidget*>("transform_target_list");
    auto* combo   = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale   = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* preview = transforms.findChild<QLabel*>("transform_preview");
    auto* apply   = transforms.findChild<QPushButton*>("apply_transform_btn");
    auto* add     = transforms.findChild<QPushButton*>("add_transform_pipeline_btn");
    ASSERT_NE(targets, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_EQ(targets->count(), 3);

    targets->item(0)->setCheckState(Qt::Checked);
    targets->item(2)->setCheckState(Qt::Checked);
    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    EXPECT_TRUE(preview->text().contains("2 series"));
    add->click();
    const auto saved = transforms.capture_pipeline_state(figure_id, 4);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->target, "multi:0:0,1:0");

    apply->click();
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{2.0f, 4.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));
    EXPECT_EQ(std::vector<float>(third.y_data().begin(), third.y_data().end()),
              (std::vector<float>{10.0f, 12.0f}));
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(third.y_data().begin(), third.y_data().end()),
              (std::vector<float>{5.0f, 6.0f}));

    spectra::adapters::qt::QtTransformWidget restored(&registry);
    restored.restore_pipeline_state(figure_id, *saved);
    restored.set_active_figure(figure_id);
    auto* restored_targets = restored.findChild<QListWidget*>("transform_target_list");
    ASSERT_NE(restored_targets, nullptr);
    ASSERT_EQ(restored_targets->count(), 3);
    EXPECT_EQ(restored_targets->item(0)->checkState(), Qt::Checked);
    EXPECT_EQ(restored_targets->item(1)->checkState(), Qt::Unchecked);
    EXPECT_EQ(restored_targets->item(2)->checkState(), Qt::Checked);
    const auto round_trip = restored.capture_pipeline_state(figure_id, 2);
    ASSERT_TRUE(round_trip.has_value());
    EXPECT_EQ(round_trip->target, saved->target);
}

TEST(QtPanels, TransformUnavailableCustomStepIsPreservedAndResolvesWhenProviderReturns)
{
    constexpr auto transform_name     = "QtUnavailablePluginShift";
    auto&          transform_registry = spectra::TransformRegistry::instance();
    transform_registry.unregister_transform(transform_name);

    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{1.0f, 2.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));
    spectra::UndoManager       undo;

    spectra::WorkspaceData::TransformState state;
    state.figure_index = 0;
    state.target       = "all";
    spectra::WorkspaceData::TransformState::Step missing;
    missing.type    = static_cast<int>(spectra::TransformType::Custom);
    missing.name    = transform_name;
    missing.source  = "Unavailable Plugin";
    missing.enabled = true;
    state.steps.push_back(missing);
    spectra::WorkspaceData::TransformState::Step scale;
    scale.type           = static_cast<int>(spectra::TransformType::Scale);
    scale.name           = "Scale";
    scale.params_version = 1;
    scale.scale_factor   = 2.0f;
    scale.enabled        = true;
    state.steps.push_back(scale);

    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo);
    transforms.restore_pipeline_state(figure_id, state);
    transforms.set_active_figure(figure_id);
    auto* pipeline = transforms.findChild<QListWidget*>("pipeline_list");
    auto* apply    = transforms.findChild<QPushButton*>("apply_transform_pipeline_btn");
    ASSERT_NE(pipeline, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_EQ(pipeline->count(), 2);
    EXPECT_TRUE(pipeline->item(0)->text().contains("[Unavailable:"));
    EXPECT_FALSE(pipeline->item(0)->flags().testFlag(Qt::ItemIsEnabled));
    EXPECT_EQ(pipeline->item(0)->checkState(), Qt::Checked);

    const auto preserved = transforms.capture_pipeline_state(figure_id, 0);
    ASSERT_TRUE(preserved.has_value());
    ASSERT_EQ(preserved->steps.size(), 2u);
    EXPECT_EQ(preserved->steps[0].name, transform_name);
    EXPECT_EQ(preserved->steps[0].source, "Unavailable Plugin");
    EXPECT_TRUE(preserved->steps[0].enabled);

    apply->click();
    EXPECT_EQ(std::vector<float>(line.y_data().begin(), line.y_data().end()),
              (std::vector<float>{2.0f, 4.0f}));
    ASSERT_TRUE(undo.undo());

    transform_registry.register_transform(
        transform_name,
        [](float value) { return value + 10.0f; },
        "Adds ten");
    ASSERT_TRUE(transform_registry.set_transform_source(transform_name, "Unavailable Plugin"));
    transforms.refresh_transform_list();
    ASSERT_EQ(pipeline->count(), 2);
    EXPECT_FALSE(pipeline->item(0)->text().contains("[Unavailable:"));
    EXPECT_TRUE(pipeline->item(0)->flags().testFlag(Qt::ItemIsEnabled));
    apply->click();
    EXPECT_EQ(std::vector<float>(line.y_data().begin(), line.y_data().end()),
              (std::vector<float>{22.0f, 24.0f}));

    EXPECT_TRUE(transform_registry.unregister_transform(transform_name));
}

TEST(QtPanels, TransformCustomFormulaValidatesPreviewsAndUsesScopedUndo)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> first_y{1.0f, 2.0f};
    const std::array<float, 2> second_y{3.0f, 4.0f};
    auto&                      first     = axes.plot(x, first_y).label("First");
    auto&                      second    = axes.plot(x, second_y).label("Second");
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* target  = transforms.findChild<QComboBox*>("transform_target");
    auto* formula = transforms.findChild<QLineEdit*>("transform_formula");
    auto* status  = transforms.findChild<QLabel*>("transform_formula_status");
    auto* apply   = transforms.findChild<QPushButton*>("apply_custom_transform_btn");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(formula, nullptr);
    ASSERT_NE(status, nullptr);
    ASSERT_NE(apply, nullptr);
    target->setCurrentIndex(target->findData("0:1"));

    formula->setText("y + s0_y");
    EXPECT_TRUE(apply->isEnabled());
    EXPECT_TRUE(status->text().contains("[4, 6]"));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));
    apply->click();
    EXPECT_EQ(std::vector<float>(first.y_data().begin(), first.y_data().end()),
              (std::vector<float>{1.0f, 2.0f}));
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{4.0f, 6.0f}));
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(std::vector<float>(second.y_data().begin(), second.y_data().end()),
              (std::vector<float>{3.0f, 4.0f}));
    EXPECT_EQ(redraw.count, 2);

    formula->setText("y *");
    EXPECT_FALSE(apply->isEnabled());
    const size_t history = undo.undo_count();
    apply->click();
    EXPECT_EQ(undo.undo_count(), history);
}

TEST(QtPanels, TransformPipelineIsOneUndoableTransaction)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    auto&                      line      = axes.plot(x, y);
    const auto                 figure_id = registry.register_figure(std::move(figure));

    spectra::UndoManager                     undo;
    CountingRedrawRequest                    redraw;
    spectra::adapters::qt::QtTransformWidget transforms(&registry, &undo, &redraw);
    transforms.set_active_figure(figure_id);

    auto* combo  = transforms.findChild<QComboBox*>("transform_combo");
    auto* scale  = transforms.findChild<QDoubleSpinBox*>("transform_scale");
    auto* offset = transforms.findChild<QDoubleSpinBox*>("transform_offset");
    auto* add    = transforms.findChild<QPushButton*>("add_transform_pipeline_btn");
    auto* apply  = transforms.findChild<QPushButton*>("apply_transform_pipeline_btn");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(apply, nullptr);

    combo->setCurrentText("Scale");
    scale->setValue(2.0);
    add->click();
    combo->setCurrentText("Offset");
    offset->setValue(1.0);
    add->click();
    apply->click();

    EXPECT_FLOAT_EQ(line.y_data()[0], 5.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 7.0f);
    EXPECT_EQ(undo.undo_count(), 1u);
    EXPECT_EQ(redraw.count, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_FLOAT_EQ(line.y_data()[0], 2.0f);
    EXPECT_FLOAT_EQ(line.y_data()[1], 3.0f);
    EXPECT_EQ(redraw.count, 2);
}

TEST(QtPanels, TransformPipelineWorkspaceStateRestoresTargetAndAllParameters)
{
    spectra::FigureRegistry    registry;
    auto                       figure = std::make_unique<spectra::Figure>();
    auto&                      axes   = figure->subplot(1, 1, 1);
    const std::array<float, 2> x{0.0f, 1.0f};
    const std::array<float, 2> y{2.0f, 3.0f};
    axes.plot(x, y).label("Persisted");
    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtTransformWidget source(&registry);
    source.set_active_figure(figure_id);
    auto* target    = source.findChild<QComboBox*>("transform_target");
    auto* combo     = source.findChild<QComboBox*>("transform_combo");
    auto* scale     = source.findChild<QDoubleSpinBox*>("transform_scale");
    auto* clamp_min = source.findChild<QDoubleSpinBox*>("transform_clamp_min");
    auto* clamp_max = source.findChild<QDoubleSpinBox*>("transform_clamp_max");
    auto* fft_db    = source.findChild<QCheckBox*>("transform_fft_db");
    auto* fft_rate  = source.findChild<QDoubleSpinBox*>("transform_fft_sample_rate");
    auto* add       = source.findChild<QPushButton*>("add_transform_pipeline_btn");
    auto* list      = source.findChild<QListWidget*>("pipeline_list");
    auto* move_up   = source.findChild<QPushButton*>("move_transform_step_up_btn");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(clamp_min, nullptr);
    ASSERT_NE(clamp_max, nullptr);
    ASSERT_NE(fft_db, nullptr);
    ASSERT_NE(fft_rate, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(move_up, nullptr);

    target->setCurrentIndex(target->findData("0:0"));
    combo->setCurrentText("Scale");
    scale->setValue(2.5);
    add->click();
    combo->setCurrentText("Clamp");
    clamp_min->setValue(-4.0);
    clamp_max->setValue(8.0);
    add->click();
    combo->setCurrentText("FFT");
    fft_db->setChecked(true);
    fft_rate->setValue(200.0);
    add->click();
    ASSERT_EQ(list->count(), 3);
    list->item(1)->setCheckState(Qt::Unchecked);
    list->setCurrentRow(2);
    move_up->click();

    const auto saved = source.capture_pipeline_state(figure_id, 7);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->figure_index, 7u);
    EXPECT_FALSE(saved->all_visible);
    EXPECT_EQ(saved->axes_index, 0u);
    EXPECT_EQ(saved->series_index, 0u);
    ASSERT_EQ(saved->steps.size(), 3u);
    EXPECT_FLOAT_EQ(saved->steps[0].scale_factor, 2.5f);
    EXPECT_TRUE(saved->steps[1].fft_db);
    EXPECT_FLOAT_EQ(saved->steps[1].fft_sample_rate, 200.0f);
    EXPECT_FLOAT_EQ(saved->steps[2].clamp_min, -4.0f);
    EXPECT_FLOAT_EQ(saved->steps[2].clamp_max, 8.0f);
    EXPECT_FALSE(saved->steps[2].enabled);

    spectra::adapters::qt::QtTransformWidget restored(&registry);
    restored.restore_pipeline_state(figure_id, *saved);
    restored.set_active_figure(figure_id);
    const auto round_trip = restored.capture_pipeline_state(figure_id, 1);
    ASSERT_TRUE(round_trip.has_value());
    EXPECT_EQ(round_trip->figure_index, 1u);
    EXPECT_FALSE(round_trip->all_visible);
    ASSERT_EQ(round_trip->steps.size(), 3u);
    EXPECT_FLOAT_EQ(round_trip->steps[0].scale_factor, 2.5f);
    EXPECT_TRUE(round_trip->steps[1].fft_db);
    EXPECT_FLOAT_EQ(round_trip->steps[1].fft_sample_rate, 200.0f);
    EXPECT_FLOAT_EQ(round_trip->steps[2].clamp_min, -4.0f);
    EXPECT_FLOAT_EQ(round_trip->steps[2].clamp_max, 8.0f);
    EXPECT_FALSE(round_trip->steps[2].enabled);
    EXPECT_EQ(restored.findChild<QComboBox*>("transform_target")->currentData().toString(), "0:0");
    EXPECT_EQ(restored.findChild<QListWidget*>("pipeline_list")->count(), 3);
    EXPECT_EQ(restored.findChild<QListWidget*>("pipeline_list")->item(2)->checkState(),
              Qt::Unchecked);
}

TEST(QtPanels, InspectorAxesStatistics)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);

    const std::array<float, 3> x1{1.0f, 2.0f, 3.0f};
    const std::array<float, 3> y1{4.0f, 5.0f, 6.0f};
    auto&                      s1 = axes.plot(x1, y1, "b-");
    s1.label("visible");

    const std::array<float, 3> x2{10.0f, 20.0f, 30.0f};
    const std::array<float, 3> y2{40.0f, 50.0f, 60.0f};
    auto&                      s2 = axes.plot(x2, y2, "r--");
    s2.label("hidden");
    s2.visible(false);

    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* visible = inspector.findChild<QLabel*>("axes_0_stats_visible");
    auto* points  = inspector.findChild<QLabel*>("axes_0_stats_total_points");
    auto* x_min   = inspector.findChild<QLabel*>("axes_0_stats_x_min");
    auto* x_max   = inspector.findChild<QLabel*>("axes_0_stats_x_max");
    auto* x_mean  = inspector.findChild<QLabel*>("axes_0_stats_x_mean");
    auto* y_min   = inspector.findChild<QLabel*>("axes_0_stats_y_min");
    auto* y_max   = inspector.findChild<QLabel*>("axes_0_stats_y_max");
    auto* y_mean  = inspector.findChild<QLabel*>("axes_0_stats_y_mean");

    ASSERT_NE(visible, nullptr);
    ASSERT_NE(points, nullptr);
    ASSERT_NE(x_min, nullptr);
    ASSERT_NE(x_max, nullptr);
    ASSERT_NE(x_mean, nullptr);
    ASSERT_NE(y_min, nullptr);
    ASSERT_NE(y_max, nullptr);
    ASSERT_NE(y_mean, nullptr);

    EXPECT_EQ(visible->text(), "1 / 2");
    EXPECT_EQ(points->text(), "3");
    EXPECT_EQ(x_min->text(), "1");
    EXPECT_EQ(x_max->text(), "3");
    EXPECT_EQ(x_mean->text(), "2");
    EXPECT_EQ(y_min->text(), "4");
    EXPECT_EQ(y_max->text(), "6");
    EXPECT_EQ(y_mean->text(), "5");

    // Toggle the second series visible and confirm the aggregate updates.
    s2.visible(true);
    inspector.sync_from_model();

    EXPECT_EQ(visible->text(), "2 / 2");
    EXPECT_EQ(points->text(), "6");
    EXPECT_EQ(x_min->text(), "1");
    EXPECT_EQ(x_max->text(), "30");
    EXPECT_EQ(x_mean->text(), "11");
    EXPECT_EQ(y_min->text(), "4");
    EXPECT_EQ(y_max->text(), "60");
    EXPECT_EQ(y_mean->text(), "27.5");
}

TEST(QtPanels, InspectorReferenceLines)
{
    spectra::FigureRegistry registry;
    auto                    figure    = std::make_unique<spectra::Figure>();
    auto&                   axes      = figure->subplot(1, 1, 1);
    const auto              figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* value     = inspector.findChild<QDoubleSpinBox*>("axes_0_ref_value");
    auto* fmt       = inspector.findChild<QLineEdit*>("axes_0_ref_fmt");
    auto* add_h     = inspector.findChild<QPushButton*>("axes_0_add_hline");
    auto* add_v     = inspector.findChild<QPushButton*>("axes_0_add_vline");
    auto* ref_empty = inspector.findChild<QLabel*>("axes_0_ref_empty");

    ASSERT_NE(value, nullptr);
    ASSERT_NE(fmt, nullptr);
    ASSERT_NE(add_h, nullptr);
    ASSERT_NE(add_v, nullptr);
    ASSERT_NE(ref_empty, nullptr);

    value->setValue(1.5);
    fmt->setText("r--");
    add_h->click();
    ASSERT_EQ(axes.series().size(), 1u);
    auto* hline = dynamic_cast<spectra::LineSeries*>(axes.series().back().get());
    ASSERT_NE(hline, nullptr);
    EXPECT_TRUE(hline->is_reference_line());
    EXPECT_FLOAT_EQ(hline->y_data()[0], 1.5f);
    EXPECT_FLOAT_EQ(hline->y_data()[1], 1.5f);

    value->setValue(-2.0);
    fmt->setText("g:");
    add_v->click();
    ASSERT_EQ(axes.series().size(), 2u);
    auto* vline = dynamic_cast<spectra::LineSeries*>(axes.series().back().get());
    ASSERT_NE(vline, nullptr);
    EXPECT_TRUE(vline->is_reference_line());
    EXPECT_FLOAT_EQ(vline->x_data()[0], -2.0f);
    EXPECT_FLOAT_EQ(vline->x_data()[1], -2.0f);

    auto* del_first = inspector.findChild<QPushButton*>("axes_0_ref_0_delete");
    ASSERT_NE(del_first, nullptr);
    del_first->click();
    EXPECT_EQ(axes.series().size(), 1u);

    auto* del_second = inspector.findChild<QPushButton*>("axes_0_ref_0_delete");
    ASSERT_NE(del_second, nullptr);
    del_second->click();
    EXPECT_EQ(axes.series().size(), 0u);

    EXPECT_NE(inspector.findChild<QLabel*>("axes_0_ref_empty"), nullptr);
}

TEST(QtPanels, InspectorSeriesStatisticsAndSparkline)
{
    spectra::FigureRegistry registry;
    auto                    figure = std::make_unique<spectra::Figure>();
    auto&                   axes   = figure->subplot(1, 1, 1);

    const std::array<float, 3> x{1.0f, 2.0f, 3.0f};
    const std::array<float, 3> y{4.0f, 5.0f, 6.0f};
    axes.plot(x, y).label("demo");

    const auto figure_id = registry.register_figure(std::move(figure));

    spectra::adapters::qt::QtInspectorWidget inspector(&registry);
    inspector.set_active_figure(figure_id);

    auto* list = inspector.findChild<QListWidget*>("series_list");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    list->setCurrentRow(0);

    auto* spark = inspector.findChild<QWidget*>("series_sparkline");
    ASSERT_NE(spark, nullptr);
    EXPECT_GT(spark->width(), 0);
    EXPECT_GT(spark->height(), 0);

    auto* x_count = inspector.findChild<QLabel*>("series_stats_x_count");
    auto* x_min   = inspector.findChild<QLabel*>("series_stats_x_min");
    auto* x_max   = inspector.findChild<QLabel*>("series_stats_x_max");
    auto* x_mean  = inspector.findChild<QLabel*>("series_stats_x_mean");
    auto* x_sum   = inspector.findChild<QLabel*>("series_stats_x_sum");
    auto* y_count = inspector.findChild<QLabel*>("series_stats_y_count");
    auto* y_min   = inspector.findChild<QLabel*>("series_stats_y_min");
    auto* y_max   = inspector.findChild<QLabel*>("series_stats_y_max");
    auto* y_mean  = inspector.findChild<QLabel*>("series_stats_y_mean");
    auto* y_sum   = inspector.findChild<QLabel*>("series_stats_y_sum");

    ASSERT_NE(x_count, nullptr);
    ASSERT_NE(x_min, nullptr);
    ASSERT_NE(x_max, nullptr);
    ASSERT_NE(x_mean, nullptr);
    ASSERT_NE(x_sum, nullptr);
    ASSERT_NE(y_count, nullptr);
    ASSERT_NE(y_min, nullptr);
    ASSERT_NE(y_max, nullptr);
    ASSERT_NE(y_mean, nullptr);
    ASSERT_NE(y_sum, nullptr);

    EXPECT_EQ(x_count->text(), "3");
    EXPECT_EQ(x_min->text(), "1");
    EXPECT_EQ(x_max->text(), "3");
    EXPECT_EQ(x_mean->text(), "2");
    EXPECT_EQ(x_sum->text(), "6");
    EXPECT_EQ(y_count->text(), "3");
    EXPECT_EQ(y_min->text(), "4");
    EXPECT_EQ(y_max->text(), "6");
    EXPECT_EQ(y_mean->text(), "5");
    EXPECT_EQ(y_sum->text(), "15");
}
