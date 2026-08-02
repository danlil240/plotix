#include "data_transform.hpp"

#include <algorithm>
#ifndef _USE_MATH_DEFINES
    #define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
#include <complex>
#include <limits>
#include <numeric>
#include <sstream>

namespace spectra
{

// ─── FFT internals (Cooley-Tukey radix-2 DIT) ──────────────────────────────

namespace
{

// Next power of two >= n
size_t next_power_of_two(size_t n)
{
    if (n == 0)
        return 1;
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// In-place iterative Cooley-Tukey radix-2 DIT FFT
void fft_radix2(std::vector<std::complex<float>>& buf)
{
    const size_t N = buf.size();
    if (N <= 1)
        return;

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < N; ++i)
    {
        size_t bit = N >> 1;
        while (j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j)
            std::swap(buf[i], buf[j]);
    }

    // Butterfly stages
    for (size_t len = 2; len <= N; len <<= 1)
    {
        const float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        const std::complex<float> wn(std::cos(angle), std::sin(angle));

        for (size_t i = 0; i < N; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                std::complex<float> u = buf[i + j];
                std::complex<float> v = buf[i + j + len / 2] * w;
                buf[i + j]            = u + v;
                buf[i + j + len / 2]  = u - v;
                w *= wn;
            }
        }
    }
}

template <class LogFn>
void apply_positive_log(std::span<const float> x_in,
                        std::span<const float> y_in,
                        std::vector<float>&    x_out,
                        std::vector<float>&    y_out,
                        LogFn                  log_fn)
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.clear();
    y_out.clear();
    x_out.reserve(n);
    y_out.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (y_in[i] > 0.0f)
        {
            x_out.push_back(x_in[i]);
            y_out.push_back(log_fn(y_in[i]));
        }
    }
}

template <class Fn>
void apply_elementwise_y(std::span<const float> x_in,
                         std::span<const float> y_in,
                         std::vector<float>&    x_out,
                         std::vector<float>&    y_out,
                         Fn                     fn)
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
    y_out.resize(n);
    for (size_t i = 0; i < n; ++i)
        y_out[i] = fn(y_in[i]);
}

}   // namespace

// ─── DataTransform construction ─────────────────────────────────────────────

DataTransform::DataTransform(TransformType type, const TransformParams& params)
    : type_(type), name_(transform_type_name(type)), params_(params)
{
}

DataTransform::DataTransform(const std::string& name, CustomFunc func)
    : type_(TransformType::Custom), name_(name), custom_func_(std::move(func))
{
}

DataTransform::DataTransform(const std::string& name, CustomXYFunc func)
    : type_(TransformType::Custom), name_(name), custom_xy_func_(std::move(func))
{
}

DataTransform DataTransform::unavailable_custom(const std::string& name, const std::string& source)
{
    DataTransform transform(name, CustomFunc{});
    transform.available_ = false;
    transform.source_    = source;
    return transform;
}

// ─── Apply ──────────────────────────────────────────────────────────────────

void DataTransform::apply_y(std::span<const float> x_in,
                            std::span<const float> y_in,
                            std::vector<float>&    x_out,
                            std::vector<float>&    y_out) const
{
    switch (type_)
    {
        case TransformType::Identity:
            apply_identity(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Log10:
            apply_log10(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Ln:
            apply_ln(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Abs:
            apply_abs(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Negate:
            apply_negate(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Normalize:
            apply_normalize(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Standardize:
            apply_standardize(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Derivative:
            apply_derivative(x_in, y_in, x_out, y_out);
            break;
        case TransformType::CumulativeSum:
            apply_cumulative_sum(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Diff:
            apply_diff(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Scale:
            apply_scale(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Offset:
            apply_offset(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Clamp:
            apply_clamp(x_in, y_in, x_out, y_out);
            break;
        case TransformType::FFT:
            apply_fft(x_in, y_in, x_out, y_out);
            break;
        case TransformType::Custom:
            if (custom_xy_func_)
            {
                custom_xy_func_(x_in, y_in, x_out, y_out);
            }
            else if (custom_func_)
            {
                const size_t n = std::min(x_in.size(), y_in.size());
                x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
                y_out.resize(n);
                for (size_t i = 0; i < n; ++i)
                {
                    y_out[i] = custom_func_(y_in[i]);
                }
            }
            else
            {
                apply_identity(x_in, y_in, x_out, y_out);
            }
            break;
    }
}

float DataTransform::apply_scalar(float value) const
{
    if (!is_elementwise())
        return std::numeric_limits<float>::quiet_NaN();

    switch (type_)
    {
        case TransformType::Identity:
            return value;
        case TransformType::Log10:
            return (value > 0.0f) ? std::log10(value) : std::numeric_limits<float>::quiet_NaN();
        case TransformType::Ln:
            return (value > 0.0f) ? std::log(value) : std::numeric_limits<float>::quiet_NaN();
        case TransformType::Abs:
            return std::abs(value);
        case TransformType::Negate:
            return -value;
        case TransformType::Scale:
            return value * params_.scale_factor;
        case TransformType::Offset:
            return value + params_.offset_value;
        case TransformType::Clamp:
            return std::clamp(value, params_.clamp_min, params_.clamp_max);
        case TransformType::Custom:
            if (custom_func_)
                return custom_func_(value);
            return value;
        default:
            return std::numeric_limits<float>::quiet_NaN();
    }
}

bool DataTransform::is_elementwise() const
{
    switch (type_)
    {
        case TransformType::Identity:
        case TransformType::Log10:
        case TransformType::Ln:
        case TransformType::Abs:
        case TransformType::Negate:
        case TransformType::Scale:
        case TransformType::Offset:
        case TransformType::Clamp:
            return true;
        case TransformType::Custom:
            return custom_func_ != nullptr && !custom_xy_func_;
        default:
            return false;
    }
}

bool DataTransform::changes_length() const
{
    switch (type_)
    {
        case TransformType::Derivative:
        case TransformType::Diff:
        case TransformType::Log10:
        case TransformType::Ln:   // may skip non-positive values
        case TransformType::FFT:  // output is N/2+1 frequency bins (left-sided)
            return true;
        case TransformType::Custom:
            return custom_xy_func_ != nullptr;
        default:
            return false;
    }
}

std::string DataTransform::description() const
{
    switch (type_)
    {
        case TransformType::Identity:
            return "Identity (no change)";
        case TransformType::Log10:
            return "Log10(y)";
        case TransformType::Ln:
            return "Ln(y)";
        case TransformType::Abs:
            return "|y|";
        case TransformType::Negate:
            return "-y";
        case TransformType::Normalize:
            return "Normalize to [0, 1]";
        case TransformType::Standardize:
            return "Standardize (z-score)";
        case TransformType::Derivative:
            return "dy/dx";
        case TransformType::CumulativeSum:
            return "Cumulative sum";
        case TransformType::Diff:
            return "First difference";
        case TransformType::Scale:
        {
            std::ostringstream ss;
            ss << "y * " << params_.scale_factor;
            return ss.str();
        }
        case TransformType::Offset:
        {
            std::ostringstream ss;
            ss << "y + " << params_.offset_value;
            return ss.str();
        }
        case TransformType::Clamp:
        {
            std::ostringstream ss;
            ss << "Clamp [" << params_.clamp_min << ", " << params_.clamp_max << "]";
            return ss.str();
        }
        case TransformType::FFT:
        {
            std::ostringstream ss;
            ss << "FFT magnitude";
            if (params_.fft_db)
                ss << " (dB)";
            return ss.str();
        }
        case TransformType::Custom:
            return "Custom: " + name_;
    }
    return "Unknown";
}

// ─── Built-in transform implementations ─────────────────────────────────────

void DataTransform::apply_identity(std::span<const float> x_in,
                                   std::span<const float> y_in,
                                   std::vector<float>&    x_out,
                                   std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
    y_out.assign(y_in.begin(), y_in.begin() + static_cast<std::ptrdiff_t>(n));
}

void DataTransform::apply_log10(std::span<const float> x_in,
                                std::span<const float> y_in,
                                std::vector<float>&    x_out,
                                std::vector<float>&    y_out) const
{
    apply_positive_log(x_in, y_in, x_out, y_out, [](float v) { return std::log10(v); });
}

void DataTransform::apply_ln(std::span<const float> x_in,
                             std::span<const float> y_in,
                             std::vector<float>&    x_out,
                             std::vector<float>&    y_out) const
{
    apply_positive_log(x_in, y_in, x_out, y_out, [](float v) { return std::log(v); });
}

void DataTransform::apply_abs(std::span<const float> x_in,
                              std::span<const float> y_in,
                              std::vector<float>&    x_out,
                              std::vector<float>&    y_out) const
{
    apply_elementwise_y(x_in, y_in, x_out, y_out, [](float v) { return std::abs(v); });
}

void DataTransform::apply_negate(std::span<const float> x_in,
                                 std::span<const float> y_in,
                                 std::vector<float>&    x_out,
                                 std::vector<float>&    y_out) const
{
    apply_elementwise_y(x_in, y_in, x_out, y_out, [](float v) { return -v; });
}

void DataTransform::apply_normalize(std::span<const float> x_in,
                                    std::span<const float> y_in,
                                    std::vector<float>&    x_out,
                                    std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
    y_out.resize(n);

    if (n == 0)
        return;

    float ymin = y_in[0];
    float ymax = y_in[0];
    for (size_t i = 1; i < n; ++i)
    {
        if (y_in[i] < ymin)
            ymin = y_in[i];
        if (y_in[i] > ymax)
            ymax = y_in[i];
    }

    float range = ymax - ymin;
    if (range == 0.0f)
    {
        // All values are the same — map to 0.5
        std::fill(y_out.begin(), y_out.end(), 0.5f);
        return;
    }

    float inv_range = 1.0f / range;
    for (size_t i = 0; i < n; ++i)
    {
        y_out[i] = (y_in[i] - ymin) * inv_range;
    }
}

void DataTransform::apply_standardize(std::span<const float> x_in,
                                      std::span<const float> y_in,
                                      std::vector<float>&    x_out,
                                      std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
    y_out.resize(n);

    if (n == 0)
        return;

    // Compute mean
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
        sum += static_cast<double>(y_in[i]);
    double mean = sum / static_cast<double>(n);

    // Compute stddev
    double var_sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double d = static_cast<double>(y_in[i]) - mean;
        var_sum += d * d;
    }
    double stddev = std::sqrt(var_sum / static_cast<double>(n));

    if (stddev == 0.0)
    {
        std::fill(y_out.begin(), y_out.end(), 0.0f);
        return;
    }

    double inv_std = 1.0 / stddev;
    for (size_t i = 0; i < n; ++i)
    {
        y_out[i] = static_cast<float>((static_cast<double>(y_in[i]) - mean) * inv_std);
    }
}

void DataTransform::apply_derivative(std::span<const float> x_in,
                                     std::span<const float> y_in,
                                     std::vector<float>&    x_out,
                                     std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    if (n < 2)
    {
        x_out.clear();
        y_out.clear();
        return;
    }

    // Central differences for interior, forward/backward for endpoints
    const size_t out_n = n - 1;
    x_out.resize(out_n);
    y_out.resize(out_n);

    for (size_t i = 0; i < out_n; ++i)
    {
        float dx = x_in[i + 1] - x_in[i];
        float dy = y_in[i + 1] - y_in[i];
        // Midpoint x
        x_out[i] = (x_in[i] + x_in[i + 1]) * 0.5f;
        y_out[i] = (dx != 0.0f) ? (dy / dx) : 0.0f;
    }
}

void DataTransform::apply_cumulative_sum(std::span<const float> x_in,
                                         std::span<const float> y_in,
                                         std::vector<float>&    x_out,
                                         std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
    y_out.resize(n);

    if (n == 0)
        return;

    double running = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        running += static_cast<double>(y_in[i]);
        y_out[i] = static_cast<float>(running);
    }
}

void DataTransform::apply_diff(std::span<const float> x_in,
                               std::span<const float> y_in,
                               std::vector<float>&    x_out,
                               std::vector<float>&    y_out) const
{
    const size_t n = std::min(x_in.size(), y_in.size());
    if (n < 2)
    {
        x_out.clear();
        y_out.clear();
        return;
    }

    const size_t out_n = n - 1;
    x_out.resize(out_n);
    y_out.resize(out_n);

    for (size_t i = 0; i < out_n; ++i)
    {
        x_out[i] = x_in[i + 1];
        y_out[i] = y_in[i + 1] - y_in[i];
    }
}

void DataTransform::apply_scale(std::span<const float> x_in,
                                std::span<const float> y_in,
                                std::vector<float>&    x_out,
                                std::vector<float>&    y_out) const
{
    const float s = params_.scale_factor;
    apply_elementwise_y(x_in, y_in, x_out, y_out, [s](float v) { return v * s; });
}

void DataTransform::apply_offset(std::span<const float> x_in,
                                 std::span<const float> y_in,
                                 std::vector<float>&    x_out,
                                 std::vector<float>&    y_out) const
{
    const float o = params_.offset_value;
    apply_elementwise_y(x_in, y_in, x_out, y_out, [o](float v) { return v + o; });
}

void DataTransform::apply_clamp(std::span<const float> x_in,
                                std::span<const float> y_in,
                                std::vector<float>&    x_out,
                                std::vector<float>&    y_out) const
{
    const float lo = params_.clamp_min;
    const float hi = params_.clamp_max;
    apply_elementwise_y(x_in,
                        y_in,
                        x_out,
                        y_out,
                        [lo, hi](float v) { return std::clamp(v, lo, hi); });
}

// ─── FFT (left-sided magnitude spectrum) ────────────────────────────────────
//
// Computes the one-sided (left-sided) FFT of the input signal:
//   - Zero-pads input to the next power of two
//   - Applies Cooley-Tukey radix-2 DIT FFT
//   - Outputs N/2+1 frequency bins (DC to Nyquist)
//   - X-axis: frequency in Hz (using params_.fft_sample_rate)
//   - Y-axis: magnitude (or dB if params_.fft_db is true)
//   - Magnitudes are normalized by 2/N (except DC and Nyquist which are 1/N)

void DataTransform::apply_fft(std::span<const float> x_in,
                              std::span<const float> y_in,
                              std::vector<float>&    x_out,
                              std::vector<float>&    y_out) const
{
    const size_t n = y_in.size();
    if (n == 0)
    {
        x_out.clear();
        y_out.clear();
        return;
    }

    // Zero-pad to next power of two
    const size_t N = next_power_of_two(n);

    // Fill complex buffer (real signal, zero imaginary)
    std::vector<std::complex<float>> buf(N, {0.0f, 0.0f});
    for (size_t i = 0; i < n; ++i)
    {
        buf[i] = {y_in[i], 0.0f};
    }

    // In-place FFT
    fft_radix2(buf);

    // Left-sided: output bins 0..N/2 (DC to Nyquist inclusive)
    const size_t out_n = N / 2 + 1;
    const float  inv_n = 1.0f / static_cast<float>(N);

    // Calculate sampling rate: use explicit parameter if provided, otherwise infer from x data
    float sample_rate = 1.0f;   // default fallback
    if (params_.fft_sample_rate > 0.0f)
    {
        sample_rate = params_.fft_sample_rate;
    }
    else if (x_in.size() >= 2)
    {
        // Infer sampling rate from x data: 1 / (x[1] - x[0])

        // calculat mean dt:
        float dt = 0.0f;
        for (size_t i = 1; i < x_in.size(); ++i)
        {
            dt += x_in[i] - x_in[i - 1];
        }
        dt /= static_cast<float>(x_in.size() - 1);

        if (dt > 0.0f)
        {
            sample_rate = 1.0f / dt;
        }
    }

    const float freq_step = sample_rate / static_cast<float>(N);

    x_out.resize(out_n);
    y_out.resize(out_n);

    for (size_t i = 0; i < out_n; ++i)
    {
        x_out[i] = static_cast<float>(i) * freq_step;

        float mag = std::abs(buf[i]) * inv_n;
        // Double the magnitude for non-DC, non-Nyquist bins (energy from negative freqs)
        if (i > 0 && i < N / 2)
        {
            mag *= 2.0f;
        }

        if (params_.fft_db)
        {
            // Convert to dB: 20*log10(mag), floor at -200 dB
            y_out[i] = (mag > 0.0f) ? 20.0f * std::log10(mag) : -200.0f;
        }
        else
        {
            y_out[i] = mag;
        }
    }
}

// ─── TransformPipeline ──────────────────────────────────────────────────────

void TransformPipeline::push_back(const DataTransform& transform)
{
    steps_.push_back({transform, true});
}

void TransformPipeline::push_back(DataTransform&& transform)
{
    steps_.push_back({std::move(transform), true});
}

void TransformPipeline::insert(size_t index, const DataTransform& transform)
{
    if (index > steps_.size())
        index = steps_.size();
    steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(index), {transform, true});
}

void TransformPipeline::remove(size_t index)
{
    if (index < steps_.size())
    {
        steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void TransformPipeline::clear()
{
    steps_.clear();
}

void TransformPipeline::move_step(size_t from, size_t to)
{
    if (from >= steps_.size() || to >= steps_.size() || from == to)
        return;
    auto step = std::move(steps_[from]);
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(from));
    steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(to), std::move(step));
}

void TransformPipeline::set_enabled(size_t index, bool enabled)
{
    if (index < steps_.size())
    {
        steps_[index].enabled = enabled;
    }
}

bool TransformPipeline::is_enabled(size_t index) const
{
    if (index < steps_.size())
        return steps_[index].enabled;
    return false;
}

void TransformPipeline::apply(std::span<const float> x_in,
                              std::span<const float> y_in,
                              std::vector<float>&    x_out,
                              std::vector<float>&    y_out) const
{
    if (is_identity())
    {
        const size_t n = std::min(x_in.size(), y_in.size());
        x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
        y_out.assign(y_in.begin(), y_in.begin() + static_cast<std::ptrdiff_t>(n));
        return;
    }

    // Apply first enabled step
    std::vector<float> tmp_x1;
    std::vector<float> tmp_y1;
    std::vector<float> tmp_x2;
    std::vector<float> tmp_y2;
    bool               first_step = true;

    for (const auto& step : steps_)
    {
        if (!step.enabled || !step.transform.available())
            continue;

        if (first_step)
        {
            step.transform.apply_y(x_in, y_in, tmp_x1, tmp_y1);
            first_step = false;
        }
        else
        {
            // Ping-pong between buffers
            step.transform.apply_y(tmp_x1, tmp_y1, tmp_x2, tmp_y2);
            std::swap(tmp_x1, tmp_x2);
            std::swap(tmp_y1, tmp_y2);
        }
    }

    if (first_step)
    {
        // No enabled steps — identity
        const size_t n = std::min(x_in.size(), y_in.size());
        x_out.assign(x_in.begin(), x_in.begin() + static_cast<std::ptrdiff_t>(n));
        y_out.assign(y_in.begin(), y_in.begin() + static_cast<std::ptrdiff_t>(n));
    }
    else
    {
        x_out = std::move(tmp_x1);
        y_out = std::move(tmp_y1);
    }
}

std::string TransformPipeline::description() const
{
    if (steps_.empty())
        return "Empty pipeline";

    std::ostringstream ss;
    bool               first = true;
    for (const auto& step : steps_)
    {
        if (!step.enabled)
            continue;
        if (!first)
            ss << " → ";
        first = false;
        ss << step.transform.description();
    }
    return first ? "All steps disabled" : ss.str();
}

bool TransformPipeline::is_identity() const
{
    if (steps_.empty())
        return true;
    for (const auto& step : steps_)
    {
        if (step.enabled && step.transform.available()
            && step.transform.type() != TransformType::Identity)
        {
            return false;
        }
    }
    return true;
}

// ─── TransformRegistry ──────────────────────────────────────────────────────

TransformRegistry::TransformRegistry()
{
    register_builtins();
}

TransformRegistry& TransformRegistry::instance()
{
    static TransformRegistry inst;
    return inst;
}

void TransformRegistry::register_transform(const std::string&        name,
                                           DataTransform::CustomFunc func,
                                           const std::string&        desc)
{
    std::lock_guard lock(mutex_);
    custom_transforms_[name] = {DataTransform(name, std::move(func)), desc};
    if (registration_capture_
        && std::ranges::find(*registration_capture_, name) == registration_capture_->end())
        registration_capture_->push_back(name);
}

void TransformRegistry::register_xy_transform(const std::string&          name,
                                              DataTransform::CustomXYFunc func,
                                              const std::string&          desc)
{
    std::lock_guard lock(mutex_);
    custom_transforms_[name] = {DataTransform(name, std::move(func)), desc};
    if (registration_capture_
        && std::ranges::find(*registration_capture_, name) == registration_capture_->end())
        registration_capture_->push_back(name);
}

bool TransformRegistry::unregister_transform(const std::string& name)
{
    std::lock_guard lock(mutex_);
    return custom_transforms_.erase(name) > 0;
}

bool TransformRegistry::set_transform_source(const std::string& name, const std::string& source)
{
    std::lock_guard lock(mutex_);
    const auto      it = custom_transforms_.find(name);
    if (it == custom_transforms_.end())
        return false;
    it->second.transform.set_source(source);
    return true;
}

std::string TransformRegistry::transform_source(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    const auto      it = custom_transforms_.find(name);
    return it == custom_transforms_.end() ? std::string{} : it->second.transform.source();
}

void TransformRegistry::begin_registration_capture(std::vector<std::string>* names)
{
    std::lock_guard lock(mutex_);
    registration_capture_ = names;
}

void TransformRegistry::end_registration_capture()
{
    std::lock_guard lock(mutex_);
    registration_capture_ = nullptr;
}

bool TransformRegistry::get_transform(const std::string& name, DataTransform& out) const
{
    std::lock_guard lock(mutex_);

    // Check custom transforms first
    auto it = custom_transforms_.find(name);
    if (it != custom_transforms_.end())
    {
        out = it->second.transform;
        return true;
    }

    // Resolve built-in type names
    static const std::unordered_map<std::string, TransformType> builtin_map = {
        {"Identity", TransformType::Identity},
        {"Log10", TransformType::Log10},
        {"Ln", TransformType::Ln},
        {"Abs", TransformType::Abs},
        {"Negate", TransformType::Negate},
        {"Normalize", TransformType::Normalize},
        {"Standardize", TransformType::Standardize},
        {"Derivative", TransformType::Derivative},
        {"CumulativeSum", TransformType::CumulativeSum},
        {"Diff", TransformType::Diff},
        {"Scale", TransformType::Scale},
        {"Offset", TransformType::Offset},
        {"Clamp", TransformType::Clamp},
        {"FFT", TransformType::FFT},
    };
    auto bit = builtin_map.find(name);
    if (bit != builtin_map.end())
    {
        out = create(bit->second);
        return true;
    }

    return false;
}

std::vector<std::string> TransformRegistry::available_transforms() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;

    // Built-in transforms
    names.emplace_back("Identity");
    names.emplace_back("Log10");
    names.emplace_back("Ln");
    names.emplace_back("Abs");
    names.emplace_back("Negate");
    names.emplace_back("Normalize");
    names.emplace_back("Standardize");
    names.emplace_back("Derivative");
    names.emplace_back("CumulativeSum");
    names.emplace_back("Diff");
    names.emplace_back("Scale");
    names.emplace_back("Offset");
    names.emplace_back("Clamp");
    names.emplace_back("FFT");

    // Custom transforms
    for (const auto& [k, _] : custom_transforms_)
    {
        names.push_back(k);
    }

    return names;
}

void TransformRegistry::save_pipeline(const std::string& name, const TransformPipeline& pipeline)
{
    std::lock_guard lock(mutex_);
    saved_pipelines_[name] = pipeline;
}

bool TransformRegistry::load_pipeline(const std::string& name, TransformPipeline& out) const
{
    std::lock_guard lock(mutex_);
    auto            it = saved_pipelines_.find(name);
    if (it != saved_pipelines_.end())
    {
        out = it->second;
        return true;
    }
    return false;
}

std::vector<std::string> TransformRegistry::saved_pipelines() const
{
    std::lock_guard          lock(mutex_);
    std::vector<std::string> names;
    names.reserve(saved_pipelines_.size());
    for (const auto& [k, _] : saved_pipelines_)
    {
        names.push_back(k);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool TransformRegistry::remove_pipeline(const std::string& name)
{
    std::lock_guard lock(mutex_);
    return saved_pipelines_.erase(name) > 0;
}

DataTransform TransformRegistry::create(TransformType type, const TransformParams& params)
{
    return DataTransform(type, params);
}

void TransformRegistry::register_builtins()
{
    // Built-in convenience transforms registered as custom entries
    // so they can be looked up by name
    register_transform("square", [](float v) { return v * v; }, "y²");
    register_transform("sqrt", [](float v) { return (v >= 0.0f) ? std::sqrt(v) : 0.0f; }, "√y");
    register_transform(
        "reciprocal",
        [](float v) { return (v != 0.0f) ? (1.0f / v) : 0.0f; },
        "1/y");
    register_transform("exp", [](float v) { return std::exp(v); }, "e^y");
    register_transform("sin", [](float v) { return std::sin(v); }, "sin(y)");
    register_transform("cos", [](float v) { return std::cos(v); }, "cos(y)");
}

// ─── Free functions ─────────────────────────────────────────────────────────

std::vector<float> transform_y(std::span<const float> y,
                               TransformType          type,
                               const TransformParams& params)
{
    DataTransform t(type, params);
    // Create dummy x data [0, 1, 2, ...]
    std::vector<float> x_in(y.size());
    std::iota(x_in.begin(), x_in.end(), 0.0f);

    std::vector<float> x_out;
    std::vector<float> y_out;
    t.apply_y(x_in, y, x_out, y_out);
    return y_out;
}

void transform_xy(std::span<const float> x_in,
                  std::span<const float> y_in,
                  std::vector<float>&    x_out,
                  std::vector<float>&    y_out,
                  TransformType          type,
                  const TransformParams& params)
{
    DataTransform t(type, params);
    t.apply_y(x_in, y_in, x_out, y_out);
}

const char* transform_type_name(TransformType type)
{
    switch (type)
    {
        case TransformType::Identity:
            return "Identity";
        case TransformType::Log10:
            return "Log10";
        case TransformType::Ln:
            return "Ln";
        case TransformType::Abs:
            return "Abs";
        case TransformType::Negate:
            return "Negate";
        case TransformType::Normalize:
            return "Normalize";
        case TransformType::Standardize:
            return "Standardize";
        case TransformType::Derivative:
            return "Derivative";
        case TransformType::CumulativeSum:
            return "CumulativeSum";
        case TransformType::Diff:
            return "Diff";
        case TransformType::Scale:
            return "Scale";
        case TransformType::Offset:
            return "Offset";
        case TransformType::Clamp:
            return "Clamp";
        case TransformType::FFT:
            return "FFT";
        case TransformType::Custom:
            return "Custom";
    }
    return "Unknown";
}

}   // namespace spectra
