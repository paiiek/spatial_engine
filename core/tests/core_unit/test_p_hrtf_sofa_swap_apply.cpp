// core/tests/core_unit/test_p_hrtf_sofa_swap_apply.cpp
//
// v0.9 Lane B (B-M2) — FUNCTIONAL test: a runtime SOFA hot-swap actually
// changes the live active table/tree, and a failing load leaves the active
// slot untouched (failure contract).
//
// Modeled on test_ambi_decoder_type_runtime_apply.cpp.
//
// Scenario:
//   1. initialize() BinauralMonitor with fixture A (synthetic_min.speh,
//      ir_len 64). Capture the az=+90 active-table ir_length + L/R onset (ITD).
//   2. setPendingSofaPath(fixtureB) + applyPendingSofaChange()
//      (== loadPendingSofa(fixtureB) — fixtureB = synthetic_swapB.speh,
//      ir_len 128, distinct per-receiver ITD).
//   3. Assert the active table CHANGED (ir_length 64→128 AND the az=+90 ITD
//      flipped/changed) and no NaN in the rendered B1 output.
//   4. Fabricate an oversized-ir_length .speh (ir_len 2048, unsupported) and
//      call loadPendingSofa() on it. Assert it returns false (failure code),
//      the active table is STILL fixtureB (untouched), and the failure-reason
//      drain reports "ir_length_unsupported".

#include "output_backend/BinauralMonitor.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef SPE_FIXTURES_DIR
#error "SPE_FIXTURES_DIR must be defined by CMake"
#endif

namespace {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kAz90    = kPi / 2.f;   // engine az=+90 (right side)
constexpr int   kBlock   = 64;
constexpr float kSr      = 48000.f;

std::string fixture(const char* name) {
    return std::string(SPE_FIXTURES_DIR) + "/" + name;
}

bool has_nan(const float* v, int n) {
    for (int i = 0; i < n; ++i)
        if (std::isnan(v[i]) || std::isinf(v[i])) return true;
    return false;
}

// Write a minimal .speh header with an UNSUPPORTED ir_length (2048 > 1024).
// loadSpeh validates ir_length before reading the body, so a header-only file
// is sufficient to exercise the IRLengthUnsupported failure path.
void write_oversized_speh(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    const char     magic[4]    = {'S', 'P', 'E', 'H'};
    const uint32_t n_positions = 1u;
    const uint32_t ir_length   = 2048u;  // > kOlaMaxIRLength (1024) — unsupported
    const uint32_t n_receivers = 2u;
    const float    sample_rate = kSr;
    const uint32_t reserved    = 0u;
    f.write(magic, 4);
    f.write(reinterpret_cast<const char*>(&n_positions), 4);
    f.write(reinterpret_cast<const char*>(&ir_length),   4);
    f.write(reinterpret_cast<const char*>(&n_receivers), 4);
    f.write(reinterpret_cast<const char*>(&sample_rate), 4);
    f.write(reinterpret_cast<const char*>(&reserved),    4);
    f.close();
}

} // namespace

int main() {
    using spe::output::BinauralMonitor;

    // ── (1) Initialise with fixture A.
    BinauralMonitor mon;
    BinauralMonitor::Config cfg;
    cfg.sofaPath   = fixture("synthetic_min.speh");
    cfg.sampleRate = kSr;
    cfg.blockSize  = kBlock;
    const auto ir = mon.initialize(cfg);
    if (ir != BinauralMonitor::InitResult::Ok) {
        std::fprintf(stderr, "FAIL: initialize(fixtureA) returned %d\n",
                     static_cast<int>(ir));
        return 1;
    }
    assert(mon.hasHrtf());

    const int irlenA = mon.activeSofaIrLengthForTest();
    int onsetLA = -1, onsetRA = -1;
    mon.activeSofaOnsetForTest(kAz90, 0.f, onsetLA, onsetRA);
    std::printf("fixtureA active: ir_length=%d az+90 onsetL=%d onsetR=%d (ITD L-R=%d)\n",
                irlenA, onsetLA, onsetRA, onsetLA - onsetRA);

    // Render a B1 block before the swap (sanity: no NaN).
    std::vector<float> mono(kBlock, 0.5f);
    std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
    mon.setDirection(0, kAz90, 0.f);
    mon.processBlockForObject(0, mono.data(), kBlock, L.data(), R.data());
    assert(!has_nan(L.data(), kBlock) && !has_nan(R.data(), kBlock));

    // ── (2) Swap to fixture B via the pending/apply control-tick path.
    mon.setPendingSofaPath(fixture("synthetic_swapB.speh"));
    const bool swapped = mon.applyPendingSofaChange();
    if (!swapped) {
        std::fprintf(stderr, "FAIL: applyPendingSofaChange(fixtureB) returned false\n");
        return 1;
    }

    // ── (3) Assert the active table changed.
    const int irlenB = mon.activeSofaIrLengthForTest();
    int onsetLB = -1, onsetRB = -1;
    mon.activeSofaOnsetForTest(kAz90, 0.f, onsetLB, onsetRB);
    std::printf("fixtureB active: ir_length=%d az+90 onsetL=%d onsetR=%d (ITD L-R=%d)\n",
                irlenB, onsetLB, onsetRB, onsetLB - onsetRB);

    if (irlenB == irlenA) {
        std::fprintf(stderr,
            "FAIL: ir_length did not change across swap (A=%d B=%d)\n",
            irlenA, irlenB);
        return 1;
    }
    if (irlenB != 128) {
        std::fprintf(stderr, "FAIL: fixtureB ir_length expected 128, got %d\n", irlenB);
        return 1;
    }
    const int itdA = onsetLA - onsetRA;
    const int itdB = onsetLB - onsetRB;
    if (itdA == itdB) {
        std::fprintf(stderr,
            "FAIL: az+90 ITD did not change across swap (A=%d B=%d)\n", itdA, itdB);
        return 1;
    }

    // B1 self-heal: render again, must re-look-up against the new active slot
    // and stay finite.
    mon.setDirection(0, kAz90, 0.f);
    mon.processBlockForObject(0, mono.data(), kBlock, L.data(), R.data());
    assert(!has_nan(L.data(), kBlock) && !has_nan(R.data(), kBlock));

    // ── (4) Failing load (oversized ir_length) leaves fixtureB active.
    const std::string bad = fixture("_oversized_tmp.speh");
    write_oversized_speh(bad);
    const bool ok = mon.loadPendingSofa(bad);
    std::remove(bad.c_str());
    if (ok) {
        std::fprintf(stderr, "FAIL: loadPendingSofa(oversized) returned true (should fail)\n");
        return 1;
    }
    // Active table must STILL be fixture B (untouched, no half-publish).
    const int irlenAfterFail = mon.activeSofaIrLengthForTest();
    if (irlenAfterFail != irlenB) {
        std::fprintf(stderr,
            "FAIL: failed load mutated active table (was %d, now %d)\n",
            irlenB, irlenAfterFail);
        return 1;
    }
    // Failure-reason drain reports the correct code.
    const std::string reason = mon.sofaLoadFailureReason();
    const bool drained = mon.drainSofaLoadFailedPending();
    if (!drained) {
        std::fprintf(stderr, "FAIL: sofa_load_failed_pending was not armed\n");
        return 1;
    }
    if (reason != "ir_length_unsupported") {
        std::fprintf(stderr,
            "FAIL: failure reason expected 'ir_length_unsupported', got '%s'\n",
            reason.c_str());
        return 1;
    }
    // Second drain is empty (one-shot).
    if (mon.drainSofaLoadFailedPending()) {
        std::fprintf(stderr, "FAIL: sofa_load_failed_pending double-fired\n");
        return 1;
    }

    std::printf(
        "OK  test_p_hrtf_sofa_swap_apply: A(ir=%d,ITD=%d) -> B(ir=%d,ITD=%d) swapped; "
        "oversized load rejected, fixtureB still active.\n",
        irlenA, itdA, irlenB, itdB);
    return 0;
}
