// core/src/render/AlgorithmAnalyticReference.cpp

#include "render/AlgorithmAnalyticReference.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace spe::render {

// v0.8 P1.3 (DSP-3) — RT-no-alloc bound for VBAP audio-thread scratch.
// All per-call temporaries (azs, idx, ux/uy/uz, ang, cands) are sized at
// most this many entries. Mirrors VBAPRenderer::ramps_'s fixed [64] cap and
// the new VBAPRenderer::prepareToPlay assert. If a layout ever asks for
// >64 speakers the assert fires loudly and the renderer refuses the layout
// at prepare time — the audio thread never sees an oversized N.
static constexpr int kMaxVbapSpeakers = 64;

// ---------------------------------------------------------------------------
// VBAP 2D (Pulkki 1997) — pair-based amplitude panning (used for horizontal layouts).
// Finds the pair (i,j) such that az_rad lies between az_i and az_j (going clockwise),
// solves the 2x2 system, then normalises.
// Falls back to nearest single speaker if no valid pair found.
//
// v0.8 P1.3 — writes into caller-provided `gains` (size ≥ N). RT-safe:
// stack-allocated std::array scratch capped at kMaxVbapSpeakers.
// ---------------------------------------------------------------------------
static void vbap_gain_2d_into(
    const spe::geometry::SpeakerLayout& layout, float az_rad, float* gains)
{
    const int N = static_cast<int>(layout.speakers.size());
    std::fill_n(gains, N, 0.f);
    if (N == 0) return;
    if (N == 1) { gains[0] = 1.f; return; }

    auto speaker_az = [](const spe::geometry::Speaker& s) {
        return std::atan2(s.x, s.z);
    };

    // Stack scratch — bounded by kMaxVbapSpeakers (renderer prepareToPlay
    // refuses any layout with N > kMaxVbapSpeakers, so the assert here is
    // a defensive belt-and-braces.
    std::array<float, kMaxVbapSpeakers> azs{};
    for (int i = 0; i < N; ++i)
        azs[static_cast<size_t>(i)] = speaker_az(layout.speakers[i]);

    // Normalise az_rad to [-pi, pi]
    while (az_rad >  3.14159265f) az_rad -= 2.f * 3.14159265f;
    while (az_rad < -3.14159265f) az_rad += 2.f * 3.14159265f;

    std::array<int, kMaxVbapSpeakers> idx{};
    for (int i = 0; i < N; ++i) idx[static_cast<size_t>(i)] = i;
    std::sort(idx.begin(), idx.begin() + N,
              [&](int a, int b){ return azs[static_cast<size_t>(a)]
                                       < azs[static_cast<size_t>(b)]; });

    int best_i = -1, best_j = -1;
    for (int k = 0; k < N; ++k) {
        int k1 = (k + 1) % N;
        float ai = azs[static_cast<size_t>(idx[static_cast<size_t>(k)])];
        float aj = azs[static_cast<size_t>(idx[static_cast<size_t>(k1)])];

        float arc = aj - ai;
        if (arc <= 0.f) arc += 2.f * 3.14159265f;

        float rel = az_rad - ai;
        while (rel <  0.f)                rel += 2.f * 3.14159265f;
        while (rel >= 2.f * 3.14159265f)  rel -= 2.f * 3.14159265f;

        if (rel <= arc + 1e-6f) {
            best_i = idx[static_cast<size_t>(k)];
            best_j = idx[static_cast<size_t>(k1)];
            float ci = std::cos(azs[static_cast<size_t>(best_i)]);
            float si = std::sin(azs[static_cast<size_t>(best_i)]);
            float cj = std::cos(azs[static_cast<size_t>(best_j)]);
            float sj = std::sin(azs[static_cast<size_t>(best_j)]);
            float cs = std::cos(az_rad),       ss = std::sin(az_rad);

            float det = ci * sj - cj * si;
            if (std::abs(det) < 1e-8f) {
                gains[best_i] = gains[best_j] = 1.f / std::sqrt(2.f);
                return;
            }
            float g_i = (cs * sj - cj * ss) / det;
            float g_j = (ci * ss - cs * si) / det;

            if (g_i < 0.f) g_i = 0.f;
            if (g_j < 0.f) g_j = 0.f;

            float norm = std::sqrt(g_i * g_i + g_j * g_j);
            if (norm < 1e-10f) norm = 1.f;
            gains[best_i] = g_i / norm;
            gains[best_j] = g_j / norm;
            return;
        }
    }

    // Fallback: nearest speaker
    float min_d = std::numeric_limits<float>::max();
    int   best  = 0;
    for (int i = 0; i < N; ++i) {
        float d = std::abs(azs[static_cast<size_t>(i)] - az_rad);
        if (d > 3.14159265f) d = 2.f * 3.14159265f - d;
        if (d < min_d) { min_d = d; best = i; }
    }
    gains[best] = 1.f;
}

// ---------------------------------------------------------------------------
// VBAP 3D (Pulkki 1997) — triplet-based amplitude panning.
// Source unit vector follows engine convention:
//   s = (cos(el)*sin(az), sin(el), cos(el)*cos(az))   // x=right, y=up, z=front
// For each C(N,3) triplet of speaker unit vectors L=[l_i l_j l_k] (cols),
// solve L * g = s. If det(L) > 1e-8 and all g_n > 0, the source is inside the
// triangle. Among valid triplets pick the one with the smallest spread
// (min of the max angular distance from speaker to source). Tie-break by
// ascending channel index. Energy-normalise.
//
// Fallback (no valid triplet — outside convex hull): pick nearest 3 speakers
// by angular distance, clamp to non-negative, energy-normalise. Equidistant
// ties (angular diff < 1e-6 rad) break by ascending array index.
// ---------------------------------------------------------------------------
// v0.8 P1.3 — writes into caller-provided `gains` (size ≥ N). RT-safe:
// stack-allocated std::array scratch capped at kMaxVbapSpeakers.
static void vbap_gain_3d_into(
    const spe::geometry::SpeakerLayout& layout, float az_rad, float el_rad,
    float* gains)
{
    const int N = static_cast<int>(layout.speakers.size());
    std::fill_n(gains, N, 0.f);
    if (N == 0) return;
    if (N == 1) { gains[0] = 1.f; return; }
    if (N == 2) {
        // Degenerate: 3D mode with only 2 speakers — just split equally.
        gains[0] = gains[1] = 1.f / std::sqrt(2.f);
        return;
    }

    // Source unit vector (engine convention: az=0 → +z front, az=π/2 → +x right)
    const float ce = std::cos(el_rad), se = std::sin(el_rad);
    const float ca = std::cos(az_rad), sa = std::sin(az_rad);
    const float sx = ce * sa;
    const float sy = se;
    const float sz = ce * ca;

    // Build unit vectors for all speakers (stack scratch ≤ kMaxVbapSpeakers).
    std::array<float, kMaxVbapSpeakers> ux{}, uy{}, uz{};
    for (int i = 0; i < N; ++i) {
        const auto& sp = layout.speakers[i];
        float r = std::sqrt(sp.x * sp.x + sp.y * sp.y + sp.z * sp.z);
        if (r < 1e-10f) {
            ux[static_cast<size_t>(i)] = uy[static_cast<size_t>(i)]
                                       = uz[static_cast<size_t>(i)] = 0.f;
        } else {
            ux[static_cast<size_t>(i)] = sp.x / r;
            uy[static_cast<size_t>(i)] = sp.y / r;
            uz[static_cast<size_t>(i)] = sp.z / r;
        }
    }

    // Angular distance from source to each speaker (acos of dot product)
    std::array<float, kMaxVbapSpeakers> ang{};
    for (int i = 0; i < N; ++i) {
        float dot = ux[static_cast<size_t>(i)] * sx
                  + uy[static_cast<size_t>(i)] * sy
                  + uz[static_cast<size_t>(i)] * sz;
        if (dot >  1.f) dot =  1.f;
        if (dot < -1.f) dot = -1.f;
        ang[static_cast<size_t>(i)] = std::acos(dot);
    }

    // Search all triplets
    int best_i = -1, best_j = -1, best_k = -1;
    float best_g[3] = {0.f, 0.f, 0.f};
    float best_spread = std::numeric_limits<float>::max();
    int best_min_chan = std::numeric_limits<int>::max();

    auto chan_of = [&](int idx) {
        return layout.speakers[idx].channel;
    };

    for (int i = 0; i < N - 2; ++i) {
        for (int j = i + 1; j < N - 1; ++j) {
            for (int k = j + 1; k < N; ++k) {
                // Build matrix L (columns = speaker unit vectors)
                // L = [ ux[i] ux[j] ux[k];
                //       uy[i] uy[j] uy[k];
                //       uz[i] uz[j] uz[k] ]
                float a = ux[i], b = ux[j], c = ux[k];
                float d = uy[i], e = uy[j], f = uy[k];
                float g = uz[i], h = uz[j], m = uz[k];

                // det = a(em - fh) - b(dm - fg) + c(dh - eg)
                float det = a * (e * m - f * h)
                          - b * (d * m - f * g)
                          + c * (d * h - e * g);
                if (std::abs(det) < 1e-8f) continue;

                // Cramer's rule: solve L * g = s
                // g_i replaces col i with s
                float det_i = sx * (e * m - f * h)
                            - b  * (sy * m - f * sz)
                            + c  * (sy * h - e * sz);
                float det_j = a  * (sy * m - f * sz)
                            - sx * (d  * m - f * g )
                            + c  * (d  * sz - sy * g);
                float det_k = a  * (e  * sz - sy * h)
                            - b  * (d  * sz - sy * g)
                            + sx * (d  * h  - e  * g);

                float gi = det_i / det;
                float gj = det_j / det;
                float gk = det_k / det;

                // All gains must be non-negative (with small tolerance)
                if (gi < -1e-6f || gj < -1e-6f || gk < -1e-6f) continue;

                // Spread = max angular distance from source to any vertex
                float spread = ang[i];
                if (ang[j] > spread) spread = ang[j];
                if (ang[k] > spread) spread = ang[k];

                int min_chan = chan_of(i);
                if (chan_of(j) < min_chan) min_chan = chan_of(j);
                if (chan_of(k) < min_chan) min_chan = chan_of(k);

                bool take = false;
                if (spread < best_spread - 1e-6f) {
                    take = true;
                } else if (std::abs(spread - best_spread) <= 1e-6f
                           && min_chan < best_min_chan) {
                    take = true;
                }
                if (take) {
                    best_spread   = spread;
                    best_min_chan = min_chan;
                    best_i = i; best_j = j; best_k = k;
                    best_g[0] = std::max(0.f, gi);
                    best_g[1] = std::max(0.f, gj);
                    best_g[2] = std::max(0.f, gk);
                }
            }
        }
    }

    if (best_i >= 0) {
        // Energy normalise
        float ss = best_g[0] * best_g[0]
                 + best_g[1] * best_g[1]
                 + best_g[2] * best_g[2];
        float norm = std::sqrt(ss);
        if (norm < 1e-10f) norm = 1.f;
        gains[best_i] = best_g[0] / norm;
        gains[best_j] = best_g[1] / norm;
        gains[best_k] = best_g[2] / norm;
        return;
    }

    // ---- Fallback: nearest-3 by angular distance ----
    // Pair (angle, channel-index, array-index) for tie-breaking.
    struct Cand { float a; int chan; int idx; };
    std::array<Cand, kMaxVbapSpeakers> cands{};
    for (int i = 0; i < N; ++i)
        cands[static_cast<size_t>(i)] =
            { ang[static_cast<size_t>(i)], layout.speakers[i].channel, i };

    std::sort(cands.begin(), cands.begin() + N,
              [](const Cand& A, const Cand& B) {
        if (std::abs(A.a - B.a) < 1e-6f) {
            return A.idx < B.idx;
        }
        return A.a < B.a;
    });

    int picks[3] = { cands[0].idx, cands[1].idx, cands[2].idx };

    // Build matrix and try Cramer once more on the chosen 3
    float a = ux[picks[0]], b = ux[picks[1]], c = ux[picks[2]];
    float d = uy[picks[0]], e = uy[picks[1]], f = uy[picks[2]];
    float g = uz[picks[0]], h = uz[picks[1]], m = uz[picks[2]];
    float det = a * (e * m - f * h)
              - b * (d * m - f * g)
              + c * (d * h - e * g);

    float gi = 0.f, gj = 0.f, gk = 0.f;
    if (std::abs(det) > 1e-8f) {
        float det_i = sx * (e * m - f * h)
                    - b  * (sy * m - f * sz)
                    + c  * (sy * h - e * sz);
        float det_j = a  * (sy * m - f * sz)
                    - sx * (d  * m - f * g )
                    + c  * (d  * sz - sy * g);
        float det_k = a  * (e  * sz - sy * h)
                    - b  * (d  * sz - sy * g)
                    + sx * (d  * h  - e  * g);
        gi = det_i / det;
        gj = det_j / det;
        gk = det_k / det;
    } else {
        // Degenerate triplet — weight inversely by angular distance
        gi = 1.f / std::max(ang[picks[0]], 1e-4f);
        gj = 1.f / std::max(ang[picks[1]], 1e-4f);
        gk = 1.f / std::max(ang[picks[2]], 1e-4f);
    }

    // Clamp to non-negative
    if (gi < 0.f) gi = 0.f;
    if (gj < 0.f) gj = 0.f;
    if (gk < 0.f) gk = 0.f;

    float ss = gi * gi + gj * gj + gk * gk;
    float norm = std::sqrt(ss);
    if (norm < 1e-10f) {
        // Last-ditch: assign all energy to the single nearest speaker
        gains[picks[0]] = 1.f;
        return;
    }
    gains[picks[0]] = gi / norm;
    gains[picks[1]] = gj / norm;
    gains[picks[2]] = gk / norm;
}

// ---------------------------------------------------------------------------
// Dispatch: probe layout dimensionality. If max(|y|) < 1e-3 → 2D path,
// otherwise → 3D triplet path.
//
// v0.8 P1.3 — RT-safe overload (writes into caller-provided scratch).
// Returns N (number of speakers written) on success, 0 on capacity
// violation. The public std::vector-returning vbap_gain() below wraps this
// for non-RT (test) callers.
// ---------------------------------------------------------------------------
int AlgorithmAnalyticReference::vbap_gain_into(
    const geometry::SpeakerLayout& layout,
    float az_rad, float el_rad,
    float* out, int out_capacity) noexcept
{
    const int N = static_cast<int>(layout.speakers.size());
    if (out == nullptr || out_capacity < N || N > kMaxVbapSpeakers) {
        return 0;
    }
    float max_abs_y = 0.f;
    for (const auto& s : layout.speakers) {
        float ay = std::abs(s.y);
        if (ay > max_abs_y) max_abs_y = ay;
    }
    if (max_abs_y < 1e-3f) {
        vbap_gain_2d_into(layout, az_rad, out);
    } else {
        vbap_gain_3d_into(layout, az_rad, el_rad, out);
    }
    return N;
}

std::vector<float> AlgorithmAnalyticReference::vbap_gain(
    const geometry::SpeakerLayout& layout,
    float az_rad, float el_rad)
{
    const int N = static_cast<int>(layout.speakers.size());
    std::vector<float> gains(static_cast<size_t>(N), 0.f);
    if (N == 0) return gains;
    // For test/non-RT callers: layouts with N > kMaxVbapSpeakers (64) are
    // not supported by the RT path either; the scratch overload would
    // return 0. Surface that consistently by leaving gains zeroed.
    if (N > kMaxVbapSpeakers) return gains;
    vbap_gain_into(layout, az_rad, el_rad, gains.data(), N);
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
