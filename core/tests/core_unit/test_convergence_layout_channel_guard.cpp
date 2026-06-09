// test_convergence_layout_channel_guard.cpp
// Phase 4.3 Inc 5 (Dreamscape convergence) — physical-output channel-count guard.
//
//   (A) Zero-fill: when the layout drives fewer channels than the physical bus
//       ([out_ch, output_channel_count) orphaned), audioBlock must zero those
//       channels every block — stale audio must not linger after a downsize.
//   (B) Overflow warning: when the layout has MORE speakers than the physical
//       bus, the audio thread publishes the unrouted count and the control-tick
//       emitLayoutOverflowWarning() emits a latched /sys/warning
//       "layout_exceeds_output" <n>, re-arming once a later layout fits.

#include "core/SpatialEngine.h"
#include "audio_io/AudioCallback.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)){ std::fprintf(stderr,"FAIL: %s\n", m); ++failures; } } while(0)

static constexpr float kPi = 3.14159265358979323846f;

static spe::geometry::SpeakerLayout make_ring(int n) {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "guard_ring"; l.regularity = Regularity::CIRCULAR;
    l.channel_to_idx_.fill(static_cast<int16_t>(-1));
    for (int i = 0; i < n; ++i) {
        Speaker s; s.channel = i + 1;
        const float a = (-kPi) + 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
        s.x = std::sin(a); s.y = 0.f; s.z = std::cos(a);
        l.channel_to_idx_[static_cast<std::size_t>(i + 1)] = static_cast<int16_t>(l.speakers.size());
        l.speakers.push_back(s);
    }
    return l;
}

static void move(spe::core::SpatialEngine& e, uint32_t id, float az) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjMove;
    spe::ipc::PayloadObjMove p; p.obj_id = id; p.az_rad = az; p.el_rad = 0.f; p.dist_m = 2.f;
    c.payload = p; e.dispatchCommand(c);
}
static void setActive(spe::core::SpatialEngine& e, uint32_t id, bool on) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::ObjActive;
    spe::ipc::PayloadObjActive p; p.obj_id = id; p.active = on;
    c.payload = p; e.dispatchCommand(c);
}

// Render one block with `phys` physical output channels; bufs sized to `phys`.
static void renderBlock(spe::core::SpatialEngine& e,
                        std::vector<std::vector<float>>& bufs, int phys, int frames) {
    std::vector<float*> ptrs(static_cast<size_t>(phys));
    for (int c = 0; c < phys; ++c) ptrs[static_cast<size_t>(c)] = bufs[static_cast<size_t>(c)].data();
    spe::audio_io::AudioBlock blk;
    blk.output_channels = ptrs.data();
    blk.output_channel_count = phys;
    blk.num_frames = frames;
    blk.sample_rate = 48000.0;
    e.audioBlock(blk);
}

// Minimal /sys/warning ,iis parser over the OSC outbound ring. Returns code string.
static std::string lastWarningCode(spe::ipc::OSCBackend& osc) {
    std::string code;
    const std::size_t pending = osc.outboundPending();
    for (std::size_t k = 0; k < pending; ++k) {
        std::size_t n = 0; const uint8_t* b = osc.outboundPeek(k, n);
        if (!b || n < 8) continue;
        std::size_t i = 0, a = 0; while (a < n && b[a] != '\0') ++a;
        std::string addr(reinterpret_cast<const char*>(b), a);
        if (addr != "/sys/warning") continue;
        i = (a + 1 + 3) & ~std::size_t(3);
        if (i >= n || b[i] != ',') continue;
        std::size_t t = i; while (t < n && b[t] != '\0') ++t;
        std::string tags(reinterpret_cast<const char*>(b + i + 1), t - (i + 1));
        i = (t + 1 + 3) & ~std::size_t(3);
        std::string firstStr;
        for (char c : tags) {
            if (c == 'i') { if (i + 4 > n) break; i += 4; }
            else if (c == 's') {
                if (i >= n) break;
                std::size_t s = i; while (s < n && b[s] != '\0') ++s;
                firstStr.assign(reinterpret_cast<const char*>(b + i), s - i);
                i = (s + 1 + 3) & ~std::size_t(3);
                break;
            }
        }
        code = firstStr;  // keep the last /sys/warning's code
    }
    return code;
}

int main() {
    constexpr int N = 8;       // layout speakers
    constexpr int FR = 64;
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_ring(N));
    engine.prepareToPlay(48000.0, FR);
    setActive(engine, 0, true);
    move(engine, 0, 0.4f);     // one active object → driven channels carry signal
    auto& osc = engine.oscBackend();
    auto drainAll = [&]{ osc.outboundDrainForTest(osc.outboundPending()); };

    // sendReply() drops unless a peer endpoint has been captured. Inject one
    // packet WITH a loopback peer so the warning replies below have a target and
    // land in the outbound ring (mirrors the layout_slot OSC test).
    {
        struct sockaddr_in peer{};
        peer.sin_family = AF_INET; peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port = htons(9999);
        std::vector<uint8_t> pkt;            // "/layout/slot/list" + "," (no args)
        for (char ch : std::string("/layout/slot/list")) pkt.push_back(static_cast<uint8_t>(ch));
        pkt.push_back(0); while (pkt.size() % 4) pkt.push_back(0);
        pkt.push_back(','); pkt.push_back(0); while (pkt.size() % 4) pkt.push_back(0);
        osc.injectPacket(std::span<const uint8_t>(pkt),
                         reinterpret_cast<const struct sockaddr*>(&peer), sizeof(peer));
        drainAll();   // discard the /layout/slot/list reply this injection produced
    }

    // (A) physical bus LARGER than layout (10 > 8): channels 8,9 must be zeroed
    // every block, even if they held stale garbage.
    {
        const int PHYS = 10;
        std::vector<std::vector<float>> bufs(PHYS, std::vector<float>(FR, 0.f));
        for (int n = 0; n < FR; ++n) { bufs[8][static_cast<size_t>(n)] = 0.5f;   // stale garbage
                                       bufs[9][static_cast<size_t>(n)] = -0.5f; }
        renderBlock(engine, bufs, PHYS, FR);
        bool orphan_silent = true, driven_has_signal = false;
        for (int n = 0; n < FR; ++n) {
            if (bufs[8][static_cast<size_t>(n)] != 0.f || bufs[9][static_cast<size_t>(n)] != 0.f)
                orphan_silent = false;
        }
        for (int c = 0; c < N; ++c)
            for (int n = 0; n < FR; ++n)
                if (bufs[static_cast<size_t>(c)][static_cast<size_t>(n)] != 0.f) driven_has_signal = true;
        CHECK(orphan_silent, "orphaned physical channels [8,10) zero-filled (no stale audio)");
        CHECK(driven_has_signal, "driven channels [0,8) still carry the rendered signal (non-vacuous)");
    }

    // (B) physical bus SMALLER than layout (6 < 8): 2 speakers unrouted → control
    // tick emits a latched /sys/warning "layout_exceeds_output".
    {
        const int PHYS = 6;
        std::vector<std::vector<float>> bufs(PHYS, std::vector<float>(FR, 0.f));
        renderBlock(engine, bufs, PHYS, FR);            // publishes unrouted = 2
        drainAll();
        engine.emitLayoutOverflowWarning();
        CHECK(lastWarningCode(osc) == "layout_exceeds_output", "overflow emits layout_exceeds_output warning");
        drainAll();
        // Latched: a second emit in the same episode is silent.
        engine.emitLayoutOverflowWarning();
        CHECK(osc.outboundPending() == 0, "overflow warning is latched (no repeat emit)");
    }

    // (C) re-arm: render a FITTING bus (8 == 8) → unrouted=0 clears the latch; a
    // later overflow warns again.
    {
        std::vector<std::vector<float>> fit(N, std::vector<float>(FR, 0.f));
        renderBlock(engine, fit, N, FR);               // unrouted = 0
        engine.emitLayoutOverflowWarning();            // clears latch, no emit
        drainAll();
        std::vector<std::vector<float>> small(6, std::vector<float>(FR, 0.f));
        renderBlock(engine, small, 6, FR);             // unrouted = 2 again
        engine.emitLayoutOverflowWarning();
        CHECK(lastWarningCode(osc) == "layout_exceeds_output", "warning re-arms after a fitting layout");
    }

    if (failures == 0) { std::printf("test_convergence_layout_channel_guard: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_convergence_layout_channel_guard: %d FAILURE(S)\n", failures);
    return 1;
}
