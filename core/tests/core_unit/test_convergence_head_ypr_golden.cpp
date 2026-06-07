// test_convergence_head_ypr_golden.cpp
// Phase 2.6b (Dreamscape Convergence) — /ypr head-tracking END-TO-END golden.
//
// 2.6a locked the rotation math (rotate_engine_dir_by_head) in a pure unit test.
// This test proves the FULL in-process wire path actually moves the binaural
// image: raw OSC /ypr packet -> CommandDecoder -> SysHeadYpr dispatch -> engine
// atomic head members -> audioBlock B1 branch reads them once/block -> rotation
// applied before binaural_.setDirection -> per-object HRTF lookup.
//
// Observable: the binaural bus inter-aural delay (ITD). With the committed
// synthetic_itd_pm90.speh fixture (grid {0, +90, -90}), az=+90 (RIGHT) makes
// the RIGHT ear lead (L delayed 32 samples), az=-90 the LEFT ear lead. We feed
// object 0 the engine's per-object 110 Hz tone and measure the L/R lag by
// cross-correlation.
//
// Sign is anchored EMPIRICALLY, not by convention: a front object (az=0) under
// head yaw +90 must produce the SAME L/R lead as a PHYSICALLY-right object
// (az=+90) with no head rotation. A missing rotation, wrong axis, or inverted
// sign (the L/R-inversion bug class, the whole reason 2.6a is a separate gate)
// fails this test.

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

static int failures = 0;
#define CHECK(c, msg) do { if(!(c)){ std::fprintf(stderr,"FAIL: %s\n", msg); ++failures; } } while(0)

static constexpr float kPi = 3.14159265358979323846f;

// 8-speaker horizontal ring (binaural path ignores the layout, but the engine
// needs a valid one to prepare).
static spe::geometry::SpeakerLayout ring8() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "ypr_ring"; l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < 8; ++i) {
        const float az = (-kPi) + 2.f * kPi * static_cast<float>(i) / 8.f;
        Speaker s; s.channel = i + 1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}

// Minimal OSC encoder for ,fff messages.
static std::vector<uint8_t> oscFFF(const std::string& addr, float a, float b, float c) {
    std::vector<uint8_t> p;
    auto pushPadded = [&](const std::string& s) {
        for (char ch : s) p.push_back(static_cast<uint8_t>(ch));
        p.push_back(0);
        while (p.size() % 4 != 0) p.push_back(0);
    };
    auto pushF = [&](float f) {
        uint32_t u; std::memcpy(&u, &f, 4);
        p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
        p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
    };
    pushPadded(addr);
    pushPadded(",fff");
    pushF(a); pushF(b); pushF(c);
    return p;
}

// Cross-correlation lag (in samples) maximizing Σ_n L[n]·R[n+d] over the search
// window. Sign is a fixed convention; the test only compares lags to each other.
static int xcorrLag(const std::vector<float>& L, const std::vector<float>& R, int maxLag) {
    const int N = static_cast<int>(L.size());
    double best = -1e300; int bestLag = 0;
    for (int d = -maxLag; d <= maxLag; ++d) {
        double acc = 0.0;
        for (int n = 0; n < N; ++n) {
            const int m = n + d;
            if (m < 0 || m >= N) continue;
            acc += static_cast<double>(L[static_cast<std::size_t>(n)]) *
                   static_cast<double>(R[static_cast<std::size_t>(m)]);
        }
        if (acc > best) { best = acc; bestLag = d; }
    }
    return bestLag;
}

// Drive a fresh binaural-enabled engine. ADM /aed azimuth (LEFT-positive) for
// object 0, optional /ypr head pose, then settle and capture the binaural bus.
// Returns the measured L/R cross-correlation lag.
static int run(float adm_az_deg, bool send_ypr, float ypr_yaw_deg,
               double& energy_out) {
    constexpr int N = 8;
    constexpr int B = 256;
    spe::core::SpatialEngine engine(0);
    engine.setLayout(ring8());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) + "/synthetic_itd_pm90.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(48000.0, B);

    // Object 0 active at the requested ADM azimuth (decode negates: app_az=-adm).
    engine.oscBackend().injectPacket(
        std::span<const uint8_t>(oscFFF("/adm/obj/0/aed", adm_az_deg, 0.f, 0.05f)));
    // Also send the explicit ADM active flag (separate from /aed move).
    {
        // /adm/obj/0/active ,i 1
        std::vector<uint8_t> p;
        auto pushPadded = [&](const std::string& s) {
            for (char ch : s) p.push_back(static_cast<uint8_t>(ch));
            p.push_back(0); while (p.size() % 4 != 0) p.push_back(0);
        };
        pushPadded("/adm/obj/0/active"); pushPadded(",i");
        int32_t v = 1;
        p.push_back((v>>24)&0xFF); p.push_back((v>>16)&0xFF);
        p.push_back((v>>8)&0xFF); p.push_back(v&0xFF);
        engine.oscBackend().injectPacket(std::span<const uint8_t>(p));
    }
    if (send_ypr) {
        engine.oscBackend().injectPacket(
            std::span<const uint8_t>(oscFFF("/ypr", ypr_yaw_deg, 0.f, 0.f)));
    }

    std::vector<std::vector<float>> bufs(static_cast<std::size_t>(N), std::vector<float>(B, 0.f));
    std::vector<float*> ptrs(static_cast<std::size_t>(N));
    for (int s = 0; s < N; ++s) ptrs[static_cast<std::size_t>(s)] = bufs[static_cast<std::size_t>(s)].data();

    // Settle: HRTF crossfade + GainRamp need a few blocks after setDirection.
    for (int b = 0; b < 40; ++b) {
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = N;
        blk.num_frames = B;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);
    }

    const float* l = engine.binauralL();
    const float* r = engine.binauralR();
    CHECK(l != nullptr && r != nullptr, "binaural bus available (SOFA loaded)");
    std::vector<float> L(static_cast<std::size_t>(B)), R(static_cast<std::size_t>(B));
    double e = 0.0;
    for (int n = 0; n < B; ++n) {
        L[static_cast<std::size_t>(n)] = l ? l[n] : 0.f;
        R[static_cast<std::size_t>(n)] = r ? r[n] : 0.f;
        e += static_cast<double>(L[static_cast<std::size_t>(n)]) * L[static_cast<std::size_t>(n)]
           + static_cast<double>(R[static_cast<std::size_t>(n)]) * R[static_cast<std::size_t>(n)];
    }
    energy_out = e;
    return xcorrLag(L, R, 64);
}

int main() {
    // Anchor: a PHYSICALLY-right object (ADM az=-90 -> engine az=+90), no head
    // rotation. This is the ground-truth "R leads" lag for the fixture.
    double eRight = 0;
    const int lagRight = run(-90.f, /*ypr=*/false, 0.f, eRight);
    std::printf("[ypr] anchor: physical az=+90 (RIGHT), no head: lag=%d e=%.4g\n", lagRight, eRight);
    CHECK(eRight > 1e-9, "anchor renders non-silent binaural");
    CHECK(lagRight != 0, "anchor has a non-zero ITD (right ear leads)");

    // Symmetric anchor: physical left (ADM az=+90 -> engine az=-90).
    double eLeft = 0;
    const int lagLeft = run(+90.f, false, 0.f, eLeft);
    std::printf("[ypr] anchor: physical az=-90 (LEFT),  no head: lag=%d e=%.4g\n", lagLeft, eLeft);
    CHECK((lagRight > 0) != (lagLeft > 0), "left/right anchors have opposite ITD sign");

    // CORE: front object (az=0) + head yaw +90 must MATCH the physical-right
    // anchor — head rotation moved the front image to the RIGHT (R-louder/leads).
    double eYprR = 0;
    const int lagYprRight = run(0.f, /*ypr=*/true, +90.f, eYprR);
    std::printf("[ypr] front obj + /ypr +90 (head right): lag=%d e=%.4g\n", lagYprRight, eYprR);
    CHECK(eYprR > 1e-9, "ypr+90 renders non-silent binaural");
    CHECK((lagYprRight > 0) == (lagRight > 0),
          "P0 LOCK: /ypr +90 on a front object must lead the SAME ear as a "
          "physical-right source (head yaw +X shifts image RIGHT — NOT inverted)");
    CHECK(std::abs(lagYprRight - lagRight) <= 6,
          "ypr+90 ITD magnitude matches the +90 anchor (rotation lands on +90)");

    // Converse: front object + head yaw -90 must MATCH the physical-left anchor.
    double eYprL = 0;
    const int lagYprLeft = run(0.f, true, -90.f, eYprL);
    std::printf("[ypr] front obj + /ypr -90 (head left):  lag=%d e=%.4g\n", lagYprLeft, eYprL);
    CHECK((lagYprLeft > 0) == (lagLeft > 0),
          "/ypr -90 on a front object must lead the SAME ear as a physical-left source");
    CHECK((lagYprRight > 0) != (lagYprLeft > 0),
          "/ypr +90 and -90 produce opposite ITD (symmetric head rotation)");

    if (failures == 0) { std::printf("test_convergence_head_ypr_golden: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_head_ypr_golden: %d FAILURE(S)\n", failures);
    return 1;
}
