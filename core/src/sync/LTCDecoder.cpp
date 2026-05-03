// core/src/sync/LTCDecoder.cpp

#include "sync/LTCDecoder.h"
#include <cmath>

namespace spe::sync {

namespace {

// Sync-word value when bit 64 lands at uint16 LSB and bit 79 at uint16 MSB.
// LTC sync (transmission order): 0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1.
constexpr std::uint16_t kSyncWord = 0xBFFC;

inline int bcd(std::uint64_t low, int shift, std::uint64_t mask) noexcept {
    return static_cast<int>((low >> shift) & mask);
}

} // namespace

void LTCDecoder::reset(float bit_period_hint) noexcept {
    last_high_ = false;
    samples_since_transition_ = 0;
    if (bit_period_hint > 0.f) bit_period_ = bit_period_hint;
    prev_was_short_ = false;
    low_ = 0;
    high_ = 0;
    filled_bits_ = 0;
}

void LTCDecoder::shiftBit(int b) noexcept {
    const std::uint64_t carry = static_cast<std::uint64_t>(high_ & 1u);
    low_  = (low_  >> 1) | (carry << 63);
    high_ = static_cast<std::uint16_t>(
              (static_cast<std::uint32_t>(high_) >> 1)
              | ((static_cast<std::uint32_t>(b & 1)) << 15));
    if (filled_bits_ < 80) ++filled_bits_;
}

bool LTCDecoder::checkAndDecode(Timecode& tc) const noexcept {
    if (filled_bits_ < 80) return false;
    if (high_ != kSyncWord) return false;

    const int fu = bcd(low_,  0, 0xFu);
    const int ft = bcd(low_,  8, 0x3u);
    const int su = bcd(low_, 16, 0xFu);
    const int st = bcd(low_, 24, 0x7u);
    const int mu = bcd(low_, 32, 0xFu);
    const int mt = bcd(low_, 40, 0x7u);
    const int hu = bcd(low_, 48, 0xFu);
    const int ht = bcd(low_, 56, 0x3u);

    Timecode candidate;
    candidate.frames     = ft * 10 + fu;
    candidate.seconds    = st * 10 + su;
    candidate.minutes    = mt * 10 + mu;
    candidate.hours      = ht * 10 + hu;
    candidate.drop_frame = ((low_ >> 10) & 1u) != 0;

    // Sanity bounds — protect callers from corrupted ring without parity.
    if (candidate.hours   > 23) return false;
    if (candidate.minutes > 59) return false;
    if (candidate.seconds > 59) return false;
    if (candidate.frames  > 30) return false;

    tc = candidate;
    return true;
}

bool LTCDecoder::processSample(float s, Timecode& tc) noexcept {
    ++samples_since_transition_;
    const bool now_high = (s > 0.f);
    if (now_high == last_high_) return false;
    last_high_ = now_high;

    const int interval = samples_since_transition_;
    samples_since_transition_ = 0;

    // Reject obviously invalid blips (transients shorter than ~⅛ bit).
    if (static_cast<float>(interval) < bit_period_ * 0.125f) return false;

    const float threshold = bit_period_ * 0.75f;
    if (static_cast<float>(interval) > threshold) {
        shiftBit(0);
        // Slow IIR on bit-period estimate using observed long interval.
        bit_period_ = bit_period_ * 0.95f + static_cast<float>(interval) * 0.05f;
        prev_was_short_ = false;
        return checkAndDecode(tc);
    }

    if (prev_was_short_) {
        shiftBit(1);
        // A '1' is two halves; combined width ≈ bit_period.
        bit_period_ = bit_period_ * 0.95f + static_cast<float>(interval) * 2.f * 0.05f;
        prev_was_short_ = false;
        return checkAndDecode(tc);
    }
    prev_was_short_ = true;
    return false;
}

} // namespace spe::sync
