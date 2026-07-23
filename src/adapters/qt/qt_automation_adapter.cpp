// qt_automation_adapter.cpp — QtAutomationAdapter implementation.

#include "qt_automation_adapter.hpp"

#include "app/application_services.hpp"
#include "app/frontend_services.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"
#include "ui/commands/command_registry.hpp"

#include <spectra/logger.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace spectra::adapters::qt
{

QtAutomationAdapter::QtAutomationAdapter(QObject* parent) : QObject(parent) {}

QtAutomationAdapter::~QtAutomationAdapter()
{
    stop();
}

bool QtAutomationAdapter::start(ApplicationServices* services, uint16_t port)
{
    if (running_.load(std::memory_order_relaxed))
        return true;

    if (!services)
        return false;

    services_ = services;

    // Determine port: explicit > env > default 9837
    if (port == 0)
    {
        const char* env_port = std::getenv("SPECTRA_MCP_PORT");
        if (env_port && *env_port)
            port = static_cast<uint16_t>(std::atoi(env_port));
        else
            port = 9837;
    }

    const bool automation_was_running = services_->automation() != nullptr;
    if (!automation_was_running)
    {
        if (!services_->start_automation("", "127.0.0.1", port))
        {
            SPECTRA_LOG_WARN("qt_automation", "Failed to start automation server");
            return false;
        }
        owns_automation_ = true;
    }

    if (!services_->automation() || !services_->mcp() || !services_->mcp()->is_running())
    {
        SPECTRA_LOG_WARN("qt_automation", "Application services did not provide a live MCP server");
        if (owns_automation_)
            services_->stop_automation();
        owns_automation_ = false;
        services_        = nullptr;
        return false;
    }

    // Drive the service-owned pending queue from the Qt event loop.
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(16);   // ~60fps
    connect(poll_timer_, &QTimer::timeout, this, &QtAutomationAdapter::on_poll_timeout);
    poll_timer_->start();

    running_.store(true, std::memory_order_release);
    SPECTRA_LOG_INFO("qt_automation", "Using MCP server on " + services_->mcp()->endpoint());
    return true;
}

void QtAutomationAdapter::stop()
{
    if (!running_.load(std::memory_order_relaxed))
        return;

    running_.store(false, std::memory_order_release);

    if (poll_timer_)
    {
        poll_timer_->stop();
        delete poll_timer_;
        poll_timer_ = nullptr;
    }

    if (owns_automation_ && services_)
        services_->stop_automation();

    owns_automation_ = false;
    services_        = nullptr;
}

void QtAutomationAdapter::on_poll_timeout()
{
    if (!services_ || !services_->automation())
        return;

    services_->automation()->poll([this](AutomationRequest& request) { handle_request(request); });
}

void QtAutomationAdapter::handle_request(AutomationRequest& request)
{
    if (request.method == "ping")
    {
        request.response_json = json_ok(request.id, R"({"pong":true})");
        return;
    }

    if (request.method == "pump_frames")
    {
        int count             = json_get_int(request.params_json, "count", 1);
        count                 = std::max(1, std::min(count, 600));
        request.response_json = json_ok(request.id, "{\"pumped\":" + std::to_string(count) + "}");
        return;
    }

    if (request.method == "wait_frames")
    {
        request.response_json = json_ok(request.id, R"({"waited":true})");
        return;
    }

    if (request.method == "dismiss_ui_capture")
    {
        request.response_json = json_ok(request.id, R"({"cleared":{}})");
        return;
    }

    if (request.method == "get_state")
    {
        if (!get_state_fn_)
        {
            request.response_json = json_error(request.id, "Qt application state is unavailable");
            return;
        }
        request.response_json = json_ok(request.id, get_state_fn_());
        return;
    }

    if (request.method == "list_commands")
    {
        if (!services_)
        {
            request.response_json = json_error(request.id, "Application services are unavailable");
            return;
        }

        const auto         commands = services_->commands().all_commands();
        std::ostringstream result;
        result << R"({"commands":[)";
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (i > 0)
                result << ',';
            const auto* command = commands[i];
            result << R"({"id":")" << json_escape(command->id) << R"(","label":")"
                   << json_escape(command->label) << R"(","category":")"
                   << json_escape(command->category) << R"(","shortcut":")"
                   << json_escape(command->shortcut) << R"(","enabled":)"
                   << (command->enabled ? "true" : "false") << '}';
        }
        result << "]}";
        request.response_json = json_ok(request.id, result.str());
        return;
    }

    if (request.method == "execute_command")
    {
        const std::string command_id = json_get_string(request.params_json, "command_id");
        if (command_id.empty())
        {
            request.response_json = json_error(request.id, "Missing parameter: command_id");
            return;
        }

        const bool executed = execute_cmd_fn_ ? execute_cmd_fn_(command_id)
                                              : services_->commands().execute(command_id);
        if (!executed)
        {
            request.response_json =
                json_error(request.id, "Command not found or disabled: " + command_id);
            return;
        }
        if (services_->redraw_request())
            services_->redraw_request()->request_redraw();
        request.response_json =
            json_ok(request.id, R"({"executed":")" + json_escape(command_id) + "\"}");
        return;
    }

    if (request.method == "create_figure")
    {
        const int width  = json_get_int(request.params_json, "width", 1280);
        const int height = json_get_int(request.params_json, "height", 720);
        if (width <= 0 || height <= 0)
        {
            request.response_json = json_error(request.id, "Figure dimensions must be positive");
            return;
        }
        if (!create_figure_fn_)
        {
            request.response_json = json_error(request.id, "Figure creation is unavailable");
            return;
        }
        const FigureId figure_id =
            create_figure_fn_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        if (figure_id == INVALID_FIGURE_ID)
        {
            request.response_json = json_error(request.id, "Figure creation failed");
            return;
        }
        request.response_json =
            json_ok(request.id, "{\"figure_id\":" + std::to_string(figure_id) + "}");
        return;
    }

    if (request.method == "get_window_size")
    {
        if (!get_size_fn_)
        {
            request.response_json = json_error(request.id, "Window size is unavailable");
            return;
        }
        const auto [width, height] = get_size_fn_();
        request.response_json      = json_ok(
            request.id,
            "{\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) + "}");
        return;
    }

    if (request.method == "resize_window")
    {
        const int width  = json_get_int(request.params_json, "width", 1280);
        const int height = json_get_int(request.params_json, "height", 720);
        if (width <= 0 || height <= 0 || !resize_fn_)
        {
            request.response_json = json_error(request.id, "Window resize is unavailable");
            return;
        }
        resize_fn_(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        request.response_json = json_ok(request.id);
        return;
    }

    if (request.method == "capture_screenshot" || request.method == "capture_window")
    {
        std::string path = json_get_string(request.params_json, "path");
        if (path.empty())
        {
            path = request.method == "capture_window" ? "/tmp/spectra_auto_window.png"
                                                      : "/tmp/spectra_auto_screenshot.png";
        }
        if (!capture_fn_ || capture_fn_(path).empty())
        {
            request.response_json = json_error(request.id, "Screenshot capture failed");
            return;
        }
        request.response_json = json_ok(request.id, R"({"path":")" + json_escape(path) + "\"}");
        return;
    }

    request.response_json =
        json_error(request.id, "Qt automation method is not implemented: " + request.method);
}

}   // namespace spectra::adapters::qt
