#include <gtest/gtest.h>

#include "ui/docking/split_view.hpp"

using namespace spectra;

// ─── SplitPane Construction ──────────────────────────────────────────────────

TEST(SplitPaneConstruction, DefaultIsLeaf)
{
    SplitPane pane(0);
    EXPECT_TRUE(pane.is_leaf());
    EXPECT_FALSE(pane.is_split());
    EXPECT_EQ(pane.figure_index(), 0u);
    EXPECT_EQ(pane.count_nodes(), 1u);
    EXPECT_EQ(pane.count_leaves(), 1u);
    EXPECT_EQ(pane.parent(), nullptr);
}

TEST(SplitPaneConstruction, UniqueIds)
{
    SplitPane a(0);
    SplitPane b(1);
    EXPECT_NE(a.id(), b.id());
}

TEST(SplitPaneConstruction, FigureIndexAssignment)
{
    SplitPane pane(42);
    EXPECT_EQ(pane.figure_index(), 42u);
    pane.set_figure_index(7);
    EXPECT_EQ(pane.figure_index(), 7u);
}

// ─── SplitPane Split ─────────────────────────────────────────────────────────

TEST(SplitPaneSplit, HorizontalSplit)
{
    SplitPane pane(0);
    auto*     second = pane.split(SplitDirection::Horizontal, 1, 0.5f);

    EXPECT_NE(second, nullptr);
    EXPECT_TRUE(pane.is_split());
    EXPECT_FALSE(pane.is_leaf());
    EXPECT_EQ(pane.count_nodes(), 3u);
    EXPECT_EQ(pane.count_leaves(), 2u);

    EXPECT_TRUE(pane.first()->is_leaf());
    EXPECT_TRUE(pane.second()->is_leaf());
    EXPECT_EQ(pane.first()->figure_index(), 0u);
    EXPECT_EQ(pane.second()->figure_index(), 1u);
    EXPECT_EQ(pane.split_direction(), SplitDirection::Horizontal);
    EXPECT_FLOAT_EQ(pane.split_ratio(), 0.5f);
}

TEST(SplitPaneSplit, VerticalSplit)
{
    SplitPane pane(0);
    auto*     second = pane.split(SplitDirection::Vertical, 1, 0.3f);

    EXPECT_NE(second, nullptr);
    EXPECT_EQ(pane.split_direction(), SplitDirection::Vertical);
    EXPECT_FLOAT_EQ(pane.split_ratio(), 0.3f);
}

TEST(SplitPaneSplit, CannotSplitTwice)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1);
    auto* result = pane.split(SplitDirection::Vertical, 2);
    EXPECT_EQ(result, nullptr);
}

TEST(SplitPaneSplit, RatioClampedToRange)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1, 0.01f);
    EXPECT_GE(pane.split_ratio(), SplitPane::MIN_RATIO);

    SplitPane pane2(0);
    pane2.split(SplitDirection::Horizontal, 1, 0.99f);
    EXPECT_LE(pane2.split_ratio(), SplitPane::MAX_RATIO);
}

TEST(SplitPaneSplit, ParentPointers)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1);

    EXPECT_EQ(pane.first()->parent(), &pane);
    EXPECT_EQ(pane.second()->parent(), &pane);
    EXPECT_EQ(pane.parent(), nullptr);
}

TEST(SplitPaneSplit, NestedSplit)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);
    auto* nested = root.first()->split(SplitDirection::Vertical, 2);

    EXPECT_NE(nested, nullptr);
    EXPECT_EQ(root.count_nodes(), 5u);
    EXPECT_EQ(root.count_leaves(), 3u);
}

// ─── SplitPane Unsplit ───────────────────────────────────────────────────────

TEST(SplitPaneUnsplit, UnsplitKeepFirst)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1);
    EXPECT_TRUE(pane.unsplit(true));
    EXPECT_TRUE(pane.is_leaf());
    EXPECT_EQ(pane.figure_index(), 0u);
}

TEST(SplitPaneUnsplit, UnsplitKeepSecond)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1);
    EXPECT_TRUE(pane.unsplit(false));
    EXPECT_TRUE(pane.is_leaf());
    EXPECT_EQ(pane.figure_index(), 1u);
}

TEST(SplitPaneUnsplit, CannotUnsplitLeaf)
{
    SplitPane pane(0);
    EXPECT_FALSE(pane.unsplit());
}

TEST(SplitPaneUnsplit, UnsplitNestedKeepsSubtree)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);
    root.first()->split(SplitDirection::Vertical, 2);

    // Root has 3 leaves: [2, 0] on left, [1] on right
    EXPECT_EQ(root.count_leaves(), 3u);

    // Unsplit root keeping first child (which is itself split)
    EXPECT_TRUE(root.unsplit(true));

    // Root should now be the internal node from the first child
    EXPECT_TRUE(root.is_split());
    EXPECT_EQ(root.count_leaves(), 2u);
    EXPECT_EQ(root.split_direction(), SplitDirection::Vertical);
}

// ─── SplitPane Layout ────────────────────────────────────────────────────────

TEST(SplitPaneLayout, LeafBounds)
{
    SplitPane pane(0);
    Rect      bounds{100.0f, 50.0f, 800.0f, 600.0f};
    pane.compute_layout(bounds);

    EXPECT_FLOAT_EQ(pane.bounds().x, 100.0f);
    EXPECT_FLOAT_EQ(pane.bounds().y, 50.0f);
    EXPECT_FLOAT_EQ(pane.bounds().w, 800.0f);
    EXPECT_FLOAT_EQ(pane.bounds().h, 600.0f);
}

TEST(SplitPaneLayout, HorizontalSplitLayout)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1, 0.5f);
    pane.compute_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    auto* first  = pane.first();
    auto* second = pane.second();

    // First child should be on the left
    EXPECT_FLOAT_EQ(first->bounds().x, 0.0f);
    EXPECT_GT(first->bounds().w, 0.0f);

    // Second child should be on the right
    EXPECT_GT(second->bounds().x, first->bounds().x);
    EXPECT_GT(second->bounds().w, 0.0f);

    // Heights should be the same
    EXPECT_FLOAT_EQ(first->bounds().h, 600.0f);
    EXPECT_FLOAT_EQ(second->bounds().h, 600.0f);

    // Total width should account for splitter
    float total = first->bounds().w + second->bounds().w + SplitPane::SPLITTER_WIDTH;
    EXPECT_NEAR(total, 1000.0f, 1.0f);
}

TEST(SplitPaneLayout, VerticalSplitLayout)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Vertical, 1, 0.5f);
    pane.compute_layout(Rect{0.0f, 0.0f, 800.0f, 1000.0f});

    auto* first  = pane.first();
    auto* second = pane.second();

    // First child should be on top
    EXPECT_FLOAT_EQ(first->bounds().y, 0.0f);
    EXPECT_GT(first->bounds().h, 0.0f);

    // Second child should be below
    EXPECT_GT(second->bounds().y, first->bounds().y);
    EXPECT_GT(second->bounds().h, 0.0f);

    // Widths should be the same
    EXPECT_FLOAT_EQ(first->bounds().w, 800.0f);
    EXPECT_FLOAT_EQ(second->bounds().w, 800.0f);
}

TEST(SplitPaneLayout, SplitterRect)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1, 0.5f);
    pane.compute_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    Rect sr = pane.splitter_rect();
    EXPECT_GT(sr.w, 0.0f);
    EXPECT_FLOAT_EQ(sr.h, 600.0f);
    EXPECT_NEAR(sr.x + sr.w * 0.5f, 500.0f, 1.0f);
}

TEST(SplitPaneLayout, LeafSplitterRectIsZero)
{
    SplitPane pane(0);
    Rect      sr = pane.splitter_rect();
    EXPECT_FLOAT_EQ(sr.w, 0.0f);
    EXPECT_FLOAT_EQ(sr.h, 0.0f);
}

// ─── SplitPane Traversal ─────────────────────────────────────────────────────

TEST(SplitPaneTraversal, CollectLeaves)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);
    root.first()->split(SplitDirection::Vertical, 2);

    std::vector<SplitPane*> leaves;
    root.collect_leaves(leaves);
    EXPECT_EQ(leaves.size(), 3u);
}

TEST(SplitPaneTraversal, FindByFigure)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);

    auto* found = root.find_by_figure(1);
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->figure_index(), 1u);

    EXPECT_EQ(root.find_by_figure(99), nullptr);
}

TEST(SplitPaneTraversal, FindAtPoint)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1, 0.5f);
    root.compute_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    // Point in left half
    auto* left = root.find_at_point(100.0f, 300.0f);
    EXPECT_NE(left, nullptr);
    EXPECT_EQ(left->figure_index(), 0u);

    // Point in right half
    auto* right = root.find_at_point(800.0f, 300.0f);
    EXPECT_NE(right, nullptr);
    EXPECT_EQ(right->figure_index(), 1u);

    // Point outside
    EXPECT_EQ(root.find_at_point(-10.0f, 300.0f), nullptr);
}

TEST(SplitPaneTraversal, FindById)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);
    auto id = root.second()->id();

    auto* found = root.find_by_id(id);
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->id(), id);
}

// ─── SplitPane Serialization ─────────────────────────────────────────────────

TEST(SplitPaneSerialization, LeafRoundTrip)
{
    SplitPane   pane(42);
    std::string data     = pane.serialize();
    auto        restored = SplitPane::deserialize(data);

    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->is_leaf());
    EXPECT_EQ(restored->figure_index(), 42u);
}

TEST(SplitPaneSerialization, SplitRoundTrip)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1, 0.6f);
    std::string data     = root.serialize();
    auto        restored = SplitPane::deserialize(data);

    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->is_split());
    EXPECT_EQ(restored->split_direction(), SplitDirection::Horizontal);
    EXPECT_NEAR(restored->split_ratio(), 0.6f, 0.01f);
    EXPECT_EQ(restored->first()->figure_index(), 0u);
    EXPECT_EQ(restored->second()->figure_index(), 1u);
}

TEST(SplitPaneSerialization, NestedRoundTrip)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);
    root.first()->split(SplitDirection::Vertical, 2);

    std::string data     = root.serialize();
    auto        restored = SplitPane::deserialize(data);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->count_leaves(), 3u);
    EXPECT_TRUE(restored->first()->is_split());
    EXPECT_TRUE(restored->second()->is_leaf());
}

TEST(SplitPaneSerialization, EmptyStringReturnsNull)
{
    EXPECT_EQ(SplitPane::deserialize(""), nullptr);
    EXPECT_EQ(SplitPane::deserialize("invalid"), nullptr);
}

// ─── SplitViewManager Construction ───────────────────────────────────────────

TEST(SplitViewManager, DefaultState)
{
    SplitViewManager mgr;
    EXPECT_FALSE(mgr.is_split());
    EXPECT_EQ(mgr.pane_count(), 1u);
    EXPECT_EQ(mgr.active_figure_index(), 0u);
    EXPECT_NE(mgr.root(), nullptr);
}

// ─── SplitViewManager Split Operations ───────────────────────────────────────

TEST(SplitViewManagerSplit, SplitActive)
{
    SplitViewManager mgr;
    auto*            pane = mgr.split_active(SplitDirection::Horizontal, 1);

    EXPECT_NE(pane, nullptr);
    EXPECT_TRUE(mgr.is_split());
    EXPECT_EQ(mgr.pane_count(), 2u);
}

TEST(SplitViewManagerSplit, SplitByFigure)
{
    SplitViewManager mgr;
    auto*            pane = mgr.split_pane(0, SplitDirection::Vertical, 1, 0.4f);

    EXPECT_NE(pane, nullptr);
    EXPECT_EQ(mgr.pane_count(), 2u);
}

TEST(SplitViewManagerSplit, SplitNonExistentFigure)
{
    SplitViewManager mgr;
    auto*            pane = mgr.split_pane(99, SplitDirection::Horizontal, 1);
    EXPECT_EQ(pane, nullptr);
}

TEST(SplitViewManagerSplit, MaxPanesEnforced)
{
    SplitViewManager mgr;
    // Split until we hit the max
    for (size_t i = 1; i < SplitViewManager::MAX_PANES; ++i)
    {
        mgr.split_pane(i - 1, SplitDirection::Horizontal, i);
    }
    EXPECT_EQ(mgr.pane_count(), SplitViewManager::MAX_PANES);

    // One more should fail
    auto* result = mgr.split_pane(0, SplitDirection::Horizontal, 100);
    EXPECT_EQ(result, nullptr);
}

TEST(SplitViewManagerSplit, MultipleSplits)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.split_pane(1, SplitDirection::Vertical, 2);

    EXPECT_EQ(mgr.pane_count(), 3u);
}

// ─── SplitViewManager Close ──────────────────────────────────────────────────

TEST(SplitViewManagerClose, ClosePane)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    EXPECT_EQ(mgr.pane_count(), 2u);

    EXPECT_TRUE(mgr.close_pane(1));
    EXPECT_EQ(mgr.pane_count(), 1u);
    EXPECT_FALSE(mgr.is_split());
}

TEST(SplitViewManagerClose, CannotCloseLastPane)
{
    SplitViewManager mgr;
    EXPECT_FALSE(mgr.close_pane(0));
}

TEST(SplitViewManagerClose, CloseNonExistent)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    EXPECT_FALSE(mgr.close_pane(99));
}

TEST(SplitViewManagerClose, CloseUpdatesActive)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.set_active_figure_index(1);

    mgr.close_pane(1);
    // Active should switch to the remaining pane
    EXPECT_EQ(mgr.active_figure_index(), 0u);
}

TEST(SplitViewManagerClose, UnsplitAll)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.split_pane(1, SplitDirection::Vertical, 2);
    EXPECT_EQ(mgr.pane_count(), 3u);

    mgr.unsplit_all();
    EXPECT_EQ(mgr.pane_count(), 1u);
    EXPECT_FALSE(mgr.is_split());
}

// ─── SplitViewManager Active Pane ────────────────────────────────────────────

TEST(SplitViewManagerActive, SetActive)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);

    mgr.set_active_figure_index(1);
    EXPECT_EQ(mgr.active_figure_index(), 1u);

    auto* active = mgr.active_pane();
    EXPECT_NE(active, nullptr);
    EXPECT_EQ(active->figure_index(), 1u);
}

TEST(SplitViewManagerActive, ActiveCallback)
{
    SplitViewManager mgr;
    size_t           callback_idx = SIZE_MAX;
    mgr.set_on_active_changed([&](size_t idx) { callback_idx = idx; });

    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.set_active_figure_index(1);
    EXPECT_EQ(callback_idx, 1u);
}

// ─── SplitViewManager Layout ─────────────────────────────────────────────────

TEST(SplitViewManagerLayout, UpdateLayout)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.5f);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    auto panes = mgr.all_panes();
    EXPECT_EQ(panes.size(), 2u);

    // Both panes should have valid bounds
    for (auto* p : panes)
    {
        EXPECT_GT(p->bounds().w, 0.0f);
        EXPECT_GT(p->bounds().h, 0.0f);
    }
}

TEST(SplitViewManagerLayout, PaneAtPoint)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.5f);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    auto* left = mgr.pane_at_point(100.0f, 300.0f);
    EXPECT_NE(left, nullptr);
    EXPECT_EQ(left->figure_index(), 0u);

    auto* right = mgr.pane_at_point(800.0f, 300.0f);
    EXPECT_NE(right, nullptr);
    EXPECT_EQ(right->figure_index(), 1u);
}

TEST(SplitViewManagerLayout, PaneForFigure)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);

    auto* pane = mgr.pane_for_figure(1);
    EXPECT_NE(pane, nullptr);
    EXPECT_EQ(pane->figure_index(), 1u);

    EXPECT_EQ(mgr.pane_for_figure(99), nullptr);
}

TEST(SplitViewManagerLayout, IsFigureVisible)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);

    EXPECT_TRUE(mgr.is_figure_visible(0));
    EXPECT_TRUE(mgr.is_figure_visible(1));
    EXPECT_FALSE(mgr.is_figure_visible(2));
}

// ─── SplitViewManager Splitter Interaction ───────────────────────────────────

TEST(SplitViewManagerSplitter, HitTest)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.5f);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    // Center of the split should hit the splitter
    auto* splitter = mgr.splitter_at_point(500.0f, 300.0f);
    EXPECT_NE(splitter, nullptr);

    // Far left should not hit
    EXPECT_EQ(mgr.splitter_at_point(100.0f, 300.0f), nullptr);
}

TEST(SplitViewManagerSplitter, DragSplitter)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.5f);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    auto* splitter = mgr.splitter_at_point(500.0f, 300.0f);
    ASSERT_NE(splitter, nullptr);

    float original_ratio = splitter->split_ratio();
    mgr.begin_splitter_drag(splitter, 500.0f);
    EXPECT_TRUE(mgr.is_dragging_splitter());

    mgr.update_splitter_drag(600.0f);   // Drag right
    EXPECT_GT(splitter->split_ratio(), original_ratio);

    mgr.end_splitter_drag();
    EXPECT_FALSE(mgr.is_dragging_splitter());
}

TEST(SplitViewManagerSplitter, DragRespectsMinSize)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.5f);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    auto* splitter = mgr.splitter_at_point(500.0f, 300.0f);
    ASSERT_NE(splitter, nullptr);

    mgr.begin_splitter_drag(splitter, 500.0f);
    mgr.update_splitter_drag(950.0f);   // Drag far right

    EXPECT_LE(splitter->split_ratio(), SplitPane::MAX_RATIO);
    mgr.end_splitter_drag();
}

// ─── SplitViewManager Serialization ──────────────────────────────────────────

TEST(SplitViewManagerSerialization, RoundTrip)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1, 0.6f);
    mgr.set_active_figure_index(1);
    mgr.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});

    std::string data = mgr.serialize();
    EXPECT_FALSE(data.empty());

    SplitViewManager mgr2;
    mgr2.update_layout(Rect{0.0f, 0.0f, 1000.0f, 600.0f});
    EXPECT_TRUE(mgr2.deserialize(data));

    EXPECT_TRUE(mgr2.is_split());
    EXPECT_EQ(mgr2.pane_count(), 2u);
    EXPECT_EQ(mgr2.active_figure_index(), 1u);
}

TEST(SplitViewManagerSerialization, EmptyStringFails)
{
    SplitViewManager mgr;
    EXPECT_FALSE(mgr.deserialize(""));
}

TEST(SplitViewManagerSerialization, PreservesPaneTabsActiveTabAndRemapsIds)
{
    SplitViewManager manager;
    auto*            root = manager.root();
    ASSERT_NE(root, nullptr);
    root->set_figure_index(10);
    root->add_figure(11);
    root->add_figure(12);
    root->set_active_local_index(1);
    ASSERT_NE(root->split(SplitDirection::Horizontal, 20, 0.4f), nullptr);
    root->second()->add_figure(21);
    root->second()->set_active_local_index(0);
    manager.set_active_figure_index(11);

    SplitViewManager restored;
    ASSERT_TRUE(restored.deserialize(manager.serialize()));
    ASSERT_TRUE(restored.root()->is_split());
    EXPECT_EQ(restored.root()->first()->figure_indices(), (std::vector<FigureId>{10, 11, 12}));
    EXPECT_EQ(restored.root()->first()->active_local_index(), 1u);
    EXPECT_EQ(restored.root()->first()->figure_index(), 11u);
    EXPECT_EQ(restored.root()->second()->figure_indices(), (std::vector<FigureId>{20, 21}));

    restored.remap_figure_ids([](FigureId old_id) { return old_id + 100; });
    EXPECT_EQ(restored.active_figure_index(), 111u);
    EXPECT_EQ(restored.root()->first()->figure_indices(), (std::vector<FigureId>{110, 111, 112}));
    EXPECT_EQ(restored.root()->first()->figure_index(), 111u);
    EXPECT_EQ(restored.root()->second()->figure_indices(), (std::vector<FigureId>{120, 121}));
}

TEST(SplitViewManagerSerialization, MalformedNumbersFailWithoutReplacingTree)
{
    SplitViewManager manager;
    manager.root()->set_figure_index(55);
    manager.set_active_figure_index(55);

    EXPECT_FALSE(manager.deserialize(R"({"active":oops,"root":{"leaf":true,"figure":1}})"));
    EXPECT_EQ(manager.active_figure_index(), 55u);
    EXPECT_EQ(manager.root()->figure_index(), 55u);

    EXPECT_FALSE(manager.deserialize(
        R"({"active":1,"root":{"leaf":false,"dir":"h","ratio":oops,"first":{"leaf":true,"figure":1},"second":{"leaf":true,"figure":2}}})"));
    EXPECT_EQ(manager.active_figure_index(), 55u);
    EXPECT_EQ(manager.root()->figure_index(), 55u);
}

// ─── SplitViewManager Callbacks ──────────────────────────────────────────────

TEST(SplitViewManagerCallbacks, OnSplit)
{
    SplitViewManager mgr;
    SplitPane*       split_pane = nullptr;
    mgr.set_on_split([&](SplitPane* p) { split_pane = p; });

    mgr.split_active(SplitDirection::Horizontal, 1);
    EXPECT_NE(split_pane, nullptr);
}

TEST(SplitViewManagerCallbacks, OnUnsplit)
{
    SplitViewManager mgr;
    bool             unsplit_called = false;
    mgr.set_on_unsplit([&](SplitPane*) { unsplit_called = true; });

    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.close_pane(1);
    EXPECT_TRUE(unsplit_called);
}

// ─── Edge Cases ──────────────────────────────────────────────────────────────

TEST(SplitViewEdgeCases, SplitAndCloseRepeatedly)
{
    SplitViewManager mgr;
    for (int i = 0; i < 10; ++i)
    {
        mgr.split_active(SplitDirection::Horizontal, static_cast<size_t>(i + 1));
        EXPECT_TRUE(mgr.is_split());
        mgr.close_pane(static_cast<size_t>(i + 1));
        EXPECT_FALSE(mgr.is_split());
    }
}

TEST(SplitViewEdgeCases, ZeroSizeBounds)
{
    SplitViewManager mgr;
    mgr.split_active(SplitDirection::Horizontal, 1);
    mgr.update_layout(Rect{0.0f, 0.0f, 0.0f, 0.0f});
    // Should not crash
    EXPECT_EQ(mgr.pane_count(), 2u);
}

TEST(SplitViewEdgeCases, SetSplitRatio)
{
    SplitPane pane(0);
    pane.split(SplitDirection::Horizontal, 1, 0.5f);
    pane.set_split_ratio(0.7f);
    EXPECT_FLOAT_EQ(pane.split_ratio(), 0.7f);

    // Clamped
    pane.set_split_ratio(0.0f);
    EXPECT_GE(pane.split_ratio(), SplitPane::MIN_RATIO);
    pane.set_split_ratio(1.0f);
    EXPECT_LE(pane.split_ratio(), SplitPane::MAX_RATIO);
}

TEST(SplitViewEdgeCases, ConstCollectLeaves)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);

    const SplitPane&              croot = root;
    std::vector<const SplitPane*> leaves;
    croot.collect_leaves(leaves);
    EXPECT_EQ(leaves.size(), 2u);
}

TEST(SplitViewEdgeCases, ConstFindByFigure)
{
    SplitPane root(0);
    root.split(SplitDirection::Horizontal, 1);

    const SplitPane& croot = root;
    const SplitPane* found = croot.find_by_figure(1);
    EXPECT_NE(found, nullptr);
}
