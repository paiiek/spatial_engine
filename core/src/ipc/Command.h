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
    VBAP      = 0,
    WFS       = 1,
    DBAP      = 2,
    Ambisonic = 3,
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
    SysAmbiOrder  = 0x13, // /sys/ambi_order ,i {1|2|3} — Ambisonic decoding order

    // Heartbeat
    HbPing        = 0x20, // /hb/ping       — publisher → subscriber
    HbPong        = 0x21, // /hb/pong       — subscriber → publisher (loopback)

    // Scene (snapshot) system
    SceneSave     = 0x30, // /scene/save    — save current scene by name
    SceneLoad     = 0x31, // /scene/load    — load scene by name
    SceneList     = 0x32, // /scene/list    — list available scenes

    // Noise generator (per-channel array verification)
    NoiseType     = 0x40, // /noise/{ch}/type ,s  white|pink
    NoiseGain     = 0x41, // /noise/{ch}/gain ,f  dB

    // Transport
    TransportPlay = 0x50, // /transport/play
    TransportStop = 0x51, // /transport/stop

    // Reverb engine select
    ReverbSelect  = 0x52, // /reverb/select ,s "fdn"|"ir"

    // Per-output-channel gain trim and limiter threshold
    OutputGain    = 0x53, // /output/{ch}/gain  ,f dB
    OutputLimit   = 0x54, // /output/{ch}/limit ,f dB threshold

    // Per-object DSP parameter (EQ band gain, user delay, HF rolloff, reverb send)
    ObjDsp        = 0x60, // /obj/dsp ,iif obj_id param_id value

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

struct PayloadSysAmbiOrder {
    uint8_t order = 1; // 1, 2, or 3 — clamps if out-of-range
};

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

struct PayloadSceneSave { char name[64] = {}; };
struct PayloadSceneLoad { char name[64] = {}; };
struct PayloadSceneList {};

struct PayloadNoiseType {
    uint32_t channel = 0;
    bool     pink    = false; // false = white, true = pink
};

struct PayloadNoiseGain {
    uint32_t channel = 0;
    float    gain_db = -60.f; // -60 dB ≡ effectively muted
};

struct PayloadTransportPlay {};
struct PayloadTransportStop {};

struct PayloadReverbSelect {
    uint8_t which = 0; // 0 = fdn, 1 = ir
};

// Per-object DSP parameter setter.
//   param 0..3 → EQ band gain in dB (low / lowmid / highmid / high)
//   param 4    → user delay in ms (0..1000)
//   param 5    → distance HF rolloff coefficient k_hf (0..1, 0=no rolloff)
//   param 6    → reverb send level (0..1, linear)
//   param 7    → source width in radians (0..π, 0=point source)
struct PayloadObjDsp {
    enum class Param : uint8_t {
        EqLow      = 0,
        EqLowMid   = 1,
        EqHighMid  = 2,
        EqHigh     = 3,
        DelayMs    = 4,
        KHF        = 5,
        ReverbSend = 6,
        Width      = 7,
    };
    uint32_t obj_id = 0;
    Param    param  = Param::EqLow;
    float    value  = 0.f;
};

struct PayloadOutputGain {
    uint32_t channel = 0;
    float    gain_db = 0.f;
};

struct PayloadOutputLimit {
    uint32_t channel      = 0;
    float    threshold_db = 0.f; // 0 dB = no limiting
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
    PayloadSysAmbiOrder,
    PayloadHbPing,
    PayloadHbPong,
    PayloadSceneSave,
    PayloadSceneLoad,
    PayloadSceneList,
    PayloadNoiseType,
    PayloadNoiseGain,
    PayloadTransportPlay,
    PayloadTransportStop,
    PayloadObjDsp,
    PayloadReverbSelect,
    PayloadOutputGain,
    PayloadOutputLimit,
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
