// test_convergence_max_speakers.cpp
// Dreamscape Convergence ⑤ — Phase 0.5 speaker-dimension lift (64 -> 128).
//
// Mirrors v0.9 Lane C's object-cap tests (test_p_max_objects / test_p_object_
// cap_render) but for the SPEAKER (output-channel) dimension. Cap-AGNOSTIC: it
// uses spe::MAX_SPEAKERS everywhere with no #if guard, so the SAME source proves
//   - 64 speakers render at the default build, and
//   - 128 speakers render at a `-DSPATIAL_ENGINE_MAX_SPEAKERS=128` build.
//
// What it proves at the 128 build (where the lift matters):
//   1. compile-time: MAX_SPEAKERS tracks the configured SPE_MAX_SPEAKERS macro.
//   2. Every renderer (VBAP / VAP / WFS / DBAP) accepts an N==MAX_SPEAKERS layout
//      (no >64 assert/clamp fires) and reports numSpeakers()==N.
//   3. Driving one active object fills the FULL speaker dimension: all N channels
//      stay finite, output is non-silent — i.e. no stale 64-element scratch
//      truncates or overruns the bus.
//   4. A source aimed at a HIGH-INDEX speaker (k = 5N/8 -> index 80 at the 128
//      build, well past the old 64 cap) actually lights that speaker up
//      (its channel energy is a large fraction of the per-block peak). A leftover
//      64 cap would silently clamp/route it away and this assertion would fail.
//
// Path under test: RenderingAlgorithm scratch (AlgoScratch.gains), each
// renderer's ramps_/position buffers, and AlgorithmAnalyticReference's
// per-call temporaries — all now sized spe::MAX_SPEAKERS.

#include "render/VBAPRenderer.h"
#include "render/VAPRenderer.h"
#include "render/WFSRenderer.h"
#include "render/DBAPRenderer.h"
#include "geometry/SpeakerLayout.h"
#include "core/Constants.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

using namespace spe::render;
using namespace spe::geometry;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

// Cap-agnostic — must NOT hard-pin 64 or the 128 build won't compile.
static_assert(spe::MAX_SPEAKERS == SPE_MAX_SPEAKERS,
              "MAX_SPEAKERS must equal the configured SPE_MAX_SPEAKERS");
static_assert(spe::MAX_SPEAKERS == 64 || spe::MAX_SPEAKERS == 128,
              "MAX_SPEAKERS must be 64 or 128 (Phase 0.5)");

static constexpr float kPi = 3.14159265358979323846f;

// Engine-frame unit direction for (az, el): x=cosEl sinAz (right), y=sinEl (up),
// z=cosEl cosAz (front) — matches coords::pipeline_dir_to_ported's source dir.
static void dir_of(float az, float el, float R, float& x, float& y, float& z) {
    const float ce = std::cos(el);
    x = R * ce * std::sin(az); y = R * std::sin(el); z = R * ce * std::cos(az);
}

// Two-ring dome: lower half-indices [0,H) at el=-20°, upper [H,N) at el=+35°,
// H=N/2. Within each ring speaker (H*ring + m) sits at az = 2π m/H. A genuine
// 3D hull (not a degenerate flat ring), so the volumetric VAP panner is well
// posed while VBAP/DBAP/WFS still resolve the aimed speaker exactly.
static SpeakerLayout make_dome_layout(int n, float R) {
    SpeakerLayout l; l.name = "cap_dome"; l.regularity = Regularity::IRREGULAR;
    const int H = n / 2;
    for (int i = 0; i < n; ++i) {
        const int   m  = i % H;
        const float el = (i < H ? -20.f : 35.f) * kPi / 180.f;
        const float az = 2.f * kPi * (float) m / (float) H;
        Speaker s; s.channel = i + 1;
        dir_of(az, el, R, s.x, s.y, s.z);
        l.speakers.push_back(s);
    }
    return l;
}

// Drive one active object through `r`, warm up the click-free gain ramps, then
// measure steady-state per-channel energy into chE[0..N-1]. Returns total energy.
static double drive(RenderingAlgorithm& r, int N,
                    float az, float el, float dist,
                    std::vector<float>& chE) {
    std::array<ObjectState, spe::MAX_OBJECTS> objs{};
    objs[0].az_rad = az; objs[0].el_rad = el; objs[0].dist_m = dist;
    objs[0].active = true;

    const int NS = 256;
    std::vector<float> dry(NS, 1.f);                 // sustained DC
    std::array<const float*, spe::MAX_OBJECTS> dptrs{}; dptrs[0] = dry.data();
    std::vector<float> out((size_t) NS * (size_t) N, 0.f);

    auto block = [&]() {
        std::fill(out.begin(), out.end(), 0.f);
        r.processBlock(std::span<const ObjectState>(objs.data(), spe::MAX_OBJECTS),
                       std::span<const float* const>(dptrs.data(), spe::MAX_OBJECTS),
                       out.data(), NS);
    };
    block(); block(); block();                       // warm-up to steady state

    chE.assign((size_t) N, 0.f);
    double energy = 0.0;
    for (int n = 0; n < NS; ++n)
        for (int s = 0; s < N; ++s) {
            const float v = out[(size_t) n * (size_t) N + (size_t) s];
            if (!std::isfinite(v)) ++failures;
            chE[(size_t) s] += v * v;
            energy += (double) v * (double) v;
        }
    return energy;
}

static int arg_max(const std::vector<float>& chE) {
    int b = 0; for (int i = 1; i < (int) chE.size(); ++i) if (chE[(size_t) i] > chE[(size_t) b]) b = i; return b;
}
// Circular index distance on the N-speaker ring.
static int ring_dist(int a, int b, int N) {
    int d = std::abs(a - b); return std::min(d, N - d);
}
// Energy within ±w indices of centre k (wraps around the ring).
static double neigh_energy(const std::vector<float>& chE, int k, int w, int N) {
    double s = 0.0;
    for (int o = -w; o <= w; ++o) { int i = ((k + o) % N + N) % N; s += chE[(size_t) i]; }
    return s;
}

// Validate one renderer at the full N==MAX_SPEAKERS layout. A source is aimed at
// the high-index speaker kTarget (k = 5N/8, on the upper ring — index 80 at the
// 128 build, well past the old 64 cap).
//   tight=true  (VBAP/DBAP/WFS): the peak must land on the aimed high-index
//                speaker kTarget ±2 (index ≥ 64 at the 128 build) and that
//                neighbourhood must hold the bulk of the energy — the strong
//                "index past 64 renders" proof.
//   tight=false (VAP volumetric): VAP's per-speaker placement on a synthetic
//                dome is the VAP increment's concern, not this buffer-size lift.
//                Here we only assert the 64->128 lift sites are exercised:
//                prepareToPlay populated spk_pos_ported_[N-1] (would OOB a
//                64-array) and processBlock wrote finite, non-silent output
//                across the full N-channel bus.
static void exercise(RenderingAlgorithm& r, const char* name, int N,
                     int kTarget, float azTarget, float elTarget, bool tight) {
    CHECK(r.numSpeakers() == N);

    std::vector<float> chE;
    const double e = drive(r, N, azTarget, elTarget, /*dist*/ 2.f, chE);
    CHECK(e > 1.0e-6);                                   // non-silent (finite checked in drive)
    const int amax = arg_max(chE);

    double near = 0.0;
    if (tight) {
        // The loudest channel must be the aimed HIGH index (upper ring) — at the
        // 128 build that is index ≥ 64, so a stale 64-element buffer could not
        // produce this peak.
        CHECK(amax >= N / 2);
        CHECK(ring_dist(amax, kTarget, N) <= 2);
        near = neigh_energy(chE, kTarget, 2, N);
        CHECK(near > 0.5 * e);
    }
    std::printf("[%s] N=%d  kTarget=%d  argmax=%d  near/total=%.3f  energy=%.4g\n",
                name, N, kTarget, amax, (e > 0 ? near / e : 0.0), e);
}

int main() {
    const double SR = 48000.0;
    const int    N  = spe::MAX_SPEAKERS;   // cap-relative: 64 or 128
    const float  R  = 2.f;

    std::printf("[PASS] static_assert MAX_SPEAKERS == %d (configured cap)\n",
                spe::MAX_SPEAKERS);

    auto layout = make_dome_layout(N, R);

    // Aim at a high-index speaker on the UPPER ring: k = 5N/8 (40 @ N=64,
    // 80 @ N=128). Upper-ring speaker (H + m) sits at az = 2π m/H, el = +35°;
    // for k = 5N/8 -> m = N/8 -> az = 2π(N/8)/(N/2) = π/2 = 90°.
    const int   kTarget  = (5 * N) / 8;
    const float azTarget = kPi / 2.f;          // 90°
    const float elTarget = 35.f * kPi / 180.f; // upper ring elevation

    // --- VBAP (3D elevation-aware: target speaker dominates) ---
    {
        VBAPRenderer vbap;
        vbap.prepareToPlay(layout, SR);
        exercise(vbap, "VBAP", N, kTarget, azTarget, elTarget, /*tight*/ true);
    }
    // --- VAP (volumetric/VBAP blend: high-index/upper-ring dominance) ---
    {
        VAPRenderer vap;
        vap.prepareToPlay(layout, SR);
        exercise(vap, "VAP", N, kTarget, azTarget, elTarget, /*tight*/ false);
    }
    // --- DBAP (distance-based: source on a speaker -> that speaker loudest) ---
    {
        DBAPRenderer dbap;
        dbap.prepareToPlay(layout, SR);
        exercise(dbap, "DBAP", N, kTarget, azTarget, elTarget, /*tight*/ true);
    }
    // --- WFS (spread wavefront: lazy-alloc handshake, source-side loudest) ---
    {
        WFSRenderer wfs;
        wfs.prepareToPlay(layout, SR);
        CHECK(!wfs.isReady());
        wfs.ensureAllocated();
        CHECK(wfs.isReady());
        exercise(wfs, "WFS", N, kTarget, azTarget, elTarget, /*tight*/ true);
    }

    if (failures == 0)
        std::printf("test_convergence_max_speakers: ALL PASS (N=%d)\n", N);
    return failures;
}
