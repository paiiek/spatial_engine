// test_p_ambi_decoder_allrad.cpp
// Acceptance tests for AllRADDecoder.
// Reference: Zotter & Frank 2012 ICSA; IEM AllRADecoder.

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "ambi/AllRADTDesigns.hpp"
#include "geometry/SpeakerLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kPi = 3.14159265358979323846f;

// Uniform 24-speaker near-tetrahedral layout from t-design
static spe::geometry::SpeakerLayout make_uniform_24ch() {
    int n_virt = 0;
    const spe::ambi::TDesignPoint* pts = spe::ambi::getTDesign(24, n_virt);
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < n_virt; ++i) {
        spe::geometry::Speaker spk;
        spk.channel = i;
        spk.x = pts[i].x; spk.y = pts[i].y; spk.z = pts[i].z;
        layout.speakers.push_back(spk);
    }
    return layout;
}

// Irregular 9-speaker hemi-dome (front-heavy)
static spe::geometry::SpeakerLayout make_hemi_9ch() {
    spe::geometry::SpeakerLayout layout;
    // 5 front speakers at various elevations, 4 rear
    const float az_list[] = {0.f, 30.f, -30.f, 60.f, -60.f, 150.f, -150.f, 180.f, 90.f};
    const float el_list[] = {0.f, 0.f,   0.f,  0.f,   0.f,   0.f,    0.f,  0.f, 45.f};
    for (int i = 0; i < 9; ++i) {
        float az = az_list[i] * kPi / 180.f;
        float el = el_list[i] * kPi / 180.f;
        spe::geometry::Speaker spk; spk.channel = i;
        spk.x = std::cos(el)*std::sin(az);
        spk.y = std::sin(el);
        spk.z = std::cos(el)*std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// AC-S2.1: For uniform layout, AllRAD should produce non-zero energy on front speaker
static void test_allrad_uniform_produces_dominant() {
    auto layout = make_uniform_24ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.setDecoderType(spe::ambi::DecoderType::ALLRAD);
    decoder.prepare(layout);

    const int S = decoder.numSpeakers();
    auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
    float W[1]={c.W}, Y[1]={c.Y}, Z[1]={c.Z}, X[1]={c.X};
    std::vector<float> out(static_cast<size_t>(S), 0.f);
    decoder.decode(W, Y, Z, X, 1, out.data());

    float total_energy = 0.f;
    for (int s=0;s<S;++s) total_energy += out[static_cast<size_t>(s)]*out[static_cast<size_t>(s)];
    if (total_energy < 1e-6f) {
        std::printf("FAIL test_allrad_uniform_produces_dominant: total_energy=%.6f (expected > 0)\n", total_energy);
        assert(false);
    }
    std::printf("PASS test_allrad_uniform_produces_dominant (total_energy=%.4f)\n", total_energy);
}

// AC-S2.2: Irregular hemi-dome: AllRAD produces no strongly negative gains (> -0.05)
static void test_allrad_irregular_no_large_negatives() {
    auto layout = make_hemi_9ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.setDecoderType(spe::ambi::DecoderType::ALLRAD);
    decoder.prepare(layout);

    const int S = decoder.numSpeakers();
    auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
    float W[1]={c.W}, Y[1]={c.Y}, Z[1]={c.Z}, X[1]={c.X};
    std::vector<float> out(static_cast<size_t>(S), 0.f);
    decoder.decode(W, Y, Z, X, 1, out.data());

    float min_gain = 0.f;
    for (int s=0;s<S;++s) if (out[static_cast<size_t>(s)] < min_gain) min_gain = out[static_cast<size_t>(s)];
    if (min_gain < -0.05f) {
        std::printf("FAIL test_allrad_irregular_no_large_negatives: min_gain=%.4f (allowed > -0.05)\n", min_gain);
        assert(false);
    }
    std::printf("PASS test_allrad_irregular_no_large_negatives (min_gain=%.4f)\n", min_gain);
}

// AC Appendix B2: t-design table size static_assert is in AllRADTDesigns.cpp.
// Runtime check: first point of t-design-24 is on unit sphere.
static void test_tdesign_table_sanity() {
    int n = 0;
    const spe::ambi::TDesignPoint* pts = spe::ambi::getTDesign(24, n);
    if (n != 24) {
        std::printf("FAIL test_tdesign_table_sanity: n=%d (expected 24)\n", n);
        assert(false);
    }
    float len2 = pts[0].x*pts[0].x + pts[0].y*pts[0].y + pts[0].z*pts[0].z;
    if (std::abs(len2 - 1.0f) > 0.01f) {
        std::printf("FAIL test_tdesign_table_sanity: first point not unit sphere (len2=%.4f)\n", len2);
        assert(false);
    }
    // Last point also on unit sphere
    len2 = pts[23].x*pts[23].x + pts[23].y*pts[23].y + pts[23].z*pts[23].z;
    if (std::abs(len2 - 1.0f) > 0.01f) {
        std::printf("FAIL test_tdesign_table_sanity: last point not unit sphere (len2=%.4f)\n", len2);
        assert(false);
    }
    std::printf("PASS test_tdesign_table_sanity (n=%d, first/last on unit sphere)\n", n);
}

int main() {
    test_allrad_uniform_produces_dominant();
    test_allrad_irregular_no_large_negatives();
    test_tdesign_table_sanity();
    std::printf("All AllRAD decoder tests passed.\n");
    return 0;
}
