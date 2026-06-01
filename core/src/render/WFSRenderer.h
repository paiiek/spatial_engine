// core/src/render/WFSRenderer.h
// Wave Field Synthesis renderer.
// Per-speaker delay (r/c) + 1/sqrt(r) gain for point source.
// Spatial aliasing edge-fade beyond c/(2*f_max*spacing).
// Delay lines allocated on heap at prepareToPlay (RT-safe thereafter).

#pragma once
#include "render/RenderingAlgorithm.h"
#include "dsp/DelayLine.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <memory>
#include <vector>

namespace spe::render {

class WFSRenderer : public RenderingAlgorithm {
public:
    static constexpr float F_MAX = 8000.f;

    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

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
    // (16384) instead of the default 48000. This is the dominant footprint term.
    std::vector<spe::dsp::DelayLine<spe::dsp::WFS_MAX_DELAY_SAMPLES>> delays_; // size = MAX_OBJECTS * num_speakers
    std::vector<spe::dsp::GainRamp>  ramps_;  // size = MAX_OBJECTS * num_speakers

    int flat_idx(int obj, int spk) const noexcept {
        return obj * num_speakers_ + spk;
    }
};

} // namespace spe::render
