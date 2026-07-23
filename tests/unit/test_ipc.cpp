#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ipc/transport.hpp"

using namespace spectra::ipc;

// ═══════════════════════════════════════════════════════════════════════════════
// Message Header Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, HeaderRoundTrip)
{
    MessageHeader hdr;
    hdr.type        = MessageType::HELLO;
    hdr.payload_len = 42;
    hdr.seq         = 123456789;
    hdr.request_id  = 99;
    hdr.session_id  = 1001;
    hdr.window_id   = 2002;

    std::vector<uint8_t> buf;
    encode_header(hdr, buf);
    ASSERT_EQ(buf.size(), HEADER_SIZE);

    auto decoded = decode_header(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->type, MessageType::HELLO);
    EXPECT_EQ(decoded->payload_len, 42u);
    EXPECT_EQ(decoded->seq, 123456789u);
    EXPECT_EQ(decoded->request_id, 99u);
    EXPECT_EQ(decoded->session_id, 1001u);
    EXPECT_EQ(decoded->window_id, 2002u);
}

TEST(IpcCodec, HeaderBadMagic)
{
    std::vector<uint8_t> buf(HEADER_SIZE, 0);
    buf[0]       = 0xFF;
    buf[1]       = 0xFF;
    auto decoded = decode_header(buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(IpcCodec, HeaderTooShort)
{
    std::vector<uint8_t> buf(10, 0);
    auto                 decoded = decode_header(buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(IpcCodec, HeaderEmptyBuffer)
{
    std::vector<uint8_t> buf;
    auto                 decoded = decode_header(buf);
    EXPECT_FALSE(decoded.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full Message Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, MessageRoundTrip)
{
    Message msg;
    msg.header.type       = MessageType::WELCOME;
    msg.header.seq        = 7;
    msg.header.session_id = 42;
    msg.payload           = {0xDE, 0xAD, 0xBE, 0xEF};

    auto wire = encode_message(msg);
    ASSERT_EQ(wire.size(), HEADER_SIZE + 4);

    auto decoded = decode_message(wire);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.type, MessageType::WELCOME);
    EXPECT_EQ(decoded->header.seq, 7u);
    EXPECT_EQ(decoded->header.session_id, 42u);
    EXPECT_EQ(decoded->header.payload_len, 4u);
    ASSERT_EQ(decoded->payload.size(), 4u);
    EXPECT_EQ(decoded->payload[0], 0xDE);
    EXPECT_EQ(decoded->payload[3], 0xEF);
}

TEST(IpcCodec, MessageEmptyPayload)
{
    Message msg;
    msg.header.type = MessageType::RESP_OK;
    msg.header.seq  = 1;

    auto wire = encode_message(msg);
    ASSERT_EQ(wire.size(), HEADER_SIZE);

    auto decoded = decode_message(wire);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.type, MessageType::RESP_OK);
    EXPECT_TRUE(decoded->payload.empty());
}

TEST(IpcCodec, MessageTruncatedPayload)
{
    Message msg;
    msg.header.type = MessageType::HELLO;
    msg.payload     = {1, 2, 3, 4, 5};

    auto wire = encode_message(msg);
    // Truncate: remove last 2 bytes
    wire.resize(wire.size() - 2);

    auto decoded = decode_message(wire);
    EXPECT_FALSE(decoded.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Payload TLV Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, PayloadDecoderEmptyBuffer)
{
    std::vector<uint8_t> empty;
    PayloadDecoder       dec(empty);
    EXPECT_FALSE(dec.next());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hello/Welcome Payload Round-Trip
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, HelloRoundTrip)
{
    HelloPayload hello;
    hello.protocol_major = 1;
    hello.protocol_minor = 0;
    hello.agent_build    = "spectra-test-v0.1";
    hello.capabilities   = 0x0F;

    auto buf     = encode_hello(hello);
    auto decoded = decode_hello(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protocol_major, 1u);
    EXPECT_EQ(decoded->protocol_minor, 0u);
    EXPECT_EQ(decoded->agent_build, "spectra-test-v0.1");
    EXPECT_EQ(decoded->capabilities, 0x0Fu);
}

TEST(IpcCodec, WelcomeRoundTrip)
{
    WelcomePayload welcome;
    welcome.session_id   = 42;
    welcome.window_id    = 7;
    welcome.process_id   = 12345;
    welcome.heartbeat_ms = 3000;
    welcome.mode         = "inproc";

    auto buf     = encode_welcome(welcome);
    auto decoded = decode_welcome(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session_id, 42u);
    EXPECT_EQ(decoded->window_id, 7u);
    EXPECT_EQ(decoded->process_id, 12345u);
    EXPECT_EQ(decoded->heartbeat_ms, 3000u);
    EXPECT_EQ(decoded->mode, "inproc");
}

TEST(IpcCodec, RespOkRoundTrip)
{
    RespOkPayload ok;
    ok.request_id = 999;

    auto buf     = encode_resp_ok(ok);
    auto decoded = decode_resp_ok(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 999u);
}

TEST(IpcCodec, RespErrRoundTrip)
{
    RespErrPayload err;
    err.request_id = 123;
    err.code       = 404;
    err.message    = "Figure not found";

    auto buf     = encode_resp_err(err);
    auto decoded = decode_resp_err(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 123u);
    EXPECT_EQ(decoded->code, 404u);
    EXPECT_EQ(decoded->message, "Figure not found");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Version Mismatch Detection
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, VersionMismatchDetection)
{
    HelloPayload hello;
    hello.protocol_major = 99;   // unsupported major version
    hello.protocol_minor = 0;
    hello.agent_build    = "future-client";

    auto buf     = encode_hello(hello);
    auto decoded = decode_hello(buf);
    ASSERT_TRUE(decoded.has_value());
    // The codec decodes it fine — version check is a policy decision
    EXPECT_EQ(decoded->protocol_major, 99u);
    EXPECT_NE(decoded->protocol_major, PROTOCOL_MAJOR);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Message Type Enum Coverage
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcMessage, MessageTypeValues)
{
    EXPECT_EQ(static_cast<uint16_t>(MessageType::HELLO), 0x0001);
    EXPECT_EQ(static_cast<uint16_t>(MessageType::WELCOME), 0x0002);
    EXPECT_EQ(static_cast<uint16_t>(MessageType::RESP_OK), 0x0010);
    EXPECT_EQ(static_cast<uint16_t>(MessageType::RESP_ERR), 0x0011);
    EXPECT_EQ(static_cast<uint16_t>(MessageType::CMD_ASSIGN_FIGURES), 0x0200);
    EXPECT_EQ(static_cast<uint16_t>(MessageType::EVT_HEARTBEAT), 0x0403);
}

TEST(IpcMessage, InvalidConstants)
{
    EXPECT_EQ(INVALID_SESSION, 0u);
    EXPECT_EQ(INVALID_WINDOW, 0u);
    EXPECT_EQ(INVALID_REQUEST, 0u);
}

TEST(IpcMessage, ProtocolVersion)
{
    EXPECT_EQ(PROTOCOL_MAJOR, 1u);
    EXPECT_EQ(PROTOCOL_MINOR, 1u);   // bumped for FlatBuffers payload support
}

// ═══════════════════════════════════════════════════════════════════════════════
// Header Size and Magic
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcMessage, HeaderSizeIs40)
{
    EXPECT_EQ(HEADER_SIZE, 40u);
}

TEST(IpcMessage, MagicBytes)
{
    EXPECT_EQ(MAGIC_0, 'S');
    EXPECT_EQ(MAGIC_1, 'P');
}

// ═══════════════════════════════════════════════════════════════════════════════
// Transport: UDS Server/Client + Handshake
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef __linux__

TEST(IpcTransport, DefaultSocketPath)
{
    auto path = default_socket_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("spectra-"), std::string::npos);
    EXPECT_NE(path.find(".sock"), std::string::npos);
}

TEST(IpcTransport, ServerListenAndClose)
{
    std::string sock_path = "/tmp/spectra-test-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));
    EXPECT_TRUE(server.is_listening());
    EXPECT_EQ(server.path(), sock_path);

    server.close();
    EXPECT_FALSE(server.is_listening());
    // Socket file should be removed
    EXPECT_FALSE(std::filesystem::exists(sock_path));
}

TEST(IpcTransport, ServerRefusesToReplaceRegularFile)
{
    std::string path = "/tmp/spectra-test-file-" + std::to_string(::getpid()) + ".sock";
    {
        std::ofstream out(path);
        out << "keep me";
    }

    Server server;
    EXPECT_FALSE(server.listen(path));
    EXPECT_FALSE(server.is_listening());

    std::ifstream in(path);
    std::string   contents;
    std::getline(in, contents);
    EXPECT_EQ(contents, "keep me");

    server.close();
    std::filesystem::remove(path);
}

TEST(IpcTransport, ServerDoubleClose)
{
    std::string sock_path = "/tmp/spectra-test-dbl-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));
    server.close();
    server.close();   // Should not crash
    EXPECT_FALSE(server.is_listening());
}

TEST(IpcTransport, ClientConnectRefused)
{
    // No server listening — connect should fail
    auto conn = Client::connect("/tmp/spectra-nonexistent-" + std::to_string(::getpid()) + ".sock");
    EXPECT_EQ(conn, nullptr);
}

TEST(IpcTransport, ConnectionSendRecv)
{
    std::string sock_path = "/tmp/spectra-test-sr-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));

    // Client connects in a thread
    std::unique_ptr<Connection> client_conn;
    std::thread                 client_thread([&]() { client_conn = Client::connect(sock_path); });

    auto server_conn = server.accept();
    client_thread.join();

    ASSERT_NE(server_conn, nullptr);
    ASSERT_NE(client_conn, nullptr);
    EXPECT_TRUE(server_conn->is_open());
    EXPECT_TRUE(client_conn->is_open());

    // Send from client → server
    Message msg;
    msg.header.type       = MessageType::HELLO;
    msg.header.seq        = 1;
    msg.header.session_id = 42;
    msg.payload           = {0x01, 0x02, 0x03};

    ASSERT_TRUE(client_conn->send(msg));

    auto received = server_conn->recv();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->header.type, MessageType::HELLO);
    EXPECT_EQ(received->header.seq, 1u);
    EXPECT_EQ(received->header.session_id, 42u);
    ASSERT_EQ(received->payload.size(), 3u);
    EXPECT_EQ(received->payload[0], 0x01);

    // Send from server → client
    Message reply;
    reply.header.type       = MessageType::WELCOME;
    reply.header.seq        = 2;
    reply.header.session_id = 42;
    reply.header.window_id  = 7;
    reply.payload           = encode_welcome({42, 7, 12345, 5000, "inproc"});

    ASSERT_TRUE(server_conn->send(reply));

    auto reply_recv = client_conn->recv();
    ASSERT_TRUE(reply_recv.has_value());
    EXPECT_EQ(reply_recv->header.type, MessageType::WELCOME);
    EXPECT_EQ(reply_recv->header.window_id, 7u);

    auto welcome = decode_welcome(reply_recv->payload);
    ASSERT_TRUE(welcome.has_value());
    EXPECT_EQ(welcome->session_id, 42u);
    EXPECT_EQ(welcome->window_id, 7u);
    EXPECT_EQ(welcome->mode, "inproc");

    // Cleanup
    client_conn->close();
    server_conn->close();
    server.close();
}

TEST(IpcTransport, FullHandshake)
{
    std::string sock_path = "/tmp/spectra-test-hs-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));

    // Simulate agent → backend handshake
    std::thread agent_thread(
        [&]()
        {
            auto conn = Client::connect(sock_path);
            ASSERT_NE(conn, nullptr);

            // Agent sends HELLO
            Message hello_msg;
            hello_msg.header.type = MessageType::HELLO;
            hello_msg.header.seq  = 1;
            hello_msg.payload = encode_hello({PROTOCOL_MAJOR, PROTOCOL_MINOR, "test-agent", 0, ""});
            ASSERT_TRUE(conn->send(hello_msg));

            // Agent receives WELCOME
            auto welcome_msg = conn->recv();
            ASSERT_TRUE(welcome_msg.has_value());
            EXPECT_EQ(welcome_msg->header.type, MessageType::WELCOME);

            auto welcome = decode_welcome(welcome_msg->payload);
            ASSERT_TRUE(welcome.has_value());
            EXPECT_NE(welcome->session_id, INVALID_SESSION);
            EXPECT_NE(welcome->window_id, INVALID_WINDOW);

            conn->close();
        });

    // Backend accepts and processes handshake
    auto conn = server.accept();
    ASSERT_NE(conn, nullptr);

    // Backend receives HELLO
    auto hello_msg = conn->recv();
    ASSERT_TRUE(hello_msg.has_value());
    EXPECT_EQ(hello_msg->header.type, MessageType::HELLO);

    auto hello = decode_hello(hello_msg->payload);
    ASSERT_TRUE(hello.has_value());
    EXPECT_EQ(hello->protocol_major, PROTOCOL_MAJOR);
    EXPECT_EQ(hello->agent_build, "test-agent");

    // Backend sends WELCOME
    Message welcome_msg;
    welcome_msg.header.type       = MessageType::WELCOME;
    welcome_msg.header.seq        = 2;
    welcome_msg.header.session_id = 100;
    welcome_msg.header.window_id  = 1;
    welcome_msg.payload           = encode_welcome({100, 1, 9999, 5000, "inproc"});
    ASSERT_TRUE(conn->send(welcome_msg));

    agent_thread.join();
    conn->close();
    server.close();
}

TEST(IpcTransport, ConnectionClosedRecvReturnsNullopt)
{
    std::string sock_path = "/tmp/spectra-test-cls-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));

    std::thread client_thread(
        [&]()
        {
            auto conn = Client::connect(sock_path);
            ASSERT_NE(conn, nullptr);
            // Close immediately — server's recv should return nullopt
            conn->close();
        });

    auto conn = server.accept();
    ASSERT_NE(conn, nullptr);
    client_thread.join();

    // Give the close a moment to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto msg = conn->recv();
    EXPECT_FALSE(msg.has_value());

    conn->close();
    server.close();
}

TEST(IpcTransport, SendOnClosedConnection)
{
    Connection conn(-1);
    EXPECT_FALSE(conn.is_open());

    Message msg;
    msg.header.type = MessageType::HELLO;
    EXPECT_FALSE(conn.send(msg));
}

TEST(IpcTransport, RecvOnClosedConnection)
{
    Connection conn(-1);
    auto       msg = conn.recv();
    EXPECT_FALSE(msg.has_value());
}

TEST(IpcTransport, FullMultiWindowFlow)
{
    // Simulate: backend + 2 agents, close agent 1 → figures redistributed to agent 2
    std::string sock_path = "/tmp/spectra-test-mw-" + std::to_string(::getpid()) + ".sock";
    Server      server;
    ASSERT_TRUE(server.listen(sock_path));

    // Agent 1 connects
    std::unique_ptr<Connection> agent1_conn;
    std::thread                 agent1_thread([&]() { agent1_conn = Client::connect(sock_path); });
    auto                        server_conn1 = server.accept();
    agent1_thread.join();
    ASSERT_NE(server_conn1, nullptr);
    ASSERT_NE(agent1_conn, nullptr);

    // Agent 2 connects
    std::unique_ptr<Connection> agent2_conn;
    std::thread                 agent2_thread([&]() { agent2_conn = Client::connect(sock_path); });
    auto                        server_conn2 = server.accept();
    agent2_thread.join();
    ASSERT_NE(server_conn2, nullptr);
    ASSERT_NE(agent2_conn, nullptr);

    // Backend sends CMD_ASSIGN_FIGURES to agent 1
    CmdAssignFiguresPayload assign1;
    assign1.window_id        = 1;
    assign1.figure_ids       = {10, 20};
    assign1.active_figure_id = 10;

    Message assign_msg;
    assign_msg.header.type        = MessageType::CMD_ASSIGN_FIGURES;
    assign_msg.header.window_id   = 1;
    assign_msg.payload            = encode_cmd_assign_figures(assign1);
    assign_msg.header.payload_len = static_cast<uint32_t>(assign_msg.payload.size());
    ASSERT_TRUE(server_conn1->send(assign_msg));

    // Agent 1 receives it
    auto recv1 = agent1_conn->recv();
    ASSERT_TRUE(recv1.has_value());
    EXPECT_EQ(recv1->header.type, MessageType::CMD_ASSIGN_FIGURES);
    auto decoded1 = decode_cmd_assign_figures(recv1->payload);
    ASSERT_TRUE(decoded1.has_value());
    EXPECT_EQ(decoded1->figure_ids.size(), 2u);
    EXPECT_EQ(decoded1->active_figure_id, 10u);

    // Simulate agent 1 closing: backend sends CMD_ASSIGN_FIGURES to agent 2
    // (redistributing agent 1's figures)
    CmdAssignFiguresPayload assign2;
    assign2.window_id        = 2;
    assign2.figure_ids       = {10, 20, 30};   // agent 2 had figure 30, now gets 10+20
    assign2.active_figure_id = 30;

    Message assign_msg2;
    assign_msg2.header.type        = MessageType::CMD_ASSIGN_FIGURES;
    assign_msg2.header.window_id   = 2;
    assign_msg2.payload            = encode_cmd_assign_figures(assign2);
    assign_msg2.header.payload_len = static_cast<uint32_t>(assign_msg2.payload.size());
    ASSERT_TRUE(server_conn2->send(assign_msg2));

    auto recv2 = agent2_conn->recv();
    ASSERT_TRUE(recv2.has_value());
    auto decoded2 = decode_cmd_assign_figures(recv2->payload);
    ASSERT_TRUE(decoded2.has_value());
    EXPECT_EQ(decoded2->figure_ids.size(), 3u);

    // Cleanup
    agent1_conn->close();
    agent2_conn->close();
    server_conn1->close();
    server_conn2->close();
    server.close();
}

#endif   // __linux__

// ═══════════════════════════════════════════════════════════════════════════════
// Control Payload Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, CmdAssignFiguresRoundTrip)
{
    CmdAssignFiguresPayload p;
    p.window_id        = 42;
    p.figure_ids       = {1, 2, 3, 100};
    p.active_figure_id = 2;

    auto buf     = encode_cmd_assign_figures(p);
    auto decoded = decode_cmd_assign_figures(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 42u);
    ASSERT_EQ(decoded->figure_ids.size(), 4u);
    EXPECT_EQ(decoded->figure_ids[0], 1u);
    EXPECT_EQ(decoded->figure_ids[1], 2u);
    EXPECT_EQ(decoded->figure_ids[2], 3u);
    EXPECT_EQ(decoded->figure_ids[3], 100u);
    EXPECT_EQ(decoded->active_figure_id, 2u);
}

TEST(IpcCodec, CmdAssignFiguresEmpty)
{
    CmdAssignFiguresPayload p;
    p.window_id = 1;
    // No figures

    auto buf     = encode_cmd_assign_figures(p);
    auto decoded = decode_cmd_assign_figures(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 1u);
    EXPECT_TRUE(decoded->figure_ids.empty());
    EXPECT_EQ(decoded->active_figure_id, 0u);
}

TEST(IpcCodec, ReqCreateWindowRoundTrip)
{
    ReqCreateWindowPayload p;
    p.template_window_id = 7;

    auto buf     = encode_req_create_window(p);
    auto decoded = decode_req_create_window(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->template_window_id, 7u);
}

TEST(IpcCodec, ReqCreateWindowNoTemplate)
{
    ReqCreateWindowPayload p;
    // template_window_id defaults to INVALID_WINDOW

    auto buf     = encode_req_create_window(p);
    auto decoded = decode_req_create_window(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->template_window_id, INVALID_WINDOW);
}

TEST(IpcCodec, ReqCloseWindowRoundTrip)
{
    ReqCloseWindowPayload p;
    p.window_id = 5;
    p.reason    = "user_close";

    auto buf     = encode_req_close_window(p);
    auto decoded = decode_req_close_window(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 5u);
    EXPECT_EQ(decoded->reason, "user_close");
}

TEST(IpcCodec, ReqCloseWindowEmptyReason)
{
    ReqCloseWindowPayload p;
    p.window_id = 3;

    auto buf     = encode_req_close_window(p);
    auto decoded = decode_req_close_window(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 3u);
    EXPECT_TRUE(decoded->reason.empty());
}

TEST(IpcCodec, CmdRemoveFigureRoundTrip)
{
    CmdRemoveFigurePayload p;
    p.window_id = 10;
    p.figure_id = 42;

    auto buf     = encode_cmd_remove_figure(p);
    auto decoded = decode_cmd_remove_figure(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 10u);
    EXPECT_EQ(decoded->figure_id, 42u);
}

TEST(IpcCodec, CmdSetActiveRoundTrip)
{
    CmdSetActivePayload p;
    p.window_id = 5;
    p.figure_id = 99;

    auto buf     = encode_cmd_set_active(p);
    auto decoded = decode_cmd_set_active(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 5u);
    EXPECT_EQ(decoded->figure_id, 99u);
}

TEST(IpcCodec, CmdCloseWindowRoundTrip)
{
    CmdCloseWindowPayload p;
    p.window_id = 8;
    p.reason    = "backend_shutdown";

    auto buf     = encode_cmd_close_window(p);
    auto decoded = decode_cmd_close_window(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 8u);
    EXPECT_EQ(decoded->reason, "backend_shutdown");
}

TEST(IpcCodec, CmdAssignFiguresLargeList)
{
    CmdAssignFiguresPayload p;
    p.window_id = 1;
    for (uint64_t i = 1; i <= 100; ++i)
        p.figure_ids.push_back(i);
    p.active_figure_id = 50;

    auto buf     = encode_cmd_assign_figures(p);
    auto decoded = decode_cmd_assign_figures(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figure_ids.size(), 100u);
    EXPECT_EQ(decoded->figure_ids[0], 1u);
    EXPECT_EQ(decoded->figure_ids[99], 100u);
    EXPECT_EQ(decoded->active_figure_id, 50u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATE_SNAPSHOT Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, StateSnapshotEmpty)
{
    StateSnapshotPayload p;
    p.revision   = 42;
    p.session_id = 1;

    auto buf     = encode_state_snapshot(p);
    auto decoded = decode_state_snapshot(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->revision, 42u);
    EXPECT_EQ(decoded->session_id, 1u);
    EXPECT_TRUE(decoded->figures.empty());
}

TEST(IpcCodec, StateSnapshotSingleFigure)
{
    StateSnapshotPayload p;
    p.revision   = 1;
    p.session_id = 1;

    SnapshotFigureState fig;
    fig.figure_id = 10;
    fig.title     = "Test Figure";
    fig.width     = 800;
    fig.height    = 600;
    fig.grid_rows = 2;
    fig.grid_cols = 3;

    SnapshotAxisState ax;
    ax.x_min        = -5.0f;
    ax.x_max        = 5.0f;
    ax.y_min        = -10.0f;
    ax.y_max        = 10.0f;
    ax.grid_visible = false;
    ax.x_label      = "Time (s)";
    ax.y_label      = "Voltage (V)";
    ax.title        = "Channel 1";
    fig.axes.push_back(ax);

    SnapshotSeriesState s;
    s.name        = "Signal A";
    s.type        = "line";
    s.color_r     = 0.2f;
    s.color_g     = 0.4f;
    s.color_b     = 0.6f;
    s.color_a     = 0.8f;
    s.line_width  = 3.0f;
    s.marker_size = 8.0f;
    s.visible     = true;
    s.opacity     = 0.9f;
    s.point_count = 3;
    s.data        = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    fig.series.push_back(s);

    p.figures.push_back(fig);

    auto buf     = encode_state_snapshot(p);
    auto decoded = decode_state_snapshot(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->revision, 1u);
    ASSERT_EQ(decoded->figures.size(), 1u);

    const auto& df = decoded->figures[0];
    EXPECT_EQ(df.figure_id, 10u);
    EXPECT_EQ(df.title, "Test Figure");
    EXPECT_EQ(df.width, 800u);
    EXPECT_EQ(df.height, 600u);
    EXPECT_EQ(df.grid_rows, 2);
    EXPECT_EQ(df.grid_cols, 3);

    ASSERT_EQ(df.axes.size(), 1u);
    EXPECT_FLOAT_EQ(df.axes[0].x_min, -5.0f);
    EXPECT_FLOAT_EQ(df.axes[0].x_max, 5.0f);
    EXPECT_FLOAT_EQ(df.axes[0].y_min, -10.0f);
    EXPECT_FLOAT_EQ(df.axes[0].y_max, 10.0f);
    EXPECT_FALSE(df.axes[0].grid_visible);
    EXPECT_EQ(df.axes[0].x_label, "Time (s)");
    EXPECT_EQ(df.axes[0].y_label, "Voltage (V)");
    EXPECT_EQ(df.axes[0].title, "Channel 1");

    ASSERT_EQ(df.series.size(), 1u);
    EXPECT_EQ(df.series[0].name, "Signal A");
    EXPECT_EQ(df.series[0].type, "line");
    EXPECT_FLOAT_EQ(df.series[0].color_r, 0.2f);
    EXPECT_FLOAT_EQ(df.series[0].color_g, 0.4f);
    EXPECT_FLOAT_EQ(df.series[0].color_b, 0.6f);
    EXPECT_FLOAT_EQ(df.series[0].color_a, 0.8f);
    EXPECT_FLOAT_EQ(df.series[0].line_width, 3.0f);
    EXPECT_FLOAT_EQ(df.series[0].marker_size, 8.0f);
    EXPECT_TRUE(df.series[0].visible);
    EXPECT_FLOAT_EQ(df.series[0].opacity, 0.9f);
    EXPECT_EQ(df.series[0].point_count, 3u);
    ASSERT_EQ(df.series[0].data.size(), 6u);
    EXPECT_FLOAT_EQ(df.series[0].data[0], 1.0f);
    EXPECT_FLOAT_EQ(df.series[0].data[5], 6.0f);
}

TEST(IpcCodec, StateSnapshotMultipleFigures)
{
    StateSnapshotPayload p;
    p.revision   = 5;
    p.session_id = 1;

    for (int i = 0; i < 3; ++i)
    {
        SnapshotFigureState fig;
        fig.figure_id = static_cast<uint64_t>(i + 1);
        fig.title     = "Figure " + std::to_string(i + 1);
        p.figures.push_back(fig);
    }

    auto buf     = encode_state_snapshot(p);
    auto decoded = decode_state_snapshot(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 3u);
    EXPECT_EQ(decoded->figures[0].figure_id, 1u);
    EXPECT_EQ(decoded->figures[1].figure_id, 2u);
    EXPECT_EQ(decoded->figures[2].figure_id, 3u);
    EXPECT_EQ(decoded->figures[2].title, "Figure 3");
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATE_DIFF Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, StateDiffEmpty)
{
    StateDiffPayload p;
    p.base_revision = 1;
    p.new_revision  = 2;

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->base_revision, 1u);
    EXPECT_EQ(decoded->new_revision, 2u);
    EXPECT_TRUE(decoded->ops.empty());
}

TEST(IpcCodec, StateDiffAxisLimits)
{
    StateDiffPayload p;
    p.base_revision = 10;
    p.new_revision  = 11;

    DiffOp op;
    op.type       = DiffOp::Type::SET_AXIS_LIMITS;
    op.figure_id  = 1;
    op.axes_index = 0;
    op.f1         = -5.0f;
    op.f2         = 5.0f;
    op.f3         = -10.0f;
    op.f4         = 10.0f;
    p.ops.push_back(op);

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_LIMITS);
    EXPECT_EQ(decoded->ops[0].figure_id, 1u);
    EXPECT_EQ(decoded->ops[0].axes_index, 0u);
    EXPECT_FLOAT_EQ(decoded->ops[0].f1, -5.0f);
    EXPECT_FLOAT_EQ(decoded->ops[0].f2, 5.0f);
    EXPECT_FLOAT_EQ(decoded->ops[0].f3, -10.0f);
    EXPECT_FLOAT_EQ(decoded->ops[0].f4, 10.0f);
}

TEST(IpcCodec, StateDiffSeriesColor)
{
    StateDiffPayload p;
    p.base_revision = 20;
    p.new_revision  = 21;

    DiffOp op;
    op.type         = DiffOp::Type::SET_SERIES_COLOR;
    op.figure_id    = 2;
    op.series_index = 1;
    op.f1           = 1.0f;
    op.f2           = 0.0f;
    op.f3           = 0.0f;
    op.f4           = 1.0f;
    p.ops.push_back(op);

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_SERIES_COLOR);
    EXPECT_FLOAT_EQ(decoded->ops[0].f1, 1.0f);
    EXPECT_FLOAT_EQ(decoded->ops[0].f2, 0.0f);
}

TEST(IpcCodec, StateDiffFigureTitle)
{
    StateDiffPayload p;
    p.base_revision = 5;
    p.new_revision  = 6;

    DiffOp op;
    op.type      = DiffOp::Type::SET_FIGURE_TITLE;
    op.figure_id = 3;
    op.str_val   = "Renamed Figure";
    p.ops.push_back(op);

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_FIGURE_TITLE);
    EXPECT_EQ(decoded->ops[0].str_val, "Renamed Figure");
}

TEST(IpcCodec, StateDiffSeriesData)
{
    StateDiffPayload p;
    p.base_revision = 100;
    p.new_revision  = 101;

    DiffOp op;
    op.type         = DiffOp::Type::SET_SERIES_DATA;
    op.figure_id    = 1;
    op.series_index = 0;
    op.data         = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    p.ops.push_back(op);

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_SERIES_DATA);
    ASSERT_EQ(decoded->ops[0].data.size(), 6u);
    EXPECT_FLOAT_EQ(decoded->ops[0].data[0], 0.0f);
    EXPECT_FLOAT_EQ(decoded->ops[0].data[5], 5.0f);
}

TEST(IpcCodec, StateDiffMultipleOps)
{
    StateDiffPayload p;
    p.base_revision = 50;
    p.new_revision  = 53;

    DiffOp op1;
    op1.type      = DiffOp::Type::SET_AXIS_LIMITS;
    op1.figure_id = 1;
    op1.f1        = 0.0f;
    op1.f2        = 100.0f;
    op1.f3        = 0.0f;
    op1.f4        = 100.0f;
    p.ops.push_back(op1);

    DiffOp op2;
    op2.type         = DiffOp::Type::SET_SERIES_VISIBLE;
    op2.figure_id    = 1;
    op2.series_index = 2;
    op2.bool_val     = false;
    p.ops.push_back(op2);

    DiffOp op3;
    op3.type         = DiffOp::Type::SET_OPACITY;
    op3.figure_id    = 1;
    op3.series_index = 0;
    op3.f1           = 0.5f;
    p.ops.push_back(op3);

    auto buf     = encode_state_diff(p);
    auto decoded = decode_state_diff(buf);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 3u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_LIMITS);
    EXPECT_EQ(decoded->ops[1].type, DiffOp::Type::SET_SERIES_VISIBLE);
    EXPECT_FALSE(decoded->ops[1].bool_val);
    EXPECT_EQ(decoded->ops[2].type, DiffOp::Type::SET_OPACITY);
    EXPECT_FLOAT_EQ(decoded->ops[2].f1, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ACK_STATE Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, AckStateRoundTrip)
{
    AckStatePayload p;
    p.revision = 999;

    auto buf     = encode_ack_state(p);
    auto decoded = decode_ack_state(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->revision, 999u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// EVT_INPUT Encode/Decode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, EvtInputRoundTrip)
{
    EvtInputPayload p;
    p.window_id  = 5;
    p.input_type = EvtInputPayload::InputType::SCROLL;
    p.key        = 0;
    p.mods       = 3;
    p.x          = 123.456;
    p.y          = 789.012;
    p.figure_id  = 42;
    p.axes_index = 1;

    auto buf     = encode_evt_input(p);
    auto decoded = decode_evt_input(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 5u);
    EXPECT_EQ(decoded->input_type, EvtInputPayload::InputType::SCROLL);
    EXPECT_EQ(decoded->key, 0);
    EXPECT_EQ(decoded->mods, 3);
    EXPECT_NEAR(decoded->x, 123.456, 0.001);
    EXPECT_NEAR(decoded->y, 789.012, 0.001);
    EXPECT_EQ(decoded->figure_id, 42u);
    EXPECT_EQ(decoded->axes_index, 1u);
}

TEST(IpcCodec, EvtInputKeyPress)
{
    EvtInputPayload p;
    p.window_id  = 1;
    p.input_type = EvtInputPayload::InputType::KEY_PRESS;
    p.key        = 65;   // 'A'
    p.mods       = 1;    // Shift

    auto buf     = encode_evt_input(p);
    auto decoded = decode_evt_input(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->input_type, EvtInputPayload::InputType::KEY_PRESS);
    EXPECT_EQ(decoded->key, 65);
    EXPECT_EQ(decoded->mods, 1);
}

TEST(IpcCodec, EvtInputMouseMove)
{
    EvtInputPayload p;
    p.window_id  = 2;
    p.input_type = EvtInputPayload::InputType::MOUSE_MOVE;
    p.x          = 500.5;
    p.y          = 300.25;
    p.figure_id  = 1;
    p.axes_index = 0;

    auto buf     = encode_evt_input(p);
    auto decoded = decode_evt_input(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->input_type, EvtInputPayload::InputType::MOUSE_MOVE);
    EXPECT_NEAR(decoded->x, 500.5, 0.001);
    EXPECT_NEAR(decoded->y, 300.25, 0.001);
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ_DETACH_FIGURE Codec
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCodec, ReqDetachFigureRoundTrip)
{
    ReqDetachFigurePayload p;
    p.source_window_id = 42;
    p.figure_id        = 7;
    p.width            = 1024;
    p.height           = 768;
    p.screen_x         = 200;
    p.screen_y         = 150;

    auto buf = encode_req_detach_figure(p);
    ASSERT_FALSE(buf.empty());

    auto decoded = decode_req_detach_figure(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source_window_id, 42u);
    EXPECT_EQ(decoded->figure_id, 7u);
    EXPECT_EQ(decoded->width, 1024u);
    EXPECT_EQ(decoded->height, 768u);
    EXPECT_EQ(decoded->screen_x, 200);
    EXPECT_EQ(decoded->screen_y, 150);
}

TEST(IpcCodec, ReqDetachFigureDefaults)
{
    ReqDetachFigurePayload p;
    auto                   buf     = encode_req_detach_figure(p);
    auto                   decoded = decode_req_detach_figure(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source_window_id, INVALID_WINDOW);
    EXPECT_EQ(decoded->figure_id, 0u);
    EXPECT_EQ(decoded->width, 800u);
    EXPECT_EQ(decoded->height, 600u);
    EXPECT_EQ(decoded->screen_x, 0);
    EXPECT_EQ(decoded->screen_y, 0);
}

TEST(IpcCodec, ReqDetachFigureNegativeCoords)
{
    ReqDetachFigurePayload p;
    p.screen_x = -100;
    p.screen_y = -50;

    auto buf     = encode_req_detach_figure(p);
    auto decoded = decode_req_detach_figure(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->screen_x, -100);
    EXPECT_EQ(decoded->screen_y, -50);
}
