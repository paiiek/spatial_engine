// vst3/SpatialEngineController.hpp
// Phase C C2 Option-B — IEditController full implementation (Step 2).
// option-β: no SDK helper base classes. ParameterInfo structs held directly.
// IConnectionPoint: connect/disconnect stored, notify returns kNotImplemented (AM-R3-10).
#pragma once

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"  // IConnectionPoint

#include <atomic>
#include <cstring>

namespace spe::vst3 {

// DEV-prefix IID (first 4 bytes = 'D','E','V','1')
static constexpr Steinberg::TUID kSpatialEngineControllerUID = {
    'D','E','V','1',
    (Steinberg::int8)0xB2,(Steinberg::int8)0xC3,(Steinberg::int8)0xD4,(Steinberg::int8)0xE5,
    (Steinberg::int8)0xF6,(Steinberg::int8)0x01,(Steinberg::int8)0x12,(Steinberg::int8)0x23,
    (Steinberg::int8)0x34,(Steinberg::int8)0x45,(Steinberg::int8)0x56,(Steinberg::int8)0x67
};

static constexpr int kParamCount = 7;

class SpatialEngineController
    : public Steinberg::Vst::IEditController
    , public Steinberg::Vst::IConnectionPoint
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

    // IEditController
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

    // IConnectionPoint (AM-R3-10: notify returns kNotImplemented)
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

private:
    std::atomic<Steinberg::uint32> ref_count_{1};
    Steinberg::Vst::IComponentHandler* comp_handler_{nullptr};
    Steinberg::Vst::IConnectionPoint*  peer_{nullptr};

    // 6 normalized values (UI-thread; host serialises access via setParamNormalized)
    double norm_values_[kParamCount]{};

    // Static ParameterInfo table — built in initialize()
    Steinberg::Vst::ParameterInfo param_infos_[kParamCount]{};
    bool initialized_{false};

    void buildParamInfos();

    // master_gain skew helpers (NormalisableRange::setSkewForCentre logic)
    // centre_plain = 0dB, range [-60, 6]
    static double gainNormToPlain(double norm) noexcept;
    static double gainPlainToNorm(double plain) noexcept;
};

} // namespace spe::vst3
