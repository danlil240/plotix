#pragma once

// Framework-neutral service interfaces for frontend abstraction.
//
// These interfaces allow ApplicationServices and command handlers to
// interact with the frontend (dialogs, clipboard, redraw, window
// management) without depending on ImGui, Qt, GLFW, or SDL3 headers.
//
// Each frontend provides concrete implementations and injects them
// into ApplicationServices during initialization.

#include <optional>
#include <string>
#include <vector>

#include <spectra/color.hpp>
#include <spectra/fwd.hpp>

namespace spectra
{

// ─── DialogService ───────────────────────────────────────────────────────────
// Abstract interface for file dialogs, message boxes, and color pickers.
// The frontend provides a concrete implementation (ImGui, Qt, or a test stub).

class DialogService
{
   public:
    virtual ~DialogService() = default;

    enum class FileType
    {
        Open,
        Save
    };

    struct FileFilter
    {
        std::string name;      // e.g. "PNG Image"
        std::string pattern;   // e.g. "*.png"
    };

    // Show a file open/save dialog.  Returns selected path or nullopt if cancelled.
    virtual std::optional<std::string> file_dialog(FileType                       type,
                                                   const std::string&             title,
                                                   const std::string&             default_path,
                                                   const std::vector<FileFilter>& filters) = 0;

    // Show a message box with OK/Cancel buttons.  Returns true for OK.
    virtual bool message_box(const std::string& title,
                             const std::string& message,
                             bool               cancel_button = false) = 0;

    // Show a color picker.  Returns selected color or nullopt if cancelled.
    virtual std::optional<Color> color_picker(const std::string& title, const Color& initial) = 0;

    // Prompt for a floating-point value. Frontends that do not provide a
    // numeric dialog may keep the default cancellation result.
    virtual std::optional<double> number_input(const std::string& title,
                                               const std::string& label,
                                               double             initial,
                                               double             minimum,
                                               double             maximum,
                                               int                decimals)
    {
        (void)title;
        (void)label;
        (void)initial;
        (void)minimum;
        (void)maximum;
        (void)decimals;
        return std::nullopt;
    }
};

// ─── ClipboardService ────────────────────────────────────────────────────────
// Abstract interface for system clipboard operations.

class ClipboardService
{
   public:
    virtual ~ClipboardService() = default;

    // Copy text to the system clipboard.
    virtual void copy_text(const std::string& text) = 0;

    // Copy image data (PNG-encoded bytes) to the system clipboard.
    virtual void copy_image(const std::vector<uint8_t>& png_data) = 0;

    // Paste text from the system clipboard.  Returns empty string if unavailable.
    virtual std::string paste_text() = 0;
};

// ─── RedrawRequest ───────────────────────────────────────────────────────────
// Abstract interface for requesting redraws from the frontend.

class RedrawRequest
{
   public:
    virtual ~RedrawRequest() = default;

    // Request a full redraw of all visible canvases.
    virtual void request_redraw() = 0;

    // Request a redraw of a specific window (by FigureId of the active figure).
    virtual void request_redraw(FigureId figure_id) = 0;
};

// ─── WindowService ───────────────────────────────────────────────────────────
// Abstract interface for window-level operations (create, close, focus, etc.)
// This is NOT a window manager — it's a minimal service interface for commands
// and automation to interact with windows without knowing the frontend.

class WindowService
{
   public:
    virtual ~WindowService() = default;

    // Create a new window with the given title and size.
    // Returns the FigureId of the figure placed in the new window,
    // or INVALID_FIGURE_ID on failure.
    virtual FigureId create_window(const std::string& title,
                                   uint32_t           width  = 800,
                                   uint32_t           height = 600) = 0;

    // Close the window containing the given figure.
    virtual void close_window(FigureId figure_id) = 0;

    // Focus the window containing the given figure.
    virtual void focus_window(FigureId figure_id) = 0;

    // Get the number of open windows.
    virtual size_t window_count() const = 0;
};

// ─── Null/default implementations ────────────────────────────────────────────
// Useful for headless mode and testing.

class NullDialogService final : public DialogService
{
   public:
    std::optional<std::string> file_dialog(FileType,
                                           const std::string&,
                                           const std::string&,
                                           const std::vector<FileFilter>&) override
    {
        return std::nullopt;
    }

    bool message_box(const std::string&, const std::string&, bool) override { return true; }

    std::optional<Color> color_picker(const std::string&, const Color&) override
    {
        return std::nullopt;
    }
};

class NullClipboardService final : public ClipboardService
{
   public:
    void        copy_text(const std::string&) override {}
    void        copy_image(const std::vector<uint8_t>&) override {}
    std::string paste_text() override { return {}; }
};

class NullRedrawRequest final : public RedrawRequest
{
   public:
    void request_redraw() override {}
    void request_redraw(FigureId) override {}
};

class NullWindowService final : public WindowService
{
   public:
    FigureId create_window(const std::string&, uint32_t, uint32_t) override
    {
        return INVALID_FIGURE_ID;
    }
    void   close_window(FigureId) override {}
    void   focus_window(FigureId) override {}
    size_t window_count() const override { return 0; }
};

}   // namespace spectra
