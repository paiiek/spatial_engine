// core/src/dsp/DistanceGain.h
// 1/r amplitude rolloff with minimum distance clamp.

#pragma once
#include <algorithm>
#include <cmath>

namespace spe::dsp {

class DistanceGain {
public:
    // ref_dist: distance at which gain = 1.0 (default 1.0 m).
    explicit DistanceGain(float ref_dist = 1.0f) : ref_dist_(ref_dist) {}

    float gainForDistance(float dist_m) const noexcept {
        float d = std::max(dist_m, ref_dist_);
        return ref_dist_ / d;
    }

private:
    float ref_dist_;
};

} // namespace spe::dsp
