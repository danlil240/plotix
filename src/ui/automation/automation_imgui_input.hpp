#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include "imgui.h"
    #include "ui/app/window_ui_context.hpp"

    #ifdef SPECTRA_USE_GLFW
        #define GLFW_INCLUDE_NONE
        #include <GLFW/glfw3.h>
        #include "backends/imgui_impl_glfw.h"
    #endif

namespace spectra::automation
{

struct ScopedImGuiContext
{
    WindowUIContext* ui_ctx;
    ImGuiContext*    prev;
    ImGuiContext*    win;

    explicit ScopedImGuiContext(WindowUIContext* ctx)
        : ui_ctx(ctx), prev(ImGui::GetCurrentContext()), win(nullptr)
    {
        if (ui_ctx && ui_ctx->imgui_ui)
        {
            win = ui_ctx->imgui_ui->imgui_context();
            if (win)
                ImGui::SetCurrentContext(win);
        }
    }

    ~ScopedImGuiContext()
    {
        if (win && prev != win)
            ImGui::SetCurrentContext(prev);
    }

    bool active() const { return win != nullptr; }
};

inline void inject_mouse_click(WindowUIContext* ui_ctx, double x, double y, int btn)
{
    ScopedImGuiContext scope(ui_ctx);
    if (!scope.active())
        return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    io.AddMouseButtonEvent(btn, true);
    io.AddMouseButtonEvent(btn, false);
}

inline void inject_scroll(WindowUIContext* ui_ctx, double x, double y, double dx, double dy)
{
    ScopedImGuiContext scope(ui_ctx);
    if (!scope.active())
        return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    io.AddMouseWheelEvent(static_cast<float>(dx), static_cast<float>(dy));
}

inline bool tab_drag_active(WindowUIContext* ui_ctx)
{
    if (!ui_ctx)
        return false;
    if (ui_ctx->tab_drag_controller.is_active())
        return true;
    return ui_ctx->imgui_ui && ui_ctx->imgui_ui->is_tab_interacting();
}

inline void cancel_tab_drag_capture(WindowUIContext* ui_ctx)
{
    if (!ui_ctx)
        return;
    if (ui_ctx->tab_drag_controller.is_active())
        ui_ctx->tab_drag_controller.cancel();
    if (ui_ctx->figure_tabs)
        ui_ctx->figure_tabs->cancel_drag();
    if (ui_ctx->imgui_ui)
        ui_ctx->imgui_ui->cancel_tab_drag_state();
}

inline void dismiss_ui_capture(WindowUIContext* ui_ctx)
{
    cancel_tab_drag_capture(ui_ctx);
    if (ui_ctx && ui_ctx->imgui_ui)
        ui_ctx->imgui_ui->dismiss_automation_capture();
}

inline void inject_key(WindowUIContext* ui_ctx, int key, int mods)
{
    #ifdef SPECTRA_USE_GLFW
    if (ui_ctx && ui_ctx->glfw_window)
    {
        auto* win = static_cast<GLFWwindow*>(ui_ctx->glfw_window);
        ImGui_ImplGlfw_KeyCallback(win, key, 0, GLFW_PRESS, mods);
        ImGui_ImplGlfw_KeyCallback(win, key, 0, GLFW_RELEASE, mods);
        return;
    }
    #endif
    ScopedImGuiContext scope(ui_ctx);
    if (!scope.active())
        return;
    if (key >= 32 && key <= 126)
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(key));
}

}   // namespace spectra::automation

#endif   // SPECTRA_USE_IMGUI
