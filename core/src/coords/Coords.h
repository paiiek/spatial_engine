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

// ── Phase 2 (Binaural / Headtracking): head-rotation of an engine direction ──
// Rotate an engine-frame source direction (az, el) by the listener's head
// orientation (yaw, pitch, roll in radians) and return the head-relative
// (az', el'). Applied at the binaural lookup boundary (before setDirection), so
// turning the head re-points every source against the HRTF set.
//
// Engine Cartesian frame: x = right, y = up, z = front; az = atan2(x, z) so
// RIGHT = +az (same convention as the rest of this file). Direction vector:
//   d = (cosEl*sinAz, sinEl, cosEl*cosAz).
// Composition (Tait-Bryan): d' = Ry(yaw) · Rx(pitch) · Rz(roll) · d, then
//   az' = atan2(d'.x, d'.z), el' = asin(clamp(d'.y, -1, 1)).
//
// ★ L/R SIGN LOCK (P0 — same bug class as the 2026-03-01 stereo_pan and the
// Phase 3.1 −az inversions): yaw uses the standard right-handed Ry so that a
// yaw-only rotation reduces to the ADDITIVE form  az' = az + yaw  (el' = el).
// Net behaviour: headYaw +30° on a front source (az=0) → az' = +30° → R-louder
// (matches stereo_pan_from_pipeline_az(+30°) > 0 above). If a golden ever fails,
// flip ONLY the yaw sign here (the single authority) — NEVER touch downstream az.
inline std::pair<float, float> rotate_engine_dir_by_head(float az_rad, float el_rad,
                                                          float yaw_rad, float pitch_rad,
                                                          float roll_rad) {
    const float ce = std::cos(el_rad);
    const float x = ce * std::sin(az_rad);   // right
    const float y = std::sin(el_rad);        // up
    const float z = ce * std::cos(az_rad);   // front

    // Rz(roll) about +Z (front)
    const float cr = std::cos(roll_rad), sr = std::sin(roll_rad);
    const float x1 = x * cr - y * sr;
    const float y1 = x * sr + y * cr;
    const float z1 = z;

    // Rx(pitch) about +X (right)
    const float cp = std::cos(pitch_rad), sp = std::sin(pitch_rad);
    const float x2 = x1;
    const float y2 = y1 * cp - z1 * sp;
    const float z2 = y1 * sp + z1 * cp;

    // Ry(yaw) about +Y (up) — standard RH so az' = az + yaw (LOCKED).
    const float cy = std::cos(yaw_rad), sy = std::sin(yaw_rad);
    const float x3 = x2 * cy + z2 * sy;
    const float y3 = y2;
    const float z3 = -x2 * sy + z2 * cy;

    const float az_out = std::atan2(x3, z3);
    const float el_out = std::asin(std::fmax(-1.0f, std::fmin(1.0f, y3)));
    return {az_out, el_out};
}

}  // namespace spe::coords
