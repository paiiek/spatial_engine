// vst3/SpatialEngineController.hpp
// Phase C C2 Option-B — IEditController skeleton.
// Step 1: initialize/terminate/getParameterCount=0 only.
// Step 2: 6-param registration and IComponentHandler wiring.
#pragma once

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>

namespace spe::vst3 {

// DEV-prefix IID (first 4 bytes = 'D','E','V','1')
static constexpr Steinberg::TUID kSpatialEngineControllerUID = {
    'D','E','V','1',
    (Steinberg::int8)0xB2,(Steinberg::int8)0xC3,(Steinberg::int8)0xD4,(Steinberg::int8)0xE5,
    (Steinberg::int8)0xF6,(Steinberg::int8)0x01,(Steinberg::int8)0x12,(Steinberg::int8)0x23,
    (Steinberg::int8)0x34,(Steinberg::int8)0x45,(Steinberg::int8)0x56,(Steinberg::int8)0x67
};

class SpatialEngineController : public Steinberg::Vst::IEditController
{
public:
    SpatialEngineController();
    virtual ~SpatialEngineController();

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API addRef()  SMTG_OVERRIDE;
    Steinberg::uint32  PLUGIN_API release() SMTG_OVERRIDE;

    // IPluginBase
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

    // IEditController — Step 1 skeleton
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::int32   PLUGIN_API getParameterCount() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParameterInfo(Steinberg::int32 paramIndex,
                                                    Steinberg::Vst::ParameterInfo& info) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID id,
                                                         Steinberg::Vst::ParamValue valueNormalized,
                                                         Steinberg::Vst::String128 string) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID id,
                                                         Steinberg::Vst::TChar* string,
                                                         Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;
    Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain(Steinberg::Vst::ParamID id,
                                                                   Steinberg::Vst::ParamValue valueNormalized) SMTG_OVERRIDE;
    Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized(Steinberg::Vst::ParamID id,
                                                                   Steinberg::Vst::ParamValue plainValue) SMTG_OVERRIDE;
    Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID id) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID id,
                                                      Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentHandler(Steinberg::Vst::IComponentHandler* handler) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    std::atomic<Steinberg::uint32> ref_count_{1};
};

} // namespace spe::vst3
