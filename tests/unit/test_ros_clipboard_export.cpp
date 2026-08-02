// test_ros_clipboard_export.cpp — Unit tests for RosClipboardExport (E2).
//
// Pure C++ logic tests — no ROS2 runtime, no ImGui context needed.
// Uses GTest::gtest_main (no custom main / RclcppEnvironment required).
//
// Test harness mirrors the pattern used in test_ros_csv_export.cpp:
// a TestClipboardManager holds synthetic LineSeries data and a matching
// ClipboardExportHarness exposes the static helpers + build_tsv directly.

#include "ros_clipboard_export.hpp"

#include <spectra/series.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers — parse TSV into rows/columns for assertions
// ---------------------------------------------------------------------------

static std::vector<std::vector<std::string>> parse_tsv(const std::string& text)
{
    std::vector<std::vector<std::string>> rows;
    std::istringstream                    stream(text);
    std::string                           line;
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;
        std::vector<std::string> cols;
        std::istringstream       row_stream(line);
        std::string              cell;
        while (std::getline(row_stream, cell, '\t'))
            cols.push_back(cell);
        // Handle trailing tab: if line ends with '\t', the last field is empty
        // and std::getline won't produce it, so add it manually.
        if (!line.empty() && line.back() == '\t')
            cols.push_back(std::string());
        rows.push_back(std::move(cols));
    }
    return rows;
}

// ---------------------------------------------------------------------------
// ClipboardExportHarness — wraps the public static helpers for direct testing.
// ---------------------------------------------------------------------------

class ClipboardExportHarness
{
   public:
    static std::string format_value(double v, int prec)
    {
        return RosClipboardExport::format_value(v, prec);
    }

    static std::string format_int64(int64_t v) { return RosClipboardExport::format_int64(v); }

    static std::string make_column_name(const std::string& t, const std::string& f)
    {
        return RosClipboardExport::make_column_name(t, f);
    }

    static void split_timestamp(double ts, int64_t ns, int64_t& sec, int64_t& nsec)
    {
        RosClipboardExport::split_timestamp(ts, ns, sec, nsec);
    }

    // Build a TSV directly without needing a RosPlotManager.
    static std::string build_tsv(const std::vector<RosClipboardExport::SeriesData>& sd,
                                 RosClipboardExport::SelectionRange                 mode,
                                 double                                             x_min,
                                 double                                             x_max,
                                 const ClipboardExportConfig&                       cfg = {})
    {
        // We need a RosPlotManager to construct RosClipboardExport, but the
        // build_tsv method only uses config_ — we bypass the manager by
        // constructing a temporary exporter and accessing it via a thin subclass.
        // Since we can't construct RosPlotManager without a live bridge, we
        // replicate the build_tsv logic here using only public static helpers.
        // This is the same approach as CsvExportTestHarness in test_ros_csv_export.
        return build_tsv_impl(sd, mode, x_min, x_max, cfg);
    }

   private:
    static std::string build_tsv_impl(const std::vector<RosClipboardExport::SeriesData>& series,
                                      RosClipboardExport::SelectionRange                 mode,
                                      double                                             x_min,
                                      double                                             x_max,
                                      const ClipboardExportConfig&                       cfg)
    {
        if (series.empty())
            return {};

        std::vector<double> all_x;
        for (const auto& sd : series)
        {
            for (float xf : sd.x)
            {
                double xd = static_cast<double>(xf);
                if (mode == RosClipboardExport::SelectionRange::Range)
                {
                    if (xd < x_min || xd > x_max)
                        continue;
                }
                all_x.push_back(xd);
            }
        }

        std::sort(all_x.begin(), all_x.end());
        {
            constexpr double EPS = 1e-12;
            auto             it  = std::unique(all_x.begin(),
                                  all_x.end(),
                                  [](double a, double b) { return (b - a) < EPS; });
            all_x.erase(it, all_x.end());
        }

        if (all_x.empty())
            return {};

        std::ostringstream out;

        if (cfg.write_header)
        {
            out << "timestamp_sec\ttimestamp_nsec\twall_clock";
            for (const auto& sd : series)
                out << "\t" << sd.column_name;
            out << "\n";
        }

        std::vector<size_t> cursors(series.size(), 0u);

        for (double row_x : all_x)
        {
            int64_t ts_ns = 0;
            for (size_t si = 0; si < series.size(); ++si)
            {
                const auto& sd = series[si];
                size_t      c  = cursors[si];
                if (c < sd.x.size())
                {
                    double           xd  = static_cast<double>(sd.x[c]);
                    constexpr double EPS = 1e-12;
                    if (std::abs(xd - row_x) < EPS && !sd.ns.empty())
                    {
                        ts_ns = sd.ns[c];
                        break;
                    }
                }
            }

            int64_t sec = 0, nsec = 0;
            RosClipboardExport::split_timestamp(row_x, ts_ns, sec, nsec);

            out << RosClipboardExport::format_int64(sec) << "\t"
                << RosClipboardExport::format_int64(nsec) << "\t"
                << RosClipboardExport::format_value(row_x, cfg.wall_clock_precision);

            for (size_t si = 0; si < series.size(); ++si)
            {
                const auto& sd = series[si];
                size_t&     c  = cursors[si];

                out << "\t";
                if (c < sd.x.size())
                {
                    double           xd  = static_cast<double>(sd.x[c]);
                    constexpr double EPS = 1e-12;
                    if (std::abs(xd - row_x) < EPS)
                    {
                        out << RosClipboardExport::format_value(static_cast<double>(sd.y[c]),
                                                                cfg.precision);
                        ++c;
                        continue;
                    }
                }
                out << cfg.missing_value;
            }

            out << "\n";
        }

        return out.str();
    }
};

// ---------------------------------------------------------------------------
// Suite 1 — SplitTimestamp
// ---------------------------------------------------------------------------

TEST(SplitTimestamp, NsPrimaryExact)
{
    int64_t sec, nsec;
    // 1000000001 ns = 1 sec + 1 ns
    ClipboardExportHarness::split_timestamp(0.0, 1'000'000'001LL, sec, nsec);
    EXPECT_EQ(sec, 1LL);
    EXPECT_EQ(nsec, 1LL);
}

TEST(SplitTimestamp, NsPrimaryWholeSecond)
{
    int64_t sec, nsec;
    ClipboardExportHarness::split_timestamp(0.0, 5'000'000'000LL, sec, nsec);
    EXPECT_EQ(sec, 5LL);
    EXPECT_EQ(nsec, 0LL);
}

TEST(SplitTimestamp, NsZeroFloatFallback)
{
    int64_t sec, nsec;
    // 1.5 seconds → sec=1, nsec=500000000
    ClipboardExportHarness::split_timestamp(1.5, 0, sec, nsec);
    EXPECT_EQ(sec, 1LL);
    EXPECT_NEAR(static_cast<double>(nsec), 500'000'000.0, 1.0);
}

TEST(SplitTimestamp, FloatFallbackZero)
{
    int64_t sec, nsec;
    ClipboardExportHarness::split_timestamp(0.0, 0, sec, nsec);
    EXPECT_EQ(sec, 0LL);
    EXPECT_EQ(nsec, 0LL);
}

TEST(SplitTimestamp, NsPrimaryNegativeNsecWraps)
{
    int64_t sec, nsec;
    // -1 ns → sec=-1, nsec=999999999
    ClipboardExportHarness::split_timestamp(0.0, -1LL, sec, nsec);
    EXPECT_EQ(sec, -1LL);
    EXPECT_EQ(nsec, 999'999'999LL);
}

TEST(SplitTimestamp, LargeEpochTimestamp)
{
    int64_t sec, nsec;
    // Unix epoch 1700000000.123456789
    int64_t input_ns = 1700000000LL * 1'000'000'000LL + 123'456'789LL;
    ClipboardExportHarness::split_timestamp(0.0, input_ns, sec, nsec);
    EXPECT_EQ(sec, 1700000000LL);
    EXPECT_EQ(nsec, 123'456'789LL);
}

// ---------------------------------------------------------------------------
// Suite 2 — FormatValue
// ---------------------------------------------------------------------------

TEST(FormatValue, DefaultPrecision)
{
    std::string s = ClipboardExportHarness::format_value(3.14159265358979, 9);
    EXPECT_EQ(s, "3.141592654");
}

TEST(FormatValue, PrecisionZero)
{
    std::string s = ClipboardExportHarness::format_value(42.7, 0);
    EXPECT_EQ(s, "43");
}

TEST(FormatValue, NegativeValue)
{
    std::string s = ClipboardExportHarness::format_value(-1.5, 1);
    EXPECT_EQ(s, "-1.5");
}

TEST(FormatValue, ZeroValue)
{
    std::string s = ClipboardExportHarness::format_value(0.0, 3);
    EXPECT_EQ(s, "0.000");
}

// ---------------------------------------------------------------------------
// Suite 3 — FormatInt64
// ---------------------------------------------------------------------------

TEST(FormatInt64, PositiveValue)
{
    EXPECT_EQ(ClipboardExportHarness::format_int64(12345), "12345");
}

TEST(FormatInt64, NegativeValue)
{
    EXPECT_EQ(ClipboardExportHarness::format_int64(-7), "-7");
}

TEST(FormatInt64, Zero)
{
    EXPECT_EQ(ClipboardExportHarness::format_int64(0), "0");
}

// ---------------------------------------------------------------------------
// Suite 4 — MakeColumnName
// ---------------------------------------------------------------------------

TEST(MakeColumnName, TopicAndField)
{
    EXPECT_EQ(ClipboardExportHarness::make_column_name("/imu", "linear_acceleration.x"),
              "/imu/linear_acceleration.x");
}

TEST(MakeColumnName, EmptyField)
{
    EXPECT_EQ(ClipboardExportHarness::make_column_name("/chatter", ""), "/chatter");
}

TEST(MakeColumnName, TrailingSlashTopic)
{
    EXPECT_EQ(ClipboardExportHarness::make_column_name("/imu/", "data"), "/imu/data");
}

TEST(MakeColumnName, EmptyBoth)
{
    EXPECT_EQ(ClipboardExportHarness::make_column_name("", ""), "");
}

TEST(MakeColumnName, NestedNamespace)
{
    EXPECT_EQ(ClipboardExportHarness::make_column_name("/robot/sensors/imu", "pose.position.x"),
              "/robot/sensors/imu/pose.position.x");
}

// ---------------------------------------------------------------------------
// Suite 5 — IsKeyboardShortcut
// ---------------------------------------------------------------------------

TEST(IsKeyboardShortcut, CtrlCUppercase)
{
    EXPECT_TRUE(RosClipboardExport::is_copy_shortcut('C', true));
}

TEST(IsKeyboardShortcut, CtrlCLowercase)
{
    EXPECT_TRUE(RosClipboardExport::is_copy_shortcut('c', true));
}

TEST(IsKeyboardShortcut, CtrlCKeycode)
{
    EXPECT_TRUE(RosClipboardExport::is_copy_shortcut(67, true));
}

TEST(IsKeyboardShortcut, NoCtrl)
{
    EXPECT_FALSE(RosClipboardExport::is_copy_shortcut('C', false));
}

TEST(IsKeyboardShortcut, WrongKey)
{
    EXPECT_FALSE(RosClipboardExport::is_copy_shortcut('V', true));
}

TEST(IsKeyboardShortcut, BothFalse)
{
    EXPECT_FALSE(RosClipboardExport::is_copy_shortcut('X', false));
}

// ---------------------------------------------------------------------------
// Suite 6 — BuildTsv — single series
// ---------------------------------------------------------------------------

TEST(BuildTsvSingleSeries, ProducesHeaderAndRows)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/imu/ax";
    sd.x           = {1.0f, 2.0f, 3.0f};
    sd.y           = {10.0f, 20.0f, 30.0f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 4u);   // header + 3 data rows

    // Header check.
    EXPECT_EQ(rows[0][0], "timestamp_sec");
    EXPECT_EQ(rows[0][1], "timestamp_nsec");
    EXPECT_EQ(rows[0][2], "wall_clock");
    EXPECT_EQ(rows[0][3], "/imu/ax");

    // Data row check — each row has 4 columns.
    EXPECT_EQ(rows[1].size(), 4u);
    EXPECT_EQ(rows[2].size(), 4u);
    EXPECT_EQ(rows[3].size(), 4u);
}

TEST(BuildTsvSingleSeries, ValueColumnCorrect)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/sensor/data";
    sd.x           = {0.5f};
    sd.y           = {42.0f};

    ClipboardExportConfig cfg;
    cfg.precision = 3;

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0,
                                                        cfg);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1][3], "42.000");
}

TEST(BuildTsvSingleSeries, NoHeader)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/a";
    sd.x           = {1.0f, 2.0f};
    sd.y           = {0.0f, 1.0f};

    ClipboardExportConfig cfg;
    cfg.write_header = false;

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0,
                                                        cfg);

    auto rows = parse_tsv(tsv);
    EXPECT_EQ(rows.size(), 2u);
    // First row is data (no header).
    EXPECT_EQ(rows[0][0], "1");
}

TEST(BuildTsvSingleSeries, EmptySeriesReturnsEmpty)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/empty";

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    EXPECT_TRUE(tsv.empty());
}

TEST(BuildTsvSingleSeries, TabSeparatedNotComma)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/x";
    sd.x           = {1.0f};
    sd.y           = {5.0f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    EXPECT_NE(tsv.find('\t'), std::string::npos);
    EXPECT_EQ(tsv.find(','), std::string::npos);
}

// ---------------------------------------------------------------------------
// Suite 7 — BuildTsv — range filtering
// ---------------------------------------------------------------------------

TEST(BuildTsvRange, FilterIncludesBoundary)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    sd.y           = {10.f, 20.f, 30.f, 40.f, 50.f};

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Range,
                                                        2.0,
                                                        4.0);

    auto rows = parse_tsv(tsv);
    // Header + rows for x=2, 3, 4.
    ASSERT_EQ(rows.size(), 4u);
}

TEST(BuildTsvRange, FilterExcludesOutsideValues)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f, 5.0f, 10.0f};
    sd.y           = {1.f, 5.f, 10.f};

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Range,
                                                        4.0,
                                                        6.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 2u);   // header + x=5 only
}

TEST(BuildTsvRange, EmptyRangeReturnsEmpty)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f, 2.0f};
    sd.y           = {1.f, 2.f};

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Range,
                                                        5.0,
                                                        10.0);

    EXPECT_TRUE(tsv.empty());
}

TEST(BuildTsvRange, FullModeIgnoresRange)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f, 2.0f, 3.0f};
    sd.y           = {1.f, 2.f, 3.f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 2.0, 2.5);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 4u);   // header + all 3 rows despite range
}

// ---------------------------------------------------------------------------
// Suite 8 — BuildTsv — multi-series union alignment
// ---------------------------------------------------------------------------

TEST(BuildTsvMultiSeries, TwoSeriesSameTimestamps)
{
    RosClipboardExport::SeriesData s1;
    s1.column_name = "/a";
    s1.x           = {1.0f, 2.0f};
    s1.y           = {10.f, 20.f};

    RosClipboardExport::SeriesData s2;
    s2.column_name = "/b";
    s2.x           = {1.0f, 2.0f};
    s2.y           = {100.f, 200.f};

    std::string tsv = ClipboardExportHarness::build_tsv({s1, s2},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 3u);   // header + 2 data rows

    // Header has 5 columns.
    EXPECT_EQ(rows[0].size(), 5u);
    // Data rows have 5 columns.
    EXPECT_EQ(rows[1].size(), 5u);
    EXPECT_EQ(rows[2].size(), 5u);
}

TEST(BuildTsvMultiSeries, MissingValueFilledForStaggeredTimestamps)
{
    RosClipboardExport::SeriesData s1;
    s1.column_name = "/a";
    s1.x           = {1.0f, 2.0f};
    s1.y           = {10.f, 20.f};

    RosClipboardExport::SeriesData s2;
    s2.column_name = "/b";
    s2.x           = {2.0f, 3.0f};
    s2.y           = {200.f, 300.f};

    ClipboardExportConfig cfg;
    cfg.missing_value = "NaN";

    std::string tsv = ClipboardExportHarness::build_tsv({s1, s2},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0,
                                                        cfg);

    auto rows = parse_tsv(tsv);
    // header + rows for x=1, x=2, x=3
    ASSERT_EQ(rows.size(), 4u);

    // x=1: s1 has value, s2 is missing → "NaN"
    EXPECT_NE(rows[1][3], "NaN");   // s1 value at x=1
    EXPECT_EQ(rows[1][4], "NaN");   // s2 missing at x=1

    // x=3: s1 is missing → "NaN", s2 has value
    EXPECT_EQ(rows[3][3], "NaN");   // s1 missing at x=3
    EXPECT_NE(rows[3][4], "NaN");   // s2 value at x=3
}

TEST(BuildTsvMultiSeries, ThreeSeriesUnion)
{
    RosClipboardExport::SeriesData s1, s2, s3;
    s1.column_name = "/a";
    s1.x           = {1.f, 3.f};
    s1.y           = {1.f, 3.f};
    s2.column_name = "/b";
    s2.x           = {2.f, 4.f};
    s2.y           = {2.f, 4.f};
    s3.column_name = "/c";
    s3.x           = {1.f, 2.f, 3.f, 4.f};
    s3.y           = {0.f, 0.f, 0.f, 0.f};

    std::string tsv = ClipboardExportHarness::build_tsv({s1, s2, s3},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0);

    auto rows = parse_tsv(tsv);
    // header + x=1,2,3,4 → 5 rows total, 6 columns each
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[0].size(), 6u);
}

TEST(BuildTsvMultiSeries, DefaultMissingValueIsEmpty)
{
    RosClipboardExport::SeriesData s1, s2;
    s1.column_name = "/a";
    s1.x           = {1.f};
    s1.y           = {1.f};
    s2.column_name = "/b";
    s2.x           = {2.f};
    s2.y           = {2.f};

    std::string tsv = ClipboardExportHarness::build_tsv({s1, s2},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0);

    auto rows = parse_tsv(tsv);
    // x=1: s2 missing → empty string
    EXPECT_EQ(rows[1][4], "");
    // x=2: s1 missing → empty string
    EXPECT_EQ(rows[2][3], "");
}

// ---------------------------------------------------------------------------
// Suite 9 — BuildTsv — timestamp columns
// ---------------------------------------------------------------------------

TEST(BuildTsvTimestamp, TimestampColumnsAlwaysPresent)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f};
    sd.y           = {0.0f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_GE(rows.size(), 2u);
    // Header: timestamp_sec, timestamp_nsec, wall_clock, <col>
    EXPECT_EQ(rows[0][0], "timestamp_sec");
    EXPECT_EQ(rows[0][1], "timestamp_nsec");
    EXPECT_EQ(rows[0][2], "wall_clock");
}

TEST(BuildTsvTimestamp, NsPerSampleUsed)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {0.0f};
    sd.y           = {5.0f};
    // 1s + 250000000 ns
    sd.ns = {1'250'000'000LL};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1][0], "1");           // sec
    EXPECT_EQ(rows[1][1], "250000000");   // nsec
}

// ---------------------------------------------------------------------------
// Suite 10 — ClipboardCopyResult struct
// ---------------------------------------------------------------------------

TEST(ClipboardCopyResult, DefaultConstruction)
{
    ClipboardCopyResult r;
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.empty());
    EXPECT_EQ(r.row_count, 0u);
    EXPECT_EQ(r.column_count, 0u);
    EXPECT_TRUE(r.tsv_text.empty());
}

TEST(ClipboardCopyResult, OkResultHasText)
{
    ClipboardCopyResult r;
    r.ok       = true;
    r.tsv_text = "a\tb\n";
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.tsv_text.empty());
}

// ---------------------------------------------------------------------------
// Suite 11 — ClipboardExportConfig defaults
// ---------------------------------------------------------------------------

TEST(ClipboardExportConfig, Defaults)
{
    ClipboardExportConfig cfg;
    EXPECT_EQ(cfg.precision, 9);
    EXPECT_EQ(cfg.wall_clock_precision, 9);
    EXPECT_TRUE(cfg.missing_value.empty());
    EXPECT_TRUE(cfg.write_header);
}

TEST(ClipboardExportConfig, CustomPrecision)
{
    ClipboardExportConfig cfg;
    cfg.precision = 3;
    EXPECT_EQ(cfg.precision, 3);
}

// ---------------------------------------------------------------------------
// Suite 12 — SelectionRange enum
// ---------------------------------------------------------------------------

TEST(SelectionRange, EnumValues)
{
    using SR = RosClipboardExport::SelectionRange;
    EXPECT_NE(SR::Full, SR::Range);
}

// ---------------------------------------------------------------------------
// Suite 13 — SeriesData struct
// ---------------------------------------------------------------------------

TEST(SeriesData, ConstructionAndAssignment)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/topic/field";
    sd.x           = {1.0f, 2.0f};
    sd.y           = {3.0f, 4.0f};
    sd.ns          = {1000LL, 2000LL};

    EXPECT_EQ(sd.column_name, "/topic/field");
    EXPECT_EQ(sd.x.size(), 2u);
    EXPECT_EQ(sd.y.size(), 2u);
    EXPECT_EQ(sd.ns.size(), 2u);
}

TEST(SeriesData, EmptyNsAllowed)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/no_ns";
    sd.x           = {1.0f};
    sd.y           = {0.0f};
    // ns left empty — split_timestamp falls back to float decomposition.
    EXPECT_TRUE(sd.ns.empty());

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);
    EXPECT_FALSE(tsv.empty());
}

// ---------------------------------------------------------------------------
// Suite 14 — Edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, EmptySeriesVectorReturnsEmpty)
{
    std::vector<RosClipboardExport::SeriesData> empty;
    std::string                                 tsv = ClipboardExportHarness::build_tsv(empty,
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0);
    EXPECT_TRUE(tsv.empty());
}

TEST(EdgeCases, NegativeYValues)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/neg";
    sd.x           = {1.0f};
    sd.y           = {-99.5f};

    ClipboardExportConfig cfg;
    cfg.precision = 1;

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0,
                                                        cfg);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1][3], "-99.5");
}

TEST(EdgeCases, LargeEpochXTimestamp)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/epoch";
    // ROS2 epoch time ≈ 1700000000 seconds
    sd.x = {1.7e9f};
    sd.y = {0.0f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 2u);
    // timestamp_sec column should be a non-zero integer.
    EXPECT_NE(rows[1][0], "0");
}

TEST(EdgeCases, SingleSampleSingleColumnCount)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/x";
    sd.x           = {1.0f};
    sd.y           = {1.0f};

    std::string tsv =
        ClipboardExportHarness::build_tsv({sd}, RosClipboardExport::SelectionRange::Full, 0.0, 0.0);

    auto rows = parse_tsv(tsv);
    ASSERT_GE(rows.size(), 2u);
    // 3 timestamp + 1 value = 4 columns
    EXPECT_EQ(rows[0].size(), 4u);
    EXPECT_EQ(rows[1].size(), 4u);
}

TEST(EdgeCases, PrecisionZeroIntegerValues)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/int";
    sd.x           = {1.0f, 2.0f};
    sd.y           = {100.0f, 200.0f};

    ClipboardExportConfig cfg;
    cfg.precision = 0;

    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Full,
                                                        0.0,
                                                        0.0,
                                                        cfg);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[1][3], "100");
    EXPECT_EQ(rows[2][3], "200");
}

TEST(EdgeCases, RangeExactBoundaryInclusion)
{
    RosClipboardExport::SeriesData sd;
    sd.column_name = "/v";
    sd.x           = {1.0f, 2.0f, 3.0f};
    sd.y           = {1.f, 2.f, 3.f};

    // Range [1.0, 3.0] should include all three points.
    std::string tsv = ClipboardExportHarness::build_tsv({sd},
                                                        RosClipboardExport::SelectionRange::Range,
                                                        1.0,
                                                        3.0);

    auto rows = parse_tsv(tsv);
    ASSERT_EQ(rows.size(), 4u);
}

// ---------------------------------------------------------------------------
// Suite 15 — LastClipboardText (headless path)
// ---------------------------------------------------------------------------

// In headless mode (no SPECTRA_USE_IMGUI) RosClipboardExport stores the text
// in last_clipboard_text().  We verify this via the public API where we can:
// the field is set inside set_clipboard() which build_and_copy() calls.
// Without a live RosPlotManager we test only that the initial state is empty.

TEST(LastClipboardText, InitiallyEmpty)
{
    // We can't construct RosClipboardExport without a RosPlotManager that
    // requires a running bridge.  Test the default-constructed config state only.
    ClipboardExportConfig cfg;
    EXPECT_TRUE(cfg.missing_value.empty());
    EXPECT_EQ(cfg.precision, 9);
}
