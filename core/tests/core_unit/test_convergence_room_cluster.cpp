// test_convergence_room_cluster.cpp
// Dreamscape Convergence ⑥e-2 — cluster diffusion core (iae::RoomCluster).
//
// The cluster is a pure FEEDFORWARD (FIR) diffuser: a single delay line summed
// through 6 triangular-weighted taps at geometric offsets. With no feedback the
// impulse response is finite — exactly the 6 taps — so we can assert its exact
// structure (the strongest byte-faithfulness check available without the JUCE
// reference build):
//   - finiteness & non-silence
//   - exactly 6 non-zero taps (feedforward, distinct offsets)
//   - tap weights in the reference 1:2:3:3:2:1 triangular ratio
//   - tap offsets form an arithmetic progression (common difference = step)
//   - diffusion01 scales the output gain monotonically
//   - a larger virtual volume pushes the first tap later (geometric d0)
//   - reset() determinism
//
// Reference: RoomEngine.cpp:605-647 (kClTri6 {1,2,3,3,2,1}, kClTri6Inv 1/12,
// clGain = (0.08 + 0.42*diff) * kClTri6Inv * 0.5, charM = 0.11*cbrt(V),
// d0 = round(charM/343 * sr * 0.2) clamped [8,1200], step = max(2, d0/10)).

#include "render/ported/RoomCluster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } } while(0)

static constexpr int kSR = 48000;

// Drive a unit impulse through `cl` for `n` samples (one process() call) and
// return the mono impulse response.
static std::vector<float> impulse_response(iae::RoomCluster& cl, int n) {
    std::vector<float> in((size_t) n, 0.f);
    std::vector<float> out((size_t) n, 0.f);
    in[0] = 1.f;
    cl.process(in.data(), n, out.data());
    return out;
}

int main() {
    static constexpr int kN = 2048;   // > max tap offset for the V values tested

    // --- Exact impulse-response structure (default params) --------------------
    {
        iae::RoomCluster cl;
        cl.prepare((double) kSR);
        CHECK(cl.ready());
        iae::RoomClusterParams p;          // diffusion01=0.48, V=630 (reference)
        cl.setParams(p);

        const std::vector<float> ir = impulse_response(cl, kN);

        // Collect non-zero taps (index, value).
        std::vector<int>   idx;
        std::vector<float> val;
        bool anyNonFinite = false;
        for (int i = 0; i < kN; ++i) {
            if (!std::isfinite(ir[(size_t) i])) anyNonFinite = true;
            if (std::abs(ir[(size_t) i]) > 1.0e-9f) { idx.push_back(i); val.push_back(ir[(size_t) i]); }
        }
        CHECK(!anyNonFinite);
        CHECK((int) idx.size() == 6);                 // feedforward: exactly 6 taps

        if (idx.size() == 6) {
            // Weights in 1:2:3:3:2:1 ratio: normalise by the smallest (corner) tap.
            const float base = val[0];
            CHECK(base > 0.f);
            const float ratio[6] = { val[0]/base, val[1]/base, val[2]/base,
                                     val[3]/base, val[4]/base, val[5]/base };
            const float want[6]  = { 1.f, 2.f, 3.f, 3.f, 2.f, 1.f };
            for (int k = 0; k < 6; ++k) CHECK(std::abs(ratio[k] - want[k]) < 1.0e-3f);

            // Offsets are an arithmetic progression (equal spacing = step >= 2).
            const int step = idx[1] - idx[0];
            CHECK(step >= 2);
            for (int k = 1; k < 6; ++k) CHECK(idx[(size_t) k] - idx[(size_t) k-1] == step);

            // Absolute gain matches the reference clGain for the smallest tap.
            const float diff   = 0.48f;
            const float clGain = (0.08f + 0.42f * diff) * (1.f / 12.f) * 0.5f;
            CHECK(std::abs(base - clGain) < 1.0e-6f);   // weight 1 * clGain
            std::printf("[ir] taps=6 d0=%d step=%d base=%.6g (clGain=%.6g)\n",
                        idx[0], step, base, clGain);
        }
    }

    // --- diffusion01 scales output gain monotonically -------------------------
    {
        auto tap_gain = [&](float diff) {
            iae::RoomCluster cl; cl.prepare((double) kSR);
            iae::RoomClusterParams p; p.diffusion01 = diff; cl.setParams(p);
            const std::vector<float> ir = impulse_response(cl, kN);
            float peak = 0.f;
            for (float v : ir) peak = std::max(peak, std::abs(v));
            return peak;
        };
        const float gLo = tap_gain(0.1f);
        const float gHi = tap_gain(0.9f);
        CHECK(gHi > gLo);
        std::printf("[diffusion] g(0.1)=%.6g  g(0.9)=%.6g\n", gLo, gHi);
    }

    // --- larger virtual volume -> later first tap (geometric d0) --------------
    {
        auto first_tap = [&](float V) {
            iae::RoomCluster cl; cl.prepare((double) kSR);
            iae::RoomClusterParams p; p.virtualVolumeM3 = V; cl.setParams(p);
            const std::vector<float> ir = impulse_response(cl, kN);
            for (int i = 0; i < kN; ++i)
                if (std::abs(ir[(size_t) i]) > 1.0e-9f) return i;
            return -1;
        };
        const int tSmall = first_tap(200.f);
        const int tLarge = first_tap(5000.f);
        CHECK(tSmall > 0);
        CHECK(tLarge > tSmall);
        std::printf("[volume] firstTap(200)=%d  firstTap(5000)=%d\n", tSmall, tLarge);
    }

    // --- reset() determinism --------------------------------------------------
    {
        iae::RoomCluster cl; cl.prepare((double) kSR);
        iae::RoomClusterParams p; cl.setParams(p);
        const std::vector<float> a = impulse_response(cl, 512);
        cl.reset();
        const std::vector<float> b = impulse_response(cl, 512);
        double d = 0.0;
        for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
        CHECK(d < 1.0e-6);
        std::printf("[reset] sum|a-b|=%.3g\n", d);
    }

    if (failures == 0) std::printf("test_convergence_room_cluster: ALL PASS\n");
    return failures;
}
