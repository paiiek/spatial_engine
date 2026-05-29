// test_p_sys_metrics_extended.cpp
//
// v0.9 Lane A (A-M1) — /sys/metrics 1 Hz emit, end-to-end (NO_JUCE ctest).
//
// Strategy (mirrors test_binaural_probe_warning_emission's real-engine path):
//   1. Bind a loopback client + an ephemeral engine OSC port.
//   2. Construct a real SpatialEngine on that port and prepareToPlay().
//   3. Drive several normal audioBlock()s to warm the CpuMeter (so cpu_pct /
//      p99_us are populated in the engine's ObservabilityCounters).
//   4. Force an OVERSIZED block (num_frames > MAX_BLOCK) — the engine's
//      overrun guard must bump engine_overrun_count without taking a wall
//      sample.
//   5. Handshake client → engine so the reply peer is captured.
//   6. Emit /sys/metrics exactly as core/src/bin/spatial_engine_core.cpp's
//      1 Hz tick does (one ,s key=value message per field — the production
//      emit code path under test).
//   7. recvfrom() each message, parse "key=value", assert field presence +
//      value ranges (cpu_pct∈[0,100], p99_us≥0, xrun_count present) and that
//      engine_overrun_count > 0.
//
// We construct the engine + emit directly (not via the binary) so the test is
// hermetic. The emit field formatting is intentionally identical to the
// binary's so the wire shape is exercised.

#include "core/SpatialEngine.h"
#include "audio_io/AudioCallback.h"
#include "ipc/Command.h"
#include "core/Constants.h"
#include "util/RtAssertNoAlloc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Sock {
    int      fd   = -1;
    uint16_t port = 0;
};

Sock bindEphemeralLoopback() {
    Sock s;
    s.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s.fd < 0) return s;
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (::bind(s.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s.fd); s.fd = -1; return s;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(s.fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    s.port = ntohs(addr.sin_port);
    struct timeval tv{0, 400000}; // 400 ms recv timeout
    ::setsockopt(s.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

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

// Extract the OSC string-argument from a /sys/metrics ,s packet.
// Layout: "/sys/metrics\0" (padded) + ",s\0\0" (padded) + "<kv>\0" (padded).
// Returns the kv string, or "" if the packet is not /sys/metrics ,s.
std::string parseMetricsKv(const uint8_t* buf, ssize_t n) {
    static const char kAddr[] = "/sys/metrics";
    const size_t addr_len = sizeof(kAddr) - 1;
    if (n <= 0 || static_cast<size_t>(n) < addr_len) return {};
    if (std::memcmp(buf, kAddr, addr_len) != 0) return {};
    // Address occupies a 4-byte-padded region (strlen+1 rounded up).
    size_t off = ((addr_len + 1) + 3) & ~size_t(3);
    if (off >= static_cast<size_t>(n)) return {};
    // Type tag ",s" padded to 4 bytes.
    if (std::memcmp(buf + off, ",s", 2) != 0) return {};
    off = (off + 4); // ",s\0\0"
    if (off >= static_cast<size_t>(n)) return {};
    // The argument string runs to its NUL terminator.
    const char* arg = reinterpret_cast<const char*>(buf + off);
    size_t maxlen = static_cast<size_t>(n) - off;
    size_t slen = ::strnlen(arg, maxlen);
    return std::string(arg, slen);
}

bool splitKv(const std::string& kv, std::string& key, long long& val) {
    auto eq = kv.find('=');
    if (eq == std::string::npos) return false;
    key = kv.substr(0, eq);
    const std::string vs = kv.substr(eq + 1);
    if (vs.empty()) return false;
    char* end = nullptr;
    val = std::strtoll(vs.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

} // namespace

int main() {
    Sock client = bindEphemeralLoopback();
    if (client.fd < 0) { std::fprintf(stderr, "FAIL: client bind\n"); return 1; }

    Sock scout = bindEphemeralLoopback();
    const uint16_t engine_port = scout.port;
    ::close(scout.fd);

    spe::core::SpatialEngine engine(static_cast<int>(engine_port));
    const double kSR = 48000.0;
    const int    kBlock = 512;
    engine.prepareToPlay(kSR, kBlock);

    // Planar zeroed output buffers for a normal block.
    const int n_ch = 8;
    std::vector<std::vector<float>> out(static_cast<size_t>(n_ch),
                                        std::vector<float>(static_cast<size_t>(kBlock), 0.f));
    std::vector<float*> ch_ptrs(static_cast<size_t>(n_ch));
    for (int c = 0; c < n_ch; ++c) ch_ptrs[static_cast<size_t>(c)] = out[static_cast<size_t>(c)].data();

    // 3. Warm the CpuMeter with several normal blocks. In an RT-asserts build
    //    (SPE_RT_ASSERTS=ON) audioBlock arms SPE_RT_NO_ALLOC_SCOPE internally,
    //    so the override records any heap alloc on the measurement path. We
    //    assert zero violations afterwards (AC2: alloc=0 on the hot path).
    spe::util::rt_alloc_violations_reset();
    for (int i = 0; i < 64; ++i) {
        spe::audio_io::AudioBlock blk;
        blk.output_channels      = ch_ptrs.data();
        blk.output_channel_count = n_ch;
        blk.num_frames           = kBlock;
        blk.sample_rate          = kSR;
        engine.audioBlock(blk);
    }
    if (spe::util::rt_alloc_violations() != 0) {
        std::fprintf(stderr, "FAIL: %llu RT alloc violations during audioBlock\n",
                     spe::util::rt_alloc_violations());
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    }

    // 4. Force an oversized block — must bump engine_overrun_count and NOT be
    //    measured as a wall sample.
    {
        spe::audio_io::AudioBlock big;
        big.output_channels      = ch_ptrs.data();
        big.output_channel_count = n_ch;
        big.num_frames           = spe::MAX_BLOCK + 1;  // > MAX_BLOCK
        big.sample_rate          = kSR;
        engine.audioBlock(big);
    }
    if (engine.engineOverrunCount() == 0) {
        std::fprintf(stderr, "FAIL: oversized block did not bump engineOverrunCount\n");
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    }

    // 5. Handshake so the engine captures the reply peer.
    auto pkt = buildHandshakePacket(1);
    struct sockaddr_in engine_addr{};
    engine_addr.sin_family      = AF_INET;
    engine_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    engine_addr.sin_port        = htons(engine_port);
    ::sendto(client.fd, pkt.data(), pkt.size(), 0,
             reinterpret_cast<struct sockaddr*>(&engine_addr), sizeof(engine_addr));
    {
        auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < dl) {
            if (engine.oscBackend().hasPeerEndpoint()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (!engine.oscBackend().hasPeerEndpoint()) {
        std::fprintf(stderr, "FAIL: engine never captured handshake peer\n");
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    }

    // 6. Emit /sys/metrics — IDENTICAL field formatting to the binary's tick.
    auto& obs = engine.observabilityCounters();
    char kvbuf[64];
    std::snprintf(kvbuf, sizeof(kvbuf), "cpu_pct=%u",
                  obs.cpu_pct_audio_thread.load(std::memory_order_relaxed));
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);
    std::snprintf(kvbuf, sizeof(kvbuf), "cpu_peak_pct=%u", engine.cpuMeter().peakPct());
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);
    std::snprintf(kvbuf, sizeof(kvbuf), "p99_us=%u",
                  obs.per_block_time_p99_us.load(std::memory_order_relaxed));
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);
    std::snprintf(kvbuf, sizeof(kvbuf), "xrun_count=%llu", 0ull);  // null backend: no device
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);
    std::snprintf(kvbuf, sizeof(kvbuf), "engine_overrun_count=%llu",
                  static_cast<unsigned long long>(engine.engineOverrunCount()));
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);
    std::snprintf(kvbuf, sizeof(kvbuf), "binaural_demote_count=%u",
                  engine.binauralIsRuntimeDemoted() ? 1u : 0u);
    engine.oscBackend().sendReply("/sys/metrics", ",s", kvbuf);

    // 7. Drain the 6 messages and validate.
    bool seen_cpu = false, seen_peak = false, seen_p99 = false,
         seen_xrun = false, seen_overrun = false, seen_demote = false;
    long long overrun_val = -1;

    for (int i = 0; i < 6; ++i) {
        uint8_t rx[256] = {0};
        struct sockaddr_in from{};
        socklen_t fromlen = sizeof(from);
        ssize_t n = ::recvfrom(client.fd, rx, sizeof(rx), 0,
                               reinterpret_cast<struct sockaddr*>(&from), &fromlen);
        if (n <= 0) {
            std::fprintf(stderr, "FAIL: only received %d /sys/metrics messages\n", i);
            engine.releaseResources();
            ::close(client.fd);
            return 1;
        }
        const std::string kv = parseMetricsKv(rx, n);
        if (kv.empty()) {
            std::fprintf(stderr, "FAIL: msg %d not a /sys/metrics ,s packet\n", i);
            engine.releaseResources();
            ::close(client.fd);
            return 1;
        }
        std::string key; long long val = 0;
        if (!splitKv(kv, key, val)) {
            std::fprintf(stderr, "FAIL: malformed kv '%s'\n", kv.c_str());
            engine.releaseResources();
            ::close(client.fd);
            return 1;
        }
        if (key == "cpu_pct") {
            seen_cpu = true;
            if (val < 0 || val > 100) {
                std::fprintf(stderr, "FAIL: cpu_pct=%lld out of [0,100]\n", val);
                engine.releaseResources(); ::close(client.fd); return 1;
            }
        } else if (key == "cpu_peak_pct") {
            seen_peak = true;
            if (val < 0 || val > 100) {
                std::fprintf(stderr, "FAIL: cpu_peak_pct=%lld out of [0,100]\n", val);
                engine.releaseResources(); ::close(client.fd); return 1;
            }
        } else if (key == "p99_us") {
            seen_p99 = true;
            if (val < 0) {
                std::fprintf(stderr, "FAIL: p99_us=%lld < 0\n", val);
                engine.releaseResources(); ::close(client.fd); return 1;
            }
        } else if (key == "xrun_count") {
            seen_xrun = true;
        } else if (key == "engine_overrun_count") {
            seen_overrun = true;
            overrun_val = val;
        } else if (key == "binaural_demote_count") {
            seen_demote = true;
        }
    }

    if (!(seen_cpu && seen_peak && seen_p99 && seen_xrun && seen_overrun && seen_demote)) {
        std::fprintf(stderr,
                     "FAIL: missing field(s): cpu=%d peak=%d p99=%d xrun=%d "
                     "overrun=%d demote=%d\n",
                     seen_cpu, seen_peak, seen_p99, seen_xrun, seen_overrun, seen_demote);
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    }
    if (overrun_val <= 0) {
        std::fprintf(stderr, "FAIL: engine_overrun_count=%lld (expected > 0)\n", overrun_val);
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    }

    engine.releaseResources();
    ::close(client.fd);
    std::printf("PASS test_p_sys_metrics_extended (6 /sys/metrics fields, "
                "engine_overrun_count=%lld)\n", overrun_val);
    return 0;
}
