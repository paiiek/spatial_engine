// core/src/sync/LTCDecoder.h
//
// SMPTE Linear Timecode (LTC) biphase-mark decoder.
//
// LTC encodes one timecode frame per 80 bits, transmitted as biphase-mark
// (a.k.a. Manchester-II) audio.  Each bit period contains a level transition
// at the bit boundary; a logical '1' adds a second transition at the
// midpoint, while a '0' has none.  The full frame ends with a fixed 16-bit
// sync word whose canonical value (bits 64..79 in transmission order) is
//   0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1
// which packs to 0xBFFC when bit 64 lands at uint16 LSB.
//
// processSample() consumes one float audio sample; when a complete frame's
// sync word matches the running 80-bit shift register the decoder fills the
// caller's Timecode struct and returns true.  RT-safe: no allocation, no
// I/O.  Bit-period estimation is a slow IIR; the decoder also accepts a
// fixed bit period for deterministic tests via setExpectedBitPeriod().

#pragma once
#include <cstdint>

namespace spe::sync {

struct Timecode {
    int  hours      = 0;
    int  minutes    = 0;
    int  seconds    = 0;
    int  frames     = 0;
    bool drop_frame = false;
};

class LTCDecoder {
public:
    LTCDecoder() = default;

    // Reset decoder state. Optional bit_period seeds the IIR; pass 0 to keep
    // the current estimate (default 24 samples → 25 fps @ 48 kHz).
    void reset(float bit_period_hint = 0.f) noexcept;

    // Override the auto-tuned bit period with a fixed value (for tests with
    // known sample-rate / fps). Pass 0 to fall back to auto-tuning.
    void setExpectedBitPeriod(float p) noexcept { bit_period_ = (p > 0.f) ? p : 24.f; }

    // Process one input sample. Returns true and writes 'tc' on a frame sync.
    bool processSample(float s, Timecode& tc) noexcept;

private:
    // Hysteresis sign tracker.
    bool last_high_              = false;
    int  samples_since_transition_ = 0;
    // Adaptive bit-period estimate (samples per LTC bit). Default 25fps@48k.
    float bit_period_ = 24.f;
    // Pending half of a '1' bit waiting for its second short interval.
    bool prev_was_short_ = false;
    // Sliding 80-bit register: data bits 0..63 in low_, sync window bits 64..79 in high_.
    // After 80 shifts: low_'s bit i == LTC bit i; high_'s uint16 bit i == LTC bit 64+i.
    std::uint64_t low_  = 0;
    std::uint16_t high_ = 0;
    int           filled_bits_ = 0;

    void shiftBit(int b) noexcept;
    bool checkAndDecode(Timecode& tc) const noexcept;
};

} // namespace spe::sync
