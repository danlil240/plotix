#include "python_message_handler.hpp"

#include <spectra/logger.hpp>

#include "../ipc/codec.hpp"

#include <cmath>

#ifndef _WIN32
    #include <unistd.h>
#endif

namespace spectra::daemon
{

// ─── Helper: apply a single property update to FigureModel ─────────────────
// Shared by handle_req_update_property and handle_req_update_batch.
static void apply_property_update(FigureModel&           fig_model,
                                  uint64_t               figure_id,
                                  uint32_t               axes_index,
                                  uint32_t               series_index,
                                  const std::string&     property,
                                  float                  f1,
                                  float                  f2,
                                  float                  f3,
                                  float                  f4,
                                  bool                   bool_val,
                                  const std::string&     str_val,
                                  ipc::StateDiffPayload& diff)
{
    if (property == "color")
    {
        diff.ops.push_back(fig_model.set_series_color(figure_id, series_index, f1, f2, f3, f4));
    }
    else if (property == "xlim")
    {
        double cx0 = NAN;
        double cx1 = NAN;
        double cy0 = NAN;
        double cy1 = NAN;
        fig_model.get_axis_limits(figure_id, axes_index, cx0, cx1, cy0, cy1);
        diff.ops.push_back(fig_model.set_axis_limits(figure_id, axes_index, f1, f2, cy0, cy1));
    }
    else if (property == "ylim")
    {
        double cx0 = NAN;
        double cx1 = NAN;
        double cy0 = NAN;
        double cy1 = NAN;
        fig_model.get_axis_limits(figure_id, axes_index, cx0, cx1, cy0, cy1);
        diff.ops.push_back(fig_model.set_axis_limits(figure_id, axes_index, cx0, cx1, f1, f2));
    }
    else if (property == "zlim")
    {
        diff.ops.push_back(fig_model.set_axis_zlimits(figure_id, axes_index, f1, f2));
    }
    else if (property == "title")
    {
        diff.ops.push_back(fig_model.set_figure_title(figure_id, str_val));
    }
    else if (property == "grid")
    {
        diff.ops.push_back(fig_model.set_grid_visible(figure_id, axes_index, bool_val));
    }
    else if (property == "visible")
    {
        diff.ops.push_back(fig_model.set_series_visible(figure_id, series_index, bool_val));
    }
    else if (property == "line_width")
    {
        diff.ops.push_back(fig_model.set_line_width(figure_id, series_index, f1));
    }
    else if (property == "marker_size")
    {
        diff.ops.push_back(fig_model.set_marker_size(figure_id, series_index, f1));
    }
    else if (property == "opacity")
    {
        diff.ops.push_back(fig_model.set_opacity(figure_id, series_index, f1));
    }
    else if (property == "xlabel")
    {
        diff.ops.push_back(fig_model.set_axis_xlabel(figure_id, axes_index, str_val));
    }
    else if (property == "ylabel")
    {
        diff.ops.push_back(fig_model.set_axis_ylabel(figure_id, axes_index, str_val));
    }
    else if (property == "axes_title")
    {
        diff.ops.push_back(fig_model.set_axis_title(figure_id, axes_index, str_val));
    }
    else if (property == "label")
    {
        diff.ops.push_back(fig_model.set_series_label(figure_id, series_index, str_val));
    }
    else if (property == "legend" || property == "legend_visible")
    {
        // Legend visibility is client-side UI state; acknowledge silently.
    }
    else if (property == "add_knob")
    {
        ipc::SnapshotKnobState ks;
        ks.name    = str_val;
        ks.type    = 0;
        ks.value   = f1;
        ks.min_val = f2;
        ks.max_val = f3;
        fig_model.add_knob(ks);
    }
}

// ─── Handlers ──────────────────────────────────────────────────────────────

HandleResult handle_req_create_figure(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_create_figure(msg.payload);
    if (!req)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_CREATE_FIGURE payload");
        return HandleResult::Continue;
    }

    auto fid = ctx.fig_model.create_figure(req->title.empty() ? "Figure" : req->title,
                                           req->width,
                                           req->height);
    ctx.graph.register_figure(fid, req->title);

    SPECTRA_LOG_DEBUG("daemon", "Python: created figure {} title={}", fid, req->title);

    ipc::RespFigureCreatedPayload resp;
    resp.request_id = msg.header.request_id;
    resp.figure_id  = fid;
    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_FIGURE_CREATED,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_resp_figure_created(resp));
    return HandleResult::Continue;
}

HandleResult handle_req_create_axes(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_create_axes(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    ctx.fig_model.set_grid(req->figure_id, req->grid_rows, req->grid_cols);

    auto axes_idx = ctx.fig_model.add_axes(req->figure_id, 0.0f, 1.0f, 0.0f, 1.0f, req->is_3d);

    SPECTRA_LOG_DEBUG("daemon",
                      "Python: created axes {}{} in figure {}",
                      axes_idx,
                      req->is_3d ? " (3D)" : "",
                      req->figure_id);

    // Broadcast ADD_AXES diff to all agents
    {
        ipc::DiffOp add_op;
        add_op.type       = ipc::DiffOp::Type::ADD_AXES;
        add_op.figure_id  = req->figure_id;
        add_op.axes_index = axes_idx;
        add_op.bool_val   = req->is_3d;

        ipc::StateDiffPayload diff;
        diff.base_revision = ctx.fig_model.revision() - 1;
        diff.new_revision  = ctx.fig_model.revision();
        diff.ops.push_back(add_op);
        broadcast_diff_to_agents(ctx, diff);
    }

    ipc::RespAxesCreatedPayload resp;
    resp.request_id = msg.header.request_id;
    resp.axes_index = axes_idx;
    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_AXES_CREATED,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_resp_axes_created(resp));
    return HandleResult::Continue;
}

HandleResult handle_req_add_series(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_add_series(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    uint32_t series_idx = 0;
    auto     add_op     = ctx.fig_model.add_series_with_diff(req->figure_id,
                                                     req->label,
                                                     req->series_type,
                                                     req->axes_index,
                                                     series_idx);

    SPECTRA_LOG_DEBUG("daemon",
                      "Python: added series {} type={} in figure {}",
                      series_idx,
                      req->series_type,
                      req->figure_id);

    // Broadcast ADD_SERIES diff to all agents
    {
        ipc::StateDiffPayload diff;
        diff.base_revision = ctx.fig_model.revision() - 1;
        diff.new_revision  = ctx.fig_model.revision();
        diff.ops.push_back(add_op);

        // If the series was created with a label, also send
        // SET_SERIES_LABEL so the agent picks it up immediately.
        if (!req->label.empty())
        {
            ipc::DiffOp label_op;
            label_op.type         = ipc::DiffOp::Type::SET_SERIES_LABEL;
            label_op.figure_id    = req->figure_id;
            label_op.series_index = series_idx;
            label_op.str_val      = req->label;
            diff.ops.push_back(label_op);
        }

        broadcast_diff_to_agents(ctx, diff);
    }

    ipc::RespSeriesAddedPayload resp;
    resp.request_id   = msg.header.request_id;
    resp.series_index = series_idx;
    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_SERIES_ADDED,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_resp_series_added(resp));
    return HandleResult::Continue;
}

HandleResult handle_req_set_data(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_set_data(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    auto op = ctx.fig_model.set_series_data(req->figure_id, req->series_index, req->data);

    ipc::StateDiffPayload diff;
    diff.base_revision = ctx.fig_model.revision() - 1;
    diff.new_revision  = ctx.fig_model.revision();
    diff.ops.push_back(op);
    broadcast_diff_to_agents(ctx, diff);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_update_property(DaemonContext&      ctx,
                                        ClientSlot&         slot,
                                        const ipc::Message& msg)
{
    auto req = ipc::decode_req_update_property(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    ipc::StateDiffPayload diff;
    auto                  base_rev = ctx.fig_model.revision();

    // Check for unknown property before applying
    static const char* known_props[] = {"color",
                                        "xlim",
                                        "ylim",
                                        "zlim",
                                        "title",
                                        "grid",
                                        "visible",
                                        "line_width",
                                        "marker_size",
                                        "opacity",
                                        "xlabel",
                                        "ylabel",
                                        "axes_title",
                                        "label",
                                        "legend",
                                        "legend_visible",
                                        "add_knob"};
    bool               known         = false;
    for (const auto* p : known_props)
    {
        if (req->property == p)
        {
            known = true;
            break;
        }
    }
    if (!known)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Unknown property: " + req->property);
        return HandleResult::Continue;
    }

    apply_property_update(ctx.fig_model,
                          req->figure_id,
                          req->axes_index,
                          req->series_index,
                          req->property,
                          req->f1,
                          req->f2,
                          req->f3,
                          req->f4,
                          req->bool_val,
                          req->str_val,
                          diff);

    // Broadcast diff to agents
    if (!diff.ops.empty())
    {
        diff.base_revision = base_rev;
        diff.new_revision  = ctx.fig_model.revision();
        broadcast_diff_to_agents(ctx, diff);
    }

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_show(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_show(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    // If window_id is specified and valid, add figure as tab to existing window
    if (req->window_id != ipc::INVALID_WINDOW && ctx.graph.agent(req->window_id) != nullptr)
    {
        SPECTRA_LOG_DEBUG("daemon",
                          "Python: REQ_SHOW figure={} as tab in window={}",
                          req->figure_id,
                          req->window_id);

        ctx.graph.assign_figure(req->figure_id, req->window_id);

        auto assigned = ctx.graph.figures_for_window(req->window_id);
        for (auto& c : ctx.clients)
        {
            if (c.window_id == req->window_id && c.conn)
            {
                send_assign_figures(*c.conn,
                                    req->window_id,
                                    ctx.graph.session_id(),
                                    assigned,
                                    assigned.empty() ? req->figure_id : assigned[0]);

                auto snap = ctx.fig_model.snapshot(assigned);
                send_state_snapshot(*c.conn, req->window_id, ctx.graph.session_id(), snap);
                break;
            }
        }

        send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id, req->window_id);
    }
    else
    {
        // No target window — spawn a new agent
        SPECTRA_LOG_DEBUG("daemon", "Python: REQ_SHOW figure={}", req->figure_id);

        auto new_wid = ctx.graph.add_agent(0, -1);
        ctx.graph.assign_figure(req->figure_id, new_wid);
        ctx.graph.heartbeat(new_wid);

        pid_t pid = ctx.proc_mgr.spawn_agent();
        if (pid > 0)
        {
            SPECTRA_LOG_INFO("daemon",
                             "Spawned agent pid={} for figure {} (window={})",
                             pid,
                             req->figure_id,
                             new_wid);
            send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id, new_wid);
        }
        else
        {
            ctx.graph.remove_agent(new_wid);
            send_resp_err(*slot.conn,
                          ctx.graph.session_id(),
                          msg.header.request_id,
                          500,
                          "Failed to spawn agent");
        }
    }
    return HandleResult::Continue;
}

HandleResult handle_req_append_data(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_append_data(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    auto op = ctx.fig_model.append_series_data(req->figure_id, req->series_index, req->data);

    ipc::StateDiffPayload diff;
    diff.base_revision = ctx.fig_model.revision() - 1;
    diff.new_revision  = ctx.fig_model.revision();
    diff.ops.push_back(op);
    broadcast_diff_to_agents(ctx, diff);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_remove_series(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_remove_series(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    auto op = ctx.fig_model.remove_series(req->figure_id, req->series_index);

    SPECTRA_LOG_DEBUG("daemon",
                      "Python: removed series {} from figure {}",
                      req->series_index,
                      req->figure_id);

    ipc::StateDiffPayload diff;
    diff.base_revision = ctx.fig_model.revision() - 1;
    diff.new_revision  = ctx.fig_model.revision();
    diff.ops.push_back(op);
    broadcast_diff_to_agents(ctx, diff);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_close_figure(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_close_figure(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    SPECTRA_LOG_DEBUG("daemon",
                      "Python: REQ_CLOSE_FIGURE figure={} (closing window, keeping figure)",
                      req->figure_id);

    // Find and close agent windows displaying this figure
    for (auto wid : ctx.graph.all_window_ids())
    {
        auto figs    = ctx.graph.figures_for_window(wid);
        bool has_fig = false;
        for (auto fid : figs)
        {
            if (fid == req->figure_id)
            {
                has_fig = true;
                break;
            }
        }
        if (!has_fig)
            continue;

        for (auto& c : ctx.clients)
        {
            if (c.window_id == wid && c.conn)
            {
                ipc::Message close_msg;
                close_msg.header.type        = ipc::MessageType::CMD_CLOSE_WINDOW;
                close_msg.header.session_id  = ctx.graph.session_id();
                close_msg.header.window_id   = wid;
                close_msg.payload            = {};
                close_msg.header.payload_len = 0;
                c.conn->send(close_msg);
                break;
            }
        }
    }

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_update_batch(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_update_batch(msg.payload);
    if (!req || req->updates.empty())
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_UPDATE_BATCH payload");
        return HandleResult::Continue;
    }

    ipc::StateDiffPayload diff;
    auto                  base_rev = ctx.fig_model.revision();

    for (const auto& upd : req->updates)
    {
        if (!ctx.fig_model.has_figure(upd.figure_id))
            continue;

        apply_property_update(ctx.fig_model,
                              upd.figure_id,
                              upd.axes_index,
                              upd.series_index,
                              upd.property,
                              upd.f1,
                              upd.f2,
                              upd.f3,
                              upd.f4,
                              upd.bool_val,
                              upd.str_val,
                              diff);
    }

    SPECTRA_LOG_DEBUG("daemon",
                      "Python: batch update with {} items, {} applied",
                      req->updates.size(),
                      diff.ops.size());

    // Broadcast diff to agents
    if (!diff.ops.empty())
    {
        diff.base_revision = base_rev;
        diff.new_revision  = ctx.fig_model.revision();
        broadcast_diff_to_agents(ctx, diff);
    }

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_destroy_figure(DaemonContext&      ctx,
                                       ClientSlot&         slot,
                                       const ipc::Message& msg)
{
    auto req = ipc::decode_req_destroy_figure(msg.payload);
    if (!req)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad payload");
        return HandleResult::Continue;
    }

    SPECTRA_LOG_DEBUG("daemon", "Python: REQ_DESTROY_FIGURE figure={}", req->figure_id);

    ctx.fig_model.remove_figure(req->figure_id);
    ctx.graph.remove_figure(req->figure_id);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_list_figures(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    ipc::RespFigureListPayload resp;
    resp.request_id = msg.header.request_id;
    resp.figure_ids = ctx.fig_model.all_figure_ids();

    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_FIGURE_LIST,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_resp_figure_list(resp));
    return HandleResult::Continue;
}

HandleResult handle_req_reconnect(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_reconnect(msg.payload);
    if (!req)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_RECONNECT payload");
        return HandleResult::Continue;
    }

    SPECTRA_LOG_DEBUG("daemon", "Python: REQ_RECONNECT session={}", req->session_id);

    // Verify session ID matches (or accept any if 0)
    if (req->session_id != 0 && req->session_id != ctx.graph.session_id())
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      409,
                      "Session ID mismatch");
        return HandleResult::Continue;
    }

    // Send full snapshot so the reconnecting client can rebuild state
    auto snap = ctx.fig_model.snapshot();
    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_SNAPSHOT,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_state_snapshot(snap));
    return HandleResult::Continue;
}

HandleResult handle_req_disconnect(DaemonContext& /*ctx*/,
                                   ClientSlot& slot,
                                   const ipc::Message& /*msg*/)
{
    SPECTRA_LOG_DEBUG("daemon", "Python client disconnected gracefully");
    slot.conn->close();
    return HandleResult::EraseAndContinue;
}

HandleResult handle_req_get_snapshot(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto snap = ctx.fig_model.snapshot();
    send_python_response(*slot.conn,
                         ipc::MessageType::RESP_SNAPSHOT,
                         ctx.graph.session_id(),
                         msg.header.request_id,
                         ipc::encode_state_snapshot(snap));
    return HandleResult::Continue;
}

HandleResult handle_req_anim_start(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_anim_start(msg.payload);
    if (!req || !ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }

    // Build a SET_LIVE_FPS diff op and broadcast to agents so
    // the rendering loop switches to continuous mode at the target FPS.
    // Also persist in the figure model so newly-connecting agents get it.
    ctx.fig_model.set_live_fps(req->figure_id, req->fps);

    ipc::StateDiffPayload diff;
    auto                  base_rev = ctx.fig_model.revision();

    ipc::DiffOp op;
    op.type      = ipc::DiffOp::Type::SET_LIVE_FPS;
    op.figure_id = req->figure_id;
    op.f1        = req->fps;
    op.bool_val  = true;   // enable live streaming
    diff.ops.push_back(std::move(op));

    diff.base_revision = base_rev;
    diff.new_revision  = ctx.fig_model.revision();
    broadcast_diff_to_agents(ctx, diff);

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

}   // namespace spectra::daemon
