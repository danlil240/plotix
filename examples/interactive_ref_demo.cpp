// interactive_ref_demo.cpp — Drag sliders to move reference lines and retune y = a·x² + b.
//
// Build: cmake --build build --target interactive_ref_demo
// Run:   ./build/examples/interactive_ref_demo

#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    constexpr int      N = 256;
    std::vector<float> x(N);
    std::vector<float> y(N);

    for (int i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / (N - 1) * 6.0f - 3.0f;
        y[i] = std::sin(x[i]);
    }

    spectra::plot(x, y, "b-").label("sin(x)");
    spectra::ihline(0.0, -2.0, 2.0, "k--", "Y = 0");
    spectra::ivline(0.0, -3.0, 3.0, "k--", "X = 0");
    spectra::ifplot([](double xv, std::span<const double> p) { return p[0] * xv * xv + p[1]; },
                    -3.0,
                    3.0,
                    {{"a", 0.15, -1.0, 1.0}, {"b", 0.0, -2.0, 2.0}},
                    200,
                    "g-");
    spectra::title("Interactive Reference Lines & Functions");
    spectra::xlabel("x");
    spectra::ylabel("y");
    spectra::grid();
    spectra::legend();
    spectra::show();
    return 0;
}
