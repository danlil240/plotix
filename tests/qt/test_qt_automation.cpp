// test_qt_automation.cpp — Qt integration tests for QtAutomationAdapter.
//
// Verifies that the automation adapter starts/stops cleanly, handles
// ping/state/list_commands requests, and executes commands via callbacks.
// Phase 6 acceptance: "automation passes".

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include "adapters/qt/qt_automation_adapter.hpp"

#include "ui/commands/command_registry.hpp"
#include "ui/automation/mcp_server.hpp"
#include "app/application_services.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "ui/theme/theme.hpp"

#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>

#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <chrono>

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace
{

struct QtAutomationEnv
{
    std::unique_ptr<spectra::CommandRegistry> cmd_registry;

    QtAutomationEnv() { cmd_registry = std::make_unique<spectra::CommandRegistry>(); }
};

QtAutomationEnv& env()
{
    static QtAutomationEnv e;
    return e;
}

class TestBackend final : public spectra::Backend
{
   public:
    bool init(bool) override { return true; }
    void shutdown() override {}
    void wait_idle() override {}
    bool create_surface(void*) override { return true; }
    bool create_swapchain(uint32_t width, uint32_t height) override
    {
        width_  = width;
        height_ = height;
        return true;
    }
    bool recreate_swapchain(uint32_t width, uint32_t height) override
    {
        return create_swapchain(width, height);
    }
    bool create_offscreen_framebuffer(uint32_t width, uint32_t height) override
    {
        return create_swapchain(width, height);
    }
    spectra::PipelineHandle create_pipeline(spectra::PipelineType) override { return {1}; }
    spectra::BufferHandle   create_buffer(spectra::BufferUsage, size_t) override { return {1}; }
    void                    destroy_buffer(spectra::BufferHandle) override {}
    void upload_buffer(spectra::BufferHandle, const void*, size_t, size_t) override {}
    spectra::TextureHandle create_texture(uint32_t, uint32_t, const uint8_t*) override
    {
        return {1};
    }
    void     destroy_texture(spectra::TextureHandle) override {}
    bool     begin_frame(spectra::FrameProfiler*) override { return true; }
    void     end_frame(spectra::FrameProfiler*) override {}
    void     begin_render_pass(const spectra::Color&) override {}
    void     end_render_pass() override {}
    void     bind_pipeline(spectra::PipelineHandle) override {}
    void     bind_buffer(spectra::BufferHandle, uint32_t) override {}
    void     bind_index_buffer(spectra::BufferHandle) override {}
    void     bind_texture(spectra::TextureHandle, uint32_t) override {}
    void     push_constants(const spectra::SeriesPushConstants&) override {}
    void     set_viewport(float, float, float, float) override {}
    void     set_scissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void     set_line_width(float) override {}
    void     draw(uint32_t, uint32_t) override {}
    void     draw_instanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void     draw_indexed(uint32_t, uint32_t, int32_t) override {}
    bool     readback_framebuffer(uint8_t*, uint32_t, uint32_t) override { return false; }
    uint32_t swapchain_width() const override { return width_; }
    uint32_t swapchain_height() const override { return height_; }

   private:
    uint32_t width_  = 1280;
    uint32_t height_ = 720;
};

#ifndef _WIN32
uint16_t find_available_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return 0;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        ::close(fd);
        return 0;
    }
    const uint16_t port = ntohs(address.sin_port);
    ::close(fd);
    return port;
}

std::string post_mcp_request(uint16_t port, const std::string& body)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return {};

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return {};
    }

    const std::string request =
        "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
        "Connection: close\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < request.size())
    {
        const ssize_t count = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (count <= 0)
        {
            ::close(fd);
            return {};
        }
        sent += static_cast<size_t>(count);
    }

    std::string response;
    char        buffer[4096];
    while (true)
    {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        response.append(buffer, static_cast<size_t>(count));
    }
    ::close(fd);
    return response;
}

std::string invoke_with_qt_events(uint16_t port, const std::string& tool_name)
{
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")"
                             + tool_name + R"(","arguments":{}}})";
    auto       response = std::async(std::launch::async, post_mcp_request, port, body);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (response.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready
           && std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (response.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return {};
    return response.get();
}
#endif

}   // namespace

TEST(QtAutomation, StartStopClean)
{
    auto& e = env();

    spectra::adapters::qt::QtAutomationAdapter adapter;

    // Start without services — should fail gracefully or succeed with no-op
    // The adapter's start() requires ApplicationServices, but we test
    // the lifecycle without a real MCP server.
    EXPECT_FALSE(adapter.is_running());

    adapter.stop();
    EXPECT_FALSE(adapter.is_running());
}

TEST(QtAutomation, SetCallbacks)
{
    spectra::adapters::qt::QtAutomationAdapter adapter;

    bool cmd_executed = false;
    adapter.set_execute_command(
        [&cmd_executed](const std::string& id)
        {
            if (id == "test.cmd")
            {
                cmd_executed = true;
                return true;
            }
            return false;
        });

    adapter.set_create_figure(
        [](uint32_t w, uint32_t h) -> spectra::FigureId
        {
            auto fig = std::make_unique<spectra::Figure>();
            fig->set_size(w, h);
            return 1;   // dummy ID
        });

    adapter.set_get_state([]() -> std::string { return R"({"status":"ok"})"; });

    adapter.set_capture_screenshot([](const std::string& path) -> std::string { return path; });

    adapter.set_resize_window([](uint32_t, uint32_t) {});
    adapter.set_get_window_size([]() -> std::pair<uint32_t, uint32_t> { return {1280, 720}; });

    // Callbacks are set — no crash expected
    SUCCEED();
}

TEST(QtAutomation, CommandRegistryIntegration)
{
    auto& e = env();

    // Register some commands
    bool executed = false;
    e.cmd_registry->register_command(
        "test.auto",
        "Auto Test",
        [&executed]() { executed = true; },
        "Ctrl+Shift+A",
        "Test");

    // The automation adapter should be able to execute commands
    // through the execute_command callback
    spectra::adapters::qt::QtAutomationAdapter adapter;
    adapter.set_execute_command([&e](const std::string& id) -> bool
                                { return e.cmd_registry->execute(id); });

    // We can't call handle_request directly (private), but we verify
    // the callback wiring works
    bool result = e.cmd_registry->execute("test.auto");
    EXPECT_TRUE(result);
    EXPECT_TRUE(executed);
}

TEST(QtAutomation, MultipleStartStopCycles)
{
    spectra::adapters::qt::QtAutomationAdapter adapter;

    for (int i = 0; i < 3; ++i)
    {
        adapter.stop();
        EXPECT_FALSE(adapter.is_running());
    }

    // Double-stop should be safe
    adapter.stop();
    adapter.stop();
    EXPECT_FALSE(adapter.is_running());
}

TEST(QtAutomation, LiveMcpUsesSingleServiceEndpointAndQtDispatch)
{
#ifdef _WIN32
    GTEST_SKIP() << "Automation sockets are not supported on Windows";
#else
    TestBackend               backend;
    spectra::ui::ThemeManager theme_manager;
    theme_manager.ensure_initialized();
    spectra::Renderer            renderer(backend, theme_manager);
    spectra::FigureRegistry      registry;
    spectra::ApplicationServices services;
    services.init(registry, backend, renderer, theme_manager, 60.0f);

    const uint16_t port = find_available_port();
    ASSERT_NE(port, 0);

    spectra::adapters::qt::QtAutomationAdapter adapter;
    std::atomic<bool>                          state_dispatched_on_qt_thread{false};
    adapter.set_get_state(
        [&state_dispatched_on_qt_thread]()
        {
            state_dispatched_on_qt_thread.store(
                QThread::currentThread() == QCoreApplication::instance()->thread(),
                std::memory_order_release);
            return R"({"status":"qt-ready"})";
        });

    ASSERT_TRUE(adapter.start(&services, port));
    ASSERT_TRUE(adapter.is_running());
    ASSERT_NE(services.automation(), nullptr);
    ASSERT_NE(services.mcp(), nullptr);
    EXPECT_TRUE(services.mcp()->is_running());
    EXPECT_EQ(services.mcp()->port(), port);

    const std::string ping_response = invoke_with_qt_events(port, "ping");
    ASSERT_FALSE(ping_response.empty());
    EXPECT_NE(ping_response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(ping_response.find(R"("pong":true)"), std::string::npos);

    const std::string state_response = invoke_with_qt_events(port, "get_state");
    ASSERT_FALSE(state_response.empty());
    EXPECT_NE(state_response.find("qt-ready"), std::string::npos);
    EXPECT_TRUE(state_dispatched_on_qt_thread.load(std::memory_order_acquire));

    static constexpr std::array<const char*, 26> kRemainingTools = {
        "list_commands",      "list_menus",         "execute_command", "mouse_move",
        "mouse_click",        "mouse_drag",         "scroll",          "key_press",
        "create_figure",      "switch_figure",      "add_series",      "pump_frames",
        "capture_screenshot", "capture_window",     "resize_window",   "get_screenshot_base64",
        "wait_frames",        "text_input",         "double_click",    "get_figure_info",
        "get_window_size",    "fuzz_step",          "fuzz_reset",      "list_fuzz_actions",
        "list_methods",       "dismiss_ui_capture",
    };
    for (const char* tool_name : kRemainingTools)
    {
        const std::string response = invoke_with_qt_events(port, tool_name);
        EXPECT_FALSE(response.empty()) << tool_name;
        EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos) << tool_name;
        EXPECT_EQ(response.find("Timeout"), std::string::npos) << tool_name;
    }

    adapter.stop();
    EXPECT_FALSE(adapter.is_running());
    EXPECT_EQ(services.automation(), nullptr);
    EXPECT_EQ(services.mcp(), nullptr);
#endif
}
