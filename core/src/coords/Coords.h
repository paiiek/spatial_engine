// core/src/coords/Coords.h — SINGLE sign-flip authority for all coordinate frames.
// All frame conversions live here and nowhere else.
//
// Frames in v0:
//   Pipeline / vid2spatial-native:
//     (az, el) in radians; az = atan2(x_listener, z_listener); RIGHT = +az.
//     el = arcsin(-y_image_normalized); UP = +el. Listener frame.
//   AmbiX / SOFA:
//     (az, el) in radians; LEFT = +az; UP = +el.
//     Conversion: pipeline_to_ambix(az, el) = (-az, el).
//   VBAP layout-frame (engine):
//     Cartesian XYZ in YAML; speaker (az_deg, el_deg) with RIGHT = +az,
//     az measured from front in degrees.
//   Image-y-down:
//     Pixel y grows downward; el = arcsin(-y_image_normalized).

#pragma once

#include <array>
#include <cmath>
#include <tuple>
#include <utility>

namespace spe::coords {

// Pipeline <-> AmbiX/SOFA.
// AmbiX uses LEFT = +az; pipeline uses RIGHT = +az. Only az flips.

inline std::pair<float, float> pipeline_to_ambix(float az_pipe, float el_pipe) {
    return {-az_pipe, el_pipe};
}

inline std::pair<float, float> ambix_to_pipeline(float az_ambix, float el_ambix) {
    return {-az_ambix, el_ambix};
}

// Cartesian (listener frame, y=up) -> pipeline (az, el, dist).
// az = atan2(x, z)  — right of listener = +x = +az.
// el = atan2(y, sqrt(x²+z²)).
// dist = ||(x,y,z)||.

inline std::tuple<float, float, float> cartesian_to_pipeline(float x, float y, float z) {
    const float dist = std::sqrt(x * x + y * y + z * z);
    const float az   = std::atan2(x, z);
    const float el   = std::atan2(y, std::sqrt(x * x + z * z));
    return {az, el, dist};
}

// Image y-down convention -> listener elevation.
// Image y grows downward, so objects below horizon have y > 0.
// el = arcsin(-y_image_normalized) makes below-horizon negative.

inline float image_y_to_listener_el(float y_image_normalized) {
    return std::asin(-y_image_normalized);
}

// YAML speaker spherical (az_deg from front, RIGHT = +az; el_deg UP = +el)
// -> Cartesian XYZ (x=right, y=up, z=front).
// x = dist * cos(el) * sin(az)
// y = dist * sin(el)
// z = dist * cos(el) * cos(az)

inline std::array<float, 3> yaml_speaker_to_cartesian(float az_deg, float el_deg,
                                                        float dist_m = 1.0f) {
    const float az = az_deg * (static_cast<float>(M_PI) / 180.0f);
    const float el = el_deg * (static_cast<float>(M_PI) / 180.0f);
    return {
        dist_m * std::cos(el) * std::sin(az),
        dist_m * std::sin(el),
        dist_m * std::cos(el) * std::cos(az),
    };
}

// Stereo panning from pipeline az.
// pan = sin(az_pipe): RIGHT (+az) -> sin > 0 -> R louder. Correct per listener frame.
// WHY: NEVER sin(-az). The 2026-03-01 inversion bug was sin(-az). Lock this in.

inline float stereo_pan_from_pipeline_az(float az_pipe) {
    return std::sin(az_pipe);
}

}  // namespace spe::coords
