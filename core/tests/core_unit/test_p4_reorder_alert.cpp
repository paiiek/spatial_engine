// test_p4_reorder_alert.cpp
// P4: 5+ reordered packets within 1-second window triggers
//     /sys/warning osc_reorder_burst callback.

#include "ipc/StateModel.h"
#include <cassert>
#include <cstdio>

using namespace spe::ipc;

int main() {
    StateModel sm;

    int warning_count = 0;
    sm.setWarningCallback([&](const Reply& r) {
        assert(r.tag == ReplyTag::Warning);
        assert(r.message == "osc_reorder_burst");
        ++warning_count;
    });

    // Apply seq=1000 first to establish high watermark.
    {
        Command cmd;
        cmd.tag = CommandTag::ObjMove; cmd.seq = 1000;
        PayloadObjMove p; p.obj_id=0; p.az_rad=0.f; p.el_rad=0.f; p.dist_m=1.f;
        cmd.payload = p;
        sm.apply(cmd, 0 /*now_ms=0*/);
    }

    // Now inject 10 reordered packets within the same 200 ms window (now_ms=50..150).
    // All have seq < 1000, so they're all dropped → reordered_drops incremented,
    // burst check fires once the window count >= BURST_THRESH (5).
    uint64_t now_ms = 50;
    for (int i = 0; i < 10; ++i) {
        Command cmd;
        cmd.tag = CommandTag::ObjMove;
        cmd.seq = static_cast<uint32_t>(999 - i); // all < 1000
        PayloadObjMove p; p.obj_id=0; p.az_rad=float(i); p.el_rad=0.f; p.dist_m=1.f;
        cmd.payload = p;
        sm.apply(cmd, now_ms);
        now_ms += 20; // spread 20 ms apart, all within 200 ms
    }

    // Must have triggered at least once.
    assert(warning_count >= 1);
    assert(sm.reorderedDrops() == 10);
    assert(sm.burstAlerts() >= 1);

    std::puts("PASS test_p4_reorder_alert");
    return 0;
}
