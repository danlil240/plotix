// qt_workspace_bridge.cpp — QtWorkspaceBridge implementation.

#include "qt_workspace_bridge.hpp"

#include "docking/main_window_registry.hpp"
#include "docking/native_qt_docking_host.hpp"
#include "docking/docking_host.hpp"

#include "ui/workspace/workspace.hpp"

namespace spectra::adapters::qt
{

QtWorkspaceBridge::QtWorkspaceBridge(MainWindowRegistry* registry)
    : registry_(registry)
{
}

void QtWorkspaceBridge::capture_layout(WorkspaceData& data) const
{
    if (!registry_)
        return;

    data.desktop_layout.provider        = provider_;
    data.desktop_layout.provider_version = "1.0";

    auto host_ids = registry_->all_hosts();
    bool first    = true;

    for (HostId hid : host_ids)
    {
        auto* host = registry_->native_host(hid);
        if (!host)
            continue;

        DockLayoutState layout = host->save_layout();

        if (first)
        {
            // First window is the main window — store its state directly.
            if (!layout.windows.empty())
            {
                data.desktop_layout.main_window_state_base64    = layout.windows[0].state_base64;
                data.desktop_layout.main_window_geometry_base64 = layout.windows[0].geometry_base64;
            }
            first = false;
        }
        else
        {
            // Additional/detached windows.
            for (const auto& ws : layout.windows)
            {
                WorkspaceData::DesktopLayoutState::WindowState dws;
                dws.state_base64    = ws.state_base64;
                dws.geometry_base64 = ws.geometry_base64;
                dws.title           = ws.title;
                data.desktop_layout.windows.push_back(std::move(dws));
            }
        }
    }
}

bool QtWorkspaceBridge::apply_layout(const WorkspaceData& data) const
{
    if (!registry_)
        return false;

    // Provider mismatch: degrade safely — keep figures but don't restore layout.
    if (!data.desktop_layout.provider.empty()
        && data.desktop_layout.provider != provider_)
    {
        return false;
    }

    bool any_applied = false;

    // Restore main window (first host).
    auto host_ids = registry_->all_hosts();
    if (host_ids.empty())
        return false;

    auto* main_host = registry_->native_host(host_ids[0]);
    if (main_host)
    {
        DockLayoutState layout;
        layout.provider         = provider_;
        layout.provider_version = "1.0";

        DockLayoutState::DockWindowState ws;
        ws.host_id          = host_ids[0];
        ws.state_base64     = data.desktop_layout.main_window_state_base64;
        ws.geometry_base64  = data.desktop_layout.main_window_geometry_base64;
        ws.title            = "Spectra";
        layout.windows.push_back(ws);

        if (main_host->restore_layout(layout))
            any_applied = true;
    }

    // Restore detached windows.
    // For each saved window state beyond the main window, create a new
    // detached window and restore its layout.
    for (const auto& saved_ws : data.desktop_layout.windows)
    {
        HostId new_id = registry_->create_detached_window();
        if (new_id == INVALID_HOST_ID)
            continue;

        auto* new_host = registry_->native_host(new_id);
        if (!new_host)
            continue;

        DockLayoutState layout;
        layout.provider         = provider_;
        layout.provider_version = "1.0";

        DockLayoutState::DockWindowState ws;
        ws.host_id          = new_id;
        ws.state_base64     = saved_ws.state_base64;
        ws.geometry_base64  = saved_ws.geometry_base64;
        ws.title            = saved_ws.title;
        layout.windows.push_back(ws);

        if (new_host->restore_layout(layout))
            any_applied = true;

        if (!saved_ws.title.empty())
            new_host->set_title(saved_ws.title);
    }

    return any_applied;
}

}   // namespace spectra::adapters::qt
