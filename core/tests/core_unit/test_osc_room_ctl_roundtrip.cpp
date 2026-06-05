// test_osc_room_ctl_roundtrip.cpp
// Dreamscape Convergence ⑥e-4 — wire-format coverage for the /room/* OSC scheme.
//
// Verifies the single-leading-tag dialect (NO ,ii seq/id header) for:
//   /room/enable ,i   /room/set ,f×13   /room/t60 ,f   /room/size ,fff
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
    // ---- /room/set ,f×13 (full atomic bundle) ----
    {
        std::vector<float> a = {1.9f, 7.f, 6.f, 4.f, 50.f, 0.5f,
                                0.55f, 0.4f, 720.f, 150.f, 9000.f, 6000.f, 0.7f};
        auto pkt = osc("/room/set", std::string(13, 'f'), a, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::RoomCtl, "set: tag");
        auto& p = std::get<PayloadRoomCtl>(c.payload);
        CHECK(p.op == PayloadRoomCtl::Op::SetAll, "set: op SetAll");
        CHECK(feq(p.t60, 1.9f) && feq(p.sx, 7.f) && feq(p.sz, 4.f), "set: t60/size");
        CHECK(feq(p.early_width_deg, 50.f) && feq(p.cluster_volume_m3, 720.f), "set: width/vol");
        CHECK(feq(p.eq_early_hp, 150.f) && feq(p.eq_early_lp, 9000.f), "set: eq");
        CHECK(feq(p.late_hf_corner_hz, 6000.f) && feq(p.late_hf_ratio01, 0.7f), "set: late/hf");
    }
    // ---- reject: partial /room/set (only 5 floats) ----
    {
        const uint32_t before = dec.rejectCount();
        auto pkt = osc("/room/set", "fffff", {1.f, 2.f, 3.f, 4.f, 5.f}, {});
        Command c = dec.decode(std::span<const uint8_t>(pkt));
        CHECK(c.tag == CommandTag::Unknown, "partial set: rejected");
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
        { PayloadRoomCtl p; p.op = PayloadRoomCtl::Op::SetAll;
          p.t60=1.5f; p.sx=6; p.sy=5; p.sz=3; p.early_width_deg=40; p.early_balance01=0.5f;
          p.cluster_send01=0.45f; p.cluster_diffusion01=0.5f; p.cluster_volume_m3=650;
          p.eq_early_hp=130; p.eq_early_lp=9800; p.late_hf_corner_hz=6300; p.late_hf_ratio01=0.6f;
          auto o = rt(p);
          CHECK(o.op == PayloadRoomCtl::Op::SetAll, "rt set op");
          CHECK(feq(o.t60,1.5f)&&feq(o.early_width_deg,40)&&feq(o.cluster_volume_m3,650), "rt set a");
          CHECK(feq(o.eq_early_hp,130)&&feq(o.late_hf_ratio01,0.6f), "rt set b"); }
    }

    if (failures == 0) { std::printf("test_osc_room_ctl_roundtrip: ALL PASS\n"); return 0; }
    std::fprintf(stderr, "test_osc_room_ctl_roundtrip: %d FAIL\n", failures);
    return 1;
}
