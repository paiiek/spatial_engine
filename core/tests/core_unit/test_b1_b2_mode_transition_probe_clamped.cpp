// test_b1_b2_mode_transition_probe_clamped.cpp
//
// v0.5.1 Q2 (A3 MAJOR 1-iter3) — when the throughput probe has surfaced
// the CPU-fallback warning, BinauralMonitor MUST arm the next B1↔B2 ramp
// at 1 block (not the default 2) AND set the truncation-pending atomic
// exactly ONCE per ramp event.
//
// The matching /sys/binaural_warning ,s "xfade_truncated_cpu" emission
// is owned by the 1-Hz IO heartbeat in vst3/SpatialEngineProcessor.cpp;
// what we exercise here is the BinauralMonitor contract:
//
//   * After injectProbeThroughputForTest(<min) → probe_warning_set_ = true.
//   * Setting requested_mode != effective_mode triggers a flip on the next
//     observeAndArmXfade(). For this test we engineer the flip by toggling
//     effective_mode directly through the public path (setRequestedMode +
//     injectProbeThroughputForTest sequencing).
//   * observeAndArmXfade() must report total_blocks == 1.
//   * xfadeActive() must report true for exactly 1 block.
//   * drainXfadeTruncatedPending() must return true exactly ONCE (then
//     false on subsequent calls, until the next probe-clamped flip).
//
// We also exercise the second-tier emission path via the engine wrapper:
//   engine.injectProbeThroughputAndEmit(0.5f) sets the warning, and a
//   subsequent setRequestedMode flip arms the truncated ramp.

#include "core/SpatialEngine.h"
#include "geometry/SpeakerLayout.h"
#include "output_backend/BinauralMonitor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

#define REQUIRE(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s (line %d)\n", #cond, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr int   kBlock      = 128;
constexpr float kSampleRate = 48000.f;

int monitorOnly()
{
    spe::output::BinauralMonitor mon;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    cfg.sampleRate = kSampleRate;
    cfg.blockSize  = kBlock;
    REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    REQUIRE(mon.hasHrtf());
    REQUIRE(mon.hasB2());

    // Start in Direct, with no probe warning. Drain any stray pending flag.
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);
    (void)mon.drainXfadeTruncatedPending();
    REQUIRE(!mon.xfadeActive());

    // Inject a fast-CPU probe (no warning) and arm an UN-truncated ramp
    // first, to establish a baseline. Setting AmbiVS then puts effective
    // at AmbiVS; observeAndArmXfade then sees prev=Direct, eff=AmbiVS.
    mon.setRequestedMode(spe::output::BinauralMode::AmbiVS);
    mon.injectProbeThroughputForTest(2.0f); // > 1.5x → no warning.
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);
    REQUIRE(std::strcmp(mon.probeWarningCode(), "") == 0);

    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(step.active);
        REQUIRE(step.total_blocks == 2);  // default — no probe warning.
        REQUIRE(!mon.drainXfadeTruncatedPending()); // no truncation pending.
        mon.finalizeXfadeBlock();
    }
    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(step.active);
        REQUIRE(step.block_index == 1);
        mon.finalizeXfadeBlock();
    }
    REQUIRE(!mon.xfadeActive());

    // Now inject the slow-CPU probe. This sets probe_warning_set_ = true
    // AND clamps effective_mode_ to Direct, so the very next
    // observeAndArmXfade() sees prev=AmbiVS, eff=Direct → arm.
    mon.injectProbeThroughputForTest(0.5f);
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);
    REQUIRE(std::strcmp(mon.probeWarningCode(), "ambivs_disabled_cpu") == 0);

    // First arm under probe warning: total_blocks MUST be 1.
    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(step.active);
        REQUIRE(step.total_blocks == 1);
        REQUIRE(step.block_index == 0);
        REQUIRE(step.outgoing == spe::output::BinauralMode::AmbiVS);
        REQUIRE(step.incoming == spe::output::BinauralMode::Direct);
        // Truncation pending flag must be TRUE exactly once on this arm.
        REQUIRE(mon.drainXfadeTruncatedPending());
        // Second drain returns false (no flood).
        REQUIRE(!mon.drainXfadeTruncatedPending());
        mon.finalizeXfadeBlock();
    }
    REQUIRE(!mon.xfadeActive());

    // Calling observeAndArmXfade() in steady state must NOT re-arm the
    // truncation flag (it's edge-triggered on a NEW mode-flip event).
    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(!step.active);
        REQUIRE(!mon.drainXfadeTruncatedPending());
        mon.finalizeXfadeBlock();
    }

    // Re-arm by flipping again (Direct → AmbiVS once probe clears).
    mon.injectProbeThroughputForTest(2.0f);
    REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);
    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(step.active);
        REQUIRE(step.total_blocks == 2);  // back to default — no warning.
        // No truncation pending because total_blocks == 2.
        REQUIRE(!mon.drainXfadeTruncatedPending());
        mon.finalizeXfadeBlock();
    }
    {
        auto step = mon.observeAndArmXfade();
        REQUIRE(step.active);
        mon.finalizeXfadeBlock();
    }
    REQUIRE(!mon.xfadeActive());

    std::puts("PASS monitorOnly: probe-clamped ramp = 1 block + 1 truncation event");
    return 0;
}

// Engine-level path: verify that SpatialEngine's injectProbeThroughputAndEmit
// path interacts correctly with the xfade arming when a subsequent OSC mode
// flip arrives. We can't easily wire the heartbeat thread here (it lives in
// the VST3 layer), so we drain the truncation flag through the engine-level
// forwarder binauralDrainXfadeTruncatedPending().
// Build an in-memory 4-channel circular layout so the engine doesn't depend
// on a configs/ YAML being reachable from the test's CWD (ctest may run
// from a different directory than direct invocation, and the engine's
// fallback layout loader probes relative paths only).
spe::geometry::SpeakerLayout makeTestLayout() {
    spe::geometry::SpeakerLayout l;
    l.name = "xfade_test_4ch";
    l.regularity = spe::geometry::Regularity::CIRCULAR;
    const float azs[] = {0.f, 90.f, 180.f, 270.f};
    for (int i = 0; i < 4; ++i) {
        const float az = azs[i] * static_cast<float>(M_PI) / 180.f;
        spe::geometry::Speaker s;
        s.channel = i + 1;
        s.x = std::sin(az);
        s.y = 0.f;
        s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}

int engineLevel()
{
    spe::core::SpatialEngine engine(/*listen_port=*/0);
    engine.setLayout(makeTestLayout());
    // prepareToPlay sized for the test block, with the fixture .speh path.
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSampleRate, kBlock);

    // After prepareToPlay the engine's BinauralMonitor is in Direct mode
    // (default). Push a probe warning + AmbiVS request via the engine's
    // public hook.
    engine.injectProbeThroughputAndEmit(0.5f); // < 1.5 → fallback armed.
    // Engine's BinauralMonitor saw setRequestedMode(AmbiVS) + injected
    // throughput. effectiveBinauralMode should clamp to Direct.
    REQUIRE(engine.effectiveBinauralMode() ==
            static_cast<int>(spe::output::BinauralMode::Direct));
    REQUIRE(std::strcmp(engine.binauralProbeWarningCode(),
                        "ambivs_disabled_cpu") == 0);

    // The mode is still Direct (clamped). To trigger the xfade ramp we
    // need a real mode flip. Now clear the probe warning and request
    // AmbiVS again — the next audioBlock will see prev=Direct, eff=AmbiVS
    // and arm a 2-block ramp.
    engine.injectProbeThroughputAndEmit(2.0f); // > 1.5 → warning clears.
    REQUIRE(engine.effectiveBinauralMode() ==
            static_cast<int>(spe::output::BinauralMode::AmbiVS));

    // Drive ONE audioBlock to consume the ramp arming.
    std::vector<float> in_chan(kBlock, 0.f);
    // Provide a non-trivial input so the inner loops actually run.
    for (int n = 0; n < kBlock; ++n)
        in_chan[static_cast<std::size_t>(n)] =
            0.1f * std::sin(2.f * static_cast<float>(M_PI) * 220.f *
                            static_cast<float>(n) / kSampleRate);
    const float* in_ptrs[1] = { in_chan.data() };

    // 4-ch output matching the layout, with 2 trailing binaural channels.
    // We only care about RT-safety + xfade state machine.
    constexpr int kOutCh = 4;
    std::vector<std::vector<float>> outs(kOutCh, std::vector<float>(kBlock, 0.f));
    std::vector<float*> out_ptrs(kOutCh);
    for (int c = 0; c < kOutCh; ++c)
        out_ptrs[static_cast<std::size_t>(c)] = outs[static_cast<std::size_t>(c)].data();

    spe::audio_io::AudioBlock block{};
    block.input_channels       = in_ptrs;
    block.input_channel_count  = 1;
    block.output_channels      = out_ptrs.data();
    block.output_channel_count = kOutCh;
    block.num_frames           = kBlock;
    block.hw_timestamp_ns      = 0;

    engine.audioBlock(block);
    // After 1 block of a 2-block ramp, the xfade should STILL be active
    // (1 block remaining). truncation flag must be FALSE (probe is clear).
    REQUIRE(engine.binauralXfadeActive());
    REQUIRE(!engine.binauralDrainXfadeTruncatedPending());

    engine.audioBlock(block);
    REQUIRE(!engine.binauralXfadeActive());
    REQUIRE(!engine.binauralDrainXfadeTruncatedPending());

    // Now arm a PROBE-CLAMPED flip: reinject the warning and flip the
    // mode. The engine should arm a 1-block ramp and set the truncation
    // flag exactly once.
    engine.injectProbeThroughputAndEmit(0.5f); // warning set + clamp to Direct
    REQUIRE(engine.effectiveBinauralMode() ==
            static_cast<int>(spe::output::BinauralMode::Direct));

    engine.audioBlock(block);
    // 1-block ramp = single block. After ONE audioBlock the ramp is done.
    REQUIRE(!engine.binauralXfadeActive());
    // Truncation flag should be set exactly once. Drain it.
    REQUIRE(engine.binauralDrainXfadeTruncatedPending());
    REQUIRE(!engine.binauralDrainXfadeTruncatedPending());

    engine.releaseResources();
    std::puts("PASS engineLevel: probe-clamped truncation flag = 1 emission per arm");
    return 0;
}

} // namespace

int main()
{
    if (monitorOnly() != 0) return 1;
    if (engineLevel() != 0) return 1;
    std::puts("PASS test_b1_b2_mode_transition_probe_clamped");
    return 0;
}
