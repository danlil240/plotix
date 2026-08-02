// GLFW callback trampolines and input callback implementations for WindowManager.
// Extracted from window_manager.cpp (QW-8) — these are pure event-routing functions
// with no dependency on window lifecycle state.

#include "window_manager.hpp"

#include "platform/window_system/win32_glfw_hook.hpp"
#include <spectra/logger.hpp>

#include "render/vulkan/window_context.hpp"
#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_impl_glfw.h>

    #include <spectra/figure_registry.hpp>
    #include "ui/figures/figure_manager.hpp"
    #include "ui/imgui/imgui_integration.hpp"
#endif

#include <string>

namespace spectra
{

void WindowManager::request_redraw(const char* reason)
{
    if (redraw_request_handler_)
        redraw_request_handler_(reason);
}

// --- GLFW callback trampolines ---

#ifdef SPECTRA_USE_GLFW

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow* window) const
{
    for (auto& w : windows_)
    {
        if (w->glfw_window == window)
            return w.get();
    }
    return nullptr;
}

void WindowManager::glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || width <= 0 || height <= 0)
        return;

    wctx->needs_resize   = true;
    wctx->pending_width  = static_cast<uint32_t>(width);
    wctx->pending_height = static_cast<uint32_t>(height);
    wctx->resize_time    = std::chrono::steady_clock::now();
    mgr->request_redraw("resize");

    // Only pump during Win32 title-bar move / edge resize (modal loop blocks tick()).
    if (win32_window_in_size_move(window))
        mgr->request_interactive_frame();

    SPECTRA_LOG_DEBUG("window_manager",
                      "Window " + std::to_string(wctx->id) + " resize: " + std::to_string(width)
                          + "x" + std::to_string(height));
}

void WindowManager::glfw_window_close_callback(GLFWwindow* window)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx)
        return;

    SPECTRA_LOG_TRACE("input", "Window {} close requested", wctx->id);
    wctx->should_close = true;
    mgr->pending_close_ids_.push_back(wctx->id);
}

void WindowManager::glfw_window_refresh_callback(GLFWwindow* window)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx)
        return;

    // Win32 fires this during WM_ENTERSIZEMOVE (title-bar move / edge resize).
    // Sync framebuffer size and keep the render loop alive while the OS modal
    // loop would otherwise stall glfwWaitEvents.
    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width > 0 && height > 0)
    {
        wctx->needs_resize   = true;
        wctx->pending_width  = static_cast<uint32_t>(width);
        wctx->pending_height = static_cast<uint32_t>(height);
        wctx->resize_time    = std::chrono::steady_clock::now();
    }
    mgr->request_redraw("refresh");
    if (win32_window_in_size_move(window))
        mgr->request_interactive_frame();
}

void WindowManager::glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx)
        return;

    SPECTRA_LOG_TRACE("input", "Window {} focus {}", wctx->id, focused ? "gained" : "lost");
    wctx->is_focused = (focused != 0);
    if (focused)
        wctx->z_order = mgr->next_z_order_++;
    mgr->request_redraw("focus");
    #ifdef SPECTRA_USE_IMGUI
    // Forward focus event to ImGui for this window's context
    if (wctx->imgui_context && wctx->ui_ctx)
    {
        ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
        ImGui_ImplGlfw_WindowFocusCallback(window, focused);
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
    }
    #endif
}

void WindowManager::install_glfw_lifecycle_callbacks(GLFWwindow* glfw_win)
{
    if (!glfw_win)
        return;
    glfwSetFramebufferSizeCallback(glfw_win, glfw_framebuffer_size_callback);
    glfwSetWindowCloseCallback(glfw_win, glfw_window_close_callback);
    glfwSetWindowFocusCallback(glfw_win, glfw_window_focus_callback);
    glfwSetWindowRefreshCallback(glfw_win, glfw_window_refresh_callback);
    install_win32_interactive_hook(glfw_win, this);
}

void WindowManager::install_input_callbacks(WindowContext& wctx)
{
    auto* glfw_win = static_cast<GLFWwindow*>(wctx.glfw_window);
    if (glfw_win)
    {
        install_glfw_lifecycle_callbacks(glfw_win);
        // Input callbacks
        glfwSetCursorPosCallback(glfw_win, glfw_cursor_pos_callback);
        glfwSetMouseButtonCallback(glfw_win, glfw_mouse_button_callback);
        glfwSetScrollCallback(glfw_win, glfw_scroll_callback);
        glfwSetKeyCallback(glfw_win, glfw_key_callback);
        glfwSetCharCallback(glfw_win, glfw_char_callback);
        glfwSetCursorEnterCallback(glfw_win, glfw_cursor_enter_callback);
        glfwSetDropCallback(glfw_win, glfw_drop_callback);
    }
}

// --- Full GLFW input callbacks for windows with UI ---

void WindowManager::glfw_cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context so input is routed correctly.
    // We passed install_callbacks=false during init, so we must forward
    // events to ImGui manually.
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CursorPosCallback(window, x, y);
    mgr->request_redraw("mouse_move");

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;

    bool input_is_dragging =
        input_handler.mode() == InteractionMode::Dragging || input_handler.is_measure_dragging()
        || input_handler.is_middle_pan_dragging() || input_handler.has_measure_result();

    if (!input_is_dragging && imgui_ui
        && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting()))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_mouse_move(x, y);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)x;
    (void)y;
    #endif
}

void WindowManager::glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    SPECTRA_LOG_TRACE("input",
                      "Mouse button {} {} (mods={})",
                      button,
                      action == 1 ? "press" : "release",
                      mods);

    // Track mouse release for tab drag (callback-based).
    // This runs before the ui_ctx check so it catches events on preview windows too.
    // We suppress releases that arrive within the suppression window — the WM
    // temporarily grabs the pointer when a new GLFW window is created/mapped,
    // sending a real ButtonRelease to the source window.  That release is an
    // artifact, not the user lifting their finger.
    if (mgr->mouse_release_tracking_ && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= mgr->suppress_release_until_)
            mgr->mouse_release_seen_ = true;
    }

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    mgr->request_redraw("mouse_button");

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    bool input_is_dragging = input_handler.mode() == InteractionMode::Dragging
                             || input_handler.is_measure_dragging()
                             || input_handler.is_middle_pan_dragging();

    if (!input_is_dragging && imgui_ui
        && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting()))
    {
        constexpr int GLFW_RELEASE_VAL = 0;
        if (action == GLFW_RELEASE_VAL)
            input_handler.on_mouse_button(button, action, mods, x, y);
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_mouse_button(button, action, mods, x, y);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)button;
    (void)action;
    (void)mods;
    #endif
}

void WindowManager::glfw_scroll_callback(GLFWwindow* window, double x_offset, double y_offset)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    SPECTRA_LOG_TRACE("input", "Scroll x={} y={}", x_offset, y_offset);

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_ScrollCallback(window, x_offset, y_offset);
    mgr->request_redraw("scroll");

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& dock_system   = ui.dock_system;
    auto& cmd_palette   = ui.cmd_palette;

    if (cmd_palette.is_open())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }
    if (imgui_ui && imgui_ui->wants_capture_mouse())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(window, &cx, &cy);

    if (dock_system.is_split())
    {
        SplitPane* root = dock_system.split_view().root();
        if (root)
        {
            SplitPane* pane = root->find_at_point(static_cast<float>(cx), static_cast<float>(cy));
            if (pane && pane->is_leaf())
            {
                FigureId fi = pane->figure_index();
                if (mgr->registry_)
                {
                    Figure* pfig = mgr->registry_->get(fi);
                    if (pfig)
                        input_handler.set_figure(pfig);
                }
            }
        }
    }

    input_handler.on_scroll(x_offset, y_offset, cx, cy);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)x_offset;
    (void)y_offset;
    #endif
}

void WindowManager::glfw_key_callback(GLFWwindow* window,
                                      int         key,
                                      int         scancode,
                                      int         action,
                                      int         mods)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    auto& ui = *wctx->ui_ctx;

    // Switch to this window's ImGui context for correct input routing
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    mgr->request_redraw("key");

    auto& input_handler = ui.input_handler;
    auto& imgui_ui      = ui.imgui_ui;
    auto& shortcut_mgr  = ui.shortcut_mgr;

    SPECTRA_LOG_TRACE("input",
                      "Key {} {} (mods={})",
                      key,
                      action == 1 ? "press" : (action == 0 ? "release" : "repeat"),
                      mods);

    // Always let the shortcut manager try modifier-key combos (Ctrl+C/V/X etc.)
    // and Delete, even when ImGui wants keyboard focus (e.g. inspector open).
    // This ensures clipboard shortcuts work regardless of panel state.
    constexpr int GLFW_PRESS_VAL_  = 1;
    constexpr int GLFW_MOD_CTRL_   = 0x0002;
    constexpr int GLFW_KEY_DELETE_ = 261;
    constexpr int GLFW_KEY_ESC_    = 256;
    bool          is_app_shortcut =
        (action == GLFW_PRESS_VAL_)
        && ((mods & GLFW_MOD_CTRL_) != 0 || key == GLFW_KEY_DELETE_ || key == GLFW_KEY_ESC_);
    if (is_app_shortcut && shortcut_mgr.on_key(key, action, mods))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    if (imgui_ui && imgui_ui->wants_capture_keyboard())
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }
    if (shortcut_mgr.on_key(key, action, mods))
    {
        if (prev_ctx)
            ImGui::SetCurrentContext(prev_ctx);
        return;
    }

    // Q (no modifiers) = close active tab; if last tab, close window
    constexpr int GLFW_KEY_Q_VAL = 81;   // GLFW_KEY_Q
    constexpr int GLFW_PRESS_VAL = 1;    // GLFW_PRESS
    if (key == GLFW_KEY_Q_VAL && action == GLFW_PRESS_VAL && mods == 0)
    {
        auto* fm = ui.fig_mgr;
        if (fm)
        {
            fm->queue_close(fm->active_index());
            if (prev_ctx)
                ImGui::SetCurrentContext(prev_ctx);
            return;
        }
    }

    input_handler.on_key(key, action, mods);

    // Restore previous ImGui context
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)scancode;
    (void)key;
    (void)action;
    (void)mods;
    #endif
}

void WindowManager::glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    #ifdef SPECTRA_USE_IMGUI
    // Switch to this window's ImGui context and forward char event
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CharCallback(window, codepoint);
    mgr->request_redraw("char");
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)codepoint;
    #endif
}

void WindowManager::glfw_cursor_enter_callback(GLFWwindow* window, int entered)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    if (!wctx || !wctx->ui_ctx)
        return;

    SPECTRA_LOG_TRACE("input", "Cursor {} window {}", entered ? "entered" : "left", wctx->id);

    #ifdef SPECTRA_USE_IMGUI
    if (!entered)
        wctx->ui_ctx->input_handler.clear_cursor_readout();

    // Switch to this window's ImGui context and forward cursor enter/leave
    ImGuiContext* prev_ctx = ImGui::GetCurrentContext();
    if (wctx->imgui_context)
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(wctx->imgui_context));
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
    mgr->request_redraw("cursor_enter");
    if (prev_ctx)
        ImGui::SetCurrentContext(prev_ctx);
    #else
    (void)entered;
    #endif
}

void WindowManager::glfw_drop_callback(GLFWwindow* window, int count, const char** paths)
{
    auto* mgr = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!mgr || !mgr->file_drop_handler_)
        return;

    WindowContext* wctx = mgr->find_by_glfw_window(window);
    uint32_t       wid  = wctx ? wctx->id : 0;

    for (int i = 0; i < count; ++i)
    {
        if (paths[i])
            mgr->file_drop_handler_(wid, std::string(paths[i]));
    }
}

#else

// Stubs when GLFW is not available
void WindowManager::glfw_framebuffer_size_callback(GLFWwindow*, int, int) {}
void WindowManager::glfw_window_close_callback(GLFWwindow*) {}
void WindowManager::glfw_window_focus_callback(GLFWwindow*, int) {}
void WindowManager::glfw_window_refresh_callback(GLFWwindow*) {}

void WindowManager::install_glfw_lifecycle_callbacks(GLFWwindow*) {}
void WindowManager::glfw_cursor_pos_callback(GLFWwindow*, double, double) {}
void WindowManager::glfw_mouse_button_callback(GLFWwindow*, int, int, int) {}
void WindowManager::glfw_scroll_callback(GLFWwindow*, double, double) {}
void WindowManager::glfw_key_callback(GLFWwindow*, int, int, int, int) {}
void WindowManager::glfw_char_callback(GLFWwindow*, unsigned int) {}
void WindowManager::glfw_cursor_enter_callback(GLFWwindow*, int) {}
void WindowManager::glfw_drop_callback(GLFWwindow*, int, const char**) {}

void WindowManager::install_input_callbacks(WindowContext& wctx)
{
    (void)wctx;
}

WindowContext* WindowManager::find_by_glfw_window(GLFWwindow*) const
{
    return nullptr;
}

#endif

}   // namespace spectra
