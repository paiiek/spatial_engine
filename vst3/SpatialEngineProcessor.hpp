// vst3/SpatialEngineProcessor.hpp
// Phase C C2 Option-B: direct vtable hand-roll.
// No helper base classes (vstcomponent/vstcomponentbase absent from
// vendored JUCE 7.0.12 bundle transitive path — AM-R4-4).
//
// IIDs are DEV-prefixed (first 4 bytes 'D','E','V','0') per C2-Q14 decision.
// Replace with registered IIDs in Phase D6.
#pragma once

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"  // IConnectionPoint

#include <atomic>
#include <memory>

namespace spe::core { class SpatialEngine; }

#ifdef SPATIAL_ENGINE_VST3_OSC
#include "AudioCommand.h"
#include "util/SpscRing.h"  // canonical C1.b ring (core/src/util/SpscRing.h)
namespace spe::vst3 {
class SpatialEnginePluginUdp;
class SpatialEngineController;  // S4: forward-decl for reverse-path pointer
}
#endif

namespace spe::vst3 {

// DEV-prefix IID (first word = 0x44455630 = 'D','E','V','0')
// Generated from /dev/urandom with DEV0 marker in bytes 0-3.
static constexpr Steinberg::TUID kSpatialEngineProcessorUID = {
    'D','E','V','0',
    (Steinberg::int8)0xA1,(Steinberg::int8)0xB2,(Steinberg::int8)0xC3,(Steinberg::int8)0xD4,
    (Steinberg::int8)0xE5,(Steinberg::int8)0xF6,(Steinberg::int8)0x01,(Steinberg::int8)0x12,
    (Steinberg::int8)0x23,(Steinberg::int8)0x34,(Steinberg::int8)0x45,(Steinberg::int8)0x56
};

// Param IDs for 8 parameters (C4-S7: kMute activated; kBypass added in C2B postmortem S1/S3)
enum ParamId : Steinberg::Vst::ParamID {
    kPanAz        = 0,
    kPanEl        = 1,
    kSourceWidth  = 2,
    kMasterGain   = 3,
    kAmbiOrder    = 4,
    kRoomPreset   = 5,
    kBypass       = 6,
    kMute         = 7,  // C4-S7: audio output zero override (distinct from kBypass dry pass-through)
};

static constexpr int kNumParams = 8;

class SpatialEngineProcessor
    : public Steinberg::Vst::IComponent
    , public Steinberg::Vst::IAudioProcessor
    , public Steinberg::Vst::IConnectionPoint
{
public:
    SpatialEngineProcessor();
    virtual ~SpatialEngineProcessor();

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API addRef()  SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API release() SMTG_OVERRIDE;

    // IPluginBase
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

    // IComponent
    Steinberg::tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) SMTG_OVERRIDE;
    Steinberg::int32   PLUGIN_API getBusCount(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getBusInfo(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index, Steinberg::Vst::BusInfo& bus) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                                                  Steinberg::Vst::RoutingInfo& outInfo) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
                                               Steinberg::int32 index, Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    // IAudioProcessor
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                      Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs,
                                                      Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getBusArrangement(Steinberg::Vst::BusDirection dir,
                                                     Steinberg::int32 index,
                                                     Steinberg::Vst::SpeakerArrangement& arr) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API getTailSamples() SMTG_OVERRIDE;

    // IConnectionPoint (AM-R3-10: notify returns kNotImplemented)
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // Test-only accessor: allows VST3 integration tests to inspect engine state
    // without going through the public VST3 parameter API.
    spe::core::SpatialEngine& engine() noexcept { return *engine_; }
    const spe::core::SpatialEngine& engine() const noexcept { return *engine_; }

private:
    std::atomic<Steinberg::uint32> ref_count_{1};
    std::unique_ptr<spe::core::SpatialEngine> engine_;
    double sample_rate_{48000.0};
    Steinberg::int32 max_block_{512};
    bool active_{false};

    // v0.4 P1: two output buses (speakers + binaural).
    //   bus 0 "Speakers"  — channel count = out_bus0_channels_ (default 8;
    //                       negotiated via setBusArrangements).
    //   bus 1 "Binaural"  — always kStereo (2 channels).
    // The negotiated bus 0 arrangement is cached so getBusArrangement
    // returns the host-selected SpeakerArrangement without re-deriving it.
    Steinberg::Vst::SpeakerArrangement out_bus0_arr_{Steinberg::Vst::SpeakerArr::kStereo};
    Steinberg::int32                   out_bus0_channels_{2};
    // Default bus 0 channel count when no setBusArrangements has been
    // called yet (matches the host's "best guess" for first-instantiation
    // before parameter routing). Engine still defaults to 8 internally.

    // Atomic norm value snapshot for 8 params (C4-S7: resized from [7] to [8]).
    // norm_values_[6] = bypass (0.0=off, 1.0=on). Read directly in process().
    // norm_values_[7] = kMute (0.0=off, 1.0=on). Zeroes output when >= 0.5.
    // bypass_active_ removed in C2B postmortem S3 — norm_values_[6] is the single source of truth.
    std::atomic<float> norm_values_[8]{};

    // Component ↔ Controller connection peer (host manages lifetime)
    Steinberg::Vst::IConnectionPoint* peer_{nullptr};

#ifdef SPATIAL_ENGINE_VST3_OSC
    // S4: direct pointer to the controller for reverse-path pushParamEdit wiring.
    // Resolved in connect() via queryInterface for SpatialEngineController.
    // Non-owning; host manages the controller lifetime.
    SpatialEngineController* ctrl_for_reverse_path_{nullptr};
#endif

#ifdef SPATIAL_ENGINE_VST3_OSC
    // Audio-path SPSC ring: UDP thread (producer) → audio thread (consumer).
    // Capacity 1024: covers ~10 s of 64-object × 1 Hz traffic.
    // Owned by the processor; udp_io_ holds a pointer (not ownership).
    spe::util::SpscRing<AudioCommand, 1024> osc_cmd_ring_;
    std::unique_ptr<SpatialEnginePluginUdp> udp_io_;
#endif

    // Dispatch a single normalized param change to the engine (Step 2.3/2.4)
    void dispatchParamChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue norm) noexcept;

    // v0.4 P1 A7: write -6 dB speaker→binaural downmix to bus 1.
    // RT-safe: no allocations, no mutex.
    void writeBinauralPlaceholder(Steinberg::Vst::ProcessData& data) noexcept;

    // v0.5 P3: write bus 1 by copying the engine's binaural buffer; falls
    // back to the v0.4 placeholder when no .speh is loaded or binaural
    // is disabled.
    void writeBinauralBus(Steinberg::Vst::ProcessData& data) noexcept;
};

} // namespace spe::vst3
