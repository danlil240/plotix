#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <deque>
    #include <functional>
    #include <string>
    #include <string_view>
    #include <vector>

    #include "ui/imgui/imgui_integration.hpp"

namespace spectra::ui::shell
{
struct MenuAction
{
    std::string             label{};
    std::string             shortcut{};
    std::function<void()>   on_click{};
    std::function<bool()>   enabled{};
    std::function<bool()>   checked{};
    bool                    separator = false;
    std::vector<MenuAction> submenu{};
};

class Menu
{
   public:
    explicit Menu(std::string name);

    const std::string&             name() const;
    Menu&                          add(MenuAction action);
    void                           add_separator();
    const std::vector<MenuAction>& items() const;
    std::vector<MenuAction>&       items_mut();
    void                           remove_submenu(std::string_view label);

   private:
    std::string             name_;
    std::vector<MenuAction> items_;
};

class PanelRegistry;

class MenuBar
{
   public:
    Menu&                    menu(std::string_view name);
    bool                     has_menu(std::string_view name) const;
    std::vector<std::string> menu_names() const;

    // Builds a checkable submenu from a SNAPSHOT of the registry's current panels.
    // The PanelRegistry must outlive this MenuBar and stay valid while menu callbacks
    // run. Panels registered after this call do not appear until bind_panel_registry
    // is called again.
    void bind_panel_registry(PanelRegistry&   registry,
                             std::string_view top_level     = "View",
                             std::string_view submenu_label = "Panels");

    // Renders top-level menus in the current ImGui window (command bar style).
    // Each menu is passed to draw_fn for custom rendering (e.g. hover-switch popups).
    void draw_inline(
        const std::function<void(const char* label, const std::vector<MenuAction>& items)>& draw_fn)
        const;

    void set_trailing_draw(std::function<void()> fn);

    // Vision command-bar frame + inline menus (requires bound ImGuiIntegration).
    void draw_command_bar(spectra::ImGuiIntegration* imgui);

    void draw();

   private:
    std::deque<Menu>      menus_;
    std::function<void()> trailing_draw_;
};

// Shared converter for Vision command-bar menu rendering.
std::vector<spectra::ImGuiIntegration::MenuItem> to_imgui_menu_items(
    const std::vector<MenuAction>& actions);
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
