// test_qt_dialogs.cpp — Qt integration tests for frontend service injection.
//
// Verifies:
//   - DialogService injection and callback execution
//   - ClipboardService text copy/paste
//   - RedrawRequest callback invocation
//   - WindowService create/close/focus/count
//   - Null service implementations (headless mode)
//   - QtRedrawRequest figure-specific callback

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>

#include "app/frontend_services.hpp"
#include "adapters/qt/qt_frontend_services.hpp"
#include "ui/native_dialog_policy.hpp"

#include <spectra/fwd.hpp>

#include <memory>
#include <atomic>
#include <cstdlib>

namespace
{

struct QtDialogEnv
{
    QtDialogEnv() = default;
};

QtDialogEnv& env()
{
    static QtDialogEnv e;
    return e;
}

}   // namespace

// ── NullDialogService ────────────────────────────────────────────────────────

TEST(QtDialogs, NullDialogServiceReturnsNullopt)
{
    spectra::NullDialogService svc;
    auto result = svc.file_dialog(spectra::DialogService::FileType::Open, "Test", "/tmp", {});
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(svc.number_input("Number", "Value", 0.0, -1.0, 1.0, 2));
}

TEST(QtDialogs, NullDialogServiceMessageBoxReturnsTrue)
{
    spectra::NullDialogService svc;
    EXPECT_TRUE(svc.message_box("Title", "Message", false));
    EXPECT_TRUE(svc.message_box("Title", "Message", true));
}

TEST(QtDialogs, NullDialogServiceColorPickerReturnsNullopt)
{
    spectra::NullDialogService svc;
    spectra::Color             c{1.0f, 0.0f, 0.0f, 1.0f};
    auto                       result = svc.color_picker("Pick", c);
    EXPECT_FALSE(result.has_value());
}

// ── NullClipboardService ─────────────────────────────────────────────────────

TEST(QtDialogs, NullClipboardServiceIsNoOp)
{
    spectra::NullClipboardService svc;
    svc.copy_text("hello");
    svc.copy_image({0x89, 0x50, 0x4E, 0x47});
    EXPECT_EQ(svc.paste_text(), "");
}

// ── NullRedrawRequest ────────────────────────────────────────────────────────

TEST(QtDialogs, NullRedrawRequestIsNoOp)
{
    spectra::NullRedrawRequest req;
    req.request_redraw();
    req.request_redraw(spectra::FigureId{1});
}

// ── NullWindowService ────────────────────────────────────────────────────────

TEST(QtDialogs, NullWindowServiceReturnsInvalid)
{
    spectra::NullWindowService svc;
    EXPECT_EQ(svc.create_window("Test", 800, 600), spectra::INVALID_FIGURE_ID);
    svc.close_window(spectra::FigureId{1});   // should not crash
    svc.focus_window(spectra::FigureId{1});   // should not crash
    EXPECT_EQ(svc.window_count(), 0u);
}

// ── QtClipboardService ───────────────────────────────────────────────────────

TEST(QtDialogs, ClipboardCopyPasteText)
{
    auto&                                     e = env();
    spectra::adapters::qt::QtClipboardService svc;
    svc.copy_text("test_clipboard_value");
    EXPECT_EQ(svc.paste_text(), "test_clipboard_value");
}

TEST(QtDialogs, ClipboardCopyEmptyText)
{
    auto&                                     e = env();
    spectra::adapters::qt::QtClipboardService svc;
    svc.copy_text("");
    EXPECT_EQ(svc.paste_text(), "");
}

TEST(QtDialogs, ClipboardCopyImageDoesNotCrash)
{
    auto&                                     e = env();
    spectra::adapters::qt::QtClipboardService svc;
    // Minimal PNG header — just verify it doesn't crash
    std::vector<uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    svc.copy_image(png);
}

// ── QtRedrawRequest ──────────────────────────────────────────────────────────

TEST(QtDialogs, RedrawRequestCallback)
{
    std::atomic<int>                       call_count{0};
    spectra::adapters::qt::QtRedrawRequest req([&call_count]() { call_count++; });
    req.request_redraw();
    EXPECT_EQ(call_count.load(), 1);
    req.request_redraw();
    EXPECT_EQ(call_count.load(), 2);
}

TEST(QtDialogs, RedrawRequestFigureCallback)
{
    std::atomic<int>               global_count{0};
    std::atomic<spectra::FigureId> last_figure_id{spectra::INVALID_FIGURE_ID};

    spectra::adapters::qt::QtRedrawRequest req([&global_count]() { global_count++; });

    req.set_figure_callback([&last_figure_id](spectra::FigureId id) { last_figure_id = id; });

    req.request_redraw(spectra::FigureId{42});
    EXPECT_EQ(global_count.load(), 0);   // global callback not called for figure-specific
    EXPECT_EQ(last_figure_id.load(), spectra::FigureId{42});
}

TEST(QtDialogs, RedrawRequestGlobalAndFigure)
{
    std::atomic<int> global_count{0};
    std::atomic<int> figure_count{0};

    spectra::adapters::qt::QtRedrawRequest req([&global_count]() { global_count++; });
    req.set_figure_callback([&figure_count](spectra::FigureId) { figure_count++; });

    req.request_redraw();
    EXPECT_EQ(global_count.load(), 1);
    EXPECT_EQ(figure_count.load(), 0);

    req.request_redraw(spectra::FigureId{1});
    EXPECT_EQ(global_count.load(), 1);
    EXPECT_EQ(figure_count.load(), 1);

    req.request_redraw();
    EXPECT_EQ(global_count.load(), 2);
}

// ── QtWindowService ──────────────────────────────────────────────────────────

TEST(QtDialogs, WindowServiceCreateWindow)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtWindowService svc;

    std::string created_title;
    uint32_t    created_w = 0;
    uint32_t    created_h = 0;

    svc.set_create_window(
        [&](const std::string& title, uint32_t w, uint32_t h)
        {
            created_title = title;
            created_w     = w;
            created_h     = h;
            return spectra::FigureId{99};
        });

    auto id = svc.create_window("My Window", 1024, 768);
    EXPECT_EQ(id, spectra::FigureId{99});
    EXPECT_EQ(created_title, "My Window");
    EXPECT_EQ(created_w, 1024u);
    EXPECT_EQ(created_h, 768u);
}

TEST(QtDialogs, WindowServiceCloseWindow)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtWindowService svc;

    std::atomic<spectra::FigureId> closed_id{spectra::INVALID_FIGURE_ID};
    svc.set_close_window([&](spectra::FigureId id) { closed_id = id; });

    svc.close_window(spectra::FigureId{7});
    EXPECT_EQ(closed_id.load(), spectra::FigureId{7});
}

TEST(QtDialogs, WindowServiceFocusWindow)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtWindowService svc;

    std::atomic<spectra::FigureId> focused_id{spectra::INVALID_FIGURE_ID};
    svc.set_focus_window([&](spectra::FigureId id) { focused_id = id; });

    svc.focus_window(spectra::FigureId{3});
    EXPECT_EQ(focused_id.load(), spectra::FigureId{3});
}

TEST(QtDialogs, WindowServiceWindowCount)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtWindowService svc;

    std::atomic<size_t> count{0};
    svc.set_window_count([&]() { return count.load(); });

    EXPECT_EQ(svc.window_count(), 0u);
    count = 3;
    EXPECT_EQ(svc.window_count(), 3u);
}

TEST(QtDialogs, WindowServiceDefaultBehavior)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtWindowService svc;

    // Without setting callbacks, should not crash and return defaults
    EXPECT_EQ(svc.create_window("Test"), spectra::INVALID_FIGURE_ID);
    EXPECT_EQ(svc.window_count(), 0u);
    svc.close_window(spectra::FigureId{1});   // no crash
    svc.focus_window(spectra::FigureId{1});   // no crash
}

// ── QtDialogService (skipped in offscreen — modal dialogs block) ─────────────

TEST(QtDialogs, DialogServiceExists)
{
    auto&                                  e = env();
    spectra::adapters::qt::QtDialogService svc;
    // Just verify the service can be instantiated
    (void)svc;
    // message_box and file_dialog would block in offscreen mode,
    // so we skip actual invocation here. They are tested via
    // integration tests with a real display.
}

TEST(QtDialogs, AutomationFileDialogUsesExplicitPathAndOtherwiseCancels)
{
    spectra::adapters::qt::QtDialogService svc;
    const bool                             dialogs_were_enabled = spectra::native_dialogs_enabled();
    spectra::set_native_dialogs_enabled(false);
    unsetenv("SPECTRA_QT_DIALOG_PATH");

    EXPECT_FALSE(svc.file_dialog(spectra::DialogService::FileType::Save,
                                 "Export PNG",
                                 "spectra_export.png",
                                 {{"PNG Image", "*.png"}}));

    setenv("SPECTRA_QT_DIALOG_PATH", "/tmp/spectra-scripted.png", 1);
    EXPECT_EQ(svc.file_dialog(spectra::DialogService::FileType::Save,
                              "Export PNG",
                              "spectra_export.png",
                              {{"PNG Image", "*.png"}}),
              "/tmp/spectra-scripted.png");

    setenv("SPECTRA_QT_DIALOG_EXPORT_PNG", "/tmp/spectra-title.png", 1);
    EXPECT_EQ(svc.file_dialog(spectra::DialogService::FileType::Save,
                              "Export PNG",
                              "spectra_export.png",
                              {{"PNG Image", "*.png"}}),
              "/tmp/spectra-title.png");

    unsetenv("SPECTRA_QT_DIALOG_EXPORT_PNG");
    unsetenv("SPECTRA_QT_DIALOG_PATH");

    EXPECT_FALSE(svc.number_input("Add Horizontal Line", "Y value", 0.0, -10.0, 10.0, 3));
    setenv("SPECTRA_QT_NUMBER_ADD_HORIZONTAL_LINE", "2.75", 1);
    EXPECT_EQ(svc.number_input("Add Horizontal Line", "Y value", 0.0, -10.0, 10.0, 3), 2.75);
    unsetenv("SPECTRA_QT_NUMBER_ADD_HORIZONTAL_LINE");
    spectra::set_native_dialogs_enabled(dialogs_were_enabled);
}
