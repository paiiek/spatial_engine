// core/src/util/ClickDetectorFFT.h
//
// Test-build-only spectral spike detector around discontinuity events
// (Critic-MA C8 substance check). Real implementation lands at P3 with
// `juce::dsp::FFT`; this declaration only exists so the build wires up the
// option SPATIAL_ENGINE_RT_ASSERTS and downstream tests can include the
// header.

#pragma once

#include <span>

namespace spe::util {

struct ClickDetectorReport {
    bool spike_detected{false};
    int  spike_bin{-1};
    float peak_db_above_neighbors{0.0f};
};

#if defined(SPE_RT_ASSERTS) && SPE_RT_ASSERTS
ClickDetectorReport detect_click(std::span<const float> mono_block,
                                 float threshold_db = 10.0f);
#else
inline ClickDetectorReport detect_click(std::span<const float> /*mono_block*/,
                                        float /*threshold_db*/ = 10.0f) {
    return {};
}
#endif

}  // namespace spe::util
