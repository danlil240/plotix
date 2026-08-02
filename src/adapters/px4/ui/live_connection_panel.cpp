#include "live_connection_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #ifdef IMGUI_HAS_DOCK
        #include <imgui_internal.h>
        #include <cmath>
    #endif
#endif

#include <format>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LiveConnectionPanel::LiveConnectionPanel(Px4Bridge& bridge, Px4PlotManager& plot_mgr)
    : bridge_(bridge), plot_mgr_(plot_mgr)
{
    detach_ctrl_.set_title("PX4 Live");
    detach_ctrl_.set_detached_size(450.0f, 350.0f);
    detach_ctrl_.set_draw_callback([this]() { draw_content(); });
}

LiveConnectionPanel::~LiveConnectionPanel() = default;

// ---------------------------------------------------------------------------
// draw — the panel owns all ImGui rendering; controller is pure state machine
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    // Let controller detect OS window close.
    detach_ctrl_.update();

    auto state = detach_ctrl_.state();

    // Detached — content rendered by session_runtime in the OS window.
    if (state == spectra::PanelDetachController::State::Detached)
        return;

    // DetachPending — skip one frame so the dock slot is released cleanly.
    if (state == spectra::PanelDetachController::State::DetachPending)
        return;

    // After re-attach: force back into the target dockspace.
    #ifdef IMGUI_HAS_DOCK
    if (state == spectra::PanelDetachController::State::AttachPending
        && detach_ctrl_.dock_id() != 0)
    {
        ImGui::SetNextWindowDockID(static_cast<ImGuiID>(detach_ctrl_.dock_id()), ImGuiCond_Always);
    }
    #endif

    if (!ImGui::Begin(detach_ctrl_.title().c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    // ── Drag-to-detach: detect ImGui undocking and replace with OS window ──
    // Let ImGui handle the drag natively. When it undocks the window
    // (transition from docked → floating), intercept it immediately
    // and create a real OS window via PanelDetachController instead.
    #ifdef IMGUI_HAS_DOCK
    bool currently_docked = ImGui::IsWindowDocked();
    if (was_docked_ && !currently_docked && detach_ctrl_.is_docked())
    {
        double sx = NAN;
        double sy = NAN;
        detach_ctrl_.get_screen_cursor(sx, sy);
        detach_ctrl_.set_screen_position(static_cast<int>(sx), static_cast<int>(sy));
        detach_ctrl_.detach();
        was_docked_ = false;
        ImGui::End();
        return;
    }
    was_docked_ = currently_docked;
    #endif

    draw_context_menu();
    draw_content();

    ImGui::End();
#else
    (void)p_open;
#endif
}

// ---------------------------------------------------------------------------
// draw_context_menu
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_context_menu()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginPopupContextWindow("##LivePanelDetach"))
    {
        if (detach_ctrl_.is_docked())
        {
            if (ImGui::MenuItem("Detach to Window"))
                detach_ctrl_.detach();
        }
        else if (detach_ctrl_.is_detached())
        {
            if (ImGui::MenuItem("Attach to Dockspace"))
                detach_ctrl_.attach();
        }
        ImGui::EndPopup();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_content()
{
#ifdef SPECTRA_USE_IMGUI
    draw_connection_controls();
    ImGui::Separator();
    draw_status();
    ImGui::Separator();
    draw_channel_list();
#endif
}

// ---------------------------------------------------------------------------
// draw_connection_controls
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_connection_controls()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::Text("MAVLink UDP Connection");

    ImGui::InputText("Host", host_buf_, sizeof(host_buf_));
    ImGui::InputInt("Port", &port_);

    if (port_ < 1)
        port_ = 1;
    if (port_ > 65535)
        port_ = 65535;

    if (!bridge_.is_connected())
    {
        if (ImGui::Button("Connect"))
        {
            bridge_.init(host_buf_, static_cast<uint16_t>(port_));
            bridge_.start();
        }
    }
    else
    {
        if (ImGui::Button("Disconnect"))
        {
            bridge_.shutdown();
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_status
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_status()
{
#ifdef SPECTRA_USE_IMGUI
    auto state = bridge_.state();
    ImGui::Text("State: %s", bridge_state_name(state));
    ImGui::Text("Messages: %lu", static_cast<unsigned long>(bridge_.total_messages()));
    ImGui::Text("Rate: %.1f msg/s", bridge_.message_rate());

    if (!bridge_.last_error().empty())
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", bridge_.last_error().c_str());
#endif
}

// ---------------------------------------------------------------------------
// draw_channel_list
// ---------------------------------------------------------------------------

void LiveConnectionPanel::draw_channel_list()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::Text("Active Channels");
    ImGui::BeginChild("Channels", ImVec2(0, 0), 1);

    auto channels = bridge_.channel_names();
    for (auto& ch_name : channels)
    {
        auto latest = bridge_.channel_latest(ch_name);
        if (!latest)
            continue;

        const std::string label = std::format("{} ({} fields)", ch_name, latest->fields.size());

        if (ImGui::TreeNode(label.c_str()))
        {
            for (auto& field : latest->fields)
            {
                const std::string field_label = std::format("{} = {:.4f}", field.name, field.value);

                if (ImGui::Selectable(field_label.c_str(),
                                      false,
                                      ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        plot_mgr_.add_live_field(ch_name, field.name);
                    }
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
#endif
}

}   // namespace spectra::adapters::px4
