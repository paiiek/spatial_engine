// core/src/reverb/IRConvReverb.cpp

#include "reverb/IRConvReverb.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>

namespace spe::reverb {

static constexpr int kDefaultIRLen = 1024;

IRConvReverb::IRConvReverb(const ReverbConfig& cfg)
    : wet_mix_(cfg.wetMix)
    , block_size_(cfg.blockSize)
    , sample_rate_(cfg.sampleRate)
{}

void IRConvReverb::buildDefaultIR() {
    default_ir_.resize(kDefaultIRLen);
    default_ir_[0] = 1.0f;
    for (int i = 1; i < kDefaultIRLen; ++i) {
        default_ir_[i] = 0.3f * std::exp(-static_cast<float>(i) / 200.f);
    }
}

void IRConvReverb::applyIR(const float* ir, int len) {
    ola_.prepare(ir, len, block_size_);
    tmp_buf_.assign(static_cast<size_t>(block_size_), 0.f);
}

void IRConvReverb::prepareToPlay(double sampleRate, int blockSize) {
    sample_rate_ = sampleRate;
    block_size_  = blockSize;
    buildDefaultIR();
    applyIR(default_ir_.data(), static_cast<int>(default_ir_.size()));
}

void IRConvReverb::process(float* inOut, int numSamples) noexcept {
    if (!ola_.isReady()) return;
    // OlaConvolver requires exactly block_size_ samples per call.
    // In normal use numSamples == block_size_; guard against mismatch.
    if (numSamples > block_size_) numSamples = block_size_;
    ola_.process(inOut, numSamples, tmp_buf_.data());
    if (wet_mix_ >= 1.f) {
        std::memcpy(inOut, tmp_buf_.data(), static_cast<size_t>(numSamples) * sizeof(float));
    } else {
        const float dry = 1.f - wet_mix_;
        for (int i = 0; i < numSamples; ++i) {
            inOut[i] = inOut[i] * dry + tmp_buf_[static_cast<size_t>(i)] * wet_mix_;
        }
    }
}

void IRConvReverb::loadIR(const float* ir, int len) {
    if (!ir || len < 1) return;
    applyIR(ir, len);
}

bool IRConvReverb::loadIRFromWav(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    auto readU16 = [&](uint16_t& v) -> bool {
        return std::fread(&v, 2, 1, f) == 1;
    };
    auto readU32 = [&](uint32_t& v) -> bool {
        return std::fread(&v, 4, 1, f) == 1;
    };
    auto readTag = [&](char tag[4]) -> bool {
        return std::fread(tag, 1, 4, f) == 4;
    };

    // RIFF header
    char riff[4]; uint32_t riff_size; char wave[4];
    if (!readTag(riff) || std::memcmp(riff, "RIFF", 4) != 0) { std::fclose(f); return false; }
    if (!readU32(riff_size)) { std::fclose(f); return false; }
    if (!readTag(wave) || std::memcmp(wave, "WAVE", 4) != 0) { std::fclose(f); return false; }

    uint16_t audio_format = 0, channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
    bool found_fmt = false, found_data = false;
    std::vector<float> ir_buf;

    while (true) {
        char chunk_id[4]; uint32_t chunk_size;
        if (!readTag(chunk_id) || !readU32(chunk_size)) break;

        if (std::memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) { std::fclose(f); return false; }
            if (!readU16(audio_format)) { std::fclose(f); return false; }
            if (!readU16(channels))     { std::fclose(f); return false; }
            if (!readU32(sample_rate))  { std::fclose(f); return false; }
            uint32_t byte_rate; uint16_t block_align;
            if (!readU32(byte_rate) || !readU16(block_align)) { std::fclose(f); return false; }
            if (!readU16(bits_per_sample)) { std::fclose(f); return false; }
            // skip rest of fmt chunk
            long extra = static_cast<long>(chunk_size) - 16;
            if (extra > 0) std::fseek(f, extra, SEEK_CUR);
            found_fmt = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            if (!found_fmt) { std::fclose(f); return false; }
            // Validate format
            if (channels != 1)        { std::fclose(f); return false; }  // mono only
            if (sample_rate != 48000) { std::fclose(f); return false; }  // 48kHz only
            if (audio_format != 1 && audio_format != 3) { std::fclose(f); return false; }
            if (bits_per_sample != 16 && bits_per_sample != 32) { std::fclose(f); return false; }

            uint32_t bytes_per_sample = bits_per_sample / 8;
            uint32_t num_samples = chunk_size / bytes_per_sample;
            static constexpr uint32_t kMaxIRSamples = 48000;
            if (num_samples > kMaxIRSamples) num_samples = kMaxIRSamples;

            ir_buf.resize(num_samples);
            if (audio_format == 3 && bits_per_sample == 32) {
                // IEEE float32
                if (std::fread(ir_buf.data(), 4, num_samples, f) != num_samples) {
                    std::fclose(f); return false;
                }
            } else if (audio_format == 1 && bits_per_sample == 16) {
                // PCM int16
                for (uint32_t i = 0; i < num_samples; ++i) {
                    int16_t s;
                    if (std::fread(&s, 2, 1, f) != 1) { std::fclose(f); return false; }
                    ir_buf[i] = static_cast<float>(s) / 32768.f;
                }
            } else {
                std::fclose(f); return false;
            }
            found_data = true;
            break;
        } else {
            // skip unknown chunk
            if (chunk_size > 0) std::fseek(f, static_cast<long>(chunk_size), SEEK_CUR);
        }
    }

    std::fclose(f);
    if (!found_data || ir_buf.empty()) return false;
    applyIR(ir_buf.data(), static_cast<int>(ir_buf.size()));
    return true;
}

} // namespace spe::reverb
