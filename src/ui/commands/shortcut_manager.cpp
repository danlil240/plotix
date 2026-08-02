#include "shortcut_manager.hpp"

#include <algorithm>
#include <cctype>
#include <spectra/logger.hpp>
#include <sstream>

#include "command_registry.hpp"

namespace spectra
{

// ─── Shortcut string conversion ──────────────────────────────────────────────

// GLFW key codes (subset needed for string conversion)
namespace glfw_keys
{
constexpr int KEY_SPACE         = 32;
constexpr int KEY_APOSTROPHE    = 39;
constexpr int KEY_COMMA         = 44;
constexpr int KEY_MINUS         = 45;
constexpr int KEY_PERIOD        = 46;
constexpr int KEY_SLASH         = 47;
constexpr int KEY_0             = 48;
constexpr int KEY_9             = 57;
constexpr int KEY_SEMICOLON     = 59;
constexpr int KEY_EQUAL         = 61;
constexpr int KEY_A             = 65;
constexpr int KEY_Z             = 90;
constexpr int KEY_LEFT_BRACKET  = 91;
constexpr int KEY_BACKSLASH     = 92;
constexpr int KEY_RIGHT_BRACKET = 93;
constexpr int KEY_ESCAPE        = 256;
constexpr int KEY_ENTER         = 257;
constexpr int KEY_TAB           = 258;
constexpr int KEY_BACKSPACE     = 259;
constexpr int KEY_INSERT        = 260;
constexpr int KEY_DELETE        = 261;
constexpr int KEY_RIGHT         = 262;
constexpr int KEY_LEFT          = 263;
constexpr int KEY_DOWN          = 264;
constexpr int KEY_UP            = 265;
constexpr int KEY_PAGE_UP       = 266;
constexpr int KEY_PAGE_DOWN     = 267;
constexpr int KEY_HOME          = 268;
constexpr int KEY_END           = 269;
constexpr int KEY_F1            = 290;
constexpr int KEY_F12           = 301;

// Named aliases for readability in register_defaults()
constexpr int KEY_B = 66;
constexpr int KEY_C = 67;
constexpr int KEY_D = 68;
constexpr int KEY_F = 70;
constexpr int KEY_G = 71;
constexpr int KEY_K = 75;
constexpr int KEY_L = 76;
constexpr int KEY_M = 77;
constexpr int KEY_N = 78;
constexpr int KEY_P = 80;
constexpr int KEY_Q = 81;
constexpr int KEY_R = 82;
constexpr int KEY_S = 83;
constexpr int KEY_T = 84;
constexpr int KEY_V = 86;
constexpr int KEY_W = 87;
constexpr int KEY_X = 88;
constexpr int KEY_Y = 89;
}   // namespace glfw_keys

static std::string key_to_string(int key)
{
    using namespace glfw_keys;
    if (key >= KEY_A && key <= KEY_Z)
    {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= KEY_0 && key <= KEY_9)
    {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= KEY_F1 && key <= KEY_F12)
    {
        std::string result("F");
        result += std::to_string(key - KEY_F1 + 1);
        return result;
    }
    switch (key)
    {
        case KEY_SPACE:
            return "Space";
        case KEY_ESCAPE:
            return "Escape";
        case KEY_ENTER:
            return "Enter";
        case KEY_TAB:
            return "Tab";
        case KEY_BACKSPACE:
            return "Backspace";
        case KEY_INSERT:
            return "Insert";
        case KEY_DELETE:
            return "Delete";
        case KEY_RIGHT:
            return "Right";
        case KEY_LEFT:
            return "Left";
        case KEY_DOWN:
            return "Down";
        case KEY_UP:
            return "Up";
        case KEY_PAGE_UP:
            return "PageUp";
        case KEY_PAGE_DOWN:
            return "PageDown";
        case KEY_HOME:
            return "Home";
        case KEY_END:
            return "End";
        case KEY_MINUS:
            return "-";
        case KEY_EQUAL:
            return "=";
        case KEY_LEFT_BRACKET:
            return "[";
        case KEY_RIGHT_BRACKET:
            return "]";
        case KEY_SEMICOLON:
            return ";";
        case KEY_APOSTROPHE:
            return "'";
        case KEY_COMMA:
            return ",";
        case KEY_PERIOD:
            return ".";
        case KEY_SLASH:
            return "/";
        case KEY_BACKSLASH:
            return "\\";
        default:
            return "Key" + std::to_string(key);
    }
}

static int string_to_key(const std::string& str)
{
    using namespace glfw_keys;
    if (str.size() == 1)
    {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
        if (c >= 'A' && c <= 'Z')
            return c;
        if (c >= '0' && c <= '9')
            return c;
        switch (c)
        {
            case '-':
                return KEY_MINUS;
            case '=':
                return KEY_EQUAL;
            case '[':
                return KEY_LEFT_BRACKET;
            case ']':
                return KEY_RIGHT_BRACKET;
            case ';':
                return KEY_SEMICOLON;
            case '\'':
                return KEY_APOSTROPHE;
            case ',':
                return KEY_COMMA;
            case '.':
                return KEY_PERIOD;
            case '/':
                return KEY_SLASH;
            case '\\':
                return KEY_BACKSLASH;
        }
    }

    std::string lower;
    lower.reserve(str.size());
    for (char c : str)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "space")
        return KEY_SPACE;
    if (lower == "escape" || lower == "esc")
        return KEY_ESCAPE;
    if (lower == "enter" || lower == "return")
        return KEY_ENTER;
    if (lower == "tab")
        return KEY_TAB;
    if (lower == "backspace")
        return KEY_BACKSPACE;
    if (lower == "insert")
        return KEY_INSERT;
    if (lower == "delete" || lower == "del")
        return KEY_DELETE;
    if (lower == "right")
        return KEY_RIGHT;
    if (lower == "left")
        return KEY_LEFT;
    if (lower == "down")
        return KEY_DOWN;
    if (lower == "up")
        return KEY_UP;
    if (lower == "pageup")
        return KEY_PAGE_UP;
    if (lower == "pagedown")
        return KEY_PAGE_DOWN;
    if (lower == "home")
        return KEY_HOME;
    if (lower == "end")
        return KEY_END;

    // F-keys
    if (lower.size() >= 2 && lower[0] == 'f')
    {
        int n = std::atoi(lower.c_str() + 1);
        if (n >= 1 && n <= 12)
            return KEY_F1 + n - 1;
    }

    return 0;
}

std::string Shortcut::to_string() const
{
    std::string result;
    if (has_mod(mods, KeyMod::Control))
        result += "Ctrl+";
    if (has_mod(mods, KeyMod::Shift))
        result += "Shift+";
    if (has_mod(mods, KeyMod::Alt))
        result += "Alt+";
    if (has_mod(mods, KeyMod::Super))
        result += "Super+";
    result += key_to_string(key);
    return result;
}

Shortcut Shortcut::from_string(const std::string& str)
{
    Shortcut                 s;
    std::istringstream       iss(str);
    std::string              token;
    std::vector<std::string> parts;

    // Split by '+'
    while (std::getline(iss, token, '+'))
    {
        // Trim whitespace
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
            token.pop_back();
        if (!token.empty())
            parts.push_back(token);
    }

    if (parts.empty())
        return s;

    // Last part is the key, everything before is modifiers
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        std::string lower;
        lower.reserve(parts[i].size());
        for (char c : parts[i])
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower == "ctrl" || lower == "control")
            s.mods = s.mods | KeyMod::Control;
        else if (lower == "shift")
            s.mods = s.mods | KeyMod::Shift;
        else if (lower == "alt")
            s.mods = s.mods | KeyMod::Alt;
        else if (lower == "super" || lower == "meta" || lower == "cmd")
            s.mods = s.mods | KeyMod::Super;
    }

    s.key = string_to_key(parts.back());
    return s;
}

// ─── ShortcutManager ─────────────────────────────────────────────────────────

void ShortcutManager::bind(Shortcut shortcut, const std::string& command_id)
{
    if (!shortcut.valid())
        return;
    std::lock_guard lock(mutex_);
    bindings_[shortcut] = command_id;
}

void ShortcutManager::unbind(const Shortcut& shortcut)
{
    std::lock_guard lock(mutex_);
    bindings_.erase(shortcut);
}

void ShortcutManager::unbind_command(const std::string& command_id)
{
    std::lock_guard lock(mutex_);
    std::erase_if(bindings_, [&](const auto& pair) { return pair.second == command_id; });
}

std::string ShortcutManager::command_for_shortcut(const Shortcut& shortcut) const
{
    std::lock_guard lock(mutex_);
    auto            it = bindings_.find(shortcut);
    return it != bindings_.end() ? it->second : "";
}

Shortcut ShortcutManager::shortcut_for_command(const std::string& command_id) const
{
    std::lock_guard lock(mutex_);
    for (const auto& [sc, id] : bindings_)
    {
        if (id == command_id)
            return sc;
    }
    return {};
}

std::vector<ShortcutBinding> ShortcutManager::all_bindings() const
{
    std::lock_guard              lock(mutex_);
    std::vector<ShortcutBinding> result;
    result.reserve(bindings_.size());
    for (const auto& [sc, id] : bindings_)
    {
        result.push_back({sc, id});
    }
    std::sort(result.begin(),
              result.end(),
              [](const ShortcutBinding& a, const ShortcutBinding& b)
              {
                  if (a.command_id != b.command_id)
                      return a.command_id < b.command_id;
                  return a.shortcut.to_string() < b.shortcut.to_string();
              });
    return result;
}

bool ShortcutManager::on_key(int key, int action, int mods)
{
    if (action != kGlfwPress)
        return false;
    if (!registry_)
        return false;

    Shortcut sc;
    sc.key  = key;
    sc.mods = static_cast<KeyMod>(mods & 0x0F);   // Mask to our modifier bits

    std::string cmd_id;
    {
        std::lock_guard lock(mutex_);
        auto            it = bindings_.find(sc);
        if (it == bindings_.end())
            return false;
        cmd_id = it->second;
    }

    SPECTRA_LOG_TRACE("input", "Shortcut {} -> command '{}'", sc.to_string(), cmd_id);
    return registry_->execute(cmd_id);
}

void ShortcutManager::register_defaults()
{
    using namespace glfw_keys;

    // View commands
    bind({KEY_R, KeyMod::None}, "view.reset");
    bind({KEY_A, KeyMod::None}, "view.autofit");
    bind({KEY_G, KeyMod::None}, "view.toggle_grid");
    bind({KEY_C, KeyMod::None}, "view.toggle_crosshair");
    bind({KEY_F, KeyMod::None}, "view.fullscreen");
    bind({KEY_HOME, KeyMod::None}, "view.home");

    // Keyboard zoom (+/- keys) — accessible without mouse
    bind({KEY_EQUAL, KeyMod::None}, "view.zoom_in");
    bind({KEY_MINUS, KeyMod::None}, "view.zoom_out");

    // Keyboard pan (arrow keys) — accessible without mouse
    bind({KEY_LEFT, KeyMod::None}, "view.pan_left");
    bind({KEY_RIGHT, KeyMod::None}, "view.pan_right");
    bind({KEY_UP, KeyMod::None}, "view.pan_up");
    bind({KEY_DOWN, KeyMod::None}, "view.pan_down");

    // Command palette
    bind({KEY_K, KeyMod::Control}, "app.command_palette");

    // File operations
    bind({KEY_S, KeyMod::Control}, "file.export_png");
    bind({KEY_S, KeyMod::Control | KeyMod::Shift}, "file.export_svg");
    bind({KEY_C, KeyMod::Control | KeyMod::Shift}, "file.copy_to_clipboard");

    // Figure management
    bind({KEY_N, KeyMod::Control}, "figure.new");
    bind({KEY_W, KeyMod::Control}, "figure.close");
    bind({KEY_Q, KeyMod::None}, "figure.close");

    // Undo/redo
    bind({KEY_Z, KeyMod::Control}, "edit.undo");
    bind({KEY_Y, KeyMod::Control}, "edit.redo");

    // Split view (KEY_SLASH for non-US layouts, KEY_BACKSLASH for US layouts)
    bind({KEY_SLASH, KeyMod::Control}, "view.split_right");
    bind({KEY_SLASH, KeyMod::Control | KeyMod::Shift}, "view.split_down");
    bind({KEY_BACKSLASH, KeyMod::Control}, "view.split_right");
    bind({KEY_BACKSLASH, KeyMod::Control | KeyMod::Shift}, "view.split_down");

    // Animation
    bind({KEY_SPACE, KeyMod::None}, "anim.toggle_play");
    bind({KEY_LEFT_BRACKET, KeyMod::None}, "anim.step_back");
    bind({KEY_RIGHT_BRACKET, KeyMod::None}, "anim.step_forward");

    // Timeline & curve editor panels
    bind({KEY_T, KeyMod::None}, "panel.toggle_timeline");
    bind({KEY_T, KeyMod::Shift}, "panel.toggle_curve_editor");

    // Tab switching (1-9)
    for (int i = 0; i < 9; ++i)
    {
        bind({KEY_0 + 1 + i, KeyMod::None}, "figure.tab_" + std::to_string(i + 1));
    }

    // Series
    bind({KEY_TAB, KeyMod::None}, "series.cycle_selection");

    // Series clipboard
    bind({KEY_C, KeyMod::Control}, "series.copy");
    bind({KEY_X, KeyMod::Control}, "series.cut");
    bind({KEY_V, KeyMod::Control}, "series.paste");
    bind({KEY_DELETE, KeyMod::None}, "series.delete");

    // Data clipboard
    bind({KEY_D, KeyMod::Control | KeyMod::Shift}, "data.copy_to_clipboard");

    // Tool modes
    bind({KEY_P, KeyMod::None}, "tool.pan");
    bind({KEY_S, KeyMod::None}, "tool.select");
    bind({KEY_Z, KeyMod::None}, "tool.box_zoom");
    bind({KEY_M, KeyMod::None}, "tool.measure");

    // Legend/border
    bind({KEY_L, KeyMod::None}, "view.toggle_legend");
    bind({KEY_B, KeyMod::None}, "view.toggle_border");

    // Topics panel (Phase 2 of plans/SPECTRA_TOPICS_PLAN.md)
    bind({KEY_T, KeyMod::Control | KeyMod::Shift}, "panel.toggle_topics");

    // Escape
    bind({KEY_ESCAPE, KeyMod::None}, "app.cancel");
}

size_t ShortcutManager::count() const
{
    std::lock_guard lock(mutex_);
    return bindings_.size();
}

void ShortcutManager::clear()
{
    std::lock_guard lock(mutex_);
    bindings_.clear();
}

}   // namespace spectra
