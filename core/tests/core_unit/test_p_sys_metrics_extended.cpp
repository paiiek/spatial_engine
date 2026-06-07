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
//   6. Emit /sys/metrics via spe::bin::emitSysMetrics() — the SAME shared
//      builder the binary's 1 Hz tick calls (review CONCERN-2). One ,s
//      key=value message per field; the production formatting code path is
//      genuinely under test, not a reimplementation.
//   7. recvfrom() each message, parse "key=value", assert field presence +
//      value ranges (cpu_pct∈[0,100], p99_us≥0) and that engine_overrun_count
//      > 0 and that xrun_count reflects the injected backend xrun value.
//   8. Emit a second time with a LARGER injected xrun value and assert
//      xrun_count is monotonic non-decreasing (AC1 xrun_count monotonic /
//      = driver->xrunCount()).
//
// We construct the engine + emit directly (not via the binary) so the test is
// hermetic, but the emit goes through the shared helper so the wire shape and
// field formatting are the actual production code.

#include "core/SpatialEngine.h"
#include "audio_io/AudioCallback.h"
#include "bin/MetricsEmit.h"
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
#include <map>
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

    // 6/7. Emit /sys/metrics via the SHARED production builder and validate.
    //
    // We drive the same spe::bin::emitSysMetrics() the binary's 1 Hz tick calls
    // (review CONCERN-2). The backend xrun_count is INJECTED here (the null
    // backend has no device counter) so we can assert AC1: xrun_count reflects
    // the passed-in driver xrun value, and is monotonic non-decreasing across
    // emits.
    auto& obs = engine.observabilityCounters();

    // emitAndDrain: emit the 6 fields with the given injected backend xrun,
    // drain + parse the 6 messages into out_fields (key -> parsed value).
    // Returns false (and prints a FAIL line) on any wire/parse error.
    // v1.0 Phase 1.4b — known per-stage values injected to assert the 4 new
    // /sys/metrics fields round-trip on the wire (same approach as xrun_count).
    constexpr std::uint32_t kStageRender = 11, kStageRoom = 22,
                            kStageDecorr = 33, kStageBinaural = 44;
    constexpr int kMetricsFieldCount = 10;  // 6 original + 4 stage timings

    auto emitAndDrain = [&](std::uint64_t backend_xrun,
                            std::map<std::string, long long>& out_fields) -> bool {
        spe::bin::emitSysMetrics(
            engine.oscBackend(),
            obs.cpu_pct_audio_thread.load(std::memory_order_relaxed),
            engine.cpuMeter().peakPct(),
            obs.per_block_time_p99_us.load(std::memory_order_relaxed),
            backend_xrun,
            static_cast<std::uint64_t>(engine.engineOverrunCount()),
            engine.binauralIsRuntimeDemoted() ? 1u : 0u,
            kStageRender, kStageRoom, kStageDecorr, kStageBinaural);

        out_fields.clear();
        for (int i = 0; i < kMetricsFieldCount; ++i) {
            uint8_t rx[256] = {0};
            struct sockaddr_in from{};
            socklen_t fromlen = sizeof(from);
            ssize_t n = ::recvfrom(client.fd, rx, sizeof(rx), 0,
                                   reinterpret_cast<struct sockaddr*>(&from), &fromlen);
            if (n <= 0) {
                std::fprintf(stderr, "FAIL: only received %d /sys/metrics messages\n", i);
                return false;
            }
            const std::string kv = parseMetricsKv(rx, n);
            if (kv.empty()) {
                std::fprintf(stderr, "FAIL: msg %d not a /sys/metrics ,s packet\n", i);
                return false;
            }
            std::string key; long long val = 0;
            if (!splitKv(kv, key, val)) {
                std::fprintf(stderr, "FAIL: malformed kv '%s'\n", kv.c_str());
                return false;
            }
            out_fields[key] = val;
        }
        return true;
    };

    auto fail = [&]() -> int {
        engine.releaseResources();
        ::close(client.fd);
        return 1;
    };

    // First emit: inject a NONZERO backend xrun so xrun_count is exercised.
    const std::uint64_t kXrun1 = 7;
    std::map<std::string, long long> f1;
    if (!emitAndDrain(kXrun1, f1)) return fail();

    static const char* kRequired[] = {
        "cpu_pct", "cpu_peak_pct", "p99_us",
        "xrun_count", "engine_overrun_count", "binaural_demote_count",
        "stage_render_us", "stage_room_us", "stage_decorr_us", "stage_binaural_us"};
    for (const char* k : kRequired) {
        if (f1.find(k) == f1.end()) {
            std::fprintf(stderr, "FAIL: missing field '%s'\n", k);
            return fail();
        }
    }
    // v1.0 Phase 1.4b — the 4 per-stage fields must round-trip the injected values.
    if (f1["stage_render_us"]   != static_cast<long long>(kStageRender) ||
        f1["stage_room_us"]     != static_cast<long long>(kStageRoom) ||
        f1["stage_decorr_us"]   != static_cast<long long>(kStageDecorr) ||
        f1["stage_binaural_us"] != static_cast<long long>(kStageBinaural)) {
        std::fprintf(stderr, "FAIL: stage_* fields did not round-trip "
                     "(render=%lld room=%lld decorr=%lld binaural=%lld)\n",
                     f1["stage_render_us"], f1["stage_room_us"],
                     f1["stage_decorr_us"], f1["stage_binaural_us"]);
        return fail();
    }
    if (f1["cpu_pct"] < 0 || f1["cpu_pct"] > 100) {
        std::fprintf(stderr, "FAIL: cpu_pct=%lld out of [0,100]\n", f1["cpu_pct"]);
        return fail();
    }
    if (f1["cpu_peak_pct"] < 0 || f1["cpu_peak_pct"] > 100) {
        std::fprintf(stderr, "FAIL: cpu_peak_pct=%lld out of [0,100]\n", f1["cpu_peak_pct"]);
        return fail();
    }
    if (f1["p99_us"] < 0) {
        std::fprintf(stderr, "FAIL: p99_us=%lld < 0\n", f1["p99_us"]);
        return fail();
    }
    if (f1["engine_overrun_count"] <= 0) {
        std::fprintf(stderr, "FAIL: engine_overrun_count=%lld (expected > 0)\n",
                     f1["engine_overrun_count"]);
        return fail();
    }
    // AC1: xrun_count == injected backend xrun value.
    if (f1["xrun_count"] != static_cast<long long>(kXrun1)) {
        std::fprintf(stderr, "FAIL: xrun_count=%lld != injected %llu\n",
                     f1["xrun_count"], static_cast<unsigned long long>(kXrun1));
        return fail();
    }

    // Second emit: inject a LARGER backend xrun and assert monotonic
    // non-decrease + exact match (AC1 xrun_count monotonic / = driver value).
    const std::uint64_t kXrun2 = 12;
    std::map<std::string, long long> f2;
    if (!emitAndDrain(kXrun2, f2)) return fail();
    if (f2["xrun_count"] != static_cast<long long>(kXrun2)) {
        std::fprintf(stderr, "FAIL: 2nd xrun_count=%lld != injected %llu\n",
                     f2["xrun_count"], static_cast<unsigned long long>(kXrun2));
        return fail();
    }
    if (f2["xrun_count"] < f1["xrun_count"]) {
        std::fprintf(stderr, "FAIL: xrun_count not monotonic (%lld -> %lld)\n",
                     f1["xrun_count"], f2["xrun_count"]);
        return fail();
    }

    engine.releaseResources();
    ::close(client.fd);
    std::printf("PASS test_p_sys_metrics_extended (10 /sys/metrics fields via "
                "shared emitSysMetrics incl. 4 stage timings; "
                "engine_overrun_count=%lld; xrun_count %lld->%lld monotonic)\n",
                f1["engine_overrun_count"], f1["xrun_count"], f2["xrun_count"]);
    return 0;
}
