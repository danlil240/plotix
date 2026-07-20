#pragma once

// Qt frontend service implementations.
//
// Concrete implementations of DialogService, ClipboardService, RedrawRequest,
// and WindowService for the Qt 6 desktop frontend.  These are created and
// injected into ApplicationServices during QtApplicationController init so
// that command handlers and automation can interact with the frontend through
// framework-neutral interfaces.

#include "app/frontend_services.hpp"

#include <spectra/fwd.hpp>

#include <functional>
#include <vector>

class QMainWindow;
class QTabWidget;

namespace spectra
{
class FigureRegistry;
class QtRuntime;
}   // namespace spectra

namespace spectra::adapters::qt
{

class FigureCanvasWidget;
class SpectraMainWindow;

// ─── QtDialogService ──────────────────────────────────────────────────────────
// Uses QFileDialog, QMessageBox, and QColorDialog for native dialogs.
class QtDialogService final : public DialogService
{
   public:
    QtDialogService() = default;

    std::optional<std::string> file_dialog(
        FileType                       type,
        const std::string&             title,
        const std::string&             default_path,
        const std::vector<FileFilter>& filters) override;

    bool message_box(
        const std::string& title,
        const std::string& message,
        bool               cancel_button = false) override;

    std::optional<Color> color_picker(
        const std::string& title,
        const Color&       initial) override;
};

// ─── QtClipboardService ───────────────────────────────────────────────────────
// Uses QClipboard for text and image clipboard operations.
class QtClipboardService final : public ClipboardService
{
   public:
    QtClipboardService() = default;

    void copy_text(const std::string& text) override;
    void copy_image(const std::vector<uint8_t>& png_data) override;
    std::string paste_text() override;
};

// ─── QtRedrawRequest ──────────────────────────────────────────────────────────
// Requests redraws from the Qt render runtime by calling requestFrame()
// on the relevant SpectraVulkanWindow(s).
class QtRedrawRequest final : public RedrawRequest
{
   public:
    using RedrawCallback = std::function<void()>;
    using FigureRedrawCallback = std::function<void(FigureId)>;

    explicit QtRedrawRequest(RedrawCallback cb) : callback_(std::move(cb)) {}

    void request_redraw() override;
    void request_redraw(FigureId figure_id) override;

    void set_figure_callback(FigureRedrawCallback cb) { figure_callback_ = std::move(cb); }

   private:
    RedrawCallback        callback_;
    FigureRedrawCallback  figure_callback_;
};

// ─── QtWindowService ──────────────────────────────────────────────────────────
// Delegates window operations to the QtApplicationController / SpectraMainWindow.
class QtWindowService final : public WindowService
{
   public:
    using CreateWindowFn = std::function<FigureId(const std::string&, uint32_t, uint32_t)>;
    using CloseWindowFn   = std::function<void(FigureId)>;
    using FocusWindowFn   = std::function<void(FigureId)>;
    using WindowCountFn   = std::function<size_t()>;

    void set_create_window(CreateWindowFn fn) { create_fn_ = std::move(fn); }
    void set_close_window(CloseWindowFn fn)   { close_fn_ = std::move(fn); }
    void set_focus_window(FocusWindowFn fn)   { focus_fn_ = std::move(fn); }
    void set_window_count(WindowCountFn fn)   { count_fn_ = std::move(fn); }

    FigureId create_window(
        const std::string& title,
        uint32_t           width  = 800,
        uint32_t           height = 600) override;

    void close_window(FigureId figure_id) override;
    void focus_window(FigureId figure_id) override;
    size_t window_count() const override;

   private:
    CreateWindowFn create_fn_;
    CloseWindowFn  close_fn_;
    FocusWindowFn  focus_fn_;
    WindowCountFn  count_fn_;
};

}   // namespace spectra::adapters::qt
