// vst3/SpatialEngineController.cpp
// Phase C C2 Option-B — IEditController full implementation (Step 2).
// option-β: no SDK helper classes. ParameterInfo structs built manually.
// master_gain skew: NormalisableRange::setSkewForCentre(0dB) logic re-implemented.

#include "SpatialEngineController.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// UString128 helper: write ASCII string into Steinberg::Vst::String128
// (String128 = Steinberg::char16[128], UTF-16LE on all platforms supported)
// ---------------------------------------------------------------------------
static void asciiToStr128(Steinberg::Vst::String128 dst, const char* src)
{
    int i = 0;
    while (src[i] && i < 127) {
        dst[i] = static_cast<Steinberg::char16>(src[i]);
        ++i;
    }
    dst[i] = 0;
}

// Compare a Steinberg::Vst::TChar* string against an ASCII literal.
static bool str128EqAscii(const Steinberg::Vst::TChar* s, const char* ascii)
{
    int i = 0;
    while (ascii[i]) {
        if (static_cast<char>(s[i]) != ascii[i]) return false;
        ++i;
    }
    return s[i] == 0;
}

namespace spe::vst3 {

// ---------------------------------------------------------------------------
// master_gain skew: centre = 0 dB, range [-60, 6] dB
// NormalisableRange::setSkewForCentre(centre):
//   skewFactor = std::log(0.5) / std::log((centre - min) / (max - min))
//   normToPlain(norm) = min + (max - min) * std::pow(norm, 1.0/skewFactor)
//   plainToNorm(plain) = std::pow((plain - min)/(max - min), skewFactor)
// ---------------------------------------------------------------------------
static constexpr double kGainMin    = -60.0;
static constexpr double kGainMax    =   6.0;
static constexpr double kGainCentre =   0.0;

static double computeSkew()
{
    // skewFactor = log(0.5) / log((centre - min) / (max - min))
    double ratio = (kGainCentre - kGainMin) / (kGainMax - kGainMin);
    return std::log(0.5) / std::log(ratio);
}

// Skew factor is constant; compute once at static init.
static const double kGainSkew = computeSkew();

double SpatialEngineController::gainNormToPlain(double norm) noexcept
{
    // norm in [0,1] -> plain in [-60, 6]
    if (norm <= 0.0) return kGainMin;
    if (norm >= 1.0) return kGainMax;
    return kGainMin + (kGainMax - kGainMin) * std::pow(norm, 1.0 / kGainSkew);
}

double SpatialEngineController::gainPlainToNorm(double plain) noexcept
{
    // plain in [-60, 6] -> norm in [0,1]
    if (plain <= kGainMin) return 0.0;
    if (plain >= kGainMax) return 1.0;
    return std::pow((plain - kGainMin) / (kGainMax - kGainMin), kGainSkew);
}

// ---------------------------------------------------------------------------
// Param layout constants
// ---------------------------------------------------------------------------
static constexpr double kPi      = 3.14159265358979323846;
static constexpr double kHalfPi  = kPi * 0.5;

// ambi_order choices: "1", "2", "3"  (stepCount = 2 → 3 discrete steps)
// room_preset_idx choices: "Dry","Small","Medium","Large"  (stepCount = 3 → 4)

// Default norm values matching plain defaults:
//   pan_az=0   -> norm=0.5
//   pan_el=0   -> norm=0.5
//   source_width=0 -> norm=0.0
//   master_gain=0dB -> gainPlainToNorm(0) computed at runtime
//   ambi_order=first (0) -> norm=0
//   room_preset_idx=first (0) -> norm=0

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
SpatialEngineController::SpatialEngineController() = default;
SpatialEngineController::~SpatialEngineController() = default;

// ---------------------------------------------------------------------------
// buildParamInfos — called once in initialize()
// ---------------------------------------------------------------------------
void SpatialEngineController::buildParamInfos()
{
    using namespace Steinberg::Vst;

    // --- 0: pan_az [-pi, pi], default 0 (norm=0.5) ---
    {
        ParameterInfo& p = param_infos_[0];
        std::memset(&p, 0, sizeof(p));
        p.id = 0;
        asciiToStr128(p.title,      "Pan Azimuth");
        asciiToStr128(p.shortTitle, "Az");
        asciiToStr128(p.units,      "rad");
        p.stepCount              = 0;       // continuous
        p.defaultNormalizedValue = 0.5;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate;
        norm_values_[0]          = 0.5;
    }

    // --- 1: pan_el [-pi/2, pi/2], default 0 (norm=0.5) ---
    {
        ParameterInfo& p = param_infos_[1];
        std::memset(&p, 0, sizeof(p));
        p.id = 1;
        asciiToStr128(p.title,      "Pan Elevation");
        asciiToStr128(p.shortTitle, "El");
        asciiToStr128(p.units,      "rad");
        p.stepCount              = 0;
        p.defaultNormalizedValue = 0.5;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate;
        norm_values_[1]          = 0.5;
    }

    // --- 2: source_width [0, pi], default 0 (norm=0.0) ---
    {
        ParameterInfo& p = param_infos_[2];
        std::memset(&p, 0, sizeof(p));
        p.id = 2;
        asciiToStr128(p.title,      "Source Width");
        asciiToStr128(p.shortTitle, "Width");
        asciiToStr128(p.units,      "rad");
        p.stepCount              = 0;
        p.defaultNormalizedValue = 0.0;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate;
        norm_values_[2]          = 0.0;
    }

    // --- 3: master_gain [-60, 6] dB, default 0dB (norm = gainPlainToNorm(0)) ---
    {
        ParameterInfo& p = param_infos_[3];
        std::memset(&p, 0, sizeof(p));
        p.id = 3;
        asciiToStr128(p.title,      "Master Gain");
        asciiToStr128(p.shortTitle, "Gain");
        asciiToStr128(p.units,      "dB");
        p.stepCount              = 0;
        p.defaultNormalizedValue = gainPlainToNorm(0.0);
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate;
        norm_values_[3]          = p.defaultNormalizedValue;
    }

    // --- 4: ambi_order, choice ["1","2","3"], default 0 (norm=0), stepCount=2 ---
    {
        ParameterInfo& p = param_infos_[4];
        std::memset(&p, 0, sizeof(p));
        p.id = 4;
        asciiToStr128(p.title,      "Ambi Order");
        asciiToStr128(p.shortTitle, "Order");
        asciiToStr128(p.units,      "");
        p.stepCount              = 2;       // 3 choices: 0/stepCount=0, 1/2, 2/2=1
        p.defaultNormalizedValue = 0.0;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate | ParameterInfo::kIsList;
        norm_values_[4]          = 0.0;
    }

    // --- 5: room_preset_idx, choice ["Dry","Small","Medium","Large"],
    //        default 0 (norm=0), stepCount=3 ---
    {
        ParameterInfo& p = param_infos_[5];
        std::memset(&p, 0, sizeof(p));
        p.id = 5;
        asciiToStr128(p.title,      "Room Preset");
        asciiToStr128(p.shortTitle, "Room");
        asciiToStr128(p.units,      "");
        p.stepCount              = 3;       // 4 choices
        p.defaultNormalizedValue = 0.0;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate | ParameterInfo::kIsList;
        norm_values_[5]          = 0.0;
    }

    // --- 6: bypass, stepCount=1 (toggle), kIsBypass | kCanAutomate ---
    {
        ParameterInfo& p = param_infos_[6];
        std::memset(&p, 0, sizeof(p));
        p.id = 6;
        asciiToStr128(p.title,      "Bypass");
        asciiToStr128(p.shortTitle, "Byp");
        asciiToStr128(p.units,      "");
        p.stepCount              = 1;       // toggle
        p.defaultNormalizedValue = 0.0;
        p.unitId                 = 0;
        p.flags                  = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass;
        norm_values_[6]          = 0.0;
    }
}

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
    if (FUnknownPrivate::iidEqual(_iid, Vst::IConnectionPoint_iid)) {
        addRef();
        *obj = static_cast<Vst::IConnectionPoint*>(this);
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
    if (!initialized_) {
        buildParamInfos();
        initialized_ = true;
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API SpatialEngineController::terminate()
{
    comp_handler_ = nullptr;
    peer_         = nullptr;
    return Steinberg::kResultOk;
}

// ---------------------------------------------------------------------------
// IEditController
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// State persistence — Step 3.3
// Same 32-byte binary format as SpatialEngineProcessor (magic 'SPE1', v1, 6 floats).
// Controller decodes and reflects into norm_values_ (UI thread, no performEdit).
// ---------------------------------------------------------------------------
namespace {
static constexpr Steinberg::int32  kCtlStateMagic    = 0x31455053; // 'SPE1' LE
static constexpr Steinberg::uint16 kCtlStateVersionV1 = 1;
static constexpr Steinberg::uint16 kCtlStateVersionV2 = 2;
static constexpr Steinberg::uint16 kCtlStateNParamsV1 = 6;
static constexpr Steinberg::uint16 kCtlStateNParamsV2 = 7;
static constexpr Steinberg::int32  kCtlStateBytesV1   = 32;
static constexpr Steinberg::int32  kCtlStateBytesV2   = 36;
} // namespace

Steinberg::tresult PLUGIN_API
SpatialEngineController::setComponentState(Steinberg::IBStream* state)
{
    if (!state) return Steinberg::kInvalidArgument;

    // Phase 1: header read (8 bytes)
    uint8_t buf[kCtlStateBytesV2];
    Steinberg::int32 numRead = 0;
    Steinberg::tresult r = state->read(buf, 8, &numRead);
    if (r != Steinberg::kResultOk || numRead < 8) {
        std::fprintf(stderr, "VST3 controller setComponentState: short header read, defaults retained\n");
        return Steinberg::kResultOk;
    }

    Steinberg::int32 magic = 0;
    std::memcpy(&magic, buf + 0, 4);
    if (magic != kCtlStateMagic) {
        std::fprintf(stderr, "VST3 controller setComponentState: magic mismatch, defaults retained\n");
        return Steinberg::kResultOk;
    }

    Steinberg::uint16 version = 0;
    std::memcpy(&version, buf + 4, 2);
    Steinberg::uint16 nparams = 0;
    std::memcpy(&nparams, buf + 6, 2);

    // Phase 2: payload read branched on version
    if (version == kCtlStateVersionV1 && nparams == kCtlStateNParamsV1) {
        // v1: read 24 bytes (6 floats)
        numRead = 0;
        r = state->read(buf + 8, 24, &numRead);
        if (r != Steinberg::kResultOk || numRead < 24) {
            std::fprintf(stderr, "VST3 controller setComponentState: v1 payload short read, defaults retained\n");
            return Steinberg::kResultOk;
        }
        for (int i = 0; i < 6; ++i) {
            float v = 0.f;
            std::memcpy(&v, buf + 8 + i * 4, 4);
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            norm_values_[i] = static_cast<double>(v);
        }
        norm_values_[6] = 0.0; // v1 fallback: bypass off
    } else if (version == kCtlStateVersionV2 && nparams == kCtlStateNParamsV2) {
        // v2: read 28 bytes (7 floats)
        numRead = 0;
        r = state->read(buf + 8, 28, &numRead);
        if (r != Steinberg::kResultOk || numRead < 28) {
            std::fprintf(stderr, "VST3 controller setComponentState: v2 payload short read, defaults retained\n");
            return Steinberg::kResultOk;
        }
        for (int i = 0; i < 7; ++i) {
            float v = 0.f;
            std::memcpy(&v, buf + 8 + i * 4, 4);
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            norm_values_[i] = static_cast<double>(v);
        }
    } else {
        std::fprintf(stderr, "VST3 controller setComponentState: version/nparams mismatch (v=%u n=%u), defaults retained\n",
                     (unsigned)version, (unsigned)nparams);
        return Steinberg::kResultOk;
    }

    // Phase 3 (Round-2 A7): notify host to refresh parameter automation cache
    if (comp_handler_) {
        comp_handler_->restartComponent(Steinberg::Vst::kParamValuesChanged);
    }

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
    return kParamCount;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParameterInfo(Steinberg::int32 paramIndex,
                                           Steinberg::Vst::ParameterInfo& info)
{
    if (paramIndex < 0 || paramIndex >= kParamCount)
        return Steinberg::kInvalidArgument;
    info = param_infos_[paramIndex];
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParamStringByValue(Steinberg::Vst::ParamID id,
                                                Steinberg::Vst::ParamValue valueNormalized,
                                                Steinberg::Vst::String128 string)
{
    using namespace Steinberg::Vst;
    char buf[64] = {};

    switch (id) {
        case 0: { // pan_az: norm [0,1] -> az_rad [-pi, pi]
            double plain = (valueNormalized - 0.5) * 2.0 * kPi;
            snprintf(buf, sizeof(buf), "%.3f", plain);
            break;
        }
        case 1: { // pan_el: norm [0,1] -> el_rad [-pi/2, pi/2]
            double plain = (valueNormalized - 0.5) * kPi;
            snprintf(buf, sizeof(buf), "%.3f", plain);
            break;
        }
        case 2: { // source_width: norm [0,1] -> [0, pi]
            double plain = valueNormalized * kPi;
            snprintf(buf, sizeof(buf), "%.3f", plain);
            break;
        }
        case 3: { // master_gain: skewed dB
            double plain = gainNormToPlain(valueNormalized);
            snprintf(buf, sizeof(buf), "%.1f", plain);
            break;
        }
        case 4: { // ambi_order: choice 0..2 -> "1","2","3"
            static const char* kOrders[3] = {"1", "2", "3"};
            int idx = static_cast<int>(valueNormalized * 2.0 + 0.5);
            if (idx < 0) idx = 0;
            if (idx > 2) idx = 2;
            snprintf(buf, sizeof(buf), "%s", kOrders[idx]);
            break;
        }
        case 5: { // room_preset_idx: choice 0..3 -> names
            static const char* kRooms[4] = {"Dry", "Small", "Medium", "Large"};
            int idx = static_cast<int>(valueNormalized * 3.0 + 0.5);
            if (idx < 0) idx = 0;
            if (idx > 3) idx = 3;
            snprintf(buf, sizeof(buf), "%s", kRooms[idx]);
            break;
        }
        case 6: { // bypass: toggle
            snprintf(buf, sizeof(buf), "%s", valueNormalized >= 0.5 ? "On" : "Off");
            break;
        }
        default:
            return Steinberg::kInvalidArgument;
    }

    asciiToStr128(string, buf);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::getParamValueByString(Steinberg::Vst::ParamID id,
                                                Steinberg::Vst::TChar* string,
                                                Steinberg::Vst::ParamValue& valueNormalized)
{
    switch (id) {
        case 4: { // ambi_order
            if (str128EqAscii(string, "1"))      { valueNormalized = 0.0;  return Steinberg::kResultOk; }
            if (str128EqAscii(string, "2"))      { valueNormalized = 0.5;  return Steinberg::kResultOk; }
            if (str128EqAscii(string, "3"))      { valueNormalized = 1.0;  return Steinberg::kResultOk; }
            return Steinberg::kInvalidArgument;
        }
        case 5: { // room_preset_idx
            if (str128EqAscii(string, "Dry"))    { valueNormalized = 0.0;         return Steinberg::kResultOk; }
            if (str128EqAscii(string, "Small"))  { valueNormalized = 1.0 / 3.0;   return Steinberg::kResultOk; }
            if (str128EqAscii(string, "Medium")) { valueNormalized = 2.0 / 3.0;   return Steinberg::kResultOk; }
            if (str128EqAscii(string, "Large"))  { valueNormalized = 1.0;         return Steinberg::kResultOk; }
            return Steinberg::kInvalidArgument;
        }
        case 6: { // bypass
            if (str128EqAscii(string, "On"))     { valueNormalized = 1.0;  return Steinberg::kResultOk; }
            if (str128EqAscii(string, "Off"))    { valueNormalized = 0.0;  return Steinberg::kResultOk; }
            return Steinberg::kInvalidArgument;
        }
        default:
            // For continuous params, we don't parse strings in MVP (Phase D6 deferral).
            valueNormalized = 0.0;
            return Steinberg::kResultFalse;
    }
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::normalizedParamToPlain(Steinberg::Vst::ParamID id,
                                                 Steinberg::Vst::ParamValue valueNormalized)
{
    switch (id) {
        case 0: return (valueNormalized - 0.5) * 2.0 * kPi;      // [-pi, pi]
        case 1: return (valueNormalized - 0.5) * kPi;             // [-pi/2, pi/2]
        case 2: return valueNormalized * kPi;                      // [0, pi]
        case 3: return gainNormToPlain(valueNormalized);           // [-60, 6] dB skewed
        case 4: return static_cast<double>(
                    static_cast<int>(valueNormalized * 2.0 + 0.5)); // 0,1,2 -> order index
        case 5: return static_cast<double>(
                    static_cast<int>(valueNormalized * 3.0 + 0.5)); // 0,1,2,3 -> preset index
        case 6: return valueNormalized >= 0.5 ? 1.0 : 0.0; // bypass toggle
        default: return valueNormalized;
    }
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::plainParamToNormalized(Steinberg::Vst::ParamID id,
                                                 Steinberg::Vst::ParamValue plainValue)
{
    switch (id) {
        case 0: { // [-pi, pi] -> [0,1]
            double v = (plainValue / (2.0 * kPi)) + 0.5;
            return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        }
        case 1: { // [-pi/2, pi/2] -> [0,1]
            double v = (plainValue / kPi) + 0.5;
            return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        }
        case 2: { // [0, pi] -> [0,1]
            double v = plainValue / kPi;
            return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        }
        case 3: return gainPlainToNorm(plainValue);   // dB skew
        case 4: { // order index 0..2 -> [0,1]
            int idx = static_cast<int>(plainValue);
            return idx <= 0 ? 0.0 : (idx >= 2 ? 1.0 : idx / 2.0);
        }
        case 5: { // preset index 0..3 -> [0,1]
            int idx = static_cast<int>(plainValue);
            return idx <= 0 ? 0.0 : (idx >= 3 ? 1.0 : idx / 3.0);
        }
        case 6: return plainValue >= 0.5 ? 1.0 : 0.0; // bypass toggle
        default: return plainValue;
    }
}

Steinberg::Vst::ParamValue PLUGIN_API
SpatialEngineController::getParamNormalized(Steinberg::Vst::ParamID id)
{
    if (id >= static_cast<Steinberg::Vst::ParamID>(kParamCount)) return 0.0;
    return norm_values_[id];
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::setParamNormalized(Steinberg::Vst::ParamID id,
                                             Steinberg::Vst::ParamValue value)
{
    if (id >= static_cast<Steinberg::Vst::ParamID>(kParamCount))
        return Steinberg::kInvalidArgument;
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    norm_values_[id] = value;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::setComponentHandler(Steinberg::Vst::IComponentHandler* handler)
{
    comp_handler_ = handler;
    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API
SpatialEngineController::createView(Steinberg::FIDString /*name*/)
{
    // No editor view in Phase C (Phase D6 deferral).
    return nullptr;
}

// ---------------------------------------------------------------------------
// IConnectionPoint (AM-R3-10: notify returns kNotImplemented)
// ---------------------------------------------------------------------------

Steinberg::tresult PLUGIN_API
SpatialEngineController::connect(Steinberg::Vst::IConnectionPoint* other)
{
    peer_ = other; // no addRef — host manages lifetime per SDK convention
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::disconnect(Steinberg::Vst::IConnectionPoint* other)
{
    if (peer_ == other) peer_ = nullptr;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API
SpatialEngineController::notify(Steinberg::Vst::IMessage* /*message*/)
{
    // IConnectionPoint::notify channel: drain the performEdit ring so that
    // any UDP-thread pushes are flushed to the DAW on the message thread.
    // This is safe: the VST3 SDK guarantees notify() is called from the
    // message/UI thread context.
    drainParamEdits();
    // The IConnectionPoint message channel itself is not used (AM-R3-10 decision).
    return Steinberg::kNotImplemented;
}

// ---------------------------------------------------------------------------
// S2.6: performEdit marshaling (strategy a — message-thread queue)
// ---------------------------------------------------------------------------

bool SpatialEngineController::pushParamEdit(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) noexcept
{
    return param_edit_ring_.push(ParamEdit{id, value});
}

int SpatialEngineController::drainParamEdits() noexcept
{
    if (!comp_handler_) return 0;
    int dispatched = 0;
    ParamEdit entry;
    while (param_edit_ring_.pop(entry)) {
        comp_handler_->beginEdit(entry.id);
        comp_handler_->performEdit(entry.id, entry.value);
        comp_handler_->endEdit(entry.id);
        ++dispatched;
    }
    return dispatched;
}

} // namespace spe::vst3
