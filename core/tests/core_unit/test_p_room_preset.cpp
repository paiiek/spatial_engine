// test_p_room_preset.cpp
// Dreamscape Convergence ⑥h — room state in scenes + /room/preset recall.
//
// Covers:
//   (1) SceneSnapshot room block JSON round-trip (toJson → fromJson).
//   (2) Backward compat: a pre-room scene (no "room" block) parses with
//       room.present=false and objects intact (the objects-array bound fix).
//   (3) /room/preset ,s decode → RoomPreset + name; empty rejected; encode rt.
//   (4) Live engine: snapshotRoom captures what /room/set applied.

#include "ipc/SceneSnapshot.h"
#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"
#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/SpeakerLayout.h"
#include "audio_io/AudioCallback.h"

#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

using namespace spe::ipc;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static bool feq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main() {
    // ---------- (1) room block JSON round-trip ----------
    {
        SceneSnapshot ss;
        ss.name = "studio";
        ObjectSnapshot o; o.id = 3; o.az_rad = 0.5f; o.reverb_send = 0.7f;
        ss.objects.push_back(o);
        RoomSnapshot& r = ss.room;
        r.present = true; r.enabled = true;
        r.t60 = 2.4f; r.sx = 7.f; r.sy = 6.f; r.sz = 4.f;
        r.early_width_deg = 33.f; r.early_balance01 = 0.6f;
        r.cluster_send01 = 0.7f; r.cluster_diffusion01 = 0.3f; r.cluster_volume_m3 = 900.f;
        r.eq_early_hp = 150.f; r.eq_early_lp = 8000.f;
        r.late_hf_corner_hz = 7000.f; r.late_hf_ratio01 = 0.5f;
        r.eq_late_hp = 60.f; r.eq_late_lp = 14000.f;
        r.dist_near_m = 0.8f; r.dist_far_m = 30.f; r.dist_linearity01 = 0.5f;
        r.early_gain_close_db = -8.f; r.early_gain_far_db = -16.f;
        r.late_gain_close_db = -11.f; r.late_gain_far_db = -1.f;
        r.early_predelay_ms = 28.f;

        const std::string json = ss.toJson();
        SceneSnapshot rt = SceneSnapshot::fromJson(json);
        CHECK(rt.name == "studio", "rt name");
        CHECK(rt.objects.size() == 1 && rt.objects[0].id == 3, "rt object survives room block");
        CHECK(feq(rt.objects[0].reverb_send, 0.7f), "rt object field");
        const RoomSnapshot& q = rt.room;
        CHECK(q.present && q.enabled, "rt room present+enabled");
        CHECK(feq(q.t60, 2.4f) && feq(q.sx, 7.f) && feq(q.sz, 4.f), "rt room t60/size");
        CHECK(feq(q.early_width_deg, 33.f) && feq(q.early_balance01, 0.6f), "rt room early");
        CHECK(feq(q.cluster_send01, 0.7f) && feq(q.cluster_volume_m3, 900.f), "rt room cluster");
        CHECK(feq(q.eq_early_hp, 150.f) && feq(q.eq_late_lp, 14000.f), "rt room eq");
        CHECK(feq(q.late_hf_corner_hz, 7000.f) && feq(q.late_hf_ratio01, 0.5f), "rt room late/hf");
        CHECK(feq(q.dist_near_m, 0.8f) && feq(q.dist_far_m, 30.f), "rt room distance");
        CHECK(feq(q.early_gain_close_db, -8.f) && feq(q.late_gain_far_db, -1.f), "rt room gains");
        CHECK(feq(q.early_predelay_ms, 28.f), "rt room predelay");
    }

    // ---------- (2) backward compat: pre-room scene ----------
    {
        // A scene with objects but NO room block (the pre-⑥h on-disk format).
        const std::string legacy =
            "{\"name\":\"old\",\"objects\":["
            "{\"id\":1,\"az_rad\":0.1,\"el_rad\":0,\"dist_m\":2,\"algorithm\":0,"
            "\"gain_linear\":1,\"muted\":false,\"width_rad\":0,\"reverb_send\":0.5}]}";
        SceneSnapshot ss = SceneSnapshot::fromJson(legacy);
        CHECK(ss.objects.size() == 1 && ss.objects[0].id == 1, "legacy: object parses");
        CHECK(feq(ss.objects[0].reverb_send, 0.5f), "legacy: object field");
        CHECK(!ss.room.present, "legacy: room.present=false (no room block)");
        // Default room values intact when absent.
        CHECK(feq(ss.room.t60, 1.2f), "legacy: room defaults preserved");
    }

    // A toJson of a snapshot with room.present=false must NOT emit a room block.
    {
        SceneSnapshot ss; ss.name = "noroom";
        CHECK(ss.toJson().find("\"room\"") == std::string::npos,
              "no-room snapshot omits room block (byte-compat)");
    }

    // ---------- (3) /room/preset ,s decode + encode rt ----------
    {
        CommandDecoder dec;
        auto pad = [](std::vector<uint8_t>& b){ while (b.size()%4) b.push_back(0); };
        auto mk = [&](const std::string& name) {
            std::vector<uint8_t> p;
            const std::string addr = "/room/preset";
            for (char c : addr) { p.push_back((uint8_t)c); }
            p.push_back(0); pad(p);
            const std::string tt = ",s";
            for (char c : tt) { p.push_back((uint8_t)c); }
            p.push_back(0); pad(p);
            for (char c : name) { p.push_back((uint8_t)c); }
            p.push_back(0); pad(p);
            return p;
        };
        auto pkt = mk("studio");
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::RoomPreset, "preset: tag RoomPreset");
        CHECK(std::get<PayloadRoomPreset>(c.payload).name == "studio", "preset: name");

        // empty name → rejected
        const uint32_t before = dec.rejectCount();
        auto empty = mk("");
        Command e = dec.decode(std::span<const uint8_t>(empty));
        CHECK(e.tag == CommandTag::Unknown, "preset: empty name rejected");
        CHECK(dec.rejectCount() == before + 1, "preset: empty reject counted");

        // encode → decode round-trip
        Command in; in.tag = CommandTag::RoomPreset;
        PayloadRoomPreset pp; pp.name = "live_room"; in.payload = pp;
        std::vector<uint8_t> buf;
        CHECK(dec.encode(in, buf), "preset: encode ok");
        Command back = dec.decode(std::span<const uint8_t>(buf));
        CHECK(back.tag == CommandTag::RoomPreset, "preset: rt tag");
        CHECK(std::get<PayloadRoomPreset>(back.payload).name == "live_room", "preset: rt name");
    }

    // ---------- (4) live snapshotRoom captures applied /room/set ----------
    {
        spe::core::SpatialEngine engine(0);
        spe::geometry::SpeakerLayout l; l.name = "stereo";
        spe::geometry::Speaker s0; s0.channel = 1; s0.x = -1; s0.z = 0;
        spe::geometry::Speaker s1; s1.channel = 2; s1.x = 1;  s1.z = 0;
        l.speakers = {s0, s1};
        engine.setLayout(l);
        engine.prepareToPlay(48000.0, 256);

        auto send = [&](const PayloadRoomCtl& p) {
            Command c; c.tag = CommandTag::RoomCtl; c.payload = p; engine.dispatchCommand(c);
        };
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::Enable; p.enable = true; send(p); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::SetAll;
          p.t60 = 3.3f; p.sx = 8; p.sy = 7; p.sz = 5; p.early_width_deg = 55; p.early_balance01 = 0.6f;
          p.cluster_send01 = 0.7f; p.cluster_diffusion01 = 0.3f; p.cluster_volume_m3 = 800;
          p.eq_early_hp = 200; p.eq_early_lp = 7000; p.late_hf_corner_hz = 5000; p.late_hf_ratio01 = 0.4f;
          p.eq_late_hp = 70; p.eq_late_lp = 13000;
          p.dist_near_m = 0.9f; p.dist_far_m = 28; p.dist_linearity01 = 0.5f;
          p.early_gain_close_db = -7; p.early_gain_far_db = -15; p.late_gain_close_db = -10; p.late_gain_far_db = -2;
          p.early_predelay_ms = 30;
          send(p); }

        // Drain the FIFO via one audioBlock.
        std::vector<std::vector<float>> bufs(2, std::vector<float>(256, 0.f));
        std::vector<float*> ptrs = {bufs[0].data(), bufs[1].data()};
        spe::audio_io::AudioBlock blk;
        blk.output_channels = ptrs.data(); blk.output_channel_count = 2;
        blk.num_frames = 256; blk.sample_rate = 48000.0;
        engine.audioBlock(blk);

        RoomSnapshot snap;
        engine.snapshotRoom(snap);
        CHECK(snap.present && snap.enabled, "snapshot: present + enabled");
        CHECK(feq(snap.t60, 3.3f), "snapshot: t60");
        CHECK(feq(snap.sx, 8.f) && feq(snap.sz, 5.f), "snapshot: size");
        CHECK(feq(snap.early_width_deg, 55.f) && feq(snap.early_balance01, 0.6f), "snapshot: early");
        CHECK(feq(snap.cluster_send01, 0.7f) && feq(snap.cluster_volume_m3, 800.f), "snapshot: cluster");
        CHECK(feq(snap.eq_early_hp, 200.f) && feq(snap.eq_early_lp, 7000.f), "snapshot: eq early");
        CHECK(feq(snap.eq_late_hp, 70.f) && feq(snap.eq_late_lp, 13000.f), "snapshot: eq late");
        CHECK(feq(snap.late_hf_corner_hz, 5000.f) && feq(snap.late_hf_ratio01, 0.4f), "snapshot: late/hf");
        CHECK(feq(snap.dist_near_m, 0.9f) && feq(snap.dist_far_m, 28.f), "snapshot: distance");
        CHECK(feq(snap.early_gain_close_db, -7.f) && feq(snap.late_gain_far_db, -2.f), "snapshot: gains");
        CHECK(feq(snap.early_predelay_ms, 30.f), "snapshot: predelay");

        // Round the full loop: snapshot → JSON → parse → matches.
        SceneSnapshot sc; sc.name = "captured"; sc.room = snap;
        SceneSnapshot rt = SceneSnapshot::fromJson(sc.toJson());
        CHECK(rt.room.present && feq(rt.room.t60, 3.3f) && feq(rt.room.eq_late_hp, 70.f),
              "snapshot: full save/load loop");
    }

    if (failures == 0) { std::printf("test_p_room_preset: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_p_room_preset: %d FAIL\n", failures);
    return 1;
}
