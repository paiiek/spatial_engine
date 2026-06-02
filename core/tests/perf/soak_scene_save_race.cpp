// soak_scene_save_race.cpp  (v0.9 Lane F4-b — AC9 TSan + no-tearing gate)
//
// Proves the production concurrent safety of SpatialEngine::snapshotObjects()
// against the audio-thread obj_cache_ writer + post-drain three-buffer publish.
//
// One "audio" thread drives objects through the REAL production path
// (dispatchCommand → cmd_fifo_ → audioBlock drain → obj_cache_ → post-drain
// publish) while a "control" thread repeatedly calls snapshotObjects() mid-
// flight. >=150 rounds.
//
// Two invariants, checked together:
//   (1) TSan reports ZERO data races (build with -fsanitize=thread). The reader
//       NEVER touches the live obj_cache_; it reads only the published snapshot
//       via the retry-until-stable seqlock loop.
//   (2) No cross-buffer tearing — the writer maintains TWO deterministic
//       correlated field-pairs the reader recomputes in every snapshot:
//         pair A:  width_rad == az_rad * kEncA
//         pair B:  gain_lin  == dist_m * kEncB
//       A tear (one field from publish N, the other from M!=N) breaks an
//       equality and fails the test. Two independent pairs catch tears that
//       happen to preserve the first.
//
// Run under ThreadSanitizer (build with -fsanitize=thread; on kernels with the
// ASLR/TSan mmap conflict use `setarch -R`).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "ipc/Command.h"
#include "ipc/SceneSnapshot.h"
#include "audio_io/AudioCallback.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace spe;

static constexpr float kEncA = 1.0e-3f;  // width_rad = az_rad * kEncA
static constexpr float kEncB = 0.5f;     // gain_lin  = dist_m * kEncB
static constexpr uint32_t kId = 4;

static void dispatchMove(core::SpatialEngine& e, float az, float dist) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjMove;
    ipc::PayloadObjMove p; p.obj_id = kId; p.az_rad = az; p.el_rad = 0.f; p.dist_m = dist;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchWidth(core::SpatialEngine& e, float w) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjWidth;
    ipc::PayloadObjWidth p; p.obj_id = kId; p.width_rad = w;
    c.payload = p; e.dispatchCommand(c);
}
static void dispatchGain(core::SpatialEngine& e, float g) {
    ipc::Command c; c.tag = ipc::CommandTag::ObjGain;
    ipc::PayloadObjGain p; p.obj_id = kId; p.gain = g;
    c.payload = p; e.dispatchCommand(c);
}

int main() {
    std::puts("soak_scene_save_race: concurrent snapshotObjects vs audioBlock publish (TSan + tearing gate)");

    constexpr int ROUNDS  = 200;       // >=150 required
    constexpr int N_SPK   = 8;
    constexpr int FRAMES  = 64;

    int failures = 0;

    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, FRAMES);

    std::vector<std::vector<float>> bufs(static_cast<size_t>(N_SPK),
                                         std::vector<float>(FRAMES, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(N_SPK));
    for (int s = 0; s < N_SPK; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    std::atomic<bool> stop{false};
    std::atomic<long>  publishes{0};
    std::atomic<long>  reader_rounds{0};
    std::atomic<int>   tear_failures{0};

    // Audio thread: per iteration, push a correlated-pair update for a strictly
    // increasing k, then drive one audioBlock (drain + publish).
    std::thread audio([&] {
        audio_io::AudioBlock block;
        block.output_channels      = ptrs.data();
        block.output_channel_count = N_SPK;
        block.num_frames           = FRAMES;
        block.sample_rate          = 48000.0;

        float k = 1.f;
        while (!stop.load(std::memory_order_relaxed)) {
            const float az   = k;
            const float w    = az * kEncA;
            const float dist = 1.f + k * 0.01f;
            const float g    = dist * kEncB;
            // Order does not matter for consistency: all four land in obj_cache_
            // during THIS block's drain, and the publish happens post-drain.
            dispatchMove(engine, az, dist);
            dispatchWidth(engine, w);
            dispatchGain(engine, g);
            engine.audioBlock(block);
            publishes.fetch_add(1, std::memory_order_relaxed);
            k += 1.f;
            if (k > 1.0e6f) k = 1.f;  // keep magnitudes bounded
        }
    });

    // Control thread: repeatedly snapshot and assert the correlated-pair
    // invariant on every emitted object in every snapshot.
    std::thread control([&] {
        std::vector<ipc::ObjectSnapshot> out;
        while (!stop.load(std::memory_order_relaxed)) {
            engine.snapshotObjects(out);
            for (const auto& o : out) {
                if (o.id != static_cast<int>(kId)) continue;
                // pair A: width_rad must equal az_rad * kEncA (recompute).
                const float expW = o.az_rad * kEncA;
                // pair B: gain_linear must equal dist_m * kEncB.
                const float expG = o.dist_m * kEncB;
                // Tolerance scaled by magnitude (values reach ~1e6 * 1e-3 = 1e3).
                const float tolW = 1e-3f + std::fabs(expW) * 1e-4f;
                const float tolG = 1e-3f + std::fabs(expG) * 1e-4f;
                if (std::fabs(o.width_rad - expW) > tolW) {
                    std::fprintf(stderr,
                        "TEAR pairA: az=%.3f width=%.6f expW=%.6f\n",
                        (double)o.az_rad, (double)o.width_rad, (double)expW);
                    tear_failures.fetch_add(1, std::memory_order_relaxed);
                }
                if (std::fabs(o.gain_linear - expG) > tolG) {
                    std::fprintf(stderr,
                        "TEAR pairB: dist=%.3f gain=%.6f expG=%.6f\n",
                        (double)o.dist_m, (double)o.gain_linear, (double)expG);
                    tear_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
            reader_rounds.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Run until both threads have done >= ROUNDS of work, interleaved.
    while (publishes.load(std::memory_order_relaxed) < ROUNDS ||
           reader_rounds.load(std::memory_order_relaxed) < ROUNDS) {
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();
    control.join();

    const long pubs  = publishes.load(std::memory_order_relaxed);
    const long reads = reader_rounds.load(std::memory_order_relaxed);
    const int  tears = tear_failures.load(std::memory_order_relaxed);

    std::printf("  publishes=%ld  reader_rounds=%ld  tear_failures=%d\n",
                pubs, reads, tears);
    if (tears != 0) {
        std::fprintf(stderr, "  FAIL: correlated-pair invariant violated (cross-buffer tearing)\n");
        ++failures;
    }
    if (pubs < ROUNDS || reads < ROUNDS) {
        std::fprintf(stderr, "  FAIL: insufficient interleaving rounds\n");
        ++failures;
    }

    if (failures == 0)
        std::printf("soak_scene_save_race: ALL PASS (%ld publishes, %ld reads, 0 tears)\n",
                    pubs, reads);
    return failures;
}
