#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    constexpr size_t   sample_count = 600;
    std::vector<float> x(sample_count);
    std::vector<float> signal(sample_count);
    std::vector<float> scalar(sample_count);
    std::vector<float> lower(sample_count);
    std::vector<float> upper(sample_count);

    for (size_t i = 0; i < sample_count; ++i)
    {
        const float t           = static_cast<float>(i) * 0.025f;
        const float uncertainty = 0.12f + 0.08f * (0.5f + 0.5f * std::sin(t * 0.4f));
        x[i]                    = t;
        signal[i]               = std::sin(t) + 0.18f * std::sin(t * 3.7f);
        scalar[i]               = std::cos(t * 0.35f);
        lower[i]                = signal[i] - uncertainty;
        upper[i]                = signal[i] + uncertainty;
    }

    spectra::subplot(1, 2, 1);
    spectra::scatter(x, signal)
        .label("signal colored by phase")
        .color_values(scalar)
        .colormap(spectra::ColormapType::Viridis)
        .colormap_range(-1.0f, 1.0f)
        .size(7.0f);
    spectra::title("GPU scalar scatter colormap");
    spectra::xlabel("time (s)");
    spectra::ylabel("amplitude");
    spectra::grid();

    spectra::subplot(1, 2, 2);
    spectra::band(x, lower, upper)
        .label("95% interval")
        .color(spectra::rgb(0.20f, 0.55f, 0.95f))
        .fill_opacity(0.28f)
        .edge_width(1.0f);
    spectra::plot(x, signal).label("estimate").color(spectra::rgb(0.08f, 0.32f, 0.72f)).width(2.0f);
    spectra::title("Native uncertainty band");
    spectra::xlabel("time (s)");
    spectra::ylabel("amplitude");
    spectra::grid();
    spectra::legend();

    spectra::show();
    return 0;
}
