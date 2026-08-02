#include "display/robot_model_display.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

#include "scene/scene_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{
const UrdfJoint* find_parent_joint(const RobotDescription& robot, const std::string& link_name)
{
    for (const auto& joint : robot.joints)
    {
        if (joint.child_link == link_name)
            return &joint;
    }
    return nullptr;
}

std::string joint_type_name(UrdfJointType type)
{
    switch (type)
    {
        case UrdfJointType::Revolute:
            return "revolute";
        case UrdfJointType::Continuous:
            return "continuous";
        case UrdfJointType::Prismatic:
            return "prismatic";
        case UrdfJointType::Fixed:
            return "fixed";
        case UrdfJointType::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::string geometry_name(UrdfGeometryType type)
{
    switch (type)
    {
        case UrdfGeometryType::Box:
            return "box";
        case UrdfGeometryType::Cylinder:
            return "cylinder";
        case UrdfGeometryType::Sphere:
            return "sphere";
        case UrdfGeometryType::Unsupported:
            return "unsupported";
    }
    return "unsupported";
}
}   // namespace

RobotModelDisplay::RobotModelDisplay()
{
    parameter_name_.copy(parameter_input_.data(), parameter_input_.size() - 1);
    parameter_input_[std::min(parameter_name_.size(), parameter_input_.size() - 1)] = '\0';
    joint_state_topic_.copy(joint_topic_input_.data(), joint_topic_input_.size() - 1);
    joint_topic_input_[std::min(joint_state_topic_.size(), joint_topic_input_.size() - 1)] = '\0';
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void RobotModelDisplay::on_enable(const DisplayContext& context)
{
#ifdef SPECTRA_USE_ROS2
    node_ = context.node;

    if (node_ && !joint_state_topic_.empty())
    {
        joint_sub_ = node_->create_subscription<sensor_msgs::msg::JointState>(
            joint_state_topic_,
            rclcpp::QoS(rclcpp::KeepLast(10)),
            [this](const sensor_msgs::msg::JointState::SharedPtr msg)
            {
                auto                        frame = adapt_joint_state(*msg);
                std::lock_guard<std::mutex> lock(joint_mutex_);
                for (const auto& [name, pos] : frame.positions)
                    joint_positions_[name] = pos;
                fk_dirty_ = true;
            });
    }
#endif
    status_      = DisplayStatus::Warn;
    status_text_ = "Waiting for robot description";
}

void RobotModelDisplay::on_disable()
{
#ifdef SPECTRA_USE_ROS2
    joint_sub_.reset();
    node_.reset();
#endif
    status_      = DisplayStatus::Disabled;
    status_text_ = "Disabled";
}

void RobotModelDisplay::on_destroy()
{
    on_disable();
    robot_ = {};
    robot_description_xml_.clear();
    pending_robot_description_xml_.clear();
    warnings_.clear();
    last_parse_failed_ = false;
    joint_positions_.clear();
    link_transforms_.clear();
    root_link_.clear();
}

void RobotModelDisplay::on_update(float)
{
    if (!pending_robot_description_xml_.empty()
        && pending_robot_description_xml_ != robot_description_xml_)
    {
        apply_robot_description_xml(pending_robot_description_xml_);
    }

#ifdef SPECTRA_USE_ROS2
    if (node_ && !parameter_name_.empty())
    {
        std::string xml;
        if (node_->get_parameter(parameter_name_, xml) && !xml.empty()
            && xml != robot_description_xml_)
        {
            apply_robot_description_xml(xml);
        }
    }
#endif

    if (fk_dirty_ && !robot_.links.empty())
    {
        compute_fk();
        fk_dirty_ = false;
    }

    if (status_ == DisplayStatus::Disabled)
        return;

    if (robot_.links.empty())
    {
        if (!last_parse_failed_)
        {
            status_      = DisplayStatus::Warn;
            status_text_ = "Waiting for robot description";
        }
        return;
    }

    status_      = warnings_.empty() ? DisplayStatus::Ok : DisplayStatus::Warn;
    status_text_ = std::to_string(robot_.links.size()) + " links, "
                   + std::to_string(robot_.joints.size()) + " joints, "
                   + std::to_string(collision_count()) + " collision shapes";
}

void RobotModelDisplay::submit_renderables(SceneManager& scene)
{
    if (!enabled_)
        return;

    if (show_collision_shapes_)
    {
        for (const auto& link : robot_.links)
        {
            for (const auto& collision : link.collisions)
            {
                SceneEntity entity;
                entity.type = "robot_" + geometry_name(collision.geometry.type);
                entity.label =
                    collision.name.empty() ? link.name : link.name + "/" + collision.name;
                entity.display_name = display_name();
                entity.frame_id     = link.name;
                entity.properties.push_back({"link", link.name});
                entity.properties.push_back({"geometry", geometry_name(collision.geometry.type)});
                entity.properties.push_back({"parameter", parameter_name_});
                if (const UrdfJoint* joint = find_parent_joint(robot_, link.name))
                {
                    entity.properties.push_back({"joint", joint->name});
                    entity.properties.push_back({"parent_link", joint->parent_link});
                    entity.properties.push_back({"joint_type", joint_type_name(joint->type)});
                    double joint_position = 0.0;
                    {
                        std::lock_guard<std::mutex> lock(joint_mutex_);
                        const auto                  it = joint_positions_.find(joint->name);
                        if (it != joint_positions_.end())
                            joint_position = it->second;
                    }
                    entity.properties.push_back({"joint_position", std::to_string(joint_position)});
                }

                // Compose FK link transform with collision origin.
                spectra::Transform link_tf = link_transform(link.name);
                entity.transform           = link_tf.compose(collision.origin);

                switch (collision.geometry.type)
                {
                    case UrdfGeometryType::Box:
                        entity.scale = collision.geometry.box_size;
                        entity.properties.push_back({
                            "size",
                            std::to_string(collision.geometry.box_size.x) + ", "
                                + std::to_string(collision.geometry.box_size.y) + ", "
                                + std::to_string(collision.geometry.box_size.z),
                        });
                        break;
                    case UrdfGeometryType::Cylinder:
                        entity.scale = {
                            collision.geometry.radius * 2.0,
                            collision.geometry.radius * 2.0,
                            collision.geometry.length,
                        };
                        entity.properties.push_back(
                            {"radius", std::to_string(collision.geometry.radius)});
                        entity.properties.push_back(
                            {"length", std::to_string(collision.geometry.length)});
                        break;
                    case UrdfGeometryType::Sphere:
                        entity.scale = {
                            collision.geometry.radius * 2.0,
                            collision.geometry.radius * 2.0,
                            collision.geometry.radius * 2.0,
                        };
                        entity.properties.push_back(
                            {"radius", std::to_string(collision.geometry.radius)});
                        break;
                    case UrdfGeometryType::Unsupported:
                        continue;
                }

                scene.add_entity(std::move(entity));
            }
        }
    }

    // Frame axes: render an RGB axis triad at each link origin.
    if (show_frame_axes_)
    {
        for (const auto& link : robot_.links)
        {
            SceneEntity entity;
            entity.type         = "tf_frame";
            entity.label        = link.name;
            entity.display_name = display_name();
            entity.frame_id     = link.name;
            entity.transform    = link_transform(link.name);
            entity.scale        = {0.15, 0.15, 0.15};
            entity.properties.push_back({"source", "robot_model"});
            scene.add_entity(std::move(entity));
        }
    }

    // Joint axes: render the joint rotation/translation axis as a line.
    if (show_joint_axes_)
    {
        for (const auto& joint : robot_.joints)
        {
            if (joint.type == UrdfJointType::Fixed)
                continue;

            spectra::Transform child_tf = link_transform(joint.child_link);

            SceneEntity entity;
            entity.type         = "marker";
            entity.label        = joint.name + " axis";
            entity.display_name = display_name();
            entity.frame_id     = joint.child_link;
            entity.transform    = child_tf;
            entity.scale        = {1.0, 1.0, 1.0};
            entity.properties.push_back({"primitive", "line_list"});
            entity.properties.push_back({"color", "1.0, 0.85, 0.0, 0.8"});
            entity.properties.push_back({"line_width", "3.0"});
            entity.properties.push_back({"joint", joint.name});

            const double  axis_len   = 0.2;
            spectra::vec3 axis_dir   = {joint.axis.x, joint.axis.y, joint.axis.z};
            spectra::vec3 axis_start = axis_dir * (-axis_len * 0.5);
            spectra::vec3 axis_end   = axis_dir * (axis_len * 0.5);

            ScenePolyline polyline;
            polyline.points.push_back(axis_start);
            polyline.points.push_back(axis_end);
            entity.polyline = std::move(polyline);
            scene.add_entity(std::move(entity));
        }
    }
}

void RobotModelDisplay::draw_inspector_ui()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;

    if (ImGui::InputText("Robot Param", parameter_input_.data(), parameter_input_.size()))
        parameter_name_ = parameter_input_.data();
    if (ImGui::InputText("Joint Topic", joint_topic_input_.data(), joint_topic_input_.size()))
        joint_state_topic_ = joint_topic_input_.data();
    ImGui::Checkbox("Show Collision Shapes", &show_collision_shapes_);
    ImGui::Checkbox("Show Frame Axes", &show_frame_axes_);
    ImGui::Checkbox("Show Joint Axes", &show_joint_axes_);
    ImGui::Text("Links: %zu", robot_.links.size());
    ImGui::Text("Joints: %zu", robot_.joints.size());
    ImGui::Text("Collision Shapes: %zu", collision_count());
    {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        ImGui::Text("Active Joints: %zu", joint_positions_.size());
    }
    if (!warnings_.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("Warnings:");
        for (const auto& warning : warnings_)
            ImGui::TextWrapped("- %s", warning.c_str());
    }
    ImGui::TextWrapped("Status: %s", status_text_.c_str());
#endif
}

std::string RobotModelDisplay::serialize_config_blob() const
{
    return std::format(
        "parameter={};show_collision_shapes={};joint_topic={};show_frame_axes={};show_joint_axes="
        "{}",
        parameter_name_,
        show_collision_shapes_ ? 1 : 0,
        joint_state_topic_,
        show_frame_axes_ ? 1 : 0,
        show_joint_axes_ ? 1 : 0);
}

void RobotModelDisplay::deserialize_config_blob(const std::string& blob)
{
    if (blob.empty())
        return;

    char parameter_name[256]   = {};
    int  show_collision_shapes = show_collision_shapes_ ? 1 : 0;
    char joint_topic[256]      = {};
    int  show_frame_axes       = show_frame_axes_ ? 1 : 0;
    int  show_joint_axes       = show_joint_axes_ ? 1 : 0;
    if (std::sscanf(blob.c_str(),
                    "parameter=%255[^;];show_collision_shapes=%d;joint_topic=%255[^;];show_frame_"
                    "axes=%d;show_joint_axes=%d",
                    parameter_name,
                    &show_collision_shapes,
                    joint_topic,
                    &show_frame_axes,
                    &show_joint_axes)
        >= 1)
    {
        parameter_name_ = parameter_name;
        parameter_name_.copy(parameter_input_.data(), parameter_input_.size() - 1);
        parameter_input_[std::min(parameter_name_.size(), parameter_input_.size() - 1)] = '\0';
        show_collision_shapes_ = show_collision_shapes != 0;
        joint_state_topic_     = joint_topic;
        joint_state_topic_.copy(joint_topic_input_.data(), joint_topic_input_.size() - 1);
        joint_topic_input_[std::min(joint_state_topic_.size(), joint_topic_input_.size() - 1)] =
            '\0';
        show_frame_axes_ = show_frame_axes != 0;
        show_joint_axes_ = show_joint_axes != 0;
    }
}

void RobotModelDisplay::set_robot_description_xml_for_test(const std::string& xml)
{
    pending_robot_description_xml_ = xml;
}

size_t RobotModelDisplay::collision_count() const
{
    size_t total = 0;
    for (const auto& link : robot_.links)
        total += link.collisions.size();
    return total;
}

void RobotModelDisplay::apply_robot_description_xml(const std::string& xml)
{
    const UrdfParseResult result = UrdfParser::parse_string(xml);
    if (!result.ok)
    {
        robot_ = {};
        warnings_.clear();
        last_parse_failed_ = true;
        status_            = DisplayStatus::Error;
        status_text_       = result.error;
        return;
    }

    robot_                 = result.robot;
    warnings_              = result.warnings;
    robot_description_xml_ = xml;
    pending_robot_description_xml_.clear();
    last_parse_failed_ = false;
    build_kinematic_chain();
    fk_dirty_ = true;
}

void RobotModelDisplay::build_kinematic_chain()
{
    link_transforms_.clear();
    root_link_.clear();

    if (robot_.links.empty())
        return;

    // Find the root link: a link that is never a child in any joint.
    std::unordered_map<std::string, bool> is_child;
    for (const auto& joint : robot_.joints)
        is_child[joint.child_link] = true;

    for (const auto& link : robot_.links)
    {
        if (!is_child.contains(link.name))
        {
            root_link_ = link.name;
            break;
        }
    }

    if (root_link_.empty())
        root_link_ = robot_.links[0].name;
}

void RobotModelDisplay::compute_fk()
{
    link_transforms_.clear();

    if (robot_.links.empty() || root_link_.empty())
        return;

    // Copy joint positions under lock for thread safety.
    std::unordered_map<std::string, double> positions;
    {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        positions = joint_positions_;
    }

    // Build a parent→children map from the URDF joints.
    std::unordered_map<std::string, std::vector<const UrdfJoint*>> children_of;
    for (const auto& joint : robot_.joints)
        children_of[joint.parent_link].push_back(&joint);

    // BFS from root, computing world transform for each link.
    link_transforms_[root_link_] = {};   // identity

    struct QueueEntry
    {
        std::string link_name;
    };
    std::vector<QueueEntry> queue;
    queue.push_back({root_link_});

    for (size_t i = 0; i < queue.size(); ++i)
    {
        const std::string&        parent_link = queue[i].link_name;
        const spectra::Transform& parent_tf   = link_transforms_[parent_link];

        auto it = children_of.find(parent_link);
        if (it == children_of.end())
            continue;

        for (const UrdfJoint* joint : it->second)
        {
            double pos = 0.0;
            auto   jit = positions.find(joint->name);
            if (jit != positions.end())
                pos = jit->second;

            spectra::Transform jtf              = joint_transform(*joint, pos);
            link_transforms_[joint->child_link] = parent_tf.compose(jtf);
            queue.push_back({joint->child_link});
        }
    }
}

spectra::Transform RobotModelDisplay::joint_transform(const UrdfJoint& joint, double position) const
{
    // Joint transform = joint origin * rotation/translation from joint position.
    spectra::Transform result = joint.origin;

    switch (joint.type)
    {
        case UrdfJointType::Revolute:
        case UrdfJointType::Continuous:
        {
            spectra::quat rot = spectra::quat_from_axis_angle(joint.axis, position);
            result.rotation   = spectra::quat_mul(result.rotation, rot);
            break;
        }
        case UrdfJointType::Prismatic:
        {
            spectra::vec3 offset = {
                joint.axis.x * position,
                joint.axis.y * position,
                joint.axis.z * position,
            };
            result.translation = result.transform_point(offset);
            break;
        }
        case UrdfJointType::Fixed:
        case UrdfJointType::Unknown:
            break;
    }

    return result;
}

spectra::Transform RobotModelDisplay::link_transform(const std::string& link_name) const
{
    auto it = link_transforms_.find(link_name);
    if (it != link_transforms_.end())
        return it->second;
    return {};
}

void RobotModelDisplay::set_joint_positions_for_test(
    const std::unordered_map<std::string, double>& positions)
{
    std::lock_guard<std::mutex> lock(joint_mutex_);
    joint_positions_ = positions;
    fk_dirty_        = true;
}

}   // namespace spectra::adapters::ros2
