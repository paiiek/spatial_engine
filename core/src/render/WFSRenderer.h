// core/src/render/WFSRenderer.h
// Wave Field Synthesis renderer (Dreamscape convergence — full modes).
// Per-speaker gain+delay are computed by the ported reference kernel
// iae::computeWavefieldSynthesisDriving (render/ported/Wfs.h), which provides
// plane-wave vs spherical fronts, wavefront-curvature shaping, obliquity
// (cos φ) radial blend, and gain/delay shape scaling. The optional VBAP gain
// blend (WfsDrivingParams::vbapGainBlend) reuses the native analytic VBAP
// (AlgorithmAnalyticReference::vbap_gain_into) so the WFS path shares the same
// elevation/2D-3D-aware VBAP core established in the ② / ③ increments.
//
// Frame: the kernel is frame-agnostic (pure dot products + Euclidean lengths in
// one consistent frame), so speaker positions and the virtual source are fed in
// mmhoa-native coordinates (x=right, y=up, z=front) — NO Y<->Z adapter needed.
// Speaker "forward" is taken as the unit vector toward the listener (origin),
// matching the reference's "orientation stored toward listener" convention.
//
// Delay lines are allocated lazily on the control thread by ensureAllocated()
// (F5-M3b, Option C) — NOT at prepareToPlay — and read RT-safe behind ready_.

#pragma once
#include "render/RenderingAlgorithm.h"
#include "render/ported/SpatialMath.h"
#include "render/ported/SpeakerKind.h"
#include "render/ported/WfsDrivingParams.h"
#include "dsp/DelayLine.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <array>
#include <atomic>
#include <vector>

namespace spe::render {

class WFSRenderer : public RenderingAlgorithm {
public:
    static constexpr float F_MAX = 8000.f;
    // Speaker fan-out ceiling (matches the engine layout cap / other renderers).
    // Phase 0.5 (128 lift): derives from the single source of truth.
    static constexpr int   MAX_SPEAKERS = spe::MAX_SPEAKERS;

    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    // Control-thread (non-RT): allocate delays_/ramps_ exactly once, THEN publish
    // ready_ (release). Idempotent. The audio thread acquire-loads ready_ and
    // renders WFS silent until it flips, so it never reads half-built storage.
    void ensureAllocated();

    bool isReady() const noexcept {
        return ready_.load(std::memory_order_acquire);
    }

    // Configure the full-mode driving parameters. Control-thread only (call before
    // audio starts or between blocks, like the other per-algorithm params). Defaults
    // already match the reference mid-range; OSC/session plumbing of these lands in
    // a later increment (mirrors VAPRenderer's deferred param wiring).
    void setWfsParams(const iae::WfsDrivingParams& params,
                      bool plane_wave,
                      iae::WfsDelayReferenceMode delay_ref) noexcept {
        params_     = params;
        plane_wave_ = plane_wave;
        delay_ref_  = delay_ref;
    }

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

private:
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;

    // Full-mode driving parameters (reference defaults). Plain members read on the
    // audio thread, written on the control thread (same discipline as VAPRenderer).
    iae::WfsDrivingParams     params_{};
    bool                      plane_wave_ = false;
    iae::WfsDelayReferenceMode delay_ref_ = iae::WfsDelayReferenceMode::MinimumToNearestSecondary;

    // Speaker geometry precomputed at prepareToPlay (mmhoa-native frame):
    //   spk_pos_  : Cartesian metres (x=right, y=up, z=front)
    //   spk_fwd_  : unit vector toward listener/origin (= normalized(-pos))
    //   spk_kind_ : all Frontal (mmhoa has no per-speaker role; every speaker
    //               participates in the spatial group).
    std::array<iae::Vec3, MAX_SPEAKERS>       spk_pos_{};
    std::array<iae::Vec3, MAX_SPEAKERS>       spk_fwd_{};
    std::array<iae::SpeakerKind, MAX_SPEAKERS> spk_kind_{};

    // Heap-allocated delay/gain state: [obj_idx * num_speakers + spk_idx].
    // Allocated lazily by ensureAllocated() (F5-M3b), behind ready_.
    std::vector<spe::dsp::DelayLine<spe::dsp::WFS_MAX_DELAY_SAMPLES>> delays_; // MAX_OBJECTS * num_speakers
    std::vector<spe::dsp::GainRamp>  ramps_;  // MAX_OBJECTS * num_speakers

    std::atomic<bool> ready_{false};

    int flat_idx(int obj, int spk) const noexcept {
        return obj * num_speakers_ + spk;
    }
};

} // namespace spe::render
