#pragma once

// QtInputRouter — maps Qt input events to Spectra's InputHandler.
//
// Provides static conversion functions for mouse buttons, keyboard modifiers,
// and key codes.  Used by SpectraVulkanWindow and other Qt canvas components.

#include <QtGui/QMouseEvent>

namespace spectra::adapters::qt
{

class QtInputRouter
{
   public:
    // Convert Qt mouse button to Spectra embed button code
    static int qtButtonToSpectra(Qt::MouseButton btn)
    {
        switch (btn)
        {
            case Qt::LeftButton:
                return 0;   // MOUSE_BUTTON_LEFT
            case Qt::RightButton:
                return 1;   // MOUSE_BUTTON_RIGHT
            case Qt::MiddleButton:
                return 2;   // MOUSE_BUTTON_MIDDLE
            default:
                return 0;
        }
    }

    // Convert Qt keyboard modifiers to Spectra embed modifier flags
    static int qtModsToSpectra(Qt::KeyboardModifiers mods)
    {
        int result = 0;
        if (mods & Qt::ShiftModifier)
            result |= 0x0001;   // MOD_SHIFT
        if (mods & Qt::ControlModifier)
            result |= 0x0002;   // MOD_CONTROL
        if (mods & Qt::AltModifier)
            result |= 0x0004;   // MOD_ALT
        if (mods & Qt::MetaModifier)
            result |= 0x0008;   // MOD_SUPER
        return result;
    }

    // Convert Qt key code to Spectra embed key code
    static int qtKeyToSpectra(int qt_key)
    {
        if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z)
            return qt_key;
        if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9)
            return qt_key;
        if (qt_key >= Qt::Key_Space && qt_key <= Qt::Key_AsciiTilde)
            return qt_key;
        if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F25)
            return 290 + (qt_key - Qt::Key_F1);

        switch (qt_key)
        {
            case Qt::Key_Escape:
                return 256;   // KEY_ESCAPE
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return 257;   // KEY_ENTER
            case Qt::Key_Tab:
                return 258;   // KEY_TAB
            case Qt::Key_Backspace:
                return 259;   // KEY_BACKSPACE
            case Qt::Key_Insert:
                return 260;   // KEY_INSERT
            case Qt::Key_Delete:
                return 261;   // KEY_DELETE
            case Qt::Key_Right:
                return 262;   // KEY_RIGHT
            case Qt::Key_Left:
                return 263;   // KEY_LEFT
            case Qt::Key_Down:
                return 264;   // KEY_DOWN
            case Qt::Key_Up:
                return 265;   // KEY_UP
            case Qt::Key_PageUp:
                return 266;   // KEY_PAGE_UP
            case Qt::Key_PageDown:
                return 267;   // KEY_PAGE_DOWN
            case Qt::Key_Home:
                return 268;   // KEY_HOME
            case Qt::Key_End:
                return 269;   // KEY_END
            case Qt::Key_CapsLock:
                return 280;   // KEY_CAPS_LOCK
            case Qt::Key_ScrollLock:
                return 281;   // KEY_SCROLL_LOCK
            case Qt::Key_NumLock:
                return 282;   // KEY_NUM_LOCK
            case Qt::Key_Print:
                return 283;   // KEY_PRINT_SCREEN
            case Qt::Key_Pause:
                return 284;   // KEY_PAUSE
            case Qt::Key_Shift:
                return 340;   // KEY_LEFT_SHIFT (Qt does not expose side)
            case Qt::Key_Control:
                return 341;   // KEY_LEFT_CONTROL
            case Qt::Key_Alt:
                return 342;   // KEY_LEFT_ALT
            case Qt::Key_Meta:
                return 343;   // KEY_LEFT_SUPER
            case Qt::Key_Menu:
                return 348;   // KEY_MENU
            default:
                return 0;
        }
    }
};

}   // namespace spectra::adapters::qt
