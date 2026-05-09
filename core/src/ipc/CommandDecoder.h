// core/src/ipc/CommandDecoder.h
// JUCE-free OSC packet decoder: raw bytes → Command.
// Minimal OSC 1.1 subset: address string + type-tag string + arguments.
// Malformed packets increment a reject counter and return CommandTag::Unknown.

#pragma once
#include "Command.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace spe::ipc {

// Lightweight OSC argument list (decoded from a raw packet).
struct OscArgs {
    std::string address;    // e.g. "/obj/move"
    std::string type_tags;  // e.g. "iifff"  (without leading ',')
    // Decoded values (union-style, positional):
    // We store up to 8 slots of each type for simplicity.
    static constexpr int MAX_ARGS = 8;
    int32_t  ints  [MAX_ARGS]{};
    float    floats[MAX_ARGS]{};
    uint64_t u64s  [MAX_ARGS]{};
    std::string strings[MAX_ARGS];
    int n_int = 0, n_float = 0, n_u64 = 0, n_str = 0;
};

// Wire dialect for encode() — default is Legacy to preserve all existing tests.
// Switch to AdmV1 via --osc-dialect adm CLI flag (ADR 0006).
enum class WireDialect : uint8_t {
    Legacy = 0,  // /obj/... prefix (existing behaviour)
    AdmV1  = 1,  // /adm/obj/... prefix (ADM-OSC v1.0)
};

class CommandDecoder {
public:
    CommandDecoder() = default;

    // Decode raw OSC bytes into a Command.
    // Returns CommandTag::Unknown and increments reject_count() on error.
    Command decode(std::span<const uint8_t> packet) noexcept;

    // Encode a Command back to OSC bytes.
    // dialect=Legacy (default): /obj/... schema (backward compat).
    // dialect=AdmV1: /adm/obj/... schema (ADM-OSC v1.0).
    // Returns false if encoding is not supported for this tag/dialect.
    bool encode(const Command& cmd, std::vector<uint8_t>& out,
                WireDialect dialect = WireDialect::Legacy) noexcept;

    uint32_t rejectCount() const noexcept { return reject_count_; }
    void     resetRejectCount() noexcept  { reject_count_ = 0; }

private:
    uint32_t reject_count_ = 0;

    // Low-level OSC parsing helpers.
    static bool parseOscPacket(std::span<const uint8_t> data, OscArgs& out) noexcept;
    static Command buildCommand(const OscArgs& args, uint32_t& reject_count) noexcept;

    // OSC encode helpers.
    static void appendPaddedString(std::vector<uint8_t>& buf, const std::string& s) noexcept;
    static void appendInt32(std::vector<uint8_t>& buf, int32_t v) noexcept;
    static void appendFloat32(std::vector<uint8_t>& buf, float v) noexcept;
    static void appendUint64(std::vector<uint8_t>& buf, uint64_t v) noexcept;
};

} // namespace spe::ipc
