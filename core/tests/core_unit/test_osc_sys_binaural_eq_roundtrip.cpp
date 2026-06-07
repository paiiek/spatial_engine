// test_osc_sys_binaural_eq_roundtrip.cpp
// Phase 2.5 (Dreamscape Convergence) — /sys/binaural_eq OSC decode unit test.
//
// Proves the inbound wire path: raw OSC packets for the binaural monitor EQ
// decode to CommandTag::SysBinauralEq with the right op + fields:
//   /sys/binaural_eq/enable ,i {0|1}     -> Op::Enable, enable flag
//   /sys/binaural_eq/band   ,ifff b f g q -> Op::Band, band/freq/gain/Q carried
//   /sys/binaural_eq/band   ,iff  b f g   -> Q defaults to 1 (missing arg)
// and that valid packets do not increment the reject count.

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

static void pushPadded(std::vector<uint8_t>& p, const std::string& s) {
    for (char c : s) p.push_back(static_cast<uint8_t>(c));
    p.push_back(0);
    while (p.size() % 4 != 0) p.push_back(0);
}
static void pushI(std::vector<uint8_t>& p, int32_t v) {
    p.push_back((v>>24)&0xFF); p.push_back((v>>16)&0xFF);
    p.push_back((v>>8)&0xFF); p.push_back(v&0xFF);
}
static void pushF(std::vector<uint8_t>& p, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    p.push_back((u>>24)&0xFF); p.push_back((u>>16)&0xFF);
    p.push_back((u>>8)&0xFF); p.push_back(u&0xFF);
}

int main() {
    CommandDecoder dec;

    // (1) enable ,i 1
    {
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_eq/enable");
        pushPadded(p, ",i"); pushI(p, 1);
        Command c = dec.decode(std::span<const uint8_t>(p));
        CHECK(c.tag == CommandTag::SysBinauralEq, "enable -> SysBinauralEq");
        if (c.tag == CommandTag::SysBinauralEq) {
            auto& pl = std::get<PayloadSysBinauralEq>(c.payload);
            CHECK(pl.op == PayloadSysBinauralEq::Op::Enable, "enable op");
            CHECK(pl.enable == true, "enable=1");
        }
    }
    // (2) enable ,i 0
    {
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_eq/enable");
        pushPadded(p, ",i"); pushI(p, 0);
        Command c = dec.decode(std::span<const uint8_t>(p));
        auto& pl = std::get<PayloadSysBinauralEq>(c.payload);
        CHECK(pl.op == PayloadSysBinauralEq::Op::Enable && pl.enable == false, "enable=0");
    }
    // (3) band ,ifff 2 1500 -6 3
    {
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_eq/band");
        pushPadded(p, ",ifff"); pushI(p, 2); pushF(p, 1500.f); pushF(p, -6.f); pushF(p, 3.f);
        Command c = dec.decode(std::span<const uint8_t>(p));
        CHECK(c.tag == CommandTag::SysBinauralEq, "band -> SysBinauralEq");
        auto& pl = std::get<PayloadSysBinauralEq>(c.payload);
        CHECK(pl.op == PayloadSysBinauralEq::Op::Band, "band op");
        CHECK(pl.band == 2, "band index");
        CHECK(near(pl.freq_hz, 1500.f), "band freq");
        CHECK(near(pl.gain_db, -6.f), "band gain");
        CHECK(near(pl.q, 3.f), "band Q");
    }
    // (4) band ,iff 0 80 4 -> Q defaults to 1
    {
        std::vector<uint8_t> p; pushPadded(p, "/sys/binaural_eq/band");
        pushPadded(p, ",iff"); pushI(p, 0); pushF(p, 80.f); pushF(p, 4.f);
        Command c = dec.decode(std::span<const uint8_t>(p));
        auto& pl = std::get<PayloadSysBinauralEq>(c.payload);
        CHECK(pl.band == 0 && near(pl.freq_hz, 80.f) && near(pl.gain_db, 4.f), "band ,iff fields");
        CHECK(near(pl.q, 1.f), "missing Q defaults to 1");
    }

    CHECK(dec.rejectCount() == 0, "no rejects for valid /sys/binaural_eq packets");

    if (failures == 0) { std::puts("PASS test_osc_sys_binaural_eq_roundtrip"); return 0; }
    std::fprintf(stderr, "test_osc_sys_binaural_eq_roundtrip: %d FAILURE(S)\n", failures);
    return 1;
}
