// core/src/hrtf/SofaBinReader.h
// Reads the .speh binary HRTF format produced by tools/sofa_to_bin.py.
// No HDF5 dependency; all data is pre-baked float32 arrays.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spe::hrtf {

struct SpehHeader {
    char     magic[4];
    uint32_t n_positions;
    uint32_t ir_length;
    uint32_t n_receivers;  // always 2
    float    sample_rate;
    uint32_t reserved;
};

struct HrtfTable {
    uint32_t n_positions = 0;
    uint32_t ir_length   = 0;
    uint32_t n_receivers = 0;
    float    sample_rate = 48000.f;

    // positions[i] = {az_deg, el_deg, dist_m}
    std::vector<float> positions;  // n_positions * 3

    // ir[pos][recv][sample] stored row-major:
    //   ir_data[pos * n_receivers * ir_length + recv * ir_length + s]
    std::vector<float> ir_data;

    const float* ir(int pos, int recv) const {
        return ir_data.data() +
               static_cast<std::ptrdiff_t>(pos) * n_receivers * ir_length +
               static_cast<std::ptrdiff_t>(recv) * ir_length;
    }
};

enum class SpehResult {
    Ok,
    FileNotFound,
    InvalidMagic,
    TruncatedFile,
    SampleRateMismatch,
    IRLengthUnsupported,
};

SpehResult loadSpeh(const std::string& path,
                    float              expected_sr,
                    HrtfTable&         out);

} // namespace spe::hrtf
