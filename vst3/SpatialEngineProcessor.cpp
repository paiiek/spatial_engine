// vst3/SpatialEngineProcessor.cpp
// Phase C C2 Option-B: IComponent + IAudioProcessor direct vtable.
// No helper base classes. FUnknown implemented with atomic ref-count.
// C2B postmortem S2/S3: state v2 (36 bytes), kIsBypass param, dry pass-through.

#include "SpatialEngineProcessor.hpp"
#include "SpatialEngineProcessData.hpp"
#include "SpatialEngineController.hpp"

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "geometry/LayoutLoader.h"
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
#include <string>
#include <vector>

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
                                     Steinberg::Vst::BusDirection dir)
{
    // v0.4 P1: one mono-ish input bus + two output buses (speakers + binaural).
    if (type != Steinberg::Vst::kAudio) return 0;
    if (dir == Steinberg::Vst::kInput)  return 1;  // 1 input bus
    if (dir == Steinberg::Vst::kOutput) return 2;  // bus 0 speakers, bus 1 binaural
    return 0;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getBusInfo(Steinberg::Vst::MediaType type,
                                    Steinberg::Vst::BusDirection dir,
                                    Steinberg::int32 index,
                                    Steinberg::Vst::BusInfo& bus)
{
    using namespace Steinberg::Vst;
    if (type != kAudio) return Steinberg::kInvalidArgument;

    bus.mediaType  = kAudio;
    bus.direction  = dir;
    bus.flags      = BusInfo::kDefaultActive;

    auto setName = [&](const char* name) {
        for (int i = 0; name[i] && i < 128; ++i)
            bus.name[i] = static_cast<Steinberg::Vst::TChar>(name[i]);
    };

    if (dir == kInput) {
        if (index != 0) return Steinberg::kInvalidArgument;
        bus.channelCount = 2; // stereo input stem
        bus.busType      = kMain;
        setName("Spatial Input");
        return Steinberg::kResultOk;
    }

    // Output buses
    if (index == 0) {
        // Speaker bus — channel count tracks the negotiated arrangement.
        bus.channelCount = out_bus0_channels_;
        bus.busType      = kMain;
        setName("Speakers");
        return Steinberg::kResultOk;
    }
    if (index == 1) {
        // v0.4 P1: binaural side-bus (stereo, A7 placeholder until v0.5).
        bus.channelCount = 2;
        bus.busType      = kAux;
        setName("Binaural");
        return Steinberg::kResultOk;
    }
    return Steinberg::kInvalidArgument;
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
// State persistence
// ---------------------------------------------------------------------------
//
// v0.4 — state v4 sectioned/TLV schema. Magic 'SPE4'. Writer always emits v4.
// Reader auto-detects v1/v2/v3 (legacy 'SPE1' magic) and walks v4 TLV when
// 'SPE4' magic is present. v5+ returns kResultFalse (forward-compat refusal).
//
// Legacy v1/v2/v3 framing (kept verbatim for back-compat reader):
//   bytes  0-3:  magic 'S','P','E','1' (0x31455053)
//   bytes  4-5:  version uint16
//   bytes  6-7:  param_count uint16
//   bytes  8..:  param_count × float32 normalized values
//
// v4 framing (this commit, v0.4):
//   bytes  0-3:  magic 'S','P','E','4' (0x34455053)
//   bytes  4-5:  version uint16 = 4
//   bytes  6-7:  section_count uint16
//   per section, packed:
//     bytes 0-1: section_id   uint16
//     bytes 2-5: section_len  uint32
//     bytes 6..: payload[section_len]
//
// Section IDs for v0.4:
//   0x0001 engine_core    — verbatim copy of the v3 8-float param block
//                            (32 bytes; byte-for-byte identical to v3 reader
//                            interpretation; merge-gate test verifies this).
//   0x0002 layout_path    — UTF-8 path (no null terminator). Empty = unset.
//   0x0003 sofa_speh_path — UTF-8 path (no null terminator). Empty = unset.
//   0x0004 binaural_state — 2 bytes: binaural_enable u8, binaural_mode u8.
//                            mode reserved for v0.5; emitted as 0 in v0.4.
// ---------------------------------------------------------------------------
namespace {
static constexpr Steinberg::int32  kStateMagicLegacy = 0x31455053; // 'SPE1' LE
static constexpr Steinberg::int32  kStateMagicV4     = 0x34455053; // 'SPE4' LE
static constexpr Steinberg::int32  kStateBytesV1    = 32;
static constexpr Steinberg::int32  kStateBytesV2    = 36;
static constexpr Steinberg::int32  kStateBytesV3    = 40;
static constexpr Steinberg::uint16 kStateVersionV1  = 1;
static constexpr Steinberg::uint16 kStateVersionV2  = 2;
static constexpr Steinberg::uint16 kStateVersionV3  = 3;
static constexpr Steinberg::uint16 kStateVersionV4  = 4;
static constexpr Steinberg::uint16 kStateNParamsV1  = 6;
static constexpr Steinberg::uint16 kStateNParamsV2  = 7;
static constexpr Steinberg::uint16 kStateNParamsV3  = 8;

// v4 section IDs
static constexpr Steinberg::uint16 kSecEngineCore   = 0x0001;
static constexpr Steinberg::uint16 kSecLayoutPath   = 0x0002;
static constexpr Steinberg::uint16 kSecSofaPath     = 0x0003;
static constexpr Steinberg::uint16 kSecBinauralState = 0x0004;
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

    // v0.4: dispatch on magic. 'SPE4' → walk TLV; 'SPE1' → legacy v1/v2/v3.
    if (magic == kStateMagicV4) {
        Steinberg::uint16 version = 0;
        std::memcpy(&version, buf + 4, 2);
        Steinberg::uint16 section_count = 0;
        std::memcpy(&section_count, buf + 6, 2);

        if (version != kStateVersionV4) {
            // v5+ forward-compat refusal
            std::fprintf(stderr, "VST3 setState: unsupported future v4-family version v=%u\n",
                         (unsigned)version);
            return Steinberg::kResultFalse;
        }

        // Walk sections. Header byte 6/7 already read into section_count.
        // For each section: 2-byte id + 4-byte len + payload.
        for (Steinberg::uint16 si = 0; si < section_count; ++si) {
            uint8_t hdr[6];
            Steinberg::int32 nr = 0;
            r = state->read(hdr, 6, &nr);
            if (r != Steinberg::kResultOk || nr < 6) {
                std::fprintf(stderr, "VST3 setState v4: truncated section header at idx=%u\n",
                             (unsigned)si);
                return Steinberg::kResultOk;
            }
            Steinberg::uint16 sec_id = 0;
            Steinberg::uint32 sec_len = 0;
            std::memcpy(&sec_id, hdr + 0, 2);
            std::memcpy(&sec_len, hdr + 2, 4);

            // Bound payload to a safe limit (paths/headers are small)
            if (sec_len > 65536u) {
                std::fprintf(stderr, "VST3 setState v4: oversized section len=%u, aborting\n",
                             (unsigned)sec_len);
                return Steinberg::kResultOk;
            }

            std::vector<uint8_t> payload(sec_len);
            if (sec_len > 0) {
                nr = 0;
                r = state->read(payload.data(),
                                static_cast<Steinberg::int32>(sec_len), &nr);
                if (r != Steinberg::kResultOk || nr < static_cast<Steinberg::int32>(sec_len)) {
                    std::fprintf(stderr, "VST3 setState v4: truncated section payload sec_id=0x%04x\n",
                                 (unsigned)sec_id);
                    return Steinberg::kResultOk;
                }
            }

            switch (sec_id) {
            case kSecEngineCore: {
                // 8 × float32 = 32 bytes (identical to v3 8-float payload).
                if (sec_len < 32) {
                    std::fprintf(stderr, "VST3 setState v4: engine_core short (%u<32)\n",
                                 (unsigned)sec_len);
                    break;
                }
                for (int i = 0; i < 8; ++i) {
                    float v = 0.f;
                    std::memcpy(&v, payload.data() + i * 4, 4);
                    norm_values_[i].store(v, std::memory_order_relaxed);
                    if (i < 6) {
                        dispatchParamChange(static_cast<Steinberg::Vst::ParamID>(i),
                                            static_cast<Steinberg::Vst::ParamValue>(v));
                    }
                }
                break;
            }
            case kSecLayoutPath: {
                std::string path(reinterpret_cast<const char*>(payload.data()), sec_len);
                if (engine_) engine_->setLayoutPath(path);
                break;
            }
            case kSecSofaPath: {
                std::string path(reinterpret_cast<const char*>(payload.data()), sec_len);
                if (engine_) engine_->setBinauralSofaPath(path);
                break;
            }
            case kSecBinauralState: {
                if (sec_len >= 1) {
                    const bool enable = (payload[0] != 0);
                    if (engine_) engine_->setBinauralEnabled(enable);
                }
                // payload[1] reserved for v0.5 binaural_mode
                break;
            }
            default:
                // Unknown section — skipped (forward-compat per ADR pattern).
                break;
            }
        }

        std::fprintf(stderr, "VST3 setState: v4 loaded (%u sections)\n",
                     (unsigned)section_count);
        return Steinberg::kResultOk;
    }

    if (magic != kStateMagicLegacy) {
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

    // v0.4: emit v4 sectioned TLV.
    //
    // Layout:
    //   header (8 bytes): magic 'SPE4' | version=4 | section_count
    //   sections:
    //     0x0001 engine_core    — 32 bytes (8 × float32 norm values).
    //     0x0002 layout_path    — UTF-8 (engine_->layoutPath()).
    //     0x0003 sofa_speh_path — UTF-8 (engine_->binauralSofaPath()).
    //     0x0004 binaural_state — 2 bytes: enable + mode.

    // Snapshot engine-owned strings/flags on the control thread (safe).
    const std::string layout_path =
        engine_ ? engine_->layoutPath() : std::string{};
    const std::string sofa_path =
        engine_ ? engine_->binauralSofaPath() : std::string{};
    const bool binaural_enable =
        engine_ ? engine_->binauralEnabled() : false;
    const uint8_t binaural_mode = 0;  // reserved for v0.5

    // Header
    Steinberg::int32 magic = kStateMagicV4;
    Steinberg::uint16 version = kStateVersionV4;
    Steinberg::uint16 section_count = 4;

    Steinberg::int32 written_total = 0;
    Steinberg::int32 written = 0;
    Steinberg::tresult r;

    uint8_t hdr[8];
    std::memcpy(hdr + 0, &magic, 4);
    std::memcpy(hdr + 4, &version, 2);
    std::memcpy(hdr + 6, &section_count, 2);
    r = state->write(hdr, 8, &written);
    if (r != Steinberg::kResultOk || written < 8) return Steinberg::kResultFalse;
    written_total += written;

    // ---- helper: emit one section ----
    auto emit_section = [&](Steinberg::uint16 sec_id, const uint8_t* payload,
                            Steinberg::uint32 payload_len) -> bool {
        uint8_t sh[6];
        std::memcpy(sh + 0, &sec_id, 2);
        std::memcpy(sh + 2, &payload_len, 4);
        Steinberg::int32 w = 0;
        if (state->write(sh, 6, &w) != Steinberg::kResultOk || w < 6) return false;
        written_total += w;
        if (payload_len > 0) {
            w = 0;
            if (state->write(const_cast<uint8_t*>(payload),
                              static_cast<Steinberg::int32>(payload_len), &w)
                != Steinberg::kResultOk
                || w < static_cast<Steinberg::int32>(payload_len)) return false;
            written_total += w;
        }
        return true;
    };

    // ---- 0x0001 engine_core (32 bytes; byte-equal to v3 8-float block) ----
    uint8_t core_payload[32];
    for (int i = 0; i < 8; ++i) {
        float v = norm_values_[i].load(std::memory_order_relaxed);
        std::memcpy(core_payload + i * 4, &v, 4);
    }
    if (!emit_section(kSecEngineCore, core_payload, 32))
        return Steinberg::kResultFalse;

    // ---- 0x0002 layout_path ----
    if (!emit_section(kSecLayoutPath,
                      reinterpret_cast<const uint8_t*>(layout_path.data()),
                      static_cast<Steinberg::uint32>(layout_path.size())))
        return Steinberg::kResultFalse;

    // ---- 0x0003 sofa_speh_path ----
    if (!emit_section(kSecSofaPath,
                      reinterpret_cast<const uint8_t*>(sofa_path.data()),
                      static_cast<Steinberg::uint32>(sofa_path.size())))
        return Steinberg::kResultFalse;

    // ---- 0x0004 binaural_state ----
    uint8_t bin_payload[2];
    bin_payload[0] = binaural_enable ? 1u : 0u;
    bin_payload[1] = binaural_mode;
    if (!emit_section(kSecBinauralState, bin_payload, 2))
        return Steinberg::kResultFalse;

    return Steinberg::kResultOk;
}

// ---------------------------------------------------------------------------
// IAudioProcessor
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setBusArrangements(Steinberg::Vst::SpeakerArrangement* /*inputs*/,
                                             Steinberg::int32 /*numIns*/,
                                             Steinberg::Vst::SpeakerArrangement* outputs,
                                             Steinberg::int32 numOuts)
{
    using namespace Steinberg::Vst;

    // v0.4 P1: require both output buses with bus 1 == kStereo.
    if (numOuts < 2 || outputs == nullptr) return Steinberg::kResultFalse;

    const SpeakerArrangement spk_arr = outputs[0];
    const SpeakerArrangement bin_arr = outputs[1];

    // Bus 1 must be exactly kStereo (binaural side-output is always 2-ch).
    if (bin_arr != SpeakerArr::kStereo) return Steinberg::kResultFalse;

    // Bus 0: accept the SDK-defined arrangements whose channel count falls
    // in {2,4,6,8,12,16,24}. We negotiate on channel count rather than
    // matching the exact bitset so DAWs that offer non-standard speaker
    // bundles (e.g. host-defined 8-channel arrangements) still work.
    const Steinberg::int32 ch = SpeakerArr::getChannelCount(spk_arr);
    const bool ok_ch = (ch == 2 || ch == 4 || ch == 6 || ch == 8
                        || ch == 12 || ch == 16 || ch == 24);
    if (!ok_ch) return Steinberg::kResultFalse;

    out_bus0_arr_      = spk_arr;
    out_bus0_channels_ = ch;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getBusArrangement(Steinberg::Vst::BusDirection dir,
                                            Steinberg::int32 index,
                                            Steinberg::Vst::SpeakerArrangement& arr)
{
    using namespace Steinberg::Vst;
    if (dir == kInput) {
        if (index != 0) return Steinberg::kInvalidArgument;
        arr = SpeakerArr::kStereo;
        return Steinberg::kResultOk;
    }
    if (dir == kOutput) {
        if (index == 0) { arr = out_bus0_arr_;          return Steinberg::kResultOk; }
        if (index == 1) { arr = SpeakerArr::kStereo;    return Steinberg::kResultOk; }
    }
    return Steinberg::kInvalidArgument;
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

    // P2: if a layout path was restored from state v4 section 0x0002, apply it
    // BEFORE prepareToPlay so the engine sizes its buffers for the correct
    // speaker count on the very first setup call.
    const std::string& lp = engine_->layoutPath();
    if (!lp.empty()) {
        auto result = spe::geometry::load_layout(lp);
        if (spe::geometry::is_ok(result)) {
            engine_->setLayout(std::get<spe::geometry::SpeakerLayout>(result));
        } else {
            std::fprintf(stderr, "setupProcessing: layout load failed for '%s': %s\n",
                         lp.c_str(), std::get<std::string>(result).c_str());
        }
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
        // Helper: zero all channels of one output bus.
        auto zeroBus = [&](AudioBusBuffers& outBus) {
            for (Steinberg::int32 ch = 0; ch < outBus.numChannels; ++ch) {
                if (outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                    std::memset(outBus.channelBuffers32[ch], 0,
                                static_cast<std::size_t>(data.numSamples) * sizeof(float));
                }
            }
        };

        if (norm_values_[kMute].load(std::memory_order_acquire) >= 0.5f) {
            // kMute=on: zero ALL output buses (speakers + binaural side-bus).
            if (data.outputs) {
                for (Steinberg::int32 b = 0; b < data.numOutputs; ++b) {
                    zeroBus(data.outputs[b]);
                }
            }
        } else if (norm_values_[kBypass].load(std::memory_order_acquire) >= 0.5f) {
            // Bypass: dry pass-through — RT-safe, alloc=0, no mutex.
            // Input → bus 0 (speakers), identity copy on min channels.
            if (data.inputs  && data.numInputs  > 0 &&
                data.outputs && data.numOutputs > 0) {
                const AudioBusBuffers& inBus  = data.inputs[0];
                AudioBusBuffers&       outBus = data.outputs[0];
                const Steinberg::int32 minCh =
                    (inBus.numChannels < outBus.numChannels)
                    ? inBus.numChannels : outBus.numChannels;

                for (Steinberg::int32 ch = 0; ch < minCh; ++ch) {
                    if (inBus.channelBuffers32  && inBus.channelBuffers32[ch] &&
                        outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                        std::memcpy(outBus.channelBuffers32[ch],
                                    inBus.channelBuffers32[ch],
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    }
                }
                for (Steinberg::int32 ch = minCh; ch < outBus.numChannels; ++ch) {
                    if (outBus.channelBuffers32 && outBus.channelBuffers32[ch]) {
                        std::memset(outBus.channelBuffers32[ch], 0,
                                    static_cast<std::size_t>(data.numSamples) * sizeof(float));
                    }
                }
            }
            // Bus 1 (binaural) under bypass: emit -6 dB downmix of the dry
            // bus-0 channels so users still hear a recognisable signal.
            if (data.outputs && data.numOutputs >= 2) {
                writeBinauralPlaceholder(data);
            }
        } else if (active_) {
            // Normal path — drive engine on bus 0 then route bus 1.
            spe::audio_io::AudioBlock block =
                ProcessDataAdapter::adapt(data, sample_rate_);
            engine_->audioBlock(block);
            if (data.outputs && data.numOutputs >= 2) {
                // v0.5 P3: route the engine's binaural buffers to bus 1 when a
                // .speh has been loaded. Falls back to the v0.4 -6 dB downmix
                // placeholder when no HRTF path is active (preserves
                // diagnostic intelligibility).
                writeBinauralBus(data);
            }
        } else {
            // Inactive — make sure bus 1 (if present) is zeroed.
            if (data.outputs && data.numOutputs >= 2) {
                zeroBus(data.outputs[1]);
            }
        }
    }

    return Steinberg::kResultOk;
}

// v0.5 P3: bus 1 routing.
//   * If the engine has a loaded .speh and binaural is enabled, copy the
//     engine's per-block binaural L/R into bus 1.
//   * Otherwise, fall back to the v0.4 -6 dB speaker downmix placeholder
//     (diagnostic intelligibility — see ADR A7).
void SpatialEngineProcessor::writeBinauralBus(
        Steinberg::Vst::ProcessData& data) noexcept
{
    using namespace Steinberg::Vst;
    if (!engine_) { writeBinauralPlaceholder(data); return; }

    const float* engL = engine_->binauralL();
    const float* engR = engine_->binauralR();
    const bool   enabled = engine_->binauralEnabled();
    if (!engL || !engR || !enabled) {
        writeBinauralPlaceholder(data);
        return;
    }

    AudioBusBuffers& bin = data.outputs[1];
    if (bin.numChannels < 2 || !bin.channelBuffers32) return;
    float* binL = bin.channelBuffers32[0];
    float* binR = bin.channelBuffers32[1];
    if (!binL || !binR) return;
    const std::size_t n = static_cast<std::size_t>(data.numSamples);
    std::memcpy(binL, engL, n * sizeof(float));
    std::memcpy(binR, engR, n * sizeof(float));
}

// v0.4 P1 A7: -6 dB speaker→binaural downmix placeholder.
// Until v0.5 wires real BinauralMonitor::process(), bus 1 emits the mono
// sum of bus 0 channels 0+1 (×0.5 = -6 dB). When binaural is disabled
// OR no .speh has been loaded, this is what the user hears on bus 1.
//
// Lives in the VST3 processor (NOT in the engine) so v0.5 can replace
// this branch with a one-line engine call without re-touching processor
// state-machine code.
void SpatialEngineProcessor::writeBinauralPlaceholder(
        Steinberg::Vst::ProcessData& data) noexcept
{
    using namespace Steinberg::Vst;
    AudioBusBuffers& spk = data.outputs[0];
    AudioBusBuffers& bin = data.outputs[1];
    if (bin.numChannels < 2 || !bin.channelBuffers32) return;
    float* binL = bin.channelBuffers32[0];
    float* binR = bin.channelBuffers32[1];
    if (!binL || !binR) return;

    // Speaker channels 0 and 1 form the mono sum. If the speaker bus is
    // mono or empty, fall back to whichever channel is available.
    const float* sL = (spk.numChannels >= 1 && spk.channelBuffers32)
        ? spk.channelBuffers32[0] : nullptr;
    const float* sR = (spk.numChannels >= 2 && spk.channelBuffers32)
        ? spk.channelBuffers32[1] : sL;
    if (!sL) {
        std::memset(binL, 0, static_cast<std::size_t>(data.numSamples) * sizeof(float));
        std::memset(binR, 0, static_cast<std::size_t>(data.numSamples) * sizeof(float));
        return;
    }

    for (Steinberg::int32 n = 0; n < data.numSamples; ++n) {
        const float mix = 0.5f * (sL[n] + (sR ? sR[n] : 0.f));
        binL[n] = mix;
        binR[n] = mix;
    }
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
        // Query IEditController_iid (not the class UID) — the controller's
        // queryInterface only registers interface IIDs, not class IIDs.
        // Cast chain: void* → IEditController* → SpatialEngineController*
        // (safe because SpatialEngineController is the only IEditController
        //  in this binary and the host wires only matching processor/controller
        //  pairs).
        if (other->queryInterface(Steinberg::Vst::IEditController_iid, &ctrl_ptr)
                == Steinberg::kResultOk && ctrl_ptr) {
            auto* as_ctrl = static_cast<Steinberg::Vst::IEditController*>(ctrl_ptr);
            ctrl_for_reverse_path_ = static_cast<SpatialEngineController*>(as_ctrl);
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
