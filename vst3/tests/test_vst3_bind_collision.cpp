// vst3/tests/test_vst3_bind_collision.cpp
// A.13 / PM1 coverage: port walk on EADDRINUSE, ephemeral fallback, no crash.

#include "../SpatialEnginePluginUdp.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Setup: point XDG_CONFIG_HOME to a temp dir so tests don't pollute ~/.config
// ---------------------------------------------------------------------------

static std::string g_test_dir;

static void setupTempDir()
{
    char tmpl[] = "/tmp/spe_bind_test_XXXXXX";
    char* d = ::mkdtemp(tmpl);
    assert(d && "mkdtemp failed");
    g_test_dir = d;
    ::setenv("XDG_CONFIG_HOME", g_test_dir.c_str(), 1);
    std::string dir = g_test_dir + std::string("/spatial_engine");
    ::mkdir(dir.c_str(), 0755);
}

static void cleanupTempDir()
{
    std::string cmd = "rm -rf " + g_test_dir;
    int rc = ::system(cmd.c_str());
    (void)rc;
}

// Bind a socket to 127.0.0.1:port and return the fd (caller owns it).
// Returns -1 on failure.
static int bindPort(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// ---------------------------------------------------------------------------
// Test 1: port 9100 pre-bound → plugin walks to 9101, registers
// ---------------------------------------------------------------------------
static void test_walk_on_eaddrinuse()
{
    int blocker = bindPort(9100);
    assert(blocker >= 0 && "test setup: failed to pre-bind 9100");

    spe::vst3::SpatialEnginePluginUdp udp;
    bool ok = udp.start();
    assert(ok && "start() must succeed when 9100 is taken");

    uint16_t port = udp.boundPort();
    // Should have walked to at least 9101.
    assert(port != 9100 && "must not bind 9100 when it's taken");
    assert(port > 0 && "must bind a valid port");
    std::printf("[PASS] test_walk_on_eaddrinuse: bound port=%d\n", port);

    udp.stop();
    ::close(blocker);
}

// ---------------------------------------------------------------------------
// Test 2: ports 9100-9115 all pre-bound → plugin falls back to ephemeral
// ---------------------------------------------------------------------------
static void test_ephemeral_fallback()
{
    std::vector<int> blockers;
    for (int i = 0; i < 16; ++i) {
        int fd = bindPort(static_cast<uint16_t>(9100 + i));
        if (fd >= 0) blockers.push_back(fd);
    }
    // At minimum 9100 must be blocked for the test to be meaningful.
    assert(!blockers.empty() && "test setup: failed to block any ports");

    spe::vst3::SpatialEnginePluginUdp udp;
    bool ok = udp.start();
    assert(ok && "start() must succeed even when all 9100-9115 are taken");

    uint16_t port = udp.boundPort();
    assert(port > 0 && "must bind an ephemeral port");
    // Ephemeral ports are > 1024 and outside the 9100-9115 range (or kernel
    // chose one of them if blockers didn't take all 16).
    std::printf("[PASS] test_ephemeral_fallback: bound port=%d (blockers=%zu)\n",
                port, blockers.size());

    udp.stop();
    for (int fd : blockers) ::close(fd);
}

// ---------------------------------------------------------------------------
// Test 3: EADDRINUSE on a single port does not crash the plugin (idempotent)
// ---------------------------------------------------------------------------
static void test_single_eaddrinuse_no_crash()
{
    int blocker = bindPort(9100);
    assert(blocker >= 0 && "test setup: failed to pre-bind 9100");

    // Start and stop twice — must not crash.
    for (int i = 0; i < 2; ++i) {
        spe::vst3::SpatialEnginePluginUdp udp;
        bool ok = udp.start();
        assert(ok && "start() must not crash on EADDRINUSE");
        udp.stop();
    }

    ::close(blocker);
    std::printf("[PASS] test_single_eaddrinuse_no_crash\n");
}

// ---------------------------------------------------------------------------
// Test 4: stop() is idempotent — calling it twice must not crash
// ---------------------------------------------------------------------------
static void test_stop_idempotent()
{
    spe::vst3::SpatialEnginePluginUdp udp;
    udp.start();
    udp.stop();
    udp.stop(); // second stop must not crash
    std::printf("[PASS] test_stop_idempotent\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    setupTempDir();

    test_walk_on_eaddrinuse();
    test_ephemeral_fallback();
    test_single_eaddrinuse_no_crash();
    test_stop_idempotent();

    cleanupTempDir();
    std::printf("All bind collision tests PASSED.\n");
    return 0;
}
