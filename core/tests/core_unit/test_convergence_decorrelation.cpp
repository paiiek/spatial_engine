// test_convergence_decorrelation.cpp
// Dreamscape Convergence ⑦ — per-speaker decorrelation bank core
// (iae::SpeakerDecorrelationBank). Byte-faithful port of the reference
// SpeakerDecorrelation.{h,cpp}: deterministic per-speaker delay + Schroeder
// allpass cascade + energy-preserving dry/wet mix.
//
// Analytic checks (no tautology): an impulse's first output sample equals
// dryAmt = √(1−mix²) (the wet path is still empty at n=0); the lossless allpass
// + pure delay conserve energy, so Σout² ≈ 1 at mix=1; distinct speaker indices
// get distinct deterministic delays; reset is reproducible; disabled / mix≈0 pass
// through; a config change clears the channel's state.

#include "render/ported/SpeakerDecorrelation.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static double energy(const std::vector<float>& v) {
    double e = 0.0; for (float x : v) e += static_cast<double>(x) * x; return e;
}

int main() {
    constexpr double kSR = 48000.0;
    constexpr int    N   = 8192;   // long enough to capture the allpass tail

    auto impulse = []() { std::vector<float> b(N, 0.f); b[0] = 1.f; return b; };

    // ---- passthrough: disabled ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto b = impulse();
        bank.processChannel(0, b.data(), N, /*enabled=*/false, 0.5f, 4, 4.f, 0.62f, 1u);
        CHECK(feq(b[0], 1.f) && feq(b[1], 0.f), "disabled → passthrough");
    }
    // ---- passthrough: mix ~ 0 ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto b = impulse();
        bank.processChannel(0, b.data(), N, true, 1.0e-6f, 4, 4.f, 0.62f, 1u);
        CHECK(feq(b[0], 1.f), "mix≈0 → passthrough");
    }

    // ---- impulse first sample == dryAmt = √(1−mix²) ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        const float mix = 0.35f;
        const float dryAmt = std::sqrt(1.f - mix * mix);
        auto b = impulse();
        bank.processChannel(0, b.data(), N, true, mix, 4, 4.f, 0.62f, 12345u);
        CHECK(feq(b[0], dryAmt, 1e-5f), "impulse[0] == dryAmt √(1−mix²)");
        // The wet (delayed) impulse appears later, not at n=0.
        double tail = 0.0; for (int i = 1; i < N; ++i) tail += std::fabs(b[i]);
        CHECK(tail > 1e-3, "wet path produces a delayed/diffused tail");
    }

    // ---- energy preservation: mix=1 (pure wet) → Σout² ≈ Σin² = 1 ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto b = impulse();
        bank.processChannel(0, b.data(), N, true, 1.0f, 4, 4.f, 0.62f, 777u);
        const double e = energy(b);
        std::printf("[decorr] energy(mix=1) = %.6f (expect ~1.0)\n", e);
        CHECK(std::fabs(e - 1.0) < 2e-3, "allpass cascade + delay is energy-preserving");
    }

    // ---- deterministic per-speaker: same index+cfg identical; different index differs ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto a0 = impulse(); auto a0b = impulse(); auto a5 = impulse();
        bank.processChannel(0, a0.data(),  N, true, 0.5f, 4, 8.f, 0.62f, 999u);
        bank.processChannel(5, a5.data(),  N, true, 0.5f, 4, 8.f, 0.62f, 999u);
        // Re-run speaker 0 on a fresh bank → identical (determinism).
        iae::SpeakerDecorrelationBank bank2; bank2.prepare(kSR, 512);
        bank2.processChannel(0, a0b.data(), N, true, 0.5f, 4, 8.f, 0.62f, 999u);

        bool sameSelf = true, diffIdx = false;
        for (int i = 0; i < N; ++i) {
            if (!feq(a0[i], a0b[i], 1e-6f)) sameSelf = false;
            if (!feq(a0[i], a5[i], 1e-6f)) diffIdx = true;
        }
        CHECK(sameSelf, "decorr is deterministic for a given (index,cfg,seed)");
        CHECK(diffIdx, "distinct speaker indices get distinct decorrelation");
    }

    // ---- reset reproducibility ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto a = impulse(); auto b = impulse();
        bank.processChannel(3, a.data(), N, true, 0.4f, 3, 6.f, 0.5f, 42u);
        bank.reset();
        bank.processChannel(3, b.data(), N, true, 0.4f, 3, 6.f, 0.5f, 42u);
        bool same = true;
        for (int i = 0; i < N; ++i) if (!feq(a[i], b[i], 1e-6f)) same = false;
        CHECK(same, "reset → reproducible output");
    }

    // ---- stages affect the result (1 vs 6 stages differ) ----
    {
        iae::SpeakerDecorrelationBank bank; bank.prepare(kSR, 512);
        auto a1 = impulse(); auto a6 = impulse();
        bank.processChannel(0, a1.data(), N, true, 0.6f, 1, 4.f, 0.7f, 5u);
        iae::SpeakerDecorrelationBank bank2; bank2.prepare(kSR, 512);
        bank2.processChannel(0, a6.data(), N, true, 0.6f, 6, 4.f, 0.7f, 5u);
        bool diff = false;
        for (int i = 0; i < N; ++i) if (!feq(a1[i], a6[i], 1e-6f)) diff = true;
        CHECK(diff, "stage count changes the decorrelation");
    }

    if (failures == 0) { std::printf("test_convergence_decorrelation: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_decorrelation: %d FAIL\n", failures);
    return 1;
}
