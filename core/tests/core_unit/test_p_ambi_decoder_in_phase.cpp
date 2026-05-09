// test_p_ambi_decoder_in_phase.cpp
// Acceptance tests for InPhaseDecoder.
// Reference: Daniel 2000 §3.30; SPARTA getInPhaseweights(); Heller et al. 2014 AES.

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "ambi/InPhaseDecoder.hpp"
#include "geometry/SpeakerLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTol = 1e-4f;

static spe::geometry::SpeakerLayout make_circular_8ch() {
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i)*45.f*(kPi/180.f);
        spe::geometry::Speaker spk; spk.channel = i;
        spk.x = std::sin(az); spk.y = 0.f; spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// AC-S2.7.0: raw g_0 = 1/(N+1) sanity
static void test_in_phase_raw_kernel() {
    for (int N = 1; N <= 3; ++N) {
        auto w = spe::ambi::InPhaseDecoder::compute_in_phase_weights_raw(N);
        float expected = 1.0f / static_cast<float>(N + 1);
        if (std::abs(w[0] - expected) > kTol) {
            std::printf("FAIL test_in_phase_raw_kernel N=%d: g0_raw=%.6f (expected 1/(N+1)=%.6f)\n",
                        N, w[0], expected);
            assert(false);
        }
        std::printf("PASS test_in_phase_raw_kernel N=%d: g0_raw=%.6f (= 1/%d)\n", N, w[0], N+1);
    }
}

// AC-S2.7.{1,2,3}: golden vectors (Daniel 2000, tol 1e-4)
static void test_in_phase_weights_golden() {
    // N=1: {1.0, 0.3333}
    {
        auto w = spe::ambi::InPhaseDecoder::compute_in_phase_weights(1);
        if (std::abs(w[0]-1.f)>kTol || std::abs(w[1]-0.3333f)>kTol) {
            std::printf("FAIL in_phase N=1: {%.4f,%.4f} (expected {1.0,0.3333})\n", w[0], w[1]);
            assert(false);
        }
        std::printf("PASS in_phase N=1: {%.4f, %.4f}\n", w[0], w[1]);
    }
    // N=2: {1.0, 0.5, 0.1}
    {
        auto w = spe::ambi::InPhaseDecoder::compute_in_phase_weights(2);
        if (std::abs(w[0]-1.f)>kTol || std::abs(w[1]-0.5f)>kTol || std::abs(w[2]-0.1f)>kTol) {
            std::printf("FAIL in_phase N=2: {%.4f,%.4f,%.4f} (expected {1.0,0.5,0.1})\n",w[0],w[1],w[2]);
            assert(false);
        }
        std::printf("PASS in_phase N=2: {%.4f, %.4f, %.4f}\n", w[0],w[1],w[2]);
    }
    // N=3: {1.0, 0.6, 0.2, 0.02857}
    {
        auto w = spe::ambi::InPhaseDecoder::compute_in_phase_weights(3);
        if (std::abs(w[0]-1.f)>kTol || std::abs(w[1]-0.6f)>kTol ||
            std::abs(w[2]-0.2f)>kTol || std::abs(w[3]-0.02857f)>2e-4f) {
            std::printf("FAIL in_phase N=3: {%.5f,%.5f,%.5f,%.5f}\n",w[0],w[1],w[2],w[3]);
            assert(false);
        }
        std::printf("PASS in_phase N=3: {%.5f, %.5f, %.5f, %.5f}\n", w[0],w[1],w[2],w[3]);
    }
}

// Dominant speaker: front source → speaker 0 loudest
// Use 8-ch circular layout for all orders: horizontal-only, symmetric, spk0 uniquely
// closest to az=0 regardless of order.
static void test_in_phase_dominant_speaker_order(int order) {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.setDecoderType(spe::ambi::DecoderType::IN_PHASE);
    decoder.prepare(layout);

    const int S = decoder.numSpeakers();
    const int K = (order+1)*(order+1);
    std::vector<float> bufs(static_cast<size_t>(K), 0.f);
    if (order == 1) {
        auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f,0.f);
        bufs[0]=c.W; bufs[1]=c.Y; bufs[2]=c.Z; bufs[3]=c.X;
    } else if (order == 2) {
        auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f,0.f);
        for (int k=0;k<9;++k) bufs[static_cast<size_t>(k)]=c[static_cast<size_t>(k)];
    } else {
        auto c = spe::ambi::AmbisonicEncoder::encode_3rd_order(0.f,0.f);
        for (int k=0;k<16;++k) bufs[static_cast<size_t>(k)]=c[static_cast<size_t>(k)];
    }
    std::vector<const float*> sh(static_cast<size_t>(K));
    for (int k=0;k<K;++k) sh[static_cast<size_t>(k)] = &bufs[static_cast<size_t>(k)];

    std::vector<float> out(static_cast<size_t>(S), 0.f);
    decoder.decode(order, sh.data(), 1, out.data());

    int ms=0; float mv=std::abs(out[0]);
    for (int s=1;s<S;++s) if (std::abs(out[static_cast<size_t>(s)])>mv) { mv=std::abs(out[static_cast<size_t>(s)]); ms=s; }

    if (ms != 0) {
        std::printf("FAIL test_in_phase_dominant_order%d: max_spk=%d\n", order, ms);
        assert(false);
    }
    std::printf("PASS test_in_phase_dominant_order%d (max_spk=%d, gain=%.4f)\n", order, ms, mv);
}

int main() {
    test_in_phase_raw_kernel();
    test_in_phase_weights_golden();
    test_in_phase_dominant_speaker_order(1);
    test_in_phase_dominant_speaker_order(2);
    test_in_phase_dominant_speaker_order(3);
    std::printf("All in-phase decoder tests passed.\n");
    return 0;
}
