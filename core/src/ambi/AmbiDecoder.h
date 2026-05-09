// core/src/ambi/AmbiDecoder.h
// Multi-order Ambisonics mode-matching decoder (1st/2nd/3rd order).
// Converts K-channel B-format (ACN, K=(order+1)²) to N-speaker output.
// Supports 5 decoder algorithms selectable via DecoderType (default PINV).
// RT-safe: no allocation in decode(). All matrices pre-allocated in prepare().

#pragma once
#include "geometry/SpeakerLayout.h"
#include <array>
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
    // decode_matrices_[order-1][s * K + k]; K = (order+1)².
    std::array<std::vector<float>, MAX_ORDER> decode_matrices_;

    void buildDecoderForOrder(const geometry::SpeakerLayout& layout, int order);
};

} // namespace spe::ambi
