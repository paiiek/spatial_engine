// state_resync_race.cpp  (v0.9 Lane 6 — C6 / T8 TSan concurrent-reader gate)
//
// Proves the C6 mutex (state_snapshot_mtx_) serializes the TWO genuinely
// independent non-RT snapshotObjects() readers that run on DIFFERENT threads:
//   (1) /scene/save  — snapshotObjects(out)            [control-loop thread]
//   (2) /sys/state_request — snapshotObjects(out,true) [OSC IO thread]
// while a third "audio" thread drives obj_cache_ + the post-drain three-buffer
// publish. Both readers stomp the single snap_reader_busy_idx_ claim slot; WITHOUT
// the mutex two concurrent claims break the F4b writer-avoidance invariant → a
// torn read vs the audio writer (TSan-reportable / a cross-buffer tear).
//
// Two invariants, checked together (same as soak_scene_save_race / AC9):
//   (1) TSan reports ZERO data races (build with -fsanitize=thread).
//   (2) No cross-buffer tearing — the writer maintains TWO correlated field
//       pairs the readers recompute in every snapshot:
//         pair A:  width_rad == az_rad * kEncA
//         pair B:  gain_lin  == dist_m * kEncB
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
    std::puts("state_resync_race: concurrent scene-save + state_request snapshotObjects vs audioBlock publish (TSan + tearing gate)");

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
    std::atomic<long>  save_rounds{0};
    std::atomic<long>  req_rounds{0};
    std::atomic<int>   tear_failures{0};

    // Recompute the correlated-pair invariant on every emitted object.
    auto checkSnap = [&](const std::vector<ipc::ObjectSnapshot>& out) {
        for (const auto& o : out) {
            if (o.id != static_cast<int>(kId)) continue;
            const float expW = o.az_rad * kEncA;
            const float expG = o.dist_m * kEncB;
            const float tolW = 1e-3f + std::fabs(expW) * 1e-4f;
            const float tolG = 1e-3f + std::fabs(expG) * 1e-4f;
            if (std::fabs(o.width_rad - expW) > tolW)
                tear_failures.fetch_add(1, std::memory_order_relaxed);
            if (std::fabs(o.gain_linear - expG) > tolG)
                tear_failures.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Audio thread: per iteration push a correlated-pair update, then drive one
    // audioBlock (drain + publish).
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
            dispatchMove(engine, az, dist);
            dispatchWidth(engine, w);
            dispatchGain(engine, g);
            engine.audioBlock(block);
            publishes.fetch_add(1, std::memory_order_relaxed);
            k += 1.f;
            if (k > 1.0e6f) k = 1.f;
        }
    });

    // Reader 1 — /scene/save path (default include_dsp_only=false), modelling
    // the control-loop thread.
    std::thread saver([&] {
        std::vector<ipc::ObjectSnapshot> out;
        while (!stop.load(std::memory_order_relaxed)) {
            engine.snapshotObjects(out);            // default false
            checkSnap(out);
            save_rounds.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Reader 2 — /sys/state_request path (include_dsp_only=true), modelling the
    // OSC IO thread. This is the NEW caller the C6 mutex must serialize against
    // reader 1.
    std::thread requester([&] {
        std::vector<ipc::ObjectSnapshot> out;
        while (!stop.load(std::memory_order_relaxed)) {
            engine.snapshotObjects(out, /*include_dsp_only=*/true);
            checkSnap(out);
            req_rounds.fetch_add(1, std::memory_order_relaxed);
        }
    });

    while (publishes.load(std::memory_order_relaxed) < ROUNDS ||
           save_rounds.load(std::memory_order_relaxed) < ROUNDS ||
           req_rounds.load(std::memory_order_relaxed) < ROUNDS) {
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);
    audio.join();
    saver.join();
    requester.join();

    const long pubs   = publishes.load(std::memory_order_relaxed);
    const long saves  = save_rounds.load(std::memory_order_relaxed);
    const long reqs   = req_rounds.load(std::memory_order_relaxed);
    const int  tears  = tear_failures.load(std::memory_order_relaxed);

    std::printf("  publishes=%ld  save_rounds=%ld  req_rounds=%ld  tear_failures=%d\n",
                pubs, saves, reqs, tears);
    if (tears != 0) {
        std::fprintf(stderr, "  FAIL: correlated-pair invariant violated (cross-buffer tearing)\n");
        ++failures;
    }
    if (pubs < ROUNDS || saves < ROUNDS || reqs < ROUNDS) {
        std::fprintf(stderr, "  FAIL: insufficient interleaving rounds\n");
        ++failures;
    }

    if (failures == 0)
        std::printf("state_resync_race: ALL PASS (%ld publishes, %ld saves, %ld reqs, 0 tears)\n",
                    pubs, saves, reqs);
    return failures;
}
