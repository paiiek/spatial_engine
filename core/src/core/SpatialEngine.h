// core/src/core/SpatialEngine.h
//
// Top-level orchestrator. P1: implements the AudioCallback shape with a
// silence-emitting body (zero objects, no DSP). Future phases compose:
//   - P3: per-object DSP chain + RenderingAlgorithm trio
//   - P4: command FIFO drain + StateModel mirror
//   - P5: AudioMatrix routing + AudioInput pull
//   - P7: FDNReverb send / mix

#pragma once

#include "audio_io/AudioCallback.h"
#include "core/Constants.h"
#include "util/TraceRing.h"
#include "util/XrunCounter.h"

#include <atomic>

namespace spe::core {

class SpatialEngine final : public spe::audio_io::AudioCallback {
public:
    SpatialEngine();
    ~SpatialEngine() override;

    void audioBlock(const spe::audio_io::AudioBlock& block) override;
    void prepareToPlay(double sample_rate, int max_block_size) override;
    void releaseResources() override;

    bool isPrepared() const noexcept { return prepared_.load(); }
    std::uint64_t blocksProcessed() const noexcept { return blocks_processed_.load(); }
    const util::TraceRing256& trace() const noexcept { return trace_; }

private:
    std::atomic<bool>          prepared_{false};
    std::atomic<std::uint64_t> blocks_processed_{0};
    double                     sample_rate_{0.0};
    int                        max_block_size_{0};
    util::TraceRing256         trace_;
    util::XrunCounter          internal_xruns_;
};

}  // namespace spe::core
