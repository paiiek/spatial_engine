// test_convergence_wfs.cpp
// Dreamscape Convergence ④ — WFS full modes (ported iae::computeWavefieldSynthesisDriving
// + WFSRenderer wiring). Exercises every axis of the ported kernel:
//   - spherical front: Σg²=1, relative delays ≥0 with min==0, source-side loudest
//   - plane wave: Σg²=1, relative delays ≥0, source-side loudest, pattern ≠ spherical
//   - wavefront curvature: changing curvature changes the delay pattern
//   - obliquity radial blend: blend 0 vs 1 changes the gain distribution
//     (kernel-level, with speaker forward tilted off-radial so the axis is live)
//   - gain shape scale: higher scale => peakier gains (max gain grows), Σg²=1
//   - delay shape scale: higher scale => larger delays, lower scale => smaller
//   - VBAP gain blend (renderer level): output stays finite, non-silent, right-biased
// Plus a renderer end-to-end: impulse in, finite + non-silent out, right-side biased.

#include "render/WFSRenderer.h"
#include "render/ported/Wfs.h"
#include "geometry/SpeakerLayout.h"
#include "core/Constants.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

using namespace spe::render;
using namespace spe::geometry;

// Horizontal ring in mmhoa-native frame (x=right, y=up, z=front).
// Speaker i sits at azimuth θ_i: pos = {R sinθ, 0, R cosθ} so az=0 => +z (front),
// az=90° => +x (right) — matches the engine's (az,el,dist) -> Cartesian mapping.
static void make_ring(int n, float R,
                      std::vector<iae::Vec3>& pos,
                      std::vector<iae::Vec3>& fwd,
                      std::vector<iae::SpeakerKind>& kind) {
    pos.clear(); fwd.clear(); kind.clear();
    for (int i = 0; i < n; ++i) {
        const float th = (2.f * 3.14159265358979323846f) * (float) i / (float) n;
        const iae::Vec3 p{ R * std::sin(th), 0.f, R * std::cos(th) };
        pos.push_back(p);
        fwd.push_back(iae::normalized(iae::Vec3{ -p.x, -p.y, -p.z })); // toward origin
        kind.push_back(iae::SpeakerKind::Frontal);
    }
}

static SpeakerLayout make_ring_layout(int n, float R) {
    SpeakerLayout l; l.name = "wfs_ring"; l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < n; ++i) {
        const float th = (2.f * 3.14159265358979323846f) * (float) i / (float) n;
        Speaker s; s.channel = i + 1;
        s.x = R * std::sin(th); s.y = 0.f; s.z = R * std::cos(th);
        l.speakers.push_back(s);
    }
    return l;
}

static float sum_sq(const float* g, int n) { float s=0.f; for (int i=0;i<n;++i) s+=g[i]*g[i]; return s; }
static int   arg_max(const float* g, int n) { int b=0; for (int i=1;i<n;++i) if (g[i]>g[b]) b=i; return b; }
static float max_of (const float* g, int n) { float m=g[0]; for (int i=1;i<n;++i) m=std::max(m,g[i]); return m; }

int main() {
    const float SR = 48000.f;
    const int   N  = 12;
    const float R  = 2.f;

    std::vector<iae::Vec3> pos, fwd; std::vector<iae::SpeakerKind> kind;
    make_ring(N, R, pos, fwd, kind);

    // Source on the +x (right) side: az=90°, dist 3 m.
    const iae::Vec3 srcRight{ 3.f, 0.f, 0.f };
    // Speaker index nearest +x is N/4 (θ=90°).
    const int spkRight = N / 4;

    iae::WfsDrivingParams base{}; // reference defaults (c=343, expo=1, blend=0.72, curv/shape=100)

    // ---- A. spherical front -------------------------------------------------
    {
        std::array<float, 64> g{}, d{};
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            base, /*planeWave*/ false, nullptr, g.data(), d.data());
        CHECK(std::abs(sum_sq(g.data(), N) - 1.f) < 1.0e-3f);        // Σg²=1
        float minD = d[0], maxD = d[0];
        for (int i=0;i<N;++i){ minD=std::min(minD,d[i]); maxD=std::max(maxD,d[i]); CHECK(d[i] >= -1.0e-4f); }
        CHECK(std::abs(minD) < 1.0e-3f);                             // relative: min delay == 0
        CHECK(maxD > 1.0f);                                          // some real spread
        CHECK(arg_max(g.data(), N) == spkRight);                    // source-side loudest
        for (int i=0;i<N;++i) CHECK(std::isfinite(g[i]) && std::isfinite(d[i]));
    }

    // ---- B. plane wave ------------------------------------------------------
    std::array<float, 64> gSph{}, dSph{}, gPw{}, dPw{};
    {
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            base, false, nullptr, gSph.data(), dSph.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            base, /*planeWave*/ true, nullptr, gPw.data(), dPw.data());
        CHECK(std::abs(sum_sq(gPw.data(), N) - 1.f) < 1.0e-3f);
        float minD = dPw[0];
        for (int i=0;i<N;++i){ minD=std::min(minD,dPw[i]); CHECK(dPw[i] >= -1.0e-4f); }
        CHECK(std::abs(minD) < 1.0e-3f);
        CHECK(arg_max(gPw.data(), N) == spkRight);
        // Plane-wave delay pattern differs from spherical (distinct front model).
        float diff = 0.f; for (int i=0;i<N;++i) diff += std::abs(dPw[i] - dSph[i]);
        CHECK(diff > 5.f);
    }

    // ---- C. wavefront curvature changes the delay pattern -------------------
    {
        iae::WfsDrivingParams lo = base; lo.wavefrontCurvature = 40.f;
        iae::WfsDrivingParams hi = base; hi.wavefrontCurvature = 180.f;
        std::array<float, 64> gl{}, dl{}, gh{}, dh{};
        const iae::Vec3 src45{ 2.1f, 0.f, 2.1f }; // az=45°, ~3 m
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, src45, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            lo, false, nullptr, gl.data(), dl.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, src45, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            hi, false, nullptr, gh.data(), dh.data());
        float diff = 0.f; for (int i=0;i<N;++i) diff += std::abs(dl[i] - dh[i]);
        CHECK(diff > 5.f);
        CHECK(std::abs(sum_sq(gl.data(), N) - 1.f) < 1.0e-3f);
        CHECK(std::abs(sum_sq(gh.data(), N) - 1.f) < 1.0e-3f);
    }

    // ---- D. obliquity radial blend (kernel-level, off-radial forward) -------
    {
        // Tilt every speaker's forward off the radial direction so cosOrient and
        // cosRad genuinely differ — then blend 0 vs 1 must change the gains.
        std::vector<iae::Vec3> tilt(N);
        for (int i=0;i<N;++i)
            tilt[i] = iae::normalized(iae::Vec3{ fwd[i].x + 0.6f, fwd[i].y + 0.4f, fwd[i].z });
        iae::WfsDrivingParams b0 = base; b0.obliquityRadialBlend = 0.f;
        iae::WfsDrivingParams b1 = base; b1.obliquityRadialBlend = 1.f;
        std::array<float, 64> g0{}, d0{}, g1{}, d1{};
        iae::computeWavefieldSynthesisDriving(pos.data(), tilt.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            b0, false, nullptr, g0.data(), d0.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), tilt.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            b1, false, nullptr, g1.data(), d1.data());
        float diff = 0.f; for (int i=0;i<N;++i) diff += std::abs(g0[i] - g1[i]);
        CHECK(diff > 1.0e-2f);
        CHECK(std::abs(sum_sq(g0.data(), N) - 1.f) < 1.0e-3f);
        CHECK(std::abs(sum_sq(g1.data(), N) - 1.f) < 1.0e-3f);
    }

    // ---- E. gain shape scale: higher scale => peakier (max gain grows) -------
    {
        iae::WfsDrivingParams flat  = base; flat.gainShapeScale = 100.f; // exp 1
        iae::WfsDrivingParams peak  = base; peak.gainShapeScale = 190.f; // exp ~3.7
        std::array<float, 64> gf{}, df{}, gp{}, dp{};
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            flat, false, nullptr, gf.data(), df.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            peak, false, nullptr, gp.data(), dp.data());
        CHECK(max_of(gp.data(), N) > max_of(gf.data(), N) + 1.0e-3f);
        CHECK(std::abs(sum_sq(gp.data(), N) - 1.f) < 1.0e-3f);
    }

    // ---- F. delay shape scale: higher => larger delays, lower => smaller -----
    {
        iae::WfsDrivingParams mid = base; mid.delayShapeScale = 100.f; // ×1
        iae::WfsDrivingParams big = base; big.delayShapeScale = 170.f; // ×~3.1
        iae::WfsDrivingParams sml = base; sml.delayShapeScale = 30.f;  // ×~0.34
        std::array<float, 64> gm{}, dm{}, gb{}, db{}, gs{}, ds{};
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            mid, false, nullptr, gm.data(), dm.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            big, false, nullptr, gb.data(), db.data());
        iae::computeWavefieldSynthesisDriving(pos.data(), fwd.data(), kind.data(),
            (size_t) N, srcRight, SR, iae::WfsDelayReferenceMode::MinimumToNearestSecondary,
            sml, false, nullptr, gs.data(), ds.data());
        const float mMid = max_of(dm.data(), N);
        CHECK(max_of(db.data(), N) > mMid * 1.5f);
        CHECK(max_of(ds.data(), N) < mMid * 0.9f);
    }

    // ---- G. renderer end-to-end: finite, non-silent, right-biased; + blend ---
    {
        auto layout = make_ring_layout(N, R);
        WFSRenderer wfs;
        wfs.prepareToPlay(layout, (double) SR);
        CHECK(wfs.numSpeakers() == N);
        CHECK(!wfs.isReady());
        wfs.ensureAllocated();
        CHECK(wfs.isReady());

        std::array<ObjectState, spe::MAX_OBJECTS> objs{};
        objs[0].az_rad = 75.f * 3.14159265f / 180.f; // right side, between speakers
        objs[0].el_rad = 0.f;
        objs[0].dist_m = 8.0f; // well outside the R=2 ring => spread wavefront (not near-field)
        objs[0].active = true;

        // Sustained input (DC). Measured at steady state after a warm-up block, so
        // the click-free gain ramp (starts at 0) doesn't skew the energy — this is
        // an end-to-end signal-flow check, not an exact-gain assertion.
        const int NS = 256;
        std::vector<float> dry(NS, 1.f);
        std::array<const float*, spe::MAX_OBJECTS> dptrs{}; dptrs[0] = dry.data();
        std::vector<float> out(NS * N, 0.f);

        auto run_and_measure = [&](float blend, float& energy, float& rightE,
                                   float& leftE, int& active) {
            iae::WfsDrivingParams p{}; p.vbapGainBlend = blend;
            wfs.setWfsParams(p, /*planeWave*/ false,
                             iae::WfsDelayReferenceMode::MinimumToNearestSecondary);
            // Warm-up block (fills delay lines + ramps to target), then measure.
            std::fill(out.begin(), out.end(), 0.f);
            wfs.processBlock(std::span<const ObjectState>(objs.data(), spe::MAX_OBJECTS),
                             std::span<const float* const>(dptrs.data(), spe::MAX_OBJECTS),
                             out.data(), NS);
            std::fill(out.begin(), out.end(), 0.f);
            wfs.processBlock(std::span<const ObjectState>(objs.data(), spe::MAX_OBJECTS),
                             std::span<const float* const>(dptrs.data(), spe::MAX_OBJECTS),
                             out.data(), NS);
            std::array<float, 64> chE{};
            energy = rightE = leftE = 0.f;
            for (int n=0;n<NS;++n) for (int s=0;s<N;++s) {
                const float v = out[n*N+s]; energy += v*v; chE[s] += v*v;
                if (!std::isfinite(v)) { ++failures; }
                if (layout.speakers[s].x > 0.1f) rightE += v*v;
                if (layout.speakers[s].x < -0.1f) leftE += v*v;
            }
            active = 0;
            for (int s=0;s<N;++s) if (chE[s] > 0.01f * energy) ++active;
        };

        float e=0,r=0,l=0; int aWfs=0, aBlend=0;
        run_and_measure(0.f, e, r, l, aWfs);   // pure WFS
        CHECK(e > 1.0e-6f);                     // non-silent
        CHECK(r > l);                           // right-biased
        CHECK(aWfs > 3);                        // WFS spreads a wavefront
        run_and_measure(1.f, e, r, l, aBlend);  // full VBAP blend
        CHECK(e > 1.0e-6f);
        CHECK(r > l);
        // Proves the VBAP-blend path actually executed (not silently skipped):
        // full VBAP collapses the wavefront to a triplet, so strictly fewer
        // speakers are active than under pure WFS.
        CHECK(aBlend < aWfs);
    }

    if (failures == 0) std::printf("test_convergence_wfs: ALL PASS\n");
    return failures;
}
