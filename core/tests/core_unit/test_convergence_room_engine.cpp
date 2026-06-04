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
// send; drive `blocks` blocks and return per-speaker accumulated energy. The
// object sits below the horizon (el=-20°) at azimuth `az_rad`.
static std::vector<double> run(float reverb_send, int n_spk, float az_rad = 0.f,
                               float cluster_send = 0.4f) {
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, 256);
    engine.setRoomClusterSend01(cluster_send);

    auto dsp = [&](spe::ipc::CommandTag tag, auto payload) {
        spe::ipc::Command c; c.tag = tag; c.payload = payload; engine.dispatchCommand(c);
    };
    { spe::ipc::PayloadReverbSelect p; p.which = 2; dsp(spe::ipc::CommandTag::ReverbSelect, p); }
    { spe::ipc::PayloadObjMove p; p.obj_id = 0; p.az_rad = az_rad; p.el_rad = -0.349f; p.dist_m = 2.f;
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

    // ⑥e late opp source-bias — direct test of the per-line steering math
    // (lateFdnLineDirection). In the live audio path the late field's directional
    // bias is mixed with the per-object early reflections (whose ceiling tap sits
    // at the source azimuth), so an isolated left/right energy assertion is
    // confounded; the integration smoke covers RT-safety/sanity. Here we verify
    // the math is faithful to RoomEngine.cpp:567-572 with no confound.
    using SE = spe::core::SpatialEngine;
    // (a) opp = +up (no-energy default) reproduces the static cube corners blended
    //     halfway toward +up: every line keeps a +y component and stays unit-length.
    {
        const iae::Vec3 oppUp{ 0.f, 1.f, 0.f };
        bool allUnit = true, allUp = true;
        for (int k = 0; k < 8; ++k) {
            const iae::Vec3 d = SE::lateFdnLineDirection(k, oppUp);
            const float len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (std::fabs(len - 1.f) > 1.0e-4f) allUnit = false;
            if (d.y <= 0.f) allUp = false;     // corner.y=±0.577, +opp.y=0.5 -> net >0
        }
        CHECK(allUnit, "opp-bias: line directions are unit length");
        CHECK(allUp,   "opp-bias: opp=+up lifts every line above the horizon");
    }
    // (b) opp steering is signed: opp pointing hard RIGHT (+x) steers EVERY line to
    //     the +x hemisphere; opp hard LEFT mirrors it to -x. This is the source-
    //     opposite bias the static fan-out could not produce.
    {
        bool allRight = true, allLeft = true, steered = true;
        for (int k = 0; k < 8; ++k) {
            const iae::Vec3 dR = SE::lateFdnLineDirection(k, iae::Vec3{ 1.f, 0.f, 0.f });
            const iae::Vec3 dL = SE::lateFdnLineDirection(k, iae::Vec3{ -1.f, 0.f, 0.f });
            if (dR.x <= 0.f) allRight = false;
            if (dL.x >= 0.f) allLeft  = false;
            if (dR.x <= dL.x) steered = false; // +x opp lands strictly right of -x opp
        }
        CHECK(allRight, "opp-bias: opp=+x steers every late line to the +x hemisphere");
        CHECK(allLeft,  "opp-bias: opp=-x steers every late line to the -x hemisphere");
        CHECK(steered,  "opp-bias: +x opp lands every line right of -x opp (signed steer)");
    }

    // ⑥e-2 cluster wiring: the cluster bus (every object's xdel*cSend, EQ'd and
    // run through the 6-tap feedforward diffusion line, fanned via the opp-biased
    // clusterU which carries a +up component) adds a mid-field diffuse component.
    // We isolate it on the upper ring (the dry sits below the horizon) and assert
    // turning the cluster send off vs on changes the rendered upper-ring field.
    // Direction-agnostic (summed energy is phase-sensitive); a non-trivial change
    // proves the bus is fed, EQ'd, diffused and distributed. The cluster core math
    // is unit-tested separately in test_convergence_room_cluster.
    auto upperEnergy = [&](const std::vector<double>& e) {
        double t = 0.0; for (int s = half; s < N; ++s) t += e[(size_t) s]; return t;
    };
    // Only cluster_send differs between the two runs (identical FDN + early
    // paths), so any change is unambiguously the cluster. Use sum of per-speaker
    // ABSOLUTE deltas (does not cancel under phase redistribution, unlike the
    // delta of summed energy) and the max cluster send for a robust margin.
    const auto eClusterOff = run(0.8f, N, 0.f, /*cluster_send=*/0.0f);
    const auto eClusterOn  = run(0.8f, N, 0.f, /*cluster_send=*/1.0f);
    const double upOffCl = upperEnergy(eClusterOff);
    double absDelta = 0.0;
    for (int s = half; s < N; ++s)
        absDelta += std::fabs(eClusterOn[(size_t) s] - eClusterOff[(size_t) s]);
    const double relAbsChange = absDelta / (upOffCl + 1e-30);
    std::printf("[room-engine] cluster wiring: upper off=%.6g  Σ|Δspk|=%.6g (%.3g%% of off)\n",
                upOffCl, absDelta, 100.0 * relAbsChange);
    CHECK(upOffCl > 1.0e-9, "⑥e-2 baseline upper-ring reverb present with cluster off");
    CHECK(relAbsChange > 0.05,
          "⑥e-2 cluster send measurably changes the upper-ring room field (bus wired)");

    if (failures == 0) { std::printf("test_convergence_room_engine: ALL PASS\n"); return 0; }
    return 1;
}
