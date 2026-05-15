// core/src/geometry/SpeakerLayout.h

#pragma once

#include <array>
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
    float       delay_ms = 0.f;  // per-speaker time alignment delay
    float       gain_db  = 0.f;  // per-speaker output trim
};

struct SpeakerLayout {
    // Maximum YAML channel number addressable by per-channel handlers
    // (OSC + algorithm fan-out). Covers any realistic count; matches the
    // engine MAX_OBJECTS ceiling.
    static constexpr int kMaxYamlChannel = 64;

    std::string           name;
    std::string           version;
    std::vector<Speaker>  speakers;
    Regularity            regularity = Regularity::IRREGULAR;

    // YAML channel (1-based) → vector index in `speakers` (0-based).
    // -1 = no speaker declares that YAML channel.
    // LayoutLoader populates this in the same pass that fills `speakers`.
    std::array<int16_t, static_cast<size_t>(kMaxYamlChannel) + 1> channel_to_idx_{};

    // Translate a wire/YAML channel number to a vector index in `speakers`.
    // Returns -1 if the channel is out of range or no speaker declares it.
    // Audio-thread safe (no allocation, no exceptions).
    int channelToIndex(int yaml_channel) const noexcept;

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
