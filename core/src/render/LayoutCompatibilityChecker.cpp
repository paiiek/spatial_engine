// core/src/render/LayoutCompatibilityChecker.cpp

#include "render/LayoutCompatibilityChecker.h"
#include <cmath>

namespace spe::render {

void LayoutCompatibilityChecker::register_rule(const std::string& layout_name,
                                                Algorithm algo,
                                                CompatStatus status,
                                                std::string reason) {
    rules_.push_back({layout_name, algo, status, std::move(reason)});
}

CompatResult LayoutCompatibilityChecker::validate(
        const geometry::SpeakerLayout& layout,
        Algorithm algo) const {

    // If a registered rule exists for this (name, algo) pair, use it first.
    for (const auto& rule : rules_) {
        if (rule.layout_name == layout.name && rule.algorithm == algo)
            return {rule.status, rule.reason};
    }

    using geometry::Regularity;
    constexpr float SOUND_C = 343.0f;
    constexpr float F_MAX   = 8000.0f;
    // WFS spatial aliasing limit: c / (2 * f_max) ≈ 0.02144 m
    constexpr float WFS_MAX_SPACING = SOUND_C / (2.0f * F_MAX);

    switch (algo) {
        case Algorithm::VAP:   // VAP shares VBAP's directional component → same layout rule
        case Algorithm::VBAP: {
            const int n = static_cast<int>(layout.speakers.size());
            if (n < 3)
                return {CompatStatus::Incompatible,
                        "VBAP requires at least 3 speakers"};
            // dimensionality: either multiple elevations (3D) OR azimuth span
            // in horizontal plane covers > 180 degrees (practical 2D VBAP).
            // Check unique elevations first.
            bool multi_elev = layout.dimensionality() >= 2;
            // Check azimuth span: find min/max azimuth spread
            float min_az =  1e9f, max_az = -1e9f;
            for (const auto& s : layout.speakers) {
                float az = std::atan2(s.x, s.z);
                if (az < min_az) min_az = az;
                if (az > max_az) max_az = az;
            }
            float az_span = max_az - min_az;
            bool az_ok = az_span > (3.14159265f / 2.f); // > 90 deg span
            if (!multi_elev && !az_ok)
                return {CompatStatus::Incompatible,
                        "VBAP requires 2D layout (dimensionality >= 2 or azimuth span > 90 deg)"};
            return {CompatStatus::Compatible, {}};
        }

        case Algorithm::WFS:
            if (layout.regularity != Regularity::LINEAR &&
                layout.regularity != Regularity::CIRCULAR &&
                layout.regularity != Regularity::PLANAR_GRID)
                return {CompatStatus::Incompatible,
                        "WFS requires LINEAR, CIRCULAR, or PLANAR_GRID layout"};
            if (layout.max_spacing_m() >= WFS_MAX_SPACING)
                return {CompatStatus::Incompatible,
                        "WFS requires max_spacing < c/(2*f_max) = 21.4 mm"};
            return {CompatStatus::Compatible, {}};

        case Algorithm::DBAP:
            if (static_cast<int>(layout.speakers.size()) < 2)
                return {CompatStatus::Incompatible,
                        "DBAP requires at least 2 speakers"};
            return {CompatStatus::Compatible, {}};
    }
    return {CompatStatus::Incompatible, "Unknown algorithm"};
}

}  // namespace spe::render
