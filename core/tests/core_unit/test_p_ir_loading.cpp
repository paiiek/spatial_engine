// test_p_ir_loading.cpp — B3: IRConvReverb WAV loading tests
#include "reverb/IRConvReverb.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers: write minimal WAV files to a temp path
// ---------------------------------------------------------------------------

static std::string tmp_path(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static bool write_float32_wav(const std::string& path, const float* samples,
                               int n, uint32_t sr) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t data_size = static_cast<uint32_t>(n) * 4;
    uint32_t fmt_size  = 18;
    uint32_t riff_size = 4 + (8 + fmt_size) + (8 + data_size);

    auto w2 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto w4 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  w4(riff_size);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  w4(fmt_size);
    w2(3);          // IEEE float
    w2(1);          // mono
    w4(sr);         // sample rate
    w4(sr * 4);     // byte rate
    w2(4);          // block align
    w2(32);         // bits/sample
    w2(0);          // extra size
    std::fwrite("data", 1, 4, f);  w4(data_size);
    std::fwrite(samples, 4, static_cast<size_t>(n), f);
    std::fclose(f);
    return true;
}

static bool write_pcm16_wav(const std::string& path, const int16_t* samples,
                             int n, uint32_t sr) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t data_size = static_cast<uint32_t>(n) * 2;
    uint32_t fmt_size  = 16;
    uint32_t riff_size = 4 + (8 + fmt_size) + (8 + data_size);

    auto w2 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto w4 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };

    std::fwrite("RIFF", 1, 4, f);  w4(riff_size);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  w4(fmt_size);
    w2(1);          // PCM
    w2(1);          // mono
    w4(sr);
    w4(sr * 2);
    w2(2);
    w2(16);
    std::fwrite("data", 1, 4, f);  w4(data_size);
    std::fwrite(samples, 2, static_cast<size_t>(n), f);
    std::fclose(f);
    return true;
}

static spe::reverb::ReverbConfig make_cfg() {
    spe::reverb::ReverbConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.blockSize  = 64;
    cfg.wetMix     = 1.0f;
    return cfg;
}

// ---------------------------------------------------------------------------
// Test 1: load float32 WAV → process impulse → output non-trivial
// ---------------------------------------------------------------------------
static void test_ir_load_synthetic_float32() {
    // Build a simple IR: direct impulse at sample 0
    const int IR_LEN = 512;
    std::vector<float> ir(IR_LEN, 0.f);
    ir[0] = 1.0f;

    std::string path = tmp_path("spe_test_ir_f32.wav");
    assert(write_float32_wav(path, ir.data(), IR_LEN, 48000));

    spe::reverb::IRConvReverb reverb(make_cfg());
    reverb.prepareToPlay(48000, 64);
    assert(reverb.loadIRFromWav(path));

    // Feed a block with impulse at position 0
    std::vector<float> buf(64, 0.f);
    buf[0] = 1.0f;
    reverb.process(buf.data(), 64);

    // With unit IR[0]=1 and wet=1, output[0] should be ~1.0
    assert(std::fabs(buf[0]) > 0.5f);

    std::remove(path.c_str());
    std::printf("PASS test_ir_load_synthetic_float32\n");
}

// ---------------------------------------------------------------------------
// Test 2: load PCM-16 WAV
// ---------------------------------------------------------------------------
static void test_ir_load_synthetic_pcm16() {
    const int IR_LEN = 256;
    std::vector<int16_t> ir(IR_LEN, 0);
    ir[0] = 32767;  // direct impulse

    std::string path = tmp_path("spe_test_ir_pcm16.wav");
    assert(write_pcm16_wav(path, ir.data(), IR_LEN, 48000));

    spe::reverb::IRConvReverb reverb(make_cfg());
    reverb.prepareToPlay(48000, 64);
    assert(reverb.loadIRFromWav(path));

    std::remove(path.c_str());
    std::printf("PASS test_ir_load_synthetic_pcm16\n");
}

// ---------------------------------------------------------------------------
// Test 3: invalid path → false
// ---------------------------------------------------------------------------
static void test_ir_load_invalid_path() {
    spe::reverb::IRConvReverb reverb(make_cfg());
    reverb.prepareToPlay(48000, 64);
    assert(!reverb.loadIRFromWav("/tmp/spe_nonexistent_ir_xyz.wav"));
    std::printf("PASS test_ir_load_invalid_path\n");
}

// ---------------------------------------------------------------------------
// Test 4: wrong sample rate (44100) → false
// ---------------------------------------------------------------------------
static void test_ir_load_wrong_samplerate() {
    const int IR_LEN = 128;
    std::vector<float> ir(IR_LEN, 0.f);
    ir[0] = 1.0f;

    std::string path = tmp_path("spe_test_ir_44100.wav");
    assert(write_float32_wav(path, ir.data(), IR_LEN, 44100));  // 44100 Hz

    spe::reverb::IRConvReverb reverb(make_cfg());
    reverb.prepareToPlay(48000, 64);
    assert(!reverb.loadIRFromWav(path));  // must reject

    std::remove(path.c_str());
    std::printf("PASS test_ir_load_wrong_samplerate\n");
}

// ---------------------------------------------------------------------------
int main() {
    test_ir_load_synthetic_float32();
    test_ir_load_synthetic_pcm16();
    test_ir_load_invalid_path();
    test_ir_load_wrong_samplerate();
    std::printf("All B3 IR loading tests passed.\n");
    return 0;
}
