// core/tests/core_unit/test_p_cue_wire_dispatch.cpp
// E-M3 gate: prove the queued-from-UDP / applied-on-control-loop split.
// Fire /cue/go ,i 1 as an actual OSC datagram through OSCBackend.injectPacket,
// drain the inbound mailbox + tick CueEngine on a simulated control loop, and
// assert the cue advanced. Also verifies the outbound mailbox → sink forwarding
// path (the single cmd_fifo_ producer funnel) is exercised.

#include "ipc/OSCBackend.h"
#include "ipc/CommandDecoder.h"
#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "scene/CueEngine.h"
#include "scene/CueList.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

using namespace spe;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); } } while(0)

namespace fs = std::filesystem;

static void writeScene(const std::string& dir, const std::string& name, int id) {
    ipc::SceneSnapshot snap;
    snap.name = name;
    ipc::ObjectSnapshot o;
    o.id = id; o.az_rad = 0.2f; o.dist_m = 1.5f; o.gain_linear = 0.7f;
    snap.objects.push_back(o);
    CHECK(snap.saveToDisk(dir), "writeScene: save");
}

int main() {
    auto tmp = fs::temp_directory_path() / "spe_cue_wire_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const std::string dir = tmp.string();
    writeScene(dir, "s0", 0);
    writeScene(dir, "s1", 1);

    // Count Commands the sink receives (the single cmd_fifo_ producer funnel in
    // production; here just a counter to prove forwarding happens).
    int sink_count = 0;
    ipc::OSCBackend backend(
        [&](const ipc::Command&) { ++sink_count; },
        /*listen_port=*/0);  // 0 → no real UDP thread; drain manually.

    ipc::SceneController ctrl(dir);
    scene::CueEngine cue(&ctrl, 48000.f,
        [&](const ipc::Command& c) { return backend.postOutbound(c); });

    scene::CueList cl;
    { scene::Cue c; c.scene = "s0"; c.crossfade_ms = 0.f; cl.cues.push_back(c); }
    { scene::Cue c; c.scene = "s1"; c.crossfade_ms = 0.f; cl.cues.push_back(c); }
    cue.setCueList(cl);

    // Start at cue 0 so a /cue/go 1 is an observable advance.
    cue.go(0, 0);
    CHECK(cue.currentIndex() == 0, "wire: start at cue 0");

    // Build /cue/go ,i 1 as a real OSC datagram and inject it (the UDP path).
    ipc::Command go1;
    go1.tag = ipc::CommandTag::CueGo;
    ipc::PayloadCueGo p; p.index = 1; go1.payload = p;
    std::vector<uint8_t> bytes;
    ipc::CommandDecoder enc;
    CHECK(enc.encode(go1, bytes), "wire: encode /cue/go ,i 1");

    backend.injectPacket(std::span<const uint8_t>(bytes));

    // The cue command must NOT have been applied inline — it is queued.
    CHECK(cue.currentIndex() == 0, "wire: cue NOT applied inline (still queued)");

    // Simulated control loop: drain inbound mailbox → apply on CueEngine.
    ipc::Command inc;
    bool applied = false;
    while (backend.drainInbound(inc)) {
        if (inc.tag == ipc::CommandTag::CueGo) {
            const auto& gp = std::get<ipc::PayloadCueGo>(inc.payload);
            cue.go(gp.index, /*now_ms=*/10);
            applied = true;
        }
    }
    CHECK(applied, "wire: inbound mailbox carried the CueGo");
    CHECK(cue.currentIndex() == 1, "wire: cue advanced to 1 on control loop");

    // CueEngine emitted object updates → outbound mailbox. Drain+forward via the
    // sink (simulates the UDP-thread wake). Proves the single-producer funnel.
    sink_count = 0;
    backend.drainOutboundToSink();
    CHECK(sink_count > 0, "wire: outbound updates forwarded via sink");

    fs::remove_all(tmp);
    std::printf("ALL PASS\n");
    return 0;
}
