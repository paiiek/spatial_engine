// core/src/scene/SceneCrossfade.h
//
// Time-based crossfade between two scene snapshots.  Used by /scene/load to
// avoid click-on-load when transitioning the per-object state to a freshly
// loaded snapshot.
//
// Numeric scalars (az, el, dist, gain_db, width, reverb_send) are linearly
// interpolated.  Discrete fields (active flag, algorithm) snap at the
// midpoint to avoid blending two unrelated topologies.
//
// The class is plain POD-arithmetic and allocation-free after start(); safe
// to step from the audio thread.

#pragma once
#include "core/Constants.h"
#include <array>
#include <cstdint>

namespace spe::scene {

// Per-object snapshot fields used for interpolation. Mirrors the subset of
// SpatialEngine::ObjCache that scene snapshots care about.
struct ObjectFrame {
    bool  active       = false;
    int   algorithm    = 0;     // ipc::Algorithm cast to int
    float az_rad       = 0.f;
    float el_rad       = 0.f;
    float dist_m       = 1.f;
    float gain_db      = 0.f;   // interpolated linearly in dB
    float width_rad    = 0.f;
    float reverb_send  = 0.f;
};

// v0.9 Lane C: derive from the single canonical cap (core/Constants.h) so the
// scene/cue/crossfade plane bumps in lockstep with spe::MAX_OBJECTS.
inline constexpr int MAX_OBJECTS = spe::MAX_OBJECTS;

struct Snapshot {
    std::array<ObjectFrame, MAX_OBJECTS> objects{};
};

class SceneCrossfade {
public:
    // Begin a fade from `from` to `to` lasting duration_ms milliseconds.
    // duration_ms ≤ 0 → snap immediately to `to` (active() returns false).
    void start(const Snapshot& from, const Snapshot& to,
               float duration_ms, float sample_rate) noexcept;

    // Advance the fade by num_samples. Returns true while the fade is still
    // in progress; false once it has completed (state pinned to `to`).
    bool advance(int num_samples) noexcept;

    bool  active()    const noexcept { return active_; }
    float progress()  const noexcept;            // 0.0 (start) .. 1.0 (end)

    // Interpolated state for one object at the current progress.
    // Out-of-range obj_id returns a default-constructed ObjectFrame.
    ObjectFrame currentObject(int obj_id) const noexcept;

    const Snapshot& fromSnapshot() const noexcept { return from_; }
    const Snapshot& toSnapshot()   const noexcept { return to_;   }

private:
    Snapshot from_{};
    Snapshot to_{};
    std::int64_t total_samples_   = 0;
    std::int64_t elapsed_samples_ = 0;
    bool         active_          = false;

    static float lerpAngle(float a, float b, float t) noexcept;
};

} // namespace spe::scene
