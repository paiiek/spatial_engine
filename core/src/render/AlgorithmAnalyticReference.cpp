// core/src/render/AlgorithmAnalyticReference.cpp

#include "render/AlgorithmAnalyticReference.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace spe::render {

// ---------------------------------------------------------------------------
// VBAP 2D (Pulkki 1997) — pair-based amplitude panning.
// Finds the pair (i,j) such that az_rad lies between az_i and az_j
// (going clockwise), solves the 2x2 system, then normalises.
// Falls back to nearest single speaker if no valid pair found.
// ---------------------------------------------------------------------------
std::vector<float> AlgorithmAnalyticReference::vbap_gain(
    const geometry::SpeakerLayout& layout,
    float az_rad, float /*el_rad*/)
{
    const int N = static_cast<int>(layout.speakers.size());
    std::vector<float> gains(N, 0.f);
    if (N == 0) return gains;
    if (N == 1) { gains[0] = 1.f; return gains; }

    // Collect azimuths
    std::vector<float> azs(N);
    for (int i = 0; i < N; ++i)
        azs[i] = speaker_az(layout.speakers[i]);

    // Normalise az_rad to [-pi, pi]
    while (az_rad >  3.14159265f) az_rad -= 2.f * 3.14159265f;
    while (az_rad < -3.14159265f) az_rad += 2.f * 3.14159265f;

    // Find best pair: the pair (i, j) where az_j is the next speaker CW after az_i,
    // and az_rad lies within that arc.
    // Strategy: sort speakers by azimuth, then find the bracket.

    std::vector<int> idx(N);
    for (int i = 0; i < N; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b){ return azs[a] < azs[b]; });

    // Find bracket
    int best_i = -1, best_j = -1;
    for (int k = 0; k < N; ++k) {
        int k1 = (k + 1) % N;
        float ai = azs[idx[k]];
        float aj = azs[idx[k1]];

        // Unwrap aj relative to ai
        float arc = aj - ai;
        if (arc <= 0.f) arc += 2.f * 3.14159265f; // wrap to [0, 2pi]

        float rel = az_rad - ai;
        while (rel <  0.f)                rel += 2.f * 3.14159265f;
        while (rel >= 2.f * 3.14159265f)  rel -= 2.f * 3.14159265f;

        if (rel <= arc + 1e-6f) {
            best_i = idx[k];
            best_j = idx[k1];
            // Solve 2x2: [l_i, l_j] * [e_i; e_j] = e_src
            // In 2D unit vectors: e = [cos(az), sin(az)]
            float ci = std::cos(azs[best_i]), si = std::sin(azs[best_i]);
            float cj = std::cos(azs[best_j]), sj = std::sin(azs[best_j]);
            float cs = std::cos(az_rad),       ss = std::sin(az_rad);

            float det = ci * sj - cj * si;
            if (std::abs(det) < 1e-8f) {
                // Degenerate: split equally
                gains[best_i] = gains[best_j] = 1.f / std::sqrt(2.f);
                return gains;
            }
            float g_i = (cs * sj - cj * ss) / det;
            float g_j = (ci * ss - cs * si) / det;

            // Clamp to non-negative
            if (g_i < 0.f) g_i = 0.f;
            if (g_j < 0.f) g_j = 0.f;

            // Energy normalise
            float norm = std::sqrt(g_i * g_i + g_j * g_j);
            if (norm < 1e-10f) norm = 1.f;
            gains[best_i] = g_i / norm;
            gains[best_j] = g_j / norm;
            return gains;
        }
    }

    // Fallback: nearest speaker
    float min_d = std::numeric_limits<float>::max();
    int   best  = 0;
    for (int i = 0; i < N; ++i) {
        float d = std::abs(azs[i] - az_rad);
        if (d > 3.14159265f) d = 2.f * 3.14159265f - d;
        if (d < min_d) { min_d = d; best = i; }
    }
    gains[best] = 1.f;
    return gains;
}

// ---------------------------------------------------------------------------
// DBAP (Lossius 2009)
// g_i = (1/d_i^a) / sqrt(Σ 1/d_i^(2a))
// ---------------------------------------------------------------------------
std::vector<float> AlgorithmAnalyticReference::dbap_gain(
    const geometry::SpeakerLayout& layout,
    float src_x, float src_y, float src_z,
    float rolloff_a)
{
    const int N = static_cast<int>(layout.speakers.size());
    std::vector<float> gains(N, 0.f);
    if (N == 0) return gains;

    std::vector<float> w(N);
    float sum_sq = 0.f;
    for (int i = 0; i < N; ++i) {
        float dx = layout.speakers[i].x - src_x;
        float dy = layout.speakers[i].y - src_y;
        float dz = layout.speakers[i].z - src_z;
        float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        d = std::max(d, 1e-4f); // avoid div-by-zero
        float wi = std::pow(d, -rolloff_a);
        w[i]     = wi;
        sum_sq  += wi * wi;
    }
    float denom = std::sqrt(sum_sq);
    if (denom < 1e-10f) denom = 1.f;
    for (int i = 0; i < N; ++i)
        gains[i] = w[i] / denom;
    return gains;
}

} // namespace spe::render
