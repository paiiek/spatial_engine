// core/src/hrtf/HrtfLookup.h
// Spherical nearest-neighbor HRTF direction lookup.
//
// v0.5 P2: primary path is KdTree3D::nearest (O(log N) unit-Cartesian).
//          Brute-force trig path remains exposed for parity tests
//          (`nearestPositionBruteForceForTest`).

#pragma once

#include "hrtf/KdTree3D.h"
#include "hrtf/SofaBinReader.h"

#include <array>
#include <atomic>
#include <string>

namespace spe::hrtf {

class KdTree3D;  // fwd (definition pulled in above for HrtfLookup storage)

// Returns index of the nearest position in `table` for (az_deg, el_deg).
// Brute-force great-circle distance (O(N) trig). Retained as ground-truth
// for parity tests against KdTree3D; production callers should use
// `lookupHrtf` or query a KdTree3D directly.
int nearestPositionBruteForceForTest(const HrtfTable& table, float az_deg, float el_deg);

// Back-compat shim — same as the brute-force above. Existing call-sites that
// reference `nearestPosition()` continue to compile; v0.5 lookups via
// BinauralMonitor go through the KdTree3D path instead.
int nearestPosition(const HrtfTable& table, float az_deg, float el_deg);

// HRIR pair returned by lookupHrtf.
// Engine convention: az=+90 deg → LEFT (AmbiX).
// SOFA convention:   az=+90 deg → LEFT (AES69) — no sign flip needed.
struct HrtfPair {
    const float* left;
    const float* right;
    int          ir_length;
};

// Lookup the nearest HRIR pair for (az_rad, el_rad) in engine convention.
// Brute-force path (back-compat). For O(log N) lookup, build a KdTree3D
// once and use `lookupHrtfFromTree`.
HrtfPair lookupHrtf(const HrtfTable& table, float az_rad, float el_rad);

// O(log N) HRIR pair lookup via a pre-built KdTree3D.
// `tree` must have been built from `table`. Alloc-free.
HrtfPair lookupHrtfFromTree(const HrtfTable& table,
                            const KdTree3D&  tree,
                            float            az_rad,
                            float            el_rad) noexcept;

// ─────────────────────────────────────────────────────────────────────────
// v0.9 Lane B (B-M2) — owning 2-slot SOFA table/tree double buffer.
//
// HrtfLookup holds TWO {HrtfTable, KdTree3D} slots plus a single
// std::atomic<int> active_sofa_slot_. It mirrors the v0.8 P1.1 publish/consume
// protocol used by AmbiDecoder (see core/src/ambi/AmbiDecoder.h:16-25
// BINDING INVARIANT): the control thread loads + builds into the INACTIVE
// slot then publishes via store-release; the audio thread loads active_sofa_slot_
// acquire EXACTLY ONCE per block and reads that slot's published table/tree.
//
// The double buffer covers the table/tree ONLY. The per-object B1 convolver
// banks (BinauralMonitor::obj_slots_) are NOT doubled — they self-heal on the
// audio thread next block via setDirection()'s existing 2-block crossfade. The
// B2 VS bank is a SEPARATE double buffer (active_vs_slot_ in BinauralMonitor)
// that mirrors this same publish/consume protocol independently.
//
// BINDING INVARIANT (carried from AmbiDecoder.h): the 2-slot scheme is safe
// ONLY because the control-thread rebuild cadence (~1 Hz, the
// spatial_engine_core.cpp control tick) is ≥ one audio-block period. The
// inactive slot is quiescent ~93 blocks before reuse. The Relacy test
// (test_hrtf_sofa_swap_race) MUST drive rapid back-to-back publishes so it
// would FAIL if this slack were ever removed (no vacuous pass).
//
// CONTROL THREAD ONLY methods (loadIntoInactive / publish) ALLOCATE
// (loadSpeh + KdTree3D::build). NEVER call them from the audio thread.
class HrtfLookup {
public:
    HrtfLookup() = default;

    // CONTROL THREAD ONLY — ALLOCATES (loadSpeh + trees_[inactive].build()).
    // Loads `path` (validated against `expected_sr`) into the INACTIVE slot
    // and builds its KD-tree. Does NOT publish — call publish() on success.
    // On any failure the inactive slot is left in whatever partial state
    // loadSpeh produced, but the ACTIVE slot is untouched; callers must NOT
    // publish() on a failure result.
    SpehResult loadIntoInactive(const std::string& path, float expected_sr) {
        const int inactive = 1 - active_sofa_slot_.load(std::memory_order_relaxed);
        const std::size_t s = static_cast<std::size_t>(inactive);
        SpehResult res = loadSpeh(path, expected_sr, tables_[s]);
        if (res != SpehResult::Ok) return res;
        trees_[s].build(tables_[s]);
        return SpehResult::Ok;
    }

    // CONTROL THREAD ONLY. Publish the inactive slot (the one just filled by
    // loadIntoInactive) as the new active slot via store-release. Pairs with
    // the audio thread's activeSlot() load-acquire.
    void publish() noexcept {
        const int inactive = 1 - active_sofa_slot_.load(std::memory_order_relaxed);
        active_sofa_slot_.store(inactive, std::memory_order_release);
    }

    // Build directly into the active slot (initialise path; control thread,
    // allocates). Used by BinauralMonitor::initialize() to seed slot 0 before
    // any swap. Does NOT touch the atomic ordering contract beyond a relaxed
    // read of the current active index.
    SpehResult loadIntoActive(const std::string& path, float expected_sr) {
        const std::size_t s =
            static_cast<std::size_t>(active_sofa_slot_.load(std::memory_order_relaxed));
        SpehResult res = loadSpeh(path, expected_sr, tables_[s]);
        if (res != SpehResult::Ok) return res;
        trees_[s].build(tables_[s]);
        return SpehResult::Ok;
    }

    // AUDIO THREAD reader — load-acquire ONCE per block. Pairs with publish().
    int activeSlot() const noexcept {
        return active_sofa_slot_.load(std::memory_order_acquire);
    }

    // Read the active table/tree given a slot index obtained from a SINGLE
    // activeSlot() snapshot (callers must NOT re-read the atomic — pass the
    // snapshotted index through to keep one acquire per block).
    const HrtfTable& tableForSlot(int slot) const noexcept {
        return tables_[static_cast<std::size_t>(slot)];
    }
    const KdTree3D& treeForSlot(int slot) const noexcept {
        return trees_[static_cast<std::size_t>(slot)];
    }

    // Convenience readers that take their own acquire snapshot. Safe only when
    // a single read per block is acceptable; the per-block hot path should use
    // activeSlot() + tableForSlot()/treeForSlot() to guarantee one acquire.
    const HrtfTable& activeTable() const noexcept { return tableForSlot(activeSlot()); }
    const KdTree3D&  activeTree()  const noexcept { return treeForSlot(activeSlot()); }

private:
    std::array<HrtfTable, 2> tables_{};
    std::array<KdTree3D, 2>  trees_{};
    std::atomic<int>         active_sofa_slot_{0};
};

} // namespace spe::hrtf
