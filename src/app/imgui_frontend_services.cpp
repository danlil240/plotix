// imgui_frontend_services.cpp — ImGui frontend service implementations.
//
// Concrete implementations of DialogService, ClipboardService, RedrawRequest,
// and WindowService for the legacy ImGui + GLFW/SDL3 frontend.

#include "app/imgui_frontend_services.hpp"

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)

    #include "ui/app/session_runtime.hpp"
    #include "ui/native_dialog_policy.hpp"
    #include "ui/window/window_manager.hpp"

    #include "render/vulkan/window_context.hpp"
    #include "platform/clipboard_image.hpp"

    #include <spectra/logger.hpp>

    #include <algorithm>

    #ifdef SPECTRA_USE_GLFW
        #define GLFW_INCLUDE_NONE
        #define GLFW_INCLUDE_VULKAN
        #include <GLFW/glfw3.h>
    #endif

    #ifdef SPECTRA_USE_SDL3
        #include <SDL3/SDL.h>
    #endif

    #include "../../../third_party/tinyfiledialogs.h"

namespace spectra
{

// ─── ImGuiDialogService ──────────────────────────────────────────────────────

std::optional<std::string>
ImGuiDialogService::file_dialog(FileType                       type,
                                const std::string&             title,
                                const std::string&             default_path,
                                const std::vector<FileFilter>& filters)
{
    if (!native_dialogs_enabled())
    {
        SPECTRA_LOG_INFO("dialog", "Native dialogs disabled — returning empty path");
        return std::nullopt;
    }

    // Convert filters to tinyfiledialogs format.
    std::vector<std::string>  patterns;
    std::vector<const char*>  pattern_ptrs;
    std::string               description;

    for (const auto& f : filters)
    {
        patterns.push_back(f.pattern);
        if (!description.empty())
            description += " ";
        description += f.name;
    }
    for (const auto& p : patterns)
        pattern_ptrs.push_back(p.c_str());

    const char* result = nullptr;
    if (type == FileType::Open)
    {
        result = tinyfd_openFileDialog(
            title.c_str(),
            default_path.c_str(),
            static_cast<int>(pattern_ptrs.size()),
            pattern_ptrs.data(),
            description.empty() ? nullptr : description.c_str(),
            0);   // single file
    }
    else
    {
        result = tinyfd_saveFileDialog(
            title.c_str(),
            default_path.c_str(),
            static_cast<int>(pattern_ptrs.size()),
            pattern_ptrs.data(),
            description.empty() ? nullptr : description.c_str());
    }

    if (result)
        return std::string(result);
    return std::nullopt;
}

bool ImGuiDialogService::message_box(const std::string& title,
                                     const std::string& message,
                                     bool               cancel_button)
{
    if (!native_dialogs_enabled())
        return true;

    const char* type = cancel_button ? "yesno" : "ok";
    int         rc   = tinyfd_messageBox(title.c_str(), message.c_str(), type, "info", 1);
    return rc == 1;
}

std::optional<Color>
ImGuiDialogService::color_picker(const std::string& title, const Color& initial)
{
    if (!native_dialogs_enabled())
        return std::nullopt;

    unsigned char rgb[3] = {
        static_cast<unsigned char>(initial.r * 255.0f),
        static_cast<unsigned char>(initial.g * 255.0f),
        static_cast<unsigned char>(initial.b * 255.0f),
    };

    unsigned char out_rgb[3] = {0, 0, 0};
    if (tinyfd_colorChooser(title.c_str(), nullptr, rgb, out_rgb))
    {
        Color result;
        result.r = out_rgb[0] / 255.0f;
        result.g = out_rgb[1] / 255.0f;
        result.b = out_rgb[2] / 255.0f;
        result.a = initial.a;
        return result;
    }
    return std::nullopt;
}

// ─── ImGuiClipboardService ───────────────────────────────────────────────────

void ImGuiClipboardService::copy_text(const std::string& text)
{
#ifdef SPECTRA_USE_GLFW
    glfwSetClipboardString(nullptr, text.c_str());
#elif defined(SPECTRA_USE_SDL3)
    SDL_SetClipboardText(text.c_str());
#endif
}

void ImGuiClipboardService::copy_image(const std::vector<uint8_t>& png_data)
{
    platform::copy_image_to_clipboard(png_data.data(), png_data.size());
}

std::string ImGuiClipboardService::paste_text()
{
#ifdef SPECTRA_USE_GLFW
    const char* text = glfwGetClipboardString(nullptr);
    return text ? std::string(text) : std::string{};
#elif defined(SPECTRA_USE_SDL3)
    char* text = SDL_GetClipboardText();
    if (!text)
        return {};
    std::string result(text);
    SDL_free(text);
    return result;
#else
    return {};
#endif
}

// ─── ImGuiRedrawRequest ──────────────────────────────────────────────────────

void ImGuiRedrawRequest::request_redraw()
{
    session_.redraw_tracker().mark_dirty("service_request");
}

void ImGuiRedrawRequest::request_redraw(FigureId /*figure_id*/)
{
    // For now, a figure-specific redraw still marks the global tracker dirty.
    // Per-canvas granularity can be added when the render scheduler supports it.
    session_.redraw_tracker().mark_dirty("service_request_figure");
}

// ─── ImGuiWindowService ──────────────────────────────────────────────────────

FigureId ImGuiWindowService::create_window(const std::string& title,
                                           uint32_t           width,
                                           uint32_t           height)
{
    auto* wctx = wm_.create_window_with_ui(width, height, title, INVALID_FIGURE_ID);
    return wctx ? wctx->active_figure_id : INVALID_FIGURE_ID;
}

void ImGuiWindowService::close_window(FigureId figure_id)
{
    // Find the window containing this figure.
    for (auto* wctx : wm_.windows())
    {
        if (!wctx)
            continue;
        const auto& figs = wctx->assigned_figures;
        if (std::find(figs.begin(), figs.end(), figure_id) != figs.end())
        {
            wm_.request_close(wctx->id);
            return;
        }
    }
}

void ImGuiWindowService::focus_window(FigureId figure_id)
{
    // Find and focus the window containing this figure.
    for (auto* wctx : wm_.windows())
    {
        if (!wctx)
            continue;
        const auto& figs = wctx->assigned_figures;
        if (std::find(figs.begin(), figs.end(), figure_id) != figs.end())
        {
            // Focus is platform-dependent; the WindowManager handles
            // focus tracking via OS callbacks.  This is a no-op stub
            // until platform focus APIs are abstracted.
            return;
        }
    }
}

size_t ImGuiWindowService::window_count() const
{
    return wm_.window_count();
}

}   // namespace spectra

#endif // SPECTRA_USE_GLFW || SPECTRA_USE_SDL3
