// test_p4_state_model_seq.cpp
// P4: seq monotone, reverse-order packets → highest-seq state wins,
//     osc_reordered_drops count matches injected reorders.

#include "ipc/StateModel.h"
#include <cassert>
#include <cstdio>

using namespace spe::ipc;

int main() {
    StateModel sm;

    // --- Basic monotone apply ---
    {
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = 1;
        PayloadObjMove p; p.obj_id=0; p.az_rad=1.f; p.el_rad=0.f; p.dist_m=1.f;
        cmd.payload = p;
        assert(sm.apply(cmd));
        assert(sm.objectState(0).az_rad == 1.f);
        assert(sm.objectState(0).last_applied_seq == 1);
    }

    // Duplicate seq is dropped.
    {
        Command cmd;
        cmd.tag = CommandTag::ObjMove; cmd.seq=1;
        PayloadObjMove p; p.obj_id=0; p.az_rad=99.f; p.el_rad=0.f; p.dist_m=1.f;
        cmd.payload = p;
        assert(!sm.apply(cmd));
        assert(sm.objectState(0).az_rad == 1.f); // unchanged
        assert(sm.reorderedDrops() == 1);
    }

    sm.reset();

    // --- Inject 1000 packets in reverse order ---
    // obj_id=0, seq goes 1000 down to 1. Only seq=1000 should be applied last
    // — but because we apply highest-seq state, seq=1000 arrives first (reverse order)
    // and all subsequent are dropped.
    {
        const int N = 1000;
        // Apply seq=N first (highest), then N-1 .. 1 (all reordered/dropped).
        {
            Command cmd;
            cmd.tag = CommandTag::ObjMove; cmd.seq = N;
            PayloadObjMove p; p.obj_id=0; p.az_rad=float(N); p.el_rad=0.f; p.dist_m=1.f;
            cmd.payload = p;
            assert(sm.apply(cmd, 0));
        }
        uint64_t expected_drops = 0;
        for (int s = N-1; s >= 1; --s) {
            Command cmd;
            cmd.tag = CommandTag::ObjMove; cmd.seq = static_cast<uint32_t>(s);
            PayloadObjMove p; p.obj_id=0; p.az_rad=float(s); p.el_rad=0.f; p.dist_m=1.f;
            cmd.payload = p;
            assert(!sm.apply(cmd, 0));
            ++expected_drops;
        }
        // Final state should reflect the highest-seq (N) packet.
        assert(sm.objectState(0).az_rad == float(N));
        assert(sm.objectState(0).last_applied_seq == static_cast<uint32_t>(N));
        assert(sm.reorderedDrops() == expected_drops);
    }

    std::puts("PASS test_p4_state_model_seq");
    return 0;
}
