// test_p_ambi_decoder.cpp
// Acceptance tests for AmbiDecoder pseudo-inverse decoding (orders 1, 2, 3).

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kPi = 3.14159265358979323846f;

// 8-speaker horizontal circular layout. Speaker i at az = i * 45°, el = 0.
static spe::geometry::SpeakerLayout make_circular_8ch() {
    spe::geometry::SpeakerLayout layout;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = i;
        spk.x = std::sin(az);
        spk.y = 0.f;
        spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// 16-speaker layout: 8 horizontal at az=i*45°, el=0; 4 upper at az=i*90°, el=+30°;
// 4 lower at az=i*90°+45°, el=-30°. Provides 3D coverage for 2nd/3rd-order decoding.
static spe::geometry::SpeakerLayout make_3d_16ch() {
    spe::geometry::SpeakerLayout layout;
    int ch = 0;
    for (int i = 0; i < 8; ++i) {
        const float az = static_cast<float>(i) * 45.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = ch++;
        spk.x = std::sin(az); spk.y = 0.f; spk.z = std::cos(az);
        layout.speakers.push_back(spk);
    }
    const float el_up = 30.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        const float az = static_cast<float>(i) * 90.f * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = ch++;
        spk.x = std::cos(el_up) * std::sin(az);
        spk.y = std::sin(el_up);
        spk.z = std::cos(el_up) * std::cos(az);
        layout.speakers.push_back(spk);
    }
    const float el_down = -30.f * kPi / 180.f;
    for (int i = 0; i < 4; ++i) {
        const float az = (static_cast<float>(i) * 90.f + 45.f) * (kPi / 180.f);
        spe::geometry::Speaker spk;
        spk.channel = ch++;
        spk.x = std::cos(el_down) * std::sin(az);
        spk.y = std::sin(el_down);
        spk.z = std::cos(el_down) * std::cos(az);
        layout.speakers.push_back(spk);
    }
    return layout;
}

// 1st-order: encode (az=0, el=0) → decode → speaker 0 (front) dominant.
static void test_ambi_decoder_dominant_speaker() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    auto coeffs = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
    float W[1] = {coeffs.W};
    float Y[1] = {coeffs.Y};
    float Z[1] = {coeffs.Z};
    float X[1] = {coeffs.X};

    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(N), 0.f);
    decoder.decode(W, Y, Z, X, 1, out.data());

    int max_spk = 0;
    float max_val = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (v > max_val) { max_val = v; max_spk = s; }
    }
    if (max_spk != 0) {
        std::printf("FAIL test_ambi_decoder_dominant_speaker: max_spk=%d\n", max_spk);
        assert(false);
    }
    std::printf("PASS test_ambi_decoder_dominant_speaker (max_spk=%d, gain=%.4f)\n",
                max_spk, max_val);
}

// Omni input (W=1, rest 0) → all speakers receive equal absolute gain.
static void test_ambi_decoder_omni() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    float W[1] = {1.f}, Y[1] = {0.f}, Z[1] = {0.f}, X[1] = {0.f};
    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(N), 0.f);
    decoder.decode(W, Y, Z, X, 1, out.data());

    const float ref = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (std::abs(v - ref) > 1e-4f) {
            std::printf("FAIL test_ambi_decoder_omni: spk0=%.6f spk%d=%.6f\n", ref, s, v);
            assert(false);
        }
    }
    std::printf("PASS test_ambi_decoder_omni (uniform gain=%.6f across %d speakers)\n",
                ref, N);
}

// 2nd-order on 16-spk 3D layout: source at (az=0, el=0) →
// speaker 0 (front) dominant, with stricter concentration than 1st order.
static void test_ambi_decoder_2nd_order_dominant() {
    auto layout = make_3d_16ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    const float az = 0.f, el = 0.f;
    auto c2 = spe::ambi::AmbisonicEncoder::encode_2nd_order(az, el);
    std::vector<float> bufs(9, 0.f);
    for (int k = 0; k < 9; ++k) bufs[static_cast<size_t>(k)] = c2[static_cast<size_t>(k)];
    const float* sh[9];
    for (int k = 0; k < 9; ++k) sh[k] = &bufs[static_cast<size_t>(k)];

    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(N), 0.f);
    decoder.decode(2, sh, 1, out.data());

    int max_spk = 0;
    float max_val = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (v > max_val) { max_val = v; max_spk = s; }
    }
    if (max_spk != 0) {
        std::printf("FAIL test_ambi_decoder_2nd_order_dominant: max_spk=%d (expected 0)\n  gains:", max_spk);
        for (int s = 0; s < N; ++s) std::printf(" %.4f", out[static_cast<size_t>(s)]);
        std::printf("\n");
        assert(false);
    }
    std::printf("PASS test_ambi_decoder_2nd_order_dominant (max_spk=%d, gain=%.4f)\n",
                max_spk, max_val);
}

// 3rd-order: same 16-spk 3D layout, sharper directivity than 2nd order.
static void test_ambi_decoder_3rd_order_dominant() {
    auto layout = make_3d_16ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    const float az = 0.f, el = 0.f;
    auto c3 = spe::ambi::AmbisonicEncoder::encode_3rd_order(az, el);
    std::vector<float> bufs(16, 0.f);
    for (int k = 0; k < 16; ++k) bufs[static_cast<size_t>(k)] = c3[static_cast<size_t>(k)];
    const float* sh[16];
    for (int k = 0; k < 16; ++k) sh[k] = &bufs[static_cast<size_t>(k)];

    const int N = decoder.numSpeakers();
    std::vector<float> out(static_cast<size_t>(N), 0.f);
    decoder.decode(3, sh, 1, out.data());

    int max_spk = 0;
    float max_val = std::abs(out[0]);
    for (int s = 1; s < N; ++s) {
        const float v = std::abs(out[static_cast<size_t>(s)]);
        if (v > max_val) { max_val = v; max_spk = s; }
    }
    if (max_spk != 0) {
        std::printf("FAIL test_ambi_decoder_3rd_order_dominant: max_spk=%d (expected 0)\n  gains:", max_spk);
        for (int s = 0; s < N; ++s) std::printf(" %.4f", out[static_cast<size_t>(s)]);
        std::printf("\n");
        assert(false);
    }
    std::printf("PASS test_ambi_decoder_3rd_order_dominant (max_spk=%d, gain=%.4f)\n",
                max_spk, max_val);
}

// 2nd vs 1st order on the 3D layout: higher order should concentrate energy
// more tightly toward the dominant speaker (gain ratio ≥ 1.0×).
static void test_ambi_decoder_order_concentration() {
    auto layout = make_3d_16ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);

    const int N = decoder.numSpeakers();
    auto compute_max = [&](int order) -> float {
        std::vector<float> bufs;
        std::vector<const float*> sh;
        if (order == 1) {
            auto c = spe::ambi::AmbisonicEncoder::encode_1st_order(0.f, 0.f);
            bufs = {c.W, c.Y, c.Z, c.X};
        } else if (order == 2) {
            auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(0.f, 0.f);
            bufs.assign(c.begin(), c.end());
        } else {
            auto c = spe::ambi::AmbisonicEncoder::encode_3rd_order(0.f, 0.f);
            bufs.assign(c.begin(), c.end());
        }
        sh.resize(bufs.size());
        for (size_t i = 0; i < bufs.size(); ++i) sh[i] = &bufs[i];
        std::vector<float> out(static_cast<size_t>(N), 0.f);
        decoder.decode(order, sh.data(), 1, out.data());
        float m = 0.f;
        for (int s = 0; s < N; ++s) m = std::max(m, std::abs(out[static_cast<size_t>(s)]));
        return m;
    };

    const float m1 = compute_max(1);
    const float m2 = compute_max(2);
    const float m3 = compute_max(3);
    if (!(m2 >= m1 - 1e-4f) || !(m3 >= m2 - 1e-4f)) {
        std::printf("FAIL test_ambi_decoder_order_concentration: m1=%.4f m2=%.4f m3=%.4f\n",
                    m1, m2, m3);
        assert(false);
    }
    std::printf("PASS test_ambi_decoder_order_concentration (m1=%.4f → m2=%.4f → m3=%.4f)\n",
                m1, m2, m3);
}

// OSC: /sys/ambi_order ,i 2  → encode → decode → SysAmbiOrder with order=2.
static void test_osc_ambi_order_roundtrip() {
    spe::ipc::Command cmd;
    cmd.tag = spe::ipc::CommandTag::SysAmbiOrder;
    spe::ipc::PayloadSysAmbiOrder p;
    p.order = 2;
    cmd.payload = p;
    cmd.seq = 7; cmd.id = 42;

    spe::ipc::CommandDecoder enc, dec;
    std::vector<uint8_t> bytes;
    if (!enc.encode(cmd, bytes)) {
        std::printf("FAIL test_osc_ambi_order_roundtrip: encode failed\n");
        assert(false);
    }
    auto cmd2 = dec.decode(bytes);
    if (cmd2.tag != spe::ipc::CommandTag::SysAmbiOrder) {
        std::printf("FAIL test_osc_ambi_order_roundtrip: tag mismatch (%d)\n",
                    static_cast<int>(cmd2.tag));
        assert(false);
    }
    auto* p2 = std::get_if<spe::ipc::PayloadSysAmbiOrder>(&cmd2.payload);
    if (!p2 || p2->order != 2) {
        std::printf("FAIL test_osc_ambi_order_roundtrip: payload mismatch\n");
        assert(false);
    }
    std::printf("PASS test_osc_ambi_order_roundtrip (order=2)\n");
}

int main() {
    test_ambi_decoder_dominant_speaker();
    test_ambi_decoder_omni();
    test_ambi_decoder_2nd_order_dominant();
    test_ambi_decoder_3rd_order_dominant();
    test_ambi_decoder_order_concentration();
    test_osc_ambi_order_roundtrip();
    std::printf("All ambi_decoder tests passed.\n");
    return 0;
}
