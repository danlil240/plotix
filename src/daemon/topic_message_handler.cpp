#include "topic_message_handler.hpp"

#include <iostream>

#include "../ipc/codec.hpp"

namespace spectra::daemon
{

namespace
{

// Send a typed Python-style response to a single client.
bool send_resp_subscribe(ipc::Connection& conn,
                         ipc::SessionId   sid,
                         ipc::RequestId   req_id,
                         uint32_t         series_index)
{
    ipc::RespSubscribeTopicPayload p;
    p.request_id   = req_id;
    p.series_index = series_index;
    return send_python_response(conn,
                                ipc::MessageType::RESP_SUBSCRIBE_TOPIC,
                                sid,
                                req_id,
                                ipc::encode_resp_subscribe_topic(p));
}

bool send_resp_topic_list(ipc::Connection&                        conn,
                          ipc::SessionId                          sid,
                          ipc::RequestId                          req_id,
                          const std::vector<TopicRegistry::Info>& infos)
{
    ipc::RespTopicListPayload p;
    p.request_id = req_id;
    p.topics.reserve(infos.size());
    for (const auto& i : infos)
    {
        ipc::TopicInfoEntry e;
        e.name             = i.name;
        e.kind             = i.kind;
        e.unit             = i.unit;
        e.estimated_hz     = i.estimated_hz;
        e.total_samples    = i.total_samples;
        e.last_publish_ns  = i.last_publish_ns;
        e.subscriber_count = i.subscriber_count;
        e.publisher_online = i.publisher_online;
        p.topics.push_back(std::move(e));
    }
    return send_python_response(conn,
                                ipc::MessageType::RESP_TOPIC_LIST,
                                sid,
                                req_id,
                                ipc::encode_resp_topic_list(p));
}

std::vector<float> to_float_vector(const std::vector<double>& samples)
{
    std::vector<float> out;
    out.reserve(samples.size());
    for (double d : samples)
        out.push_back(static_cast<float>(d));
    return out;
}

}   // namespace

void broadcast_topic_list_changed(DaemonContext& ctx)
{
    ipc::EvtTopicListChangedPayload p;
    auto                            payload = ipc::encode_evt_topic_list_changed(p);
    for (auto& c : ctx.clients)
    {
        if (!c.conn || !c.conn->is_open() || !c.handshake_done)
            continue;
        ipc::Message msg;
        msg.header.type       = ipc::MessageType::EVT_TOPIC_LIST_CHANGED;
        msg.header.session_id = ctx.graph.session_id();
        msg.payload           = payload;
        c.conn->send(msg);
    }
}

HandleResult handle_req_declare_topic(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto req = ipc::decode_req_declare_topic(msg.payload);
    if (!req || req->name.empty())
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_DECLARE_TOPIC payload");
        return HandleResult::Continue;
    }

    auto r =
        ctx.topics.declare(req->name, req->kind, req->unit, req->ring_capacity, slot.client_id);
    if (r == TopicRegistry::DeclareResult::Conflict)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      409,
                      "Topic already owned by another live publisher");
        return HandleResult::Continue;
    }

    std::cerr << "[spectra-backend] Topic declared: " << req->name
              << " kind=" << static_cast<int>(req->kind) << " unit=" << req->unit << "\n";

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    broadcast_topic_list_changed(ctx);
    return HandleResult::Continue;
}

HandleResult handle_req_publish_topic_samples(DaemonContext&      ctx,
                                              ClientSlot&         slot,
                                              const ipc::Message& msg)
{
    auto req = ipc::decode_req_publish_topic_samples(msg.payload);
    if (!req || req->name.empty())
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_PUBLISH_TOPIC_SAMPLES payload");
        return HandleResult::Continue;
    }

    ipc::TopicKind                 kind = ipc::TopicKind::Scalar2D;
    std::vector<TopicSubscription> subs;
    if (!ctx.topics.publish(req->name, req->samples, &kind, &subs))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Topic not declared");
        return HandleResult::Continue;
    }

    // Fan out to subscribed (figure, series).
    if (!subs.empty() && !req->samples.empty())
    {
        const size_t       stride = (kind == ipc::TopicKind::Scalar3D) ? 3u : 2u;
        std::vector<float> fdata;
        fdata.reserve(req->samples.size());
        for (double d : req->samples)
            fdata.push_back(static_cast<float>(d));

        ipc::StateDiffPayload diff;
        diff.ops.reserve(subs.size());
        for (const auto& s : subs)
        {
            if (!ctx.fig_model.has_figure(s.figure_id))
                continue;
            auto op = ctx.fig_model.append_series_data(s.figure_id, s.series_index, fdata);
            diff.ops.push_back(std::move(op));
        }
        if (!diff.ops.empty())
        {
            diff.new_revision  = ctx.fig_model.revision();
            diff.base_revision = diff.new_revision > 0 ? diff.new_revision - 1 : 0;
            broadcast_diff_to_agents(ctx, diff);
        }
        (void)stride;
    }

    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    return HandleResult::Continue;
}

HandleResult handle_req_subscribe_topic(DaemonContext&      ctx,
                                        ClientSlot&         slot,
                                        const ipc::Message& msg)
{
    auto req = ipc::decode_req_subscribe_topic(msg.payload);
    if (!req || req->name.empty())
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_SUBSCRIBE_TOPIC payload");
        return HandleResult::Continue;
    }
    // When figure_id == 0 and the request comes from a window agent, auto-create
    // a new figure with one default axes and assign it to the requesting window.
    if (req->figure_id == 0 && slot.window_id != ipc::INVALID_WINDOW)
    {
        auto kind  = ctx.topics.kind_of(req->name).value_or(ipc::TopicKind::Scalar2D);
        bool is_3d = (kind == ipc::TopicKind::Scalar3D);

        auto fid = ctx.fig_model.create_figure(req->name, 1280, 720);
        ctx.graph.register_figure(fid, req->name);
        ctx.fig_model.add_axes(fid, 0.0f, 1.0f, 0.0f, 1.0f, is_3d);
        ctx.graph.assign_figure(fid, slot.window_id);

        auto assigned = ctx.graph.figures_for_window(slot.window_id);
        send_assign_figures(*slot.conn, slot.window_id, ctx.graph.session_id(), assigned, fid);
        auto snap = ctx.fig_model.snapshot(assigned);
        send_state_snapshot(*slot.conn, slot.window_id, ctx.graph.session_id(), snap);

        req->figure_id  = fid;
        req->axes_index = 0;

        std::cerr << "[spectra-backend] Auto-created figure " << fid
                  << " for topic drag-drop: " << req->name << "\n";
    }

    if (!ctx.fig_model.has_figure(req->figure_id))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Figure not found");
        return HandleResult::Continue;
    }
    if (!ctx.topics.exists(req->name))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Topic not declared");
        return HandleResult::Continue;
    }

    ipc::Revision         base_revision = ctx.fig_model.revision();
    ipc::StateDiffPayload subscription_diff;
    uint32_t              series_index = req->series_index;
    // Auto-create a series on the target axes when sentinel is used.
    if (series_index == 0xFFFFFFFFu)
    {
        auto        kind = ctx.topics.kind_of(req->name).value_or(ipc::TopicKind::Scalar2D);
        std::string type = (kind == ipc::TopicKind::Scalar3D) ? "line3d" : "line";

        uint32_t              new_index = 0;
        ipc::StateDiffPayload diff;
        auto                  op = ctx.fig_model.add_series_with_diff(req->figure_id,
                                                     req->name,
                                                     type,
                                                     req->axes_index,
                                                     new_index);
        diff.ops.push_back(std::move(op));
        diff.new_revision  = ctx.fig_model.revision();
        diff.base_revision = diff.new_revision > 0 ? diff.new_revision - 1 : 0;
        broadcast_diff_to_agents(ctx, diff);
        series_index = new_index;
    }

    TopicSubscription sub;
    sub.figure_id    = req->figure_id;
    sub.axes_index   = req->axes_index;
    sub.series_index = series_index;
    std::vector<double> retained_samples;
    if (!ctx.topics.subscribe(req->name, sub, nullptr, &retained_samples))
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      404,
                      "Topic not declared");
        return HandleResult::Continue;
    }

    if (!retained_samples.empty())
    {
        auto op = ctx.fig_model.set_series_data(req->figure_id,
                                                series_index,
                                                to_float_vector(retained_samples));
        subscription_diff.ops.push_back(std::move(op));
    }

    if (!subscription_diff.ops.empty())
    {
        subscription_diff.base_revision = base_revision;
        subscription_diff.new_revision  = ctx.fig_model.revision();
        broadcast_diff_to_agents(ctx, subscription_diff);
    }

    std::cerr << "[spectra-backend] Topic subscribed: " << req->name
              << " -> figure=" << req->figure_id << " axes=" << req->axes_index
              << " series=" << series_index << "\n";

    send_resp_subscribe(*slot.conn, ctx.graph.session_id(), msg.header.request_id, series_index);
    broadcast_topic_list_changed(ctx);
    return HandleResult::Continue;
}

HandleResult handle_req_unsubscribe_topic(DaemonContext&      ctx,
                                          ClientSlot&         slot,
                                          const ipc::Message& msg)
{
    auto req = ipc::decode_req_unsubscribe_topic(msg.payload);
    if (!req)
    {
        send_resp_err(*slot.conn,
                      ctx.graph.session_id(),
                      msg.header.request_id,
                      400,
                      "Bad REQ_UNSUBSCRIBE_TOPIC payload");
        return HandleResult::Continue;
    }
    TopicSubscription sub;
    sub.figure_id    = req->figure_id;
    sub.axes_index   = req->axes_index;
    sub.series_index = req->series_index;
    ctx.topics.unsubscribe(sub);
    send_resp_ok(*slot.conn, ctx.graph.session_id(), msg.header.request_id);
    broadcast_topic_list_changed(ctx);
    return HandleResult::Continue;
}

HandleResult handle_req_list_topics(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg)
{
    auto infos = ctx.topics.snapshot();
    send_resp_topic_list(*slot.conn, ctx.graph.session_id(), msg.header.request_id, infos);
    return HandleResult::Continue;
}

}   // namespace spectra::daemon
