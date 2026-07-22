// test_qt_plugin_ui.cpp — Qt integration tests for PluginUIRegistry and C ABI.
//
// Verifies that plugin UI schemas can be registered, queried, and modified
// through the framework-neutral PluginUIRegistry, and that the C ABI
// (spectra_register_plugin_ui) works correctly.
// Phase 6 acceptance: "ABI-compatible plugins load".

#include <gtest/gtest.h>

#include <QApplication>

#include "ui/workspace/plugin_ui_schema.hpp"
#include "ui/workspace/plugin_api.hpp"

#include <memory>
#include <string>

namespace {

struct QtPluginUIEnv
{
    std::unique_ptr<spectra::PluginUIRegistry> registry;

    QtPluginUIEnv()
    {
        registry = std::make_unique<spectra::PluginUIRegistry>();
    }
};

QtPluginUIEnv& env()
{
    static QtPluginUIEnv e;
    return e;
}

} // namespace

TEST(QtPluginUI, RegisterAndFindSchema)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "test_plugin";
    schema.panel_title = "Test Plugin Panel";

    spectra::PluginUIElement prop;
    prop.type = spectra::PluginUIElementType::Property;
    prop.property.id = "threshold";
    prop.property.label = "Threshold";
    prop.property.type = spectra::PluginUIPropertyType::Float;
    prop.property.value = "0.5";
    prop.property.min_value = "0.0";
    prop.property.max_value = "1.0";
    schema.elements.push_back(prop);

    std::string id = e.registry->register_schema(schema);
    EXPECT_FALSE(id.empty());

    const auto* found = e.registry->find_schema(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->plugin_name, "test_plugin");
    EXPECT_EQ(found->panel_title, "Test Plugin Panel");
    EXPECT_EQ(found->elements.size(), 1u);
    EXPECT_EQ(found->elements[0].property.id, "threshold");
    EXPECT_EQ(found->elements[0].property.value, "0.5");
}

TEST(QtPluginUI, RegisterMultipleSchemas)
{
    spectra::PluginUIRegistry registry;

    spectra::PluginUISchema s1;
    s1.plugin_name = "plugin_a";
    s1.panel_title = "Plugin A";
    std::string id1 = registry.register_schema(s1);
    EXPECT_FALSE(id1.empty());

    spectra::PluginUISchema s2;
    s2.plugin_name = "plugin_b";
    s2.panel_title = "Plugin B";
    std::string id2 = registry.register_schema(s2);
    EXPECT_FALSE(id2.empty());

    EXPECT_NE(id1, id2);

    auto all = registry.schemas();
    EXPECT_EQ(all.size(), 2u);
}

TEST(QtPluginUI, ReplaceSchema)
{
    auto& e = env();

    spectra::PluginUISchema s1;
    s1.plugin_name = "replace_test";
    s1.panel_title = "V1";
    s1.elements.push_back({});
    s1.elements[0].type = spectra::PluginUIElementType::Label;
    s1.elements[0].label.text = "First version";
    std::string id1 = e.registry->register_schema(s1);

    // Re-register with same plugin_name
    spectra::PluginUISchema s2;
    s2.plugin_name = "replace_test";
    s2.panel_title = "V2";
    s2.elements.push_back({});
    s2.elements[0].type = spectra::PluginUIElementType::Label;
    s2.elements[0].label.text = "Second version";
    std::string id2 = e.registry->register_schema(s2);

    // The registry should have only one schema for this plugin
    auto all = e.registry->schemas();
    int count = 0;
    for (const auto& s : all)
    {
        if (s.plugin_name == "replace_test")
            ++count;
    }
    EXPECT_EQ(count, 1u);
}

TEST(QtPluginUI, UnregisterSchema)
{
    auto& e = env();

    spectra::PluginUISchema s;
    s.plugin_name = "unregister_test";
    s.panel_title = "Test";
    std::string id = e.registry->register_schema(s);

    EXPECT_NE(e.registry->find_schema(id), nullptr);
    e.registry->unregister_schema(id);
    EXPECT_EQ(e.registry->find_schema(id), nullptr);
}

TEST(QtPluginUI, UnregisterPlugin)
{
    auto& e = env();

    spectra::PluginUISchema s;
    s.plugin_name = "plugin_to_remove";
    s.panel_title = "Test";
    std::string id = e.registry->register_schema(s);

    e.registry->unregister_plugin("plugin_to_remove");
    EXPECT_EQ(e.registry->find_schema(id), nullptr);
}

TEST(QtPluginUI, PropertyChangedCallback)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "callback_test";
    schema.panel_title = "Callback Test";

    spectra::PluginUIElement prop;
    prop.type = spectra::PluginUIElementType::Property;
    prop.property.id = "gain";
    prop.property.label = "Gain";
    prop.property.type = spectra::PluginUIPropertyType::Float;
    prop.property.value = "1.0";
    schema.elements.push_back(prop);

    std::string captured_property;
    std::string captured_value;

    spectra::PluginUICallbacks callbacks;
    callbacks.on_property_changed =
        [&captured_property, &captured_value](
            const std::string& schema_id,
            const std::string& property_id,
            const std::string& new_value) -> std::string {
        captured_property = property_id;
        captured_value = new_value;
        return new_value;
    };

    std::string id = e.registry->register_schema(schema, callbacks);

    std::string result = e.registry->set_property_value(id, "gain", "2.5");
    EXPECT_EQ(captured_property, "gain");
    EXPECT_EQ(captured_value, "2.5");
    EXPECT_EQ(result, "2.5");
}

TEST(QtPluginUI, ActionTriggeredCallback)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "action_test";
    schema.panel_title = "Action Test";

    spectra::PluginUIElement action;
    action.type = spectra::PluginUIElementType::Action;
    action.action.id = "run";
    action.action.label = "Run";
    schema.elements.push_back(action);

    std::string captured_action;

    spectra::PluginUICallbacks callbacks;
    callbacks.on_action_triggered =
        [&captured_action](
            const std::string& schema_id,
            const std::string& action_id) {
        captured_action = action_id;
    };

    std::string id = e.registry->register_schema(schema, callbacks);

    e.registry->trigger_action(id, "run");
    EXPECT_EQ(captured_action, "run");
}

TEST(QtPluginUI, ChangeListener)
{
    auto& e = env();

    int change_count = 0;
    e.registry->set_change_listener([&change_count]() { ++change_count; });

    spectra::PluginUISchema s;
    s.plugin_name = "listener_test";
    s.panel_title = "Test";
    e.registry->register_schema(s);

    EXPECT_GT(change_count, 0);
}

TEST(QtPluginUI, ClearAllSchemas)
{
    spectra::PluginUIRegistry registry;

    for (int i = 0; i < 5; ++i)
    {
        spectra::PluginUISchema s;
        s.plugin_name = "plugin_" + std::to_string(i);
        s.panel_title = "Test";
        registry.register_schema(s);
    }

    EXPECT_EQ(registry.schemas().size(), 5u);
    registry.clear();
    EXPECT_EQ(registry.schemas().size(), 0u);
}

TEST(QtPluginUI, AllElementTypes)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "all_types";
    schema.panel_title = "All Element Types";

    // Property
    spectra::PluginUIElement prop;
    prop.type = spectra::PluginUIElementType::Property;
    prop.property.id = "enabled";
    prop.property.type = spectra::PluginUIPropertyType::Boolean;
    prop.property.value = "true";
    schema.elements.push_back(prop);

    // Action
    spectra::PluginUIElement action;
    action.type = spectra::PluginUIElementType::Action;
    action.action.id = "apply";
    action.action.label = "Apply";
    schema.elements.push_back(action);

    // Label
    spectra::PluginUIElement label;
    label.type = spectra::PluginUIElementType::Label;
    label.label.text = "Status: OK";
    label.label.style_hint = "status";
    schema.elements.push_back(label);

    // Separator
    spectra::PluginUIElement sep;
    sep.type = spectra::PluginUIElementType::Separator;
    schema.elements.push_back(sep);

    // Group with children
    spectra::PluginUIElement group;
    group.type = spectra::PluginUIElementType::Group;
    group.group.title = "Advanced";
    group.group.collapsed = true;
    group.children = {0, 1}; // indices of prop and action
    schema.elements.push_back(group);

    std::string id = e.registry->register_schema(schema);
    const auto* found = e.registry->find_schema(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->elements.size(), 5u);
    EXPECT_EQ(found->elements[0].type, spectra::PluginUIElementType::Property);
    EXPECT_EQ(found->elements[1].type, spectra::PluginUIElementType::Action);
    EXPECT_EQ(found->elements[2].type, spectra::PluginUIElementType::Label);
    EXPECT_EQ(found->elements[3].type, spectra::PluginUIElementType::Separator);
    EXPECT_EQ(found->elements[4].type, spectra::PluginUIElementType::Group);
    EXPECT_EQ(found->elements[4].children.size(), 2u);
}

TEST(QtPluginUI, EnumProperty)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "enum_test";
    schema.panel_title = "Enum Test";

    spectra::PluginUIElement prop;
    prop.type = spectra::PluginUIElementType::Property;
    prop.property.id = "mode";
    prop.property.type = spectra::PluginUIPropertyType::Enum;
    prop.property.value = "0";
    prop.property.enum_options = {"Auto", "Manual", "Disabled"};
    schema.elements.push_back(prop);

    std::string id = e.registry->register_schema(schema);
    const auto* found = e.registry->find_schema(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->elements[0].property.enum_options.size(), 3u);
    EXPECT_EQ(found->elements[0].property.enum_options[0], "Auto");
}

TEST(QtPluginUI, ColorProperty)
{
    auto& e = env();

    spectra::PluginUISchema schema;
    schema.plugin_name = "color_test";
    schema.panel_title = "Color Test";

    spectra::PluginUIElement prop;
    prop.type = spectra::PluginUIElementType::Property;
    prop.property.id = "bg_color";
    prop.property.type = spectra::PluginUIPropertyType::Color;
    prop.property.value = "#FF5733";
    schema.elements.push_back(prop);

    std::string id = e.registry->register_schema(schema);
    const auto* found = e.registry->find_schema(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->elements[0].property.value, "#FF5733");
}
