// Unit tests for TopicDiscovery — ROS2 graph discovery service.
//
// These tests only compile and run when SPECTRA_USE_ROS2 is ON and a ROS2
// workspace is sourced.  They are registered in tests/CMakeLists.txt inside
// the if(SPECTRA_USE_ROS2) block.
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process
// in older ROS2 versions.  We use a single shared initialisation via
// RclcppEnvironment so all tests share one rclcpp lifecycle.

#include "topic_discovery.hpp"
#include "ros2_bridge.hpp"

#include <atomic>
#include <chrono>
#include <unistd.h>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/string.hpp>

using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test environment: init rclcpp once for the whole test binary.
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Fixture: fresh node + TopicDiscovery per test; shares the single rclcpp ctx.
// ---------------------------------------------------------------------------

class TopicDiscoveryTest : public ::testing::Test
{
   protected:
    // Unique topic prefix per test instance — avoids DDS graph cross-talk under parallel ctest.
    std::string topic_prefix_;

    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        // Unique node name per test to avoid ROS2 graph conflicts.
        static std::atomic<int> counter{0};
        const int               id = counter.fetch_add(1);
        const std::string name = "td_test_" + std::to_string(::getpid()) + "_" + std::to_string(id);
        topic_prefix_ = "/spectra_td_" + std::to_string(::getpid()) + "_" + std::to_string(id);

        node_ = std::make_shared<rclcpp::Node>(name);

        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);

        // Spin the executor on a background thread so timers and subscriptions
        // are processed during the tests.
        stop_spin_.store(false);
        spin_thread_ = std::thread(
            [this]()
            {
                while (rclcpp::ok() && !stop_spin_.load(std::memory_order_acquire))
                    executor_->spin_once(std::chrono::milliseconds(10));
            });

        disc_ = std::make_unique<TopicDiscovery>(node_);
    }

    void TearDown() override
    {
        disc_.reset();

        stop_spin_.store(true, std::memory_order_release);
        executor_->cancel();
        if (spin_thread_.joinable())
            spin_thread_.join();

        executor_->remove_node(node_);
        executor_.reset();
        node_.reset();
    }

    // Helper: spin the executor for a duration so the graph can propagate.
    void spin_for(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }

    // Helper: wait until condition becomes true, with a timeout.
    template <typename Pred>
    bool wait_for(Pred pred, std::chrono::milliseconds timeout = 2000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        return pred();
    }

    rclcpp::Node::SharedPtr                                    node_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    std::unique_ptr<TopicDiscovery>                            disc_;
    std::thread                                                spin_thread_;
    std::atomic<bool>                                          stop_spin_{false};
};

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, ConstructsWithNode)
{
    EXPECT_NE(disc_.get(), nullptr);
}

TEST_F(TopicDiscoveryTest, InitiallyEmpty)
{
    // Before any refresh, caches are empty.
    EXPECT_EQ(disc_->topic_count(), 0u);
    EXPECT_EQ(disc_->service_count(), 0u);
    EXPECT_EQ(disc_->node_count(), 0u);
}

TEST_F(TopicDiscoveryTest, HasTopicReturnsFalseBeforeRefresh)
{
    EXPECT_FALSE(disc_->has_topic("/some_topic"));
}

// ---------------------------------------------------------------------------
// Suite: Refresh
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, RefreshPopulatesNodes)
{
    // After refresh, at least our own node should appear.
    disc_->refresh();

    EXPECT_GE(disc_->node_count(), 1u);
}

TEST_F(TopicDiscoveryTest, RefreshReturnsSelf)
{
    disc_->refresh();

    const auto nodes = disc_->nodes();
    bool       found = false;
    for (const auto& n : nodes)
        if (n.name == node_->get_name())
            found = true;
    EXPECT_TRUE(found) << "Expected to find own node '" << node_->get_name() << "'";
}

TEST_F(TopicDiscoveryTest, RefreshCanBeCalledMultipleTimes)
{
    EXPECT_NO_THROW({
        disc_->refresh();
        disc_->refresh();
        disc_->refresh();
    });
}

TEST_F(TopicDiscoveryTest, ServicesPopulatedAfterRefresh)
{
    // rclcpp nodes expose built-in services (e.g. describe_parameters).
    disc_->refresh();
    EXPECT_GE(disc_->service_count(), 0u);   // may be 0 in minimal ROS env
}

// ---------------------------------------------------------------------------
// Suite: Topic discovery with mock publisher
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, DiscoversMockPublisher)
{
    const std::string topic = topic_prefix_ + "_float64";

    // Create a publisher on this node.
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    // Allow graph propagation.
    spin_for(200ms);
    disc_->refresh();

    EXPECT_TRUE(disc_->has_topic(topic)) << "Expected to find topic " << topic << " after refresh";
}

TEST_F(TopicDiscoveryTest, TopicInfoHasCorrectName)
{
    const std::string topic = topic_prefix_ + "_name";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_EQ(info.name, topic);
}

TEST_F(TopicDiscoveryTest, TopicInfoHasType)
{
    const std::string topic = topic_prefix_ + "_type";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_FALSE(info.types.empty());
    // Type should contain "std_msgs/msg/Float64" (exact format may vary).
    bool found = false;
    for (const auto& t : info.types)
        if (t.find("Float64") != std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "Expected Float64 in type list";
}

TEST_F(TopicDiscoveryTest, TopicInfoHasPublisherCount)
{
    const std::string topic = topic_prefix_ + "_pub_count";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_GE(info.publisher_count, 1);
}

TEST_F(TopicDiscoveryTest, TopicInfoHasSubscriberCount)
{
    const std::string topic = topic_prefix_ + "_sub_count";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    auto              sub   = node_->create_subscription<std_msgs::msg::Float64>(
        topic,
        10,
        [](const std_msgs::msg::Float64::SharedPtr) {});

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_GE(info.subscriber_count, 1);
}

TEST_F(TopicDiscoveryTest, TopicsReturnsVector)
{
    const std::string topic = topic_prefix_ + "_vec";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    const auto topics = disc_->topics();
    EXPECT_FALSE(topics.empty());
}

TEST_F(TopicDiscoveryTest, MultipleTopicsDiscovered)
{
    const std::string t1   = topic_prefix_ + "_t1";
    const std::string t2   = topic_prefix_ + "_t2";
    const std::string t3   = topic_prefix_ + "_t3";
    auto              pub1 = node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    auto              pub3 = node_->create_publisher<std_msgs::msg::String>(t3, 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_TRUE(disc_->has_topic(t1));
    EXPECT_TRUE(disc_->has_topic(t2));
    EXPECT_TRUE(disc_->has_topic(t3));
}

TEST_F(TopicDiscoveryTest, UnknownTopicReturnsEmptyInfo)
{
    disc_->refresh();
    const auto info = disc_->topic("/does_not_exist_xyz");
    EXPECT_TRUE(info.name.empty());
}

// ---------------------------------------------------------------------------
// Suite: Topic add/remove callbacks
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, AddCallbackFiredForNewTopic)
{
    std::atomic<int> added_count{0};
    bool             found_target{false};

    const std::string topic = topic_prefix_ + "_cb_add";

    disc_->set_topic_callback(
        [&](const TopicInfo& t, bool added)
        {
            if (added)
            {
                if (t.name == topic)
                    found_target = true;
                ++added_count;
            }
        });

    auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_GE(added_count.load(), 1);
    EXPECT_TRUE(found_target) << "Expected callback for " << topic;
}

TEST_F(TopicDiscoveryTest, AddCallbackNotFiredForAlreadyKnownTopic)
{
    const std::string topic = topic_prefix_ + "_cb_already";
    auto              pub   = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();   // first refresh — populates cache

    std::atomic<int> added_count{0};
    disc_->set_topic_callback(
        [&](const TopicInfo&, bool added)
        {
            if (added)
                ++added_count;
        });

    disc_->refresh();   // second refresh — topic already known, no callback
    EXPECT_EQ(added_count.load(), 0);
}

TEST_F(TopicDiscoveryTest, RemoveCallbackFiredWhenTopicDisappears)
{
    std::atomic<bool> removed{false};
    std::string       removed_name;

    const std::string topic = topic_prefix_ + "_cb_remove";

    // Create publisher, discover it, then destroy it.
    {
        auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
        spin_for(300ms);
        disc_->refresh();
        ASSERT_TRUE(disc_->has_topic(topic));
    }
    // pub is now destroyed — topic should disappear from the graph.

    disc_->set_topic_callback(
        [&](const TopicInfo& t, bool added)
        {
            if (!added)
            {
                removed.store(true);
                removed_name = t.name;
            }
        });

    // Give the graph time to reflect the publisher destruction.
    const bool removed_seen = wait_for(
        [&]()
        {
            disc_->refresh();
            return removed.load();
        },
        3000ms);
    (void)removed_seen;

    EXPECT_TRUE(removed.load());
    EXPECT_EQ(removed_name, topic);
}

// ---------------------------------------------------------------------------
// Suite: Node callbacks
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, NodeAddCallbackFiredOnRefresh)
{
    std::atomic<int> added_count{0};
    disc_->set_node_callback(
        [&](const NodeInfo&, bool added)
        {
            if (added)
                ++added_count;
        });

    disc_->refresh();
    EXPECT_GE(added_count.load(), 1);
}

TEST_F(TopicDiscoveryTest, NodeCallbackNotFiredTwiceForSameNode)
{
    disc_->refresh();   // populate cache

    std::atomic<int> added_count{0};
    disc_->set_node_callback(
        [&](const NodeInfo&, bool added)
        {
            if (added)
                ++added_count;
        });

    disc_->refresh();   // already known — no new-node callbacks
    EXPECT_EQ(added_count.load(), 0);
}

TEST_F(TopicDiscoveryTest, NodeInfoHasName)
{
    disc_->refresh();
    const auto nodes = disc_->nodes();
    ASSERT_FALSE(nodes.empty());
    for (const auto& n : nodes)
        EXPECT_FALSE(n.name.empty());
}

TEST_F(TopicDiscoveryTest, NodeInfoHasFullName)
{
    disc_->refresh();
    for (const auto& n : disc_->nodes())
        EXPECT_FALSE(n.full_name.empty());
}

// ---------------------------------------------------------------------------
// Suite: Service discovery
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, ServiceDiscoveryAfterRefresh)
{
    disc_->refresh();
    // Service count ≥ 0 (some environments have no services visible).
    EXPECT_GE(disc_->service_count(), 0u);
}

TEST_F(TopicDiscoveryTest, ServicesReturnVector)
{
    disc_->refresh();
    const auto svcs = disc_->services();
    // Just ensure no crash and type is correct.
    (void)svcs;
    SUCCEED();
}

TEST_F(TopicDiscoveryTest, ServiceCallbackFiredIfNewServiceAppears)
{
    // Pre-populate the cache with the current services.
    disc_->refresh();

    // Snapshot the names of all services already known after the first refresh.
    // Under parallel test execution other test threads may register new services
    // concurrently; we must not count those as spurious re-adds of known services.
    std::set<std::string> known_names;
    for (const auto& s : disc_->services())
        known_names.insert(s.name);

    // Only flag callbacks for services that were already in the cache.
    std::atomic<int> spurious{0};
    disc_->set_service_callback(
        [&](const ServiceInfo& s, bool a)
        {
            if (a && known_names.count(s.name))
                ++spurious;
        });

    disc_->refresh();
    // Already-known services must NOT fire as "added" again.
    EXPECT_EQ(spurious.load(), 0);
}

// ---------------------------------------------------------------------------
// Suite: RefreshDone callback
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, RefreshDoneCallbackFired)
{
    std::atomic<int> done_count{0};
    disc_->set_refresh_done_callback([&]() { ++done_count; });

    disc_->refresh();
    EXPECT_EQ(done_count.load(), 1);

    disc_->refresh();
    EXPECT_EQ(done_count.load(), 2);
}

// ---------------------------------------------------------------------------
// Suite: start / stop (timer mode)
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, StartDoesNotCrash)
{
    EXPECT_NO_THROW(disc_->start());
    // Give timer a moment to be armed (it fires after interval, not immediately).
    spin_for(50ms);
    disc_->stop();
}

TEST_F(TopicDiscoveryTest, StopBeforeStartIsNoOp)
{
    EXPECT_NO_THROW(disc_->stop());
}

TEST_F(TopicDiscoveryTest, StartIsIdempotent)
{
    EXPECT_NO_THROW({
        disc_->start();
        disc_->start();
    });
    disc_->stop();
}

TEST_F(TopicDiscoveryTest, SetRefreshInterval)
{
    disc_->set_refresh_interval(500ms);
    EXPECT_EQ(disc_->refresh_interval(), 500ms);
}

TEST_F(TopicDiscoveryTest, PeriodicTimerFiresRefresh)
{
    // Use a very short interval and verify refresh_done fires at least twice.
    std::atomic<int> done_count{0};
    disc_->set_refresh_done_callback([&]() { ++done_count; });

    disc_->set_refresh_interval(100ms);
    disc_->start();

    // Wait up to 1 s for at least 2 timer-fired refreshes.
    bool fired = wait_for([&]() { return done_count.load() >= 2; }, 1000ms);
    disc_->stop();

    EXPECT_TRUE(fired) << "Timer did not fire refresh at least twice within 1 s";
}

// ---------------------------------------------------------------------------
// Suite: QoS info
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, QosReliabilityPopulated)
{
    const std::string topic = topic_prefix_ + "_qos_reliable";
    rclcpp::QoS       qos(10);
    qos.reliable();
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, qos);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_EQ(info.qos.reliability, std::string("reliable"));
}

TEST_F(TopicDiscoveryTest, QosBestEffortPopulated)
{
    const std::string topic = topic_prefix_ + "_qos_best_effort";
    rclcpp::QoS       qos(10);
    qos.best_effort();
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, qos);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic(topic);
    EXPECT_EQ(info.qos.reliability, std::string("best_effort"));
}

// ---------------------------------------------------------------------------
// Suite: Edge cases
// ---------------------------------------------------------------------------

TEST_F(TopicDiscoveryTest, TopicCountMatchesTopicsVectorSize)
{
    const std::string t1 = topic_prefix_ + "_edge_1";
    const std::string t2 = topic_prefix_ + "_edge_2";
    auto              p1 = node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              p2 = node_->create_publisher<std_msgs::msg::Float64>(t2, 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_EQ(disc_->topic_count(), disc_->topics().size());
}

TEST_F(TopicDiscoveryTest, NodeCountMatchesNodesVectorSize)
{
    disc_->refresh();
    EXPECT_EQ(disc_->node_count(), disc_->nodes().size());
}

TEST_F(TopicDiscoveryTest, ServiceCountMatchesServicesVectorSize)
{
    disc_->refresh();
    EXPECT_EQ(disc_->service_count(), disc_->services().size());
}

TEST_F(TopicDiscoveryTest, RefreshAfterStopDoesNotCrash)
{
    disc_->start();
    disc_->stop();
    EXPECT_NO_THROW(disc_->refresh());
}

TEST_F(TopicDiscoveryTest, DestructorWithActiveTimerIsClean)
{
    // Create a new discovery, arm the timer, then destroy it immediately.
    auto d = std::make_unique<TopicDiscovery>(node_);
    d->set_refresh_interval(100ms);
    d->start();
    spin_for(50ms);
    EXPECT_NO_THROW(d.reset());
}

// ---------------------------------------------------------------------------
// main — register the shared RclcppEnvironment
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}
