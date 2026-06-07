// test_osc_ypr_roundtrip.cpp
// Phase 2.6b (Dreamscape Convergence) — /ypr OSC decode unit test.
//
// Proves the inbound wire path: a raw OSC `/ypr ,fff yaw pitch roll` packet
// (and its `/sys/ypr` alias) decodes to CommandTag::SysHeadYpr with the three
// float angles carried through verbatim (DEGREES). Also checks the
// missing-argument default (,f yaw-only → pitch=roll=0), matching the decoder
// contract, and that a bare address with no args is still accepted as a
// zeroing pose (all angles 0) rather than rejected.

#include "ipc/CommandDecoder.h"
#include "ipc/Command.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace spe::ipc;

static int failures = 0;
#define CHECK(c, msg) do { if(!(c)){ std::fprintf(stderr,"FAIL: %s\n", msg); ++failures; } } while(0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Build a raw OSC packet: address + ",<types>" + float args.
static std::vector<uint8_t> oscPacket(const std::string& addr,
                                      const std::string& types,
                                      const std::vector<float>& floats) {
    std::vector<uint8_t> p;
    auto pushPadded = [&](const std::string& s) {
        for (char c : s) p.push_back(static_cast<uint8_t>(c));
        p.push_back(0);
        while (p.size() % 4 != 0) p.push_back(0);
    };
    auto pushF = [&](float f) {
        uint32_t u; std::memcpy(&u, &f, 4);
        p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
        p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
    };
    pushPadded(addr);
    pushPadded("," + types);
    for (float f : floats) pushF(f);
    return p;
}

static void check_ypr(CommandDecoder& dec, const std::string& addr,
                      const std::string& types, const std::vector<float>& floats,
                      float exp_yaw, float exp_pitch, float exp_roll,
                      const char* label) {
    Command c = dec.decode(std::span<const uint8_t>(oscPacket(addr, types, floats)));
    CHECK(c.tag == CommandTag::SysHeadYpr, label);
    if (c.tag != CommandTag::SysHeadYpr) return;
    auto& p = std::get<PayloadSysHeadYpr>(c.payload);
    CHECK(near(p.yaw_deg,   exp_yaw),   "yaw matches");
    CHECK(near(p.pitch_deg, exp_pitch), "pitch matches");
    CHECK(near(p.roll_deg,  exp_roll),  "roll matches");
}

int main() {
    CommandDecoder dec;

    // (1) /ypr ,fff full pose.
    check_ypr(dec, "/ypr", "fff", {30.f, -10.f, 5.f}, 30.f, -10.f, 5.f,
              "/ypr ,fff -> SysHeadYpr");

    // (2) /sys/ypr alias decodes identically.
    check_ypr(dec, "/sys/ypr", "fff", {-45.f, 12.f, -3.f}, -45.f, 12.f, -3.f,
              "/sys/ypr alias -> SysHeadYpr");

    // (3) Missing args default to 0: ,f yaw-only.
    check_ypr(dec, "/ypr", "f", {90.f}, 90.f, 0.f, 0.f,
              "/ypr ,f yaw-only -> pitch=roll=0");

    // (4) ,ff yaw+pitch.
    check_ypr(dec, "/ypr", "ff", {15.f, 7.f}, 15.f, 7.f, 0.f,
              "/ypr ,ff -> roll=0");

    // (5) No args at all is a valid zeroing pose (all 0), not a reject.
    check_ypr(dec, "/ypr", "", {}, 0.f, 0.f, 0.f,
              "/ypr no-args -> zero pose");

    // The five accepted /ypr packets must NOT have incremented the reject count.
    CHECK(dec.rejectCount() == 0, "no rejects for valid /ypr packets");

    if (failures == 0) { std::puts("PASS test_osc_ypr_roundtrip"); return 0; }
    std::fprintf(stderr, "test_osc_ypr_roundtrip: %d FAILURE(S)\n", failures);
    return 1;
}
