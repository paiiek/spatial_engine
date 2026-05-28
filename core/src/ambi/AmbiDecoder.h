// core/src/ambi/AmbiDecoder.h
// Multi-order Ambisonics mode-matching decoder (1st/2nd/3rd order).
// Converts K-channel B-format (ACN, K=(order+1)²) to N-speaker output.
// Supports 5 decoder algorithms selectable via DecoderType (default PINV).
// RT-safe: no allocation in decode(). All matrices pre-allocated in prepare().
//
// v0.8 audit P1.1 (DSP-1 / M2HOA-Q14) — runtime decoder-type switch via
// LOCK-FREE DOUBLE-BUFFER. The pre-P1.1 layout was a single
// std::array<std::vector<float>, MAX_ORDER>; calling prepare() from a
// control-tick while the audio thread was inside decode() would reallocate
// the buffer the audio thread was mid-read on (use-after-free / torn read).
// The fix: two slots, each holding ALL MAX_ORDER decode matrices, with a
// single atomic<int> active_slot_ published store-release on the control
// thread and load-acquire once per block on the audio thread.
//
// BINDING INVARIANT — the 2-slot scheme is safe ONLY because the control-
// thread rebuild cadence (~1 Hz) is ≥1 audio-block-period (~10.7 ms @
// 512/48k) slower than the audio thread. The inactive slot is quiescent
// ~93 blocks before the next reuse. If a future change ever drives the
// apply rate faster than one-per-block, swap to an explicit quiescence
// handshake (audio publishes last-consumed slot index; control waits until
// the to-be-rebuilt slot ≠ last-consumed). The concurrency test
// (build_relacy_off_rton) MUST drive RAPID back-to-back switches (faster
// than the production 1 Hz tick) so it would FAIL if this slack is ever
// removed — otherwise the test passes vacuously.

#pragma once
#include "geometry/SpeakerLayout.h"
#include <array>
#include <atomic>
#include <vector>

namespace spe::ambi {

// Decoder algorithm selection. CONTROL THREAD ONLY — switching type triggers
// a prepare() rebuild; never changed mid-buffer.
// Explicit integer values for ABI stability.
enum class DecoderType : int {
    PINV     = 0,  // Tikhonov regularised pseudo-inverse (default, backward-compat)
    MAX_RE   = 1,  // max-rE weighted (Zotter & Frank 2019 eq.4.49; SPARTA canonical)
    ALLRAD   = 2,  // All-Round Ambisonic Decoding via virtual loudspeakers (Zotter 2012)
    EPAD     = 3,  // Energy-Preserving Ambisonic Decoding, Jacobi SVD (Zotter & Frank 2012)
    IN_PHASE = 4,  // In-phase decoder (Daniel 2000 §3.30)
};

class AmbiDecoder {
public:
    static constexpr int MAX_ORDER = 3;
    static constexpr int MAX_K     = (MAX_ORDER + 1) * (MAX_ORDER + 1); // 16

    // Build decoders for orders 1, 2, 3 using the active DecoderType. Allocates.
    // CONTROL THREAD ONLY.
    void prepare(const geometry::SpeakerLayout& layout);

    // Set decoder algorithm. Clamps unknown int values to PINV.
    // Takes effect on the next prepare() call. CONTROL THREAD ONLY.
    void setDecoderType(DecoderType t) noexcept { type_ = t; }
    DecoderType decoderType() const noexcept { return type_; }

    // Multi-order decode: K-channel ACN-ordered SH input → N-speaker output.
    // K = (order+1)² ∈ {4, 9, 16}. order ∈ {1, 2, 3}.
    // sh_planar[k]: planar buffer for ACN channel k, num_samples long.
    // out_interleaved: output buffer sized num_samples * numSpeakers().
    //   out[sample * num_spk + spk_idx]
    // RT-safe: no allocation. Out-of-range order silently clamps to 1.
    //
    // CONTROL THREAD ONLY responsibility: do NOT add any switch on type_ here.
    // All decoder-type-specific logic lives in prepare() / buildDecoderForOrder().
    // See HOA Decoder Diversification plan Principles 2 and 4.
    void decode(int order, const float* const* sh_planar,
                int num_samples, float* out_interleaved) const noexcept;

    // Legacy 1st-order signature (W, Y, Z, X planar) — kept for compatibility.
    void decode(const float* W, const float* Y, const float* Z, const float* X,
                int num_samples, float* out_interleaved) const noexcept;

    int numSpeakers() const noexcept { return num_speakers_; }

    // Build the K×S encoding matrix E for a given order and speaker layout.
    // E[k * S + s] = SH_k(speaker s), ACN ordering, SN3D normalisation.
    //
    // ACN field-order re-map for 1st order: AmbiCoeffs1st fields are {W,X,Y,Z}
    // but ACN ordering is {W,Y,Z,X} → E[1]=c.Y, E[2]=c.Z, E[3]=c.X.
    // This is the single source of truth — do NOT re-derive in new decoders.
    //
    // CONTROL THREAD ONLY (may allocate if E.capacity() < K*S).
    static void buildEncodingMatrixE(const geometry::SpeakerLayout& layout,
                                     int order,
                                     std::vector<double>& E);

private:
    int         num_speakers_ = 0;
    DecoderType type_         = DecoderType::PINV;

    // v0.8 P1.1 — DOUBLE-BUFFER. 2 slots, each holds ALL MAX_ORDER
    // decode matrices (NOT a per-order index, see BINDING INVARIANT above:
    // one block must NOT mix order-2 from a new slot and order-3 from the
    // old). decode_matrices_[slot][order-1][s * K + k]; K = (order+1)².
    // The control thread builds the new matrices into the INACTIVE slot
    // (1 - active_slot_), then publishes via store-release on active_slot_.
    // The audio thread loads active_slot_ acquire ONCE per decode() block.
    std::array<std::array<std::vector<float>, MAX_ORDER>, 2> decode_matrices_;
    std::atomic<int> active_slot_{0};

    void buildDecoderForOrder(const geometry::SpeakerLayout& layout,
                              int order, int slot);
};

} // namespace spe::ambi
