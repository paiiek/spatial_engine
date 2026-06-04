// test_convergence_room_engine.cpp
// Dreamscape Convergence ⑥b — audio-path coverage for the wired spatial room
// reverb (closes the reviewer's MEDIUM: the room_spatial test only checked the
// VBAP gain math, not the live SpatialEngine::audioBlock path).
//
// Drives the real engine on a 3D dome with a LOWER-hemisphere object and the
// room reverb selected. The dry direct sound stays in the lower ring, so any
// UPPER-ring energy must come from the room FDN's +y cube-corner fan-out. The
// test asserts upper-ring energy appears with a reverb send and is ~absent
// without one (send=0), exercising the engine's room_ready_ precompute, the
// rev==2 mode gating, and the per-line fan-out.

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

// 16-speaker dome: lower ring (el=-20°) indices 0..7, upper ring (el=+30°) 8..15.
static spe::geometry::SpeakerLayout make_dome() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "room_engine_dome"; l.regularity = Regularity::IRREGULAR;
    int ch = 1;
    for (float el_deg : {-20.f, 30.f}) {
        const float el = el_deg * kPi / 180.f;
        for (int i = 0; i < 8; ++i) {
            const float az = (-kPi) + 2.f * kPi * (float) i / 8.f;
            const float ce = std::cos(el);
            Speaker s; s.channel = ch++;
            s.x = ce * std::sin(az); s.y = std::sin(el); s.z = ce * std::cos(az);
            l.speakers.push_back(s);
        }
    }
    return l;
}

// Run a fresh engine with room reverb selected and the given per-object reverb
// send; drive `blocks` blocks and return per-speaker accumulated energy.
static std::vector<double> run(float reverb_send, int n_spk) {
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, 256);

    auto dsp = [&](spe::ipc::CommandTag tag, auto payload) {
        spe::ipc::Command c; c.tag = tag; c.payload = payload; engine.dispatchCommand(c);
    };
    { spe::ipc::PayloadReverbSelect p; p.which = 2; dsp(spe::ipc::CommandTag::ReverbSelect, p); }
    { spe::ipc::PayloadObjMove p; p.obj_id = 0; p.az_rad = 0.f; p.el_rad = -0.349f; p.dist_m = 2.f;
      dsp(spe::ipc::CommandTag::ObjMove, p); }
    { spe::ipc::PayloadObjDsp p; p.obj_id = 0;
      p.param = spe::ipc::PayloadObjDsp::Param::ReverbSend; p.value = reverb_send;
      dsp(spe::ipc::CommandTag::ObjDsp, p); }

    constexpr int kFrames = 256;
    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk),
                                         std::vector<float>(kFrames, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    std::vector<double> energy(static_cast<size_t>(n_spk), 0.0);
    for (int b = 0; b < 60; ++b) {            // ~0.32 s — lets the FDN tail build
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data();
        blk.output_channel_count = n_spk;
        blk.num_frames = kFrames;
        blk.sample_rate = 48000.0;
        engine.audioBlock(blk);
        for (int s = 0; s < n_spk; ++s)
            for (int n = 0; n < kFrames; ++n) {
                const float v = bufs[static_cast<size_t>(s)][static_cast<size_t>(n)];
                energy[static_cast<size_t>(s)] += static_cast<double>(v) * v;
            }
    }
    return energy;
}

int main() {
    const int N = 16, half = 8;

    const auto eOff = run(0.0f, N);    // dry object only
    const auto eOn  = run(0.8f, N);    // dry + spatial room reverb

    double dryTotal = 0.0, upOff = 0.0, upOn = 0.0;
    int upActive = 0;
    double upMax = 0.0;
    for (int s = 0; s < N; ++s) dryTotal += eOff[(size_t) s];
    for (int s = half; s < N; ++s) { upOff += eOff[(size_t) s]; upOn += eOn[(size_t) s];
                                     if (eOn[(size_t) s] > upMax) upMax = eOn[(size_t) s]; }
    for (int s = half; s < N; ++s) if (upMax > 0 && eOn[(size_t) s] > 0.05 * upMax) ++upActive;

    std::printf("[room-engine] dryTotal=%.4g  upperOff=%.4g  upperOn=%.4g  upActive=%d/8\n",
                dryTotal, upOff, upOn, upActive);

    CHECK(dryTotal > 1.0e-9, "engine renders the dry object (non-silent)");
    // The lower object cannot light the upper ring; only the room reverb can.
    CHECK(upOn > 10.0 * (upOff + 1.0e-12), "room reverb adds upper-ring energy a lower object cannot");
    CHECK(upActive >= 3, "room reverb fans onto multiple upper-ring speakers (+y cube corners)");

    if (failures == 0) { std::printf("test_convergence_room_engine: ALL PASS\n"); return 0; }
    return 1;
}
