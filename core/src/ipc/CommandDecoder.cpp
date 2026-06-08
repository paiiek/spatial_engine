// core/src/ipc/CommandDecoder.cpp
// Minimal OSC 1.1 subset decoder/encoder. No JUCE dependency.

#include "ipc/CommandDecoder.h"
#include "ipc/AdmOscConstants.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// Verify encoder and decoder share the same MAX_DIST constant.
static_assert(spe::ipc::ADM_OSC_MAX_DIST == 20.0f,
              "ADM_OSC_MAX_DIST encoder/decoder identity check failed");

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

static inline double readF64(const uint8_t* p) noexcept {
    uint64_t u = readU64(p);
    double d; std::memcpy(&d, &u, 8);
    return d;
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
    out.n_int = out.n_float = out.n_u64 = out.n_str = out.n_double = 0;
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
            if (out.n_u64 < OscArgs::MAX_ARGS)
                out.u64s[out.n_u64++] = readU64(p);
            p += 8;
            break;
        case 'd': // float64
            if (p + 8 > end) return false;
            if (out.n_double < OscArgs::MAX_ARGS)
                out.doubles[out.n_double++] = readF64(p);
            p += 8;
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

// Map an integer algorithm token (0/1/2) to the Algorithm enum.
// VBAP is the default for unknown values, matching legacy decoder behaviour.
static inline Algorithm algoFromInt(int v) noexcept {
    switch (v) {
        case 1: return Algorithm::WFS;
        case 2: return Algorithm::DBAP;
        case 3: return Algorithm::Ambisonic;
        default: return Algorithm::VBAP;
    }
}

// Copy an OSC string into a fixed 64-byte name buffer (truncates to 63 + null).
static inline void copySceneName(char (&dst)[64], const std::string& src) noexcept {
    const std::size_t n = src.size() < 63 ? src.size() : 63;
    std::memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

// M5.1: copy a subscriber tag string into a fixed 64-byte buffer.
// Non-printable characters are rejected (tag set to empty string on detection).
static inline void copySubscriberTag(char (&dst)[64], const std::string& src) noexcept {
    const std::size_t n = src.size() < 63 ? src.size() : 63;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c < 0x20 || c > 0x7E) {
            // Non-printable character found — reject the entire tag.
            dst[0] = '\0';
            return;
        }
    }
    std::memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

// ---- decodeAdmAddress ------------------------------------------------------
// Handles all /adm/obj/{N}/{sub} patterns for ADM-OSC v1.0.
// Extracted from buildCommand to keep that function readable (A-β decision).
// INVARIANT: All ADM commands MUST use obj_cache_ path in SpatialEngine —
//            NEVER StateModel::apply() (seq=0 would be silently dropped).

static void decodeAdmAddress(const std::string& addr, const OscArgs& args,
                             Command& cmd, uint32_t& reject_count) noexcept {
    static constexpr float DEG2RAD  = 3.14159265358979323846f / 180.f;
    // Use the shared constant (static_assert above verifies identity with encoder).
    static constexpr float MAX_DIST = spe::ipc::ADM_OSC_MAX_DIST;

    int obj_id = -1;
    char sub[32] = {};
    if (std::sscanf(addr.c_str(), "/adm/obj/%d/%31s", &obj_id, sub) != 2 || obj_id < 0) {
        ++reject_count;
        cmd.tag = CommandTag::Unknown;
        PayloadUnknown pu; pu.address = addr;
        cmd.payload = pu;
        return;
    }

    const auto oid = static_cast<uint32_t>(obj_id);
    cmd.seq = 0; // ADM-OSC carries no sequence number (ADR 0006)
    cmd.id  = 0;

    const std::string subpath(sub);

    // Helper: build ObjMove payload from spherical components.
    auto setMove = [&](float az, float el, float dist) {
        cmd.tag = CommandTag::ObjMove;
        PayloadObjMove p;
        p.obj_id = oid;
        p.az_rad = az;
        p.el_rad = el;
        p.dist_m = dist;
        cmd.payload = p;
    };

    if (subpath == "azim") {
        setMove(args.n_float > 0 ? args.floats[0] * DEG2RAD : 0.f, 0.f, 1.f);
    } else if (subpath == "elev") {
        setMove(0.f, args.n_float > 0 ? args.floats[0] * DEG2RAD : 0.f, 1.f);
    } else if (subpath == "dist") {
        // normalised [0..1] → metres via MAX_DIST
        setMove(0.f, 0.f, (args.n_float > 0 ? args.floats[0] : 0.f) * MAX_DIST);
    } else if (subpath == "aed") {
        // ,fff  azimuth_deg  elevation_deg  distance_normalised
        setMove((args.n_float > 0 ? args.floats[0] : 0.f) * DEG2RAD,
                (args.n_float > 1 ? args.floats[1] : 0.f) * DEG2RAD,
                (args.n_float > 2 ? args.floats[2] : 0.f) * MAX_DIST);
    } else if (subpath == "gain") {
        cmd.tag = CommandTag::ObjGain;
        PayloadObjGain p;
        p.obj_id = oid;
        p.gain   = args.n_float > 0 ? args.floats[0] : 0.f;
        cmd.payload = p;
    } else if (subpath == "mute") {
        cmd.tag = CommandTag::ObjMute;
        PayloadObjMute p;
        p.obj_id = oid;
        p.muted  = (args.n_int > 0 ? args.ints[0] : 0) != 0;
        cmd.payload = p;
    } else if (subpath == "xyz") {
        // Cartesian position: ,fff x y z
        cmd.tag = CommandTag::ObjXYZ;
        PayloadObjXYZ p;
        p.obj_id = oid;
        p.x = args.n_float > 0 ? args.floats[0] : 0.f;
        p.y = args.n_float > 1 ? args.floats[1] : 0.f;
        p.z = args.n_float > 2 ? args.floats[2] : 0.f;
        cmd.payload = p;
    } else if (subpath == "active") {
        // ,i 0|1 — ADM-OSC active flag (separate from legacy /obj/active)
        cmd.tag = CommandTag::ObjActiveAdm;
        PayloadObjActiveAdm p;
        p.obj_id = oid;
        p.active = (args.n_int > 0 ? args.ints[0] : 0) != 0;
        cmd.payload = p;
    } else if (subpath == "width") {
        // ,f radians — source width
        cmd.tag = CommandTag::ObjWidth;
        PayloadObjWidth p;
        p.obj_id    = oid;
        p.width_rad = args.n_float > 0 ? args.floats[0] : 0.f;
        cmd.payload = p;
    } else if (subpath == "name") {
        // ,s — source label (truncated to 31 chars + null)
        cmd.tag = CommandTag::ObjName;
        PayloadObjName p;
        p.obj_id = oid;
        if (args.n_str > 0) {
            const std::size_t n = args.strings[0].size() < 31u ? args.strings[0].size() : 31u;
            std::memcpy(p.name, args.strings[0].c_str(), n);
            p.name[n] = '\0';
        }
        cmd.payload = p;
    } else {
        // Unknown/unsupported ADM-OSC sub-address (e.g. /w, /h, /d, /divergence)
        ++reject_count;
        cmd.tag = CommandTag::Unknown;
        PayloadUnknown pu; pu.address = addr;
        cmd.payload = pu;
    }
}

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

    // Build an Unknown reply for malformed/unsupported addresses.
    auto makeUnknown = [&]() {
        ++reject_count;
        cmd.tag = CommandTag::Unknown;
        PayloadUnknown pu;
        pu.address = addr;
        cmd.payload = pu;
    };

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
        p.algo   = algoFromInt(getInt(1));
        cmd.payload = p;
    } else if (addr == "/sys/handshake") {
        cmd.tag = CommandTag::SysHandshake;
        PayloadSysHandshake p;
        p.client_schema_version = static_cast<uint16_t>(getInt(0));
        // v0.5.1 Q1 (WM-2) — optional reply_port int. When absent (legacy
        // clients) getInt() returns 0, which the engine interprets as
        // "reply to the sender's source port" via the recvfrom()-captured
        // endpoint. When present and valid (>0), the engine targets that
        // explicit port on the same host.
        const int32_t rp = getInt(1);
        if (rp > 0 && rp <= 65535) {
            p.reply_port = static_cast<uint16_t>(rp);
        }
        // M5.1 — optional subscriber tag string (,iis variant).
        if (args.n_str > 0) {
            copySubscriberTag(p.subscriber_tag, args.strings[0]);
        } else {
            p.subscriber_tag[0] = '\0';
        }
        cmd.payload = p;
    } else if (addr == "/sys/algo_swap") {
        cmd.tag = CommandTag::SysAlgoSwap;
        PayloadSysAlgoSwap p;
        p.algo = algoFromInt(getInt(0));
        cmd.payload = p;
    } else if (addr == "/sys/reset") {
        cmd.tag = CommandTag::SysReset;
        cmd.payload = PayloadSysReset{};
    } else if (addr == "/sys/ambi_order") {
        cmd.tag = CommandTag::SysAmbiOrder;
        PayloadSysAmbiOrder p;
        int v = getInt(0);
        if (v < 1) v = 1;
        if (v > 3) v = 3;
        p.order = static_cast<uint8_t>(v);
        cmd.payload = p;
    } else if (addr == "/sys/ambi_decoder_type") {
        // HOA decoder type: 0=PINV,1=MAX_RE,2=ALLRAD,3=EPAD,4=IN_PHASE.
        // Out-of-range clamps to PINV (mirrors /sys/ambi_order clamp pattern, AC-S4.2).
        cmd.tag = CommandTag::SysAmbiDecoderType;
        PayloadSysAmbiDecoderType p;
        int v = getInt(0);
        if (v < 0 || v > 4) v = 0;
        p.type = static_cast<uint8_t>(v);
        cmd.payload = p;
    } else if (addr == "/sys/ltc_chase") {
        cmd.tag = CommandTag::SysLtcChase;
        PayloadSysLtcChase p;
        p.enable = (getInt(0) != 0);
        cmd.payload = p;
    } else if (addr == "/sys/load_layout") {
        // v0.4: ,s "<path>" — control-thread layout YAML injection
        if (args.n_str > 0) {
            cmd.tag = CommandTag::SysLoadLayout;
            PayloadSysLoadLayout p;
            p.path = args.strings[0];
            cmd.payload = p;
        } else {
            makeUnknown();
        }
    } else if (addr == "/sys/binaural_sofa") {
        // v0.4: ,s "<.speh path>" — control-thread sofa path injection
        if (args.n_str > 0) {
            cmd.tag = CommandTag::SysBinauralSofa;
            PayloadSysBinauralSofa p;
            p.path = args.strings[0];
            cmd.payload = p;
        } else {
            makeUnknown();
        }
    } else if (addr == "/sys/binaural_sofa_select") {
        // B-M3: ,s "<catalog-name>" — live SOFA swap via catalog name lookup.
        // Empty string is rejected (no crash, just Unknown).
        if (args.n_str > 0 && !args.strings[0].empty()) {
            cmd.tag = CommandTag::SysBinauralSofaSelect;
            PayloadSysBinauralSofaSelect p;
            p.name = args.strings[0];
            cmd.payload = p;
        } else {
            makeUnknown();
        }
    } else if (addr == "/sys/binaural_enable") {
        // v0.4: ,i {0|1} — toggle binaural bus 1 rendering
        cmd.tag = CommandTag::SysBinauralEnable;
        PayloadSysBinauralEnable p;
        p.enable = (getInt(0) != 0);
        cmd.payload = p;
    } else if (addr == "/sys/binaural_mode") {
        // v0.5 P4: ,i {0=B1 Direct | 1=B2 AmbiVS}
        cmd.tag = CommandTag::SysBinauralMode;
        PayloadSysBinauralMode p;
        const int32_t v = getInt(0);
        p.mode = (v == 1) ? 1u : 0u;
        cmd.payload = p;
    } else if (addr == "/sys/binaural_reset_demote") {
        // v0.7 D-S1: ,i {0|1} — user-controlled runtime demote reset hatch.
        // Peer-validation is enforced upstream by OSCBackend (same path as all
        // other /sys/ verbs); osc_security_peer_validation ctest covers this.
        cmd.tag = CommandTag::SysBinauralResetDemote;
        PayloadSysBinauralResetDemote p;
        p.enable = (getInt(0) != 0);
        cmd.payload = p;
    } else if (addr == "/sys/state_request") {
        // C6: ,i token — client asks for a full-state resync. A bare ,i token
        // (no seq/id prefix) decodes token as ints[0]; a no-arg variant → 0.
        cmd.tag = CommandTag::SysStateRequest;
        PayloadSysStateRequest p;
        p.token = static_cast<uint32_t>(getInt(0));
        cmd.payload = p;
    } else if (addr == "/hb/ping") {
        cmd.tag = CommandTag::HbPing;
        PayloadHbPing p;
        if (args.n_u64 > 0) {
            // ,h or ,t path (engine-internal HeartbeatPublisher — value already in ms)
            p.timestamp_ms  = args.u64s[0];
            p.from_external = false;
        } else if (args.n_double > 0) {
            // ,d seconds → ms (Phase B, adm_player M3 path); clamp negatives to 0.
            // ADR 0018 D-5: the `,d` tag identifies the EXTERNAL player; tick
            // last_player_ping_unix_ms_ on the control thread for staleness.
            const double s = args.doubles[0];
            p.timestamp_ms  = (s > 0.0) ? static_cast<uint64_t>(s * 1000.0) : 0;
            p.from_external = true;
        } else {
            p.timestamp_ms  = 0;
            p.from_external = false;
        }
        cmd.payload = p;
    } else if (addr == "/hb/pong") {
        cmd.tag = CommandTag::HbPong;
        PayloadHbPong p;
        p.timestamp_ms = (args.n_u64 > 0) ? args.u64s[0] : 0;
        cmd.payload = p;
    } else if (addr == "/scene/save") {
        cmd.tag = CommandTag::SceneSave;
        PayloadSceneSave p;
        copySceneName(p.name, (args.n_str > 0) ? args.strings[0] : std::string{});
        cmd.payload = p;
    } else if (addr == "/scene/load") {
        cmd.tag = CommandTag::SceneLoad;
        PayloadSceneLoad p;
        copySceneName(p.name, (args.n_str > 0) ? args.strings[0] : std::string{});
        cmd.payload = p;
    } else if (addr == "/scene/list") {
        cmd.tag = CommandTag::SceneList;
        cmd.payload = PayloadSceneList{};
    } else if (addr == "/scene/rename") {
        cmd.tag = CommandTag::SceneRename;
        PayloadSceneRename p;
        copySceneName(p.from, (args.n_str > 0) ? args.strings[0] : std::string{});
        copySceneName(p.to,   (args.n_str > 1) ? args.strings[1] : std::string{});
        cmd.payload = p;
    } else if (addr == "/scene/duplicate") {
        cmd.tag = CommandTag::SceneDuplicate;
        PayloadSceneDuplicate p;
        copySceneName(p.from, (args.n_str > 0) ? args.strings[0] : std::string{});
        copySceneName(p.to,   (args.n_str > 1) ? args.strings[1] : std::string{});
        cmd.payload = p;
    } else if (addr == "/scene/delete") {
        cmd.tag = CommandTag::SceneDelete;
        PayloadSceneDelete p;
        copySceneName(p.name, (args.n_str > 0) ? args.strings[0] : std::string{});
        cmd.payload = p;
    } else if (addr == "/scene/meta") {
        cmd.tag = CommandTag::SceneMeta;
        PayloadSceneMeta p;
        copySceneName(p.name, (args.n_str > 0) ? args.strings[0] : std::string{});
        p.meta_json = (args.n_str > 1) ? args.strings[1] : std::string{};
        cmd.payload = p;
    } else if (addr == "/cue/go") {
        cmd.tag = CommandTag::CueGo;
        PayloadCueGo p;
        p.index = getInt(0);
        cmd.payload = p;
    } else if (addr == "/cue/next") {
        cmd.tag = CommandTag::CueNext;
        cmd.payload = PayloadCueNext{};
    } else if (addr == "/cue/prev") {
        cmd.tag = CommandTag::CuePrev;
        cmd.payload = PayloadCuePrev{};
    } else if (addr == "/cue/stop") {
        cmd.tag = CommandTag::CueStop;
        cmd.payload = PayloadCueStop{};
    } else if (addr == "/transport/play") {
        // ADR 0018 D-2 — edge-triggered. Gate flips immediately downstream;
        // the optional `,d unix_time_seconds` timetag is advisory only (no
        // scheduler). Requires D-1's parser fix to populate args.doubles.
        cmd.tag = CommandTag::TransportPlay;
        PayloadTransportPlay p;
        if (args.n_double > 0) {
            p.start_unix_seconds = args.doubles[0];
        }
        cmd.payload = p;
    } else if (addr == "/transport/stop") {
        cmd.tag = CommandTag::TransportStop;
        cmd.payload = PayloadTransportStop{};
    } else if (addr == "/transport/pause") {
        // ADR 0018 D-3 — alias of /transport/stop at decode time. The engine
        // gate is binary (no distinct pause state); the player owns its own
        // playhead. Intentionally NOT a new CommandTag.
        cmd.tag = CommandTag::TransportStop;
        cmd.payload = PayloadTransportStop{};
    } else if (addr == "/reverb/select") {
        if (args.n_str > 0) {
            cmd.tag = CommandTag::ReverbSelect;
            PayloadReverbSelect p;
            p.which = (args.strings[0] == "ir") ? 1u : 0u;
            cmd.payload = p;
        } else {
            makeUnknown();
        }
    } else if (addr == "/obj/dsp") {
        const int param_int = getInt(1);
        // F4b-T0: accept the full valid param range 0..7 (incl. 7 = Width).
        // An out-of-range param must NOT silently default-route to Param::EqLow
        // (band 0) — reject it so the caller sees Unknown.
        if (param_int >= 0 && param_int <= 7) {
            cmd.tag = CommandTag::ObjDsp;
            PayloadObjDsp p;
            p.obj_id = static_cast<uint32_t>(getInt(0));
            p.param  = static_cast<PayloadObjDsp::Param>(param_int);
            p.value  = getFloat(0);
            cmd.payload = p;
        } else {
            makeUnknown();
        }
    } else if (addr.size() > 7 && addr.compare(0, 7, "/noise/") == 0) {
        // Noise generator: /noise/{ch}/{type|gain}
        int ch = -1;
        char sub[16] = {};
        if (std::sscanf(addr.c_str(), "/noise/%d/%15s", &ch, sub) == 2 && ch >= 0) {
            const std::string subpath(sub);
            const auto chu = static_cast<uint32_t>(ch);
            if (subpath == "type" && args.n_str > 0) {
                PayloadNoiseType p;
                p.channel = chu;
                p.pink    = (args.strings[0] == "pink");
                cmd.tag     = CommandTag::NoiseType;
                cmd.payload = p;
            } else if (subpath == "gain") {
                PayloadNoiseGain p;
                p.channel = chu;
                p.gain_db = getFloat(0);
                cmd.tag     = CommandTag::NoiseGain;
                cmd.payload = p;
            } else {
                makeUnknown();
            }
        } else {
            makeUnknown();
        }
    } else if (addr.size() > 8 && addr.compare(0, 8, "/output/") == 0) {
        int ch = -1; char sub[16] = {};
        if (std::sscanf(addr.c_str(), "/output/%d/%15s", &ch, sub) == 2 && ch >= 0) {
            if (std::string(sub) == "gain") {
                cmd.tag = CommandTag::OutputGain;
                PayloadOutputGain p; p.channel = static_cast<uint32_t>(ch); p.gain_db = getFloat(0);
                cmd.payload = p;
            } else if (std::string(sub) == "limit") {
                cmd.tag = CommandTag::OutputLimit;
                PayloadOutputLimit p; p.channel = static_cast<uint32_t>(ch); p.threshold_db = getFloat(0);
                cmd.payload = p;
            } else { makeUnknown(); }
        } else { makeUnknown(); }
    } else if (addr.size() > 9 && addr.compare(0, 9, "/adm/obj/") == 0) {
        // ADM-OSC Living Standard receive paths (Phase C3 full v1.0 subset).
        // All new tags route through obj_cache_ — NEVER via StateModel::apply().
        // (See ADR 0006: StateModel seq-drop would kill ADM traffic at seq=0.)
        decodeAdmAddress(addr, args, cmd, reject_count);
    } else {
        makeUnknown();
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

bool CommandDecoder::encode(const Command& cmd, std::vector<uint8_t>& out,
                            WireDialect dialect) noexcept {
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

    // ---- ADM-V1 dialect: encode ObjMove/ObjGain/ObjMute as /adm/obj/N/... ----
    if (dialect == WireDialect::AdmV1) {
        // For ADM dialect, use pure float args (no seq/id prefix on wire).
        // We still have seq/id in args_buf from the header above; discard and restart.
        out.clear();
        args_buf.clear();
        tags = ","; // reset — ADM has no seq/id prefix

        static constexpr float RAD2DEG = 180.f / 3.14159265358979323846f;
        // Same constant as decoder — identity enforced by static_assert at top of file.
        static constexpr float MAX_DIST_ADM = spe::ipc::ADM_OSC_MAX_DIST;

        auto add_f_adm = [&](float v) {
            tags += 'f';
            uint32_t u; std::memcpy(&u, &v, 4);
            uint8_t tmp[4]; writeU32(tmp, u);
            args_buf.insert(args_buf.end(), tmp, tmp + 4);
        };
        auto add_i_adm = [&](int32_t v) {
            tags += 'i';
            uint8_t tmp[4]; writeU32(tmp, static_cast<uint32_t>(v));
            args_buf.insert(args_buf.end(), tmp, tmp + 4);
        };

        switch (cmd.tag) {
        case CommandTag::ObjMove: {
            auto& p = std::get<PayloadObjMove>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/aed";
            // ADM-OSC: az degrees, el degrees, dist normalised [0..1]
            add_f_adm(p.az_rad * RAD2DEG);
            add_f_adm(p.el_rad * RAD2DEG);
            add_f_adm(MAX_DIST_ADM > 0.f ? p.dist_m / MAX_DIST_ADM : 0.f);
            break;
        }
        case CommandTag::ObjGain: {
            auto& p = std::get<PayloadObjGain>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/gain";
            add_f_adm(p.gain);
            break;
        }
        case CommandTag::ObjMute: {
            auto& p = std::get<PayloadObjMute>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/mute";
            add_i_adm(p.muted ? 1 : 0);
            break;
        }
        case CommandTag::ObjXYZ: {
            auto& p = std::get<PayloadObjXYZ>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/xyz";
            add_f_adm(p.x); add_f_adm(p.y); add_f_adm(p.z);
            break;
        }
        case CommandTag::ObjActiveAdm: {
            auto& p = std::get<PayloadObjActiveAdm>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/active";
            add_i_adm(p.active ? 1 : 0);
            break;
        }
        case CommandTag::ObjWidth: {
            auto& p = std::get<PayloadObjWidth>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/width";
            add_f_adm(p.width_rad);
            break;
        }
        case CommandTag::ObjName: {
            auto& p = std::get<PayloadObjName>(cmd.payload);
            addr = "/adm/obj/" + std::to_string(p.obj_id) + "/name";
            tags += 's';
            for (const char* q = p.name; *q; ++q)
                args_buf.push_back(static_cast<uint8_t>(*q));
            args_buf.push_back(0);
            while (args_buf.size() % 4 != 0) args_buf.push_back(0);
            break;
        }
        default:
            return false; // tag not encodable in AdmV1 dialect
        }

        appendPaddedString(out, addr);
        appendPaddedString(out, tags);
        out.insert(out.end(), args_buf.begin(), args_buf.end());
        return true;
    }

    // ---- Legacy dialect (default) -------------------------------------------
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
        // v0.5.1 Q1 (WM-2) — additive reply_port int. Only emit when set
        // so legacy single-int encoders/decoders stay byte-identical for
        // the common (reply_port==0) case.
        if (p.reply_port != 0) {
            add_i(static_cast<int32_t>(p.reply_port));
        }
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
    case CommandTag::SysAmbiOrder: {
        addr = "/sys/ambi_order";
        auto& p = std::get<PayloadSysAmbiOrder>(cmd.payload);
        add_i(static_cast<int32_t>(p.order));
        break;
    }
    case CommandTag::SysAmbiDecoderType: {
        addr = "/sys/ambi_decoder_type";
        auto& p = std::get<PayloadSysAmbiDecoderType>(cmd.payload);
        add_i(static_cast<int32_t>(p.type));
        break;
    }
    case CommandTag::SysLtcChase: {
        addr = "/sys/ltc_chase";
        auto& p = std::get<PayloadSysLtcChase>(cmd.payload);
        add_i(p.enable ? 1 : 0);
        break;
    }
    case CommandTag::SysLoadLayout: {
        // v0.4: ,s "<yaml-path>" (control-thread only)
        auto& p = std::get<PayloadSysLoadLayout>(cmd.payload);
        addr = "/sys/load_layout";
        tags += 's';
        for (char c : p.path) args_buf.push_back(static_cast<uint8_t>(c));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SysBinauralSofa: {
        // v0.4: ,s "<.speh-path>" (control-thread only)
        auto& p = std::get<PayloadSysBinauralSofa>(cmd.payload);
        addr = "/sys/binaural_sofa";
        tags += 's';
        for (char c : p.path) args_buf.push_back(static_cast<uint8_t>(c));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SysBinauralSofaSelect: {
        // B-M3: ,s "<catalog-name>" (control-thread only)
        auto& p = std::get<PayloadSysBinauralSofaSelect>(cmd.payload);
        addr = "/sys/binaural_sofa_select";
        tags += 's';
        for (char c : p.name) args_buf.push_back(static_cast<uint8_t>(c));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SysBinauralEnable: {
        // v0.4: ,i {0|1}
        auto& p = std::get<PayloadSysBinauralEnable>(cmd.payload);
        addr = "/sys/binaural_enable";
        add_i(p.enable ? 1 : 0);
        break;
    }
    case CommandTag::SysBinauralMode: {
        // v0.5 P4: ,i {0|1}
        auto& p = std::get<PayloadSysBinauralMode>(cmd.payload);
        addr = "/sys/binaural_mode";
        add_i(p.mode ? 1 : 0);
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
    case CommandTag::SceneSave:
    case CommandTag::SceneLoad: {
        // Both carry a single null-terminated scene name as an OSC string arg.
        const bool is_save = (cmd.tag == CommandTag::SceneSave);
        const char* nm = is_save
            ? std::get<PayloadSceneSave>(cmd.payload).name
            : std::get<PayloadSceneLoad>(cmd.payload).name;
        addr = is_save ? "/scene/save" : "/scene/load";
        tags += 's';
        for (const char* p = nm; *p; ++p) args_buf.push_back(static_cast<uint8_t>(*p));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SceneList: {
        addr = "/scene/list";
        break;
    }
    case CommandTag::SceneRename:
    case CommandTag::SceneDuplicate: {
        // Both carry two null-terminated scene names (from, to) as ,ss.
        const bool is_rename = (cmd.tag == CommandTag::SceneRename);
        const char* from_nm = is_rename
            ? std::get<PayloadSceneRename>(cmd.payload).from
            : std::get<PayloadSceneDuplicate>(cmd.payload).from;
        const char* to_nm = is_rename
            ? std::get<PayloadSceneRename>(cmd.payload).to
            : std::get<PayloadSceneDuplicate>(cmd.payload).to;
        addr = is_rename ? "/scene/rename" : "/scene/duplicate";
        tags += 's';
        for (const char* p = from_nm; *p; ++p) args_buf.push_back(static_cast<uint8_t>(*p));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        tags += 's';
        for (const char* p = to_nm; *p; ++p) args_buf.push_back(static_cast<uint8_t>(*p));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SceneDelete: {
        auto& p = std::get<PayloadSceneDelete>(cmd.payload);
        addr = "/scene/delete";
        tags += 's';
        for (const char* q = p.name; *q; ++q) args_buf.push_back(static_cast<uint8_t>(*q));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::SceneMeta: {
        auto& p = std::get<PayloadSceneMeta>(cmd.payload);
        addr = "/scene/meta";
        tags += 's';
        for (const char* q = p.name; *q; ++q) args_buf.push_back(static_cast<uint8_t>(*q));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        tags += 's';
        for (char c : p.meta_json) args_buf.push_back(static_cast<uint8_t>(c));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::CueGo: {
        // E-M3: /cue/go ,i <index>
        auto& p = std::get<PayloadCueGo>(cmd.payload);
        addr = "/cue/go";
        add_i(p.index);
        break;
    }
    case CommandTag::CueNext: {
        addr = "/cue/next";
        break;
    }
    case CommandTag::CuePrev: {
        addr = "/cue/prev";
        break;
    }
    case CommandTag::CueStop: {
        addr = "/cue/stop";
        break;
    }
    case CommandTag::NoiseType: {
        auto& p = std::get<PayloadNoiseType>(cmd.payload);
        addr = "/noise/" + std::to_string(p.channel) + "/type";
        const char* nm = p.pink ? "pink" : "white";
        tags += 's';
        for (const char* q = nm; *q; ++q) args_buf.push_back(static_cast<uint8_t>(*q));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::NoiseGain: {
        auto& p = std::get<PayloadNoiseGain>(cmd.payload);
        addr = "/noise/" + std::to_string(p.channel) + "/gain";
        add_f(p.gain_db);
        break;
    }
    case CommandTag::TransportPlay: {
        addr = "/transport/play";
        break;
    }
    case CommandTag::TransportStop: {
        addr = "/transport/stop";
        break;
    }
    case CommandTag::ReverbSelect: {
        auto& p = std::get<PayloadReverbSelect>(cmd.payload);
        addr = "/reverb/select";
        tags += 's';
        const char* nm = (p.which == 1) ? "ir" : "fdn";
        for (const char* q = nm; *q; ++q) args_buf.push_back(static_cast<uint8_t>(*q));
        args_buf.push_back(0);
        while (args_buf.size() % 4 != 0) args_buf.push_back(0);
        break;
    }
    case CommandTag::ObjDsp: {
        auto& p = std::get<PayloadObjDsp>(cmd.payload);
        addr = "/obj/dsp";
        add_i(static_cast<int32_t>(p.obj_id));
        add_i(static_cast<int32_t>(p.param));
        add_f(p.value);
        break;
    }
    case CommandTag::OutputGain: {
        auto& p = std::get<PayloadOutputGain>(cmd.payload);
        addr = "/output/" + std::to_string(p.channel) + "/gain";
        add_f(p.gain_db);
        break;
    }
    case CommandTag::OutputLimit: {
        auto& p = std::get<PayloadOutputLimit>(cmd.payload);
        addr = "/output/" + std::to_string(p.channel) + "/limit";
        add_f(p.threshold_db);
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
