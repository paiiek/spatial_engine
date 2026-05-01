// core/src/ipc/CommandDecoder.cpp
// Minimal OSC 1.1 subset decoder/encoder. No JUCE dependency.

#include "ipc/CommandDecoder.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace spe::ipc {

// ---- OSC integer byte-swap helpers (big-endian on wire) --------------------

static inline uint32_t readU32(const uint8_t* p) noexcept {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

static inline uint64_t readU64(const uint8_t* p) noexcept {
    return (uint64_t(readU32(p)) << 32) | uint64_t(readU32(p + 4));
}

static inline float readF32(const uint8_t* p) noexcept {
    uint32_t u = readU32(p);
    float f; std::memcpy(&f, &u, 4);
    return f;
}

static inline void writeU32(uint8_t* p, uint32_t v) noexcept {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

static inline void writeU64(uint8_t* p, uint64_t v) noexcept {
    writeU32(p,     uint32_t(v >> 32));
    writeU32(p + 4, uint32_t(v));
}

// ---- parseOscPacket --------------------------------------------------------

bool CommandDecoder::parseOscPacket(std::span<const uint8_t> data, OscArgs& out) noexcept {
    if (data.size() < 8) return false;

    // 1. Address string (null-terminated, padded to 4 bytes).
    const uint8_t* p   = data.data();
    const uint8_t* end = data.data() + data.size();

    if (p[0] != '/') return false; // must start with '/'

    // Find null terminator for address.
    const uint8_t* addr_end = p;
    while (addr_end < end && *addr_end != '\0') ++addr_end;
    if (addr_end >= end) return false;

    out.address.assign(reinterpret_cast<const char*>(p),
                       reinterpret_cast<const char*>(addr_end));

    // Advance p to 4-byte boundary after address (including null).
    ptrdiff_t addr_len = addr_end - p + 1;
    ptrdiff_t addr_padded = (addr_len + 3) & ~3;
    p += addr_padded;
    if (p >= end) return false;

    // 2. Type-tag string (starts with ',').
    if (*p != ',') return false;
    const uint8_t* tt_start = p + 1; // skip ','
    const uint8_t* tt_end   = p;
    while (tt_end < end && *tt_end != '\0') ++tt_end;
    if (tt_end >= end) return false;

    out.type_tags.assign(reinterpret_cast<const char*>(tt_start),
                         reinterpret_cast<const char*>(tt_end));

    ptrdiff_t tt_len = tt_end - p + 1;
    ptrdiff_t tt_padded = (tt_len + 3) & ~3;
    p += tt_padded;

    // 3. Arguments.
    out.n_int = out.n_float = out.n_u64 = out.n_str = 0;
    for (char c : out.type_tags) {
        if (p + 4 > end && c != 's') return false;
        switch (c) {
        case 'i':
            if (out.n_int < OscArgs::MAX_ARGS)
                out.ints[out.n_int++] = static_cast<int32_t>(readU32(p));
            p += 4;
            break;
        case 'f':
            if (out.n_float < OscArgs::MAX_ARGS)
                out.floats[out.n_float++] = readF32(p);
            p += 4;
            break;
        case 'h': // int64
            if (p + 8 > end) return false;
            p += 8; // skip (not used currently)
            break;
        case 't': // timetag
            if (p + 8 > end) return false;
            if (out.n_u64 < OscArgs::MAX_ARGS)
                out.u64s[out.n_u64++] = readU64(p);
            p += 8;
            break;
        case 's': {
            // String argument.
            const uint8_t* s_end = p;
            while (s_end < end && *s_end != '\0') ++s_end;
            if (s_end >= end) return false;
            if (out.n_str < OscArgs::MAX_ARGS)
                out.strings[out.n_str++].assign(
                    reinterpret_cast<const char*>(p),
                    reinterpret_cast<const char*>(s_end));
            ptrdiff_t slen = (s_end - p + 1 + 3) & ~3;
            p += slen;
            break;
        }
        default:
            return false; // unsupported type — reject
        }
    }
    return true;
}

// ---- buildCommand ----------------------------------------------------------

Command CommandDecoder::buildCommand(const OscArgs& args, uint32_t& reject_count) noexcept {
    Command cmd;
    cmd.schema_version = SCHEMA_VERSION;

    // seq and id are encoded as first two 'i' arguments when present.
    // Encoding convention: for commands that carry seq/id, format is:
    //   /addr ,ii... seq id <payload args>
    // We parse seq/id as args.ints[0] and args.ints[1] when type_tags starts "ii".
    int payload_int_offset = 0;
    if (args.type_tags.size() >= 2 &&
        args.type_tags[0] == 'i' && args.type_tags[1] == 'i' &&
        args.n_int >= 2) {
        cmd.seq = static_cast<uint32_t>(args.ints[0]);
        cmd.id  = static_cast<uint32_t>(args.ints[1]);
        payload_int_offset = 2;
    }

    auto getInt   = [&](int idx) -> int32_t {
        int real = idx + payload_int_offset;
        return (real < args.n_int) ? args.ints[real] : 0;
    };
    auto getFloat = [&](int idx) -> float {
        return (idx < args.n_float) ? args.floats[idx] : 0.f;
    };

    const std::string& addr = args.address;

    if (addr == "/obj/move") {
        cmd.tag = CommandTag::ObjMove;
        PayloadObjMove p;
        p.obj_id = static_cast<uint32_t>(getInt(0));
        p.az_rad = getFloat(0);
        p.el_rad = getFloat(1);
        p.dist_m = getFloat(2);
        cmd.payload = p;
    } else if (addr == "/obj/gain") {
        cmd.tag = CommandTag::ObjGain;
        PayloadObjGain p;
        p.obj_id = static_cast<uint32_t>(getInt(0));
        p.gain   = getFloat(0);
        cmd.payload = p;
    } else if (addr == "/obj/active") {
        cmd.tag = CommandTag::ObjActive;
        PayloadObjActive p;
        p.obj_id = static_cast<uint32_t>(getInt(0));
        p.active = (getInt(1) != 0);
        cmd.payload = p;
    } else if (addr == "/obj/algo") {
        cmd.tag = CommandTag::ObjAlgo;
        PayloadObjAlgo p;
        p.obj_id = static_cast<uint32_t>(getInt(0));
        int algo_i = getInt(1);
        p.algo = (algo_i == 1) ? Algorithm::WFS :
                 (algo_i == 2) ? Algorithm::DBAP : Algorithm::VBAP;
        cmd.payload = p;
    } else if (addr == "/sys/handshake") {
        cmd.tag = CommandTag::SysHandshake;
        PayloadSysHandshake p;
        p.client_schema_version = static_cast<uint16_t>(getInt(0));
        cmd.payload = p;
    } else if (addr == "/sys/algo_swap") {
        cmd.tag = CommandTag::SysAlgoSwap;
        PayloadSysAlgoSwap p;
        int algo_i = getInt(0);
        p.algo = (algo_i == 1) ? Algorithm::WFS :
                 (algo_i == 2) ? Algorithm::DBAP : Algorithm::VBAP;
        cmd.payload = p;
    } else if (addr == "/sys/reset") {
        cmd.tag = CommandTag::SysReset;
        cmd.payload = PayloadSysReset{};
    } else if (addr == "/hb/ping") {
        cmd.tag = CommandTag::HbPing;
        PayloadHbPing p;
        p.timestamp_ms = (args.n_u64 > 0) ? args.u64s[0] : 0;
        cmd.payload = p;
    } else if (addr == "/hb/pong") {
        cmd.tag = CommandTag::HbPong;
        PayloadHbPong p;
        p.timestamp_ms = (args.n_u64 > 0) ? args.u64s[0] : 0;
        cmd.payload = p;
    } else if (addr.size() > 9 && addr.compare(0, 9, "/adm/obj/") == 0) {
        // ADM-OSC Living Standard receive paths.
        // No seq/id prefix — ADM-OSC uses pure float/int args only.
        static constexpr float DEG2RAD  = 3.14159265358979323846f / 180.f;
        static constexpr float MAX_DIST = 20.0f; // metres for normalised dist

        int obj_id = -1;
        char sub[32] = {};
        // Parse "/adm/obj/{n}/{sub}"
        if (std::sscanf(addr.c_str(), "/adm/obj/%d/%31s", &obj_id, sub) == 2 && obj_id >= 0) {
            std::string subpath(sub);
            cmd.seq = 0; // ADM-OSC carries no sequence number
            cmd.id  = 0;

            if (subpath == "azim") {
                cmd.tag = CommandTag::ObjMove;
                PayloadObjMove p;
                p.obj_id  = static_cast<uint32_t>(obj_id);
                p.az_rad  = getFloat(0) * DEG2RAD;
                p.el_rad  = 0.f;
                p.dist_m  = 1.f;
                cmd.payload = p;
            } else if (subpath == "elev") {
                cmd.tag = CommandTag::ObjMove;
                PayloadObjMove p;
                p.obj_id  = static_cast<uint32_t>(obj_id);
                p.az_rad  = 0.f;
                p.el_rad  = getFloat(0) * DEG2RAD;
                p.dist_m  = 1.f;
                cmd.payload = p;
            } else if (subpath == "dist") {
                cmd.tag = CommandTag::ObjMove;
                PayloadObjMove p;
                p.obj_id  = static_cast<uint32_t>(obj_id);
                p.az_rad  = 0.f;
                p.el_rad  = 0.f;
                p.dist_m  = getFloat(0) * MAX_DIST; // normalised 0..1 → metres
                cmd.payload = p;
            } else if (subpath == "aed") {
                // ,fff  azimuth_deg  elevation_deg  distance_normalised
                cmd.tag = CommandTag::ObjMove;
                PayloadObjMove p;
                p.obj_id  = static_cast<uint32_t>(obj_id);
                p.az_rad  = getFloat(0) * DEG2RAD;
                p.el_rad  = getFloat(1) * DEG2RAD;
                p.dist_m  = getFloat(2) * MAX_DIST;
                cmd.payload = p;
            } else if (subpath == "gain") {
                cmd.tag = CommandTag::ObjGain;
                PayloadObjGain p;
                p.obj_id = static_cast<uint32_t>(obj_id);
                p.gain   = getFloat(0);
                cmd.payload = p;
            } else if (subpath == "mute") {
                cmd.tag = CommandTag::ObjMute;
                PayloadObjMute p;
                p.obj_id = static_cast<uint32_t>(obj_id);
                p.muted  = (args.n_int > 0 ? args.ints[0] : 0) != 0;
                cmd.payload = p;
            } else if (subpath == "w") {
                // Width/spread — not implemented in v0, silently ignore.
                // Return Unknown so caller can log if desired.
                ++reject_count;
                cmd.tag = CommandTag::Unknown;
                PayloadUnknown p;
                p.address = addr;
                cmd.payload = p;
            } else {
                ++reject_count;
                cmd.tag = CommandTag::Unknown;
                PayloadUnknown p;
                p.address = addr;
                cmd.payload = p;
            }
        } else {
            ++reject_count;
            cmd.tag = CommandTag::Unknown;
            PayloadUnknown p;
            p.address = addr;
            cmd.payload = p;
        }
    } else {
        ++reject_count;
        cmd.tag = CommandTag::Unknown;
        PayloadUnknown p;
        p.address = addr;
        cmd.payload = p;
    }
    return cmd;
}

// ---- decode ----------------------------------------------------------------

Command CommandDecoder::decode(std::span<const uint8_t> packet) noexcept {
    OscArgs args;
    if (!parseOscPacket(packet, args)) {
        ++reject_count_;
        Command bad;
        bad.tag = CommandTag::Unknown;
        PayloadUnknown pu;
        pu.address = "(malformed)";
        bad.payload = pu;
        return bad;
    }
    return buildCommand(args, reject_count_);
}

// ---- encode helpers --------------------------------------------------------

void CommandDecoder::appendPaddedString(std::vector<uint8_t>& buf, const std::string& s) noexcept {
    for (char c : s) buf.push_back(static_cast<uint8_t>(c));
    buf.push_back(0); // null terminator
    while (buf.size() % 4 != 0) buf.push_back(0);
}

void CommandDecoder::appendInt32(std::vector<uint8_t>& buf, int32_t v) noexcept {
    uint8_t tmp[4]; writeU32(tmp, static_cast<uint32_t>(v));
    buf.insert(buf.end(), tmp, tmp + 4);
}

void CommandDecoder::appendFloat32(std::vector<uint8_t>& buf, float v) noexcept {
    uint32_t u; std::memcpy(&u, &v, 4);
    uint8_t tmp[4]; writeU32(tmp, u);
    buf.insert(buf.end(), tmp, tmp + 4);
}

void CommandDecoder::appendUint64(std::vector<uint8_t>& buf, uint64_t v) noexcept {
    uint8_t tmp[8]; writeU64(tmp, v);
    buf.insert(buf.end(), tmp, tmp + 8);
}

// ---- encode ----------------------------------------------------------------

bool CommandDecoder::encode(const Command& cmd, std::vector<uint8_t>& out) noexcept {
    out.clear();
    std::string addr;
    std::string tags = ",ii"; // seq, id always first
    std::vector<uint8_t> args_buf;

    // Helper lambdas.
    auto add_i = [&](int32_t v) {
        tags += 'i';
        uint8_t tmp[4]; writeU32(tmp, static_cast<uint32_t>(v));
        args_buf.insert(args_buf.end(), tmp, tmp + 4);
    };
    auto add_f = [&](float v) {
        tags += 'f';
        uint32_t u; std::memcpy(&u, &v, 4);
        uint8_t tmp[4]; writeU32(tmp, u);
        args_buf.insert(args_buf.end(), tmp, tmp + 4);
    };
    auto add_t = [&](uint64_t v) {
        tags += 't';
        uint8_t tmp[8]; writeU64(tmp, v);
        args_buf.insert(args_buf.end(), tmp, tmp + 8);
    };

    // seq, id always first.
    {
        uint8_t tmp[4];
        writeU32(tmp, cmd.seq); args_buf.insert(args_buf.end(), tmp, tmp + 4);
        writeU32(tmp, cmd.id);  args_buf.insert(args_buf.end(), tmp, tmp + 4);
    }

    switch (cmd.tag) {
    case CommandTag::ObjMove: {
        addr = "/obj/move";
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        add_i(static_cast<int32_t>(p.obj_id));
        add_f(p.az_rad); add_f(p.el_rad); add_f(p.dist_m);
        break;
    }
    case CommandTag::ObjGain: {
        addr = "/obj/gain";
        auto& p = std::get<PayloadObjGain>(cmd.payload);
        add_i(static_cast<int32_t>(p.obj_id));
        add_f(p.gain);
        break;
    }
    case CommandTag::ObjActive: {
        addr = "/obj/active";
        auto& p = std::get<PayloadObjActive>(cmd.payload);
        add_i(static_cast<int32_t>(p.obj_id));
        add_i(p.active ? 1 : 0);
        break;
    }
    case CommandTag::ObjAlgo: {
        addr = "/obj/algo";
        auto& p = std::get<PayloadObjAlgo>(cmd.payload);
        add_i(static_cast<int32_t>(p.obj_id));
        add_i(static_cast<int32_t>(p.algo));
        break;
    }
    case CommandTag::SysHandshake: {
        addr = "/sys/handshake";
        auto& p = std::get<PayloadSysHandshake>(cmd.payload);
        add_i(static_cast<int32_t>(p.client_schema_version));
        break;
    }
    case CommandTag::SysAlgoSwap: {
        addr = "/sys/algo_swap";
        auto& p = std::get<PayloadSysAlgoSwap>(cmd.payload);
        add_i(static_cast<int32_t>(p.algo));
        break;
    }
    case CommandTag::SysReset: {
        addr = "/sys/reset";
        break;
    }
    case CommandTag::HbPing: {
        addr = "/hb/ping";
        auto& p = std::get<PayloadHbPing>(cmd.payload);
        add_t(p.timestamp_ms);
        break;
    }
    case CommandTag::HbPong: {
        addr = "/hb/pong";
        auto& p = std::get<PayloadHbPong>(cmd.payload);
        add_t(p.timestamp_ms);
        break;
    }
    default:
        return false;
    }

    appendPaddedString(out, addr);
    appendPaddedString(out, tags);
    out.insert(out.end(), args_buf.begin(), args_buf.end());
    return true;
}

} // namespace spe::ipc
