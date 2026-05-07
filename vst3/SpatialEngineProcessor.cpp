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

#include <cstring>
#include <cmath>

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
    if (data.inputParameterChanges) {
        const Steinberg::int32 numQueues = data.inputParameterChanges->getParameterCount();
        for (Steinberg::int32 q = 0; q < numQueues; ++q) {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;

            const ParamID paramId  = queue->getParameterId();
            const Steinberg::int32 numPts = queue->getPointCount();
            if (numPts <= 0) continue;

            // Last point = block-rate value
            Steinberg::int32  sampleOffset = 0;
            ParamValue        normValue    = 0.0;
            if (queue->getPoint(numPts - 1, sampleOffset, normValue) != Steinberg::kResultOk) continue;

            // Dispatch to engine (skeleton wiring — Step 2 completes this)
            spe::ipc::Command cmd;
            cmd.schema_version = spe::ipc::SCHEMA_VERSION;

            switch (paramId) {
                case kPanAz: {
                    // norm [0,1] -> az_rad [-pi, pi]
                    spe::ipc::PayloadObjMove p;
                    p.obj_id = 0;
                    p.az_rad = static_cast<float>((normValue - 0.5) * 2.0 * 3.14159265358979);
                    p.el_rad = 0.f;
                    p.dist_m = 1.f;
                    cmd.tag     = spe::ipc::CommandTag::ObjMove;
                    cmd.payload = p;
                    engine_->dispatchCommand(cmd);
                    break;
                }
                case kPanEl: {
                    spe::ipc::PayloadObjMove p;
                    p.obj_id = 0;
                    p.az_rad = 0.f;
                    p.el_rad = static_cast<float>((normValue - 0.5) * 3.14159265358979);
                    p.dist_m = 1.f;
                    cmd.tag     = spe::ipc::CommandTag::ObjMove;
                    cmd.payload = p;
                    engine_->dispatchCommand(cmd);
                    break;
                }
                case kSourceWidth: {
                    spe::ipc::PayloadObjDsp p;
                    p.obj_id = 0;
                    p.param  = spe::ipc::PayloadObjDsp::Param::Width;
                    p.value  = static_cast<float>(normValue * 3.14159265358979); // [0, pi]
                    cmd.tag     = spe::ipc::CommandTag::ObjDsp;
                    cmd.payload = p;
                    engine_->dispatchCommand(cmd);
                    break;
                }
                case kMasterGain: {
                    // norm [0,1] -> gain linear (0dB at norm=1, -60dB at norm=0)
                    spe::ipc::PayloadObjGain p;
                    p.obj_id = 0;
                    p.gain   = static_cast<float>(normValue); // linear for now
                    cmd.tag     = spe::ipc::CommandTag::ObjGain;
                    cmd.payload = p;
                    engine_->dispatchCommand(cmd);
                    break;
                }
                case kAmbiOrder: {
                    spe::ipc::PayloadSysAmbiOrder p;
                    // norm [0,1] -> order {1,2,3}
                    p.order = static_cast<uint8_t>(1 + static_cast<int>(normValue * 2.999));
                    cmd.tag     = spe::ipc::CommandTag::SysAmbiOrder;
                    cmd.payload = p;
                    engine_->dispatchCommand(cmd);
                    break;
                }
                case kRoomPreset: {
                    // Phase D6 deferral — MVP passthrough (no-op)
                    break;
                }
                default:
                    break;
            }
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

} // namespace spe::vst3
