// core/src/render/VAPRenderer.h
// Volumetric Amplitude Panning renderer (Dreamscape convergence).
// Wraps the ported iae::computeVolumetricAmplitudePanning kernel
// (render/ported/Vap.h) behind the engine's RenderingAlgorithm interface.
// Speaker geometry is pre-converted to the ported frame (Y<->Z swap) at
// prepareToPlay; per-object source positions are converted per block.
// RT-safe: no allocation in processBlock (VAP uses fixed stack scratch and
// the 3D VBAP triplet path, which is alloc-free).

#pragma once
#include "render/RenderingAlgorithm.h"
#include "render/ported/SpatialMath.h"
#include "dsp/GainRamp.h"
#include "core/Constants.h"
#include <array>

namespace spe::render {

class VAPRenderer : public RenderingAlgorithm {
public:
    void prepareToPlay(const geometry::SpeakerLayout& layout,
                       double sample_rate) override;

    void processBlock(
        std::span<const ObjectState> objects,
        std::span<const float* const> dry_mono,
        float* out,
        int    num_samples) override;

private:
    // Ported-frame speaker geometry, precomputed at prepareToPlay (no per-block
    // conversion of static speaker positions).
    std::array<iae::Vec3, 64> spk_pos_ported_{};
    std::array<iae::Vec3, 64> spk_dir_ported_{};
    iae::Vec3 room_center_ported_{0.f, 0.f, 0.f};

    // VAP shaping params. Defaults match the Dreamscape reference mid-range;
    // session-param wiring (vapInsideRadialCurvePower / vapVolumetricDistance-
    // Exponent / vapWallRadiusMeters) lands in the ADM/session phase.
    float wall_radius_m_   = 0.f;   // <=0 -> kernel auto (mean speaker radius)
    float curve_power_     = 2.0f;  // insideRadialCurvePower (0.15..8)
    float dist_exponent_   = 2.0f;  // volumetricDistanceExponent (0.25..8)

    std::array<std::array<spe::dsp::GainRamp, 64>, spe::MAX_OBJECTS> ramps_;
    geometry::SpeakerLayout layout_;
    double sr_ = 48000.0;
};

} // namespace spe::render
