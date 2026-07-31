#include <gtest/gtest.h>

#include "ui/theme/icons.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

using namespace spectra::ui;

TEST(IconFont, AllIconsListUsesEnumValues)
{
    const auto& icons = IconFont::instance().get_all_icons();
    ASSERT_FALSE(icons.empty());

    // FA6 Solid codepoints are scattered across U+E000-U+E5FF and U+F000-U+F8FF.
    // Verify each icon is a valid non-zero codepoint below the Last sentinel.
    const auto enum_last = static_cast<uint16_t>(Icon::Last);

    for (Icon icon : icons)
    {
        const auto value = static_cast<uint16_t>(icon);
        EXPECT_GT(value, 0u);
        EXPECT_LT(value, enum_last);
    }
}

#ifdef SPECTRA_USE_IMGUI
TEST(IconFont, FontPointersAreScopedToCurrentImGuiContext)
{
    auto make_context = []()
    {
        ImGuiContext* context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(320.0f, 200.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        ImGui::NewFrame();
        return context;
    };

    IconFont& icons = IconFont::instance();

    ImGuiContext* first_context = make_context();
    ASSERT_TRUE(icons.initialize());
    ImFont* first_font = icons.get_font();
    ASSERT_NE(first_font, nullptr);
    ImGui::EndFrame();

    ImGuiContext* second_context = make_context();
    ASSERT_TRUE(icons.initialize());
    ImFont* second_font = icons.get_font();
    ASSERT_NE(second_font, nullptr);
    EXPECT_NE(second_font, first_font);
    ImGui::EndFrame();

    ImGui::SetCurrentContext(first_context);
    EXPECT_EQ(icons.get_font(), first_font);
    icons.release_context(first_context);
    EXPECT_EQ(icons.get_font(), nullptr);
    ImGui::DestroyContext(first_context);

    ImGui::SetCurrentContext(second_context);
    EXPECT_EQ(icons.get_font(), second_font);
    icons.release_context(second_context);
    ImGui::DestroyContext(second_context);
}
#endif
