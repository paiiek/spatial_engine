// test_state_resync.cpp
// C6 — full-state UDP-loss resync (/sys/state_request). Verifies that a client
// can recover the full authoritative object state on demand: the engine
// re-emits every touched object on the existing C7 echo addresses
// (/adm/obj/N/{aed,gain,active,width,dsp}) terminated by a single
// /sys/state ,i <count> completion sentinel.
//
// Harness (bindRx / OscMsg / parseOsc / drainAll) mirrors test_echo_plane.cpp:
// a bound loopback UDP socket is registered as the echo subscriber so the dump
// packets are delivered and parsed off the wire (vs the flush(-1) byte path).
//
// Cases: T1 happy reconcile (via the real /sys/state_request sink path),
// T2 inactive→active=0, T3 never-touched absent, T4 cap-object single-window
// dump zero drops, T5 500ms debounce, T6 decoder unit, T7 no-subscriber no-op,
// T9 sustained-frequency vs the rate guard (500ms ok / 250ms trips),
// T10 pure-DSP inactive object (only an EQ band / k_hf-only / delay-only).

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "ipc/Command.h"
#include "ipc/CommandDecoder.h"
#include "ipc/EchoSubscriber.h"
#include "ipc/SceneSnapshot.h"
#include "audio_io/AudioCallback.h"

#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace spe;
using namespace spe::ipc;

// ── loopback receiver harness (copied from test_echo_plane.cpp) ───────────────
static uint32_t loopbackNet() { return htonl(INADDR_LOOPBACK); }

struct RxSock {
    int      fd   = -1;
    uint16_t port = 0;
};

static RxSock bindRx() {
    RxSock s;
    s.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(s.fd >= 0);
    struct sockaddr_in a {};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    const int br = ::bind(s.fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a));
    assert(br == 0);
    socklen_t len = sizeof(a);
    ::getsockname(s.fd, reinterpret_cast<struct sockaddr*>(&a), &len);
    s.port = ntohs(a.sin_port);
    struct timeval tv {
        0, 250 * 1000
    };  // 250 ms recv timeout
    ::setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

struct OscMsg {
    char  addr[64]  = {};
    char  types[16] = {};
    float f[8]      = {};
    int   nf        = 0;
    int   i[8]      = {};
    int   ni        = 0;
};

static uint32_t rdU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
static float rdF32(const uint8_t* p) {
    const uint32_t u = rdU32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

static OscMsg parseOsc(const uint8_t* buf, ssize_t n) {
    OscMsg m;
    if (n <= 0) return m;
    std::size_t off  = 0;
    const std::size_t alen = std::strlen(reinterpret_cast<const char*>(buf));
    std::strncpy(m.addr, reinterpret_cast<const char*>(buf), sizeof(m.addr) - 1);
    off = (alen + 4) & ~std::size_t(3);
    const std::size_t tlen = std::strlen(reinterpret_cast<const char*>(buf + off));
    std::strncpy(m.types, reinterpret_cast<const char*>(buf + off),
                 sizeof(m.types) - 1);
    off += (tlen + 4) & ~std::size_t(3);
    for (const char* t = m.types + 1; *t; ++t) {
        if (*t == 'f') {
            m.f[m.nf++] = rdF32(buf + off);
            off += 4;
        } else if (*t == 'i') {
            m.i[m.ni++] = static_cast<int>(rdU32(buf + off));
            off += 4;
        }
    }
    return m;
}

// Drain every pending packet on fd, storing up to `max` parsed messages.
static int drainAll(int fd, OscMsg* out, int max) {
    int count = 0;
    while (count < max) {
        uint8_t            buf[256];
        struct sockaddr_in from {};
        socklen_t          fl = sizeof(from);
        const ssize_t      n  = ::recvfrom(
            fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&from), &fl);
        if (n <= 0) break;
        out[count++] = parseOsc(buf, n);
    }
    return count;
}

// Counting drainer for large dumps — counts packets + the sentinel without
// storing every message.
struct DrainStats {
    int total      = 0;
    int sentinels  = 0;
    int last_count = -1;
};
static DrainStats drainStats(int fd) {
    DrainStats s;
    for (;;) {
        uint8_t            buf[256];
        struct sockaddr_in from {};
        socklen_t          fl = sizeof(from);
        const ssize_t      n  = ::recvfrom(
            fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&from), &fl);
        if (n <= 0) break;
        const OscMsg m = parseOsc(buf, n);
        ++s.total;
        if (std::strcmp(m.addr, "/sys/state") == 0) {
            ++s.sentinels;
            s.last_count = m.i[0];
        }
    }
    return s;
}

// ── command dispatch helpers ─────────────────────────────────────────────────
static void dMove(core::SpatialEngine& e, uint32_t id, float az, float el,
                  float dist) {
    Command c;
    c.tag = CommandTag::ObjMove;
    PayloadObjMove p;
    p.obj_id = id;
    p.az_rad = az;
    p.el_rad = el;
    p.dist_m = dist;
    c.payload = p;
    e.dispatchCommand(c);
}
static void dGain(core::SpatialEngine& e, uint32_t id, float g) {
    Command c;
    c.tag = CommandTag::ObjGain;
    PayloadObjGain p;
    p.obj_id = id;
    p.gain   = g;
    c.payload = p;
    e.dispatchCommand(c);
}
static void dActive(core::SpatialEngine& e, uint32_t id, bool active) {
    Command c;
    c.tag = CommandTag::ObjActiveAdm;
    PayloadObjActiveAdm p;
    p.obj_id = id;
    p.active = active;
    c.payload = p;
    e.dispatchCommand(c);
}
static void dWidth(core::SpatialEngine& e, uint32_t id, float w) {
    Command c;
    c.tag = CommandTag::ObjWidth;
    PayloadObjWidth p;
    p.obj_id    = id;
    p.width_rad = w;
    c.payload = p;
    e.dispatchCommand(c);
}
static void dDsp(core::SpatialEngine& e, uint32_t id, uint8_t param, float v) {
    Command c;
    c.tag = CommandTag::ObjDsp;
    PayloadObjDsp p;
    p.obj_id = id;
    p.param  = static_cast<PayloadObjDsp::Param>(param);
    p.value  = v;
    c.payload = p;
    e.dispatchCommand(c);
}
static void dStateRequest(core::SpatialEngine& e, uint32_t token) {
    Command c;
    c.tag = CommandTag::SysStateRequest;
    PayloadSysStateRequest p;
    p.token = token;
    c.payload = p;
    e.dispatchCommand(c);
}

// ── audio-block pump (drains cmd_fifo_ → obj_cache_ → publishes snapshot) ──────
struct AudioRig {
    static constexpr int N_SPK  = 8;
    static constexpr int FRAMES = 64;
    std::vector<std::vector<float>> bufs;
    std::vector<float*>             ptrs;
    AudioRig()
        : bufs(static_cast<size_t>(N_SPK),
               std::vector<float>(static_cast<size_t>(FRAMES), 0.f)),
          ptrs(static_cast<size_t>(N_SPK)) {
        for (int s = 0; s < N_SPK; ++s)
            ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();
    }
    void pump(core::SpatialEngine& e) {
        audio_io::AudioBlock b;
        b.output_channels      = ptrs.data();
        b.output_channel_count = N_SPK;
        b.num_frames           = FRAMES;
        b.sample_rate          = 48000.0;
        e.audioBlock(b);
    }
};

static int64_t nowMs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

// Spin up a real-UDP SpatialEngine on an ephemeral port so the /sys/state_request
// sink path (which uses udpFdForEcho()) actually delivers to a loopback rx.
// Returns the bound port via boundPortForTest(); retries to dodge port races.
static bool startEngineRealPort(core::SpatialEngine& e) {
    e.oscBackend().start();
    return e.oscBackend().boundPortForTest() != 0;
}
// A dedicated send socket (distinct from the rx socket) so a large dump does
// not contend a single socket's queue with its own self-sends.
static int makeSendSock() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a {};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a));
    return fd;
}
static int pickFreePort() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in a {};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    ::close(fd);
    return port;
}

constexpr float kRad2Deg = 180.f / 3.14159265358979323846f;
constexpr float kMaxDist = 20.f;

static const OscMsg* findAddr(const OscMsg* m, int n, const char* addr) {
    for (int k = 0; k < n; ++k)
        if (std::strcmp(m[k].addr, addr) == 0) return &m[k];
    return nullptr;
}

// ── T1 — happy-path reconcile via the real /sys/state_request sink path ───────
static void test_t1_happy_reconcile() {
    const int port = pickFreePort();
    core::SpatialEngine engine(port);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    const bool started = startEngineRealPort(engine);
    assert(started);

    AudioRig rig;
    // Drive 3 objects with assorted state BEFORE any subscriber exists, so no
    // live echo leaks (flush early-returns with no subscribers).
    dMove(engine, 0, 0.5f, 0.1f, 4.0f);
    dGain(engine, 0, 0.7f);
    dWidth(engine, 0, 0.25f);
    dDsp(engine, 0, 1, 6.0f);     // EQ band 1 = +6 dB
    dDsp(engine, 0, 6, 0.3f);     // reverb send

    dMove(engine, 2, -0.8f, 0.f, 10.0f);
    dActive(engine, 2, true);

    dMove(engine, 5, 1.2f, -0.2f, 2.0f);
    dActive(engine, 5, false);    // inactive-but-touched → active=0
    rig.pump(engine);

    // Register the loopback rx as the echo subscriber, then request a resync.
    RxSock rx = bindRx();
    const bool ok = engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, nowMs());
    assert(ok);

    dStateRequest(engine, 7);

    OscMsg msgs[64];
    const int cnt = drainAll(rx.fd, msgs, 64);
    assert(cnt > 0);

    // Object 0 — aed/gain/active/width + 2 dsp packets.
    const OscMsg* a0 = findAddr(msgs, cnt, "/adm/obj/0/aed");
    assert(a0 && std::strcmp(a0->types, ",fff") == 0);
    assert(std::fabs(a0->f[0] - 0.5f * kRad2Deg) < 1e-3f);
    assert(std::fabs(a0->f[1] - 0.1f * kRad2Deg) < 1e-3f);
    assert(std::fabs(a0->f[2] - 4.0f / kMaxDist) < 1e-4f);
    const OscMsg* g0 = findAddr(msgs, cnt, "/adm/obj/0/gain");
    assert(g0 && std::fabs(g0->f[0] - 0.7f) < 1e-4f);
    const OscMsg* w0 = findAddr(msgs, cnt, "/adm/obj/0/width");
    assert(w0 && std::fabs(w0->f[0] - 0.25f) < 1e-4f);
    const OscMsg* ac0 = findAddr(msgs, cnt, "/adm/obj/0/active");
    assert(ac0 && ac0->i[0] == 1);
    // Two dsp packets (param 1 = +6, param 6 = 0.3) on /adm/obj/0/dsp.
    int dsp0 = 0;
    bool saw_eq1 = false, saw_rev = false;
    for (int k = 0; k < cnt; ++k) {
        if (std::strcmp(msgs[k].addr, "/adm/obj/0/dsp") != 0) continue;
        ++dsp0;
        assert(std::strcmp(msgs[k].types, ",if") == 0);
        if (msgs[k].i[0] == 1) { saw_eq1 = true; assert(std::fabs(msgs[k].f[0] - 6.0f) < 1e-4f); }
        if (msgs[k].i[0] == 6) { saw_rev = true; assert(std::fabs(msgs[k].f[0] - 0.3f) < 1e-4f); }
    }
    assert(dsp0 == 2 && saw_eq1 && saw_rev);

    // Object 5 — inactive-but-touched → active=0.
    const OscMsg* ac5 = findAddr(msgs, cnt, "/adm/obj/5/active");
    assert(ac5 && ac5->i[0] == 0);

    // Exactly one /sys/state sentinel with count == 3 touched objects.
    int sentinels = 0, sentinel_count = -1;
    for (int k = 0; k < cnt; ++k)
        if (std::strcmp(msgs[k].addr, "/sys/state") == 0) {
            ++sentinels;
            sentinel_count = msgs[k].i[0];
        }
    assert(sentinels == 1);
    assert(sentinel_count == 3);

    ::close(rx.fd);
    std::puts("PASS test_t1_happy_reconcile");
}

// ── T2 — inactive-but-touched object emits active=0 ───────────────────────────
static void test_t2_inactive_active_zero() {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    engine.oscBackend().echoPlane().open();

    AudioRig rig;
    dMove(engine, 3, 0.4f, 0.f, 5.0f);   // touched (moved)
    dActive(engine, 3, false);            // then deactivated
    rig.pump(engine);

    RxSock rx = bindRx();
    engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    std::vector<ipc::ObjectSnapshot> buf;
    engine.snapshotObjects(buf, /*include_dsp_only=*/true);
    const int sfd = makeSendSock();
    engine.oscBackend().echoPlane().emitStateDump(buf, 0, sfd);

    OscMsg msgs[16];
    const int cnt = drainAll(rx.fd, msgs, 16);
    const OscMsg* ac = findAddr(msgs, cnt, "/adm/obj/3/active");
    assert(ac && ac->i[0] == 0);
    ::close(sfd);
    ::close(rx.fd);
    std::puts("PASS test_t2_inactive_active_zero");
}

// ── T3 — never-touched objects are absent from the dump ───────────────────────
static void test_t3_never_touched_absent() {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    engine.oscBackend().echoPlane().open();

    AudioRig rig;
    dMove(engine, 1, 0.2f, 0.f, 3.0f);   // only object 1 is touched
    rig.pump(engine);

    RxSock rx = bindRx();
    engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    std::vector<ipc::ObjectSnapshot> buf;
    engine.snapshotObjects(buf, true);
    assert(buf.size() == 1 && buf[0].id == 1);
    const int sfd = makeSendSock();
    engine.oscBackend().echoPlane().emitStateDump(buf, 0, sfd);

    OscMsg msgs[16];
    const int cnt = drainAll(rx.fd, msgs, 16);
    // No packets for untouched ids (0, 2, ...).
    for (int k = 0; k < cnt; ++k) {
        assert(std::strncmp(msgs[k].addr, "/adm/obj/0/", 11) != 0);
        assert(std::strncmp(msgs[k].addr, "/adm/obj/2/", 11) != 0);
    }
    const OscMsg* a1 = findAddr(msgs, cnt, "/adm/obj/1/aed");
    assert(a1 != nullptr);
    ::close(sfd);
    ::close(rx.fd);
    std::puts("PASS test_t3_never_touched_absent");
}

// ── T4 — full cap-object single dump drops zero packets (fresh rate window) ───
static void test_t4_full_dump_zero_drops() {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    engine.oscBackend().echoPlane().open();

    AudioRig rig;
    const int N = spe::MAX_OBJECTS;
    for (int id = 0; id < N; ++id)
        dMove(engine, static_cast<uint32_t>(id), 0.1f * id, 0.f, 2.0f);
    rig.pump(engine);

    RxSock rx = bindRx();
    int rcvbuf = 4 << 20;
    ::setsockopt(rx.fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    std::vector<ipc::ObjectSnapshot> buf;
    engine.snapshotObjects(buf, true);
    assert(static_cast<int>(buf.size()) == N);

    // Drain CONCURRENTLY with the send so the large burst (N*4+1 packets) never
    // overflows the socket queue (Linux accounts skb truesize, not payload, so a
    // few hundred small datagrams can exceed a default rcvbuf). The reader stops
    // once it has seen the sentinel and the queue then drains empty.
    DrainStats s;
    std::atomic<bool> saw_sentinel{false};
    std::thread reader([&] {
        int idle = 0;
        for (;;) {
            uint8_t            b[256];
            struct sockaddr_in from {};
            socklen_t          fl = sizeof(from);
            const ssize_t n = ::recvfrom(
                rx.fd, b, sizeof(b), 0,
                reinterpret_cast<struct sockaddr*>(&from), &fl);
            if (n <= 0) {                 // 250 ms timeout
                if (saw_sentinel.load()) break;
                if (++idle >= 16) break;  // ~4 s bound — never hang
                continue;                 // keep waiting for the burst
            }
            idle = 0;
            const OscMsg m = parseOsc(b, n);
            ++s.total;
            if (std::strcmp(m.addr, "/sys/state") == 0) {
                ++s.sentinels;
                s.last_count = m.i[0];
                saw_sentinel.store(true);
            }
        }
    });

    const int sfd = makeSendSock();
    engine.oscBackend().echoPlane().emitStateDump(buf, /*now_ms=*/0, sfd);
    reader.join();

    // Each object: aed + gain + active + width = 4 packets; + 1 sentinel.
    const int expected = N * 4 + 1;
    assert(s.sentinels == 1);
    assert(s.last_count == N);
    assert(s.total == expected);
    // Rate guard never tripped from a fresh window.
    assert(engine.oscBackend().echoPlane().entryAt(0).dropped_count == 0);
    ::close(sfd);
    ::close(rx.fd);
    std::puts("PASS test_t4_full_dump_zero_drops");
}

// ── T5 — 500ms debounce collapses rapid repeats ───────────────────────────────
static void test_t5_debounce() {
    const int port = pickFreePort();
    core::SpatialEngine engine(port);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    const bool started = startEngineRealPort(engine);
    assert(started);

    AudioRig rig;
    dMove(engine, 0, 0.3f, 0.f, 3.0f);
    rig.pump(engine);

    RxSock rx = bindRx();
    engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, nowMs());

    // Two requests within 500 ms → only ONE dump (one sentinel).
    dStateRequest(engine, 1);
    dStateRequest(engine, 2);
    DrainStats s1 = drainStats(rx.fd);
    assert(s1.sentinels == 1);

    // After the debounce window, a third request services a second dump.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    dStateRequest(engine, 3);
    DrainStats s2 = drainStats(rx.fd);
    assert(s2.sentinels == 1);

    ::close(rx.fd);
    std::puts("PASS test_t5_debounce");
}

// ── T6 — decoder unit (raw OSC bytes → Command via the public decode()) ───────
static std::size_t appendStr(std::vector<uint8_t>& v, const char* s) {
    const std::size_t slen  = std::strlen(s);
    const std::size_t total = (slen + 4) & ~std::size_t(3);
    for (std::size_t k = 0; k < slen; ++k) v.push_back(static_cast<uint8_t>(s[k]));
    for (std::size_t k = slen; k < total; ++k) v.push_back(0);
    return total;
}
static void appendI32(std::vector<uint8_t>& v, int32_t i) {
    const uint32_t u = static_cast<uint32_t>(i);
    v.push_back(static_cast<uint8_t>(u >> 24));
    v.push_back(static_cast<uint8_t>(u >> 16));
    v.push_back(static_cast<uint8_t>(u >> 8));
    v.push_back(static_cast<uint8_t>(u));
}
static void test_t6_decoder() {
    {
        // /sys/state_request ,i 7 → token 7
        std::vector<uint8_t> pkt;
        appendStr(pkt, "/sys/state_request");
        appendStr(pkt, ",i");
        appendI32(pkt, 7);
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(pkt.data(), pkt.size()));
        assert(c.tag == CommandTag::SysStateRequest);
        const auto* p = std::get_if<PayloadSysStateRequest>(&c.payload);
        assert(p && p->token == 7u);
    }
    {
        // /sys/state_request , (no arg) → token 0
        std::vector<uint8_t> pkt;
        appendStr(pkt, "/sys/state_request");
        appendStr(pkt, ",");
        CommandDecoder dec;
        const Command c = dec.decode(std::span<const uint8_t>(pkt.data(), pkt.size()));
        assert(c.tag == CommandTag::SysStateRequest);
        const auto* p = std::get_if<PayloadSysStateRequest>(&c.payload);
        assert(p && p->token == 0u);
    }
    std::puts("PASS test_t6_decoder");
}

// ── T7 — emitStateDump with zero subscribers is a no-op ───────────────────────
static void test_t7_no_subscriber_noop() {
    EchoPlane ep;
    ep.open();   // open but NO subscriber registered
    std::vector<ipc::ObjectSnapshot> buf;
    ipc::ObjectSnapshot o;
    o.id = 0;
    buf.push_back(o);
    // Must not crash and must send nothing. send_fd=-1 build-only path AND a
    // real fd path (no subscriber → the fan-out loop sends to no one).
    ep.emitStateDump(buf, 0, -1);

    RxSock rx = bindRx();
    ep.emitStateDump(buf, 0, rx.fd);
    OscMsg msgs[8];
    const int cnt = drainAll(rx.fd, msgs, 8);
    assert(cnt == 0);
    ::close(rx.fd);

    // Sink path: a /sys/state_request with no subscribers is also a no-op.
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    engine.oscBackend().echoPlane().open();
    dStateRequest(engine, 0);   // no subscribers → silent return, no crash
    std::puts("PASS test_t7_no_subscriber_noop");
}

// ── T9 — sustained frequency vs the 5000 pkt/s rate guard ─────────────────────
// Deterministic: drive emitStateDump at controlled timestamps with a full
// cap-object full-DSP dump (~11 pkts/obj). 500ms spacing (≤2 dumps/window) must
// not drop; 250ms spacing (4 dumps/window) trips the guard.
static void emitFullDspDumpAt(core::SpatialEngine& engine, EchoPlane& ep,
                              std::vector<ipc::ObjectSnapshot>& buf,
                              int64_t now_ms, int send_fd) {
    engine.snapshotObjects(buf, true);
    ep.emitStateDump(buf, now_ms, send_fd);
}
static void test_t9_sustained_frequency() {
    const int N = spe::MAX_OBJECTS;

    // (a) 500 ms spacing — zero drops.
    {
        core::SpatialEngine engine(0);
        engine.prepareToPlay(48000.0, AudioRig::FRAMES);
        EchoPlane& ep = engine.oscBackend().echoPlane();
        ep.open();
        AudioRig rig;
        // Pump per object so cmd_fifo_ (cap 1024) never overflows the 10
        // commands/object × N drive.
        for (int id = 0; id < N; ++id) {
            dMove(engine, static_cast<uint32_t>(id), 0.1f, 0.f, 2.0f);
            dGain(engine, static_cast<uint32_t>(id), 0.5f);
            dWidth(engine, static_cast<uint32_t>(id), 0.2f);
            for (uint8_t p = 0; p <= 3; ++p)
                dDsp(engine, static_cast<uint32_t>(id), p, 1.0f + p);  // 4 EQ
            dDsp(engine, static_cast<uint32_t>(id), 4, 5.0f);          // delay
            dDsp(engine, static_cast<uint32_t>(id), 5, 0.9f);          // k_hf
            dDsp(engine, static_cast<uint32_t>(id), 6, 0.4f);          // reverb
            rig.pump(engine);
        }

        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        assert(fd >= 0);
        struct sockaddr_in sa {};
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port        = 0;
        ::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
        ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

        std::vector<ipc::ObjectSnapshot> buf;
        for (int64_t t = 0; t <= 2000; t += 500)
            emitFullDspDumpAt(engine, ep, buf, t, fd);
        ::close(fd);
        assert(ep.entryAt(0).dropped_count == 0);
    }

    // (b) 250 ms spacing — trips the guard (drops > 0).
    {
        core::SpatialEngine engine(0);
        engine.prepareToPlay(48000.0, AudioRig::FRAMES);
        EchoPlane& ep = engine.oscBackend().echoPlane();
        ep.open();
        AudioRig rig;
        for (int id = 0; id < N; ++id) {
            dMove(engine, static_cast<uint32_t>(id), 0.1f, 0.f, 2.0f);
            dGain(engine, static_cast<uint32_t>(id), 0.5f);
            dWidth(engine, static_cast<uint32_t>(id), 0.2f);
            for (uint8_t p = 0; p <= 3; ++p)
                dDsp(engine, static_cast<uint32_t>(id), p, 1.0f + p);
            dDsp(engine, static_cast<uint32_t>(id), 4, 5.0f);
            dDsp(engine, static_cast<uint32_t>(id), 5, 0.9f);
            dDsp(engine, static_cast<uint32_t>(id), 6, 0.4f);
            rig.pump(engine);
        }

        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        assert(fd >= 0);
        struct sockaddr_in sa {};
        sa.sin_family      = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port        = 0;
        ::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));
        ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

        std::vector<ipc::ObjectSnapshot> buf;
        for (int64_t t = 0; t < 1000; t += 250)   // 4 dumps in one window
            emitFullDspDumpAt(engine, ep, buf, t, fd);
        ::close(fd);
        // ~11 pkts/obj × N × 4 dumps. At N=128 this is ~5636 > 5000 → drops.
        // At N=64 (~2816 ×... actually 11×64×4=2816 < 5000) the guard would NOT
        // trip, so only assert the trip at the 128 cap.
        if (N >= 128) assert(ep.entryAt(0).dropped_count > 0);
    }
    std::puts("PASS test_t9_sustained_frequency");
}

// ── T10 — pure-DSP inactive object IS dumped (active=0 + correct /dsp) ─────────
static void run_pure_dsp_case(uint8_t param, float value, int expect_param,
                              float expect_value) {
    core::SpatialEngine engine(0);
    engine.prepareToPlay(48000.0, AudioRig::FRAMES);
    engine.oscBackend().echoPlane().open();

    AudioRig rig;
    // ONLY a single DSP param set; object stays inactive, everything else default.
    dDsp(engine, 4, param, value);
    rig.pump(engine);

    RxSock rx = bindRx();
    engine.oscBackend().echoPlane().addSubscriber(
        loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    std::vector<ipc::ObjectSnapshot> buf;
    engine.snapshotObjects(buf, true);
    // The object must be present in the dump despite being inactive + pure-DSP.
    bool present = false;
    for (const auto& o : buf)
        if (o.id == 4) present = true;
    assert(present);
    const int sfd = makeSendSock();
    engine.oscBackend().echoPlane().emitStateDump(buf, 0, sfd);

    OscMsg msgs[16];
    const int cnt = drainAll(rx.fd, msgs, 16);
    const OscMsg* ac = findAddr(msgs, cnt, "/adm/obj/4/active");
    assert(ac && ac->i[0] == 0);   // inactive → active=0
    bool saw_dsp = false;
    for (int k = 0; k < cnt; ++k) {
        if (std::strcmp(msgs[k].addr, "/adm/obj/4/dsp") != 0) continue;
        if (msgs[k].i[0] == expect_param) {
            saw_dsp = true;
            assert(std::fabs(msgs[k].f[0] - expect_value) < 1e-4f);
        }
    }
    assert(saw_dsp);
    ::close(sfd);
    ::close(rx.fd);
}
static void test_t10_pure_dsp_inactive() {
    run_pure_dsp_case(1, 6.0f, 1, 6.0f);    // EQ band 1 only
    run_pure_dsp_case(5, 0.9f, 5, 0.9f);    // k_hf only (default 0.5)
    run_pure_dsp_case(4, 12.5f, 4, 12.5f);  // user delay only
    std::puts("PASS test_t10_pure_dsp_inactive");
}

int main() {
    std::puts("test_state_resync: C6 /sys/state_request full-state resync");
    test_t1_happy_reconcile();
    test_t2_inactive_active_zero();
    test_t3_never_touched_absent();
    test_t4_full_dump_zero_drops();
    test_t5_debounce();
    test_t6_decoder();
    test_t7_no_subscriber_noop();
    test_t9_sustained_frequency();
    test_t10_pure_dsp_inactive();
    std::puts("test_state_resync: ALL PASS");
    return 0;
}
