#include <gtest/gtest.h>

#include "scene/scene_manager.hpp"

using namespace spectra::adapters::ros2;

TEST(SceneManager, PickReturnsNearestHitAlongRay)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type      = "marker",
        .label     = "Near",
        .transform = spectra::Transform{{0.0, 0.0, -2.0}, spectra::quat_identity()},
        .scale     = {1.0, 1.0, 1.0},
    });
    scene.add_entity(SceneEntity{
        .type      = "marker",
        .label     = "Far",
        .transform = spectra::Transform{{0.0, 0.0, -5.0}, spectra::quat_identity()},
        .scale     = {1.0, 1.0, 1.0},
    });

    const auto picked = scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickUsesPolylineBounds)
{
    SceneManager scene;
    SceneEntity  path;
    path.type                  = "path";
    path.label                 = "/plan";
    path.transform.translation = {0.0, 0.0, -3.0};
    path.polyline = ScenePolyline{{spectra::vec3{-1.0, 0.0, 0.0}, spectra::vec3{1.0, 0.0, 0.0}}};
    scene.add_entity(std::move(path));

    const auto picked = scene.pick(spectra::Ray{{0.5, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickMissReturnsNullopt)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type      = "marker",
        .label     = "OffAxis",
        .transform = spectra::Transform{{3.0, 0.0, -2.0}, spectra::quat_identity()},
        .scale     = {0.5, 0.5, 0.5},
    });

    EXPECT_FALSE(scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}}).has_value());
}

TEST(SceneManager, SelectionPersistsByStableIdentityAcrossRebuild)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{
        .type         = "marker",
        .label        = "Marker",
        .display_name = "Markers",
        .topic        = "/markers_a",
    });
    scene.add_entity(SceneEntity{
        .type         = "marker",
        .label        = "Marker",
        .display_name = "Markers",
        .topic        = "/markers_b",
    });
    scene.set_selected_index(1u);

    scene.clear();
    scene.add_entity(SceneEntity{
        .type         = "marker",
        .label        = "Marker",
        .display_name = "Markers",
        .topic        = "/markers_a",
    });
    scene.add_entity(SceneEntity{
        .type         = "marker",
        .label        = "Marker",
        .display_name = "Markers",
        .topic        = "/markers_b",
    });

    ASSERT_TRUE(scene.selected_index().has_value());
    EXPECT_EQ(*scene.selected_index(), 1u);
    ASSERT_NE(scene.selected_entity(), nullptr);
    EXPECT_EQ(scene.selected_entity()->topic, "/markers_b");
}

// --- Phase 6 expanded coverage ---

TEST(SceneManager, StartsEmpty)
{
    SceneManager scene;
    EXPECT_EQ(scene.entity_count(), 0u);
    EXPECT_TRUE(scene.entities().empty());
    EXPECT_FALSE(scene.selected_index().has_value());
    EXPECT_EQ(scene.selected_entity(), nullptr);
}

TEST(SceneManager, AddEntityReturnsIndex)
{
    SceneManager scene;
    const size_t idx0 = scene.add_entity(SceneEntity{.type = "grid", .label = "G0"});
    const size_t idx1 = scene.add_entity(SceneEntity{.type = "marker", .label = "M0"});
    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(scene.entity_count(), 2u);
}

TEST(SceneManager, ClearSelectionDropsSelection)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "G"});
    scene.set_selected_index(0u);
    ASSERT_TRUE(scene.selected_index().has_value());

    scene.clear_selection();
    EXPECT_FALSE(scene.selected_index().has_value());
    EXPECT_EQ(scene.selected_entity(), nullptr);
}

TEST(SceneManager, SetSelectedIndexNulloptClearsSelection)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "G"});
    scene.set_selected_index(0u);
    scene.set_selected_index(std::nullopt);
    EXPECT_FALSE(scene.selected_index().has_value());
}

TEST(SceneManager, SelectedEntityNullWhenEmpty)
{
    SceneManager scene;
    EXPECT_EQ(scene.selected_entity(), nullptr);
    scene.set_selected_index(0u);
    EXPECT_EQ(scene.selected_entity(), nullptr);
}

TEST(SceneManager, PickArrowEntityByBounds)
{
    SceneManager scene;
    SceneEntity  arrow_ent;
    arrow_ent.type                  = "pose";
    arrow_ent.label                 = "PoseArrow";
    arrow_ent.transform.translation = {0.0, 0.0, -3.0};
    arrow_ent.arrow                 = SceneArrow{
                        .origin       = {0.0, 0.0, 0.0},
                        .direction    = {1.0, 0.0, 0.0},
                        .shaft_length = 1.0,
                        .head_length  = 0.2,
                        .head_width   = 0.15,
    };
    scene.add_entity(std::move(arrow_ent));

    const auto picked = scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickPointSetEntityByBounds)
{
    SceneManager scene;
    SceneEntity  ps_ent;
    ps_ent.type                  = "pointcloud";
    ps_ent.label                 = "PC";
    ps_ent.transform.translation = {0.0, 0.0, -4.0};
    ps_ent.point_set             = ScenePointSet{
                    .points =
                        {
                {spectra::vec3{-0.5, 0.0, 0.0}, 0xFFFFFFFFu},
                {spectra::vec3{0.5, 0.0, 0.0}, 0xFFFFFFFFu},
            },
    };
    scene.add_entity(std::move(ps_ent));

    const auto picked = scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, PickBillboardEntityByBounds)
{
    SceneManager scene;
    SceneEntity  bb_ent;
    bb_ent.type                  = "image";
    bb_ent.label                 = "Cam";
    bb_ent.transform.translation = {0.0, 0.0, -2.0};
    bb_ent.billboard             = SceneBillboard{.width = 2.0, .height = 2.0};
    scene.add_entity(std::move(bb_ent));

    const auto picked = scene.pick(spectra::Ray{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 0u);
}

TEST(SceneManager, EntitiesPreserveInsertionOrder)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "First"});
    scene.add_entity(SceneEntity{.type = "marker", .label = "Second"});
    scene.add_entity(SceneEntity{.type = "path", .label = "Third"});

    ASSERT_EQ(scene.entity_count(), 3u);
    EXPECT_EQ(scene.entities()[0].label, "First");
    EXPECT_EQ(scene.entities()[1].label, "Second");
    EXPECT_EQ(scene.entities()[2].label, "Third");
}

TEST(SceneManager, ClearDoesNotAffectNewEntities)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "Old"});
    scene.clear();
    scene.add_entity(SceneEntity{.type = "marker", .label = "New"});

    EXPECT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities()[0].label, "New");
}

TEST(SceneManager, SelectionOutOfBoundsGivesNullEntity)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "G"});
    scene.set_selected_index(999u);
    // selected_entity should return nullptr for out of bounds
    EXPECT_EQ(scene.selected_entity(), nullptr);
}

TEST(SceneManager, EntityPropertiesPreserved)
{
    SceneManager scene;
    SceneEntity  ent;
    ent.type  = "marker";
    ent.label = "M";
    ent.properties.push_back({"key1", "val1"});
    ent.properties.push_back({"key2", "val2"});
    scene.add_entity(std::move(ent));

    const auto& e = scene.entities()[0];
    ASSERT_EQ(e.properties.size(), 2u);
    EXPECT_EQ(e.properties[0].key, "key1");
    EXPECT_EQ(e.properties[1].value, "val2");
}
