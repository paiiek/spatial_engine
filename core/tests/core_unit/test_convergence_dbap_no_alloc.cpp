// test_convergence_dbap_no_alloc.cpp
// Dreamscape Convergence v1.0 — Phase 1.3: DBAP audio-thread no-alloc.
//
// Before this increment, DBAPRenderer::dbapForPosition called
// AlgorithmAnalyticReference::dbap_gain(), which allocated two std::vectors per
// call — i.e. on the audio thread, per active DBAP object, every block (and 3x
// per object on the width>0 multi-virtual-source path). That violated the RT
// no-alloc contract. The fix routes DBAP through dbap_gain_into(), which folds
// the weights into the caller's fixed buffer.
//
// This test drives a live SpatialEngine with DBAP objects (both width==0 and
// width>0 branches) through audioBlock() and asserts ZERO heap allocations on
// the audio thread, using the RT_ASSERTS allocation sentinel. A warmup precedes
// the measured window so any legitimate one-time first-touch allocation does not
// confound the per-block steady-state measurement (DBAP's old leak was per
// block, so it shows up in the measured window regardless).
//
// The allocation count is only instrumented when SPE_RT_ASSERTS=1 (the CI test
// build); without it the sentinel is inert and the check is skipped (not vacuous).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

static spe::geometry::SpeakerLayout make_dome() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "dbap_dome"; l.regularity = Regularity::IRREGULAR;
    int ch = 1;
    for (float el_deg : {-20.f, 30.f}) {
        const float el = el_deg * kPi / 180.f;
        for (int i = 0; i < 8; ++i) {
            const float az = (-kPi) + 2.f * kPi * (float) i / 8.f;
            const float ce = std::cos(el);
            Speaker s; s.channel = ch++;
            s.x = ce * std::sin(az); s.y = std::sin(el); s.z = ce * std::cos(az);
            l.speakers.push_back(s);
        }
    }
    return l;
}

static void move(spe::core::SpatialEngine& e, uint32_t id, float az, float el, float dist) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = el; p.dist_m = dist;
    c.payload = p; e.dispatchCommand(c);
}
static void setAlgo(spe::core::SpatialEngine& e, uint32_t id, spe::ipc::Algorithm a) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjAlgo;
    spe::ipc::PayloadObjAlgo p; p.obj_id = id; p.algo = a;
    c.payload = p; e.dispatchCommand(c);
}
static void setWidth(spe::core::SpatialEngine& e, uint32_t id, float w) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjWidth;
    spe::ipc::PayloadObjWidth p; p.obj_id = id; p.width_rad = w;
    c.payload = p; e.dispatchCommand(c);
}

int main() {
    const int N = 16;
    constexpr int kFrames = 256;

    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, kFrames);

    // Two DBAP objects: obj0 point source (width==0), obj1 spread (width>0) to
    // exercise both the single-source and 3-virtual-source DBAP branches.
    setAlgo(engine, 0, spe::ipc::Algorithm::DBAP);
    move(engine, 0, 0.5f, 0.1f, 2.0f);
    setAlgo(engine, 1, spe::ipc::Algorithm::DBAP);
    move(engine, 1, -0.7f, 0.2f, 2.5f);
    setWidth(engine, 1, 0.6f);

    std::vector<std::vector<float>> bufs(static_cast<size_t>(N),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(N));
    for (int s = 0; s < N; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    auto drive = [&]() {
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = N;
        blk.num_frames = kFrames;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);
    };

    // Warmup (settle any one-time first-touch allocation before measuring).
    drive();
    drive();

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
    spe::util::rt_alloc_violations_reset();
#endif

    double energy = 0.0;
    for (int b = 0; b < 8; ++b) {
        drive();
        for (int s = 0; s < N; ++s)
            for (int n = 0; n < kFrames; ++n) {
                const float v = bufs[static_cast<size_t>(s)][static_cast<size_t>(n)];
                energy += static_cast<double>(v) * v;
            }
    }

    CHECK(energy > 1.0e-6, "DBAP objects render (non-silent) — measurement not vacuous");

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
    const unsigned long long viol = spe::util::rt_alloc_violations();
    std::printf("[dbap-no-alloc] audio-thread allocations over 8 blocks = %llu (energy=%.6g)\n",
                viol, energy);
    CHECK(viol == 0ull, "DBAP audio-thread path allocates (dbap_gain_into must be used)");
#else
    std::printf("[dbap-no-alloc] SKIPPED alloc check (build without SPE_RT_ASSERTS); energy=%.6g\n",
                energy);
#endif

    if (failures == 0) { std::printf("test_convergence_dbap_no_alloc: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_dbap_no_alloc: %d FAILURE(S)\n", failures);
    return 1;
}
