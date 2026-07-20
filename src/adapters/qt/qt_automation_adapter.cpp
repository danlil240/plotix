// qt_automation_adapter.cpp — QtAutomationAdapter implementation.

#include "qt_automation_adapter.hpp"

#include "app/application_services.hpp"
#include "ui/automation/mcp_server.hpp"

#include <spectra/logger.hpp>

#include <cstdlib>

namespace spectra::adapters::qt
{

namespace
{

std::string json_ok_str(uint64_t id, const std::string& result)
{
    return R"({"id":)" + std::to_string(id)
           + R"(,"ok":true,"result":)" + result + "}";
}

std::string json_err_str(uint64_t id, const std::string& error)
{
    return R"({"id":)" + std::to_string(id)
           + R"(,"ok":false,"error":")" + error + R"("})";
}

std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string extract_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return "";
    size_t end = pos + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\')
            break;
        ++end;
    }
    return json.substr(pos + 1, end - pos - 1);
}

double extract_number(const std::string& json, const std::string& key, double default_val = 0)
{
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return default_val;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return default_val;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        ++pos;
    size_t end = pos;
    while (end < json.size()
           && (json[end] == '-' || json[end] == '.' || json[end] == '+'
               || json[end] == 'e' || json[end] == 'E'
               || (json[end] >= '0' && json[end] <= '9')))
        ++end;
    if (end == pos)
        return default_val;
    try { return std::stod(json.substr(pos, end - pos)); }
    catch (...) { return default_val; }
}

}   // namespace

QtAutomationAdapter::QtAutomationAdapter(QObject* parent)
    : QObject(parent)
{
}

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

    mcp_server_ = std::make_unique<McpServer>();

    // We need an AutomationServer for the MCP server to call.
    // For now, use ApplicationServices' built-in automation server.
    if (!services_->automation())
    {
        // Start automation with default socket path
        if (!services_->start_automation("", "127.0.0.1", port))
        {
            SPECTRA_LOG_WARN("qt_automation", "Failed to start automation server");
            mcp_server_.reset();
            return false;
        }
    }

    // Start MCP server pointing to the automation server
    if (!mcp_server_->start(*services_->automation(), "127.0.0.1", port))
    {
        SPECTRA_LOG_WARN("qt_automation", "Failed to start MCP server on port " + std::to_string(port));
        mcp_server_.reset();
        return false;
    }

    // Set up poll timer to drive automation from Qt event loop
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(16);  // ~60fps
    connect(poll_timer_, &QTimer::timeout, this, &QtAutomationAdapter::on_poll_timeout);
    poll_timer_->start();

    running_.store(true, std::memory_order_release);
    SPECTRA_LOG_INFO("qt_automation", "Started MCP server on " + mcp_server_->endpoint());
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

    if (mcp_server_)
    {
        mcp_server_->stop();
        mcp_server_.reset();
    }

    services_ = nullptr;
}

void QtAutomationAdapter::on_poll_timeout()
{
    // The AutomationServer::poll requires App& and WindowUIContext*.
    // In the Qt frontend, we don't have those. The MCP server calls
    // AutomationServer::invoke() directly for synchronous requests.
    // The poll-based execution path is ImGui-specific.
    //
    // For Qt, we rely on MCP server's synchronous invoke() which
    // queues requests and waits for response. The AutomationServer
    // listener thread handles dispatch.
    //
    // Future: implement a Qt-specific automation dispatch that doesn't
    // require App/WindowUIContext.
}

}   // namespace spectra::adapters::qt
