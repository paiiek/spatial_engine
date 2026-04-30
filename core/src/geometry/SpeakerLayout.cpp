// core/src/geometry/SpeakerLayout.cpp

#include "geometry/SpeakerLayout.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace spe::geometry {

int SpeakerLayout::dimensionality() const {
    // Count distinct rounded-to-cm elevation values as dimensionality proxy.
    std::set<int> elevations;
    for (const auto& s : speakers) {
        // Round y to nearest centimetre.
        elevations.insert(static_cast<int>(std::round(s.y * 100.0f)));
    }
    return static_cast<int>(elevations.size());
}

float SpeakerLayout::max_spacing_m() const {
    float max_d = 0.0f;
    const auto n = speakers.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            float dx = speakers[i].x - speakers[j].x;
            float dy = speakers[i].y - speakers[j].y;
            float dz = speakers[i].z - speakers[j].z;
            float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > max_d) max_d = d;
        }
    }
    return max_d;
}

int SpeakerLayout::min_speaker_count() const {
    switch (regularity) {
        case Regularity::LINEAR:      return 2;
        case Regularity::CIRCULAR:    return 3;
        case Regularity::PLANAR_GRID: return 4;
        case Regularity::IRREGULAR:   return 1;
    }
    return 1;
}

}  // namespace spe::geometry
