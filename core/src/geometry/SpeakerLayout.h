// core/src/geometry/SpeakerLayout.h

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spe::geometry {

enum class Regularity : uint8_t {
    LINEAR      = 0,
    CIRCULAR    = 1,
    PLANAR_GRID = 2,
    IRREGULAR   = 3,
};

struct Speaker {
    int         channel;   // 1-based channel index
    float       x, y, z;  // Cartesian metres, x=right y=up z=front
};

struct SpeakerLayout {
    std::string           name;
    std::string           version;
    std::vector<Speaker>  speakers;
    Regularity            regularity = Regularity::IRREGULAR;

    // Helpers

    // Number of unique elevation values as a rough dimensionality proxy.
    int dimensionality() const;

    // Maximum pairwise distance between speakers (metres).
    float max_spacing_m() const;

    // Minimum speaker count required for the classified regularity.
    // LINEAR>=2, CIRCULAR>=3, PLANAR_GRID>=4, IRREGULAR>=1.
    int min_speaker_count() const;
};

}  // namespace spe::geometry
