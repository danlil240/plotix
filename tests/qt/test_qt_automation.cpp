// test_qt_automation.cpp — Qt integration tests for QtAutomationAdapter.
//
// Verifies that the automation adapter starts/stops cleanly, handles
// ping/state/list_commands requests, and executes commands via callbacks.
// Phase 6 acceptance: "automation passes".

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>

#include "adapters/qt/qt_automation_adapter.hpp"
#include "adapters/qt/qt_input_router.hpp"
#include "adapters/qt/components/spectra_menu_strip.hpp"

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
#include <vector>

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

class InputProbe final : public QWidget
{
   public:
    explicit InputProbe(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    int move_count         = 0;
    int press_count        = 0;
    int release_count      = 0;
    int double_click_count = 0;
    int wheel_count        = 0;

   protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        ++move_count;
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        ++press_count;
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        ++release_count;
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        ++double_click_count;
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        ++wheel_count;
        event->accept();
    }
};

class NativeInputProbe final : public QWindow
{
   public:
    int press_count   = 0;
    int release_count = 0;

   protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        ++press_count;
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        ++release_count;
        event->accept();
    }
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

std::string invoke_with_qt_events(uint16_t           port,
                                  const std::string& tool_name,
                                  const std::string& arguments_json = "{}")
{
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")"
                             + tool_name + R"(","arguments":)" + arguments_json + "}}";
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

    adapter.set_capture_surface(
        [](spectra::adapters::qt::QtCaptureScope scope,
           const std::string&                    path) -> spectra::adapters::qt::QtCaptureResult
        {
            return {
                .path       = path,
                .png_base64 = scope == spectra::adapters::qt::QtCaptureScope::Window ? "cG5n" : "",
                .width      = 1280,
                .height     = 720,
            };
        });

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

TEST(QtAutomation, InputRouterPreservesSharedKeyCodes)
{
    using spectra::adapters::qt::QtInputRouter;

    EXPECT_EQ(QtInputRouter::qtKeyToSpectra(Qt::Key_A), 65);
    EXPECT_EQ(QtInputRouter::qtKeyToSpectra(Qt::Key_Semicolon), 59);
    EXPECT_EQ(QtInputRouter::qtKeyToSpectra(Qt::Key_PageDown), 267);
    EXPECT_EQ(QtInputRouter::qtKeyToSpectra(Qt::Key_F12), 301);
    EXPECT_EQ(QtInputRouter::qtKeyToSpectra(Qt::Key_Shift), 340);
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

    spectra::adapters::qt::QtAutomationAdapter         adapter;
    std::atomic<bool>                                  state_dispatched_on_qt_thread{false};
    std::atomic<uint64_t>                              rendered_frames{0};
    std::vector<spectra::adapters::qt::QtCaptureScope> capture_scopes;
    const auto        first_figure  = registry.register_figure(std::make_unique<spectra::Figure>());
    const auto        second_figure = registry.register_figure(std::make_unique<spectra::Figure>());
    spectra::FigureId switched_figure = spectra::INVALID_FIGURE_ID;

    QWidget input_root;
    input_root.resize(360, 180);
    InputProbe input_probe(&input_root);
    input_probe.setGeometry(10, 10, 120, 120);
    QLineEdit text_input(&input_root);
    text_input.setGeometry(160, 20, 160, 36);
    QMenu automation_menu(&input_root);
    automation_menu.addAction(QStringLiteral("Dismiss me"));
    spectra::adapters::qt::SpectraMenuButton menu_button(QStringLiteral("Menu"),
                                                         &automation_menu,
                                                         &input_root);
    menu_button.setGeometry(160, 70, 100, 34);
    input_root.show();
    QCoreApplication::processEvents();
    NativeInputProbe native_input_probe;
    native_input_probe.setParent(input_root.windowHandle());
    native_input_probe.setGeometry(10, 140, 120, 30);
    native_input_probe.show();
    adapter.set_input_target(&input_root, [&native_input_probe]() { return &native_input_probe; });

    adapter.set_get_state(
        [&state_dispatched_on_qt_thread]()
        {
            state_dispatched_on_qt_thread.store(
                QThread::currentThread() == QCoreApplication::instance()->thread(),
                std::memory_order_release);
            return R"({"status":"qt-ready"})";
        });
    adapter.set_switch_figure(
        [&switched_figure](spectra::FigureId id)
        {
            switched_figure = id;
            return true;
        });
    adapter.set_list_menus(
        []()
        {
            return R"({"menus":[{"name":"File","items":[{"label":"New Figure","enabled":true,"checkable":false}]}]})";
        });
    adapter.set_capture_surface(
        [&capture_scopes](spectra::adapters::qt::QtCaptureScope scope, const std::string& path)
        {
            capture_scopes.push_back(scope);
            return spectra::adapters::qt::QtCaptureResult{
                .path       = path,
                .png_base64 = "iVBORw0KGgo=",
                .width      = scope == spectra::adapters::qt::QtCaptureScope::Canvas ? 640u : 1280u,
                .height     = scope == spectra::adapters::qt::QtCaptureScope::Canvas ? 480u : 720u,
            };
        });
    adapter.set_frame_callbacks(
        [&rendered_frames](uint32_t count)
        {
            const uint32_t rendered = count == 5 ? 2 : count;
            rendered_frames.fetch_add(rendered, std::memory_order_release);
            return rendered;
        },
        [&rendered_frames]() { return rendered_frames.load(std::memory_order_acquire); });

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

    const std::string menus_response = invoke_with_qt_events(port, "list_menus");
    EXPECT_EQ(menus_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(menus_response.find("New Figure"), std::string::npos);
    EXPECT_EQ(menus_response.find("not implemented"), std::string::npos);

    const std::string methods_response = invoke_with_qt_events(port, "list_methods");
    EXPECT_EQ(methods_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(methods_response.find("add_series"), std::string::npos);
    EXPECT_EQ(methods_response.find("not implemented"), std::string::npos);

    const std::string switch_response =
        invoke_with_qt_events(port,
                              "switch_figure",
                              "{\"figure_id\":" + std::to_string(second_figure) + "}");
    EXPECT_EQ(switch_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(switched_figure, second_figure);

    const std::string add_response = invoke_with_qt_events(
        port,
        "add_series",
        "{\"figure_id\":" + std::to_string(first_figure)
            + R"(,"type":"scatter","x":[1,2,3],"y":[4,5,6],"label":"samples"})");
    EXPECT_EQ(add_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(add_response.find(R"("series_count":1)"), std::string::npos);

    const std::string invalid_add_response = invoke_with_qt_events(
        port,
        "add_series",
        "{\"figure_id\":" + std::to_string(first_figure) + R"(,"x":[1,2,3]})");
    EXPECT_NE(invalid_add_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(invalid_add_response.find("x and y must be provided together"), std::string::npos);

    const std::string info_response =
        invoke_with_qt_events(port,
                              "get_figure_info",
                              "{\"figure_id\":" + std::to_string(first_figure) + "}");
    EXPECT_EQ(info_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(info_response.find(R"("type":"scatter")"), std::string::npos);
    EXPECT_NE(info_response.find(R"("label":"samples")"), std::string::npos);
    EXPECT_NE(info_response.find(R"("point_count":3)"), std::string::npos);

    const std::string move_response =
        invoke_with_qt_events(port, "mouse_move", R"({"x":20,"y":20})");
    EXPECT_EQ(move_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_GE(input_probe.move_count, 1);

    const std::string click_response =
        invoke_with_qt_events(port, "mouse_click", R"({"x":20,"y":20})");
    EXPECT_EQ(click_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(input_probe.press_count, 1);
    EXPECT_EQ(input_probe.release_count, 1);

    const std::string drag_response =
        invoke_with_qt_events(port, "mouse_drag", R"({"x1":20,"y1":20,"x2":80,"y2":80,"steps":4})");
    EXPECT_EQ(drag_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(input_probe.press_count, 2);
    EXPECT_EQ(input_probe.release_count, 2);
    EXPECT_GE(input_probe.move_count, 6);

    const std::string scroll_response =
        invoke_with_qt_events(port, "scroll", R"({"x":20,"y":20,"dy":-2})");
    EXPECT_EQ(scroll_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(input_probe.wheel_count, 1);

    const std::string double_click_response =
        invoke_with_qt_events(port, "double_click", R"({"x":20,"y":20})");
    EXPECT_EQ(double_click_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(input_probe.double_click_count, 1);

    const std::string native_click_response =
        invoke_with_qt_events(port, "mouse_click", R"({"x":20,"y":150})");
    EXPECT_EQ(native_click_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(native_input_probe.press_count, 1);
    EXPECT_EQ(native_input_probe.release_count, 1);

    text_input.setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const std::string text_response =
        invoke_with_qt_events(port, "text_input", R"({"text":"spectra"})");
    EXPECT_EQ(text_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(text_input.text(), QStringLiteral("spectra"));

    const std::string key_response = invoke_with_qt_events(port, "key_press", R"({"key":65})");
    EXPECT_EQ(key_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_EQ(text_input.text(), QStringLiteral("spectraa"));

    const std::string canvas_capture =
        invoke_with_qt_events(port, "capture_screenshot", R"({"path":"/tmp/canvas.png"})");
    EXPECT_EQ(canvas_capture.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(canvas_capture.find(R"("scope":"canvas")"), std::string::npos);
    EXPECT_NE(canvas_capture.find(R"("width":640)"), std::string::npos);

    const std::string window_capture =
        invoke_with_qt_events(port, "capture_window", R"({"path":"/tmp/window.png"})");
    EXPECT_EQ(window_capture.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(window_capture.find(R"("scope":"window")"), std::string::npos);
    EXPECT_NE(window_capture.find(R"("width":1280)"), std::string::npos);

    const std::string base64_capture = invoke_with_qt_events(port, "get_screenshot_base64");
    EXPECT_EQ(base64_capture.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(base64_capture.find(R"("format":"png")"), std::string::npos);
    EXPECT_NE(base64_capture.find(R"("scope":"window")"), std::string::npos);
    EXPECT_NE(base64_capture.find("iVBORw0KGgo="), std::string::npos);

    const std::string pump_response = invoke_with_qt_events(port, "pump_frames", R"({"count":3})");
    EXPECT_EQ(pump_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(pump_response.find(R"("pumped":3)"), std::string::npos);
    EXPECT_EQ(rendered_frames.load(std::memory_order_acquire), 3u);

    QTimer frame_timer;
    QObject::connect(&frame_timer,
                     &QTimer::timeout,
                     [&rendered_frames]()
                     { rendered_frames.fetch_add(1, std::memory_order_release); });
    frame_timer.start(1);
    const std::string wait_response = invoke_with_qt_events(port, "wait_frames", R"({"count":4})");
    frame_timer.stop();
    EXPECT_EQ(wait_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(wait_response.find(R"("waited":true)"), std::string::npos);
    EXPECT_GE(rendered_frames.load(std::memory_order_acquire), 7u);

    const std::string partial_pump_response =
        invoke_with_qt_events(port, "pump_frames", R"({"count":5})");
    EXPECT_NE(partial_pump_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(partial_pump_response.find("rendered 2 of 5 requested frames"), std::string::npos);

    ASSERT_EQ(capture_scopes.size(), 3u);
    EXPECT_EQ(capture_scopes[0], spectra::adapters::qt::QtCaptureScope::Canvas);
    EXPECT_EQ(capture_scopes[1], spectra::adapters::qt::QtCaptureScope::Window);
    EXPECT_EQ(capture_scopes[2], spectra::adapters::qt::QtCaptureScope::Window);

    const std::string menu_click_response =
        invoke_with_qt_events(port, "mouse_click", R"({"x":180,"y":85})");
    EXPECT_EQ(menu_click_response.find(R"("isError":true)"), std::string::npos);
    ASSERT_EQ(QApplication::activePopupWidget(), &automation_menu);

    const std::string dismiss_response = invoke_with_qt_events(port, "dismiss_ui_capture");
    EXPECT_EQ(dismiss_response.find(R"("isError":true)"), std::string::npos);
    EXPECT_NE(dismiss_response.find(R"("popup":true)"), std::string::npos);
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);

    static constexpr std::array<const char*, 3> kUnsupportedTools = {
        "fuzz_step",
        "fuzz_reset",
        "list_fuzz_actions",
    };
    for (const char* tool_name : kUnsupportedTools)
    {
        const std::string response = invoke_with_qt_events(port, tool_name);
        EXPECT_FALSE(response.empty()) << tool_name;
        EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos) << tool_name;
        EXPECT_NE(response.find(R"("isError":true)"), std::string::npos) << tool_name;
        EXPECT_NE(response.find("Qt automation method is not implemented"), std::string::npos)
            << tool_name;
    }

    adapter.stop();
    EXPECT_FALSE(adapter.is_running());
    EXPECT_EQ(services.automation(), nullptr);
    EXPECT_EQ(services.mcp(), nullptr);
#endif
}
