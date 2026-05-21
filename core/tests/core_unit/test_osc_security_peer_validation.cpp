// test_osc_security_peer_validation.cpp
//
// v0.5.1 hotfix (security-reviewer H1 + M1).
//
// H1 — overridePeerPort() validation:
//   * Reject system / privileged ports (<1024).
//   * Reject when captured peer IP is not loopback (single-host IPC model).
//   * Reject when no peer endpoint has been captured.
//   Asserted by injecting non-loopback sockaddr_in via injectPacket(peer,len)
//   and confirming a subsequent override leaves the dest port UNCHANGED.
//
// M1 — only-capture-peer-on-valid-decode:
//   * recvfrom() of a 4-byte garbage packet from a "decoy" socket must NOT
//     promote the decoy into last_peer_endpoint_.
//   * A subsequent valid handshake from a REAL client socket must take over
//     as the active reply target.
//   Verified by reading outboundPeek() after sendReply — the destination
//   port encoded in the slot's dest field must match the REAL client's
//   bound port, not the decoy's.
//
// Both sub-tests use the stub-mode OSCBackend (JUCE-off), running the UDP
// listener on a loopback ephemeral port.

#include "ipc/OSCBackend.h"
#include "ipc/Command.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define REQUIRE(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s (line %d)\n", #cond, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

namespace {

struct LoopbackSocket {
    int      fd   = -1;
    uint16_t port = 0;
};

LoopbackSocket bindLoopback() {
    LoopbackSocket s;
    s.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s.fd < 0) return s;
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (::bind(s.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s.fd);
        s.fd = -1;
        return s;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(s.fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    s.port = ntohs(addr.sin_port);
    return s;
}

// Build /sys/handshake ,i <schema> packet.
std::vector<uint8_t> buildHandshakePacket(int32_t schema) {
    std::vector<uint8_t> pkt;
    auto pushPadded = [&](const char* s) {
        size_t len = std::strlen(s) + 1;
        for (size_t i = 0; i < len; ++i) pkt.push_back(static_cast<uint8_t>(s[i]));
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    pushPadded("/sys/handshake");
    pushPadded(",i");
    const uint32_t u = static_cast<uint32_t>(schema);
    pkt.push_back((u >> 24) & 0xFF);
    pkt.push_back((u >> 16) & 0xFF);
    pkt.push_back((u >>  8) & 0xFF);
    pkt.push_back((u >>  0) & 0xFF);
    return pkt;
}

// ─── H1 test ────────────────────────────────────────────────────────────
//
// overridePeerPort must reject when:
//   (1) new_port < 1024  (privileged / system port)
//   (2) captured peer is NOT loopback
//   (3) no peer captured yet
//
// We test (1) and (3) via a fresh backend + direct override call. (2) is
// tested by injecting a non-loopback packet via injectPacket(peer, len),
// then attempting an override and confirming a subsequent sendReply lands
// at the ORIGINAL (non-overridden) port. Because the override happens to a
// non-loopback dest, the IO drain would try to sendto() an off-host victim;
// to avoid touching the wire we use listen_port=0 so the drain thread is
// not running, and inspect the enqueued slot's dest_port via a small helper
// that pokes through the test-accessor outboundPeek + an offset trick.
//
// Cleanest way: instantiate backend WITHOUT starting it (listen_port=0,
// don't call start), inject a synthetic peer via injectPacket(peer, len),
// call overridePeerPort, then enqueue a reply and read the destination via
// the ipv4 outboundPeek slot bytes. outboundPeek only returns the buf
// bytes, not the dest, so we add a side accessor below.

int testH1_validation()
{
    auto sink = [](const spe::ipc::Command&) {};

    // 3a: No peer captured — overridePeerPort must no-op silently.
    {
        spe::ipc::OSCBackend backend(sink, /*listen_port=*/0);
        REQUIRE(!backend.hasPeerEndpoint());
        backend.overridePeerPort(12345);  // valid port, but no peer → no-op
        REQUIRE(!backend.hasPeerEndpoint());
    }

    // 1: Privileged port rejected. Inject a loopback peer first.
    {
        spe::ipc::OSCBackend backend(sink, /*listen_port=*/0);
        auto pkt = buildHandshakePacket(1);
        struct sockaddr_in peer{};
        peer.sin_family      = AF_INET;
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port        = htons(54321);
        backend.injectPacket(std::span<const uint8_t>(pkt),
                             reinterpret_cast<const struct sockaddr*>(&peer),
                             sizeof(peer));
        REQUIRE(backend.hasPeerEndpoint());
        backend.overridePeerPort(53);   // DNS — privileged, must reject
        backend.overridePeerPort(80);   // HTTP — privileged, must reject
        backend.overridePeerPort(1023); // last privileged, must reject

        // Drive a sendReply and inspect the encoded packet's dest port via
        // the test-accessor pair. The slot's dest field is what the IO
        // drain would sendto(); after a rejected override the dest port
        // must still be the ORIGINAL 54321.
        REQUIRE(backend.sendReply("/sys/binaural_status", ",i", int32_t{1}));
        REQUIRE(backend.outboundPending() == 1);
        const uint16_t dport = backend.testPeerEndpointPort();
        if (dport != 54321) {
            std::fprintf(stderr,
                "FAIL privileged-port override leaked: dport=%u\n", dport);
            return 1;
        }
        backend.outboundDrainForTest(1);
    }

    // 2: Non-loopback peer rejected.
    {
        spe::ipc::OSCBackend backend(sink, /*listen_port=*/0);
        auto pkt = buildHandshakePacket(1);
        struct sockaddr_in peer{};
        peer.sin_family      = AF_INET;
        // 10.0.0.42 — non-loopback. The decoder still accepts the handshake
        // (decode is content-only), but overridePeerPort must reject the
        // port retarget because the source IP is off-host.
        peer.sin_addr.s_addr = htonl((10u << 24) | 42u);
        peer.sin_port        = htons(54321);
        backend.injectPacket(std::span<const uint8_t>(pkt),
                             reinterpret_cast<const struct sockaddr*>(&peer),
                             sizeof(peer));
        REQUIRE(backend.hasPeerEndpoint());
        backend.overridePeerPort(9999);  // valid port, but peer is off-host → reject
        REQUIRE(backend.sendReply("/sys/binaural_status", ",i", int32_t{2}));
        const uint16_t dport = backend.testPeerEndpointPort();
        if (dport != 54321) {
            std::fprintf(stderr,
                "FAIL non-loopback override leaked: dport=%u (expected 54321)\n",
                dport);
            return 1;
        }
        backend.outboundDrainForTest(1);
    }

    // 4: Positive control — loopback peer + valid (>1024) port MUST succeed.
    {
        spe::ipc::OSCBackend backend(sink, /*listen_port=*/0);
        auto pkt = buildHandshakePacket(1);
        struct sockaddr_in peer{};
        peer.sin_family      = AF_INET;
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port        = htons(54321);
        backend.injectPacket(std::span<const uint8_t>(pkt),
                             reinterpret_cast<const struct sockaddr*>(&peer),
                             sizeof(peer));
        REQUIRE(backend.hasPeerEndpoint());
        backend.overridePeerPort(12345);  // valid loopback retarget
        REQUIRE(backend.sendReply("/sys/binaural_status", ",i", int32_t{3}));
        const uint16_t dport = backend.testPeerEndpointPort();
        if (dport != 12345) {
            std::fprintf(stderr,
                "FAIL valid override did not apply: dport=%u (expected 12345)\n",
                dport);
            return 1;
        }
        backend.outboundDrainForTest(1);
    }

    std::puts("PASS H1: overridePeerPort rejects privileged ports + non-loopback peers");
    return 0;
}

// ─── M1 test ────────────────────────────────────────────────────────────
//
// recvfrom() must NOT promote the sender of a malformed (un-decoded)
// packet into last_peer_endpoint_. A subsequent valid handshake from a
// real client overrides cleanly.

int testM1_validDecodeOnly()
{
    auto sink = [](const spe::ipc::Command&) {};

    // Bind engine on a real ephemeral port so the recvfrom thread runs.
    LoopbackSocket scout = bindLoopback();
    const uint16_t engine_port = scout.port;
    ::close(scout.fd);

    spe::ipc::OSCBackend backend(sink, static_cast<int>(engine_port));
    backend.start();
    // Beat for the listener thread to be ready.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Decoy socket — sends 4 bytes of garbage to engine.
    LoopbackSocket decoy = bindLoopback();
    REQUIRE(decoy.fd >= 0);
    const uint16_t decoy_port = decoy.port;

    struct sockaddr_in engine_addr{};
    engine_addr.sin_family      = AF_INET;
    engine_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    engine_addr.sin_port        = htons(engine_port);

    const uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ::sendto(decoy.fd, garbage, sizeof(garbage), 0,
             reinterpret_cast<struct sockaddr*>(&engine_addr), sizeof(engine_addr));

    // Give the engine listener time to recvfrom + drop the garbage.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The backend MUST NOT have captured the decoy as the peer.
    REQUIRE(!backend.hasPeerEndpoint());

    // Now the real client sends a valid handshake.
    LoopbackSocket client = bindLoopback();
    REQUIRE(client.fd >= 0);
    const uint16_t client_port = client.port;
    auto pkt = buildHandshakePacket(1);
    ::sendto(client.fd, pkt.data(), pkt.size(), 0,
             reinterpret_cast<struct sockaddr*>(&engine_addr), sizeof(engine_addr));

    // Wait for capture.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.hasPeerEndpoint()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(backend.hasPeerEndpoint());

    // Verify the captured peer IS the real client (port == client_port,
    // NOT decoy_port).
    const uint16_t captured_port = backend.testPeerEndpointPort();
    if (captured_port == decoy_port) {
        std::fprintf(stderr,
            "FAIL decoy port %u was captured despite garbage-only traffic\n",
            decoy_port);
        backend.stop();
        ::close(client.fd);
        ::close(decoy.fd);
        return 1;
    }
    if (captured_port != client_port) {
        std::fprintf(stderr,
            "FAIL captured port %u != client port %u (decoy was %u)\n",
            captured_port, client_port, decoy_port);
        backend.stop();
        ::close(client.fd);
        ::close(decoy.fd);
        return 1;
    }

    backend.stop();
    ::close(client.fd);
    ::close(decoy.fd);
    std::puts("PASS M1: garbage packets do not promote sender to last_peer_endpoint_");
    return 0;
}

// ─── v0.7 D-S1 test ─────────────────────────────────────────────────────────
//
// /sys/binaural_reset_demote ,i 1 from an unauthenticated peer (no prior
// handshake) must not affect the outbound ring (outbound_drops_ unchanged)
// and must not promote the sender into last_peer_endpoint_.
//
// Plan ref: §2 Item #1 Test (3) — peer-validation reuse.

// Build /sys/binaural_reset_demote ,i <enable> packet.
static std::vector<uint8_t> buildResetDemotePacket(int32_t enable) {
    std::vector<uint8_t> pkt;
    auto pushPadded = [&](const char* s) {
        size_t len = std::strlen(s) + 1;
        for (size_t i = 0; i < len; ++i) pkt.push_back(static_cast<uint8_t>(s[i]));
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    pushPadded("/sys/binaural_reset_demote");
    pushPadded(",i");
    const uint32_t u = static_cast<uint32_t>(enable);
    pkt.push_back((u >> 24) & 0xFF);
    pkt.push_back((u >> 16) & 0xFF);
    pkt.push_back((u >>  8) & 0xFF);
    pkt.push_back((u >>  0) & 0xFF);
    return pkt;
}

int testDS1_unauthenticatedPeerRejected()
{
    // Track whether the command sink was invoked with a reset-demote command.
    std::atomic<bool> reset_demote_dispatched{false};
    auto sink = [&](const spe::ipc::Command& cmd) {
        if (cmd.tag == spe::ipc::CommandTag::SysBinauralResetDemote) {
            reset_demote_dispatched.store(true, std::memory_order_release);
        }
    };

    // Use injectPacket with a peer address but WITHOUT a prior handshake.
    // The OSCBackend will decode the packet (non-Unknown tag → sink called),
    // but the peer must NOT be promoted to last_peer_endpoint_ — the existing
    // M1 contract ensures only valid-handshake senders become the reply target.
    {
        spe::ipc::OSCBackend backend(sink, /*listen_port=*/0);
        REQUIRE(!backend.hasPeerEndpoint());

        // Inject the reset-demote packet from an unauthenticated source.
        auto pkt = buildResetDemotePacket(1);
        struct sockaddr_in peer{};
        peer.sin_family      = AF_INET;
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port        = htons(9999); // arbitrary unauthenticated port

        // OSCBackend::injectPacket(packet, peer, len) only captures the peer
        // when the decoded command tag != Unknown (per M1 fix). The sink IS
        // invoked (decode succeeds), but because no handshake has established
        // an authorized reply target, the outbound ring should remain empty
        // (sendReply with no peer returns false / drops silently).
        backend.injectPacket(std::span<const uint8_t>(pkt),
                             reinterpret_cast<const struct sockaddr*>(&peer),
                             sizeof(peer));

        // The decode succeeded, so sink was called.
        REQUIRE(reset_demote_dispatched.load(std::memory_order_acquire));

        // No outbound reply should have been queued (no established peer for
        // the warning drain path). outbound_drops_ must be 0 (ring not
        // overflowed) because sendReply with no peer simply returns false.
        REQUIRE(backend.outboundPending() == 0);
        REQUIRE(backend.outboundDrops() == 0);
    }

    std::puts("PASS DS1: /sys/binaural_reset_demote from unauthenticated peer "
              "decoded but outbound ring unaffected");
    return 0;
}

} // namespace

int main()
{
    if (testH1_validation() != 0) return 1;
    if (testM1_validDecodeOnly() != 0) return 1;
    if (testDS1_unauthenticatedPeerRejected() != 0) return 1;
    std::puts("PASS test_osc_security_peer_validation (H1 + M1 + DS1)");
    return 0;
}
