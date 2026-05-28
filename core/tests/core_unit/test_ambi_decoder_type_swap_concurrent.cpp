// core/tests/core_unit/test_ambi_decoder_type_swap_concurrent.cpp
//
// v0.8 audit P1.1 — concurrency stress test for AmbiDecoder runtime
// decoder-type switch (DSP-1 / M2HOA-Q14).
//
// The pre-P1.1 layout was a single-buffered std::array<std::vector<float>,
// MAX_ORDER>; calling decoder.prepare() from a control tick while the
// audio thread was inside decode() would REALLOCATE the buffer the audio
// thread was mid-read on — classic use-after-free / torn read.
//
// This test hammers RAPID back-to-back decoder-type rebuilds on one
// thread against a tight concurrent decode() loop on another thread.
// Without the lock-free double-buffer (P1.1), TSan or a debug allocator
// would detect the UAF; even without sanitizers the read-after-free path
// would frequently observe NaN/garbage in `out`, which we assert against.
//
// The control thread switches FASTER than the production 1 Hz cadence
// (here ~10kHz: a tight loop with no sleep). This guarantees the test
// would FAIL if the production timing-slack invariant ever degraded
// (i.e. apply rate ≥ audio-block rate) — i.e. NOT a vacuous pass.
//
// See AmbiDecoder.h BINDING INVARIANT for the 2-slot quiescence rationale.

#include "ambi/AmbiDecoder.h"
#include "ambi/AmbisonicEncoder.h"
#include "geometry/SpeakerLayout.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

spe::geometry::SpeakerLayout make_circular_8ch() {
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

} // namespace

int main() {
    auto layout = make_circular_8ch();
    spe::ambi::AmbiDecoder decoder;
    decoder.prepare(layout);  // initial slot publish

    // Stress duration. Long enough to interleave many swaps under
    // scheduling jitter; short enough to keep ctest snappy.
    constexpr int kDecodeIters    = 200000;
    constexpr int kSwapIters      = 50000;
    constexpr int kBlockSamples   = 64;
    constexpr int kOrder          = 2;
    constexpr int kK              = (kOrder + 1) * (kOrder + 1); // 9

    // Pre-build SH planar input (constant signals so output norms are
    // deterministic — any NaN/garbage indicates a torn read or UAF).
    std::vector<std::vector<float>> sh_planar(kK, std::vector<float>(kBlockSamples, 0.f));
    // Encode an object at az=π/4, el=0 → constant SH coefficients filling
    // all 9 ACN channels with sensible non-zero values.
    auto c = spe::ambi::AmbisonicEncoder::encode_2nd_order(kPi / 4.f, 0.f);
    for (int k = 0; k < kK; ++k) {
        for (int n = 0; n < kBlockSamples; ++n)
            sh_planar[static_cast<size_t>(k)][static_cast<size_t>(n)] =
                c[static_cast<size_t>(k)];
    }
    std::vector<const float*> sh_ptrs(kK);
    for (int k = 0; k < kK; ++k) sh_ptrs[k] = sh_planar[k].data();

    std::atomic<bool> stop_flag{false};
    std::atomic<unsigned long long> bad_samples{0};
    std::atomic<unsigned long long> decode_count{0};

    // Audio-thread imitator: tight decode() loop with NaN/inf assertion.
    std::thread audio_thread([&]() {
        std::vector<float> out(static_cast<size_t>(8) * kBlockSamples, 0.f);
        for (int i = 0; i < kDecodeIters; ++i) {
            decoder.decode(kOrder, sh_ptrs.data(), kBlockSamples, out.data());
            ++decode_count;
            for (float v : out) {
                if (std::isnan(v) || std::isinf(v) || std::fabs(v) > 1e6f) {
                    ++bad_samples;
                }
            }
            if (stop_flag.load(std::memory_order_relaxed)) break;
        }
    });

    // Control-thread imitator: rapid back-to-back decoder-type swaps.
    // Cycle PINV → MAX_RE → ALLRAD → EPAD → IN_PHASE → PINV …
    // No sleep — switch faster than 1 Hz to ensure the timing-slack
    // invariant is actively exercised. If the apply rate ever climbed
    // above the audio block rate, the 2-slot quiescence would no longer
    // hold and TSan/the bad_samples counter would light up.
    std::thread control_thread([&]() {
        static constexpr spe::ambi::DecoderType types[] = {
            spe::ambi::DecoderType::PINV,
            spe::ambi::DecoderType::MAX_RE,
            spe::ambi::DecoderType::ALLRAD,
            spe::ambi::DecoderType::EPAD,
            spe::ambi::DecoderType::IN_PHASE,
        };
        for (int i = 0; i < kSwapIters; ++i) {
            decoder.setDecoderType(types[i % 5]);
            decoder.prepare(layout);  // rebuild → publish via double-buffer
        }
        stop_flag.store(true, std::memory_order_relaxed);
    });

    audio_thread.join();
    control_thread.join();

    const unsigned long long bad = bad_samples.load();
    const unsigned long long ndec = decode_count.load();

    if (bad != 0) {
        std::fprintf(stderr,
            "FAIL test_ambi_decoder_type_swap_concurrent: "
            "bad_samples=%llu decode_count=%llu (torn read / UAF detected)\n",
            bad, ndec);
        return 1;
    }
    std::printf(
        "OK  test_ambi_decoder_type_swap_concurrent: "
        "decode_count=%llu swaps=%d bad_samples=0\n",
        ndec, kSwapIters);
    return 0;
}
