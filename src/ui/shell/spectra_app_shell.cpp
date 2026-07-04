#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/spectra_app_shell.hpp"

    #include <spectra/figure.hpp>

    #include "imgui.h"
    #include "io/export_registry.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/input/input.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/overlay/custom_transform_dialog.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/overlay/knob_manager.hpp"
    #include "ui/settings/settings_panel.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/topics/topics_panel.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
namespace
{
using spectra::ui::Icon;

MenuAction make_toggle_action(const char*               label,
                              std::function<bool()>     checked,
                              std::function<void()>     on_click,
                              std::string               shortcut = {})
{
    MenuAction action;
    action.label    = label;
    action.shortcut = std::move(shortcut);
    action.checked  = std::move(checked);
    action.on_click = std::move(on_click);
    return action;
}
}   // namespace

// ─── SpectraCanvasHost ───────────────────────────────────────────────────────

SpectraCanvasHost::SpectraCanvasHost(spectra::ImGuiIntegration* imgui,
                                     spectra::LayoutManager*    lm)
    : CanvasHost(lm), imgui_(imgui)
{
}

void SpectraCanvasHost::draw()
{
    if (!imgui_)
        return;
    auto* shell = imgui_->app_shell();
    if (!shell)
        return;
    if (auto* fig = shell->current_figure())
        imgui_->draw_canvas_content(*fig);
}

// ─── SpectraNavRail ──────────────────────────────────────────────────────────

SpectraNavRail::SpectraNavRail(spectra::ImGuiIntegration* imgui, PanelRegistry* registry)
    : NavRail(registry), imgui_(imgui)
{
}

void SpectraNavRail::build_items(std::vector<NavItem>& out) const
{
    if (!imgui_)
        return;

    auto add_tool = [&](Icon icon, const char* label, ToolMode mode)
    {
        NavItem item;
        item.icon    = icon;
        item.label   = label;
        item.id      = label;
        item.tooltip = label;
        item.is_active = [imgui = imgui_, mode]()
        { return imgui->interaction_mode_ == mode; };
        item.on_click = [imgui = imgui_, mode]() { imgui->interaction_mode_ = mode; };
        out.push_back(std::move(item));
    };

    add_tool(Icon::MousePointer, "Select", ToolMode::Select);
    add_tool(Icon::Hand, "Pan", ToolMode::Pan);
    add_tool(Icon::ZoomIn, "Zoom", ToolMode::BoxZoom);

    NavItem sep1;
    sep1.is_section_header = true;
    sep1.label             = "##sep1";
    out.push_back(std::move(sep1));

    add_tool(Icon::Ruler, "Measure", ToolMode::Measure);
    add_tool(Icon::Comment, "Annotate", ToolMode::Annotate);
    add_tool(Icon::VectorSquare, "ROI", ToolMode::ROI);

    NavItem sep2;
    sep2.is_section_header = true;
    sep2.label             = "##sep2";
    out.push_back(std::move(sep2));

    {
        NavItem markers;
        markers.icon    = Icon::MapPin;
        markers.label   = "Markers";
        markers.id      = "markers";
        markers.tooltip = "Markers";
        markers.is_active = [imgui = imgui_]()
        {
            return imgui->data_interaction_ != nullptr
                   && !imgui->data_interaction_->markers().empty();
        };
        markers.on_click = [imgui = imgui_]()
        {
            if (imgui->data_interaction_)
                imgui->data_interaction_->clear_markers();
        };
        out.push_back(std::move(markers));
    }

    {
        NavItem xform;
        xform.icon    = Icon::MagicWand;
        xform.label   = "Transform";
        xform.id      = "transform";
        xform.tooltip = "Transform";
        xform.is_active = [imgui = imgui_]() { return imgui->custom_transform_dialog_.is_open(); };
        xform.on_click  = [imgui = imgui_]()
        {
            if (!imgui->custom_transform_dialog_.is_open())
            {
                imgui->custom_transform_dialog_.set_fonts(imgui->font_body_,
                                                          imgui->font_heading_,
                                                          imgui->font_title_);
                imgui->custom_transform_dialog_.open(imgui->current_figure_);
            }
        };
        out.push_back(std::move(xform));
    }

    NavItem sep3;
    sep3.is_section_header = true;
    sep3.label             = "##sep3";
    out.push_back(std::move(sep3));

    NavRail::build_items(out);

    if (auto* shell = imgui_->app_shell())
    {
        for (NavItem& item : out)
        {
            if (item.is_section_header || item.id.empty() || item.id.rfind("core.", 0) != 0)
                continue;

            // SettingsPanel gates its own draw() on an internal visible_ flag that is
            // independent of the PanelRegistry entry used for menu/nav bookkeeping, so
            // toggling the registry panel alone never opens the window. Drive the real
            // panel directly instead.
            if (item.id == "core.settings")
            {
                item.is_active = [imgui = imgui_]()
                { return imgui->settings_panel() && imgui->settings_panel()->is_visible(); };
                item.on_click = [imgui = imgui_]()
                {
                    if (auto* sp = imgui->settings_panel())
                        sp->set_visible(!sp->is_visible());
                };
                continue;
            }

            const std::string panel_id = item.id;
            item.is_active             = [shell, panel_id]()
            { return shell->panel_visible(panel_id); };
            item.on_click = [shell, panel_id]() { shell->toggle_panel(panel_id); };
        }
    }

    NavItem help;
    help.icon    = Icon::Help;
    help.label   = "Help";
    help.id      = "help";
    help.tooltip = "Help";
    help.is_active = []() { return false; };
    help.on_click  = [imgui = imgui_]()
    {
        if (imgui->command_registry())
            imgui->command_registry()->execute("help.show");
    };
    out.push_back(std::move(help));
}

void SpectraNavRail::draw()
{
    if (!imgui_ || !layout_manager_ || !imgui_->show_nav_rail_)
        return;

    const spectra::Rect bounds = layout_manager_->nav_rail_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoBackground;

    const float rail_w = bounds.w;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags))
    {
        {
            ImDrawList* dl            = ImGui::GetWindowDrawList();
            const float right_edge    = bounds.x + rail_w;
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
            const auto& c = ui::theme();
            dl->AddLine(ImVec2(right_edge - 1.0f, bounds.y),
                        ImVec2(right_edge - 1.0f, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.52f)),
                        1.0f);
        }

        ImFont* label_font = imgui_->font_heading_;
        const float btn_w  = rail_w;

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

        const float scale = LayoutManager::nav_rail_scale_for_height(
            bounds.h, button_count, sep_count);

        auto draw_separator = [&]()
        {
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * scale));
            const float sep_inset = ui::tokens::SPACE_4 * scale;
            ImVec2      p0        = ImVec2(ImGui::GetWindowPos().x + sep_inset,
                                  std::floor(ImGui::GetCursorScreenPos().y));
            ImVec2 p1 = ImVec2(ImGui::GetWindowPos().x + rail_w - sep_inset, p0.y);
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

        for (const NavItem& item : items)
        {
            if (item.is_section_header)
            {
                draw_separator();
                continue;
            }
            const bool active = item.is_active && item.is_active();
            if (imgui_->icon_label_button_rail(ui::icon_str(item.icon),
                                               item.label.c_str(),
                                               active,
                                               imgui_->font_icon_,
                                               label_font,
                                               btn_w,
                                               scale))
            {
                if (item.on_click)
                    item.on_click();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);
}

// ─── SpectraAppShell ─────────────────────────────────────────────────────────

SpectraAppShell::SpectraAppShell(spectra::ImGuiIntegration* imgui) : imgui_(imgui)
{
    config_.dockspace  = false;
    config_.menu_bar   = false;
    config_.nav_rail   = false;
    config_.status_bar = false;
    config_.app_name   = "Spectra";
}

void SpectraAppShell::bind_imgui(spectra::ImGuiIntegration* imgui)
{
    imgui_ = imgui;
}

void SpectraAppShell::set_layout_manager(spectra::LayoutManager* lm)
{
    AppShell::set_layout_manager(lm);
    if (spectra_nav_)
        spectra_nav_->set_layout_manager(lm);
}

void SpectraAppShell::set_current_figure(spectra::Figure* figure)
{
    current_figure_ = figure;
}

void SpectraAppShell::ensure_initialized()
{
    if (!imgui_)
        return;

    if (!initialized_)
    {
        set_layout_manager(&imgui_->get_layout_manager());
        spectra_nav_ = std::make_unique<SpectraNavRail>(imgui_, &panels_);
        spectra_nav_->set_layout_manager(layout_manager_);
        nav_rail_.set_registry(&panels_);
        initialize();
    }

    if (!spectra_canvas_)
        spectra_canvas_ = dynamic_cast<SpectraCanvasHost*>(&canvas_host());
}

void SpectraAppShell::sync_before_frame()
{
    ensure_initialized();
    sync_panel_state_from_imgui();
    sync_file_menu();
}

SpectraNavRail& SpectraAppShell::spectra_nav_rail()
{
    ensure_initialized();
    return *spectra_nav_;
}

const SpectraNavRail& SpectraAppShell::spectra_nav_rail() const
{
    return *spectra_nav_;
}

SpectraCanvasHost& SpectraAppShell::spectra_canvas_host()
{
    ensure_initialized();
    return *spectra_canvas_;
}

bool SpectraAppShell::panel_visible(std::string_view id) const
{
    const Panel* panel = panels_.find(id);
    return panel && panel->visible();
}

bool SpectraAppShell::set_panel_visible(std::string_view id, bool visible)
{
    if (!panels_.set_visible(id, visible))
        return false;
    sync_panel_state_to_imgui();
    return true;
}

bool SpectraAppShell::toggle_panel(std::string_view id)
{
    if (!panels_.toggle(id))
        return false;
    sync_panel_state_to_imgui();
    return true;
}

std::map<std::string, bool> SpectraAppShell::capture_panel_visibility() const
{
    return panels_.capture_visibility();
}

void SpectraAppShell::apply_panel_visibility(const std::map<std::string, bool>& vis)
{
    panels_.apply_visibility(vis);
    sync_panel_state_to_imgui();
}

void SpectraAppShell::draw_nav_rail()
{
    ensure_initialized();
    if (spectra_nav_)
        spectra_nav_->draw();
}

void SpectraAppShell::draw_status_bar()
{
    ensure_initialized();
    sync_status_bar_segments();
    status_bar_.draw();
}

void SpectraAppShell::draw_menus_inline()
{
    if (!imgui_)
        return;

    menu_bar_.draw_inline(
        [this](const char* label, const std::vector<MenuAction>& items)
        {
            const auto menu_items = to_imgui_menu_items(items);
            imgui_->render_menubar_menu(label, menu_items);
        });
}

void SpectraAppShell::draw_registered_panels()
{
    if (!imgui_ || !current_figure_)
        return;
    panels_.draw_all();
}

void SpectraAppShell::sync_status_bar_segments()
{
    status_bar_.clear();
    if (imgui_)
        imgui_->populate_status_bar(status_bar_);
}

void SpectraAppShell::sync_panel_state_from_imgui()
{
    if (!imgui_)
        return;

    auto& lm = imgui_->get_layout_manager();
    if (auto* p = panels_.find("core.inspector"))
        p->set_visible(lm.is_inspector_visible());
    if (auto* p = panels_.find("core.timeline"))
        p->set_visible(imgui_->show_timeline_);
    if (auto* p = panels_.find("core.curve_editor"))
        p->set_visible(imgui_->show_curve_editor_);
    if (auto* p = panels_.find("core.plugins"))
        p->set_visible(imgui_->show_plugins_panel_);
    if (auto* p = panels_.find("core.topics"))
        p->set_visible(imgui_->topics_panel_ && imgui_->topics_panel_->is_visible());
}

void SpectraAppShell::sync_panel_state_to_imgui()
{
    if (!imgui_)
        return;

    auto& lm = imgui_->get_layout_manager();
    if (const Panel* p = panels_.find("core.inspector"))
    {
        lm.set_inspector_visible(p->visible());
        imgui_->panel_open_ = p->visible();
    }
    if (const Panel* p = panels_.find("core.timeline"))
        imgui_->show_timeline_ = p->visible();
    if (const Panel* p = panels_.find("core.curve_editor"))
        imgui_->show_curve_editor_ = p->visible();
    if (const Panel* p = panels_.find("core.plugins"))
        imgui_->show_plugins_panel_ = p->visible();
    if (const Panel* p = panels_.find("core.topics"))
    {
        if (imgui_->topics_panel_)
            imgui_->topics_panel_->set_visible(p->visible());
    }
}

std::unique_ptr<CanvasHost> SpectraAppShell::create_canvas_host()
{
    return std::make_unique<SpectraCanvasHost>(imgui_, layout_manager_);
}

void SpectraAppShell::on_register_panels()
{
    if (!imgui_)
        return;

    auto add = [this](const char* id,
                      const char* title,
                      const char* category,
                      Icon        icon,
                      DockSlot    slot,
                      bool        default_visible,
                      auto        draw_fn)
    {
        PanelInfo info;
        info.id              = id;
        info.title           = title;
        info.category        = category;
        info.icon            = icon;
        info.slot            = slot;
        info.default_visible = default_visible;
        panels().add(std::make_unique<CallbackPanel>(std::move(info), std::move(draw_fn)));
    };

    add("core.inspector",
        "Inspector",
        "Panels",
        Icon::Axes,
        DockSlot::Right,
        true,
        [this](bool* p_open)
        {
            if (!imgui_ || !current_figure_)
                return;
            if (p_open && !*p_open)
            {
                imgui_->get_layout_manager().set_inspector_visible(false);
                imgui_->panel_open_ = false;
                return;
            }
            imgui_->draw_inspector(*current_figure_);
        });

    add("core.timeline",
        "Timeline",
        "Panels",
        Icon::Timeline,
        DockSlot::Bottom,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_timeline_ = false;
            if (imgui_->show_timeline_ && imgui_->timeline_editor_)
                imgui_->draw_timeline_panel();
        });

    add("core.curve_editor",
        "Curve Editor",
        "Panels",
        Icon::ChartLine,
        DockSlot::Floating,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_curve_editor_ = false;
            if (imgui_->show_curve_editor_ && imgui_->curve_editor_)
                imgui_->draw_curve_editor_panel();
        });

    add("core.plugins",
        "Plugins",
        "Panels",
        Icon::Wrench,
        DockSlot::Floating,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_plugins_panel_ = false;
            imgui_->draw_plugins_panel();
        });

    add("core.topics",
        "Topics",
        "Panels",
        Icon::Broadcast,
        DockSlot::Floating,
        false,
        [this](bool* /*p_open*/)
        {
            if (imgui_->topics_panel_)
                imgui_->topics_panel_->draw();
        });

    add("core.settings",
        "Settings",
        "Panels",
        Icon::Settings,
        DockSlot::Floating,
        false,
        [this](bool* /*p_open*/)
        {
            if (imgui_->settings_panel_)
                imgui_->settings_panel_->draw();
        });
}

void SpectraAppShell::on_populate_menus(MenuBar& bar)
{
    if (!imgui_)
        return;

    auto& file = bar.menu("File");
    file.add({.label = "New Figure", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("figure.new");
              }});
    file.add_separator();
    file.add({.label = "Export PNG", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.export_png");
              }});
    file.add({.label = "Export SVG", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.export_svg");
              }});
    file.add({.label    = "Copy as Image",
              .shortcut = "Ctrl+Shift+C",
              .on_click = [this]()
              {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.copy_to_clipboard");
              }});
    file.add({.label = "Save Workspace", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.save_workspace");
              }});
    file.add({.label = "Load Workspace", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.load_workspace");
              }});
    file.add_separator();
    file.add({.label = "Save Figure...", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.save_figure");
              }});
    file.add({.label = "Load Figure...", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("file.load_figure");
              }});
    file.add_separator();
    file.add({.label = "Exit", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("app.cancel");
              }});

    auto& edit = bar.menu("Edit");
    edit.add({.label = "Undo", .shortcut = "Ctrl+Z", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("edit.undo");
              }});
    edit.add({.label = "Redo", .shortcut = "Ctrl+Y", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("edit.redo");
              }});

    auto& view = bar.menu("View");
    view.add(make_toggle_action(
        "Toggle Inspector",
        [this]()
        {
            const Panel* p = panels_.find("core.inspector");
            return p && p->visible();
        },
        [this]() { toggle_panel("core.inspector"); }));
    view.add(make_toggle_action(
        "Toggle Navigation Rail",
        [this]() { return imgui_->show_nav_rail_; },
        [this]() { imgui_->set_nav_rail_visible(!imgui_->show_nav_rail_); }));
    view.add({.label = "Zoom to Fit", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("view.autofit");
              }});
    view.add({.label = "Reset View", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("view.reset");
              }});
    view.add({.label = "Toggle Grid", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("view.toggle_grid");
              }});
    view.add({.label = "Toggle Legend", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("view.toggle_legend");
              }});
    view.add({.label = "Remove All Data Tips", .on_click = [this]() {
                  if (imgui_->data_interaction_)
                      imgui_->data_interaction_->clear_markers();
              }});
    view.add_separator();
    view.add(make_toggle_action(
        "Toggle Timeline",
        [this]()
        {
            const Panel* p = panels_.find("core.timeline");
            return p && p->visible();
        },
        [this]() { toggle_panel("core.timeline"); }));
    view.add(make_toggle_action(
        "Toggle Curve Editor",
        [this]()
        {
            const Panel* p = panels_.find("core.curve_editor");
            return p && p->visible();
        },
        [this]() { toggle_panel("core.curve_editor"); }));
    view.add({.label = "Toggle Parameters", .on_click = [this]() {
                  if (imgui_->knob_manager_ && !imgui_->knob_manager_->empty())
                      imgui_->knob_manager_->set_visible(!imgui_->knob_manager_->is_visible());
              }});
    view.add({.label = "Toggle Data Editor", .on_click = [this]() { toggle_panel("core.inspector"); }});
    view.add(make_toggle_action(
        "Toggle Topics",
        [this]()
        {
            const Panel* p = panels_.find("core.topics");
            return p && p->visible();
        },
        [this]() { toggle_panel("core.topics"); }));
    view.add(make_toggle_action(
        "Plugins...",
        [this]()
        {
            const Panel* p = panels_.find("core.plugins");
            return p && p->visible();
        },
        [this]() { toggle_panel("core.plugins"); }));

    auto& tools = bar.menu("Tools");
    tools.add({.label = "Screenshot (PNG)", .on_click = [this]() {
                   if (imgui_->command_registry_)
                       imgui_->command_registry_->execute("file.export_png");
               }});
    tools.add_separator();
    tools.add({.label = "Theme Settings", .on_click = [this]() {
                   imgui_->show_theme_settings_ = !imgui_->show_theme_settings_;
               }});
    tools.add({.label = "Command Palette", .on_click = [this]() {
                   if (imgui_->command_registry_)
                       imgui_->command_registry_->execute("app.command_palette");
               }});

    auto& plot = bar.menu("Plot");
    plot.add({.label = "Y = 0 Line", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("plot.hline_zero");
              }});
    plot.add({.label = "X = 0 Line", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("plot.vline_zero");
              }});
    plot.add_separator();
    plot.add({.label = "Horizontal Line...", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("plot.hline");
              }});
    plot.add({.label = "Vertical Line...", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("plot.vline");
              }});
    plot.add({.label = "Plot Function...", .on_click = [this]() {
                  if (imgui_->command_registry_)
                      imgui_->command_registry_->execute("plot.function");
              }});

    menus_populated_ = true;
}

void SpectraAppShell::sync_file_menu()
{
    if (!imgui_ || !menus_populated_)
        return;

    auto& file = menu_bar_.menu("File");
    file.remove_submenu("##plugin_exports");
    if (!imgui_->export_format_registry_)
        return;

    const auto formats = imgui_->export_format_registry_->available_formats();
    if (formats.empty())
        return;

    MenuAction plugin_parent;
    plugin_parent.label = "##plugin_exports";
    for (const auto& fmt : formats)
    {
        const std::string label = "Export " + fmt.name + " (." + fmt.extension + ")";
        const std::string name  = fmt.name;
        plugin_parent.submenu.push_back({.label = label, .on_click = [this, name]() {
                                             if (imgui_->command_registry_)
                                                 imgui_->command_registry_->execute(
                                                     "file.export_plugin." + name);
                                         }});
    }
    file.add(std::move(plugin_parent));
}

void SpectraAppShell::on_populate_nav_rail(NavRail& /*rail*/) {}

void SpectraAppShell::on_build_status_bar(StatusBar& /*bar*/) {}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
