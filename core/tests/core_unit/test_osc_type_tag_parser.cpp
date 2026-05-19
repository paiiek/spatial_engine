// test_osc_type_tag_parser.cpp
// ADR 0018 PR1 — P1-P4: parser-level type-tag switch coverage.
//
// Strategy: drive through CommandDecoder::decode() with crafted raw OSC
// packets addressed to /hb/ping, which the address dispatcher already
// handles. The /hb/ping branch exposes n_double and n_u64 indirectly via
// the decoded timestamp_ms field, letting us assert parser behaviour without
// exposing parseOscPacket (which remains private).

#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace spe::ipc;

// ---- OSC packet builder helpers --------------------------------------------

static void appendPaddedStr(std::vector<uint8_t>& buf, const char* s) {
    while (*s) buf.push_back(static_cast<uint8_t>(*s++));
    buf.push_back(0);
    while (buf.size() % 4 != 0) buf.push_back(0);
}

static void appendU32BE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >>  8));
    buf.push_back(static_cast<uint8_t>(v));
}

static void appendU64BE(std::vector<uint8_t>& buf, uint64_t v) {
    appendU32BE(buf, static_cast<uint32_t>(v >> 32));
    appendU32BE(buf, static_cast<uint32_t>(v));
}

static void appendF64BE(std::vector<uint8_t>& buf, double d) {
    uint64_t u; std::memcpy(&u, &d, 8);
    appendU64BE(buf, u);
}

static void appendI32BE(std::vector<uint8_t>& buf, int32_t v) {
    appendU32BE(buf, static_cast<uint32_t>(v));
}

// Build a minimal OSC 1.1 packet: addr + type-tag string + raw arg bytes.
static std::vector<uint8_t> buildPacket(const char* addr, const char* tags,
                                         const std::vector<uint8_t>& argbytes) {
    std::vector<uint8_t> buf;
    appendPaddedStr(buf, addr);
    appendPaddedStr(buf, tags);
    buf.insert(buf.end(), argbytes.begin(), argbytes.end());
    return buf;
}

// ---- P1: ',d 1.5' → doubles[0] == 1.5 ------------------------------------
// /hb/ping ,d uses n_double → timestamp_ms = (uint64_t)(1.5 * 1000) = 1500.

static void p1_tag_d_double_populates_args_doubles() {
    std::vector<uint8_t> args;
    appendF64BE(args, 1.5);
    auto pkt = buildPacket("/hb/ping", ",d", args);

    CommandDecoder dec;
    Command cmd = dec.decode(std::span<const uint8_t>(pkt));
    assert(cmd.tag == CommandTag::HbPing);
    auto& ph = std::get<PayloadHbPing>(cmd.payload);
    assert(ph.timestamp_ms == 1500);
    std::printf("P1 PASS: tag_d_double_populates_args_doubles\n");
}

// ---- P2: ',h 42' → u64s[0] == 42 -----------------------------------------
// /hb/ping ,h: n_u64 > 0 → timestamp_ms = 42.

static void p2_tag_h_int64_populates_args_u64s() {
    std::vector<uint8_t> args;
    appendU64BE(args, 42ULL);
    auto pkt = buildPacket("/hb/ping", ",h", args);

    CommandDecoder dec;
    Command cmd = dec.decode(std::span<const uint8_t>(pkt));
    assert(cmd.tag == CommandTag::HbPing);
    auto& ph = std::get<PayloadHbPing>(cmd.payload);
    assert(ph.timestamp_ms == 42);
    std::printf("P2 PASS: tag_h_int64_populates_args_u64s\n");
}

// ---- P3: ',id' int=7 double=3.25 → both lanes populated -------------------
// The int occupies n_int (7), the double occupies n_double (3.25).
// /hb/ping branch: n_u64==0, n_double==1 → timestamp_ms = (uint64_t)(3250) = 3250.

static void p3_tag_d_mixed_with_i() {
    std::vector<uint8_t> args;
    appendI32BE(args, 7);
    appendF64BE(args, 3.25);
    auto pkt = buildPacket("/hb/ping", ",id", args);

    CommandDecoder dec;
    Command cmd = dec.decode(std::span<const uint8_t>(pkt));
    assert(cmd.tag == CommandTag::HbPing);
    auto& ph = std::get<PayloadHbPing>(cmd.payload);
    assert(ph.timestamp_ms == 3250);
    std::printf("P3 PASS: tag_d_mixed_with_i\n");
}

// ---- P4: ',z' → parse returns false → CommandTag::Unknown -----------------
// Pins that default: return false; is not accidentally widened.

static void p4_tag_unknown_still_rejects() {
    std::vector<uint8_t> args(4, 0); // dummy 4 bytes
    auto pkt = buildPacket("/hb/ping", ",z", args);

    CommandDecoder dec;
    Command cmd = dec.decode(std::span<const uint8_t>(pkt));
    assert(cmd.tag == CommandTag::Unknown);
    assert(dec.rejectCount() == 1);
    std::printf("P4 PASS: tag_unknown_still_rejects\n");
}

// ---------------------------------------------------------------------------

int main() {
    p1_tag_d_double_populates_args_doubles();
    p2_tag_h_int64_populates_args_u64s();
    p3_tag_d_mixed_with_i();
    p4_tag_unknown_still_rejects();
    std::printf("All P1-P4 parser tests PASSED\n");
    return 0;
}
