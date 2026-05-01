// core/src/ipc/Command.h
// Wire-format Command structs for the IPC / OSC layer.
// All OSC address patterns are enumerated as CommandTag.
// JUCE-free: no juce:: dependencies here.

#pragma once
#include <cstdint>
#include <string>
#include <variant>

namespace spe::ipc {

// ---- Schema version ---------------------------------------------------------
static constexpr uint16_t SCHEMA_VERSION = 1;

// ---- Algorithm enum (mirrors plan §C-MA #6) ---------------------------------
enum class Algorithm : uint8_t {
    VBAP = 0,
    WFS  = 1,
    DBAP = 2,
};

// ---- Command tag (one per OSC address pattern) ------------------------------
enum class CommandTag : uint8_t {
    // Object control
    ObjMove    = 0x01,  // /obj/move             — set az/el/dist for one object
    ObjGain    = 0x02,  // /obj/gain             — set per-object gain scalar
    ObjActive  = 0x03,  // /obj/active           — enable / disable object
    ObjAlgo    = 0x04,  // /obj/algo             — select rendering algorithm
    ObjMute    = 0x05,  // /adm/obj/n/mute       — mute/unmute (ADM-OSC)

    // System
    SysHandshake  = 0x10, // /sys/handshake — client sends schema_version
    SysAlgoSwap   = 0x11, // /sys/algo_swap — engine-wide default algo
    SysReset      = 0x12, // /sys/reset     — reset all objects to defaults

    // Heartbeat
    HbPing        = 0x20, // /hb/ping       — publisher → subscriber
    HbPong        = 0x21, // /hb/pong       — subscriber → publisher (loopback)

    // Unknown / malformed placeholder
    Unknown       = 0xFF,
};

// ---- Per-tag payload structs ------------------------------------------------

struct PayloadObjMove {
    uint32_t obj_id   = 0;
    float    az_rad   = 0.f;
    float    el_rad   = 0.f;
    float    dist_m   = 1.f;
};

struct PayloadObjGain {
    uint32_t obj_id = 0;
    float    gain   = 1.f;
};

struct PayloadObjActive {
    uint32_t obj_id = 0;
    bool     active = false;
};

struct PayloadObjAlgo {
    uint32_t  obj_id = 0;
    Algorithm algo   = Algorithm::VBAP;
};

struct PayloadSysHandshake {
    uint16_t client_schema_version = SCHEMA_VERSION;
};

struct PayloadSysAlgoSwap {
    Algorithm algo = Algorithm::VBAP;
};

struct PayloadSysReset {};

struct PayloadHbPing {
    uint64_t timestamp_ms = 0; // wall-clock ms at publisher side
};

struct PayloadHbPong {
    uint64_t timestamp_ms = 0;
};

struct PayloadObjMute {
    uint32_t obj_id = 0;
    bool     muted  = false;
};

struct PayloadUnknown {
    std::string address; // original OSC address for diagnostics
};

// ---- Variant ----------------------------------------------------------------
using CommandPayload = std::variant<
    PayloadObjMove,
    PayloadObjGain,
    PayloadObjActive,
    PayloadObjAlgo,
    PayloadObjMute,
    PayloadSysHandshake,
    PayloadSysAlgoSwap,
    PayloadSysReset,
    PayloadHbPing,
    PayloadHbPong,
    PayloadUnknown
>;

// ---- Top-level Command ------------------------------------------------------
struct Command {
    CommandTag     tag            = CommandTag::Unknown;
    uint16_t       schema_version = SCHEMA_VERSION;
    uint32_t       seq            = 0;  // monotone per-object sequence number
    uint32_t       id             = 0;  // sender-side message ID (globally unique per session)
    CommandPayload payload        = PayloadUnknown{};
};

// ---- Reply tags (engine → client) -------------------------------------------
enum class ReplyTag : uint8_t {
    HandshakeOk      = 0x01, // /sys/handshake_ok
    HandshakeMismatch= 0x02, // /sys/error  (version mismatch)
    StateUpdate      = 0x03, // /sys/state
    Warning          = 0x04, // /sys/warning
    Error            = 0x05, // /sys/error  (generic)
    HeartbeatMiss    = 0x06, // /sys/heartbeat_miss
};

struct Reply {
    ReplyTag    tag     = ReplyTag::Error;
    uint32_t    seq     = 0;
    std::string message; // human-readable detail
};

} // namespace spe::ipc
