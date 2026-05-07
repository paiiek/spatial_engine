// vst3/SpatialEngineProcessor.cpp
// Phase C C2 Option-B: IComponent + IAudioProcessor direct vtable.
// No helper base classes. FUnknown implemented with atomic ref-count.

#include "SpatialEngineProcessor.hpp"
#include "SpatialEngineProcessData.hpp"
#include "SpatialEngineController.hpp"

#include "core/SpatialEngine.h"
#include "core/Constants.h"
#include "ipc/Command.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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
    } else if (!state && active_) {
        engine_->releaseResources();
        active_ = false;
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::setState(Steinberg::IBStream* /*state*/)
{
    // Step 3: full state restore
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::getState(Steinberg::IBStream* /*state*/)
{
    // Step 3: full state persist
    return Steinberg::kResultOk;
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

            // Step 2.4: atomic store before dispatch (stale reads by other params use this)
            if (paramId < 6u) {
                norm_values_[paramId].store(static_cast<float>(normValue),
                                            std::memory_order_relaxed);
            }

            dispatchParamChange(paramId, normValue);
        }
    }

    // --- Audio: adapt VST3 buffers -> AudioBlock -> engine ---
    if (data.numSamples > 0 && active_) {
        spe::audio_io::AudioBlock block =
            ProcessDataAdapter::adapt(data, sample_rate_);
        engine_->audioBlock(block);
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
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::disconnect(Steinberg::Vst::IConnectionPoint* other)
{
    if (peer_ == other) peer_ = nullptr;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineProcessor::notify(Steinberg::Vst::IMessage* /*message*/)
{
    // IConnectionPoint::notify channel not used (AM-R3-10 decision).
    return Steinberg::kNotImplemented;
}

} // namespace spe::vst3
