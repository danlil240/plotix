// test_qt_visual_regression.cpp — Visual regression tests for the Spectra Qt UI.
//
// Verifies:
//   - No duplicate in-client title bar is shown
//   - App header exists and has correct height
//   - Navigation rail exists and has correct width
//   - Document tab bar exists and has correct height
//   - Status bar exists and has correct height
//   - Inspector drawer is hidden by default
//   - Default Qt menu bar is not visible
//   - Default Qt status bar is not visible
//   - Canvas frame exists and is visible
//   - Window has the legacy default client size (1280x720)
//   - Screenshot capture works (pixmap is not null)
//   - Compact mode triggers below 1100px width

#include <gtest/gtest.h>

#include <QApplication>
#include <QMenuBar>
#include <QPixmap>
#include <QImage>
#include <QWidget>

#include "adapters/qt/qt_main_window.hpp"
#include "adapters/qt/qt_action_bridge.hpp"

#include "ui/commands/command_registry.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <memory>

namespace {

struct QtVisualEnv
{
    std::unique_ptr<spectra::FigureRegistry> registry;
    std::unique_ptr<spectra::CommandRegistry> cmd_registry;
    std::unique_ptr<spectra::adapters::qt::QtActionBridge> action_bridge;

    QtVisualEnv()
    {
        registry = std::make_unique<spectra::FigureRegistry>();
        cmd_registry = std::make_unique<spectra::CommandRegistry>();
        action_bridge = std::make_unique<spectra::adapters::qt::QtActionBridge>(*cmd_registry);
        action_bridge->rebuild();
    }

};

QtVisualEnv& env()
{
    static QtVisualEnv e;
    return e;
}

// Helper: find a child widget by object name
static QWidget* find_widget(QObject* parent, const char* name)
{
    return parent->findChild<QWidget*>(name);
}

} // namespace

// ── Window default size ──────────────────────────────────────────────────────

TEST(QtVisualRegression, DefaultWindowSize)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    EXPECT_EQ(mw.width(), 1280);
    EXPECT_EQ(mw.height(), 720);
}

// ── Custom title bar exists ───────────────────────────────────────────────────

TEST(QtVisualRegression, CustomTitleBarIsNotDuplicatedInClientArea)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* title_bar = find_widget(&mw, "spectra_title_bar");
    ASSERT_NE(title_bar, nullptr);
    EXPECT_TRUE(title_bar->isHidden());
}

// ── App header exists ─────────────────────────────────────────────────────────

TEST(QtVisualRegression, AppHeaderExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* header = find_widget(&mw, "spectra_app_header");
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->height(), 48);
}

// ── Navigation rail exists ────────────────────────────────────────────────────

TEST(QtVisualRegression, NavRailExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* nav_rail = find_widget(&mw, "spectra_nav_rail");
    ASSERT_NE(nav_rail, nullptr);
    EXPECT_EQ(nav_rail->width(), 72);
}

// ── Document tab bar exists ───────────────────────────────────────────────────

TEST(QtVisualRegression, DocumentTabBarExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* tab_bar = find_widget(&mw, "spectra_doc_tab_bar");
    ASSERT_NE(tab_bar, nullptr);
    EXPECT_EQ(tab_bar->height(), 26);
}

// ── Status bar exists ─────────────────────────────────────────────────────────

TEST(QtVisualRegression, StatusBarExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* status_bar = find_widget(&mw, "spectra_status_bar");
    ASSERT_NE(status_bar, nullptr);
    EXPECT_EQ(status_bar->height(), 34);
}

// ── Inspector hidden by default ───────────────────────────────────────────────

TEST(QtVisualRegression, InspectorHiddenByDefault)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* inspector = find_widget(&mw, "spectra_inspector");
    ASSERT_NE(inspector, nullptr);
    EXPECT_TRUE(inspector->isHidden());
}

// ── Default Qt menu bar not visible ───────────────────────────────────────────

TEST(QtVisualRegression, DefaultMenuBarNotVisible)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* menubar = mw.menuBar();
    ASSERT_NE(menubar, nullptr);
    EXPECT_FALSE(menubar->isVisible());
}

// ── Canvas frame exists ───────────────────────────────────────────────────────

TEST(QtVisualRegression, CanvasFrameExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* canvas_frame = find_widget(&mw, "spectra_canvas_frame");
    ASSERT_NE(canvas_frame, nullptr);
    EXPECT_FALSE(canvas_frame->isHidden());
}

// ── Central container exists ──────────────────────────────────────────────────

TEST(QtVisualRegression, CentralContainerExists)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);

    auto* container = find_widget(&mw, "spectra_central_container");
    ASSERT_NE(container, nullptr);
    EXPECT_FALSE(container->isHidden());
}

// ── Screenshot capture works ──────────────────────────────────────────────────

TEST(QtVisualRegression, ScreenshotCapture)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    mw.show();

    // Process events to let the window render
    QApplication::processEvents();

    // Grab the window as a pixmap
    QPixmap pixmap = mw.grab();
    EXPECT_FALSE(pixmap.isNull());
    EXPECT_EQ(pixmap.width(), 1280);
    EXPECT_EQ(pixmap.height(), 720);

    mw.hide();
}

TEST(QtVisualRegression, NoUnexpectedLightNativeSurfaces)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    mw.show();
    QApplication::processEvents();

    const QImage image = mw.grab().toImage().convertToFormat(QImage::Format_RGB32);
    ASSERT_FALSE(image.isNull());

    qsizetype bright_pixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb pixel = row[x];
            if (qRed(pixel) > 220 && qGreen(pixel) > 220 && qBlue(pixel) > 220)
                ++bright_pixels;
        }
    }

    const double bright_ratio = static_cast<double>(bright_pixels)
        / static_cast<double>(image.width() * image.height());
    EXPECT_LT(bright_ratio, 0.01)
        << "A large light region usually means an unthemed native dock obscured the canvas";
    mw.hide();
}

// ── Compact mode triggers below 1100px ────────────────────────────────────────

TEST(QtVisualRegression, CompactModeTriggersBelow1100)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    mw.show();
    QApplication::processEvents();

    // At default width, nav rail matches the legacy 72px region.
    auto* nav_rail = find_widget(&mw, "spectra_nav_rail");
    ASSERT_NE(nav_rail, nullptr);
    EXPECT_EQ(nav_rail->width(), 72);

    // Resize below 1100px to trigger compact mode
    mw.resize(1000, 700);
    QApplication::processEvents();

    // In compact mode, nav rail should be 48px
    EXPECT_EQ(nav_rail->width(), 48);

    mw.hide();
}

// ── Reset layout hides inspector ──────────────────────────────────────────────

TEST(QtVisualRegression, ResetLayoutHidesInspector)
{
    auto& e = env();
    spectra::adapters::qt::SpectraMainWindow mw(
        nullptr, e.registry.get(), e.action_bridge.get(), nullptr);
    mw.show();
    QApplication::processEvents();

    mw.reset_layout();
    QApplication::processEvents();

    auto* inspector = find_widget(&mw, "spectra_inspector");
    ASSERT_NE(inspector, nullptr);
    EXPECT_TRUE(inspector->isHidden());
    EXPECT_EQ(mw.size(), QSize(1280, 720));

    mw.hide();
}
