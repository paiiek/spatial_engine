// test_p4_flood_malformed.cpp
// P4-b: Flood with malformed packets. Short version (default): 10,000.
//       Long version (LABEL "long"): SPE_FLOOD_LONG=1 → 1,000,000.
// Verifies: rejectCount() == N, no crashes, no memory corruption.
//
// v0.8 audit P3.7 — EXTENDED with three additional malformed-frame cases
// driven by closed-form OSC 1.1 violations:
//   (a) truncated type-tag (",ii" cut after ",i" with no null terminator
//                            before the buffer ends)
//   (b) unknown type byte ",q" where 'q' is NOT a defined OSC type tag
//   (c) misaligned 4-byte padding (address string not padded to a
//       multiple of 4 → byte after null is not at the type-tag boundary)
// For each: assert the decoder REJECTS without crashing, that rejectCount()
// monotonically increases, and that a subsequent WELL-FORMED frame still
// decodes normally (no FSM corruption). Independent oracle: the well-formed
// frame is built via CommandDecoder::encode() so its byte layout is
// guaranteed by the encoder contract — not by happenstance.

#include "ipc/CommandDecoder.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace spe::ipc;

namespace {

int g_extra_failures = 0;

// Build a well-formed /sys/handshake frame via the engine's own encoder.
// Independent oracle for "what a valid frame looks like".
std::vector<uint8_t> make_valid_handshake_frame() {
    Command cmd;
    cmd.tag = CommandTag::SysHandshake;
    cmd.seq = 11;
    cmd.id  = 22;
    PayloadSysHandshake p;
    p.client_schema_version = 0;
    cmd.payload = p;
    std::vector<uint8_t> out;
    CommandDecoder enc;
    [[maybe_unused]] bool ok = enc.encode(cmd, out);
    assert(ok);
    return out;
}

// Run one malformed case: assert reject, count bump, well-formed decode still works.
void run_malformed_case(CommandDecoder& dec,
                        const char* tag,
                        const std::vector<uint8_t>& malformed_bytes) {
    const uint32_t pre = dec.rejectCount();

    // (i) decoder rejects without crash (return-by-value is the no-crash check).
    Command bad = dec.decode(std::span<const uint8_t>(
        malformed_bytes.data(), malformed_bytes.size()));
    if (bad.tag != CommandTag::Unknown) {
        std::fprintf(stderr,
            "FAIL p4_malformed_extra[%s]: expected Unknown, got tag=%d\n",
            tag, static_cast<int>(bad.tag));
        ++g_extra_failures;
        return;
    }

    // (ii) reject counter incremented exactly by 1.
    const uint32_t post = dec.rejectCount();
    if (post != pre + 1u) {
        std::fprintf(stderr,
            "FAIL p4_malformed_extra[%s]: rejectCount %u → %u "
            "(expected +1)\n", tag, pre, post);
        ++g_extra_failures;
        return;
    }

    // (iii) FSM-integrity probe: a subsequent well-formed frame still decodes.
    const auto valid = make_valid_handshake_frame();
    Command good = dec.decode(std::span<const uint8_t>(valid.data(), valid.size()));
    if (good.tag != CommandTag::SysHandshake) {
        std::fprintf(stderr,
            "FAIL p4_malformed_extra[%s]: post-malformed valid frame "
            "rejected (tag=%d)\n", tag, static_cast<int>(good.tag));
        ++g_extra_failures;
        return;
    }
    if (good.seq != 11u || good.id != 22u) {
        std::fprintf(stderr,
            "FAIL p4_malformed_extra[%s]: post-malformed valid frame "
            "seq/id corrupt (seq=%u id=%u, expected 11/22)\n",
            tag, good.seq, good.id);
        ++g_extra_failures;
        return;
    }

    std::printf("PASS p4_malformed_extra[%s]: rejected, reject_count++, "
                "valid frame still decodes\n", tag);
}

// ---- Adversarial frame builders (independent oracles) ---------------------
//
// Each builder constructs a byte sequence that violates a SPECIFIC clause of
// OSC 1.1 — NOT pulled from CommandDecoder.cpp's parser, but typed from the
// spec. If the parser is ever rewritten to ACCEPT one of these, this test
// fails loudly.

// (a) Truncated type-tag: the address ends correctly on a 4-byte boundary,
//     but the type-tag bytes (",ii") have no null terminator before EOF.
//     OSC 1.1 §3 requires the type-tag string to be null-terminated AND
//     padded to 4 bytes. Truncating violates the null-terminator clause.
std::vector<uint8_t> build_truncated_type_tag() {
    std::vector<uint8_t> b;
    // Address "/sys/reset\0\0" — 12 bytes (padded to 4).
    static const char kAddr[] = "/sys/reset";
    for (const char* p = kAddr; *p; ++p) b.push_back(static_cast<uint8_t>(*p));
    b.push_back(0);
    while (b.size() % 4 != 0) b.push_back(0);
    // Type-tag bytes WITHOUT null terminator and WITHOUT padding —
    // exactly 3 bytes ",ii". The parser must run out of buffer before
    // finding the null and reject.
    b.push_back(',');
    b.push_back('i');
    b.push_back('i');
    return b;
}

// (b) Unknown type byte: 'q' is not in OSC 1.1's defined type set
//     {i,f,h,d,t,s,b,T,F,N,I,...} — and specifically not in the parser's
//     supported set {i,f,h,d,t,s}. Frame structure is otherwise valid.
std::vector<uint8_t> build_unknown_type_byte() {
    std::vector<uint8_t> b;
    // Address "/obj/move\0\0\0" — 12 bytes.
    static const char kAddr[] = "/obj/move";
    for (const char* p = kAddr; *p; ++p) b.push_back(static_cast<uint8_t>(*p));
    b.push_back(0);
    while (b.size() % 4 != 0) b.push_back(0);
    // Type-tag ",q\0\0" — 4 bytes. 'q' is not a known OSC type byte.
    b.push_back(',');
    b.push_back('q');
    b.push_back(0);
    b.push_back(0);
    // One 4-byte argument blob so the parser does not bail on length first.
    b.push_back(0x42); b.push_back(0); b.push_back(0); b.push_back(0);
    return b;
}

// (c) Misaligned padding: address null terminator placed so that the
//     type-tag start is NOT on a 4-byte boundary. OSC 1.1 §2.1 requires
//     OSC-strings to be padded to a multiple of 4 bytes including their
//     null terminator. We omit the trailing pad bytes after the address.
//     The parser advances by (addr_len + 3) & ~3 from the address start,
//     so a missing pad lands `p` past the actual type-tag ',', producing
//     either a non-comma byte at the new position OR a buffer overrun.
std::vector<uint8_t> build_misaligned_padding() {
    std::vector<uint8_t> b;
    // Address "/abc\0" — 5 bytes (correct pad would be 8). Skip the pad.
    static const char kAddr[] = "/abc";
    for (const char* p = kAddr; *p; ++p) b.push_back(static_cast<uint8_t>(*p));
    b.push_back(0);
    // Now place a comma directly at byte 5 (not at byte 8). The parser
    // advances p by ((5 + 3) & ~3) = 8 from the address start → lands on
    // whatever is at byte 8, which is the second byte of our (improperly
    // padded) type-tag — not the ','. Result: parser sees a non-',' byte
    // at the type-tag start position and rejects.
    b.push_back(',');
    b.push_back('i');
    b.push_back(0);
    // Now at byte 8 — pad-aligned, but address-padding rule violated.
    // Add a few harmless bytes so the buffer is long enough to keep
    // parsing into the integer slot if (by bug) it accepts.
    b.push_back(0);
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(7);
    return b;
}

} // namespace

int main() {
#if defined(SPE_FLOOD_LONG) && SPE_FLOOD_LONG
    const int N = 1'000'000;
    const char* label = "long";
#else
    const int N = 10'000;
    const char* label = "short";
#endif

    CommandDecoder dec;

    // Various malformed packet patterns.
    const uint8_t patterns[][8] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // no leading '/'
        {0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x00, 0x00, 0x00}, // garbage
        {0x01},                                             // too short (1 byte)
        {'/',  'a',  'b',  0x00, 'X',  0x00, 0x00, 0x00}, // no ',' in type-tag
    };
    const int NUM_PATTERNS = 4;

    int total_rejected = 0;
    for (int i = 0; i < N; ++i) {
        const auto& pat = patterns[i % NUM_PATTERNS];
        // pat[2] is 1-byte for the short case; we use actual lengths.
        std::span<const uint8_t> sp;
        if (i % NUM_PATTERNS == 2) {
            sp = std::span<const uint8_t>(pat, 1);
        } else {
            sp = std::span<const uint8_t>(pat, 8);
        }
        Command rt = dec.decode(sp);
        assert(rt.tag == CommandTag::Unknown);
        ++total_rejected;
    }

    assert(dec.rejectCount() == static_cast<uint32_t>(total_rejected));
    (void)label;
    std::printf("PASS test_p4_flood_malformed [%s, N=%d, rejected=%d]\n",
                label, N, total_rejected);

    // ── v0.8 audit P3.7 — three additional malformed-frame cases.
    //    Each case asserts: (i) reject (Unknown tag), (ii) rejectCount()
    //    increments exactly +1, (iii) subsequent well-formed frame decodes
    //    cleanly (no FSM corruption). The well-formed probe uses
    //    CommandDecoder::encode() as an independent oracle for "valid".
    run_malformed_case(dec, "truncated_type_tag",   build_truncated_type_tag());
    run_malformed_case(dec, "unknown_type_byte",    build_unknown_type_byte());
    run_malformed_case(dec, "misaligned_padding",   build_misaligned_padding());

    if (g_extra_failures > 0) {
        std::fprintf(stderr,
            "FAIL test_p4_flood_malformed P3.7 extras: %d failure(s)\n",
            g_extra_failures);
        return 1;
    }
    std::printf("PASS test_p4_flood_malformed P3.7 extras (3/3 cases)\n");
    return 0;
}
