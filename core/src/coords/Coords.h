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

// ── Dreamscape Convergence: canonical render-frame adapter ───────────────────
// Two Cartesian frames meet at the port boundary:
//   mmhoa  (engine-native): +X = right, +Y = up,      +Z = front.  az = atan2(x,z)
//   ported (Dreamscape/iae): +X = right, +Y = forward, +Z = up.
// The ported VBAP / VAP / DBAP / WFS kernels consume speaker + source geometry
// in the PORTED frame. The conversion is a Y<->Z swap — its own inverse
// (involution). Per-speaker gains are frame-independent (just per-index weights),
// so ONLY the input geometry is converted; outputs need no conversion.
//
// L/R INVARIANT (locked by test_convergence_coords): mmhoa az>0 (RIGHT) maps to
// ported x>0 (RIGHT). NEVER negate here — same bug class as the 2026-03-01
// stereo_pan inversion. Source and speakers MUST both pass through this adapter
// so VBAP sees one consistent frame.

inline std::array<float, 3> mmhoa_to_ported(float x, float y, float z) {
    return {x, z, y};  // right stays x; up(y) -> z; front(z) -> y
}

inline std::array<float, 3> ported_to_mmhoa(float x, float y, float z) {
    return {x, z, y};  // involution: swapping back
}

// mmhoa (az, el) unit direction -> ported-frame unit vector.
// mmhoa dir: x = cosEl*sinAz (right), y = sinEl (up), z = cosEl*cosAz (front);
// after Y<->Z swap: ported = (cosEl*sinAz, cosEl*cosAz, sinEl).
//   az=0,el=0   -> (0,1,0)  ported +Y front
//   az=+90°,el=0-> (1,0,0)  ported +X right
//   el=+90°     -> (0,0,1)  ported +Z up
inline std::array<float, 3> pipeline_dir_to_ported(float az_rad, float el_rad) {
    const float ce = std::cos(el_rad);
    return {ce * std::sin(az_rad), ce * std::cos(az_rad), std::sin(el_rad)};
}

}  // namespace spe::coords
