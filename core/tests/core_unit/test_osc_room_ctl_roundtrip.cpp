// test_osc_room_ctl_roundtrip.cpp
// Dreamscape Convergence ⑥e-4 — wire-format coverage for the /room/* OSC scheme.
//
// Verifies the single-leading-tag dialect (NO ,ii seq/id header) for:
//   /room/enable ,i   /room/set ,f×22   /room/t60 ,f   /room/size ,fff
//   /room/early/{width,balance} ,f   /room/cluster/{send,diffusion,volume} ,f
//   /room/eq/early ,ff   /room/late/hf ,ff
// plus reject paths (partial /room/set, unknown /room/* leaf). Each op is also
// encode()->decode() round-tripped so the encoder and decoder agree.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace spe::ipc;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } } while (0)

static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build a raw OSC packet with a single-leading-tag (no seq/id) type string.
static std::vector<uint8_t> osc(const std::string& addr, const std::string& types,
                                const std::vector<float>& fargs,
                                const std::vector<int32_t>& iargs) {
    auto pad = [](std::vector<uint8_t>& b) { while (b.size() % 4 != 0) b.push_back(0); };
    std::vector<uint8_t> p;
    for (char c : addr) p.push_back(static_cast<uint8_t>(c));
    p.push_back(0); pad(p);
    std::string tt = "," + types;
    for (char c : tt) p.push_back(static_cast<uint8_t>(c));
    p.push_back(0); pad(p);
    size_t fi = 0, ii = 0;
    auto be32 = [&](uint32_t u) {
        p.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
        p.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
        p.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        p.push_back(static_cast<uint8_t>(u & 0xFF));
    };
    for (char t : types) {
        if (t == 'f') { uint32_t u; float v = fargs[fi++]; std::memcpy(&u, &v, 4); be32(u); }
        else if (t == 'i') { be32(static_cast<uint32_t>(iargs[ii++])); }
    }
    return p;
}

int main() {
    CommandDecoder dec;

    // ---- /room/enable ,i 1 ----
    {
        auto pkt = osc("/room/enable", "i", {}, {1});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::RoomCtl, "enable: tag RoomCtl");
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::Enable, "enable: op Enable");
        CHECK(p.enable == true, "enable: flag true");
    }
    // ---- /room/t60 ,f 1.8 ----
    {
        auto pkt = osc("/room/t60", "f", {1.8f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(c.tag == CommandTag::RoomCtl && p.op == PayloadRoomCtl::Op::T60, "t60: op");
        CHECK(feq(p.t60, 1.8f), "t60: value");
    }
    // ---- /room/size ,fff 6 5 3 ----
    {
        auto pkt = osc("/room/size", "fff", {6.f, 5.f, 3.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::Size, "size: op");
        CHECK(feq(p.sx, 6.f) && feq(p.sy, 5.f) && feq(p.sz, 3.f), "size: xyz");
    }
    // ---- /room/eq/early ,ff 200 8000 ----
    {
        auto pkt = osc("/room/eq/early", "ff", {200.f, 8000.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::EqEarly, "eq/early: op");
        CHECK(feq(p.eq_early_hp, 200.f) && feq(p.eq_early_lp, 8000.f), "eq/early: hp/lp");
    }
    // ---- /room/late/hf ,ff 7000 0.5 ----
    {
        auto pkt = osc("/room/late/hf", "ff", {7000.f, 0.5f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::LateHf, "late/hf: op");
        CHECK(feq(p.late_hf_corner_hz, 7000.f) && feq(p.late_hf_ratio01, 0.5f), "late/hf: vals");
    }
    // ---- /room/eq/late ,ff 60 14000 ----
    {
        auto pkt = osc("/room/eq/late", "ff", {60.f, 14000.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::EqLate, "eq/late: op");
        CHECK(feq(p.eq_late_hp, 60.f) && feq(p.eq_late_lp, 14000.f), "eq/late: hp/lp");
    }
    // ---- /room/distance ,fff 0.8 30 0.5 ----
    {
        auto pkt = osc("/room/distance", "fff", {0.8f, 30.f, 0.5f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::Distance, "distance: op");
        CHECK(feq(p.dist_near_m, 0.8f) && feq(p.dist_far_m, 30.f) && feq(p.dist_linearity01, 0.5f),
              "distance: near/far/lin");
    }
    // ---- /room/early/gain ,ff -8 -20 ----
    {
        auto pkt = osc("/room/early/gain", "ff", {-8.f, -20.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::EarlyGain, "early/gain: op");
        CHECK(feq(p.early_gain_close_db, -8.f) && feq(p.early_gain_far_db, -20.f), "early/gain: dB");
    }
    // ---- /room/late/gain ,ff -14 -2 ----
    {
        auto pkt = osc("/room/late/gain", "ff", {-14.f, -2.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::LateGain, "late/gain: op");
        CHECK(feq(p.late_gain_close_db, -14.f) && feq(p.late_gain_far_db, -2.f), "late/gain: dB");
    }
    // ---- /room/predelay ,f 35 ----
    {
        auto pkt = osc("/room/predelay", "f", {35.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::Predelay, "predelay: op");
        CHECK(feq(p.early_predelay_ms, 35.f), "predelay: ms");
    }
    // ---- /room/cluster/* and /room/early/* ----
    {
        auto v = [&](const char* a, PayloadRoomCtl::Op op, float val, float PayloadRoomCtl::* m) {
            auto pkt = osc(a, "f", {val}, {});
            Command c = dec.decode(std::span<const uint8_t>(pkt));
            auto& p = std::get<PayloadRoomCtl>(c.payload);
            CHECK(c.tag == CommandTag::RoomCtl && p.op == op, a);
            CHECK(feq(p.*m, val), a);
        };
        v("/room/early/width",       PayloadRoomCtl::Op::EarlyWidth,       33.f,  &PayloadRoomCtl::early_width_deg);
        v("/room/early/balance",     PayloadRoomCtl::Op::EarlyBalance,     0.6f,  &PayloadRoomCtl::early_balance01);
        v("/room/cluster/send",      PayloadRoomCtl::Op::ClusterSend,      0.7f,  &PayloadRoomCtl::cluster_send01);
        v("/room/cluster/diffusion", PayloadRoomCtl::Op::ClusterDiffusion, 0.3f,  &PayloadRoomCtl::cluster_diffusion01);
        v("/room/cluster/volume",    PayloadRoomCtl::Op::ClusterVolume,    900.f, &PayloadRoomCtl::cluster_volume_m3);
    }
    // ---- /room/set ,f×22 (full atomic bundle) ----
    {
        std::vector<float> a = {1.9f, 7.f, 6.f, 4.f, 50.f, 0.5f,
                                0.55f, 0.4f, 720.f, 150.f, 9000.f, 6000.f, 0.7f,
                                55.f, 13000.f,
                                0.8f, 30.f, 0.5f, -8.f, -20.f, -14.f, -2.f, 35.f};
        auto pkt = osc("/room/set", std::string(23, 'f'), a, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::RoomCtl, "set: tag");
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::SetAll, "set: op SetAll");
        CHECK(feq(p.t60, 1.9f) && feq(p.sx, 7.f) && feq(p.sz, 4.f), "set: t60/size");
        CHECK(feq(p.early_width_deg, 50.f) && feq(p.cluster_volume_m3, 720.f), "set: width/vol");
        CHECK(feq(p.eq_early_hp, 150.f) && feq(p.eq_early_lp, 9000.f), "set: eq early");
        CHECK(feq(p.late_hf_corner_hz, 6000.f) && feq(p.late_hf_ratio01, 0.7f), "set: late/hf");
        CHECK(feq(p.eq_late_hp, 55.f) && feq(p.eq_late_lp, 13000.f), "set: eq late");
        CHECK(feq(p.dist_near_m, 0.8f) && feq(p.dist_far_m, 30.f) && feq(p.dist_linearity01, 0.5f),
              "set: distance");
        CHECK(feq(p.early_gain_close_db, -8.f) && feq(p.early_gain_far_db, -20.f), "set: early gain");
        CHECK(feq(p.late_gain_close_db, -14.f) && feq(p.late_gain_far_db, -2.f), "set: late gain");
        CHECK(feq(p.early_predelay_ms, 35.f), "set: predelay");
    }
    // ---- reject: partial /room/set (only 22 floats — now needs 23) ----
    {
        const uint32_t before = dec.rejectCount();
        std::vector<float> a22(22, 1.f);
        auto pkt = osc("/room/set", std::string(22, 'f'), a22, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::Unknown, "partial set (22<23): rejected");
        CHECK(dec.rejectCount() == before + 1, "partial set: reject counted");
    }
    // ---- reject: unknown /room/* leaf ----
    {
        auto pkt = osc("/room/bogus", "f", {1.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::Unknown, "unknown leaf: rejected");
    }

    // ---- encode -> decode round-trip for each op ----
    {
        auto rt = [&](const PayloadRoomCtl& in) -> PayloadRoomCtl {
            Command c; c.tag = CommandTag::RoomCtl; c.payload = in;
            std::vector<uint8_t> buf;
            bool ok = dec.encode(c, buf);
            CHECK(ok, "encode ok");
            Command d = dec.decode(std::span<const uint8_t>(buf));
            CHECK(d.tag == CommandTag::RoomCtl, "rt tag");
            return std::get<PayloadRoomCtl>(d.payload);
        };
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::Enable; p.enable = true;
          auto o = rt(p); CHECK(o.op == p.op && o.enable, "rt enable"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::T60; p.t60 = 2.4f;
          auto o = rt(p); CHECK(o.op == p.op && feq(o.t60, 2.4f), "rt t60"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::Size; p.sx = 8; p.sy = 7; p.sz = 5;
          auto o = rt(p); CHECK(feq(o.sx,8)&&feq(o.sy,7)&&feq(o.sz,5), "rt size"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::EqEarly; p.eq_early_hp = 180; p.eq_early_lp = 9500;
          auto o = rt(p); CHECK(feq(o.eq_early_hp,180)&&feq(o.eq_early_lp,9500), "rt eq"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::LateHf; p.late_hf_corner_hz = 7200; p.late_hf_ratio01 = 0.55f;
          auto o = rt(p); CHECK(feq(o.late_hf_corner_hz,7200)&&feq(o.late_hf_ratio01,0.55f), "rt late/hf"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::EqLate; p.eq_late_hp = 50; p.eq_late_lp = 15000;
          auto o = rt(p); CHECK(feq(o.eq_late_hp,50)&&feq(o.eq_late_lp,15000), "rt eq/late"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::Distance; p.dist_near_m=0.7f; p.dist_far_m=28; p.dist_linearity01=0.4f;
          auto o = rt(p); CHECK(feq(o.dist_near_m,0.7f)&&feq(o.dist_far_m,28)&&feq(o.dist_linearity01,0.4f), "rt distance"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::EarlyGain; p.early_gain_close_db=-9; p.early_gain_far_db=-16;
          auto o = rt(p); CHECK(feq(o.early_gain_close_db,-9)&&feq(o.early_gain_far_db,-16), "rt early/gain"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::LateGain; p.late_gain_close_db=-13; p.late_gain_far_db=-1;
          auto o = rt(p); CHECK(feq(o.late_gain_close_db,-13)&&feq(o.late_gain_far_db,-1), "rt late/gain"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::Predelay; p.early_predelay_ms=28;
          auto o = rt(p); CHECK(feq(o.early_predelay_ms,28), "rt predelay"); }
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::SetAll;
          p.t60=1.5f; p.sx=6; p.sy=5; p.sz=3; p.early_width_deg=40; p.early_balance01=0.5f;
          p.cluster_send01=0.45f; p.cluster_diffusion01=0.5f; p.cluster_volume_m3=650;
          p.eq_early_hp=130; p.eq_early_lp=9800; p.late_hf_corner_hz=6300; p.late_hf_ratio01=0.6f;
          p.eq_late_hp=48; p.eq_late_lp=15500;
          p.dist_near_m=0.6f; p.dist_far_m=26; p.dist_linearity01=0.3f;
          p.early_gain_close_db=-11; p.early_gain_far_db=-19; p.late_gain_close_db=-13; p.late_gain_far_db=-1;
          p.early_predelay_ms=22;
          auto o = rt(p);
          CHECK(o.op == PayloadRoomCtl::Op::SetAll, "rt set op");
          CHECK(feq(o.t60,1.5f)&&feq(o.early_width_deg,40)&&feq(o.cluster_volume_m3,650), "rt set a");
          CHECK(feq(o.eq_early_hp,130)&&feq(o.late_hf_ratio01,0.6f), "rt set b");
          CHECK(feq(o.eq_late_hp,48)&&feq(o.eq_late_lp,15500), "rt set eq/late");
          CHECK(feq(o.dist_near_m,0.6f)&&feq(o.dist_far_m,26)&&feq(o.dist_linearity01,0.3f), "rt set distance");
          CHECK(feq(o.early_gain_close_db,-11)&&feq(o.late_gain_far_db,-1), "rt set gains");
          CHECK(feq(o.early_predelay_ms,22), "rt set predelay"); }
    }

    if (failures == 0) { std::printf("test_osc_room_ctl_roundtrip: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_osc_room_ctl_roundtrip: %d FAIL\n", failures);
    return 1;
}
