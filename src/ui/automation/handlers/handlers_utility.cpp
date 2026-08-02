// handlers_utility.cpp — ping, pump_frames, wait_frames handlers.

#include "../automation_handler.hpp"
#include "../automation_imgui_input.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include "ui/app/window_ui_context.hpp"

#include <sstream>

namespace spectra
{

std::vector<AutomationHandlerEntry> make_utility_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

    entries.push_back(automation_handler(
        "pump_frames",
        "Advance the application by rendering the specified number of frames.",
        AutomationContextFlag::None,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
        {
            int count = json_get_int(req.params_json, "count", 1);
            if (count < 1)
                count = 1;
            if (count > 600)
                count = 600;
            req.response_json = json_ok(req.id, "{\"pumped\":" + std::to_string(count) + "}");
        }));

    entries.push_back(
        automation_handler("wait_frames",
                           "Defer the response until N frames have elapsed.",
                           AutomationContextFlag::None,
                           {},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_ok(req.id, "{\"waited\":true}"); }));

    entries.push_back(
        automation_handler("ping",
                           "Ping the Spectra application to verify the connection is alive.",
                           AutomationContextFlag::None,
                           {},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_ok(req.id, "{\"pong\":true}"); }));

    entries.push_back(automation_handler(
        "dismiss_ui_capture",
        "Cancel tab drag, dock drag, and open menus so MCP clicks are not stuck in drag mode.",
        AutomationContextFlag::UiContext,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
        {
#ifdef SPECTRA_USE_IMGUI
            const bool tab_drag = ui_ctx->tab_drag_controller.is_active();
            const bool pane_tab = ui_ctx->imgui_ui && ui_ctx->imgui_ui->is_tab_interacting();
            const bool menu     = ui_ctx->imgui_ui && ui_ctx->imgui_ui->is_menu_open();
            automation::dismiss_ui_capture(ui_ctx);
            std::ostringstream oss;
            oss << "{\"cleared\":{" << "\"tab_drag\":" << (tab_drag ? "true" : "false")
                << ",\"pane_tab\":" << (pane_tab ? "true" : "false")
                << ",\"menu\":" << (menu ? "true" : "false") << "}}";
            req.response_json = json_ok(req.id, oss.str());
#else
            (void)ui_ctx;
            req.response_json = json_ok(req.id, "{\"cleared\":{}}");
#endif
        }));

    return entries;
}

}   // namespace spectra
