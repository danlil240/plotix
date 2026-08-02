#define _USE_MATH_DEFINES
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "math/data_transform.hpp"

using namespace spectra;

// ─── Helper ─────────────────────────────────────────────────────────────────

static std::vector<float> make_x(size_t n)
{
    std::vector<float> x(n);
    std::iota(x.begin(), x.end(), 0.0f);
    return x;
}

static std::vector<float> make_y(std::initializer_list<float> vals)
{
    return std::vector<float>(vals);
}

// ═══════════════════════════════════════════════════════════════════════════
// Identity
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformIdentity, Passthrough)
{
    auto               x = make_x(5);
    auto               y = make_y({1, 2, 3, 4, 5});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Identity);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(xo.size(), 5u);
    ASSERT_EQ(yo.size(), 5u);
    for (size_t i = 0; i < 5; ++i)
    {
        EXPECT_FLOAT_EQ(xo[i], x[i]);
        EXPECT_FLOAT_EQ(yo[i], y[i]);
    }
}

TEST(DataTransformIdentity, Empty)
{
    std::vector<float> x, y, xo, yo;
    DataTransform      t(TransformType::Identity);
    t.apply_y(x, y, xo, yo);
    EXPECT_TRUE(xo.empty());
    EXPECT_TRUE(yo.empty());
}

TEST(DataTransformIdentity, ScalarPassthrough)
{
    DataTransform t(TransformType::Identity);
    EXPECT_FLOAT_EQ(t.apply_scalar(42.0f), 42.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Log10
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformLog10, PositiveValues)
{
    auto               x = make_x(4);
    auto               y = make_y({1, 10, 100, 1000});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Log10);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 4u);
    EXPECT_NEAR(yo[0], 0.0f, 1e-6f);
    EXPECT_NEAR(yo[1], 1.0f, 1e-6f);
    EXPECT_NEAR(yo[2], 2.0f, 1e-6f);
    EXPECT_NEAR(yo[3], 3.0f, 1e-6f);
}

TEST(DataTransformLog10, SkipsNonPositive)
{
    auto               x = make_x(5);
    auto               y = make_y({-1, 0, 1, 10, 100});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Log10);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);   // Only 1, 10, 100
    EXPECT_NEAR(yo[0], 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(xo[0], 2.0f);   // x index of value 1
}

TEST(DataTransformLog10, ScalarPositive)
{
    DataTransform t(TransformType::Log10);
    EXPECT_NEAR(t.apply_scalar(100.0f), 2.0f, 1e-6f);
}

TEST(DataTransformLog10, ScalarNonPositive)
{
    DataTransform t(TransformType::Log10);
    EXPECT_TRUE(std::isnan(t.apply_scalar(-1.0f)));
    EXPECT_TRUE(std::isnan(t.apply_scalar(0.0f)));
}

// ═══════════════════════════════════════════════════════════════════════════
// Ln
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformLn, PositiveValues)
{
    auto               x = make_x(3);
    auto               y = make_y({1.0f, static_cast<float>(M_E), static_cast<float>(M_E * M_E)});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Ln);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_NEAR(yo[0], 0.0f, 1e-5f);
    EXPECT_NEAR(yo[1], 1.0f, 1e-5f);
    EXPECT_NEAR(yo[2], 2.0f, 1e-4f);
}

TEST(DataTransformLn, SkipsNonPositive)
{
    auto               x = make_x(3);
    auto               y = make_y({-5.0f, 0.0f, 1.0f});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Ln);
    t.apply_y(x, y, xo, yo);
    ASSERT_EQ(yo.size(), 1u);
}

// ═══════════════════════════════════════════════════════════════════════════
// Abs
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformAbs, MixedValues)
{
    auto               x = make_x(5);
    auto               y = make_y({-3, -1, 0, 1, 3});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Abs);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 5u);
    EXPECT_FLOAT_EQ(yo[0], 3.0f);
    EXPECT_FLOAT_EQ(yo[1], 1.0f);
    EXPECT_FLOAT_EQ(yo[2], 0.0f);
    EXPECT_FLOAT_EQ(yo[3], 1.0f);
    EXPECT_FLOAT_EQ(yo[4], 3.0f);
}

TEST(DataTransformAbs, Scalar)
{
    DataTransform t(TransformType::Abs);
    EXPECT_FLOAT_EQ(t.apply_scalar(-7.0f), 7.0f);
    EXPECT_FLOAT_EQ(t.apply_scalar(7.0f), 7.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Negate
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformNegate, Basic)
{
    auto               x = make_x(3);
    auto               y = make_y({1, -2, 3});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Negate);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], -1.0f);
    EXPECT_FLOAT_EQ(yo[1], 2.0f);
    EXPECT_FLOAT_EQ(yo[2], -3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Normalize
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformNormalize, BasicRange)
{
    auto               x = make_x(5);
    auto               y = make_y({0, 25, 50, 75, 100});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Normalize);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 5u);
    EXPECT_NEAR(yo[0], 0.0f, 1e-6f);
    EXPECT_NEAR(yo[1], 0.25f, 1e-6f);
    EXPECT_NEAR(yo[2], 0.5f, 1e-6f);
    EXPECT_NEAR(yo[3], 0.75f, 1e-6f);
    EXPECT_NEAR(yo[4], 1.0f, 1e-6f);
}

TEST(DataTransformNormalize, ConstantValue)
{
    auto               x = make_x(3);
    auto               y = make_y({5, 5, 5});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Normalize);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], 0.5f);
    EXPECT_FLOAT_EQ(yo[1], 0.5f);
    EXPECT_FLOAT_EQ(yo[2], 0.5f);
}

TEST(DataTransformNormalize, NegativeRange)
{
    auto               x = make_x(3);
    auto               y = make_y({-10, 0, 10});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Normalize);
    t.apply_y(x, y, xo, yo);

    EXPECT_NEAR(yo[0], 0.0f, 1e-6f);
    EXPECT_NEAR(yo[1], 0.5f, 1e-6f);
    EXPECT_NEAR(yo[2], 1.0f, 1e-6f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Standardize
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformStandardize, ZeroMeanUnitVariance)
{
    auto               x = make_x(4);
    auto               y = make_y({2, 4, 6, 8});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Standardize);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 4u);

    // Mean should be ~0
    double mean = 0;
    for (float v : yo)
        mean += v;
    mean /= yo.size();
    EXPECT_NEAR(mean, 0.0, 1e-5);

    // Stddev should be ~1
    double var = 0;
    for (float v : yo)
        var += (v - mean) * (v - mean);
    double stddev = std::sqrt(var / yo.size());
    EXPECT_NEAR(stddev, 1.0, 1e-5);
}

TEST(DataTransformStandardize, ConstantValue)
{
    auto               x = make_x(3);
    auto               y = make_y({7, 7, 7});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Standardize);
    t.apply_y(x, y, xo, yo);

    for (float v : yo)
        EXPECT_FLOAT_EQ(v, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Derivative
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformDerivative, LinearFunction)
{
    // y = 2x → dy/dx = 2
    auto               x = make_x(5);   // 0, 1, 2, 3, 4
    auto               y = make_y({0, 2, 4, 6, 8});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Derivative);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 4u);   // n-1 points
    for (float v : yo)
    {
        EXPECT_NEAR(v, 2.0f, 1e-6f);
    }
}

TEST(DataTransformDerivative, MidpointX)
{
    auto               x = make_x(3);   // 0, 1, 2
    auto               y = make_y({0, 1, 4});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Derivative);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(xo.size(), 2u);
    EXPECT_FLOAT_EQ(xo[0], 0.5f);   // Midpoint of 0 and 1
    EXPECT_FLOAT_EQ(xo[1], 1.5f);   // Midpoint of 1 and 2
}

TEST(DataTransformDerivative, TooFewPoints)
{
    std::vector<float> x = {1.0f};
    std::vector<float> y = {5.0f};
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Derivative);
    t.apply_y(x, y, xo, yo);
    EXPECT_TRUE(xo.empty());
    EXPECT_TRUE(yo.empty());
}

TEST(DataTransformDerivative, EmptyInput)
{
    std::vector<float> x, y, xo, yo;
    DataTransform      t(TransformType::Derivative);
    t.apply_y(x, y, xo, yo);
    EXPECT_TRUE(xo.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// CumulativeSum
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformCumulativeSum, Basic)
{
    auto               x = make_x(5);
    auto               y = make_y({1, 2, 3, 4, 5});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::CumulativeSum);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 5u);
    EXPECT_FLOAT_EQ(yo[0], 1.0f);
    EXPECT_FLOAT_EQ(yo[1], 3.0f);
    EXPECT_FLOAT_EQ(yo[2], 6.0f);
    EXPECT_FLOAT_EQ(yo[3], 10.0f);
    EXPECT_FLOAT_EQ(yo[4], 15.0f);
}

TEST(DataTransformCumulativeSum, PreservesX)
{
    auto               x = make_x(3);
    auto               y = make_y({1, 1, 1});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::CumulativeSum);
    t.apply_y(x, y, xo, yo);

    for (size_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(xo[i], x[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Diff
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformDiff, Basic)
{
    auto               x = make_x(5);
    auto               y = make_y({1, 3, 6, 10, 15});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Diff);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 4u);
    EXPECT_FLOAT_EQ(yo[0], 2.0f);
    EXPECT_FLOAT_EQ(yo[1], 3.0f);
    EXPECT_FLOAT_EQ(yo[2], 4.0f);
    EXPECT_FLOAT_EQ(yo[3], 5.0f);
}

TEST(DataTransformDiff, UsesNextX)
{
    auto               x = make_x(3);
    auto               y = make_y({0, 1, 3});
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Diff);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(xo.size(), 2u);
    EXPECT_FLOAT_EQ(xo[0], 1.0f);
    EXPECT_FLOAT_EQ(xo[1], 2.0f);
}

TEST(DataTransformDiff, TooFewPoints)
{
    std::vector<float> x = {0.0f};
    std::vector<float> y = {5.0f};
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Diff);
    t.apply_y(x, y, xo, yo);
    EXPECT_TRUE(yo.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Scale
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformScale, MultiplyByFactor)
{
    auto               x = make_x(3);
    auto               y = make_y({1, 2, 3});
    std::vector<float> xo, yo;

    TransformParams p;
    p.scale_factor = 2.5f;
    DataTransform t(TransformType::Scale, p);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], 2.5f);
    EXPECT_FLOAT_EQ(yo[1], 5.0f);
    EXPECT_FLOAT_EQ(yo[2], 7.5f);
}

TEST(DataTransformScale, Scalar)
{
    TransformParams p;
    p.scale_factor = 3.0f;
    DataTransform t(TransformType::Scale, p);
    EXPECT_FLOAT_EQ(t.apply_scalar(4.0f), 12.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Offset
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformOffset, AddConstant)
{
    auto               x = make_x(3);
    auto               y = make_y({1, 2, 3});
    std::vector<float> xo, yo;

    TransformParams p;
    p.offset_value = 10.0f;
    DataTransform t(TransformType::Offset, p);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], 11.0f);
    EXPECT_FLOAT_EQ(yo[1], 12.0f);
    EXPECT_FLOAT_EQ(yo[2], 13.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clamp
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformClamp, ClampsToRange)
{
    auto               x = make_x(5);
    auto               y = make_y({-10, -1, 0.5f, 1, 10});
    std::vector<float> xo, yo;

    TransformParams p;
    p.clamp_min = -1.0f;
    p.clamp_max = 1.0f;
    DataTransform t(TransformType::Clamp, p);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 5u);
    EXPECT_FLOAT_EQ(yo[0], -1.0f);
    EXPECT_FLOAT_EQ(yo[1], -1.0f);
    EXPECT_FLOAT_EQ(yo[2], 0.5f);
    EXPECT_FLOAT_EQ(yo[3], 1.0f);
    EXPECT_FLOAT_EQ(yo[4], 1.0f);
}

TEST(DataTransformClamp, Scalar)
{
    TransformParams p;
    p.clamp_min = 0.0f;
    p.clamp_max = 1.0f;
    DataTransform t(TransformType::Clamp, p);
    EXPECT_FLOAT_EQ(t.apply_scalar(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(t.apply_scalar(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(t.apply_scalar(5.0f), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Custom transforms
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformCustom, PerElement)
{
    DataTransform t("square", [](float v) { return v * v; });

    auto               x = make_x(4);
    auto               y = make_y({1, 2, 3, 4});
    std::vector<float> xo, yo;
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 4u);
    EXPECT_FLOAT_EQ(yo[0], 1.0f);
    EXPECT_FLOAT_EQ(yo[1], 4.0f);
    EXPECT_FLOAT_EQ(yo[2], 9.0f);
    EXPECT_FLOAT_EQ(yo[3], 16.0f);
}

TEST(DataTransformCustom, XYFunction)
{
    // Custom transform that reverses the data
    DataTransform t("reverse",
                    DataTransform::CustomXYFunc(
                        [](std::span<const float> x_in,
                           std::span<const float> y_in,
                           std::vector<float>&    x_out,
                           std::vector<float>&    y_out)
                        {
                            size_t n = std::min(x_in.size(), y_in.size());
                            x_out.resize(n);
                            y_out.resize(n);
                            for (size_t i = 0; i < n; ++i)
                            {
                                x_out[i] = x_in[n - 1 - i];
                                y_out[i] = y_in[n - 1 - i];
                            }
                        }));

    auto               x = make_x(3);
    auto               y = make_y({10, 20, 30});
    std::vector<float> xo, yo;
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], 30.0f);
    EXPECT_FLOAT_EQ(yo[1], 20.0f);
    EXPECT_FLOAT_EQ(yo[2], 10.0f);
}

TEST(DataTransformCustom, ScalarPerElement)
{
    DataTransform t("double", [](float v) { return v * 2.0f; });
    EXPECT_FLOAT_EQ(t.apply_scalar(5.0f), 10.0f);
    EXPECT_TRUE(t.is_elementwise());
}

TEST(DataTransformCustom, ScalarXYReturnsNaN)
{
    DataTransform t("xy_func",
                    DataTransform::CustomXYFunc([](std::span<const float>,
                                                   std::span<const float>,
                                                   std::vector<float>&,
                                                   std::vector<float>&) {}));
    EXPECT_TRUE(std::isnan(t.apply_scalar(5.0f)));
    EXPECT_FALSE(t.is_elementwise());
}

// ═══════════════════════════════════════════════════════════════════════════
// Metadata
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformMetadata, IsElementwise)
{
    EXPECT_TRUE(DataTransform(TransformType::Identity).is_elementwise());
    EXPECT_TRUE(DataTransform(TransformType::Log10).is_elementwise());
    EXPECT_TRUE(DataTransform(TransformType::Abs).is_elementwise());
    EXPECT_TRUE(DataTransform(TransformType::Scale).is_elementwise());
    EXPECT_FALSE(DataTransform(TransformType::Normalize).is_elementwise());
    EXPECT_FALSE(DataTransform(TransformType::Derivative).is_elementwise());
    EXPECT_FALSE(DataTransform(TransformType::CumulativeSum).is_elementwise());
}

TEST(DataTransformMetadata, ChangesLength)
{
    EXPECT_FALSE(DataTransform(TransformType::Identity).changes_length());
    EXPECT_FALSE(DataTransform(TransformType::Abs).changes_length());
    EXPECT_TRUE(DataTransform(TransformType::Derivative).changes_length());
    EXPECT_TRUE(DataTransform(TransformType::Diff).changes_length());
    EXPECT_TRUE(DataTransform(TransformType::Log10).changes_length());
}

TEST(DataTransformMetadata, Description)
{
    EXPECT_EQ(DataTransform(TransformType::Identity).description(), "Identity (no change)");
    EXPECT_EQ(DataTransform(TransformType::Log10).description(), "Log10(y)");
    EXPECT_EQ(DataTransform(TransformType::Derivative).description(), "dy/dx");

    TransformParams p;
    p.scale_factor = 2.5f;
    EXPECT_NE(DataTransform(TransformType::Scale, p).description().find("2.5"), std::string::npos);
}

TEST(DataTransformMetadata, TypeName)
{
    EXPECT_STREQ(transform_type_name(TransformType::Identity), "Identity");
    EXPECT_STREQ(transform_type_name(TransformType::Log10), "Log10");
    EXPECT_STREQ(transform_type_name(TransformType::Derivative), "Derivative");
    EXPECT_STREQ(transform_type_name(TransformType::Custom), "Custom");
}

// ═══════════════════════════════════════════════════════════════════════════
// TransformPipeline
// ═══════════════════════════════════════════════════════════════════════════

TEST(TransformPipeline, EmptyIsIdentity)
{
    TransformPipeline pipe;
    EXPECT_TRUE(pipe.is_identity());
    EXPECT_EQ(pipe.step_count(), 0u);
}

TEST(TransformPipeline, SingleStep)
{
    TransformPipeline pipe("test");
    pipe.push_back(DataTransform(TransformType::Negate));

    auto               x = make_x(3);
    auto               y = make_y({1, 2, 3});
    std::vector<float> xo, yo;
    pipe.apply(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], -1.0f);
    EXPECT_FLOAT_EQ(yo[1], -2.0f);
    EXPECT_FLOAT_EQ(yo[2], -3.0f);
}

TEST(TransformPipeline, ChainedSteps)
{
    TransformPipeline pipe;
    TransformParams   p;
    p.scale_factor = 2.0f;
    pipe.push_back(DataTransform(TransformType::Scale, p));
    pipe.push_back(DataTransform(TransformType::Negate));

    auto               x = make_x(3);
    auto               y = make_y({1, 2, 3});
    std::vector<float> xo, yo;
    pipe.apply(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], -2.0f);   // 1*2 = 2, then -2
    EXPECT_FLOAT_EQ(yo[1], -4.0f);
    EXPECT_FLOAT_EQ(yo[2], -6.0f);
}

TEST(TransformPipeline, DisabledStep)
{
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Negate));
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.set_enabled(0, false);   // Disable negate

    auto               x = make_x(3);
    auto               y = make_y({-1, -2, -3});
    std::vector<float> xo, yo;
    pipe.apply(x, y, xo, yo);

    // Only Abs applied (negate disabled)
    ASSERT_EQ(yo.size(), 3u);
    EXPECT_FLOAT_EQ(yo[0], 1.0f);
    EXPECT_FLOAT_EQ(yo[1], 2.0f);
    EXPECT_FLOAT_EQ(yo[2], 3.0f);
}

TEST(TransformPipeline, AllDisabledIsIdentity)
{
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Negate));
    pipe.set_enabled(0, false);

    EXPECT_TRUE(pipe.is_identity());
}

TEST(TransformPipeline, InsertAndRemove)
{
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.insert(0, DataTransform(TransformType::Negate));

    EXPECT_EQ(pipe.step_count(), 2u);
    EXPECT_EQ(pipe.step(0).type(), TransformType::Negate);
    EXPECT_EQ(pipe.step(1).type(), TransformType::Abs);

    pipe.remove(0);
    EXPECT_EQ(pipe.step_count(), 1u);
    EXPECT_EQ(pipe.step(0).type(), TransformType::Abs);
}

TEST(TransformPipeline, MoveStep)
{
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.push_back(DataTransform(TransformType::Negate));
    pipe.push_back(DataTransform(TransformType::Log10));

    pipe.move_step(2, 0);   // Move Log10 to front
    EXPECT_EQ(pipe.step(0).type(), TransformType::Log10);
    EXPECT_EQ(pipe.step(1).type(), TransformType::Abs);
    EXPECT_EQ(pipe.step(2).type(), TransformType::Negate);
}

TEST(TransformPipeline, Clear)
{
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.push_back(DataTransform(TransformType::Negate));
    pipe.clear();
    EXPECT_EQ(pipe.step_count(), 0u);
    EXPECT_TRUE(pipe.is_identity());
}

TEST(TransformPipeline, Description)
{
    TransformPipeline pipe("my_pipe");
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.push_back(DataTransform(TransformType::Negate));

    auto desc = pipe.description();
    EXPECT_NE(desc.find("|y|"), std::string::npos);
    EXPECT_NE(desc.find("-y"), std::string::npos);
    EXPECT_NE(desc.find("→"), std::string::npos);
}

TEST(TransformPipeline, LengthChangingChain)
{
    // Derivative reduces length by 1, then Diff reduces by 1 more
    TransformPipeline pipe;
    pipe.push_back(DataTransform(TransformType::Derivative));
    pipe.push_back(DataTransform(TransformType::Diff));

    auto               x = make_x(5);
    auto               y = make_y({0, 1, 4, 9, 16});   // y = x^2
    std::vector<float> xo, yo;
    pipe.apply(x, y, xo, yo);

    // Derivative: 4 points, Diff: 3 points
    EXPECT_EQ(yo.size(), 3u);
}

// ═══════════════════════════════════════════════════════════════════════════
// TransformRegistry
// ═══════════════════════════════════════════════════════════════════════════

TEST(TransformRegistryTest, Singleton)
{
    auto& r1 = TransformRegistry::instance();
    auto& r2 = TransformRegistry::instance();
    EXPECT_EQ(&r1, &r2);
}

TEST(TransformRegistryTest, BuiltinTransformsAvailable)
{
    auto names = TransformRegistry::instance().available_transforms();
    EXPECT_GE(names.size(), 13u);   // At least 13 built-in types

    // Check some expected names
    bool has_identity = false, has_log10 = false, has_derivative = false;
    for (const auto& n : names)
    {
        if (n == "Identity")
            has_identity = true;
        if (n == "Log10")
            has_log10 = true;
        if (n == "Derivative")
            has_derivative = true;
    }
    EXPECT_TRUE(has_identity);
    EXPECT_TRUE(has_log10);
    EXPECT_TRUE(has_derivative);
}

TEST(TransformRegistryTest, BuiltinCustomTransforms)
{
    auto&         reg = TransformRegistry::instance();
    DataTransform t;
    EXPECT_TRUE(reg.get_transform("square", t));
    EXPECT_FLOAT_EQ(t.apply_scalar(3.0f), 9.0f);

    EXPECT_TRUE(reg.get_transform("sqrt", t));
    EXPECT_NEAR(t.apply_scalar(9.0f), 3.0f, 1e-6f);

    EXPECT_TRUE(reg.get_transform("reciprocal", t));
    EXPECT_FLOAT_EQ(t.apply_scalar(4.0f), 0.25f);
}

TEST(TransformRegistryTest, RegisterCustom)
{
    TransformRegistry reg;
    reg.register_transform("cube", [](float v) { return v * v * v; }, "y³");
    ASSERT_TRUE(reg.set_transform_source("cube", "TestProvider"));

    DataTransform t;
    EXPECT_TRUE(reg.get_transform("cube", t));
    EXPECT_FLOAT_EQ(t.apply_scalar(2.0f), 8.0f);
    EXPECT_TRUE(t.available());
    EXPECT_EQ(t.source(), "TestProvider");
    EXPECT_EQ(reg.transform_source("cube"), "TestProvider");
}

TEST(DataTransformTest, UnavailableCustomPipelineStepIsPreservedButSkipped)
{
    TransformPipeline pipeline;
    pipeline.push_back(DataTransform(TransformType::Scale, TransformParams{.scale_factor = 2.0f}));
    pipeline.push_back(DataTransform::unavailable_custom("MissingPluginStep", "Plugin A"));
    pipeline.push_back(DataTransform(TransformType::Offset, TransformParams{.offset_value = 1.0f}));

    ASSERT_EQ(pipeline.step_count(), 3u);
    EXPECT_FALSE(pipeline.step(1).available());
    EXPECT_EQ(pipeline.step(1).source(), "Plugin A");
    std::vector<float> x_out;
    std::vector<float> y_out;
    pipeline.apply(std::array<float, 2>{0.0f, 1.0f},
                   std::array<float, 2>{2.0f, 3.0f},
                   x_out,
                   y_out);
    EXPECT_EQ(y_out, (std::vector<float>{5.0f, 7.0f}));
}

TEST(TransformRegistryTest, GetNonexistent)
{
    TransformRegistry reg;
    DataTransform     t;
    EXPECT_FALSE(reg.get_transform("nonexistent_xyzzy", t));
}

TEST(TransformRegistryTest, SaveLoadPipeline)
{
    TransformRegistry reg;

    TransformPipeline pipe("test_pipe");
    pipe.push_back(DataTransform(TransformType::Abs));
    pipe.push_back(DataTransform(TransformType::Negate));

    reg.save_pipeline("my_preset", pipe);

    TransformPipeline loaded;
    EXPECT_TRUE(reg.load_pipeline("my_preset", loaded));
    EXPECT_EQ(loaded.step_count(), 2u);
    EXPECT_EQ(loaded.step(0).type(), TransformType::Abs);
    EXPECT_EQ(loaded.step(1).type(), TransformType::Negate);
}

TEST(TransformRegistryTest, SavedPipelineNames)
{
    TransformRegistry reg;
    reg.save_pipeline("beta", TransformPipeline("b"));
    reg.save_pipeline("alpha", TransformPipeline("a"));

    auto names = reg.saved_pipelines();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
}

TEST(TransformRegistryTest, RemovePipeline)
{
    TransformRegistry reg;
    reg.save_pipeline("test", TransformPipeline());
    EXPECT_TRUE(reg.remove_pipeline("test"));
    EXPECT_FALSE(reg.remove_pipeline("test"));
}

TEST(TransformRegistryTest, CreateFactory)
{
    auto t = TransformRegistry::create(TransformType::Abs);
    EXPECT_EQ(t.type(), TransformType::Abs);
    EXPECT_FLOAT_EQ(t.apply_scalar(-5.0f), 5.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Free functions
// ═══════════════════════════════════════════════════════════════════════════

TEST(TransformFreeFunc, TransformY)
{
    std::vector<float> y      = {1, 2, 3, 4};
    auto               result = transform_y(y, TransformType::Negate);

    ASSERT_EQ(result.size(), 4u);
    EXPECT_FLOAT_EQ(result[0], -1.0f);
    EXPECT_FLOAT_EQ(result[3], -4.0f);
}

TEST(TransformFreeFunc, TransformXY)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {1, 10, 100, 1000};
    std::vector<float> xo, yo;

    transform_xy(x, y, xo, yo, TransformType::Log10);

    ASSERT_EQ(yo.size(), 4u);
    EXPECT_NEAR(yo[0], 0.0f, 1e-6f);
    EXPECT_NEAR(yo[3], 3.0f, 1e-6f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(DataTransformEdge, MismatchedLengths)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {1, 2, 3};   // Shorter than x
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Identity);
    t.apply_y(x, y, xo, yo);

    EXPECT_EQ(xo.size(), 3u);   // Uses min(x.size, y.size)
    EXPECT_EQ(yo.size(), 3u);
}

TEST(DataTransformEdge, SinglePoint)
{
    std::vector<float> x = {1.0f};
    std::vector<float> y = {5.0f};
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Normalize);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 1u);
    EXPECT_FLOAT_EQ(yo[0], 0.5f);   // Constant → 0.5
}

TEST(DataTransformEdge, DerivativeZeroDx)
{
    std::vector<float> x = {1.0f, 1.0f};   // Same x values
    std::vector<float> y = {0.0f, 5.0f};
    std::vector<float> xo, yo;

    DataTransform t(TransformType::Derivative);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), 1u);
    EXPECT_FLOAT_EQ(yo[0], 0.0f);   // Division by zero → 0
}

TEST(DataTransformEdge, LargeDataset)
{
    const size_t       N = 100000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(static_cast<float>(i) * 0.01f);
    }

    std::vector<float> xo, yo;
    DataTransform      t(TransformType::Normalize);
    t.apply_y(x, y, xo, yo);

    ASSERT_EQ(yo.size(), N);
    // Normalized values should be in [0, 1]
    for (float v : yo)
    {
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
    }
}
