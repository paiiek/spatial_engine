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
    ObjMute      = 0x05,  // /adm/obj/n/mute       — mute/unmute (ADM-OSC)

    // ADM-OSC v1.0 extended tags (Phase C3)
    ObjXYZ       = 0x06,  // /adm/obj/n/xyz ,fff   — Cartesian position
    ObjActiveAdm = 0x07,  // /adm/obj/n/active ,i  — active flag (ADM-OSC)
    ObjWidth     = 0x08,  // /adm/obj/n/width ,f   — source width in radians
    ObjName      = 0x09,  // /adm/obj/n/name ,s    — source label

    // System
    SysHandshake  = 0x10, // /sys/handshake — client sends schema_version
    SysAlgoSwap   = 0x11, // /sys/algo_swap — engine-wide default algo
    SysReset      = 0x12, // /sys/reset     — reset all objects to defaults
    SysAmbiOrder       = 0x13, // /sys/ambi_order ,i {1|2|3} — Ambisonic decoding order
    SysLtcChase        = 0x14, // /sys/ltc_chase ,i {0|1} — enable LTC chase from input ch 0
    SysAmbiDecoderType = 0x15, // /sys/ambi_decoder_type ,i {0..4} — decoder algorithm
    // v0.4: runtime layout / binaural path injection (control-thread only).
    SysLoadLayout      = 0x16, // /sys/load_layout ,s "<yaml-path>" — store layout YAML path
    SysBinauralSofa    = 0x17, // /sys/binaural_sofa ,s "<.speh-path>" — store binaural SOFA path
    SysBinauralEnable  = 0x18, // /sys/binaural_enable ,i {0|1} — enable binaural bus 1 rendering
    SysBinauralMode    = 0x19, // /sys/binaural_mode ,i {0|1} — 0 = B1 Direct, 1 = B2 AmbiVS (v0.5 P4)

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
    // v0.5.1 Q1 (WM-2): optional client reply port. When 0 (default), the
    // engine replies to the sender's source UDP port (captured via
    // recvfrom()). Additive — old clients ship reply_port=0 and still
    // receive replies via the source-port fallback.
    uint16_t reply_port = 0;
};

struct PayloadSysAlgoSwap {
    Algorithm algo = Algorithm::VBAP;
};

struct PayloadSysReset {};

struct PayloadSysAmbiOrder {
    uint8_t order = 1; // 1, 2, or 3 — clamps if out-of-range
};

struct PayloadSysLtcChase {
    bool enable = false; // true → audio-thread feeds input ch 0 → LtcChase ring
};

// HOA decoder algorithm selector (M2 sprint).
// 0=PINV,1=MAX_RE,2=ALLRAD,3=EPAD,4=IN_PHASE — clamps if out-of-range.
struct PayloadSysAmbiDecoderType {
    uint8_t type = 0;
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

// ADM-OSC Phase C3 extended payloads

struct PayloadObjXYZ {
    uint32_t obj_id = 0;
    float    x      = 0.f;
    float    y      = 0.f;
    float    z      = 0.f;
};

struct PayloadObjActiveAdm {
    uint32_t obj_id = 0;
    bool     active = false;
};

struct PayloadObjWidth {
    uint32_t obj_id    = 0;
    float    width_rad = 0.f;
};

struct PayloadObjName {
    uint32_t obj_id    = 0;
    char     name[32]  = {};
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

// v0.4 — runtime path injection. Strings are control-thread only; never
// touched on the audio thread (see SpatialEngine OSC handler dispatch).
struct PayloadSysLoadLayout {
    std::string path;
};

struct PayloadSysBinauralSofa {
    std::string path;
};

struct PayloadSysBinauralEnable {
    bool enable = false;
};

struct PayloadSysBinauralMode {
    // 0 = B1 Direct (per-object HRTF), 1 = B2 AmbiVS (24-pt t-design VS).
    // Unknown values are clamped to 0 by the engine handler.
    uint8_t mode = 0;
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
    PayloadObjXYZ,
    PayloadObjActiveAdm,
    PayloadObjWidth,
    PayloadObjName,
    PayloadSysHandshake,
    PayloadSysAlgoSwap,
    PayloadSysReset,
    PayloadSysAmbiOrder,
    PayloadSysLtcChase,
    PayloadSysAmbiDecoderType,
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
    PayloadSysLoadLayout,
    PayloadSysBinauralSofa,
    PayloadSysBinauralEnable,
    PayloadSysBinauralMode,
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
    // v0.5.1 Q1 — typed engine→client telemetry channels.
    BinauralWarning  = 0x07, // /sys/binaural_warning  ,s | ,sf
    BinauralStatus   = 0x08, // /sys/binaural_status   ,i  (1 Hz heartbeat)
};

struct Reply {
    ReplyTag    tag     = ReplyTag::Error;
    uint32_t    seq     = 0;
    std::string message; // human-readable detail (existing Warning/Error users)
    // v0.5.1 Q1 — optional typed payload for BinauralWarning (warning code
    // string + optional throughput) and BinauralStatus (int counter). The
    // existing message-based Warning/Error path is untouched; consumers that
    // do not read these fields are unaffected.
    std::string code;            // e.g. "ambivs_disabled_cpu", "no_sofa_loaded"
    float       float_payload = 0.f;
    int32_t     int_payload   = 0;
};

} // namespace spe::ipc
