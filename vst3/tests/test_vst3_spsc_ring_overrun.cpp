// vst3/tests/test_vst3_spsc_ring_overrun.cpp
// S3 (C4): Explicit overrun behaviour test for SpscRing<AudioCommand, 1024>.
//
// Policy: drop-newest on full (push returns false, drops_ incremented).
// This is "reject producer" semantics — the oldest entry is preserved,
// the newest oversupply is dropped. This minimises audio-thread surprise
// because commands already in the ring have been waiting longest and are
// more likely to reflect current state; a burst of new commands that
// would overflow is dropped rather than silently overwriting older ones.
//
// Validates:
//   1. Producer pushes (capacity + 100) commands without consumer popping.
//   2. Producer does NOT block, does NOT throw, does NOT corrupt memory.
//   3. Exactly (capacity - 1) slots are filled (ring capacity = 1024,
//      usable = 1023, one slot reserved for empty/full disambiguation).
//   4. ring.drops() == 101 (100 overflow + 1 final attempt post-full).
//      Actually: 100 overflow items dropped (push returns false each time).
//   5. A drop counter (ring.drops()) increments for each rejected push.
//   6. Consumer can drain all (capacity-1) items without crash.

#include "AudioCommand.h"
#include "util/SpscRing.h"

#include <cassert>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",              \
                         msg, __FILE__, __LINE__);                   \
            ++failures;                                              \
        }                                                            \
    } while (0)

int main()
{
    using namespace spe::vst3;
    using namespace spe::util;

    constexpr std::size_t kCap     = 1024;              // ring Capacity template arg
    constexpr std::size_t kUsable  = kCap - 1;          // usable slots (N-1)
    constexpr std::size_t kOverrun = 100;               // extra pushes beyond usable

    SpscRing<AudioCommand, kCap> ring;

    // -----------------------------------------------------------------------
    // Phase 1: fill the ring to capacity, then push kOverrun more.
    // None of these should block or throw.
    // -----------------------------------------------------------------------
    std::size_t accepted = 0;
    std::size_t rejected = 0;

    for (std::size_t i = 0; i < kUsable + kOverrun; ++i) {
        AudioCommand ac{};
        ac.tag = spe::ipc::CommandTag::ObjMove;
        ac.seq = static_cast<uint32_t>(i);
        ac.payload.obj_move.obj_id = static_cast<uint32_t>(i);

        if (ring.push(ac)) {
            ++accepted;
        } else {
            ++rejected;
        }
    }

    // Accepted == kUsable, rejected == kOverrun
    CHECK(accepted == kUsable,
          "accepted_equals_usable_capacity");
    CHECK(rejected == kOverrun,
          "rejected_equals_overrun_count");

    // -----------------------------------------------------------------------
    // Phase 2: ring.drops() must equal kOverrun (each failed push increments).
    // -----------------------------------------------------------------------
    CHECK(ring.drops() == kOverrun,
          "ring_drops_counter_equals_overrun");

    // -----------------------------------------------------------------------
    // Phase 3: size_approx() should match accepted count.
    // -----------------------------------------------------------------------
    CHECK(ring.size_approx() == kUsable,
          "size_approx_equals_accepted");

    // -----------------------------------------------------------------------
    // Phase 4: consumer can drain all kUsable items without crash or corruption.
    // -----------------------------------------------------------------------
    std::size_t drained = 0;
    bool order_ok = true;
    uint32_t prev_seq = 0;

    AudioCommand ac{};
    while (ring.pop(ac)) {
        if (drained > 0 && ac.seq != prev_seq + 1) {
            order_ok = false;
        }
        prev_seq = ac.seq;
        ++drained;
    }

    CHECK(drained == kUsable,  "drained_equals_accepted");
    CHECK(order_ok,            "fifo_order_preserved_after_overrun");

    // -----------------------------------------------------------------------
    // Phase 5: ring is now empty.
    // -----------------------------------------------------------------------
    CHECK(ring.empty_approx(), "ring_empty_after_drain");

    // -----------------------------------------------------------------------
    // Phase 6: reset_drops() works.
    // -----------------------------------------------------------------------
    ring.reset_drops();
    CHECK(ring.drops() == 0,   "reset_drops_clears_counter");

    std::printf("test_vst3_spsc_ring_overrun: %s "
                "(accepted=%zu rejected=%zu drained=%zu)\n",
                failures == 0 ? "PASS" : "FAIL",
                accepted, rejected, drained);
    return failures == 0 ? 0 : 1;
}
