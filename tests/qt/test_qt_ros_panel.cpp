// Test for RosPanelManager — Qt panel composition for ROS2/PX4 displays.
//
// Validates:
//   - Add/remove displays
//   - Enable/disable displays
//   - Layout serialization/deserialization
//   - Display list refresh
//
// Requires Qt6 and ROS2 adapter. Runs headless via QT_QPA_PLATFORM=offscreen.

#include <gtest/gtest.h>

#include <QApplication>
#include <QMainWindow>

#include "adapters/qt/ros2/ros_panel_manager.hpp"
#include "adapters/ros2/display/display_registry.hpp"
#include "adapters/ros2/display/display_plugin.hpp"

#include <memory>

using namespace spectra::adapters;

// Stub display plugin for testing.
class StubDisplay : public ros2::DisplayPlugin
{
   public:
    std::string              type_id() const override { return "stub"; }
    std::string              display_name() const override { return "Stub Display"; }
    std::string              icon() const override { return "stub"; }
    void                     draw_inspector_ui() override {}
    std::vector<std::string> compatible_message_types() const override { return {}; }
    std::string              serialize_config_blob() const override { return "{}"; }
    void                     deserialize_config_blob(const std::string&) override {}
};

static int   argc    = 1;
static char* argv0[] = {(char*)"test_ros_panel", nullptr};

class RosPanelManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Use existing QApplication if already created
        app_ = qApp;
        if (!app_)
        {
            app_ = new QApplication(argc, argv0);
        }

        registry_ = std::make_unique<ros2::DisplayRegistry>();
        registry_->register_display<StubDisplay>();

        main_window_ = std::make_unique<QMainWindow>();
        manager_ =
            std::make_unique<qt::RosPanelManager>(main_window_.get(), registry_.get(), nullptr);
    }

    void TearDown() override
    {
        manager_.reset();
        main_window_.reset();
        registry_.reset();
    }

    QApplication*                          app_ = nullptr;
    std::unique_ptr<ros2::DisplayRegistry> registry_;
    std::unique_ptr<QMainWindow>           main_window_;
    std::unique_ptr<qt::RosPanelManager>   manager_;
};

TEST_F(RosPanelManagerTest, AddDisplay)
{
    std::string id = manager_->add_display("stub");
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(manager_->displays().size(), 1u);
    EXPECT_EQ(manager_->displays()[0].type_id, "stub");
}

TEST_F(RosPanelManagerTest, RemoveDisplay)
{
    std::string id = manager_->add_display("stub");
    ASSERT_FALSE(id.empty());

    manager_->remove_display(id);
    EXPECT_EQ(manager_->displays().size(), 0u);
}

TEST_F(RosPanelManagerTest, EnableDisableDisplay)
{
    std::string id = manager_->add_display("stub");
    ASSERT_FALSE(id.empty());

    auto* entry = manager_->find_display(id);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->enabled);

    manager_->set_display_enabled(id, false);
    entry = manager_->find_display(id);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->enabled);

    manager_->set_display_enabled(id, true);
    entry = manager_->find_display(id);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->enabled);
}

TEST_F(RosPanelManagerTest, FindDisplay)
{
    std::string id1 = manager_->add_display("stub");
    std::string id2 = manager_->add_display("stub");

    EXPECT_NE(id1, id2);
    EXPECT_NE(manager_->find_display(id1), nullptr);
    EXPECT_NE(manager_->find_display(id2), nullptr);
    EXPECT_EQ(manager_->find_display("nonexistent"), nullptr);
}

TEST_F(RosPanelManagerTest, SerializeLayout)
{
    manager_->add_display("stub");
    manager_->add_display("stub");

    std::string json = manager_->serialize_layout();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("stub"), std::string::npos);
    EXPECT_NE(json.find("display_1"), std::string::npos);
    EXPECT_NE(json.find("display_2"), std::string::npos);
}

TEST_F(RosPanelManagerTest, DeserializeLayout)
{
    std::string json =
        R"([{"id":"test1","type_id":"stub","enabled":true,"topic":"/test","config":"{}"}])";
    bool result = manager_->deserialize_layout(json);
    EXPECT_TRUE(result);
    EXPECT_GE(manager_->displays().size(), 1u);
}

TEST_F(RosPanelManagerTest, MultipleDisplays)
{
    for (int i = 0; i < 5; ++i)
        manager_->add_display("stub");

    EXPECT_EQ(manager_->displays().size(), 5u);

    // Remove middle one
    std::string mid_id = manager_->displays()[2].id;
    manager_->remove_display(mid_id);
    EXPECT_EQ(manager_->displays().size(), 4u);
    EXPECT_EQ(manager_->find_display(mid_id), nullptr);
}
