// core/src/ambi/AmbiDecoder.cpp
//
// Pseudo-inverse mode-matching decoder for Ambisonics orders 1..3.
//
// For each order build the encoding matrix E (K × S) where column s holds
// the K real spherical-harmonic coefficients (ACN, SN3D-consistent) at the
// position of speaker s. The decoding matrix D (S × K) is the Tikhonov-
// regularised left pseudo-inverse:
//
//   D = (E^T E + λI_S)^{-1} E^T
//
// This formulation works in both regimes (K ≤ S and K > S) and tolerates
// rank-deficient layouts (e.g. horizontal-only rigs where Y_1^0 = sin(el) is
// identically zero).  Audio-thread decode() is a plain S×K matrix-vector
// multiply per sample.

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spe::ambi {

namespace {

constexpr int kK[3] = {4, 9, 16};

// Solve (G + λI) M = E^T  via Gauss-Jordan with partial pivoting.
// Inputs:  G (S×S, double, will be overwritten), Et (S×K, double — RHS).
// Outputs: M (S×K, double — result D = G^{-1} E^T).
// On singular matrix the routine bails out with the partial result; lambda
// regularisation in the caller ensures G is well-conditioned in practice.
static void solveSPD(int S, int K,
                     std::vector<double>& G,
                     std::vector<double>& Et) noexcept {
    for (int i = 0; i < S; ++i) {
        // Pivot: max |G[r][i]| for r ≥ i.
        int piv = i;
        double piv_abs = std::abs(G[static_cast<size_t>(i) * S + i]);
        for (int r = i + 1; r < S; ++r) {
            const double v = std::abs(G[static_cast<size_t>(r) * S + i]);
            if (v > piv_abs) { piv_abs = v; piv = r; }
        }
        if (piv != i) {
            for (int c = 0; c < S; ++c) std::swap(G[static_cast<size_t>(i)*S+c], G[static_cast<size_t>(piv)*S+c]);
            for (int c = 0; c < K; ++c) std::swap(Et[static_cast<size_t>(i)*K+c], Et[static_cast<size_t>(piv)*K+c]);
        }
        const double diag = G[static_cast<size_t>(i)*S+i];
        if (std::abs(diag) < 1e-18) return; // numerical disaster; caller's λ should prevent
        const double inv = 1.0 / diag;
        for (int c = 0; c < S; ++c) G[static_cast<size_t>(i)*S+c]  *= inv;
        for (int c = 0; c < K; ++c) Et[static_cast<size_t>(i)*K+c] *= inv;
        for (int r = 0; r < S; ++r) {
            if (r == i) continue;
            const double f = G[static_cast<size_t>(r)*S+i];
            if (f == 0.0) continue;
            for (int c = 0; c < S; ++c) G[static_cast<size_t>(r)*S+c]  -= f * G[static_cast<size_t>(i)*S+c];
            for (int c = 0; c < K; ++c) Et[static_cast<size_t>(r)*K+c] -= f * Et[static_cast<size_t>(i)*K+c];
        }
    }
}

} // namespace

void AmbiDecoder::prepare(const geometry::SpeakerLayout& layout) {
    num_speakers_ = static_cast<int>(layout.speakers.size());
    if (num_speakers_ <= 0) return;
    for (int order = 1; order <= MAX_ORDER; ++order) {
        buildDecoderForOrder(layout, order);
    }
}

void AmbiDecoder::buildDecoderForOrder(const geometry::SpeakerLayout& layout, int order) {
    const int S = num_speakers_;
    const int K = kK[order - 1];

    // E[k * S + s] = SH_k(speaker s).  ACN ordering, SN3D normalisation.
    std::vector<double> E(static_cast<size_t>(K) * S, 0.0);
    for (int s = 0; s < S; ++s) {
        const auto& spk  = layout.speakers[static_cast<size_t>(s)];
        const float horiz = std::sqrt(spk.x * spk.x + spk.z * spk.z);
        const float az    = std::atan2(spk.x, spk.z);
        const float el    = std::atan2(spk.y, horiz);
        if (order == 1) {
            const auto c = AmbisonicEncoder::encode_1st_order(az, el);
            // ACN 0=W, 1=Y_1^-1 (legacy .Y), 2=Y_1^0 (legacy .Z), 3=Y_1^1 (legacy .X)
            E[0 * S + s] = c.W;
            E[1 * S + s] = c.Y;
            E[2 * S + s] = c.Z;
            E[3 * S + s] = c.X;
        } else if (order == 2) {
            const auto c = AmbisonicEncoder::encode_2nd_order(az, el);
            for (int k = 0; k < 9; ++k) E[static_cast<size_t>(k) * S + s] = c[static_cast<size_t>(k)];
        } else { // order == 3
            const auto c = AmbisonicEncoder::encode_3rd_order(az, el);
            for (int k = 0; k < 16; ++k) E[static_cast<size_t>(k) * S + s] = c[static_cast<size_t>(k)];
        }
    }

    // G = E^T E   (S × S)
    std::vector<double> G(static_cast<size_t>(S) * S, 0.0);
    for (int s1 = 0; s1 < S; ++s1) {
        for (int s2 = 0; s2 < S; ++s2) {
            double sum = 0.0;
            for (int k = 0; k < K; ++k) {
                sum += E[static_cast<size_t>(k) * S + s1] *
                       E[static_cast<size_t>(k) * S + s2];
            }
            G[static_cast<size_t>(s1) * S + s2] = sum;
        }
    }

    // Tikhonov regularisation: λ = max(1e-9, 1e-6 * trace/S).  Keeps the
    // matrix solvable when E is rank-deficient (degenerate layouts) without
    // perturbing well-conditioned cases.
    double trace = 0.0;
    for (int s = 0; s < S; ++s) trace += G[static_cast<size_t>(s) * S + s];
    const double lambda = std::max(1e-9, 1e-6 * trace / std::max(S, 1));
    for (int s = 0; s < S; ++s) G[static_cast<size_t>(s) * S + s] += lambda;

    // RHS = E^T arranged as (S × K) so solveSPD produces M = G^{-1} E^T.
    std::vector<double> Et(static_cast<size_t>(S) * K, 0.0);
    for (int s = 0; s < S; ++s) {
        for (int k = 0; k < K; ++k) {
            Et[static_cast<size_t>(s) * K + k] = E[static_cast<size_t>(k) * S + s];
        }
    }

    solveSPD(S, K, G, Et);

    auto& M = decode_matrices_[static_cast<size_t>(order - 1)];
    M.assign(static_cast<size_t>(S) * static_cast<size_t>(K), 0.f);
    for (int s = 0; s < S; ++s) {
        for (int k = 0; k < K; ++k) {
            M[static_cast<size_t>(s) * K + k] =
                static_cast<float>(Et[static_cast<size_t>(s) * K + k]);
        }
    }
}

void AmbiDecoder::decode(int order, const float* const* sh_planar,
                          int num_samples, float* out_interleaved) const noexcept
{
    if (order < 1 || order > MAX_ORDER) order = 1;
    const int S = num_speakers_;
    const int K = kK[order - 1];
    const float* M = decode_matrices_[static_cast<size_t>(order - 1)].data();

    std::memset(out_interleaved, 0,
                sizeof(float) * static_cast<size_t>(num_samples) * static_cast<size_t>(S));

    for (int n = 0; n < num_samples; ++n) {
        float* out_frame = out_interleaved + n * S;
        for (int s = 0; s < S; ++s) {
            const float* row = M + s * K;
            float acc = 0.f;
            for (int k = 0; k < K; ++k) acc += row[k] * sh_planar[k][n];
            out_frame[s] = acc;
        }
    }
}

void AmbiDecoder::decode(const float* W, const float* Y, const float* Z, const float* X,
                          int num_samples, float* out_interleaved) const noexcept
{
    const float* sh[4] = { W, Y, Z, X };
    decode(1, sh, num_samples, out_interleaved);
}

} // namespace spe::ambi
