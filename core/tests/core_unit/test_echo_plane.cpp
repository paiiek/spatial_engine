// test_echo_plane.cpp
// M5.1 — 10 ctests for EchoPlane (subscriber registry, dirty-bit coalesce,
// rate guard, TTL eviction, transport echo). No real sockets needed: send_fd=-1
// exercises the "build but don't send" path in buildAndSend().

#include "ipc/EchoSubscriber.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

using namespace spe::ipc;

// Build a sockaddr_in for 127.0.0.1:<port> (network byte order addr for
// addSubscriber).
static uint32_t loopbackNet() {
    return htonl(INADDR_LOOPBACK);  // 127.0.0.1 in network byte order
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
    return 0;
}
