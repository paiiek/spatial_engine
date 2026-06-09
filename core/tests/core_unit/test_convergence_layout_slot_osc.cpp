// test_convergence_layout_slot_osc.cpp
// Phase 4.3 Inc 2b (Dreamscape convergence) — OSC wiring for the 50-slot
// speaker-layout library (/layout/slot/{save,load,clear,list,current}).
//
// Exercises the FULL control-thread path end-to-end, mirroring the threading
// contract of the binaural SOFA-select feature:
//   raw OSC bytes -> OSCBackend::injectPacket(peer) -> decode -> engine command
//   callback (stash pending op + flag, early-return) -> applyPendingLayoutSlotOp()
//   on the control tick (LayoutLibrary file I/O + sendReply).
//
// Coverage:
//   (1) save -> file actually written (verified by an independent LayoutLibrary
//       opened on the same dir) + ,iiss success reply.
//   (2) list / current replies (occupied slots + labels + summary terminator).
//   (3) load = validate/stage ONLY — the RUNNING engine layout is left
//       untouched (Inc 2b defers live apply to Inc 4); reply carries the loaded
//       layout's name.
//   (4) clear -> file removed + success reply.
//   (5) error paths: load empty slot, save out-of-range slot -> status 0.
//
// Port 0 (no UDP) — the outbound drain thread is not running, so replies
// accumulate in the SPSC ring and are read back via outboundPeek/Pending.

#include "core/SpatialEngine.h"
#include "geometry/LayoutLibrary.h"
#include "geometry/SpeakerLayout.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c, msg) do { if(!(c)){ std::fprintf(stderr,"FAIL: %s\n", msg); ++failures; } } while(0)

namespace fs = std::filesystem;
using spe::geometry::LayoutLibrary;
using spe::geometry::SpeakerLayout;

// ---------------------------------------------------------------------------
// OSC packet builders (big-endian, 4-byte padded) — same idiom as the other
// convergence wire tests.
// ---------------------------------------------------------------------------
static void pushPadded(std::vector<uint8_t>& p, const std::string& s) {
    for (char ch : s) p.push_back(static_cast<uint8_t>(ch));
    p.push_back(0);
    while (p.size() % 4 != 0) p.push_back(0);
}
static void pushI(std::vector<uint8_t>& p, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
    p.push_back((u>>8)&0xFF);  p.push_back(u&0xFF);
}
static std::vector<uint8_t> oscSave(int slot, const std::string& label) {
    std::vector<uint8_t> p;
    pushPadded(p, "/layout/slot/save"); pushPadded(p, ",is");
    pushI(p, slot); pushPadded(p, label); return p;
}
static std::vector<uint8_t> oscSlotI(const std::string& addr, int slot) {
    std::vector<uint8_t> p;
    pushPadded(p, addr); pushPadded(p, ",i"); pushI(p, slot); return p;
}
static std::vector<uint8_t> oscNoArg(const std::string& addr) {
    std::vector<uint8_t> p;
    pushPadded(p, addr); pushPadded(p, ","); return p;
}

// ---------------------------------------------------------------------------
// Minimal ,iiss reply parser. Returns addr + first two ints + first string.
// ---------------------------------------------------------------------------
struct Reply { std::string addr; int32_t i0 = 0, i1 = 0; std::string s0; bool valid = false; };

static int32_t rdI(const uint8_t* p) {
    return static_cast<int32_t>((uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
                                (uint32_t(p[2])<<8)|uint32_t(p[3]));
}
static Reply parseReply(const uint8_t* buf, std::size_t len) {
    Reply r;
    if (!buf || len < 8) return r;
    std::size_t i = 0;
    // addr
    std::size_t a = i; while (a < len && buf[a] != '\0') ++a;
    r.addr.assign(reinterpret_cast<const char*>(buf + i), a - i);
    i = (a + 1 + 3) & ~std::size_t(3);
    if (i >= len || buf[i] != ',') return r;
    std::size_t t = i; while (t < len && buf[t] != '\0') ++t;
    std::string tags(reinterpret_cast<const char*>(buf + i + 1), t - (i + 1));
    i = (t + 1 + 3) & ~std::size_t(3);
    int int_seen = 0;
    for (char c : tags) {
        if (c == 'i') {
            if (i + 4 > len) return r;
            const int32_t v = rdI(buf + i); i += 4;
            if (int_seen == 0) r.i0 = v; else if (int_seen == 1) r.i1 = v;
            ++int_seen;
        } else if (c == 's') {
            std::size_t s = i; while (s < len && buf[s] != '\0') ++s;
            if (r.s0.empty()) r.s0.assign(reinterpret_cast<const char*>(buf + i), s - i);
            i = (s + 1 + 3) & ~std::size_t(3);
        }
    }
    r.valid = true;
    return r;
}

// ---------------------------------------------------------------------------
static SpeakerLayout makeLayout(const char* name) {
    using namespace spe::geometry;
    SpeakerLayout l; l.version = "1.0"; l.name = name; l.regularity = Regularity::CIRCULAR;
    l.channel_to_idx_.fill(static_cast<int16_t>(-1));
    auto add = [&](int ch, float x, float y, float z) {
        Speaker s; s.channel = ch; s.x = x; s.y = y; s.z = z;
        l.channel_to_idx_[static_cast<std::size_t>(ch)] = static_cast<int16_t>(l.speakers.size());
        l.speakers.push_back(s);
    };
    add(1, -0.7071068f, 0.f, 0.7071068f);
    add(2,  0.7071068f, 0.f, 0.7071068f);
    add(3,  0.f,        0.f, -1.f);
    return l;
}

int main() {
    const std::string dir = "/tmp/spe_layout_slot_osc_" + std::to_string(::getpid());
    std::error_code ec;
    fs::remove_all(dir, ec);

    spe::core::SpatialEngine engine(0);
    engine.setLayoutLibraryDir(dir);
    engine.setLayout(makeLayout("running_rt"));
    engine.prepareToPlay(48000.0, 64);

    // Loopback peer so sendReply() does not drop (last_peer_endpoint_ capture).
    struct sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer.sin_port = htons(9999);

    auto& osc = engine.oscBackend();
    auto drive = [&](const std::vector<uint8_t>& pkt) {
        osc.injectPacket(std::span<const uint8_t>(pkt),
                         reinterpret_cast<const struct sockaddr*>(&peer), sizeof(peer));
        engine.applyPendingLayoutSlotOp();  // control-tick apply
    };
    auto reply = [&](std::size_t idx) {
        std::size_t n = 0; const uint8_t* b = osc.outboundPeek(idx, n);
        return parseReply(b, n);
    };
    auto drainAll = [&]() { osc.outboundDrainForTest(osc.outboundPending()); };

    // (1) save slot 3 with a label ------------------------------------------
    drive(oscSave(3, "studioA"));
    CHECK(osc.outboundPending() == 1, "save emits exactly one reply");
    {
        Reply r = reply(0);
        CHECK(r.addr == "/layout/slot/save", "save reply addr");
        CHECK(r.i0 == 3, "save reply slot == 3");
        CHECK(r.i1 == 1, "save reply status == success");
        CHECK(r.s0 == "studioA", "save reply echoes label");
    }
    drainAll();
    // Independent verification: the slot file really exists with the label.
    {
        LayoutLibrary verify(dir);
        CHECK(verify.occupied(3), "slot 3 file written to disk");
        CHECK(verify.label(3) == "studioA", "slot 3 label persisted");
        CHECK(verify.occupiedCount() == 1, "exactly one slot occupied");
    }

    // (2) list --------------------------------------------------------------
    drive(oscNoArg("/layout/slot/list"));
    CHECK(osc.outboundPending() == 2, "list: 1 occupied entry + 1 summary");
    {
        Reply e = reply(0), sum = reply(1);
        CHECK(e.addr == "/layout/slot/list" && e.i0 == 3 && e.s0 == "studioA",
              "list entry = slot 3 'studioA'");
        CHECK(sum.i0 == -1 && sum.i1 == 1, "list summary = (-1, count=1)");
    }
    drainAll();

    // (3) current(3) + current(summary) -------------------------------------
    drive(oscSlotI("/layout/slot/current", 3));
    {
        Reply r = reply(0);
        CHECK(r.addr == "/layout/slot/current" && r.i0 == 3 && r.i1 == 1 && r.s0 == "studioA",
              "current(3) = occupied 'studioA'");
    }
    drainAll();
    drive(oscNoArg("/layout/slot/current"));
    {
        Reply r = reply(0);
        CHECK(r.i0 == -1 && r.i1 == 1, "current summary = (-1, count=1)");
    }
    drainAll();

    // (4) load slot 3 = validate/stage ONLY (no live apply) -----------------
    const std::string running_before = engine.currentLayout().name;
    drive(oscSlotI("/layout/slot/load", 3));
    {
        Reply r = reply(0);
        CHECK(r.addr == "/layout/slot/load", "load reply addr");
        CHECK(r.i0 == 3 && r.i1 == 1, "load reply slot 3 status success");
        CHECK(r.s0 == "running_rt", "load reply carries saved layout name");
    }
    CHECK(engine.currentLayout().name == running_before,
          "load does NOT mutate the running layout (Inc 2b: stage only)");
    drainAll();

    // (5) clear slot 3 ------------------------------------------------------
    drive(oscSlotI("/layout/slot/clear", 3));
    {
        Reply r = reply(0);
        CHECK(r.addr == "/layout/slot/clear" && r.i0 == 3 && r.i1 == 1, "clear success reply");
    }
    drainAll();
    {
        LayoutLibrary verify(dir);
        CHECK(!verify.occupied(3), "slot 3 file removed after clear");
        CHECK(verify.occupiedCount() == 0, "library empty after clear");
    }

    // (6) error paths -------------------------------------------------------
    drive(oscSlotI("/layout/slot/load", 7));   // empty slot
    {
        Reply r = reply(0);
        CHECK(r.i0 == 7 && r.i1 == 0, "load empty slot -> status 0");
    }
    drainAll();
    drive(oscSave(99, "oob"));                  // out-of-range slot
    {
        Reply r = reply(0);
        CHECK(r.i0 == 99 && r.i1 == 0, "save out-of-range slot -> status 0");
    }
    drainAll();

    fs::remove_all(dir, ec);

    if (failures == 0) { std::printf("test_convergence_layout_slot_osc: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_layout_slot_osc: %d FAILURE(S)\n", failures);
    return 1;
}
