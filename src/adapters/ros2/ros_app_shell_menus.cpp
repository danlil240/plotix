#include "ros_app_shell.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "scene/scene_manager.hpp"
#include "ui/tf_tree_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <cinttypes>
    #ifdef IMGUI_HAS_DOCK
        #include <imgui_internal.h>
    #endif
    #include "ui/imgui/widgets.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/nav_rail.hpp"
    #include "ui/shell/status_bar.hpp"
    #include "ui/theme/icons.hpp"
#endif

namespace spectra::adapters::ros2
{

namespace
{

std::string normalize_frame_label(const std::string& frame)
{
    if (!frame.empty() && frame.front() == '/')
        return frame.substr(1);
    return frame;
}

spectra::ui::shell::MenuAction make_menu_action(std::string           label,
                                                std::function<void()> on_click,
                                                std::string           shortcut = {},
                                                std::function<bool()> enabled  = {},
                                                std::function<bool()> checked  = {})
{
    spectra::ui::shell::MenuAction action;
    action.label    = std::move(label);
    action.shortcut = std::move(shortcut);
    action.on_click = std::move(on_click);
    if (enabled)
        action.enabled = std::move(enabled);
    if (checked)
        action.checked = std::move(checked);
    return action;
}

std::vector<std::string> collect_known_fixed_frames(const TfTreePanel*  tf_tree_panel,
                                                    const SceneManager& scene_manager,
                                                    const std::string&  current_fixed_frame)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string>        frames;

    auto add_frame = [&](const std::string& frame)
    {
        const std::string normalized = normalize_frame_label(frame);
        if (normalized.empty())
            return;
        if (seen.insert(normalized).second)
            frames.push_back(normalized);
    };

    if (tf_tree_panel)
    {
        for (const auto& frame : tf_tree_panel->buffer().all_frames())
            add_frame(frame);
    }

    for (const auto& entity : scene_manager.entities())
        add_frame(entity.frame_id);

    add_frame(current_fixed_frame);

    std::sort(frames.begin(), frames.end());
    return frames;
}

}   // namespace

#ifdef SPECTRA_USE_IMGUI
void RosAppShell::on_populate_menus(spectra::ui::shell::MenuBar& bar)
{
    auto& view = bar.menu("View");
    view.add(make_menu_action(
        "Navigation Rail",
        [this]()
        {
            set_nav_rail_visible(!nav_rail_visible());
            sync_layout_chrome();
        },
        {},
        {},
        [this]() { return nav_rail_visible(); }));
    view.add(make_menu_action(
        "Expand Rail",
        [this]()
        {
            set_nav_rail_expanded(!nav_rail_expanded());
            sync_layout_chrome();
        },
        {},
        [this]() { return nav_rail_visible(); },
        [this]() { return nav_rail_expanded(); }));
    view.add_separator();

    view.add(make_menu_action(
        "Fixed Frame...",
        [this]() { ImGui::OpenPopup("##ros_fixed_frame_menu_popup"); },
        {},
        [this]()
        {
            return panel_visible("ros.scene_viewport") || panel_visible("ros.displays")
                   || panel_visible("ros.tf_tree");
        }));

    view.add_separator();
    view.add({.label = "Reset Dock Layout", .on_click = [this]() { request_dock_layout_reset(); }});

    auto& layout_menu = bar.menu("Layout");
    layout_menu.add(
        {.label = "Reset Docked Layout", .on_click = [this]() { request_dock_layout_reset(); }});
    layout_menu.add_separator();
    {
        static const LayoutPreset kPresets[] = {
            LayoutPreset::Default,
            LayoutPreset::Debug,
            LayoutPreset::Monitor,
            LayoutPreset::BagReview,
            LayoutPreset::RViz,
            LayoutPreset::RVizPlot,
        };
        for (const auto p : kPresets)
        {
            layout_menu.add(make_menu_action(
                layout_preset_name(p),
                [this, p]() { apply_layout_preset(p); },
                {},
                {},
                [this, p]() { return current_preset_ == p; }));
        }
    }

    auto& plots = bar.menu("Plots");
    plots.add(make_menu_action(
        "Add Selected Field to Plot",
        [this]() { workspace_state_.request_plot(); },
        {},
        [this]() { return !workspace_state_.selected_field.empty(); }));
    plots.add(make_menu_action(
        "Add Selected Topic to Plot",
        [this]() { workspace_state_.request_plot(); },
        {},
        [this]() { return !workspace_state_.selected_topic.empty(); }));
    plots.add_separator();
    plots.add({.label    = "Add Subplot Row",
               .on_click = [this]() { cmd_registry_.execute("ros.plot.add_subplot"); }});
    plots.add(make_menu_action(
        "Remove Last Row",
        [this]()
        {
            if (subplot_mgr_)
                subplot_mgr_->remove_last_row();
        },
        {},
        [this]() { return subplot_mgr_ && subplot_mgr_->rows() > 1; }));
    plots.add_separator();
    plots.add({.label    = "Clear Active Plot",
               .on_click = [this]() { cmd_registry_.execute("ros.plot.clear_active"); }});
    plots.add({.label = "Clear All Plots", .on_click = [this]() { clear_plots(); }});
    plots.add_separator();
    plots.add({.label = "Reset Basic Display", .shortcut = "R", .on_click = [this]() {
                   cmd_registry_.execute("ros.plot.reset");
               }});
    plots.add(make_menu_action(
        "Auto-Fit Y",
        [this]() { cmd_registry_.execute("ros.plot.autofit"); },
        "A",
        [this]() { return subplot_mgr_ != nullptr; }));
    plots.add_separator();
    plots.add(make_menu_action(
        "Toggle Pause/Live",
        [this]() { cmd_registry_.execute("ros.live_pause"); },
        "Space",
        [this]() { return subplot_mgr_ != nullptr; }));
    plots.add({.label = "Resume All Scroll", .shortcut = "Home", .on_click = [this]() {
                   cmd_registry_.execute("ros.plot.resume_all");
               }});
    plots.add({.label    = "Pause All Scroll",
               .on_click = [this]()
               {
                   if (subplot_mgr_)
                       subplot_mgr_->pause_all_scroll();
               }});
    plots.add_separator();
    plots.add(make_menu_action(
        "Toggle Grid",
        [this]() { cmd_registry_.execute("ros.plot.toggle_grid"); },
        "G",
        [this]() { return subplot_mgr_ != nullptr; }));

    auto& session = bar.menu("Session");
    session.add({.label = "Save Session", .shortcut = "Ctrl+Shift+W", .on_click = [this]() {
                     cmd_registry_.execute("ros.session.save");
                 }});
    session.add({.label    = "Save Session As...",
                 .on_click = [this]()
                 {
                     session_save_path_buf_ =
                         RosSessionManager::default_session_path(cfg_.node_name);
                     cmd_registry_.execute("ros.session.save");
                 }});
    session.add({.label    = "Load Session...",
                 .on_click = [this]() { cmd_registry_.execute("ros.session.load"); }});
    session.add({.label    = "Import Session (merge)...",
                 .on_click = [this]() { cmd_registry_.execute("ros.session.merge"); }});
    session.add_separator();

    spectra::ui::shell::MenuAction recent_parent;
    recent_parent.label = "Recent Sessions";
    if (session_mgr_)
    {
        const auto recents = session_mgr_->load_recent();
        for (size_t i = 0; i < recents.size() && i < 5; ++i)
        {
            const std::string& path  = recents[i].path;
            const auto         slash = path.rfind('/');
            const std::string  label = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            recent_parent.submenu.push_back({.label    = label,
                                             .shortcut = recents[i].node,
                                             .on_click = [this, path, label]()
                                             {
                                                 const auto result = load_session(path);
                                                 if (result.ok)
                                                 {
                                                     session_status_msg_   = "Loaded: " + label;
                                                     session_status_timer_ = 3.0f;
                                                 }
                                                 else
                                                 {
                                                     session_status_msg_ =
                                                         "Load failed: " + result.error;
                                                     session_status_timer_ = 4.0f;
                                                 }
                                             }});
        }
        if (!recents.empty())
        {
            recent_parent.submenu.push_back({.separator = true});
            recent_parent.submenu.push_back(
                {.label = "Clear Recent", .on_click = [this]() { session_mgr_->clear_recent(); }});
        }
    }
    session.add(std::move(recent_parent));

    auto& tools = bar.menu("Tools");
    tools.add(make_menu_action(
        "Screenshot",
        [this]() { cmd_registry_.execute("ros.screenshot"); },
        "Ctrl+Shift+S",
        [this]() { return screenshot_export_ != nullptr; }));
    tools.add(make_menu_action(
        "Record Video...",
        [this]() { show_record_dialog_ = !show_record_dialog_; },
        {},
        [this]() { return screenshot_export_ != nullptr; },
        [this]() { return show_record_dialog_; }));

    auto& help = bar.menu("Help");
    help.add({.label    = "About Spectra-ROS",
              .on_click = []() { ImGui::OpenPopup("##ros_about_popup"); }});
    help.add({.label    = "Keyboard Shortcuts",
              .on_click = []() { ImGui::OpenPopup("##ros_shortcuts_popup"); }});
    help.add({.label    = "Command Palette (Ctrl+K)",
              .on_click = [this]() { cmd_registry_.execute("ros.app.command_palette"); }});
}

void RosAppShell::on_populate_nav_rail(spectra::ui::shell::NavRail& rail)
{
    rail.set_show_registry_panels(false);

    auto add_panel_toggle = [this, &rail](const char* id, const char* label, spectra::ui::Icon icon)
    {
        spectra::ui::shell::NavItem item;
        item.id        = id;
        item.label     = label;
        item.icon      = icon;
        item.tooltip   = label;
        item.is_active = [this, id]() { return panel_visible(id); };
        item.on_click  = [this, id]() { panels().toggle(id); };
        rail.add_custom_item(std::move(item));
    };

    spectra::ui::shell::NavItem workspace_header;
    workspace_header.label             = "Workspace";
    workspace_header.is_section_header = true;
    rail.add_custom_item(std::move(workspace_header));

    add_panel_toggle("ros.topic_list", "Topics", spectra::ui::Icon::List);
    add_panel_toggle("ros.plot_area", "Plots", spectra::ui::Icon::ChartLine);
    add_panel_toggle("ros.scene_viewport", "3D Scene", spectra::ui::Icon::Maximize);
    add_panel_toggle("ros.diagnostics", "Diagnostics", spectra::ui::Icon::Warning);

    if (bag_player_ && bag_player_->is_open())
        add_panel_toggle("ros.bag_playback", "Bag", spectra::ui::Icon::Play);

    spectra::ui::shell::NavItem tools_header;
    tools_header.label             = "Tools";
    tools_header.is_section_header = true;
    rail.add_custom_item(std::move(tools_header));

    spectra::ui::shell::NavItem layouts_item;
    layouts_item.id       = "ros.nav.layouts";
    layouts_item.label    = "Layouts";
    layouts_item.icon     = spectra::ui::Icon::Layout;
    layouts_item.tooltip  = "Layout presets";
    layouts_item.on_click = []() { ImGui::OpenPopup("##ros_nav_layouts_popup"); };
    rail.add_custom_item(std::move(layouts_item));

    spectra::ui::shell::NavItem reset_item;
    reset_item.id       = "ros.nav.reset_layout";
    reset_item.label    = "Reset";
    reset_item.icon     = spectra::ui::Icon::Refresh;
    reset_item.tooltip  = "Reset dock layout";
    reset_item.on_click = [this]() { request_dock_layout_reset(); };
    rail.add_custom_item(std::move(reset_item));
}

namespace
{

void dock_ros_orphan_windows(ImGuiID fallback_node)
{
    ImGui::DockBuilderDockWindow("Node Graph###NodeGraphPanel", fallback_node);
    ImGui::DockBuilderDockWindow("Expression Editor", fallback_node);
    ImGui::DockBuilderDockWindow("Parameter Editor", fallback_node);
    ImGui::DockBuilderDockWindow("Service Caller", fallback_node);
}

void dock_all_ros_windows(ImGuiID node)
{
    ImGui::DockBuilderDockWindow("Plot Area", node);
    ImGui::DockBuilderDockWindow("Scene Viewport", node);
    ImGui::DockBuilderDockWindow("Topic Monitor", node);
    ImGui::DockBuilderDockWindow("Displays", node);
    ImGui::DockBuilderDockWindow("Inspector", node);
    ImGui::DockBuilderDockWindow("Topic Statistics", node);
    ImGui::DockBuilderDockWindow("Topic Echo", node);
    ImGui::DockBuilderDockWindow("ROS2 Log", node);
    ImGui::DockBuilderDockWindow("Bag Info", node);
    ImGui::DockBuilderDockWindow("Bag Playback", node);
    ImGui::DockBuilderDockWindow("Diagnostics", node);
    ImGui::DockBuilderDockWindow("TF Tree", node);
    dock_ros_orphan_windows(node);
}

void begin_ros_dock_layout(unsigned int dockspace_id, ImGuiID& out_root)
{
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetWindowSize());
    out_root = dockspace_id;
}

void finish_ros_dock_layout(unsigned int dockspace_id)
{
    ImGui::DockBuilderFinish(dockspace_id);
}

void build_default_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_left         = 0;
    ImGuiID dock_main         = 0;
    ImGuiID dock_right        = 0;
    ImGuiID dock_bottom       = 0;
    ImGuiID dock_right_bottom = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.18f, &dock_left, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, &dock_right, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_main);
    ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.42f, &dock_right_bottom, &dock_right);

    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    ImGui::DockBuilderDockWindow("Node Graph###NodeGraphPanel", dock_right_bottom);
    ImGui::DockBuilderDockWindow("Topic Echo", dock_bottom);
    ImGui::DockBuilderDockWindow("ROS2 Log", dock_bottom);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

void build_debug_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_left  = 0;
    ImGuiID dock_main  = 0;
    ImGuiID dock_right = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.26f, &dock_left, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, &dock_right, &dock_main);

    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Topic Echo", dock_main);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    ImGui::DockBuilderDockWindow("ROS2 Log", dock_right);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

void build_monitor_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_left  = 0;
    ImGuiID dock_main  = 0;
    ImGuiID dock_right = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.26f, &dock_left, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, &dock_right, &dock_main);

    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    ImGui::DockBuilderDockWindow("Diagnostics", dock_right);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

void build_bag_review_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_bottom = 0;
    ImGuiID dock_top    = 0;
    ImGuiID dock_left   = 0;
    ImGuiID dock_main   = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Down, 0.22f, &dock_bottom, &dock_top);
    ImGui::DockBuilderSplitNode(dock_top, ImGuiDir_Left, 0.28f, &dock_left, &dock_main);

    ImGui::DockBuilderDockWindow("Bag Playback", dock_bottom);
    ImGui::DockBuilderDockWindow("Bag Info", dock_left);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

void build_rviz_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_left  = 0;
    ImGuiID dock_main  = 0;
    ImGuiID dock_right = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.24f, &dock_left, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.26f, &dock_right, &dock_main);

    ImGui::DockBuilderDockWindow("Displays", dock_left);
    ImGui::DockBuilderDockWindow("TF Tree", dock_left);
    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Scene Viewport", dock_main);
    ImGui::DockBuilderDockWindow("Inspector", dock_right);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

void build_rviz_plot_ros_dock(unsigned int dockspace_id)
{
    ImGuiID root = 0;
    begin_ros_dock_layout(dockspace_id, root);

    ImGuiID dock_left  = 0;
    ImGuiID dock_main  = 0;
    ImGuiID dock_right = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.24f, &dock_left, &dock_main);
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, &dock_right, &dock_main);

    ImGui::DockBuilderDockWindow("Displays", dock_left);
    ImGui::DockBuilderDockWindow("TF Tree", dock_left);
    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Scene Viewport", dock_main);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    ImGui::DockBuilderDockWindow("Inspector", dock_right);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    dock_ros_orphan_windows(root);
    finish_ros_dock_layout(dockspace_id);
}

}   // namespace

void RosAppShell::on_default_layout(unsigned int dockspace_id)
{
    #ifdef IMGUI_HAS_DOCK
    if (dockspace_id == 0)
        return;

    switch (current_preset_)
    {
        case LayoutPreset::Debug:
            build_debug_ros_dock(dockspace_id);
            break;
        case LayoutPreset::Monitor:
            build_monitor_ros_dock(dockspace_id);
            break;
        case LayoutPreset::BagReview:
            build_bag_review_ros_dock(dockspace_id);
            break;
        case LayoutPreset::RViz:
            build_rviz_ros_dock(dockspace_id);
            break;
        case LayoutPreset::RVizPlot:
            build_rviz_plot_ros_dock(dockspace_id);
            break;
        case LayoutPreset::Default:
        default:
            build_default_ros_dock(dockspace_id);
            break;
    }

    layout_change_tracking_enabled_ = false;
    layout_settle_frames_           = 0;
    layout_unsaved_                 = false;
    #else
    (void)dockspace_id;
    #endif
}

void RosAppShell::on_build_status_bar(spectra::ui::shell::StatusBar& bar)
{
    bar.clear();

    bar.add_segment(
        {.align   = spectra::ui::shell::StatusAlign::Left,
         .draw_fn = [this]()
         {
             const auto&    colors    = spectra::ui::theme();
             const bool     connected = bridge_ && bridge_->is_ok();
             const uint64_t total     = total_messages_.load(std::memory_order_relaxed);
             const int      plots     = active_plot_count();
             std::vector<spectra::ui::widgets::StatusPillSpec> pills;
             pills.push_back({connected ? "ROS connected" : "ROS idle",
                              connected ? colors.success : colors.warning,
                              true});
             pills.push_back(
                 {std::format("{} · domain {}", cfg_.node_name, bridge_ ? bridge_->domain_id() : 0),
                  colors.accent,
                  false});
             pills.push_back(
                 {std::format("{} plots · {} msgs", plots, total), colors.accent, false});
             if (layout_unsaved_)
                 pills.push_back({"layout unsaved", colors.warning, false});
             spectra::ui::widgets::draw_status_pills(pills);
         }});

    bar.add_segment(
        {.align   = spectra::ui::shell::StatusAlign::Center,
         .draw_fn = [this]()
         {
             const auto&                                       colors = spectra::ui::theme();
             std::vector<spectra::ui::widgets::StatusPillSpec> pills;
             if (discovery_)
             {
                 pills.push_back(
                     {std::format("{} topics", discovery_->topics().size()), colors.accent, false});
             }
             if (bag_player_ && bag_player_->is_open())
                 pills.push_back({"bag", colors.success, false});
             if (workspace_state_.clock.is_bag_mode())
             {
                 pills.push_back({std::format("t={:.2f}", workspace_state_.clock.plot_now_sec()),
                                  colors.accent,
                                  false});
             }
             if (!workspace_state_.selected_topic.empty())
                 pills.push_back({workspace_state_.selected_topic, colors.accent, false});
             spectra::ui::widgets::draw_status_pills(pills);
         }});

    bar.add_segment(
        {.align   = spectra::ui::shell::StatusAlign::Right,
         .draw_fn = [this]()
         {
             const auto&                                       colors = spectra::ui::theme();
             std::vector<spectra::ui::widgets::StatusPillSpec> pills;
             if (panel_visible("ros.scene_viewport") || panel_visible("ros.displays")
                 || !displays_.empty())
             {
                 const char* fixed_frame = workspace_state_.fixed_frame.empty()
                                               ? "(auto)"
                                               : workspace_state_.fixed_frame.c_str();
                 pills.push_back({std::format("frame {}", fixed_frame), colors.accent, false});
             }
             const int plots = active_plot_count();
             if (plots > 0 && subplot_mgr_)
             {
                 const size_t mem = subplot_mgr_->total_memory_bytes();
                 if (mem > 0)
                 {
                     pills.push_back({std::format("{:.1f} KB", static_cast<double>(mem) / 1024.0),
                                      colors.accent,
                                      false});
                 }
             }
             pills.push_back(
                 {std::format("{:.0f} FPS", ImGui::GetIO().Framerate), colors.success, false});
             spectra::ui::widgets::draw_status_pills(pills);
         }});
}
#endif

void RosAppShell::draw_ros_shell_popups()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginPopup("##ros_nav_layouts_popup"))
    {
        draw_layout_preset_menu();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##ros_fixed_frame_menu_popup"))
    {
        const std::vector<std::string> frames =
            collect_known_fixed_frames(tf_tree_panel_.get(),
                                       scene_manager_,
                                       workspace_state_.fixed_frame);
        const bool is_auto = workspace_state_.fixed_frame.empty();
        if (ImGui::Selectable("(auto)", is_auto))
            workspace_state_.fixed_frame.clear();
        for (const auto& frame : frames)
        {
            const bool selected = workspace_state_.fixed_frame == frame;
            if (ImGui::Selectable(frame.c_str(), selected))
                workspace_state_.fixed_frame = frame;
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("##ros_about_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Spectra ROS — GPU-accelerated rqt + RViz studio");
        ImGui::TextDisabled("Node: %s", cfg_.node_name.c_str());
        if (ImGui::Button("Close##about"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("##ros_shortcuts_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("Global");
        ImGui::TextUnformatted("Ctrl+K          Command Palette");
        ImGui::TextUnformatted("Ctrl+Shift+S    Screenshot");
        ImGui::TextUnformatted("Ctrl+Shift+W    Save session");
        ImGui::Spacing();
        ImGui::TextDisabled("Plot");
        ImGui::TextUnformatted("Space           Toggle pause/live (active plot)");
        ImGui::TextUnformatted("Home            Resume all scroll");
        ImGui::TextUnformatted("G               Toggle grid (active plot)");
        ImGui::TextUnformatted("R               Reset plot display");
        ImGui::TextUnformatted("A               Auto-fit Y (active plot)");
        ImGui::Spacing();
        ImGui::TextDisabled("ROS");
        ImGui::TextUnformatted("Ctrl+K          Search commands (echo, refresh, time window...)");
        ImGui::Spacing();
        ImGui::TextDisabled("Tip");
        ImGui::TextDisabled("Press Ctrl+K to search and run any command.");
        if (ImGui::Button("Close##shortcuts"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#endif
}

void RosAppShell::draw_layout_preset_menu()
{
#ifdef SPECTRA_USE_IMGUI
    static const LayoutPreset kPresets[] = {
        LayoutPreset::Default,
        LayoutPreset::Debug,
        LayoutPreset::Monitor,
        LayoutPreset::BagReview,
        LayoutPreset::RViz,
        LayoutPreset::RVizPlot,
    };
    for (const auto p : kPresets)
    {
        const bool sel = (current_preset_ == p);
        if (ImGui::MenuItem(layout_preset_name(p), nullptr, sel))
            apply_layout_preset(p);
    }
#endif
}

void RosAppShell::draw_recent_sessions_menu()
{
#ifdef SPECTRA_USE_IMGUI
    if (!session_mgr_)
    {
        ImGui::TextDisabled("(no session manager)");
        return;
    }
    const auto recents = session_mgr_->load_recent();
    if (recents.empty())
    {
        ImGui::TextDisabled("(no recent sessions)");
        return;
    }
    for (size_t i = 0; i < recents.size() && i < 5; ++i)
    {
        const std::string& path  = recents[i].path;
        const auto         slash = path.rfind('/');
        const std::string  label = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        if (ImGui::MenuItem(label.c_str(), recents[i].node.c_str()))
        {
            const auto result = load_session(path);
            if (result.ok)
            {
                session_status_msg_   = "Loaded: " + label;
                session_status_timer_ = 3.0f;
            }
            else
            {
                session_status_msg_   = "Load failed: " + result.error;
                session_status_timer_ = 4.0f;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", path.c_str());
    }
#endif
}

}   // namespace spectra::adapters::ros2
