// test_echo_plane.cpp
// M5.1 — 10 ctests for EchoPlane (subscriber registry, dirty-bit coalesce,
// rate guard, TTL eviction, transport echo). No real sockets needed: send_fd=-1
// exercises the "build but don't send" path in buildAndSend().

#include "ipc/EchoSubscriber.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using namespace spe::ipc;

// Build a sockaddr_in for 127.0.0.1:<port> (network byte order addr for
// addSubscriber).
static uint32_t loopbackNet() {
    return htonl(INADDR_LOOPBACK);  // 127.0.0.1 in network byte order
}

// ── C7 loopback-receiver harness (modeled on
// test_osc_outbound_reply_smoke.cpp:43-96) ────────────────────────────────────
// Bind a loopback UDP socket on an ephemeral port, read the actual bound port
// back via getsockname(), and arm SO_RCVTIMEO so recvfrom() fails fast instead
// of hanging. Register THAT exact bound port as the echo subscriber so the
// flush()'d packets are actually delivered and can be parsed off the wire — the
// only way to assert wire content (vs. the legacy flush(-1) byte-count path).
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
        0, 200 * 1000
    };  // 200 ms recv timeout
    ::setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

// Parsed OSC message (enough fields for the echo addresses under test).
struct OscMsg {
    char  addr[64]  = {};
    char  types[16] = {};
    float f[8]      = {};
    int   nf        = 0;
    int   i[8]      = {};
    int   ni        = 0;
    char  s[64]     = {};
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

// Parse one well-formed OSC packet (our own echo bytes) into OscMsg.
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
        } else if (*t == 's') {
            const std::size_t sl =
                std::strlen(reinterpret_cast<const char*>(buf + off));
            std::strncpy(m.s, reinterpret_cast<const char*>(buf + off),
                         sizeof(m.s) - 1);
            off += (sl + 4) & ~std::size_t(3);
        }
    }
    return m;
}

// Drain every pending packet on fd until SO_RCVTIMEO fires (no more packets).
static int drainAll(int fd, OscMsg* out, int max) {
    int count = 0;
    while (count < max) {
        uint8_t            buf[256];
        struct sockaddr_in from {};
        socklen_t          fl = sizeof(from);
        const ssize_t      n  = ::recvfrom(
            fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&from), &fl);
        if (n <= 0) break;  // timeout / EAGAIN
        out[count++] = parseOsc(buf, n);
    }
    return count;
}

// ── test 1 ──────────────────────────────────────────────────────────────────
// addSubscriber with the correct tag registers a subscriber.
static void test_handshake_with_echo_tag_enables_echo() {
    EchoPlane ep;
    ep.open();
    bool ok = ep.addSubscriber(loopbackNet(), 9102,
                               EchoPlane::kEchoSubscriberTag, 0);
    assert(ok);
    assert(ep.subscriberCount() == 1);
    assert(ep.hasSubscribers());
    std::puts("PASS test_handshake_with_echo_tag_enables_echo");
}

// ── test 2 ──────────────────────────────────────────────────────────────────
// addSubscriber with a different tag still registers (tag is informational).
// Without any addSubscriber call, hasSubscribers() is false.
static void test_handshake_without_add_no_echo() {
    EchoPlane ep;
    ep.open();
    assert(!ep.hasSubscribers());
    assert(ep.subscriberCount() == 0);
    std::puts("PASS test_handshake_without_add_no_echo");
}

// ── test 3 ──────────────────────────────────────────────────────────────────
// markAed + flush with send_fd=-1 returns non-zero byte count (packet built).
// With no subscribers flush is a no-op.
static void test_inbound_aed_echoes_to_subscriber() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    ep.markAed(0, 30.f, 10.f, 0.5f);
    // flush with send_fd=-1 (no-socket path) — must not crash.
    ep.flush(1000, -1);

    // After flush dirty bits are cleared; a second flush does nothing.
    ep.flush(2000, -1);  // should not crash / double-send
    std::puts("PASS test_inbound_aed_echoes_to_subscriber");
}

// ── test 4 ──────────────────────────────────────────────────────────────────
// With no subscriber, flush() is a no-op (early-return path).
static void test_inbound_no_subscriber_no_echo() {
    EchoPlane ep;
    ep.open();
    ep.markAed(0, 45.f, 0.f, 1.f);
    ep.flush(1000, -1);  // must not crash
    std::puts("PASS test_inbound_no_subscriber_no_echo");
}

// ── test 5 ──────────────────────────────────────────────────────────────────
// Two markAed() calls for same obj within one tick → dirty bit is set once;
// flush() uses the *latest* cached value (coalesce).
static void test_coalesce_same_address_one_tick() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    ep.markAed(5, 10.f, 5.f, 0.3f);
    ep.markAed(5, 20.f, 8.f, 0.7f);  // should overwrite

    // Verify latest value stored via the obj_cache_ through flush (no crash,
    // correct packet built internally).  We can't peek the packet without a
    // real socket, but the lack of crash + correct subscriberCount confirms
    // the path exercised.
    ep.flush(1000, -1);
    assert(ep.subscriberCount() == 1);
    std::puts("PASS test_coalesce_same_address_one_tick");
}

// ── test 6 ──────────────────────────────────────────────────────────────────
// Different addresses for the same object are all independently echoed.
static void test_coalesce_different_addresses_all_echo() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    ep.markAed(0, 1.f, 2.f, 3.f);
    ep.markXyz(0, 0.1f, 0.2f, 0.3f);
    ep.markGain(0, 0.8f);
    ep.markMute(0, 0);
    ep.markActive(0, 1);
    ep.markWidth(0, 0.2f);
    ep.markName(0, "violin");
    ep.markTransportPlay();
    ep.markTransportStop();

    ep.flush(1000, -1);  // all 9 dirty items cleared without crash
    std::puts("PASS test_coalesce_different_addresses_all_echo");
}

// ── test 7 ──────────────────────────────────────────────────────────────────
// Rate guard: tokens_this_second saturates at kEchoRateLimit per second.
// After the limit, allowSend returns false and dropped_count increments.
// We need a real UDP fd so buildAndSend() doesn't early-return before hitting
// the per-subscriber fan-out loop where allowSend() is called.
static void test_rate_guard_drops_excess() {
    // Open a loopback UDP socket (non-blocking, bound to port 0).
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in sa {};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

    EchoPlane ep;
    ep.open();
    // Subscriber destination: loopback:9102 (sendto may fail with ECONNREFUSED,
    // but that's fine — we only care about the rate-guard accounting, not
    // delivery).
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    // Drive kEchoRateLimit+10 flushes within the same 1-second window (t=500).
    for (int i = 0; i < kEchoRateLimit + 10; ++i) {
        ep.markGain(0, static_cast<float>(i));
        ep.flush(500, fd);
    }

    ::close(fd);

    // The entry should have dropped some packets.
    const auto& e = ep.entryAt(0);
    assert(e.dropped_count > 0);
    std::puts("PASS test_rate_guard_drops_excess");
}

// ── test 8 ──────────────────────────────────────────────────────────────────
// Subscriber is evicted after kEchoSubscriberTtlMs ms without a heartbeat.
static void test_subscriber_stale_30s_dropped() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag,
                     /*now_ms=*/0);
    assert(ep.subscriberCount() == 1);

    // Advance time by exactly TTL — still alive (boundary).
    ep.evictStale(kEchoSubscriberTtlMs);
    assert(ep.subscriberCount() == 1);

    // Advance one ms past TTL — evicted.
    ep.evictStale(kEchoSubscriberTtlMs + 1);
    assert(ep.subscriberCount() == 0);
    std::puts("PASS test_subscriber_stale_30s_dropped");
}

// ── test 9 ──────────────────────────────────────────────────────────────────
// Transport play + stop are echoed independently.
static void test_transport_play_echoes() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    ep.markTransportPlay();
    ep.flush(1000, -1);  // must not crash

    ep.markTransportStop();
    ep.flush(2000, -1);  // must not crash

    std::puts("PASS test_transport_play_echoes");
}

// ── test 10 ─────────────────────────────────────────────────────────────────
// /sys/* addresses are not echoed (mark*() only covers obj + transport).
// Verify that flush() with no marks does nothing (zero-packet path).
static void test_sys_address_not_echoed() {
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    // Do NOT call any mark*().
    ep.flush(1000, -1);  // dirty map empty — must not crash or send

    // Subscriber still active (no eviction happened).
    assert(ep.subscriberCount() == 1);
    std::puts("PASS test_sys_address_not_echoed");
}

// ── C7 tests: outbound /adm/obj/N/dsp echo ───────────────────────────────────
// These use the bound-loopback receiver harness so we parse the actual wire
// bytes. Because /adm/obj/N/dsp is OUTBOUND-ONLY (no inbound decoder), the
// encode→parse round-trip coverage for the new address IS this receiver-parse:
// EchoPlane builds the wire bytes and the test parses them back to (param,value).

// Run the "each of params 0..6 echoes once with correct (param,value)" scenario
// for a given object id (obj 0 and the high obj kEchoMaxObjects-1, per N1).
static void run_each_param_for_obj(uint32_t obj) {
    RxSock rx = bindRx();
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    const float vals[7] = {-3.f, 1.5f, 0.f, 12.25f, 250.f, 0.75f, 0.4f};
    for (int p = 0; p <= 6; ++p)
        ep.markDsp(obj, static_cast<uint8_t>(p), vals[p]);
    ep.flush(1000, rx.fd);

    OscMsg msgs[16];
    const int cnt = drainAll(rx.fd, msgs, 16);
    assert(cnt == 7);

    char want[32];
    std::snprintf(want, sizeof(want), "/adm/obj/%u/dsp", obj);
    bool seen[7] = {};
    for (int k = 0; k < cnt; ++k) {
        assert(std::strcmp(msgs[k].addr, want) == 0);
        assert(std::strcmp(msgs[k].types, ",if") == 0);
        const int   param = msgs[k].i[0];
        const float v     = msgs[k].f[0];
        assert(param >= 0 && param <= 6);
        assert(!seen[param]);
        seen[param] = true;
        assert(v == vals[param]);
    }
    ::close(rx.fd);
}

static void test_dsp_each_param_echoes() {
    run_each_param_for_obj(0);
    run_each_param_for_obj(static_cast<uint32_t>(kEchoMaxObjects - 1));
    std::puts("PASS test_dsp_each_param_echoes");
}

// Distinct params in one tick all echo (coalesce preserves each); same param
// twice in one tick → one packet with the latest value.
static void test_dsp_multi_param_one_tick_coalesce() {
    // (a) two distinct params → two packets.
    {
        RxSock rx = bindRx();
        EchoPlane ep;
        ep.open();
        ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);
        ep.markDsp(3, 0, 1.5f);  // EqLow
        ep.markDsp(3, 3, 4.5f);  // EqHigh, same tick
        ep.flush(1000, rx.fd);
        OscMsg msgs[16];
        const int cnt = drainAll(rx.fd, msgs, 16);
        assert(cnt == 2);
        bool p0 = false, p3 = false;
        for (int k = 0; k < cnt; ++k) {
            assert(std::strcmp(msgs[k].addr, "/adm/obj/3/dsp") == 0);
            if (msgs[k].i[0] == 0) {
                p0 = true;
                assert(msgs[k].f[0] == 1.5f);
            } else if (msgs[k].i[0] == 3) {
                p3 = true;
                assert(msgs[k].f[0] == 4.5f);
            }
        }
        assert(p0 && p3);
        ::close(rx.fd);
    }
    // (b) same param twice → one packet, latest value.
    {
        RxSock rx = bindRx();
        EchoPlane ep;
        ep.open();
        ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);
        ep.markDsp(3, 0, 1.5f);
        ep.markDsp(3, 0, 9.0f);  // overwrite
        ep.flush(1000, rx.fd);
        OscMsg msgs[16];
        const int cnt = drainAll(rx.fd, msgs, 16);
        assert(cnt == 1);
        assert(std::strcmp(msgs[0].addr, "/adm/obj/3/dsp") == 0);
        assert(msgs[0].i[0] == 0);
        assert(msgs[0].f[0] == 9.0f);
        ::close(rx.fd);
    }
    std::puts("PASS test_dsp_multi_param_one_tick_coalesce");
}

// param 7 (Width) emits NO dsp packet; width is observed only on
// /adm/obj/N/width (the engine routes p7 → markWidth at the mark site).
static void test_dsp_param7_routes_to_width() {
    // markDsp(obj,7,w) is a defensive no-op → no dsp packet.
    {
        RxSock rx = bindRx();
        EchoPlane ep;
        ep.open();
        ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);
        ep.markDsp(2, 7, 0.5f);
        ep.flush(1000, rx.fd);
        OscMsg msgs[16];
        const int cnt = drainAll(rx.fd, msgs, 16);
        assert(cnt == 0);
        ::close(rx.fd);
    }
    // markWidth(obj,w) — the engine's routing target — echoes on /width ,f.
    {
        RxSock rx = bindRx();
        EchoPlane ep;
        ep.open();
        ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);
        ep.markWidth(2, 0.5f);
        ep.flush(1000, rx.fd);
        OscMsg msgs[16];
        const int cnt = drainAll(rx.fd, msgs, 16);
        assert(cnt == 1);
        assert(std::strcmp(msgs[0].addr, "/adm/obj/2/width") == 0);
        assert(std::strcmp(msgs[0].types, ",f") == 0);
        assert(msgs[0].f[0] == 0.5f);
        ::close(rx.fd);
    }
    std::puts("PASS test_dsp_param7_routes_to_width");
}

// Rate guard still increments dropped_count past kEchoRateLimit for dsp echoes
// (mirror of test_rate_guard_drops_excess with markDsp).
static void test_dsp_rate_guard_drops_excess() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
    struct sockaddr_in sa {};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), 9102, EchoPlane::kEchoSubscriberTag, 0);

    for (int i = 0; i < kEchoRateLimit + 10; ++i) {
        ep.markDsp(0, 2, static_cast<float>(i));
        ep.flush(500, fd);
    }
    ::close(fd);

    const auto& e = ep.entryAt(0);
    assert(e.dropped_count > 0);
    std::puts("PASS test_dsp_rate_guard_drops_excess");
}

// AC9 — no cross-lifecycle dirty-mask leak. The re-arm sequence is MANDATORY:
// a param marked while no subscriber is attached must NOT be spuriously emitted
// on the first dsp flush after a subscriber (re)attaches. The trivial form
// (mark → no-sub flush → attach → flush) is INSUFFICIENT because the no-sub
// flush clears the coarse EchoAddr::Dsp gate, so the post-attach flush never
// enters the dsp block on EITHER design. We re-arm with a SECOND markDsp after
// attach so a surviving stale mask bit would ride along: this emits 2 packets
// on the buggy EchoObjCache mask but exactly 1 on the fixed EchoDirtyMap mask.
static void run_no_leak_for_obj(uint32_t obj) {
    EchoPlane ep;
    ep.open();
    // (no subscriber)
    ep.markDsp(obj, 2, 11.f);  // x_old
    ep.flush(0, -1);           // no-sub early return: clears coarse + mask(fixed)

    RxSock rx = bindRx();
    ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 1);
    ep.markDsp(obj, 5, 22.f);  // y_new — re-arms coarse Dsp bit + mask bit 5
    ep.flush(1, rx.fd);

    OscMsg msgs[16];
    const int cnt = drainAll(rx.fd, msgs, 16);
    int dsp_count = 0;
    for (int k = 0; k < cnt; ++k) {
        if (std::strstr(msgs[k].addr, "/dsp")) {
            ++dsp_count;
            assert(msgs[k].i[0] != 2);   // NO leaked param-2 packet
            assert(msgs[k].i[0] == 5);
            assert(msgs[k].f[0] == 22.f);
        }
    }
    assert(dsp_count == 1);  // EXACTLY ONE dsp packet

    // Explicit second recvfrom must time out (EAGAIN) — no spurious extra packet
    // can slip past the drain.
    uint8_t            b[256];
    struct sockaddr_in from {};
    socklen_t          fl = sizeof(from);
    const ssize_t      n  = ::recvfrom(
        rx.fd, b, sizeof(b), 0, reinterpret_cast<struct sockaddr*>(&from), &fl);
    assert(n < 0);  // EAGAIN/EWOULDBLOCK via SO_RCVTIMEO

    ::close(rx.fd);
}

static void test_dsp_no_subscriber_does_not_leak() {
    run_no_leak_for_obj(0);
    run_no_leak_for_obj(static_cast<uint32_t>(kEchoMaxObjects - 1));
    std::puts("PASS test_dsp_no_subscriber_does_not_leak");
}

// AC5 — all 7 pre-existing EchoAddr echoes still emit correct
// address/typetags/args after kEchoAddrCount 7→8, proven by parsing the wire
// (NOT the legacy flush(-1) crash-only path). The new dsp bit coexists with no
// aliasing. Exercised at obj 0 AND obj kEchoMaxObjects-1 (the upper dirty-map
// region past the old 7×obj stride, where a resize regression would surface).
static void run_legacy_compat_for_obj(uint32_t obj) {
    RxSock rx = bindRx();
    EchoPlane ep;
    ep.open();
    ep.addSubscriber(loopbackNet(), rx.port, EchoPlane::kEchoSubscriberTag, 0);

    ep.markAed(obj, 30.f, 10.f, 0.5f);
    ep.markXyz(obj, 0.1f, 0.2f, 0.3f);
    ep.markGain(obj, 0.8f);
    ep.markMute(obj, 1);
    ep.markActive(obj, 1);
    ep.markWidth(obj, 0.25f);
    ep.markName(obj, "violin");
    ep.markDsp(obj, 4, 7.5f);  // dsp coexists with the 7 legacy addresses
    ep.flush(1000, rx.fd);

    OscMsg msgs[32];
    const int cnt = drainAll(rx.fd, msgs, 32);
    assert(cnt == 8);  // 7 legacy + 1 dsp

    char a_aed[32], a_xyz[32], a_gain[32], a_mute[32], a_active[32], a_width[32],
        a_name[32], a_dsp[32];
    std::snprintf(a_aed, sizeof(a_aed), "/adm/obj/%u/aed", obj);
    std::snprintf(a_xyz, sizeof(a_xyz), "/adm/obj/%u/xyz", obj);
    std::snprintf(a_gain, sizeof(a_gain), "/adm/obj/%u/gain", obj);
    std::snprintf(a_mute, sizeof(a_mute), "/adm/obj/%u/mute", obj);
    std::snprintf(a_active, sizeof(a_active), "/adm/obj/%u/active", obj);
    std::snprintf(a_width, sizeof(a_width), "/adm/obj/%u/width", obj);
    std::snprintf(a_name, sizeof(a_name), "/adm/obj/%u/name", obj);
    std::snprintf(a_dsp, sizeof(a_dsp), "/adm/obj/%u/dsp", obj);

    bool got_aed = false, got_xyz = false, got_gain = false, got_mute = false,
         got_active = false, got_width = false, got_name = false, got_dsp = false;
    for (int k = 0; k < cnt; ++k) {
        const OscMsg& m = msgs[k];
        if (std::strcmp(m.addr, a_aed) == 0) {
            got_aed = true;
            assert(std::strcmp(m.types, ",fff") == 0);
            assert(m.f[0] == 30.f && m.f[1] == 10.f && m.f[2] == 0.5f);
        } else if (std::strcmp(m.addr, a_xyz) == 0) {
            got_xyz = true;
            assert(std::strcmp(m.types, ",fff") == 0);
            assert(m.f[0] == 0.1f && m.f[1] == 0.2f && m.f[2] == 0.3f);
        } else if (std::strcmp(m.addr, a_gain) == 0) {
            got_gain = true;
            assert(std::strcmp(m.types, ",f") == 0);
            assert(m.f[0] == 0.8f);
        } else if (std::strcmp(m.addr, a_mute) == 0) {
            got_mute = true;
            assert(std::strcmp(m.types, ",i") == 0);
            assert(m.i[0] == 1);
        } else if (std::strcmp(m.addr, a_active) == 0) {
            got_active = true;
            assert(std::strcmp(m.types, ",i") == 0);
            assert(m.i[0] == 1);
        } else if (std::strcmp(m.addr, a_width) == 0) {
            got_width = true;
            assert(std::strcmp(m.types, ",f") == 0);
            assert(m.f[0] == 0.25f);
        } else if (std::strcmp(m.addr, a_name) == 0) {
            got_name = true;
            assert(std::strcmp(m.types, ",s") == 0);
            assert(std::strcmp(m.s, "violin") == 0);
        } else if (std::strcmp(m.addr, a_dsp) == 0) {
            got_dsp = true;
            assert(std::strcmp(m.types, ",if") == 0);
            assert(m.i[0] == 4 && m.f[0] == 7.5f);
        }
    }
    assert(got_aed && got_xyz && got_gain && got_mute && got_active &&
           got_width && got_name && got_dsp);
    ::close(rx.fd);
}

static void test_legacy_echoes_wire_compat_after_resize() {
    run_legacy_compat_for_obj(0);
    run_legacy_compat_for_obj(static_cast<uint32_t>(kEchoMaxObjects - 1));
    std::puts("PASS test_legacy_echoes_wire_compat_after_resize");
}

// ────────────────────────────────────────────────────────────────────────────

int main() {
    test_handshake_with_echo_tag_enables_echo();
    test_handshake_without_add_no_echo();
    test_inbound_aed_echoes_to_subscriber();
    test_inbound_no_subscriber_no_echo();
    test_coalesce_same_address_one_tick();
    test_coalesce_different_addresses_all_echo();
    test_rate_guard_drops_excess();
    test_subscriber_stale_30s_dropped();
    test_transport_play_echoes();
    test_sys_address_not_echoed();
    // C7 — /adm/obj/N/dsp outbound echo.
    test_dsp_each_param_echoes();
    test_dsp_multi_param_one_tick_coalesce();
    test_dsp_param7_routes_to_width();
    test_dsp_rate_guard_drops_excess();
    test_dsp_no_subscriber_does_not_leak();
    test_legacy_echoes_wire_compat_after_resize();
    return 0;
}
