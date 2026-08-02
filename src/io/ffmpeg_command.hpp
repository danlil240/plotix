#pragma once

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace spectra::detail
{

struct FfmpegCommandConfig
{
    std::string output_path;
    uint32_t    width   = 1280;
    uint32_t    height  = 720;
    float       fps     = 60.0f;
    std::string codec   = "libx264";
    std::string pix_fmt = "yuv420p";
    int         crf     = -1;
};

inline std::string shell_quote(std::string_view value)
{
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (char ch : value)
    {
        if (ch == '\'')
        {
            quoted += "'\"'\"'";
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

inline bool ensure_ffmpeg_output_parent(const std::string& output_path, std::string* error)
{
    namespace fs = std::filesystem;

    const fs::path parent = fs::path(output_path).parent_path();
    if (parent.empty())
    {
        return true;
    }

    std::error_code ec;
    fs::create_directories(parent, ec);
    if (!ec)
    {
        return true;
    }

    if (error != nullptr)
    {
        *error = "Failed to create output directory: " + parent.string();
    }
    return false;
}

inline std::string build_ffmpeg_command(const FfmpegCommandConfig& config)
{
    std::ostringstream cmd;
    cmd << "ffmpeg -y -hide_banner -loglevel error -nostats" << " -f rawvideo"
        << " -vcodec rawvideo" << " -pix_fmt rgba" << " -s " << config.width << "x" << config.height
        << " -r " << std::fixed << std::setprecision(6) << config.fps << " -i -" << " -c:v "
        << shell_quote(config.codec) << " -pix_fmt " << shell_quote(config.pix_fmt);

    if (config.crf >= 0)
    {
        cmd << " -crf " << config.crf;
    }

    cmd << " " << shell_quote(config.output_path);
    return cmd.str();
}

}   // namespace spectra::detail