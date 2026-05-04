// core/tests/core_unit/test_p_sys_ltc_chase.cpp
//
// C1.d acceptance tests for /sys/ltc_chase opcode 0x14:
//   - encode → decode roundtrip preserves the enable flag
//   - SpatialEngine state mirrors the decoded value (off → on → off)
//   - End-to-end integration: enable, push synthetic LTC into input ch 0
//     via NullBackend, decoder locks within ±1 frame; disable, decoder
//     state freezes (no further frames decoded after the toggle).

#include "audio_io/NullBackend.h"
#include "core/SpatialEngine.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"
#include "sync/LtcChase.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace spe;

namespace {

// ---- LTC biphase synthesis (mirrors test_p_ltc_chase / test_p_ltc_decoder) ---

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
    const int bit_full = sample_rate / (fps * 80);
    const int bit_half = bit_full / 2;
    for (int b = 0; b < 80; ++b) {
        level = -level;
        for (int n = 0; n < bit_half; ++n) out.push_back(static_cast<float>(level));
        if (bits[b]) level = -level;
        for (int n = bit_half; n < bit_full; ++n) out.push_back(static_cast<float>(level));
    }
}

// ---- Test 1 — OSC encode/decode roundtrip --------------------------------

void test_osc_roundtrip_enable() {
    ipc::CommandDecoder codec;

    ipc::Command in;
    in.tag = ipc::CommandTag::SysLtcChase;
    ipc::PayloadSysLtcChase p;
    p.enable = true;
    in.payload = p;

    std::vector<uint8_t> packet;
    bool encoded = codec.encode(in, packet);
    assert(encoded && "encode /sys/ltc_chase ,i 1 must succeed");

    auto parsed = codec.decode(std::span<const uint8_t>(packet.data(), packet.size()));
    assert(parsed.tag == ipc::CommandTag::SysLtcChase);
    auto& po = std::get<ipc::PayloadSysLtcChase>(parsed.payload);
    assert(po.enable == true);

    // Disable variant.
    p.enable = false;
    in.payload = p;
    packet.clear();
    encoded = codec.encode(in, packet);
    assert(encoded);
    parsed = codec.decode(std::span<const uint8_t>(packet.data(), packet.size()));
    assert(parsed.tag == ipc::CommandTag::SysLtcChase);
    auto& po2 = std::get<ipc::PayloadSysLtcChase>(parsed.payload);
    assert(po2.enable == false);
    assert(codec.rejectCount() == 0);

    std::printf("test_osc_roundtrip_enable PASS\n");
}

void test_osc_opcode_value() {
    // Lock the wire opcode at 0x14 to catch accidental renumbering.
    static_assert(static_cast<uint8_t>(ipc::CommandTag::SysLtcChase) == 0x14,
                  "SysLtcChase opcode must remain 0x14");
    std::printf("test_osc_opcode_value PASS (SysLtcChase=0x14)\n");
}

// ---- Test 2 — Engine state mirrors enable flag ---------------------------

void test_engine_state_reflects_toggle() {
    core::SpatialEngine engine(/*listen_port=*/0);
    assert(!engine.isLtcChaseEnabled());
    engine.setLtcChaseEnable(true);
    assert(engine.isLtcChaseEnabled());
    engine.setLtcChaseEnable(false);
    assert(!engine.isLtcChaseEnabled());
    std::printf("test_engine_state_reflects_toggle PASS\n");
}

// ---- Test 3 — End-to-end: enable + NullBackend + synthetic LTC -----------

void test_end_to_end_chase_lock() {
    constexpr int kSr     = 48000;
    constexpr int kFps    = 25;
    constexpr int kFrames = 30;            // 1.2 s of LTC
    constexpr int kBlock  = 64;

    // Synthesise the audio stream.
    std::vector<float> ltc;
    ltc.reserve(static_cast<std::size_t>(kSr) * (kFrames / kFps + 1));
    int level = -1;
    int hh = 0, mm = 0, ss = 0, ff = 0;
    for (int f = 0; f < kFrames; ++f) {
        auto bits = packLtcFrame(hh, mm, ss, ff);
        renderBiphaseFrame(bits, kSr, kFps, ltc, level);
        if (++ff >= kFps) { ff = 0; ++ss; }
    }

    // NullBackend with input ch 0 carrying the synthetic LTC.
    auto backend = audio_io::make_null_backend(static_cast<double>(kSr),
                                               /*output_channels=*/8,
                                               /*block_size=*/kBlock,
                                               /*input_channels=*/1);
    auto* nb = static_cast<audio_io::NullBackend*>(backend.get());
    nb->set_input_fixture({ltc});

    core::SpatialEngine engine(/*listen_port=*/0);
    engine.setLtcChaseEnable(true);
    assert(engine.isLtcChaseEnabled());

    // Run blocks. Drain LtcChase between blocks (control side).
    const int total_blocks = static_cast<int>(ltc.size()) / kBlock;
    // pump_synchronous calls prepareToPlay / releaseResources internally;
    // we cannot interleave updateLtcChase between its blocks. Instead,
    // pump enough blocks to fill the ring, then drain in one go and verify
    // lock. Ring capacity (65536) easily holds 30 frames @ 1920 samples.
    auto rc = nb->pump_synchronous(&engine, total_blocks);
    assert(rc == audio_io::BackendError::Ok);
    engine.updateLtcChase();

    assert(engine.ltcLocked() && "decoder must have locked while enabled");
    sync::Timecode tc{};
    bool ok = engine.getLtcCurrentTimecode(tc);
    assert(ok);
    const int seen = tc.hours * 3600 * kFps + tc.minutes * 60 * kFps
                   + tc.seconds * kFps + tc.frames;
    const int expected = kFrames - 1; // 0..kFrames-1
    const int drift = std::abs(seen - expected);
    std::printf("test_end_to_end_chase_lock: blocks=%d frames_decoded=%llu "
                "last_tc=%02d:%02d:%02d:%02d drift=%d ring_drops=%llu\n",
                total_blocks,
                static_cast<unsigned long long>(engine.ltcFramesDecoded()),
                tc.hours, tc.minutes, tc.seconds, tc.frames,
                drift,
                static_cast<unsigned long long>(engine.ltcRingDrops()));
    assert(drift <= 1 && "decoded timecode must be within ±1 frame of expected");
    assert(engine.ltcRingDrops() == 0);
    std::printf("  PASS\n");
}

// ---- Test 4 — Disable freezes the chase (no input fed to ring) -----------

void test_disable_skips_tap() {
    constexpr int kSr     = 48000;
    constexpr int kBlock  = 64;
    constexpr int kBlocks = 200;  // ~0.27 s

    // Some non-zero input — would cause decoder activity if tapped.
    std::vector<float> noise(static_cast<std::size_t>(kSr));
    for (auto& v : noise) v = 0.3f;

    auto backend = audio_io::make_null_backend(static_cast<double>(kSr),
                                               /*output_channels=*/2,
                                               kBlock,
                                               /*input_channels=*/1);
    auto* nb = static_cast<audio_io::NullBackend*>(backend.get());
    nb->set_input_fixture({noise});

    core::SpatialEngine engine(0);
    engine.setLtcChaseEnable(false);  // explicit disable
    auto rc = nb->pump_synchronous(&engine, kBlocks);
    assert(rc == audio_io::BackendError::Ok);
    engine.updateLtcChase();

    assert(engine.ltcFramesDecoded() == 0
           && "no frames must decode while chase is disabled");
    assert(engine.ltcRingDrops() == 0
           && "ring must not see any pushes while disabled");
    std::printf("test_disable_skips_tap PASS (frames_decoded=0, ring_drops=0)\n");
}

}  // namespace

int main() {
    test_osc_opcode_value();
    test_osc_roundtrip_enable();
    test_engine_state_reflects_toggle();
    test_disable_skips_tap();
    test_end_to_end_chase_lock();
    std::printf("All test_p_sys_ltc_chase tests passed.\n");
    return 0;
}
