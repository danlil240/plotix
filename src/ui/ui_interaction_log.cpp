#include "ui_interaction_log.hpp"

#include <spectra/logger.hpp>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_internal.h>
#endif

namespace spectra::ui
{
namespace
{

std::string sanitize_id(std::string_view id)
{
    std::string out;
    out.reserve(id.size());
    for (char ch : id)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
            out.push_back('_');
        else
            out.push_back(ch);
    }
    if (out.empty())
        out = "-";
    return out;
}

}   // namespace

void log_ui_action(std::string_view kind,
                   std::string_view id,
                   std::string_view result,
                   std::string_view detail)
{
    const std::string safe_id = sanitize_id(id);
    if (detail.empty())
    {
        SPECTRA_LOG_DEBUG("ui.action", "kind={} id={} result={}", kind, safe_id, result);
    }
    else
    {
        SPECTRA_LOG_DEBUG("ui.action",
                          "kind={} id={} result={} detail={}",
                          kind,
                          safe_id,
                          result,
                          detail);
    }
}

#ifdef SPECTRA_USE_IMGUI
void log_imgui_frame_activations()
{
    ImGuiContext& g            = *GImGui;
    const bool    mouse_click  = g.IO.MouseClicked[0] || g.IO.MouseClicked[1];
    const bool    nav_activate = g.NavActivateId != 0;
    if (!mouse_click && !nav_activate)
        return;

    const ImGuiID id = g.ActiveId != 0 ? g.ActiveId : g.NavActivateId;
    if (id == 0)
        return;

    const char* window =
        (g.ActiveIdWindow && g.ActiveIdWindow->Name) ? g.ActiveIdWindow->Name : "unknown";
    log_ui_action("imgui", std::to_string(id), "ok", window);
}
#endif

}   // namespace spectra::ui
