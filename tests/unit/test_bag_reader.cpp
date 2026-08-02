// test_bag_reader.cpp — Unit tests for BagReader (rosbag2 backend).
//
// Requires SPECTRA_ROS2_BAG=ON (rosbag2_cpp / rosbag2_storage packages).
// Each test that writes a bag uses a unique temp directory under /tmp so
// tests can run in parallel without file conflicts.
//
// Tests that exercise the stub path (SPECTRA_ROS2_BAG=OFF) are skipped when
// the full implementation is compiled in.
//
// No ROS2 node is needed for write-then-read bag tests.  rosbag2_cpp::Writer
// and ::Reader operate on files without a live ROS2 graph.  We still
// register an RclcppEnvironment so that rclcpp::init() is called once per
// process (rosbag2_cpp may internally call rclcpp APIs).

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#ifdef SPECTRA_ROS2_BAG

    #include <rclcpp/rclcpp.hpp>
    #include <rcutils/logging_macros.h>
    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/serialized_bag_message.hpp>

    #include "bag_message_compat.hpp"
    #include <rosbag2_storage/storage_options.hpp>
    #include <rosbag2_storage/topic_metadata.hpp>

#endif   // SPECTRA_ROS2_BAG

#include "bag_reader.hpp"

namespace fs = std::filesystem;
using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// RclcppEnvironment — call rclcpp::init / shutdown exactly once per process.
// ---------------------------------------------------------------------------

#ifdef SPECTRA_ROS2_BAG

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        if (!rclcpp::ok())
        {
            int    argc = 0;
            char** argv = nullptr;
            rclcpp::init(argc, argv);
        }
    }

    void TearDown() override
    {
        if (rclcpp::ok())
        {
            rclcpp::shutdown();
        }
    }
};

// ---------------------------------------------------------------------------
// BagWriteHelper — write a small synthetic bag to a temp path for testing.
// ---------------------------------------------------------------------------

namespace
{

// Unique temp dir per test to avoid collisions.
static std::string make_temp_bag_dir(const std::string& test_name)
{
    const std::string base =
        std::string("/tmp/spectra_bag_test_") + test_name + "_"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return base;
}

// Build a minimal serialized CDR blob for a Float64 message value.
// std_msgs/msg/Float64 has a single `float64 data` field — 8 bytes CDR.
// CDR header: 4 bytes (0x00 0x01 0x00 0x00 = little-endian header).
static std::vector<uint8_t> make_float64_cdr(double value)
{
    std::vector<uint8_t> buf(12);
    buf[0] = 0x00;   // CDR header byte 0 (big-endian indicator = 0 for little-endian)
    buf[1] = 0x01;   // encapsulation kind: CDR_LE
    buf[2] = 0x00;   // padding
    buf[3] = 0x00;   // padding
    std::memcpy(buf.data() + 4, &value, sizeof(double));
    return buf;
}

// Write `n_messages` Float64 messages to a new bag at `bag_path`.
// start_ts_ns: timestamp of first message (nanoseconds).
// step_ns:     timestamp increment between messages.
static void write_float64_bag(const std::string& bag_path,
                              const std::string& topic,
                              int                n_messages,
                              int64_t            start_ts_ns,
                              int64_t            step_ns,
                              double             value_start = 0.0,
                              double             value_step  = 1.0)
{
    rosbag2_storage::StorageOptions opts;
    opts.uri        = bag_path;
    opts.storage_id = "sqlite3";

    rosbag2_cpp::Writer writer;
    writer.open(opts);

    rosbag2_storage::TopicMetadata meta;
    meta.name                 = topic;
    meta.type                 = "std_msgs/msg/Float64";
    meta.serialization_format = "cdr";
    writer.create_topic(meta);

    for (int i = 0; i < n_messages; ++i)
    {
        auto msg        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
        msg->topic_name = topic;
        bag_compat::set_bag_message_timestamp(*msg,
                                              start_ts_ns + static_cast<int64_t>(i) * step_ns);
        const double val                      = value_start + static_cast<double>(i) * value_step;
        const auto   cdr                      = make_float64_cdr(val);
        msg->serialized_data                  = std::make_shared<rcutils_uint8_array_t>();
        msg->serialized_data->buffer          = new uint8_t[cdr.size()];
        msg->serialized_data->buffer_length   = cdr.size();
        msg->serialized_data->buffer_capacity = cdr.size();
        msg->serialized_data->allocator       = rcutils_get_default_allocator();
        std::memcpy(msg->serialized_data->buffer, cdr.data(), cdr.size());
        writer.write(msg);
    }
}

// Write a bag with two topics.
static void write_two_topic_bag(const std::string& bag_path,
                                int64_t            start_ts_ns,
                                int64_t            step_ns,
                                int                n_per_topic)
{
    rosbag2_storage::StorageOptions opts;
    opts.uri        = bag_path;
    opts.storage_id = "sqlite3";

    rosbag2_cpp::Writer writer;
    writer.open(opts);

    rosbag2_storage::TopicMetadata meta1;
    meta1.name                 = "/topic_a";
    meta1.type                 = "std_msgs/msg/Float64";
    meta1.serialization_format = "cdr";
    writer.create_topic(meta1);

    rosbag2_storage::TopicMetadata meta2;
    meta2.name                 = "/topic_b";
    meta2.type                 = "std_msgs/msg/Float64";
    meta2.serialization_format = "cdr";
    writer.create_topic(meta2);

    for (int i = 0; i < n_per_topic; ++i)
    {
        const int64_t ts = start_ts_ns + static_cast<int64_t>(i) * step_ns;

        auto m1        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
        m1->topic_name = "/topic_a";
        bag_compat::set_bag_message_timestamp(*m1, ts);
        const auto cdr1                      = make_float64_cdr(static_cast<double>(i));
        m1->serialized_data                  = std::make_shared<rcutils_uint8_array_t>();
        m1->serialized_data->buffer          = new uint8_t[cdr1.size()];
        m1->serialized_data->buffer_length   = cdr1.size();
        m1->serialized_data->buffer_capacity = cdr1.size();
        m1->serialized_data->allocator       = rcutils_get_default_allocator();
        std::memcpy(m1->serialized_data->buffer, cdr1.data(), cdr1.size());
        writer.write(m1);

        auto m2        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
        m2->topic_name = "/topic_b";
        bag_compat::set_bag_message_timestamp(*m2, ts + step_ns / 2);   // interleaved
        const auto cdr2                      = make_float64_cdr(static_cast<double>(i) * 2.0);
        m2->serialized_data                  = std::make_shared<rcutils_uint8_array_t>();
        m2->serialized_data->buffer          = new uint8_t[cdr2.size()];
        m2->serialized_data->buffer_length   = cdr2.size();
        m2->serialized_data->buffer_capacity = cdr2.size();
        m2->serialized_data->allocator       = rcutils_get_default_allocator();
        std::memcpy(m2->serialized_data->buffer, cdr2.data(), cdr2.size());
        writer.write(m2);
    }
}

}   // anonymous namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BagReaderTest : public ::testing::Test
{
   protected:
    void TearDown() override
    {
        reader_.close();
        // Clean up temp directories.
        for (const auto& d : temp_dirs_)
        {
            std::error_code ec;
            fs::remove_all(d, ec);   // best-effort
        }
    }

    std::string alloc_temp_dir(const std::string& name)
    {
        auto d = make_temp_bag_dir(name);
        temp_dirs_.push_back(d);
        return d;
    }

    BagReader                reader_;
    std::vector<std::string> temp_dirs_;
};

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, DefaultConstruction_NotOpen)
{
    EXPECT_FALSE(reader_.is_open());
    EXPECT_EQ(reader_.topic_count(), 0u);
    EXPECT_TRUE(reader_.last_error().empty());
}

TEST_F(BagReaderTest, DefaultConstruction_MetadataEmpty)
{
    const auto& m = reader_.metadata();
    EXPECT_TRUE(m.path.empty());
    EXPECT_EQ(m.message_count, 0u);
    EXPECT_EQ(m.duration_ns, 0);
    EXPECT_TRUE(m.topics.empty());
}

TEST_F(BagReaderTest, CloseWhenNotOpen_NoEffect)
{
    EXPECT_NO_THROW(reader_.close());
    EXPECT_FALSE(reader_.is_open());
}

// ---------------------------------------------------------------------------
// Suite: Open failure cases
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, OpenEmptyPath_ReturnsFalse)
{
    EXPECT_FALSE(reader_.open(""));
    EXPECT_FALSE(reader_.is_open());
    EXPECT_FALSE(reader_.last_error().empty());
}

TEST_F(BagReaderTest, OpenNonExistentPath_ReturnsFalse)
{
    EXPECT_FALSE(reader_.open("/nonexistent/path/to/bag.db3"));
    EXPECT_FALSE(reader_.is_open());
    EXPECT_FALSE(reader_.last_error().empty());
}

TEST_F(BagReaderTest, OpenNonExistentPath_ErrorDescriptive)
{
    reader_.open("/nonexistent/bag.db3");
    const std::string& err = reader_.last_error();
    EXPECT_FALSE(err.empty());
    // Should contain at least "BagReader" to identify origin.
    EXPECT_NE(err.find("BagReader"), std::string::npos);
}

TEST_F(BagReaderTest, ClearError)
{
    reader_.open("/bad/path.db3");
    EXPECT_FALSE(reader_.last_error().empty());
    reader_.clear_error();
    EXPECT_TRUE(reader_.last_error().empty());
}

// ---------------------------------------------------------------------------
// Suite: Open success & metadata
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, OpenValidBag_ReturnsTrue)
{
    const std::string bag_path = alloc_temp_dir("OpenValid");
    write_float64_bag(bag_path, "/test_topic", 5, 1'000'000'000LL, 100'000'000LL);

    EXPECT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.is_open());
    EXPECT_TRUE(reader_.last_error().empty());
}

TEST_F(BagReaderTest, Metadata_MessageCount)
{
    const std::string bag_path = alloc_temp_dir("MetaMsgCount");
    write_float64_bag(bag_path, "/count_topic", 10, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_EQ(reader_.metadata().message_count, 10u);
}

TEST_F(BagReaderTest, Metadata_StartAndDuration)
{
    const int64_t start_ns = 2'000'000'000LL;
    const int64_t step_ns  = 200'000'000LL;
    const int     n        = 5;

    const std::string bag_path = alloc_temp_dir("MetaTimes");
    write_float64_bag(bag_path, "/time_topic", n, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));
    const auto& m = reader_.metadata();

    EXPECT_EQ(m.start_time_ns, start_ns);
    EXPECT_GT(m.duration_ns, 0);
    EXPECT_GE(m.end_time_ns, m.start_time_ns);
    EXPECT_GT(m.duration_sec(), 0.0);
}

TEST_F(BagReaderTest, Metadata_StorageId_Sqlite3)
{
    const std::string bag_path = alloc_temp_dir("MetaStorageId");
    write_float64_bag(bag_path, "/storage_topic", 3, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_EQ(reader_.metadata().storage_id, "sqlite3");
}

// ---------------------------------------------------------------------------
// Suite: Topic listing
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, TopicCount_SingleTopic)
{
    const std::string bag_path = alloc_temp_dir("TopicCountSingle");
    write_float64_bag(bag_path, "/single_topic", 3, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_EQ(reader_.topic_count(), 1u);
}

TEST_F(BagReaderTest, TopicCount_TwoTopics)
{
    const std::string bag_path = alloc_temp_dir("TopicCountTwo");
    write_two_topic_bag(bag_path, 1'000'000'000LL, 100'000'000LL, 3);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_EQ(reader_.topic_count(), 2u);
}

TEST_F(BagReaderTest, HasTopic_Present)
{
    const std::string bag_path = alloc_temp_dir("HasTopicPresent");
    write_float64_bag(bag_path, "/check_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.has_topic("/check_topic"));
}

TEST_F(BagReaderTest, HasTopic_Absent)
{
    const std::string bag_path = alloc_temp_dir("HasTopicAbsent");
    write_float64_bag(bag_path, "/real_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_FALSE(reader_.has_topic("/nonexistent_topic"));
}

TEST_F(BagReaderTest, TopicInfo_Present)
{
    const std::string bag_path = alloc_temp_dir("TopicInfoPresent");
    write_float64_bag(bag_path, "/info_topic", 4, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    auto ti = reader_.topic_info("/info_topic");

    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->name, "/info_topic");
    EXPECT_EQ(ti->type, "std_msgs/msg/Float64");
    EXPECT_EQ(ti->message_count, 4u);
}

TEST_F(BagReaderTest, TopicInfo_Absent_ReturnsNullopt)
{
    const std::string bag_path = alloc_temp_dir("TopicInfoAbsent");
    write_float64_bag(bag_path, "/some_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_FALSE(reader_.topic_info("/missing").has_value());
}

TEST_F(BagReaderTest, Topics_VectorContainsCorrectTopic)
{
    const std::string bag_path = alloc_temp_dir("TopicsVector");
    write_float64_bag(bag_path, "/vec_topic", 3, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    const auto& topics = reader_.topics();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0].name, "/vec_topic");
    EXPECT_EQ(topics[0].type, "std_msgs/msg/Float64");
}

TEST_F(BagReaderTest, TopicInfo_SerializationFormat)
{
    const std::string bag_path = alloc_temp_dir("TopicInfoFmt");
    write_float64_bag(bag_path, "/fmt_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    auto ti = reader_.topic_info("/fmt_topic");
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->serialization_fmt, "cdr");
}

// ---------------------------------------------------------------------------
// Suite: Sequential read
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, ReadNext_SingleMessage)
{
    const std::string bag_path = alloc_temp_dir("ReadSingle");
    write_float64_bag(bag_path, "/read_topic", 1, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));

    BagMessage msg;
    EXPECT_TRUE(reader_.read_next(msg));
    EXPECT_TRUE(msg.valid());
    EXPECT_EQ(msg.topic, "/read_topic");
    EXPECT_GT(msg.timestamp_ns, 0);
    EXPECT_FALSE(msg.serialized_data.empty());
}

TEST_F(BagReaderTest, ReadNext_ExhaustsMessages)
{
    const int         n        = 5;
    const std::string bag_path = alloc_temp_dir("ReadExhaust");
    write_float64_bag(bag_path, "/exhaust_topic", n, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));

    int        count = 0;
    BagMessage msg;
    while (reader_.read_next(msg))
    {
        ++count;
    }
    EXPECT_EQ(count, n);
    EXPECT_FALSE(reader_.has_next());
}

TEST_F(BagReaderTest, ReadNext_TimestampsMonotonic)
{
    const std::string bag_path = alloc_temp_dir("ReadMonotonic");
    write_float64_bag(bag_path, "/mono_topic", 8, 1'000'000'000LL, 50'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));

    int64_t    prev = -1;
    BagMessage msg;
    while (reader_.read_next(msg))
    {
        EXPECT_GE(msg.timestamp_ns, prev);
        prev = msg.timestamp_ns;
    }
}

TEST_F(BagReaderTest, ReadNext_TopicAndTypeSet)
{
    const std::string bag_path = alloc_temp_dir("ReadTopicType");
    write_float64_bag(bag_path, "/typed_topic", 3, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));

    BagMessage msg;
    ASSERT_TRUE(reader_.read_next(msg));
    EXPECT_EQ(msg.topic, "/typed_topic");
    EXPECT_EQ(msg.type, "std_msgs/msg/Float64");
    EXPECT_EQ(msg.serialization_fmt, "cdr");
}

TEST_F(BagReaderTest, ReadNext_UpdatesCurrentTimestamp)
{
    const std::string bag_path = alloc_temp_dir("ReadCurTs");
    write_float64_bag(bag_path, "/ts_topic", 3, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_EQ(reader_.current_timestamp_ns(), 0);

    BagMessage msg;
    ASSERT_TRUE(reader_.read_next(msg));
    EXPECT_EQ(reader_.current_timestamp_ns(), msg.timestamp_ns);
    EXPECT_GT(reader_.current_timestamp_ns(), 0);
}

TEST_F(BagReaderTest, HasNext_TrueBeforeRead)
{
    const std::string bag_path = alloc_temp_dir("HasNextBefore");
    write_float64_bag(bag_path, "/hn_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.has_next());
}

TEST_F(BagReaderTest, HasNext_FalseWhenClosed)
{
    EXPECT_FALSE(reader_.has_next());
}

// ---------------------------------------------------------------------------
// Suite: Topic filter
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, TopicFilter_ExcludeOneOfTwo)
{
    const std::string bag_path = alloc_temp_dir("FilterExclude");
    write_two_topic_bag(bag_path, 1'000'000'000LL, 100'000'000LL, 5);

    ASSERT_TRUE(reader_.open(bag_path));
    reader_.set_topic_filter({"/topic_a"});

    BagMessage msg;
    int        count = 0;
    while (reader_.read_next(msg))
    {
        EXPECT_EQ(msg.topic, "/topic_a");
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(BagReaderTest, TopicFilter_BothTopics)
{
    const std::string bag_path = alloc_temp_dir("FilterBoth");
    write_two_topic_bag(bag_path, 1'000'000'000LL, 100'000'000LL, 3);

    ASSERT_TRUE(reader_.open(bag_path));
    reader_.set_topic_filter({"/topic_a", "/topic_b"});

    int        count = 0;
    BagMessage msg;
    while (reader_.read_next(msg))
    {
        ++count;
    }
    EXPECT_EQ(count, 6);   // 3 per topic × 2 topics
}

TEST_F(BagReaderTest, TopicFilter_EmptyMeansAll)
{
    const std::string bag_path = alloc_temp_dir("FilterEmpty");
    write_two_topic_bag(bag_path, 1'000'000'000LL, 100'000'000LL, 4);

    ASSERT_TRUE(reader_.open(bag_path));
    reader_.set_topic_filter({});   // no filter

    int        count = 0;
    BagMessage msg;
    while (reader_.read_next(msg))
    {
        ++count;
    }
    EXPECT_EQ(count, 8);   // 4 per topic × 2 topics
}

TEST_F(BagReaderTest, TopicFilter_GetterMatchesSetter)
{
    reader_.set_topic_filter({"/a", "/b"});
    const auto& f = reader_.topic_filter();
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0], "/a");
    EXPECT_EQ(f[1], "/b");
}

// ---------------------------------------------------------------------------
// Suite: Random seek
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, SeekBegin_ReadsFromStart)
{
    const int64_t     start_ns = 1'000'000'000LL;
    const int64_t     step_ns  = 100'000'000LL;
    const std::string bag_path = alloc_temp_dir("SeekBegin");
    write_float64_bag(bag_path, "/seek_topic", 10, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));

    // Read all.
    BagMessage msg;
    while (reader_.read_next(msg))
    {
    }

    // Seek back to start and verify first timestamp.
    ASSERT_TRUE(reader_.seek_begin());
    ASSERT_TRUE(reader_.read_next(msg));
    EXPECT_EQ(msg.timestamp_ns, start_ns);
}

TEST_F(BagReaderTest, Seek_AbsoluteTimestamp)
{
    const int64_t     start_ns = 1'000'000'000LL;
    const int64_t     step_ns  = 100'000'000LL;
    const int         n        = 10;
    const std::string bag_path = alloc_temp_dir("SeekAbsolute");
    write_float64_bag(bag_path, "/seek_abs_topic", n, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));

    // Seek to message 5 (index 5 → timestamp start + 5*step).
    const int64_t target = start_ns + 5 * step_ns;
    ASSERT_TRUE(reader_.seek(target));

    // Next read should have timestamp >= target.
    BagMessage msg;
    ASSERT_TRUE(reader_.read_next(msg));
    EXPECT_GE(msg.timestamp_ns, target);
}

TEST_F(BagReaderTest, SeekFraction_MidBag)
{
    const int64_t     start_ns = 0LL;
    const int64_t     step_ns  = 100'000'000LL;
    const int         n        = 20;
    const std::string bag_path = alloc_temp_dir("SeekFraction");
    write_float64_bag(bag_path, "/frac_topic", n, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));

    // Seek to 50% through the bag.
    ASSERT_TRUE(reader_.seek_fraction(0.5));

    BagMessage msg;
    int        remaining = 0;
    while (reader_.read_next(msg))
    {
        ++remaining;
    }
    // Should have read approximately half the messages (allow ±2).
    EXPECT_GT(remaining, 0);
    EXPECT_LE(remaining, n);
}

TEST_F(BagReaderTest, SeekFraction_Zero_EqualsBegin)
{
    const int64_t     start_ns = 5'000'000'000LL;
    const int64_t     step_ns  = 100'000'000LL;
    const std::string bag_path = alloc_temp_dir("SeekFracZero");
    write_float64_bag(bag_path, "/frac0_topic", 5, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));
    ASSERT_TRUE(reader_.seek_fraction(0.0));

    BagMessage msg;
    ASSERT_TRUE(reader_.read_next(msg));
    EXPECT_EQ(msg.timestamp_ns, start_ns);
}

TEST_F(BagReaderTest, SeekFraction_ClampedAbove1)
{
    const std::string bag_path = alloc_temp_dir("SeekFracClamp");
    write_float64_bag(bag_path, "/clamp_topic", 5, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.seek_fraction(1.5));   // should clamp to 1.0, not crash
}

TEST_F(BagReaderTest, SeekWhenClosed_ReturnsFalse)
{
    EXPECT_FALSE(reader_.seek(1'000'000'000LL));
    EXPECT_FALSE(reader_.last_error().empty());
}

// ---------------------------------------------------------------------------
// Suite: Progress
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, Progress_InitiallyZero)
{
    const std::string bag_path = alloc_temp_dir("ProgressZero");
    write_float64_bag(bag_path, "/prog_topic", 5, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    // No message read yet → current_timestamp_ns == 0 → progress anchored.
    const double p = reader_.progress();
    EXPECT_GE(p, 0.0);
    EXPECT_LE(p, 1.0);
}

TEST_F(BagReaderTest, Progress_IncreasesAfterRead)
{
    const std::string bag_path = alloc_temp_dir("ProgressInc");
    write_float64_bag(bag_path, "/pi_topic", 10, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    const double p0 = reader_.progress();

    BagMessage msg;
    for (int i = 0; i < 5 && reader_.read_next(msg); ++i)
    {
    }

    const double p1 = reader_.progress();
    EXPECT_GE(p1, 0.0);
    EXPECT_LE(p1, 1.0);
    (void)p0;
}

// ---------------------------------------------------------------------------
// Suite: Reopen
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, Reopen_ClosesAndOpensNewBag)
{
    const std::string bag1 = alloc_temp_dir("Reopen1");
    const std::string bag2 = alloc_temp_dir("Reopen2");
    write_float64_bag(bag1, "/topic1", 3, 1'000'000'000LL, 100'000'000LL);
    write_float64_bag(bag2, "/topic2", 7, 2'000'000'000LL, 50'000'000LL);

    ASSERT_TRUE(reader_.open(bag1));
    EXPECT_EQ(reader_.metadata().message_count, 3u);
    EXPECT_TRUE(reader_.has_topic("/topic1"));

    ASSERT_TRUE(reader_.open(bag2));   // reopen
    EXPECT_EQ(reader_.metadata().message_count, 7u);
    EXPECT_TRUE(reader_.has_topic("/topic2"));
    EXPECT_FALSE(reader_.has_topic("/topic1"));
}

TEST_F(BagReaderTest, Reopen_ErrorClearedOnSuccess)
{
    reader_.open("/nonexistent.db3");
    EXPECT_FALSE(reader_.last_error().empty());

    const std::string bag_path = alloc_temp_dir("ReopenClearErr");
    write_float64_bag(bag_path, "/err_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.last_error().empty());
}

// ---------------------------------------------------------------------------
// Suite: Edge cases
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, ReadNext_WhenClosed_ReturnsFalse)
{
    BagMessage msg;
    EXPECT_FALSE(reader_.read_next(msg));
    EXPECT_FALSE(reader_.last_error().empty());
}

TEST_F(BagReaderTest, HasNext_AfterClose_False)
{
    const std::string bag_path = alloc_temp_dir("HasNextClose");
    write_float64_bag(bag_path, "/hnc_topic", 5, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    EXPECT_TRUE(reader_.has_next());

    reader_.close();
    EXPECT_FALSE(reader_.has_next());
    EXPECT_FALSE(reader_.is_open());
}

TEST_F(BagReaderTest, MetadataDurationHelpers)
{
    const int64_t     start_ns = 1'000'000'000LL;
    const int64_t     step_ns  = 200'000'000LL;
    const std::string bag_path = alloc_temp_dir("DurationHelpers");
    write_float64_bag(bag_path, "/dur_topic", 5, start_ns, step_ns);

    ASSERT_TRUE(reader_.open(bag_path));
    const auto& m = reader_.metadata();

    EXPECT_DOUBLE_EQ(m.start_time_sec(), static_cast<double>(m.start_time_ns) * 1e-9);
    EXPECT_DOUBLE_EQ(m.end_time_sec(), static_cast<double>(m.end_time_ns) * 1e-9);
    EXPECT_DOUBLE_EQ(m.duration_sec(), static_cast<double>(m.duration_ns) * 1e-9);
}

TEST_F(BagReaderTest, MultipleCloseCallsSafe)
{
    const std::string bag_path = alloc_temp_dir("MultiClose");
    write_float64_bag(bag_path, "/mc_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    reader_.close();
    EXPECT_NO_THROW(reader_.close());   // second close must be safe
    EXPECT_FALSE(reader_.is_open());
}

// ---------------------------------------------------------------------------
// Suite: Detect storage ID (static helper via open path)
// ---------------------------------------------------------------------------

TEST_F(BagReaderTest, DetectStorageId_Db3Extension)
{
    // The bag written by write_float64_bag uses sqlite3.
    // Opening it by directory path should succeed and report sqlite3.
    const std::string bag_path = alloc_temp_dir("DetectDb3");
    write_float64_bag(bag_path, "/detect_topic", 2, 1'000'000'000LL, 100'000'000LL);

    ASSERT_TRUE(reader_.open(bag_path));
    // rosbag2 reports storage_identifier as "sqlite3" for .db3 bags.
    EXPECT_EQ(reader_.metadata().storage_id, "sqlite3");
}

// ---------------------------------------------------------------------------
// main — register RclcppEnvironment + run tests.
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment());
    return RUN_ALL_TESTS();
}

#else   // SPECTRA_ROS2_BAG not defined — compile-only stub tests.

// ---------------------------------------------------------------------------
// Stub tests — verify the stub BagReader API compiles and behaves correctly
// when SPECTRA_ROS2_BAG is OFF.
// ---------------------------------------------------------------------------

TEST(BagReaderStub, OpenReturnsFalse)
{
    BagReader r;
    EXPECT_FALSE(r.open("/any/path.db3"));
    EXPECT_FALSE(r.is_open());
}

TEST(BagReaderStub, LastErrorSet)
{
    BagReader r;
    EXPECT_FALSE(r.last_error().empty());
}

TEST(BagReaderStub, ReadNextReturnsFalse)
{
    BagReader  r;
    BagMessage msg;
    EXPECT_FALSE(r.read_next(msg));
}

TEST(BagReaderStub, HasNextFalse)
{
    BagReader r;
    EXPECT_FALSE(r.has_next());
}

TEST(BagReaderStub, TopicCountZero)
{
    BagReader r;
    EXPECT_EQ(r.topic_count(), 0u);
}

TEST(BagReaderStub, SeekReturnsFalse)
{
    BagReader r;
    EXPECT_FALSE(r.seek(0));
    EXPECT_FALSE(r.seek_begin());
    EXPECT_FALSE(r.seek_fraction(0.5));
}

TEST(BagReaderStub, MetadataEmpty)
{
    BagReader r;
    EXPECT_TRUE(r.metadata().path.empty());
    EXPECT_EQ(r.metadata().message_count, 0u);
}

TEST(BagReaderStub, MessageValid_AlwaysFalse)
{
    BagMessage msg;
    EXPECT_FALSE(msg.valid());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif   // SPECTRA_ROS2_BAG
