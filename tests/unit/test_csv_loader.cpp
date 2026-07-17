#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "ui/data/csv_loader.hpp"

namespace
{

class TempCsvFile
{
   public:
    explicit TempCsvFile(const std::string& contents)
        : path_(std::filesystem::temp_directory_path()
                / ("spectra_csv_loader_" + std::to_string(unique_suffix_++) + ".csv"))
    {
        std::ofstream out(path_);
        out << contents;
    }

    ~TempCsvFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempCsvFile(const TempCsvFile&)            = delete;
    TempCsvFile& operator=(const TempCsvFile&) = delete;
    TempCsvFile(TempCsvFile&&)                 = delete;
    TempCsvFile& operator=(TempCsvFile&&)      = delete;

    std::string path() const { return path_.string(); }

   private:
    std::filesystem::path  path_;
    static inline uint64_t unique_suffix_ =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
};

}   // namespace

TEST(CsvLoader, ParsesDatetimeColumnAsRelativeSeconds)
{
    TempCsvFile file("time,value\n"
                     "2026-03-15T20:47:00Z,1\n"
                     "2026-03-15T20:47:01.500Z,2\n"
                     "2026-03-15T20:47:03Z,3\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_cols, 2u);
    ASSERT_EQ(data.num_rows, 3u);
    ASSERT_EQ(data.headers.size(), 2u);
    EXPECT_EQ(data.headers[0], "time");
    EXPECT_EQ(data.headers[1], "value");
    ASSERT_EQ(data.columns[0].size(), 3u);
    ASSERT_EQ(data.columns[1].size(), 3u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.5f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 3.0f, 1e-5f);
    EXPECT_FLOAT_EQ(data.columns[1][0], 1.0f);
    EXPECT_FLOAT_EQ(data.columns[1][1], 2.0f);
    EXPECT_FLOAT_EQ(data.columns[1][2], 3.0f);
}

TEST(CsvLoader, DoesNotTreatDatetimeFirstRowAsHeader)
{
    TempCsvFile file("2026-03-15T20:47:00+02:00,1\n"
                     "2026-03-15T18:47:02Z,2\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_cols, 2u);
    ASSERT_EQ(data.num_rows, 2u);
    ASSERT_EQ(data.headers.size(), 2u);
    EXPECT_EQ(data.headers[0], "Column 1");
    EXPECT_EQ(data.headers[1], "Column 2");
    ASSERT_EQ(data.columns[0].size(), 2u);
    ASSERT_EQ(data.columns[1].size(), 2u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 2.0f, 1e-5f);
    EXPECT_FLOAT_EQ(data.columns[1][0], 1.0f);
    EXPECT_FLOAT_EQ(data.columns[1][1], 2.0f);
}

TEST(CsvLoader, SupportsAdditionalCommonDatetimeFormats)
{
    TempCsvFile file("time;value\n"
                     "2026-03-04_15-09-55_718414;1\n"
                     "2026-03-04T15:09:56.718414;2\n"
                     "2026/03/04 15:09:57,718414;3\n"
                     "20260304 150958.718414;4\n"
                     "20260304150959.718414;5\n"
                     "2026.03.04 15:10:00;6\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_rows, 6u);
    ASSERT_EQ(data.columns[0].size(), 6u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 2.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][3], 3.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][4], 4.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][5], 4.281586f, 1e-5f);
}

TEST(CsvLoader, PreservesLargeNumericTimestampsViaColumnOffset)
{
    // Epoch-style timestamps (~1.7e9) exceed float's ~7-digit precision.
    // The loader stores them as small relative floats plus a double base in
    // column_offsets, so the absolute value is fully recoverable.
    TempCsvFile file("ros_time_s,value\n"
                     "1783350621.752999,1\n"
                     "1783350621.772817,2\n"
                     "1783350621.792635,3\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_cols, 2u);
    ASSERT_EQ(data.num_rows, 3u);
    ASSERT_EQ(data.columns[0].size(), 3u);
    ASSERT_EQ(data.column_offsets.size(), 2u);
    // Base offset holds the first absolute value at double precision
    EXPECT_NEAR(data.column_offsets[0], 1783350621.752999, 1e-6);
    // Stored floats are relative: first value 0, sub-second diffs preserved
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-4f);
    EXPECT_NEAR(data.columns[0][1], 0.019818f, 1e-4f);
    EXPECT_NEAR(data.columns[0][2], 0.039636f, 1e-4f);
    // Absolute value round-trips: offset + float ≈ original
    EXPECT_NEAR(data.column_offsets[0] + static_cast<double>(data.columns[0][2]),
                1783350621.792635,
                1e-4);
    // Y column unaffected (small values, zero offset)
    EXPECT_EQ(data.column_offsets[1], 0.0);
    EXPECT_FLOAT_EQ(data.columns[1][0], 1.0f);
    EXPECT_FLOAT_EQ(data.columns[1][1], 2.0f);
    EXPECT_FLOAT_EQ(data.columns[1][2], 3.0f);
}

TEST(CsvLoader, DatetimeColumnRecordsEpochBaseInColumnOffsets)
{
    TempCsvFile file("time,value\n"
                     "2026-03-15T20:47:00Z,1\n"
                     "2026-03-15T20:47:01.500Z,2\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.column_offsets.size(), 2u);
    // Base is the epoch seconds of the first datetime (non-zero)
    EXPECT_GT(data.column_offsets[0], 1e9);
    EXPECT_EQ(data.column_offsets[1], 0.0);
}

TEST(CsvLoader, DoesNotNormalizeSmallNumericValues)
{
    // Values below 1e6 should be stored as-is with zero offset.
    TempCsvFile file("x,y\n"
                     "100,1\n"
                     "200,2\n"
                     "300,3\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_rows, 3u);
    ASSERT_EQ(data.column_offsets.size(), 2u);
    EXPECT_EQ(data.column_offsets[0], 0.0);
    EXPECT_FLOAT_EQ(data.columns[0][0], 100.0f);
    EXPECT_FLOAT_EQ(data.columns[0][1], 200.0f);
    EXPECT_FLOAT_EQ(data.columns[0][2], 300.0f);
}

TEST(CsvLoader, DoesNotNormalizeLargeNonTimeMeasurements)
{
    TempCsvFile file("sample,pressure_pa\n"
                     "1,101325000.25\n"
                     "2,101325010.50\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.column_offsets.size(), 2u);
    EXPECT_EQ(data.column_offsets[0], 0.0);
    EXPECT_EQ(data.column_offsets[1], 0.0);
    EXPECT_GT(data.columns[1][0], 1.0e8f);
}

TEST(CsvLoader, SupportsTimezoneVariants)
{
    TempCsvFile file("time,value\n"
                     "2026-03-04_15-09-55_718414+02:00,1\n"
                     "2026-03-04T13:09:56.718414Z,2\n"
                     "2026-03-04 13:09:57.718414-00:00,3\n"
                     "2026-03-04 15:09:58.718414+0200,4\n"
                     "2026-03-04 15:09:59.718414+02_00,5\n");

    const spectra::CsvData data = spectra::parse_csv(file.path());

    ASSERT_TRUE(data.error.empty());
    ASSERT_EQ(data.num_rows, 5u);
    ASSERT_EQ(data.columns[0].size(), 5u);
    EXPECT_NEAR(data.columns[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][1], 1.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][2], 2.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][3], 3.0f, 1e-5f);
    EXPECT_NEAR(data.columns[0][4], 4.0f, 1e-5f);
}
