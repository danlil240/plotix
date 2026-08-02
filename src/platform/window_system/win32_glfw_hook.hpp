#pragma once

struct GLFWwindow;

namespace spectra
{

class WindowManager;

// Win32-only: track WM_ENTERSIZEMOVE and render while the OS modal loop blocks
// the main Spectra tick (title-bar move / live edge resize).
#ifdef _WIN32
void install_win32_interactive_hook(GLFWwindow* window, WindowManager* mgr);
void remove_win32_interactive_hook(GLFWwindow* window);
bool win32_window_in_size_move(GLFWwindow* window);
#else
inline void install_win32_interactive_hook(GLFWwindow*, WindowManager*) {}
inline void remove_win32_interactive_hook(GLFWwindow*) {}
inline bool win32_window_in_size_move(GLFWwindow*)
{
    return false;
}
#endif

}   // namespace spectra
