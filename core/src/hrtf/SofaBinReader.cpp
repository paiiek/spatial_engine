// core/src/hrtf/SofaBinReader.cpp

#include "hrtf/SofaBinReader.h"

#include <cstring>
#include <fstream>

namespace spe::hrtf {

// Sanity caps to prevent malformed-file integer overflow / runaway allocation.
static constexpr uint32_t kMaxPositions = 200'000u;  // larger than any published HRTF DB
static constexpr uint32_t kMaxIRLength  = 1024u;

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

    // n_receivers must be exactly 2 (binaural: left + right)
    if (hdr.n_receivers != 2u)
        return SpehResult::InvalidMagic;  // reuse closest error; add InvalidFormat if desired

    // Sanity bounds to avoid overflow on 64-bit size arithmetic
    if (hdr.n_positions == 0 || hdr.n_positions > kMaxPositions)
        return SpehResult::TruncatedFile;

    // Validate IR length in supported set {64, 128, 256, 384, 512, 1024}
    bool valid_ir = false;
    for (uint32_t v : {64u, 128u, 256u, 384u, 512u, kMaxIRLength})
        if (hdr.ir_length == v) { valid_ir = true; break; }
    if (!valid_ir)
        return SpehResult::IRLengthUnsupported;

    out.n_positions = hdr.n_positions;
    out.ir_length   = hdr.ir_length;
    out.n_receivers = hdr.n_receivers;
    out.sample_rate = hdr.sample_rate;

    // Use explicit uint64_t arithmetic to prevent uint32 overflow before cast to size_t.
    const auto pos_bytes = static_cast<std::size_t>(
        static_cast<uint64_t>(hdr.n_positions) * 3u);
    const auto ir_bytes  = static_cast<std::size_t>(
        static_cast<uint64_t>(hdr.n_positions) * hdr.n_receivers * hdr.ir_length);

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
