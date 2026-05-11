// vst3/AudioCommand.h
// S3 (C4): POD audio-path command for the UDP-thread → audio-thread SPSC ring.
//
// spe::ipc::Command uses std::variant<..., PayloadUnknown{std::string}> which is
// NOT trivially copyable and therefore cannot go into SpscRing<T,N> directly.
// AudioCommand is a flat POD struct that captures the fields the audio path needs:
//   - tag:     which command type (same CommandTag enum)
//   - payload: a union of all audio-relevant payload types (no std::string member)
//
// The UDP thread converts spe::ipc::Command → AudioCommand before pushing into
// the ring. Unknown / string-bearing commands (ObjName, SceneSave, SceneLoad,
// SceneList, ReverbSelect) are discarded at the push site with a drop counter
// increment; only audio-relevant tags are forwarded.
//
// RT-safety guarantee:
//   - AudioCommand is trivially copyable (static_assert verified below).
//   - pop() in the audio callback: no allocation, no mutex, no exception.
//   - push() on UDP thread: allowed to allocate per ADR 0010 §A4-β.
#pragma once

#include "ipc/Command.h"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace spe::vst3 {

// ---------------------------------------------------------------------------
// AudioPayload — union of all audio-relevant payload types.
// All members must be trivially copyable PODs (no std::string, no std::vector).
// ---------------------------------------------------------------------------
union AudioPayload {
    spe::ipc::PayloadObjMove        obj_move;
    spe::ipc::PayloadObjGain        obj_gain;
    spe::ipc::PayloadObjActive      obj_active;
    spe::ipc::PayloadObjAlgo        obj_algo;
    spe::ipc::PayloadObjMute        obj_mute;
    spe::ipc::PayloadObjXYZ         obj_xyz;
    spe::ipc::PayloadObjActiveAdm   obj_active_adm;
    spe::ipc::PayloadObjWidth       obj_width;
    spe::ipc::PayloadSysHandshake   sys_handshake;
    spe::ipc::PayloadSysAlgoSwap    sys_algo_swap;
    spe::ipc::PayloadSysReset       sys_reset;
    spe::ipc::PayloadSysAmbiOrder   sys_ambi_order;
    spe::ipc::PayloadSysLtcChase    sys_ltc_chase;
    spe::ipc::PayloadSysAmbiDecoderType sys_ambi_decoder_type;
    spe::ipc::PayloadHbPing         hb_ping;
    spe::ipc::PayloadHbPong         hb_pong;
    spe::ipc::PayloadNoiseType      noise_type;
    spe::ipc::PayloadNoiseGain      noise_gain;
    spe::ipc::PayloadTransportPlay  transport_play;
    spe::ipc::PayloadTransportStop  transport_stop;
    spe::ipc::PayloadObjDsp         obj_dsp;
    spe::ipc::PayloadOutputGain     output_gain;
    spe::ipc::PayloadOutputLimit    output_limit;

    AudioPayload() noexcept { std::memset(this, 0, sizeof(*this)); }
};

// Verify all union members are trivially copyable.
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjMove>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjGain>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjActive>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjMute>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjXYZ>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjWidth>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadSysAmbiOrder>);
static_assert(std::is_trivially_copyable_v<spe::ipc::PayloadObjDsp>);

// ---------------------------------------------------------------------------
// AudioCommand — flat POD that goes into SpscRing<AudioCommand, N>.
// ---------------------------------------------------------------------------
struct AudioCommand {
    spe::ipc::CommandTag tag     = spe::ipc::CommandTag::Unknown;
    uint16_t schema_version      = spe::ipc::SCHEMA_VERSION;
    uint32_t seq                 = 0;
    uint32_t id                  = 0;
    AudioPayload payload         = {};
};

static_assert(std::is_trivially_copyable_v<AudioCommand>,
              "AudioCommand must be trivially copyable for SpscRing RT-safety");

// ---------------------------------------------------------------------------
// fromCommand() — convert spe::ipc::Command → AudioCommand.
// Returns false (and leaves `out` unchanged) for commands with non-POD
// payloads (ObjName, SceneSave, SceneLoad, SceneList, ReverbSelect, Unknown).
// ---------------------------------------------------------------------------
inline bool fromCommand(const spe::ipc::Command& src, AudioCommand& out) noexcept
{
    AudioCommand ac;
    ac.tag            = src.tag;
    ac.schema_version = src.schema_version;
    ac.seq            = src.seq;
    ac.id             = src.id;

    using T = spe::ipc::CommandTag;
    switch (src.tag) {
        case T::ObjMove:
            ac.payload.obj_move = std::get<spe::ipc::PayloadObjMove>(src.payload);
            break;
        case T::ObjGain:
            ac.payload.obj_gain = std::get<spe::ipc::PayloadObjGain>(src.payload);
            break;
        case T::ObjActive:
            ac.payload.obj_active = std::get<spe::ipc::PayloadObjActive>(src.payload);
            break;
        case T::ObjAlgo:
            ac.payload.obj_algo = std::get<spe::ipc::PayloadObjAlgo>(src.payload);
            break;
        case T::ObjMute:
            ac.payload.obj_mute = std::get<spe::ipc::PayloadObjMute>(src.payload);
            break;
        case T::ObjXYZ:
            ac.payload.obj_xyz = std::get<spe::ipc::PayloadObjXYZ>(src.payload);
            break;
        case T::ObjActiveAdm:
            ac.payload.obj_active_adm = std::get<spe::ipc::PayloadObjActiveAdm>(src.payload);
            break;
        case T::ObjWidth:
            ac.payload.obj_width = std::get<spe::ipc::PayloadObjWidth>(src.payload);
            break;
        case T::SysHandshake:
            ac.payload.sys_handshake = std::get<spe::ipc::PayloadSysHandshake>(src.payload);
            break;
        case T::SysAlgoSwap:
            ac.payload.sys_algo_swap = std::get<spe::ipc::PayloadSysAlgoSwap>(src.payload);
            break;
        case T::SysReset:
            ac.payload.sys_reset = std::get<spe::ipc::PayloadSysReset>(src.payload);
            break;
        case T::SysAmbiOrder:
            ac.payload.sys_ambi_order = std::get<spe::ipc::PayloadSysAmbiOrder>(src.payload);
            break;
        case T::SysLtcChase:
            ac.payload.sys_ltc_chase = std::get<spe::ipc::PayloadSysLtcChase>(src.payload);
            break;
        case T::SysAmbiDecoderType:
            ac.payload.sys_ambi_decoder_type = std::get<spe::ipc::PayloadSysAmbiDecoderType>(src.payload);
            break;
        case T::HbPing:
            ac.payload.hb_ping = std::get<spe::ipc::PayloadHbPing>(src.payload);
            break;
        case T::HbPong:
            ac.payload.hb_pong = std::get<spe::ipc::PayloadHbPong>(src.payload);
            break;
        case T::NoiseType:
            ac.payload.noise_type = std::get<spe::ipc::PayloadNoiseType>(src.payload);
            break;
        case T::NoiseGain:
            ac.payload.noise_gain = std::get<spe::ipc::PayloadNoiseGain>(src.payload);
            break;
        case T::TransportPlay:
            ac.payload.transport_play = std::get<spe::ipc::PayloadTransportPlay>(src.payload);
            break;
        case T::TransportStop:
            ac.payload.transport_stop = std::get<spe::ipc::PayloadTransportStop>(src.payload);
            break;
        case T::ObjDsp:
            ac.payload.obj_dsp = std::get<spe::ipc::PayloadObjDsp>(src.payload);
            break;
        case T::OutputGain:
            ac.payload.output_gain = std::get<spe::ipc::PayloadOutputGain>(src.payload);
            break;
        case T::OutputLimit:
            ac.payload.output_limit = std::get<spe::ipc::PayloadOutputLimit>(src.payload);
            break;
        // Non-POD / string-bearing tags — drop at push site
        case T::ObjName:        // PayloadObjName has char[32] — actually POD, but
                                // treat as audio-irrelevant metadata; drop.
        case T::SceneSave:      // PayloadSceneSave has char[64]
        case T::SceneLoad:      // PayloadSceneLoad has char[64]
        case T::SceneList:      // no audio side-effect
        case T::ReverbSelect:   // PayloadReverbSelect is POD but audio path ignores
        case T::Unknown:        // PayloadUnknown has std::string — must drop
        default:
            return false;
    }

    out = ac;
    return true;
}

} // namespace spe::vst3
