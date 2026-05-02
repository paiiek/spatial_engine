// core/src/render/AmbisonicRenderer.h
// Ambisonic encoder + decoder chain renderer.
// Encodes each active object into 1st-order B-format, sums, then decodes
// to speaker outputs via AmbiDecoder (mode-matching projection).
// RT-safe: no allocation in processBlock().

#pragma once
#include "render/RenderingAlgorithm.h"
#include "ambi/AmbiDecoder.h"
#include "core/Constants.h"
#include <vector>

namespace spe::render {

class AmbisonicRenderer : public RenderingAlgorithm {
public:
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

private:
    ambi::AmbiDecoder decoder_;
    // 4-channel B-format accumulation buffers (W, Y, Z, X), each MAX_BLOCK long.
    std::vector<float> buf_W_;
    std::vector<float> buf_Y_;
    std::vector<float> buf_Z_;
    std::vector<float> buf_X_;
};

} // namespace spe::render
