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
    VAP       = 4,  // Volumetric Amplitude Panning (Dreamscape convergence)
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
    SysBinauralResetDemote = 0x1A, // /sys/binaural_reset_demote ,i {0|1} — v0.7 D-S1 user hatch
    SysBinauralSofaSelect  = 0x1B, // /sys/binaural_sofa_select ,s "<name>" — B-M3 catalog name → live SOFA swap

    // Heartbeat
    HbPing        = 0x20, // /hb/ping       — publisher → subscriber
    HbPong        = 0x21, // /hb/pong       — subscriber → publisher (loopback)

    // Scene (snapshot) system
    SceneSave     = 0x30, // /scene/save    — save current scene by name
    SceneLoad     = 0x31, // /scene/load    — load scene by name
    SceneList     = 0x32, // /scene/list    — list available scenes
    // v0.9 Lane E (E-M1): scene library management ops.
    SceneRename    = 0x33, // /scene/rename ,ss    — rename scene from→to
    SceneDuplicate = 0x34, // /scene/duplicate ,ss — duplicate scene from→to
    SceneDelete    = 0x35, // /scene/delete ,s     — delete scene by name
    SceneMeta      = 0x36, // /scene/meta ,ss      — set scene meta (name, json)
    // v0.9 Lane E (E-M3): cue (snapshot) automation transport.
    CueGo          = 0x37, // /cue/go ,i           — fire cue by index
    CueNext        = 0x38, // /cue/next            — advance to next cue
    CuePrev        = 0x39, // /cue/prev            — go to previous cue
    CueStop        = 0x3A, // /cue/stop            — freeze + cancel pending dwell

    // Noise generator (per-channel array verification)
    NoiseType     = 0x40, // /noise/{ch}/type ,s  white|pink
    NoiseGain     = 0x41, // /noise/{ch}/gain ,f  dB

    // Transport
    TransportPlay = 0x50, // /transport/play
    TransportStop = 0x51, // /transport/stop

    // Reverb engine select
    ReverbSelect  = 0x52, // /reverb/select ,s "fdn"|"ir"|"room"

    // Per-output-channel gain trim and limiter threshold
    OutputGain    = 0x53, // /output/{ch}/gain  ,f dB
    OutputLimit   = 0x54, // /output/{ch}/limit ,f dB threshold

    // Dreamscape room engine OSC control (⑥e-4). One tag, op-selected payload.
    // Single-leading-tag wire format (,i / ,f / ,ff / ,fff / ,f×13) — NO ,ii
    // seq/id header, so the decoder's payload_int_offset trap never fires.
    RoomCtl       = 0x55, // /room/* — enable | set (atomic bundle) | per-param

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
    // M5.1: optional subscriber tag string. When set to
    // "echo_subscriber=adm_object_stream" (and reply_port > 0), the engine
    // adds the peer to the echo subscriber registry (port 9102 by convention).
    // 64 bytes (63 + null), same idiom as PayloadSceneSave.
    char     subscriber_tag[64] = {};
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
    // ADR 0018 D-5 — distinguishes the producer. The engine-internal
    // HeartbeatPublisher tags pings with `,h` (int64 ms) / `,t` (timetag);
    // the external adm_player (M3) tags them with `,d` (unix seconds). The
    // decoder sets from_external=true on the `,d` path so the control thread
    // can tick last_player_ping_unix_ms_ only for player-originated pings.
    bool from_external = false;
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

// v0.9 Lane E (E-M1): scene library management payloads. Fixed 64-byte name
// buffers mirror PayloadSceneSave/Load. SceneMeta carries a control-thread-only
// std::string for the meta JSON (tags/note); never touched on the audio thread.
struct PayloadSceneRename    { char from[64] = {}; char to[64] = {}; };
struct PayloadSceneDuplicate { char from[64] = {}; char to[64] = {}; };
struct PayloadSceneDelete    { char name[64] = {}; };
struct PayloadSceneMeta      { char name[64] = {}; std::string meta_json; };

// v0.9 Lane E (E-M3): cue transport payloads. CueGo carries a cue index;
// Next/Prev/Stop are nullary. POD — decoded on the UDP thread, applied on the
// control loop (never touched on the audio thread).
struct PayloadCueGo   { int32_t index = 0; };
struct PayloadCueNext {};
struct PayloadCuePrev {};
struct PayloadCueStop {};

struct PayloadNoiseType {
    uint32_t channel = 0;
    bool     pink    = false; // false = white, true = pink
};

struct PayloadNoiseGain {
    uint32_t channel = 0;
    float    gain_db = -60.f; // -60 dB ≡ effectively muted
};

// ADR 0018 D-2 — /transport/play optionally carries a `,d unix_time_seconds`
// timetag from adm_player M3. The engine stays EDGE-TRIGGERED: the gate flips
// immediately on decode; this field is advisory only (logged), never used to
// schedule a delayed start in this milestone. 0 = unset → immediate (legacy
// behaviour). The field is future-proofing for an M4 sample-clock scheduler.
struct PayloadTransportPlay {
    double start_unix_seconds = 0.0;
};
struct PayloadTransportStop {};

struct PayloadReverbSelect {
    uint8_t which = 0; // 0 = fdn, 1 = ir, 2 = room (spatial late FDN)
};

// Dreamscape room engine control (⑥e-4). A single tag carrying an op selector
// and the live room parameters. For per-param ops only the relevant field(s)
// are read; for SetAll the whole struct is applied atomically (one FIFO entry).
// All fields default to the engine's reference defaults so an under-specified
// command (e.g. an Enable) leaves a sensible struct.
//
// Address → op mapping (single-leading-tag wire format, no ,ii header):
//   /room/enable          ,i      Enable            (enable)
//   /room/set             ,f×13   SetAll            (all 13 floats below, in order)
//   /room/t60             ,f      T60               (t60)
//   /room/size            ,fff    Size              (sx, sy, sz)
//   /room/early/width     ,f      EarlyWidth        (early_width_deg)
//   /room/early/balance   ,f      EarlyBalance      (early_balance01)
//   /room/cluster/send    ,f      ClusterSend       (cluster_send01)
//   /room/cluster/diffusion ,f    ClusterDiffusion  (cluster_diffusion01)
//   /room/cluster/volume  ,f      ClusterVolume     (cluster_volume_m3)
//   /room/eq/early        ,ff     EqEarly           (eq_early_hp, eq_early_lp) — LOCKSTEP
//   /room/late/hf         ,ff     LateHf            (late_hf_corner_hz, late_hf_ratio01)
struct PayloadRoomCtl {
    enum class Op : uint8_t {
        Enable           = 0,
        SetAll           = 1,
        T60              = 2,
        Size             = 3,
        EarlyWidth       = 4,
        EarlyBalance     = 5,
        ClusterSend      = 6,
        ClusterDiffusion = 7,
        ClusterVolume    = 8,
        EqEarly          = 9,  // recoeffs cluster-bus EQ + all per-object early EQ in lockstep
        LateHf           = 10,
    };
    Op    op = Op::Enable;
    bool  enable = false;
    // SetAll canonical order (also the per-param carriers):
    float t60                = 1.2f;     // RoomFdn t60Seconds
    float sx                 = 6.f;      // halfExtents.x
    float sy                 = 5.f;      // halfExtents.y
    float sz                 = 3.f;      // halfExtents.z
    float early_width_deg    = 45.f;     // early reflection width-spread cone
    float early_balance01    = 0.45f;    // RoomEarlyParams earlyLateBalance01
    float cluster_send01     = 0.4f;     // per-object cluster send (×0.48 internally)
    float cluster_diffusion01= 0.48f;    // RoomClusterParams diffusion01
    float cluster_volume_m3  = 630.f;    // RoomClusterParams virtualVolumeM3
    float eq_early_hp        = 120.f;    // early-cluster absorption HP corner
    float eq_early_lp        = 10000.f;  // early-cluster absorption LP corner
    float late_hf_corner_hz  = 6200.f;   // RoomFdn hfDecayCornerHz
    float late_hf_ratio01    = 0.62f;    // RoomFdn hfDecayRatio01
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

// v0.7 D-S1: /sys/binaural_reset_demote ,i {0|1}
// enable != 0 triggers reset attempt; enable == 0 is a no-op (future reserve).
struct PayloadSysBinauralResetDemote {
    bool enable = false;
};

// B-M3: /sys/binaural_sofa_select ,s "<catalog-name>" — resolve catalog name
// → speh_path on the control thread and trigger a live SOFA swap on the ~1 Hz
// tick. Empty string is rejected (makeUnknown) at decode time.
struct PayloadSysBinauralSofaSelect {
    std::string name;
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
    PayloadSceneRename,
    PayloadSceneDuplicate,
    PayloadSceneDelete,
    PayloadSceneMeta,
    PayloadCueGo,
    PayloadCueNext,
    PayloadCuePrev,
    PayloadCueStop,
    PayloadNoiseType,
    PayloadNoiseGain,
    PayloadTransportPlay,
    PayloadTransportStop,
    PayloadObjDsp,
    PayloadReverbSelect,
    PayloadRoomCtl,
    PayloadOutputGain,
    PayloadOutputLimit,
    PayloadSysLoadLayout,
    PayloadSysBinauralSofa,
    PayloadSysBinauralEnable,
    PayloadSysBinauralMode,
    PayloadSysBinauralResetDemote,
    PayloadSysBinauralSofaSelect,
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
