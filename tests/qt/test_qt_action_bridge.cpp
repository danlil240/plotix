// test_qt_action_bridge.cpp — Qt integration tests for QtActionBridge.
//
// Verifies that QActions are created from CommandRegistry entries, that
// triggering a QAction executes the underlying command, and that
// refresh() updates label/enabled state.

#include <gtest/gtest.h>

#include <QApplication>
#include <QAction>
#include <QKeySequence>

#include "adapters/qt/qt_action_bridge.hpp"
#include "ui/commands/command_registry.hpp"

#include <memory>

namespace {

// We need a QApplication for Qt widgets.  Use offscreen platform.
struct QtTestEnv
{
    std::unique_ptr<QApplication> app;
    std::unique_ptr<spectra::CommandRegistry> registry;

    QtTestEnv()
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        // Force offscreen platform for headless testing
        qputenv("QT_QPA_PLATFORM", "offscreen");
        app = std::make_unique<QApplication>(argc, argv);
        registry = std::make_unique<spectra::CommandRegistry>();
    }
};

QtTestEnv& env()
{
    static QtTestEnv e;
    return e;
}

} // namespace

TEST(QtActionBridge, RebuildCreatesActions)
{
    auto& reg = *env().registry;
    reg.register_command("test.cmd1", "Test Command 1", []{}, "Ctrl+T", "Test");
    reg.register_command("test.cmd2", "Test Command 2", []{}, "", "Test");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    EXPECT_NE(bridge.action_for("test.cmd1"), nullptr);
    EXPECT_NE(bridge.action_for("test.cmd2"), nullptr);
    EXPECT_EQ(bridge.action_for("nonexistent"), nullptr);
}

TEST(QtActionBridge, ActionMetadataMirrorsCommand)
{
    auto& reg = *env().registry;
    reg.register_command("view.reset", "Reset View", []{}, "Ctrl+R", "View");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("view.reset");
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->text().toStdString(), "Reset View");
    EXPECT_EQ(action->shortcut(), QKeySequence("Ctrl+R"));
    EXPECT_TRUE(action->isEnabled());
    EXPECT_EQ(action->objectName().toStdString(), "action_view.reset");
}

TEST(QtActionBridge, TriggerExecutesCommand)
{
    auto& reg = *env().registry;
    bool executed = false;
    reg.register_command("test.execute", "Execute Me", [&] { executed = true; }, "", "Test");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.execute");
    ASSERT_NE(action, nullptr);
    action->trigger();
    EXPECT_TRUE(executed);
}

TEST(QtActionBridge, RefreshUpdatesEnabledState)
{
    auto& reg = *env().registry;
    reg.register_command("test.refresh", "Refresh Me", []{}, "", "Test");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.refresh");
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(action->isEnabled());

    reg.set_enabled("test.refresh", false);
    bridge.refresh();
    EXPECT_FALSE(action->isEnabled());

    reg.set_enabled("test.refresh", true);
    bridge.refresh();
    EXPECT_TRUE(action->isEnabled());
}

TEST(QtActionBridge, RefreshUpdatesLabel)
{
    auto& reg = *env().registry;
    reg.register_command("test.relabel", "Old Label", []{}, "", "Test");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.relabel");
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->text().toStdString(), "Old Label");

    // Re-register with new label
    reg.register_command("test.relabel", "New Label", []{}, "", "Test");
    bridge.refresh();
    EXPECT_EQ(action->text().toStdString(), "New Label");
}

TEST(QtActionBridge, ActionsByCategory)
{
    // Use a fresh registry to avoid interference from other tests
    spectra::CommandRegistry reg;
    reg.register_command("cat1.cmd1", "C1-1", []{}, "", "Category1");
    reg.register_command("cat1.cmd2", "C1-2", []{}, "", "Category1");
    reg.register_command("cat2.cmd1", "C2-1", []{}, "", "Category2");

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto cats = bridge.categories();
    EXPECT_EQ(cats.size(), 2u);

    auto by_cat = bridge.actions_by_category();
    EXPECT_EQ(by_cat.size(), 2u);

    for (const auto& ca : by_cat)
    {
        if (ca.category == "Category1")
            EXPECT_EQ(ca.actions.size(), 2u);
        else if (ca.category == "Category2")
            EXPECT_EQ(ca.actions.size(), 1u);
    }
}

TEST(QtActionBridge, DisabledCommandActionDisabled)
{
    auto& reg = *env().registry;
    reg.register_command("test.disabled", "Disabled", []{}, "", "Test");
    reg.set_enabled("test.disabled", false);

    spectra::adapters::qt::QtActionBridge bridge(reg);
    bridge.rebuild();

    auto* action = bridge.action_for("test.disabled");
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->isEnabled());
}
