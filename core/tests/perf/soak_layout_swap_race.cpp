// soak_layout_swap_race.cpp  (Phase 4.3 Inc 4 — live layout-swap TSan gate)
//
// Stresses the runtime speaker-layout swap quiescence handshake. One "audio"
// thread spins SpatialEngine::audioBlock() continuously (draining a FIFO of
// noise/output commands that read layout_/noise_chans_/spk_limiters_ UNGATED,
// plus rendering an active object) while a "control" thread repeatedly applies a
// new layout via /layout/slot/load → applyPendingLayoutSlotOp → applyLayoutLive,
// alternating between an 8ch and a 12ch ring so reprepareForLayout() reallocates
// noise_chans_ / spk_* / mix_buf_ / the renderer set in place.
//
// Invariant under test: applyLayoutLive arms layout_swap_pending_ (release),
// spin-waits for the audio callback's layout_swap_quiesced_ ack (the callback
// early-returns silence BEFORE the OSC drain), THEN mutates layout_ +
// reprepareForLayout() off the audio thread, THEN releases. So the audio thread
// NEVER reads a half-moved layout_ / reallocated noise_chans_ / spk_limiters_,
// and there is ZERO data race on them.
//
// Run under ThreadSanitizer (build with -fsanitize=thread; on kernels with the
// ASLR/TSan mmap conflict use `setarch -R`). Also a functional check in a normal
// build: every output sample is finite, and after a settle the engine renders
// non-silent audio (the rebuild published correctly).

#include "core/SpatialEngine.h"
#include "geometry/LayoutLibrary.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace spe::geometry;

// n-speaker ring on the z=0 plane, ~2 m radius.
static SpeakerLayout make_ring(int n, const char* name) {
    SpeakerLayout l; l.name = name; l.regularity = Regularity::CIRCULAR;
    l.channel_to_idx_.fill(static_cast<int16_t>(-1));
    const float R = 2.0f;
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i + 1;
        const float a = 2.f * 3.14159265f * static_cast<float>(i) / static_cast<float>(n);
        s.x = R * std::cos(a); s.y = 0.f; s.z = R * std::sin(a);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(l.speakers.size());
        l.speakers.push_back(s);
    }
    return l;
}

static void dispatchNoise(spe::core::SpatialEngine& e, int ch) {
    // /noise/{ch}/gain — drained on the audio thread, reads layout_.channelToIndex
    // + noise_chans_[idx] (one of the UNGATED layout-dependent reads).
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::NoiseGain;
    spe::ipc::PayloadNoiseGain p; p.channel = static_cast<uint32_t>(ch); p.gain_db = -6.f;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchOutputLimit(spe::core::SpatialEngine& e, int ch) {
    // /output/{ch}/limit — drained on the audio thread, reads layout_.channelToIndex
    // + spk_limiters_[idx] (another UNGATED layout-dependent read).
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::OutputLimit;
    spe::ipc::PayloadOutputLimit p; p.channel = static_cast<uint32_t>(ch); p.threshold_db = -3.f;
    c.payload = p; e.dispatchCommand(c);
}
static void moveObj(spe::core::SpatialEngine& e, uint32_t id, float az) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = 0.f; p.dist_m = 2.f;
    c.payload = p; e.dispatchCommand(c);
}
static void setActive(spe::core::SpatialEngine& e, uint32_t id, bool on) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjActive;
    spe::ipc::PayloadObjActive p; p.obj_id = id; p.active = on;
    c.payload = p; e.dispatchCommand(c);
}
static void requestLoad(spe::core::SpatialEngine& e, int slot) {
    // LayoutSlot::Load → control-thread early-return stores pending_layout_op_;
    // applyPendingLayoutSlotOp() (called next, same thread) consumes it.
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::LayoutSlot;
    spe::ipc::PayloadLayoutSlot p; p.op = spe::ipc::PayloadLayoutSlot::Op::Load; p.slot = slot;
    c.payload = p; e.dispatchCommand(c);
}

int main() {
    const double SR = 48000.0;
    const int    BLK = 64;
    const int    OUT_CH = 16;   // fixed physical bus (≥ both layouts' speaker counts)
    const int    ROUNDS = 200;

    const std::string dir = "/tmp/spe_layout_swap_race_" + std::to_string(::getpid());
    std::error_code ec; fs::remove_all(dir, ec);
    {
        LayoutLibrary lib(dir);
        lib.save(0, make_ring(8,  "ring8"),  "ring8");
        lib.save(1, make_ring(12, "ring12"), "ring12");
    }

    spe::core::SpatialEngine engine(0);
    engine.setLayoutLibraryDir(dir);
    engine.setLayout(make_ring(8, "ring8"));
    engine.prepareToPlay(SR, BLK);          // prepared_=true → handshake path
    setActive(engine, 0, true);
    moveObj(engine, 0, 0.4f);               // one active VBAP object → non-silent render

    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0};
    std::atomic<int>  nonfinite{0};
    std::atomic<double> last_energy{0.0};

    std::thread audio([&] {
        std::vector<std::vector<float>> bufs(static_cast<size_t>(OUT_CH),
                                             std::vector<float>(static_cast<size_t>(BLK), 0.f));
        std::vector<float*> ptrs(static_cast<size_t>(OUT_CH));
        for (int c = 0; c < OUT_CH; ++c) ptrs[static_cast<size_t>(c)] = bufs[static_cast<size_t>(c)].data();
        while (!stop.load(std::memory_order_relaxed)) {
            spe::audio_io::AudioBlock blk;
            blk.output_channels = ptrs.data();
            blk.output_channel_count = OUT_CH;
            blk.num_frames = BLK;
            blk.sample_rate = SR;
            engine.audioBlock(blk);
            double energy = 0.0;
            for (int c = 0; c < OUT_CH; ++c)
                for (int n = 0; n < BLK; ++n) {
                    const float v = bufs[static_cast<size_t>(c)][static_cast<size_t>(n)];
                    if (!std::isfinite(v)) nonfinite.fetch_add(1, std::memory_order_relaxed);
                    energy += static_cast<double>(v) * v;
                }
            last_energy.store(energy, std::memory_order_relaxed);
            blocks.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Let the audio thread spin first.
    while (blocks.load(std::memory_order_relaxed) < 4) std::this_thread::yield();

    int applied = 0;
    for (int round = 0; round < ROUNDS; ++round) {
        // Fill the FIFO with UNGATED-read commands so the audio drain races the swap.
        for (int ch = 1; ch <= 8; ++ch) { dispatchNoise(engine, ch); dispatchOutputLimit(engine, ch); }
        requestLoad(engine, round % 2);       // alternate 8ch ↔ 12ch
        engine.applyPendingLayoutSlotOp();     // → applyLayoutLive (spins for ack)
        ++applied;
    }

    // Settle: stop swapping, let the audio thread render the final (12ch or 8ch)
    // layout for a while, then confirm it is producing non-silent finite audio.
    long settle_target = blocks.load(std::memory_order_relaxed) + 200;
    while (blocks.load(std::memory_order_relaxed) < settle_target) std::this_thread::yield();
    const double settled_energy = last_energy.load(std::memory_order_relaxed);

    stop.store(true, std::memory_order_relaxed);
    audio.join();

    fs::remove_all(dir, ec);

    int failures = 0;
    if (nonfinite.load() != 0) {
        std::printf("FAIL: %d non-finite output samples during swap soak\n", nonfinite.load());
        ++failures;
    }
    if (applied != ROUNDS) {
        std::printf("FAIL: only %d/%d swaps applied\n", applied, ROUNDS);
        ++failures;
    }
    if (!(settled_energy > 0.0)) {
        std::printf("FAIL: engine silent after settle (energy=%.6f) — rebuild not published\n",
                    settled_energy);
        ++failures;
    }

    if (failures == 0)
        std::printf("soak_layout_swap_race: ALL PASS (%d swaps, %ld blocks, settled_energy=%.4f)\n",
                    applied, blocks.load(), settled_energy);
    return failures;
}
