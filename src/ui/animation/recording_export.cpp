#include "ui/animation/recording_export.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <sstream>

#include "io/ffmpeg_command.hpp"

// Windows does not expose POSIX popen/pclose; use the MSVC equivalents.
#if defined(_WIN32)
    #define popen  _popen
    #define pclose _pclose
#endif

// Suppress warnings in third-party STB headers
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wall"
    #pragma clang diagnostic ignored "-Wextra"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif

// stb_image_write for PNG frame export
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
    #define STB_IMAGE_WRITE_STATIC
    #define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include "stb_image_write.h"

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace spectra
{

// ─── Wall clock helper ───────────────────────────────────────────────────────

static float get_wall_time()
{
    using Clock       = std::chrono::steady_clock;
    static auto start = Clock::now();
    auto        now   = Clock::now();
    return std::chrono::duration<float>(now - start).count();
}

// ─── RecordingSession ────────────────────────────────────────────────────────

RecordingSession::RecordingSession() = default;

RecordingSession::~RecordingSession()
{
    // Ensure cleanup
    std::lock_guard lock(mutex_);
#ifdef SPECTRA_USE_FFMPEG
    // Only close if we actually opened an MP4 pipe
    if (config_.format == RecordingFormat::MP4 && ffmpeg_pipe_ != nullptr)
    {
        pclose(ffmpeg_pipe_);
        ffmpeg_pipe_ = nullptr;
    }
#endif
}

// ─── Session lifecycle ───────────────────────────────────────────────────────

bool RecordingSession::begin(const RecordingConfig& config, FrameRenderCallback render_cb)
{
    std::lock_guard lock(mutex_);

    if (state_ == RecordingState::Recording || state_ == RecordingState::Encoding)
    {
        set_error("Recording already in progress");
        return false;
    }

    config_    = config;
    render_cb_ = std::move(render_cb);
    error_.clear();
    current_frame_   = 0;
    start_wall_time_ = get_wall_time();

    if (!render_cb_)
    {
        set_error("No render callback provided");
        state_ = RecordingState::Failed;
        return false;
    }

    if (!validate_config())
    {
        state_ = RecordingState::Failed;
        return false;
    }

    // Compute total frames
    float duration = config_.end_time - config_.start_time;
    if (duration <= 0.0f)
    {
        set_error("Invalid time range (end <= start)");
        state_ = RecordingState::Failed;
        return false;
    }
    total_frames_ = static_cast<uint32_t>(std::ceil(duration * config_.fps));
    if (total_frames_ == 0)
        total_frames_ = 1;

    // Compute digit count for PNG filenames
    png_frame_digits_ = 1;
    uint32_t n        = total_frames_;
    while (n >= 10)
    {
        n /= 10;
        png_frame_digits_++;
    }
    if (png_frame_digits_ < 4)
        png_frame_digits_ = 4;

    // Allocate frame buffer
    frame_buffer_.resize(static_cast<size_t>(config_.width) * config_.height * 4, 0);

    state_ = RecordingState::Preparing;

    if (!prepare_output())
    {
        state_ = RecordingState::Failed;
        return false;
    }

    state_ = RecordingState::Recording;
    return true;
}

bool RecordingSession::begin_multi_pane(const RecordingConfig& config, PaneRenderCallback pane_cb)
{
    std::lock_guard lock(mutex_);

    if (state_ == RecordingState::Recording || state_ == RecordingState::Encoding)
    {
        set_error("Recording already in progress");
        return false;
    }

    config_         = config;
    pane_render_cb_ = std::move(pane_cb);
    multi_pane_     = true;
    error_.clear();
    current_frame_   = 0;
    start_wall_time_ = get_wall_time();

    if (!pane_render_cb_)
    {
        set_error("No pane render callback provided");
        state_ = RecordingState::Failed;
        return false;
    }

    if (config_.pane_count < 1)
        config_.pane_count = 1;

    // Resolve pane rects: use provided rects or auto-grid
    resolved_pane_rects_.clear();
    if (!config_.pane_rects.empty())
    {
        resolved_pane_rects_ = config_.pane_rects;
    }
    else if (config_.pane_count > 1)
    {
        // Auto-grid layout: compute rows/cols
        auto cols =
            static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(config_.pane_count))));
        uint32_t rows = (config_.pane_count + cols - 1) / cols;
        float    pw   = 1.0f / static_cast<float>(cols);
        float    ph   = 1.0f / static_cast<float>(rows);
        for (uint32_t i = 0; i < config_.pane_count; ++i)
        {
            RecordingConfig::PaneRect r;
            r.x                = static_cast<float>(i % cols) * pw;
            const uint32_t row = i / cols;
            r.y                = static_cast<float>(row) * ph;
            r.w                = pw;
            r.h                = ph;
            resolved_pane_rects_.push_back(r);
        }
    }
    else
    {
        resolved_pane_rects_.push_back({0.0f, 0.0f, 1.0f, 1.0f});
    }

    // Create a wrapper FrameRenderCallback that composites panes
    render_cb_ = [this](uint32_t frame_index,
                        float    time,
                        uint8_t* rgba_buffer,
                        uint32_t w,
                        uint32_t h) -> bool
    {
        // Clear the composite buffer
        std::memset(rgba_buffer, 0, static_cast<size_t>(w) * h * 4);

        for (uint32_t pi = 0; pi < config_.pane_count; ++pi)
        {
            const auto& rect   = resolved_pane_rects_[pi];
            auto        pane_w = static_cast<uint32_t>(rect.w * static_cast<float>(w));
            auto        pane_h = static_cast<uint32_t>(rect.h * static_cast<float>(h));
            if (pane_w == 0 || pane_h == 0)
                continue;

            // Resize pane buffer if needed
            size_t pane_bytes = static_cast<size_t>(pane_w) * pane_h * 4;
            if (pane_buffer_.size() < pane_bytes)
            {
                pane_buffer_.resize(pane_bytes);
            }

            // Render this pane
            bool ok = pane_render_cb_(pi, frame_index, time, pane_buffer_.data(), pane_w, pane_h);
            if (!ok)
                return false;

            // Blit pane into composite buffer
            auto dst_x = static_cast<uint32_t>(rect.x * static_cast<float>(w));
            auto dst_y = static_cast<uint32_t>(rect.y * static_cast<float>(h));

            for (uint32_t row = 0; row < pane_h && (dst_y + row) < h; ++row)
            {
                uint32_t src_offset = row * pane_w * 4;
                uint32_t dst_offset = ((dst_y + row) * w + dst_x) * 4;
                uint32_t copy_w     = std::min(pane_w, w - dst_x);
                std::memcpy(rgba_buffer + dst_offset,
                            pane_buffer_.data() + src_offset,
                            static_cast<size_t>(copy_w) * 4);
            }
        }
        return true;
    };

    if (!validate_config())
    {
        state_ = RecordingState::Failed;
        return false;
    }

    // Compute total frames
    float duration = config_.end_time - config_.start_time;
    if (duration <= 0.0f)
    {
        set_error("Invalid time range (end <= start)");
        state_ = RecordingState::Failed;
        return false;
    }
    total_frames_ = static_cast<uint32_t>(std::ceil(duration * config_.fps));
    if (total_frames_ == 0)
        total_frames_ = 1;

    png_frame_digits_ = 1;
    uint32_t n        = total_frames_;
    while (n >= 10)
    {
        n /= 10;
        png_frame_digits_++;
    }
    if (png_frame_digits_ < 4)
        png_frame_digits_ = 4;

    frame_buffer_.resize(static_cast<size_t>(config_.width) * config_.height * 4, 0);

    state_ = RecordingState::Preparing;

    if (!prepare_output())
    {
        state_ = RecordingState::Failed;
        return false;
    }

    state_ = RecordingState::Recording;
    return true;
}

bool RecordingSession::advance()
{
    std::lock_guard lock(mutex_);

    if (state_ != RecordingState::Recording)
    {
        return false;
    }

    if (current_frame_ >= total_frames_)
    {
        return false;
    }

    // Compute time for this frame
    float t = frame_time(current_frame_);

    // Render the frame
    bool ok = render_cb_(current_frame_, t, frame_buffer_.data(), config_.width, config_.height);
    if (!ok)
    {
        set_error("Frame render callback failed at frame " + std::to_string(current_frame_));
        state_ = RecordingState::Failed;
        if (on_complete_)
            on_complete_(false);
        return false;
    }

    // Write frame to output
    switch (config_.format)
    {
        case RecordingFormat::PNG_Sequence:
            if (!write_png_frame())
            {
                state_ = RecordingState::Failed;
                if (on_complete_)
                    on_complete_(false);
                return false;
            }
            break;
        case RecordingFormat::GIF:
            if (!accumulate_gif_frame())
            {
                state_ = RecordingState::Failed;
                if (on_complete_)
                    on_complete_(false);
                return false;
            }
            break;
        case RecordingFormat::MP4:
            if (!write_mp4_frame())
            {
                state_ = RecordingState::Failed;
                if (on_complete_)
                    on_complete_(false);
                return false;
            }
            break;
    }

    current_frame_++;
    update_progress();

    return current_frame_ < total_frames_;
}

bool RecordingSession::run_all()
{
    // Don't hold lock during the entire run — advance() locks per-frame
    while (true)
    {
        bool more = advance();
        {
            std::lock_guard lock(mutex_);
            if (state_ == RecordingState::Failed || state_ == RecordingState::Cancelled)
            {
                return false;
            }
        }
        if (!more)
            break;
    }
    return finish();
}

bool RecordingSession::finish()
{
    std::lock_guard lock(mutex_);

    if (state_ != RecordingState::Recording && state_ != RecordingState::Encoding)
    {
        // Already finished or never started
        return state_ == RecordingState::Finished;
    }

    state_ = RecordingState::Encoding;

    bool success = true;

    switch (config_.format)
    {
        case RecordingFormat::PNG_Sequence:
            // Nothing to finalize for PNG sequence
            break;
        case RecordingFormat::GIF:
            success = write_gif();
            break;
        case RecordingFormat::MP4:
            success = finalize_mp4();
            break;
    }

    if (success)
    {
        state_ = RecordingState::Finished;
    }
    else
    {
        state_ = RecordingState::Failed;
    }

    if (on_complete_)
        on_complete_(success);
    return success;
}

void RecordingSession::cancel()
{
    std::lock_guard lock(mutex_);
    if (state_ == RecordingState::Recording || state_ == RecordingState::Encoding)
    {
        state_ = RecordingState::Cancelled;
#ifdef SPECTRA_USE_FFMPEG
        if (ffmpeg_pipe_)
        {
            pclose(ffmpeg_pipe_);
            ffmpeg_pipe_ = nullptr;
        }
#endif
        if (on_complete_)
            on_complete_(false);
    }
}

// ─── State queries ───────────────────────────────────────────────────────────

RecordingState RecordingSession::state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

bool RecordingSession::is_active() const
{
    std::lock_guard lock(mutex_);
    return state_ == RecordingState::Recording || state_ == RecordingState::Encoding
           || state_ == RecordingState::Preparing;
}

bool RecordingSession::is_finished() const
{
    std::lock_guard lock(mutex_);
    return state_ == RecordingState::Finished;
}

const RecordingConfig& RecordingSession::config() const
{
    std::lock_guard lock(mutex_);
    return config_;
}

RecordingProgress RecordingSession::progress() const
{
    std::lock_guard   lock(mutex_);
    RecordingProgress p;
    p.current_frame = current_frame_;
    p.total_frames  = total_frames_;
    p.percent =
        total_frames_ > 0
            ? (static_cast<float>(current_frame_) / static_cast<float>(total_frames_)) * 100.0f
            : 0.0f;
    p.elapsed_sec = get_wall_time() - start_wall_time_;
    if (current_frame_ > 0 && current_frame_ < total_frames_)
    {
        float per_frame           = p.elapsed_sec / static_cast<float>(current_frame_);
        p.estimated_remaining_sec = per_frame * static_cast<float>(total_frames_ - current_frame_);
    }
    p.cancelled = (state_ == RecordingState::Cancelled);
    return p;
}

std::string RecordingSession::error() const
{
    std::lock_guard lock(mutex_);
    return error_;
}

// ─── Frame data ──────────────────────────────────────────────────────────────

uint32_t RecordingSession::total_frames() const
{
    std::lock_guard lock(mutex_);
    return total_frames_;
}

uint32_t RecordingSession::current_frame() const
{
    std::lock_guard lock(mutex_);
    return current_frame_;
}

float RecordingSession::frame_time(uint32_t frame_index) const
{
    // Note: caller may or may not hold mutex_ — this is a pure computation
    if (config_.fps <= 0.0f)
        return config_.start_time;
    return config_.start_time + static_cast<float>(frame_index) / config_.fps;
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

void RecordingSession::set_on_progress(ProgressCallback cb)
{
    std::lock_guard lock(mutex_);
    on_progress_ = std::move(cb);
}

void RecordingSession::set_on_complete(std::function<void(bool)> cb)
{
    std::lock_guard lock(mutex_);
    on_complete_ = std::move(cb);
}

// ─── GIF utilities (static) ──────────────────────────────────────────────────

std::vector<Color> RecordingSession::median_cut(const uint8_t* rgba,
                                                size_t         pixel_count,
                                                uint32_t       max_colors)
{
    if (pixel_count == 0 || max_colors == 0)
        return {};

    struct ColorBox
    {
        std::vector<uint32_t> indices;
        uint8_t               r_min{}, r_max{}, g_min{}, g_max{}, b_min{}, b_max{};

        void compute_bounds(const uint8_t* data)
        {
            r_min = g_min = b_min = 255;
            r_max = g_max = b_max = 0;
            for (uint32_t idx : indices)
            {
                uint8_t r = data[idx * 4 + 0];
                uint8_t g = data[idx * 4 + 1];
                uint8_t b = data[idx * 4 + 2];
                r_min     = std::min(r_min, r);
                r_max     = std::max(r_max, r);
                g_min     = std::min(g_min, g);
                g_max     = std::max(g_max, g);
                b_min     = std::min(b_min, b);
                b_max     = std::max(b_max, b);
            }
        }

        Color average(const uint8_t* data) const
        {
            if (indices.empty())
                return {};
            uint64_t sr = 0;
            uint64_t sg = 0;
            uint64_t sb = 0;
            for (uint32_t idx : indices)
            {
                sr += data[idx * 4 + 0];
                sg += data[idx * 4 + 1];
                sb += data[idx * 4 + 2];
            }
            auto n = static_cast<float>(indices.size());
            return Color{static_cast<float>(sr) / (n * 255.0f),
                         static_cast<float>(sg) / (n * 255.0f),
                         static_cast<float>(sb) / (n * 255.0f),
                         1.0f};
        }

        [[nodiscard]] uint8_t longest_axis() const
        {
            uint8_t dr = r_max - r_min;
            uint8_t dg = g_max - g_min;
            uint8_t db = b_max - b_min;
            if (dr >= dg && dr >= db)
                return 0;
            if (dg >= dr && dg >= db)
                return 1;
            return 2;
        }
    };

    // Build initial box with all pixels
    ColorBox initial;
    initial.indices.resize(pixel_count);
    std::iota(initial.indices.begin(), initial.indices.end(), 0u);
    initial.compute_bounds(rgba);

    std::vector<ColorBox> boxes;
    boxes.push_back(std::move(initial));

    // Iteratively split the box with the largest range
    while (boxes.size() < max_colors)
    {
        // Find box with most pixels (and non-trivial range)
        size_t best      = 0;
        size_t best_size = 0;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            if (boxes[i].indices.size() > best_size
                && (boxes[i].r_max > boxes[i].r_min || boxes[i].g_max > boxes[i].g_min
                    || boxes[i].b_max > boxes[i].b_min))
            {
                best      = i;
                best_size = boxes[i].indices.size();
            }
        }
        if (best_size <= 1)
            break;

        auto&   box  = boxes[best];
        uint8_t axis = box.longest_axis();

        // Sort by the longest axis
        std::sort(box.indices.begin(),
                  box.indices.end(),
                  [rgba, axis](uint32_t a, uint32_t b)
                  { return rgba[a * 4 + axis] < rgba[b * 4 + axis]; });

        // Split at median
        size_t   mid = box.indices.size() / 2;
        ColorBox box2;
        box2.indices.assign(box.indices.begin() + static_cast<ptrdiff_t>(mid), box.indices.end());
        box.indices.resize(mid);

        box.compute_bounds(rgba);
        box2.compute_bounds(rgba);

        boxes.push_back(std::move(box2));
    }

    // Compute average color for each box
    std::vector<Color> palette;
    palette.reserve(boxes.size());
    for (const auto& box : boxes)
    {
        palette.push_back(box.average(rgba));
    }
    return palette;
}

uint8_t RecordingSession::nearest_palette_index(const std::vector<Color>& palette,
                                                uint8_t                   r,
                                                uint8_t                   g,
                                                uint8_t                   b)
{
    if (palette.empty())
        return 0;

    float fr = static_cast<float>(r) / 255.0f;
    float fg = static_cast<float>(g) / 255.0f;
    float fb = static_cast<float>(b) / 255.0f;

    uint8_t best      = 0;
    float   best_dist = 1e30f;
    for (size_t i = 0; i < palette.size(); ++i)
    {
        float dr   = fr - palette[i].r;
        float dg   = fg - palette[i].g;
        float db   = fb - palette[i].b;
        float dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist)
        {
            best_dist = dist;
            best      = static_cast<uint8_t>(i);
        }
    }
    return best;
}

void RecordingSession::quantize_frame(const uint8_t*        rgba,
                                      uint32_t              width,
                                      uint32_t              height,
                                      uint32_t              max_colors,
                                      std::vector<uint8_t>& palette_out,
                                      std::vector<uint8_t>& indexed_out)
{
    size_t pixel_count = static_cast<size_t>(width) * height;

    // Subsample for palette computation (max 10000 pixels)
    std::vector<uint8_t> sample_rgba;
    const uint8_t*       palette_src         = rgba;
    size_t               palette_pixel_count = pixel_count;

    if (pixel_count > 10000)
    {
        size_t stride = pixel_count / 10000;
        sample_rgba.reserve(10000 * 4);
        for (size_t i = 0; i < pixel_count; i += stride)
        {
            sample_rgba.push_back(rgba[i * 4 + 0]);
            sample_rgba.push_back(rgba[i * 4 + 1]);
            sample_rgba.push_back(rgba[i * 4 + 2]);
            sample_rgba.push_back(rgba[i * 4 + 3]);
        }
        palette_src         = sample_rgba.data();
        palette_pixel_count = sample_rgba.size() / 4;
    }

    auto colors = median_cut(palette_src, palette_pixel_count, max_colors);

    // Build RGB palette output
    palette_out.resize(colors.size() * 3);
    for (size_t i = 0; i < colors.size(); ++i)
    {
        palette_out[i * 3 + 0] = static_cast<uint8_t>(colors[i].r * 255.0f);
        palette_out[i * 3 + 1] = static_cast<uint8_t>(colors[i].g * 255.0f);
        palette_out[i * 3 + 2] = static_cast<uint8_t>(colors[i].b * 255.0f);
    }

    // Map each pixel to nearest palette entry
    indexed_out.resize(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i)
    {
        indexed_out[i] =
            nearest_palette_index(colors, rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    }
}

// ─── Internal helpers ────────────────────────────────────────────────────────

bool RecordingSession::validate_config() const
{
    // Caller must hold mutex_
    if (config_.output_path.empty())
    {
        const_cast<RecordingSession*>(this)->set_error("Output path is empty");
        return false;
    }
    if (config_.width == 0 || config_.height == 0)
    {
        const_cast<RecordingSession*>(this)->set_error("Invalid dimensions");
        return false;
    }
    if (config_.fps <= 0.0f)
    {
        const_cast<RecordingSession*>(this)->set_error("Invalid FPS");
        return false;
    }
    if (config_.format == RecordingFormat::MP4)
    {
#ifndef SPECTRA_USE_FFMPEG
        const_cast<RecordingSession*>(this)->set_error("MP4 export requires SPECTRA_USE_FFMPEG");
        return false;
#endif
    }
    return true;
}

bool RecordingSession::prepare_output()
{
    // Caller must hold mutex_
    namespace fs = std::filesystem;

    switch (config_.format)
    {
        case RecordingFormat::PNG_Sequence:
        {
            // Create output directory
            std::error_code ec;
            fs::create_directories(config_.output_path, ec);
            if (ec)
            {
                set_error("Failed to create directory: " + config_.output_path);
                return false;
            }
            return true;
        }
        case RecordingFormat::GIF:
        {
            gif_state_ = std::make_unique<GifState>();
            // Ensure parent directory exists
            auto parent = fs::path(config_.output_path).parent_path();
            if (!parent.empty())
            {
                std::error_code ec;
                fs::create_directories(parent, ec);
            }
            return true;
        }
        case RecordingFormat::MP4:
        {
#ifdef SPECTRA_USE_FFMPEG
            std::string error;
            if (!detail::ensure_ffmpeg_output_parent(config_.output_path, &error))
            {
                set_error(error);
                return false;
            }

            const detail::FfmpegCommandConfig ffmpeg_config{
                .output_path = config_.output_path,
                .width       = config_.width,
                .height      = config_.height,
                .fps         = config_.fps,
                .codec       = config_.codec,
                .pix_fmt     = config_.pix_fmt,
                .crf         = config_.crf,
            };

            const std::string cmd = detail::build_ffmpeg_command(ffmpeg_config);
            ffmpeg_pipe_          = popen(cmd.c_str(), "w");
            if (!ffmpeg_pipe_)
            {
                set_error("Failed to open ffmpeg pipe");
                return false;
            }
            return true;
#else
            set_error("MP4 export requires SPECTRA_USE_FFMPEG");
            return false;
#endif
        }
    }
    return false;
}

bool RecordingSession::write_png_frame()
{
    // Caller must hold mutex_
    std::ostringstream filename;
    filename << config_.output_path << "/frame_" << std::setfill('0')
             << std::setw(static_cast<int>(png_frame_digits_)) << current_frame_ << ".png";

    int result = stbi_write_png(filename.str().c_str(),
                                static_cast<int>(config_.width),
                                static_cast<int>(config_.height),
                                4,   // RGBA
                                frame_buffer_.data(),
                                static_cast<int>(config_.width * 4));

    if (!result)
    {
        set_error("Failed to write PNG frame: " + filename.str());
        return false;
    }
    return true;
}

bool RecordingSession::accumulate_gif_frame()
{
    // Caller must hold mutex_
    if (!gif_state_)
        return false;

    // Compute global palette from first frame
    if (!gif_state_->palette_computed)
    {
        auto colors = median_cut(frame_buffer_.data(),
                                 static_cast<size_t>(config_.width) * config_.height,
                                 config_.gif_palette_size);
        gif_state_->global_palette.resize(colors.size() * 3);
        for (size_t i = 0; i < colors.size(); ++i)
        {
            gif_state_->global_palette[i * 3 + 0] = static_cast<uint8_t>(colors[i].r * 255.0f);
            gif_state_->global_palette[i * 3 + 1] = static_cast<uint8_t>(colors[i].g * 255.0f);
            gif_state_->global_palette[i * 3 + 2] = static_cast<uint8_t>(colors[i].b * 255.0f);
        }
        gif_state_->palette_computed = true;
    }

    // Quantize this frame using the global palette
    size_t pixel_count    = static_cast<size_t>(config_.width) * config_.height;
    auto   palette_colors = median_cut(frame_buffer_.data(), pixel_count, config_.gif_palette_size);

    std::vector<uint8_t> indexed(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i)
    {
        indexed[i] = nearest_palette_index(palette_colors,
                                           frame_buffer_[i * 4 + 0],
                                           frame_buffer_[i * 4 + 1],
                                           frame_buffer_[i * 4 + 2]);
    }

    gif_state_->frames.push_back(std::move(indexed));
    return true;
}

bool RecordingSession::write_gif()
{
    // Caller must hold mutex_
    // Minimal GIF89a writer
    if (!gif_state_ || gif_state_->frames.empty())
    {
        set_error("No frames to write");
        return false;
    }

    FILE* fp = std::fopen(config_.output_path.c_str(), "wb");
    if (!fp)
    {
        set_error("Failed to open GIF output: " + config_.output_path);
        return false;
    }

    auto   w             = static_cast<uint16_t>(config_.width);
    auto   h             = static_cast<uint16_t>(config_.height);
    size_t palette_count = gif_state_->global_palette.size() / 3;
    if (palette_count == 0)
        palette_count = 1;

    // Find the smallest power of 2 >= palette_count
    int color_table_bits = 1;
    while ((1 << color_table_bits) < static_cast<int>(palette_count))
    {
        color_table_bits++;
    }
    if (color_table_bits > 8)
        color_table_bits = 8;
    int color_table_size = 1 << color_table_bits;

    // GIF Header
    std::fwrite("GIF89a", 1, 6, fp);

    // Logical Screen Descriptor
    std::fwrite(&w, 2, 1, fp);
    std::fwrite(&h, 2, 1, fp);
    uint8_t packed = 0x80 | ((color_table_bits - 1) & 0x07);   // Global color table flag + size
    std::fwrite(&packed, 1, 1, fp);
    uint8_t bg_index = 0;
    std::fwrite(&bg_index, 1, 1, fp);
    uint8_t aspect = 0;
    std::fwrite(&aspect, 1, 1, fp);

    // Global Color Table
    std::vector<uint8_t> gct(color_table_size * 3, 0);
    size_t               copy_bytes =
        std::min(gif_state_->global_palette.size(), static_cast<size_t>(color_table_size * 3));
    std::memcpy(gct.data(), gif_state_->global_palette.data(), copy_bytes);
    std::fwrite(gct.data(), 1, gct.size(), fp);

    // Netscape Application Extension (for looping)
    {
        uint8_t ext[] = {
            0x21,
            0xFF,
            0x0B,
            'N',
            'E',
            'T',
            'S',
            'C',
            'A',
            'P',
            'E',
            '2',
            '.',
            '0',
            0x03,
            0x01,
            0x00,
            0x00,   // Loop count (0 = infinite)
            0x00    // Block terminator
        };
        std::fwrite(ext, 1, sizeof(ext), fp);
    }

    // Frame delay in centiseconds
    auto delay_cs = static_cast<uint16_t>(std::round(100.0f / config_.fps));
    if (delay_cs < 2)
        delay_cs = 2;   // Minimum GIF delay

    int min_code_size = color_table_bits;
    if (min_code_size < 2)
        min_code_size = 2;

    for (const auto& indexed : gif_state_->frames)
    {
        // Graphic Control Extension
        uint8_t gce[] = {
            0x21,
            0xF9,
            0x04,
            0x00,   // Disposal method: none
            static_cast<uint8_t>(delay_cs & 0xFF),
            static_cast<uint8_t>((delay_cs >> 8) & 0xFF),
            0x00,   // Transparent color index (unused)
            0x00    // Block terminator
        };
        std::fwrite(gce, 1, sizeof(gce), fp);

        // Image Descriptor
        uint8_t img_desc = 0x2C;
        std::fwrite(&img_desc, 1, 1, fp);
        uint16_t left = 0;
        uint16_t top  = 0;
        std::fwrite(&left, 2, 1, fp);
        std::fwrite(&top, 2, 1, fp);
        std::fwrite(&w, 2, 1, fp);
        std::fwrite(&h, 2, 1, fp);
        uint8_t img_packed = 0x00;   // No local color table
        std::fwrite(&img_packed, 1, 1, fp);

        // LZW Minimum Code Size
        auto lzw_min = static_cast<uint8_t>(min_code_size);
        std::fwrite(&lzw_min, 1, 1, fp);

        // Simple uncompressed LZW encoding:
        // For simplicity, we use a basic approach that outputs clear codes
        // frequently to avoid building a large dictionary.
        int clear_code = 1 << min_code_size;
        int eoi_code   = clear_code + 1;
        int code_size  = min_code_size + 1;

        // Bit packing state
        std::vector<uint8_t> sub_block;
        sub_block.reserve(256);
        uint32_t bit_buffer = 0;
        int      bit_count  = 0;

        auto emit_code = [&](int code)
        {
            bit_buffer |= static_cast<uint32_t>(code) << bit_count;
            bit_count += code_size;
            while (bit_count >= 8)
            {
                sub_block.push_back(static_cast<uint8_t>(bit_buffer & 0xFF));
                bit_buffer >>= 8;
                bit_count -= 8;
                if (sub_block.size() == 255)
                {
                    uint8_t sz = 255;
                    std::fwrite(&sz, 1, 1, fp);
                    std::fwrite(sub_block.data(), 1, 255, fp);
                    sub_block.clear();
                }
            }
        };

        auto flush_bits = [&]()
        {
            if (bit_count > 0)
            {
                sub_block.push_back(static_cast<uint8_t>(bit_buffer & 0xFF));
                bit_buffer = 0;
                bit_count  = 0;
            }
            if (!sub_block.empty())
            {
                auto sz = static_cast<uint8_t>(sub_block.size());
                std::fwrite(&sz, 1, 1, fp);
                std::fwrite(sub_block.data(), 1, sub_block.size(), fp);
                sub_block.clear();
            }
        };

        // Emit clear code first
        emit_code(clear_code);

        // Emit each pixel as a literal code, resetting dictionary frequently
        int next_code = eoi_code + 1;
        int max_code  = (1 << code_size) - 1;

        for (unsigned char pi : indexed)
        {
            emit_code(pi);
            next_code++;
            if (next_code > max_code)
            {
                if (code_size < 12)
                {
                    code_size++;
                    max_code = (1 << code_size) - 1;
                }
                else
                {
                    // Reset
                    emit_code(clear_code);
                    code_size = min_code_size + 1;
                    next_code = eoi_code + 1;
                    max_code  = (1 << code_size) - 1;
                }
            }
        }

        emit_code(eoi_code);
        flush_bits();

        // Block terminator
        uint8_t zero = 0;
        std::fwrite(&zero, 1, 1, fp);
    }

    // GIF Trailer
    uint8_t trailer = 0x3B;
    std::fwrite(&trailer, 1, 1, fp);

    std::fclose(fp);

    gif_state_.reset();
    return true;
}

bool RecordingSession::write_mp4_frame()
{
    // Caller must hold mutex_
#ifdef SPECTRA_USE_FFMPEG
    if (!ffmpeg_pipe_)
    {
        set_error("ffmpeg pipe not open");
        return false;
    }

    size_t frame_bytes = static_cast<size_t>(config_.width) * config_.height * 4;
    size_t written     = std::fwrite(frame_buffer_.data(), 1, frame_bytes, ffmpeg_pipe_);
    if (written != frame_bytes)
    {
        set_error("Failed to write frame to ffmpeg pipe");
        return false;
    }
    return true;
#else
    set_error("MP4 export requires SPECTRA_USE_FFMPEG");
    return false;
#endif
}

bool RecordingSession::finalize_mp4()
{
    // Caller must hold mutex_
#ifdef SPECTRA_USE_FFMPEG
    if (ffmpeg_pipe_)
    {
        int result   = pclose(ffmpeg_pipe_);
        ffmpeg_pipe_ = nullptr;
        if (result != 0)
        {
            set_error("ffmpeg exited with non-zero status");
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

void RecordingSession::update_progress()
{
    // Caller must hold mutex_
    if (on_progress_)
    {
        RecordingProgress p;
        p.current_frame = current_frame_;
        p.total_frames  = total_frames_;
        p.percent =
            total_frames_ > 0
                ? (static_cast<float>(current_frame_) / static_cast<float>(total_frames_)) * 100.0f
                : 0.0f;
        p.elapsed_sec = get_wall_time() - start_wall_time_;
        if (current_frame_ > 0 && current_frame_ < total_frames_)
        {
            float per_frame = p.elapsed_sec / static_cast<float>(current_frame_);
            p.estimated_remaining_sec =
                per_frame * static_cast<float>(total_frames_ - current_frame_);
        }
        p.cancelled = false;
        on_progress_(p);
    }
}

void RecordingSession::set_error(const std::string& msg)
{
    // Caller must hold mutex_
    error_ = msg;
}

float RecordingSession::wall_time() const
{
    return get_wall_time();
}

}   // namespace spectra
