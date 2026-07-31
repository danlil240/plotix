#include "icons.hpp"

#include <cfloat>
#include <unordered_map>
#include <vector>

#include "imgui.h"

namespace spectra::ui
{

namespace
{

// Convert a PUA codepoint (0xE001-0xE063) to a UTF-8 string.
// All PUA codepoints are in the BMP (U+E000-U+F8FF) so they encode as 3 bytes.
std::string codepoint_to_utf8(uint32_t cp)
{
    std::string s;
    if (cp <= 0x7F)
    {
        s.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

}   // namespace

IconFont& IconFont::instance()
{
    static IconFont instance;
    return instance;
}

bool IconFont::initialize()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return false;

    if (!initialized_)
    {
        build_icon_map();
        initialized_ = true;
    }

    // The icon font glyphs are merged into every ImGui font by load_fonts()
    // in imgui_integration.cpp. Cache pointers per ImGui context so one
    // canvas can never use another canvas's font atlas.
    ContextFonts fonts;
    fonts.font_16 = ImGui::GetFont();
    fonts.font_20 = ImGui::GetFont();
    fonts.font_24 = ImGui::GetFont();
    fonts.font_32 = ImGui::GetFont();

    // Try to find size-specific fonts from the atlas
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas)
    {
        for (ImFont* font : atlas->Fonts)
        {
            if (!font)
                continue;
            float sz = font->LegacySize;
            if (sz >= 15.5f && sz <= 16.5f)
                fonts.font_16 = font;
            else if (sz >= 19.5f && sz <= 20.5f)
                fonts.font_20 = font;
            else if (sz >= 17.5f && sz <= 18.5f)
                fonts.font_24 = font;   // use 18px as closest to 24
        }
        // font_32 falls back to the largest available
        if (atlas->Fonts.Size > 0)
        {
            ImFont* largest = atlas->Fonts[0];
            for (ImFont* font : atlas->Fonts)
            {
                if (font && font->LegacySize > largest->LegacySize)
                    largest = font;
            }
            fonts.font_32 = largest;
        }
    }

    fonts_by_context_[context] = fonts;
    return true;
}

void IconFont::release_context(ImGuiContext* context)
{
    if (context)
        fonts_by_context_.erase(context);
}

ImFont* IconFont::get_font(float size) const
{
    if (!initialized_)
        return nullptr;

    auto it = fonts_by_context_.find(ImGui::GetCurrentContext());
    if (it == fonts_by_context_.end())
        return nullptr;

    const auto& fonts = it->second;
    if (size <= 16.0f)
        return fonts.font_16;
    if (size <= 20.0f)
        return fonts.font_20;
    if (size <= 24.0f)
        return fonts.font_24;
    return fonts.font_32;
}

void IconFont::draw(Icon icon, float size, const Color& color) const
{
    if (!initialized_)
        return;

    ImFont* font = get_font(size);
    if (!font)
        return;

    const char* icon_str = get_icon_string(icon);
    if (!icon_str)
        return;

    ImVec4 imgui_color(color.r, color.g, color.b, color.a);

    ImGui::PushFont(font);
    ImGui::TextColored(imgui_color, "%s", icon_str);
    ImGui::PopFont();
}

void IconFont::draw(Icon icon, float size, const ImVec4& color) const
{
    draw(icon, size, Color(color.x, color.y, color.z, color.w));
}

const char* IconFont::get_icon_string(Icon icon) const
{
    // The Icon enum values ARE the PUA codepoints (0xE001-0xE063).
    // Just look up the cached UTF-8 string.
    auto it = codepoint_strings_.find(static_cast<uint32_t>(icon));
    if (it != codepoint_strings_.end())
    {
        return it->second.c_str();
    }
    return "?";
}

float IconFont::get_width(Icon icon, float size) const
{
    if (!initialized_)
        return size;

    ImFont* font = get_font(size);
    if (!font)
        return size;

    const char* icon_str = get_icon_string(icon);
    if (!icon_str)
        return size;

    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon_str).x;
}

bool IconFont::has_icon(Icon icon) const
{
    if (icon_map_.empty())
        const_cast<IconFont*>(this)->build_icon_map();
    return icon_map_.find(icon) != icon_map_.end();
}

const std::vector<Icon>& IconFont::get_all_icons() const
{
    if (icon_map_.empty())
        const_cast<IconFont*>(this)->build_icon_map();
    static std::vector<Icon> all_icons;
    if (all_icons.empty())
    {
        for (const auto& [ic, _] : icon_map_)
        {
            all_icons.push_back(ic);
        }
    }
    return all_icons;
}

void IconFont::build_icon_map()
{
    icon_map_.clear();
    codepoint_strings_.clear();

    // Font Awesome 6 Solid codepoints are scattered across U+E000-U+E5FF
    // and U+F000-U+F8FF. Enumerate every Icon enum value explicitly.
    static constexpr Icon all[] = {
        Icon::ChartLine,
        Icon::ScatterChart,
        Icon::Axes,
        Icon::Wrench,
        Icon::Folder,
        Icon::Settings,
        Icon::Help,
        Icon::ZoomIn,
        Icon::Hand,
        Icon::Ruler,
        Icon::Crosshair,
        Icon::Pin,
        Icon::Type,
        Icon::Export,
        Icon::Save,
        Icon::Copy,
        Icon::Undo,
        Icon::Redo,
        Icon::Search,
        Icon::Filter,
        Icon::Check,
        Icon::Warning,
        Icon::Error,
        Icon::Info,
        Icon::ChevronRight,
        Icon::ChevronDown,
        Icon::Close,
        Icon::Menu,
        Icon::Maximize,
        Icon::Minimize,
        Icon::Eye,
        Icon::EyeOff,
        Icon::Palette,
        Icon::LineWidth,
        Icon::Plus,
        Icon::Minus,
        Icon::Play,
        Icon::Pause,
        Icon::Stop,
        Icon::StepForward,
        Icon::StepBackward,
        Icon::Sun,
        Icon::Moon,
        Icon::Contrast,
        Icon::Layout,
        Icon::SplitHorizontal,
        Icon::SplitVertical,
        Icon::Tab,
        Icon::LineChart,
        Icon::BarChart,
        Icon::PieChart,
        Icon::Heatmap,
        Icon::ArrowUp,
        Icon::ArrowDown,
        Icon::ArrowLeft,
        Icon::ArrowRight,
        Icon::Refresh,
        Icon::Clock,
        Icon::Calendar,
        Icon::Tag,
        Icon::Star,
        Icon::Link,
        Icon::Unlink,
        Icon::Lock,
        Icon::Unlock,
        Icon::Command,
        Icon::Keyboard,
        Icon::Shortcut,
        Icon::FolderOpen,
        Icon::File,
        Icon::FileText,
        Icon::Grid,
        Icon::List,
        Icon::Fullscreen,
        Icon::FullscreenExit,
        Icon::Edit,
        Icon::Scissors,
        Icon::Trash,
        Icon::Duplicate,
        Icon::Function,
        Icon::Integral,
        Icon::Sigma,
        Icon::Sqrt,
        Icon::Circle,
        Icon::Square,
        Icon::Triangle,
        Icon::Diamond,
        Icon::Cross,
        Icon::PlusMarker,
        Icon::MinusMarker,
        Icon::Asterisk,
        Icon::LineSolid,
        Icon::LineDashed,
        Icon::LineDotted,
        Icon::LineDashDot,
        Icon::Home,
        Icon::Back,
        Icon::Forward,
        Icon::Up,
        Icon::Down,
        // Vision nav rail icons
        Icon::MousePointer,
        Icon::Comment,
        Icon::VectorSquare,
        Icon::MapPin,
        Icon::MagicWand,
        Icon::Database,
        Icon::Timeline,
        Icon::Code,
        Icon::Broadcast,
        Icon::ChartArea,
        Icon::WindowIcon,
        Icon::PlayCircle,
        Icon::UserCircle,
    };

    for (Icon icon : all)
    {
        auto cp                = static_cast<uint32_t>(icon);
        icon_map_[icon]        = cp;
        codepoint_strings_[cp] = codepoint_to_utf8(cp);
    }
}

}   // namespace spectra::ui
