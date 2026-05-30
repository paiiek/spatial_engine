/*  test_hrtf_sofa_swap_race.cpp — v0.9 Lane B (B-M2) relacy race-detector test
 *  spatial_engine
 *
 *  Models the runtime SOFA hot-swap concurrency surface under relacy's
 *  systematic interleaving explorer. TWO lock-free double buffers are modeled
 *  (they share the audio thread's single per-block snapshot):
 *
 *  (1) The HrtfLookup 2-slot table/tree double buffer (mirrors AmbiDecoder
 *      P1.1): control builds into the inactive slot then store-release on
 *      active_sofa_slot_; audio load-acquire ONCE per block, then DEREFERENCES
 *      the published slot's DATA (a nearest()-style scan of `positions` + an
 *      `ir()` sample) — not just the atomic index.
 *
 *  (2) The B2 VS bank, ALSO a 2-slot double buffer (active_vs_slot_).
 *      DESIGN NOTE: the plan's DEFAULT was a single-slot VS bank quiesced by a
 *      vs_rebuild_in_progress_ handshake (audio force-Directs while a rebuild is
 *      in flight). Modeling that here showed the single-slot + one-way-flag
 *      protocol (even with a two-phase ack) is NOT race-free — a bank read can
 *      overlap a bank write because an acquire-load may legally observe a STALE
 *      flag. The plan explicitly authorizes the FALLBACK ("double-buffer the VS
 *      bank with active_vs_slot_ ... does NOT have the TOCTOU problem and is the
 *      cleaner answer"). We took it. So this test models the double buffer:
 *      control builds the inactive VS slot then store-release on active_vs_slot_;
 *      audio load-acquire once per block and reads that slot's bank DATA.
 *
 *  Both buffers are kept safe by the SAME ≥1-block CADENCE INVARIANT
 *  (HrtfLookup.h / AmbiDecoder.h:16-25): the control tick must not reuse /
 *  overwrite an INACTIVE slot until any in-flight audio block that still holds
 *  that slot index has finished. The control thread waits on the audio
 *  per-block tick before reusing a slot.
 *
 *  NON-VACUITY (B-M2 mandate):
 *    - control publishes >=2 DISTINCT slots (kSwaps) faster than the production
 *      ~1 Hz tick.
 *    - audio reads the published table/tree AND VS-bank DATA, not just indices.
 *
 *  TWO DOCUMENTED NEGATIVE CONTROLS (#ifdef-toggleable):
 *    (a) -DNEG_REMOVE_CADENCE_GUARD : control reuses the inactive slot with no
 *        wait for in-flight readers -> relacy reports a data race on the
 *        table/tree AND/OR VS-bank DATA (slot-reuse-under-reader). Proves the
 *        ≥1-block cadence guard is load-bearing.
 *    (b) -DNEG_REMOVE_VS_DOUBLE_BUFFER : control overwrites the ACTIVE VS slot
 *        in place (single-slot behavior — what the plan's rejected default would
 *        do) instead of building the inactive slot and publishing -> relacy
 *        reports a data race on the VS-bank DATA (write overlaps the audio's
 *        same-slot read). Proves the VS double buffer (publish/consume of
 *        active_vs_slot_) is load-bearing.
 *
 *  Build
 *  -----
 *  cmake .. -DSPATIAL_ENGINE_NO_JUCE=ON -DSPATIAL_ENGINE_BUILD_RELACY_TESTS=ON
 *  make test_hrtf_sofa_swap_race
 *  ./test_hrtf_sofa_swap_race          # must report ZERO races on correct code
 *
 *  Negative-control runs (must each REPORT a race):
 *  c++ -DNEG_REMOVE_CADENCE_GUARD    ...  ./a.out
 *  c++ -DNEG_REMOVE_VS_DOUBLE_BUFFER ...  ./a.out
 */

#include <relacy/relacy_std.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Model parameters
// ---------------------------------------------------------------------------
static constexpr int kPositions = 4;   // tiny "table": 4 positions
static constexpr int kIrTaps    = 4;   // tiny per-position IR

// ---------------------------------------------------------------------------
// One {table, tree} slot. DATA fields are rl::var so relacy tracks read/write
// races on the actual payload (positions + ir samples), satisfying the
// non-vacuity requirement that the audio thread DEREFERENCE the slot.
// ---------------------------------------------------------------------------
struct TableSlot {
    std::array<rl::var<float>, kPositions>            positions;  // az proxy
    std::array<rl::var<float>, kPositions * kIrTaps>  ir;         // ir samples

    void build(unsigned gen) {
        for (int i = 0; i < kPositions; ++i)
            positions[static_cast<std::size_t>(i)]($) =
                static_cast<float>(gen * 100 + i);
        for (int i = 0; i < kPositions * kIrTaps; ++i)
            ir[static_cast<std::size_t>(i)]($) =
                static_cast<float>(gen) + static_cast<float>(i) * 0.01f;
    }

    // "nearest()+ir()" read of the slot DATA (audio thread).
    float read() {
        int best = 0;
        float bestv = positions[0]($);
        for (int i = 1; i < kPositions; ++i) {
            const float v = positions[static_cast<std::size_t>(i)]($);
            if (v > bestv) { bestv = v; best = i; }
        }
        return bestv + ir[static_cast<std::size_t>(best * kIrTaps)]($);
    }
};

// ---------------------------------------------------------------------------
// One B2 VS-bank slot (the per-VS convolver IR cache, modeled as a few taps).
// ---------------------------------------------------------------------------
struct VsSlot {
    std::array<rl::var<float>, kPositions> taps;

    void write(unsigned gen) {
        for (int i = 0; i < kPositions; ++i)
            taps[static_cast<std::size_t>(i)]($) =
                static_cast<float>(gen) + static_cast<float>(i);
    }
    float read() {
        float s = 0.f;
        for (int i = 0; i < kPositions; ++i)
            s += taps[static_cast<std::size_t>(i)]($);
        return s;
    }
};

// ---------------------------------------------------------------------------
// Shared model state
// ---------------------------------------------------------------------------
struct SofaSwapModel {
    std::array<TableSlot, 2> tables;
    rl::atomic<int>          active_slot{0};   // HrtfLookup::active_sofa_slot_

    std::array<VsSlot, 2>    vs;               // B2 VS bank, 2-slot double buffer
    rl::atomic<int>          active_vs_slot{0}; // BinauralMonitor::active_vs_slot_

    // effective_mode_: 0 = Direct, 1 = AmbiVS. Written by the audio thread
    // (models the :428 promote / :553 demote) and snapshotted once per block.
    rl::atomic<int>          effective_mode{0};

    // Per-block tick + last-consumed slot publishes (production:
    // BinauralMonitor::audio_block_tick_ / last_consumed_{table,vs}_slot_).
    // EXPLICIT QUIESCENCE HANDSHAKE (AmbiDecoder.h:16-25 robust fix): the audio
    // thread publishes (release), at the END of each block, the slot indices it
    // actually consumed this block. The control thread waits until the slot it is
    // about to overwrite is NO LONGER the audio's last-consumed slot (and the
    // tick has advanced, so the reading is not vacuous). This makes the 2-slot
    // double buffer provably race-free WITHOUT relying on the ~1 Hz timing slack.
    rl::atomic<int>          audio_block_tick{0};
    rl::atomic<int>          last_consumed_table{-1};
    rl::atomic<int>          last_consumed_vs{-1};

    // control_done: set release-true by the control thread after its last swap.
    // The audio thread loops (producing ticks on demand) until it observes this,
    // so the control thread's cadence waits are always satisfied (no livelock
    // from the audio thread exiting early).
    rl::atomic<bool>         control_done{false};

    [[maybe_unused]] float   sink{0.f};
};

// kSwaps>=2 distinct publishes satisfies the ">=2 distinct slots" non-vacuity
// requirement. The audio-block cap is a relacy state-space bound only — the
// audio thread really stops on control_done; the cap comfortably exceeds the
// control thread's total tick demand so the audio never exhausts early.
static constexpr int kSwaps       = 2;
static constexpr int kAudioBlocks = 24;

struct HrtfSofaSwapTest : rl::test_suite<HrtfSofaSwapTest, /*threads=*/2> {
    SofaSwapModel m;

    void before() {
        m.tables[0].build(1u);
        m.tables[1].build(2u);
        m.vs[0].write(1u);
        m.vs[1].write(2u);
        m.active_slot.store(0, rl::memory_order_release);
        m.active_vs_slot.store(0, rl::memory_order_release);
        m.audio_block_tick.store(0, rl::memory_order_release);
        m.last_consumed_table.store(-1, rl::memory_order_release);
        m.last_consumed_vs.store(-1, rl::memory_order_release);
        m.control_done.store(false, rl::memory_order_release);
    }

    // EXPLICIT QUIESCENCE HANDSHAKE: wait until the audio's last-consumed slot
    // for BOTH buffers is no longer the slot we are about to overwrite, with the
    // per-block tick advanced at least once (non-vacuous). The acquire-loads pair
    // with the audio's end-of-block release stores. The audio keeps producing
    // blocks until control_done, so this terminates.
    void wait_quiescent(int inactive_table, int inactive_vs) {
        const int start = m.audio_block_tick.load(rl::memory_order_acquire);
        while (true) {
            const int tick = m.audio_block_tick.load(rl::memory_order_acquire);
            if (tick != start) {
                const int lc_t = m.last_consumed_table.load(rl::memory_order_acquire);
                const int lc_v = m.last_consumed_vs.load(rl::memory_order_acquire);
                if (lc_t != inactive_table && lc_v != inactive_vs)
                    return;
            }
            rl::yield(1, $);
        }
    }

    void control_thread() {
        unsigned gen = 3;
        for (int swap = 0; swap < kSwaps; ++swap, ++gen) {
            const int t_inactive = 1 - m.active_slot.load(rl::memory_order_relaxed);
            const int v_inactive = 1 - m.active_vs_slot.load(rl::memory_order_relaxed);
#ifndef NEG_REMOVE_CADENCE_GUARD
            // QUIESCENCE HANDSHAKE before reusing either inactive slot. Waits
            // until no in-flight audio block still holds an inactive slot as its
            // last-consumed index. The acquire-loads pair with the audio thread's
            // end-of-block release stores — establishing the happens-before that
            // makes the overwrite race-free.
            wait_quiescent(t_inactive, v_inactive);
#else
            // NEGATIVE CONTROL (a): drop the quiescence handshake. Control reuses
            // an inactive slot an audio block may still hold as its consumed
            // index -> relacy reports a data race on the table/tree or VS DATA.
#endif
            // (1) TABLE double buffer: build into INACTIVE slot, store-release.
            m.tables[static_cast<std::size_t>(t_inactive)].build(gen);
            m.active_slot.store(t_inactive, rl::memory_order_release);

            // (2) VS-BANK double buffer: build into INACTIVE slot, store-release.
#ifndef NEG_REMOVE_VS_DOUBLE_BUFFER
            m.vs[static_cast<std::size_t>(v_inactive)].write(gen);
            m.active_vs_slot.store(v_inactive, rl::memory_order_release);
#else
            // NEGATIVE CONTROL (b): overwrite the ACTIVE VS slot IN PLACE (the
            // rejected single-slot behavior) — no publish into the inactive
            // slot. The audio thread reads the same active slot concurrently ->
            // relacy reports a data race on the VS-bank DATA.
            const int v_active = m.active_vs_slot.load(rl::memory_order_relaxed);
            m.vs[static_cast<std::size_t>(v_active)].write(gen);
#endif
        }
        m.control_done.store(true, rl::memory_order_release);
    }

    // Audio thread: each block takes ONE acquire-snapshot of {table slot, VS
    // slot, effective_mode}, dereferences the published table + VS DATA, then
    // bumps the per-block tick at end of block.
    void audio_thread() {
        for (int blk = 0; blk < kAudioBlocks; ++blk) {
            if (m.control_done.load(rl::memory_order_acquire)) break;
            // Audio flips the mode (models the :428 promote / :553 demote with
            // no handshake) BEFORE its per-block snapshot.
            m.effective_mode.store(1, rl::memory_order_release);  // promote AmbiVS

            // ── single per-block snapshot point ──
            const int t_slot = m.active_slot.load(rl::memory_order_acquire);
            const int v_slot = m.active_vs_slot.load(rl::memory_order_acquire);
            const int eff    = m.effective_mode.load(rl::memory_order_acquire);

            // (A) B1: dereference the published table/tree DATA from the snapshot.
            m.sink += m.tables[static_cast<std::size_t>(t_slot)].read();

            // (B) B2: dereference the published VS-bank DATA from the snapshot,
            //     when AmbiVS is effective. The double buffer makes this safe
            //     with no handshake — v_slot is the published slot, which control
            //     never overwrites in place.
            int consumed_vs = -1;
            if (eff == 1) {
                consumed_vs = v_slot;
                m.sink += m.vs[static_cast<std::size_t>(v_slot)].read();
            }

            // END OF BLOCK (mirrors finalizeXfadeBlock()): publish the slots
            // actually consumed this block (release) for the control thread's
            // quiescence handshake, THEN bump the per-block tick (release). The
            // release ordering ensures the control thread's acquire-load of the
            // last-consumed slots happens-after this block's DATA reads.
            m.last_consumed_table.store(t_slot, rl::memory_order_release);
            m.last_consumed_vs.store(consumed_vs, rl::memory_order_release);
            m.audio_block_tick.fetch_add(1, rl::memory_order_release);
        }
    }

    void thread(unsigned int idx) {
        if (idx == 0) control_thread();
        else          audio_thread();
    }
};

int main() {
    rl::test_params params;
    params.iteration_count = 50000;  // wide exploration of the small state space
    rl::simulate<HrtfSofaSwapTest>(params);
    // simulate() calls std::terminate() on a detected violation; reaching here
    // means all explored interleavings passed with no relacy-detected race.
    return 0;
}
