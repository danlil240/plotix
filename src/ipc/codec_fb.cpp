#include "codec_fb.hpp"

#include <flatbuffers/flatbuffers.h>

#include "spectra_ipc_generated.h"

namespace spectra::ipc
{

// ─── Helper: wrap FlatBuffers output with format prefix ──────────────────────

static std::vector<uint8_t> finalize(flatbuffers::FlatBufferBuilder& fbb)
{
    std::vector<uint8_t> out;
    out.reserve(1 + fbb.GetSize());
    out.push_back(static_cast<uint8_t>(PayloadFormat::FLATBUFFERS));
    out.insert(out.end(), fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    return out;
}

// ─── Handshake ───────────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_hello(const HelloPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto                           offset = fb::CreateHelloPayloadDirect(fbb,
                                               p.protocol_major,
                                               p.protocol_minor,
                                               p.agent_build.c_str(),
                                               p.capabilities,
                                               p.client_type.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<HelloPayload> decode_fb_hello(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::HelloPayload>())
        return std::nullopt;

    auto         fb = flatbuffers::GetRoot<fb::HelloPayload>(data.data());
    HelloPayload p;
    p.protocol_major = fb->protocol_major();
    p.protocol_minor = fb->protocol_minor();
    if (fb->agent_build())
        p.agent_build = fb->agent_build()->str();
    p.capabilities = fb->capabilities();
    if (fb->client_type())
        p.client_type = fb->client_type()->str();
    return p;
}

std::vector<uint8_t> encode_fb_welcome(const WelcomePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto                           offset = fb::CreateWelcomePayloadDirect(fbb,
                                                 p.session_id,
                                                 p.window_id,
                                                 p.process_id,
                                                 p.heartbeat_ms,
                                                 p.mode.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<WelcomePayload> decode_fb_welcome(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::WelcomePayload>())
        return std::nullopt;

    auto           fb = flatbuffers::GetRoot<fb::WelcomePayload>(data.data());
    WelcomePayload p;
    p.session_id   = fb->session_id();
    p.window_id    = fb->window_id();
    p.process_id   = fb->process_id();
    p.heartbeat_ms = fb->heartbeat_ms();
    if (fb->mode())
        p.mode = fb->mode()->str();
    return p;
}

// ─── Response ────────────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_resp_ok(const RespOkPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto                           offset = fb::CreateRespOkPayload(fbb, p.request_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespOkPayload> decode_fb_resp_ok(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespOkPayload>())
        return std::nullopt;

    auto          fb = flatbuffers::GetRoot<fb::RespOkPayload>(data.data());
    RespOkPayload p;
    p.request_id = fb->request_id();
    return p;
}

std::vector<uint8_t> encode_fb_resp_err(const RespErrPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto offset = fb::CreateRespErrPayloadDirect(fbb, p.request_id, p.code, p.message.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespErrPayload> decode_fb_resp_err(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespErrPayload>())
        return std::nullopt;

    auto           fb = flatbuffers::GetRoot<fb::RespErrPayload>(data.data());
    RespErrPayload p;
    p.request_id = fb->request_id();
    p.code       = fb->code();
    if (fb->message())
        p.message = fb->message()->str();
    return p;
}

// ─── Control ─────────────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_cmd_assign_figures(const CmdAssignFiguresPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto                           ids = fbb.CreateVector(p.figure_ids);
    auto offset = fb::CreateCmdAssignFiguresPayload(fbb, p.window_id, ids, p.active_figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<CmdAssignFiguresPayload> decode_fb_cmd_assign_figures(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::CmdAssignFiguresPayload>())
        return std::nullopt;

    auto                    fb = flatbuffers::GetRoot<fb::CmdAssignFiguresPayload>(data.data());
    CmdAssignFiguresPayload p;
    p.window_id        = fb->window_id();
    p.active_figure_id = fb->active_figure_id();
    if (fb->figure_ids())
    {
        p.figure_ids.reserve(fb->figure_ids()->size());
        for (auto id : *fb->figure_ids())
            p.figure_ids.push_back(id);
    }
    return p;
}

std::vector<uint8_t> encode_fb_req_create_window(const ReqCreateWindowPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateReqCreateWindowPayload(fbb, p.template_window_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqCreateWindowPayload> decode_fb_req_create_window(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqCreateWindowPayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::ReqCreateWindowPayload>(data.data());
    ReqCreateWindowPayload p;
    p.template_window_id = fb->template_window_id();
    return p;
}

std::vector<uint8_t> encode_fb_req_close_window(const ReqCloseWindowPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto offset = fb::CreateReqCloseWindowPayloadDirect(fbb, p.window_id, p.reason.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqCloseWindowPayload> decode_fb_req_close_window(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqCloseWindowPayload>())
        return std::nullopt;

    auto                  fb = flatbuffers::GetRoot<fb::ReqCloseWindowPayload>(data.data());
    ReqCloseWindowPayload p;
    p.window_id = fb->window_id();
    if (fb->reason())
        p.reason = fb->reason()->str();
    return p;
}

std::vector<uint8_t> encode_fb_req_detach_figure(const ReqDetachFigurePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto                           offset = fb::CreateReqDetachFigurePayload(fbb,
                                                   p.source_window_id,
                                                   p.figure_id,
                                                   p.width,
                                                   p.height,
                                                   p.screen_x,
                                                   p.screen_y);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqDetachFigurePayload> decode_fb_req_detach_figure(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqDetachFigurePayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::ReqDetachFigurePayload>(data.data());
    ReqDetachFigurePayload p;
    p.source_window_id = fb->source_window_id();
    p.figure_id        = fb->figure_id();
    p.width            = fb->width();
    p.height           = fb->height();
    p.screen_x         = fb->screen_x();
    p.screen_y         = fb->screen_y();
    return p;
}

std::vector<uint8_t> encode_fb_cmd_remove_figure(const CmdRemoveFigurePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateCmdRemoveFigurePayload(fbb, p.window_id, p.figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<CmdRemoveFigurePayload> decode_fb_cmd_remove_figure(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::CmdRemoveFigurePayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::CmdRemoveFigurePayload>(data.data());
    CmdRemoveFigurePayload p;
    p.window_id = fb->window_id();
    p.figure_id = fb->figure_id();
    return p;
}

std::vector<uint8_t> encode_fb_cmd_set_active(const CmdSetActivePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateCmdSetActivePayload(fbb, p.window_id, p.figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<CmdSetActivePayload> decode_fb_cmd_set_active(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::CmdSetActivePayload>())
        return std::nullopt;

    auto                fb = flatbuffers::GetRoot<fb::CmdSetActivePayload>(data.data());
    CmdSetActivePayload p;
    p.window_id = fb->window_id();
    p.figure_id = fb->figure_id();
    return p;
}

std::vector<uint8_t> encode_fb_cmd_close_window(const CmdCloseWindowPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto offset = fb::CreateCmdCloseWindowPayloadDirect(fbb, p.window_id, p.reason.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<CmdCloseWindowPayload> decode_fb_cmd_close_window(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::CmdCloseWindowPayload>())
        return std::nullopt;

    auto                  fb = flatbuffers::GetRoot<fb::CmdCloseWindowPayload>(data.data());
    CmdCloseWindowPayload p;
    p.window_id = fb->window_id();
    if (fb->reason())
        p.reason = fb->reason()->str();
    return p;
}

// ─── State sync ──────────────────────────────────────────────────────────────

// Helper to build a FlatBuffers SnapshotAxisState from the C++ struct
static flatbuffers::Offset<fb::SnapshotAxisState> build_fb_axis(flatbuffers::FlatBufferBuilder& fbb,
                                                                const SnapshotAxisState&        ax)
{
    return fb::CreateSnapshotAxisStateDirect(fbb,
                                             ax.x_min,
                                             ax.x_max,
                                             ax.y_min,
                                             ax.y_max,
                                             ax.z_min,
                                             ax.z_max,
                                             ax.grid_visible,
                                             ax.is_3d,
                                             ax.x_label.c_str(),
                                             ax.y_label.c_str(),
                                             ax.title.c_str());
}

static SnapshotAxisState read_fb_axis(const fb::SnapshotAxisState* fb)
{
    SnapshotAxisState ax;
    ax.x_min        = fb->x_min();
    ax.x_max        = fb->x_max();
    ax.y_min        = fb->y_min();
    ax.y_max        = fb->y_max();
    ax.z_min        = fb->z_min();
    ax.z_max        = fb->z_max();
    ax.grid_visible = fb->grid_visible();
    ax.is_3d        = fb->is_3d();
    if (fb->x_label())
        ax.x_label = fb->x_label()->str();
    if (fb->y_label())
        ax.y_label = fb->y_label()->str();
    if (fb->title())
        ax.title = fb->title()->str();
    return ax;
}

static flatbuffers::Offset<fb::SnapshotSeriesState> build_fb_series(
    flatbuffers::FlatBufferBuilder& fbb,
    const SnapshotSeriesState&      s)
{
    auto                                            name_off = fbb.CreateString(s.name);
    auto                                            type_off = fbb.CreateString(s.type);
    flatbuffers::Offset<flatbuffers::Vector<float>> data_off;
    if (!s.data.empty())
        data_off = fbb.CreateVector(s.data);

    fb::SnapshotSeriesStateBuilder b(fbb);
    b.add_name(name_off);
    b.add_series_type(type_off);
    b.add_color_r(s.color_r);
    b.add_color_g(s.color_g);
    b.add_color_b(s.color_b);
    b.add_color_a(s.color_a);
    b.add_line_width(s.line_width);
    b.add_marker_size(s.marker_size);
    b.add_visible(s.visible);
    b.add_opacity(s.opacity);
    b.add_point_count(s.point_count);
    b.add_axes_index(s.axes_index);
    if (!s.data.empty())
        b.add_data(data_off);
    return b.Finish();
}

static SnapshotSeriesState read_fb_series(const fb::SnapshotSeriesState* fb)
{
    SnapshotSeriesState s;
    if (fb->name())
        s.name = fb->name()->str();
    if (fb->series_type())
        s.type = fb->series_type()->str();
    s.color_r     = fb->color_r();
    s.color_g     = fb->color_g();
    s.color_b     = fb->color_b();
    s.color_a     = fb->color_a();
    s.line_width  = fb->line_width();
    s.marker_size = fb->marker_size();
    s.visible     = fb->visible();
    s.opacity     = fb->opacity();
    s.point_count = fb->point_count();
    s.axes_index  = fb->axes_index();
    if (fb->data())
    {
        s.data.reserve(fb->data()->size());
        for (auto v : *fb->data())
            s.data.push_back(v);
    }
    return s;
}

static flatbuffers::Offset<fb::SnapshotFigureState> build_fb_figure(
    flatbuffers::FlatBufferBuilder& fbb,
    const SnapshotFigureState&      fig)
{
    auto title_off = fbb.CreateString(fig.title);

    std::vector<flatbuffers::Offset<fb::SnapshotAxisState>> axes_offsets;
    axes_offsets.reserve(fig.axes.size());
    for (const auto& ax : fig.axes)
        axes_offsets.push_back(build_fb_axis(fbb, ax));
    auto axes_off = fbb.CreateVector(axes_offsets);

    std::vector<flatbuffers::Offset<fb::SnapshotSeriesState>> series_offsets;
    series_offsets.reserve(fig.series.size());
    for (const auto& s : fig.series)
        series_offsets.push_back(build_fb_series(fbb, s));
    auto series_off = fbb.CreateVector(series_offsets);

    fb::SnapshotFigureStateBuilder b(fbb);
    b.add_figure_id(fig.figure_id);
    b.add_title(title_off);
    b.add_width(fig.width);
    b.add_height(fig.height);
    b.add_grid_rows(fig.grid_rows);
    b.add_grid_cols(fig.grid_cols);
    b.add_window_group(fig.window_group);
    b.add_axes(axes_off);
    b.add_series(series_off);
    b.add_live_fps(fig.live_fps);
    return b.Finish();
}

static SnapshotFigureState read_fb_figure(const fb::SnapshotFigureState* fb)
{
    SnapshotFigureState fig;
    fig.figure_id    = fb->figure_id();
    fig.width        = fb->width();
    fig.height       = fb->height();
    fig.grid_rows    = fb->grid_rows();
    fig.grid_cols    = fb->grid_cols();
    fig.window_group = fb->window_group();
    fig.live_fps     = fb->live_fps();
    if (fb->title())
        fig.title = fb->title()->str();
    if (fb->axes())
    {
        for (const auto* ax : *fb->axes())
            fig.axes.push_back(read_fb_axis(ax));
    }
    if (fb->series())
    {
        for (const auto* s : *fb->series())
            fig.series.push_back(read_fb_series(s));
    }
    return fig;
}

static flatbuffers::Offset<fb::SnapshotKnobState> build_fb_knob(flatbuffers::FlatBufferBuilder& fbb,
                                                                const SnapshotKnobState&        k)
{
    auto                                                  name_off = fbb.CreateString(k.name);
    std::vector<flatbuffers::Offset<flatbuffers::String>> choice_offsets;
    choice_offsets.reserve(k.choices.size());
    for (const auto& c : k.choices)
        choice_offsets.push_back(fbb.CreateString(c));
    auto choices_off = fbb.CreateVector(choice_offsets);

    fb::SnapshotKnobStateBuilder b(fbb);
    b.add_name(name_off);
    b.add_knob_type(k.type);
    b.add_value(k.value);
    b.add_min_val(k.min_val);
    b.add_max_val(k.max_val);
    b.add_step(k.step);
    b.add_choices(choices_off);
    return b.Finish();
}

static SnapshotKnobState read_fb_knob(const fb::SnapshotKnobState* fb)
{
    SnapshotKnobState k;
    if (fb->name())
        k.name = fb->name()->str();
    k.type    = fb->knob_type();
    k.value   = fb->value();
    k.min_val = fb->min_val();
    k.max_val = fb->max_val();
    k.step    = fb->step();
    if (fb->choices())
    {
        for (const auto* c : *fb->choices())
            k.choices.push_back(c->str());
    }
    return k;
}

std::vector<uint8_t> encode_fb_state_snapshot(const StateSnapshotPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(4096);

    std::vector<flatbuffers::Offset<fb::SnapshotFigureState>> fig_offsets;
    fig_offsets.reserve(p.figures.size());
    for (const auto& fig : p.figures)
        fig_offsets.push_back(build_fb_figure(fbb, fig));
    auto figs_off = fbb.CreateVector(fig_offsets);

    std::vector<flatbuffers::Offset<fb::SnapshotKnobState>> knob_offsets;
    knob_offsets.reserve(p.knobs.size());
    for (const auto& k : p.knobs)
        knob_offsets.push_back(build_fb_knob(fbb, k));
    auto knobs_off = fbb.CreateVector(knob_offsets);

    auto offset =
        fb::CreateStateSnapshotPayload(fbb, p.revision, p.session_id, figs_off, knobs_off);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<StateSnapshotPayload> decode_fb_state_snapshot(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::StateSnapshotPayload>())
        return std::nullopt;

    auto                 fb = flatbuffers::GetRoot<fb::StateSnapshotPayload>(data.data());
    StateSnapshotPayload p;
    p.revision   = fb->revision();
    p.session_id = fb->session_id();
    if (fb->figures())
    {
        for (const auto* fig : *fb->figures())
            p.figures.push_back(read_fb_figure(fig));
    }
    if (fb->knobs())
    {
        for (const auto* k : *fb->knobs())
            p.knobs.push_back(read_fb_knob(k));
    }
    return p;
}

// DiffOp helpers

static flatbuffers::Offset<fb::DiffOp> build_fb_diff_op(flatbuffers::FlatBufferBuilder& fbb,
                                                        const DiffOp&                   op)
{
    flatbuffers::Offset<flatbuffers::String>        str_off;
    flatbuffers::Offset<flatbuffers::Vector<float>> data_off;

    if (!op.str_val.empty())
        str_off = fbb.CreateString(op.str_val);
    if (!op.data.empty())
        data_off = fbb.CreateVector(op.data);

    fb::DiffOpBuilder b(fbb);
    b.add_op_type(static_cast<fb::DiffOpType>(op.type));
    b.add_figure_id(op.figure_id);
    b.add_axes_index(op.axes_index);
    b.add_series_index(op.series_index);
    b.add_f1(op.f1);
    b.add_f2(op.f2);
    b.add_f3(op.f3);
    b.add_f4(op.f4);
    b.add_bool_val(op.bool_val);
    if (!op.str_val.empty())
        b.add_str_val(str_off);
    if (!op.data.empty())
        b.add_data(data_off);
    return b.Finish();
}

static DiffOp read_fb_diff_op(const fb::DiffOp* fb)
{
    DiffOp op;
    op.type         = static_cast<DiffOp::Type>(fb->op_type());
    op.figure_id    = fb->figure_id();
    op.axes_index   = fb->axes_index();
    op.series_index = fb->series_index();
    op.f1           = fb->f1();
    op.f2           = fb->f2();
    op.f3           = fb->f3();
    op.f4           = fb->f4();
    op.bool_val     = fb->bool_val();
    if (fb->str_val())
        op.str_val = fb->str_val()->str();
    if (fb->data())
    {
        op.data.reserve(fb->data()->size());
        for (auto v : *fb->data())
            op.data.push_back(v);
    }
    return op;
}

std::vector<uint8_t> encode_fb_state_diff(const StateDiffPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(4096);

    std::vector<flatbuffers::Offset<fb::DiffOp>> op_offsets;
    op_offsets.reserve(p.ops.size());
    for (const auto& op : p.ops)
        op_offsets.push_back(build_fb_diff_op(fbb, op));
    auto ops_off = fbb.CreateVector(op_offsets);

    auto offset = fb::CreateStateDiffPayload(fbb, p.base_revision, p.new_revision, ops_off);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<StateDiffPayload> decode_fb_state_diff(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::StateDiffPayload>())
        return std::nullopt;

    auto             fb = flatbuffers::GetRoot<fb::StateDiffPayload>(data.data());
    StateDiffPayload p;
    p.base_revision = fb->base_revision();
    p.new_revision  = fb->new_revision();
    if (fb->ops())
    {
        for (const auto* op : *fb->ops())
            p.ops.push_back(read_fb_diff_op(op));
    }
    return p;
}

std::vector<uint8_t> encode_fb_ack_state(const AckStatePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto                           offset = fb::CreateAckStatePayload(fbb, p.revision);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<AckStatePayload> decode_fb_ack_state(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::AckStatePayload>())
        return std::nullopt;

    auto            fb = flatbuffers::GetRoot<fb::AckStatePayload>(data.data());
    AckStatePayload p;
    p.revision = fb->revision();
    return p;
}

// ─── Input events ────────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_evt_input(const EvtInputPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto                           offset = fb::CreateEvtInputPayload(fbb,
                                            p.window_id,
                                            static_cast<fb::InputType>(p.input_type),
                                            p.key,
                                            p.mods,
                                            p.x,
                                            p.y,
                                            p.figure_id,
                                            p.axes_index);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<EvtInputPayload> decode_fb_evt_input(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::EvtInputPayload>())
        return std::nullopt;

    auto            fb = flatbuffers::GetRoot<fb::EvtInputPayload>(data.data());
    EvtInputPayload p;
    p.window_id  = fb->window_id();
    p.input_type = static_cast<EvtInputPayload::InputType>(fb->input_type());
    p.key        = fb->key();
    p.mods       = fb->mods();
    p.x          = fb->x();
    p.y          = fb->y();
    p.figure_id  = fb->figure_id();
    p.axes_index = fb->axes_index();
    return p;
}

// ─── Python lifecycle requests ───────────────────────────────────────────────

std::vector<uint8_t> encode_fb_req_create_figure(const ReqCreateFigurePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto offset = fb::CreateReqCreateFigurePayloadDirect(fbb, p.title.c_str(), p.width, p.height);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqCreateFigurePayload> decode_fb_req_create_figure(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqCreateFigurePayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::ReqCreateFigurePayload>(data.data());
    ReqCreateFigurePayload p;
    if (fb->title())
        p.title = fb->title()->str();
    p.width  = fb->width();
    p.height = fb->height();
    return p;
}

std::vector<uint8_t> encode_fb_req_destroy_figure(const ReqDestroyFigurePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto                           offset = fb::CreateReqDestroyFigurePayload(fbb, p.figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqDestroyFigurePayload> decode_fb_req_destroy_figure(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqDestroyFigurePayload>())
        return std::nullopt;

    auto                    fb = flatbuffers::GetRoot<fb::ReqDestroyFigurePayload>(data.data());
    ReqDestroyFigurePayload p;
    p.figure_id = fb->figure_id();
    return p;
}

std::vector<uint8_t> encode_fb_req_create_axes(const ReqCreateAxesPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto                           offset = fb::CreateReqCreateAxesPayload(fbb,
                                                 p.figure_id,
                                                 p.grid_rows,
                                                 p.grid_cols,
                                                 p.grid_index,
                                                 p.is_3d);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqCreateAxesPayload> decode_fb_req_create_axes(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqCreateAxesPayload>())
        return std::nullopt;

    auto                 fb = flatbuffers::GetRoot<fb::ReqCreateAxesPayload>(data.data());
    ReqCreateAxesPayload p;
    p.figure_id  = fb->figure_id();
    p.grid_rows  = fb->grid_rows();
    p.grid_cols  = fb->grid_cols();
    p.grid_index = fb->grid_index();
    p.is_3d      = fb->is_3d();
    return p;
}

std::vector<uint8_t> encode_fb_req_add_series(const ReqAddSeriesPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto                           offset = fb::CreateReqAddSeriesPayloadDirect(fbb,
                                                      p.figure_id,
                                                      p.axes_index,
                                                      p.series_type.c_str(),
                                                      p.label.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqAddSeriesPayload> decode_fb_req_add_series(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqAddSeriesPayload>())
        return std::nullopt;

    auto                fb = flatbuffers::GetRoot<fb::ReqAddSeriesPayload>(data.data());
    ReqAddSeriesPayload p;
    p.figure_id  = fb->figure_id();
    p.axes_index = fb->axes_index();
    if (fb->series_type())
        p.series_type = fb->series_type()->str();
    if (fb->label())
        p.label = fb->label()->str();
    return p;
}

std::vector<uint8_t> encode_fb_req_remove_series(const ReqRemoveSeriesPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateReqRemoveSeriesPayload(fbb, p.figure_id, p.series_index);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqRemoveSeriesPayload> decode_fb_req_remove_series(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqRemoveSeriesPayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::ReqRemoveSeriesPayload>(data.data());
    ReqRemoveSeriesPayload p;
    p.figure_id    = fb->figure_id();
    p.series_index = fb->series_index();
    return p;
}

std::vector<uint8_t> encode_fb_req_set_data(const ReqSetDataPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(1024 + static_cast<size_t>(p.data.size()) * 4);
    flatbuffers::Offset<flatbuffers::Vector<float>> data_off;
    if (!p.data.empty())
        data_off = fbb.CreateVector(p.data);

    fb::ReqSetDataPayloadBuilder b(fbb);
    b.add_figure_id(p.figure_id);
    b.add_series_index(p.series_index);
    b.add_dtype(p.dtype);
    if (!p.data.empty())
        b.add_data(data_off);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqSetDataPayload> decode_fb_req_set_data(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqSetDataPayload>())
        return std::nullopt;

    auto              fb = flatbuffers::GetRoot<fb::ReqSetDataPayload>(data.data());
    ReqSetDataPayload p;
    p.figure_id    = fb->figure_id();
    p.series_index = fb->series_index();
    p.dtype        = fb->dtype();
    if (fb->data())
    {
        p.data.reserve(fb->data()->size());
        for (auto v2 : *fb->data())
            p.data.push_back(v2);
    }
    return p;
}

std::vector<uint8_t> encode_fb_req_update_property(const ReqUpdatePropertyPayload& p)
{
    flatbuffers::FlatBufferBuilder           fbb(512);
    auto                                     prop_off = fbb.CreateString(p.property);
    flatbuffers::Offset<flatbuffers::String> str_off;
    if (!p.str_val.empty())
        str_off = fbb.CreateString(p.str_val);

    fb::ReqUpdatePropertyPayloadBuilder b(fbb);
    b.add_figure_id(p.figure_id);
    b.add_axes_index(p.axes_index);
    b.add_series_index(p.series_index);
    b.add_property(prop_off);
    b.add_f1(p.f1);
    b.add_f2(p.f2);
    b.add_f3(p.f3);
    b.add_f4(p.f4);
    b.add_bool_val(p.bool_val);
    if (!p.str_val.empty())
        b.add_str_val(str_off);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqUpdatePropertyPayload> decode_fb_req_update_property(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqUpdatePropertyPayload>())
        return std::nullopt;

    auto                     fb = flatbuffers::GetRoot<fb::ReqUpdatePropertyPayload>(data.data());
    ReqUpdatePropertyPayload p;
    p.figure_id    = fb->figure_id();
    p.axes_index   = fb->axes_index();
    p.series_index = fb->series_index();
    if (fb->property())
        p.property = fb->property()->str();
    p.f1       = fb->f1();
    p.f2       = fb->f2();
    p.f3       = fb->f3();
    p.f4       = fb->f4();
    p.bool_val = fb->bool_val();
    if (fb->str_val())
        p.str_val = fb->str_val()->str();
    return p;
}

std::vector<uint8_t> encode_fb_req_show(const ReqShowPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto                           offset = fb::CreateReqShowPayload(fbb, p.figure_id, p.window_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqShowPayload> decode_fb_req_show(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqShowPayload>())
        return std::nullopt;

    auto           fb = flatbuffers::GetRoot<fb::ReqShowPayload>(data.data());
    ReqShowPayload p;
    p.figure_id = fb->figure_id();
    p.window_id = fb->window_id();
    return p;
}

std::vector<uint8_t> encode_fb_req_close_figure(const ReqCloseFigurePayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto                           offset = fb::CreateReqCloseFigurePayload(fbb, p.figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqCloseFigurePayload> decode_fb_req_close_figure(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqCloseFigurePayload>())
        return std::nullopt;

    auto                  fb = flatbuffers::GetRoot<fb::ReqCloseFigurePayload>(data.data());
    ReqCloseFigurePayload p;
    p.figure_id = fb->figure_id();
    return p;
}

std::vector<uint8_t> encode_fb_req_append_data(const ReqAppendDataPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256 + static_cast<size_t>(p.data.size()) * 4);
    flatbuffers::Offset<flatbuffers::Vector<float>> data_off;
    if (!p.data.empty())
        data_off = fbb.CreateVector(p.data);

    fb::ReqAppendDataPayloadBuilder b(fbb);
    b.add_figure_id(p.figure_id);
    b.add_series_index(p.series_index);
    if (!p.data.empty())
        b.add_data(data_off);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqAppendDataPayload> decode_fb_req_append_data(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqAppendDataPayload>())
        return std::nullopt;

    auto                 fb = flatbuffers::GetRoot<fb::ReqAppendDataPayload>(data.data());
    ReqAppendDataPayload p;
    p.figure_id    = fb->figure_id();
    p.series_index = fb->series_index();
    if (fb->data())
    {
        p.data.reserve(fb->data()->size());
        for (auto v2 : *fb->data())
            p.data.push_back(v2);
    }
    return p;
}

std::vector<uint8_t> encode_fb_req_update_batch(const ReqUpdateBatchPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(1024);

    std::vector<flatbuffers::Offset<fb::ReqUpdatePropertyPayload>> upd_offsets;
    for (const auto& upd : p.updates)
    {
        auto                                     prop_off = fbb.CreateString(upd.property);
        flatbuffers::Offset<flatbuffers::String> str_off;
        if (!upd.str_val.empty())
            str_off = fbb.CreateString(upd.str_val);

        fb::ReqUpdatePropertyPayloadBuilder b(fbb);
        b.add_figure_id(upd.figure_id);
        b.add_axes_index(upd.axes_index);
        b.add_series_index(upd.series_index);
        b.add_property(prop_off);
        b.add_f1(upd.f1);
        b.add_f2(upd.f2);
        b.add_f3(upd.f3);
        b.add_f4(upd.f4);
        b.add_bool_val(upd.bool_val);
        if (!upd.str_val.empty())
            b.add_str_val(str_off);
        upd_offsets.push_back(b.Finish());
    }
    auto upds_off = fbb.CreateVector(upd_offsets);

    auto offset = fb::CreateReqUpdateBatchPayload(fbb, upds_off);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqUpdateBatchPayload> decode_fb_req_update_batch(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqUpdateBatchPayload>())
        return std::nullopt;

    auto                  fb = flatbuffers::GetRoot<fb::ReqUpdateBatchPayload>(data.data());
    ReqUpdateBatchPayload p;
    if (fb->updates())
    {
        for (const auto* upd : *fb->updates())
        {
            ReqUpdatePropertyPayload u;
            u.figure_id    = upd->figure_id();
            u.axes_index   = upd->axes_index();
            u.series_index = upd->series_index();
            if (upd->property())
                u.property = upd->property()->str();
            u.f1       = upd->f1();
            u.f2       = upd->f2();
            u.f3       = upd->f3();
            u.f4       = upd->f4();
            u.bool_val = upd->bool_val();
            if (upd->str_val())
                u.str_val = upd->str_val()->str();
            p.updates.push_back(std::move(u));
        }
    }
    return p;
}

std::vector<uint8_t> encode_fb_req_reconnect(const ReqReconnectPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto offset = fb::CreateReqReconnectPayloadDirect(fbb, p.session_id, p.session_token.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<ReqReconnectPayload> decode_fb_req_reconnect(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqReconnectPayload>())
        return std::nullopt;

    auto                fb = flatbuffers::GetRoot<fb::ReqReconnectPayload>(data.data());
    ReqReconnectPayload p;
    p.session_id = fb->session_id();
    if (fb->session_token())
        p.session_token = fb->session_token()->str();
    return p;
}

// ─── Python responses ────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_resp_figure_created(const RespFigureCreatedPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateRespFigureCreatedPayload(fbb, p.request_id, p.figure_id);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespFigureCreatedPayload> decode_fb_resp_figure_created(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespFigureCreatedPayload>())
        return std::nullopt;

    auto                     fb = flatbuffers::GetRoot<fb::RespFigureCreatedPayload>(data.data());
    RespFigureCreatedPayload p;
    p.request_id = fb->request_id();
    p.figure_id  = fb->figure_id();
    return p;
}

std::vector<uint8_t> encode_fb_resp_axes_created(const RespAxesCreatedPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateRespAxesCreatedPayload(fbb, p.request_id, p.axes_index);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespAxesCreatedPayload> decode_fb_resp_axes_created(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespAxesCreatedPayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::RespAxesCreatedPayload>(data.data());
    RespAxesCreatedPayload p;
    p.request_id = fb->request_id();
    p.axes_index = fb->axes_index();
    return p;
}

std::vector<uint8_t> encode_fb_resp_series_added(const RespSeriesAddedPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(64);
    auto offset = fb::CreateRespSeriesAddedPayload(fbb, p.request_id, p.series_index);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespSeriesAddedPayload> decode_fb_resp_series_added(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespSeriesAddedPayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::RespSeriesAddedPayload>(data.data());
    RespSeriesAddedPayload p;
    p.request_id   = fb->request_id();
    p.series_index = fb->series_index();
    return p;
}

std::vector<uint8_t> encode_fb_resp_figure_list(const RespFigureListPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(256);
    auto                           ids    = fbb.CreateVector(p.figure_ids);
    auto                           offset = fb::CreateRespFigureListPayload(fbb, p.request_id, ids);
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<RespFigureListPayload> decode_fb_resp_figure_list(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespFigureListPayload>())
        return std::nullopt;

    auto                  fb = flatbuffers::GetRoot<fb::RespFigureListPayload>(data.data());
    RespFigureListPayload p;
    p.request_id = fb->request_id();
    if (fb->figure_ids())
    {
        p.figure_ids.reserve(fb->figure_ids()->size());
        for (auto id : *fb->figure_ids())
            p.figure_ids.push_back(id);
    }
    return p;
}

// ─── Python events ───────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_evt_window_closed(const EvtWindowClosedPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto                           offset =
        fb::CreateEvtWindowClosedPayloadDirect(fbb, p.figure_id, p.window_id, p.reason.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<EvtWindowClosedPayload> decode_fb_evt_window_closed(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::EvtWindowClosedPayload>())
        return std::nullopt;

    auto                   fb = flatbuffers::GetRoot<fb::EvtWindowClosedPayload>(data.data());
    EvtWindowClosedPayload p;
    p.figure_id = fb->figure_id();
    p.window_id = fb->window_id();
    if (fb->reason())
        p.reason = fb->reason()->str();
    return p;
}

std::vector<uint8_t> encode_fb_evt_figure_destroyed(const EvtFigureDestroyedPayload& p)
{
    flatbuffers::FlatBufferBuilder fbb(128);
    auto offset = fb::CreateEvtFigureDestroyedPayloadDirect(fbb, p.figure_id, p.reason.c_str());
    fbb.Finish(offset);
    return finalize(fbb);
}

std::optional<EvtFigureDestroyedPayload> decode_fb_evt_figure_destroyed(
    std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::EvtFigureDestroyedPayload>())
        return std::nullopt;

    auto                      fb = flatbuffers::GetRoot<fb::EvtFigureDestroyedPayload>(data.data());
    EvtFigureDestroyedPayload p;
    p.figure_id = fb->figure_id();
    if (fb->reason())
        p.reason = fb->reason()->str();
    return p;
}

// ─── Topics (pub/sub) ────────────────────────────────────────────────────────

std::vector<uint8_t> encode_fb_req_declare_topic(const ReqDeclareTopicPayload& p)
{
    flatbuffers::FlatBufferBuilder    fbb(256);
    auto                              name_off = fbb.CreateString(p.name);
    auto                              unit_off = fbb.CreateString(p.unit);
    fb::ReqDeclareTopicPayloadBuilder b(fbb);
    b.add_name(name_off);
    b.add_kind(static_cast<uint8_t>(p.kind));
    b.add_unit(unit_off);
    b.add_ring_capacity(p.ring_capacity);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqDeclareTopicPayload> decode_fb_req_declare_topic(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqDeclareTopicPayload>())
        return std::nullopt;
    auto                   fb = flatbuffers::GetRoot<fb::ReqDeclareTopicPayload>(data.data());
    ReqDeclareTopicPayload p;
    if (fb->name())
        p.name = fb->name()->str();
    p.kind = static_cast<TopicKind>(fb->kind());
    if (fb->unit())
        p.unit = fb->unit()->str();
    p.ring_capacity = fb->ring_capacity();
    return p;
}

std::vector<uint8_t> encode_fb_req_publish_topic_samples(const ReqPublishTopicSamplesPayload& p)
{
    flatbuffers::FlatBufferBuilder                   fbb(256 + p.samples.size() * 8);
    auto                                             name_off = fbb.CreateString(p.name);
    flatbuffers::Offset<flatbuffers::Vector<double>> samples_off;
    if (!p.samples.empty())
        samples_off = fbb.CreateVector(p.samples);
    fb::ReqPublishTopicSamplesPayloadBuilder b(fbb);
    b.add_name(name_off);
    if (!p.samples.empty())
        b.add_samples(samples_off);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqPublishTopicSamplesPayload> decode_fb_req_publish_topic_samples(
    std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqPublishTopicSamplesPayload>())
        return std::nullopt;
    auto fb = flatbuffers::GetRoot<fb::ReqPublishTopicSamplesPayload>(data.data());
    ReqPublishTopicSamplesPayload p;
    if (fb->name())
        p.name = fb->name()->str();
    if (fb->samples())
    {
        p.samples.reserve(fb->samples()->size());
        for (auto v2 : *fb->samples())
            p.samples.push_back(v2);
    }
    return p;
}

std::vector<uint8_t> encode_fb_req_subscribe_topic(const ReqSubscribeTopicPayload& p)
{
    flatbuffers::FlatBufferBuilder      fbb(256);
    auto                                name_off = fbb.CreateString(p.name);
    fb::ReqSubscribeTopicPayloadBuilder b(fbb);
    b.add_name(name_off);
    b.add_figure_id(p.figure_id);
    b.add_axes_index(p.axes_index);
    b.add_series_index(p.series_index);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqSubscribeTopicPayload> decode_fb_req_subscribe_topic(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqSubscribeTopicPayload>())
        return std::nullopt;
    auto                     fb = flatbuffers::GetRoot<fb::ReqSubscribeTopicPayload>(data.data());
    ReqSubscribeTopicPayload p;
    if (fb->name())
        p.name = fb->name()->str();
    p.figure_id    = fb->figure_id();
    p.axes_index   = fb->axes_index();
    p.series_index = fb->series_index();
    return p;
}

std::vector<uint8_t> encode_fb_req_unsubscribe_topic(const ReqUnsubscribeTopicPayload& p)
{
    flatbuffers::FlatBufferBuilder        fbb(64);
    fb::ReqUnsubscribeTopicPayloadBuilder b(fbb);
    b.add_figure_id(p.figure_id);
    b.add_axes_index(p.axes_index);
    b.add_series_index(p.series_index);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqUnsubscribeTopicPayload> decode_fb_req_unsubscribe_topic(
    std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqUnsubscribeTopicPayload>())
        return std::nullopt;
    auto fb = flatbuffers::GetRoot<fb::ReqUnsubscribeTopicPayload>(data.data());
    ReqUnsubscribeTopicPayload p;
    p.figure_id    = fb->figure_id();
    p.axes_index   = fb->axes_index();
    p.series_index = fb->series_index();
    return p;
}

std::vector<uint8_t> encode_fb_req_list_topics(const ReqListTopicsPayload&)
{
    flatbuffers::FlatBufferBuilder  fbb(32);
    fb::ReqListTopicsPayloadBuilder b(fbb);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<ReqListTopicsPayload> decode_fb_req_list_topics(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::ReqListTopicsPayload>())
        return std::nullopt;
    return ReqListTopicsPayload{};
}

std::vector<uint8_t> encode_fb_resp_topic_list(const RespTopicListPayload& p)
{
    flatbuffers::FlatBufferBuilder                       fbb(512);
    std::vector<flatbuffers::Offset<fb::TopicInfoEntry>> entries;
    entries.reserve(p.topics.size());
    for (const auto& t : p.topics)
    {
        auto                      name_off = fbb.CreateString(t.name);
        auto                      unit_off = fbb.CreateString(t.unit);
        fb::TopicInfoEntryBuilder eb(fbb);
        eb.add_name(name_off);
        eb.add_kind(static_cast<uint8_t>(t.kind));
        eb.add_unit(unit_off);
        eb.add_estimated_hz(t.estimated_hz);
        eb.add_total_samples(t.total_samples);
        eb.add_last_publish_ns(t.last_publish_ns);
        eb.add_subscriber_count(t.subscriber_count);
        eb.add_publisher_online(t.publisher_online);
        entries.push_back(eb.Finish());
    }
    auto                            topics_off = fbb.CreateVector(entries);
    fb::RespTopicListPayloadBuilder b(fbb);
    b.add_request_id(p.request_id);
    b.add_topics(topics_off);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<RespTopicListPayload> decode_fb_resp_topic_list(std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespTopicListPayload>())
        return std::nullopt;
    auto                 fb = flatbuffers::GetRoot<fb::RespTopicListPayload>(data.data());
    RespTopicListPayload p;
    p.request_id = fb->request_id();
    if (fb->topics())
    {
        p.topics.reserve(fb->topics()->size());
        for (auto e : *fb->topics())
        {
            TopicInfoEntry t;
            if (e->name())
                t.name = e->name()->str();
            t.kind = static_cast<TopicKind>(e->kind());
            if (e->unit())
                t.unit = e->unit()->str();
            t.estimated_hz     = e->estimated_hz();
            t.total_samples    = e->total_samples();
            t.last_publish_ns  = e->last_publish_ns();
            t.subscriber_count = e->subscriber_count();
            t.publisher_online = e->publisher_online();
            p.topics.push_back(std::move(t));
        }
    }
    return p;
}

std::vector<uint8_t> encode_fb_resp_subscribe_topic(const RespSubscribeTopicPayload& p)
{
    flatbuffers::FlatBufferBuilder       fbb(64);
    fb::RespSubscribeTopicPayloadBuilder b(fbb);
    b.add_request_id(p.request_id);
    b.add_series_index(p.series_index);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<RespSubscribeTopicPayload> decode_fb_resp_subscribe_topic(
    std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::RespSubscribeTopicPayload>())
        return std::nullopt;
    auto                      fb = flatbuffers::GetRoot<fb::RespSubscribeTopicPayload>(data.data());
    RespSubscribeTopicPayload p;
    p.request_id   = fb->request_id();
    p.series_index = fb->series_index();
    return p;
}

std::vector<uint8_t> encode_fb_evt_topic_list_changed(const EvtTopicListChangedPayload&)
{
    flatbuffers::FlatBufferBuilder        fbb(32);
    fb::EvtTopicListChangedPayloadBuilder b(fbb);
    fbb.Finish(b.Finish());
    return finalize(fbb);
}

std::optional<EvtTopicListChangedPayload> decode_fb_evt_topic_list_changed(
    std::span<const uint8_t> data)
{
    flatbuffers::Verifier v(data.data(), data.size());
    if (!v.VerifyBuffer<fb::EvtTopicListChangedPayload>())
        return std::nullopt;
    return EvtTopicListChangedPayload{};
}

}   // namespace spectra::ipc
