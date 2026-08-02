#include "agent_message_handler.hpp"

#include <iostream>
#include <unordered_map>

#include "../ipc/codec.hpp"

#ifndef _WIN32
    #include <csignal>
    #include <unistd.h>
#endif

namespace spectra::daemon
{

HandleResult handle_hello(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto       hello = ipc::decode_hello(msg.payload);
    ClientType ctype = ClientType::AGENT;
    if (hello)
    {
        ctype = classify_client(*hello);
        std::cerr << "[spectra-backend] HELLO from "
                  << (ctype == ClientType::PYTHON      ? "python"
                      : ctype == ClientType::APP       ? "app"
                      : ctype == ClientType::PUBLISHER ? "publisher"
                                                       : "agent")
                  << " (build=" << hello->agent_build << ", client_type=" << hello->client_type
                  << ")\n";
    }

    slot.client_type = ctype;
    if (ctype == ClientType::APP)
        slot.is_source_client = true;

    // Python clients and app clients are NOT render agents —
    // don't add them to the session graph.
    ipc::WindowId wid = ipc::INVALID_WINDOW;
    if (ctype == ClientType::AGENT)
    {
        // Try to claim a pre-registered agent slot (created by
        // STATE_SNAPSHOT or REQ_DETACH_FIGURE handlers).
        // If none available, register as a brand-new agent.
        wid = ctx.graph.claim_pending_agent(slot.conn->fd());
        if (wid == ipc::INVALID_WINDOW)
            wid = ctx.graph.add_agent(0, slot.conn->fd());
    }
    slot.window_id      = wid;
    slot.handshake_done = true;

    // Send WELCOME
    ipc::WelcomePayload wp;
    wp.session_id   = ctx.graph.session_id();
    wp.window_id    = wid;
    wp.process_id   = static_cast<ipc::ProcessId>(::getpid());
    wp.heartbeat_ms = 5000;
    wp.mode         = "multiproc";

    ipc::Message reply;
    reply.header.type        = ipc::MessageType::WELCOME;
    reply.header.session_id  = wp.session_id;
    reply.header.window_id   = wid;
    reply.payload            = ipc::encode_welcome(wp);
    reply.header.payload_len = static_cast<uint32_t>(reply.payload.size());
    slot.conn->send(reply);

    // For agents: send figure assignments and state snapshot
    if (ctype == ClientType::AGENT)
    {
        auto assigned = ctx.graph.figures_for_window(wid);
        if (!assigned.empty())
        {
            send_assign_figures(*slot.conn, wid, ctx.graph.session_id(), assigned, assigned[0]);
        }

        auto snap = ctx.fig_model.snapshot(assigned);
        send_state_snapshot(*slot.conn, wid, ctx.graph.session_id(), snap);

        std::cerr << "[spectra-backend] Assigned window_id=" << wid << " with " << assigned.size()
                  << " figures\n";
    }
    return HandleResult::Continue;
}

HandleResult handle_heartbeat(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& /*msg*/)
{
    if (slot.window_id != ipc::INVALID_WINDOW)
        ctx.graph.heartbeat(slot.window_id);
    return HandleResult::Continue;
}

HandleResult handle_req_create_window(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    std::cerr << "[spectra-backend] REQ_CREATE_WINDOW from window=" << slot.window_id << "\n";

    pid_t pid = ctx.proc_mgr.spawn_agent();
    if (pid > 0)
    {
        std::cerr << "[spectra-backend] Spawned new agent pid=" << pid << "\n";
        send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id, slot.window_id);
    }
    else
    {
        std::cerr << "[spectra-backend] Failed to spawn agent\n";
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      500,
                      "Failed to spawn agent");
    }
    return HandleResult::Continue;
}

HandleResult handle_req_close_window(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto          close_req  = ipc::decode_req_close_window(msg.payload);
    ipc::WindowId target_wid = slot.window_id;
    if (close_req && close_req->window_id != ipc::INVALID_WINDOW)
        target_wid = close_req->window_id;

    std::cerr << "[spectra-backend] REQ_CLOSE_WINDOW window=" << target_wid
              << " reason=" << (close_req ? close_req->reason : "unknown") << "\n";

    // Remove agent from graph, get orphaned figures
    auto orphaned = ctx.graph.remove_agent(target_wid);

    // Redistribute orphaned figures to first remaining agent
    if (!orphaned.empty())
    {
        auto remaining = ctx.graph.all_window_ids();
        if (!remaining.empty())
        {
            auto target = remaining[0];
            for (auto fid : orphaned)
                ctx.graph.assign_figure(fid, target);

            std::cerr << "[spectra-backend] Redistributed " << orphaned.size()
                      << " figures to window=" << target << "\n";

            auto figs = ctx.graph.figures_for_window(target);
            for (auto& c : ctx.clients)
            {
                if (c.window_id == target && c.conn)
                {
                    send_assign_figures(*c.conn,
                                        target,
                                        ctx.graph.session_id(),
                                        figs,
                                        figs.empty() ? 0 : figs[0]);
                    break;
                }
            }
        }
        else
        {
            std::cerr << "[spectra-backend] No remaining agents for " << orphaned.size()
                      << " orphaned figures\n";
        }
    }

    // Send CMD_CLOSE_WINDOW to the target agent
    if (target_wid == slot.window_id)
    {
        send_close_window(*slot.conn, target_wid, ctx.graph.session_id(), "close_ack");
        slot.conn->close();
        return HandleResult::EraseAndContinue;
    }

    // Close a different window
    for (auto& c : ctx.clients)
    {
        if (c.window_id == target_wid && c.conn)
        {
            send_close_window(*c.conn, target_wid, ctx.graph.session_id(), "close_ack");
            c.conn->close();
            break;
        }
    }
    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);

    return HandleResult::Continue;
}

HandleResult handle_req_detach_figure(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto detach = ipc::decode_req_detach_figure(msg.payload);
    if (!detach)
        return HandleResult::Continue;

    std::cerr << "[spectra-backend] REQ_DETACH_FIGURE: figure=" << detach->figure_id
              << " from window=" << detach->source_window_id << " → new window at ("
              << detach->screen_x << "," << detach->screen_y << ")\n";

    // Verify the figure exists
    if (!ctx.fig_model.has_figure(detach->figure_id))
    {
        std::cerr << "[spectra-backend] Figure " << detach->figure_id
                  << " not found, ignoring detach\n";
        return HandleResult::Continue;
    }

    // Remove figure from source agent in session graph
    ctx.graph.unassign_figure(detach->figure_id, detach->source_window_id);

    // Notify source agent to remove the figure
    {
        ipc::CmdRemoveFigurePayload rm;
        rm.window_id = detach->source_window_id;
        rm.figure_id = detach->figure_id;
        ipc::Message rm_msg;
        rm_msg.header.type        = ipc::MessageType::CMD_REMOVE_FIGURE;
        rm_msg.header.session_id  = ctx.graph.session_id();
        rm_msg.header.window_id   = detach->source_window_id;
        rm_msg.payload            = ipc::encode_cmd_remove_figure(rm);
        rm_msg.header.payload_len = static_cast<uint32_t>(rm_msg.payload.size());
        for (auto& c : ctx.clients)
        {
            if (c.window_id == detach->source_window_id && c.conn)
            {
                c.conn->send(rm_msg);
                break;
            }
        }
    }

    // Spawn a new agent process for the detached figure.
    auto new_wid = ctx.graph.add_agent(0, -1);
    ctx.graph.assign_figure(detach->figure_id, new_wid);
    ctx.graph.heartbeat(new_wid);

    std::cerr << "[spectra-backend] Spawning new agent for detached figure, window=" << new_wid
              << "\n";

    ctx.proc_mgr.spawn_agent_for_window(new_wid);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_evt_window(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& /*msg*/)
{
    // Agent reports window event (e.g. close_requested)
    std::cerr << "[spectra-backend] EVT_WINDOW from window=" << slot.window_id << "\n";

    if (slot.window_id != ipc::INVALID_WINDOW)
    {
        auto orphaned = ctx.graph.remove_agent(slot.window_id);
        std::cerr << "[spectra-backend] Agent closed (window=" << slot.window_id
                  << ", orphaned_figures=" << orphaned.size() << ")\n";

        // Notify Python clients about closed figures
        for (auto fid : orphaned)
            notify_python_window_closed(ctx, fid, slot.window_id, "user_close");

        // Redistribute orphaned figures
        redistribute_orphaned_figures(ctx, orphaned);
    }
    slot.conn->close();
    return HandleResult::EraseAndContinue;
}

HandleResult handle_evt_input(DaemonContext& ctx, ClientSlot& /*slot*/, const ipc::Message& msg)
{
    auto input = ipc::decode_evt_input(msg.payload);
    if (!input)
        return HandleResult::Continue;

    // All model mutations go through the backend's FigureModel.
    ipc::StateDiffPayload diff;
    auto                  base_rev = ctx.fig_model.revision();

    switch (input->input_type)
    {
        case ipc::EvtInputPayload::InputType::SCROLL:
        {
            auto snap = ctx.fig_model.snapshot({input->figure_id});
            if (!snap.figures.empty() && input->axes_index < snap.figures[0].axes.size())
            {
                const auto& ax   = snap.figures[0].axes[input->axes_index];
                double      zoom = 1.0 - input->y * 0.1;
                if (zoom < 0.1)
                    zoom = 0.1;
                if (zoom > 10.0)
                    zoom = 10.0;
                double cx = (ax.x_min + ax.x_max) * 0.5;
                double cy = (ax.y_min + ax.y_max) * 0.5;
                double hw = (ax.x_max - ax.x_min) * 0.5 * zoom;
                double hh = (ax.y_max - ax.y_min) * 0.5 * zoom;
                auto   op = ctx.fig_model.set_axis_limits(input->figure_id,
                                                        input->axes_index,
                                                        cx - hw,
                                                        cx + hw,
                                                        cy - hh,
                                                        cy + hh);
                diff.ops.push_back(op);
            }
            break;
        }

        case ipc::EvtInputPayload::InputType::KEY_PRESS:
        {
            if (input->key == 'G' || input->key == 'g')
            {
                auto snap = ctx.fig_model.snapshot({input->figure_id});
                if (!snap.figures.empty() && input->axes_index < snap.figures[0].axes.size())
                {
                    bool cur = snap.figures[0].axes[input->axes_index].grid_visible;
                    auto op =
                        ctx.fig_model.set_grid_visible(input->figure_id, input->axes_index, !cur);
                    diff.ops.push_back(op);
                }
            }
            break;
        }

        case ipc::EvtInputPayload::InputType::KEY_RELEASE:
        case ipc::EvtInputPayload::InputType::MOUSE_BUTTON:
        case ipc::EvtInputPayload::InputType::MOUSE_MOVE:
            // Reserved for future interaction (pan, selection, etc.)
            break;
    }

    // Broadcast STATE_DIFF to ALL agents (including sender)
    if (!diff.ops.empty())
    {
        diff.base_revision = base_rev;
        diff.new_revision  = ctx.fig_model.revision();
        broadcast_diff_to_all(ctx, diff);
    }
    return HandleResult::Continue;
}

HandleResult handle_state_snapshot(DaemonContext& ctx,
                                   ClientSlot& /*slot*/,
                                   const ipc::Message& msg)
{
    // App client pushes its figures to the backend.
    auto incoming = ipc::decode_state_snapshot(msg.payload);
    if (!incoming || incoming->figures.empty())
    {
        std::cerr << "[spectra-backend] STATE_SNAPSHOT: empty or decode failed\n";
        return HandleResult::Continue;
    }

    std::cerr << "[spectra-backend] STATE_SNAPSHOT: received " << incoming->figures.size()
              << " figure(s) from app\n";

    auto new_ids = ctx.fig_model.load_snapshot(*incoming);

    // Register all figures in session graph
    for (size_t i = 0; i < new_ids.size(); ++i)
    {
        const auto& fig = incoming->figures[i];
        ctx.graph.register_figure(
            new_ids[i],
            fig.title.empty() ? "Figure " + std::to_string(i + 1) : fig.title);
    }

    // Group figures by window_group and spawn one agent per group.
    std::unordered_map<uint32_t, std::vector<size_t>> groups;
    std::vector<size_t>                               ungrouped;
    for (size_t fi = 0; fi < new_ids.size(); ++fi)
    {
        uint32_t wg = incoming->figures[fi].window_group;
        if (wg != 0)
            groups[wg].push_back(fi);
        else
            ungrouped.push_back(fi);
    }

    // Spawn one agent per group
    for (auto& [wg, fig_indices] : groups)
    {
        auto pre_wid = ctx.graph.add_agent(0, -1);
        for (size_t fi : fig_indices)
            ctx.graph.assign_figure(new_ids[fi], pre_wid);
        ctx.graph.heartbeat(pre_wid);

        pid_t pid = ctx.proc_mgr.spawn_agent();
        if (pid <= 0)
        {
            std::cerr << "[spectra-backend] Failed to spawn agent for group " << wg << "\n";
        }
        else
        {
            std::cerr << "[spectra-backend] Spawned agent pid=" << pid << " for group " << wg
                      << " with " << fig_indices.size() << " figure(s)"
                      << " (pre-assigned window=" << pre_wid << ")\n";
        }
    }

    // Spawn one agent per ungrouped figure
    for (size_t fi : ungrouped)
    {
        auto pre_wid = ctx.graph.add_agent(0, -1);
        ctx.graph.assign_figure(new_ids[fi], pre_wid);
        ctx.graph.heartbeat(pre_wid);

        pid_t pid = ctx.proc_mgr.spawn_agent();
        if (pid <= 0)
        {
            std::cerr << "[spectra-backend] Failed to spawn agent for figure " << new_ids[fi]
                      << "\n";
        }
        else
        {
            std::cerr << "[spectra-backend] Spawned agent pid=" << pid << " for figure "
                      << new_ids[fi] << " (pre-assigned window=" << pre_wid << ")\n";
        }
    }
    return HandleResult::Continue;
}

HandleResult handle_state_diff(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    // Decode, apply to FigureModel, and forward.
    auto incoming_diff = ipc::decode_state_diff(msg.payload);
    if (!incoming_diff || incoming_diff->ops.empty())
        return HandleResult::Continue;

    auto base_rev = ctx.fig_model.revision();
    for (const auto& op : incoming_diff->ops)
        ctx.fig_model.apply_diff_op(op);

    ipc::StateDiffPayload fwd_diff;
    fwd_diff.ops           = incoming_diff->ops;
    fwd_diff.base_revision = base_rev;
    fwd_diff.new_revision  = ctx.fig_model.revision();

    bool from_source = slot.is_source_client;
    for (auto& c : ctx.clients)
    {
        if (!c.conn || !c.handshake_done)
            continue;
        if (from_source)
        {
            // App → agents: forward to all render agents
            if (!c.is_source_client)
                send_state_diff(*c.conn, c.window_id, ctx.graph.session_id(), fwd_diff);
        }
        else
        {
            // Agent → app: forward to source client
            if (c.is_source_client)
                send_state_diff(*c.conn, c.window_id, ctx.graph.session_id(), fwd_diff);
        }
    }
    return HandleResult::Continue;
}

}   // namespace spectra::daemon
