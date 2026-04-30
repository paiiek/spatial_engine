// P1: NullBackend pump_synchronous drives SpatialEngine for N blocks
// without exceptions, the engine reports blocks_processed == N, and the
// trace ring records exactly N events.

#include "audio_io/NullBackend.h"
#include "core/SpatialEngine.h"
#include "util/TraceRing.h"

#include <cstdio>
#include <cstdlib>

int main() {
    constexpr int kBlocks = 32;
    constexpr int kBlockSize = 64;
    constexpr int kOutChans = 8;

    spe::audio_io::NullBackend backend(48000.0, kOutChans, kBlockSize);
    spe::core::SpatialEngine engine;

    auto err = backend.pump_synchronous(&engine, kBlocks);
    if (err != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "FAIL: pump_synchronous returned %s\n",
                     spe::audio_io::describe(err));
        return 1;
    }

    if (engine.blocksProcessed() != kBlocks) {
        std::fprintf(stderr, "FAIL: blocks_processed=%llu (expected %d)\n",
                     static_cast<unsigned long long>(engine.blocksProcessed()), kBlocks);
        return 1;
    }

    int drained = 0;
    spe::util::TraceEvent ev;
    auto& trace = const_cast<spe::util::TraceRing256&>(engine.trace());
    while (trace.pop(ev)) {
        if (ev.payload_a != kBlockSize)  return 1;
        if (ev.payload_b != kOutChans)   return 1;
        ++drained;
    }
    if (drained != kBlocks) {
        std::fprintf(stderr, "FAIL: trace events drained=%d (expected %d)\n", drained, kBlocks);
        return 1;
    }

    std::printf("p1_null_backend OK: blocks=%d drained=%d\n", kBlocks, drained);
    return 0;
}
