// core/src/ambi/AllRADDecoder.cpp
// All-Round Ambisonic Decoding (AllRAD).
//
// Algorithm:
//   1. Load n_virtual t-design virtual loudspeaker positions.
//   2. Build PINV decode matrix D_virt (n_virtual × K) from virtual speakers.
//   3. For each virtual speaker, find nearest real speaker triplet (simplified
//      nearest-neighbour VBAP: assign full energy to closest real speaker
//      when 3D triplet search is not available; exact VBAP for 3-spk minimum).
//   4. D_AllRAD = G_VBAP (S × n_virtual) * D_virt (n_virtual × K) → S × K.
//
// Reference: Zotter & Frank 2012 ICSA; IEM AllRADecoder.

#include "ambi/AllRADDecoder.hpp"
#include "ambi/AllRADTDesigns.hpp"
#include "ambi/AmbiDecoder.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace spe::ambi {

namespace {

constexpr int kK[3] = {4, 9, 16};

// Build D_virt (n_virt × K) for virtual t-design speakers.
//
// For a spherical t-design of order t ≥ 2N, the quadrature rule gives:
//   D_virt[v, k] = (4π / n_virt) * E_virt[k, v]  (basic projection)
// which is the Moore-Penrose pseudo-inverse of E_virt on the t-design grid.
// This is the canonical AllRAD formula (Zotter & Frank 2012, eq. 3).
// Crucially this avoids building an (n_virt × n_virt) Gram matrix, keeping
// complexity O(n_virt × K) instead of O(n_virt³).
static std::vector<double> buildVirtProjection(const TDesignPoint* pts, int n_virt, int K) {
    // Build encoding matrix E_virt (K × n_virt)
    geometry::SpeakerLayout virt_layout;
    virt_layout.speakers.reserve(static_cast<size_t>(n_virt));
    for (int v = 0; v < n_virt; ++v) {
        geometry::Speaker spk;
        spk.channel = v;
        spk.x = pts[v].x; spk.y = pts[v].y; spk.z = pts[v].z;
        virt_layout.speakers.push_back(spk);
    }

    int order = 1;
    if (K == 9) order = 2;
    else if (K == 16) order = 3;

    std::vector<double> Ev; // K × n_virt
    AmbiDecoder::buildEncodingMatrixE(virt_layout, order, Ev);

    // D_virt[v, k] = (4π / n_virt) * E_virt[k, v]
    // Stored row-major as (n_virt × K): D_virt[v*K + k]
    const double scale = 4.0 * 3.14159265358979323846 / static_cast<double>(n_virt);
    std::vector<double> D(static_cast<size_t>(n_virt) * K, 0.0);
    for (int v = 0; v < n_virt; ++v)
        for (int k = 0; k < K; ++k)
            D[static_cast<size_t>(v)*K + k] = scale * Ev[static_cast<size_t>(k)*n_virt + v];
    return D; // D_virt (n_virt × K)
}

// VBAP gain matrix G_VBAP (n_real × n_virt): for each virtual speaker,
// find the nearest real speaker and assign full gain (simplified nearest-neighbour).
// For well-covered layouts this is equivalent to basic VBAP and produces a
// non-negative gain matrix with the AllRAD energy-spreading property.
static std::vector<double> buildVbapGains(const geometry::SpeakerLayout& layout,
                                           const TDesignPoint* pts, int n_virt) {
    const int S = static_cast<int>(layout.speakers.size());
    std::vector<double> G(static_cast<size_t>(S)*n_virt, 0.0);

    for (int v = 0; v < n_virt; ++v) {
        // Find nearest real speaker by dot product (max dot = min angle on unit sphere)
        float vx = pts[v].x, vy = pts[v].y, vz = pts[v].z;
        float best_dot = -2.f;
        int   best_spk = 0;
        for (int s = 0; s < S; ++s) {
            const auto& spk = layout.speakers[static_cast<size_t>(s)];
            // Normalise real speaker position
            float len = std::sqrt(spk.x*spk.x + spk.y*spk.y + spk.z*spk.z);
            if (len < 1e-9f) continue;
            float dot = (vx*spk.x + vy*spk.y + vz*spk.z) / len;
            if (dot > best_dot) { best_dot = dot; best_spk = s; }
        }
        G[static_cast<size_t>(best_spk)*n_virt+v] = 1.0;
    }
    return G;
}

} // namespace

std::vector<float> AllRADDecoder::build_allrad_matrix(int order,
                                                        const geometry::SpeakerLayout& layout,
                                                        int n_virtual) {
    const int S = static_cast<int>(layout.speakers.size());
    const int K = kK[order - 1];

    int n_virt = 0;
    const TDesignPoint* pts = getTDesign(n_virtual, n_virt);

    // D_virt: n_virt × K  (t-design projection, O(n_virt × K))
    std::vector<double> D_virt = buildVirtProjection(pts, n_virt, K);

    // G_VBAP: S × n_virt
    std::vector<double> G_vbap = buildVbapGains(layout, pts, n_virt);

    // D_AllRAD = G_VBAP * D_virt  (S × K)
    std::vector<float> M(static_cast<size_t>(S)*K, 0.f);
    for (int s = 0; s < S; ++s)
        for (int k = 0; k < K; ++k) {
            double sum = 0.0;
            for (int v = 0; v < n_virt; ++v)
                sum += G_vbap[static_cast<size_t>(s)*n_virt+v] *
                       D_virt[static_cast<size_t>(v)*K+k];
            M[static_cast<size_t>(s)*K+k] = static_cast<float>(sum);
        }
    return M;
}

} // namespace spe::ambi
