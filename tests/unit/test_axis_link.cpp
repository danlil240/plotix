#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <thread>

#include "ui/data/axis_link.hpp"
#include "ui/input/input.hpp"

using namespace spectra;

// ─── Helper ──────────────────────────────────────────────────────────────────

// Create a figure with N subplots (1 row, N cols) and set explicit limits
static std::unique_ptr<Figure> make_figure(int n_axes)
{
    auto fig = std::make_unique<Figure>();
    for (int i = 0; i < n_axes; ++i)
    {
        auto& ax = fig->subplot(1, n_axes, i + 1);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.0f, 1.0f);
    }
    return fig;
}

static Axes& ax(Figure& fig, int idx)
{
    return *fig.axes_mut()[static_cast<size_t>(idx)];
}

// ─── LinkAxis enum ───────────────────────────────────────────────────────────

TEST(LinkAxisEnum, BitwiseOr)
{
    auto both = LinkAxis::X | LinkAxis::Y;
    EXPECT_EQ(static_cast<uint8_t>(both), 0x03);
    EXPECT_EQ(both, LinkAxis::Both);
}

TEST(LinkAxisEnum, BitwiseAnd)
{
    EXPECT_TRUE(has_flag(LinkAxis::Both, LinkAxis::X));
    EXPECT_TRUE(has_flag(LinkAxis::Both, LinkAxis::Y));
    EXPECT_TRUE(has_flag(LinkAxis::X, LinkAxis::X));
    EXPECT_FALSE(has_flag(LinkAxis::X, LinkAxis::Y));
    EXPECT_FALSE(has_flag(LinkAxis::Y, LinkAxis::X));
}

// ─── Construction ────────────────────────────────────────────────────────────

TEST(AxisLinkConstruction, DefaultEmpty)
{
    AxisLinkManager mgr;
    EXPECT_EQ(mgr.group_count(), 0u);
}

TEST(AxisLinkConstruction, CreateGroup)
{
    AxisLinkManager mgr;
    auto            id = mgr.create_group("X Link", LinkAxis::X);
    EXPECT_GT(id, 0u);
    EXPECT_EQ(mgr.group_count(), 1u);
    auto* g = mgr.group(id);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "X Link");
    EXPECT_EQ(g->axis, LinkAxis::X);
    EXPECT_TRUE(g->members.empty());
}

TEST(AxisLinkConstruction, MultipleGroups)
{
    AxisLinkManager mgr;
    auto            id1 = mgr.create_group("G1", LinkAxis::X);
    auto            id2 = mgr.create_group("G2", LinkAxis::Y);
    auto            id3 = mgr.create_group("G3", LinkAxis::Both);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_EQ(mgr.group_count(), 3u);
}

// ─── Membership ──────────────────────────────────────────────────────────────

TEST(AxisLinkMembership, AddToGroup)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));
    mgr.add_to_group(id, &ax(*fig, 1));

    auto* g = mgr.group(id);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->members.size(), 2u);
}

TEST(AxisLinkMembership, NoDuplicates)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));
    mgr.add_to_group(id, &ax(*fig, 0));   // duplicate
    EXPECT_EQ(mgr.group(id)->members.size(), 1u);
}

TEST(AxisLinkMembership, RemoveFromGroup)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));
    mgr.add_to_group(id, &ax(*fig, 1));
    mgr.add_to_group(id, &ax(*fig, 2));

    mgr.remove_from_group(id, &ax(*fig, 1));
    auto* g = mgr.group(id);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->members.size(), 2u);
    EXPECT_FALSE(g->contains(&ax(*fig, 1)));
}

TEST(AxisLinkMembership, RemoveFromAll)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id1 = mgr.create_group("G1", LinkAxis::X);
    auto            id2 = mgr.create_group("G2", LinkAxis::Y);
    mgr.add_to_group(id1, &ax(*fig, 0));
    mgr.add_to_group(id1, &ax(*fig, 1));
    mgr.add_to_group(id2, &ax(*fig, 0));
    mgr.add_to_group(id2, &ax(*fig, 2));

    mgr.remove_from_all(&ax(*fig, 0));
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
    EXPECT_EQ(mgr.group(id1)->members.size(), 1u);
    EXPECT_EQ(mgr.group(id2)->members.size(), 1u);
}

TEST(AxisLinkMembership, RemoveGroupCleansUp)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));
    mgr.add_to_group(id, &ax(*fig, 1));

    mgr.remove_group(id);
    EXPECT_EQ(mgr.group_count(), 0u);
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
}

TEST(AxisLinkMembership, EmptyGroupAutoRemoved)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));

    mgr.remove_from_group(id, &ax(*fig, 0));
    EXPECT_EQ(mgr.group_count(), 0u);   // Empty group auto-removed
}

TEST(AxisLinkMembership, AddNullIgnored)
{
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, nullptr);
    EXPECT_EQ(mgr.group(id)->members.size(), 0u);
}

TEST(AxisLinkMembership, AddToNonexistentGroup)
{
    auto            fig = make_figure(1);
    AxisLinkManager mgr;
    mgr.add_to_group(999, &ax(*fig, 0));   // No crash
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
}

// ─── Convenience link() ──────────────────────────────────────────────────────

TEST(AxisLinkConvenience, LinkTwoAxes)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    EXPECT_GT(id, 0u);
    EXPECT_EQ(mgr.group_count(), 1u);
    EXPECT_TRUE(mgr.is_linked(&ax(*fig, 0)));
    EXPECT_TRUE(mgr.is_linked(&ax(*fig, 1)));
}

TEST(AxisLinkConvenience, LinkAlreadyLinked)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id1 = mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    auto            id2 = mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    EXPECT_EQ(id1, id2);   // Same group reused
    EXPECT_EQ(mgr.group_count(), 1u);
}

TEST(AxisLinkConvenience, LinkThirdToExistingGroup)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id1 = mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    auto            id2 = mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::X);
    EXPECT_EQ(id1, id2);   // ax0 already in group, ax2 joins
    EXPECT_EQ(mgr.group_count(), 1u);
    EXPECT_EQ(mgr.group(id1)->members.size(), 3u);
}

TEST(AxisLinkConvenience, LinkSameAxesReturnsZero)
{
    auto            fig = make_figure(1);
    AxisLinkManager mgr;
    auto            id = mgr.link(&ax(*fig, 0), &ax(*fig, 0), LinkAxis::X);
    EXPECT_EQ(id, 0u);
}

TEST(AxisLinkConvenience, LinkNullReturnsZero)
{
    auto            fig = make_figure(1);
    AxisLinkManager mgr;
    EXPECT_EQ(mgr.link(nullptr, &ax(*fig, 0), LinkAxis::X), 0u);
    EXPECT_EQ(mgr.link(&ax(*fig, 0), nullptr, LinkAxis::X), 0u);
}

TEST(AxisLinkConvenience, UnlinkRemovesFromAll)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::Y);

    mgr.unlink(&ax(*fig, 0));
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
}

// ─── Queries ─────────────────────────────────────────────────────────────────

TEST(AxisLinkQueries, GroupsFor)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id1 = mgr.create_group("G1", LinkAxis::X);
    auto            id2 = mgr.create_group("G2", LinkAxis::Y);
    mgr.add_to_group(id1, &ax(*fig, 0));
    mgr.add_to_group(id2, &ax(*fig, 0));
    mgr.add_to_group(id1, &ax(*fig, 1));

    auto groups = mgr.groups_for(&ax(*fig, 0));
    EXPECT_EQ(groups.size(), 2u);
}

TEST(AxisLinkQueries, LinkedPeers)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::Y);

    auto peers = mgr.linked_peers(&ax(*fig, 0));
    EXPECT_EQ(peers.size(), 2u);
}

TEST(AxisLinkQueries, LinkedPeersNoDuplicates)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id1 = mgr.create_group("G1", LinkAxis::X);
    auto            id2 = mgr.create_group("G2", LinkAxis::Y);
    // ax0 and ax1 in both groups
    mgr.add_to_group(id1, &ax(*fig, 0));
    mgr.add_to_group(id1, &ax(*fig, 1));
    mgr.add_to_group(id2, &ax(*fig, 0));
    mgr.add_to_group(id2, &ax(*fig, 1));

    auto peers = mgr.linked_peers(&ax(*fig, 0));
    EXPECT_EQ(peers.size(), 1u);   // ax1 appears once despite being in 2 groups
}

TEST(AxisLinkQueries, IsLinkedFalseForUnlinked)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
}

TEST(AxisLinkQueries, IsLinkedFalseForSoleGroupMember)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("G1", LinkAxis::X);
    mgr.add_to_group(id, &ax(*fig, 0));
    // Only 1 member — not meaningfully "linked"
    EXPECT_FALSE(mgr.is_linked(&ax(*fig, 0)));
}

TEST(AxisLinkQueries, GroupReturnsNullForInvalidId)
{
    AxisLinkManager mgr;
    EXPECT_EQ(mgr.group(999), nullptr);
}

// ─── Propagation: X-axis ────────────────────────────────────────────────────

TEST(AxisLinkPropagateX, PropagateFromSetsXLimits)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::X);

    auto old_xlim = ax(*fig, 0).x_limits();
    auto old_ylim = ax(*fig, 0).y_limits();
    ax(*fig, 0).xlim(2.0f, 8.0f);
    mgr.propagate_from(&ax(*fig, 0), old_xlim, old_ylim);

    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 8.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 2).x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 2).x_limits().max, 8.0f);
}

TEST(AxisLinkPropagateX, PropagateFromDoesNotChangeY)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    ax(*fig, 1).ylim(-5.0f, 5.0f);
    auto old_xlim = ax(*fig, 0).x_limits();
    auto old_ylim = ax(*fig, 0).y_limits();
    ax(*fig, 0).xlim(1.0f, 9.0f);
    mgr.propagate_from(&ax(*fig, 0), old_xlim, old_ylim);

    // Y should be unchanged
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -5.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 5.0f);
}

TEST(AxisLinkPropagateX, PropagateZoom)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    // Zoom ax0 around center (5.0) with factor 0.5 (zoom in)
    float cx = 5.0f, cy = 0.0f, factor = 0.5f;
    auto  xlim     = ax(*fig, 0).x_limits();
    float new_xmin = cx + (xlim.min - cx) * factor;
    float new_xmax = cx + (xlim.max - cx) * factor;
    ax(*fig, 0).xlim(new_xmin, new_xmax);

    mgr.propagate_zoom(&ax(*fig, 0), cx, cy, factor);

    // ax1 should have same zoom applied
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, new_xmin);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, new_xmax);
}

TEST(AxisLinkPropagateX, PropagatePan)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    float dx = 3.0f, dy = 0.5f;
    auto  xlim0 = ax(*fig, 1).x_limits();
    mgr.propagate_pan(&ax(*fig, 0), dx, dy);

    // X should shift by dx
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, xlim0.min + dx);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, xlim0.max + dx);
    // Y should NOT shift (X-only link)
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -1.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 1.0f);
}

TEST(AxisLinkPropagateX, PropagateLimits)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    AxisLimits new_x{-5.0f, 15.0f};
    AxisLimits new_y{-2.0f, 2.0f};
    mgr.propagate_limits(&ax(*fig, 0), new_x, new_y);

    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, -5.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 15.0f);
    // Y unchanged for X-only link
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -1.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 1.0f);
}

// ─── Propagation: Y-axis ────────────────────────────────────────────────────

TEST(AxisLinkPropagateY, PropagateFromSetsYLimits)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::Y);

    auto old_xlim = ax(*fig, 0).x_limits();
    auto old_ylim = ax(*fig, 0).y_limits();
    ax(*fig, 0).ylim(-3.0f, 3.0f);
    mgr.propagate_from(&ax(*fig, 0), old_xlim, old_ylim);

    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -3.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 3.0f);
    // X unchanged
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 10.0f);
}

TEST(AxisLinkPropagateY, PropagatePanYOnly)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::Y);

    float dx = 3.0f, dy = 0.5f;
    auto  ylim0 = ax(*fig, 1).y_limits();
    mgr.propagate_pan(&ax(*fig, 0), dx, dy);

    // Y should shift by dy
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, ylim0.min + dy);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, ylim0.max + dy);
    // X should NOT shift
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 10.0f);
}

// ─── Propagation: Both axes ─────────────────────────────────────────────────

TEST(AxisLinkPropagateBoth, PropagateFromSetsBoth)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::Both);

    auto old_xlim = ax(*fig, 0).x_limits();
    auto old_ylim = ax(*fig, 0).y_limits();
    ax(*fig, 0).xlim(1.0f, 5.0f);
    ax(*fig, 0).ylim(-0.5f, 0.5f);
    mgr.propagate_from(&ax(*fig, 0), old_xlim, old_ylim);

    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 5.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -0.5f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 0.5f);
}

TEST(AxisLinkPropagateBoth, PropagatePanBoth)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::Both);

    float dx = 2.0f, dy = 0.3f;
    mgr.propagate_pan(&ax(*fig, 0), dx, dy);

    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 0.0f + dx);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 10.0f + dx);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -1.0f + dy);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().max, 1.0f + dy);
}

// ─── Propagation: edge cases ─────────────────────────────────────────────────

TEST(AxisLinkPropagateEdge, PropagateNullSource)
{
    AxisLinkManager mgr;
    // Should not crash
    mgr.propagate_from(nullptr, {0, 10}, {-1, 1});
    mgr.propagate_zoom(nullptr, 5.0f, 0.0f, 0.5f);
    mgr.propagate_pan(nullptr, 1.0f, 1.0f);
    mgr.propagate_limits(nullptr, {0, 10}, {-1, 1});
}

TEST(AxisLinkPropagateEdge, PropagateUnlinkedAxes)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    // ax0 is not linked to anything
    auto old_xlim = ax(*fig, 0).x_limits();
    auto old_ylim = ax(*fig, 0).y_limits();
    ax(*fig, 0).xlim(1.0f, 5.0f);
    mgr.propagate_from(&ax(*fig, 0), old_xlim, old_ylim);
    // ax1 should be unchanged
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 0.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().max, 10.0f);
}

TEST(AxisLinkPropagateEdge, SourceNotModifiedByPropagate)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    ax(*fig, 0).xlim(2.0f, 8.0f);
    mgr.propagate_limits(&ax(*fig, 0), {2.0f, 8.0f}, {-1.0f, 1.0f});

    // Source should be unchanged
    EXPECT_FLOAT_EQ(ax(*fig, 0).x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 0).x_limits().max, 8.0f);
}

TEST(AxisLinkPropagateEdge, ReentrantGuard)
{
    // Propagation should not recurse
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 1), &ax(*fig, 2), LinkAxis::X);

    ax(*fig, 0).xlim(2.0f, 8.0f);
    mgr.propagate_limits(&ax(*fig, 0), {2.0f, 8.0f}, {-1.0f, 1.0f});

    // All three should have the same X limits (they're in the same group
    // since link() merges into existing groups)
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 2.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 2).x_limits().min, 2.0f);
}

// ─── Multiple groups ─────────────────────────────────────────────────────────

TEST(AxisLinkMultiGroup, SeparateXAndYGroups)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::Y);

    // Change both X and Y on ax0
    ax(*fig, 0).xlim(1.0f, 5.0f);
    ax(*fig, 0).ylim(-2.0f, 2.0f);
    mgr.propagate_limits(&ax(*fig, 0), {1.0f, 5.0f}, {-2.0f, 2.0f});

    // ax1: X linked, Y not
    EXPECT_FLOAT_EQ(ax(*fig, 1).x_limits().min, 1.0f);
    EXPECT_FLOAT_EQ(ax(*fig, 1).y_limits().min, -1.0f);   // unchanged

    // ax2: Y linked, X not
    EXPECT_FLOAT_EQ(ax(*fig, 2).x_limits().min, 0.0f);   // unchanged
    EXPECT_FLOAT_EQ(ax(*fig, 2).y_limits().min, -2.0f);
}

TEST(AxisLinkMultiGroup, AxesInMultipleGroups)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    auto            id1 = mgr.create_group("X-link", LinkAxis::X);
    auto            id2 = mgr.create_group("Y-link", LinkAxis::Y);
    mgr.add_to_group(id1, &ax(*fig, 0));
    mgr.add_to_group(id1, &ax(*fig, 1));
    mgr.add_to_group(id2, &ax(*fig, 0));
    mgr.add_to_group(id2, &ax(*fig, 2));

    auto groups = mgr.groups_for(&ax(*fig, 0));
    EXPECT_EQ(groups.size(), 2u);

    auto peers = mgr.linked_peers(&ax(*fig, 0));
    EXPECT_EQ(peers.size(), 2u);   // ax1 and ax2
}

// ─── Callback ────────────────────────────────────────────────────────────────

TEST(AxisLinkCallback, OnChangeCalledOnLink)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    int             count = 0;
    mgr.set_on_change([&]() { count++; });

    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    EXPECT_GT(count, 0);
}

TEST(AxisLinkCallback, OnChangeCalledOnUnlink)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);

    int count = 0;
    mgr.set_on_change([&]() { count++; });
    mgr.unlink(&ax(*fig, 0));
    EXPECT_GT(count, 0);
}

TEST(AxisLinkCallback, OnChangeCalledOnRemoveGroup)
{
    AxisLinkManager mgr;
    auto            id    = mgr.create_group("G1", LinkAxis::X);
    int             count = 0;
    mgr.set_on_change([&]() { count++; });
    mgr.remove_group(id);
    EXPECT_GT(count, 0);
}

// ─── Serialization ───────────────────────────────────────────────────────────

TEST(AxisLinkSerialization, EmptySerialize)
{
    AxisLinkManager mgr;
    auto            json = mgr.serialize([](const Axes*) { return -1; });
    EXPECT_EQ(json, "{}");
}

TEST(AxisLinkSerialization, RoundTrip)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::Y);

    // Serialize
    auto json = mgr.serialize(
        [&](const Axes* a) -> int
        {
            for (size_t i = 0; i < fig->axes().size(); ++i)
            {
                if (fig->axes()[i].get() == a)
                    return static_cast<int>(i);
            }
            return -1;
        });

    EXPECT_FALSE(json.empty());
    EXPECT_NE(json, "{}");

    // Deserialize into a new manager
    AxisLinkManager mgr2;
    mgr2.deserialize(json,
                     [&](int idx) -> Axes*
                     {
                         if (idx < 0 || idx >= static_cast<int>(fig->axes().size()))
                             return nullptr;
                         return fig->axes_mut()[static_cast<size_t>(idx)].get();
                     });

    EXPECT_EQ(mgr2.group_count(), 2u);
    EXPECT_TRUE(mgr2.is_linked(&ax(*fig, 0)));
    EXPECT_TRUE(mgr2.is_linked(&ax(*fig, 1)));
    EXPECT_TRUE(mgr2.is_linked(&ax(*fig, 2)));
}

TEST(AxisLinkSerialization, DeserializeEmpty)
{
    AxisLinkManager mgr;
    mgr.deserialize("", [](int) -> Axes* { return nullptr; });
    EXPECT_EQ(mgr.group_count(), 0u);

    mgr.deserialize("{}", [](int) -> Axes* { return nullptr; });
    EXPECT_EQ(mgr.group_count(), 0u);
}

TEST(AxisLinkSerialization, DeserializePreservesAxisType)
{
    auto            fig = make_figure(2);
    AxisLinkManager mgr;
    auto            id = mgr.create_group("XY Link", LinkAxis::Both);
    mgr.add_to_group(id, &ax(*fig, 0));
    mgr.add_to_group(id, &ax(*fig, 1));

    auto json = mgr.serialize(
        [&](const Axes* a) -> int
        {
            for (size_t i = 0; i < fig->axes().size(); ++i)
            {
                if (fig->axes()[i].get() == a)
                    return static_cast<int>(i);
            }
            return -1;
        });

    AxisLinkManager mgr2;
    mgr2.deserialize(json,
                     [&](int idx) -> Axes*
                     {
                         if (idx < 0 || idx >= static_cast<int>(fig->axes().size()))
                             return nullptr;
                         return fig->axes_mut()[static_cast<size_t>(idx)].get();
                     });

    EXPECT_EQ(mgr2.group_count(), 1u);
    auto groups = mgr2.groups_for(&ax(*fig, 0));
    ASSERT_EQ(groups.size(), 1u);
    auto* g = mgr2.group(groups[0]);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->axis, LinkAxis::Both);
}

TEST(AxisLinkSerialization, RoundTrips3DGroupsAndClear)
{
    Figure figure;
    auto&  first  = figure.subplot3d(1, 2, 1);
    auto&  second = figure.subplot3d(1, 2, 2);
    first.zlim(-2.0, 2.0);
    second.zlim(-5.0, 5.0);

    AxisLinkManager manager;
    ASSERT_GT(manager.link_3d(&first, &second, LinkAxis::Z), 0u);
    const std::vector<Axes3D*> axes{&first, &second};
    const std::string          json = manager.serialize(
        [](const Axes*) { return -1; },
        [&axes](const Axes3D* candidate)
        {
            const auto found = std::find(axes.begin(), axes.end(), candidate);
            return found == axes.end() ? -1 : static_cast<int>(found - axes.begin());
        });
    EXPECT_NE(json.find("groups3d"), std::string::npos);

    AxisLinkManager restored;
    restored.deserialize(
        json,
        [](int) -> Axes* { return nullptr; },
        [&axes](int index) -> Axes3D*
        {
            return index >= 0 && static_cast<size_t>(index) < axes.size()
                       ? axes[static_cast<size_t>(index)]
                       : nullptr;
        });
    EXPECT_EQ(restored.group_count(), 0u);
    EXPECT_EQ(restored.group_3d_count(), 1u);
    first.zlim(10.0, 20.0);
    restored.propagate_from_3d(&first);
    EXPECT_DOUBLE_EQ(second.z_limits().min, 10.0);
    EXPECT_DOUBLE_EQ(second.z_limits().max, 20.0);

    restored.clear();
    EXPECT_EQ(restored.group_3d_count(), 0u);
}

// ─── Thread safety ───────────────────────────────────────────────────────────

TEST(AxisLinkThreadSafety, ConcurrentLinkUnlink)
{
    auto            fig = make_figure(4);
    AxisLinkManager mgr;

    std::atomic<bool> done{false};
    std::thread       t1(
        [&]()
        {
            for (int i = 0; i < 100; ++i)
            {
                mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
                mgr.unlink(&ax(*fig, 0));
            }
            done = true;
        });

    std::thread t2(
        [&]()
        {
            for (int i = 0; i < 100; ++i)
            {
                mgr.link(&ax(*fig, 2), &ax(*fig, 3), LinkAxis::Y);
                mgr.unlink(&ax(*fig, 2));
            }
        });

    t1.join();
    t2.join();
    // No crash, no deadlock
    EXPECT_TRUE(done.load());
}

TEST(AxisLinkThreadSafety, ConcurrentPropagateAndQuery)
{
    auto            fig = make_figure(3);
    AxisLinkManager mgr;
    mgr.link(&ax(*fig, 0), &ax(*fig, 1), LinkAxis::X);
    mgr.link(&ax(*fig, 0), &ax(*fig, 2), LinkAxis::X);

    std::atomic<bool> done{false};
    std::thread       t1(
        [&]()
        {
            for (int i = 0; i < 100; ++i)
            {
                mgr.propagate_limits(&ax(*fig, 0),
                                     {static_cast<float>(i), static_cast<float>(i + 10)},
                                     {-1.0f, 1.0f});
            }
            done = true;
        });

    std::thread t2(
        [&]()
        {
            for (int i = 0; i < 100; ++i)
            {
                mgr.is_linked(&ax(*fig, 1));
                mgr.linked_peers(&ax(*fig, 0));
                mgr.groups_for(&ax(*fig, 0));
            }
        });

    t1.join();
    t2.join();
    EXPECT_TRUE(done.load());
}

// ─── LinkGroup struct ────────────────────────────────────────────────────────

TEST(LinkGroupStruct, ContainsAndRemove)
{
    auto      fig = make_figure(3);
    LinkGroup group;
    group.members.push_back(&ax(*fig, 0));
    group.members.push_back(&ax(*fig, 1));

    EXPECT_TRUE(group.contains(&ax(*fig, 0)));
    EXPECT_TRUE(group.contains(&ax(*fig, 1)));
    EXPECT_FALSE(group.contains(&ax(*fig, 2)));

    group.remove(&ax(*fig, 0));
    EXPECT_FALSE(group.contains(&ax(*fig, 0)));
    EXPECT_EQ(group.members.size(), 1u);
}

// ─── Integration with InputHandler ──────────────────────────────────────────

TEST(AxisLinkInput, SetterGetter)
{
    InputHandler handler;
    EXPECT_EQ(handler.axis_link_manager(), nullptr);

    AxisLinkManager mgr;
    handler.set_axis_link_manager(&mgr);
    EXPECT_EQ(handler.axis_link_manager(), &mgr);
}
