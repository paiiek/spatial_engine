// test_p4_heartbeat_miss.cpp
// P4: publisher "stops" for 350 ms (mock time) → monitor detects ≥3 misses
//     and emits /sys/heartbeat_miss.
// Audio thread is unaffected (no audio-thread calls here).

#include "ipc/HeartbeatPublisher.h"
#include "ipc/HeartbeatMonitor.h"
#include <cassert>
#include <cstdio>

using namespace spe::ipc;

int main() {
    // Publisher sends pings to monitor via callback.
    int miss_callbacks = 0;

    HeartbeatMonitor monitor([&](const Reply& r) {
        assert(r.tag == ReplyTag::HeartbeatMiss);
        assert(r.message == "heartbeat_miss");
        ++miss_callbacks;
    });

    HeartbeatPublisher publisher([&](const Command& cmd) {
        // Forward ping to monitor.
        auto& p = std::get<PayloadHbPing>(cmd.payload);
        monitor.onPing(p.timestamp_ms);
    });

    // Run publisher + monitor for 200 ms (2 pings at 100 ms each).
    publisher.tick(100); monitor.tick(100);
    publisher.tick(100); monitor.tick(100);
    assert(publisher.pingsSent() == 2);

    // Now "stop" publisher for 350 ms — only tick monitor.
    // Period=100 ms, so 3 missed periods in 350 ms.
    monitor.tick(100); // miss 1
    monitor.tick(100); // miss 2
    monitor.tick(100); // miss 3 → should trigger
    monitor.tick(50);  // partial period

    // At least one miss callback must have fired.
    assert(miss_callbacks >= 1);
    assert(monitor.missCount() >= 3);

    // Audio thread invariant: HeartbeatPublisher and HeartbeatMonitor are
    // control-thread only. No RT-assert violations are triggered here.
    // (RT-assert harness would catch any alloc on audio thread.)

    std::puts("PASS test_p4_heartbeat_miss");
    return 0;
}
