// vst3/SpatialEnginePluginFactory.cpp
// Phase C C2 Option-B — IPluginFactory vtable direct implementation.
// No BEGIN_FACTORY_DEF / DEF_CLASS2 / END_FACTORY macros (absent in vendored
// JUCE 7.0.12 bundle — AM-R3-1 / AM-R4-3 locked).
// NOTE-R3-C: IPluginFactory2/3::queryInterface returns kNotImplemented.

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/fplatform.h"

#include "SpatialEngineProcessor.hpp"
#include "SpatialEngineController.hpp"

#include <atomic>
#include <cstring>

// Pull in IID static definitions (IComponent::iid, IAudioProcessor::iid, etc.)
// These are compiled via vstinitiids.cpp in the CMake source list.

using namespace Steinberg;

// ---------------------------------------------------------------------------
// SpatialEnginePluginFactory
// ---------------------------------------------------------------------------

class SpatialEnginePluginFactory final : public IPluginFactory
{
public:
    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) SMTG_OVERRIDE
    {
        if (!obj) return kInvalidArgument;

        // IPluginFactory
        if (FUnknownPrivate::iidEqual(_iid, IPluginFactory_iid)) {
            addRef();
            *obj = static_cast<IPluginFactory*>(this);
            return kResultOk;
        }
        // FUnknown
        if (FUnknownPrivate::iidEqual(_iid, FUnknown_iid)) {
            addRef();
            *obj = static_cast<IPluginFactory*>(this);
            return kResultOk;
        }

        // NOTE-R3-C: IPluginFactory2 / IPluginFactory3 -> kNotImplemented
        // (out-param must be set to nullptr per FUnknown contract)
        *obj = nullptr;
        return kNotImplemented;
    }

    uint32 PLUGIN_API addRef() SMTG_OVERRIDE  { return ++ref_count_; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        // Singleton: never actually delete — factory lives for module lifetime
        return --ref_count_;
    }

    // IPluginFactory
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) SMTG_OVERRIDE
    {
        if (!info) return kInvalidArgument;
        strncpy8(info->vendor, "Seoul National University / MMHOA",
                 PFactoryInfo::kNameSize);
        strncpy8(info->url,   "https://github.com/paiiek/spatial_engine",
                 PFactoryInfo::kURLSize);
        strncpy8(info->email, "paik402@snu.ac.kr",
                 PFactoryInfo::kEmailSize);
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }

    int32 PLUGIN_API countClasses() SMTG_OVERRIDE
    {
        return 2; // Processor + Controller
    }

    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) SMTG_OVERRIDE
    {
        if (!info || index < 0 || index > 1) return kInvalidArgument;

        memset(info, 0, sizeof(PClassInfo));
        info->cardinality = PClassInfo::kManyInstances;

        if (index == 0) {
            // Processor
            memcpy(info->cid, spe::vst3::kSpatialEngineProcessorUID, sizeof(TUID));
            strncpy8(info->category, kVstAudioEffectClass, PClassInfo::kCategorySize);
            strncpy8(info->name,     "Spatial Engine",     PClassInfo::kNameSize);
        } else {
            // Controller
            memcpy(info->cid, spe::vst3::kSpatialEngineControllerUID, sizeof(TUID));
            strncpy8(info->category, kVstComponentControllerClass, PClassInfo::kCategorySize);
            strncpy8(info->name,     "Spatial Engine Controller", PClassInfo::kNameSize);
        }
        return kResultOk;
    }

    tresult PLUGIN_API createInstance(FIDString cid, FIDString _iid, void** obj) SMTG_OVERRIDE
    {
        if (!cid || !_iid || !obj) return kInvalidArgument;
        *obj = nullptr;

        if (memcmp(cid, spe::vst3::kSpatialEngineProcessorUID, sizeof(TUID)) == 0) {
            auto* proc = new spe::vst3::SpatialEngineProcessor();
            tresult r  = proc->queryInterface(_iid, obj);
            proc->release(); // createInstance hands ownership to caller via queryInterface addRef
            return r;
        }
        if (memcmp(cid, spe::vst3::kSpatialEngineControllerUID, sizeof(TUID)) == 0) {
            auto* ctrl = new spe::vst3::SpatialEngineController();
            tresult r  = ctrl->queryInterface(_iid, obj);
            ctrl->release();
            return r;
        }

        return kNoInterface;
    }

private:
    std::atomic<uint32> ref_count_{1};
};

// ---------------------------------------------------------------------------
// Module entry / exit (Linux host loader hooks — module_linux.cpp convention)
// ---------------------------------------------------------------------------

static SpatialEnginePluginFactory gFactory;

extern "C" SMTG_EXPORT_SYMBOL bool PLUGIN_API ModuleEntry(void* /*sharedLibraryHandle*/)
{
    return true;
}

extern "C" SMTG_EXPORT_SYMBOL bool PLUGIN_API ModuleExit()
{
    return true;
}

extern "C" SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory()
{
    gFactory.addRef();
    return &gFactory;
}
