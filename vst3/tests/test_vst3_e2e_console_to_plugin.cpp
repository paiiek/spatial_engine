// vst3/tests/test_vst3_e2e_console_to_plugin.cpp
// S6 A.9: Reverse-path E2E soak — console UDP → PluginUdp → controller ring
//         → drainParamEdits → FakeComponentHandler::performEdit.
//
// Load (SPATIAL_ENGINE_SOAK=ON):  64 obj × round-robin 1kHz × 60s
// Load (default / mini-soak):     64 obj × round-robin 1kHz × 5s
//
// Acceptance (A.9):
//   SPATIAL_ENGINE_SOAK=ON  → p99 ≤ 50 ms  (DAW automation tick granularity)
//   mini-soak (default)     → p99 ≤ 30 ms  (same as S4 A.7 short-duration)
//
// The "console" role is simulated by a raw sendto() loop in this process —
// the standalone forwarder binary is not required for this test because the
// plan §S6 confirms we can test the chain by sending directly to the plugin's
// per-instance port (the forwarding hop adds <1 ms and is covered by A.10).
//
// Guard: SPATIAL_ENGINE_SOAK=ON enables 60s soak; without it a 5s mini-soak
// runs that completes well inside ctest's default 30s timeout.

#include "SpatialEngineController.hpp"
#include "SpatialEnginePluginUdp.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// ---------------------------------------------------------------------------
// FakeComponentHandler — records every performEdit arrival time.
// ---------------------------------------------------------------------------
struct EditRecord {
    Steinberg::Vst::ParamID    param_id;
    Steinberg::Vst::ParamValue value;
    TimePoint                  arrival;
};

class FakeComponentHandler : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override
    { *obj = nullptr; return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef()  override { return ++ref_; }
    Steinberg::uint32 PLUGIN_API release() override { return --ref_; }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override
    { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                               Steinberg::Vst::ParamValue v) override
    {
        std::lock_guard<std::mutex> lk(mu_);
        records_.push_back({id, v, Clock::now()});
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override
    { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override
    { return Steinberg::kResultOk; }

    std::vector<EditRecord> copyRecords() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return records_;
    }

private:
    std::atomic<Steinberg::uint32> ref_{1};
    mutable std::mutex             mu_;
    std::vector<EditRecord>        records_;
};

// ---------------------------------------------------------------------------
// OSC /adm/obj/N/azim ,f builder (stack-only).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> buildOscAzim(int obj_id, float az_deg)
{
    char addr[64];
    std::snprintf(addr, sizeof(addr), "/adm/obj/%d/azim", obj_id);
    const char type_tag[] = ",f";

    std::vector<uint8_t> pkt;
    auto appendPadded = [&](const char* s) {
        size_t len = std::strlen(s) + 1;
        for (size_t i = 0; i < len; ++i)
            pkt.push_back(static_cast<uint8_t>(s[i]));
        while (pkt.size() % 4 != 0)
            pkt.push_back(0);
    };
    appendPadded(addr);
    appendPadded(type_tag);

    uint32_t fv;
    std::memcpy(&fv, &az_deg, 4);
    pkt.push_back((fv >> 24) & 0xFF);
    pkt.push_back((fv >> 16) & 0xFF);
    pkt.push_back((fv >>  8) & 0xFF);
    pkt.push_back((fv >>  0) & 0xFF);
    return pkt;
}

// ---------------------------------------------------------------------------
// Percentile helper.
// ---------------------------------------------------------------------------
static double percentile(std::vector<double>& v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p / 100.0 * (v.size() - 1) + 0.5);
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    std::printf("[test_vst3_e2e_console_to_plugin] start\n");

    // SPATIAL_ENGINE_SOAK=ON → 60s full soak; otherwise 5s mini-soak.
    const char* soak_env = std::getenv("SPATIAL_ENGINE_SOAK");
    bool full_soak = (soak_env != nullptr && std::string(soak_env) == "ON");
    double duration_s = full_soak ? 60.0 : 5.0;
    double p99_limit_ms = full_soak ? 50.0 : 30.0;

    std::printf("[test_vst3_e2e_console_to_plugin] mode=%s duration=%.0fs p99_limit=%.0fms\n",
                full_soak ? "full-soak(60s)" : "mini-soak(5s)", duration_s, p99_limit_ms);

    // --- Controller + fake handler ---
    spe::vst3::SpatialEngineController ctrl;
    ctrl.initialize(nullptr);

    FakeComponentHandler fake_handler;
    ctrl.setComponentHandler(&fake_handler);

    // Wire push_param_edit from controller.
    spe::vst3::PushParamEditFn push_fn =
        [&ctrl](uint32_t id, double norm) -> bool {
            return ctrl.pushParamEdit(
                static_cast<Steinberg::Vst::ParamID>(id),
                static_cast<Steinberg::Vst::ParamValue>(norm));
        };

    // --- Plugin UDP (simulates one plugin instance) ---
    spe::vst3::SpatialEnginePluginUdp udp("test_e2e_console_to_plugin", nullptr, push_fn);
    bool started = udp.start();
    assert(started && "PluginUdp::start() failed");
    uint16_t port = udp.boundPort();
    assert(port > 0);
    std::printf("[test_vst3_e2e_console_to_plugin] plugin UDP bound port=%d\n", port);

    // --- Send socket (simulates the console / standalone forwarder) ---
    int send_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(send_fd >= 0);
    struct sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // --- Drain thread (simulates DAW message thread at ~1ms polling) ---
    std::atomic<bool> drain_running{true};
    std::thread drain_thread([&]() {
        while (drain_running.load(std::memory_order_relaxed)) {
            ctrl.drainParamEdits();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Final flush.
        ctrl.drainParamEdits();
    });

    // --- Send loop: 64-obj round-robin at ~1ms intervals ---
    constexpr int kObjects = 64;
    int total_iterations   = static_cast<int>(duration_s * 1000.0);

    std::vector<TimePoint> send_times(total_iterations);

    std::printf("[test_vst3_e2e_console_to_plugin] sending %d packets "
                "(64-obj round-robin, ~1ms intervals)...\n", total_iterations);

    for (int i = 0; i < total_iterations; ++i) {
        int   obj_id = i % kObjects;
        float az_deg = static_cast<float>(i % 360);
        auto  pkt    = buildOscAzim(obj_id, az_deg);

        send_times[i] = Clock::now();
        ::sendto(send_fd, pkt.data(), pkt.size(), 0,
                 reinterpret_cast<const struct sockaddr*>(&dest), sizeof(dest));

        std::this_thread::sleep_for(std::chrono::microseconds(900));
    }
    ::close(send_fd);

    std::printf("[test_vst3_e2e_console_to_plugin] send complete. Flushing (2s)...\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    drain_running.store(false, std::memory_order_relaxed);
    drain_thread.join();

    // --- Compute latencies ---
    auto records  = fake_handler.copyRecords();
    size_t n_recv = records.size();
    uint64_t rx   = udp.packetCount();
    std::printf("[test_vst3_e2e_console_to_plugin] UDP packets received: %llu\n",
                (unsigned long long)rx);
    std::printf("[test_vst3_e2e_console_to_plugin] performEdit records: %zu\n", n_recv);

    // Each /adm/obj/N/azim produces 2 performEdit calls (az + el from ObjMove).
    // Pair records[2*i] → send_times[i].
    std::vector<double> latency_ms;
    latency_ms.reserve(total_iterations);
    for (int i = 0; i < total_iterations; ++i) {
        size_t rec_idx = static_cast<size_t>(i) * 2;
        if (rec_idx >= n_recv) break;
        double lat = std::chrono::duration<double, std::milli>(
            records[rec_idx].arrival - send_times[i]).count();
        if (lat >= 0.0) latency_ms.push_back(lat);
    }

    assert(!latency_ms.empty() && "No latency samples — check UDP bind or drain thread");

    double p50 = percentile(latency_ms, 50.0);
    double p99 = percentile(latency_ms, 99.0);

    std::printf("[test_vst3_e2e_console_to_plugin] latency p50=%.2f ms  p99=%.2f ms  "
                "(samples=%zu, limit=%.0f ms)\n",
                p50, p99, latency_ms.size(), p99_limit_ms);

    double loss_pct = 100.0 * (1.0 - static_cast<double>(rx) / total_iterations);
    std::printf("[test_vst3_e2e_console_to_plugin] packet loss: %.1f%%\n", loss_pct);

    // Observability metrics (emitted to stdout for log inspection).
    std::printf("[test_vst3_e2e_console_to_plugin] "
                "tag=reverse_path_p50_ms value=%.2f\n", p50);
    std::printf("[test_vst3_e2e_console_to_plugin] "
                "tag=reverse_path_p99_ms value=%.2f\n", p99);
    std::printf("[test_vst3_e2e_console_to_plugin] "
                "tag=osc_drop_count value=%llu\n",
                (unsigned long long)udp.dropCount());

    udp.stop();
    ctrl.terminate();

    // --- A.9 assertion ---
    bool p50_ok = p50 <= (p99_limit_ms * 0.5);  // p50 ≤ half of p99 limit
    bool p99_ok = p99 <= p99_limit_ms;

    if (!p99_ok) {
        std::fprintf(stderr,
            "[test_vst3_e2e_console_to_plugin] FAIL A.9: p99=%.2f ms > %.0f ms limit\n",
            p99, p99_limit_ms);
    }
    (void)p50_ok; // p50 is informational only; not a hard gate

    assert(p99_ok && "A.9: p99 reverse-path latency exceeds limit");

    std::printf("[test_vst3_e2e_console_to_plugin] PASS — A.9 p50=%.2f ms, p99=%.2f ms\n",
                p50, p99);
    return 0;
}
