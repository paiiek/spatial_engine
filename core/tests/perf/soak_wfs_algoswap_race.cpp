// soak_wfs_algoswap_race.cpp  (Lane F5-M3b — Option C TSan gate)
//
// Stresses the allocate-then-publish handshake for lazy WFS allocation. One
// "audio" thread spins WFSRenderer::processBlock() continuously while a
// "control" thread calls ensureAllocated() mid-flight — the real path when an
// object's algorithm first becomes WFS while audio is rendering.
//
// Invariant under test: processBlock acquire-loads ready_ and renders silent
// until it flips; ensureAllocated resizes delays_/ramps_ THEN release-stores
// ready_. So the audio thread NEVER reads a half-built delays_/ramps_, and there
// is ZERO data race on ready_/delays_/ramps_.
//
// Run under ThreadSanitizer (build with -fsanitize=thread; on kernels with the
// ASLR/TSan mmap conflict use `setarch -R`). Also a functional check in a normal
// build: after the flip publishes, WFS must render non-silent audio.

#include "render/WFSRenderer.h"
#include "geometry/SpeakerLayout.h"
#include "core/Constants.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <span>
#include <thread>
#include <vector>
#include <array>

using namespace spe::render;
using namespace spe::geometry;

// 8-speaker ring on the z=0 plane, ~2 m radius.
static SpeakerLayout make_ring(int n) {
    SpeakerLayout l;
    l.name = "wfs_ring";
    l.regularity = Regularity::CIRCULAR;
    const float R = 2.0f;
    for (int i = 0; i < n; ++i) {
        Speaker s;
        s.channel = i + 1;
        const float a = 2.f * 3.14159265f * static_cast<float>(i) / static_cast<float>(n);
        s.x = R * std::cos(a);
        s.y = 0.f;
        s.z = R * std::sin(a);
        l.speakers.push_back(s);
    }
    return l;
}

int main() {
    const double SR    = 48000.0;
    const int    N_SPK = 8;
    const int    BLK   = 64;
    const int    ROUNDS = 150;

    auto layout = make_ring(N_SPK);
    int failures = 0;

    for (int round = 0; round < ROUNDS; ++round) {
        WFSRenderer wfs;
        wfs.prepareToPlay(layout, SR);   // ready_ = false, delays_ cleared (lazy)

        if (wfs.isReady()) {
            std::printf("FAIL round %d: ready before ensureAllocated\n", round);
            ++failures;
        }

        // Shared, read-only during the race.
        std::array<ObjectState, spe::MAX_OBJECTS> objects{};
        objects[0].az_rad = 0.3f;
        objects[0].el_rad = 0.0f;
        objects[0].dist_m = 2.0f;
        objects[0].active = true;
        std::vector<float> dry(BLK, 0.f);
        for (int n = 0; n < BLK; ++n) dry[n] = std::sin(0.1f * static_cast<float>(n));
        std::array<const float*, spe::MAX_OBJECTS> dry_ptrs{};
        dry_ptrs[0] = dry.data();

        std::atomic<bool> stop{false};
        std::atomic<long> blocks{0};

        std::thread audio([&] {
            std::vector<float> out(static_cast<size_t>(BLK * N_SPK), 0.f);
            while (!stop.load(std::memory_order_relaxed)) {
                wfs.processBlock(
                    std::span<const ObjectState>(objects.data(), spe::MAX_OBJECTS),
                    std::span<const float* const>(dry_ptrs.data(), spe::MAX_OBJECTS),
                    out.data(), BLK);
                blocks.fetch_add(1, std::memory_order_relaxed);
            }
        });

        // Let the audio thread spin with ready_=false (silent early-return path),
        // then flip mid-flight. Second call stresses idempotency.
        while (blocks.load(std::memory_order_relaxed) < 4) std::this_thread::yield();
        wfs.ensureAllocated();
        wfs.ensureAllocated();

        // Let it render the post-activation (ready_=true) path.
        long target = blocks.load(std::memory_order_relaxed) + 40;
        while (blocks.load(std::memory_order_relaxed) < target) std::this_thread::yield();
        stop.store(true, std::memory_order_relaxed);
        audio.join();

        if (!wfs.isReady()) {
            std::printf("FAIL round %d: not ready after ensureAllocated\n", round);
            ++failures;
        }

        // Post-activation correctness: a fresh block must produce non-zero energy.
        std::vector<float> out(static_cast<size_t>(BLK * N_SPK), 0.f);
        wfs.processBlock(
            std::span<const ObjectState>(objects.data(), spe::MAX_OBJECTS),
            std::span<const float* const>(dry_ptrs.data(), spe::MAX_OBJECTS),
            out.data(), BLK);
        float energy = 0.f;
        for (float v : out) energy += v * v;
        if (!(energy > 0.f)) {
            std::printf("FAIL round %d: WFS silent after activation (energy=%.6f)\n",
                        round, static_cast<double>(energy));
            ++failures;
        }
    }

    if (failures == 0)
        std::printf("soak_wfs_algoswap_race: ALL PASS (%d rounds)\n", ROUNDS);
    return failures;
}
