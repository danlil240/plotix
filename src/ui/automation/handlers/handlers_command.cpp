// handlers_command.cpp — execute_command, list_commands handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/shell/menu_bar.hpp"

#include <sstream>

namespace spectra
{

#ifdef SPECTRA_USE_IMGUI
namespace
{
void append_menu_actions_json(std::ostringstream&                       oss,
                              const std::vector<ui::shell::MenuAction>& actions,
                              bool&                                     first)
{
    for (const ui::shell::MenuAction& action : actions)
    {
        if (action.separator)
        {
            if (!first)
                oss << ",";
            first = false;
            oss << R"({"separator":true})";
            continue;
        }
        if (!action.submenu.empty())
        {
            append_menu_actions_json(oss, action.submenu, first);
            continue;
        }
        if (action.label.empty() || action.label.rfind("##", 0) == 0)
            continue;
        if (!first)
            oss << ",";
        first                = false;
        const bool enabled   = !action.enabled || action.enabled();
        const bool checkable = static_cast<bool>(action.checked);
        oss << R"({"label":")" << json_escape(action.label) << R"(","enabled":)"
            << (enabled ? "true" : "false") << R"(,"checkable":)" << (checkable ? "true" : "false")
            << "}";
    }
}
}   // namespace
#endif

std::vector<AutomationHandlerEntry> make_command_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

#ifdef SPECTRA_USE_IMGUI
    entries.push_back(automation_handler(
        "execute_command",
        "Execute a registered Spectra UI command by its ID.",
        AutomationContextFlag::ImGui,
        {{.name = "command_id", .kind = ParamKind::String, .required = true}},
        [](AutomationRequest& req, App* app, WindowUIContext* ui_ctx)
        {
            std::string cmd_id = json_get_string(req.params_json, "command_id");
            bool        ok     = ui_ctx->cmd_registry.execute(cmd_id);
            if (ok)
            {
                if (auto* sess = app->session())
                    sess->redraw_tracker().mark_dirty("execute_command");
            }
            req.response_json =
                ok ? json_ok(req.id, R"({"executed":")" + json_escape(cmd_id) + "\"}")
                   : json_error(req.id, "Command not found or disabled: " + cmd_id);
        }));

    entries.push_back(automation_handler(
        "list_commands",
        "List all registered UI commands in the Spectra application.",
        AutomationContextFlag::ImGui,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
        {
            auto               all = ui_ctx->cmd_registry.all_commands();
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (i > 0)
                    oss << ",";
                oss << R"({"id":")" << json_escape(all[i]->id) << R"(","label":")"
                    << json_escape(all[i]->label) << R"(","category":")"
                    << json_escape(all[i]->category) << R"(","shortcut":")"
                    << json_escape(all[i]->shortcut) << R"(","enabled":)"
                    << (all[i]->enabled ? "true" : "false") << "}";
            }
            oss << "]";
            req.response_json = json_ok(req.id, "{\"commands\":" + oss.str() + "}");
        }));

    entries.push_back(automation_handler(
        "list_menus",
        "List top-level menu bar entries and their actionable items.",
        AutomationContextFlag::ImGui,
        {},
        [](AutomationRequest& req, App* /*app*/, WindowUIContext* ui_ctx)
        {
            if (!ui_ctx->app_shell)
            {
                req.response_json = json_error(req.id, "App shell not available");
                return;
            }
            ui_ctx->app_shell->sync_before_frame();
            ui::shell::MenuBar& bar = ui_ctx->app_shell->menu_bar();
            std::ostringstream  oss;
            oss << R"({"menus":[)";
            bool first_menu = true;
            for (const std::string& menu_name : bar.menu_names())
            {
                const ui::shell::Menu& menu = bar.menu(menu_name);
                if (!first_menu)
                    oss << ",";
                first_menu = false;
                oss << R"({"name":")" << json_escape(menu_name) << R"(","items":[)";
                bool first_item = true;
                append_menu_actions_json(oss, menu.items(), first_item);
                oss << "]}";
            }
            oss << "]}";
            req.response_json = json_ok(req.id, oss.str());
        }));

#else
    entries.push_back(
        automation_handler("execute_command",
                           "Execute a registered Spectra UI command by its ID.",
                           AutomationContextFlag::ImGui,
                           {{.name = "command_id", .kind = ParamKind::String, .required = true}},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_error(req.id, "ImGui not available"); }));

    entries.push_back(
        automation_handler("list_commands",
                           "List all registered UI commands in the Spectra application.",
                           AutomationContextFlag::ImGui,
                           {},
                           [](AutomationRequest& req, App* /*app*/, WindowUIContext* /*ui_ctx*/)
                           { req.response_json = json_error(req.id, "ImGui not available"); }));
#endif

    return entries;
}

}   // namespace spectra
