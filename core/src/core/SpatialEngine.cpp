// core/src/core/SpatialEngine.cpp

#include "core/SpatialEngine.h"

#include "util/RtAssertNoAlloc.h"

#include <chrono>

namespace spe::core {

SpatialEngine::SpatialEngine()  = default;
SpatialEngine::~SpatialEngine() = default;

void SpatialEngine::prepareToPlay(double sample_rate, int max_block_size) {
    sample_rate_    = sample_rate;
    max_block_size_ = max_block_size;
    prepared_.store(true);
}

void SpatialEngine::releaseResources() {
    prepared_.store(false);
}

void SpatialEngine::audioBlock(const spe::audio_io::AudioBlock& block) {
    SPE_RT_NO_ALLOC_SCOPE();

    if (block.num_frames > spe::MAX_BLOCK) {
        internal_xruns_.record_overrun();
        return;
    }

    // P1: silence emission. Zeros are already filled by the backend; we
    // record a TraceEvent for observability so /sys/metrics and the soak
    // harness can see the engine is alive.
    blocks_processed_.fetch_add(1, std::memory_order_relaxed);

    util::TraceEvent ev;
    ev.timestamp_ns = block.hw_timestamp_ns;
    ev.kind         = 1;  // SPE_TRACE_BLOCK_PROCESSED
    ev.payload_a    = static_cast<std::uint32_t>(block.num_frames);
    ev.payload_b    = static_cast<std::uint32_t>(block.output_channel_count);
    trace_.push(ev);
}

}  // namespace spe::core
