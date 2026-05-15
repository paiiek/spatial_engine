// test_b2_layout_change_does_not_reinit_vs.cpp
//
// v0.5 P4 (A4 clarif): the B2 chain (vs_layout_, vs_hrir_L/R_) is a function
// only of the loaded .speh, independent of the physical SpeakerLayout. A
// runtime layout swap must NOT mutate B2 state.
//
// Architecture invariant proof:
//   * BinauralMonitor never receives the physical SpeakerLayout — its
//     constructor + initialize(cfg) only consume sofaPath, sampleRate,
//     blockSize. So any "layout change" path in the engine can only
//     re-trigger BinauralMonitor::initialize(cfg) with the same sofaPath,
//     and the B2 cache must rebuild to bit-identical contents.
//   * The test runs initialize() twice on independent monitors with the
//     same sofa fixture and asserts:
//       1. b2HrirLength(i) identical for all i ∈ [0..24).
//       2. vs HRIR L/R arrays byte-identical.
//   * It also runs a "layout swap simulation" on a single monitor by
//     re-initialising it with two different block sizes (which is the
//     primary trigger for a SpatialEngine::prepareToPlay() rebuild) and
//     asserts the B2 HRIR cache stays bit-identical across the rebuild.

#include "output_backend/BinauralMonitor.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#define SPE_FIXTURES_DIR "./fixtures"
#endif

#define REQUIRE(cond)                                                  \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr,                                        \
                         "FAIL: %s (line %d)\n", #cond, __LINE__);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

namespace {

struct Snapshot {
    std::vector<int>                lens;     // 24
    std::vector<std::vector<float>> hL, hR;   // 24 × kOlaMaxIRLength
};

Snapshot capture(const spe::output::BinauralMonitor& mon)
{
    Snapshot s;
    s.lens.resize(24);
    s.hL.resize(24);
    s.hR.resize(24);
    for (int i = 0; i < 24; ++i) {
        s.lens[static_cast<std::size_t>(i)] = mon.b2HrirLength(i);
        const float* L = mon.b2HrirLeft(i);
        const float* R = mon.b2HrirRight(i);
        s.hL[static_cast<std::size_t>(i)].assign(
            L, L + spe::hrtf::kOlaMaxIRLength);
        s.hR[static_cast<std::size_t>(i)].assign(
            R, R + spe::hrtf::kOlaMaxIRLength);
    }
    return s;
}

bool equal(const Snapshot& a, const Snapshot& b)
{
    if (a.lens != b.lens) return false;
    for (int i = 0; i < 24; ++i) {
        if (a.hL[static_cast<std::size_t>(i)]
            != b.hL[static_cast<std::size_t>(i)]) return false;
        if (a.hR[static_cast<std::size_t>(i)]
            != b.hR[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}

} // namespace

int main()
{
    const std::string sofa = std::string(SPE_FIXTURES_DIR) + "/synthetic_min.speh";
    constexpr float sr = 48000.f;

    // ─── Determinism across independent monitor instances ──────────────
    spe::output::BinauralMonitor monA, monB;
    spe::output::BinauralMonitor::Config cfg;
    cfg.sofaPath   = sofa;
    cfg.sampleRate = sr;

    cfg.blockSize = 64;
    REQUIRE(monA.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    cfg.blockSize = 128;  // different block size simulates host re-arrange
    REQUIRE(monB.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);

    const Snapshot sa = capture(monA);
    const Snapshot sb = capture(monB);
    REQUIRE(equal(sa, sb));

    // ─── Single-monitor re-init (the SpatialEngine prepareToPlay path) ─
    spe::output::BinauralMonitor monC;
    cfg.blockSize = 64;
    REQUIRE(monC.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    const Snapshot s_pre = capture(monC);

    // Run a few audio blocks (the physical layout doesn't exist for
    // BinauralMonitor; the runtime swap is just the engine calling
    // prepareToPlay → initialize() again).
    std::vector<float> mono(64, 0.5f);
    std::vector<float> L(64, 0.f), R(64, 0.f);
    monC.setDirection(0, 0.f, 0.f);
    for (int b = 0; b < 8; ++b)
        monC.processBlockForObject(0, mono.data(), 64, L.data(), R.data());

    cfg.blockSize = 256;
    REQUIRE(monC.initialize(cfg) == spe::output::BinauralMonitor::InitResult::Ok);
    const Snapshot s_post = capture(monC);

    REQUIRE(equal(s_pre, s_post));

    std::puts("PASS test_b2_layout_change_does_not_reinit_vs");
    return 0;
}
