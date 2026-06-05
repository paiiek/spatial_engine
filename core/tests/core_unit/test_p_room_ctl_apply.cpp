// test_p_room_ctl_apply.cpp
// Dreamscape Convergence ⑥e-4 — live-engine coverage for the /room/* control
// path (OSC -> cmd_fifo_ -> audio-thread drain -> applyRoomCtl).
//
// Proves the three reviewer-mandated invariants:
//   (1) EQ LOCKSTEP — /room/eq/early recoeffs the cluster-bus EQ and EVERY
//       per-object early EQ to the SAME corners (direct coefficient assertion).
//   (2) RT SAFETY  — draining the heaviest room op (eq/early = 130 biquad
//       recoeffs) inside audioBlock's SPE_RT_NO_ALLOC_SCOPE raises zero
//       allocation violations (build-test runs with SPE_RT_ASSERTS=ON).
//   (3) Behaviour  — /room/enable, /room/t60 and /room/cluster/send each move
//       the rendered field, so the wire path actually reaches the DSP. (Preset
//       is deferred; not exercised here.)

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "ipc/Command.h"
#include "audio_io/AudioCallback.h"
#include "render/ported/RoomBiquad.h"
#include "util/RtAssertNoAlloc.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

static spe::geometry::SpeakerLayout make_dome() {
    using namespace spe::geometry;
    SpeakerLayout l; l.name = "room_ctl_dome"; l.regularity = Regularity::IRREGULAR;
    int ch = 1;
    for (float el_deg : {-20.f, 30.f}) {
        const float el = el_deg * kPi / 180.f;
        for (int i = 0; i < 8; ++i) {
            const float az = (-kPi) + 2.f * kPi * static_cast<float>(i) / 8.f;
            const float ce = std::cos(el);
            Speaker s; s.channel = ch++;
            s.x = ce * std::sin(az); s.y = std::sin(el); s.z = ce * std::cos(az);
            l.speakers.push_back(s);
        }
    }
    return l;
}

static bool coeffEq(const iae::RoomBiquad& a, const iae::RoomBiquad& b) {
    auto e = [](float x, float y) { return std::fabs(x - y) <= 1e-6f; };
    return e(a.b0(), b.b0()) && e(a.b1(), b.b1()) && e(a.b2(), b.b2())
        && e(a.a1(), b.a1()) && e(a.a2(), b.a2());
}

// Inject a RoomCtl command (control-thread sink → FIFO).
static void send(spe::core::SpatialEngine& e, const spe::ipc::PayloadRoomCtl& p) {
    spe::ipc::Command c; c.tag = spe::ipc::CommandTag::RoomCtl; c.payload = p;
    e.dispatchCommand(c);
}
static void sendCmd(spe::core::SpatialEngine& e, spe::ipc::CommandTag t, auto payload) {
    spe::ipc::Command c; c.tag = t; c.payload = payload; e.dispatchCommand(c);
}

static void block(spe::core::SpatialEngine& e, std::vector<std::vector<float>>& bufs,
                  std::vector<float*>& ptrs, int n_spk) {
    spe::audio_io::AudioBlock blk;
    blk.output_channels = ptrs.data();
    blk.output_channel_count = n_spk;
    blk.num_frames = 256;
    blk.sample_rate = 48000.0;
    e.audioBlock(blk);
    (void) bufs;
}

// Run `nb` blocks with room enabled + object 0 (110 Hz, below the horizon)
// feeding the reverb; return per-speaker bus energy. `enable_room` lets the
// caller pick HOW the room is engaged (/room/enable vs /reverb/select) so the
// alias contract can be tested. t60 < 0 leaves the default.
static std::vector<double> runPerSpk(int n_spk,
        const std::function<void(spe::core::SpatialEngine&)>& enable_room,
        float t60 = -1.f, float cluster_send = 0.4f, int nb = 60) {
    spe::core::SpatialEngine engine(0);
    engine.setLayout(make_dome());
    engine.prepareToPlay(48000.0, 256);

    enable_room(engine);
    if (t60 > 0.f) { spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::T60; p.t60 = t60; send(engine, p); }
    { spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::ClusterSend; p.cluster_send01 = cluster_send; send(engine, p); }
    { spe::ipc::PayloadObjMove p; p.obj_id = 0; p.az_rad = 0.f; p.el_rad = -0.349f; p.dist_m = 2.f;
      sendCmd(engine, spe::ipc::CommandTag::ObjMove, p); }
    { spe::ipc::PayloadObjDsp p; p.obj_id = 0; p.param = spe::ipc::PayloadObjDsp::Param::ReverbSend; p.value = 0.8f;
      sendCmd(engine, spe::ipc::CommandTag::ObjDsp, p); }

    std::vector<std::vector<float>> bufs(static_cast<size_t>(n_spk), std::vector<float>(256, 0.f));
    std::vector<float*> ptrs(static_cast<size_t>(n_spk));
    for (int s = 0; s < n_spk; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

    std::vector<double> e(static_cast<size_t>(n_spk), 0.0);
    for (int b = 0; b < nb; ++b) {
        block(engine, bufs, ptrs, n_spk);
        for (int s = 0; s < n_spk; ++s)
            for (int n = 0; n < 256; ++n) {
                const float v = bufs[static_cast<size_t>(s)][static_cast<size_t>(n)];
                e[static_cast<size_t>(s)] += static_cast<double>(v) * v;
            }
    }
    return e;
}

// Upper-ring (speakers 8..N) sum — isolates the reverb tail, since the object
// sits below the horizon and the dry direct sound never reaches the upper ring.
static double upperSum(const std::vector<double>& e) {
    double t = 0.0; for (size_t s = 8; s < e.size(); ++s) t += e[s]; return t;
}

int main() {
    const int N = 16;

    // ───────────────────────── (1) EQ LOCKSTEP + (2) RT SAFETY ─────────────────
    {
        spe::core::SpatialEngine engine(0);
        engine.setLayout(make_dome());
        engine.prepareToPlay(48000.0, 256);

        std::vector<std::vector<float>> bufs(static_cast<size_t>(N), std::vector<float>(256, 0.f));
        std::vector<float*> ptrs(static_cast<size_t>(N));
        for (int s = 0; s < N; ++s) ptrs[static_cast<size_t>(s)] = bufs[static_cast<size_t>(s)].data();

        // Reference corners after prepare: HP 120 / LP 10000. Capture defaults.
        iae::RoomBiquad defHp = engine.clusterEqHpForTest();

        // Inject /room/eq/early HP=250 LP=7000, then drain it inside one audioBlock.
        { spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::EqEarly;
          p.eq_early_hp = 250.f; p.eq_early_lp = 7000.f; send(engine, p); }

        spe::util::rt_alloc_violations_reset();
        block(engine, bufs, ptrs, N);
        CHECK(spe::util::rt_alloc_violations() == 0,
              "(RT) eq/early drain (130 biquad recoeffs) allocates nothing on the audio thread");

        // The cluster-bus EQ changed from its default corner...
        CHECK(!coeffEq(engine.clusterEqHpForTest(), defHp),
              "(lockstep) cluster HP actually re-coeffed by /room/eq/early");
        // ...and EVERY per-object early EQ is coefficient-identical to the cluster bus.
        const auto& chp = engine.clusterEqHpForTest();
        const auto& clp = engine.clusterEqLpForTest();
        bool allHpLocked = true, allLpLocked = true;
        for (size_t i = 0; i < engine.objCacheSize(); ++i) {
            if (!coeffEq(engine.earlyEqHpForTest(i), chp)) allHpLocked = false;
            if (!coeffEq(engine.earlyEqLpForTest(i), clp)) allLpLocked = false;
        }
        CHECK(allHpLocked, "(lockstep) every per-object early HP == cluster-bus HP");
        CHECK(allLpLocked, "(lockstep) every per-object early LP == cluster-bus LP");

        // /room/set must route its eq fields through the SAME lockstep path.
        { spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::SetAll;
          p.t60 = 2.f; p.sx = 6; p.sy = 5; p.sz = 3; p.early_width_deg = 45; p.early_balance01 = 0.45f;
          p.cluster_send01 = 0.4f; p.cluster_diffusion01 = 0.48f; p.cluster_volume_m3 = 630.f;
          p.eq_early_hp = 300.f; p.eq_early_lp = 6000.f; p.late_hf_corner_hz = 6200.f; p.late_hf_ratio01 = 0.62f;
          send(engine, p); }
        spe::util::rt_alloc_violations_reset();
        block(engine, bufs, ptrs, N);
        CHECK(spe::util::rt_alloc_violations() == 0, "(RT) /room/set drain allocates nothing");
        iae::RoomBiquad want; want.setHighPass(48000.0, 300.f);
        CHECK(coeffEq(engine.clusterEqHpForTest(), want),
              "(set) cluster HP matches /room/set eq_early_hp=300");
        CHECK(coeffEq(engine.earlyEqHpForTest(0), engine.clusterEqHpForTest())
           && coeffEq(engine.earlyEqHpForTest(engine.objCacheSize() - 1), engine.clusterEqHpForTest()),
              "(set) early EQ stays locked to cluster after /room/set");
    }

    // ───────────────────────── (3) BEHAVIOUR ──────────────────────────────────
    auto enRoom = [](spe::core::SpatialEngine& e) {
        spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::Enable; p.enable = true; send(e, p);
    };

    // t60: a longer decay accumulates strictly more UPPER-RING reverb energy from
    // the same continuous 110 Hz source. The upper ring isolates the FDN tail —
    // the object sits below the horizon so the (much larger) dry direct sound
    // never reaches it and cannot swamp the t60 effect.
    const double upShort = upperSum(runPerSpk(N, enRoom, /*t60=*/0.3f));
    const double upLong  = upperSum(runPerSpk(N, enRoom, /*t60=*/5.0f));
    std::printf("[room-ctl] t60 upper: short(0.3)=%.4g  long(5.0)=%.4g  ratio=%.2f\n",
                upShort, upLong, upLong / (upShort + 1e-30));
    CHECK(upShort > 1e-9, "(t60) baseline reverb is audible on the upper ring");
    CHECK(upLong > 1.3 * upShort, "(t60) /room/t60 5.0 accumulates more upper-ring energy than 0.3");

    // cluster send via the NEW OSC path moves the upper-ring field (dry sits below
    // the horizon, so the change is unambiguously the cluster bus).
    {
        const auto eOff = runPerSpk(N, enRoom, -1.f, /*cluster=*/0.0f);
        const auto eOn  = runPerSpk(N, enRoom, -1.f, /*cluster=*/1.0f);
        const double base = upperSum(eOff);
        double absDelta = 0.0;
        for (int s = 8; s < N; ++s) absDelta += std::fabs(eOn[(size_t) s] - eOff[(size_t) s]);
        std::printf("[room-ctl] /room/cluster/send upper Σ|Δ|=%.4g (%.2f%% of off)\n",
                    absDelta, 100.0 * absDelta / (base + 1e-30));
        CHECK(base > 1e-9, "(cluster) upper-ring reverb present with cluster send off");
        CHECK(absDelta > 0.05 * base, "(cluster) /room/cluster/send measurably moves the field");
    }

    // enable ALIAS (the documented contract): /room/enable 1 must be behaviourally
    // identical to /reverb/select "room", and /room/enable 0 to /reverb/select
    // "fdn". Both paths are fully deterministic, so the per-speaker fields match
    // bit-for-bit. This is the precise meaning of the alias, independent of which
    // path happens to put more energy where.
    {
        auto selRoom = [](spe::core::SpatialEngine& e) {
            spe::ipc::PayloadReverbSelect p; p.which = 2; sendCmd(e, spe::ipc::CommandTag::ReverbSelect, p);
        };
        auto enOff = [](spe::core::SpatialEngine& e) {
            spe::ipc::PayloadRoomCtl p; p.op = spe::ipc::PayloadRoomCtl::Op::Enable; p.enable = false; send(e, p);
        };
        auto selFdn = [](spe::core::SpatialEngine& e) {
            spe::ipc::PayloadReverbSelect p; p.which = 0; sendCmd(e, spe::ipc::CommandTag::ReverbSelect, p);
        };
        auto aliasDelta = [&](const std::vector<double>& a, const std::vector<double>& b) {
            double num = 0.0, den = 0.0;
            for (int s = 0; s < N; ++s) { num += std::fabs(a[(size_t) s] - b[(size_t) s]); den += b[(size_t) s]; }
            return std::pair<double, double>{num, den};
        };
        const auto [n1, d1] = aliasDelta(runPerSpk(N, enRoom), runPerSpk(N, selRoom));
        std::printf("[room-ctl] enable1==select room: Σ|Δ|/Σ=%.3g%% (den=%.4g)\n",
                    100.0 * n1 / (d1 + 1e-30), d1);
        CHECK(d1 > 1e-9, "(enable) /reverb/select room renders a non-trivial field");
        CHECK(n1 <= 1e-6 * d1, "(enable) /room/enable 1 == /reverb/select room (alias)");

        const auto [n0, d0] = aliasDelta(runPerSpk(N, enOff), runPerSpk(N, selFdn));
        CHECK(d0 > 1e-9, "(enable) /reverb/select fdn renders a non-trivial field");
        CHECK(n0 <= 1e-6 * d0, "(enable) /room/enable 0 == /reverb/select fdn (alias)");
    }

    if (failures == 0) { std::printf("test_p_room_ctl_apply: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_p_room_ctl_apply: %d FAIL\n", failures);
    return 1;
}
