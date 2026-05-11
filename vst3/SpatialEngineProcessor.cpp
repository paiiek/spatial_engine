// vst3/SpatialEngineProcessor.cpp
// Phase C C2 Option-B: IComponent + IAudioProcessor direct vtable.
// No helper base classes. FUnknown implemented with atomic ref-count.
// C2B postmortem S2/S3: state v2 (36 bytes), kIsBypass param, dry pass-through.

#include "SpatialEngineProcessor.hpp"
#include "SpatialEngineProcessData.hpp"
#include "SpatialEngineController.hpp"

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "ipc/Command.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

#ifdef SPATIAL_ENGINE_VST3_OSC
#include "SpatialEnginePluginUdp.h"
#endif

// Pull in class IID definitions (DEF_CLASS_IID macro from vstinitiids.cpp
// registers IComponent::iid / IAudioProcessor::iid / IEditController::iid).
// We include vstinitiids.cpp via CMake source list, not here.

namespace spe::vst3 {

SpatialEngineProcessor::SpatialEngineProcessor()
    : engine_(std::make_unique<spe::core::SpatialEngine>(0 /*no UDP*/))
{}

SpatialEngineProcessor::~SpatialEngineProcessor() = default;

// ---------------------------------------------------------------------------
// FUnknown
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::queryInterface(const Steinberg::TUID _iid, void** obj)
{
    if (!obj) return Steinberg::kInvalidArgument;

    using namespace Steinberg;
    // IComponent
    if (FUnknownPrivate::iidEqual(_iid, Vst::IComponent_iid)) {
        addRef();
        *obj = static_cast<Vst::IComponent*>(this);
        return kResultOk;
    }
    // IAudioProcessor
    if (FUnknownPrivate::iidEqual(_iid, Vst::IAudioProcessor_iid)) {
        addRef();
        *obj = static_cast<Vst::IAudioProcessor*>(this);
        return kResultOk;
    }
    // IConnectionPoint
    if (FUnknownPrivate::iidEqual(_iid, Vst::IConnectionPoint_iid)) {
        addRef();
        *obj = static_cast<Vst::IConnectionPoint*>(this);
        return kResultOk;
    }
    // IPluginBase (IComponent inherits it — same vtable pointer)
    if (FUnknownPrivate::iidEqual(_iid, IPluginBase_iid)) {
        addRef();
        *obj = static_cast<Vst::IComponent*>(this);
        return kResultOk;
    }
    // FUnknown itself
    if (FUnknownPrivate::iidEqual(_iid, FUnknown_iid)) {
        addRef();
        *obj = static_cast<Vst::IComponent*>(this);
        return kResultOk;
    }

    *obj = nullptr;
    return kNoInterface;
}

Steinberg::uint32 PLUGIN_API SpatialEngineProcessor::addRef()
{
    return ++ref_count_;
}

Steinberg::uint32 PLUGIN_API SpatialEngineProcessor::release()
{
    auto r = --ref_count_;
    if (r == 0) delete this;
    return r;
}

// ---------------------------------------------------------------------------
// IPluginBase
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::initialize(Steinberg::FUnknown* /*context*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SpatialEngineProcessor::terminate()
{
    if (active_) {
        engine_->releaseResources();
        active_ = false;
    }
#ifdef SPATIAL_ENGINE_VST3_OSC
    if (udp_io_) {
        udp_io_->stop();
        udp_io_.reset();
    }
#endif
    return Steinberg::kResultOk;
}

// ---------------------------------------------------------------------------
// IComponent
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getControllerClassId(Steinberg::TUID classId)
{
    std::memcpy(classId, kSpatialEngineControllerUID, sizeof(Steinberg::TUID));
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setIoMode(Steinberg::Vst::IoMode /*mode*/)
{
    // C2B postmortem S3: setIoMode hijack REMOVED. Bypass is now controlled
    // exclusively by the kIsBypass parameter (id=6) per VST3 SDK convention
    // (pluginterfaces/vst/ivsteditcontroller.h:71 kIsBypass = 1<<16).
    // Accept any IoMode silently — matches SDK example behaviour.
    return Steinberg::kResultOk;
}

Steinberg::int32 PLUGIN_API
SpatialEngineProcessor::getBusCount(Steinberg::Vst::MediaType type,
                                     Steinberg::Vst::BusDirection /*dir*/)
{
    // One audio input bus + one audio output bus. No event buses.
    if (type == Steinberg::Vst::kAudio) return 1;
    return 0;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getBusInfo(Steinberg::Vst::MediaType type,
                                    Steinberg::Vst::BusDirection dir,
                                    Steinberg::int32 index,
                                    Steinberg::Vst::BusInfo& bus)
{
    using namespace Steinberg::Vst;
    if (type != kAudio || index != 0) return Steinberg::kInvalidArgument;

    bus.mediaType    = kAudio;
    bus.direction    = dir;
    bus.channelCount = 2; // stereo
    bus.busType      = kMain;
    bus.flags        = BusInfo::kDefaultActive;

    if (dir == kInput) {
        // "Spatial Input" — stereo stem from host
        const char* name = "Spatial Input";
        for (int i = 0; name[i] && i < 128; ++i)
            bus.name[i] = static_cast<Steinberg::Vst::TChar>(name[i]);
    } else {
        const char* name = "Spatial Output";
        for (int i = 0; name[i] && i < 128; ++i)
            bus.name[i] = static_cast<Steinberg::Vst::TChar>(name[i]);
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getRoutingInfo(Steinberg::Vst::RoutingInfo& /*inInfo*/,
                                        Steinberg::Vst::RoutingInfo& /*outInfo*/)
{
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::activateBus(Steinberg::Vst::MediaType /*type*/,
                                     Steinberg::Vst::BusDirection /*dir*/,
                                     Steinberg::int32 /*index*/,
                                     Steinberg::TBool /*state*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setActive(Steinberg::TBool state)
{
    if (state && !active_) {
        active_ = true;
#ifdef SPATIAL_ENGINE_VST3_OSC
        if (!udp_io_) {
            // S4: build reverse-path lambda if controller is already connected.
            // If connect() has not been called yet (host calls setActive before
            // connect in some DAWs), ctrl_for_reverse_path_ will be null and
            // the reverse path will be disabled for this session. In practice
            // hosts call connect before setActive(true) per SDK lifecycle spec.
            PushParamEditFn push_fn;
            if (ctrl_for_reverse_path_) {
                SpatialEngineController* ctrl = ctrl_for_reverse_path_;
                push_fn = [ctrl](uint32_t id, double norm) noexcept -> bool {
                    return ctrl->pushParamEdit(
                        static_cast<Steinberg::Vst::ParamID>(id),
                        static_cast<Steinberg::Vst::ParamValue>(norm));
                };
            }
            udp_io_ = std::make_unique<SpatialEnginePluginUdp>(
                "spatial_engine_vst3", &osc_cmd_ring_, std::move(push_fn));
            udp_io_->start();
        }
#endif
    } else if (!state && active_) {
        engine_->releaseResources();
        active_ = false;
#ifdef SPATIAL_ENGINE_VST3_OSC
        if (udp_io_) {
            udp_io_->stop();
            udp_io_.reset();
        }
#endif
    }
    return Steinberg::kResultOk;
}

// ---------------------------------------------------------------------------
// State persistence — C4-S7: v3 writer activated; v3 reader promoted from D3-γ.
// Binary format v3 (40 bytes, little-endian):
//   bytes  0-3:  magic 'S','P','E','1' (0x31455053)
//   bytes  4-5:  version uint16 = 3
//   bytes  6-7:  param_count uint16 = 8
//   bytes  8-39: 8 × float32 normalized values (params 0..7, [6]=bypass, [7]=kMute)
//
// v2 format (36 bytes): version=2, param_count=7, 7 floats ([6]=bypass).
// v1 format (32 bytes): version=1, param_count=6, 6 floats.
// v4+: forward-compat refusal — returns kResultFalse (ADR 0011 §rule-6).
// Reader handles v1/v2/v3. Writer emits v3 from this commit onward (S7).
// Backward compat: v2 reader sets kMute=0 (mute-off default).
// ---------------------------------------------------------------------------
namespace {
static constexpr Steinberg::int32  kStateMagic      = 0x31455053; // 'SPE1' LE
static constexpr Steinberg::int32  kStateBytesV1    = 32;
static constexpr Steinberg::int32  kStateBytesV2    = 36;
static constexpr Steinberg::int32  kStateBytesV3    = 40;
static constexpr Steinberg::uint16 kStateVersionV1  = 1;
static constexpr Steinberg::uint16 kStateVersionV2  = 2;
static constexpr Steinberg::uint16 kStateVersionV3  = 3;
static constexpr Steinberg::uint16 kStateNParamsV1  = 6;
static constexpr Steinberg::uint16 kStateNParamsV2  = 7;
static constexpr Steinberg::uint16 kStateNParamsV3  = 8;
} // namespace

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setState(Steinberg::IBStream* state)
{
    if (!state) return Steinberg::kInvalidArgument;

    // Control thread (Driver #2) — stderr logging permitted.
    // Phase 1: read header (8 bytes)
    uint8_t buf[kStateBytesV3];  // sized for largest known version
    Steinberg::int32 numRead = 0;
    Steinberg::tresult r = state->read(buf, 8, &numRead);
    if (r != Steinberg::kResultOk || numRead < 8) {
        std::fprintf(stderr, "VST3 setState: short header read, defaults retained\n");
        return Steinberg::kResultOk;
    }

    Steinberg::int32 magic = 0;
    std::memcpy(&magic, buf + 0, 4);
    if (magic != kStateMagic) {
        std::fprintf(stderr, "VST3 setState: magic mismatch, defaults retained\n");
        return Steinberg::kResultOk;
    }

    Steinberg::uint16 version = 0;
    std::memcpy(&version, buf + 4, 2);
    Steinberg::uint16 nparams = 0;
    std::memcpy(&nparams, buf + 6, 2);

    // Phase 2: branch on version
    if (version == kStateVersionV1 && nparams == kStateNParamsV1) {
        // v1: read 24 bytes (6 floats)
        numRead = 0;
        r = state->read(buf + 8, 24, &numRead);
        if (r != Steinberg::kResultOk || numRead < 24) {
            std::fprintf(stderr, "VST3 setState: v1 payload short read, defaults retained\n");
            return Steinberg::kResultOk;
        }
        for (int i = 0; i < 6; ++i) {
            float v = 0.f;
            std::memcpy(&v, buf + 8 + i * 4, 4);
            norm_values_[i].store(v, std::memory_order_relaxed);
            dispatchParamChange(static_cast<Steinberg::Vst::ParamID>(i),
                                static_cast<Steinberg::Vst::ParamValue>(v));
        }
        // v1 fallback: bypass=0, kMute=0 (mute-off)
        norm_values_[6].store(0.f, std::memory_order_relaxed);
        norm_values_[7].store(0.f, std::memory_order_relaxed);
    } else if (version == kStateVersionV2 && nparams == kStateNParamsV2) {
        // v2: read 28 bytes (7 floats)
        numRead = 0;
        r = state->read(buf + 8, 28, &numRead);
        if (r != Steinberg::kResultOk || numRead < 28) {
            std::fprintf(stderr, "VST3 setState: v2 payload short read, defaults retained\n");
            return Steinberg::kResultOk;
        }
        for (int i = 0; i < 7; ++i) {
            float v = 0.f;
            std::memcpy(&v, buf + 8 + i * 4, 4);
            norm_values_[i].store(v, std::memory_order_relaxed);
            if (i < 6) {
                dispatchParamChange(static_cast<Steinberg::Vst::ParamID>(i),
                                    static_cast<Steinberg::Vst::ParamValue>(v));
            }
            // kBypass (i==6): audio path reads norm_values_[6] directly in process()
        }
        // v2: kMute not present in stream — default to 0 (mute-off)
        norm_values_[7].store(0.f, std::memory_order_relaxed);
    } else if (version == kStateVersionV3 && nparams == kStateNParamsV3) {
        // v3 (C4-S7 activated): read 32 bytes (8 floats)
        // 8th float is kMute — routed into norm_values_[kMute] (active from S7).
        numRead = 0;
        r = state->read(buf + 8, 32, &numRead);
        if (r != Steinberg::kResultOk || numRead < 32) {
            std::fprintf(stderr, "VST3 setState: v3 payload short read, defaults retained\n");
            return Steinberg::kResultOk;
        }
        for (int i = 0; i < 8; ++i) {
            float v = 0.f;
            std::memcpy(&v, buf + 8 + i * 4, 4);
            norm_values_[i].store(v, std::memory_order_relaxed);
            if (i < 6) {
                dispatchParamChange(static_cast<Steinberg::Vst::ParamID>(i),
                                    static_cast<Steinberg::Vst::ParamValue>(v));
            }
            // kBypass (i==6) and kMute (i==7): audio path reads norm_values_ directly
        }
        std::fprintf(stderr, "VST3 setState: v3 loaded (kMute=%.3f active)\n",
                     (double)norm_values_[kMute].load(std::memory_order_relaxed));
    } else if (version > kStateVersionV3) {
        // Forward-compat refusal per ADR 0011 §rule-6
        std::fprintf(stderr, "VST3 setState: unsupported future version v=%u, refusing load\n",
                     (unsigned)version);
        return Steinberg::kResultFalse;
    } else {
        std::fprintf(stderr, "VST3 setState: version/nparams mismatch (v=%u n=%u), defaults retained\n",
                     (unsigned)version, (unsigned)nparams);
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getState(Steinberg::IBStream* state)
{
    if (!state) return Steinberg::kInvalidArgument;

    // C4-S7: emit v3 (40 bytes) — includes kMute at index 7.
    // Binary layout v3 (40 bytes, little-endian):
    //   bytes  0-3:  magic 'SPE1' = 0x31455053
    //   bytes  4-5:  version uint16 = 3
    //   bytes  6-7:  param_count uint16 = 8
    //   bytes  8-11: float32 norm_values_[0] = kPanAz
    //   bytes 12-15: float32 norm_values_[1] = kPanEl
    //   bytes 16-19: float32 norm_values_[2] = kSourceWidth
    //   bytes 20-23: float32 norm_values_[3] = kMasterGain
    //   bytes 24-27: float32 norm_values_[4] = kAmbiOrder
    //   bytes 28-31: float32 norm_values_[5] = kRoomPreset
    //   bytes 32-35: float32 norm_values_[6] = kBypass
    //   bytes 36-39: float32 norm_values_[7] = kMute
    uint8_t buf[kStateBytesV3];

    // Magic
    Steinberg::int32 magic = kStateMagic;
    std::memcpy(buf + 0, &magic, 4);

    // Version = 3
    Steinberg::uint16 version = kStateVersionV3;
    std::memcpy(buf + 4, &version, 2);

    // Param count = 8
    Steinberg::uint16 nparams = kStateNParamsV3;
    std::memcpy(buf + 6, &nparams, 2);

    // 8 float normalized values (params 0..7, [7]=kMute)
    for (int i = 0; i < 8; ++i) {
        float v = norm_values_[i].load(std::memory_order_relaxed);
        std::memcpy(buf + 8 + i * 4, &v, 4);
    }

    Steinberg::int32 written = 0;
    Steinberg::tresult r = state->write(buf, kStateBytesV3, &written);
    return (r == Steinberg::kResultOk && written == kStateBytesV3)
               ? Steinberg::kResultOk
               : Steinberg::kResultFalse;
}

// ---------------------------------------------------------------------------
// IAudioProcessor
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setBusArrangements(Steinberg::Vst::SpeakerArrangement* /*inputs*/,
                                             Steinberg::int32 /*numIns*/,
                                             Steinberg::Vst::SpeakerArrangement* /*outputs*/,
                                             Steinberg::int32 /*numOuts*/)
{
    // Accept any arrangement for now; stereo is reported in getBusInfo.
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getBusArrangement(Steinberg::Vst::BusDirection /*dir*/,
                                            Steinberg::int32 /*index*/,
                                            Steinberg::Vst::SpeakerArrangement& arr)
{
    arr = Steinberg::Vst::SpeakerArr::kStereo;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::canProcessSampleSize(Steinberg::int32 symbolicSampleSize)
{
    // Support 32-bit float only.
    return (symbolicSampleSize == Steinberg::Vst::kSample32)
               ? Steinberg::kResultOk
               : Steinberg::kResultFalse;
}

Steinberg::uint32 PLUGIN_API SpatialEngineProcessor::getLatencySamples()
{
    return 0;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setupProcessing(Steinberg::Vst::ProcessSetup& setup)
{
    sample_rate_ = setup.sampleRate;
    max_block_   = setup.maxSamplesPerBlock;

    if (max_block_ > spe::MAX_BLOCK) {
        // Block size exceeds engine hard cap — suspend (cannot process).
        return Steinberg::kResultFalse;
    }

    engine_->prepareToPlay(sample_rate_, static_cast<int>(max_block_));
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setProcessing(Steinberg::TBool /*state*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::process(Steinberg::Vst::ProcessData& data)
{
    using namespace Steinberg::Vst;

#ifdef SPATIAL_ENGINE_VST3_OSC
    // --- S3: drain audio-path OSC command ring (UDP thread → audio thread) ---
    // RT-safe: SpscRing::pop() is wait-free, allocation-free, no mutex.
    // Per-block cap = ring capacity to bound worst-case drain time.
    // Commands decoded on the UDP thread (allowed to alloc per ADR 0010 §A4-β);
    // only the pop side (here) must be allocation-free.
    {
        AudioCommand ac;
        // Drain all pending commands; bounded by ring capacity (1024).
        while (osc_cmd_ring_.pop(ac)) {
            // S4 will forward these commands to the controller-side ring or
            // engine; for now dispatch directly to the engine (audio path).
            // Build a spe::ipc::Command and dispatch — allocation-free because
            // all audio-relevant payloads are trivially copyable PODs.
            spe::ipc::Command cmd;
            cmd.tag            = ac.tag;
            cmd.schema_version = ac.schema_version;
            cmd.seq            = ac.seq;
            cmd.id             = ac.id;

            using CT = spe::ipc::CommandTag;
            switch (ac.tag) {
                case CT::ObjMove:
                    cmd.payload = ac.payload.obj_move;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjGain:
                    cmd.payload = ac.payload.obj_gain;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjActive:
                    cmd.payload = ac.payload.obj_active;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjAlgo:
                    cmd.payload = ac.payload.obj_algo;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjMute:
                    cmd.payload = ac.payload.obj_mute;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjXYZ:
                    cmd.payload = ac.payload.obj_xyz;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjActiveAdm:
                    cmd.payload = ac.payload.obj_active_adm;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjWidth:
                    cmd.payload = ac.payload.obj_width;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::SysAmbiOrder:
                    cmd.payload = ac.payload.sys_ambi_order;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::SysLtcChase:
                    cmd.payload = ac.payload.sys_ltc_chase;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::SysAmbiDecoderType:
                    cmd.payload = ac.payload.sys_ambi_decoder_type;
                    engine_->dispatchCommand(cmd);
                    break;
                case CT::ObjDsp:
                    cmd.payload = ac.payload.obj_dsp;
                    engine_->dispatchCommand(cmd);
                    break;
                default:
                    // Remaining tags are non-audio or already dropped at push site.
                    break;
            }
        }
    }
#endif

    // --- Parameter changes: block-rate snapshot (last point only) ---
    // Step 2.3: drain inputParameterChanges, atomic store norm value, then dispatch.
    // RT-safety: atomic store/load only — alloc 0, mutex 0.
    if (data.inputParameterChanges) {
        const Steinberg::int32 numQueues = data.inputParameterChanges->getParameterCount();
        for (Steinberg::int32 q = 0; q < numQueues; ++q) {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;

            const ParamID paramId  = queue->getParameterId();
            const Steinberg::int32 numPts = queue->getPointCount();
            if (numPts <= 0) continue;

            // Last point = block-rate value (sample-accurate deferred to Phase D6)
            Steinberg::int32 sampleOffset = 0;
            ParamValue       normValue    = 0.0;
            if (queue->getPoint(numPts - 1, sampleOffset, normValue) != Steinberg::kResultOk) continue;

            // Step 2.4: atomic store before dispatch (8 params after C4-S7)
            if (paramId < 8u) {
                norm_values_[paramId].store(static_cast<float>(normValue),
                                            std::memory_order_relaxed);
            }

            dispatchParamChange(paramId, normValue);
        }
    }

    // --- Audio: kMute → zero output; kBypass → dry pass-through; else engine ---
    // kMute (id=7, C4-S7): overrides everything — output buffers zeroed regardless
    //   of bypass state. Distinct from kBypass (dry pass-through, input→output).
    // kBypass (id=6, C2B postmortem S3): dry pass-through — input identity-copied.
    if (data.numSamples > 0) {
        if (norm_values_[kMute].load(std::memory_order_acquire) >= 0.5f) {
            // kMute=on: zero all output buffers, no audio output.
            if (data.outputs && data.numOutputs > 0) {
                AudioBusBuffers& outBus = data.outputs[0];
                for (Steinberg::int32 ch = 0; ch < outBus.numChannels; ++ch) {
                    if (outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                        std::memset(outBus.channelBuffers32[ch], 0,
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    }
                }
            }
        } else if (norm_values_[kBypass].load(std::memory_order_acquire) >= 0.5f) {
            // Bypass: dry pass-through — RT-safe, alloc=0, no mutex.
            if (data.inputs  && data.numInputs  > 0 &&
                data.outputs && data.numOutputs > 0) {
                const AudioBusBuffers& inBus  = data.inputs[0];
                AudioBusBuffers&       outBus = data.outputs[0];
                const Steinberg::int32 minCh =
                    (inBus.numChannels < outBus.numChannels)
                    ? inBus.numChannels : outBus.numChannels;

                // Identity copy for min(in,out) channels
                for (Steinberg::int32 ch = 0; ch < minCh; ++ch) {
                    if (inBus.channelBuffers32  && inBus.channelBuffers32[ch] &&
                        outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                        std::memcpy(outBus.channelBuffers32[ch],
                                    inBus.channelBuffers32[ch],
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    }
                }
                // Zero remaining output channels (if outBus has more than inBus)
                for (Steinberg::int32 ch = minCh; ch < outBus.numChannels; ++ch) {
                    if (outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                        std::memset(outBus.channelBuffers32[ch], 0,
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    }
                }
            }
        } else if (active_) {
            spe::audio_io::AudioBlock block =
                ProcessDataAdapter::adapt(data, sample_rate_);
            engine_->audioBlock(block);
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::uint32 PLUGIN_API SpatialEngineProcessor::getTailSamples()
{
    return Steinberg::Vst::kInfiniteTail; // reverb tail
}

// ---------------------------------------------------------------------------
// dispatchParamChange — RT-safe (no alloc, no mutex, atomic load for stale values)
// Decision gate (§6 Step 2.3): audio thread direct call OK (RT_ASSERT_NO_ALLOC
// validated in test_vst3_dispatch_rt_safety). SPSC ring not needed.
// ---------------------------------------------------------------------------
static constexpr double kPiProc = 3.14159265358979323846;

// master_gain skew replication (matches SpatialEngineController::gainNormToPlain)
// centre = 0 dB, range [-60, 6], skewFactor = log(0.5)/log((0-(-60))/(6-(-60)))
static float gainNormToLinear(double norm) noexcept
{
    // skewFactor computed once
    static const double skew = std::log(0.5) / std::log(60.0 / 66.0);
    if (norm <= 0.0) return 0.f;  // -inf dB -> 0 linear
    double dB = -60.0 + 66.0 * std::pow(norm, 1.0 / skew);
    return static_cast<float>(std::pow(10.0, dB / 20.0));
}

void SpatialEngineProcessor::dispatchParamChange(Steinberg::Vst::ParamID id,
                                                  Steinberg::Vst::ParamValue norm) noexcept
{
    spe::ipc::Command cmd;
    cmd.schema_version = spe::ipc::SCHEMA_VERSION;

    switch (id) {
        case kPanAz: {
            // norm [0,1] -> az_rad [-pi, pi]; use stale el from atomic snapshot
            float az = static_cast<float>((norm - 0.5) * 2.0 * kPiProc);
            float el = static_cast<float>(
                (norm_values_[kPanEl].load(std::memory_order_relaxed) - 0.5) * kPiProc);
            spe::ipc::PayloadObjMove p;
            p.obj_id = 0; p.az_rad = az; p.el_rad = el; p.dist_m = 1.f;
            cmd.tag = spe::ipc::CommandTag::ObjMove;
            cmd.payload = p;
            engine_->dispatchCommand(cmd);
            break;
        }
        case kPanEl: {
            // norm [0,1] -> el_rad [-pi/2, pi/2]; use stale az from atomic snapshot
            float az = static_cast<float>(
                (norm_values_[kPanAz].load(std::memory_order_relaxed) - 0.5) * 2.0 * kPiProc);
            float el = static_cast<float>((norm - 0.5) * kPiProc);
            spe::ipc::PayloadObjMove p;
            p.obj_id = 0; p.az_rad = az; p.el_rad = el; p.dist_m = 1.f;
            cmd.tag = spe::ipc::CommandTag::ObjMove;
            cmd.payload = p;
            engine_->dispatchCommand(cmd);
            break;
        }
        case kSourceWidth: {
            spe::ipc::PayloadObjDsp p;
            p.obj_id = 0;
            p.param  = spe::ipc::PayloadObjDsp::Param::Width;
            p.value  = static_cast<float>(norm * kPiProc); // [0, pi]
            cmd.tag = spe::ipc::CommandTag::ObjDsp;
            cmd.payload = p;
            engine_->dispatchCommand(cmd);
            break;
        }
        case kMasterGain: {
            // norm -> skewed dB -> linear gain
            spe::ipc::PayloadObjGain p;
            p.obj_id = 0;
            p.gain   = gainNormToLinear(norm);
            cmd.tag = spe::ipc::CommandTag::ObjGain;
            cmd.payload = p;
            engine_->dispatchCommand(cmd);
            break;
        }
        case kAmbiOrder: {
            // norm [0,1] -> choice index 0..2 -> order 1..3
            int idx = static_cast<int>(norm * 2.0 + 0.5);
            if (idx < 0) idx = 0;
            if (idx > 2) idx = 2;
            spe::ipc::PayloadSysAmbiOrder p;
            p.order = static_cast<uint8_t>(idx + 1);
            cmd.tag = spe::ipc::CommandTag::SysAmbiOrder;
            cmd.payload = p;
            engine_->dispatchCommand(cmd);
            break;
        }
        case kRoomPreset:
            // Phase D6 deferral — MVP no audio side-effect, passthrough.
            break;
        case kBypass:
            // No engine-side action — bypass is read directly in process()
            // via norm_values_[kBypass]. Param is addressable for round-trip.
            break;
        case kMute:
            // No engine-side action — kMute is read directly in process()
            // via norm_values_[kMute]. Output zeroing happens in the audio path.
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// IConnectionPoint (AM-R3-10: notify returns kNotImplemented)
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::connect(Steinberg::Vst::IConnectionPoint* other)
{
    peer_ = other; // no addRef — host manages lifetime per SDK convention

#ifdef SPATIAL_ENGINE_VST3_OSC
    // S4: try to resolve the controller for reverse-path wiring.
    // queryInterface on the peer; if it's a SpatialEngineController it will
    // return kResultOk. No addRef held — peer lifetime is host-managed.
    ctrl_for_reverse_path_ = nullptr;
    if (other) {
        void* ctrl_ptr = nullptr;
        if (other->queryInterface(kSpatialEngineControllerUID, &ctrl_ptr)
                == Steinberg::kResultOk && ctrl_ptr) {
            ctrl_for_reverse_path_ = static_cast<SpatialEngineController*>(ctrl_ptr);
            // Release the ref we just grabbed — we hold a raw non-owning pointer.
            ctrl_for_reverse_path_->release();
        }
    }
#endif

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::disconnect(Steinberg::Vst::IConnectionPoint* other)
{
    if (peer_ == other) {
        peer_ = nullptr;
#ifdef SPATIAL_ENGINE_VST3_OSC
        ctrl_for_reverse_path_ = nullptr;
#endif
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::notify(Steinberg::Vst::IMessage* /*message*/)
{
    // IConnectionPoint::notify channel not used (AM-R3-10 decision).
    return Steinberg::kNotImplemented;
}

} // namespace spe::vst3
