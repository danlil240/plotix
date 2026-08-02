// Unit tests for headless bag CSV export (spectra-ros-analyze).

#ifdef SPECTRA_ROS2_BAG

    #include "tools/ros_bag_analyze.hpp"

    #include <cmath>
    #include <cstring>
    #include <filesystem>
    #include <fstream>
    #include <sstream>
    #include <string>

    #include <gtest/gtest.h>
    #include <rcutils/allocator.h>
    #include <rosbag2_cpp/writer.hpp>
    #include <rosbag2_storage/serialized_bag_message.hpp>
    #include <rosbag2_storage/storage_options.hpp>
    #include <rosbag2_storage/topic_metadata.hpp>

    #include "bag_message_compat.hpp"

using namespace spectra::adapters::ros2;

namespace
{

std::string write_float64_bag(const std::filesystem::path& dir, int n_msgs)
{
    rosbag2_storage::StorageOptions opts;
    opts.uri        = dir.string();
    opts.storage_id = "sqlite3";

    rosbag2_cpp::Writer writer;
    writer.open(opts);

    rosbag2_storage::TopicMetadata meta;
    meta.name                 = "/qa/float";
    meta.type                 = "std_msgs/msg/Float64";
    meta.serialization_format = "cdr";
    writer.create_topic(meta);

    for (int i = 0; i < n_msgs; ++i)
    {
        const double  value = static_cast<double>(i);
        const int64_t t_ns  = 1'000'000'000'000LL + static_cast<int64_t>(i) * 100'000'000LL;

        std::vector<uint8_t> cdr(12, 0);
        cdr[0] = 0x00;
        cdr[1] = 0x01;
        std::memcpy(cdr.data() + 4, &value, sizeof(double));

        auto msg        = std::make_shared<rosbag2_storage::SerializedBagMessage>();
        msg->topic_name = "/qa/float";
        bag_compat::set_bag_message_timestamp(*msg, t_ns);
        msg->serialized_data                  = std::make_shared<rcutils_uint8_array_t>();
        msg->serialized_data->allocator       = rcutils_get_default_allocator();
        msg->serialized_data->buffer_length   = cdr.size();
        msg->serialized_data->buffer_capacity = cdr.size();
        msg->serialized_data->buffer          = new uint8_t[cdr.size()];
        std::memcpy(msg->serialized_data->buffer, cdr.data(), cdr.size());
        writer.write(msg);
    }

    return dir.string();
}

size_t count_csv_lines(const std::string& path)
{
    std::ifstream in(path);
    size_t        lines = 0;
    std::string   line;
    while (std::getline(in, line))
        ++lines;
    return lines;
}

}   // namespace

TEST(RosBagAnalyze, ExportsFloat64Bag)
{
    const auto        dir   = std::filesystem::temp_directory_path() / "spectra_bag_analyze_test";
    constexpr int     kMsgs = 8;
    const std::string bag_path = write_float64_bag(dir, kMsgs);
    ASSERT_FALSE(bag_path.empty());

    const auto csv_path =
        (std::filesystem::temp_directory_path() / "spectra_analyze_out.csv").string();

    BagAnalyzeConfig cfg;
    cfg.bag_path = bag_path;
    cfg.fields   = {"/qa/float:data"};   // std_msgs/Float64 field
    cfg.csv_path = csv_path;

    const auto result = analyze_bag_to_csv(cfg);
    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.row_count, static_cast<size_t>(kMsgs));
    EXPECT_EQ(count_csv_lines(csv_path), kMsgs + 1);   // header + rows
}

TEST(RosBagAnalyze, MissingBagFails)
{
    BagAnalyzeConfig cfg;
    cfg.bag_path = "/nonexistent/bag.db3";
    cfg.fields   = {"/t:x"};
    cfg.csv_path = "/tmp/should_not_matter.csv";

    const auto result = analyze_bag_to_csv(cfg);
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.error.empty());
}

#endif   // SPECTRA_ROS2_BAG
