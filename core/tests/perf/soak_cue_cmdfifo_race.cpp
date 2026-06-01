// core/tests/perf/soak_cue_cmdfifo_race.cpp
// v0.9 Lane E (E-M3) D1(b) — concurrent producer-race stress for the fix-1a
// single-producer funnel. Designed to run under ThreadSanitizer.
//
// Setup (mirrors soak_adm_osc_flood real-UDP path):
//   * A real SpatialEngine on a UDP port → its UDP listener thread is the sole
//     cmd_fifo_ producer AND the consumer/forwarder of the control→UDP mailbox.
//   * Thread A floods /adm/obj/N/aed datagrams (inline-sink path → cmd_fifo_).
//   * Thread B drives a CueEngine on a simulated control loop: go()+tick() emit
//     interpolated object updates into the OSCBackend outbound mailbox; the UDP
//     listener drains+forwards them via the SAME sink → cmd_fifo_.
//   * The audio callback runs concurrently (NullBackend) draining cmd_fifo_.
//
// Both producers of cue/scene + obj updates ultimately funnel through the ONE
// UDP thread → cmd_fifo_ has a single physical producer. TSan must report ZERO
// data races on head_/tail_/slots_ and no ring corruption.
//
// Acceptance: no crash, no TSan report, xruns==0.

#include "core/SpatialEngine.h"
#include "ipc/SceneController.h"
#include "ipc/SceneSnapshot.h"
#include "scene/CueEngine.h"
#include "scene/CueList.h"
#include "audio_io/NullBackend.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static void writeU32be(uint8_t* p, uint32_t v) noexcept {
    p[0]=uint8_t(v>>24); p[1]=uint8_t(v>>16); p[2]=uint8_t(v>>8); p[3]=uint8_t(v);
}
static void writeF32be(uint8_t* p, float f) noexcept {
    uint32_t u; std::memcpy(&u,&f,4); writeU32be(p,u);
}
static int buildAed(uint8_t* buf, int id, float az, float el, float d) noexcept {
    char addr[32];
    int alen = std::snprintf(addr, sizeof(addr), "/adm/obj/%d/aed", id);
    int ap = (alen + 1 + 3) & ~3;
    static const char tags[] = ",fff";
    int tp = (int(sizeof(tags)) + 3) & ~3;
    int total = ap + tp + 12;
    std::memset(buf, 0, size_t(total));
    std::memcpy(buf, addr, size_t(alen));
    std::memcpy(buf + ap, tags, sizeof(tags));
    uint8_t* a = buf + ap + tp;
    writeF32be(a+0, az); writeF32be(a+4, el); writeF32be(a+8, d);
    return total;
}

int main() {
    std::puts("soak_cue_cmdfifo_race: UDP /adm flood + concurrent cue crossfade (TSan gate)");

    constexpr int PORT = 9117;
    const int SECS = 5; // short under TSan; enough to interleave producers

    // Scene library for the cue engine.
    auto tmp = fs::temp_directory_path() / "spe_cue_race_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const std::string dir = tmp.string();
    for (int s = 0; s < 4; ++s) {
        spe::ipc::SceneSnapshot snap;
        snap.name = "s" + std::to_string(s);
        for (int o = 0; o < 8; ++o) {
            spe::ipc::ObjectSnapshot os;
            os.id = o; os.az_rad = 0.1f*float(s+o); os.dist_m = 1.f + 0.1f*float(o);
            os.gain_linear = 0.5f; os.muted = (o % 3 == 0);
            snap.objects.push_back(os);
        }
        snap.saveToDisk(dir);
    }

    spe::core::SpatialEngine engine(PORT);
    auto backend = spe::audio_io::make_null_backend(48000.0, 8, 64);
    if (backend->start(&engine) != spe::audio_io::BackendError::Ok) {
        std::fprintf(stderr, "  ERROR: backend start failed\n");
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // UDP bind

    spe::ipc::SceneController ctrl(dir);
    spe::scene::CueEngine cue(
        &ctrl, 48000.f,
        [&engine](const spe::ipc::Command& c) {
            return engine.oscBackend().postOutbound(c);
        });
    spe::scene::CueList cl;
    for (int s = 0; s < 4; ++s) {
        spe::scene::Cue c; c.scene = "s"+std::to_string(s); c.crossfade_ms = 150.f;
        cl.cues.push_back(c);
    }
    cue.setCueList(cl);

    std::atomic<bool> stop{false};
    using clock = std::chrono::steady_clock;
    const auto end = clock::now() + std::chrono::seconds(SECS);

    // Thread A: real-UDP /adm flood (inline sink → cmd_fifo_ on the UDP thread).
    std::thread flood([&]() {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;
        struct sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(uint16_t(PORT));
        inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
        uint8_t buf[64];
        while (!stop.load(std::memory_order_relaxed)) {
            for (int o = 0; o < 8; ++o) {
                int n = buildAed(buf, o, float(o*10), 0.f, 0.5f);
                sendto(fd, buf, size_t(n), 0,
                       reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        close(fd);
    });

    // Thread B: simulated control loop — fire cues + tick crossfades (control
    // producer → outbound mailbox → UDP thread forwards → cmd_fifo_).
    std::thread control([&]() {
        int64_t now = 0;
        int idx = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            cue.go(idx % 4, now);
            ++idx;
            for (int k = 0; k < 8; ++k) {     // tick across the crossfade
                now += 20;                     // 20ms steps
                cue.tick(now);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    });

    while (clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    stop.store(true, std::memory_order_relaxed);
    flood.join();
    control.join();

    backend->stop();
    fs::remove_all(tmp);

    const unsigned long long xruns =
        static_cast<unsigned long long>(backend->xrunCount());
    std::printf("  xruns: %llu\n", xruns);
    std::printf("  inbound_drops: %llu  outbound_mailbox_drops: %llu\n",
                (unsigned long long)engine.oscBackend().inboundDrops(),
                (unsigned long long)engine.oscBackend().outboundMailboxDrops());
    if (xruns != 0) {
        std::fprintf(stderr, "  FAIL: xruns detected — RT safety violation\n");
        return 1;
    }
    std::puts("  PASS soak_cue_cmdfifo_race (no crash; see TSan output above for races)");
    return 0;
}
