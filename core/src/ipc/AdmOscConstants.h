// core/src/ipc/AdmOscConstants.h
// Single source of truth for ADM-OSC v1.0 numeric constants.
// ADR 0006: MAX_DIST=20.0f is a v0 contract — do NOT change without ADR amendment.

#pragma once

namespace spe::ipc {

// ADM-OSC v1.0 normalised distance → metres conversion factor.
// Spec: dist ∈ [0..1] where 1.0 = far. We map dist_normalised * ADM_OSC_MAX_DIST = dist_metres.
// v0 contract: 20.0 metres = maximum scene radius.
inline constexpr float ADM_OSC_MAX_DIST = 20.0f;

// Encoder and decoder MUST both use this constant (enforced by static_assert in each TU).
// Verify: static_assert(spe::ipc::ADM_OSC_MAX_DIST == 20.0f, "ADM_OSC_MAX_DIST mismatch");

// v1.0 Phase 3.2 — ADM-OSC Cartesian half-span. ADM normalised x,y,z ∈ [-1,1]
// map to metres via *_HALF_SPAN (matches the reference's cartesianHalfSpanMeters).
// ADM Cartesian frame is x=right, y=front, z=up (reference AdmOscProtocol +
// metersFromAppPolarDegrees); the engine frame is x=right, y=up, z=front, so the
// xyz->engine conversion swaps Y<->Z (az=atan2(x,y), el=asin(z/r)).
inline constexpr float ADM_OSC_CARTESIAN_HALF_SPAN = 5.0f;

} // namespace spe::ipc
