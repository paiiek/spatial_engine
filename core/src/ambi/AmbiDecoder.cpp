// core/src/ambi/AmbiDecoder.cpp
//
// Multi-algorithm Ambisonics decoder for orders 1..3.
// Default: Tikhonov-regularised pseudo-inverse (PINV).
// Additional: MAX_RE, ALLRAD, EPAD, IN_PHASE (HOA Decoder Diversification sprint).
//
// Audio-thread decode() is a plain S×K matrix-vector multiply per sample —
// identical regardless of DecoderType (all type-specific logic in prepare()).

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "ambi/MaxREDecoder.hpp"
#include "ambi/AllRADDecoder.hpp"
#include "ambi/EPADDecoder.hpp"
#include "ambi/InPhaseDecoder.hpp"
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
        if (std::abs(diag) < 1e-18) return;
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

// ---- S0.5: buildEncodingMatrixE — single source of truth for E construction --
// E[k * S + s] = SH_k(speaker s), ACN ordering, SN3D normalisation.
// ACN re-map for 1st order: AmbiCoeffs1st {W,X,Y,Z} → ACN {W,Y,Z,X}
//   E[1]=c.Y, E[2]=c.Z, E[3]=c.X  — do NOT "fix" this, it is correct ACN.

void AmbiDecoder::buildEncodingMatrixE(const geometry::SpeakerLayout& layout,
                                        int order,
                                        std::vector<double>& E) {
    const int S = static_cast<int>(layout.speakers.size());
    const int K = kK[order - 1];
    E.assign(static_cast<size_t>(K) * S, 0.0);

    for (int s = 0; s < S; ++s) {
        const auto& spk   = layout.speakers[static_cast<size_t>(s)];
        const float horiz = std::sqrt(spk.x * spk.x + spk.z * spk.z);
        const float az    = std::atan2(spk.x, spk.z);
        const float el    = std::atan2(spk.y, horiz);
        if (order == 1) {
            const auto c = AmbisonicEncoder::encode_1st_order(az, el);
            // ACN 0=W, 1=Y_1^{-1}(legacy .Y), 2=Y_1^0(legacy .Z), 3=Y_1^1(legacy .X)
            E[0 * S + s] = c.W;
            E[1 * S + s] = c.Y;
            E[2 * S + s] = c.Z;
            E[3 * S + s] = c.X;
        } else if (order == 2) {
            const auto c = AmbisonicEncoder::encode_2nd_order(az, el);
            for (int k = 0; k < 9; ++k) E[static_cast<size_t>(k) * S + s] = c[static_cast<size_t>(k)];
        } else {
            const auto c = AmbisonicEncoder::encode_3rd_order(az, el);
            for (int k = 0; k < 16; ++k) E[static_cast<size_t>(k) * S + s] = c[static_cast<size_t>(k)];
        }
    }
}

// ---- PINV helper: Tikhonov pseudo-inverse decode matrix from E --------------

static void buildPinvMatrix(int S, int K,
                             const std::vector<double>& E,
                             std::vector<float>& M_out) {
    std::vector<double> G(static_cast<size_t>(S) * S, 0.0);
    for (int s1 = 0; s1 < S; ++s1)
        for (int s2 = 0; s2 < S; ++s2) {
            double sum = 0.0;
            for (int k = 0; k < K; ++k)
                sum += E[static_cast<size_t>(k)*S+s1] * E[static_cast<size_t>(k)*S+s2];
            G[static_cast<size_t>(s1)*S+s2] = sum;
        }

    double trace = 0.0;
    for (int s = 0; s < S; ++s) trace += G[static_cast<size_t>(s)*S+s];
    const double lambda = std::max(1e-9, 1e-6 * trace / std::max(S, 1));
    for (int s = 0; s < S; ++s) G[static_cast<size_t>(s)*S+s] += lambda;

    std::vector<double> Et(static_cast<size_t>(S) * K, 0.0);
    for (int s = 0; s < S; ++s)
        for (int k = 0; k < K; ++k)
            Et[static_cast<size_t>(s)*K+k] = E[static_cast<size_t>(k)*S+s];

    solveSPD(S, K, G, Et);

    M_out.assign(static_cast<size_t>(S) * static_cast<size_t>(K), 0.f);
    for (int s = 0; s < S; ++s)
        for (int k = 0; k < K; ++k)
            M_out[static_cast<size_t>(s)*K+k] = static_cast<float>(Et[static_cast<size_t>(s)*K+k]);
}

// ---- prepare / buildDecoderForOrder ----------------------------------------
//
// v0.8 P1.1 — Lock-free double-buffer publish. The control thread builds
// the new matrices into the INACTIVE slot (1 - active_slot_.load(relaxed));
// while building, the audio thread continues to read the still-published
// active slot. After all MAX_ORDER matrices are built, store-release on
// active_slot_ atomically swaps which slot the audio thread will pick up
// on its next decode() acquire-load. CONTROL THREAD ONLY (allocates).

void AmbiDecoder::prepare(const geometry::SpeakerLayout& layout) {
    num_speakers_ = static_cast<int>(layout.speakers.size());
    if (num_speakers_ <= 0) return;

    // Build into the INACTIVE slot. relaxed load is fine — only this
    // (control) thread writes active_slot_, so we know our own most recent
    // value without ordering. On first prepare(), active_slot_ is 0 →
    // inactive_slot=1 → publish to 1 on first call. Subsequent rebuilds
    // bounce between 0 and 1.
    const int active   = active_slot_.load(std::memory_order_relaxed);
    const int inactive = 1 - active;

    for (int order = 1; order <= MAX_ORDER; ++order)
        buildDecoderForOrder(layout, order, inactive);

    // Publish: all MAX_ORDER matrices in `inactive` are now fully built.
    // store-release pairs with the audio thread's load-acquire in decode().
    active_slot_.store(inactive, std::memory_order_release);
}

void AmbiDecoder::buildDecoderForOrder(const geometry::SpeakerLayout& layout,
                                       int order, int slot) {
    const int S = num_speakers_;
    const int K = kK[order - 1];

    std::vector<double> E;
    buildEncodingMatrixE(layout, order, E);  // S0.5 single source of truth

    auto& M = decode_matrices_[static_cast<size_t>(slot)]
                              [static_cast<size_t>(order - 1)];

    // 5-way dispatch. All paths produce S×K float matrix in M.
    // decode() reads M unchanged — no type_ switch there (Principle 4).
    switch (type_) {
    default:               // unknown value → clamp to PINV (AC-S3.4)
    case DecoderType::PINV:
        buildPinvMatrix(S, K, E, M);
        break;

    case DecoderType::MAX_RE: {
        // PINV base, then SH-side pre-multiply by max-rE weights g_l (M2HOA-Q2 decision).
        buildPinvMatrix(S, K, E, M);
        const auto w = MaxREDecoder::compute_max_re_weights(order);
        for (int s = 0; s < S; ++s)
            for (int k = 0; k < K; ++k) {
                const int l = static_cast<int>(std::sqrt(static_cast<double>(k)));
                M[static_cast<size_t>(s)*K+k] *= w[static_cast<size_t>(l)];
            }
        break;
    }

    case DecoderType::ALLRAD:
        M = AllRADDecoder::build_allrad_matrix(order, layout);
        break;

    case DecoderType::EPAD:
        M = EPADDecoder::build_epad_matrix(order, layout);
        break;

    case DecoderType::IN_PHASE: {
        // PINV base, then SH-side pre-multiply by in-phase weights g_l (M2HOA-Q2).
        buildPinvMatrix(S, K, E, M);
        const auto w = InPhaseDecoder::compute_in_phase_weights(order);
        for (int s = 0; s < S; ++s)
            for (int k = 0; k < K; ++k) {
                const int l = static_cast<int>(std::sqrt(static_cast<double>(k)));
                M[static_cast<size_t>(s)*K+k] *= w[static_cast<size_t>(l)];
            }
        break;
    }
    }
}

// ---- decode (audio thread — RT-safe, no allocation) ------------------------
//
// CONTROL THREAD ONLY responsibility: do NOT add any switch on type_ here.
// All decoder-type-specific logic lives in prepare() / buildDecoderForOrder().
// See HOA Decoder Diversification plan Principles 2 and 4.
//
// v0.8 P1.1 — Lock-free double-buffer consume.
//
// We load active_slot_ ACQUIRE exactly ONCE per block and pin that slot
// for the entire block (no torn read across an in-flight swap). The
// store-release in prepare() synchronises-with this acquire so all matrix
// bytes published by the control thread are visible.
//
// Click-free choice: 1-BLOCK HARD SWITCH on the block boundary. The
// existing setOrder() flip already changes the active K mid-block-period
// the same way (atomic relaxed read at top of decode), and the algorithm
// dispatch (VBAP/DBAP/WFS/Ambi) is gated by per-renderer activity flags —
// the engine has no architectural smoothness contract on decoder type
// changes. A full crossfade was deemed unnecessary churn; if a future
// listening test wants it, mirror the GainRamp pattern used in
// VBAPRenderer.

void AmbiDecoder::decode(int order, const float* const* sh_planar,
                          int num_samples, float* out_interleaved) const noexcept
{
    if (order < 1 || order > MAX_ORDER) order = 1;
    const int S = num_speakers_;
    const int K = kK[order - 1];
    // Acquire pairs with release in prepare(). Single read per block —
    // even if the control thread swaps mid-block (next block sees new
    // slot), this block's S×K matrix pointer remains stable.
    const int slot = active_slot_.load(std::memory_order_acquire);
    const float* M = decode_matrices_[static_cast<size_t>(slot)]
                                     [static_cast<size_t>(order - 1)].data();

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
