#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <set>

#include "ui/theme/theme.hpp"

using namespace spectra::ui;

// ─── Color Utility Methods ───────────────────────────────────────────────────

TEST(ColorUtils, LuminanceBlackIsZero)
{
    Color black(0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(black.luminance(), 0.0f, 0.001f);
}

TEST(ColorUtils, LuminanceWhiteIsHigh)
{
    Color white(1.0f, 1.0f, 1.0f);
    EXPECT_GT(white.luminance(), 0.9f);
}

TEST(ColorUtils, LuminanceGreenDominates)
{
    Color red(1.0f, 0.0f, 0.0f);
    Color green(0.0f, 1.0f, 0.0f);
    EXPECT_GT(green.luminance(), red.luminance());
}

TEST(ColorUtils, ContrastRatioBlackWhite)
{
    Color black(0.0f, 0.0f, 0.0f);
    Color white(1.0f, 1.0f, 1.0f);
    float ratio = black.contrast_ratio(white);
    EXPECT_GT(ratio, 15.0f);   // Should be ~21:1
}

TEST(ColorUtils, ContrastRatioSymmetric)
{
    Color a(0.2f, 0.4f, 0.6f);
    Color b(0.8f, 0.9f, 1.0f);
    EXPECT_FLOAT_EQ(a.contrast_ratio(b), b.contrast_ratio(a));
}

TEST(ColorUtils, ContrastRatioSameColorIsOne)
{
    Color c(0.5f, 0.5f, 0.5f);
    EXPECT_NEAR(c.contrast_ratio(c), 1.0f, 0.01f);
}

TEST(ColorUtils, ContrastRatioWCAGAA)
{
    // WCAG AA requires 4.5:1 for normal text
    Color dark_text(0.1f, 0.1f, 0.1f);
    Color light_bg(0.95f, 0.95f, 0.95f);
    EXPECT_GT(dark_text.contrast_ratio(light_bg), 4.5f);
}

TEST(ColorUtils, ToLinearAndBack)
{
    Color original(0.5f, 0.3f, 0.8f, 0.9f);
    Color linear = original.to_linear();
    Color back   = linear.to_srgb();
    EXPECT_NEAR(back.r, original.r, 0.01f);
    EXPECT_NEAR(back.g, original.g, 0.01f);
    EXPECT_NEAR(back.b, original.b, 0.01f);
    EXPECT_FLOAT_EQ(back.a, original.a);
}

TEST(ColorUtils, ToLinearBlackIsBlack)
{
    Color black(0.0f, 0.0f, 0.0f);
    Color lin = black.to_linear();
    EXPECT_FLOAT_EQ(lin.r, 0.0f);
    EXPECT_FLOAT_EQ(lin.g, 0.0f);
    EXPECT_FLOAT_EQ(lin.b, 0.0f);
}

TEST(ColorUtils, ToLinearWhiteIsWhite)
{
    Color white(1.0f, 1.0f, 1.0f);
    Color lin = white.to_linear();
    EXPECT_NEAR(lin.r, 1.0f, 0.01f);
    EXPECT_NEAR(lin.g, 1.0f, 0.01f);
    EXPECT_NEAR(lin.b, 1.0f, 0.01f);
}

TEST(ColorUtils, LinearIsLowerThanSRGB)
{
    // sRGB mid-gray should be darker in linear space
    Color mid(0.5f, 0.5f, 0.5f);
    Color lin = mid.to_linear();
    EXPECT_LT(lin.r, mid.r);
}

TEST(ColorUtils, HSLRoundTrip)
{
    Color original(0.8f, 0.3f, 0.5f);
    auto  hsl  = original.to_hsl();
    Color back = Color::from_hsl(hsl.h, hsl.s, hsl.l);
    EXPECT_NEAR(back.r, original.r, 0.01f);
    EXPECT_NEAR(back.g, original.g, 0.01f);
    EXPECT_NEAR(back.b, original.b, 0.01f);
}

TEST(ColorUtils, HSLRedHue)
{
    Color red(1.0f, 0.0f, 0.0f);
    auto  hsl = red.to_hsl();
    EXPECT_NEAR(hsl.h, 0.0f, 1.0f);   // Red is at 0 degrees
    EXPECT_NEAR(hsl.s, 1.0f, 0.01f);
}

TEST(ColorUtils, HSLGreenHue)
{
    Color green(0.0f, 1.0f, 0.0f);
    auto  hsl = green.to_hsl();
    EXPECT_NEAR(hsl.h, 120.0f, 1.0f);
}

TEST(ColorUtils, HSLBlueHue)
{
    Color blue(0.0f, 0.0f, 1.0f);
    auto  hsl = blue.to_hsl();
    EXPECT_NEAR(hsl.h, 240.0f, 1.0f);
}

TEST(ColorUtils, HSLGrayHasZeroSaturation)
{
    Color gray(0.5f, 0.5f, 0.5f);
    auto  hsl = gray.to_hsl();
    EXPECT_FLOAT_EQ(hsl.s, 0.0f);
    EXPECT_NEAR(hsl.l, 0.5f, 0.01f);
}

TEST(ColorUtils, FromHSLGray)
{
    Color gray = Color::from_hsl(0.0f, 0.0f, 0.5f);
    EXPECT_NEAR(gray.r, 0.5f, 0.01f);
    EXPECT_NEAR(gray.g, 0.5f, 0.01f);
    EXPECT_NEAR(gray.b, 0.5f, 0.01f);
}

TEST(ColorUtils, EqualityOperator)
{
    Color a(0.1f, 0.2f, 0.3f, 0.4f);
    Color b(0.1f, 0.2f, 0.3f, 0.4f);
    Color c(0.1f, 0.2f, 0.3f, 0.5f);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ─── CVD Simulation ──────────────────────────────────────────────────────────

TEST(CVDSimulation, NoneReturnsOriginal)
{
    Color c(0.5f, 0.3f, 0.8f);
    Color result = simulate_cvd(c, CVDType::None);
    EXPECT_FLOAT_EQ(result.r, c.r);
    EXPECT_FLOAT_EQ(result.g, c.g);
    EXPECT_FLOAT_EQ(result.b, c.b);
}

TEST(CVDSimulation, ProtanopiaReducesRed)
{
    Color red(1.0f, 0.0f, 0.0f);
    Color simulated = simulate_cvd(red, CVDType::Protanopia);
    // Protanopes see red as much darker/different
    EXPECT_LT(simulated.r, red.r);
}

TEST(CVDSimulation, DeuteranopiaReducesGreen)
{
    Color green(0.0f, 1.0f, 0.0f);
    Color simulated = simulate_cvd(green, CVDType::Deuteranopia);
    // Deuteranopes see green differently
    EXPECT_NE(simulated.g, green.g);
}

TEST(CVDSimulation, TritanopiaAffectsBlue)
{
    Color blue(0.0f, 0.0f, 1.0f);
    Color simulated = simulate_cvd(blue, CVDType::Tritanopia);
    EXPECT_NE(simulated.b, blue.b);
}

TEST(CVDSimulation, AchromatopsiaIsGrayscale)
{
    Color c(0.8f, 0.2f, 0.5f);
    Color simulated = simulate_cvd(c, CVDType::Achromatopsia);
    // All channels should be equal (grayscale)
    EXPECT_NEAR(simulated.r, simulated.g, 0.01f);
    EXPECT_NEAR(simulated.g, simulated.b, 0.01f);
}

TEST(CVDSimulation, BlackRemainsBlack)
{
    Color black(0.0f, 0.0f, 0.0f);
    for (auto type :
         {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia, CVDType::Achromatopsia})
    {
        Color sim = simulate_cvd(black, type);
        EXPECT_NEAR(sim.r, 0.0f, 0.02f);
        EXPECT_NEAR(sim.g, 0.0f, 0.02f);
        EXPECT_NEAR(sim.b, 0.0f, 0.02f);
    }
}

TEST(CVDSimulation, WhiteRemainsWhite)
{
    Color white(1.0f, 1.0f, 1.0f);
    for (auto type :
         {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia, CVDType::Achromatopsia})
    {
        Color sim = simulate_cvd(white, type);
        EXPECT_NEAR(sim.r, 1.0f, 0.05f);
        EXPECT_NEAR(sim.g, 1.0f, 0.05f);
        EXPECT_NEAR(sim.b, 1.0f, 0.05f);
    }
}

TEST(CVDSimulation, PreservesAlpha)
{
    Color c(0.5f, 0.5f, 0.5f, 0.7f);
    Color sim = simulate_cvd(c, CVDType::Protanopia);
    EXPECT_FLOAT_EQ(sim.a, 0.7f);
}

TEST(CVDSimulation, OutputIsClamped)
{
    // Extreme colors shouldn't produce out-of-range values
    Color bright(1.0f, 1.0f, 0.0f);
    for (auto type : {CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia})
    {
        Color sim = simulate_cvd(bright, type);
        EXPECT_GE(sim.r, 0.0f);
        EXPECT_LE(sim.r, 1.0f);
        EXPECT_GE(sim.g, 0.0f);
        EXPECT_LE(sim.g, 1.0f);
        EXPECT_GE(sim.b, 0.0f);
        EXPECT_LE(sim.b, 1.0f);
    }
}

// ─── DataPalette Struct ──────────────────────────────────────────────────────

TEST(DataPaletteStruct, IndexWrapsAround)
{
    DataPalette dp;
    dp.colors = {Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)};
    EXPECT_FLOAT_EQ(dp[0].r, 1.0f);
    EXPECT_FLOAT_EQ(dp[3].r, 1.0f);   // wraps to index 0
    EXPECT_FLOAT_EQ(dp[4].g, 1.0f);   // wraps to index 1
}

TEST(DataPaletteStruct, IsSafeForNoneAlwaysTrue)
{
    DataPalette dp;
    EXPECT_TRUE(dp.is_safe_for(CVDType::None));
}

TEST(DataPaletteStruct, IsSafeForChecksVector)
{
    DataPalette dp;
    dp.safe_for = {CVDType::Protanopia, CVDType::Deuteranopia};
    EXPECT_TRUE(dp.is_safe_for(CVDType::Protanopia));
    EXPECT_TRUE(dp.is_safe_for(CVDType::Deuteranopia));
    EXPECT_FALSE(dp.is_safe_for(CVDType::Tritanopia));
}

// ─── Colorblind Palettes ─────────────────────────────────────────────────────

class ColorblindPaletteTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        original_theme_   = tm_.current_theme_name();
        original_palette_ = tm_.current_data_palette_name();
    }
    void TearDown() override
    {
        if (tm_.is_transitioning())
            tm_.update(10.0f);
        if (tm_.is_palette_transitioning())
            tm_.update(10.0f);
        tm_.set_theme(original_theme_);
        tm_.set_data_palette(original_palette_);
        ThemeManager::set_current(nullptr);
    }
    ThemeManager tm_;
    std::string  original_theme_;
    std::string  original_palette_;
};

TEST_F(ColorblindPaletteTest, AllExpectedPalettesExist)
{
    auto&                 tm    = ThemeManager::instance();
    const auto&           names = tm.available_data_palettes();
    std::set<std::string> name_set(names.begin(), names.end());

    EXPECT_TRUE(name_set.count("default"));
    EXPECT_TRUE(name_set.count("colorblind"));
    EXPECT_TRUE(name_set.count("tol_bright"));
    EXPECT_TRUE(name_set.count("tol_muted"));
    EXPECT_TRUE(name_set.count("ibm"));
    EXPECT_TRUE(name_set.count("wong"));
    EXPECT_TRUE(name_set.count("viridis"));
    EXPECT_TRUE(name_set.count("monochrome"));
}

TEST_F(ColorblindPaletteTest, PaletteNamesAreSorted)
{
    auto&       tm    = ThemeManager::instance();
    const auto& names = tm.available_data_palettes();
    for (size_t i = 1; i < names.size(); ++i)
    {
        EXPECT_LE(names[i - 1], names[i]) << "Palette names not sorted at index " << i;
    }
}

TEST_F(ColorblindPaletteTest, OkabeItoHas8Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("colorblind");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 8u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, TolBrightHas7Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("tol_bright");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 7u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, TolMutedHas9Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("tol_muted");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 9u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, IBMHas5Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("ibm");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 5u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, WongHas8Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("wong");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 8u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, ViridisHas10Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("viridis");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 10u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, MonochromeHas5Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("monochrome");
    EXPECT_EQ(tm.current_data_palette().colors.size(), 5u);
    EXPECT_TRUE(tm.current_data_palette().colorblind_safe);
}

TEST_F(ColorblindPaletteTest, MonochromeSafeForAchromatopsia)
{
    auto&       tm   = ThemeManager::instance();
    const auto& mono = tm.get_data_palette("monochrome");
    EXPECT_TRUE(mono.is_safe_for(CVDType::Achromatopsia));
}

TEST_F(ColorblindPaletteTest, AllPaletteColorsAreDistinct)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : tm.available_data_palettes())
    {
        tm.set_data_palette(name);
        const auto& colors = tm.current_data_palette().colors;
        for (size_t i = 0; i + 1 < colors.size(); ++i)
        {
            bool same = (colors[i].r == colors[i + 1].r && colors[i].g == colors[i + 1].g
                         && colors[i].b == colors[i + 1].b);
            EXPECT_FALSE(same) << "Palette '" << name << "' colors " << i << " and " << i + 1
                               << " are identical";
        }
    }
}

TEST_F(ColorblindPaletteTest, ColorblindPalettesHaveSafeForMetadata)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : tm.available_data_palettes())
    {
        const auto& pal = tm.get_data_palette(name);
        if (pal.colorblind_safe)
        {
            EXPECT_FALSE(pal.safe_for.empty())
                << "Palette '" << name << "' is marked colorblind_safe but has no safe_for entries";
        }
    }
}

TEST_F(ColorblindPaletteTest, AllPalettesHaveDescriptions)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : tm.available_data_palettes())
    {
        const auto& pal = tm.get_data_palette(name);
        if (!pal.name.empty())
        {
            EXPECT_FALSE(pal.description.empty()) << "Palette '" << name << "' has no description";
        }
    }
}

TEST_F(ColorblindPaletteTest, CVDSafeColorsRemainDistinguishable)
{
    // For each colorblind-safe palette, simulate CVD and verify colors
    // remain distinguishable (minimum luminance difference between adjacent simulated colors)
    auto& tm = ThemeManager::instance();
    for (const auto& name : tm.available_data_palettes())
    {
        const auto& pal = tm.get_data_palette(name);
        if (!pal.colorblind_safe)
            continue;

        for (auto cvd_type : pal.safe_for)
        {
            std::vector<Color> simulated;
            for (const auto& c : pal.colors)
            {
                simulated.push_back(simulate_cvd(c, cvd_type));
            }

            // Check that no two simulated colors are identical
            for (size_t i = 0; i < simulated.size(); ++i)
            {
                for (size_t j = i + 1; j < simulated.size(); ++j)
                {
                    float dr   = simulated[i].r - simulated[j].r;
                    float dg   = simulated[i].g - simulated[j].g;
                    float db   = simulated[i].b - simulated[j].b;
                    float dist = std::sqrt(dr * dr + dg * dg + db * db);
                    EXPECT_GT(dist, 0.02f)
                        << "Palette '" << name << "' colors " << i << " and " << j
                        << " are indistinguishable under CVD type " << static_cast<int>(cvd_type);
                }
            }
        }
    }
}

TEST_F(ColorblindPaletteTest, GetDataPaletteByName)
{
    auto&       tm  = ThemeManager::instance();
    const auto& pal = tm.get_data_palette("colorblind");
    EXPECT_EQ(pal.name, "colorblind");
    EXPECT_FALSE(pal.colors.empty());
}

TEST_F(ColorblindPaletteTest, GetDataPaletteInvalidReturnsEmpty)
{
    auto&       tm  = ThemeManager::instance();
    const auto& pal = tm.get_data_palette("nonexistent_palette_xyz");
    EXPECT_TRUE(pal.colors.empty());
}

TEST_F(ColorblindPaletteTest, RegisterCustomPalette)
{
    auto&       tm = ThemeManager::instance();
    DataPalette custom;
    custom.name            = "custom_test_pal";
    custom.description     = "Test palette";
    custom.colors          = {Color(1, 0, 0), Color(0, 1, 0)};
    custom.colorblind_safe = false;

    tm.register_data_palette("custom_test_pal", custom);

    const auto& names = tm.available_data_palettes();
    bool        found = false;
    for (const auto& n : names)
    {
        if (n == "custom_test_pal")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    const auto& retrieved = tm.get_data_palette("custom_test_pal");
    EXPECT_EQ(retrieved.colors.size(), 2u);
}

TEST_F(ColorblindPaletteTest, CurrentDataPaletteName)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("colorblind");
    EXPECT_EQ(tm.current_data_palette_name(), "colorblind");
    tm.set_data_palette("default");
    EXPECT_EQ(tm.current_data_palette_name(), "default");
}

// ─── Theme Transition Bug Fix ────────────────────────────────────────────────

TEST_F(ColorblindPaletteTest, TransitionDoesNotCorruptStoredTheme)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    // Record the dark theme's stored bg_primary
    ThemeColors dark_colors_before = tm.current().colors;

    // Start transition to light
    tm.transition_to("light", 1.0f);

    // Advance halfway
    tm.update(0.5f);
    EXPECT_TRUE(tm.is_transitioning());

    // The display colors should be mid-transition
    float mid_bg_r = tm.colors().bg_primary.r;
    EXPECT_NE(mid_bg_r, dark_colors_before.bg_primary.r);

    // Complete transition
    tm.update(0.6f);
    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_EQ(tm.current_theme_name(), "light");

    // Now switch back to dark and verify it wasn't corrupted
    tm.set_theme("dark");
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, dark_colors_before.bg_primary.r);
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.g, dark_colors_before.bg_primary.g);
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.b, dark_colors_before.bg_primary.b);
}

TEST_F(ColorblindPaletteTest, TransitionDisplayColorsInterpolate)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    float dark_bg = tm.colors().bg_primary.r;

    tm.set_theme("light");
    float light_bg = tm.colors().bg_primary.r;

    // Start from dark, transition to light
    tm.set_theme("dark");
    tm.transition_to("light", 1.0f);
    tm.update(0.5f);

    float mid_bg = tm.colors().bg_primary.r;
    // Mid-transition should be between dark and light
    if (dark_bg < light_bg)
    {
        EXPECT_GT(mid_bg, dark_bg);
        EXPECT_LT(mid_bg, light_bg);
    }
}

TEST_F(ColorblindPaletteTest, TransitionChainDoesNotCorrupt)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    // Chain: dark -> light (interrupt) -> high_contrast
    tm.transition_to("light", 1.0f);
    tm.update(0.3f);

    // Interrupt with new transition
    tm.transition_to("high_contrast", 0.5f);
    tm.update(0.6f);

    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_EQ(tm.current_theme_name(), "high_contrast");

    // Verify dark theme is still pristine
    tm.set_theme("dark");
    float lum = 0.2126f * tm.colors().bg_primary.r + 0.7152f * tm.colors().bg_primary.g
                + 0.0722f * tm.colors().bg_primary.b;
    EXPECT_LT(lum, 0.15f);
}

// ─── Palette Transitions ─────────────────────────────────────────────────────

TEST_F(ColorblindPaletteTest, PaletteTransitionStarts)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    EXPECT_FALSE(tm.is_palette_transitioning());

    tm.transition_palette("colorblind", 0.5f);
    EXPECT_TRUE(tm.is_palette_transitioning());
}

TEST_F(ColorblindPaletteTest, PaletteTransitionCompletes)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    tm.transition_palette("colorblind", 0.2f);

    tm.update(0.25f);
    EXPECT_FALSE(tm.is_palette_transitioning());
    EXPECT_EQ(tm.current_data_palette_name(), "colorblind");
}

TEST_F(ColorblindPaletteTest, PaletteTransitionInterpolates)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    Color start_color = tm.current_data_palette().colors[0];

    tm.transition_palette("colorblind", 1.0f);
    tm.update(0.5f);

    Color mid_color = tm.current_data_palette().colors[0];
    // Mid-transition color should differ from start
    EXPECT_NE(mid_color.r, start_color.r);
}

TEST_F(ColorblindPaletteTest, PaletteTransitionZeroDurationIsInstant)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    tm.transition_palette("colorblind", 0.0f);
    EXPECT_FALSE(tm.is_palette_transitioning());
    EXPECT_EQ(tm.current_data_palette_name(), "colorblind");
}

TEST_F(ColorblindPaletteTest, PaletteTransitionInvalidNameIsNoOp)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    tm.transition_palette("nonexistent", 0.5f);
    EXPECT_FALSE(tm.is_palette_transitioning());
    EXPECT_EQ(tm.current_data_palette_name(), "default");
}

// ─── Theme Export/Import ─────────────────────────────────────────────────────

class ThemeExportImportTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        original_theme_ = tm_.current_theme_name();
        test_dir_       = std::filesystem::temp_directory_path() / "spectra_test_themes";
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override
    {
        if (tm_.is_transitioning())
            tm_.update(10.0f);
        tm_.set_theme(original_theme_);
        std::filesystem::remove_all(test_dir_);
        ThemeManager::set_current(nullptr);
    }
    ThemeManager          tm_;
    std::string           original_theme_;
    std::filesystem::path test_dir_;
};

TEST_F(ThemeExportImportTest, ExportCreatesFile)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    auto path = test_dir_ / "dark_export.json";
    EXPECT_TRUE(tm.export_theme(path.string()));
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 100u);
}

TEST_F(ThemeExportImportTest, ExportContainsThemeName)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    auto path = test_dir_ / "dark_name.json";
    tm.export_theme(path.string());

    std::ifstream f(path);
    std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"dark\""), std::string::npos);
}

TEST_F(ThemeExportImportTest, ExportContainsColorFields)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    auto path = test_dir_ / "dark_colors.json";
    tm.export_theme(path.string());

    std::ifstream f(path);
    std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("bg_primary"), std::string::npos);
    EXPECT_NE(content.find("accent"), std::string::npos);
    EXPECT_NE(content.find("text_primary"), std::string::npos);
}

TEST_F(ThemeExportImportTest, ImportLoadsTheme)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    auto path = test_dir_ / "dark_import.json";
    tm.export_theme(path.string());

    // Import should succeed
    EXPECT_TRUE(tm.import_theme(path.string()));
}

TEST_F(ThemeExportImportTest, ExportImportRoundTrip)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    // Record original colors
    float orig_accent_r = tm.colors().accent.r;
    float orig_bg_r     = tm.colors().bg_primary.r;

    auto path = test_dir_ / "roundtrip.json";
    tm.export_theme(path.string());

    // Modify the theme name in the file to avoid collision
    {
        std::ifstream fin(path);
        std::string   content((std::istreambuf_iterator<char>(fin)),
                            std::istreambuf_iterator<char>());
        fin.close();

        auto pos = content.find("\"dark\"");
        if (pos != std::string::npos)
        {
            content.replace(pos, 6, "\"dark_roundtrip\"");
        }

        std::ofstream fout(path);
        fout << content;
    }

    EXPECT_TRUE(tm.import_theme(path.string()));
    tm.set_theme("dark_roundtrip");

    EXPECT_NEAR(tm.colors().accent.r, orig_accent_r, 0.001f);
    EXPECT_NEAR(tm.colors().bg_primary.r, orig_bg_r, 0.001f);
}

TEST_F(ThemeExportImportTest, ImportNonexistentFileFails)
{
    auto& tm = ThemeManager::instance();
    EXPECT_FALSE(tm.import_theme("/nonexistent/path/theme.json"));
}

TEST_F(ThemeExportImportTest, ImportEmptyFileFails)
{
    auto path = test_dir_ / "empty.json";
    {
        std::ofstream f(path);
    }

    auto& tm = ThemeManager::instance();
    EXPECT_FALSE(tm.import_theme(path.string()));
}

TEST_F(ThemeExportImportTest, ImportInvalidJsonFails)
{
    auto path = test_dir_ / "invalid.json";
    {
        std::ofstream f(path);
        f << "not json at all";
    }

    auto& tm = ThemeManager::instance();
    EXPECT_FALSE(tm.import_theme(path.string()));
}

TEST_F(ThemeExportImportTest, ExportAllThemes)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : {"dark", "light", "high_contrast"})
    {
        tm.set_theme(name);
        auto path = test_dir_ / (std::string(name) + ".json");
        EXPECT_TRUE(tm.export_theme(path.string())) << "Failed to export theme: " << name;
        EXPECT_TRUE(std::filesystem::exists(path));
    }
}

TEST_F(ThemeExportImportTest, ExportContainsScalarProperties)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");

    auto path = test_dir_ / "scalars.json";
    tm.export_theme(path.string());

    std::ifstream f(path);
    std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("opacity_panel"), std::string::npos);
    EXPECT_NE(content.find("animation_speed"), std::string::npos);
    EXPECT_NE(content.find("enable_animations"), std::string::npos);
}

// ─── High Contrast Accessibility ─────────────────────────────────────────────

TEST_F(ColorblindPaletteTest, HighContrastThemePassesWCAGAA)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("high_contrast");
    const auto& c = tm.colors();

    // Text on background should have >= 4.5:1 contrast (WCAG AA)
    float ratio = c.text_primary.contrast_ratio(c.bg_primary);
    EXPECT_GT(ratio, 4.5f) << "High contrast text/bg fails WCAG AA";
}

TEST_F(ColorblindPaletteTest, HighContrastSecondaryTextPassesWCAGAA)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("high_contrast");
    const auto& c = tm.colors();

    float ratio = c.text_secondary.contrast_ratio(c.bg_primary);
    EXPECT_GT(ratio, 4.5f) << "High contrast secondary text fails WCAG AA";
}

TEST_F(ColorblindPaletteTest, DarkThemeTextIsReadable)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    const auto& c = tm.colors();

    // At minimum, primary text should have decent contrast
    float ratio = c.text_primary.contrast_ratio(c.bg_primary);
    EXPECT_GT(ratio, 3.0f);
}

TEST_F(ColorblindPaletteTest, LightThemeTextIsReadable)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("light");
    const auto& c = tm.colors();

    float ratio = c.text_primary.contrast_ratio(c.bg_primary);
    EXPECT_GT(ratio, 3.0f);
}
