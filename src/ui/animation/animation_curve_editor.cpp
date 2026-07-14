#include "ui/animation/animation_curve_editor.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
#endif

namespace spectra
{

// ─── CurveViewTransform ──────────────────────────────────────────────────────

float CurveViewTransform::time_to_x(float t) const
{
    if (time_max <= time_min)
        return origin_x;
    return origin_x + (t - time_min) / (time_max - time_min) * width;
}

float CurveViewTransform::value_to_y(float v) const
{
    if (value_max <= value_min)
        return origin_y + height;
    // Y is inverted: higher values are at the top (lower screen Y)
    return origin_y + height - (v - value_min) / (value_max - value_min) * height;
}

float CurveViewTransform::x_to_time(float x) const
{
    if (width <= 0.0f)
        return time_min;
    return time_min + (x - origin_x) / width * (time_max - time_min);
}

float CurveViewTransform::y_to_value(float y) const
{
    if (height <= 0.0f)
        return value_min;
    return value_min + (origin_y + height - y) / height * (value_max - value_min);
}

void CurveViewTransform::zoom_time(float factor, float center_time)
{
    float left  = center_time - time_min;
    float right = time_max - center_time;
    time_min    = center_time - left / factor;
    time_max    = center_time + right / factor;
}

void CurveViewTransform::zoom_value(float factor, float center_value)
{
    float below = center_value - value_min;
    float above = value_max - center_value;
    value_min   = center_value - below / factor;
    value_max   = center_value + above / factor;
}

void CurveViewTransform::zoom(float factor, float center_x, float center_y)
{
    float ct = x_to_time(center_x);
    float cv = y_to_value(center_y);
    zoom_time(factor, ct);
    zoom_value(factor, cv);
}

void CurveViewTransform::pan(float dx, float dy)
{
    float dt = -(dx / width) * (time_max - time_min);
    float dv = (dy / height) * (value_max - value_min);
    time_min += dt;
    time_max += dt;
    value_min += dv;
    value_max += dv;
}

void CurveViewTransform::fit_to_channel(const AnimationChannel& channel, float padding)
{
    if (channel.empty())
    {
        time_min  = 0.0f;
        time_max  = 10.0f;
        value_min = -0.1f;
        value_max = 1.1f;
        return;
    }

    float t_min = channel.start_time();
    float t_max = channel.end_time();
    float v_min = channel.keyframes().front().value;
    float v_max = v_min;

    for (const auto& kf : channel.keyframes())
    {
        v_min = std::min(v_min, kf.value);
        v_max = std::max(v_max, kf.value);
    }

    float t_range = t_max - t_min;
    float v_range = v_max - v_min;
    if (t_range < 0.01f)
        t_range = 1.0f;
    if (v_range < 0.01f)
        v_range = 1.0f;

    time_min  = t_min - t_range * padding;
    time_max  = t_max + t_range * padding;
    value_min = v_min - v_range * padding;
    value_max = v_max + v_range * padding;
}

// ─── AnimationCurveEditor ────────────────────────────────────────────────────

AnimationCurveEditor::AnimationCurveEditor() = default;

void AnimationCurveEditor::set_interpolator(KeyframeInterpolator* interp)
{
    interpolator_ = interp;
}

// ─── Channel visibility ──────────────────────────────────────────────────────

void AnimationCurveEditor::set_channel_visible(uint32_t channel_id, bool visible)
{
    ensure_display(channel_id).visible = visible;
}

bool AnimationCurveEditor::is_channel_visible(uint32_t channel_id) const
{
    const auto* d = find_display(channel_id);
    return d ? d->visible : true;
}

void AnimationCurveEditor::set_channel_color(uint32_t channel_id, Color color)
{
    ensure_display(channel_id).color = color;
}

Color AnimationCurveEditor::channel_color(uint32_t channel_id) const
{
    const auto* d = find_display(channel_id);
    if (d)
        return d->color;
    return kChannelColors[channel_id % kChannelColorCount];
}

void AnimationCurveEditor::solo_channel(uint32_t channel_id)
{
    for (auto& d : channel_displays_)
    {
        d.visible = (d.channel_id == channel_id);
    }
}

void AnimationCurveEditor::show_all_channels()
{
    for (auto& d : channel_displays_)
    {
        d.visible = true;
    }
}

// ─── View ────────────────────────────────────────────────────────────────────

void AnimationCurveEditor::fit_view()
{
    if (!interpolator_)
        return;

    const auto& channels = interpolator_->channels();
    if (channels.empty())
    {
        reset_view();
        return;
    }

    float t_min = 1e30f;
    float t_max = -1e30f;
    float v_min = 1e30f;
    float v_max = -1e30f;
    bool  found = false;

    for (const auto& [id, ch] : channels)
    {
        if (!is_channel_visible(id))
            continue;
        if (ch.empty())
            continue;

        found = true;
        t_min = std::min(t_min, ch.start_time());
        t_max = std::max(t_max, ch.end_time());
        for (const auto& kf : ch.keyframes())
        {
            v_min = std::min(v_min, kf.value);
            v_max = std::max(v_max, kf.value);
        }
    }

    if (!found)
    {
        reset_view();
        return;
    }

    float t_range = t_max - t_min;
    float v_range = v_max - v_min;
    if (t_range < 0.01f)
        t_range = 1.0f;
    if (v_range < 0.01f)
        v_range = 1.0f;

    view_.time_min  = t_min - t_range * 0.1f;
    view_.time_max  = t_max + t_range * 0.1f;
    view_.value_min = v_min - v_range * 0.1f;
    view_.value_max = v_max + v_range * 0.1f;
}

void AnimationCurveEditor::reset_view()
{
    view_.time_min  = 0.0f;
    view_.time_max  = 10.0f;
    view_.value_min = -0.1f;
    view_.value_max = 1.1f;
}

// ─── Selection ───────────────────────────────────────────────────────────────

void AnimationCurveEditor::select_keyframe(uint32_t channel_id, size_t index)
{
    if (!interpolator_)
        return;
    auto* ch = interpolator_->channel(channel_id);
    if (!ch)
        return;
    auto& kfs = const_cast<std::vector<TypedKeyframe>&>(ch->keyframes());
    if (index < kfs.size())
    {
        kfs[index].selected = true;
    }
}

void AnimationCurveEditor::deselect_all()
{
    if (!interpolator_)
        return;
    for (auto& [id, ch] :
         const_cast<std::vector<std::pair<uint32_t, AnimationChannel>>&>(interpolator_->channels()))
    {
        for (auto& kf : const_cast<std::vector<TypedKeyframe>&>(ch.keyframes()))
        {
            kf.selected = false;
        }
    }
}

void AnimationCurveEditor::select_keyframes_in_rect(float t_min,
                                                    float t_max,
                                                    float v_min,
                                                    float v_max)
{
    if (!interpolator_)
        return;
    for (auto& [id, ch] :
         const_cast<std::vector<std::pair<uint32_t, AnimationChannel>>&>(interpolator_->channels()))
    {
        if (!is_channel_visible(id))
            continue;
        for (auto& kf : const_cast<std::vector<TypedKeyframe>&>(ch.keyframes()))
        {
            if (kf.time >= t_min && kf.time <= t_max && kf.value >= v_min && kf.value <= v_max)
            {
                kf.selected = true;
            }
        }
    }
}

size_t AnimationCurveEditor::selected_count() const
{
    if (!interpolator_)
        return 0;
    size_t count = 0;
    for (const auto& [id, ch] : interpolator_->channels())
    {
        for (const auto& kf : ch.keyframes())
        {
            if (kf.selected)
                ++count;
        }
    }
    return count;
}

void AnimationCurveEditor::delete_selected()
{
    if (!interpolator_)
        return;
    for (auto& [id, ch] :
         const_cast<std::vector<std::pair<uint32_t, AnimationChannel>>&>(interpolator_->channels()))
    {
        auto& kfs = const_cast<std::vector<TypedKeyframe>&>(ch.keyframes());
        std::erase_if(kfs, [](const TypedKeyframe& kf) { return kf.selected; });
        ch.compute_auto_tangents();
    }
}

void AnimationCurveEditor::set_selected_interp(InterpMode mode)
{
    if (!interpolator_)
        return;
    for (auto& [id, ch] :
         const_cast<std::vector<std::pair<uint32_t, AnimationChannel>>&>(interpolator_->channels()))
    {
        for (auto& kf : const_cast<std::vector<TypedKeyframe>&>(ch.keyframes()))
        {
            if (kf.selected)
                kf.interp = mode;
        }
    }
}

void AnimationCurveEditor::set_selected_tangent_mode(TangentMode mode)
{
    if (!interpolator_)
        return;
    for (auto& [id, ch] :
         const_cast<std::vector<std::pair<uint32_t, AnimationChannel>>&>(interpolator_->channels()))
    {
        for (auto& kf : const_cast<std::vector<TypedKeyframe>&>(ch.keyframes()))
        {
            if (kf.selected)
            {
                kf.tangent_mode = mode;
                if (mode == TangentMode::Flat)
                {
                    kf.in_tangent  = TangentHandle{0.0f, 0.0f};
                    kf.out_tangent = TangentHandle{0.0f, 0.0f};
                }
            }
        }
        if (mode == TangentMode::Auto)
        {
            ch.compute_auto_tangents();
        }
    }
}

// ─── Hit testing ─────────────────────────────────────────────────────────────

CurveHitResult AnimationCurveEditor::hit_test(float screen_x, float screen_y, float tolerance) const
{
    CurveHitResult best;
    best.type       = CurveHitType::Background;
    float best_dist = tolerance;

    if (!interpolator_)
        return best;

    for (const auto& [id, ch] : interpolator_->channels())
    {
        if (!is_channel_visible(id))
            continue;

        const auto& kfs = ch.keyframes();
        for (size_t i = 0; i < kfs.size(); ++i)
        {
            const auto& kf = kfs[i];

            // Test keyframe diamond
            float kx = view_.time_to_x(kf.time);
            float ky = view_.value_to_y(kf.value);
            float dist =
                std::sqrt((screen_x - kx) * (screen_x - kx) + (screen_y - ky) * (screen_y - ky));
            if (dist < best_dist)
            {
                best_dist           = dist;
                best.type           = CurveHitType::Keyframe;
                best.channel_id     = id;
                best.keyframe_index = i;
                best.time           = kf.time;
                best.value          = kf.value;
            }

            // Test tangent handles (only if tangents are shown)
            if (show_tangents_)
            {
                // In tangent
                float in_x    = view_.time_to_x(kf.time + kf.in_tangent.dt);
                float in_y    = view_.value_to_y(kf.value + kf.in_tangent.dv);
                float in_dist = std::sqrt((screen_x - in_x) * (screen_x - in_x)
                                          + (screen_y - in_y) * (screen_y - in_y));
                if (in_dist < best_dist && (kf.in_tangent.dt != 0.0f || kf.in_tangent.dv != 0.0f))
                {
                    best_dist           = in_dist;
                    best.type           = CurveHitType::InTangent;
                    best.channel_id     = id;
                    best.keyframe_index = i;
                    best.time           = kf.time;
                    best.value          = kf.value;
                }

                // Out tangent
                float out_x    = view_.time_to_x(kf.time + kf.out_tangent.dt);
                float out_y    = view_.value_to_y(kf.value + kf.out_tangent.dv);
                float out_dist = std::sqrt((screen_x - out_x) * (screen_x - out_x)
                                           + (screen_y - out_y) * (screen_y - out_y));
                if (out_dist < best_dist
                    && (kf.out_tangent.dt != 0.0f || kf.out_tangent.dv != 0.0f))
                {
                    best_dist           = out_dist;
                    best.type           = CurveHitType::OutTangent;
                    best.channel_id     = id;
                    best.keyframe_index = i;
                    best.time           = kf.time;
                    best.value          = kf.value;
                }
            }
        }
    }

    return best;
}

// ─── Drag interaction ────────────────────────────────────────────────────────

void AnimationCurveEditor::begin_drag(float screen_x, float screen_y)
{
    auto hit = hit_test(screen_x, screen_y);
    if (hit.type == CurveHitType::None || hit.type == CurveHitType::Background)
        return;

    drag_.active         = true;
    drag_.dragging       = hit.type;
    drag_.channel_id     = hit.channel_id;
    drag_.keyframe_index = hit.keyframe_index;
    drag_.start_time     = hit.time;
    drag_.start_value    = hit.value;
    drag_.start_mouse_x  = screen_x;
    drag_.start_mouse_y  = screen_y;
}

void AnimationCurveEditor::update_drag(float screen_x, float screen_y)
{
    if (!drag_.active || !interpolator_)
        return;

    auto* ch = interpolator_->channel(drag_.channel_id);
    if (!ch)
    {
        cancel_drag();
        return;
    }

    auto& kfs = const_cast<std::vector<TypedKeyframe>&>(ch->keyframes());
    if (drag_.keyframe_index >= kfs.size())
    {
        cancel_drag();
        return;
    }

    float new_time  = view_.x_to_time(screen_x);
    float new_value = view_.y_to_value(screen_y);

    auto& kf = kfs[drag_.keyframe_index];

    switch (drag_.dragging)
    {
        case CurveHitType::Keyframe:
        {
            float old_value = kf.value;
            kf.time         = new_time;
            kf.value        = new_value;
            // Re-sort after time change
            std::sort(kfs.begin(),
                      kfs.end(),
                      [](const TypedKeyframe& a, const TypedKeyframe& b)
                      { return a.time < b.time; });
            // Find new index after sort
            for (size_t i = 0; i < kfs.size(); ++i)
            {
                if (std::abs(kfs[i].time - new_time) < 0.0001f
                    && std::abs(kfs[i].value - new_value) < 0.0001f)
                {
                    drag_.keyframe_index = i;
                    break;
                }
            }
            ch->compute_auto_tangents();
            if (on_value_changed_)
            {
                on_value_changed_(drag_.channel_id, new_time, old_value, new_value);
            }
            break;
        }
        case CurveHitType::InTangent:
        {
            kf.in_tangent.dt = new_time - kf.time;
            kf.in_tangent.dv = new_value - kf.value;
            if (kf.tangent_mode == TangentMode::Aligned)
            {
                // Mirror to out tangent (co-linear)
                float len_out = std::sqrt(kf.out_tangent.dt * kf.out_tangent.dt
                                          + kf.out_tangent.dv * kf.out_tangent.dv);
                float len_in  = std::sqrt(kf.in_tangent.dt * kf.in_tangent.dt
                                          + kf.in_tangent.dv * kf.in_tangent.dv);
                if (len_in > 0.0001f)
                {
                    kf.out_tangent.dt = -kf.in_tangent.dt * (len_out / len_in);
                    kf.out_tangent.dv = -kf.in_tangent.dv * (len_out / len_in);
                }
            }
            kf.tangent_mode =
                (kf.tangent_mode == TangentMode::Auto) ? TangentMode::Free : kf.tangent_mode;
            if (on_tangent_changed_)
            {
                on_tangent_changed_(drag_.channel_id, drag_.keyframe_index);
            }
            break;
        }
        case CurveHitType::OutTangent:
        {
            kf.out_tangent.dt = new_time - kf.time;
            kf.out_tangent.dv = new_value - kf.value;
            if (kf.tangent_mode == TangentMode::Aligned)
            {
                float len_in  = std::sqrt(kf.in_tangent.dt * kf.in_tangent.dt
                                          + kf.in_tangent.dv * kf.in_tangent.dv);
                float len_out = std::sqrt(kf.out_tangent.dt * kf.out_tangent.dt
                                          + kf.out_tangent.dv * kf.out_tangent.dv);
                if (len_out > 0.0001f)
                {
                    kf.in_tangent.dt = -kf.out_tangent.dt * (len_in / len_out);
                    kf.in_tangent.dv = -kf.out_tangent.dv * (len_in / len_out);
                }
            }
            kf.tangent_mode =
                (kf.tangent_mode == TangentMode::Auto) ? TangentMode::Free : kf.tangent_mode;
            if (on_tangent_changed_)
            {
                on_tangent_changed_(drag_.channel_id, drag_.keyframe_index);
            }
            break;
        }
        default:
            break;
    }
}

void AnimationCurveEditor::end_drag()
{
    if (drag_.active && on_keyframe_moved_ && drag_.dragging == CurveHitType::Keyframe)
    {
        on_keyframe_moved_(drag_.channel_id, drag_.keyframe_index);
    }
    drag_ = CurveDragState{};
}

void AnimationCurveEditor::cancel_drag()
{
    drag_ = CurveDragState{};
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

void AnimationCurveEditor::set_on_keyframe_moved(CurveEditCallback cb)
{
    on_keyframe_moved_ = std::move(cb);
}

void AnimationCurveEditor::set_on_value_changed(CurveValueChangeCallback cb)
{
    on_value_changed_ = std::move(cb);
}

void AnimationCurveEditor::set_on_tangent_changed(CurveEditCallback cb)
{
    on_tangent_changed_ = std::move(cb);
}

// ─── Internal helpers ────────────────────────────────────────────────────────

AnimationCurveEditor::ChannelDisplay* AnimationCurveEditor::find_display(uint32_t channel_id)
{
    for (auto& d : channel_displays_)
    {
        if (d.channel_id == channel_id)
            return &d;
    }
    return nullptr;
}

const AnimationCurveEditor::ChannelDisplay* AnimationCurveEditor::find_display(
    uint32_t channel_id) const
{
    for (const auto& d : channel_displays_)
    {
        if (d.channel_id == channel_id)
            return &d;
    }
    return nullptr;
}

AnimationCurveEditor::ChannelDisplay& AnimationCurveEditor::ensure_display(uint32_t channel_id)
{
    auto* d = find_display(channel_id);
    if (d)
        return *d;

    ChannelDisplay nd;
    nd.channel_id = channel_id;
    nd.color      = kChannelColors[channel_displays_.size() % kChannelColorCount];
    nd.visible    = true;
    channel_displays_.push_back(nd);
    return channel_displays_.back();
}

// ─── ImGui Drawing ───────────────────────────────────────────────────────────

#ifdef SPECTRA_USE_IMGUI

void AnimationCurveEditor::draw(float width, float height)
{
    if (!interpolator_)
        return;

    view_.width  = width;
    view_.height = height;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##curve_editor", ImVec2(width, height), 1);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2      origin    = ImGui::GetCursorScreenPos();
    view_.origin_x        = origin.x;
    view_.origin_y        = origin.y;

    // ─── Mouse / keyboard input ──────────────────────────────────
    ImGui::InvisibleButton("##curve_editor_input", ImVec2(width, height));
    bool     hovered = ImGui::IsItemHovered();
    ImGuiIO& io      = ImGui::GetIO();
    float    mx      = io.MousePos.x;
    float    my      = io.MousePos.y;

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        auto hit = hit_test(mx, my);
        if (hit.type == CurveHitType::Keyframe || hit.type == CurveHitType::InTangent
            || hit.type == CurveHitType::OutTangent)
        {
            if (!io.KeyShift)
                deselect_all();
            if (hit.type == CurveHitType::Keyframe)
                select_keyframe(hit.channel_id, hit.keyframe_index);
            begin_drag(mx, my);
        }
        else
        {
            if (!io.KeyShift)
                deselect_all();
            box_select_active_  = true;
            box_select_start_x_ = mx;
            box_select_start_y_ = my;
        }
    }

    if (is_dragging() && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
    {
        update_drag(mx, my);
        if (interpolator_)
            interpolator_->evaluate(playhead_time_);
    }
    if (is_dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        end_drag();
        if (interpolator_)
            interpolator_->evaluate(playhead_time_);
    }
    if (is_dragging() && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        cancel_drag();
        if (interpolator_)
            interpolator_->evaluate(playhead_time_);
    }

    if (box_select_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        float t_min  = view_.x_to_time(std::min(box_select_start_x_, mx));
        float t_max  = view_.x_to_time(std::max(box_select_start_x_, mx));
        float y_top  = std::min(box_select_start_y_, my);
        float y_bot  = std::max(box_select_start_y_, my);
        float v_max  = view_.y_to_value(y_top);
        float v_min  = view_.y_to_value(y_bot);
        select_keyframes_in_rect(t_min, t_max, v_min, v_max);
        box_select_active_ = false;
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        view_.pan(io.MouseDelta.x, io.MouseDelta.y);
    }

    if (hovered && io.MouseWheel != 0.0f)
    {
        float factor = std::pow(1.1f, io.MouseWheel);
        view_.zoom(factor, mx, my);
    }

    if (hovered && selected_count() > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        delete_selected();
    }

    // ─── Background ──────────────────────────────────────────────
    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + width, origin.y + height),
                             IM_COL32(25, 25, 30, 255));

    // ─── Grid ────────────────────────────────────────────────────
    if (show_grid_)
    {
        float time_range  = view_.time_max - view_.time_min;
        float value_range = view_.value_max - view_.value_min;

        // Adaptive tick spacing
        auto compute_tick = [](float range, float pixels) -> float
        {
            float ideal     = range / (pixels / 60.0f);
            float magnitude = std::pow(10.0f, std::floor(std::log10(ideal)));
            if (ideal / magnitude < 2.0f)
                return magnitude;
            if (ideal / magnitude < 5.0f)
                return magnitude * 2.0f;
            return magnitude * 5.0f;
        };

        float time_tick  = compute_tick(time_range, width);
        float value_tick = compute_tick(value_range, height);

        // Vertical grid lines (time)
        float t = std::floor(view_.time_min / time_tick) * time_tick;
        while (t <= view_.time_max)
        {
            float x = view_.time_to_x(t);
            draw_list->AddLine(ImVec2(x, origin.y),
                               ImVec2(x, origin.y + height),
                               IM_COL32(50, 50, 55, 255));
            t += time_tick;
        }

        // Horizontal grid lines (value)
        float v = std::floor(view_.value_min / value_tick) * value_tick;
        while (v <= view_.value_max)
        {
            float y = view_.value_to_y(v);
            draw_list->AddLine(ImVec2(origin.x, y),
                               ImVec2(origin.x + width, y),
                               IM_COL32(50, 50, 55, 255));
            v += value_tick;
        }

        // Zero axes (brighter)
        float zero_x = view_.time_to_x(0.0f);
        float zero_y = view_.value_to_y(0.0f);
        if (zero_x >= origin.x && zero_x <= origin.x + width)
        {
            draw_list->AddLine(ImVec2(zero_x, origin.y),
                               ImVec2(zero_x, origin.y + height),
                               IM_COL32(80, 80, 90, 255));
        }
        if (zero_y >= origin.y && zero_y <= origin.y + height)
        {
            draw_list->AddLine(ImVec2(origin.x, zero_y),
                               ImVec2(origin.x + width, zero_y),
                               IM_COL32(80, 80, 90, 255));
        }
    }

    // ─── Curves ──────────────────────────────────────────────────
    for (const auto& [id, ch] : interpolator_->channels())
    {
        if (!is_channel_visible(id))
            continue;
        if (ch.empty())
            continue;

        Color col       = channel_color(id);
        ImU32 curve_col = IM_COL32(static_cast<int>(col.r * 255),
                                   static_cast<int>(col.g * 255),
                                   static_cast<int>(col.b * 255),
                                   200);

        // Sample the curve
        auto  samples = ch.sample(view_.time_min, view_.time_max, curve_resolution_);
        float step = (view_.time_max - view_.time_min) / static_cast<float>(curve_resolution_ - 1);

        for (size_t i = 0; i + 1 < samples.size(); ++i)
        {
            float t0 = view_.time_min + step * static_cast<float>(i);
            float t1 = view_.time_min + step * static_cast<float>(i + 1);
            float x0 = view_.time_to_x(t0);
            float y0 = view_.value_to_y(samples[i]);
            float x1 = view_.time_to_x(t1);
            float y1 = view_.value_to_y(samples[i + 1]);
            draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), curve_col, 2.0f);
        }

        // ─── Keyframe diamonds + tangent handles ─────────────────
        const auto& kfs = ch.keyframes();
        for (const auto& kf : kfs)
        {
            float kx = view_.time_to_x(kf.time);
            float ky = view_.value_to_y(kf.value);

            // Tangent handles
            if (show_tangents_ && kf.interp == InterpMode::CubicBezier)
            {
                // In tangent
                if (kf.in_tangent.dt != 0.0f || kf.in_tangent.dv != 0.0f)
                {
                    float ix = view_.time_to_x(kf.time + kf.in_tangent.dt);
                    float iy = view_.value_to_y(kf.value + kf.in_tangent.dv);
                    draw_list->AddLine(ImVec2(kx, ky),
                                       ImVec2(ix, iy),
                                       IM_COL32(150, 150, 150, 150),
                                       1.0f);
                    draw_list->AddCircleFilled(ImVec2(ix, iy), 3.0f, IM_COL32(180, 180, 180, 200));
                }

                // Out tangent
                if (kf.out_tangent.dt != 0.0f || kf.out_tangent.dv != 0.0f)
                {
                    float ox = view_.time_to_x(kf.time + kf.out_tangent.dt);
                    float oy = view_.value_to_y(kf.value + kf.out_tangent.dv);
                    draw_list->AddLine(ImVec2(kx, ky),
                                       ImVec2(ox, oy),
                                       IM_COL32(150, 150, 150, 150),
                                       1.0f);
                    draw_list->AddCircleFilled(ImVec2(ox, oy), 3.0f, IM_COL32(180, 180, 180, 200));
                }
            }

            // Diamond keyframe marker
            float sz     = kf.selected ? 6.0f : 4.5f;
            ImU32 kf_col = kf.selected ? IM_COL32(255, 255, 100, 255) : curve_col;
            draw_list->AddQuadFilled(ImVec2(kx, ky - sz),
                                     ImVec2(kx + sz, ky),
                                     ImVec2(kx, ky + sz),
                                     ImVec2(kx - sz, ky),
                                     kf_col);

            if (kf.selected)
            {
                draw_list->AddQuad(ImVec2(kx, ky - sz - 1),
                                   ImVec2(kx + sz + 1, ky),
                                   ImVec2(kx, ky + sz + 1),
                                   ImVec2(kx - sz - 1, ky),
                                   IM_COL32(255, 255, 255, 200));
            }

            // Value label
            if (show_value_labels_)
            {
                const std::string buf = std::format("{:.2f}", kf.value);
                draw_list->AddText(ImVec2(kx + 8, ky - 8), IM_COL32(200, 200, 200, 180), buf.c_str());
            }
        }
    }

    // ─── Playhead ────────────────────────────────────────────────
    float ph_x = view_.time_to_x(playhead_time_);
    if (ph_x >= origin.x && ph_x <= origin.x + width)
    {
        draw_list->AddLine(ImVec2(ph_x, origin.y),
                           ImVec2(ph_x, origin.y + height),
                           IM_COL32(255, 80, 80, 200),
                           1.5f);
    }

    // ─── Box-select overlay ──────────────────────────────────────
    if (box_select_active_)
    {
        draw_list->AddRect(ImVec2(box_select_start_x_, box_select_start_y_),
                           ImVec2(mx, my),
                           IM_COL32(255, 255, 255, 180));
        draw_list->AddRectFilled(ImVec2(box_select_start_x_, box_select_start_y_),
                                 ImVec2(mx, my),
                                 IM_COL32(255, 255, 255, 20));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra
