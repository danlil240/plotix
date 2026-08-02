#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/nav_rail.hpp"

    #include <algorithm>
    #include <cctype>

    #include "imgui.h"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/imgui/widgets.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/shell/panel_registry.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
namespace
{
std::string to_lower_ascii(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}
}   // namespace

NavRail::NavRail(PanelRegistry* registry) : registry_(registry) {}

void NavRail::set_registry(PanelRegistry* r)
{
    registry_ = r;
}

PanelRegistry* NavRail::registry() const
{
    return registry_;
}

void NavRail::set_expanded(bool e)
{
    expanded_ = e;
}

bool NavRail::expanded() const
{
    return expanded_;
}

void NavRail::set_search_enabled(bool e)
{
    search_enabled_ = e;
}

bool NavRail::search_enabled() const
{
    return search_enabled_;
}

void NavRail::set_search(std::string text)
{
    search_ = std::move(text);
}

const std::string& NavRail::search() const
{
    return search_;
}

void NavRail::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
}

void NavRail::set_chrome(spectra::ImGuiIntegration* imgui)
{
    imgui_chrome_ = imgui;
}

void NavRail::set_show_registry_panels(bool show)
{
    show_registry_panels_ = show;
}

bool NavRail::matches_filter(std::string_view title, std::string_view filter)
{
    if (filter.empty())
        return true;

    const std::string title_lower  = to_lower_ascii(title);
    const std::string filter_lower = to_lower_ascii(filter);
    return title_lower.find(filter_lower) != std::string::npos;
}

void NavRail::add_custom_item(NavItem item)
{
    custom_items_.push_back(std::move(item));
}

void NavRail::clear_custom_items()
{
    custom_items_.clear();
}

void NavRail::build_items(std::vector<NavItem>& out) const
{
    for (const NavItem& item : custom_items_)
        out.push_back(item);

    PanelRegistry* reg = registry_;
    if (!reg)
        return;

    const bool include_registry = show_registry_panels_ || !search_.empty();
    if (!include_registry)
        return;

    for (const std::string& category : reg->categories())
    {
        const std::vector<Panel*> panels = reg->in_category(category);

        bool category_has_match = search_.empty();
        if (!category_has_match)
        {
            for (const Panel* panel : panels)
            {
                if (matches_filter(panel->title(), search_))
                {
                    category_has_match = true;
                    break;
                }
            }
        }

        if (!category_has_match)
            continue;

        NavItem header;
        header.label             = category;
        header.section           = category;
        header.is_section_header = true;
        out.push_back(std::move(header));

        for (Panel* panel : panels)
        {
            if (!matches_filter(panel->title(), search_))
                continue;

            const std::string id = panel->id();

            NavItem item;
            item.icon      = panel->icon();
            item.label     = panel->title();
            item.id        = id;
            item.tooltip   = panel->title();
            item.section   = category;
            item.is_active = [reg, id]()
            {
                Panel* p = reg->find(id);
                return p && p->visible();
            };
            item.on_click = [reg, id]() { reg->toggle(id); };
            out.push_back(std::move(item));
        }
    }
}

void NavRail::draw()
{
    if (layout_manager_ && !layout_manager_->is_nav_rail_visible())
        return;

    const auto draw_content = [this]()
    {
        if (search_enabled_)
        {
            char buf[256] = {};
            if (!search_.empty())
                std::copy_n(search_.begin(), std::min(search_.size(), sizeof(buf) - 1), buf);
            if (ImGui::InputText("##nav_rail_search", buf, sizeof(buf)))
                search_ = buf;
        }

        std::vector<NavItem> items;
        build_items(items);

        int button_count = 0;
        int sep_count    = 0;
        for (const NavItem& item : items)
        {
            if (item.is_section_header)
                ++sep_count;
            else
                ++button_count;
        }

        const float scale = layout_manager_ ? LayoutManager::nav_rail_scale_for_height(
                                                  layout_manager_->nav_rail_rect().h,
                                                  button_count,
                                                  sep_count)
                                            : 1.0f;

        auto draw_separator = [&]()
        {
            if (!layout_manager_)
                return;
            const spectra::Rect bounds = layout_manager_->nav_rail_rect();
            const float         rail_w = bounds.w;
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * scale));
            const float sep_inset = ui::tokens::SPACE_4 * scale;
            ImVec2      p0        = ImVec2(ImGui::GetWindowPos().x + sep_inset,
                               std::floor(ImGui::GetCursorScreenPos().y));
            ImVec2      p1        = ImVec2(ImGui::GetWindowPos().x + rail_w - sep_inset, p0.y);
            ImGui::GetWindowDrawList()->AddLine(
                p0,
                p1,
                IM_COL32(static_cast<int>(ui::theme().border_subtle.r * 255),
                         static_cast<int>(ui::theme().border_subtle.g * 255),
                         static_cast<int>(ui::theme().border_subtle.b * 255),
                         32),
                1.0f);
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * scale));
        };

        static bool section_open = true;
        const float btn_w        = layout_manager_ ? layout_manager_->nav_rail_rect().w : 48.0f;

        for (const NavItem& item : items)
        {
            if (item.is_section_header)
            {
                if (imgui_chrome_)
                {
                    draw_separator();
                }
                else if (expanded_)
                {
                    widgets::section_header(item.label.c_str(), &section_open);
                }
                continue;
            }

            const char* cmd_id  = item.id.empty() ? item.label.c_str() : item.id.c_str();
            const bool  active  = item.is_active && item.is_active();
            bool        clicked = false;

            if (imgui_chrome_)
            {
                clicked = imgui_chrome_->icon_label_button_rail(ui::icon_str(item.icon),
                                                                item.label.c_str(),
                                                                active,
                                                                imgui_chrome_->icon_font(),
                                                                imgui_chrome_->heading_font(),
                                                                btn_w,
                                                                scale);
            }
            else
            {
                clicked = widgets::icon_button(cmd_id, item.icon, item.tooltip.c_str(), active);

                if (expanded_)
                    ImGui::SameLine();

                if (expanded_)
                    ImGui::TextUnformatted(item.label.c_str());
            }

            if (clicked && item.on_click)
                item.on_click();
        }
    };

    if (!layout_manager_)
    {
        draw_content();
        return;
    }

    const spectra::Rect bounds = layout_manager_->nav_rail_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    const auto& theme = ui::theme();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##AppShellNavRail", nullptr, flags))
    {
        ImDrawList* dl         = ImGui::GetWindowDrawList();
        const float right_edge = bounds.x + bounds.w;
        if (imgui_chrome_)
        {
            const float shadow_spread = ui::tokens::ELEVATION_1_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                const float t     = static_cast<float>(i) / 4.0f;
                const float alpha = 0.09f * (1.0f - t);
                const float off   = shadow_spread * t;
                dl->AddRectFilled(ImVec2(right_edge, bounds.y),
                                  ImVec2(right_edge + off + 1.0f, bounds.y + bounds.h),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }
        }
        dl->AddLine(
            ImVec2(right_edge - 1.0f, bounds.y),
            ImVec2(right_edge - 1.0f, bounds.y + bounds.h),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(theme.border_subtle.r, theme.border_subtle.g, theme.border_subtle.b, 0.52f)),
            1.0f);
        draw_content();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
