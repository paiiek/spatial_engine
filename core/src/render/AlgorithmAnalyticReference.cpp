// core/src/render/AlgorithmAnalyticReference.cpp

#include "render/AlgorithmAnalyticReference.h"
#include "render/ported/SpatialMath.h"
#include "render/ported/VbapMask.h"
#include "coords/Coords.h"
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

    // --- 5-tier elevation layering (Dreamscape convergence) ----------------
    // Restrict the VBAP candidate set to speakers angularly near the source,
    // with progressive relaxation and a steep-source opposite-layer cut. The
    // mask is built in the ported frame (z = up) via the canonical Y<->Z swap
    // adapter — both speaker positions and the source direction pass through
    // coords::mmhoa_to_ported so the ported logic sees one consistent frame
    // (same L/R invariant locked by test_convergence_coords).
    std::array<iae::Vec3, kMaxVbapSpeakers> spk_ported{};
    for (int i = 0; i < N; ++i) {
        const auto& sp = layout.speakers[static_cast<size_t>(i)];
        const auto p = spe::coords::mmhoa_to_ported(sp.x, sp.y, sp.z);
        spk_ported[static_cast<size_t>(i)] = iae::Vec3{p[0], p[1], p[2]};
    }
    const auto psrc = spe::coords::mmhoa_to_ported(sx, sy, sz);
    const iae::Vec3 src_ported{psrc[0], psrc[1], psrc[2]};
    // Reference derives flatness from the Cartesian object position (a hard
    // |z_ported| <= 1e-4 gate then |atan2(z,horiz)°| <= 0.02). For a unit-length
    // source z = sin(el), so the Cartesian early-out is subsumed by the angle
    // check — this single |el°| <= eps form is equivalent at the el≈0 boundary.
    const float el_deg = el_rad * (180.f / 3.14159265358979323846f);
    const bool object_flat = std::abs(el_deg) <= iae::kVbapObjectElevationDegEps;

    std::array<bool, iae::kPrototypeChannels> participate{};
    iae::fillVbapMaskForObject(spk_ported.data(), N, /*spatialGroupMask=*/nullptr,
                               object_flat, src_ported, participate.data());
    // Safety: the reference's final tier admits the whole spatial group, so a
    // mask with <3 candidates only happens when the layout itself has <3
    // eligible speakers (e.g. a flat object on a rig with only 2 horizontal
    // speakers). In that case ignore the mask rather than under-select.
    if (iae::countVbapMaskTrue(participate.data(), N) < 3)
        for (int i = 0; i < N; ++i)
            participate[static_cast<size_t>(i)] = true;

    // Search all triplets
    int best_i = -1, best_j = -1, best_k = -1;
    float best_g[3] = {0.f, 0.f, 0.f};
    float best_spread = std::numeric_limits<float>::max();
    int best_min_chan = std::numeric_limits<int>::max();

    auto chan_of = [&](int idx) {
        return layout.speakers[idx].channel;
    };

    for (int i = 0; i < N - 2; ++i) {
        if (!participate[static_cast<size_t>(i)]) continue;
        for (int j = i + 1; j < N - 1; ++j) {
            if (!participate[static_cast<size_t>(j)]) continue;
            for (int k = j + 1; k < N; ++k) {
                if (!participate[static_cast<size_t>(k)]) continue;
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
    // Only the participating (elevation-masked) speakers are candidates; the
    // safety clamp above guarantees >=3 of them whenever N>=3.
    struct Cand { float a; int chan; int idx; };
    std::array<Cand, kMaxVbapSpeakers> cands{};
    int nc = 0;
    for (int i = 0; i < N; ++i) {
        if (!participate[static_cast<size_t>(i)]) continue;
        cands[static_cast<size_t>(nc++)] =
            { ang[static_cast<size_t>(i)], layout.speakers[i].channel, i };
    }

    std::sort(cands.begin(), cands.begin() + nc,
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

// ---------------------------------------------------------------------------
// MDAP (Multiple-Direction Amplitude Panning) — source spread/width.
// Samples K=8 directions around the nominal (az,el) and sums their VBAP gains,
// then energy-normalises. 2D layouts sample an azimuth arc; 3D layouts sample a
// cone of half-angle spread/2 about the nominal direction (mirrors the ported
// computeHorizontal/SpatialMdap geometry). Each sample reuses vbap_gain_into so
// the elevation participation mask + 2D/3D dispatch apply per sample.
// spread_deg is clamped to [0, 40]; spread ≈ 0 returns the point source.
// RT-SAFE — stack scratch only.
// ---------------------------------------------------------------------------
int AlgorithmAnalyticReference::vbap_mdap_gain_into(
    const geometry::SpeakerLayout& layout,
    float az_rad, float el_rad, float spread_deg,
    float* out, int out_capacity) noexcept
{
    const int N = static_cast<int>(layout.speakers.size());
    if (out == nullptr || out_capacity < N || N > kMaxVbapSpeakers || N == 0) {
        return 0;
    }

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMdapSpreadMaxDeg = 40.f;   // ref kMdapSpreadMaxDegrees
    constexpr float kSpreadEpsDeg     = 1.0e-3f;
    constexpr int   K                 = 8;      // ref kMdapDefaultSpreadSegments

    float spread = spread_deg;
    if (spread < 0.f)               spread = 0.f;
    if (spread > kMdapSpreadMaxDeg) spread = kMdapSpreadMaxDeg;

    // Tiny spread → point source (bit-identical to the non-MDAP path).
    if (spread <= kSpreadEpsDeg)
        return vbap_gain_into(layout, az_rad, el_rad, out, out_capacity);

    std::fill_n(out, N, 0.f);

    // Layout dimensionality — same probe as vbap_gain_into's dispatch.
    float max_abs_y = 0.f;
    for (const auto& s : layout.speakers) {
        const float ay = std::abs(s.y);
        if (ay > max_abs_y) max_abs_y = ay;
    }
    const bool layout2d = max_abs_y < 1e-3f;

    std::array<float, kMaxVbapSpeakers> tmp{};

    if (layout2d) {
        // Azimuth arc across [az - spread/2, az + spread/2].
        const float spreadRad = spread * (kPi / 180.f);
        for (int k = 0; k < K; ++k) {
            const float t = (K <= 1) ? 0.f
                          : (-0.5f + static_cast<float>(k) / static_cast<float>(K - 1));
            const float az_k = az_rad + t * spreadRad;
            if (vbap_gain_into(layout, az_k, el_rad, tmp.data(), N) <= 0) continue;
            for (int i = 0; i < N; ++i) out[i] += tmp[static_cast<size_t>(i)];
        }
    } else {
        // Cone about the nominal direction (engine frame x=right,y=up,z=front).
        const float ce = std::cos(el_rad), se = std::sin(el_rad);
        const float ca = std::cos(az_rad), sa = std::sin(az_rad);
        const float ux = ce * sa, uy = se, uz = ce * ca;   // nominal unit vector
        // Tangent basis e1, e2 ⟂ u (same construction as the ported kernel).
        const float ax = (std::abs(ux) < 0.9f) ? 1.f : 0.f;
        const float ay = (std::abs(ux) < 0.9f) ? 0.f : 1.f;
        float e1x = ay * uz - 0.f * uy;   // a × u, with a = (ax, ay, 0)
        float e1y = 0.f * ux - ax * uz;
        float e1z = ax * uy - ay * ux;
        float e1n = std::sqrt(e1x * e1x + e1y * e1y + e1z * e1z);
        if (e1n < 1e-8f) e1n = 1.f;
        e1x /= e1n; e1y /= e1n; e1z /= e1n;
        const float e2x = uy * e1z - uz * e1y;   // u × e1
        const float e2y = uz * e1x - ux * e1z;
        const float e2z = ux * e1y - uy * e1x;

        const float beta = (spread * 0.5f) * (kPi / 180.f);
        const float sb = std::sin(beta), cb = std::cos(beta);
        constexpr float twoPi = 6.28318530717958647693f;
        for (int k = 0; k < K; ++k) {
            const float phi = twoPi * static_cast<float>(k) / static_cast<float>(K);
            const float c = sb * std::cos(phi);
            const float s = sb * std::sin(phi);
            float dx = ux * cb + e1x * c + e2x * s;
            float dy = uy * cb + e1y * c + e2y * s;
            float dz = uz * cb + e1z * c + e2z * s;
            const float dn = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dn > 1e-8f) { dx /= dn; dy /= dn; dz /= dn; }
            const float az_k = std::atan2(dx, dz);   // az = atan2(x, z)
            float yv = dy;
            if (yv >  1.f) yv =  1.f;
            if (yv < -1.f) yv = -1.f;
            const float el_k = std::asin(yv);
            if (vbap_gain_into(layout, az_k, el_k, tmp.data(), N) <= 0) continue;
            for (int i = 0; i < N; ++i) out[i] += tmp[static_cast<size_t>(i)];
        }
    }

    // Energy-normalise Σg² = 1.
    float ss = 0.f;
    for (int i = 0; i < N; ++i) ss += out[i] * out[i];
    if (ss < 1e-8f)
        return vbap_gain_into(layout, az_rad, el_rad, out, out_capacity); // degenerate → point
    const float inv = 1.f / std::sqrt(ss);
    for (int i = 0; i < N; ++i) out[i] *= inv;
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
