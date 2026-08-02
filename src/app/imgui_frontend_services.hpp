#pragma once

// ImGui frontend service implementations.
//
// Concrete implementations of DialogService, ClipboardService, RedrawRequest,
// and WindowService for the legacy ImGui + GLFW/SDL3 frontend.  These are
// created and injected into ApplicationServices during init_runtime() so that
// command handlers and automation can interact with the frontend through
// framework-neutral interfaces.

#include "app/frontend_services.hpp"

#include <spectra/fwd.hpp>

namespace spectra
{

class SessionRuntime;
class WindowManager;

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)

// ─── ImGuiDialogService ──────────────────────────────────────────────────────
// Uses tinyfiledialogs for native file dialogs and message boxes.
// Respects the --no-native-dialogs / SPECTRA_NO_NATIVE_DIALOGS policy.
class ImGuiDialogService final : public DialogService
{
   public:
    ImGuiDialogService() = default;

    std::optional<std::string> file_dialog(FileType                       type,
                                           const std::string&             title,
                                           const std::string&             default_path,
                                           const std::vector<FileFilter>& filters) override;

    bool message_box(const std::string& title,
                     const std::string& message,
                     bool               cancel_button = false) override;

    std::optional<Color> color_picker(const std::string& title, const Color& initial) override;
};

// ─── ImGuiClipboardService ───────────────────────────────────────────────────
// Uses platform::copy_image_to_clipboard for images and GLFW/SDL3 for text.
class ImGuiClipboardService final : public ClipboardService
{
   public:
    ImGuiClipboardService() = default;

    void        copy_text(const std::string& text) override;
    void        copy_image(const std::vector<uint8_t>& png_data) override;
    std::string paste_text() override;
};

// ─── ImGuiRedrawRequest ──────────────────────────────────────────────────────
// Marks the session's RedrawTracker dirty to request a frame.
class ImGuiRedrawRequest final : public RedrawRequest
{
   public:
    explicit ImGuiRedrawRequest(SessionRuntime& session) : session_(session) {}

    void request_redraw() override;
    void request_redraw(FigureId figure_id) override;

   private:
    SessionRuntime& session_;
};

// ─── ImGuiWindowService ──────────────────────────────────────────────────────
// Delegates window operations to the WindowManager.
class ImGuiWindowService final : public WindowService
{
   public:
    explicit ImGuiWindowService(WindowManager& wm) : wm_(wm) {}

    FigureId create_window(const std::string& title,
                           uint32_t           width  = 800,
                           uint32_t           height = 600) override;

    void   close_window(FigureId figure_id) override;
    void   focus_window(FigureId figure_id) override;
    size_t window_count() const override;

   private:
    WindowManager& wm_;
};

#endif   // SPECTRA_USE_GLFW || SPECTRA_USE_SDL3

}   // namespace spectra
