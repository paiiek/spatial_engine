// core/src/render/VBAPRenderer.h
// 2D VBAP renderer. Pair-based amplitude panning.
// SoA scratch pre-allocated at prepareToPlay.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "render/AlgorithmAnalyticReference.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <array>
#include <cstdint>
#include <vector>

namespace spe::render {

class VBAPRenderer : public RenderingAlgorithm {
public:
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

    // Test accessors for the gain cache.
    uint64_t cacheHits()   const noexcept { return cache_hits_; }
    uint64_t cacheMisses() const noexcept { return cache_misses_; }
    void resetCacheStats() noexcept { cache_hits_ = cache_misses_ = 0; }

private:
    // Per-object, per-speaker GainRamp (SoA scratch, MAX_OBJECTS * MAX_SPEAKERS).
    // Pre-allocated — no runtime allocation.
    std::array<std::array<spe::dsp::GainRamp, spe::MAX_SPEAKERS>, spe::MAX_OBJECTS> ramps_;
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;

    // --- Gain cache (open-addressing, fixed-size) ---
    struct CacheSlot {
        uint64_t key = UINT64_MAX;    // UINT64_MAX = empty sentinel
        std::vector<float> gains;     // sized to num_speakers in prepareToPlay
    };
    static constexpr int MAX_CACHE = 4096;
    static constexpr int RING_CAP  = MAX_CACHE / 2;   // max occupancy <= 50%
    static constexpr float AZ_BIN_DEG = 0.5f;
    static constexpr float EL_BIN_DEG = 0.5f;
    // Key packing: offset by AZ_OFFSET/EL_OFFSET so UINT64_MAX is unreachable
    static constexpr int AZ_OFFSET = 1440;
    static constexpr int EL_OFFSET =  360;

    std::array<CacheSlot, MAX_CACHE> cache_slots_{};
    std::array<int, RING_CAP> cache_ring_{};
    int ring_head_  = 0;
    int cache_size_ = 0;
    uint64_t cache_hits_   = 0;
    uint64_t cache_misses_ = 0;

    // v0.8 P1.3 (DSP-3) — audio-thread scratch for the cold-cache-miss VBAP
    // compute. Sized at prepareToPlay() to num_speakers_; never reallocated
    // afterwards. Replaces the pre-P1.3 std::vector temporaries inside
    // AlgorithmAnalyticReference::vbap_gain() that allocated on the audio
    // thread inside SPE_RT_NO_ALLOC_SCOPE(). Capacity is bounded by the fixed
    // ramps_[MAX_SPEAKERS] cap above; prepareToPlay() asserts num_speakers ≤
    // spe::MAX_SPEAKERS.
    std::vector<float> gain_scratch_;
};

} // namespace spe::render
