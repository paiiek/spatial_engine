// test_convergence_binaural_delay.cpp
// Phase 2.4 (Dreamscape Convergence) — binaural monitor stereo delay ring.
//
// The reference delays the binaural L/R bus through a 65536-sample stereo ring,
// tap = binauralDelayMs (BinauralMonitorChain.cpp:132-154). mmhoa applies it on
// binaural_l_buf_/r_buf_ before the EQ and exposes the tap via /sys/binaural_delay.
//
// Two layers:
//   (A) OSC decode roundtrip for /sys/binaural_delay ,f.
//   (B) Engine in-process golden — capture the binaural bus from block 0 (the
//       onset transient) at tap=0 and tap=50 ms. The whole output envelope is
//       shifted right by the tap; cross-correlating the two ENERGY envelopes
//       (non-periodic, unlike the raw sine) recovers the delay = ~50 ms. Runs
//       under the audioBlock RT no-alloc scope (the allocation-free gate).

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
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_delay");
        pushPadded(p, ",f"); pushF(p, 12.5f);
        auto c = dec.decode(std::span<const uint8_t>(p));
        CHECK(c.tag == spe::ipc::CommandTag::SysBinauralDelay, "delay -> SysBinauralDelay");
        if (c.tag == spe::ipc::CommandTag::SysBinauralDelay) {
            auto& pl = std::get<spe::ipc::PayloadSysBinauralDelay>(c.payload);
            CHECK(std::fabs(pl.ms - 12.5f) < 1e-3f, "delay ms carried");
        }
    }
    {   // missing arg defaults to 0
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_delay"); pushPadded(p, ",");
        auto c = dec.decode(std::span<const uint8_t>(p));
        auto& pl = std::get<spe::ipc::PayloadSysBinauralDelay>(c.payload);
        CHECK(std::fabs(pl.ms - 0.f) < 1e-6f, "delay no-arg defaults to 0");
    }
    CHECK(dec.rejectCount() == 0, "no rejects for valid /sys/binaural_delay");
}

// ---------------------------------------------------------------------------
// (B) engine in-process golden
// ---------------------------------------------------------------------------
static spe::geometry::SpeakerLayout ring8() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "dl_ring"; l.regularity = Regularity::CIRCULAR;
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
static std::vector<uint8_t> oscDelay(float ms) {
    std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_delay");
    pushPadded(p, ",f"); pushF(p, ms); return p;
}

// Capture the binaural-L energy envelope from block 0 (the onset). hop-summed
// squared magnitude. tap applied before the first block so the whole envelope
// shifts right by the delay.
static std::vector<double> run_envelope(float delay_ms, int hop) {
    constexpr int N = 8;
    constexpr int B = 256;
    constexpr int BLOCKS = 90;   // ~480 ms — covers 50 ms tap + warmup + steady
    spe::core::SpatialEngine engine(0);
    engine.setLayout(ring8());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) + "/synthetic_itd_pm90.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSR, B);

    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscDelay(delay_ms)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscAed(0, 0.f, 0.f, 0.05f)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscActive(0, 1)));

    std::vector<std::vector<float>> bufs(static_cast<std::size_t>(N), std::vector<float>(B, 0.f));
    std::vector<float*> ptrs(static_cast<std::size_t>(N));
    for (int s = 0; s < N; ++s) ptrs[static_cast<std::size_t>(s)] = bufs[static_cast<std::size_t>(s)].data();

    std::vector<float> L; L.reserve(static_cast<std::size_t>(BLOCKS * B));
    for (int b = 0; b < BLOCKS; ++b) {
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = N;
        blk.num_frames = B;
        blk.sample_rate = kSR;
        engine.audioBlock(blk);
        const float* l = engine.binauralL();
        for (int n = 0; n < B; ++n) L.push_back(l ? l[n] : 0.f);
    }
    std::vector<double> env;
    for (std::size_t s = 0; s + static_cast<std::size_t>(hop) <= L.size(); s += static_cast<std::size_t>(hop)) {
        double e = 0;
        for (int k = 0; k < hop; ++k) { const double v = L[s + static_cast<std::size_t>(k)]; e += v * v; }
        env.push_back(e);
    }
    return env;
}

// Onset (in samples): the first hop whose energy exceeds 20 % of the run's
// peak. The whole output envelope is shifted right by the tap, so the onset
// moves by exactly the tap — and onset detection (unlike envelope xcorr) is not
// biased toward lag 0 by the long steady-state plateau.
static double onsetSample(const std::vector<double>& env, int hop) {
    double peak = 0; for (double v : env) peak = std::max(peak, v);
    if (peak <= 0) return -1;
    const double th = 0.2 * peak;
    for (std::size_t h = 0; h < env.size(); ++h)
        if (env[h] > th) return static_cast<double>(h) * hop;
    return -1;
}

static void test_engine_golden() {
    const int hop = 64;
    auto env0  = run_envelope(0.f,  hop);   // no delay
    auto env50 = run_envelope(50.f, hop);   // 50 ms = 2400 samples

    const double on0  = onsetSample(env0,  hop);
    const double on50 = onsetSample(env50, hop);
    const double shift = on50 - on0;
    const double expect = 0.050 * kSR;  // 2400 samples
    std::printf("[delay] onset: tap0=%.0f tap50=%.0f -> shift=%.0f samples (expect ~%.0f)\n",
                on0, on50, shift, expect);

    // Total energy must be preserved (delay reorders, does not attenuate).
    double e0 = 0, e50 = 0;
    for (double v : env0)  e0  += v;
    for (double v : env50) e50 += v;
    CHECK(on0 >= 0 && on50 >= 0, "both runs render a detectable onset");
    CHECK(std::fabs(shift - expect) < 256.0,
          "50 ms tap shifts the binaural onset by ~2400 samples");
    CHECK(e50 > 0.7 * e0, "delay preserves total energy (reorder, not attenuate)");
}

int main() {
    test_osc_roundtrip();
    test_engine_golden();
    if (failures == 0) { std::printf("test_convergence_binaural_delay: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_binaural_delay: %d FAILURE(S)\n", failures);
    return 1;
}
