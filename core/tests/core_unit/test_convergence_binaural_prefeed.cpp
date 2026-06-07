// test_convergence_binaural_prefeed.cpp
// Phase 2.1 (Dreamscape Convergence) — binaural HRTF prefeed low-pass.
//
// The reference applies a first-order LP at 4200 Hz to the HRTF input feed
// (BinauralMonitorChain.cpp:106-125). mmhoa filters each active object's dry
// signal once per block into bin_prefeed_ (B1 and B2 both read it) and exposes
// the corner via /sys/binaural_prefeed (default 4200 Hz; a corner above Nyquist
// is an effective bypass).
//
// Two layers:
//   (A) OSC decode roundtrip for /sys/binaural_prefeed ,f.
//   (B) Engine in-process golden — the FULL wire path /sys/binaural_prefeed ->
//       decode -> SysBinauralPrefeed -> engine atomic -> audioBlock prefilter.
//       Two object tones (obj0=110 Hz, obj63=3575 Hz). Comparing a 4200 Hz
//       corner against a bypass corner (both runs share the SAME HRTF, so its
//       colouration cancels) the LP must attenuate the 3575 Hz tone while
//       leaving the 110 Hz tone essentially intact — and it runs under the
//       audioBlock RT no-alloc scope (the allocation-free gate).

#include "core/SpatialEngine.h"
#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
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
static constexpr double kSR = 48000.0;
static constexpr double kPi = 3.14159265358979323846;
static constexpr float  kPiF = 3.14159265358979323846f;

static void pushPadded(std::vector<uint8_t>& p, const std::string& s) {
    for (char ch : s) p.push_back(static_cast<uint8_t>(ch));
    p.push_back(0);
    while (p.size() % 4 != 0) p.push_back(0);
}
static void pushI(std::vector<uint8_t>& p, int32_t v) {
    p.push_back((v>>24)&0xFF); p.push_back((v>>16)&0xFF); p.push_back((v>>8)&0xFF); p.push_back(v&0xFF);
}
static void pushF(std::vector<uint8_t>& p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF); p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
}

// ---------------------------------------------------------------------------
// (A) OSC decode roundtrip
// ---------------------------------------------------------------------------
static void test_osc_roundtrip() {
    spe::ipc::CommandDecoder dec;
    {
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_prefeed");
        pushPadded(p, ",f"); pushF(p, 2500.f);
        auto c = dec.decode(std::span<const uint8_t>(p));
        CHECK(c.tag == spe::ipc::CommandTag::SysBinauralPrefeed, "prefeed -> SysBinauralPrefeed");
        if (c.tag == spe::ipc::CommandTag::SysBinauralPrefeed) {
            auto& pl = std::get<spe::ipc::PayloadSysBinauralPrefeed>(c.payload);
            CHECK(std::fabs(pl.cutoff_hz - 2500.f) < 1e-3f, "prefeed cutoff carried");
        }
    }
    {   // missing arg defaults to 4200
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_prefeed"); pushPadded(p, ",");
        auto c = dec.decode(std::span<const uint8_t>(p));
        auto& pl = std::get<spe::ipc::PayloadSysBinauralPrefeed>(c.payload);
        CHECK(std::fabs(pl.cutoff_hz - 4200.f) < 1e-3f, "prefeed no-arg defaults to 4200");
    }
    CHECK(dec.rejectCount() == 0, "no rejects for valid /sys/binaural_prefeed");
}

// ---------------------------------------------------------------------------
// (B) engine in-process golden
// ---------------------------------------------------------------------------
static spe::geometry::SpeakerLayout ring8() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "pf_ring"; l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < 8; ++i) {
        const float az = (-kPiF) + 2.f * kPiF * static_cast<float>(i) / 8.f;
        Speaker s; s.channel = i + 1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}
static std::vector<uint8_t> oscAed(int obj, float az, float el, float dist) {
    std::vector<uint8_t> p; pushPadded(p, "/adm/obj/" + std::to_string(obj) + "/aed");
    pushPadded(p, ",fff"); pushF(p, az); pushF(p, el); pushF(p, dist); return p;
}
static std::vector<uint8_t> oscActive(int obj, int on) {
    std::vector<uint8_t> p; pushPadded(p, "/adm/obj/" + std::to_string(obj) + "/active");
    pushPadded(p, ",i"); pushI(p, on); return p;
}
static std::vector<uint8_t> oscPrefeed(float fc) {
    std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_prefeed");
    pushPadded(p, ",f"); pushF(p, fc); return p;
}

static double goertzel(const std::vector<float>& x, double freqHz) {
    const int N = static_cast<int>(x.size());
    const double w = 2.0 * kPi * freqHz / kSR;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int n = 0; n < N; ++n) {
        const double s0 = static_cast<double>(x[static_cast<std::size_t>(n)]) + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// obj0 (110 Hz) + obj63 (3575 Hz) front; given prefeed cutoff. Returns bus
// energy at 110 and 3575 Hz.
static void run_pf(float cutoff_hz, double& e110, double& e3575) {
    constexpr int N = 8;
    constexpr int B = 512;
    spe::core::SpatialEngine engine(0);
    engine.setLayout(ring8());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) + "/synthetic_itd_pm90.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSR, B);

    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscAed(0, 0.f, 0.f, 0.05f)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscActive(0, 1)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscAed(63, 0.f, 0.f, 0.05f)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscActive(63, 1)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscPrefeed(cutoff_hz)));

    std::vector<std::vector<float>> bufs(static_cast<std::size_t>(N), std::vector<float>(B, 0.f));
    std::vector<float*> ptrs(static_cast<std::size_t>(N));
    for (int s = 0; s < N; ++s) ptrs[static_cast<std::size_t>(s)] = bufs[static_cast<std::size_t>(s)].data();
    auto block = [&]() {
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = N;
        blk.num_frames = B;
        blk.sample_rate = kSR;
        engine.audioBlock(blk);
    };
    for (int b = 0; b < 40; ++b) block();
    std::vector<float> L; L.reserve(64 * B);
    for (int b = 0; b < 64; ++b) {
        block();
        const float* l = engine.binauralL();
        for (int n = 0; n < B; ++n) L.push_back(l ? l[n] : 0.f);
    }
    e110  = goertzel(L, 110.0);
    e3575 = goertzel(L, 3575.0);
}

static void test_engine_golden() {
    double on110 = 0, on3575 = 0, by110 = 0, by3575 = 0;
    run_pf(4200.f,  on110, on3575);   // reference corner (LP engaged)
    run_pf(30000.f, by110, by3575);   // corner above Nyquist -> effective bypass
    std::printf("[prefeed] LP@4200: E110=%.4g E3575=%.4g | bypass: E110=%.4g E3575=%.4g\n",
                on110, on3575, by110, by3575);

    CHECK(by110 > 0 && by3575 > 0, "bypass run carries both tones");
    // 3575 Hz sits below the corner but in the rolloff: a 4200 Hz one-pole gives
    // ~-2.3 dB there (energy ~0.6 of bypass). Require a clear attenuation.
    CHECK(on3575 < 0.8 * by3575, "prefeed LP attenuates the 3575 Hz tone vs bypass");
    // 110 Hz is two decades below the corner -> essentially unity.
    CHECK(on110 > 0.9 * by110, "prefeed LP leaves the 110 Hz tone ~untouched");
    // And the LP must bend the high/low balance downward.
    CHECK((on3575 / on110) < 0.85 * (by3575 / by110), "prefeed shifts HF/LF balance down");
}

int main() {
    test_osc_roundtrip();
    test_engine_golden();
    if (failures == 0) { std::printf("test_convergence_binaural_prefeed: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_binaural_prefeed: %d FAILURE(S)\n", failures);
    return 1;
}
