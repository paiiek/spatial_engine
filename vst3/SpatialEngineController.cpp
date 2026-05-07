// vst3/SpatialEngineController.cpp
// Phase C C2 Option-B — IEditController skeleton.
// Step 2 will add 6-param registration.

#include "SpatialEngineController.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace spe::vst3 {

SpatialEngineController::SpatialEngineController() = default;
SpatialEngineController::~SpatialEngineController() = default;

// ---------------------------------------------------------------------------
// FUnknown
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineController::queryInterface(const Steinberg::TUID _iid, void** obj)
{
    if (!obj) return Steinberg::kInvalidArgument;

    using namespace Steinberg;
    if (FUnknownPrivate::iidEqual(_iid, Vst::IEditController_iid)) {
        addRef();
        *obj = static_cast<Vst::IEditController*>(this);
        return kResultOk;
    }
    if (FUnknownPrivate::iidEqual(_iid, IPluginBase_iid)) {
        addRef();
        *obj = static_cast<Vst::IEditController*>(this);
        return kResultOk;
    }
    if (FUnknownPrivate::iidEqual(_iid, FUnknown_iid)) {
        addRef();
        *obj = static_cast<Vst::IEditController*>(this);
        return kResultOk;
    }

    *obj = nullptr;
    return kNoInterface;
}

Steinberg::uint32 PLUGIN_API SpatialEngineController::addRef()
{
    return ++ref_count_;
}

Steinberg::uint32 PLUGIN_API SpatialEngineController::release()
{
    auto r = --ref_count_;
    if (r == 0) delete this;
    return r;
}

// ---------------------------------------------------------------------------
// IPluginBase
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineController::initialize(Steinberg::FUnknown* /*context*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SpatialEngineController::terminate()
{
    return Steinberg::kResultOk;
}

// ---------------------------------------------------------------------------
// IEditController skeleton — Step 2 implements 6-param wiring
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineController::setComponentState(Steinberg::IBStream* /*state*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::setState(Steinberg::IBStream* /*state*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getState(Steinberg::IBStream* /*state*/)
{
    return Steinberg::kResultOk;
}

Steinberg::int32 PLUGIN_API SpatialEngineController::getParameterCount()
{
    return 0; // Step 2: return 6
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParameterInfo(Steinberg::int32 /*paramIndex*/,
                                           Steinberg::Vst::ParameterInfo& /*info*/)
{
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParamStringByValue(Steinberg::Vst::ParamID /*id*/,
                                                Steinberg::Vst::ParamValue /*valueNormalized*/,
                                                Steinberg::Vst::String128 /*string*/)
{
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParamValueByString(Steinberg::Vst::ParamID /*id*/,
                                                Steinberg::Vst::TChar* /*string*/,
                                                Steinberg::Vst::ParamValue& valueNormalized)
{
    valueNormalized = 0.0;
    return Steinberg::kResultFalse;
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::normalizedParamToPlain(Steinberg::Vst::ParamID /*id*/,
                                                 Steinberg::Vst::ParamValue valueNormalized)
{
    return valueNormalized;
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::plainParamToNormalized(Steinberg::Vst::ParamID /*id*/,
                                                 Steinberg::Vst::ParamValue plainValue)
{
    return plainValue;
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::getParamNormalized(Steinberg::Vst::ParamID /*id*/)
{
    return 0.0;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::setParamNormalized(Steinberg::Vst::ParamID /*id*/,
                                             Steinberg::Vst::ParamValue /*value*/)
{
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::setComponentHandler(Steinberg::Vst::IComponentHandler* /*handler*/)
{
    // Step 2: store handler for performEdit callbacks
    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API
SpatialEngineController::createView(Steinberg::FIDString /*name*/)
{
    // No editor view in Phase C (Phase D6 deferral)
    return nullptr;
}

} // namespace spe::vst3
