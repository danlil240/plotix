#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/app_shell.hpp"

    #include "imgui.h"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/shell/shell_style.hpp"

namespace spectra::ui::shell
{
AppShell::AppShell(AppShellConfig cfg) : config_(std::move(cfg)) {}

AppShell::~AppShell() = default;

void AppShell::set_layout_manager(spectra::LayoutManager* lm)
{
    layout_manager_ = lm;
    status_bar_.set_layout_manager(lm);
    nav_rail_.set_layout_manager(lm);
    if (canvas_host_)
        canvas_host_->set_layout_manager(lm);
}

spectra::LayoutManager* AppShell::layout_manager() const
{
    return layout_manager_;
}

void AppShell::set_chrome_integration(spectra::ImGuiIntegration* imgui)
{
    imgui_chrome_ = imgui;
    nav_rail_.set_chrome(imgui);
}

void AppShell::initialize()
{
    if (initialized_)
        return;

    if (!canvas_host_)
        canvas_host_ = create_canvas_host();

    on_register_panels();
    nav_rail_.set_registry(&panels_);
    on_populate_nav_rail(nav_rail_);
    on_populate_menus(menu_bar_);
    menu_bar_.bind_panel_registry(panels_);
    on_build_status_bar(status_bar_);
    initialized_ = true;
}

void AppShell::draw_frame()
{
    if (config_.menu_bar)
    {
        if (imgui_chrome_)
            menu_bar_.draw_command_bar(imgui_chrome_);
        else
            menu_bar_.draw();
    }

    if (config_.nav_rail)
        nav_rail_.draw();

    if (config_.dockspace)
        draw_dockspace();
    else if (canvas_host_)
        draw_canvas_host_window();

    panels_.draw_all();

    if (config_.status_bar)
        status_bar_.draw();

    on_draw_canvas();
}

PanelRegistry& AppShell::panels()
{
    return panels_;
}

const PanelRegistry& AppShell::panels() const
{
    return panels_;
}

NavRail& AppShell::nav_rail()
{
    return nav_rail_;
}

const NavRail& AppShell::nav_rail() const
{
    return nav_rail_;
}

MenuBar& AppShell::menu_bar()
{
    return menu_bar_;
}

StatusBar& AppShell::status_bar()
{
    return status_bar_;
}

CanvasHost& AppShell::canvas_host()
{
    if (!canvas_host_)
        canvas_host_ = create_canvas_host();
    return *canvas_host_;
}

std::unique_ptr<CanvasHost> AppShell::create_canvas_host()
{
    return std::make_unique<CanvasHost>(layout_manager_);
}

void AppShell::draw_canvas_host_window()
{
    Rect workspace{};
    if (layout_manager_)
    {
        workspace = layout_manager_->workspace_rect();
    }
    else
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        workspace.x             = vp->WorkPos.x;
        workspace.y             = vp->WorkPos.y;
        workspace.w             = vp->WorkSize.x;
        workspace.h             = vp->WorkSize.y;
    }

    ImGui::SetNextWindowPos(ImVec2(workspace.x, workspace.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(160.0f, workspace.w), std::max(120.0f, workspace.h)),
                             ImGuiCond_Always);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                  | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoSavedSettings
                                  | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##AppShellCanvasHost", nullptr, host_flags))
    {
        if (canvas_host_)
            canvas_host_->draw();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void AppShell::draw_dockspace()
{
    #ifdef IMGUI_HAS_DOCK
    Rect workspace{};
    if (layout_manager_)
    {
        workspace = layout_manager_->workspace_rect();
    }
    else
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        workspace.x             = vp->WorkPos.x;
        workspace.y             = vp->WorkPos.y;
        workspace.w             = vp->WorkSize.x;
        workspace.h             = vp->WorkSize.y;
    }

    ImGui::SetNextWindowPos(ImVec2(workspace.x, workspace.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(160.0f, workspace.w), std::max(120.0f, workspace.h)),
                             ImGuiCond_Always);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                  | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                                  | ImGuiWindowFlags_NoDocking;

    const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    push_dock_style();
    if (ImGui::Begin("##AppShellDockHost", nullptr, host_flags))
    {
        dockspace_id_ = ImGui::GetID("##AppShellDockSpace");

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), dock_flags);
        ImGui::PopStyleColor();

        if (!dock_layout_initialized_)
        {
            on_default_layout(dockspace_id_);
            dock_layout_initialized_ = true;
        }

        if (canvas_host_)
            canvas_host_->draw();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    pop_dock_style();
    #else
    dock_layout_initialized_ = true;
    if (canvas_host_)
        canvas_host_->draw();
    #endif
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
