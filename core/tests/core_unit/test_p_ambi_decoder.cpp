// test_p_ambi_decoder.cpp
// Acceptance tests for AmbiDecoder (1st-order mode-matching projection).

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "geometry/SpeakerLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kPi = 3.14159265358979323846f;

// Build an 8-speaker circular horizontal layout.
// Speaker i at az = i * 45 degrees, el = 0.
// Cartesian: x = sin(az), z = cos(az), y = 0  (unit sphere, x=right, z=front).
// Speakers:  0=front(az=0), 1=az45, 2=az90(right), 3=az135,
//            4=az180(back),  5=az-135, 6=az-90(left), 7=az-45.
static spe::geometry::SpeakerLayout make_circular_8ch() {
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = i;
        spk.x = std::sin(az);   // right
        spk.y = 0.f;            // horizontal
        spk.z = std::cos(az);   // front
        layout.speakers.push_back(spk);
    }
    return layout;
}

// Test: encode az=0, el=0 → decode → speaker 0 (at az=0, front) has max gain.
static void test_ambi_decoder_dominant_speaker() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    const int num_samples = 1;
    // az=0, el=0: W=1, X=cos(el)*cos(az)=1 (front), Y=cos(el)*sin(az)=0, Z=sin(el)=0
    auto coeffs = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
    // AmbiCoeffs1st fields: .W .X .Y .Z — decode() args order: W, Y, Z, X
    float W[1] = {coeffs.W};
    float Y[1] = {coeffs.Y};
    float Z[1] = {coeffs.Z};
    float X[1] = {coeffs.X};

    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(num_samples * N), 0.f);
    decoder.decode(W, Y, Z, X, num_samples, out.data());

    // Find speaker with max absolute gain
    int max_spk = 0;
    float max_val = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (v > max_val) { max_val = v; max_spk = s; }
    }

    // Speaker 0 is at az=0 (front) — the source direction. Must have max gain.
    if (max_spk != 0) {
        std::printf("FAIL test_ambi_decoder_dominant_speaker: max_spk=%d (expected 0)\n  gains:", max_spk);
        for (int s = 0; s < N; ++s) std::printf(" %.4f", out[static_cast<size_t>(s)]);
        std::printf("\n");
        assert(false);
    }
    std::printf("PASS test_ambi_decoder_dominant_speaker (max_spk=%d, gain=%.4f)\n", max_spk, max_val);
}

// Test: omni signal (W=1, X=Y=Z=0) → all speakers receive equal absolute gain.
static void test_ambi_decoder_omni() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    const int num_samples = 1;
    float W[1] = {1.f};
    float Y[1] = {0.f};
    float Z[1] = {0.f};
    float X[1] = {0.f};

    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(num_samples * N), 0.f);
    decoder.decode(W, Y, Z, X, num_samples, out.data());

    // All speakers must have the same absolute gain.
    const float ref = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (std::abs(v - ref) > 1e-5f) {
            std::printf("FAIL test_ambi_decoder_omni: spk0=%.6f spk%d=%.6f\n", ref, s, v);
            assert(false);
        }
    }
    std::printf("PASS test_ambi_decoder_omni (uniform gain=%.6f across %d speakers)\n", ref, N);
}

int main() {
    test_ambi_decoder_dominant_speaker();
    test_ambi_decoder_omni();
    std::printf("All ambi_decoder tests passed.\n");
    return 0;
}
