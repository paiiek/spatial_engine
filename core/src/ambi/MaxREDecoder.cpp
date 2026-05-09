// core/src/ambi/MaxREDecoder.cpp
// max-rE Ambisonic decoder weights.
//
// Formula: g_l = P_l(r_E_max) where r_E_max = largest root of P_{N+1}(x)=0.
// Legendre polynomials via Bonnet's 3-term recursion: exact for l ≤ 3.
//
// Roots (closed-form):
//   N=1: root of P_2(x) = (3x^2-1)/2 = 0  →  x = 1/sqrt(3) ≈ 0.57735
//   N=2: root of P_3(x) = (5x^3-3x)/2 = 0 →  x = sqrt(3/5) ≈ 0.77460
//   N=3: root of P_4(x) = (35x^4-30x^2+3)/8 = 0
//              → x^2 = (30 + sqrt(900-12*35*8))/(2*35*4) ... simplifies to
//                x^2 = (15+2*sqrt(5)*sqrt(14-sqrt(5)))/(35) ... numerical: ≈ 0.74156
//              → x ≈ 0.86114
//
// References: Zotter & Frank 2019 eq.4.49; SPARTA saf_hoa_internal.c getMaxREweights().

#include "ambi/MaxREDecoder.hpp"
#include <cmath>

namespace spe::ambi {

// Evaluate Legendre P_l(x) via Bonnet recursion: P_0=1, P_1=x,
// P_{l+1} = ((2l+1)*x*P_l - l*P_{l-1}) / (l+1).
static float legendre(int l, float x) noexcept {
    if (l == 0) return 1.f;
    if (l == 1) return x;
    float p0 = 1.f, p1 = x, p2 = 0.f;
    for (int i = 1; i < l; ++i) {
        p2 = ((2*i + 1) * x * p1 - i * p0) / (i + 1);
        p0 = p1; p1 = p2;
    }
    return p2;
}

std::array<float, 4> MaxREDecoder::compute_max_re_weights(int order) noexcept {
    std::array<float, 4> w{};

    // r_E_max: largest root of P_{N+1}(x) = 0
    float r = 0.f;
    switch (order) {
    case 1:
        // P_2(x) = (3x^2 - 1)/2 = 0  →  x = 1/sqrt(3)
        r = static_cast<float>(1.0 / std::sqrt(3.0));  // ≈ 0.57735
        break;
    case 2:
        // P_3(x) = x(5x^2-3)/2 = 0  →  x = sqrt(3/5)
        r = static_cast<float>(std::sqrt(3.0 / 5.0));  // ≈ 0.77460
        break;
    case 3:
    default:
        // P_4(x)=0 → 35u^2-30u+3=0 (u=x^2); u=(30±sqrt(900-420))/70=(30±sqrt(480))/70
        // Largest positive root: u = (30 + sqrt(480))/70
        r = static_cast<float>(std::sqrt((30.0 + std::sqrt(480.0)) / 70.0));  // ≈ 0.86114
        break;
    }

    // g_l = P_l(r_E_max), normalised so g_0 = 1.0
    w[0] = 1.0f;
    for (int l = 1; l <= order; ++l)
        w[static_cast<size_t>(l)] = legendre(l, r);

    return w;
}

} // namespace spe::ambi
