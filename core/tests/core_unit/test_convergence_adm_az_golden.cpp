// test_convergence_adm_az_golden.cpp
// Dreamscape Convergence v1.0 — Phase 3.1: ADM-OSC azimuth L/R golden test.
//
// ADM-OSC azimuth is LEFT-positive (ITU-R BS.2076 / reference AdmOscProtocol.h:
// "front 0°, left +"); the mmhoa engine renders RIGHT-positive. The decode
// negates (app_az = -adm_az). This test proves the sign END-TO-END through the
// real OSC wire path: an ADM object at az=+30° (LEFT) must light the LEFT
// speakers more than the RIGHT, and az=-30° (RIGHT) must do the opposite. A
// missing or wrong-direction negation (the historical L/R-inversion bug class)
// fails this test.
//
// Path under test: injectPacket(/adm/obj/0/aed) -> CommandDecoder (negation) ->
// cmd_fifo_ -> audioBlock drain -> VBAP render -> per-speaker bus.

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

static int failures = 0;
#define CHECK(c, msg) do { if(!(c)){ std::fprintf(stderr,"FAIL: %s\n", msg); ++failures; } } while(0)

static constexpr float kPi = 3.14159265358979323846f;

// 8-speaker horizontal ring; speakers with x<0 are LEFT, x>0 are RIGHT.
static spe::geometry::SpeakerLayout ring8() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "adm_az_ring"; l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < 8; ++i) {
        const float az = (-kPi) + 2.f * kPi * static_cast<float>(i) / 8.f;
        Speaker s; s.channel = i + 1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);  // engine frame x=right
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}

// Minimal OSC encoder for /adm/obj/<id>/aed ,fff.
static std::vector<uint8_t> aedPacket(int obj_id, float az_deg, float el_deg, float dist_norm) {
    std::vector<uint8_t> p;
    auto pushPadded = [&](const std::string& s) {
        for (char c : s) p.push_back(static_cast<uint8_t>(c));
        p.push_back(0);
        while (p.size() % 4 != 0) p.push_back(0);
    };
    auto pushF = [&](float f) {
        uint32_t u; std::memcpy(&u, &f, 4);
        p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
        p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
    };
    pushPadded("/adm/obj/" + std::to_string(obj_id) + "/aed");
    pushPadded(",fff");
    pushF(az_deg); pushF(el_deg); pushF(dist_norm);
    return p;
}

// Drive the ADM object at the given ADM azimuth; return {leftEnergy, rightEnergy}.
static void run(float adm_az_deg, double& left_e, double& right_e) {
    const int N = 8;
    spe::core::SpatialEngine engine(0);
    auto layout = ring8();
    engine.setLayout(layout);
    engine.prepareToPlay(48000.0, 256);

    auto pkt = aedPacket(0, adm_az_deg, 0.f, 0.05f);  // near-field so dist gain is high
    engine.oscBackend().injectPacket(std::span<const uint8_t>(pkt));

    std::vector<std::vector<float>> bufs(static_cast<size_t>(N), std::vector<float>(256, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(N));
    for (int s = 0; s < N; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    left_e = right_e = 0.0;
    for (int b = 0; b < 40; ++b) {  // let the VBAP gain ramps settle
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = N;
        blk.num_frames = 256;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);
    }
    // Measure the last block's per-speaker energy, bucketed by speaker x sign.
    for (int s = 0; s < N; ++s) {
        double e = 0.0;
        for (int n = 0; n < 256; ++n) {
            const float v = bufs[static_cast<size_t>(s)][static_cast<size_t>(n)];
            e += static_cast<double>(v) * v;
        }
        const float x = layout.speakers[static_cast<size_t>(s)].x;
        if (x < -1e-3f) left_e += e;
        else if (x > 1e-3f) right_e += e;
    }
}

int main() {
    double L_left = 0, L_right = 0, R_left = 0, R_right = 0;

    // ADM +30° = LEFT (ADM left-positive) -> engine az<0 -> LEFT speakers dominate.
    run(+30.f, L_left, L_right);
    std::printf("[adm-az] ADM +30 (left):  left=%.4g  right=%.4g\n", L_left, L_right);
    CHECK(L_left > 1e-9, "ADM +30 renders non-silent");
    CHECK(L_left > 4.0 * L_right,
          "ADM az=+30 (LEFT) must light LEFT speakers >> RIGHT (negation correct)");

    // ADM -30° = RIGHT -> engine az>0 -> RIGHT speakers dominate (converse).
    run(-30.f, R_left, R_right);
    std::printf("[adm-az] ADM -30 (right): left=%.4g  right=%.4g\n", R_left, R_right);
    CHECK(R_right > 1e-9, "ADM -30 renders non-silent");
    CHECK(R_right > 4.0 * R_left,
          "ADM az=-30 (RIGHT) must light RIGHT speakers >> LEFT (sign symmetric)");

    if (failures == 0) { std::printf("test_convergence_adm_az_golden: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_adm_az_golden: %d FAILURE(S)\n", failures);
    return 1;
}
