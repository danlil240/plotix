#include <ranges>
#include <spectra/series_stats.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace spectra
{

BandSeries::BandSeries(std::span<const float> x,
                       std::span<const float> lower,
                       std::span<const float> upper)
{
    set_data(x, lower, upper);
}

BandSeries& BandSeries::set_data(std::span<const float> x,
                                 std::span<const float> lower,
                                 std::span<const float> upper)
{
    if (x.size() != lower.size() || x.size() != upper.size())
        throw std::invalid_argument("BandSeries requires equally sized x, lower, and upper arrays");

    x_.assign(x.begin(), x.end());
    lower_.assign(lower.begin(), lower.end());
    upper_.assign(upper.begin(), upper.end());
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

void BandSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();

    if (x_.empty())
        return;

    line_x_.reserve(x_.size() * 2 + 1);
    line_y_.reserve(x_.size() * 2 + 1);
    for (size_t i = 0; i < x_.size(); ++i)
    {
        line_x_.push_back(x_[i]);
        line_y_.push_back(upper_[i]);
    }
    line_x_.push_back(std::numeric_limits<float>::quiet_NaN());
    line_y_.push_back(std::numeric_limits<float>::quiet_NaN());
    for (size_t i = 0; i < x_.size(); ++i)
    {
        line_x_.push_back(x_[i]);
        line_y_.push_back(lower_[i]);
    }

    if (x_.size() < 2)
        return;

    fill_verts_.reserve((x_.size() - 1) * 18);
    auto push_vertex = [this](float x_value, float y_value)
    {
        fill_verts_.push_back(x_value);
        fill_verts_.push_back(y_value);
        fill_verts_.push_back(1.0f);
    };

    for (size_t i = 0; i + 1 < x_.size(); ++i)
    {
        if (!std::isfinite(x_[i]) || !std::isfinite(x_[i + 1]) || !std::isfinite(lower_[i])
            || !std::isfinite(lower_[i + 1]) || !std::isfinite(upper_[i])
            || !std::isfinite(upper_[i + 1]))
        {
            continue;
        }

        push_vertex(x_[i], lower_[i]);
        push_vertex(x_[i], upper_[i]);
        push_vertex(x_[i + 1], upper_[i + 1]);
        push_vertex(x_[i], lower_[i]);
        push_vertex(x_[i + 1], upper_[i + 1]);
        push_vertex(x_[i + 1], lower_[i + 1]);
    }
}

// ─── Helper: percentile via linear interpolation ────────────────────────────

static float percentile(std::vector<float>& sorted, float p)
{
    if (sorted.empty())
        return 0.0f;
    if (sorted.size() == 1)
        return sorted[0];

    float idx = p * static_cast<float>(sorted.size() - 1);
    auto  lo  = static_cast<size_t>(std::floor(idx));
    auto  hi  = static_cast<size_t>(std::ceil(idx));
    if (lo == hi)
        return sorted[lo];
    float frac = idx - static_cast<float>(lo);
    return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

// ─── Helper: Gaussian KDE ───────────────────────────────────────────────────

static float gaussian_kde(float x, const std::vector<float>& data, float bandwidth)
{
    float sum = 0.0f;
    auto  n   = static_cast<float>(data.size());
    for (float d : data)
    {
        float z = (x - d) / bandwidth;
        sum += std::exp(-0.5f * z * z);
    }
    constexpr float INV_SQRT_2PI = 0.3989422804014327f;
    return sum * INV_SQRT_2PI / (n * bandwidth);
}

// ─── NaN constant for line breaks ───────────────────────────────────────────

static constexpr float NAN_BREAK = std::numeric_limits<float>::quiet_NaN();

// ─── Helper: emit vertex {x, y, alpha} into interleaved buffer ─────────────
static void emit_vert(std::vector<float>& buf, float x, float y, float alpha)
{
    buf.push_back(x);
    buf.push_back(y);
    buf.push_back(alpha);
}

// ─── Helper: compute gradient alpha for a horizontal position ──────────────
// Returns alpha in [lo_alpha, hi_alpha] based on where x falls in [x_min, x_max].
// Left edge is brighter (hi_alpha), right edge is darker (lo_alpha).
static float grad_alpha(float x,
                        float x_min,
                        float x_max,
                        bool  gradient,
                        float hi_alpha = 1.0f,
                        float lo_alpha = 0.45f)
{
    if (!gradient)
        return 1.0f;
    if (x_max <= x_min)
        return hi_alpha;
    float t = (x - x_min) / (x_max - x_min);   // 0 = left, 1 = right
    return hi_alpha + t * (lo_alpha - hi_alpha);
}

// ─── Helper: emit a filled quad as 2 triangles (6 vertices) with gradient ──
static void emit_filled_quad(std::vector<float>& buf,
                             float               x0,
                             float               y0,
                             float               x1,
                             float               y1,
                             bool                gradient)
{
    float a0 = 1.0f;                      // left alpha (always bright)
    float a1 = gradient ? 0.45f : 1.0f;   // right alpha (dim)
    // Triangle 1: bottom-left, bottom-right, top-left
    emit_vert(buf, x0, y0, a0);
    emit_vert(buf, x1, y0, a1);
    emit_vert(buf, x0, y1, a0);
    // Triangle 2: top-left, bottom-right, top-right
    emit_vert(buf, x0, y1, a0);
    emit_vert(buf, x1, y0, a1);
    emit_vert(buf, x1, y1, a1);
}

// ─── Helper: emit a filled triangle with per-vertex alpha ──────────────────
static void emit_filled_tri(std::vector<float>& buf,
                            float               x0,
                            float               y0,
                            float               a0,
                            float               x1,
                            float               y1,
                            float               a1,
                            float               x2,
                            float               y2,
                            float               a2)
{
    emit_vert(buf, x0, y0, a0);
    emit_vert(buf, x1, y1, a1);
    emit_vert(buf, x2, y2, a2);
}

// ═══════════════════════════════════════════════════════════════════════════
// BoxPlotSeries
// ═══════════════════════════════════════════════════════════════════════════

BoxPlotStats BoxPlotSeries::compute_stats(std::span<const float> values)
{
    BoxPlotStats result;
    if (values.empty())
        return result;

    std::vector<float> sorted(values.begin(), values.end());
    // Remove NaN
    sorted.erase(
        std::remove_if(sorted.begin(), sorted.end(), [](float v) { return std::isnan(v); }),
        sorted.end());
    if (sorted.empty())
        return result;

    std::sort(sorted.begin(), sorted.end());

    result.median = percentile(sorted, 0.5f);
    result.q1     = percentile(sorted, 0.25f);
    result.q3     = percentile(sorted, 0.75f);

    float iqr        = result.q3 - result.q1;
    float low_fence  = result.q1 - 1.5f * iqr;
    float high_fence = result.q3 + 1.5f * iqr;

    // Whiskers extend to the most extreme data point within the fences
    result.whisker_low  = sorted.front();
    result.whisker_high = sorted.back();
    for (float v : sorted)
    {
        if (v >= low_fence)
        {
            result.whisker_low = v;
            break;
        }
    }
    for (float& it : std::ranges::reverse_view(sorted))
    {
        if (it <= high_fence)
        {
            result.whisker_high = it;
            break;
        }
    }

    // Outliers
    for (float v : sorted)
    {
        if (v < low_fence || v > high_fence)
        {
            result.outliers.push_back(v);
        }
    }

    return result;
}

BoxPlotSeries& BoxPlotSeries::add_box(float x_position, std::span<const float> values)
{
    auto s = compute_stats(values);
    positions_.push_back(x_position);
    stats_.push_back(std::move(s));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

BoxPlotSeries& BoxPlotSeries::add_box(float                  x_position,
                                      float                  median,
                                      float                  q1,
                                      float                  q3,
                                      float                  whisker_low,
                                      float                  whisker_high,
                                      std::span<const float> outliers)
{
    BoxPlotStats s;
    s.median       = median;
    s.q1           = q1;
    s.q3           = q3;
    s.whisker_low  = whisker_low;
    s.whisker_high = whisker_high;
    s.outliers.assign(outliers.begin(), outliers.end());

    positions_.push_back(x_position);
    stats_.push_back(std::move(s));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void BoxPlotSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();
    outlier_x_.clear();
    outlier_y_.clear();

    float hw = box_width_ * 0.5f;

    for (size_t i = 0; i < positions_.size(); ++i)
    {
        float               x = positions_[i];
        const BoxPlotStats& s = stats_[i];

        // ── Fill: box rectangle (Q1 to Q3) ──
        emit_filled_quad(fill_verts_, x - hw, s.q1, x + hw, s.q3, gradient_);

        // ── Outline: box rectangle ──
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q1);
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q3);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.q3);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.q1);
        line_x_.push_back(x - hw);
        line_y_.push_back(s.q1);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // ── Median line (thicker, rendered as outline) ──
        line_x_.push_back(x - hw);
        line_y_.push_back(s.median);
        line_x_.push_back(x + hw);
        line_y_.push_back(s.median);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // ── Whiskers ──
        float cap_hw = hw * 0.5f;

        // Lower whisker
        line_x_.push_back(x);
        line_y_.push_back(s.q1);
        line_x_.push_back(x);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Lower cap
        line_x_.push_back(x - cap_hw);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(x + cap_hw);
        line_y_.push_back(s.whisker_low);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Upper whisker
        line_x_.push_back(x);
        line_y_.push_back(s.q3);
        line_x_.push_back(x);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // Upper cap
        line_x_.push_back(x - cap_hw);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(x + cap_hw);
        line_y_.push_back(s.whisker_high);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // ── Outliers ──
        if (show_outliers_)
        {
            for (float o : s.outliers)
            {
                outlier_x_.push_back(x);
                outlier_y_.push_back(o);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ViolinSeries
// ═══════════════════════════════════════════════════════════════════════════

ViolinSeries& ViolinSeries::add_violin(float x_position, std::span<const float> values)
{
    ViolinData vd;
    vd.x_position = x_position;
    vd.values.assign(values.begin(), values.end());
    // Remove NaN
    vd.values.erase(
        std::remove_if(vd.values.begin(), vd.values.end(), [](float v) { return std::isnan(v); }),
        vd.values.end());
    violins_.push_back(std::move(vd));
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void ViolinSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();

    float hw = violin_width_ * 0.5f;

    for (const auto& vd : violins_)
    {
        if (vd.values.empty())
            continue;

        std::vector<float> sorted = vd.values;
        std::sort(sorted.begin(), sorted.end());

        float data_min = sorted.front();
        float data_max = sorted.back();
        float range    = data_max - data_min;
        if (range == 0.0f)
            range = 1.0f;

        // Silverman's rule of thumb for bandwidth
        auto  n       = static_cast<float>(sorted.size());
        float std_dev = 0.0f;
        float mean    = 0.0f;
        for (float v : sorted)
            mean += v;
        mean /= n;
        for (float v : sorted)
            std_dev += (v - mean) * (v - mean);
        std_dev = std::sqrt(std_dev / n);
        if (std_dev == 0.0f)
            std_dev = 1.0f;
        float bandwidth = 1.06f * std_dev * std::pow(n, -0.2f);

        // Evaluate KDE at resolution_ points
        std::vector<float> y_vals(resolution_);
        std::vector<float> kde_vals(resolution_);
        float              max_kde = 0.0f;

        for (int i = 0; i < resolution_; ++i)
        {
            float t     = static_cast<float>(i) / static_cast<float>(resolution_ - 1);
            float y     = data_min + t * range;
            y_vals[i]   = y;
            kde_vals[i] = gaussian_kde(y, sorted, bandwidth);
            max_kde     = std::max(max_kde, kde_vals[i]);
        }

        // Normalize KDE to fit within violin_width
        if (max_kde > 0.0f)
        {
            for (int i = 0; i < resolution_; ++i)
                kde_vals[i] /= max_kde;
        }

        // ── Fill: triangulated violin shape (horizontal slices) ──
        for (int i = 0; i < resolution_ - 1; ++i)
        {
            float rx0 = vd.x_position + kde_vals[i] * hw;
            float lx0 = vd.x_position - kde_vals[i] * hw;
            float rx1 = vd.x_position + kde_vals[i + 1] * hw;
            float lx1 = vd.x_position - kde_vals[i + 1] * hw;
            float y0  = y_vals[i];
            float y1  = y_vals[i + 1];

            // Gradient: center is bright (1.0), edges are dim
            float ac  = 1.0f;   // center alpha (always bright)
            float ar0 = grad_alpha(rx0, lx0, rx0, gradient_);
            float ar1 = grad_alpha(rx1, lx1, rx1, gradient_);
            float al0 = grad_alpha(lx0, lx0, rx0, gradient_);
            float al1 = grad_alpha(lx1, lx1, rx1, gradient_);

            // Right half quad
            emit_filled_tri(fill_verts_, vd.x_position, y0, ac, rx0, y0, ar0, rx1, y1, ar1);
            emit_filled_tri(fill_verts_,
                            vd.x_position,
                            y0,
                            ac,
                            rx1,
                            y1,
                            ar1,
                            vd.x_position,
                            y1,
                            ac);
            // Left half quad
            emit_filled_tri(fill_verts_, vd.x_position, y0, ac, lx0, y0, al0, lx1, y1, al1);
            emit_filled_tri(fill_verts_,
                            vd.x_position,
                            y0,
                            ac,
                            lx1,
                            y1,
                            al1,
                            vd.x_position,
                            y1,
                            ac);
        }

        // ── Outline: violin contour ──
        // Right half (going up)
        for (int i = 0; i < resolution_; ++i)
        {
            line_x_.push_back(vd.x_position + kde_vals[i] * hw);
            line_y_.push_back(y_vals[i]);
        }
        // Left half (going down)
        for (int i = resolution_ - 1; i >= 0; --i)
        {
            line_x_.push_back(vd.x_position - kde_vals[i] * hw);
            line_y_.push_back(y_vals[i]);
        }
        // Close the shape
        line_x_.push_back(vd.x_position + kde_vals[0] * hw);
        line_y_.push_back(y_vals[0]);
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);

        // ── Inner box plot (thin) ──
        if (show_box_)
        {
            float q1     = percentile(sorted, 0.25f);
            float median = percentile(sorted, 0.5f);
            float q3     = percentile(sorted, 0.75f);
            float bw     = hw * 0.15f;

            // Inner box fill (darker)
            emit_filled_quad(fill_verts_,
                             vd.x_position - bw,
                             q1,
                             vd.x_position + bw,
                             q3,
                             gradient_);

            // Inner box outline
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q1);
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q3);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(q3);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(q1);
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(q1);
            line_x_.push_back(NAN_BREAK);
            line_y_.push_back(NAN_BREAK);

            // Median line
            line_x_.push_back(vd.x_position - bw);
            line_y_.push_back(median);
            line_x_.push_back(vd.x_position + bw);
            line_y_.push_back(median);
            line_x_.push_back(NAN_BREAK);
            line_y_.push_back(NAN_BREAK);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HistogramSeries
// ═══════════════════════════════════════════════════════════════════════════

HistogramSeries::HistogramSeries(std::span<const float> values, int bins)
{
    set_data(values, bins);
}

HistogramSeries& HistogramSeries::set_data(std::span<const float> values, int bins)
{
    raw_values_.assign(values.begin(), values.end());
    // Non-finite samples cannot be assigned to finite-width bins.
    raw_values_.erase(std::remove_if(raw_values_.begin(),
                                     raw_values_.end(),
                                     [](float v) { return !std::isfinite(v); }),
                      raw_values_.end());
    bins_  = bins;
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void HistogramSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();
    bin_edges_.clear();
    bin_counts_.clear();

    if (raw_values_.empty() || bins_ <= 0)
        return;

    float lo = *std::min_element(raw_values_.begin(), raw_values_.end());
    float hi = *std::max_element(raw_values_.begin(), raw_values_.end());
    if (lo == hi)
        hi = lo + 1.0f;

    float bin_width = (hi - lo) / static_cast<float>(bins_);

    // Compute bin edges
    bin_edges_.resize(bins_ + 1);
    for (int i = 0; i <= bins_; ++i)
        bin_edges_[i] = lo + static_cast<float>(i) * bin_width;

    // Count values in each bin
    bin_counts_.resize(bins_, 0.0f);
    for (float v : raw_values_)
    {
        int idx = static_cast<int>((v - lo) / bin_width);
        if (idx >= bins_)
            idx = bins_ - 1;
        if (idx < 0)
            idx = 0;
        bin_counts_[idx] += 1.0f;
    }

    // Cumulative
    if (cumulative_)
    {
        for (int i = 1; i < bins_; ++i)
            bin_counts_[i] += bin_counts_[i - 1];
    }

    // Density normalization
    if (density_)
    {
        auto total = static_cast<float>(raw_values_.size());
        for (int i = 0; i < bins_; ++i)
            bin_counts_[i] /= (total * bin_width);
    }

    // ── Fill: one filled quad per bin ──
    for (int i = 0; i < bins_; ++i)
    {
        if (bin_counts_[i] > 0.0f)
            emit_filled_quad(fill_verts_,
                             bin_edges_[i],
                             0.0f,
                             bin_edges_[i + 1],
                             bin_counts_[i],
                             gradient_);
    }

    // ── Outline: step-function contour ──
    line_x_.push_back(bin_edges_[0]);
    line_y_.push_back(0.0f);

    for (int i = 0; i < bins_; ++i)
    {
        float edge_l = bin_edges_[i];
        float edge_r = bin_edges_[i + 1];
        line_x_.push_back(edge_l);
        line_y_.push_back(bin_counts_[i]);
        line_x_.push_back(edge_r);
        line_y_.push_back(bin_counts_[i]);
    }

    line_x_.push_back(bin_edges_[bins_]);
    line_y_.push_back(0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BarSeries
// ═══════════════════════════════════════════════════════════════════════════

BarSeries::BarSeries(std::span<const float> positions, std::span<const float> heights)
{
    set_data(positions, heights);
}

BarSeries& BarSeries::set_data(std::span<const float> positions, std::span<const float> heights)
{
    positions_.assign(positions.begin(), positions.end());
    heights_.assign(heights.begin(), heights.end());
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void BarSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    fill_verts_.clear();

    float  hw = bar_width_ * 0.5f;
    size_t n  = std::min(positions_.size(), heights_.size());

    for (size_t i = 0; i < n; ++i)
    {
        float pos = positions_[i];
        float h   = heights_[i];

        if (orientation_ == BarOrientation::Vertical)
        {
            // ── Fill: solid bar rectangle ──
            emit_filled_quad(fill_verts_, pos - hw, baseline_, pos + hw, h, gradient_);

            // ── Outline: rectangle border ──
            line_x_.push_back(pos - hw);
            line_y_.push_back(baseline_);
            line_x_.push_back(pos - hw);
            line_y_.push_back(h);
            line_x_.push_back(pos + hw);
            line_y_.push_back(h);
            line_x_.push_back(pos + hw);
            line_y_.push_back(baseline_);
            line_x_.push_back(pos - hw);
            line_y_.push_back(baseline_);
        }
        else
        {
            // ── Fill: horizontal bar ──
            emit_filled_quad(fill_verts_, baseline_, pos - hw, h, pos + hw, gradient_);

            // ── Outline: horizontal bar border ──
            line_x_.push_back(baseline_);
            line_y_.push_back(pos - hw);
            line_x_.push_back(h);
            line_y_.push_back(pos - hw);
            line_x_.push_back(h);
            line_y_.push_back(pos + hw);
            line_x_.push_back(baseline_);
            line_y_.push_back(pos + hw);
            line_x_.push_back(baseline_);
            line_y_.push_back(pos - hw);
        }

        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// StemSeries
// ═══════════════════════════════════════════════════════════════════════════

StemSeries& StemSeries::set_data(std::span<const float> x, std::span<const float> y)
{
    x_.assign(x.begin(), x.end());
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    rebuild_geometry();
    return *this;
}

void StemSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();

    size_t n = std::min(x_.size(), y_.size());

    for (size_t i = 0; i < n; ++i)
    {
        // Vertical line from baseline to data point
        line_x_.push_back(x_[i]);
        line_y_.push_back(baseline_);
        line_x_.push_back(x_[i]);
        line_y_.push_back(y_[i]);
        // NaN break between stems
        line_x_.push_back(NAN_BREAK);
        line_y_.push_back(NAN_BREAK);
    }
}

}   // namespace spectra
