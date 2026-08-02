#include "ros_app_shell_drop_targets.hpp"

#include <algorithm>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "ui/imgui/widgets.hpp"
#endif

namespace spectra::adapters::ros2
{

namespace
{

#ifdef SPECTRA_USE_IMGUI

bool accept_field_drop(const RosPlotDropTargetContext& ctx, FieldDragPayload& payload_out)
{
    if (!ctx.drag_drop || !ctx.drag_drop->is_dragging())
        return false;

    if (!ImGui::BeginDragDropTarget())
        return false;

    const ImGuiPayload* imgui_payload = ImGui::AcceptDragDropPayload(FieldDragDrop::DRAG_TYPE);
    const bool          accepted      = imgui_payload != nullptr
                          && FieldDragDrop::try_parse_imgui_payload(imgui_payload, payload_out);
    ImGui::EndDragDropTarget();
    return accepted;
}

void add_plot_from_payload(const RosPlotDropTargetContext& ctx,
                           const FieldDragPayload&         payload,
                           int                             target_slot)
{
    if (!ctx.subplot_mgr || target_slot <= 0 || !payload.valid())
        return;

    std::string field = payload.field_path;
    if (field.empty() && ctx.default_numeric_field)
        field = ctx.default_numeric_field(payload.topic_name, payload.type_name);
    if (!field.empty())
        ctx.subplot_mgr->add_plot(target_slot, payload.topic_name, field, payload.type_name);
}

#endif   // SPECTRA_USE_IMGUI

}   // namespace

void draw_subplot_slot_drop_target(const RosPlotDropTargetContext& ctx,
                                   int                             slot,
                                   float                           drop_width,
                                   float                           remaining_height)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ctx.drag_drop || !ctx.subplot_mgr)
        return;

    const float  remaining = std::max(8.0f, remaining_height);
    const ImVec2 pos       = ImGui::GetCursorScreenPos();
    const float  w         = std::max(1.0f, drop_width);
    ImGui::InvisibleButton("##slot_drop", ImVec2(w, remaining));

    if (!ctx.drag_drop->is_dragging())
        return;

    const bool   hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 pmax    = {pos.x + w, pos.y + remaining};
    spectra::ui::widgets::drop_zone_overlay(pmax.y - pos.y > 36.0f ? pos : ImVec2(pos.x, pos.y),
                                            pmax,
                                            hovered ? "Add to current plot" : "Drop numeric field",
                                            hovered);

    FieldDragPayload payload;
    if (accept_field_drop(ctx, payload))
        add_plot_from_payload(ctx, payload, slot);
#else
    (void)ctx;
    (void)slot;
    (void)drop_width;
    (void)remaining_height;
#endif
}

void draw_global_plot_drop_target(const RosPlotDropTargetContext& ctx, float width, float height)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ctx.drag_drop || !ctx.subplot_mgr)
        return;

    const ImVec2 avail = ImVec2(std::max(1.0f, width), height);
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##global_drop", avail);

    if (!ctx.drag_drop->is_dragging())
        return;

    const bool   hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 pmax    = {pos.x + avail.x, pos.y + avail.y};
    spectra::ui::widgets::drop_zone_overlay(pos,
                                            pmax,
                                            hovered ? "New subplot" : "Drop for new plot",
                                            hovered);

    FieldDragPayload payload;
    if (!accept_field_drop(ctx, payload))
        return;

    int target_slot = -1;
    for (int s = 1; s <= ctx.subplot_mgr->capacity(); ++s)
    {
        if (!ctx.subplot_mgr->has_plot(s))
        {
            target_slot = s;
            break;
        }
    }
    if (target_slot < 0)
        target_slot = ctx.subplot_mgr->add_row();

    add_plot_from_payload(ctx, payload, target_slot);
#else
    (void)ctx;
    (void)width;
    (void)height;
#endif
}

}   // namespace spectra::adapters::ros2
