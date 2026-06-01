// core/src/render/WFSRenderer.h
// Wave Field Synthesis renderer.
// Per-speaker delay (r/c) + 1/sqrt(r) gain for point source.
// Spatial aliasing edge-fade beyond c/(2*f_max*spacing).
// Delay lines allocated lazily on the control thread by ensureAllocated() (F5-M3b,
// Option C) — NOT at prepareToPlay — and read RT-safe behind the ready_ flag.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "dsp/DelayLine.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <atomic>
#include <memory>
#include <vector>

namespace spe::render {

class WFSRenderer : public RenderingAlgorithm {
public:
    static constexpr float F_MAX = 8000.f;

    // F5-M3b (Option C): prepareToPlay does the non-allocating setup (layout,
    // sr, alias gain) and leaves the delay-line storage UNALLOCATED, publishing
    // ready_=false. The dominant footprint term (delays_, MAX_OBJECTS*num_spk *
    // 64 KB) is only allocated by ensureAllocated() — called on the control
    // thread the first time an object's algorithm becomes WFS — so a deployment
    // that never uses WFS never pays the term.
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    // Control-thread (non-RT): allocate delays_/ramps_ exactly once, THEN publish
    // ready_ (release). Idempotent. The audio thread acquire-loads ready_ and
    // renders WFS silent until it flips, so it never reads half-built storage.
    void ensureAllocated();

    bool isReady() const noexcept {
        return ready_.load(std::memory_order_acquire);
    }

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

private:
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;
    float  alias_fade_gain_ = 1.0f;

    // Heap-allocated: [obj_idx * num_speakers + spk_idx]
    // Allocated at prepareToPlay, RT-safe thereafter.
    // WFS delay is geometry-bounded (r/c) → right-sized to WFS_MAX_DELAY_SAMPLES
    // (16384) instead of the default 48000. This is the dominant footprint term;
    // allocated lazily by ensureAllocated() (F5-M3b), behind ready_.
    std::vector<spe::dsp::DelayLine<spe::dsp::WFS_MAX_DELAY_SAMPLES>> delays_; // size = MAX_OBJECTS * num_speakers
    std::vector<spe::dsp::GainRamp>  ramps_;  // size = MAX_OBJECTS * num_speakers

    // Allocate-then-publish flag (F5-M3b). Written release on the control thread
    // by ensureAllocated() AFTER delays_/ramps_ are sized & initialised; read
    // acquire by processBlock on the audio thread. delays_/ramps_ are NEVER
    // resized while the audio thread may read them (allocated exactly once).
    std::atomic<bool> ready_{false};

    int flat_idx(int obj, int spk) const noexcept {
        return obj * num_speakers_ + spk;
    }
};

} // namespace spe::render
