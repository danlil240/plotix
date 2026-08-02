// Phase 5 robot model display tests — URDF collision shapes into scene entities.

#include <gtest/gtest.h>

#include <cmath>

#include "display/robot_model_display.hpp"
#include "scene/scene_manager.hpp"

using namespace spectra::adapters::ros2;

TEST(RobotModelDisplay, ConfigBlobRoundTrip)
{
    RobotModelDisplay display;
    display.deserialize_config_blob("parameter=my_robot_description;show_collision_shapes=0");

    const std::string blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("parameter=my_robot_description"), std::string::npos);
    EXPECT_NE(blob.find("show_collision_shapes=0"), std::string::npos);
}

TEST(RobotModelDisplay, CollisionEntitiesExposeJointMetadataForPicking)
{
    RobotModelDisplay display;
    DisplayContext    context;
    SceneManager      scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(R"(
<robot name="demo">
  <link name="base"/>
  <link name="arm">
    <collision>
      <geometry><box size="1 1 1"/></geometry>
    </collision>
  </link>
  <joint name="arm_joint" type="revolute">
    <parent link="base"/>
    <child link="arm"/>
    <axis xyz="0 0 1"/>
  </joint>
</robot>)");
    display.set_joint_positions_for_test({{"arm_joint", 0.42}});
    display.on_update(0.016f);
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const auto& entity        = scene.entities().front();
    auto        find_property = [&](const char* key) -> std::string
    {
        for (const auto& property : entity.properties)
        {
            if (property.key == key)
                return property.value;
        }
        return {};
    };
    EXPECT_EQ(find_property("link"), "arm");
    EXPECT_EQ(find_property("joint"), "arm_joint");
    EXPECT_EQ(find_property("parent_link"), "base");
    EXPECT_EQ(find_property("joint_type"), "revolute");
    EXPECT_FALSE(find_property("joint_position").empty());
    EXPECT_NEAR(std::stod(find_property("joint_position")), 0.42, 1e-6);
}

TEST(RobotModelDisplay, ParsesUrdfAndSubmitsCollisionEntities)
{
    RobotModelDisplay display;
    DisplayContext    context;
    SceneManager      scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(R"(
<robot name="demo">
  <link name="base">
    <collision name="base_box">
      <origin xyz="1 0 0"/>
      <geometry><box size="1 2 3"/></geometry>
    </collision>
    <collision>
      <geometry><sphere radius="0.5"/></geometry>
    </collision>
  </link>
  <link name="arm">
    <collision>
      <geometry><cylinder radius="0.25" length="2.0"/></geometry>
    </collision>
  </link>
</robot>)");

    display.on_update(0.016f);
    ASSERT_EQ(display.collision_count(), 3u);
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 3u);
    EXPECT_EQ(scene.entities()[0].type, "robot_box");
    EXPECT_EQ(scene.entities()[0].frame_id, "base");
    EXPECT_EQ(scene.entities()[0].display_name, "Robot Model");
    EXPECT_DOUBLE_EQ(scene.entities()[0].transform.translation.x, 1.0);
    EXPECT_EQ(scene.entities()[1].type, "robot_sphere");
    EXPECT_EQ(scene.entities()[2].type, "robot_cylinder");
}

TEST(RobotModelDisplay, InvalidUrdfSetsErrorState)
{
    RobotModelDisplay display;
    DisplayContext    context;
    SceneManager      scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test("<robot><link></robot>");
    display.on_update(0.016f);
    EXPECT_EQ(display.status(), DisplayStatus::Error);
    EXPECT_TRUE(display.robot_description().links.empty());
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

// --- Forward kinematics tests ---

static const char* FK_URDF = R"(
<robot name="fk_test">
  <link name="base_link">
    <collision><geometry><sphere radius="0.1"/></geometry></collision>
  </link>
  <joint name="joint1" type="revolute">
    <parent link="base_link"/>
    <child link="link1"/>
    <origin xyz="0 0 1" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="10" velocity="1"/>
  </joint>
  <link name="link1">
    <collision><geometry><box size="0.2 0.2 1.0"/></geometry></collision>
  </link>
  <joint name="joint2" type="revolute">
    <parent link="link1"/>
    <child link="link2"/>
    <origin xyz="0 0 1" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-3.14" upper="3.14" effort="10" velocity="1"/>
  </joint>
  <link name="link2">
    <collision><geometry><box size="0.2 0.2 1.0"/></geometry></collision>
  </link>
</robot>)";

TEST(RobotModelDisplay, FkIdentityWhenZeroJoints)
{
    RobotModelDisplay display;
    DisplayContext    context;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(FK_URDF);
    display.on_update(0.016f);

    // With zero joint positions, link1 should be at (0,0,1) from origin.
    spectra::Transform link1_tf = display.link_transform("link1");
    EXPECT_NEAR(link1_tf.translation.x, 0.0, 1e-6);
    EXPECT_NEAR(link1_tf.translation.y, 0.0, 1e-6);
    EXPECT_NEAR(link1_tf.translation.z, 1.0, 1e-6);

    // link2 at (0,0,2)
    spectra::Transform link2_tf = display.link_transform("link2");
    EXPECT_NEAR(link2_tf.translation.x, 0.0, 1e-6);
    EXPECT_NEAR(link2_tf.translation.y, 0.0, 1e-6);
    EXPECT_NEAR(link2_tf.translation.z, 2.0, 1e-6);
}

TEST(RobotModelDisplay, FkRevoluteRotatesChild)
{
    RobotModelDisplay display;
    DisplayContext    context;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(FK_URDF);
    display.on_update(0.016f);

    // Rotate joint1 by 90 degrees around Z axis.
    display.set_joint_positions_for_test({{"joint1", M_PI / 2.0}});
    display.on_update(0.016f);

    // link1 is at joint1 origin (0,0,1) — rotation doesn't move the child link origin.
    spectra::Transform link1_tf = display.link_transform("link1");
    EXPECT_NEAR(link1_tf.translation.z, 1.0, 1e-6);

    // link2: joint2 origin is (0,0,1) relative to link1, but link1 has 90-deg Z rotation,
    // so the second offset becomes (-1, 0, 0) rotated = (0,1,0) relative after rotation.
    // Actually: joint2 origin (0,0,1) in link1 frame, link1 rotated 90 around Z at (0,0,1).
    // World coord of link2: (0,0,1) + Rz(pi/2) * (0,0,1) = (0,0,1) + (0,0,1) = (0,0,2).
    // Z rotation doesn't affect Z-axis translations.
    spectra::Transform link2_tf = display.link_transform("link2");
    EXPECT_NEAR(link2_tf.translation.z, 2.0, 1e-6);
}

TEST(RobotModelDisplay, FkPrismaticTranslatesChild)
{
    const char* prismatic_urdf = R"(
<robot name="prismatic_test">
  <link name="base">
    <collision><geometry><sphere radius="0.1"/></geometry></collision>
  </link>
  <joint name="slide" type="prismatic">
    <parent link="base"/>
    <child link="slider"/>
    <origin xyz="0 0 0" rpy="0 0 0"/>
    <axis xyz="1 0 0"/>
    <limit lower="0" upper="2" effort="10" velocity="1"/>
  </joint>
  <link name="slider">
    <collision><geometry><box size="0.5 0.5 0.5"/></geometry></collision>
  </link>
</robot>)";

    RobotModelDisplay display;
    DisplayContext    context;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(prismatic_urdf);
    display.on_update(0.016f);

    display.set_joint_positions_for_test({{"slide", 1.5}});
    display.on_update(0.016f);

    spectra::Transform slider_tf = display.link_transform("slider");
    EXPECT_NEAR(slider_tf.translation.x, 1.5, 1e-6);
    EXPECT_NEAR(slider_tf.translation.y, 0.0, 1e-6);
    EXPECT_NEAR(slider_tf.translation.z, 0.0, 1e-6);
}

TEST(RobotModelDisplay, FkSubmitRenderablesUsesTransform)
{
    RobotModelDisplay display;
    DisplayContext    context;
    SceneManager      scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(FK_URDF);
    display.on_update(0.016f);
    display.submit_renderables(scene);

    // base_link collision at origin, link1 collision at (0,0,1), link2 at (0,0,2).
    ASSERT_EQ(scene.entity_count(), 3u);

    // base_link entity: identity FK * identity collision origin
    EXPECT_NEAR(scene.entities()[0].transform.translation.z, 0.0, 1e-6);
    // link1 entity: FK (0,0,1) * identity collision origin
    EXPECT_NEAR(scene.entities()[1].transform.translation.z, 1.0, 1e-6);
    // link2 entity: FK (0,0,2) * identity collision origin
    EXPECT_NEAR(scene.entities()[2].transform.translation.z, 2.0, 1e-6);
}

TEST(RobotModelDisplay, ConfigBlobRoundTripWithJointTopic)
{
    RobotModelDisplay display;
    display.deserialize_config_blob(
        "parameter=my_robot;show_collision_shapes=1;joint_topic=/joint_states");

    const std::string blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("joint_topic=/joint_states"), std::string::npos);
}
