#include "ros_app_shell.hpp"

#include "ui/node_graph_panel.hpp"
#include "ui/shell/panel.hpp"
#include "ui/theme/icons.hpp"

namespace spectra::adapters::ros2
{

namespace
{
using spectra::ui::Icon;
using spectra::ui::shell::CallbackPanel;
using spectra::ui::shell::DockSlot;
using spectra::ui::shell::PanelInfo;
}   // namespace

void RosAppShell::wire_panel_callbacks()
{
    if (topic_list_)
    {
        topic_list_->set_select_callback([this](const std::string& topic)
                                         { on_topic_selected(topic); });

        topic_list_->set_plot_callback([this](const std::string& topic) { on_topic_plot(topic); });

        topic_list_->set_field_plot_callback(
            [this](const std::string& topic, const std::string& field, const std::string& type)
            {
                workspace_state_.select_topic(topic, type);
                workspace_state_.select_field(field);
                workspace_state_.request_plot();
            });

        topic_list_->set_favorite_callbacks(
            [this](const std::string& topic) { return favorite_topics_.contains(topic); },
            [this](const std::string& topic)
            {
                if (topic.empty())
                    return;
                if (favorite_topics_.contains(topic))
                    favorite_topics_.erase(topic);
                else
                    favorite_topics_.insert(topic);
                if (session_mgr_)
                {
                    std::vector<std::string> favorites(favorite_topics_.begin(),
                                                       favorite_topics_.end());
                    session_mgr_->save_favorites(favorites);
                }
            });
    }

    drag_drop_ = std::make_unique<FieldDragDrop>();
    drag_drop_->set_plot_request_callback([this](const FieldDragPayload& payload, PlotTarget target)
                                          { handle_plot_request(payload, target); });

    if (topic_list_)
        topic_list_->set_drag_drop(drag_drop_.get());
    if (topic_echo_)
        topic_echo_->set_drag_drop(drag_drop_.get());

    // Wire echo panel into topic list so the monitor can show inline echo.
    if (topic_list_ && topic_echo_)
        topic_list_->set_echo_panel(topic_echo_.get());

    // Forward echo panel message arrivals to topic stats and topic list so
    // that statistics update for the selected topic even when it isn't plotted.
    if (topic_echo_)
    {
        topic_echo_->set_message_callback(
            [this](const std::string& topic, size_t bytes)
            {
                bool has_monitor = false;
                {
                    std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                    has_monitor = monitor_subs_.contains(topic);
                }
                if (!has_monitor)
                {
                    if (topic_stats_)
                        topic_stats_->notify_message(topic, bytes, -1);
                    if (topic_list_)
                        topic_list_->notify_message(topic, bytes);
                }
            });
    }

    if (topic_stats_)
    {
        topic_stats_->set_plot_callback(
            [this](const std::string& topic)
            {
                if (!topic.empty())
                    on_topic_plot(topic);
            });
        topic_stats_->set_echo_callback(
            [this](const std::string& topic)
            {
                if (!topic.empty())
                    on_topic_selected(topic);
                set_panel_visible("ros.topic_echo", true);
            });
        topic_stats_->set_favorite_callback(
            [this](const std::string& topic)
            {
                if (topic.empty())
                    return;
                if (favorite_topics_.contains(topic))
                    favorite_topics_.erase(topic);
                else
                    favorite_topics_.insert(topic);
                if (session_mgr_)
                {
                    std::vector<std::string> favorites(favorite_topics_.begin(),
                                                       favorite_topics_.end());
                    session_mgr_->save_favorites(favorites);
                }
            });
    }

    if (node_graph_panel_)
    {
        node_graph_panel_->set_select_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                {
                    on_topic_selected(n.id);
                }
                else if (param_editor_)
                {
                    param_editor_->set_target_node(n.id);
                    set_panel_visible("ros.param_editor", true);
                }
            });

        node_graph_panel_->set_activate_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                    add_topic_plot(n.id);
            });
    }
}

void RosAppShell::handle_plot_request(const FieldDragPayload& payload, PlotTarget target)
{
    if (!payload.valid())
        return;

    std::string topic_field = payload.topic_name;
    if (!payload.field_path.empty())
        topic_field += ':' + payload.field_path;

    // If workspace has an active subplot selection, target that slot.
    if (target == PlotTarget::CurrentAxes && workspace_state_.active_subplot_idx > 0
        && subplot_mgr_)
    {
        const int   slot  = workspace_state_.active_subplot_idx;
        std::string field = payload.field_path;
        if (field.empty())
            field = default_numeric_field(payload.topic_name, payload.type_name);
        if (!field.empty())
        {
            subplot_mgr_->add_plot(slot, payload.topic_name, field, payload.type_name);
            set_panel_visible("ros.plot_area", true);
            return;
        }
    }

    add_topic_plot(topic_field);
}

#ifdef SPECTRA_USE_IMGUI
void RosAppShell::on_register_panels()
{
    auto add_panel = [this](const char* id,
                            const char* title,
                            const char* category,
                            Icon        icon,
                            DockSlot    slot,
                            bool        default_visible,
                            auto        draw_fn)
    {
        PanelInfo info;
        info.id              = id;
        info.title           = title;
        info.category        = category;
        info.icon            = icon;
        info.slot            = slot;
        info.default_visible = default_visible;
        panels().add(std::make_unique<CallbackPanel>(std::move(info), std::move(draw_fn)));
    };

    add_panel("ros.topic_list",
              "Topic Monitor",
              "Topics",
              Icon::List,
              DockSlot::Left,
              true,
              [this](bool* p_open) { draw_topic_list(p_open); });

    add_panel("ros.topic_echo",
              "Topic Echo",
              "Topics",
              Icon::Command,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_topic_echo(p_open); });

    add_panel("ros.topic_stats",
              "Topic Stats",
              "Topics",
              Icon::BarChart,
              DockSlot::Right,
              false,
              [this](bool* p_open) { draw_topic_stats(p_open); });

    add_panel("ros.plot_area",
              "Plot Area",
              "Plots",
              Icon::ChartLine,
              DockSlot::Center,
              true,
              [this](bool* p_open) { draw_plot_area(p_open); });

    add_panel("ros.expression_editor",
              "Expression Editor",
              "Plots",
              Icon::Axes,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_expression_editor(p_open); });

    add_panel("ros.bag_info",
              "Bag Info",
              "Bag",
              Icon::Database,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_bag_info(p_open); });

    add_panel("ros.bag_playback",
              "Bag Playback",
              "Bag",
              Icon::Play,
              DockSlot::Bottom,
              false,
              [this](bool* p_open) { draw_bag_playback(p_open); });

    add_panel("ros.log_viewer",
              "Log Viewer",
              "Diagnostics",
              Icon::FileText,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_log_viewer(p_open); });

    add_panel("ros.diagnostics",
              "Diagnostics",
              "Diagnostics",
              Icon::Warning,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_diagnostics(p_open); });

    add_panel("ros.node_graph",
              "Node Graph",
              "Diagnostics",
              Icon::Link,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_node_graph(p_open); });

    add_panel("ros.displays",
              "Displays",
              "3D / RViz",
              Icon::Eye,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_displays_panel(p_open); });

    add_panel("ros.scene_viewport",
              "3D Scene",
              "3D / RViz",
              Icon::Maximize,
              DockSlot::Center,
              false,
              [this](bool* p_open) { draw_scene_viewport(p_open); });

    add_panel("ros.inspector",
              "Inspector",
              "3D / RViz",
              Icon::Search,
              DockSlot::Right,
              false,
              [this](bool* p_open) { draw_inspector_panel(p_open); });

    add_panel("ros.tf_tree",
              "TF Tree",
              "3D / RViz",
              Icon::Timeline,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_tf_tree(p_open); });

    add_panel("ros.param_editor",
              "Param Editor",
              "Services",
              Icon::Settings,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_param_editor(p_open); });

    add_panel("ros.service_caller",
              "Service Caller",
              "Services",
              Icon::Wrench,
              DockSlot::Floating,
              false,
              [this](bool* p_open) { draw_service_caller(p_open); });

    for (const auto& [id, visible] : pending_panel_visibility_)
        panels().set_visible(id, visible);
}
#endif

}   // namespace spectra::adapters::ros2
