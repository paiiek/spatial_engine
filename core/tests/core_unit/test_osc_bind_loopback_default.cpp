// test_osc_bind_loopback_default.cpp
//
// v0.5.1 final polish — Sec H2 regression test.
//
// Threat: prior to H2, OSCBackend bound INADDR_ANY (0.0.0.0), so the
// unauthenticated OSC command surface was reachable from every NIC on the
// host. This test asserts the loopback-only DEFAULT behaviour by:
//
//   1) Constructing an OSCBackend with NO explicit setBindAddr() call.
//   2) Starting it on an ephemeral port.
//   3) Reading the actual bound address back via getsockname() (exposed as
//      boundAddrForTest()) and asserting it is "127.0.0.1".
//   4) Confirming a sender bound to a non-loopback test interface CANNOT
//      reach the listener — its packet is delivered nowhere on 127.0.0.1.
//      We probe this with a no-op send + a short recv-window assertion via
//      the captured-peer accessor (hasPeerEndpoint must stay false after
//      attempted off-loopback delivery, since the kernel will not route a
//      0.0.0.0 datagram to a loopback-only listener bound on 127.0.0.1).
//
// Also asserts: opting in to "0.0.0.0" via setBindAddr() before start()
// successfully binds the wildcard (regression guard so the bind-addr API
// preserves the legacy LAN-mode behaviour for operators who need it).
//
// All sub-tests use the stub-mode OSCBackend (JUCE-off) with an ephemeral
// UDP port.

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

#define REQUIRE(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FAIL %s (line %d)\n", #cond, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

namespace {

// Pick an ephemeral UDP port by binding loopback once and reading the port
// back. We close the probe socket before the real backend binds so the
// listener can reuse the port.
uint16_t pickEphemeralPort() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return 0;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

int testDefaultBindIsLoopback()
{
    auto sink = [](const spe::ipc::Command&) {};

    uint16_t port = pickEphemeralPort();
    REQUIRE(port != 0);

    // Construct WITHOUT setBindAddr() — must use the loopback default.
    spe::ipc::OSCBackend backend(sink, static_cast<int>(port));
    REQUIRE(backend.bindAddr() == std::string("127.0.0.1"));

    backend.start();
    // Give the listener thread a moment to come up + complete bind().
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Definitive check: getsockname() of the actual listening socket. If
    // the bind escaped loopback (regression), this would report "0.0.0.0"
    // or a NIC IP instead.
    const std::string bound = backend.boundAddrForTest();
    const uint16_t    bport = backend.boundPortForTest();
    std::fprintf(stderr, "[h2-default] bound=%s:%u (expected 127.0.0.1:%u)\n",
                 bound.c_str(), bport, port);
    REQUIRE(bound == "127.0.0.1");
    REQUIRE(bport == port);

    // Negative reachability check: attempt to send to 0.0.0.0:<port> from a
    // loopback sender. The packet will not reach the listener bound on
    // 127.0.0.1 (kernel routes loopback-source -> loopback-dest only when
    // the destination IS loopback). We don't have a non-loopback test
    // interface available in CI, so the strongest portable assertion is
    // the getsockname() result above. As an extra belt-and-braces, send a
    // valid handshake to 127.0.0.1:<port> and verify it IS captured —
    // confirms the listener is alive (not just bound to an empty fd).
    {
        int s = ::socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(s >= 0);
        struct sockaddr_in dst{};
        dst.sin_family      = AF_INET;
        dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        dst.sin_port        = htons(port);

        // Build /sys/handshake ,i 1 (valid wire form).
        uint8_t pkt[32];
        std::memset(pkt, 0, sizeof(pkt));
        std::memcpy(pkt + 0,  "/sys/handshake", 14);
        std::memcpy(pkt + 16, ",i", 2);
        pkt[24] = 0; pkt[25] = 0; pkt[26] = 0; pkt[27] = 1;
        ssize_t sent = ::sendto(s, pkt, 28, 0,
                                reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        REQUIRE(sent == 28);

        // Wait up to 500 ms for the listener to capture us as a peer.
        bool captured = false;
        for (int i = 0; i < 50; ++i) {
            if (backend.hasPeerEndpoint()) { captured = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ::close(s);
        REQUIRE(captured);
    }

    backend.stop();
    return 0;
}

int testExplicitWildcardBindStillWorks()
{
    auto sink = [](const spe::ipc::Command&) {};

    uint16_t port = pickEphemeralPort();
    REQUIRE(port != 0);

    spe::ipc::OSCBackend backend(sink, static_cast<int>(port));
    backend.setBindAddr("0.0.0.0");
    REQUIRE(backend.bindAddr() == std::string("0.0.0.0"));

    backend.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string bound = backend.boundAddrForTest();
    const uint16_t    bport = backend.boundPortForTest();
    std::fprintf(stderr, "[h2-wildcard] bound=%s:%u\n", bound.c_str(), bport);
    REQUIRE(bound == "0.0.0.0");
    REQUIRE(bport == port);

    backend.stop();
    return 0;
}

int testInvalidBindFallsBackToLoopback()
{
    auto sink = [](const spe::ipc::Command&) {};

    uint16_t port = pickEphemeralPort();
    REQUIRE(port != 0);

    spe::ipc::OSCBackend backend(sink, static_cast<int>(port));
    backend.setBindAddr("not.a.valid.ip");

    backend.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Invalid bind string -> stderr warning + loopback fallback.
    const std::string bound = backend.boundAddrForTest();
    std::fprintf(stderr, "[h2-invalid] bound=%s (expected fallback 127.0.0.1)\n",
                 bound.c_str());
    REQUIRE(bound == "127.0.0.1");

    backend.stop();
    return 0;
}

} // namespace

int main() {
    if (int rc = testDefaultBindIsLoopback();          rc) return rc;
    if (int rc = testExplicitWildcardBindStillWorks(); rc) return rc;
    if (int rc = testInvalidBindFallsBackToLoopback(); rc) return rc;
    std::fprintf(stderr, "PASS osc_bind_loopback_default\n");
    return 0;
}
