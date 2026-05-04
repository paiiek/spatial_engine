// core/src/sync/LtcChase.h
//
// C1.c — Control-rate LTC consumer.
//
// Threads cleanly separate:
//   Audio thread (single producer):
//     LtcChase::pushSamples(in, n)  — copies samples into SpscRing<float>.
//     Allocation-free (SPE_RT_NO_ALLOC_SCOPE-safe).
//
//   Control thread (single consumer):
//     LtcChase::update()  — drains ring, feeds each sample through M7
//     LTCDecoder, atomically publishes the latest timecode for readers.
//
//   Any thread:
//     LtcChase::getCurrentTimecode(out)  — returns last published Timecode
//     plus a `valid` flag (lock-free, packed-uint32 atomic).
//
// The ring carries 65536 mono samples (~1.36 s at 48 kHz). With a 64-sample
// audio block and a control-thread that calls update() at >= 50 Hz, drops
// stay at zero by a wide margin — the C1.c test gate is "ring xruns == 0
// over 10 s @ 48 kHz, block 64".

#pragma once

#include "sync/LTCDecoder.h"
#include "util/SpscRing.h"

#include <atomic>
#include <cstdint>

namespace spe::sync {

class LtcChase {
public:
    LtcChase() = default;

    // Control thread. Initialise sample rate (used to seed bit-period hint
    // for 25 fps LTC) and reset internal decoder state.
    void prepare(double sample_rate, int fps_hint = 25) noexcept;

    // Audio thread (single producer). Push n samples from `in` into the
    // ring. Returns number actually accepted; remainder counts as drops.
    // Allocation-free.
    std::size_t pushSamples(const float* in, int n) noexcept;

    // Control thread (single consumer). Drain whatever is queued and run
    // each sample through the M7 biphase decoder. Publishes the latest
    // decoded timecode atomically. Safe to call at any cadence — calling
    // more often reduces ring occupancy.
    void update() noexcept;

    // Any thread. Reads the most recent timecode atomically. Returns false
    // if no full frame has decoded yet.
    bool getCurrentTimecode(Timecode& out) const noexcept;

    std::uint64_t ringDrops()      const noexcept { return ring_.drops(); }
    std::uint64_t framesDecoded()  const noexcept { return frames_decoded_.load(std::memory_order_relaxed); }
    bool          isLocked()       const noexcept { return tc_valid_.load(std::memory_order_acquire); }

    // Test hook: drain ring without invoking the decoder. Used by unit
    // tests that need to flush the producer side independently.
    void resetCounters() noexcept;

private:
    // Capacity tuned so a control-thread tick at 50 Hz cannot lag the
    // audio thread enough to overflow: 65536 / 48000 ≈ 1.36 s headroom.
    static constexpr std::size_t kRingCapacity = 65536;

    spe::util::SpscRing<float, kRingCapacity> ring_{};
    LTCDecoder                                decoder_{};
    std::atomic<bool>                         tc_valid_{false};
    // Packed timecode: bits [31:24]=hh, [23:16]=mm, [15:8]=ss, [7:0]=ff.
    // Atomic uint32 so getCurrentTimecode() is lock-free.
    std::atomic<std::uint32_t>                tc_packed_{0};
    std::atomic<std::uint64_t>                frames_decoded_{0};
    double                                    sample_rate_{48000.0};
};

}  // namespace spe::sync
