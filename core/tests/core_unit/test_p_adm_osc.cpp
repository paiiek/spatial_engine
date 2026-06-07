// test_p_adm_osc.cpp
// ADM-OSC Living Standard receive-path decoder tests.

#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace spe::ipc;

// Build a minimal OSC packet from address + type-tag string + raw arg bytes.
static std::vector<uint8_t> makeOsc(const std::string& addr,
                                     const std::string& tags_no_comma,
                                     const std::vector<uint8_t>& arg_bytes) {
    std::vector<uint8_t> pkt;
    auto padStr = [&](const std::string& s) {
        for (char c : s) pkt.push_back(static_cast<uint8_t>(c));
        pkt.push_back(0);
        while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    padStr(addr);
    padStr("," + tags_no_comma);
    pkt.insert(pkt.end(), arg_bytes.begin(), arg_bytes.end());
    return pkt;
}

static void appendF32(std::vector<uint8_t>& v, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    v.push_back(uint8_t(u >> 24)); v.push_back(uint8_t(u >> 16));
    v.push_back(uint8_t(u >> 8));  v.push_back(uint8_t(u));
}

static void appendI32(std::vector<uint8_t>& v, int32_t i) {
    uint32_t u = static_cast<uint32_t>(i);
    v.push_back(uint8_t(u >> 24)); v.push_back(uint8_t(u >> 16));
    v.push_back(uint8_t(u >> 8));  v.push_back(uint8_t(u));
}

static constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
static constexpr float MAX_DIST = 20.0f;
static constexpr float EPS = 1e-4f;

int main() {
    CommandDecoder dec;

    // 1. /adm/obj/3/azim ,f 45.0 → ObjMove, obj_id=3.
    //    Phase 3.1: ADM az is LEFT-positive, engine az is RIGHT-positive, so the
    //    decode negates: ADM +45° (left) → engine az ≈ -0.785.
    {
        std::vector<uint8_t> args;
        appendF32(args, 45.0f);
        auto pkt = makeOsc("/adm/obj/3/azim", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjMove);
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        assert(p.obj_id == 3);
        assert(std::fabs(p.az_rad - (-45.0f * DEG2RAD)) < EPS);
        (void)p;
        std::puts("  PASS 1: /adm/obj/3/azim (ADM left+ -> engine right+ negated)");
    }

    // 2. /adm/obj/0/elev ,f 30.0 → ObjMove, el_rad≈0.524
    {
        std::vector<uint8_t> args;
        appendF32(args, 30.0f);
        auto pkt = makeOsc("/adm/obj/0/elev", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjMove);
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        assert(p.obj_id == 0);
        assert(std::fabs(p.el_rad - 30.0f * DEG2RAD) < EPS);
        (void)p;
        std::puts("  PASS 2: /adm/obj/0/elev");
    }

    // 3. /adm/obj/1/aed ,fff 90.0 -15.0 0.5 → el≈-0.262, dist≈10.0.
    //    Phase 3.1: ADM +90° az (left) → engine az ≈ -1.571 (negated); el/dist
    //    unchanged.
    {
        std::vector<uint8_t> args;
        appendF32(args, 90.0f);
        appendF32(args, -15.0f);
        appendF32(args, 0.5f);
        auto pkt = makeOsc("/adm/obj/1/aed", "fff", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjMove);
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        assert(p.obj_id == 1);
        assert(std::fabs(p.az_rad - (-90.0f * DEG2RAD)) < EPS);
        assert(std::fabs(p.el_rad - (-15.0f * DEG2RAD)) < EPS);
        assert(std::fabs(p.dist_m - 0.5f * MAX_DIST) < EPS);
        (void)p;
        std::puts("  PASS 3: /adm/obj/1/aed (az negated)");
    }

    // 4. /adm/obj/2/gain ,f 0.5 → ObjGain, gain_linear=0.5
    {
        std::vector<uint8_t> args;
        appendF32(args, 0.5f);
        auto pkt = makeOsc("/adm/obj/2/gain", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjGain);
        auto& p = std::get<PayloadObjGain>(cmd.payload);
        assert(p.obj_id == 2);
        assert(p.gain == 0.5f);
        (void)p;
        std::puts("  PASS 4: /adm/obj/2/gain");
    }

    // 5. /adm/obj/2/mute ,i 1 → ObjMute, muted=true
    {
        std::vector<uint8_t> args;
        appendI32(args, 1);
        auto pkt = makeOsc("/adm/obj/2/mute", "i", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjMute);
        auto& p = std::get<PayloadObjMute>(cmd.payload);
        assert(p.obj_id == 2);
        assert(p.muted == true);
        (void)p;
        std::puts("  PASS 5: /adm/obj/2/mute");
    }

    // 6. /adm/obj/7/w ,f 0.3 → Unknown (width ignored in v0)
    {
        std::vector<uint8_t> args;
        appendF32(args, 0.3f);
        auto pkt = makeOsc("/adm/obj/7/w", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::Unknown);
        std::puts("  PASS 6: /adm/obj/7/w => Unknown");
    }

    // 7. /adm/obj/-1/azim → Unknown (negative obj_id rejected by sscanf guard)
    {
        std::vector<uint8_t> args;
        appendF32(args, 10.0f);
        auto pkt = makeOsc("/adm/obj/-1/azim", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::Unknown);
        std::puts("  PASS 7: /adm/obj/-1/azim => Unknown");
    }

    // 8. Out-of-range obj_id (9999) is decoded but flagged for downstream
    //    range-check by StateModel; decoder itself accepts any non-negative int.
    {
        std::vector<uint8_t> args;
        appendF32(args, 0.0f);
        auto pkt = makeOsc("/adm/obj/9999/azim", "f", args);
        Command cmd = dec.decode(std::span<const uint8_t>(pkt));
        assert(cmd.tag == CommandTag::ObjMove);
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        assert(p.obj_id == 9999u);
        (void)p;
        std::puts("  PASS 8: /adm/obj/9999/azim => ObjMove (range check is downstream)");
    }

    std::puts("PASS test_p_adm_osc");
    return 0;
}
