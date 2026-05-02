// core/src/bin/WavWriter.cpp
#include "WavWriter.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace spe::bin {

WavWriter::WavWriter(int channels, double sample_rate, const std::string& path)
    : channels_(channels), sample_rate_(sample_rate), path_(path) {}

void WavWriter::append(float* const* ch_ptrs, int n_ch, int n_frames) {
    int ch = std::min(n_ch, channels_);
    for (int n = 0; n < n_frames; ++n) {
        for (int c = 0; c < ch; ++c) {
            float s = ch_ptrs[c] ? ch_ptrs[c][n] : 0.f;
            s = std::max(-1.0f, std::min(1.0f, s));
            pcm_.push_back(static_cast<int16_t>(s * 32767.f));
        }
        // Pad missing channels
        for (int c = ch; c < channels_; ++c) pcm_.push_back(0);
    }
}

void WavWriter::write_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(v & 0xFF); b.push_back(v >> 8);
}
void WavWriter::write_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(v & 0xFF); b.push_back((v>>8)&0xFF);
    b.push_back((v>>16)&0xFF); b.push_back(v>>24);
}

bool WavWriter::flush() {
    uint32_t data_bytes = static_cast<uint32_t>(pcm_.size() * 2);
    uint32_t sr = static_cast<uint32_t>(sample_rate_);
    uint16_t ch = static_cast<uint16_t>(channels_);
    uint16_t bps = 16;

    std::vector<uint8_t> hdr;
    hdr.reserve(44);
    // RIFF header
    for (char c : std::string("RIFF")) hdr.push_back(c);
    write_u32(hdr, 36 + data_bytes);
    for (char c : std::string("WAVE")) hdr.push_back(c);
    // fmt chunk
    for (char c : std::string("fmt ")) hdr.push_back(c);
    write_u32(hdr, 16);
    write_u16(hdr, 1);  // PCM
    write_u16(hdr, ch);
    write_u32(hdr, sr);
    write_u32(hdr, sr * ch * bps / 8);
    write_u16(hdr, static_cast<uint16_t>(ch * bps / 8));
    write_u16(hdr, bps);
    // data chunk
    for (char c : std::string("data")) hdr.push_back(c);
    write_u32(hdr, data_bytes);

    std::ofstream f(path_, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    f.write(reinterpret_cast<const char*>(pcm_.data()), data_bytes);
    return f.good();
}

} // namespace spe::bin
