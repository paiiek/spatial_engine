// core/src/render/ported/RoomCluster.h
//
// Dreamscape Convergence ⑥e-2 — Cluster (mid-field) diffusion core ported from
// the reference Room Engine.
//
// Provenance: github.com/dreamscapeaudio2023-star/immersive-audio-engine
//   Source/RoomEngine.cpp @ commit f2cb796. Byte-faithful port of the cluster
//   feedforward-tap diffusion delay line (RoomEngine::finishBlock, :605-647):
//   a single delay line driven by the cluster bus, summed through 6 triangular-
//   weighted feedforward taps whose offsets are derived geometrically from a
//   virtual room volume, with a diffusion-scaled output gain. State setup mirrors
//   RoomEngine.cpp:204-206 (line length 16384) and reset :260-262.
//
// This core is pure scalar DSP — no geometry, no JUCE. It emits the MONO diffused
// cluster signal (the reference `outC` stream, before per-speaker distribution).
// The cluster bus absorption EQ (clusterBusHp/Lp) and the spatial distribution of
// the cluster output across the array (the opp-biased diffuse VBAP gains) are the
// follow-on live-wiring increment, mirroring the ⑥a RoomFdn -> ⑥b wiring and
// ⑥c RoomEarly -> ⑥d wiring split. This lets the cluster timing/diffusion be
// unit-tested in isolation.
//
// Adaptations vs the reference: juce::jlimit/jmax/jmin -> std::clamp/max/min,
// juce::AudioBuffer cluster line -> an owned std::vector<float>.

#pragma once

#include <array>
#include <vector>

namespace iae {

// Reference cluster defaults (SpatialSessionState.h roomCluster*).
struct RoomClusterParams {
    float diffusion01    = 0.48f;  // [0,1] feedforward density/output gain
    float virtualVolumeM3 = 630.f; // virtual room volume (m^3); floored at 50
};

class RoomCluster {
public:
    static constexpr int kFeedforwardTaps = 6;     // RoomEngine.h kClusterFeedforwardTaps
    static constexpr int kLineLen         = 16384; // RoomEngine.cpp:205 clusterDelayLine length

    // Allocates the cluster delay line (control thread). May allocate.
    void prepare(double sampleRateHz);

    // Clear the delay line + write position (keeps allocation).
    void reset() noexcept;

    void setParams(const RoomClusterParams& p) noexcept { params_ = p; }
    const RoomClusterParams& params() const noexcept { return params_; }

    bool ready() const noexcept { return ready_; }

    // RT-safe: no allocation. Reads `in[0..n)` (the cluster bus), writes the mono
    // diffused cluster output into `out[0..n)`. `out` must hold n floats.
    void process(const float* in, int n, float* out) noexcept;

private:
    static inline int wrapIndex(int x, int m) noexcept {
        x %= m; if (x < 0) x += m; return x;
    }

    RoomClusterParams   params_ {};
    double              sampleRate_ = 48000.0;
    std::vector<float>  line_;          // length kLineLen
    int                 writePos_ = 0;
    bool                ready_ = false;
};

} // namespace iae
