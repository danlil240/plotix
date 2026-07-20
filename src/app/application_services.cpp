#include "application_services.hpp"

#include "ui/commands/command_queue.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/app/session_runtime.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/workspace/plugin_api.hpp"
#include "ui/workspace/plugin_ui_schema.hpp"
#include "io/export_registry.hpp"
#include "render/series_type_registry.hpp"
#include "adapters/data_source_registry.hpp"
#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"
#include "anim/frame_scheduler.hpp"

#ifdef SPECTRA_USE_IMGUI
#include "ui/commands/command_palette.hpp"
#endif

#ifndef _WIN32
#include "app/inproc_topic_server.hpp"
#endif

#include <spectra/animator.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/logger.hpp>

namespace spectra
{

ApplicationServices::ApplicationServices() = default;

ApplicationServices::~ApplicationServices()
{
    shutdown();
}

void ApplicationServices::init(FigureRegistry&   registry,
                               Backend&          backend,
                               Renderer&         renderer,
                               ui::ThemeManager& theme_mgr,
                               float             fps)
{
    if (initialized_)
        return;

    registry_  = &registry;
    backend_   = &backend;
    renderer_  = &renderer;
    theme_mgr_ = &theme_mgr;

    // Core registries (framework-neutral)
    cmd_registry_         = std::make_unique<CommandRegistry>();
    shortcut_mgr_         = std::make_unique<ShortcutManager>();
    undo_mgr_             = std::make_unique<UndoManager>();

    // Plugin and data registries
    plugin_mgr_           = std::make_unique<PluginManager>();
    plugin_ui_registry_   = std::make_unique<PluginUIRegistry>();
    export_format_registry_ = std::make_unique<ExportFormatRegistry>();
    data_source_registry_ = std::make_unique<DataSourceRegistry>();
    series_type_registry_ = std::make_unique<SeriesTypeRegistry>();

    // Settings
    settings_store_       = std::make_unique<ui::settings::SettingsStore>();
    settings_store_->load();
    settings_store_->apply_to(theme_mgr);
    {
        auto* store_ptr = settings_store_.get();
        settings_store_->set_on_change([store_ptr]() { store_ptr->save(); });
    }

    // Frame infrastructure
    cmd_queue_            = std::make_unique<CommandQueue>();
    scheduler_            = std::make_unique<FrameScheduler>(fps);
    animator_             = std::make_unique<Animator>();

    // Session (ties backend + renderer + registry together)
    session_              = std::make_unique<SessionRuntime>(backend, renderer, registry);

    shortcut_mgr_->set_command_registry(cmd_registry_.get());

    // Wire plugin manager to registries
    plugin_mgr_->set_command_registry(cmd_registry_.get());
    plugin_mgr_->set_shortcut_manager(shortcut_mgr_.get());
    plugin_mgr_->set_undo_manager(undo_mgr_.get());
    plugin_mgr_->set_export_format_registry(export_format_registry_.get());
    plugin_mgr_->set_data_source_registry(data_source_registry_.get());
    plugin_mgr_->set_series_type_registry(series_type_registry_.get());
    plugin_mgr_->set_plugin_ui_registry(plugin_ui_registry_.get());

#ifdef SPECTRA_USE_IMGUI
    cmd_palette_ = std::make_unique<CommandPalette>();
    cmd_palette_->set_command_registry(cmd_registry_.get());
    cmd_palette_->set_shortcut_manager(shortcut_mgr_.get());
#endif

    initialized_ = true;
    SPECTRA_LOG_INFO("app_services", "ApplicationServices initialized");
}

void ApplicationServices::shutdown()
{
    if (!initialized_)
        return;

    // Stop automation first (it may reference services below)
    stop_automation();

#ifndef _WIN32
    stop_topic_server();
#endif

    // Detach plugin manager from registries before they're destroyed
    if (plugin_mgr_)
    {
        plugin_mgr_->set_command_registry(nullptr);
        plugin_mgr_->set_shortcut_manager(nullptr);
        plugin_mgr_->set_undo_manager(nullptr);
        plugin_mgr_->set_transform_registry(nullptr);
        plugin_mgr_->set_export_format_registry(nullptr);
        plugin_mgr_->set_data_source_registry(nullptr);
        plugin_mgr_->set_series_type_registry(nullptr);
        plugin_mgr_->set_plugin_ui_registry(nullptr);
        plugin_mgr_->set_backend(nullptr);
    }

    // Destroy in reverse order of creation
    session_.reset();
    animator_.reset();
    scheduler_.reset();
    cmd_queue_.reset();

    settings_store_.reset();

    series_type_registry_.reset();
    data_source_registry_.reset();
    export_format_registry_.reset();
    plugin_ui_registry_.reset();
    plugin_mgr_.reset();

#ifdef SPECTRA_USE_IMGUI
    cmd_palette_.reset();
#endif
    undo_mgr_.reset();
    shortcut_mgr_.reset();
    cmd_registry_.reset();

    registry_  = nullptr;
    backend_   = nullptr;
    renderer_  = nullptr;
    theme_mgr_ = nullptr;

    initialized_ = false;
    SPECTRA_LOG_INFO("app_services", "ApplicationServices shutdown complete");
}

bool ApplicationServices::start_automation(const std::string& socket_path,
                                           const std::string& mcp_bind,
                                           uint16_t           mcp_port)
{
    if (auto_server_)
        return true;  // already running

    std::string sock = socket_path;
    if (sock.empty())
        sock = AutomationServer::default_socket_path();

    auto_server_ = std::make_unique<AutomationServer>();
    if (!auto_server_->start(sock))
    {
        SPECTRA_LOG_WARN("app_services", "Automation server failed to start");
        auto_server_.reset();
        return false;
    }

    SPECTRA_LOG_INFO("app_services", "Automation server started: " + sock);

    mcp_server_ = std::make_unique<McpServer>();

    // If the user didn't pin a port, probe a small range so multiple
    // Spectra instances on the same machine don't collide.
    bool           started   = false;
    const uint16_t max_tries = (mcp_port != 0) ? 1 : 16;
    for (uint16_t i = 0; i < max_tries; ++i)
    {
        const auto try_port = static_cast<uint16_t>(mcp_port + i);
        if (mcp_server_->start(*auto_server_, mcp_bind, try_port))
        {
            started = true;
            break;
        }
    }
    if (started)
        SPECTRA_LOG_INFO("app_services", "MCP server started: " + mcp_server_->endpoint());
    else
        mcp_server_.reset();

    return true;
}

void ApplicationServices::stop_automation()
{
    if (mcp_server_)
    {
        mcp_server_->stop();
        mcp_server_.reset();
    }

    if (auto_server_)
    {
        auto_server_->stop();
        auto_server_.reset();
    }
}

#ifndef _WIN32
bool ApplicationServices::start_topic_server(FigureRegistry& registry)
{
    if (topic_server_)
        return true;

    topic_server_ = std::make_unique<InprocTopicServer>();
    topic_server_->start(&registry);
    return true;
}

void ApplicationServices::stop_topic_server()
{
    if (topic_server_)
    {
        topic_server_->stop();
        topic_server_.reset();
    }
}
#endif

}   // namespace spectra
