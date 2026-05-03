// core/src/scene/SceneCrossfade.cpp

#include "scene/SceneCrossfade.h"
#include <cmath>

namespace spe::scene {

namespace {
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.f * kPi;

inline float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}
} // namespace

// Shortest-arc lerp on a circular angle (in radians, range −π..π).
float SceneCrossfade::lerpAngle(float a, float b, float t) noexcept {
    float delta = b - a;
    while (delta >  kPi) delta -= kTwoPi;
    while (delta < -kPi) delta += kTwoPi;
    float result = a + delta * t;
    while (result >  kPi) result -= kTwoPi;
    while (result < -kPi) result += kTwoPi;
    return result;
}

void SceneCrossfade::start(const Snapshot& from, const Snapshot& to,
                            float duration_ms, float sample_rate) noexcept
{
    from_ = from;
    to_   = to;
    elapsed_samples_ = 0;
    if (duration_ms <= 0.f || sample_rate <= 0.f) {
        // Snap immediately.
        total_samples_ = 0;
        active_        = false;
        return;
    }
    total_samples_ = static_cast<std::int64_t>(
        static_cast<double>(duration_ms) * 1e-3 * static_cast<double>(sample_rate));
    if (total_samples_ <= 0) total_samples_ = 1;
    active_ = true;
}

bool SceneCrossfade::advance(int num_samples) noexcept {
    if (!active_) return false;
    elapsed_samples_ += num_samples;
    if (elapsed_samples_ >= total_samples_) {
        elapsed_samples_ = total_samples_;
        active_ = false;
    }
    return active_;
}

float SceneCrossfade::progress() const noexcept {
    if (total_samples_ <= 0) return 1.f;
    const double t = static_cast<double>(elapsed_samples_) /
                     static_cast<double>(total_samples_);
    if (t < 0.0) return 0.f;
    if (t > 1.0) return 1.f;
    return static_cast<float>(t);
}

ObjectFrame SceneCrossfade::currentObject(int obj_id) const noexcept {
    if (obj_id < 0 || obj_id >= MAX_OBJECTS) return ObjectFrame{};
    const auto& a = from_.objects[static_cast<size_t>(obj_id)];
    const auto& b = to_.objects[static_cast<size_t>(obj_id)];
    const float t = progress();

    ObjectFrame out;
    // Discrete fields snap at the midpoint to avoid topology blends.
    if (t < 0.5f) {
        out.active    = a.active;
        out.algorithm = a.algorithm;
    } else {
        out.active    = b.active;
        out.algorithm = b.algorithm;
    }
    out.az_rad      = lerpAngle(a.az_rad, b.az_rad, t);
    out.el_rad      = lerpAngle(a.el_rad, b.el_rad, t);
    out.dist_m      = lerp(a.dist_m,      b.dist_m,      t);
    out.gain_db     = lerp(a.gain_db,     b.gain_db,     t);
    out.width_rad   = lerp(a.width_rad,   b.width_rad,   t);
    out.reverb_send = lerp(a.reverb_send, b.reverb_send, t);
    return out;
}

} // namespace spe::scene
