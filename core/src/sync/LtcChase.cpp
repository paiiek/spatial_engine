// core/src/sync/LtcChase.cpp
//
// C1.c implementation. See header for thread contract.

#include "sync/LtcChase.h"

namespace spe::sync {

namespace {

inline std::uint32_t packTimecode(const Timecode& tc) noexcept {
    return  (static_cast<std::uint32_t>(tc.hours   & 0xFF) << 24)
          | (static_cast<std::uint32_t>(tc.minutes & 0xFF) << 16)
          | (static_cast<std::uint32_t>(tc.seconds & 0xFF) <<  8)
          |  static_cast<std::uint32_t>(tc.frames  & 0xFF);
}

inline Timecode unpackTimecode(std::uint32_t packed) noexcept {
    Timecode tc;
    tc.hours      = static_cast<int>((packed >> 24) & 0xFFu);
    tc.minutes    = static_cast<int>((packed >> 16) & 0xFFu);
    tc.seconds    = static_cast<int>((packed >>  8) & 0xFFu);
    tc.frames     = static_cast<int>( packed        & 0xFFu);
    tc.drop_frame = false;  // drop-frame flag intentionally not packed
    return tc;
}

}  // namespace

void LtcChase::prepare(double sample_rate, int fps_hint) noexcept {
    sample_rate_ = sample_rate;
    const float bit_period = (fps_hint > 0)
        ? static_cast<float>(sample_rate / static_cast<double>(fps_hint * 80))
        : 24.f;
    decoder_.reset(bit_period);
    decoder_.setExpectedBitPeriod(bit_period);
    tc_valid_.store(false, std::memory_order_release);
    tc_packed_.store(0,    std::memory_order_release);
    frames_decoded_.store(0, std::memory_order_relaxed);
    // Drain anything stale.
    float discard;
    while (ring_.pop(discard)) {}
    ring_.reset_drops();
}

std::size_t LtcChase::pushSamples(const float* in, int n) noexcept {
    if (!in || n <= 0) return 0;
    std::size_t accepted = 0;
    for (int i = 0; i < n; ++i) {
        if (ring_.push(in[i])) ++accepted;
        // SpscRing::push already increments drops_ on failure; nothing more
        // to track here.
    }
    return accepted;
}

void LtcChase::update() noexcept {
    Timecode tc;
    float    s;
    while (ring_.pop(s)) {
        if (decoder_.processSample(s, tc)) {
            tc_packed_.store(packTimecode(tc), std::memory_order_release);
            tc_valid_.store(true, std::memory_order_release);
            frames_decoded_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool LtcChase::getCurrentTimecode(Timecode& out) const noexcept {
    if (!tc_valid_.load(std::memory_order_acquire)) return false;
    out = unpackTimecode(tc_packed_.load(std::memory_order_acquire));
    return true;
}

void LtcChase::resetCounters() noexcept {
    ring_.reset_drops();
    frames_decoded_.store(0, std::memory_order_relaxed);
}

}  // namespace spe::sync
