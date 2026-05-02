// core/src/hrtf/SofaBinReader.cpp

#include "hrtf/SofaBinReader.h"

#include <cstring>
#include <fstream>

namespace spe::hrtf {

SpehResult loadSpeh(const std::string& path,
                    float              expected_sr,
                    HrtfTable&         out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return SpehResult::FileNotFound;

    SpehHeader hdr{};
    if (!f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)))
        return SpehResult::TruncatedFile;

    if (std::memcmp(hdr.magic, "SPEH", 4) != 0)
        return SpehResult::InvalidMagic;

    if (expected_sr > 0.f && std::abs(hdr.sample_rate - expected_sr) > 1.f)
        return SpehResult::SampleRateMismatch;

    // Validate IR length in supported set {64, 128, 256, 384, 512, 1024}
    bool valid_ir = false;
    for (uint32_t v : {64u, 128u, 256u, 384u, 512u, 1024u})
        if (hdr.ir_length == v) { valid_ir = true; break; }
    if (!valid_ir)
        return SpehResult::IRLengthUnsupported;

    out.n_positions = hdr.n_positions;
    out.ir_length   = hdr.ir_length;
    out.n_receivers = hdr.n_receivers;
    out.sample_rate = hdr.sample_rate;

    const std::size_t pos_bytes = static_cast<std::size_t>(hdr.n_positions) * 3;
    const std::size_t ir_bytes  = static_cast<std::size_t>(hdr.n_positions) *
                                  hdr.n_receivers * hdr.ir_length;

    out.positions.resize(pos_bytes);
    out.ir_data.resize(ir_bytes);

    if (!f.read(reinterpret_cast<char*>(out.positions.data()),
                static_cast<std::streamsize>(pos_bytes * sizeof(float))))
        return SpehResult::TruncatedFile;

    if (!f.read(reinterpret_cast<char*>(out.ir_data.data()),
                static_cast<std::streamsize>(ir_bytes * sizeof(float))))
        return SpehResult::TruncatedFile;

    return SpehResult::Ok;
}

} // namespace spe::hrtf
