// core/src/bin/WavWriter.h
// Minimal PCM WAV writer. No external dependencies.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spe::bin {

class WavWriter {
public:
    WavWriter(int channels, double sample_rate, const std::string& path);
    void append(float* const* channel_ptrs, int n_channels, int n_frames);
    bool flush(); // write WAV header + data; returns true on success
    const std::string& path() const { return path_; }

private:
    int         channels_;
    double      sample_rate_;
    std::string path_;
    std::vector<int16_t> pcm_; // interleaved

    static void write_u16(std::vector<uint8_t>& b, uint16_t v);
    static void write_u32(std::vector<uint8_t>& b, uint32_t v);
};

} // namespace spe::bin
