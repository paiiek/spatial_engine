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

} // namespace spe::ipc
