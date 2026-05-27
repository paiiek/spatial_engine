// test_osc_outbound_reply_smoke.cpp
//
// v0.5.1 Q1 — exercise the OSCBackend outbound reply channel end-to-end.
//
// Path under test (stub-mode OSCBackend, JUCE-off build):
//   1. Bind a loopback UDP listener on an ephemeral port (the "client").
//   2. Send a synthetic OSC packet from that socket into OSCBackend.
//   3. recvfrom() captures the sender → last_peer_endpoint_.
//   4. From the control thread, call OSCBackend::sendReply().
//   5. The IO drain thread sendto()s to last_peer_endpoint_.
//   6. The client recvfrom()s the reply within 200 ms.
//
// WM-2 (legacy-client sub-case): the handshake we inject carries
// reply_port == 0, so the reply MUST land at the client's source port (the
// ephemeral port we bound in step 1), captured via recvfrom().

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

namespace {

constexpr int kReplyTimeoutMs = 200;

// Bind a UDP socket to 127.0.0.1 on an ephemeral port. Returns (fd, port).
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
    // 200 ms recv timeout so the test can fail fast.
    struct timeval tv{0, kReplyTimeoutMs * 1000};
    ::setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

// Build /sys/handshake ,i <schema>  packet (legacy single-int form).
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

// Wait for a reply on the client socket; return bytes received, or -1 on timeout.
ssize_t waitForReply(int client_fd, uint8_t* buf, size_t cap,
                     struct sockaddr_in* from) {
    socklen_t fromlen = sizeof(*from);
    return ::recvfrom(client_fd, buf, cap, 0,
                      reinterpret_cast<struct sockaddr*>(from), &fromlen);
}

bool addr_contains(const uint8_t* buf, ssize_t n, const char* needle) {
    if (n <= 0) return false;
    const size_t needle_len = std::strlen(needle);
    if (static_cast<size_t>(n) < needle_len) return false;
    return std::memcmp(buf, needle, needle_len) == 0;
}

// ─────────────────────────────────────────────────────────────────────────
// Sub-test runner: returns true on success, false on failure (and prints
// diagnostic to stderr). Each run uses fresh sockets / a fresh backend.
// ─────────────────────────────────────────────────────────────────────────
bool runSmokeRound(int round_idx) {
    // 1. Client socket — also the engine's reply target (WM-2 legacy path).
    LoopbackSocket client = bindLoopback();
    if (client.fd < 0) {
        std::fprintf(stderr, "[round %d] client bind failed\n", round_idx);
        return false;
    }

    // 2. Engine OSCBackend on a separate ephemeral port. We use a sink that
    //    records the decoded command tag so we can assert handshake decode.
    std::atomic<int> sink_tag_seen{0};
    auto sink = [&](const spe::ipc::Command& cmd) {
        sink_tag_seen.fetch_add(1, std::memory_order_relaxed);
        (void)cmd;
    };

    // To get an ephemeral port we need to bind first; the easiest path is
    // to use a temporary socket to grab a free port, close it, then hand
    // the port to OSCBackend. There is a tiny TOCTOU but in practice the
    // ephemeral port is rarely reused within microseconds.
    LoopbackSocket scout = bindLoopback();
    const uint16_t engine_port = scout.port;
    ::close(scout.fd);

    spe::ipc::OSCBackend backend(sink, static_cast<int>(engine_port));
    backend.start();

    // Wait for the engine listener to be fully bound (deterministic readiness).
    {
        auto ready_dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < ready_dl) {
            if (backend.boundPortForTest() > 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (backend.boundPortForTest() == 0) {
            std::fprintf(stderr, "[round %d] backend never bound UDP socket\n", round_idx);
            backend.stop();
            ::close(client.fd);
            return false;
        }
    }

    // 3. Send the handshake packet from client → engine.
    auto pkt = buildHandshakePacket(1);
    struct sockaddr_in engine_addr{};
    engine_addr.sin_family      = AF_INET;
    engine_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    engine_addr.sin_port        = htons(engine_port);
    ::sendto(client.fd, pkt.data(), pkt.size(), 0,
             reinterpret_cast<struct sockaddr*>(&engine_addr), sizeof(engine_addr));

    // Wait for the engine to capture the peer endpoint.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.hasPeerEndpoint()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!backend.hasPeerEndpoint()) {
        std::fprintf(stderr, "[round %d] backend never captured peer endpoint\n",
                     round_idx);
        backend.stop();
        ::close(client.fd);
        return false;
    }

    // 4. Drive sendReply() from the control thread (this main thread).
    const bool enqueued = backend.sendReply("/sys/binaural_warning", ",s",
                                             "no_sofa_loaded");
    if (!enqueued) {
        std::fprintf(stderr, "[round %d] sendReply enqueue failed\n", round_idx);
        backend.stop();
        ::close(client.fd);
        return false;
    }

    // 5. Wait for reply at client socket — must arrive within 200 ms.
    uint8_t rxbuf[256] = {0};
    struct sockaddr_in from{};
    ssize_t n = waitForReply(client.fd, rxbuf, sizeof(rxbuf), &from);
    if (n <= 0) {
        std::fprintf(stderr, "[round %d] no reply within %dms (n=%zd, errno=%d)\n",
                     round_idx, kReplyTimeoutMs, n, errno);
        backend.stop();
        ::close(client.fd);
        return false;
    }

    // 6. Validate reply content + source.
    if (!addr_contains(rxbuf, n, "/sys/binaural_warning")) {
        std::fprintf(stderr, "[round %d] reply addr mismatch (first bytes: %.*s)\n",
                     round_idx, static_cast<int>(std::min<ssize_t>(n, 32)),
                     reinterpret_cast<const char*>(rxbuf));
        backend.stop();
        ::close(client.fd);
        return false;
    }

    // WM-2: the reply MUST have come from the engine port and we must have
    // received it on OUR client port (which is the source port we sent the
    // handshake from — i.e. last_peer_endpoint_'s captured port).
    if (ntohs(from.sin_port) != engine_port) {
        std::fprintf(stderr, "[round %d] reply src port %d != engine %d\n",
                     round_idx, ntohs(from.sin_port), engine_port);
        backend.stop();
        ::close(client.fd);
        return false;
    }
    if (sink_tag_seen.load() < 1) {
        std::fprintf(stderr, "[round %d] sink never saw handshake decode\n", round_idx);
        backend.stop();
        ::close(client.fd);
        return false;
    }

    backend.stop();
    ::close(client.fd);
    return true;
}

} // namespace

int main() {
    // Acceptance criterion: ≥10 consecutive passes on ephemeral ports.
    constexpr int kRounds = 10;
    for (int i = 0; i < kRounds; ++i) {
        if (!runSmokeRound(i)) {
            std::fprintf(stderr, "FAIL: round %d failed\n", i);
            return 1;
        }
    }
    std::printf("PASS test_osc_outbound_reply_smoke (%d rounds — WM-2 legacy path)\n",
                kRounds);
    return 0;
}
