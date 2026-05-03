// test_p_ltc_decoder.cpp
// M7: synthesise 1 second of 25-fps LTC at 48 kHz and verify the LTCDecoder
// recovers every frame's HH:MM:SS:FF correctly.

#include "sync/LTCDecoder.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace spe::sync;

namespace {

// Pack the 80-bit LTC frame for the given timecode (transmission order).
// Sub-fields not modelled (user bits, BGF flags, parity) stay zero.
static std::array<int, 80> packLtcFrame(int hh, int mm, int ss, int ff) {
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

    // Sync word (LTC bits 64..79).
    static constexpr int kSync[16] = {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1};
    for (int i = 0; i < 16; ++i) bits[64 + i] = kSync[i];
    return bits;
}

// Render 80 bits as biphase-mark audio. `level` is the output state carried
// across calls so multi-frame sequences don't double-toggle at frame seams.
static void renderBiphaseFrame(const std::array<int, 80>& bits,
                                int sample_rate, int fps,
                                std::vector<float>& out, int& level)
{
    const int bit_full = sample_rate / (fps * 80);
    const int bit_half = bit_full / 2;
    for (int b = 0; b < 80; ++b) {
        level = -level; // bit boundary always toggles
        for (int n = 0; n < bit_half; ++n) out.push_back(static_cast<float>(level));
        if (bits[b]) level = -level; // mid-bit toggle for '1'
        for (int n = bit_half; n < bit_full; ++n) out.push_back(static_cast<float>(level));
    }
}

} // namespace

static void test_ltc_25fps_one_second() {
    constexpr int kSr  = 48000;
    constexpr int kFps = 25;

    // 25 frames of 00:00:00:FF (FF = 0..24).
    std::vector<float> audio;
    audio.reserve(static_cast<size_t>(kSr));
    int level = -1;
    for (int f = 0; f < kFps; ++f) {
        auto bits = packLtcFrame(0, 0, 0, f);
        renderBiphaseFrame(bits, kSr, kFps, audio, level);
    }
    if (static_cast<int>(audio.size()) != kSr) {
        std::printf("FAIL: synthesized %d samples, expected %d\n",
                    static_cast<int>(audio.size()), kSr);
        assert(false);
    }

    // Run through decoder.
    LTCDecoder dec;
    dec.reset(static_cast<float>(kSr) / static_cast<float>(kFps * 80)); // 24 samples
    std::vector<Timecode> decoded;
    decoded.reserve(static_cast<size_t>(kFps));
    Timecode tc;
    for (float s : audio) {
        if (dec.processSample(s, tc)) decoded.push_back(tc);
    }

    // Expect 25 unique frame decodes (FF = 0..24), HH=MM=SS=0.
    if (decoded.size() < 24) {
        std::printf("FAIL: got %zu decodes (expected ≥ 24)\n", decoded.size());
        for (size_t i = 0; i < decoded.size(); ++i) {
            std::printf("  [%zu] %02d:%02d:%02d:%02d\n", i,
                        decoded[i].hours, decoded[i].minutes,
                        decoded[i].seconds, decoded[i].frames);
        }
        assert(false);
    }
    int matched_ff[25] = {0};
    for (const auto& d : decoded) {
        if (d.hours != 0 || d.minutes != 0 || d.seconds != 0) {
            std::printf("FAIL: bad TC %02d:%02d:%02d:%02d\n",
                        d.hours, d.minutes, d.seconds, d.frames);
            assert(false);
        }
        if (d.frames < 0 || d.frames > 24) {
            std::printf("FAIL: out-of-range frames=%d\n", d.frames);
            assert(false);
        }
        matched_ff[d.frames]++;
    }
    int unique = 0;
    for (int i = 0; i < 25; ++i) if (matched_ff[i] > 0) ++unique;
    if (unique < 24) {
        std::printf("FAIL: only %d unique frame numbers decoded (expected ≥24)\n", unique);
        assert(false);
    }
    std::printf("PASS test_ltc_25fps_one_second (%zu decodes, %d unique frames)\n",
                decoded.size(), unique);
}

static void test_ltc_known_timecode() {
    constexpr int kSr  = 48000;
    constexpr int kFps = 25;

    // Build a single frame at 12:34:56:18 and verify exact decode.
    std::vector<float> audio;
    int level = -1;
    auto bits = packLtcFrame(12, 34, 56, 18);
    // The decoder requires 80 bits to be in the ring before a sync match —
    // emit two consecutive frames so the second one decodes cleanly.
    renderBiphaseFrame(bits, kSr, kFps, audio, level);
    renderBiphaseFrame(bits, kSr, kFps, audio, level);

    LTCDecoder dec;
    dec.reset(static_cast<float>(kSr) / static_cast<float>(kFps * 80));
    Timecode tc;
    bool got = false;
    for (float s : audio) {
        if (dec.processSample(s, tc)) {
            if (tc.hours == 12 && tc.minutes == 34 && tc.seconds == 56 && tc.frames == 18) {
                got = true;
                break;
            }
        }
    }
    if (!got) {
        std::printf("FAIL test_ltc_known_timecode: 12:34:56:18 not decoded\n");
        assert(false);
    }
    std::printf("PASS test_ltc_known_timecode (12:34:56:18)\n");
}

int main() {
    test_ltc_25fps_one_second();
    test_ltc_known_timecode();
    std::printf("All ltc_decoder tests passed.\n");
    return 0;
}
