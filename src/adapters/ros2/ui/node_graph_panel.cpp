// NodeGraphPanel — implementation.
//
// Force-directed layout: Fruchterman–Reingold style.
//   - Repulsion between every pair of nodes  (O(n²), acceptable for n ≤ 200)
//   - Spring attraction along edges
//   - Velocity damping each step
//   - Bounded step count per frame (MAX_STEPS_PER_FRAME)
//   - Convergence detected when max velocity < CONVERGENCE_THRESHOLD

#include "ui/node_graph_panel.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <format>

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string last_component(const std::string& s)
{
    auto pos = s.rfind('/');
    if (pos == std::string::npos || pos + 1 >= s.size())
        return s;
    return s.substr(pos + 1);
}

static std::string node_namespace(const std::string& full)
{
    auto pos = full.rfind('/');
    if (pos == std::string::npos || pos == 0)
        return "/";
    return full.substr(0, pos);
}

// Deterministic hash of a string to a hue in [0, 1)
static float string_to_hue(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 16777619u;
    }
    return static_cast<float>(h % 1000) / 1000.0f;
}

// HSV → RGB  (s=0.55, v=0.82 for pastel namespace colors)
static void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b)
{
    int   i = static_cast<int>(h * 6.0f);
    float f = h * 6.0f - static_cast<float>(i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6)
    {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
    }
}

// ---------------------------------------------------------------------------
// NodeGraphPanel — construction
// ---------------------------------------------------------------------------

NodeGraphPanel::NodeGraphPanel()
{
    last_refresh_time_ =
        std::chrono::steady_clock::now() - std::chrono::milliseconds(refresh_interval_.count() + 1);
}

// ---------------------------------------------------------------------------
// wiring
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_topic_discovery(TopicDiscovery* disc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    discovery_ = disc;
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void NodeGraphPanel::tick(float dt)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Auto-refresh check
        auto now = std::chrono::steady_clock::now();
        if ((discovery_ != nullptr)
            && (first_refresh_ || now - last_refresh_time_ >= refresh_interval_))
        {
            first_refresh_     = false;
            last_refresh_time_ = now;
            rebuild_from_discovery();
        }
    }

    if (!built_.load())
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Hierarchical mode is fully deterministic — no iterative force pass.
        if (!layout_converged_ && layout_mode_ == GraphLayoutMode::Force)
        {
            for (int i = 0; i < MAX_STEPS_PER_FRAME; ++i)
            {
                layout_step_unlocked();
                if (layout_converged_)
                    break;
            }
        }
    }
    (void)dt;
}

void NodeGraphPanel::refresh()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (discovery_)
        rebuild_from_discovery();
}

void NodeGraphPanel::reset_layout()
{
    std::lock_guard<std::mutex> lock(mutex_);
    reset_layout_unlocked();
    recenter_view_pending_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_namespace_filter(const std::string& prefix)
{
    std::lock_guard<std::mutex> lock(mutex_);
    namespace_filter_ = prefix;
}

std::string NodeGraphPanel::namespace_filter() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return namespace_filter_;
}

void NodeGraphPanel::set_refresh_interval(std::chrono::milliseconds ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    refresh_interval_ = ms;
}

std::chrono::milliseconds NodeGraphPanel::refresh_interval() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return refresh_interval_;
}

void NodeGraphPanel::set_title(const std::string& title)
{
    std::lock_guard<std::mutex> lock(mutex_);
    title_ = title;
}

std::string NodeGraphPanel::title() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return title_;
}

void NodeGraphPanel::set_repulsion(float k)
{
    std::lock_guard<std::mutex> lock(mutex_);
    repulsion_ = k;
}

float NodeGraphPanel::repulsion() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return repulsion_;
}

void NodeGraphPanel::set_attraction(float k)
{
    std::lock_guard<std::mutex> lock(mutex_);
    attraction_ = k;
}

float NodeGraphPanel::attraction() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return attraction_;
}

void NodeGraphPanel::set_damping(float d)
{
    std::lock_guard<std::mutex> lock(mutex_);
    damping_ = d;
}

float NodeGraphPanel::damping() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return damping_;
}

void NodeGraphPanel::set_ideal_length(float l)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ideal_length_ = l;
}

float NodeGraphPanel::ideal_length() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ideal_length_;
}

void NodeGraphPanel::set_layout_mode(GraphLayoutMode m)
{
    std::lock_guard<std::mutex> lock(mutex_);
    layout_mode_ = m;
    reset_layout_unlocked();
}

GraphLayoutMode NodeGraphPanel::layout_mode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return layout_mode_;
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------

std::size_t NodeGraphPanel::node_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

std::size_t NodeGraphPanel::edge_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return edges_.size();
}

GraphSnapshot NodeGraphPanel::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {nodes_, edges_};
}

std::string NodeGraphPanel::selected_id() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_id_;
}

bool NodeGraphPanel::is_built() const
{
    return built_.load();
}

bool NodeGraphPanel::is_animating() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !layout_converged_;
}

// ---------------------------------------------------------------------------
// callbacks
// ---------------------------------------------------------------------------

void NodeGraphPanel::set_select_callback(SelectCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    select_cb_ = std::move(cb);
}

void NodeGraphPanel::set_activate_callback(ActivateCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    activate_cb_ = std::move(cb);
}

void NodeGraphPanel::set_node_filter_callback(NodeFilterCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    node_filter_cb_ = std::move(cb);
}

std::string NodeGraphPanel::selected_ros_node() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Return only if the selected item is a ROS node (not a topic).
    if (selected_id_.empty())
        return {};
    auto it = node_index_.find(selected_id_);
    if (it == node_index_.end())
        return {};
    const GraphNode& n = nodes_[it->second];
    return (n.kind == GraphNodeKind::RosNode) ? n.id : std::string{};
}

// ---------------------------------------------------------------------------
// graph building
// ---------------------------------------------------------------------------

void NodeGraphPanel::build_graph(const std::vector<TopicInfo>& topics,
                                 const std::vector<NodeInfo>&  nodes)
{
    // Preserve existing positions for nodes that survive the rebuild.
    std::unordered_map<std::string, std::pair<float, float>> old_pos;
    for (const auto& n : nodes_)
        old_pos[n.id] = {n.px, n.py};

    nodes_.clear();
    edges_.clear();

    std::unordered_map<std::string, bool> known_nodes;

    // Add ROS2 nodes — skip ros2cli transient nodes
    for (const auto& ni : nodes)
    {
        // ros2cli spawns temporary nodes like /_ros2cli_XXXX — exclude them.
        if (ni.name.rfind("_ros2cli", 0) == 0)
            continue;

        GraphNode gn;
        gn.id           = ni.full_name.empty() ? (ni.namespace_ + "/" + ni.name) : ni.full_name;
        gn.display_name = ni.name;
        gn.namespace_   = ni.namespace_;
        gn.kind         = GraphNodeKind::RosNode;

        auto it = old_pos.find(gn.id);
        if (it != old_pos.end())
        {
            gn.px = it->second.first;
            gn.py = it->second.second;
        }
        known_nodes[gn.id] = true;
        nodes_.push_back(std::move(gn));
    }

    // Add topic nodes — only include topics connected to at least one kept node.
    for (const auto& ti : topics)
    {
        // Count how many known nodes publish/subscribe to this topic.
        bool has_node_info = !ti.publisher_nodes.empty() || !ti.subscriber_nodes.empty();
        int  connections   = 0;
        for (const auto& pub : ti.publisher_nodes)
            if (known_nodes.contains(pub))
                ++connections;
        for (const auto& sub : ti.subscriber_nodes)
            if (known_nodes.contains(sub))
                ++connections;

        // Skip orphan topics (spectra-ros internal, cli-only, etc.)
        // only when we have publisher/subscriber node lists to decide from.
        if (has_node_info && connections == 0)
            continue;

        GraphNode tn;
        tn.id           = ti.name;
        tn.display_name = last_component(ti.name);
        tn.namespace_   = node_namespace(ti.name);
        tn.kind         = GraphNodeKind::Topic;
        tn.pub_count    = ti.publisher_count;
        tn.sub_count    = ti.subscriber_count;

        auto it = old_pos.find(tn.id);
        if (it != old_pos.end())
        {
            tn.px = it->second.first;
            tn.py = it->second.second;
        }
        nodes_.push_back(std::move(tn));

        for (const auto& publisher : ti.publisher_nodes)
        {
            if (!known_nodes.contains(publisher))
                continue;
            GraphEdge e;
            e.from_id    = publisher;
            e.to_id      = ti.name;
            e.is_publish = true;
            edges_.push_back(std::move(e));
        }

        for (const auto& subscriber : ti.subscriber_nodes)
        {
            if (!known_nodes.contains(subscriber))
                continue;
            GraphEdge e;
            e.from_id    = ti.name;
            e.to_id      = subscriber;
            e.is_publish = false;
            edges_.push_back(std::move(e));
        }
    }

    rebuild_index();

    // Only position nodes that don't have a preserved position yet (px==py==0).
    if (layout_mode_ == GraphLayoutMode::Hierarchical)
    {
        place_nodes_hierarchical();   // sets layout_converged_ = true
    }
    else
    {
        scatter_new_nodes();
        layout_converged_ = false;
        max_velocity_     = 1e9f;
    }
    built_.store(true);
}

// ---------------------------------------------------------------------------
// Internal: rebuild from discovery
// ---------------------------------------------------------------------------

void NodeGraphPanel::rebuild_from_discovery()
{
    if (!discovery_)
        return;

    // Snapshot without holding our mutex_ (discovery has its own lock)
    auto topics = discovery_->topics();
    auto rnodes = discovery_->nodes();

    build_graph(topics, rnodes);
}

// ---------------------------------------------------------------------------
// Internal: scatter new nodes (those still at 0,0)
// ---------------------------------------------------------------------------

void NodeGraphPanel::reset_layout_unlocked()
{
    for (auto& n : nodes_)
    {
        n.px = 0.0f;
        n.py = 0.0f;
        n.vx = 0.0f;
        n.vy = 0.0f;
    }

    if (layout_mode_ == GraphLayoutMode::Hierarchical)
    {
        place_nodes_hierarchical();
    }
    else
    {
        scatter_new_nodes();
    }

    // Always start animation so the reset provides visual feedback.
    layout_converged_ = false;
    max_velocity_     = 1e9f;

    recenter_view_pending_.store(true, std::memory_order_release);
}

void NodeGraphPanel::scatter_new_nodes()
{
    // Cap spread so nodes start on-screen even with many nodes.
    const auto  n_f    = static_cast<float>(nodes_.size() + 1);
    const float spread = std::min(ideal_length_ * std::sqrt(n_f) * 0.8f, 600.0f);
    for (auto& n : nodes_)
    {
        if (n.px == 0.0f && n.py == 0.0f)
        {
            float angle  = rng_next() * 6.2831853f;
            float radius = (0.25f + rng_next() * 0.75f) * spread;
            n.px         = std::cos(angle) * radius;
            n.py         = std::sin(angle) * radius;
        }
        n.vx = 0.0f;
        n.vy = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Hierarchical (layered) initial placement — deterministic, no force pass.
// ---------------------------------------------------------------------------
//
// Columns (left→right):
//   0 — ROS nodes that ONLY publish  (source nodes)
//   1 — Topics connected mainly to publishers
//   2 — ROS nodes that both pub AND sub  (mixed / hub nodes)
//   3 — Topics connected mainly to subscribers
//   4 — ROS nodes that ONLY subscribe  (sink nodes)
//
// If a tier is empty it collapses to zero width.
//
// Within each column, nodes are ordered by barycenter heuristic:
//   the average Y of their neighbours in adjacent columns.
//   This is iterated a few passes (Sugiyama-style) to reduce edge crossings.

void NodeGraphPanel::place_nodes_hierarchical()
{
    const std::size_t N = nodes_.size();
    if (N == 0)
    {
        layout_converged_ = true;
        return;
    }

    // ---- 1.  Build directed adjacency ----
    // fwd_adj[i] = nodes that i has an edge TO  (following data flow direction)
    // rev_adj[i] = nodes that have an edge TO i
    std::vector<std::vector<std::size_t>> fwd_adj(N);
    std::vector<std::vector<std::size_t>> rev_adj(N);
    // Symmetric adjacency for barycenter
    std::vector<std::vector<std::size_t>> adj(N);
    for (const auto& e : edges_)
    {
        auto ia = node_index_.find(e.from_id);
        auto ib = node_index_.find(e.to_id);
        if (ia == node_index_.end() || ib == node_index_.end())
            continue;
        fwd_adj[ia->second].push_back(ib->second);
        rev_adj[ib->second].push_back(ia->second);
        adj[ia->second].push_back(ib->second);
        adj[ib->second].push_back(ia->second);
    }

    // ---- 2.  Classify every node into 5 tiers (rqt_graph-style) ----
    //
    //  Tier 0 — pure publisher ROS nodes  (sources, left-most)
    //  Tier 1 — topics whose publishers are in tier 0 or 2 (pub-side topics)
    //  Tier 2 — mixed ROS nodes (both pub and sub)
    //  Tier 3 — topics whose subscribers are in tier 2 or 4 (sub-side topics)
    //  Tier 4 — pure subscriber ROS nodes (sinks, right-most)
    //
    // ROS nodes that are isolated (no pub/sub edges) go to tier 2.

    // Count pub/sub edges per ROS node.
    std::unordered_map<std::string, int> pub_deg;
    std::unordered_map<std::string, int> sub_deg;
    for (const auto& e : edges_)
    {
        if (e.is_publish)
            pub_deg[e.from_id]++;
        else
            sub_deg[e.to_id]++;
    }

    constexpr int    NUM_TIERS = 5;
    std::vector<int> tier(N, 2);   // default: middle (mixed)

    // First pass: classify ROS nodes.
    for (std::size_t i = 0; i < N; ++i)
    {
        const GraphNode& n = nodes_[i];
        if (n.kind == GraphNodeKind::Topic)
            continue;   // topics classified below

        bool has_pub = pub_deg.contains(n.id) && pub_deg[n.id] > 0;
        bool has_sub = sub_deg.contains(n.id) && sub_deg[n.id] > 0;
        if (has_pub && !has_sub)
            tier[i] = 0;   // pure publisher → left
        else if (!has_pub && has_sub)
            tier[i] = 4;   // pure subscriber → right
        else
            tier[i] = 2;   // mixed or isolated → center
    }

    // Second pass: classify topics.
    // A topic connects to publishers (upstream) and subscribers (downstream).
    // Place it between its publishers and subscribers.
    for (std::size_t i = 0; i < N; ++i)
    {
        if (nodes_[i].kind != GraphNodeKind::Topic)
            continue;

        // Find the highest (rightmost) tier of upstream publishers
        // and the lowest (leftmost) tier of downstream subscribers.
        int max_pub_tier = -1;
        int min_sub_tier = NUM_TIERS;

        for (std::size_t up : rev_adj[i])
        {
            if (nodes_[up].kind == GraphNodeKind::RosNode)
                max_pub_tier = std::max(max_pub_tier, tier[up]);
        }
        for (std::size_t dn : fwd_adj[i])
        {
            if (nodes_[dn].kind == GraphNodeKind::RosNode)
                min_sub_tier = std::min(min_sub_tier, tier[dn]);
        }

        // Place topic between its producers and consumers.
        if (max_pub_tier >= 0 && min_sub_tier < NUM_TIERS)
        {
            // Topic sits between the two — pick the midpoint tier.
            int mid = (max_pub_tier + min_sub_tier) / 2;
            // Ensure it's an odd tier (1 or 3) where topics belong.
            if (mid <= 1)
                tier[i] = 1;
            else
                tier[i] = 3;
        }
        else if (max_pub_tier >= 0)
        {
            // Only has publishers → place just right of them.
            tier[i] = (max_pub_tier <= 1) ? 1 : 3;
        }
        else if (min_sub_tier < NUM_TIERS)
        {
            // Only has subscribers → place just left of them.
            tier[i] = (min_sub_tier >= 3) ? 3 : 1;
        }
        else
        {
            tier[i] = 1;   // orphan topic → default left-of-center
        }
    }

    // ---- 3.  Collect per-tier lists ----
    std::vector<std::vector<std::size_t>> by_tier(NUM_TIERS);
    for (std::size_t i = 0; i < N; ++i)
        by_tier[tier[i]].push_back(i);

    // ---- 4.  Namespace-aware initial ordering within each tier ----
    // Sort nodes within each tier by namespace first, then by display name.
    // This groups nodes from the same namespace together vertically.
    for (int t = 0; t < NUM_TIERS; ++t)
    {
        auto& indices = by_tier[t];
        std::sort(indices.begin(),
                  indices.end(),
                  [&](std::size_t a, std::size_t b)
                  {
                      const auto& na = nodes_[a];
                      const auto& nb = nodes_[b];
                      if (na.namespace_ != nb.namespace_)
                          return na.namespace_ < nb.namespace_;
                      return na.display_name < nb.display_name;
                  });
    }

    // ---- 5.  Initial Y positions: evenly spaced per tier ----
    // Use wider spacing — topics get tighter packing, ROS nodes wider.
    const float NODE_GAP  = 48.0f;   // vertical gap for ROS nodes
    const float TOPIC_GAP = 36.0f;   // vertical gap for topics (denser)

    // Add extra gap between namespace groups within a tier.
    const float NS_GAP = 18.0f;

    for (int t = 0; t < NUM_TIERS; ++t)
    {
        const auto& indices = by_tier[t];
        const int   cnt     = static_cast<int>(indices.size());
        if (cnt == 0)
            continue;

        bool  is_topic_tier = (t == 1 || t == 3);
        float gap           = is_topic_tier ? TOPIC_GAP : NODE_GAP;

        // Place with namespace cluster gaps.
        float       y = 0.0f;
        std::string prev_ns;
        for (int k = 0; k < cnt; ++k)
        {
            const auto& n = nodes_[indices[k]];
            if (k > 0 && n.namespace_ != prev_ns)
                y += NS_GAP;   // extra gap between namespace groups
            nodes_[indices[k]].py = y;
            prev_ns               = n.namespace_;
            y += gap;
        }

        // Center the column vertically around 0.
        float total_h = y - gap;
        float offset  = total_h * 0.5f;
        for (int k = 0; k < cnt; ++k)
            nodes_[indices[k]].py -= offset;
    }

    // ---- 6.  Barycenter ordering — 24 passes (Sugiyama-style) ----
    // More passes = fewer edge crossings.  Cost is negligible for n ≤ 200.
    for (int pass = 0; pass < 24; ++pass)
    {
        // Alternate sweep direction each pass.
        int t_start = 0;
        int t_end   = 0;
        int t_step  = 0;
        if (pass % 2 == 0)
        {
            t_start = 0;
            t_end   = NUM_TIERS;
            t_step  = 1;
        }
        else
        {
            t_start = NUM_TIERS - 1;
            t_end   = -1;
            t_step  = -1;
        }

        for (int t = t_start; t != t_end; t += t_step)
        {
            auto& indices = by_tier[t];
            if (indices.size() <= 1)
                continue;

            bool  is_topic_tier = (t == 1 || t == 3);
            float gap           = is_topic_tier ? TOPIC_GAP : NODE_GAP;

            // Compute barycenter: average Y of cross-tier neighbours.
            // Use median for even passes, mean for odd (improves convergence).
            std::vector<float> bary(indices.size(), 0.0f);
            for (std::size_t k = 0; k < indices.size(); ++k)
            {
                std::size_t        ni = indices[k];
                std::vector<float> nbr_ys;
                for (std::size_t nb : adj[ni])
                {
                    if (tier[nb] != t)
                        nbr_ys.push_back(nodes_[nb].py);
                }

                if (nbr_ys.empty())
                {
                    bary[k] = nodes_[ni].py;
                }
                else if (pass % 2 == 0)
                {
                    // Median
                    std::sort(nbr_ys.begin(), nbr_ys.end());
                    bary[k] = nbr_ys[nbr_ys.size() / 2];
                }
                else
                {
                    // Mean
                    float sum = 0.0f;
                    for (float y : nbr_ys)
                        sum += y;
                    bary[k] = sum / static_cast<float>(nbr_ys.size());
                }
            }

            // Sort by barycenter.
            std::vector<std::size_t> perm(indices.size());
            for (std::size_t k = 0; k < perm.size(); ++k)
                perm[k] = k;
            std::sort(perm.begin(),
                      perm.end(),
                      [&](std::size_t a, std::size_t b) { return bary[a] < bary[b]; });

            // Re-assign Y positions in sorted order with namespace gaps.
            const int                cnt = static_cast<int>(indices.size());
            std::vector<std::size_t> sorted_indices(cnt);
            for (int k = 0; k < cnt; ++k)
                sorted_indices[k] = indices[perm[k]];

            float       y = 0.0f;
            std::string prev_ns;
            for (int k = 0; k < cnt; ++k)
            {
                const auto& n = nodes_[sorted_indices[k]];
                if (k > 0 && n.namespace_ != prev_ns)
                    y += NS_GAP;
                nodes_[sorted_indices[k]].py = y;
                prev_ns                      = n.namespace_;
                y += gap;
            }
            float total_h = y - gap;
            float offset  = total_h * 0.5f;
            for (int k = 0; k < cnt; ++k)
                nodes_[sorted_indices[k]].py -= offset;

            indices = sorted_indices;
        }
    }

    // ---- 7.  Assign X positions per tier ----
    // ROS node tiers get more width; topic tiers narrower.
    const float NODE_TIER_GAP  = 400.0f;   // gap before/after ROS node tiers
    const float TOPIC_TIER_GAP = 280.0f;   // gap before/after topic tiers

    float cur_x = 0.0f;
    bool  first = true;
    for (int t = 0; t < NUM_TIERS; ++t)
    {
        if (by_tier[t].empty())
            continue;

        if (!first)
        {
            bool next_is_topic = (t == 1 || t == 3);
            cur_x += next_is_topic ? TOPIC_TIER_GAP : NODE_TIER_GAP;
        }
        first = false;

        for (std::size_t idx : by_tier[t])
        {
            nodes_[idx].px = cur_x;
            nodes_[idx].vx = 0.0f;
            nodes_[idx].vy = 0.0f;
        }
    }

    // Centre the whole graph around origin.
    if (!first)
    {
        float min_x = nodes_[0].px;
        float max_x = nodes_[0].px;
        float min_y = nodes_[0].py;
        float max_y = nodes_[0].py;
        for (const auto& n : nodes_)
        {
            min_x = std::min(min_x, n.px);
            max_x = std::max(max_x, n.px);
            min_y = std::min(min_y, n.py);
            max_y = std::max(max_y, n.py);
        }
        float cx = (min_x + max_x) * 0.5f;
        float cy = (min_y + max_y) * 0.5f;
        for (auto& n : nodes_)
        {
            n.px -= cx;
            n.py -= cy;
        }
    }

    // Hierarchical layout is final — no force refinement needed.
    layout_converged_ = true;
}

// ---------------------------------------------------------------------------
// Internal: rebuild id→index lookup
// ---------------------------------------------------------------------------

void NodeGraphPanel::rebuild_index()
{
    node_index_.clear();
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        node_index_[nodes_[i].id] = i;
}

// ---------------------------------------------------------------------------
// Internal: passes_filter
// ---------------------------------------------------------------------------

// Topics hidden when "Hide Debug" is on (same as rqt_graph default).
static bool is_debug_topic(const std::string& name)
{
    auto ends_with = [](const std::string& s, const char* suffix) -> bool
    {
        std::size_t slen = std::strlen(suffix);
        return s.size() >= slen && s.compare(s.size() - slen, slen, suffix) == 0;
    };
    // ROS2 infrastructure topics that clutter the graph (hidden by rqt_graph default).
    // Includes per-node lifecycle/action topics that add noise.
    if (name == "/rosout" || name == "/parameter_events")
        return true;
    return ends_with(name, "/rosout") || ends_with(name, "/parameter_events")
           || ends_with(name, "/transition_event") || ends_with(name, "/describe_parameters")
           || ends_with(name, "/get_parameters") || ends_with(name, "/set_parameters")
           || ends_with(name, "/list_parameters") || ends_with(name, "/get_parameter_types")
           || ends_with(name, "/set_parameters_atomically") || ends_with(name, "/status");
}

bool NodeGraphPanel::passes_filter(const GraphNode& n) const
{
    // Display mode gate
    if (show_mode_ == GraphShowMode::NodesOnly && n.kind != GraphNodeKind::RosNode)
        return false;
    if (show_mode_ == GraphShowMode::TopicsOnly && n.kind != GraphNodeKind::Topic)
        return false;

    // Debug topic filter
    if (hide_debug_ && n.kind == GraphNodeKind::Topic && is_debug_topic(n.id))
        return false;

    if (namespace_filter_.empty())
        return true;
    return n.namespace_.rfind(namespace_filter_, 0) == 0 || n.id.rfind(namespace_filter_, 0) == 0;
}

// ---------------------------------------------------------------------------
// Layout — single step (mutex_ must already be held)
// ---------------------------------------------------------------------------

void NodeGraphPanel::layout_step_unlocked()
{
    const std::size_t N = nodes_.size();
    if (N == 0)
    {
        layout_converged_ = true;
        return;
    }

    const float k2 = repulsion_ * repulsion_;

    // Accumulate forces into (vx, vy) displacements
    std::vector<float> fx(N, 0.0f);
    std::vector<float> fy(N, 0.0f);

    // Repulsion: every pair
    for (std::size_t i = 0; i < N; ++i)
    {
        for (std::size_t j = i + 1; j < N; ++j)
        {
            float dx    = nodes_[i].px - nodes_[j].px;
            float dy    = nodes_[i].py - nodes_[j].py;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < 1.0f)
                dist2 = 1.0f;
            float force = k2 / dist2;
            float nx    = dx / std::sqrt(dist2);
            float ny    = dy / std::sqrt(dist2);
            fx[i] += force * nx;
            fy[i] += force * ny;
            fx[j] -= force * nx;
            fy[j] -= force * ny;
        }
    }

    // Attraction: along edges
    for (const auto& e : edges_)
    {
        auto ia = node_index_.find(e.from_id);
        auto ib = node_index_.find(e.to_id);
        if (ia == node_index_.end() || ib == node_index_.end())
            continue;

        std::size_t i = ia->second;
        std::size_t j = ib->second;

        float dx   = nodes_[j].px - nodes_[i].px;
        float dy   = nodes_[j].py - nodes_[i].py;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f)
            dist = 1.0f;
        float force = attraction_ * (dist - ideal_length_);
        float nx    = dx / dist;
        float ny    = dy / dist;
        fx[i] += force * nx;
        fy[i] += force * ny;
        fx[j] -= force * nx;
        fy[j] -= force * ny;
    }

    // Speed limit: no node may move more than this per step.
    // Prevents the thrashing phase when many nodes start clustered together.
    const float max_speed = ideal_length_ * 0.4f;

    // Update velocities + positions
    float max_v = 0.0f;
    for (std::size_t i = 0; i < N; ++i)
    {
        nodes_[i].vx = (nodes_[i].vx + fx[i]) * damping_;
        nodes_[i].vy = (nodes_[i].vy + fy[i]) * damping_;

        // Clamp to speed limit
        float spd = std::sqrt(nodes_[i].vx * nodes_[i].vx + nodes_[i].vy * nodes_[i].vy);
        if (spd > max_speed)
        {
            float scale = max_speed / spd;
            nodes_[i].vx *= scale;
            nodes_[i].vy *= scale;
            spd = max_speed;
        }

        nodes_[i].px += nodes_[i].vx;
        nodes_[i].py += nodes_[i].vy;
        if (spd > max_v)
            max_v = spd;
    }

    max_velocity_     = max_v;
    layout_converged_ = max_v < CONVERGENCE_THRESHOLD;
}

void NodeGraphPanel::layout_steps(int n)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; ++i)
        layout_step_unlocked();
}

void NodeGraphPanel::layout_step()
{
    std::lock_guard<std::mutex> lock(mutex_);
    layout_step_unlocked();
}

// ---------------------------------------------------------------------------
// RNG (xorshift32, not cryptographic)
// ---------------------------------------------------------------------------

float NodeGraphPanel::rng_next()
{
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return static_cast<float>(rng_state_ & 0x7FFFFFFFu) / static_cast<float>(0x7FFFFFFFu);
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void NodeGraphPanel::draw(bool* p_open)
{
    std::string win_title;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        win_title = title_ + "###NodeGraphPanel";
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(win_title.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

    draw_toolbar();
    draw_graph_canvas();

    ImGui::End();
}

void NodeGraphPanel::draw_toolbar()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Namespace filter — styled frame
    ImGui::SetNextItemWidth(200.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.16f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.17f, 0.20f, 0.26f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    char buf[256];
    std::strncpy(buf, namespace_filter_.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputTextWithHint("##ns_filter", "Filter namespace…", buf, sizeof(buf)))
        namespace_filter_ = buf;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0.0f, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.20f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));
    if (ImGui::Button("Re-layout"))
    {
        reset_layout_unlocked();
        recenter_view_pending_.store(true, std::memory_order_release);
    }

    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("Refresh"))
    {
        if (discovery_)
        {
            rebuild_from_discovery();
            recenter_view_pending_.store(true, std::memory_order_release);
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::SameLine(0.0f, 10.0f);
    std::size_t vis_nodes = 0;
    std::size_t vis_edges = 0;
    for (const auto& n : nodes_)
        if (passes_filter(n))
            ++vis_nodes;
    for (const auto& e : edges_)
    {
        auto ia = node_index_.find(e.from_id);
        auto ib = node_index_.find(e.to_id);
        if (ia != node_index_.end() && ib != node_index_.end() && passes_filter(nodes_[ia->second])
            && passes_filter(nodes_[ib->second]))
            ++vis_edges;
    }

    const std::string stats = std::format("{} nodes  ·  {} edges", vis_nodes, vis_edges);
    ImGui::TextDisabled("%s", stats.c_str());

    if (!layout_converged_)
    {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.0f, 0.7f), "settling…");
    }

    // Display mode toggle — Nodes / Both / Topics
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

    auto mode_btn = [&](const char* label, GraphShowMode mode)
    {
        const bool active = (show_mode_ == mode);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.51f, 0.97f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.58f, 1.00f, 0.95f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.20f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));
        }
        if (ImGui::Button(label))
            show_mode_ = mode;
        ImGui::PopStyleColor(2);
    };

    mode_btn("Nodes", GraphShowMode::NodesOnly);
    ImGui::SameLine(0.0f, 2.0f);
    mode_btn("Both", GraphShowMode::Both);
    ImGui::SameLine(0.0f, 2.0f);
    mode_btn("Topics", GraphShowMode::TopicsOnly);

    // Layout mode toggle — Hierarchical / Force
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0.0f, 8.0f);

    auto layout_btn = [&](const char* label, GraphLayoutMode lm)
    {
        const bool active = (layout_mode_ == lm);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.22f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.28f, 1.00f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.20f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));
        }
        if (ImGui::Button(label))
        {
            layout_mode_ = lm;
            reset_layout_unlocked();
            recenter_view_pending_.store(true, std::memory_order_release);
        }
        ImGui::PopStyleColor(2);
    };

    layout_btn("Hierarchical", GraphLayoutMode::Hierarchical);
    ImGui::SameLine(0.0f, 2.0f);
    layout_btn("Force", GraphLayoutMode::Force);

    // "Hide Debug" toggle — filters rosout, parameter_events, etc.
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0.0f, 8.0f);

    {
        const bool active = hide_debug_;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.12f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.38f, 0.16f, 1.00f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.20f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.34f, 1.0f));
        }
        if (ImGui::Button(active ? "Debug Hidden" : "Show All"))
            hide_debug_ = !hide_debug_;
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar(2);
}

void NodeGraphPanel::draw_graph_canvas()
{
    ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 50 || canvas_size.y < 50)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background — matches theme bg_secondary
    dl->AddRectFilled(canvas_pos,
                      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                      IM_COL32(18, 21, 28, 255));
    // Subtle dot-grid
    {
        constexpr float GRID = 28.0f;
        float           gox  = std::fmod(view_ox_, GRID);
        float           goy  = std::fmod(view_oy_, GRID);
        for (float gx = canvas_pos.x + gox; gx < canvas_pos.x + canvas_size.x; gx += GRID)
            for (float gy = canvas_pos.y + goy; gy < canvas_pos.y + canvas_size.y; gy += GRID)
                dl->AddCircleFilled(ImVec2(gx, gy), 1.0f, IM_COL32(60, 68, 82, 140));
    }
    // Border
    dl->AddRect(canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(38, 44, 56, 200));

    // Invisible button for mouse interaction (pan + zoom)
    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::InvisibleButton("##canvas",
                           ImVec2(canvas_size.x, canvas_size.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    // Pan via left-drag on canvas background
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        view_ox_ += delta.x;
        view_oy_ += delta.y;
    }

    // Zoom via scroll wheel
    if (ImGui::IsItemHovered())
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            float factor = (wheel > 0) ? 1.1f : (1.0f / 1.1f);
            // Zoom toward mouse position
            ImVec2 mouse = ImGui::GetIO().MousePos;
            float  mx    = (mouse.x - canvas_pos.x - view_ox_) / view_scale_;
            float  my    = (mouse.y - canvas_pos.y - view_oy_) / view_scale_;
            view_scale_  = std::clamp(view_scale_ * factor, MIN_SCALE, MAX_SCALE);
            view_ox_     = mouse.x - canvas_pos.x - mx * view_scale_;
            view_oy_     = mouse.y - canvas_pos.y - my * view_scale_;
        }
    }

    // Clip drawing to canvas
    dl->PushClipRect(canvas_pos,
                     ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                     true);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (recenter_view_pending_.exchange(false, std::memory_order_acq_rel))
        {
            float min_x       = 0.0f;
            float max_x       = 0.0f;
            float min_y       = 0.0f;
            float max_y       = 0.0f;
            bool  have_bounds = false;
            for (const auto& n : nodes_)
            {
                if (!passes_filter(n))
                    continue;
                if (!have_bounds)
                {
                    min_x = max_x = n.px;
                    min_y = max_y = n.py;
                    have_bounds   = true;
                    continue;
                }
                min_x = std::min(min_x, n.px);
                max_x = std::max(max_x, n.px);
                min_y = std::min(min_y, n.py);
                max_y = std::max(max_y, n.py);
            }

            if (!have_bounds)
            {
                view_ox_    = 0.0f;
                view_oy_    = 0.0f;
                view_scale_ = 1.0f;
            }
            else
            {
                const float span_x = std::max(max_x - min_x, 1.0f);
                const float span_y = std::max(max_y - min_y, 1.0f);
                const float fit_x  = (canvas_size.x * 0.8f) / span_x;
                const float fit_y  = (canvas_size.y * 0.8f) / span_y;
                view_scale_        = std::clamp(std::min(fit_x, fit_y), MIN_SCALE, MAX_SCALE);

                const float center_x = 0.5f * (min_x + max_x);
                const float center_y = 0.5f * (min_y + max_y);
                view_ox_             = 0.5f * canvas_size.x - center_x * view_scale_;
                view_oy_             = 0.5f * canvas_size.y - center_y * view_scale_;
            }
        }

        // ---- Namespace group boxes (drawn behind everything) ----
        {
            // Collect bounding box per namespace from visible nodes.
            struct NsBounds
            {
                float min_x{1e9f};
                float min_y{1e9f};
                float max_x{-1e9f};
                float max_y{-1e9f};
            };
            std::unordered_map<std::string, NsBounds> ns_bounds;

            const float ox_d = canvas_pos.x + view_ox_;
            const float oy_d = canvas_pos.y + view_oy_;

            for (const auto& n : nodes_)
            {
                if (!passes_filter(n))
                    continue;

                // Estimate half-width/height of each node (mirrors draw_node sizes)
                float hw = 0.0f;
                float hh = 0.0f;
                if (n.kind == GraphNodeKind::RosNode)
                {
                    float tw = ImGui::CalcTextSize(n.display_name.c_str()).x;
                    hw       = std::max(48.0f, tw * 0.5f + 14.0f);
                    hh       = 15.0f;
                }
                else
                {
                    float tw = ImGui::CalcTextSize(n.display_name.c_str()).x;
                    hw       = std::max(24.0f, tw * 0.5f + 8.0f);
                    hh       = 10.0f;
                }

                float sx  = ox_d + n.px * view_scale_;
                float sy  = oy_d + n.py * view_scale_;
                float shw = hw * view_scale_;
                float shh = hh * view_scale_;

                auto& b = ns_bounds[n.namespace_];
                b.min_x = std::min(b.min_x, sx - shw);
                b.max_x = std::max(b.max_x, sx + shw);
                b.min_y = std::min(b.min_y, sy - shh);
                b.max_y = std::max(b.max_y, sy + shh);
            }

            // Draw each namespace group box.
            const float pad = 12.0f * view_scale_;
            const float rr  = 8.0f * view_scale_;

            for (const auto& [ns, b] : ns_bounds)
            {
                // Skip the root "/" namespace if it's the only one (no visual grouping needed)
                if (ns_bounds.size() <= 1)
                    break;

                if (ns == "/")
                    continue;

                float hue = string_to_hue(ns);
                float cr  = 0.0f;
                float cg  = 0.0f;
                float cb  = 0.0f;
                hsv_to_rgb(hue, 0.45f, 0.55f, cr, cg, cb);

                ImU32 fill_col   = IM_COL32(static_cast<int>(cr * 255),
                                          static_cast<int>(cg * 255),
                                          static_cast<int>(cb * 255),
                                          18);
                ImU32 border_col = IM_COL32(static_cast<int>(cr * 255),
                                            static_cast<int>(cg * 255),
                                            static_cast<int>(cb * 255),
                                            55);
                ImU32 text_col   = IM_COL32(static_cast<int>(cr * 255),
                                          static_cast<int>(cg * 255),
                                          static_cast<int>(cb * 255),
                                          140);

                ImVec2 tl(b.min_x - pad, b.min_y - pad - 16.0f * view_scale_);
                ImVec2 br(b.max_x + pad, b.max_y + pad);

                dl->AddRectFilled(tl, br, fill_col, rr);
                dl->AddRect(tl, br, border_col, rr, 0, 1.2f);

                // Namespace label at top-left inside the box
                if (view_scale_ > 0.2f)
                {
                    const char* label = ns.c_str();
                    dl->AddText(ImVec2(tl.x + 6.0f * view_scale_, tl.y + 3.0f * view_scale_),
                                text_col,
                                label);
                }
            }
        }

        // Draw edges first (below nodes)
        for (const auto& e : edges_)
            draw_edge(e, canvas_pos.x + view_ox_, canvas_pos.y + view_oy_, view_scale_);

        // Draw nodes
        for (auto& n : nodes_)
        {
            if (!passes_filter(n))
                continue;
            draw_node(n, canvas_pos.x + view_ox_, canvas_pos.y + view_oy_, view_scale_);
        }

        // Detail popup for selected node
        if (!selected_id_.empty())
        {
            auto it = node_index_.find(selected_id_);
            if (it != node_index_.end())
                draw_detail_popup(nodes_[it->second]);
        }
    }

    dl->PopClipRect();

    // Hit-test for clicks AFTER drawing (so we read the correct node positions)
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float  wx    = (mouse.x - canvas_pos.x - view_ox_) / view_scale_;
        float  wy    = (mouse.y - canvas_pos.y - view_oy_) / view_scale_;

        std::lock_guard<std::mutex> lock(mutex_);
        std::string                 hit;
        float                       best_dist2 = 50.0f * 50.0f;   // max click radius
        for (const auto& n : nodes_)
        {
            if (!passes_filter(n))
                continue;
            float dx    = wx - n.px;
            float dy    = wy - n.py;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < best_dist2)
            {
                best_dist2 = dist2;
                hit        = n.id;
            }
        }

        if (hit != selected_id_)
        {
            // Deselect previous
            if (!selected_id_.empty())
            {
                auto it = node_index_.find(selected_id_);
                if (it != node_index_.end())
                    nodes_[it->second].selected = false;
            }
            selected_id_ = hit;
            if (!hit.empty())
            {
                auto it = node_index_.find(hit);
                if (it != node_index_.end())
                {
                    const GraphNode& gn         = nodes_[it->second];
                    nodes_[it->second].selected = true;
                    if (select_cb_)
                        select_cb_(gn);
                    // Fire the topic-filter callback only for ROS nodes.
                    if (node_filter_cb_ && gn.kind == GraphNodeKind::RosNode)
                        node_filter_cb_(gn.id);
                }
            }
        }
        else if (!hit.empty() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            auto it = node_index_.find(hit);
            if (it != node_index_.end() && activate_cb_)
                activate_cb_(nodes_[it->second]);
        }
    }
}

void NodeGraphPanel::draw_node(const GraphNode& n, float ox, float oy, float scale) const
{
    if (!passes_filter(n))
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float       cx = ox + n.px * scale;
    float       cy = oy + n.py * scale;

    if (n.kind == GraphNodeKind::RosNode)
    {
        // Rounded rect: auto-sized to text width
        float text_w = ImGui::CalcTextSize(n.display_name.c_str()).x;
        float hw     = std::max(48.0f, text_w * 0.5f + 14.0f) * scale;
        float hh     = 15.0f * scale;
        float rr     = 5.0f * scale;

        // Namespace-keyed accent color (desaturated, dark)
        float hue = string_to_hue(n.namespace_);
        float nr  = 0.0f;
        float ng  = 0.0f;
        float nb  = 0.0f;
        hsv_to_rgb(hue, 0.5f, 0.5f, nr, ng, nb);
        ImU32 ns_col = IM_COL32(static_cast<int>(nr * 255),
                                static_cast<int>(ng * 255),
                                static_cast<int>(nb * 255),
                                200);

        // Fill: dark panel
        ImU32 fill = n.selected ? IM_COL32(30, 38, 58, 245) : IM_COL32(26, 31, 42, 235);

        // Selection glow shadow
        if (n.selected)
            dl->AddRectFilled(ImVec2(cx - hw - 3.0f * scale, cy - hh - 3.0f * scale),
                              ImVec2(cx + hw + 3.0f * scale, cy + hh + 3.0f * scale),
                              IM_COL32(59, 130, 246, 60),
                              rr + 2.0f * scale);

        dl->AddRectFilled(ImVec2(cx - hw, cy - hh), ImVec2(cx + hw, cy + hh), fill, rr);

        // Namespace color accent bar on left edge
        float bar_w = 3.0f * scale;
        dl->AddRectFilled(ImVec2(cx - hw, cy - hh + rr),
                          ImVec2(cx - hw + bar_w, cy + hh - rr),
                          ns_col);

        // Border
        ImU32 border_col = n.selected ? IM_COL32(59, 130, 246, 220) : IM_COL32(55, 65, 85, 200);
        dl->AddRect(ImVec2(cx - hw, cy - hh),
                    ImVec2(cx + hw, cy + hh),
                    border_col,
                    rr,
                    0,
                    n.selected ? 1.5f : 1.0f);

        // Label
        if (scale > 0.3f)
        {
            const char* label = n.display_name.c_str();
            ImVec2      tsz   = ImGui::CalcTextSize(label);
            ImU32 tcol = n.selected ? IM_COL32(220, 235, 255, 255) : IM_COL32(180, 195, 215, 230);
            // Clamp text inside node
            float text_x = cx - tsz.x * 0.5f + bar_w * 0.5f;
            float text_y = cy - tsz.y * 0.5f;
            dl->AddText(ImVec2(text_x, text_y), tcol, label);
        }
    }
    else
    {
        // Topic node — small pill shape
        float hw =
            std::max(24.0f, ImGui::CalcTextSize(n.display_name.c_str()).x * 0.5f + 8.0f) * scale;
        float hh = 10.0f * scale;
        float rr = hh;   // fully rounded

        ImU32 fill       = n.selected ? IM_COL32(28, 40, 60, 230) : IM_COL32(22, 30, 44, 210);
        ImU32 border_col = n.selected ? IM_COL32(59, 130, 246, 200) : IM_COL32(50, 75, 110, 160);

        if (n.selected)
            dl->AddRectFilled(ImVec2(cx - hw - 2.0f * scale, cy - hh - 2.0f * scale),
                              ImVec2(cx + hw + 2.0f * scale, cy + hh + 2.0f * scale),
                              IM_COL32(59, 130, 246, 40),
                              rr + 2.0f * scale);

        dl->AddRectFilled(ImVec2(cx - hw, cy - hh), ImVec2(cx + hw, cy + hh), fill, rr);
        dl->AddRect(ImVec2(cx - hw, cy - hh),
                    ImVec2(cx + hw, cy + hh),
                    border_col,
                    rr,
                    0,
                    n.selected ? 1.5f : 1.0f);

        if (scale > 0.35f)
        {
            const char* label = n.display_name.c_str();
            ImVec2      tsz   = ImGui::CalcTextSize(label);
            ImU32 tcol = n.selected ? IM_COL32(160, 210, 255, 255) : IM_COL32(120, 170, 210, 200);
            dl->AddText(ImVec2(cx - tsz.x * 0.5f, cy - tsz.y * 0.5f), tcol, label);
        }
    }
}

void NodeGraphPanel::draw_edge(const GraphEdge& e, float ox, float oy, float scale) const
{
    auto ia = node_index_.find(e.from_id);
    auto ib = node_index_.find(e.to_id);
    if (ia == node_index_.end() || ib == node_index_.end())
        return;

    const GraphNode& A = nodes_[ia->second];
    const GraphNode& B = nodes_[ib->second];

    if (!passes_filter(A) || !passes_filter(B))
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Publish = accent blue, subscribe = muted teal
    ImU32 col  = e.is_publish ? IM_COL32(59, 130, 246, 80) : IM_COL32(56, 178, 172, 65);
    ImU32 acol = e.is_publish ? IM_COL32(59, 130, 246, 160) : IM_COL32(56, 178, 172, 130);

    if (layout_mode_ == GraphLayoutMode::Hierarchical)
    {
        // Compute half-widths to anchor edges at node borders (right→left).
        auto node_half_width = [&](const GraphNode& n) -> float
        {
            if (n.kind == GraphNodeKind::RosNode)
            {
                float tw = ImGui::CalcTextSize(n.display_name.c_str()).x;
                return std::max(48.0f, tw * 0.5f + 14.0f) * scale;
            }

            float tw = ImGui::CalcTextSize(n.display_name.c_str()).x;
            return std::max(24.0f, tw * 0.5f + 8.0f) * scale;
        };

        float a_cx = ox + A.px * scale;
        float a_cy = oy + A.py * scale;
        float b_cx = ox + B.px * scale;
        float b_cy = oy + B.py * scale;

        float a_hw = node_half_width(A);
        float b_hw = node_half_width(B);

        // Anchor: right edge of A → left edge of B
        float ax = a_cx + a_hw;
        float ay = a_cy;
        float bx = b_cx - b_hw;
        float by = b_cy;

        // Horizontal bezier with proportional control points.
        float dx_abs    = std::abs(bx - ax);
        float cp_offset = std::max(dx_abs * 0.4f, 40.0f * scale);

        ImVec2 p1(ax, ay);
        ImVec2 p2(ax + cp_offset, ay);
        ImVec2 p3(bx - cp_offset, by);
        ImVec2 p4(bx, by);
        dl->AddBezierCubic(p1, p2, p3, p4, col, 1.2f * scale);

        // Arrowhead pointing right at the left edge of B.
        float  arrow_len  = 6.0f * scale;
        float  arrow_half = 2.5f * scale;
        ImVec2 tip(bx, by);
        ImVec2 wing_a(bx - arrow_len, by - arrow_half);
        ImVec2 wing_b(bx - arrow_len, by + arrow_half);
        dl->AddTriangleFilled(tip, wing_a, wing_b, acol);
    }
    else
    {
        // Force-directed: center-to-center straight lines.
        float ax = ox + A.px * scale;
        float ay = oy + A.py * scale;
        float bx = ox + B.px * scale;
        float by = oy + B.py * scale;

        dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), col, 1.0f);

        // Arrowhead at B
        float dx  = bx - ax;
        float dy  = by - ay;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f)
            return;
        dx /= len;
        dy /= len;

        float arrow_len  = 7.0f * scale;
        float arrow_half = 3.0f * scale;

        float tip_x = bx - dx * 2.0f * scale;
        float tip_y = by - dy * 2.0f * scale;

        float base_x = tip_x - dx * arrow_len;
        float base_y = tip_y - dy * arrow_len;

        dl->AddTriangleFilled(ImVec2(tip_x, tip_y),
                              ImVec2(base_x - dy * arrow_half, base_y + dx * arrow_half),
                              ImVec2(base_x + dy * arrow_half, base_y - dx * arrow_half),
                              acol);
    }
}

void NodeGraphPanel::draw_detail_popup(const GraphNode& n)
{
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);

    std::string popup_id = "##detail_" + n.id;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().MousePos.x + 12, ImGui::GetIO().MousePos.y + 12),
                            ImGuiCond_Always);

    if (ImGui::BeginTooltip())
    {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f),
                           "%s",
                           n.kind == GraphNodeKind::RosNode ? "ROS2 Node" : "Topic");
        ImGui::Separator();
        ImGui::TextWrapped("%s", n.id.c_str());
        ImGui::Spacing();

        if (n.kind == GraphNodeKind::Topic)
        {
            ImGui::Text("Publishers:   %d", n.pub_count);
            ImGui::Text("Subscribers:  %d", n.sub_count);
        }
        else
        {
            ImGui::Text("Namespace: %s", n.namespace_.c_str());
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Click to select  •  Dbl-click to open");
        ImGui::EndTooltip();
    }
}

#else   // !SPECTRA_USE_IMGUI

void NodeGraphPanel::draw(bool*) {}

#endif   // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
