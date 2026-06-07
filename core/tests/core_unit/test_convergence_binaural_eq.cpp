// test_convergence_binaural_eq.cpp
// Phase 2.5 (Dreamscape Convergence) — binaural monitor 5-band peak EQ.
//
// Two layers, mirroring the room-biquad split:
//   (A) RoomBiquad::setPeak coefficient byte-faithfulness — an analytic
//       |H(e^jw)| check straight from the realised coeffs (non-circular: it does
//       NOT re-derive makePeakFilter). An RBJ peaking EQ has |H|=gainFactor at
//       the center frequency and |H|->1 at DC/Nyquist; a 0 dB band is exactly
//       unity (b==a). Plus reset() determinism.
//   (B) Engine in-process golden — the FULL wire path /sys/binaural_eq -> decode
//       -> FIFO -> applyBinauralEq -> post-chain EQ on the binaural bus. Two
//       objects emit distinct tones (obj0=110 Hz, obj4=330 Hz). A narrow -18 dB
//       cut at 110 Hz must drop the 110 Hz energy on the binaural bus while
//       leaving 330 Hz essentially untouched (frequency-selective, wired, and —
//       running under the audioBlock RT no-alloc scope — allocation-free).

#include "core/SpatialEngine.h"
#include "geometry/SpeakerLayout.h"
#include "audio_io/AudioCallback.h"
#include "render/ported/RoomBiquad.h"

#include <array>
#include <cmath>
#include <complex>
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

// ---------------------------------------------------------------------------
// (A) setPeak coefficient checks
// ---------------------------------------------------------------------------

// |H(e^jw)| from raw coeffs {b0,b1,b2,a1,a2} (a0=1).
static double mag_at(const iae::RoomBiquad& f, double freqHz) {
    const double w = 2.0 * kPi * freqHz / kSR;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num =
        (double) f.b0() + (double) f.b1() * z1 + (double) f.b2() * z2;
    const std::complex<double> den =
        1.0 + (double) f.a1() * z1 + (double) f.a2() * z2;
    return std::abs(num) / std::abs(den);
}

static void test_setpeak_coeffs() {
    // (A1) +12 dB bell at 1 kHz, Q=2: center gain == 10^(12/20), DC/Nyquist ~1.
    {
        iae::RoomBiquad f; f.setPeak(kSR, 1000.f, 2.f, 12.f);
        const double peak = std::pow(10.0, 12.0 / 20.0); // 3.981...
        CHECK(std::abs(mag_at(f, 1000.0) - peak) < 1e-3, "setPeak +12dB center gain == 10^(dB/20)");
        CHECK(std::abs(mag_at(f, 20.0)    - 1.0)  < 2e-2, "setPeak +12dB ~unity at low freq");
        CHECK(std::abs(mag_at(f, 23000.0) - 1.0)  < 2e-2, "setPeak +12dB ~unity near Nyquist");
    }
    // (A2) -18 dB cut at 200 Hz, Q=3: center gain == 10^(-18/20).
    {
        iae::RoomBiquad f; f.setPeak(kSR, 200.f, 3.f, -18.f);
        const double dip = std::pow(10.0, -18.0 / 20.0); // 0.1259
        CHECK(std::abs(mag_at(f, 200.0) - dip) < 1e-3, "setPeak -18dB center gain == 10^(dB/20)");
        CHECK(mag_at(f, 4000.0) > 0.9, "setPeak -18dB leaves an octave+ away ~untouched");
    }
    // (A3) 0 dB band is an exact unity passthrough: b==a, |H|==1 everywhere.
    {
        iae::RoomBiquad f; f.setPeak(kSR, 1250.f, 1.f, 0.f);
        CHECK(std::abs(f.b0() - 1.f) < 1e-6f, "0 dB peak b0==1");
        CHECK(std::abs(f.b1() - f.a1()) < 1e-6f, "0 dB peak b1==a1");
        CHECK(std::abs(f.b2() - f.a2()) < 1e-6f, "0 dB peak b2==a2");
        // Unity only to float precision: b0 = (1+a)/(1+a) carries ~1 ULP of
        // round-off, so |H| sits within ~1e-5 of 1 (−100 dB) — not bit-exact.
        CHECK(std::abs(mag_at(f, 500.0) - 1.0) < 1e-5, "0 dB peak |H|==1 (to float precision)");
    }
    // (A4) reset() determinism: identical output for repeated impulse responses.
    {
        iae::RoomBiquad f; f.setPeak(kSR, 800.f, 1.5f, 6.f);
        std::array<float, 16> a{}, b{};
        a[0] = f.processSample(1.f); for (int i = 1; i < 16; ++i) a[i] = f.processSample(0.f);
        f.reset();
        b[0] = f.processSample(1.f); for (int i = 1; i < 16; ++i) b[i] = f.processSample(0.f);
        bool same = true; for (int i = 0; i < 16; ++i) same &= (a[i] == b[i]);
        CHECK(same, "setPeak reset() gives a deterministic impulse response");
    }
}

// ---------------------------------------------------------------------------
// (B) engine in-process golden
// ---------------------------------------------------------------------------

static spe::geometry::SpeakerLayout ring8() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "eq_ring"; l.regularity = Regularity::CIRCULAR;
    for (int i = 0; i < 8; ++i) {
        const float az = (-kPiF) + 2.f * kPiF * static_cast<float>(i) / 8.f;
        Speaker s; s.channel = i + 1;
        s.x = std::sin(az); s.y = 0.f; s.z = std::cos(az);
        l.speakers.push_back(s);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(i);
    }
    return l;
}

static void pushPadded(std::vector<uint8_t>& p, const std::string& s) {
    for (char ch : s) p.push_back(static_cast<uint8_t>(ch));
    p.push_back(0);
    while (p.size() % 4 != 0) p.push_back(0);
}
static void pushI(std::vector<uint8_t>& p, int32_t v) {
    p.push_back((v>>24)&0xFF); p.push_back((v>>16)&0xFF);
    p.push_back((v>>8)&0xFF); p.push_back(v&0xFF);
}
static void pushF(std::vector<uint8_t>& p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
    p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
}
static std::vector<uint8_t> oscAed(int obj, float az, float el, float dist) {
    std::vector<uint8_t> p;
    pushPadded(p, "/adm/obj/" + std::to_string(obj) + "/aed");
    pushPadded(p, ",fff"); pushF(p, az); pushF(p, el); pushF(p, dist); return p;
}
static std::vector<uint8_t> oscActive(int obj, int on) {
    std::vector<uint8_t> p;
    pushPadded(p, "/adm/obj/" + std::to_string(obj) + "/active");
    pushPadded(p, ",i"); pushI(p, on); return p;
}
static std::vector<uint8_t> oscEqEnable(int on) {
    std::vector<uint8_t> p;
    pushPadded(p, "/sys/binaural_eq/enable"); pushPadded(p, ",i"); pushI(p, on); return p;
}
static std::vector<uint8_t> oscEqBand(int band, float f, float gdb, float q) {
    std::vector<uint8_t> p;
    pushPadded(p, "/sys/binaural_eq/band");
    pushPadded(p, ",ifff"); pushI(p, band); pushF(p, f); pushF(p, gdb); pushF(p, q); return p;
}

// Goertzel power of a real signal at target frequency.
static double goertzel(const std::vector<float>& x, double freqHz) {
    const int N = static_cast<int>(x.size());
    const double w = 2.0 * kPi * freqHz / kSR;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int n = 0; n < N; ++n) {
        s0 = static_cast<double>(x[static_cast<std::size_t>(n)]) + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Drive a fresh binaural engine with obj0 (110 Hz) + obj4 (330 Hz) front, run
// the EQ either off or with a -18 dB cut at 110 Hz, and return the bus energy at
// 110 and 330 Hz. Accumulates many blocks for frequency resolution.
static void run_eq(bool eq_on, double& e110, double& e330) {
    constexpr int N = 8;
    constexpr int B = 512;
    spe::core::SpatialEngine engine(0);
    engine.setLayout(ring8());
    engine.setBinauralSofaPath(std::string(SPE_FIXTURES_DIR) + "/synthetic_itd_pm90.speh");
    engine.setBinauralEnabled(true);
    engine.prepareToPlay(kSR, B);

    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscAed(0, 0.f, 0.f, 0.05f)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscActive(0, 1)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscAed(4, 0.f, 0.f, 0.05f)));
    engine.oscBackend().injectPacket(std::span<const uint8_t>(oscActive(4, 1)));
    if (eq_on) {
        engine.oscBackend().injectPacket(std::span<const uint8_t>(oscEqEnable(1)));
        // Narrow, deep cut centred on the 110 Hz tone.
        engine.oscBackend().injectPacket(std::span<const uint8_t>(oscEqBand(0, 110.f, -18.f, 4.f)));
    }

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

    for (int b = 0; b < 40; ++b) block();              // settle HRTF xfade + ramps

    std::vector<float> L; L.reserve(64 * B);
    for (int b = 0; b < 64; ++b) {                     // 32768 samples for resolution
        block();
        const float* l = engine.binauralL();
        for (int n = 0; n < B; ++n) L.push_back(l ? l[n] : 0.f);
    }
    e110 = goertzel(L, 110.0);
    e330 = goertzel(L, 330.0);
}

static void test_engine_golden() {
    double off110 = 0, off330 = 0, on110 = 0, on330 = 0;
    run_eq(/*eq_on=*/false, off110, off330);
    run_eq(/*eq_on=*/true,  on110,  on330);
    std::printf("[eq] OFF: E110=%.4g E330=%.4g | ON(-18dB@110): E110=%.4g E330=%.4g\n",
                off110, off330, on110, on330);

    CHECK(off110 > 0 && off330 > 0, "baseline bus carries both tones");
    // The 110 Hz tone must be strongly attenuated by the cut.
    CHECK(on110 < 0.25 * off110, "EQ -18dB@110 cuts the 110 Hz tone (>~6 dB drop)");
    // The 330 Hz tone (an octave+ away) must be left essentially intact.
    CHECK(on330 > 0.6 * off330, "EQ cut at 110 leaves the 330 Hz tone ~untouched");
    // The relative balance must shift decisively toward 330 Hz.
    CHECK((on110 / on330) < 0.5 * (off110 / off330), "EQ shifts the 110/330 balance");
}

int main() {
    test_setpeak_coeffs();
    test_engine_golden();
    if (failures == 0) { std::printf("test_convergence_binaural_eq: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_binaural_eq: %d FAILURE(S)\n", failures);
    return 1;
}
