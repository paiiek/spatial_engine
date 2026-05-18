// vst3/tests/test_writebinaural_no_sofa_muted.cpp
//
// v0.5.1 Q3 — active-path no-SOFA fallback policy.
//
// Drives the SpatialEngineProcessor WITHOUT bypass and WITHOUT a loaded
// .speh. Asserts:
//   1. Bus 1 L/R are all-zero across rendered blocks (mute policy).
//   2. /sys/binaural_warning ,s "no_sofa_loaded" is observed EXACTLY ONCE
//      per prepareToPlay() cycle (Q1's latch is the emission source; we
//      verify the integration through the OSCBackend outbound ring).
//   3. The test runs ≥ 2 prepareToPlay() cycles (re-prep across two sample
//      rates: 48000 → 44100 → 48000) and asserts EXACTLY 2 warning emissions
//      and EXACTLY 2 /sys/state snapshots across the full run (MAJOR 3
//      coverage of the latch reset on re-prepare).
//   4. /sys/state ,s "fallback_mode=muted" is emitted alongside the warning
//      whenever the active-path no-SOFA fallback is engaged.
//
// The processor instantiates SpatialEngine(0) so the engine's OSCBackend
// never opens a UDP listener; sendReply() drops on missing peer endpoint.
// To exercise the latch path, the test seeds a synthetic peer endpoint via
// injectPacket(pkt, peer, len) before each prepareToPlay() and inspects the
// outbound ring directly via outboundPending() / outboundPeek().

#include "SpatialEngineProcessor.hpp"
#include "core/SpatialEngine.h"
#include "ipc/OSCBackend.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kBlock        = 64;
constexpr int kBlocksPerRun = 4;  // > 1 to verify warning fires ONCE per prep

// Build a /sys/handshake ,i <schema> OSC packet (legacy single-int form).
std::vector<uint8_t> buildHandshakePacket(int32_t schema) {
    std::vector<uint8_t> pkt;
    auto pushPadded = [&](const char* s) {
        std::size_t len = std::strlen(s) + 1;
        for (std::size_t i = 0; i < len; ++i) pkt.push_back(static_cast<uint8_t>(s[i]));
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    pushPadded("/sys/handshake");
    pushPadded(",i");
    const uint32_t u = static_cast<uint32_t>(schema);
    pkt.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
    pkt.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    pkt.push_back(static_cast<uint8_t>((u >>  8) & 0xFF));
    pkt.push_back(static_cast<uint8_t>((u      ) & 0xFF));
    return pkt;
}

// Seed last_peer_endpoint_ on the engine's OSCBackend so sendReply() can
// claim a ring slot. The synthetic peer is 127.0.0.1:50000 — never read by
// anything because no drain thread is running.
void seedPeer(spe::core::SpatialEngine& engine) {
    struct sockaddr_in peer{};
    peer.sin_family      = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer.sin_port        = htons(50000);
    const auto pkt = buildHandshakePacket(1);
    engine.oscBackend().injectPacket(
        std::span<const uint8_t>(pkt.data(), pkt.size()),
        reinterpret_cast<const sockaddr*>(&peer),
        sizeof(peer));
}

// Enable binaural via state v4 binaural_state section so the latch arms
// (Q1 condition: binauralEnabled() && !binauralHasHrtf()). The processor's
// engine has no .speh loaded, so binauralHasHrtf() returns false.
void enableBinauralViaState(spe::vst3::SpatialEngineProcessor& proc) {
    // Build a minimal v4 blob with engine_core (8 floats, all 0) + a
    // binaural section that enables binaural. Section layout per
    // SpatialEngineProcessor::getState/setState.
    std::vector<uint8_t> blob;
    blob.resize(8);
    int32 magic = 0x34455053;
    uint16 ver  = 4;
    uint16 sc   = 2;  // engine_core + binaural_state
    std::memcpy(blob.data() + 0, &magic, 4);
    std::memcpy(blob.data() + 4, &ver, 2);
    std::memcpy(blob.data() + 6, &sc, 2);

    // engine_core section (id=0x0001, 32 bytes payload).
    {
        const float vals[8] = {0,0,0,0,0,0,0,0};
        uint16 id  = 0x0001;
        uint32 len = 32;
        uint8_t hdr[6];
        std::memcpy(hdr + 0, &id, 2);
        std::memcpy(hdr + 2, &len, 4);
        blob.insert(blob.end(), hdr, hdr + 6);
        for (int i = 0; i < 8; ++i) {
            uint8_t fb[4];
            std::memcpy(fb, &vals[i], 4);
            blob.insert(blob.end(), fb, fb + 4);
        }
    }
    // binaural_state section (id=0x0004, v0.5 layout — 4 bytes):
    //   byte[0] = binaural_enable (1)
    //   byte[1] = effective_mode  (telemetry-only, reader ignores)
    //   byte[2] = requested_mode  (0 = Direct / B1)
    //   byte[3] = reserved padding
    {
        uint16 id  = 0x0004;
        uint32 len = 4;
        uint8_t hdr[6];
        std::memcpy(hdr + 0, &id, 2);
        std::memcpy(hdr + 2, &len, 4);
        blob.insert(blob.end(), hdr, hdr + 6);
        blob.push_back(1);  // enable
        blob.push_back(0);  // effective_mode (ignored on read)
        blob.push_back(0);  // requested_mode = Direct (B1)
        blob.push_back(0);  // reserved padding
    }

    MemoryStream* ms = new MemoryStream();
    int32 written = 0;
    ms->write(blob.data(), static_cast<int32>(blob.size()), &written);
    int64 res = 0;
    ms->seek(0, IBStream::kIBSeekSet, &res);
    proc.setState(ms);
    ms->release();
}

bool addrIs(const uint8_t* buf, std::size_t n, const char* addr) {
    const std::size_t L = std::strlen(addr);
    if (n < L) return false;
    return std::memcmp(buf, addr, L) == 0;
}

// Returns true if the OSC packet at `buf` (`n` bytes) starts with the
// expected address AND its string payload region contains `needle`.
bool packetContains(const uint8_t* buf, std::size_t n,
                    const char* addr, const char* needle) {
    if (!addrIs(buf, n, addr)) return false;
    // Cheap substring scan over the packet bytes — OSC string args are
    // null-terminated and 4-byte padded, so the literal substring will
    // appear contiguously.
    const std::size_t needle_len = std::strlen(needle);
    if (n < needle_len) return false;
    for (std::size_t i = 0; i + needle_len <= n; ++i) {
        if (std::memcmp(buf + i, needle, needle_len) == 0) return true;
    }
    return false;
}

// Render `blocks` blocks at the current setup, with bus 0 silent input.
// Returns the peak |sample| observed on bus 1 across all blocks.
float renderAndPeakBus1(spe::vst3::SpatialEngineProcessor& proc, int blocks) {
    float in_l[kBlock] = {0};
    float in_r[kBlock] = {0};
    float out0_l[kBlock]{}, out0_r[kBlock]{};
    float out1_l[kBlock]{}, out1_r[kBlock]{};

    float* in_ptrs[2]   = {in_l, in_r};
    float* out0_ptrs[2] = {out0_l, out0_r};
    float* out1_ptrs[2] = {out1_l, out1_r};

    AudioBusBuffers inBus{};   inBus.numChannels = 2; inBus.channelBuffers32 = in_ptrs;
    AudioBusBuffers out0Bus{}; out0Bus.numChannels = 2; out0Bus.channelBuffers32 = out0_ptrs;
    AudioBusBuffers out1Bus{}; out1Bus.numChannels = 2; out1Bus.channelBuffers32 = out1_ptrs;
    AudioBusBuffers outs[2] = {out0Bus, out1Bus};

    ProcessData data{};
    data.processMode        = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numInputs          = 1;
    data.numOutputs         = 2;
    data.inputs             = &inBus;
    data.outputs            = outs;
    data.numSamples         = kBlock;

    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        // Make sure inputs are zero on every block (host normally feeds audio).
        std::memset(in_l, 0, sizeof(in_l));
        std::memset(in_r, 0, sizeof(in_r));
        std::memset(out1_l, 0xFE, sizeof(out1_l));  // poison to detect "no-write"
        std::memset(out1_r, 0xFE, sizeof(out1_r));
        proc.process(data);
        for (int n = 0; n < kBlock; ++n) {
            const float aL = std::fabs(out1_l[n]);
            const float aR = std::fabs(out1_r[n]);
            if (aL > peak) peak = aL;
            if (aR > peak) peak = aR;
        }
    }
    return peak;
}

// Prepare the processor at a given sample rate and return the number of
// /sys/binaural_warning + /sys/state packets enqueued during the first
// rendered block. The latch is armed in prepareToPlay and drained on the
// first process() pass.
struct PrepResult {
    int  warning_count = 0;
    int  state_count   = 0;
    bool state_has_muted = false;
    float bus1_peak    = 0.f;
};

PrepResult runPrepareCycle(spe::vst3::SpatialEngineProcessor& proc,
                            double sample_rate, int blocks) {
    PrepResult r{};

    ProcessSetup setup{};
    setup.processMode        = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate         = sample_rate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Render blocks; the audio thread no longer emits (v0.6 #4) — the
    // heartbeat IO thread is the sole emitter.
    r.bus1_peak = renderAndPeakBus1(proc, blocks);

    // v0.6 #4: emission moved from audio thread to the heartbeat IO
    // thread. setActive(true) spawned the heartbeat; its first iteration
    // drains the no_sofa + state latches BEFORE the wait_for(1s). The
    // ring should fill within a few hundred microseconds of setActive().
    // Poll for up to 200 ms (matching the plan's per-emission latency
    // budget) before inspecting.
    auto& backend = proc.engine().oscBackend();
    const auto poll_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    std::size_t pending = 0;
    while (std::chrono::steady_clock::now() < poll_deadline) {
        pending = backend.outboundPending();
        // Expect at least 3 packets per cycle: /sys/binaural_status,
        // /sys/binaural_warning (no_sofa), /sys/state (fallback_mode).
        if (pending >= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (std::size_t i = 0; i < pending; ++i) {
        std::size_t n = 0;
        const uint8_t* buf = backend.outboundPeek(i, n);
        if (!buf || n == 0) continue;
        if (packetContains(buf, n, "/sys/binaural_warning", "no_sofa_loaded")) {
            ++r.warning_count;
        } else if (addrIs(buf, n, "/sys/state")) {
            ++r.state_count;
            if (packetContains(buf, n, "/sys/state", "fallback_mode=muted")) {
                r.state_has_muted = true;
            }
        }
    }
    // Drain so the next prepare cycle starts with an empty ring.
    backend.outboundDrainForTest(pending);

    proc.setActive(false);
    return r;
}

} // namespace

int main() {
    int pass = 0, fail = 0;
    auto CHECK = [&](bool cond, const char* name) {
        if (cond) { ++pass; std::printf("PASS %s\n", name); }
        else      { ++fail; std::fprintf(stderr, "FAIL %s\n", name); }
    };

    spe::vst3::SpatialEngineProcessor proc;
    proc.initialize(nullptr);

    // Negotiate 2ch speakers + stereo binaural.
    {
        SpeakerArrangement outs[2] = {SpeakerArr::kStereo, SpeakerArr::kStereo};
        SpeakerArrangement ins[1]  = {SpeakerArr::kStereo};
        proc.setBusArrangements(ins, 1, outs, 2);
    }

    // Enable binaural via state v4 (so binauralEnabled() returns true). No
    // SOFA path is set, so binauralHasHrtf() returns false → Q1's latch arms.
    enableBinauralViaState(proc);

    // Capture a synthetic peer endpoint BEFORE the first prepareToPlay so
    // OSCBackend::sendReply() can enqueue successfully.
    seedPeer(proc.engine());

    // ---- Prepare cycle 1 (48 kHz) ----
    const PrepResult r1 = runPrepareCycle(proc, 48000.0, kBlocksPerRun);
    CHECK(r1.bus1_peak == 0.f,    "cycle1_bus1_silent");
    CHECK(r1.warning_count == 1,  "cycle1_warning_emitted_once");
    CHECK(r1.state_count == 1,    "cycle1_state_emitted_once");
    CHECK(r1.state_has_muted,     "cycle1_state_fallback_muted");

    // Re-seed the peer in case the engine's last_peer_endpoint_ was reset
    // by setActive(false) — keeps subsequent sendReply() enqueues alive.
    seedPeer(proc.engine());

    // ---- Prepare cycle 2 (44.1 kHz — exercises the re-prepare path) ----
    const PrepResult r2 = runPrepareCycle(proc, 44100.0, kBlocksPerRun);
    CHECK(r2.bus1_peak == 0.f,    "cycle2_bus1_silent");
    CHECK(r2.warning_count == 1,  "cycle2_warning_emitted_once");
    CHECK(r2.state_count == 1,    "cycle2_state_emitted_once");
    CHECK(r2.state_has_muted,     "cycle2_state_fallback_muted");

    // MAJOR 3 — across the full run we expect exactly 2 emissions of
    // /sys/binaural_warning ,s "no_sofa_loaded" (one per prepare cycle).
    const int total_warnings = r1.warning_count + r2.warning_count;
    const int total_state    = r1.state_count   + r2.state_count;
    CHECK(total_warnings == 2,    "MAJOR3_exactly_two_warnings_across_two_prepares");
    CHECK(total_state    == 2,    "MAJOR3_exactly_two_state_snapshots_across_two_prepares");

    // ---- Optional 3rd cycle back at 48 kHz — confirms the latch keeps
    //      arming on every prepareToPlay regardless of rate. ----
    seedPeer(proc.engine());
    const PrepResult r3 = runPrepareCycle(proc, 48000.0, kBlocksPerRun);
    CHECK(r3.bus1_peak == 0.f,    "cycle3_bus1_silent");
    CHECK(r3.warning_count == 1,  "cycle3_warning_emitted_once_after_rate_revert");

    proc.terminate();

    std::printf("writebinaural_no_sofa_muted: %d pass, %d fail\n", pass, fail);
    return (fail == 0) ? 0 : 1;
}
