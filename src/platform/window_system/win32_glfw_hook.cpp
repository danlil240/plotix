#ifdef _WIN32

    #include "win32_glfw_hook.hpp"

    #include "ui/window/window_manager.hpp"

    #ifndef GLFW_EXPOSE_NATIVE_WIN32
        #define GLFW_EXPOSE_NATIVE_WIN32
    #endif
    #include <GLFW/glfw3.h>
    #include <GLFW/glfw3native.h>

    #include <commctrl.h>
    #include <unordered_map>

namespace spectra
{

namespace
{

constexpr UINT_PTR kSubclassId = 1;

std::unordered_map<HWND, bool> g_size_move;

LRESULT CALLBACK
win32_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR ref_data)
{
    auto* mgr = reinterpret_cast<WindowManager*>(ref_data);

    switch (msg)
    {
        case WM_ENTERSIZEMOVE:
            g_size_move[hwnd] = true;
            break;
        case WM_EXITSIZEMOVE:
            g_size_move[hwnd] = false;
            break;
        case WM_SIZE:
        case WM_SIZING:
        case WM_MOVE:
        case WM_PAINT:
            if (mgr && g_size_move[hwnd])
                mgr->request_interactive_frame();
            break;
        default:
            break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}   // namespace

void install_win32_interactive_hook(GLFWwindow* window, WindowManager* mgr)
{
    if (!window || !mgr)
        return;

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd)
        return;

    SetWindowSubclass(hwnd, win32_subclass_proc, kSubclassId, reinterpret_cast<DWORD_PTR>(mgr));
}

void remove_win32_interactive_hook(GLFWwindow* window)
{
    if (!window)
        return;

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd)
        return;

    g_size_move.erase(hwnd);
    RemoveWindowSubclass(hwnd, win32_subclass_proc, kSubclassId);
}

bool win32_window_in_size_move(GLFWwindow* window)
{
    if (!window)
        return false;

    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd)
        return false;

    auto it = g_size_move.find(hwnd);
    return it != g_size_move.end() && it->second;
}

}   // namespace spectra

#endif   // _WIN32
