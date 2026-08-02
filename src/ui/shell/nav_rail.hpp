#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <string>
    #include <string_view>
    #include <vector>
    #include "ui/theme/icons.hpp"

namespace spectra
{
class LayoutManager;
class ImGuiIntegration;
}   // namespace spectra

namespace spectra::ui::shell
{
class PanelRegistry;

struct NavItem
{
    Icon                  icon = Icon::WindowIcon;
    std::string           label;
    std::string           id;   // stable ImGui id (panel id); empty for section headers
    std::string           tooltip;
    std::string           section;
    bool                  is_section_header = false;
    std::function<bool()> is_active;
    std::function<void()> on_click;
};

class NavRail
{
   public:
    // The PanelRegistry must outlive this NavRail and remain valid while nav callbacks run.
    explicit NavRail(PanelRegistry* registry = nullptr);
    virtual ~NavRail() = default;

    NavRail(const NavRail&)            = delete;
    NavRail& operator=(const NavRail&) = delete;

    // The PanelRegistry must outlive this NavRail and remain valid while nav callbacks run.
    void           set_registry(PanelRegistry* r);
    PanelRegistry* registry() const;

    void set_expanded(bool e);
    bool expanded() const;

    void set_search_enabled(bool e);
    bool search_enabled() const;

    void               set_search(std::string text);
    const std::string& search() const;

    void set_layout_manager(spectra::LayoutManager* lm);

    // When set, draw() uses Vision icon+label rail styling via ImGuiIntegration.
    void set_chrome(spectra::ImGuiIntegration* imgui);

    // When false, build_items() omits registry panel toggles (curated rail mode).
    void set_show_registry_panels(bool show);
    bool show_registry_panels() const { return show_registry_panels_; }

    virtual void build_items(std::vector<NavItem>& out) const;

    void add_custom_item(NavItem item);
    void clear_custom_items();

    virtual void draw();

   protected:
    static bool matches_filter(std::string_view title, std::string_view filter);

    PanelRegistry*             registry_       = nullptr;
    spectra::LayoutManager*    layout_manager_ = nullptr;
    spectra::ImGuiIntegration* imgui_chrome_   = nullptr;
    std::vector<NavItem>       custom_items_;
    bool                       expanded_             = false;
    bool                       search_enabled_       = false;
    bool                       show_registry_panels_ = true;
    std::string                search_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
