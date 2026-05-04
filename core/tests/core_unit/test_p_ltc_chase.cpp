// core/tests/core_unit/test_p_ltc_chase.cpp
//
// C1.c acceptance tests for LtcChase (audio→ring→control-thread decoder).
// Gates from .omc/plans/spatial-engine-phaseC.md C1.c:
//   - synthetic LTC fixture at 48 kHz biphase pattern, 25 fps
//   - control-rate getCurrentTimecode advances 1 frame per 1/25 s within
//     ±1 frame over 1000 frames (40 s simulated)
//   - ring xruns == 0 over 10 s @ 48 kHz, block 64 (NullBackend cadence)
//   - allocation-free hot path (asserted under SPE_RT_ASSERTS=1)

#include "sync/LtcChase.h"
#include "sync/LTCDecoder.h"
#include "util/RtAssertNoAlloc.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace spe::sync;

namespace {

// ---- LTC biphase synthesis (matches test_p_ltc_decoder reference) ----------

std::array<int, 80> packLtcFrame(int hh, int mm, int ss, int ff) {
    std::array<int, 80> bits{};
    auto setBcd = [&](int start, int width, int value) {
        for (int i = 0; i < width; ++i) bits[start + i] = (value >> i) & 1;
    };
    setBcd(0,  4, ff % 10);
    setBcd(8,  2, ff / 10);
    setBcd(16, 4, ss % 10);
    setBcd(24, 3, ss / 10);
    setBcd(32, 4, mm % 10);
    setBcd(40, 3, mm / 10);
    setBcd(48, 4, hh % 10);
    setBcd(56, 2, hh / 10);
    static constexpr int kSync[16] = {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1};
    for (int i = 0; i < 16; ++i) bits[64 + i] = kSync[i];
    return bits;
}

void renderBiphaseFrame(const std::array<int, 80>& bits, int sample_rate, int fps,
                        std::vector<float>& out, int& level) {
    const int bit_full = sample_rate / (fps * 80);   // 24 samples @ 48 kHz / 25 fps
    const int bit_half = bit_full / 2;
    for (int b = 0; b < 80; ++b) {
        level = -level;  // bit-boundary toggle
        for (int n = 0; n < bit_half; ++n) out.push_back(static_cast<float>(level));
        if (bits[b]) level = -level;  // mid-bit toggle for '1'
        for (int n = bit_half; n < bit_full; ++n) out.push_back(static_cast<float>(level));
    }
}

// Increment a Timecode by one frame at fps. Drop-frame ignored (matches
// test_p_ltc_decoder convention; encoder above produces 25 fps non-drop).
void incTc(int& hh, int& mm, int& ss, int& ff, int fps) {
    if (++ff >= fps) { ff = 0;
        if (++ss >= 60) { ss = 0;
            if (++mm >= 60) { mm = 0;
                ++hh;
            }
        }
    }
}

// ---- Test 1 — 1000-frame timecode advance (40 s @ 25 fps) -----------------

void test_1000_frame_advance() {
    constexpr int kSr     = 48000;
    constexpr int kFps    = 25;
    constexpr int kFrames = 1000;
    constexpr int kBlock  = 64;

    // Synthesise the full audio stream.
    std::vector<float> audio;
    audio.reserve(static_cast<std::size_t>(kSr) * (kFrames / kFps));
    int hh = 0, mm = 0, ss = 0, ff = 0;
    int level = -1;
    for (int f = 0; f < kFrames; ++f) {
        auto bits = packLtcFrame(hh, mm, ss, ff);
        renderBiphaseFrame(bits, kSr, kFps, audio, level);
        incTc(hh, mm, ss, ff, kFps);
    }
    const int expected_total_samples = kSr * (kFrames / kFps);   // 1,920,000
    assert(static_cast<int>(audio.size()) == expected_total_samples);

    LtcChase chase;
    chase.prepare(static_cast<double>(kSr), kFps);
    spe::util::rt_alloc_violations_reset();

    // Block-cadence push + control-thread drain (interleaved, single-thread
    // simulation of the audio→control flow).
    const int total_blocks = static_cast<int>(audio.size()) / kBlock;
    {
        SPE_RT_NO_ALLOC_SCOPE();
        for (int b = 0; b < total_blocks; ++b) {
            const float* in = audio.data() + static_cast<std::size_t>(b) * kBlock;
            std::size_t pushed = chase.pushSamples(in, kBlock);
            assert(pushed == static_cast<std::size_t>(kBlock));
            chase.update();
        }
    }
    auto v = spe::util::rt_alloc_violations();
    assert(v == 0 && "LtcChase hot path must be allocation-free");

    // Final timecode after 1000 frames at 25 fps starting from 00:00:00:00 ==
    //   total seconds = 1000 / 25 = 40 → frame 999 corresponds to 00:00:39:24.
    Timecode tc{};
    bool valid = chase.getCurrentTimecode(tc);
    assert(valid && "decoder must have locked");

    const int expected_total_frames_minus_one = kFrames - 1;
    const int seen_total = tc.hours * 3600 * kFps
                         + tc.minutes * 60 * kFps
                         + tc.seconds * kFps
                         + tc.frames;
    const int drift = std::abs(seen_total - expected_total_frames_minus_one);
    std::printf("test_1000_frame_advance: blocks=%d, ring.drops=%llu, "
                "frames_decoded=%llu, last_tc=%02d:%02d:%02d:%02d, drift=%d, alloc_violations=%llu\n",
                total_blocks,
                static_cast<unsigned long long>(chase.ringDrops()),
                static_cast<unsigned long long>(chase.framesDecoded()),
                tc.hours, tc.minutes, tc.seconds, tc.frames,
                drift,
                static_cast<unsigned long long>(v));
    assert(drift <= 1 && "last timecode must be within ±1 frame of expected");
    assert(chase.ringDrops() == 0 && "no ring drops at this push/drain cadence");
    std::printf("  PASS\n");
}

// ---- Test 2 — 10-s xruns gate (NullBackend cadence) -----------------------

void test_10s_block64_no_xruns() {
    constexpr int kSr     = 48000;
    constexpr int kBlock  = 64;
    constexpr int kBlocks = (kSr * 10) / kBlock;   // 7500 blocks = 10 s

    // Loop a short LTC pattern through the ring; content does not matter
    // here — only producer/consumer cadence.
    std::vector<float> one_frame;
    one_frame.reserve(static_cast<std::size_t>(kSr / 25));
    int level = -1;
    auto bits = packLtcFrame(0, 0, 0, 0);
    renderBiphaseFrame(bits, kSr, 25, one_frame, level);

    LtcChase chase;
    chase.prepare(static_cast<double>(kSr), 25);
    spe::util::rt_alloc_violations_reset();

    std::size_t cursor = 0;
    {
        SPE_RT_NO_ALLOC_SCOPE();
        for (int b = 0; b < kBlocks; ++b) {
            std::array<float, kBlock> blk{};
            for (int n = 0; n < kBlock; ++n) {
                blk[static_cast<std::size_t>(n)] =
                    one_frame[(cursor + static_cast<std::size_t>(n)) % one_frame.size()];
            }
            cursor = (cursor + kBlock) % one_frame.size();
            std::size_t pushed = chase.pushSamples(blk.data(), kBlock);
            assert(pushed == static_cast<std::size_t>(kBlock));
            chase.update();
        }
    }
    auto v = spe::util::rt_alloc_violations();
    std::printf("test_10s_block64_no_xruns: blocks=%d, ring.drops=%llu, alloc_violations=%llu\n",
                kBlocks,
                static_cast<unsigned long long>(chase.ringDrops()),
                static_cast<unsigned long long>(v));
    assert(chase.ringDrops() == 0 && "ring xruns must be 0 over 10 s @ 48 kHz / block 64");
    assert(v == 0 && "allocation-free hot path");
    std::printf("  PASS\n");
}

// ---- Test 3 — pre-lock state -----------------------------------------------

void test_unlocked_returns_false() {
    LtcChase chase;
    chase.prepare(48000.0);
    Timecode tc;
    bool ok = chase.getCurrentTimecode(tc);
    assert(!ok && "no frames decoded yet → getCurrentTimecode returns false");
    assert(!chase.isLocked());
    std::printf("test_unlocked_returns_false PASS\n");
}

}  // namespace

int main() {
    test_unlocked_returns_false();
    test_10s_block64_no_xruns();
    test_1000_frame_advance();
    std::printf("All test_p_ltc_chase tests passed.\n");
    return 0;
}
