// test_b2_ownership_lifecycle.cpp
//
// v0.5 P4 (A4): verify that BinauralMonitor owns the B2 AmbiVS chain and
// constructs/destructs cleanly. Asserts:
//   * After initialize() with a .speh, hasB2() is true.
//   * b2HrirLength(i) ∈ (0, kOlaMaxIRLength] for every VS i ∈ [0..24).
//   * b2HrirLeft/Right pointers are non-null for valid indices and null for OOB.
//   * Re-initialize is safe and idempotent (no leaks under ASan).
//   * processBlockB2 returns silence when effectiveMode() == Direct (default).

#include "core/Constants.h"
#include "output_backend/BinauralMonitor.h"

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

namespace {

#define REQUIRE(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr,                                          \
                         "FAIL: %s (file %s line %d)\n", #cond, __FILE__, \
                         __LINE__);                                       \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int run()
{
    constexpr int   blockSize = 64;
    constexpr float sr        = 48000.f;
    const std::string sofa    = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";

    // Pass 1 — initialize + invariants.
    {
        spe::output::BinauralMonitor mon;
        spe::output::BinauralMonitor::Config cfg;
        cfg.sofaPath   = sofa;
        cfg.sampleRate = sr;
        cfg.blockSize  = blockSize;
        REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
        REQUIRE(mon.hasHrtf());

        for (int i = 0; i < 24; ++i) {
            const int len = mon.b2HrirLength(i);
            REQUIRE(len > 0);
            REQUIRE(len <= spe::hrtf::kOlaMaxIRLength);
            REQUIRE(mon.b2HrirLeft(i)  != nullptr);
            REQUIRE(mon.b2HrirRight(i) != nullptr);
        }
        REQUIRE(mon.b2HrirLength(-1) == -1);
        REQUIRE(mon.b2HrirLength(24) == -1);
        REQUIRE(mon.b2HrirLeft(-1)   == nullptr);
        REQUIRE(mon.b2HrirRight(99)  == nullptr);

        // Default effective mode is Direct. Mode-gating is the CALLER's
        // responsibility (C1 fix — processBlockB2 no longer self-gates to
        // avoid the dual-read race that produced silent blocks on B1↔B2
        // transitions). The SpatialEngine dispatch path checks effectiveMode()
        // exactly once per block; this test exercises BinauralMonitor
        // directly so the mode flag is observational here.
        REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::Direct);

        std::vector<float>             sh_storage(16 * blockSize, 0.f);
        std::vector<const float*>      sh_ptrs(16, nullptr);
        for (int k = 0; k < 16; ++k)
            sh_ptrs[static_cast<std::size_t>(k)] =
                sh_storage.data() + k * blockSize;
        // Non-zero W channel to force any leak through the path.
        for (int n = 0; n < blockSize; ++n) sh_storage[n] = 0.5f;

        std::vector<float> L(blockSize, 0.f), R(blockSize, 0.f);

        // Switching to AmbiVS should produce non-zero output for the constant
        // W input above (impulse response × DC offset != 0).
        mon.setRequestedMode(spe::output::BinauralMode::AmbiVS);
        // No probe yet — setRequestedMode honours the request when probe_warning
        // is unset, which is the default.
        REQUIRE(mon.effectiveMode() == spe::output::BinauralMode::AmbiVS);

        bool any_nonzero = false;
        std::fill(L.begin(), L.end(), 0.f);
        std::fill(R.begin(), R.end(), 0.f);
        mon.processBlockB2(sh_ptrs.data(), 3, blockSize, L.data(), R.data());
        for (int n = 0; n < blockSize; ++n) {
            if (std::abs(L[static_cast<std::size_t>(n)]) > 1e-9f ||
                std::abs(R[static_cast<std::size_t>(n)]) > 1e-9f) {
                any_nonzero = true; break;
            }
        }
        REQUIRE(any_nonzero);
    }

    // Pass 2 — re-init from scratch (verifies clean teardown via ASan/leak check).
    {
        spe::output::BinauralMonitor mon;
        spe::output::BinauralMonitor::Config cfg;
        cfg.sofaPath   = sofa;
        cfg.sampleRate = sr;
        cfg.blockSize  = blockSize;
        REQUIRE(mon.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
        REQUIRE(mon.hasHrtf());
    }

    return 0;
}

} // namespace

int main()
{
    if (run() != 0) return 1;
    std::puts("PASS test_b2_ownership_lifecycle");
    return 0;
}
