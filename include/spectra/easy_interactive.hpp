#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <spectra/frame.hpp>
#include <string>
#include <string_view>
#include <vector>

// Included at the end of easy.hpp — uses detail::EasyState and detail::easy_state().

namespace spectra::detail
{

inline void EasyState::tick_interactive()
{
    for (auto& h : interactive_hlines_)
    {
        if (!h.series || !h.knob)
            continue;
        const float yv  = h.knob->value;
        const float y[] = {yv, yv};
        h.series->set_y(y);
    }

    for (auto& v : interactive_vlines_)
    {
        if (!v.series || !v.knob)
            continue;
        const float xv  = v.knob->value;
        const float x[] = {xv, xv};
        v.series->set_x(x);
    }

    for (auto& f : interactive_fplots_)
    {
        if (!f.series || !f.func || f.n < 2)
            continue;
        for (size_t i = 0; i < f.knobs.size(); ++i)
            f.param_scratch[i] = static_cast<double>(f.knobs[i]->value);
        const auto params = std::span<const double>(f.param_scratch);
        for (int i = 0; i < f.n; ++i)
            f.ys[static_cast<size_t>(i)] = static_cast<float>(
                f.func(static_cast<double>(f.xs[static_cast<size_t>(i)]), params));
        f.series->set_y(f.ys);
    }
}

inline void wire_interactive_animation_if_needed(EasyState& s)
{
    if (s.interactive_anim_wired_ || !s.has_interactive_bindings())
        return;

    s.interactive_anim_wired_ = true;
    auto user_cb              = s.on_update_cb;
    s.on_update_cb            = [&s, user_cb](float dt, float elapsed)
    {
        s.tick_interactive();
        if (user_cb)
            user_cb(dt, elapsed);
    };

    s.ensure_figure();
    auto on_frame_cb = [&s](Frame& f)
    {
        uint64_t fc = f.frame_number();
        if (fc == s.on_update_frame_)
            return;
        s.on_update_frame_ = fc;
        if (s.on_update_cb)
            s.on_update_cb(f.delta_time(), f.elapsed_seconds());
    };
    for (auto* fig : s.all_figures)
        fig->animate().fps(60).on_frame(on_frame_cb).play();
}

}   // namespace spectra::detail

namespace spectra
{

// Interactive horizontal reference line — drag the slider to move y.
inline LineSeries& ihline(double             y,
                          double             range_min = -10.0,
                          double             range_max = 10.0,
                          std::string_view   fmt       = "k--",
                          const std::string& knob_name = "Y line")
{
    auto& s    = detail::easy_state();
    auto& line = s.ensure_axes().hline(y, fmt);
    auto& knob = s.knob_mgr.add_float(knob_name,
                                      static_cast<float>(y),
                                      static_cast<float>(range_min),
                                      static_cast<float>(range_max));
    s.interactive_hlines_.push_back({&line, &knob});
    detail::wire_interactive_animation_if_needed(s);
    return line;
}

// Interactive vertical reference line — drag the slider to move x.
inline LineSeries& ivline(double             x,
                          double             range_min = -10.0,
                          double             range_max = 10.0,
                          std::string_view   fmt       = "k--",
                          const std::string& knob_name = "X line")
{
    auto& s    = detail::easy_state();
    auto& line = s.ensure_axes().vline(x, fmt);
    auto& knob = s.knob_mgr.add_float(knob_name,
                                      static_cast<float>(x),
                                      static_cast<float>(range_min),
                                      static_cast<float>(range_max));
    s.interactive_vlines_.push_back({&line, &knob});
    detail::wire_interactive_animation_if_needed(s);
    return line;
}

// Interactive function plot — sliders retune each parameter in *params*.
//
//   spectra::ifplot(
//       [](double x, std::span<const double> p) { return p[0] * x * x + p[1]; },
//       -3.0, 3.0,
//       {{"a", 1.0, -5.0, 5.0}, {"b", 0.0, -2.0, 2.0}});
//
inline LineSeries& ifplot(std::function<double(double, std::span<const double>)> func,
                          double                                                 xmin,
                          double                                                 xmax,
                          std::initializer_list<FplotParam>                      params = {},
                          int                                                    n      = 200,
                          std::string_view                                       fmt    = "-")
{
    auto& s = detail::easy_state();
    n       = std::max(n, 2);

    detail::InteractiveFplot binding;
    binding.func = std::move(func);
    binding.n    = n;
    binding.xs.resize(static_cast<size_t>(n));
    binding.ys.resize(static_cast<size_t>(n));
    binding.param_scratch.resize(params.size());

    const double dx = (xmax - xmin) / static_cast<double>(n - 1);
    for (int i = 0; i < n; ++i)
        binding.xs[static_cast<size_t>(i)] = static_cast<float>(xmin + dx * static_cast<double>(i));

    for (const auto& p : params)
    {
        auto& knob = s.knob_mgr.add_float(p.name.empty() ? "param" : p.name,
                                          static_cast<float>(p.value),
                                          static_cast<float>(p.min_val),
                                          static_cast<float>(p.max_val));
        binding.knobs.push_back(&knob);
        binding.param_scratch.push_back(p.value);
    }

    for (int i = 0; i < n; ++i)
    {
        const auto span_params             = std::span<const double>(binding.param_scratch);
        binding.ys[static_cast<size_t>(i)] = static_cast<float>(
            binding.func(static_cast<double>(binding.xs[static_cast<size_t>(i)]), span_params));
    }

    auto& line     = s.ensure_axes().plot(binding.xs, binding.ys, fmt);
    binding.series = &line;
    s.interactive_fplots_.push_back(std::move(binding));
    detail::wire_interactive_animation_if_needed(s);
    return line;
}

}   // namespace spectra
