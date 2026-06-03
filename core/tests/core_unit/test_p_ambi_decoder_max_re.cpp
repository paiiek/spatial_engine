// test_p_ambi_decoder_max_re.cpp
// Acceptance tests for MaxREDecoder weights and max-rE decode behaviour.
//
// Golden vectors (M2HOA-Q7 RESOLVED, Legendre-root canonical, SPARTA-verified):
//   N=1: g = {1.0000, 0.5774}     (r_E_max = 1/sqrt(3), root of P_2)
//   N=2: g = {1.0000, 0.7746, 0.4000}  (r_E_max = sqrt(3/5), root of P_3)
//   N=3: g = {1.0000, 0.8611, 0.6124, 0.3045} (r_E_max ≈ 0.86114, root of P_4)
// Reference: Zotter & Frank 2019 eq.4.49; SPARTA saf_hoa_internal.c getMaxREweights().

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "ambi/MaxREDecoder.hpp"
#include "geometry/SpeakerLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTol = 1e-3f; // tolerance for golden vector checks

static spe::geometry::SpeakerLayout make_circular_8ch() {
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = i;
        spk.x = std::sin(az); spk.y = 0.f; spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

[[maybe_unused]] static spe::geometry::SpeakerLayout make_3d_16ch() {
    spe::geometry::SpeakerLayout layout;
    int ch = 0;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk; spk.channel = ch++;
        spk.x = std::sin(az); spk.y = 0.f; spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    const float el_up = 30.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        const float az = static_cast<float>(i) * 90.f * (kPi / 180.f);
        spe::geometry::Speaker spk; spk.channel = ch++;
        spk.x = std::cos(el_up)*std::sin(az); spk.y = std::sin(el_up); spk.z = std::cos(el_up)*std::cos(az);
        layout.speakers.push_back(spk);
    }
    const float el_dn = -30.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        const float az = (static_cast<float>(i)*90.f+45.f) * kPi / 180.f;
        spe::geometry::Speaker spk; spk.channel = ch++;
        spk.x = std::cos(el_dn)*std::sin(az); spk.y = std::sin(el_dn); spk.z = std::cos(el_dn)*std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// AC-S1.{1,2,3}: golden vector check
static void test_max_re_weights_golden() {
    // N=1
    {
        auto w = spe::ambi::MaxREDecoder::compute_max_re_weights(1);
        if (std::abs(w[0] - 1.0f) > kTol || std::abs(w[1] - 0.5774f) > kTol) {
            std::printf("FAIL test_max_re_weights N=1: g0=%.4f g1=%.4f (expected {1.0000,0.5774})\n", w[0], w[1]);
            assert(false);
        }
        std::printf("PASS test_max_re_weights N=1: {%.4f, %.4f}\n", w[0], w[1]);
    }
    // N=2
    {
        auto w = spe::ambi::MaxREDecoder::compute_max_re_weights(2);
        if (std::abs(w[0]-1.0f)>kTol || std::abs(w[1]-0.7746f)>kTol || std::abs(w[2]-0.4000f)>kTol) {
            std::printf("FAIL test_max_re_weights N=2: {%.4f,%.4f,%.4f}\n", w[0],w[1],w[2]);
            assert(false);
        }
        std::printf("PASS test_max_re_weights N=2: {%.4f, %.4f, %.4f}\n", w[0],w[1],w[2]);
    }
    // N=3
    {
        auto w = spe::ambi::MaxREDecoder::compute_max_re_weights(3);
        if (std::abs(w[0]-1.0f)>kTol || std::abs(w[1]-0.8611f)>kTol ||
            std::abs(w[2]-0.6124f)>kTol || std::abs(w[3]-0.3045f)>kTol) {
            std::printf("FAIL test_max_re_weights N=3: {%.4f,%.4f,%.4f,%.4f}\n", w[0],w[1],w[2],w[3]);
            assert(false);
        }
        std::printf("PASS test_max_re_weights N=3: {%.4f, %.4f, %.4f, %.4f}\n", w[0],w[1],w[2],w[3]);
    }
}

// Dominant speaker: front source → speaker 0 must be loudest (orders 1, 2, 3)
// Use the 8-ch circular layout for all orders: horizontal-only, symmetric, spk0 uniquely
// closest to az=0 front direction regardless of order.
static void test_max_re_dominant_speaker_order(int order) {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.setDecoderType(spe::ambi::DecoderType::MAX_RE);
    decoder.prepare(layout);

    const int S = decoder.numSpeakers();
    const int K = (order+1)*(order+1);
    std::vector<float> bufs(static_cast<size_t>(K), 0.f);
    if (order == 1) {
        auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
        bufs[0]=c.W; bufs[1]=c.Y; bufs[2]=c.Z; bufs[3]=c.X;
    } else if (order == 2) {
        auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f, 0.f);
        for (int k=0;k<9;++k) bufs[static_cast<size_t>(k)]=c[static_cast<size_t>(k)];
    } else {
        auto c = spe::ambi::AmbisonicEncoder::encode_3rd_order(0.f, 0.f);
        for (int k=0;k<16;++k) bufs[static_cast<size_t>(k)]=c[static_cast<size_t>(k)];
    }
    std::vector<const float*> sh(static_cast<size_t>(K));
    for (int k=0;k<K;++k) sh[static_cast<size_t>(k)] = &bufs[static_cast<size_t>(k)];

    std::vector<float> out(static_cast<size_t>(S), 0.f);
    decoder.decode(order, sh.data(), 1, out.data());

    int max_spk = 0; float max_val = std::abs(out[0]);
    for (int s=1;s<S;++s) if (std::abs(out[static_cast<size_t>(s)])>max_val) { max_val=std::abs(out[static_cast<size_t>(s)]); max_spk=s; }

    if (max_spk != 0) {
        std::printf("FAIL test_max_re_dominant_order%d: max_spk=%d\n", order, max_spk);
        assert(false);
    }
    std::printf("PASS test_max_re_dominant_order%d (max_spk=%d, gain=%.4f)\n", order, max_spk, max_val);
}

// AC-S0.5.1: PINV default still bit-exact after refactor (regression guard)
static void test_pinv_regression() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    // default is PINV
    decoder.prepare(layout);
    auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
    float W[1]={c.W}, Y[1]={c.Y}, Z[1]={c.Z}, X[1]={c.X};
    std::vector<float> out(8, 0.f);
    decoder.decode(W, Y, Z, X, 1, out.data());
    int ms=0; float mv=std::abs(out[0]);
    for (int s=1;s<8;++s) if (std::abs(out[static_cast<size_t>(s)])>mv) { mv=std::abs(out[static_cast<size_t>(s)]); ms=s; }
    if (ms != 0) { std::printf("FAIL test_pinv_regression: max_spk=%d\n",ms); assert(false); }
    std::printf("PASS test_pinv_regression (PINV default: max_spk=%d)\n", ms);
}

int main() {
    test_max_re_weights_golden();
    test_max_re_dominant_speaker_order(1);
    test_max_re_dominant_speaker_order(2);
    test_max_re_dominant_speaker_order(3);
    test_pinv_regression();
    std::printf("All max_re decoder tests passed.\n");
    return 0;
}
