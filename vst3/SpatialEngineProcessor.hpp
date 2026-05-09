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

namespace spe::vst3 {

// DEV-prefix IID (first word = 0x44455630 = 'D','E','V','0')
// Generated from /dev/urandom with DEV0 marker in bytes 0-3.
static constexpr Steinberg::TUID kSpatialEngineProcessorUID = {
    'D','E','V','0',
    (Steinberg::int8)0xA1,(Steinberg::int8)0xB2,(Steinberg::int8)0xC3,(Steinberg::int8)0xD4,
    (Steinberg::int8)0xE5,(Steinberg::int8)0xF6,(Steinberg::int8)0x01,(Steinberg::int8)0x12,
    (Steinberg::int8)0x23,(Steinberg::int8)0x34,(Steinberg::int8)0x45,(Steinberg::int8)0x56
};

// Param IDs for 7 parameters (Step 2 wiring; kBypass added in C2B postmortem S1/S3)
enum ParamId : Steinberg::Vst::ParamID {
    kPanAz        = 0,
    kPanEl        = 1,
    kSourceWidth  = 2,
    kMasterGain   = 3,
    kAmbiOrder    = 4,
    kRoomPreset   = 5,
    kBypass       = 6,
};

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

private:
    std::atomic<Steinberg::uint32> ref_count_{1};
    std::unique_ptr<spe::core::SpatialEngine> engine_;
    double sample_rate_{48000.0};
    Steinberg::int32 max_block_{512};
    bool active_{false};

    // Atomic norm value snapshot for 7 params (Step 2.4 — RT-safe audio thread reads)
    // norm_values_[6] = bypass (0.0=off, 1.0=on). Read directly in process().
    // bypass_active_ removed in C2B postmortem S3 — norm_values_[6] is the single source of truth.
    std::atomic<float> norm_values_[7]{};

    // Component ↔ Controller connection peer (host manages lifetime)
    Steinberg::Vst::IConnectionPoint* peer_{nullptr};

    // Dispatch a single normalized param change to the engine (Step 2.3/2.4)
    void dispatchParamChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue norm) noexcept;
};

} // namespace spe::vst3
